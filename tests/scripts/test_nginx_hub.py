#!/usr/bin/env python3
"""docker/nginx.conf routes the chat hub's door, wss://<site>/hub/ws (run by ./run_tests.sh --fast and by the CI; docs/HUB.md "The site's nginx").

The hub (service ants-hub, port 4040) is a small service of its own; the page's nginx passes one address to it and nothing else. The text of the file is checked everywhere:

  - /hub/ws is an exact location, the only one of its name; only a GET without a query goes through (405 and 404), the checks come before anything is passed on;
  - the name of the hub is resolved when a request comes (a variable and Docker's resolver, like /ws), so the page starts and works without the hub; a hub that is not there is a JSON 503
    `hub_off`, never a page of the site;
  - it is an upgrade with a 120 second read timeout (the hub pings every 25 seconds), no buffering, and a limit of open sockets for each client address (429);
  - nothing of the visitor's request is passed on but the headers of the upgrade, Origin, Cookie, Sec-Fetch-Site and the one X-Client-Address that nginx sets (a header of that name from a
    visitor, and any other header, never reaches the hub).

Where an nginx program is installed (nginx, or a docker that can start nginx:alpine) the block is RUN: the real text of the file in front of a real hub in this process, so that the upgrade, the
headers that arrive, the refusals and the answer for a hub that is gone are seen. Skipped, with the reason, where there is no nginx.
"""
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
import test_nginx_stats as stats  # noqa: E402  (the helpers that read the file)

read = stats.read
CONF = stats.strip_comments(read("docker", "nginx.conf"))
HEAD = CONF[:CONF.index("server {")]
HUB = stats.block_of(CONF, "location = /hub/ws ")
OFF = stats.block_of(CONF, "location @hub_off ")
WS = stats.block_of(CONF, "location = /ws ")


class TheText(unittest.TestCase):
    def test_it_is_an_exact_location_and_the_only_one_of_its_name(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual([l for l in locations if "hub" in l].count("= /hub/ws"), 1)
        self.assertEqual(sorted(l for l in locations if "hub" in l),                       # (the pages of the login are tests/scripts/test_nginx_hub_login.py)
                         sorted(["= /hub/ws", "@hub_off", "= /hub/login", "= /hub/callback", "= /hub/me", "= /hub/logout", "= /hub/forget", "@hub_page_off", "@hub_page_limit"]))
        self.assertTrue(HUB and OFF)

    def test_only_a_get_without_a_query_goes_through_and_the_checks_come_first(self):
        self.assertIn("if ($request_method != GET) { return 405; }", HUB)
        self.assertIn("if ($is_args) { return 404; }", HUB)
        for check in ("$request_method", "$is_args", "limit_conn ", "limit_req "):
            self.assertLess(HUB.index(check), HUB.index("proxy_pass "), check)

    def test_the_name_is_resolved_per_request_so_the_site_starts_without_the_hub(self):
        self.assertIn("resolver 127.0.0.11", HUB)
        self.assertRegex(HUB, r"set \$ants_hub ants-hub:4040;")
        self.assertIn("proxy_pass http://$ants_hub;", HUB)
        self.assertIn("error_page 502 504 = @hub_off;", HUB)
        self.assertIn('return 503 \'{"error":"hub_off"', OFF)
        self.assertIn("default_type application/json;", OFF)
        self.assertIn('add_header Cache-Control "no-store" always;', OFF)

    def test_the_port_is_the_one_the_image_listens_on_and_the_service_is_the_one_the_stacks_name(self):
        self.assertIn("EXPOSE 4040", read("Dockerfile.hub"))
        self.assertIn("ANTS_HUB_PORT=4040", read("Dockerfile.hub"))
        for stack in ("docker-compose.stack.yml", "docker-compose.staging.yml"):
            self.assertRegex(read(stack), r"(?m)^  ants-hub:\s*$", stack)

    def test_it_is_an_upgrade_that_lives_as_long_as_the_hubs_pings_do(self):
        for line in ("proxy_http_version 1.1;", "proxy_set_header Upgrade $http_upgrade;", 'proxy_set_header Connection "upgrade";', "proxy_buffering off;"):
            self.assertIn(line, HUB)
        self.assertIn("proxy_read_timeout 120s;", HUB)             # the hub pings every 25 seconds
        self.assertIn("proxy_send_timeout 120s;", HUB)
        self.assertIn("proxy_connect_timeout 3s;", HUB)
        self.assertIn("proxy_read_timeout 3600s;", WS)             # (the game door keeps its hour: the two are not the same)

    def test_open_sockets_are_limited_for_each_address(self):
        zone = re.search(r"limit_conn_zone \$binary_remote_addr zone=ants_hub_conn:(\d+)m;", CONF)
        self.assertIsNotNone(zone)
        self.assertLess(CONF.index(zone.group(0)), CONF.index("server {"))        # http context
        limit = re.search(r"limit_conn ants_hub_conn (\d+);", HUB)
        self.assertIsNotNone(limit)
        self.assertGreaterEqual(int(limit.group(1)), 8)                           # (the hub's own eight for each address must be reachable)
        self.assertLessEqual(int(limit.group(1)), 32)
        self.assertIn("limit_conn_status 429;", HUB)

    def test_new_sockets_are_limited_in_rate_for_each_address(self):
        zone = re.search(r"limit_req_zone \$binary_remote_addr zone=ants_hub_req:(\d+)m rate=(\d+)r/s;", CONF)
        self.assertIsNotNone(zone)
        self.assertLess(CONF.index(zone.group(0)), CONF.index("server {"))        # http context
        self.assertLessEqual(int(zone.group(2)), 10)                              # (each handshake costs the hub a welcome of up to 50 KB)
        self.assertIn("limit_req zone=ants_hub_req burst=10 nodelay;", HUB)
        self.assertIn("limit_req_status 503;", HUB)

    def test_nothing_about_the_visitor_is_written_to_the_access_log(self):
        self.assertIn("access_log off;", HUB)                                     # (the server's default format writes the address, the referrer and the user agent)
        self.assertIn("access_log off;", OFF)                                     # (nginx logs a request with the settings of the place where it ends: after the redirect of error_page that is this one)
        text = read("docker", "nginx.conf")
        comment = text[text.index("# The chat hub's door"):text.index("location = /hub/ws")]
        self.assertIn("access log is off", comment)
        self.assertNotIn("which holds the request line only", comment)

    def test_only_these_headers_go_on(self):
        self.assertIn("proxy_pass_request_headers off;", HUB)
        sent = re.findall(r"proxy_set_header ([A-Za-z-]+) ", HUB)
        self.assertEqual(sorted(sent), sorted(["Upgrade", "Connection", "Host", "Sec-WebSocket-Key", "Sec-WebSocket-Version", "Origin", "Cookie", "Sec-Fetch-Site", "X-Client-Address", "Content-Length"]))
        self.assertIn("proxy_set_header X-Client-Address $remote_addr;", HUB)
        self.assertIn("proxy_set_header Origin $http_origin;", HUB)
        self.assertIn("proxy_pass_request_body off;", HUB)
        for forbidden in ("proxy_cache", "add_header", "rewrite", "alias", "root", "try_files", "sub_filter", "X-Forwarded", "proxy_set_header Authorization"):
            self.assertNotIn(forbidden, HUB, forbidden)

    def test_the_hub_door_is_not_a_second_way_into_the_game(self):
        self.assertNotIn("4002", HUB)
        self.assertNotIn("ants-server", HUB)


def nginx_binary():
    return shutil.which("nginx") or ("/usr/sbin/nginx" if os.path.exists("/usr/sbin/nginx") else "")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


COOKIE = "__Host-ants_hub=" + "a1" * 16


class Recorder:
    """A login layer that remembers the headers of each handshake and believes a header of its own (so that the test can give a socket an identity through nginx)."""
    login = True

    def __init__(self):
        self.seen = []

    async def identify(self, headers):
        from hub import session
        self.seen.append(dict(headers))
        return session.Identity("1001", "alice", False) if headers.get("cookie") == COOKIE else None


@unittest.skipUnless(nginx_binary(), "no nginx program on this machine")
class TheBlockRuns(unittest.TestCase):
    """The real text of the two blocks and the zone, in a small server of their own, in front of a real hub (the name of the hub replaced by this machine's address and the port)."""

    access_logs = {}

    @classmethod
    def setUpClass(cls):
        cls.rig = hub_rig.Rig(sockets_per_address=100)
        cls.rig.__enter__()
        cls.tmp = tempfile.mkdtemp(prefix="ants_hub_nginx.")
        try:
            cls.recorder = Recorder()
            cls.rig.hub.sessions = cls.recorder
            cls.rig.hub.chat.login = True
            cls.front, cls.proc = cls.start_nginx(cls.rig.port, drop=("limit_req zone=ants_hub_req burst=10 nodelay;",))        # (the rate limit is tested apart: these tests open many sockets quickly)
            cls.fast, cls.fast_proc = cls.start_nginx(cls.rig.port, drop=("limit_conn ants_hub_conn 16;",))
            cls.dead, cls.dead_proc = cls.start_nginx(free_port(), drop=("limit_req zone=ants_hub_req burst=10 nodelay;",))
        except BaseException:
            cls.rig.__exit__(None, None, None)
            shutil.rmtree(cls.tmp, ignore_errors=True)
            raise

    @classmethod
    def start_nginx(cls, hub_port, drop=()):
        """nginx with the real text of the zones and the block; `drop` names directives to cut out (the tests of one limit do not want the other in the way)."""
        port = free_port()
        zones = "\n  ".join(re.findall(r"limit_(?:conn|req)_zone \$binary_remote_addr zone=ants_hub_(?:conn|req):[^;]+;", HEAD) + ["map $http_cookie $hub_cookie_session {%s}" % stats.block_of(HEAD, "map $http_cookie $hub_cookie_session ")])
        assert zones.count("limit_") == 2, zones
        block = HUB.replace("resolver 127.0.0.11 valid=10s ipv6=off;", "").replace("set $ants_hub ants-hub:4040;", "set $ants_hub 127.0.0.1:%d;" % hub_port)
        for directive in drop:
            assert directive in block, directive
            block = block.replace(directive, "")
        conf = os.path.join(cls.tmp, "nginx-%d.conf" % port)
        with open(conf, "w", encoding="utf-8") as f:
            f.write("daemon off;\nmaster_process off;\npid %s/n%d.pid;\nerror_log %s/e%d.log;\nevents { worker_connections 256; }\nhttp {\n  access_log %s/a%d.log;\n  client_body_temp_path %s/b%d;\n"
                    "  proxy_temp_path %s/p%d;\n  %s\n  server {\n    listen 127.0.0.1:%d;\n    location = /hub/ws {%s}\n    location @hub_off {%s}\n    location / { return 200 'the page'; }\n  }\n}\n"
                    % (cls.tmp, port, cls.tmp, port, cls.tmp, port, cls.tmp, port, cls.tmp, port, zones, port, block, OFF))
        cls.access_logs[port] = os.path.join(cls.tmp, "a%d.log" % port)
        proc = subprocess.Popen([nginx_binary(), "-c", conf, "-p", cls.tmp], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
                return port, proc
            except OSError:
                if proc.poll() is not None:
                    raise unittest.SkipTest("nginx would not start here: " + proc.stderr.read().decode(errors="replace")[:300])
                time.sleep(0.05)
        raise unittest.SkipTest("nginx did not come up")

    @classmethod
    def tearDownClass(cls):
        for proc in (cls.proc, cls.fast_proc, cls.dead_proc):
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(5)
            except subprocess.TimeoutExpired:
                proc.kill()
            proc.stderr.close()
        cls.rig.__exit__(None, None, None)
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        self.clients = []
        self.recorder.seen.clear()
        self.addCleanup(lambda: [c.close() for c in self.clients])

    def connect(self, port=None, **kw):
        c = hc.Client(port or self.front, **kw)
        self.clients.append(c)
        return c

    def test_the_upgrade_works_and_a_line_goes_through(self):
        a = self.connect(headers={"Cookie": COOKIE})
        self.assertEqual(a.status, "HTTP/1.1 101 Switching Protocols")
        welcome = a.recv_json()
        self.assertEqual(welcome["you"], {"name": "alice", "mod": False})
        b = self.connect()
        self.assertEqual(b.recv_json()["you"], None)
        a.say("through nginx")
        self.assertEqual(next(m for m in b.drain(0.5) if m["t"] == "line")["text"], "through nginx")

    def test_the_hub_gets_the_upgrade_headers_the_visitors_cookie_and_the_address_nginx_saw_and_nothing_else(self):
        c = self.connect(headers={"Cookie": "a=b; ants_admin=v2.canary-admin-cookie; %s; __Host-ants_oauth=%s; z=1" % (COOKIE, "b2" * 16), "X-Client-Address": "9.9.9.9", "X-Forwarded-For": "8.8.8.8", "X-Hub-Test-Login": "1001:mallory", "Authorization": "Bearer secret-token",
                                  "User-Agent": "test-agent", "Accept-Language": "en", "Sec-Fetch-Site": "same-origin", "X-Custom": "1", "Referer": "https://evil.example/"})
        self.assertTrue(c.ok)
        c.recv_json()
        seen = self.recorder.seen[-1]
        self.assertEqual(sorted(seen), sorted(["host", "upgrade", "connection", "sec-websocket-key", "sec-websocket-version", "origin", "cookie", "sec-fetch-site", "x-client-address"]))
        self.assertEqual(seen["x-client-address"], "127.0.0.1")                  # the address of the connection, never the visitor's own header
        self.assertEqual(seen["origin"], hc.SITE)
        self.assertEqual(seen["cookie"], COOKIE)                                   # (the login's cookie, and none of the visitor's others: not the admin page's, not the state of a login)
        self.assertEqual(seen["sec-fetch-site"], "same-origin")
        self.assertEqual(seen["upgrade"].lower(), "websocket")
        self.assertNotIn("secret-token", str(seen))
        self.assertNotIn("canary-admin-cookie", str(seen))

    def test_refusals_of_the_hub_reach_the_visitor(self):
        c = self.connect(origin="https://evil.example")
        self.assertEqual(c.status.split()[1], "403")

    def test_only_a_get_without_a_query(self):
        for request, status in ((b"POST /hub/ws HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", "405"), (b"GET /hub/ws?x=1 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", "404"),
                                (b"PUT /hub/ws HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", "405")):
            with socket.create_connection(("127.0.0.1", self.front), timeout=3) as s:
                s.sendall(request)
                self.assertEqual(hc.read_head(s)[0].split()[1], status)

    def test_a_body_is_not_passed_on(self):
        request, _ = hc.handshake_request(origin=hc.SITE, headers={"Content-Length": "5"})
        with socket.create_connection(("127.0.0.1", self.front), timeout=3) as s:
            s.sendall(request + b"hello")
            head = hc.read_head(s)
            self.assertEqual(head[0].split()[1], "101")                          # (the hub was told no body: it does not see the five bytes)

    def test_a_hub_that_is_not_there_is_a_json_503(self):
        for headers in ({}, {"Cookie": "a=b"}):
            c = self.connect(port=self.dead, headers=headers)
            self.assertEqual(c.status.split()[1], "503")
            self.assertEqual(c.reply_headers["content-type"], "application/json")
            self.assertEqual(c.reply_headers["cache-control"], "no-store")
            body = c.buffer + c.recv_exact(int(c.reply_headers["content-length"]) - len(c.buffer), 2)
            self.assertEqual(body, b'{"error":"hub_off","message":"Chat is not available right now."}')
        with socket.create_connection(("127.0.0.1", self.dead), timeout=3) as s:
            s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")           # (the rest of the site is untouched)
            self.assertEqual(hc.read_head(s)[0].split()[1], "200")

    def test_the_seventeenth_socket_of_an_address_is_429(self):
        open_ones = [self.connect() for _ in range(16)]
        for c in open_ones:
            self.assertTrue(c.ok)
        extra = self.connect()
        self.assertEqual(extra.status.split()[1], "429")
        open_ones[0].close()
        time.sleep(0.3)
        self.assertTrue(self.connect().ok)

    def test_more_than_five_new_sockets_a_second_get_503_and_the_hub_is_not_asked(self):
        before = len(self.rig.hub.chat.peers)
        answers = []
        for _ in range(40):
            c = self.connect(port=self.fast)
            answers.append(c.status.split()[1])
        self.assertEqual(answers[:10], ["101"] * 10)                            # the burst
        self.assertGreaterEqual(answers.count("503"), 20)                      # the rest, in well under a second
        self.assertEqual(set(answers), {"101", "503"})
        self.assertLessEqual(len(self.rig.hub.chat.peers) - before, answers.count("101"))
        for c in self.clients:
            c.close()
        time.sleep(1.2)                                                         # the allowance comes back at five a second
        self.assertEqual(self.connect(port=self.fast).status.split()[1], "101")

    def access_log(self, port):
        try:
            with open(self.access_logs[port], encoding="utf-8") as f:
                return f.read()
        except OSError:
            return ""

    def test_nothing_is_written_to_the_access_log_for_the_door_whether_the_hub_is_there_or_not(self):
        # the stand-in server has a real access log (the default format, which writes the address, the referrer and the browser), so that "off" in the block is what is tested
        for port in (self.front, self.dead):
            with socket.create_connection(("127.0.0.1", port), timeout=3) as s:
                s.sendall(b"GET /ordinary-page HTTP/1.1\r\nHost: x\r\nUser-Agent: canary-agent-77\r\nConnection: close\r\n\r\n")     # a page of the site is logged, which shows that the log works
                hc.read_head(s)
            for _ in range(5):
                c = self.connect(port=port, headers={"User-Agent": "canary-agent-77", "Referer": "https://canary-referrer.example/"})
                self.assertEqual(c.status.split()[1], "101" if port == self.front else "503")
            time.sleep(0.3)
            log = self.access_log(port)
            self.assertIn("/ordinary-page", log, "the stand-in's access log does not work")
            self.assertNotIn("/hub/ws", log, "the door is in the access log (port %s)" % ("front" if port == self.front else "hub down"))
            self.assertNotIn("canary-referrer", log)

    def test_a_quiet_socket_stays_open_through_nginx(self):
        c = self.connect()
        c.recv_json()
        time.sleep(1.0)
        c.send(hc.frame(1, b'{"t":"ping"}'))
        self.assertEqual(c.recv_json()["t"], "online")


if __name__ == "__main__":
    unittest.main()
