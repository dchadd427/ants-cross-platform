#!/usr/bin/env python3
"""docker/nginx.conf routes the hub's pictures, GET /hub/pic/<name>.png (run by ./run_tests.sh --fast and by the CI; docs/HUB.md "Pictures", "The site's nginx").

The picture of an account is kept by the hub under a random name and shown to every visitor of the front page, so the door is open to anyone who has the name. The text of the file is checked where no run
can see the defect (a regular expression that is looser than the name's shape, and a static-image location that comes before it and takes a .png first; the page's content security policy, which
must not name another host), and where an nginx program is installed the block is RUN: the real text in front of a stand-in that records what arrives, and in front of the real hub:

  - only a GET of the exact shape and no query goes through (405, 404; every other shape is not passed on at all); a request of another site (Sec-Fetch-Site cross-site or same-site) is 403;
  - nothing of the visitor's request is passed on but the address that nginx saw: no cookie, no header of the visitor's, no body;
  - the answer is kept for five minutes for everybody (the hub is asked once) and a 404 is not kept; a burst of 240 and then twenty a second for an address (429, and the hub is not asked);
  - the headers of the hub are there once each, with the server's own (COOP, COEP, nosniff, HSTS);
  - a hub that is not there is the JSON 503 of the chat's door.
"""
import http.server
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import fake_discord as fd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
import quick_http  # noqa: E402
import test_nginx_stats as stats  # noqa: E402  (the readers of the file)
from login_flow import Visitor  # noqa: E402

CONF = stats.strip_comments(stats.read("docker", "nginx.conf"))
HEAD = CONF[:CONF.index("server {")]
SERVER = CONF[CONF.index("server {"):]
START = SERVER.index('location ~ "^/hub/pic/')
OPEN = SERVER.index('" {', START) + 2


def block():
    depth = 0
    for i in range(OPEN, len(SERVER)):
        depth += {"{": 1, "}": -1}.get(SERVER[i], 0)
        if depth == 0:
            return SERVER[OPEN + 1:i]
    raise AssertionError("an unclosed block")


PIC = block()
NAME = "0123456789abcdef0123456789abcdef"
BODY = fd.make_png()
SECURITY = ("Cross-Origin-Opener-Policy", "Cross-Origin-Embedder-Policy", "X-Content-Type-Options", "Strict-Transport-Security")


class TheText(unittest.TestCase):
    def test_the_door_is_one_regular_expression_of_the_exact_shape_and_it_comes_before_the_static_images(self):
        # (a run sees what a shape lets through, but not that another regular expression of the file takes the address first: nginx picks the first one that matches, in the order of the file)
        here = 'location ~ "^/hub/pic/[0-9a-f]{32}\\.png\\z" {'
        self.assertEqual(SERVER.count(here), 1)
        self.assertEqual(SERVER.count("/hub/pic"), 1)                                                # (no other place of the file names the folder: nothing else is passed on)
        self.assertEqual(SERVER.index("location ~"), SERVER.index(here))                           # (the first regular expression of the file, `~` or `~*`, is this one)
        self.assertLess(SERVER.index(here), SERVER.index("location ~* \\.(png"))

    def test_the_page_policy_still_lets_a_same_origin_picture_in_and_names_no_other_host_for_it(self):
        # (the pictures are served by this site: the policy of the pages must not grow a host for them; a policy is a setting that has nothing to run here)
        self.assertIn("img-src 'self' data: blob: https://cdn.buymeacoffee.com;", HEAD)
        self.assertNotIn("discord", HEAD.lower().replace("ants_hub", "").replace("hub_cookie", ""))
        self.assertNotIn("cdn.discordapp", CONF)

    def test_the_cache_is_small_short_and_keeps_only_a_picture(self):
        path = re.search(r"proxy_cache_path (/\S+) keys_zone=(\w+):(\d+)m max_size=(\d+)m inactive=(\d+)m use_temp_path=off;", HEAD[HEAD.index("ants_hub_pics") - 80:])
        self.assertEqual(path.group(2), "ants_hub_pics")
        self.assertNotIn(path.group(2), re.findall(r"limit_req_zone [^;]*\bzone=(\w+):", HEAD))
        self.assertEqual(tuple(int(path.group(n)) for n in (3, 4, 5)), (1, 16, 5))
        self.assertEqual(len(re.findall(r"\bproxy_cache_valid\b", PIC)), 1)
        self.assertIn("proxy_cache_valid 200 5m;", PIC)
        for line in ("proxy_cache ants_hub_pics;", 'proxy_cache_key "ants-pic$uri";', "proxy_cache_lock on;", "proxy_ignore_headers Cache-Control;"):
            self.assertIn(line, PIC)
        self.assertNotIn("proxy_cache_use_stale", PIC)
        self.assertLess(PIC.index("limit_req "), PIC.index("proxy_cache "))                          # (a request over the limit never reaches the cache)

    def test_the_allowance_is_twenty_a_second_with_a_burst_of_one_whole_list(self):
        zone = re.search(r"limit_req_zone \$binary_remote_addr zone=ants_hub_pic:(\d+)m rate=(\d+)r/([sm]);", HEAD)
        self.assertEqual((int(zone.group(1)), int(zone.group(2)), zone.group(3)), (1, 20, "s"))
        self.assertIn("limit_req zone=ants_hub_pic burst=240 nodelay;", PIC)
        self.assertIn("limit_req_status 429;", PIC)

    def test_the_checks_come_before_anything_is_passed_on_and_nothing_of_the_visitor_goes_with_it(self):
        for check in ('if ($request_method != GET) { return 405; }', 'if ($is_args) { return 404; }', 'if ($http_sec_fetch_site ~ "^(cross-site|same-site)$") { return 403; }', "limit_req "):
            self.assertIn(check, PIC)
            self.assertLess(PIC.index(check), PIC.index("proxy_pass "), check)
        self.assertIn("access_log off;", PIC)
        self.assertIn("proxy_pass_request_headers off;", PIC)
        self.assertIn("proxy_pass_request_body off;", PIC)
        self.assertEqual(sorted(re.findall(r"proxy_set_header ([A-Za-z-]+) ", PIC)), ["Connection", "Content-Length", "Host", "X-Client-Address"])
        self.assertIn("proxy_set_header X-Client-Address $remote_addr;", PIC)
        for name in ("hub_cookie", "http_cookie", "http_origin", "http_authorization"):
            self.assertNotIn(name, PIC)
        self.assertIn("resolver 127.0.0.11", PIC)
        self.assertIn("set $ants_hub ants-hub:4040;", PIC)
        self.assertIn("proxy_pass http://$ants_hub;", PIC)
        self.assertIn("proxy_read_timeout 5s;", PIC)
        self.assertIn("proxy_connect_timeout 3s;", PIC)


def nginx_binary():
    return shutil.which("nginx") or ("/usr/sbin/nginx" if os.path.exists("/usr/sbin/nginx") else "")


free_port = quick_http.free_port    # (every test takes its port from the one range that quick_http hands out)


class Stand(quick_http.QuickThreadingHTTPServer):
    """What the hub is, for the cache's and the headers' tests: the picture at NAME with the hub's headers, a JSON 404 for any other name; it records every request that reaches it."""

    daemon_threads = True

    def __init__(self):
        self.seen = []
        stand = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_ANY(self):
                stand.seen.append((self.command, self.path, [k.lower() for k in self.headers.keys()], dict((k.lower(), v) for k, v in self.headers.items())))
                if self.path == "/hub/pic/%s.png" % NAME:
                    self.send_response(200)
                    for k, v in (("Content-Type", "image/png"), ("X-Content-Type-Options", "nosniff"), ("Content-Security-Policy", "default-src 'none'; sandbox"), ("Cross-Origin-Resource-Policy", "same-origin"),
                                 ("Cache-Control", "private, max-age=86400, immutable"), ("Content-Length", str(len(BODY)))):
                        self.send_header(k, v)
                    self.end_headers()
                    self.wfile.write(BODY)
                else:
                    body = b'{"error":"not_found"}'
                    self.send_response(404)
                    for k, v in (("Content-Type", "application/json"), ("Cache-Control", "no-store"), ("X-Content-Type-Options", "nosniff"), ("Content-Length", str(len(body)))):
                        self.send_header(k, v)
                    self.end_headers()
                    self.wfile.write(body)

            do_GET = do_POST = do_ANY

            def log_message(self, *args):
                pass

        super().__init__(("127.0.0.1", 0), Handler)
        threading.Thread(target=self.serve_forever, daemon=True).start()

    def asked(self, path=None):
        return [s for s in self.seen if path is None or s[1] == path]


def mini_conf(tmp, port, hub_port):
    """The real text of the zone, the cache, the block and the server's own security headers in a small server; the hub's name is this machine's port."""
    zone = re.search(r"limit_req_zone \$binary_remote_addr zone=ants_hub_pic:[^;]+;", HEAD).group(0)
    cache = re.search(r"proxy_cache_path /var/cache/nginx/ants_hub_pics [^;]+;", HEAD).group(0).replace("/var/cache/nginx/ants_hub_pics", tmp + "/cache%d" % port)
    headers = "\n    ".join(m.group(0) for m in re.finditer(r"^\s*add_header (?:%s) [^;]+;" % "|".join(SECURITY), SERVER[:SERVER.index("location")], re.M))
    text = PIC.replace("resolver 127.0.0.11 valid=10s ipv6=off;", "").replace("set $ants_hub ants-hub:4040;", "set $ants_hub 127.0.0.1:%d;" % hub_port)
    off = "location @hub_off {%s}" % stats.block_of(CONF, "location @hub_off ")
    return ("daemon off;\nmaster_process off;\npid %(t)s/n%(p)d.pid;\nerror_log %(t)s/e%(p)d.log error;\nevents { worker_connections 1024; }\nhttp {\n  access_log %(t)s/a%(p)d.log;\n"
            "  client_body_temp_path %(t)s/b%(p)d;\n  proxy_temp_path %(t)s/p%(p)d;\n  %(zone)s\n  %(cache)s\n  server {\n    listen 127.0.0.1:%(p)d;\n    %(headers)s\n"
            "    %(header)s{%(block)s}\n    %(off)s\n    location / { return 404 'not passed on'; }\n  }\n}\n"
            % {"t": tmp, "p": port, "zone": zone, "cache": cache, "headers": headers, "header": SERVER[START:OPEN], "block": text, "off": off})


class Nginx:
    def __init__(self, tmp, hub_port):
        self.port, self.tmp = free_port(), tmp
        os.makedirs(tmp + "/cache%d" % self.port, exist_ok=True)
        conf = os.path.join(tmp, "nginx-%d.conf" % self.port)
        with open(conf, "w", encoding="utf-8") as f:
            f.write(mini_conf(tmp, self.port, hub_port))
        self.proc = subprocess.Popen([nginx_binary(), "-c", conf, "-p", tmp], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.5).close()
                return
            except OSError:
                if self.proc.poll() is not None:
                    err = self.proc.stderr.read().decode(errors="replace")[:400]
                    self.proc.stderr.close()
                    raise unittest.SkipTest("nginx would not start here: " + err)
                time.sleep(0.05)
        self.stop()
        raise unittest.SkipTest("nginx did not come up")

    def stop(self):
        self.proc.send_signal(signal.SIGTERM)
        try:
            self.proc.wait(5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.proc.stderr.close()

    def get(self, path, headers=(), method="GET"):
        return hc.request(self.port, method, path, list(headers))


@unittest.skipUnless(nginx_binary(), "no nginx program on this machine")
class TheBlockRuns(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants_hub_pic_nginx.")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.stand = Stand()
        self.addCleanup(self.stand.server_close)
        self.addCleanup(self.stand.shutdown)
        self.nginx = Nginx(self.tmp, self.stand.server_address[1])
        self.addCleanup(self.nginx.stop)
        self.path = "/hub/pic/%s.png" % NAME

    def names(self, response, name):
        return [v for k, v in response.headers if k == name]

    def test_a_picture_goes_through_whole_with_the_headers_of_the_hub_and_of_the_server_once_each(self):
        r = self.nginx.get(self.path)
        self.assertEqual((r.status, r.body), (200, BODY))
        wanted = {"content-type": "image/png", "x-content-type-options": "nosniff", "content-security-policy": "default-src 'none'; sandbox", "cross-origin-resource-policy": "same-origin",
                  "cache-control": "private, max-age=86400, immutable", "cross-origin-opener-policy": "same-origin", "cross-origin-embedder-policy": "require-corp",
                  "strict-transport-security": "max-age=604800"}
        for name, value in wanted.items():
            self.assertEqual(self.names(r, name), [value], name)
        self.assertEqual(self.names(r, "set-cookie") + self.names(r, "access-control-allow-origin"), [])

    def test_the_hub_is_asked_once_in_five_minutes_whatever_the_number_of_visitors_and_a_404_is_not_kept(self):
        for _ in range(5):
            self.assertEqual(self.nginx.get(self.path).status, 200)
        self.assertEqual(len(self.stand.asked(self.path)), 1)
        other = "/hub/pic/%s.png" % ("f" * 32)
        for _ in range(3):
            r = self.nginx.get(other)
            self.assertEqual((r.status, r.json()), (404, {"error": "not_found"}))
            self.assertEqual(self.names(r, "cache-control"), ["no-store"])
        self.assertEqual(len(self.stand.asked(other)), 3)
        # (the 404 of a name that the hub deleted is the hub's own: what nginx kept of an earlier answer is the cache's five minutes, which a test of seconds cannot wait for)

    def test_only_a_get_of_the_exact_shape_without_a_query_is_passed_on(self):
        for method in ("POST", "PUT", "DELETE", "HEAD"):
            self.assertEqual(self.nginx.get(self.path, method=method).status, 405, method)
        for path in (self.path + "?x=1", self.path + "?" + "a" * 10):
            self.assertEqual(self.nginx.get(path).status, 404, path)
        for path in ("/hub/pic/%s.png" % NAME[:31], "/hub/pic/%s0.png" % NAME, "/hub/pic/%s.png" % NAME.upper(), "/hub/pic/%s" % NAME, "/hub/pic/%s.png/" % NAME, "/hub/pic/%s.PNG" % NAME,
                     "/hub/pic/x/%s.png" % NAME, "/hub/pic/%%2e%s.png" % NAME[:31], "/hub/pic/.png", "/hub/pic/", "/hub/pic",
                     "/hub/pic/%s.jpg" % NAME, "/hub/pic/%s.png.gif" % NAME):
            r = self.nginx.get(path)
            self.assertEqual((r.status, r.text()), (404, "not passed on"), path)
        self.assertIn(self.nginx.get("/hub/pic/%s.png%%00" % NAME).status, (400, 404))           # (a byte that nginx itself refuses)
        self.assertEqual(self.stand.seen, [])
        self.assertEqual(self.nginx.get(self.path + "?").status, 404)                          # (a bare ? is no argument for nginx, so the hub is asked, and the hub's own check says no)
        self.assertEqual([s[1] for s in self.stand.seen], [self.path + "?"])

    def test_a_request_of_another_site_is_refused_before_the_hub_or_the_allowance_is_asked(self):
        for site in ("cross-site", "same-site"):
            self.assertEqual(self.nginx.get(self.path, [("Sec-Fetch-Site", site)]).status, 403, site)
        self.assertEqual(self.stand.seen, [])
        for site in ("same-origin", "none"):
            self.assertEqual(self.nginx.get(self.path, [("Sec-Fetch-Site", site)]).status, 200, site)
        self.assertEqual(self.nginx.get(self.path).status, 200)                                  # (no header at all: a program, an address typed)

    def test_nothing_of_the_visitors_request_reaches_the_hub_but_the_address_nginx_saw(self):
        self.nginx.get(self.path, [("Cookie", "__Host-ants_hub=%s; ants_admin=secret-1; other=2" % ("a" * 32)), ("Authorization", "Bearer canary-token"), ("Origin", "https://elsewhere.example"),
                                   ("Referer", "https://elsewhere.example/page"), ("X-Client-Address", "203.0.113.99"), ("X-Forwarded-For", "203.0.113.98"), ("Accept-Language", "de"), ("User-Agent", "canary-agent"),
                                   ("Sec-Fetch-Site", "same-origin"), ("Cache-Control", "no-cache"), ("Range", "bytes=0-10"), ("If-None-Match", "x")])
        (_, _, names, values), = self.stand.asked(self.path)
        self.assertEqual(sorted(names), ["connection", "host", "x-client-address"])
        self.assertEqual(values["x-client-address"], "127.0.0.1")                                   # (nginx's own view of the peer: the header of the visitor's was not passed on)
        self.assertEqual(values["connection"], "close")
        self.assertNotIn("canary", str(values))

    def test_an_address_has_a_burst_and_then_a_pace_and_what_is_over_never_reaches_the_hub(self):
        statuses = [self.nginx.get("/hub/pic/%s.png" % ("%032x" % n)).status for n in range(300)]
        self.assertEqual(statuses[:240], [404] * 240)
        self.assertIn(429, statuses[240:])
        self.assertLess(len(self.stand.seen), 300)
        self.assertEqual(len(self.stand.seen), statuses.count(404))

    def test_no_log_of_nginx_has_the_name(self):
        self.nginx.get(self.path)
        self.nginx.get("/hub/pic/%s.png" % ("e" * 32))
        time.sleep(0.2)
        for name in ("a%d.log", "e%d.log"):
            try:
                with open(os.path.join(self.tmp, name % self.nginx.port), encoding="utf-8") as f:
                    text = f.read()
            except OSError:
                text = ""
            self.assertNotIn(NAME, text)
            self.assertNotIn("e" * 32, text)

    def test_a_hub_that_is_not_there_is_the_json_503_of_the_chats_door(self):
        dead = Nginx(self.tmp, 1)
        self.addCleanup(dead.stop)
        r = dead.get(self.path)
        self.assertEqual((r.status, r.json()["error"]), (503, "hub_off"))


@unittest.skipUnless(nginx_binary(), "no nginx program on this machine")
class TheRealHubBehindIt(unittest.TestCase):
    def test_a_login_with_a_picture_ends_with_the_picture_through_the_door_and_the_old_name_is_gone_after_the_next_login(self):
        tmp = tempfile.mkdtemp(prefix="ants_hub_pic_real.")
        self.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
        site_port = free_port()
        site = "http://127.0.0.1:%d" % site_port
        fake = fd.FakeDiscord(site + "/hub/callback", user=(fd.snowflake(900), "maple.leaf"))
        fake.consent = False
        fake.avatar = fd.AVATAR
        self.addCleanup(fake.close)
        rig = hub_rig.Rig(discord=fake, origin=site, sockets_per_address=100)
        rig.__enter__()
        self.addCleanup(rig.__exit__, None, None, None)
        nginx = Nginx(tmp, rig.port)
        self.addCleanup(nginx.stop)
        names = []
        for _ in range(2):
            Visitor(rig.port, fake, address="198.51.100.5").login()
            for _ in range(100):
                found = rig.call(rig.store.picture_names)
                if found and list(found.values())[0] not in names:
                    names.append(list(found.values())[0])
                    break
                time.sleep(0.05)
        self.assertEqual(len(names), 2)
        new = nginx.get("/hub/pic/%s.png" % names[1])
        self.assertEqual((new.status, new.body), (200, __import__("hub.pictures", fromlist=["x"]).rewrite(fake.picture)))
        old = nginx.get("/hub/pic/%s.png" % names[0])
        self.assertEqual((old.status, old.json()), (404, {"error": "not_found"}))
        self.assertEqual([v for k, v in old.headers if k == "x-content-type-options"], ["nosniff"])       # (the hub's, and the server's: once)


if __name__ == "__main__":
    unittest.main()
