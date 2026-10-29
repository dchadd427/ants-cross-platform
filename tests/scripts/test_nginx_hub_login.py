#!/usr/bin/env python3
"""docker/nginx.conf routes the pages of the chat hub's Discord login (run by ./run_tests.sh --fast and by the CI; docs/HUB.md "Login", "The site's nginx").

/hub/login, /hub/callback, /hub/me, /hub/logout and /hub/forget go to the hub, and the login's cookie (`__Host-ants_hub`, Path=/, so the browser sends it with EVERY request to the site) goes to those
and to nothing else. The text of the file is checked everywhere:

  - each page is an exact location, the only one of its name: only the one method goes through, the checks (method, query, Fetch Metadata) come before anything is passed on;
  - the name of the hub is resolved when a request comes, like /hub/ws, so the site starts without it, and a hub that is not there is a page that says so (for the two that the browser navigates to)
    or the JSON 503 of the chat door (for the three that the page calls with fetch);
  - ten a minute for each address on /hub/login and /hub/callback (429 and a page, and the hub is not asked), twenty on the others;
  - the visitor's request is not passed on but the headers each page needs; the cookie is picked out of the whole header by name and shape, so that no other cookie reaches the hub, and the hub's
    cookies reach no other place: every other proxy block drops the Cookie header, or sets the one cookie that it needs (the admin page's API: its own);
  - no access log of these pages (the callback's address holds the code and the state), and the error log of the two that carry a query holds the serious errors only (nginx writes the whole request
    line of a request that a missing upstream fails).

Where an nginx program is installed the blocks are RUN: the real text in front of the real hub and a fake Discord (the whole login through nginx), in front of a stand-in that records what arrives
(the headers and cookies), and with real access and error logs that are read for the canaries.
"""
import http.server
import json
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
import urllib.parse

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import fake_discord as fd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
import test_nginx_stats as stats  # noqa: E402  (the readers of the file)
from login_flow import Visitor  # noqa: E402
from hub import login as loginmod  # noqa: E402

CONF = stats.strip_comments(stats.read("docker", "nginx.conf"))
HEAD = CONF[:CONF.index("server {")]
LOGIN, CALLBACK, ME, LOGOUT, FORGET = (stats.block_of(CONF, "location = /hub/%s " % n) for n in ("login", "callback", "me", "logout", "forget"))
WS_HUB = stats.block_of(CONF, "location = /hub/ws ")
OFF = stats.block_of(CONF, "location @hub_off ")
PAGE_OFF = stats.block_of(CONF, "location @hub_page_off ")
PAGE_LIMIT = stats.block_of(CONF, "location @hub_page_limit ")
PAGES = {"login": LOGIN, "callback": CALLBACK, "me": ME, "logout": LOGOUT, "forget": FORGET}
CANARY = "canary-query-5b81e3"


def headers_set(block):
    return sorted(re.findall(r"proxy_set_header ([A-Za-z-]+) ", block))


class TheText(unittest.TestCase):
    def test_each_page_is_an_exact_location_and_the_only_one_of_its_name(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        for name in PAGES:
            self.assertEqual([l for l in locations if l == "= /hub/" + name], ["= /hub/" + name])
        self.assertTrue(all(PAGES.values()) and PAGE_OFF and PAGE_LIMIT)

    def test_only_the_one_method_goes_through_and_the_checks_come_first(self):
        self.assertIn("if ($request_method != GET) { return 405; }", LOGIN)
        self.assertIn("if ($request_method != GET) { return 405; }", CALLBACK)
        self.assertIn("if ($request_method != GET) { return 405; }", ME)
        for block in (LOGOUT, FORGET):
            self.assertIn("if ($request_method != POST) { return 405; }", block)
        for block in (ME, LOGOUT, FORGET):                                                  # (what the page calls with fetch: no query, and the page's own request)
            self.assertIn("if ($is_args) { return 404; }", block)
            self.assertIn('if ($http_sec_fetch_site != "same-origin") { return 403; }', block)
        for name, block in PAGES.items():
            for check in ("$request_method", "limit_req "):
                self.assertLess(block.index(check), block.index("proxy_pass "), (name, check))
        for name in ("me", "logout", "forget"):
            for check in ("$is_args", "$http_sec_fetch_site"):
                self.assertLess(PAGES[name].index(check), PAGES[name].index("limit_req "), (name, check))     # (a request that is refused does not use up the allowance)

    def test_the_name_is_resolved_per_request_so_the_site_starts_without_the_hub(self):
        for name, block in PAGES.items():
            self.assertIn("resolver 127.0.0.11", block, name)
            self.assertIn("set $ants_hub ants-hub:4040;", block, name)
            self.assertIn("proxy_pass http://$ants_hub;", block, name)
        for name in ("login", "callback"):
            self.assertIn("error_page 502 504 = @hub_page_off;", PAGES[name])
            self.assertIn("error_page 429 = @hub_page_limit;", PAGES[name])
        for name in ("me", "logout", "forget"):
            self.assertIn("error_page 502 504 = @hub_off;", PAGES[name])
        self.assertIn("Login unavailable, try again later", PAGE_OFF)
        self.assertIn('href="/"', PAGE_OFF)
        self.assertIn("return 503 ", PAGE_OFF)
        self.assertIn("return 429 ", PAGE_LIMIT)
        self.assertIn("default_type text/html;", PAGE_OFF)
        self.assertIn('add_header Cache-Control "no-store" always;', PAGE_OFF)

    def test_the_allowances(self):
        for zone, rate in (("ants_hub_login", 10), ("ants_hub_callback", 10), ("ants_hub_session", 20)):
            m = re.search(r"limit_req_zone \$binary_remote_addr zone=%s:(\d+)m rate=(\d+)r/m;" % zone, HEAD)
            self.assertIsNotNone(m, zone)
            self.assertEqual(int(m.group(2)), rate, zone)
        self.assertIn("limit_req zone=ants_hub_login burst=5 nodelay;", LOGIN)
        self.assertIn("limit_req zone=ants_hub_callback burst=5 nodelay;", CALLBACK)
        for block in (ME, LOGOUT, FORGET):
            self.assertIn("limit_req zone=ants_hub_session burst=10 nodelay;", block)
        for block in (LOGIN, CALLBACK, ME, LOGOUT, FORGET):
            self.assertIn("limit_req_status 429;", block)

    def test_only_these_headers_go_on_and_no_body(self):
        base = ["Connection", "Content-Length", "Host", "X-Client-Address"]
        self.assertEqual(headers_set(LOGIN), sorted(base + ["Sec-Fetch-Site"]))
        self.assertEqual(headers_set(CALLBACK), sorted(base + ["Cookie"]))
        for block in (ME, LOGOUT, FORGET):
            self.assertEqual(headers_set(block), sorted(base + ["Cookie", "Origin", "Sec-Fetch-Site"]))
        for name, block in PAGES.items():
            self.assertIn("proxy_pass_request_headers off;", block, name)
            self.assertIn("proxy_pass_request_body off;", block, name)
            self.assertIn("proxy_set_header X-Client-Address $remote_addr;", block, name)
            self.assertIn("proxy_redirect off;", block, name)                                # (the address that the hub sends the visitor to is not rewritten)
            self.assertIn('proxy_set_header Connection "close";', block, name)
            for forbidden in ("proxy_cache", "add_header", "rewrite", "alias", "root", "try_files", "sub_filter", "X-Forwarded", "Authorization", "4002", "ants-server"):
                self.assertNotIn(forbidden, block, (name, forbidden))
        self.assertIn("proxy_set_header Cookie $hub_cookie_state;", CALLBACK)                  # (the state of a login on its way)
        for block in (ME, LOGOUT, FORGET, WS_HUB):
            self.assertIn("proxy_set_header Cookie $hub_cookie_session;", block)                # (the login)
        self.assertIn("proxy_set_header Origin $http_origin;", ME)

    def test_the_cookies_are_picked_out_by_the_names_and_shapes_that_the_hub_makes(self):
        self.assertEqual(loginmod.COOKIE, "__Host-ants_hub")
        self.assertEqual(loginmod.STATE_COOKIE, "__Host-ants_oauth")
        self.assertRegex(HEAD, r"map \$http_cookie \$hub_cookie_session \{[^}]*__Host-ants_hub=\[0-9a-f\]\{32\}")
        self.assertRegex(HEAD, r"map \$http_cookie \$hub_cookie_state \{[^}]*__Host-ants_oauth=\[0-9a-f\]\{32\}")
        self.assertRegex(HEAD, r"map \$http_cookie \$admin_cookie \{[^}]*ants_admin=")

    def test_the_hubs_cookies_reach_no_other_place_and_no_other_cookie_reaches_the_hub(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        allowed = {"= /hub/ws": "$hub_cookie_session", "= /hub/login": None, "= /hub/callback": "$hub_cookie_state", "= /hub/me": "$hub_cookie_session", "= /hub/logout": "$hub_cookie_session",
                   "= /hub/forget": "$hub_cookie_session", "^~ /admin/api/": "$admin_cookie"}
        checked = 0
        for location in locations:
            block = stats.block_of(CONF, "location %s " % location)
            if "proxy_pass " not in block:
                continue
            checked += 1
            sets = re.findall(r"proxy_set_header Cookie (\S+);", block)
            if location in allowed:
                self.assertEqual(sets, [allowed[location]] if allowed[location] else [], location)
            elif "proxy_pass_request_headers off;" in block:
                self.assertEqual(sets, [], location)                                           # (nothing of the visitor's request is passed on at all)
            else:
                self.assertEqual(sets, ['""'], location)                                       # (/ws and /busy pass the headers of the visitor: the cookie is the one that they drop)
            if location not in allowed and "proxy_pass_request_headers off;" in block:
                self.assertNotIn("$http_cookie", block, location)
        self.assertGreaterEqual(checked, 18)
        self.assertNotIn("$http_cookie;", re.sub(r"map \$http_cookie \$\w+", "", CONF))        # (no block passes the whole header)

    def test_nothing_is_written_to_the_access_log_for_these_pages_and_the_two_with_a_query_log_only_the_serious_errors(self):
        for name, block in PAGES.items():
            self.assertIn("access_log off;", block, name)
        for block in (PAGE_OFF, PAGE_LIMIT):
            self.assertIn("access_log off;", block)                                            # (nginx logs a request with the settings of the place where it ends)
        for name in ("login", "callback"):
            self.assertIn("error_log /dev/stderr crit;", PAGES[name], name)
        for block in (LOGIN, CALLBACK):
            self.assertNotRegex(block, r"\$(request|request_uri|args|query_string|arg_\w+)\b")

    def test_the_timeouts_hold_a_hub_that_waits_for_discord_and_no_longer(self):
        self.assertIn("proxy_read_timeout 15s;", CALLBACK)                                     # (the hub waits 8 seconds for Discord)
        for name, block in PAGES.items():
            self.assertIn("proxy_connect_timeout 3s;", block, name)
            if name != "callback":
                self.assertIn("proxy_read_timeout 5s;", block, name)


def nginx_binary():
    return shutil.which("nginx") or ("/usr/sbin/nginx" if os.path.exists("/usr/sbin/nginx") else "")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Stand(http.server.ThreadingHTTPServer):
    """A server that records the headers of what arrives and answers {"ok":true}."""

    daemon_threads = True

    def __init__(self):
        self.seen = []
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_ANY(self):
                owner.seen.append((self.command, self.path, [(k.lower(), v) for k, v in self.headers.items()]))
                body = b'{"ok":true}'
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            do_GET = do_POST = do_ANY

            def log_message(self, *args):
                pass

        super().__init__(("127.0.0.1", 0), Handler)
        threading.Thread(target=self.serve_forever, daemon=True).start()

    def header(self, name):
        return [dict(h).get(name) for _, _, h in self.seen]


def mini_conf(tmp, port, hub_port, stand_port, dead_port, error_level="error"):
    """The real text of the zones, the maps and the blocks in a small server: the hub's name is this machine's port, the game server's is a stand-in's, and the logs are real files."""
    zones = "\n  ".join(re.findall(r"limit_req_zone \$binary_remote_addr zone=ants_hub_(?:login|callback|session|req):[^;]+;", HEAD) + re.findall(r"limit_conn_zone \$binary_remote_addr zone=ants_hub_conn:[^;]+;", HEAD))
    zones += "\n  " + re.search(r"log_format ants_noquery [^;]+;", HEAD).group(0)
    maps = "\n  ".join("map $http_cookie $%s {%s}" % (name, stats.block_of(HEAD, "map $http_cookie $%s " % name)) for name in ("hub_cookie_session", "hub_cookie_state", "admin_cookie"))

    def block(header, where):
        text = stats.block_of(CONF, header)
        text = text.replace("resolver 127.0.0.11 valid=10s ipv6=off;", "")
        text = text.replace("set $ants_hub ants-hub:4040;", "set $ants_hub 127.0.0.1:%d;" % where)
        text = text.replace("set $ants_server ants-server:4002;", "set $ants_server 127.0.0.1:%d;" % stand_port)
        text = text.replace("set $ants_submit ants-server:4030;", "set $ants_submit 127.0.0.1:%d;" % stand_port)
        text = text.replace("error_log /dev/stderr crit;", "error_log %s/loc%d.log crit;" % (tmp, port))
        text = re.sub(r"limit_req zone=ants_(?:hub_req|busy|admin_api|admin_login) [^;]*;", "", text)         # (the other doors have their own tests, and their zones are not in this small server)
        return "location %s {%s}" % (header[len("location "):].strip(), text)

    blocks = []
    for name in ("login", "callback", "me", "logout", "forget", "ws"):
        blocks.append(block("location = /hub/%s " % name, hub_port))
    # the same pages, with a hub that is not there (a port that nothing listens on): the names of the pages change, the text does not
    for name in ("login", "callback", "me"):
        text = block("location = /hub/%s " % name, dead_port).replace("location = /hub/%s" % name, "location = /dead/%s" % name, 1)
        blocks.append(text.replace("proxy_pass http://$ants_hub;", "proxy_pass http://$ants_hub;"))
    blocks.append("location @hub_off {%s}" % stats.block_of(CONF, "location @hub_off "))
    blocks.append("location @hub_page_off {%s}" % stats.block_of(CONF, "location @hub_page_off "))
    blocks.append("location @hub_page_limit {%s}" % stats.block_of(CONF, "location @hub_page_limit "))
    for header in ("location = /ws ", "location = /busy ", "location ^~ /admin/api/ "):
        blocks.append(block(header, stand_port))
    blocks.append("location @submit_off {return 503 'off';}")
    # a control: a request that the stand-in's own location sends to a hub that is not there, with the DEFAULT error log and access log, shows that those logs would write the query
    blocks.append("location = /probe { error_page 502 504 = @probe_off; set $x 127.0.0.1:%d; proxy_pass http://$x; }\n    location @probe_off { return 503 'off'; }" % dead_port)
    return ("daemon off;\nmaster_process off;\npid %(t)s/n%(p)d.pid;\nerror_log %(t)s/e%(p)d.log %(lvl)s;\nevents { worker_connections 256; }\nhttp {\n  access_log %(t)s/a%(p)d.log;\n"
            "  client_body_temp_path %(t)s/b%(p)d;\n  proxy_temp_path %(t)s/p%(p)d;\n  %(zones)s\n  %(maps)s\n  server {\n    listen 127.0.0.1:%(p)d;\n    %(blocks)s\n"
            "    location / { return 200 'the page'; }\n  }\n}\n" % {"t": tmp, "p": port, "zones": zones, "maps": maps, "blocks": "\n    ".join(blocks), "lvl": error_level})


class Nginx:
    """One nginx of the real text (its own zones, so that a test that floods a page does not use up another's allowance) and its logs."""

    def __init__(self, tmp, port, hub_port, stand_port, dead_port):
        self.port, self.tmp = port, tmp
        conf = os.path.join(tmp, "nginx-%d.conf" % port)
        with open(conf, "w", encoding="utf-8") as f:
            f.write(mini_conf(tmp, port, hub_port, stand_port, dead_port))
        self.proc = subprocess.Popen([nginx_binary(), "-c", conf, "-p", tmp], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
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

    def log(self, name):
        try:
            with open(os.path.join(self.tmp, name % self.port), encoding="utf-8") as f:
                return f.read()
        except OSError:
            return ""


@unittest.skipUnless(nginx_binary(), "no nginx program on this machine")
class TheBlocksRun(unittest.TestCase):
    """The pages in front of the real hub, a fake Discord and a stand-in for the game server. `self.nginx` is the one that the hub believes is the site (its Origin); `more()` is another, in front of
    the same hub, with zones of its own."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants_hub_login_nginx.")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.stand = Stand()
        self.addCleanup(self.stand.server_close)
        self.addCleanup(self.stand.shutdown)
        self.dead = free_port()
        site_port = free_port()
        self.site = "http://127.0.0.1:%d" % site_port
        self.fake = fd.FakeDiscord(self.site + "/hub/callback")
        self.fake.consent = False
        self.addCleanup(self.fake.close)
        self.rig = hub_rig.Rig(discord=self.fake, origin=self.site, sockets_per_address=100)
        self.rig.__enter__()
        self.addCleanup(self.rig.__exit__, None, None, None)
        self.nginx = self.more(site_port)
        self.clients = []
        self.addCleanup(lambda: [c.close() for c in self.clients])

    def more(self, port=None, hub_port=None):
        n = Nginx(self.tmp, port or free_port(), hub_port or self.rig.port, self.stand.server_address[1], self.dead)
        self.addCleanup(n.stop)
        return n

    def visitor(self):
        v = Visitor(self.nginx.port, self.fake)
        v.origin = self.site
        return v

    # ---- the whole login through nginx -----------------------------------------------------------------------------------------------

    def test_the_whole_login_goes_through_nginx_and_the_cookies_and_the_addresses_arrive_whole(self):
        v = self.visitor()
        start = v.start("room")
        self.assertEqual(start.status, 302)
        self.assertTrue(start.get("location").startswith(self.fake.url + "/oauth2/authorize?client_id="))      # (not rewritten by nginx)
        self.assertEqual(len(start.all("set-cookie")), 1)
        back = v.at_discord(start.get("location"))
        self.assertEqual(urllib.parse.urlsplit(back).netloc, "127.0.0.1:%d" % self.nginx.port)
        out = v.callback(back)
        self.assertEqual((out.status, out.get("location")), (302, "/#room"))
        cookies = out.all("set-cookie")                                                         # (two Set-Cookie lines, both through)
        self.assertEqual(len(cookies), 2)
        self.assertRegex(cookies[0], r"^__Host-ants_hub=[0-9a-f]{32}; Path=/; Max-Age=2592000; Secure; HttpOnly; SameSite=Strict$")
        me = v.call("GET", "/hub/me")
        self.assertEqual(me.json()["you"], {"name": "maple.leaf", "mod": False})
        c = hc.Client(self.nginx.port, origin=self.site, headers={"Cookie": "other=1; " + v.jar.header(), "Sec-Fetch-Site": "same-origin"})
        self.clients.append(c)
        self.assertEqual(c.recv_json()["you"], {"name": "maple.leaf", "mod": False})
        out = v.call("POST", "/hub/logout")
        self.assertEqual((out.status, out.json()), (200, {"ok": True}))
        self.assertEqual(c.expect_close(3), 1000)
        self.assertEqual(v.call("GET", "/hub/me").json()["you"], None)

    def test_the_hub_sees_the_address_nginx_saw_and_not_the_one_a_header_names(self):
        v = self.visitor()
        r = v.call("GET", "/hub/login", headers=[("X-Client-Address", "203.0.113.9"), ("X-Forwarded-For", "203.0.113.10")])
        self.assertEqual(r.status, 302)
        self.assertEqual(self.rig.call(lambda: sorted(self.rig.hub.sessions.rate_login.buckets)), ["127.0.0.1"])

    def test_the_methods_the_query_and_the_page_of_the_visitor(self):
        for method, path, status in (("POST", "/hub/login", 405), ("PUT", "/hub/callback", 405), ("POST", "/hub/me", 405), ("GET", "/hub/logout", 405), ("GET", "/hub/forget", 405),
                                     ("GET", "/hub/me?x=1", 404), ("POST", "/hub/logout?x=1", 404), ("POST", "/hub/forget?x=1", 404)):
            r = hc.request(self.nginx.port, method, path, {"Sec-Fetch-Site": "same-origin", "Origin": self.site})
            self.assertEqual(r.status, status, (method, path))
        for path, method in (("/hub/me", "GET"), ("/hub/logout", "POST"), ("/hub/forget", "POST")):
            for headers in ({}, {"Sec-Fetch-Site": "cross-site"}, {"Sec-Fetch-Site": "same-site"}, {"Sec-Fetch-Site": "none"}):
                self.assertEqual(hc.request(self.nginx.port, method, path, dict(headers, Origin=self.site)).status, 403, (path, headers))
        v = self.visitor()
        v.login()
        r = v.call("POST", "/hub/logout", origin="https://evil.example")                         # (nginx lets it by, the hub says no)
        self.assertEqual(r.status, 403)

    # ---- what the hub (here: a stand-in) is given ----------------------------------------------------------------------------------------

    def test_each_page_passes_the_cookie_that_it_needs_and_the_visitors_other_cookies_never_reach_the_hub(self):
        front = self.more(hub_port=self.stand.server_address[1])                                  # (the stand-in is the hub: it records what arrives)
        session, state = "__Host-ants_hub=" + "ab" * 16, "__Host-ants_oauth=" + "cd" * 16
        site = {"Sec-Fetch-Site": "same-origin", "Origin": "https://chat.example.org", "Authorization": "Bearer canary-auth", "X-Forwarded-For": "6.6.6.6", "User-Agent": "canary-agent",
                "Referer": "https://evil.example/", "X-Client-Address": "9.9.9.9", "Cookie": "a=b; ants_admin=v2.canary-admin-6f; %s; z=1; %s; track=canary-track" % (session, state)}
        expect = {
            "/hub/login": ("GET", None, ["connection", "host", "sec-fetch-site", "x-client-address"]),
            "/hub/callback?code=%s&state=%s" % (CANARY, "cd" * 16): ("GET", state, ["connection", "cookie", "host", "x-client-address"]),
            "/hub/me": ("GET", session, ["connection", "cookie", "host", "origin", "sec-fetch-site", "x-client-address"]),
            "/hub/logout": ("POST", session, ["connection", "content-length", "cookie", "host", "origin", "sec-fetch-site", "x-client-address"]),
            "/hub/forget": ("POST", session, ["connection", "content-length", "cookie", "host", "origin", "sec-fetch-site", "x-client-address"]),
            "/hub/ws": ("GET", session, ["connection", "cookie", "host", "origin", "sec-fetch-site", "sec-websocket-key", "sec-websocket-version", "upgrade", "x-client-address"]),
        }
        for path, (method, cookie, names) in expect.items():
            self.stand.seen.clear()
            headers = dict(site)
            if path == "/hub/ws":
                headers.update({"Upgrade": "websocket", "Connection": "Upgrade", "Sec-WebSocket-Key": "dGhlIHNhbXBsZSBub25jZQ==", "Sec-WebSocket-Version": "13"})
            r = hc.request(front.port, method, path, headers, send_length=False)
            self.assertEqual(r.status, 200, (path, r.text()))
            (_, _, got), = self.stand.seen
            self.assertEqual(sorted({k for k, _ in got}), names, path)
            self.assertEqual(dict(got).get("cookie"), cookie, path)
            self.assertEqual(dict(got)["x-client-address"], "127.0.0.1", path)               # (nginx's own: not the one of the visitor's header)
            for canary in ("canary-admin", "canary-track", "canary-auth", "canary-agent", "6.6.6.6", "9.9.9.9", "evil.example"):
                self.assertNotIn(canary, str(got), (path, canary))
        # the hub's cookies reach no other door of the site, and the admin page's API gets its own cookie and no other
        for path, extra in (("/ws", {"Upgrade": "websocket", "Connection": "Upgrade"}), ("/busy", {}), ("/admin/api/maps", {"X-Ants-Csrf": "tok"})):
            self.stand.seen.clear()
            r = hc.request(front.port, "GET", path, dict(site, **extra), send_length=False)
            self.assertEqual(r.status, 200, (path, r.text()))
            got = dict(self.stand.seen[0][2])
            if path.startswith("/admin"):
                self.assertEqual(got.get("cookie"), "ants_admin=v2.canary-admin-6f")
            else:
                self.assertNotIn("cookie", got, path)
            self.assertNotIn("__Host", str(got), path)

    # ---- a hub that is not there, and a visitor over the limit --------------------------------------------------------------------------

    def test_a_hub_that_is_not_there_is_a_page_for_the_two_that_the_browser_goes_to_and_json_for_the_others(self):
        for path in ("/dead/login", "/dead/callback?code=x&state=y"):
            r = hc.request(self.nginx.port, "GET", path, {"Sec-Fetch-Site": "same-origin"})
            self.assertEqual(r.status, 503, path)
            self.assertTrue(r.get("content-type").startswith("text/html"))
            self.assertIn("Login unavailable, try again later", r.text())
            self.assertIn('href="/">Back to the front page</a>', r.text())
            self.assertEqual(r.get("cache-control"), "no-store")
            self.assertIn("default-src 'none'", r.get("content-security-policy"))
        r = hc.request(self.nginx.port, "GET", "/dead/me", {"Sec-Fetch-Site": "same-origin"})
        self.assertEqual((r.status, r.json()["error"]), (503, "hub_off"))

    def test_ten_a_minute_for_each_address_then_a_page_and_the_hub_is_not_asked(self):
        front = self.more()
        for page, path in (("callback", "/hub/callback?code=x&state=" + "0" * 32), ("login", "/hub/login")):
            before = self.rig.call(lambda: len(self.rig.hub.sessions.rate_callback.buckets) + len(self.rig.hub.sessions.rate_login.buckets))
            answers = [hc.request(front.port, "GET", path, {"Sec-Fetch-Site": "same-origin"}).status for _ in range(12)]
            self.assertEqual(answers.count(429), 6, (page, answers))                              # (the first, and a burst of five more; the rest in the same minute are 429)
            self.assertEqual(answers[:6], [302 if page == "login" else 400] * 6)
            r = hc.request(front.port, "GET", path, {"Sec-Fetch-Site": "same-origin"})
            self.assertEqual(r.status, 429)
            self.assertIn("Too many tries", r.text())
            self.assertIn('href="/">Back to the front page</a>', r.text())
            self.assertEqual(r.get("cache-control"), "no-store")
        # (the hub's own allowance of the same pages was not what answered: it has ten, and it was asked six times)
        self.assertEqual(self.rig.call(lambda: sorted(self.rig.hub.sessions.rate_login.buckets)), ["127.0.0.1"])
        self.assertEqual(self.rig.call(lambda: round(self.rig.hub.sessions.rate_login.buckets["127.0.0.1"].tokens)), 4)
        # the pages that look at a login or end one: twenty a minute, a burst of ten more
        answers = [hc.request(front.port, "GET", "/hub/me", {"Sec-Fetch-Site": "same-origin"}).status for _ in range(14)]
        self.assertEqual(answers.count(429), 3)

    # ---- the logs -------------------------------------------------------------------------------------------------------------------

    def test_no_log_of_nginx_holds_the_code_the_state_or_the_place_to_return_to(self):
        front = self.more()
        control = hc.request(front.port, "GET", "/probe?code=%s-control&state=x" % CANARY)                 # (a location that has the default logs: they DO write the query)
        self.assertEqual(control.status, 503)
        for path in ("/dead/callback?code=%s&state=%s" % (CANARY, "ab" * 16), "/dead/login?next=%s" % CANARY, "/hub/login?next=%s" % CANARY, "/hub/callback?code=%s&state=%s" % (CANARY, "ab" * 16),
                     "/hub/callback?code=%s-two" % CANARY):
            hc.request(front.port, "GET", path, {"Cookie": "__Host-ants_oauth=" + "ab" * 16})
        v = Visitor(front.port, self.fake)
        v.origin = self.site
        v.login()                                                                                 # (a whole one: the real code and state of the fake Discord)
        time.sleep(0.3)
        access, errors, mine = front.log("a%d.log"), front.log("e%d.log"), front.log("loc%d.log")
        self.assertIn("/probe", access)                                                           # (the log works)
        self.assertIn("%s-control" % CANARY, access)
        self.assertIn("%s-control" % CANARY, errors)                                              # (and the error log would write the query of a request that a missing hub fails)
        for text in (access, errors, mine):
            self.assertNotIn(v.code, text)
            self.assertNotIn(v.state, text)
            self.assertNotIn("code=%s-two" % CANARY, text)
            self.assertNotIn("code=%s&" % CANARY, text)
            self.assertNotIn("next=%s" % CANARY, text)
        for canary in ("/hub/callback", "/hub/login", "/hub/me", "/hub/logout", "/hub/forget", "/dead/callback", "/dead/login"):
            self.assertNotIn(canary, access)


if __name__ == "__main__":
    unittest.main()
