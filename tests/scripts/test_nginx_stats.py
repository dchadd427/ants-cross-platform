#!/usr/bin/env python3
"""docker/nginx.conf routes the numbers of the front page: GET /stats and POST /stats/local (run by ./run_tests.sh --fast and by the CI where docker works).

ants_server answers both on its WebSocket port (docs/NETWORK_PORT.md "Site statistics"); the page's nginx forwards them like /busy, as the one request each takes and nothing else:

  - /stats: a GET without a query (405 and 404 otherwise), twenty a second for each client address with a burst of 100, answered from a cache of five seconds (one entry for everybody: the game
    server is asked about once in five seconds however many people look; an old answer is served while the new one is fetched and when the server does not answer);
  - /stats/local: a POST without a query (405 and 404 otherwise), never a body (the server is given an empty POST whatever came, a body over 1 KiB is 413), a request that the browser says is
    cross-site is 403 before it counts against the limit, sixty a minute for each client address with a burst of 20 (the rest are 503 and never reach the server);
  - both: nothing of the visitor's headers goes on (the server is sent Host and Connection, and Content-Length 0 for the POST): no cookie, however big.

The text of the file is checked everywhere (the blocks promise what they should, the zones are their own). Where docker works the blocks are RUN: the real file in front of a stand-in server
(a second nginx that logs what it was sent), so that what reaches the server, and what does not, is seen; the cache's timing has classes of its own (they wait for five seconds, and run side
by side with the others); and where the program is built the real ants_server stands behind the real file (the container shares this machine's network). A machine without IPv6 (a kernel
without it) runs the file without its IPv6 listen line.
"""
import concurrent.futures
import http.client
import json
import os
import re
import shutil
import socket
import subprocess
import tempfile
import time
import unittest
import uuid

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
IMAGE = "nginx:alpine"


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def strip_comments(text):
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def block_of(text, header):
    """The text between the braces of the block that starts with `header {` (nested braces counted); "" when there is none."""
    start = text.find(header)
    if start < 0:
        return ""
    open_at = text.index("{", start)
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1:i]
    raise AssertionError("unbalanced braces after " + header)


CONF = strip_comments(read("docker", "nginx.conf"))
HEAD = CONF[:CONF.index("server {")]                                                          # what is declared outside the server block: the zones
STATS = block_of(CONF, "location = /stats ")
LOCAL = block_of(CONF, "location = /stats/local ")
WS = block_of(CONF, "location = /ws ")
BUSY = block_of(CONF, "location = /busy ")
FORBIDDEN = ("add_header", "expires", "alias", "root", "try_files", "rewrite", "sub_filter")
CACHE_LINES = ("proxy_cache ants_stats_cache;", 'proxy_cache_key "ants-stats";', "proxy_cache_valid 200 5s;", "proxy_ignore_headers Cache-Control;", "proxy_cache_lock on;",
               "proxy_cache_use_stale updating error timeout;")


class TheLocations(unittest.TestCase):
    def test_each_is_an_exact_location_and_these_are_the_only_ones_of_their_name(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual([l for l in locations if "stats" in l], ["= /stats", "= /stats/local"])      # (no prefix or regular-expression location could also catch /stats/x or /statsx)
        self.assertTrue(STATS and LOCAL)

    def test_only_the_one_method_without_a_query_goes_through_and_the_checks_come_first(self):
        self.assertIn("if ($request_method != GET) { return 405; }", STATS)
        self.assertIn("if ($request_method != POST) { return 405; }", LOCAL)
        for name, block in (("/stats", STATS), ("/stats/local", LOCAL)):
            self.assertIn("if ($is_args) { return 404; }", block, name)
            self.assertLess(block.index("$request_method"), block.index("proxy_pass"), name)
            self.assertLess(block.index("$is_args"), block.index("proxy_pass"), name)
            self.assertLess(block.index("limit_req "), block.index("proxy_pass"), name)

    def test_only_return_is_used_inside_an_if(self):
        for name, block in (("/stats", STATS), ("/stats/local", LOCAL)):
            bodies = re.findall(r"\bif\s*\([^)]*\)\s*\{([^}]*)\}", block)
            self.assertGreaterEqual(len(bodies), 2, name)
            for body in bodies:
                self.assertRegex(body.strip(), r"^return \d{3};$", name)                         # ("if" is only safe with a return: nothing else may sit in one)

    def test_a_browser_that_says_cross_site_is_refused_before_it_counts_against_the_limit(self):
        self.assertIn('if ($http_sec_fetch_site = "cross-site") { return 403; }', LOCAL)
        self.assertLess(LOCAL.index("$http_sec_fetch_site"), LOCAL.index("limit_req "))
        self.assertLess(LOCAL.index("$http_sec_fetch_site"), LOCAL.index("proxy_pass"))
        self.assertNotIn("sec_fetch", STATS)                                                    # (the numbers are public: only the reports are refused)

    def test_no_body_and_no_upgrade_is_passed_on_and_the_connection_is_not_kept(self):
        for name, block in (("/stats", STATS), ("/stats/local", LOCAL)):
            self.assertIn("proxy_pass_request_body off;", block, name)
            self.assertIn('proxy_set_header Connection "close";', block, name)
            self.assertNotRegex(block, r"(?i)proxy_set_header\s+Upgrade", name)
            self.assertNotIn("upgrade", block.lower(), name)
        self.assertIn('proxy_set_header Content-Length "";', STATS)                          # a GET has none
        self.assertIn('proxy_set_header Content-Length "0";', LOCAL)                         # the server gets an empty POST, whatever the client sent
        self.assertIn("client_max_body_size 1k;", LOCAL)                                     # (a report has no body at all: a big one is refused before it is read)
        self.assertIn("proxy_set_header Upgrade $http_upgrade;", WS)                         # (the other door does: this is what tells them apart)

    def test_nothing_of_the_visitors_headers_is_passed_on(self):
        for name, block in (("/stats", STATS), ("/stats/local", LOCAL)):
            self.assertIn("proxy_pass_request_headers off;", block, name)
            sent = sorted(h.lower() for h in re.findall(r"proxy_set_header\s+(\S+)", block))
            self.assertEqual(sent, ["connection", "content-length", "host"], name)           # (the whole list of what the server is sent: no Cookie, no address, nothing of the browser)
            self.assertIn("proxy_set_header Host $host;", block, name)                       # (exactly one Host: the door refuses none and two)
        self.assertNotIn("proxy_pass_request_headers", WS)                                   # (the other door needs the visitor's headers: the handshake is made of them)

    def test_they_go_to_the_same_server_and_port_as_the_websocket_door(self):
        door = re.search(r"set \$ants_server ([a-z0-9.-]+):(\d+);", WS)
        self.assertIsNotNone(door)
        for name, block in (("/stats", STATS), ("/stats/local", LOCAL)):
            target = re.search(r"set \$ants_server ([a-z0-9.-]+):(\d+);", block)
            self.assertIsNotNone(target, name)
            self.assertEqual(target.groups(), door.groups(), name)
            self.assertIn("proxy_pass http://$ants_server;", block, name)                    # (a variable: the name is resolved per request, so the page starts without the server)
            self.assertIn("resolver 127.0.0.11", block, name)
        for stack in ("docker-compose.stack.yml", "docker-compose.staging.yml"):
            command = re.search(r'command: \[.*?"--ws-port", "(\d+)"', read(stack))
            self.assertIsNotNone(command, stack)
            self.assertEqual(command.group(1), door.group(2), stack)

    def test_each_has_a_rate_limit_of_its_own_declared_outside_the_server(self):
        zones = dict((m.group(1), (int(m.group(2)), int(m.group(3)), m.group(4))) for m in re.finditer(r"limit_req_zone \$binary_remote_addr zone=(\w+):(\d+)m rate=(\d+)r/([sm]);", CONF))
        self.assertEqual(sorted(zones), ["ants_busy", "ants_local", "ants_stats"])           # (three zones, none shared: a flood of one never uses up another's allowance)
        for zone in zones:
            self.assertLess(CONF.index("zone=%s:" % zone), CONF.index("server {"))           # http context: before the server block
        # behind a reverse proxy every visitor may have the proxy's address: these are limits of the whole site, sized for it (the server's own cap of 120 a minute is the bound that matters)
        self.assertEqual(zones["ants_stats"][1:], (20, "s"))                                 # twenty a second for the numbers (the cache answers them) ...
        self.assertEqual(zones["ants_local"][1:], (60, "m"))                                 # ... sixty reports a minute ...
        self.assertIn("limit_req zone=ants_stats burst=100 nodelay;", STATS)                 # ... with a burst of 100 ...
        self.assertIn("limit_req zone=ants_local burst=20 nodelay;", LOCAL)                  # ... and of 20
        for block in (STATS, LOCAL):
            self.assertIn("limit_req_status 503;", block)

    def test_the_cache_of_the_numbers_is_one_small_short_entry_and_never_holds_a_report(self):
        path = re.search(r"proxy_cache_path (/\S+) keys_zone=(\w+):(\d+)m max_size=(\d+)m inactive=(\d+)m use_temp_path=off;", HEAD)
        self.assertIsNotNone(path)
        zone = path.group(2)
        self.assertEqual(zone, "ants_stats_cache")
        self.assertNotIn(zone, re.findall(r"limit_req_zone [^;]*\bzone=(\w+):", HEAD))        # (a limit zone and a cache zone of one name: nginx refuses the file)
        self.assertEqual((int(path.group(3)), int(path.group(5))), (1, 1))                   # one megabyte of keys, an entry that nobody asked for in a minute goes
        self.assertTrue(1 <= int(path.group(4)) <= 8, path.group(4))                         # a few megabytes at the most (the answer is a hundred bytes)
        for line in CACHE_LINES:
            self.assertEqual(STATS.count(line), 1, line)
        self.assertEqual(len(re.findall(r"\bproxy_cache_valid\b", STATS)), 1)                # only a 200 is kept: an error is never served for five seconds
        self.assertLess(STATS.index("limit_req "), STATS.index("proxy_cache "))              # a request over the limit never reaches the cache
        self.assertNotRegex(LOCAL, r"\bproxy_cache\w*\b")                                    # the answer to a report is never kept
        self.assertNotRegex(LOCAL, r"\bproxy_ignore_headers\b")
        self.assertNotRegex(WS + BUSY, r"\bproxy_cache\w*\b")                                # (the other doors are as they were)

    def test_they_have_short_timeouts_and_add_nothing_of_their_own(self):
        for name, block in (("/stats", STATS), ("/stats/local", LOCAL)):
            for directive, limit in (("proxy_connect_timeout", 5), ("proxy_read_timeout", 10), ("proxy_send_timeout", 10)):
                value = re.search(r"%s (\d+)s;" % directive, block)
                self.assertIsNotNone(value, name + " " + directive)
                self.assertLessEqual(int(value.group(1)), limit, name + " " + directive)
            for forbidden in FORBIDDEN:
                self.assertNotRegex(block, r"\b%s\b" % forbidden, name + " " + forbidden)       # (the server's own Cache-Control: no-store is the answer's header)
            self.assertIn("proxy_hide_header X-Content-Type-Options;", block, name)          # (the server-level nosniff line applies once)
        self.assertEqual(sorted(set(re.findall(r"\bproxy_cache\w*|\bproxy_ignore_headers", STATS))), sorted(set(l.split()[0] for l in CACHE_LINES)))   # (the cache is these six lines and no more)

    def test_the_documents_say_what_the_blocks_do(self):
        notes = read("docs", "NETWORK_PORT.md")
        for needle in ("`GET /stats`", "`POST /stats/local`", '{"now":{"matches":N,"players":M},"online":{"day":D,"total":T},"local":{"day":d,"total":t},"since":"YYYY-MM-DD"}', "site-stats.json",
                       "at most 120 reports count in any 60 seconds", "twenty a second per address with a burst of 100", "answered from a cache of 5 seconds", "sixty a minute per address with a burst of 20",
                       "cross-site", "nothing of the visitor's headers", "a body over 1 KiB is 413"):
            self.assertIn(needle, notes)
        server_page = read("docs", "SERVER.md")
        for needle in ("`GET /stats`", "`POST /stats/local`", "site-stats.json", "at most 120 count a minute"):
            self.assertIn(needle, server_page)

    def test_the_counting_rule_of_the_documents_is_the_one_of_the_code(self):
        header = read("include", "ants_server", "site_stats.hpp")
        ticks = re.search(r"kMinTicks\s*=\s*(\d+)\s*;", header)
        self.assertIsNotNone(ticks)
        self.assertEqual(ticks.group(1), "600")                                              # 30 seconds of play at 20 ticks a second
        for name in (os.path.join("docs", "NETWORK_PORT.md"), os.path.join("docs", "SERVER.md"), os.path.join("docs", "audit", "site_stats_notes.md")):
            self.assertIn("at least 600 ticks (30 seconds of play)", read(name), name)


    def test_they_are_the_addresses_that_the_server_answers(self):
        main = read("src", "ants_server", "main.cpp")
        self.assertIn('ws->add_status("/stats"', main)
        self.assertIn('ws->set_post("/stats/local"', main)
        self.assertTrue(BUSY)                                                                # (the block of /busy is still there: these were added beside it)


def docker_ready():
    if not shutil.which("docker"):
        return "docker is not installed"
    done = subprocess.run(["docker", "info", "--format", "{{.ServerVersion}}"], capture_output=True, text=True)
    if done.returncode != 0:
        return "the docker daemon does not answer"
    if subprocess.run(["docker", "image", "inspect", IMAGE], capture_output=True, text=True).returncode != 0:
        if subprocess.run(["docker", "pull", "--quiet", IMAGE], capture_output=True, text=True).returncode != 0:
            return "the image %s is not here and cannot be pulled" % IMAGE
    return ""


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


# What the stand-in server logs for each request: the method, the path, the query, the Host, the lengths and the headers that a visitor could have (a "-" is a header that was not sent)
LOG_FORMAT = ("log_format stub '$request_method|$uri|$args|$http_host|cl=$http_content_length|te=$http_transfer_encoding|conn=$http_connection|up=$http_upgrade|len=$request_length|st=$status"
              "|ck=$http_cookie|ua=$http_user_agent|az=$http_authorization|xf=$http_x_forwarded_for|rf=$http_referer|og=$http_origin|sf=$http_sec_fetch_site|ae=$http_accept_encoding|xc=$http_x_custom';\n")
LOG_FIELDS = ("method", "path", "args", "host", "cl", "te", "conn", "up", "len", "st", "ck", "ua", "az", "xf", "rf", "og", "sf", "ae", "xc")
STUB = LOG_FORMAT + """server {
    listen 4002;
    access_log /dev/stdout stub;
    location = /stats { default_type application/json; add_header Cache-Control "no-store" always; add_header X-Content-Type-Options "nosniff" always; return 200 '{"stub":true}'; }
    location = /stats/local { add_header Cache-Control "no-store" always; add_header X-Content-Type-Options "nosniff" always; return 204; }
    location = /ready { return 200 'ready'; }
}
"""
# The stand-in of the cache's tests: the answer is a file, and a file "slow" next to it makes the server take three seconds to send it (a rate limit on the way out)
SLOW_STUB = LOG_FORMAT + """server {
    listen 4002;
    access_log /dev/stdout stub;
    location = /stats {
        if (-e /srv/stub/fail) { return 500; }
        alias /srv/stub/stats.json;
        default_type application/json;
        add_header Cache-Control "no-store" always;
        add_header X-Content-Type-Options "nosniff" always;
        set $stub_rate 0;
        if (-e /srv/stub/slow) { set $stub_rate 1000; }
        limit_rate $stub_rate;
    }
    location = /ready { return 200 'ready'; }
}
"""
# nginx runs under a shell, so that a test can stop it and start it again inside its container (a container whose first process ends is gone)
KEEP = ["sh", "-c", "nginx -g 'daemon off;' & while true; do sleep 1; done"]
START_NGINX = "nginx -g 'daemon off;' > /proc/1/fd/1 2>&1"
PAGE = '<!DOCTYPE html><html><head><title>Ants (1998)</title></head><body><p>a stand-in page</p></body></html>\n'


def stub_body(n):
    """The stand-in's numbers number n: 4000 bytes of JSON (the padding is what makes a slow server take three seconds)"""
    text = '{"stub":%d}' % n
    return text + " " * (4000 - len(text) - 1) + "\n"


class Rig:
    """The page's nginx (docker/nginx.conf, published on a free port of this machine) and a stand-in for the game server (the name ants-server, port 4002), each in a container of its own on a
    network of their own. A subclass can set STUB (the stand-in's configuration) and STUB_FILES (the files of its /srv/stub, which the test can change). Both nginx run under a shell: a test
    can stop either and start it again (restart_front forgets the cache and the allowances; the stand-in is "gone" when its nginx is stopped)."""
    STUB = STUB
    STUB_FILES = {}
    why = ""

    @classmethod
    def start_rig(cls):
        """Starts the containers. A machine without docker skips (cls.why says so); a page whose nginx does not come up is an error (a broken file is not a missing docker)."""
        cls.why = docker_ready()
        cls.front = cls.stub = cls.network = ""
        if cls.why:
            return
        try:
            cls.start_containers()
        except BaseException:
            cls.stop_rig()
            raise

    @classmethod
    def start_containers(cls):
        cls.tmp = tempfile.mkdtemp(prefix="ants_stats.")
        html = os.path.join(cls.tmp, "html")
        os.makedirs(html)
        for name in ("lobby.html", "index.html"):
            with open(os.path.join(html, name), "w", encoding="utf-8") as f:
                f.write(PAGE)
        conf = os.path.join(cls.tmp, "default.conf")
        text = read("docker", "nginx.conf")
        if not os.path.exists("/proc/net/if_inet6"):                                         # a kernel without IPv6 cannot listen on [::]: the rest of the file is the one under test
            text = text.replace("listen [::]:80;", "")
        with open(conf, "w", encoding="utf-8") as f:
            f.write(text)
        stub = os.path.join(cls.tmp, "stub.conf")
        with open(stub, "w", encoding="utf-8") as f:
            f.write(cls.STUB)
        cls.stubdir = os.path.join(cls.tmp, "stubdir")
        os.makedirs(cls.stubdir)
        for name, content in cls.STUB_FILES.items():
            with open(os.path.join(cls.stubdir, name), "w", encoding="utf-8") as f:
                f.write(content)
        tag = uuid.uuid4().hex[:8]
        cls.network = "ants-stats-net-" + tag
        cls.port = free_port()
        made = subprocess.run(["docker", "network", "create", cls.network], capture_output=True, text=True)
        if made.returncode != 0:
            cls.why = "the docker network could not be made: " + made.stderr.strip()[:200]
            return
        cls.stub = "ants-stats-stub-" + tag
        ran = subprocess.run(["docker", "run", "-d", "--rm", "--name", cls.stub, "--network", cls.network, "--network-alias", "ants-server", "-v", stub + ":/etc/nginx/conf.d/default.conf:ro",
                              "-v", cls.stubdir + ":/srv/stub:ro", IMAGE] + KEEP, capture_output=True, text=True)
        if ran.returncode != 0:
            cls.why = "the stand-in server did not start: " + ran.stderr.strip()[:200]
            return
        cls.front = "ants-stats-front-" + tag
        ran = subprocess.run(["docker", "run", "-d", "--rm", "--name", cls.front, "--network", cls.network, "-p", "127.0.0.1:%d:80" % cls.port, "-v", conf + ":/etc/nginx/conf.d/default.conf:ro",
                              "-v", html + ":/usr/share/nginx/html:ro", IMAGE] + KEEP, capture_output=True, text=True)
        if ran.returncode != 0:
            raise RuntimeError("the container of the page did not start: " + ran.stderr.strip()[:300])
        for _ in range(100):
            up = subprocess.run(["docker", "exec", cls.stub, "wget", "-q", "-O", "/dev/null", "http://127.0.0.1:4002/ready"], capture_output=True)
            try:
                if up.returncode == 0 and cls.ask("GET", "/lobby.html")[0] == 200:
                    return
            except OSError:
                pass
            time.sleep(0.2)
        logs = subprocess.run(["docker", "logs", cls.front], capture_output=True, text=True)
        raise RuntimeError("nginx of the page did not answer in front of the stand-in server:\n" + (logs.stdout + logs.stderr)[-1500:])

    @classmethod
    def stop_rig(cls):
        for name in (cls.front, cls.stub):
            if name:
                subprocess.run(["docker", "rm", "-f", name], capture_output=True)
        if cls.network:
            subprocess.run(["docker", "network", "rm", cls.network], capture_output=True)
        if getattr(cls, "tmp", None):
            shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        if self.why:
            self.skipTest(self.why)
        self.restart_front()                                                                  # (every test starts with an empty cache and the whole allowance: none depends on another)

    @classmethod
    def ask(cls, method, path, body=None, headers=None, chunked=False):
        """(status, headers as a list of (name, value), body) of one request to the page's nginx"""
        conn = http.client.HTTPConnection("127.0.0.1", cls.port, timeout=30)
        try:
            conn.request(method, path, body=body, headers=headers or {}, encode_chunked=chunked)
            r = conn.getresponse()
            data = r.read()
            return r.status, [(k.lower(), v) for k, v in r.getheaders()], data
        finally:
            conn.close()

    def timed(self, method, path, **kw):
        started = time.monotonic()
        status, headers, body = self.ask(method, path, **kw)
        return status, headers, body, time.monotonic() - started

    def stub_requests(self, path):
        """What the stand-in server was sent for `path`, in order: dicts of the logged fields (the lengths as numbers)"""
        out = subprocess.run(["docker", "logs", self.stub], capture_output=True, text=True).stdout
        found = []
        for line in out.splitlines():
            parts = line.split("|")
            if len(parts) == len(LOG_FIELDS) and parts[1] == path:
                one = dict(zip(LOG_FIELDS, parts))
                for key in ("cl", "te", "conn", "up", "len", "st", "ck", "ua", "az", "xf", "rf", "og", "sf", "ae", "xc"):
                    one[key] = one[key].split("=", 1)[1]
                one["len"] = int(one["len"])
                found.append(one)
        return found

    def names(self, headers, name):
        return [v for k, v in headers if k == name]

    def nginx_answers(self):
        try:
            return self.ask("GET", "/lobby.html")[0] == 200
        except OSError:
            return False

    def restart_front(self):
        """The page's nginx starts again with nothing in its memory: no cached numbers (the files of the cache are removed too) and no allowance used up. The container stays."""
        subprocess.run(["docker", "exec", self.front, "nginx", "-s", "stop"], capture_output=True)
        for _ in range(100):
            if not self.nginx_answers():
                break
            time.sleep(0.05)
        subprocess.run(["docker", "exec", self.front, "sh", "-c", "rm -f /var/cache/nginx/ants_stats/*"], capture_output=True)
        subprocess.run(["docker", "exec", "-d", self.front, "sh", "-c", START_NGINX], capture_output=True)
        for _ in range(100):
            if self.nginx_answers():
                return
            time.sleep(0.05)
        self.fail("nginx did not start again")

    def sleep_until(self, started, seconds):
        time.sleep(max(0.0, started + seconds - time.monotonic()))


@unittest.skipUnless(shutil.which("docker"), "docker is not installed: the blocks of docker/nginx.conf for /stats and /stats/local were NOT run (tests/scripts/test_nginx_stats.py)")
class TheBlocksRun(Rig, unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.start_rig()

    @classmethod
    def tearDownClass(cls):
        cls.stop_rig()

    def test_the_configuration_is_accepted_by_nginx(self):
        done = subprocess.run(["docker", "exec", self.front, "nginx", "-t"], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)

    def test_stats_goes_to_the_server_as_a_plain_get(self):
        before = len(self.stub_requests("/stats"))
        status, headers, body = self.ask("GET", "/stats")
        self.assertEqual((status, body), (200, b'{"stub":true}'))
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])                  # the server's own line, once, whatever the cache does: nothing downstream may keep the numbers
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])          # once: the block hides the server's and keeps the page's
        self.assertEqual(self.names(headers, "cross-origin-opener-policy"), ["same-origin"])
        self.assertEqual(self.names(headers, "cross-origin-embedder-policy"), ["require-corp"])
        self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"))
        for method in ("POST", "PUT", "DELETE", "PATCH", "OPTIONS"):
            self.assertEqual(self.ask(method, "/stats", body=b"" if method in ("POST", "PUT", "PATCH") else None)[0], 405, method)
        self.assertEqual(self.ask("HEAD", "/stats")[0], 405)
        for query in ("/stats?x=1", "/stats?busy", "/stats?x"):
            self.assertEqual(self.ask("GET", query)[0], 404, query)                            # a query is no request for the numbers
        self.assertEqual(self.ask("GET", "/stats?")[0], 200)                                  # (a "?" with nothing behind it carries no query: nginx's $args is empty)
        seen = self.stub_requests("/stats")[before:]
        self.assertEqual(len(seen), 1)                                                        # the first GET: the refusals never reached the server, and the one with a bare "?" was the cache's
        for one in seen:
            self.assertEqual((one["method"], one["args"], one["cl"], one["te"], one["up"], one["conn"], one["st"]), ("GET", "-", "-", "-", "-", "close", "200"))
            self.assertEqual(one["host"], "127.0.0.1")

    def test_stats_local_takes_an_empty_post_only_and_never_passes_a_body_on(self):
        before = len(self.stub_requests("/stats/local"))
        for method in ("GET", "HEAD", "PUT", "DELETE", "PATCH", "OPTIONS"):
            self.assertEqual(self.ask(method, "/stats/local", body=b"" if method in ("PUT", "PATCH") else None)[0], 405, method)
        for query in ("/stats/local?x=1", "/stats/local?a=b&c=d"):
            self.assertEqual(self.ask("POST", query, body=b"")[0], 404, query)
        big = self.ask("POST", "/stats/local", body=b"a" * 5000)
        self.assertEqual(big[0], 413)                                                         # a body over 1 KiB is refused before it is read
        self.assertEqual(self.stub_requests("/stats/local")[before:], [])                     # none of these reached the server, and none used up the allowance
        # the first report: 204, nothing in it, the server's own headers once
        status, headers, body = self.ask("POST", "/stats/local")
        self.assertEqual((status, body), (204, b""))
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(self.names(headers, "cross-origin-opener-policy"), ["same-origin"])
        self.assertEqual(self.names(headers, "cross-origin-embedder-policy"), ["require-corp"])
        # a report with a body (100 bytes, then a chunked one): the server is given an empty POST, the same bytes as for the first
        self.assertEqual(self.ask("POST", "/stats/local", body=b"b" * 100)[0], 204)
        self.assertEqual(self.ask("POST", "/stats/local", body=iter([b"hello", b" world"]), headers={"Transfer-Encoding": "chunked"}, chunked=True)[0], 204)
        seen = self.stub_requests("/stats/local")[before:]
        self.assertEqual(len(seen), 3)
        for one in seen:
            self.assertEqual((one["method"], one["args"], one["cl"], one["te"], one["up"], one["conn"]), ("POST", "-", "0", "-", "-", "close"))
            self.assertEqual(one["len"], seen[0]["len"])                                      # (the request that the server was sent is the same size whatever the client sent)
        for method in ("GET", "PUT"):                                                         # (a refused method is refused before the allowance is looked at)
            self.assertEqual(self.ask(method, "/stats/local", body=b"" if method == "PUT" else None)[0], 405)

    def test_a_browser_that_says_cross_site_is_refused_and_it_costs_the_allowance_nothing(self):
        before = len(self.stub_requests("/stats/local"))
        for _ in range(60):                                                                    # (three times the burst: if these counted, the reports below would be refused)
            self.assertEqual(self.ask("POST", "/stats/local", headers={"Sec-Fetch-Site": "cross-site"})[0], 403)
        self.assertEqual(self.ask("POST", "/stats/local", body=b"x", headers={"Sec-Fetch-Site": "cross-site"})[0], 403)
        self.assertEqual(self.ask("PUT", "/stats/local", headers={"Sec-Fetch-Site": "cross-site"})[0], 405)              # (the method is looked at first)
        self.assertEqual(self.ask("POST", "/stats/local?x=1", headers={"Sec-Fetch-Site": "cross-site"})[0], 404)       # (and the query)
        self.assertEqual(self.stub_requests("/stats/local")[before:], [])                     # none reached the server
        for value in ("same-origin", "same-site", "none", "Cross-Origin", ""):                # what a page of the site says, and anything else: a report
            self.assertEqual(self.ask("POST", "/stats/local", headers={"Sec-Fetch-Site": value})[0], 204, value)
        self.assertEqual(self.ask("POST", "/stats/local")[0], 204)                            # (a script or an old browser says nothing)
        self.assertEqual(len(self.stub_requests("/stats/local")[before:]), 6)
        self.assertEqual(self.ask("GET", "/stats", headers={"Sec-Fetch-Site": "cross-site"})[0], 200)   # the numbers are for everybody

    def test_the_server_is_sent_a_host_and_a_connection_and_nothing_else_of_the_visitor(self):
        visitor = {"Cookie": "session=" + "c" * 8100, "User-Agent": "a-browser/1.0", "Authorization": "Bearer a-secret", "X-Forwarded-For": "203.0.113.9", "Referer": "https://example.org/page",
                   "Origin": "https://example.org", "Accept-Language": "de", "X-Custom": "yes", "Sec-Fetch-Site": "same-origin", "Accept": "*/*", "Pragma": "no-cache", "Cache-Control": "no-cache",
                   "If-None-Match": '"abc"', "Range": "bytes=0-5", "X-Real-IP": "203.0.113.9", "Forwarded": "for=203.0.113.9"}
        before = len(self.stub_requests("/stats"))
        before_post = len(self.stub_requests("/stats/local"))
        self.assertEqual(self.ask("GET", "/stats", headers=visitor)[0], 200)
        get = self.stub_requests("/stats")[before:]
        self.assertEqual(len(get), 1)
        self.assertEqual(self.ask("POST", "/stats/local", headers=visitor)[0], 204)
        post = self.stub_requests("/stats/local")[before_post:]
        self.assertEqual(len(post), 1)
        # the whole request that the server was sent, to the byte: the request line, a Host (the visitor's, without a port), a Connection, and for the POST a Content-Length of 0
        self.assertEqual(get[0]["len"], len("GET /stats HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"))
        self.assertEqual(post[0]["len"], len("POST /stats/local HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"))
        for one in (get[0], post[0]):
            for field in ("ck", "ua", "az", "xf", "rf", "og", "sf", "ae", "xc"):
                self.assertEqual(one[field], "-", field)                                      # (and the ones that could matter, by name)
            self.assertEqual((one["host"], one["conn"], one["up"], one["te"]), ("127.0.0.1", "close", "-", "-"))

    def flood(self, method, path, count, workers=8):
        """(the status of each request, the seconds that all took, the seconds that the slowest took)"""
        def one(_):
            started = time.monotonic()
            return self.ask(method, path)[0], time.monotonic() - started
        started = time.monotonic()
        with concurrent.futures.ThreadPoolExecutor(workers) as pool:
            results = list(pool.map(one, range(count)))
        return [r[0] for r in results], time.monotonic() - started, max(r[1] for r in results)

    def test_stats_lets_a_burst_of_a_hundred_through_at_twenty_a_second_and_refuses_the_rest(self):
        before = len(self.stub_requests("/stats"))
        self.assertEqual(self.ask("GET", "/stats")[0], 200)                                   # (the first one fills the cache: the server sees this one only)
        codes, took, slowest = self.flood("GET", "/stats", 400)
        allowed = codes.count(200) + 1
        self.assertTrue(set(codes) <= {200, 503}, set(codes))
        self.assertGreaterEqual(allowed, 100, codes.count(200))                                # the burst
        self.assertLessEqual(allowed, 101 + 20 * (took + 0.5) + 5, (allowed, took))            # ... and what the rate gives while the flood lasts
        self.assertGreater(codes.count(503), 0)
        self.assertEqual(codes.count(200) + codes.count(503), 400)
        self.assertLess(slowest, 3.0, slowest)                                                 # (what is over the burst is refused at once: it is not queued to be let through at the rate)
        time.sleep(0.4)                                                                        # twenty a second: eight tokens are back in 0.4 s (two a second would give none)
        again = [self.ask("GET", "/stats")[0] for _ in range(3)]
        self.assertGreaterEqual(again.count(200), 2, again)
        self.assertLessEqual(len(self.stub_requests("/stats")) - before, 1 + int(took / 4))    # (the flood is the cache's: the server is asked once, or twice when the flood lasts)

    def test_stats_local_lets_a_burst_of_twenty_through_at_sixty_a_minute_and_refuses_the_rest(self):
        before = len(self.stub_requests("/stats/local"))
        started = time.monotonic()
        codes = [self.ask("POST", "/stats/local")[0] for _ in range(40)]
        took = time.monotonic() - started
        allowed = codes.count(204)
        self.assertTrue(set(codes) <= {204, 503}, set(codes))
        self.assertLess(took, 8.0, took)                                                       # (forty requests in one go: refused at once, not queued to be let through at the rate)
        self.assertTrue(21 <= allowed <= 21 + int(took) + 1, (allowed, took))                  # the first request and the burst of 20, and a token a second while this goes on
        self.assertEqual(codes[:allowed], [204] * allowed)                                     # (the refusals come after the allowance, none between)
        self.assertEqual(len(self.stub_requests("/stats/local")) - before, allowed)            # what was refused never reached the server
        time.sleep(1.2)                                                                        # sixty a minute: a token in a second (six a minute would need ten, ten a second a dozen)
        again = [self.ask("POST", "/stats/local")[0] for _ in range(6)]
        self.assertTrue(1 <= again.count(204) <= 3, again)
        self.assertEqual(len(self.stub_requests("/stats/local")) - before, allowed + again.count(204))

    def test_the_neighbours_of_the_two_addresses_are_the_game_page_and_nothing_is_passed_on(self):
        for path in ("/stats/", "/stats/local/", "/stats/x", "/statsx", "/stat"):
            status, headers, body = self.ask("GET", path)
            self.assertEqual(status, 200, path)                                               # (the try_files of the site: the game page, as any unknown address)
            self.assertIn(b"a stand-in page", body, path)
        for path in ("/stats/", "/stats/local/", "/stats/x", "/statsx", "/stat"):
            self.assertEqual(self.stub_requests(path), [], path)                             # (nothing was passed on)


@unittest.skipUnless(shutil.which("docker"), "docker is not installed: the cache of /stats was NOT run (tests/scripts/test_nginx_stats.py)")
class TheCacheRuns(Rig, unittest.TestCase):
    """The cache of the numbers. Its time is real: five seconds. The stand-in answers from a file, and takes three seconds when the file `slow` is there."""
    STUB = SLOW_STUB
    STUB_FILES = {"stats.json": stub_body(1)}

    @classmethod
    def setUpClass(cls):
        cls.start_rig()

    @classmethod
    def tearDownClass(cls):
        cls.stop_rig()

    def numbers(self, n):
        with open(os.path.join(self.stubdir, "stats.json"), "w", encoding="utf-8") as f:
            f.write(stub_body(n))

    def slow(self, on):
        path = os.path.join(self.stubdir, "slow")
        if on:
            open(path, "w").close()
        elif os.path.exists(path):
            os.remove(path)

    def broken(self, on):
        path = os.path.join(self.stubdir, "fail")
        if on:
            open(path, "w").close()
        elif os.path.exists(path):
            os.remove(path)

    def asked(self):
        return len(self.stub_requests("/stats"))

    def tearDown(self):
        if not self.why:
            self.slow(False)
            self.broken(False)
            self.numbers(1)

    def test_a_second_get_within_five_seconds_is_answered_by_the_cache_and_after_them_the_old_answer_is_served_while_the_new_one_is_fetched(self):
        asked = self.asked()
        status, headers, first = self.ask("GET", "/stats")
        filled = time.monotonic()
        self.assertEqual(status, 200)
        self.assertIn(b'"stub":1', first)
        self.assertEqual(self.asked(), asked + 1)
        self.numbers(2)                                                                        # (the server's numbers change: only a request that reaches it can say so)
        status, again_headers, second = self.ask("GET", "/stats")
        self.assertEqual((status, second), (200, first))                                       # the same answer, and the server was not asked
        self.assertEqual(self.asked(), asked + 1)
        for name in ("cache-control", "x-content-type-options", "content-type", "cross-origin-opener-policy", "cross-origin-embedder-policy"):
            self.assertEqual(self.names(again_headers, name), self.names(headers, name), name)    # (a cached answer says what a fresh one says)
        self.assertEqual(self.names(again_headers, "cache-control"), ["no-store"])             # the server's no-store reaches the browser although the cache keeps the answer
        # a bare "?" is the same address, and a visitor that asks for a fresh answer gets the cached one: neither makes a request to the server
        self.assertEqual(self.ask("GET", "/stats?")[2], first)
        self.assertEqual(self.ask("GET", "/stats", headers={"Cache-Control": "no-cache", "Pragma": "no-cache"})[2], first)
        self.assertEqual(self.asked(), asked + 1)
        self.sleep_until(filled, 2.0)
        self.assertEqual(self.ask("GET", "/stats")[2], first)                                  # still the answer of two seconds ago
        self.assertEqual(self.asked(), asked + 1)
        self.sleep_until(filled, 6.0)                                                          # five seconds are over, and nobody has asked since: the answer is old
        self.broken(True)
        error = self.ask("GET", "/stats")[0]
        self.broken(False)
        self.assertEqual(error, 500)                                                           # a server that answers with an error is not covered by the old numbers (one that does not answer is); the error is not kept
        self.slow(True)
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            fetching = pool.submit(self.timed, "GET", "/stats")                                # this one goes to the (slow) server ...
            time.sleep(0.5)
            old = self.timed("GET", "/stats")                                                  # ... and this one does not wait for it
            fresh = fetching.result()
        self.assertEqual(old[0], 200)
        self.assertIn(b'"stub":1', old[2])                                                     # the answer that it had
        self.assertLess(old[3], 1.2, old[3])
        self.assertEqual(fresh[0], 200)
        self.assertIn(b'"stub":2', fresh[2])                                                   # the one that the server gave: it was asked (once) when the five seconds were over
        self.assertGreater(fresh[3], 2.0, fresh[3])
        self.assertEqual(self.asked(), asked + 3)                                              # the first answer, the error, and the new numbers (once, whatever the visitors were)

    def test_a_crowd_that_comes_at_once_is_one_request_to_the_server(self):
        asked = self.asked()
        self.slow(True)
        started = time.monotonic()
        with concurrent.futures.ThreadPoolExecutor(20) as pool:
            answers = list(pool.map(lambda _: self.ask("GET", "/stats"), range(20)))
        took = time.monotonic() - started
        self.assertGreater(took, 2.0)                                                          # (the stand-in is slow on purpose: a stand-in that was not would prove nothing)
        self.assertEqual({a[0] for a in answers}, {200})
        self.assertEqual(len({a[2] for a in answers}), 1)
        self.assertEqual(self.asked(), asked + 1)                                              # twenty waited for the one

    def stub_up(self):
        return subprocess.run(["docker", "exec", self.stub, "wget", "-q", "-O", "/dev/null", "-T", "2", "http://127.0.0.1:4002/ready"], capture_output=True).returncode == 0

    def wait_stub(self, up):
        for _ in range(100):
            if self.stub_up() == up:
                return
            time.sleep(0.1)
        self.fail("the stand-in did not %s" % ("come back" if up else "stop"))

    def test_when_the_server_does_not_answer_the_old_numbers_are_served_for_a_server_that_takes_the_connection_and_one_that_refuses_it(self):
        status, _, old = self.ask("GET", "/stats")
        filled = time.monotonic()
        self.assertEqual(status, 200)
        self.assertIn(b'"stub":1', old)
        self.sleep_until(filled, 6.0)                                                          # the answer is old now
        # a server that is stopped (its process is frozen: the connection is taken, nothing is said): five seconds of waiting, then the old answer
        self.assertEqual(subprocess.run(["docker", "pause", self.stub], capture_output=True).returncode, 0)
        try:
            status, headers, body, took = self.timed("GET", "/stats")
        finally:
            subprocess.run(["docker", "unpause", self.stub], capture_output=True)
        self.assertEqual((status, body), (200, old))
        self.assertGreater(took, 4.0, took)                                                    # (it waited its read timeout of five seconds: that is what was stopped)
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])
        # a server that is gone (the connection is refused): at once
        subprocess.run(["docker", "exec", self.stub, "nginx", "-s", "stop"], capture_output=True)
        try:
            self.wait_stub(False)
            status, _, body, took = self.timed("GET", "/stats")
            self.assertEqual((status, body), (200, old))
            self.assertLess(took, 4.0, took)                                                   # (at once: not the five seconds of a server that says nothing)
            # nothing old to serve: the error is the visitor's, and it is not kept (the next answer after the server is back is the server's)
            self.restart_front()
            self.assertEqual(self.ask("GET", "/stats")[0], 502)
        finally:
            subprocess.run(["docker", "exec", "-d", self.stub, "sh", "-c", START_NGINX], capture_output=True)
            self.wait_stub(True)
        status, _, body = self.ask("GET", "/stats")
        self.assertEqual(status, 200)
        self.assertIn(b'"stub":', body)


@unittest.skipUnless(shutil.which("docker"), "docker is not installed: the real ants_server behind the page's nginx was NOT run (tests/scripts/test_nginx_stats.py)")
class TheRealServerBehindTheFile(unittest.TestCase):
    """The real ants_server (when it is built) behind the real docker/nginx.conf, in a container that shares this machine's network: what the stand-in cannot say is whether the door
    of the program takes what nginx sends (one Host, no cookie, an empty POST) and counts it."""

    @classmethod
    def setUpClass(cls):
        try:
            cls.start()
        except BaseException:
            cls.tearDownClass()
            raise

    @classmethod
    def start(cls):
        cls.why = docker_ready()
        cls.server = None
        cls.front = ""
        if cls.why:
            return
        binary = os.path.join(REPO, os.environ.get("BUILD_DIR", "build"), "src", "ants_server", "ants_server")
        maps = os.path.join(REPO, "Original-Ants", "Maps")
        if not os.access(binary, os.X_OK) or not os.path.isdir(maps):
            cls.why = "ants_server is not built (%s) or the maps are not here" % binary
            return
        cls.tmp = tempfile.mkdtemp(prefix="ants_stats_real.")
        cls.ws_port = free_port()
        cls.port = free_port()
        cls.log = open(os.path.join(cls.tmp, "server.log"), "w")
        cls.server = subprocess.Popen([binary, "--maps", maps, "--port", str(free_port()), "--ws-port", str(cls.ws_port), "--results-dir", os.path.join(cls.tmp, "results")],
                                      stdout=cls.log, stderr=subprocess.STDOUT)
        for _ in range(100):
            try:
                if cls.direct("GET", "/busy")[0] == 200:
                    break
            except OSError:
                pass
            if cls.server.poll() is not None:
                cls.why = "ants_server stopped at start-up"
                return
            time.sleep(0.1)
        else:
            cls.why = "ants_server did not answer"
            return
        probe = subprocess.run(["docker", "run", "--rm", "--network", "host", "--entrypoint", "wget", IMAGE, "-q", "-O", "/dev/null", "-T", "3", "http://127.0.0.1:%d/busy" % cls.ws_port],
                               capture_output=True)
        if probe.returncode != 0:
            cls.why = "a container on the host network cannot reach this machine's loopback address (Docker Desktop?)"
            return
        html = os.path.join(cls.tmp, "html")
        os.makedirs(html)
        for name in ("lobby.html", "index.html"):
            with open(os.path.join(html, name), "w", encoding="utf-8") as f:
                f.write(PAGE)
        text = read("docker", "nginx.conf")
        door = "set $ants_server ants-server:4002;"
        assert text.count(door) >= 4
        text = text.replace("listen [::]:80;", "").replace("listen 80;", "listen %d;" % cls.port).replace(door, "set $ants_server 127.0.0.1:%d;" % cls.ws_port)
        conf = os.path.join(cls.tmp, "default.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write(text)
        cls.front = "ants-stats-real-" + uuid.uuid4().hex[:8]
        ran = subprocess.run(["docker", "run", "-d", "--rm", "--name", cls.front, "--network", "host", "-v", conf + ":/etc/nginx/conf.d/default.conf:ro", "-v", html + ":/usr/share/nginx/html:ro", IMAGE],
                             capture_output=True, text=True)
        if ran.returncode != 0:
            cls.front = ""
            raise RuntimeError("the container of the page did not start: " + ran.stderr.strip()[:300])
        for _ in range(100):
            try:
                if cls.through("GET", "/lobby.html")[0] == 200:
                    return
            except OSError:
                pass
            time.sleep(0.2)
        logs = subprocess.run(["docker", "logs", cls.front], capture_output=True, text=True)
        raise RuntimeError("nginx of the page did not answer in front of ants_server:\n" + (logs.stdout + logs.stderr)[-1500:])

    @classmethod
    def tearDownClass(cls):
        if cls.front:
            subprocess.run(["docker", "rm", "-f", cls.front], capture_output=True)
        if cls.server is not None and cls.server.poll() is None:
            cls.server.terminate()
            try:
                cls.server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                cls.server.kill()
        if getattr(cls, "log", None):
            cls.log.close()
        if getattr(cls, "tmp", None):
            shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        if self.why:
            self.skipTest(self.why)

    @staticmethod
    def request(port, method, path, headers=None, body=None):
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
        try:
            conn.request(method, path, body=body, headers=headers or {})
            r = conn.getresponse()
            data = r.read()
            return r.status, [(k.lower(), v) for k, v in r.getheaders()], data
        finally:
            conn.close()

    @classmethod
    def direct(cls, method, path, **kw):
        return cls.request(cls.ws_port, method, path, **kw)

    @classmethod
    def through(cls, method, path, **kw):
        return cls.request(cls.port, method, path, **kw)

    def test_the_numbers_and_the_reports_pass_the_file_into_the_real_door_and_are_counted(self):
        status, headers, body = self.through("GET", "/stats")
        filled = time.monotonic()
        self.assertEqual(status, 200)
        numbers = json.loads(body)
        self.assertEqual(sorted(numbers), ["local", "now", "online", "since"])
        self.assertEqual(numbers["local"], {"day": 0, "total": 0})
        self.assertEqual([v for k, v in headers if k == "cache-control"], ["no-store"])
        self.assertEqual([v for k, v in headers if k == "x-content-type-options"], ["nosniff"])
        visitor = {"Cookie": "session=" + "c" * 8100, "User-Agent": "a-browser/1.0", "Referer": "https://example.org/", "Sec-Fetch-Site": "same-origin"}   # (a cookie of 8100 bytes is 431 at the door)
        for headers_sent in ({}, {"Sec-Fetch-Site": "same-origin"}, visitor):
            status, _, body = self.through("POST", "/stats/local", headers=headers_sent)
            self.assertEqual((status, body), (204, b""), headers_sent)
        self.assertEqual(self.through("POST", "/stats/local", headers={"Sec-Fetch-Site": "cross-site"})[0], 403)           # (refused by nginx: not counted)
        self.assertEqual(self.through("GET", "/stats", headers=visitor)[0], 200)
        self.assertEqual(self.direct("POST", "/stats/local", headers={"Cookie": "session=" + "c" * 8100})[0], 431)         # (what the door does with that cookie: the reason for the file's rule)
        counted = json.loads(self.direct("GET", "/stats")[2])
        self.assertEqual(counted["local"], {"day": 3, "total": 3})                             # the server counted the three, at its own door, at once
        self.assertEqual(json.loads(self.through("GET", "/stats")[2])["local"], {"day": 0, "total": 0})                   # the page's nginx shows the numbers of its last look
        time.sleep(max(0.0, filled + 6.0 - time.monotonic()))
        self.assertEqual(json.loads(self.through("GET", "/stats")[2])["local"], {"day": 3, "total": 3})                   # and the new ones after five seconds


if __name__ == "__main__":
    unittest.main()
