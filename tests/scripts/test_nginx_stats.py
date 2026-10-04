#!/usr/bin/env python3
"""docker/nginx.conf routes the numbers of the front page: GET /stats and POST /stats/local (run by ./run_tests.sh --fast and by the CI where docker works).

ants_server answers both on its WebSocket port (docs/NETWORK_PORT.md "Site statistics"); the page's nginx forwards them like /busy, as the one request each takes and nothing else:

  - /stats: a GET without a query (405 and 404 otherwise), two a second for each client address, no body and no upgrade passed on, the answer never cached;
  - /stats/local: a POST without a query (405 and 404 otherwise), never a body (the server is given an empty POST whatever came, a body over 1 KiB is 413), six a minute for each
    client address with a burst of two (the rest are 503 and never reach the server).

The text of the file is checked everywhere (the blocks promise what they should, the zones are their own). Where docker works the blocks are RUN: the real file in front of a stand-in server
(a second nginx that logs what it was sent), so that what reaches the server, and what does not, is seen. A machine without IPv6 (a kernel without it) runs the file without its IPv6 listen line.
"""
import http.client
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
STATS = block_of(CONF, "location = /stats ")
LOCAL = block_of(CONF, "location = /stats/local ")
WS = block_of(CONF, "location = /ws ")
BUSY = block_of(CONF, "location = /busy ")
FORBIDDEN = ("add_header", "expires", "proxy_cache", "proxy_ignore_headers", "alias", "root", "try_files", "rewrite", "sub_filter")


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
        rate = lambda zone: zones[zone][1] / (1.0 if zones[zone][2] == "s" else 60.0)
        self.assertGreaterEqual(rate("ants_stats"), 1 / 60.0)                                # the page may ask now and then
        self.assertLessEqual(rate("ants_stats"), 10.0)                                       # and a flood is not let through
        self.assertEqual(zones["ants_local"][1:], (6, "m"))                                  # six reports a minute for each address ...
        self.assertRegex(STATS, r"limit_req zone=ants_stats burst=\d+ nodelay;")
        self.assertIn("limit_req zone=ants_local burst=2 nodelay;", LOCAL)                   # ... with a burst of two
        for block in (STATS, LOCAL):
            self.assertIn("limit_req_status 503;", block)

    def test_they_have_short_timeouts_and_add_nothing_of_their_own(self):
        for name, block in (("/stats", STATS), ("/stats/local", LOCAL)):
            for directive, limit in (("proxy_connect_timeout", 5), ("proxy_read_timeout", 10), ("proxy_send_timeout", 10)):
                value = re.search(r"%s (\d+)s;" % directive, block)
                self.assertIsNotNone(value, name + " " + directive)
                self.assertLessEqual(int(value.group(1)), limit, name + " " + directive)
            for forbidden in FORBIDDEN:
                self.assertNotRegex(block, r"\b%s\b" % forbidden, name + " " + forbidden)       # (the server's own Cache-Control: no-store is the answer's header)
            self.assertIn("proxy_hide_header X-Content-Type-Options;", block, name)          # (the server-level nosniff line applies once)

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


STUB = """log_format stub '$request_method|$uri|$args|$http_host|cl=$http_content_length|te=$http_transfer_encoding|conn=$http_connection|up=$http_upgrade|len=$request_length';
server {
    listen 4002;
    access_log /dev/stdout stub;
    location = /stats { default_type application/json; add_header Cache-Control "no-store" always; add_header X-Content-Type-Options "nosniff" always; return 200 '{"stub":true}'; }
    location = /stats/local { add_header Cache-Control "no-store" always; add_header X-Content-Type-Options "nosniff" always; return 204; }
    location = /ready { return 200 'ready'; }
}
"""
PAGE = '<!DOCTYPE html><html><head><title>Ants (1998)</title></head><body><p>a stand-in page</p></body></html>\n'


@unittest.skipUnless(shutil.which("docker"), "docker is not installed: the blocks of docker/nginx.conf for /stats and /stats/local were NOT run (tests/scripts/test_nginx_stats.py)")
class TheBlocksRun(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.why = docker_ready()
        cls.front = cls.stub = cls.network = ""
        if cls.why:
            return
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
            f.write(STUB)
        tag = uuid.uuid4().hex[:8]
        cls.network = "ants-stats-net-" + tag
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        cls.port = s.getsockname()[1]
        s.close()
        made = subprocess.run(["docker", "network", "create", cls.network], capture_output=True, text=True)
        if made.returncode != 0:
            cls.why = "the docker network could not be made: " + made.stderr.strip()[:200]
            return
        cls.stub = "ants-stats-stub-" + tag
        ran = subprocess.run(["docker", "run", "-d", "--rm", "--name", cls.stub, "--network", cls.network, "--network-alias", "ants-server", "-v", stub + ":/etc/nginx/conf.d/default.conf:ro", IMAGE],
                             capture_output=True, text=True)
        if ran.returncode != 0:
            cls.why = "the stand-in server did not start: " + ran.stderr.strip()[:200]
            return
        cls.front = "ants-stats-front-" + tag
        ran = subprocess.run(["docker", "run", "-d", "--rm", "--name", cls.front, "--network", cls.network, "-p", "127.0.0.1:%d:80" % cls.port, "-v", conf + ":/etc/nginx/conf.d/default.conf:ro",
                              "-v", html + ":/usr/share/nginx/html:ro", IMAGE], capture_output=True, text=True)
        if ran.returncode != 0:
            cls.why = "the container did not start: " + ran.stderr.strip()[:200]
            return
        for _ in range(100):
            up = subprocess.run(["docker", "exec", cls.stub, "wget", "-q", "-O", "/dev/null", "http://127.0.0.1:4002/ready"], capture_output=True)
            try:
                if up.returncode == 0 and cls.ask("GET", "/lobby.html")[0] == 200:
                    return
            except OSError:
                pass
            time.sleep(0.2)
        cls.why = "nginx did not answer"

    @classmethod
    def tearDownClass(cls):
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

    @classmethod
    def ask(cls, method, path, body=None, headers=None, chunked=False):
        """(status, headers as a list of (name, value), body) of one request to the page's nginx"""
        conn = http.client.HTTPConnection("127.0.0.1", cls.port, timeout=20)
        try:
            conn.request(method, path, body=body, headers=headers or {}, encode_chunked=chunked)
            r = conn.getresponse()
            data = r.read()
            return r.status, [(k.lower(), v) for k, v in r.getheaders()], data
        finally:
            conn.close()

    def stub_requests(self, path):
        """What the stand-in server was sent for `path`, in order: dicts of the logged fields"""
        out = subprocess.run(["docker", "logs", self.stub], capture_output=True, text=True).stdout
        found = []
        for line in out.splitlines():
            parts = line.split("|")
            if len(parts) == 9 and parts[1] == path:
                found.append({"method": parts[0], "args": parts[2], "host": parts[3], "cl": parts[4][3:], "te": parts[5][3:], "conn": parts[6][5:], "up": parts[7][3:], "len": int(parts[8][4:])})
        return found

    def names(self, headers, name):
        return [v for k, v in headers if k == name]

    def test_the_configuration_is_accepted_by_nginx(self):
        done = subprocess.run(["docker", "exec", self.front, "nginx", "-t"], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)

    def test_stats_goes_to_the_server_as_a_plain_get_and_is_limited(self):
        status, headers, body = self.ask("GET", "/stats")
        self.assertEqual((status, body), (200, b'{"stub":true}'))
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])                  # the server's own line, once: nothing caches the numbers
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
        seen = self.stub_requests("/stats")
        self.assertEqual(len(seen), 2)                                                        # the first GET and the one with a bare "?": nothing else reached the server
        for one in seen:
            self.assertEqual((one["method"], one["args"], one["cl"], one["te"], one["up"], one["conn"]), ("GET", "-", "-", "-", "-", "close"))
            self.assertEqual(one["host"], "127.0.0.1")
        # a flood: a request and a burst of ten are let through in all (two were used above, the allowance fills at two a second), the rest is refused here
        codes = [self.ask("GET", "/stats")[0] for _ in range(24)]
        self.assertTrue(8 <= codes.count(200) <= 12, codes)
        self.assertTrue(codes.count(503) >= 12 and set(codes) <= {200, 503}, codes)
        self.assertEqual(len(self.stub_requests("/stats")), 2 + codes.count(200))              # (what was refused never reached the server)

    def test_stats_local_takes_an_empty_post_only_three_at_once_and_never_passes_a_body_on(self):
        for method in ("GET", "HEAD", "PUT", "DELETE", "PATCH", "OPTIONS"):
            self.assertEqual(self.ask(method, "/stats/local", body=b"" if method in ("PUT", "PATCH") else None)[0], 405, method)
        for query in ("/stats/local?x=1", "/stats/local?a=b&c=d"):
            self.assertEqual(self.ask("POST", query, body=b"")[0], 404, query)
        big = self.ask("POST", "/stats/local", body=b"a" * 5000)
        self.assertEqual(big[0], 413)                                                         # a body over 1 KiB is refused before it is read
        self.assertEqual(self.stub_requests("/stats/local"), [])                              # none of these reached the server, and none used up the allowance
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
        seen = self.stub_requests("/stats/local")
        self.assertEqual(len(seen), 3)
        for one in seen:
            self.assertEqual((one["method"], one["args"], one["cl"], one["te"], one["up"], one["conn"]), ("POST", "-", "0", "-", "-", "close"))
            self.assertEqual(one["len"], seen[0]["len"])                                      # (the request that the server was sent is the same size whatever the client sent)
        # the allowance is a burst of two and the report itself: the fourth in a row is refused here and does not reach the server
        self.assertEqual(self.ask("POST", "/stats/local")[0], 503)
        self.assertEqual(self.ask("POST", "/stats/local", body=b"x")[0], 503)
        self.assertEqual(len(self.stub_requests("/stats/local")), 3)
        for method in ("GET", "PUT"):                                                         # (a refused method is refused before the allowance is looked at)
            self.assertEqual(self.ask(method, "/stats/local", body=b"" if method == "PUT" else None)[0], 405)

    def test_the_neighbours_of_the_two_addresses_are_the_game_page_and_nothing_is_passed_on(self):
        for path in ("/stats/", "/stats/local/", "/stats/x", "/statsx", "/stat"):
            status, headers, body = self.ask("GET", path)
            self.assertEqual(status, 200, path)                                               # (the try_files of the site: the game page, as any unknown address)
            self.assertIn(b"a stand-in page", body, path)
        for path in ("/stats/", "/stats/local/", "/stats/x", "/statsx", "/stat"):
            self.assertEqual(self.stub_requests(path), [], path)                             # (nothing was passed on)


if __name__ == "__main__":
    unittest.main()
