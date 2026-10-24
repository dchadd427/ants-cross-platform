#!/usr/bin/env python3
"""docker/nginx.conf routes the map submissions and the owner's page safely (run by ./run_tests.sh --fast; the blocks are RUN where the program nginx is installed).

The game server's SUBMISSION door (ants_server --submit-port 4030, docs/MAP_SUBMISSIONS.md) is reached by the site's nginx only:

  - the paths: /submissions and /submissions/<32 hex digits>[/withdraw] (a player), /admin/api/ (the owner's calls, the sign-in among them), /admin (the page, a static file that nothing links to), /maps/all (the
    list of the newest version) and the fallback that asks the game server for the file and the picture of an APPROVED map; everything else under /admin/ is a 404 (there is no helper, no callback);
  - what is passed on: nothing the visitor sent but the few headers that each block names (the token of a maker, the cookie and the CSRF token of the owner, the type of the body), the real client address in
    X-Client-Address (a header of that name from a visitor never gets through), no cookie set by the game server for a player's call and the cookie of the sign-in (Set-Cookie) for the owner's calls;
  - the sign-in (POST /admin/api/login) has an allowance of its own, six a minute for each address;
  - the limits that nginx holds before the game server is asked: the size of a map (under a million bytes for a Classic one, 4 MiB for an Extended one), the methods, an allowance for each address;
  - a game server that is not there is a JSON 503, never a page of the site; the owner's page is never kept by a cache, has a strict content security policy and cannot be framed.
"""
import http.client
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import quick_http  # noqa: E402
import test_nginx_maps as maps  # noqa: E402

REPO = maps.REPO
RAW = maps.RAW
CONF = maps.CONF
block_of = maps.block_of
PNG = b"\x89PNG\r\n\x1a\n" + b"stand-in picture"
LVL = b"stand-in map file"
ID = "0123456789abcdef0123456789abcdef"


class Door(BaseHTTPRequestHandler):
    """The submission door, the test-map door and the replay port of the rig: each answers JSON (or a file) and remembers what it was sent."""
    seen = []
    lock = threading.Lock()
    name = "door"

    def handle_any(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""
        with self.lock:
            self.seen.append((self.command, self.path, dict((k.lower(), v) for k, v in self.headers.items()), len(body)))
        if self.name == "replay" and self.path.startswith("/maps/files/Approved%20One"):
            data, kind = LVL, "application/octet-stream"
        elif self.name == "replay" and self.path.startswith("/maps/previews/Approved%20One"):
            data, kind = PNG, "image/png"
        elif self.name == "replay" and self.path == "/maps/all":
            data, kind = b'{"format":2,"maps":[]}', "application/json"
        elif self.name == "replay":
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        else:
            data, kind = json.dumps({"stub": self.name, "path": self.path}).encode(), "application/json"
        self.send_response(201 if self.command == "POST" and self.name in ("door", "testdoor") else 200)
        self.send_header("Content-Type", kind)
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        if self.name in ("door", "testdoor"):
            self.send_header("Set-Cookie", "evil=1")                  # (the game server sets the cookie of the owner's sign-in; for a player's call the site would not pass one on)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    do_GET = do_POST = do_PUT = do_DELETE = handle_any

    def log_message(self, *args):
        pass


def stub(name):
    cls = type("Stub_" + name, (Door,), {"seen": [], "lock": threading.Lock(), "name": name})
    port = maps.free_port()
    server = quick_http.QuickThreadingHTTPServer(("127.0.0.1", port), cls)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return cls, server, port


class Rig:
    def __init__(self, trusted="", relaxed=True, log=False, real_ports=None):
        """`real_ports` = (submission door, replay door[, test-map door]) of a running ants_server: the doors are then not the stand-ins of the rig but the real program (test_nginx_big_maps.py, test_nginx_test_maps.py)."""
        self.work = tempfile.mkdtemp(prefix="ants_nginx_submit_")
        for sub in ("srv/previews", "srv/files", "cache/ants_stats", "cache/ants_history", "tmp", "html", "trusted"):
            os.makedirs(os.path.join(self.work, sub), exist_ok=True)
        shutil.copyfile(os.path.join(REPO, "web", "admin.html"), os.path.join(self.work, "html", "admin.html"))
        shutil.copyfile(os.path.join(REPO, "web", "mapmaker.html"), os.path.join(self.work, "html", "mapmaker.html"))
        os.makedirs(os.path.join(self.work, "html", "front"), exist_ok=True)
        with open(os.path.join(self.work, "html", "front", "mapmaker_app.js"), "w") as f:
            f.write("// a script of the page\n")
        with open(os.path.join(self.work, "html", "index.html"), "w") as f:
            f.write("<html>the page of the site</html>")
        with open(os.path.join(self.work, "srv", "files", "Static One.lvl"), "wb") as f:
            f.write(b"static file")
        with open(os.path.join(self.work, "trusted", "proxies.conf"), "w") as f:
            f.write(trusted)
        self.port = maps.free_port()
        if real_ports is None:
            self.door, self.door_server, self.door_port = stub("door")
            self.replay, self.replay_server, self.replay_port = stub("replay")
            self.test_door, self.test_door_server, self.test_door_port = stub("testdoor")
        else:
            self.door = self.replay = self.door_server = self.replay_server = self.test_door = self.test_door_server = None
            self.door_port, self.replay_port = real_ports[0], real_ports[1]
            self.test_door_port = real_ports[2] if len(real_ports) > 2 else real_ports[0]
        text = RAW
        text = text.replace("/usr/share/nginx/html", self.work + "/html")
        text = text.replace("/etc/nginx/ants-trusted-proxies/", self.work + "/trusted/")
        text = text.replace("set $ants_submit ants-server:4030;", "set $ants_submit 127.0.0.1:%d;" % self.door_port)
        text = text.replace("set $ants_test_maps ants-server:4031;", "set $ants_test_maps 127.0.0.1:%d;" % self.test_door_port)
        text = text.replace("set $ants_replays ants-server:4020;", "set $ants_replays 127.0.0.1:%d;" % self.replay_port)
        text = text.replace("set $ants_replays ants-server:4002;", "set $ants_replays 127.0.0.1:%d;" % self.replay_port)
        text = text.replace("set $ants_server ants-server:4002;", "set $ants_server 127.0.0.1:%d;" % self.replay_port)
        text = text.replace("listen 80;", "listen 127.0.0.1:%d;" % self.port).replace("listen [::]:80;", "")
        if relaxed:                                                                              # (the tests that send many requests are not the flood; the test of the allowance is)
            text = re.sub(r"(zone=ants_submit_post:1m rate=)10r/m", r"\g<1>100000r/s", text)
            text = re.sub(r"(zone=ants_submit_read:1m rate=)10r/s", r"\g<1>100000r/s", text)
            text = re.sub(r"(zone=ants_test_maps:1m rate=)10r/m", r"\g<1>100000r/s", text)
        text = text.replace("/srv/community-maps/", self.work + "/srv/").replace("/var/cache/nginx/", self.work + "/cache/")
        self.log_file = os.path.join(self.work, "access.log")
        text = text.replace("access_log /dev/stdout ants_noquery;", ("access_log %s ants_noquery;" % self.log_file) if log else "access_log off;")          # (the image logs to its output)
        user = "user root;\n" if hasattr(os, "geteuid") and os.geteuid() == 0 else ""
        full = user + "pid %s/nginx.pid;\nerror_log %s/error.log;\nevents { worker_connections 256; }\nhttp {\n access_log off;\n client_body_temp_path %s/tmp/body;\n proxy_temp_path %s/tmp/proxy;\n fastcgi_temp_path %s/tmp/fcgi;\n uwsgi_temp_path %s/tmp/uwsgi;\n scgi_temp_path %s/tmp/scgi;\n%s\n}\n" % ((self.work,) * 7 + (text,))
        conf = os.path.join(self.work, "nginx.conf")
        with open(conf, "w") as f:
            f.write(full)
        test = subprocess.run(["nginx", "-t", "-p", self.work, "-c", conf], capture_output=True, text=True)
        if test.returncode != 0:
            raise AssertionError("nginx -t: " + test.stderr)
        self.proc = subprocess.Popen(["nginx", "-p", self.work, "-c", conf, "-g", "daemon off;"], stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
                break
            except OSError:
                time.sleep(0.05)

    def stop(self):
        self.proc.terminate()
        self.proc.wait(timeout=10)
        for server in (self.door_server, self.replay_server, self.test_door_server):
            if server is not None:
                server.shutdown()
                server.server_close()
        shutil.rmtree(self.work, ignore_errors=True)

    def ask(self, method, target, headers=None, body=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=20)
        try:
            conn.putrequest(method, target, skip_host=False, skip_accept_encoding=True)
            for k, v in (headers or {}).items():
                conn.putheader(k, v)
            if body is not None and "Content-Length" not in (headers or {}):
                conn.putheader("Content-Length", str(len(body)))
            conn.endheaders(body)
            r = conn.getresponse()
            return r.status, [(k.lower(), v) for k, v in r.getheaders()], r.read()
        finally:
            conn.close()

    def ask_head_only(self, method, target, length, headers=None):
        """A request that says it has a body of `length` bytes and sends none: what nginx answers on the head alone."""
        s = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        try:
            lines = ["%s %s HTTP/1.1" % (method, target), "Host: x", "Content-Length: %d" % length] + ["%s: %s" % kv for kv in (headers or {}).items()]
            s.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
            return int(s.recv(4096).split(b" ", 2)[1])
        finally:
            s.close()


def names(headers, name):
    return [v for k, v in headers if k == name]


class TheText(unittest.TestCase):
    def test_every_new_location_is_there_once_and_nothing_else_can_catch_the_paths(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        mine = [l for l in locations if "admin" in l or "submi" in l or "approved" in l]
        self.assertEqual(sorted(mine), sorted(["= /submissions", "^~ /submissions/", "@submit_off", "= /admin", "= /admin.html", "= /admin/", "^~ /admin/api/", "^~ /admin/", "@approved_files"]))
        for header in ("location = /submissions {", "location ^~ /submissions/ {", "location = /admin {", "location ^~ /admin/api/ {", "location ^~ /admin/ {"):
            self.assertEqual(CONF.count(header), 1, header)
        # (a regular-expression location would win over a prefix one only without ^~; the extension rule of scripts and styles must not answer for /admin.js either: /admin_page.js is a file of the page)
        self.assertNotRegex(CONF, r"location\s+~\*?\s+[^\n]*admin")

    def test_the_hardening_of_the_owner_paths(self):
        admin = block_of(CONF, "location = /admin ")
        self.assertIn('add_header Permissions-Policy "camera=(), microphone=(), geolocation=()', admin)
        api = block_of(CONF, "location ^~ /admin/api/ ")
        self.assertIn("proxy_pass http://$ants_submit$uri$is_args$args;", api)                            # (the normalized path goes on, never the raw request line)
        self.assertRegex(api, r'if \(\$uri !~ "\^/admin/api/\[A-Za-z0-9/_-\]\{0,120\}\\z"\) \{ return 404; \}')
        self.assertIn("limit_req zone=ants_admin_login burst=5 nodelay;", api)                           # (the sign-in's own allowance: the zone has a key only for that path)
        self.assertNotIn("login_off", CONF)                                                               # (no helper: nothing else answers for the sign-in)
        for gone in ("/admin/login", "/admin/callback", "/admin/logout", "ants-admin-login", "iscord"):
            self.assertNotIn(gone, CONF)
        # the size of a map is decided from the raw query exactly as the game server reads it, and the small limit is the Classic one
        head = CONF[:CONF.index("server {")]
        self.assertIn("map $args $submit_extended", head)
        self.assertNotIn("$arg_kind", head)
        self.assertIn("52428[0-8]", head)

    def test_the_zones_are_outside_the_server_and_sized_for_the_whole_site(self):
        head = CONF[:CONF.index("server {")]
        for zone, rate in (("ants_submit_post", "10r/m"), ("ants_submit_read", "10r/s"), ("ants_admin_api", "20r/s"), ("ants_admin_login", "6r/m")):
            self.assertRegex(head, r"limit_req_zone \$[a-z_]+ zone=%s:1m rate=%s;" % (zone, rate))
        for zone in ("ants_submit_post", "ants_submit_read", "ants_admin_api", "ants_admin_login"):
            self.assertIn("zone=" + zone + " ", CONF[CONF.index("server {"):])
        self.assertIn("map $request_method $submit_post_key", head)                     # (only a POST counts against the allowance of sending)
        self.assertIn("POST $binary_remote_addr;", head)
        self.assertRegex(head, r"map \$uri \$admin_login_key \{\s+default \"\";\s+/admin/api/login \$binary_remote_addr;\s+\}")          # (only the sign-in counts against the allowance of signing in)
        self.assertIn("limit_req_zone $admin_login_key zone=ants_admin_login:1m rate=6r/m;", head)

    def test_only_return_is_used_inside_an_if_and_the_checks_come_before_the_pass(self):
        for header in ("location = /submissions ", "location ^~ /submissions/ ", "location = /admin ", "location ^~ /admin/api/ ", "location @approved_files "):
            block = block_of(CONF, header)
            for line in [l.strip() for l in block.splitlines() if l.strip().startswith("if (")]:
                self.assertRegex(line, r"\{ return \d{3}; \}$", header)
            if "proxy_pass" in block:
                for check in ("$request_method", "limit_req "):
                    self.assertLess(block.index(check), block.index("proxy_pass"), header + check)

    def test_what_a_block_passes_on_is_named_and_the_visitors_own_headers_are_not(self):
        for header in ("location = /submissions ", "location ^~ /submissions/ ", "location ^~ /admin/api/ "):
            block = block_of(CONF, header)
            self.assertIn("proxy_pass_request_headers off;", block, header)
            self.assertIn('proxy_set_header Connection "close";', block, header)
        for header in ("location = /submissions ", "location ^~ /submissions/", "location ^~ /admin/api/"):
            block = block_of(CONF, header)
            self.assertIn("proxy_set_header X-Client-Address $remote_addr;", block, header)
            if "admin" in header:
                self.assertNotIn("Set-Cookie", block, header)                                          # (the game server's sign-in cookie goes through)
            else:
                self.assertIn("proxy_hide_header Set-Cookie;", block, header)                          # (a player's call never sets a cookie)
        self.assertEqual(re.findall(r"proxy_set_header\s+(\S+)", block_of(CONF, "location ^~ /admin/api/ ")),
                         ["Connection", "Host", "Content-Type", "Cookie", "X-Ants-Csrf", "Sec-Fetch-Site", "X-Client-Address"])
        self.assertEqual(re.findall(r"proxy_set_header\s+(\S+)", block_of(CONF, "location = /submissions ")),
                         ["Connection", "Host", "Content-Type", "Authorization", "Sec-Fetch-Site", "X-Client-Address"])
        self.assertNotIn("proxy_set_header Cookie", block_of(CONF, "location = /submissions "))           # (the cookie of the owner goes only where the owner's calls are)
        self.assertNotIn("proxy_set_header Authorization", block_of(CONF, "location ^~ /admin/api/ "))
        self.assertIn("include /etc/nginx/ants-trusted-proxies/*.conf;", CONF)

    def test_the_page_has_a_strict_policy_and_cannot_be_kept_or_framed(self):
        page = block_of(CONF, "location = /admin ")
        policy = re.search(r'add_header Content-Security-Policy "([^"]+)" always;', page).group(1)
        for part in ("default-src 'none'", "script-src 'self'", "style-src 'self'", "frame-ancestors 'none'", "base-uri 'none'", "form-action 'self'", "connect-src 'self'"):
            self.assertIn(part, policy)
        self.assertNotIn("unsafe", policy)
        for header in ('add_header Cache-Control "no-store" always;', 'add_header X-Frame-Options "DENY" always;', 'add_header Referrer-Policy "no-referrer" always;', 'add_header X-Robots-Tag "noindex, nofollow" always;'):
            self.assertIn(header, page)
        self.assertIn("try_files /admin.html =404;", page)
        self.assertEqual(block_of(CONF, "location = /admin.html ").strip(), "return 404;")
        self.assertEqual(block_of(CONF, "location ^~ /admin/ ").strip(), "return 404;")

    def test_the_editor_page_has_the_policy_and_the_headers_of_a_page_that_cannot_be_framed(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual(locations.count("= /mapmaker.html"), 1)
        page = block_of(CONF, "location = /mapmaker.html ")
        policy = re.search(r'add_header Content-Security-Policy "([^"]+)" always;', page).group(1)
        directives = dict((d.split(" ", 1)[0], d.split(" ", 1)[1] if " " in d else "") for d in policy.split("; "))
        self.assertEqual(directives, {"default-src": "'self'", "script-src": "'self' 'wasm-unsafe-eval'", "style-src": "'self' 'unsafe-inline'", "img-src": "'self' data: blob:", "connect-src": "'self'", "object-src": "'none'",
                                      "base-uri": "'none'", "frame-ancestors": "'none'"})
        self.assertNotIn("'unsafe-eval'", policy)
        self.assertNotRegex(policy, r"script-src[^;]*unsafe-inline")                         # (no inline script, ever)
        for header in ('add_header X-Frame-Options "DENY" always;', 'add_header Referrer-Policy "no-referrer" always;', 'add_header Cache-Control "no-cache, must-revalidate" always;',
                       'add_header Cross-Origin-Opener-Policy "same-origin" always;', 'add_header Cross-Origin-Embedder-Policy "require-corp" always;', 'add_header X-Content-Type-Options "nosniff" always;'):
            self.assertIn(header, page)
        # the page is the file that the extension rule would serve: the exact rule is in front of it, so the headers above are the ones the page gets
        self.assertLess(CONF.index("location = /mapmaker.html "), CONF.index("location ~* \\.(html|css|js)$"))

    def test_the_paths_that_carry_a_players_text_are_logged_without_their_query(self):
        head = CONF[:CONF.index("server {")]
        fmt = re.search(r"log_format\s+ants_noquery\s+(.+?);", head, re.S).group(1)
        self.assertIn("$uri", fmt)
        for query in ("$request ", "$request\"", "$request_uri", "$args", "$query_string", "$is_args"):
            self.assertNotIn(query, fmt)
        for header in ("location = /submissions ", "location ^~ /submissions/ ", "location ^~ /admin/api/ "):
            self.assertIn("access_log /dev/stdout ants_noquery;", block_of(CONF, header), header)
        self.assertIn("access_log /dev/stdout ants_noquery;", block_of(CONF, "location = /test-maps "))          # (the name of a test map is in the query: tests/scripts/test_nginx_test_maps.py)
        self.assertEqual(CONF.count("access_log /dev/stdout ants_noquery;"), 4)             # (no other block says it: the other paths keep the stock log)

    def test_the_stacks_the_image_and_the_documents_agree_with_the_file(self):
        for name in ("docker-compose.stack.yml", "docker-compose.staging.yml"):
            text = "\n".join(l for l in open(os.path.join(REPO, name), encoding="utf-8").read().splitlines() if not l.lstrip().startswith("#"))
            self.assertIn('"--submit-port", "${ANTS_SUBMIT_PORT:-0}"', text, name)                  # (off until the stack says so)
            self.assertNotIn("ants-admin-login", text, name)                                       # (no helper container: the game server signs people in)
            self.assertNotIn("DISCORD", text, name)
            self.assertNotIn("ANTS_PUBLIC_URL", text, name)
            self.assertEqual(text.count("ANTS_ADMIN_SESSION_SECRET: ${ANTS_ADMIN_SESSION_SECRET:-}"), 1, name)       # (the server's: the one setting that stays)
        self.assertIn("4030", RAW)
        self.assertIn("--submit-any-interface", open(os.path.join(REPO, "Dockerfile.server"), encoding="utf-8").read())
        dockerfile = open(os.path.join(REPO, "Dockerfile"), encoding="utf-8").read()
        for name in ("admin.html", "admin.css", "admin_page.js"):
            self.assertIn("COPY web/%s /usr/share/nginx/html/%s" % (name, name), dockerfile)
        self.assertIn("COPY docker/trusted-proxies.conf /etc/nginx/ants-trusted-proxies/proxies.conf", dockerfile)
        self.assertIn("ANTS_ADMIN_SESSION_SECRET", open(os.path.join(REPO, "docs", "MAP_SUBMISSIONS.md"), encoding="utf-8").read())


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the blocks of docker/nginx.conf for the map submissions were NOT run (tests/scripts/test_nginx_submissions.py)")
class TheBlocksRun(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rig = Rig()

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()

    def last(self, stub):
        with stub.lock:
            return stub.seen[-1]

    def count(self, stub):
        with stub.lock:
            return len(stub.seen)

    def test_the_page_is_the_file_with_its_headers_and_nothing_else_of_the_name_is_a_page(self):
        status, headers, body = self.rig.ask("GET", "/admin")
        self.assertEqual(status, 200)
        self.assertIn(b"admin_page.js", body)
        self.assertEqual(names(headers, "cache-control"), ["no-store"])
        self.assertIn("default-src 'none'", names(headers, "content-security-policy")[0])
        self.assertEqual(names(headers, "x-frame-options"), ["DENY"])
        self.assertEqual(names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(names(headers, "x-robots-tag"), ["noindex, nofollow"])
        self.assertEqual(self.rig.ask("HEAD", "/admin")[0], 200)
        self.assertEqual(self.rig.ask("POST", "/admin", body=b"")[0], 405)
        self.assertEqual(self.rig.ask("GET", "/admin.html")[0], 404)
        self.assertEqual(self.rig.ask("GET", "/admin/")[0], 301)
        before = self.count(self.rig.door)
        for path in ("/admin/x", "/admin/api", "/admin/logo.png", "/admin/login", "/admin/login/", "/admin/callback", "/admin/callback/x", "/admin/logout", "/admin/logout/x", "/admin/login.html"):
            status, headers, body = self.rig.ask("GET", path)
            self.assertIn(status, (301, 404), path)
            self.assertNotIn(b"the page of the site", body, path)                           # (never the page of the site)
            self.assertNotIn(b"stub", body, path)
        self.assertEqual(self.count(self.rig.door), before)

    def test_the_editor_page_comes_with_its_policy_and_cannot_be_framed_and_its_scripts_are_still_served_as_scripts(self):
        status, headers, body = self.rig.ask("GET", "/mapmaker.html")
        self.assertEqual(status, 200)
        self.assertIn(b"Map maker", body)
        policy = names(headers, "content-security-policy")
        self.assertEqual(len(policy), 1)
        self.assertIn("script-src 'self' 'wasm-unsafe-eval'", policy[0])
        self.assertIn("frame-ancestors 'none'", policy[0])
        self.assertEqual(names(headers, "x-frame-options"), ["DENY"])
        self.assertEqual(names(headers, "referrer-policy"), ["no-referrer"])
        self.assertEqual(names(headers, "cache-control"), ["no-cache, must-revalidate"])
        self.assertEqual(names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(names(headers, "cross-origin-embedder-policy"), ["require-corp"])
        self.assertEqual(names(headers, "cross-origin-opener-policy"), ["same-origin"])
        self.assertEqual(self.rig.ask("HEAD", "/mapmaker.html")[0], 200)
        status, headers, body = self.rig.ask("GET", "/front/mapmaker_app.js")
        self.assertEqual(status, 200)
        self.assertEqual(names(headers, "cache-control"), ["no-cache, must-revalidate"])                 # (a script keeps the headers it had)
        self.assertEqual(names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(names(headers, "x-frame-options"), [])

    def test_the_owners_api_passes_the_cookie_and_the_token_and_nothing_else_of_the_visitor_and_the_cookie_of_the_sign_in_comes_back(self):
        status, headers, body = self.rig.ask("POST", "/admin/api/maps/%s/approve" % ID, {"Cookie": "ants_admin=abc", "X-Ants-Csrf": "tok", "Content-Type": "application/json", "Authorization": "Bearer secret",
                                                                                      "X-Client-Address": "6.6.6.6", "X-Forwarded-For": "7.7.7.7", "Referer": "http://evil.example/", "User-Agent": "attacker/1",
                                                                                      "Sec-Fetch-Site": "same-origin", "X-Real-IP": "8.8.8.8"}, b"{}")
        self.assertEqual(status, 201)
        self.assertEqual(names(headers, "set-cookie"), ["evil=1"])                              # (what the game server sets for the owner's calls goes through: the sign-in, the new password, the sign-out)
        method, path, seen, length = self.last(self.rig.door)
        self.assertEqual((method, path, length), ("POST", "/admin/api/maps/%s/approve" % ID, 2))
        self.assertEqual(seen.get("cookie"), "ants_admin=abc")
        self.assertEqual(seen.get("x-ants-csrf"), "tok")
        self.assertEqual(seen.get("x-client-address"), "127.0.0.1")                           # (the real address of the connection; the visitor's own header is gone)
        self.assertEqual(seen.get("sec-fetch-site"), "same-origin")
        for gone in ("authorization", "x-forwarded-for", "referer", "user-agent", "x-real-ip"):
            self.assertNotIn(gone, seen)
        self.assertEqual(seen.get("connection"), "close")
        self.assertEqual(self.rig.ask("GET", "/admin/api/me")[0], 200)
        self.assertEqual(self.rig.ask("PUT", "/admin/api/me", body=b"")[0], 405)
        self.assertEqual(self.rig.ask("DELETE", "/admin/api/maps")[0], 405)
        self.assertEqual(self.rig.ask("POST", "/admin/api/maps/x/approve", {"Content-Type": "application/json"}, b"x" * 9000)[0], 413)
        before = self.count(self.rig.door)
        self.assertEqual(self.rig.ask("POST", "/admin/api/maps/x/approve", {"Content-Type": "application/json"}, b"x" * 9000)[0], 413)
        self.assertEqual(self.count(self.rig.door), before)                                    # (a body that is too large never reaches the game server)

    def test_a_player_sends_a_map_with_the_token_in_the_authorization_header_and_the_cookie_does_not_go_along(self):
        status, headers, body = self.rig.ask("POST", "/submissions?kind=classic&name=Abc&maker=Mia", {"Authorization": "Bearer tok", "Cookie": "ants_admin=abc", "Content-Type": "application/octet-stream",
                                                                                                       "X-Client-Address": "6.6.6.6"}, b"m" * 1000)
        self.assertEqual(status, 201)
        self.assertEqual(names(headers, "set-cookie"), [])                                      # (a player's call never gets a cookie, whatever the game server sent)
        method, path, seen, length = self.last(self.rig.door)
        self.assertEqual((method, path, length), ("POST", "/submissions?kind=classic&name=Abc&maker=Mia", 1000))
        self.assertEqual((seen.get("authorization"), seen.get("x-client-address"), seen.get("content-type")), ("Bearer tok", "127.0.0.1", "application/octet-stream"))
        self.assertNotIn("cookie", seen)
        self.assertEqual(self.rig.ask("GET", "/submissions", {"Authorization": "Bearer tok"})[0], 200)
        self.assertEqual(self.rig.ask("PUT", "/submissions", body=b"")[0], 405)
        self.assertEqual(self.rig.ask("DELETE", "/submissions")[0], 405)
        self.assertEqual(self.rig.ask("PATCH", "/submissions", body=b"")[0], 405)

    def test_the_size_of_a_map_is_held_before_the_game_server_is_asked(self):
        rig = self.rig
        before = self.count(rig.door)
        self.assertEqual(rig.ask_head_only("POST", "/submissions?kind=classic&name=Abc&maker=Mia", 524289), 413)          # (a Classic map is 512 KB at most: one byte more is turned away here)
        self.assertEqual(rig.ask_head_only("POST", "/submissions?kind=classic&name=Abc&maker=Mia", 1000000), 413)
        for same_as_none in ("KIND=extended", "Kind=extended", "kind=Extended", "kind=extended%20", "kind=classic&kind=extended", "xkind=extended", "name=kind=extended", "kind=extendedx"):          # (only the exact kind the server reads gets 4 MiB)
            self.assertEqual(rig.ask_head_only("POST", "/submissions?" + same_as_none + "&name=Abc", 600000), 413, same_as_none)
        self.assertEqual(rig.ask_head_only("POST", "/submissions?name=Abc&kind=extended", 4 * 1024 * 1024 + 1), 413)
        self.assertEqual(rig.ask_head_only("POST", "/submissions?name=Abc&maker=Mia", 2000000), 413)                      # (no kind: the Classic limit)
        self.assertEqual(rig.ask_head_only("POST", "/submissions?kind=huge&name=Abc", 2000000), 413)
        self.assertEqual(rig.ask_head_only("POST", "/submissions?kind=extended&name=Abc&maker=Mia", 4 * 1024 * 1024 + 1), 413)
        self.assertEqual(rig.ask_head_only("POST", "/submissions?kind=extended&name=Abc&maker=Mia", 10 * 1024 * 1024), 413)
        self.assertEqual(self.count(rig.door), before)
        self.assertEqual(rig.ask("POST", "/submissions?kind=classic&name=Abc&maker=Mia", {"Content-Type": "application/octet-stream"}, b"c" * 524288)[0], 201)
        self.assertEqual(self.last(rig.door)[3], 524288)
        self.assertEqual(rig.ask("POST", "/submissions?kind=extended&name=Abc&maker=Mia", {"Content-Type": "application/octet-stream"}, b"e" * 600000)[0], 201)       # (the same size is fine as Extended)
        self.assertEqual(rig.ask("POST", "/submissions?name=Abc&kind=extended", {"Content-Type": "application/octet-stream"}, b"e" * 600000)[0], 201)
        self.assertEqual(rig.ask("POST", "/submissions?kind=extended&name=Abc&maker=Mia", {"Content-Type": "application/octet-stream"}, b"e" * (3 * 1024 * 1024))[0], 201)
        self.assertEqual(self.last(rig.door)[3], 3 * 1024 * 1024)
        self.assertEqual(rig.ask("POST", "/submissions?kind=extended&name=Abc&maker=Mia", {"Content-Type": "application/octet-stream"}, b"e" * (4 * 1024 * 1024))[0], 201)

    def test_the_owner_api_gets_the_normalized_path_and_only_plain_characters_pass(self):
        rig = self.rig
        before = self.count(rig.door)
        for target in ("/admin/api/maps/%41%42/approve", "/admin/api/./maps", "/admin/api/maps//x"):          # (decoded and cleaned up by nginx: the game server sees the clean path)
            self.assertIn(rig.ask("GET", target)[0], (200, 404), target)
        method, path, seen, length = self.last(rig.door)
        self.assertEqual(path, "/admin/api/maps/x")                                                       # (the last of them: "//" is one "/")
        rig.ask("GET", "/admin/api/maps/%41%42/approve")
        self.assertEqual(self.last(rig.door)[1], "/admin/api/maps/AB/approve")
        after = self.count(rig.door)
        for target in ("/admin/api/a%20b", "/admin/api/a%0d%0ab", "/admin/api/a%00b", "/admin/api/a.b", "/admin/api/maps?state=x%0d%0a", "/admin/api/" + "a" * 130, "/admin/api/%2e%2e/x"):
            status = rig.ask("GET", target)[0]
            if "?" not in target:
                self.assertIn(status, (400, 404), target)                                                  # (a NUL is a bad request for nginx itself)
        self.assertEqual(self.count(rig.door), after + 1)                                                   # (only the one with a query got through: nginx cleans a query's line breaks away itself)

    def test_a_players_own_calls_by_id_are_only_the_shape_of_an_id(self):
        rig = self.rig
        before = self.count(rig.door)
        for target in ("/submissions/", "/submissions/x", "/submissions/" + ID.upper(), "/submissions/" + ID[:-1], "/submissions/" + ID + "0", "/submissions/" + ID + "/other", "/submissions/" + ID + "/withdraw/x",
                       "/submissions/" + ID + "?x=1"):
            status = rig.ask("GET", target)[0]
            self.assertEqual(status, 404, target)
        self.assertEqual(self.count(rig.door), before)
        self.assertEqual(rig.ask("GET", "/submissions/" + ID, {"Authorization": "Bearer t"})[0], 200)
        self.assertEqual(rig.ask("POST", "/submissions/" + ID + "/withdraw", {"Authorization": "Bearer t"}, b"")[0], 201)
        self.assertEqual(self.last(rig.door)[1], "/submissions/" + ID + "/withdraw")
        self.assertEqual(rig.ask("DELETE", "/submissions/" + ID)[0], 405)
        self.assertEqual(rig.ask("POST", "/submissions/" + ID + "/withdraw", {"Authorization": "Bearer t"}, b"x" * 2000)[0], 413)

    def test_the_sign_in_is_a_post_to_the_game_server_with_the_body_the_cookie_and_nothing_else_of_the_visitor(self):
        rig = self.rig
        body = b'{"username":"dave","password":"a password that nginx must not keep"}'
        status, headers, text = rig.ask("POST", "/admin/api/login", {"Content-Type": "application/json", "Cookie": "ants_admin=old", "Referer": "http://x.example/", "X-Client-Address": "6.6.6.6", "Sec-Fetch-Site": "same-origin"}, body)
        self.assertEqual(status, 201)
        method, path, seen, length = self.last(rig.door)
        self.assertEqual((method, path, length), ("POST", "/admin/api/login", len(body)))
        self.assertEqual((seen.get("content-type"), seen.get("x-client-address"), seen.get("sec-fetch-site"), seen.get("cookie")), ("application/json", "127.0.0.1", "same-origin", "ants_admin=old"))
        self.assertNotIn("referer", seen)
        self.assertEqual(rig.ask("GET", "/admin/api/login")[0], 200)                              # (a GET goes on too: the game server answers it as a path that is not there)
        self.assertEqual(rig.ask("POST", "/admin/api/login", {"Content-Type": "application/json"}, b"x" * 9000)[0], 413)
        # nothing of the body is written down: the access log holds the request line only (the rig's log is off; the block has no log_format that names a body)
        self.assertNotIn("$request_body", RAW)

    def test_the_sign_in_has_an_allowance_of_its_own_and_the_other_calls_do_not_use_it(self):
        rig = Rig()
        try:
            body = b'{"username":"dave","password":"x"}'
            codes = [rig.ask("POST", "/admin/api/login", {"Content-Type": "application/json"}, body)[0] for _ in range(8)]
            self.assertEqual(codes[:6], [201] * 6)                                              # (one and a burst of five)
            self.assertEqual(codes[6:], [503, 503])
            self.assertEqual(rig.ask("GET", "/admin/api/me")[0], 200)                           # (the page's other calls are not held back by it)
            self.assertEqual(rig.ask("POST", "/admin/api/password", {"Content-Type": "application/json"}, b"{}")[0], 201)
            self.assertEqual(rig.ask("GET", "/admin/api/login")[0], 503)                        # (the key is the path: any call to it uses the allowance of the address)
        finally:
            rig.stop()

    def test_a_file_and_a_picture_of_an_approved_map_come_from_the_game_server_when_the_folder_has_none(self):
        rig = self.rig
        status, headers, body = rig.ask("GET", "/maps/files/Static%20One.lvl")
        self.assertEqual((status, body), (200, b"static file"))                                  # (the folder of the image first)
        before = self.count(rig.replay)
        status, headers, body = rig.ask("GET", "/maps/files/Approved%20One.lvl")
        self.assertEqual((status, body), (200, LVL))
        self.assertEqual(self.count(rig.replay), before + 1)
        self.assertEqual(self.last(rig.replay)[1], "/maps/files/Approved%20One.lvl")
        self.assertEqual(names(headers, "cache-control"), ["public, max-age=300"])               # (the server's no-store is replaced: a picture may be kept for a few minutes)
        self.assertEqual(names(headers, "x-content-type-options"), ["nosniff"])
        status, headers, body = rig.ask("GET", "/maps/previews/Approved%20One.png")
        self.assertEqual((status, body), (200, PNG))
        self.assertEqual(names(headers, "content-type"), ["image/png"])
        self.assertEqual(rig.ask("HEAD", "/maps/previews/Approved%20One.png")[0], 200)
        seen = self.last(rig.replay)[2]
        for gone in ("cookie", "authorization", "referer", "user-agent"):
            self.assertNotIn(gone, seen)
        before = self.count(rig.replay)
        for target in ("/maps/previews/x.png?y=1", "/maps/files/Approved%20One.lvl?y=1", "/maps/previews/..%2fx.png", "/maps/files/a%2fb.lvl", "/maps/files/" + "a" * 90 + ".lvl", "/maps/previews/.png/",
                       "/maps/previews/Approved%20One.gif"):
            self.assertEqual(rig.ask("GET", target)[0], 404, target)
        self.assertEqual(rig.ask("POST", "/maps/files/Approved%20One.lvl", body=b"")[0], 405)
        self.assertEqual(self.count(rig.replay), before)                                        # (a name of the wrong shape or a method that is not GET never reaches the game server)
        self.assertEqual(rig.ask("GET", "/maps/files/Nothing%20Here.lvl")[0], 404)               # (the game server says 404 for a name it does not offer)

    def test_the_newest_versions_list_is_the_old_lists_twin_with_its_own_cache_entry(self):
        rig = self.rig
        before = self.count(rig.replay)
        status, headers, body = rig.ask("GET", "/maps/all")
        self.assertEqual((status, json.loads(body)["format"]), (200, 2))
        rig.ask("GET", "/maps/all")
        self.assertEqual(self.count(rig.replay), before + 1)                                    # (five seconds, one entry for everybody)
        self.assertEqual(rig.ask("GET", "/maps/all?x=1")[0], 404)
        self.assertEqual(rig.ask("POST", "/maps/all", body=b"")[0], 405)
        self.assertEqual(rig.ask("GET", "/maps/all/")[0], 404)


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the allowances of docker/nginx.conf for the map submissions were NOT run (tests/scripts/test_nginx_submissions.py)")
class TheAllowances(unittest.TestCase):
    def test_sending_has_an_allowance_for_each_address_and_reading_has_its_own(self):
        rig = Rig(relaxed=False)
        try:
            results = [rig.ask("POST", "/submissions?kind=classic&name=Abc&maker=Mia", {"Content-Type": "application/octet-stream"}, b"m" * 100)[0] for _ in range(12)]
            self.assertEqual(results[:4], [201, 201, 201, 201])                                   # (ten a minute with a burst of three: the fourth in a row still passes ...)
            self.assertEqual(set(results[4:]), {503})                                              # ... and a flood is turned away here
            reads = [rig.ask("GET", "/submissions", {"Authorization": "Bearer t"})[0] for _ in range(40)]
            self.assertEqual(reads[0], 200)                                                         # (reading has a zone of its own: the used-up allowance of sending does not touch it)
            self.assertIn(503, reads)                                                               # (ten a second with a burst of twenty)
            self.assertEqual(sum(1 for r in reads if r == 200) >= 15, True)
        finally:
            rig.stop()


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the real-address rule of docker/nginx.conf was NOT run (tests/scripts/test_nginx_submissions.py)")
class TheRealAddress(unittest.TestCase):
    def test_the_address_is_the_connections_unless_the_proxy_is_named_and_then_the_one_it_saw(self):
        plain = Rig()
        try:
            plain.ask("GET", "/admin/api/me", {"X-Forwarded-For": "198.51.100.7"})
            self.assertEqual(plain.door.seen[-1][2].get("x-client-address"), "127.0.0.1")        # (nothing is named: a header that anyone can write is not believed)
        finally:
            plain.stop()
        proxied = Rig(trusted="set_real_ip_from 127.0.0.1;\nreal_ip_header X-Forwarded-For;\nreal_ip_recursive on;\n")
        try:
            proxied.ask("GET", "/admin/api/me", {"X-Forwarded-For": "198.51.100.7"})
            self.assertEqual(proxied.door.seen[-1][2].get("x-client-address"), "198.51.100.7")
            proxied.ask("GET", "/admin/api/me", {"X-Forwarded-For": "1.2.3.4, 198.51.100.8"})     # (a visitor who put his own address first: the proxy's own entry is the last one)
            self.assertEqual(proxied.door.seen[-1][2].get("x-client-address"), "198.51.100.8")
            proxied.ask("POST", "/submissions?kind=classic&name=Abc&maker=Mia", {"X-Forwarded-For": "198.51.100.9", "X-Client-Address": "6.6.6.6", "Content-Type": "application/octet-stream"}, b"m")
            self.assertEqual(proxied.door.seen[-1][2].get("x-client-address"), "198.51.100.9")
        finally:
            proxied.stop()


class TheDoorIsDown(unittest.TestCase):
    @unittest.skipUnless(shutil.which("nginx"), "nginx is not installed")
    def test_a_game_server_that_is_not_there_is_a_json_503_never_a_page_of_the_site(self):
        rig = Rig()
        try:
            rig.door_server.shutdown()
            rig.door_server.server_close()
            for method, target, body in (("GET", "/admin/api/me", None), ("POST", "/admin/api/login", b"{}"), ("POST", "/submissions?kind=classic&name=Abc&maker=Mia", b"m"), ("GET", "/submissions", None)):
                status, headers, text = rig.ask(method, target, {"Content-Type": "application/octet-stream"} if body else None, body)
                self.assertEqual(status, 503, target)
                self.assertEqual(json.loads(text)["error"], "submissions_off")
                self.assertEqual(names(headers, "cache-control"), ["no-store"])
            self.assertEqual(rig.ask("GET", "/admin")[0], 200)                                   # (the page is a file: it still opens, and says what it can)
        finally:
            rig.stop()


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the access log of the map submissions was NOT run (tests/scripts/test_nginx_submissions.py)")
class TheLogHasNoQuery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rig = Rig(log=True)

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()

    def lines(self):
        for _ in range(50):                                                                    # (the log is written when the answer has gone: wait for the last line)
            with open(self.rig.log_file, encoding="utf-8") as f:
                lines = f.read().splitlines()
            if lines:
                return lines
            time.sleep(0.05)
        return []

    def test_a_note_a_name_and_a_maker_are_not_in_the_log_and_the_path_is(self):
        secret = "SecretNote%20from%20a%20player"
        self.assertEqual(self.rig.ask("POST", "/submissions?kind=classic&name=Secret%20Name&maker=Secret%20Maker&note=" + secret, {"Content-Type": "application/octet-stream"}, b"m" * 100)[0], 201)
        self.assertEqual(self.rig.ask("GET", "/submissions?x=QueryOne")[0], 200)
        self.assertEqual(self.rig.ask("GET", "/submissions/%s?x=QueryTwo" % ID)[0], 404)                  # (a query there is a 404: still logged without it)
        self.assertEqual(self.rig.ask("GET", "/admin/api/maps?state=QueryThree")[0], 200)
        self.assertEqual(self.rig.ask("POST", "/admin/api/maps/%s/decline?reason=QueryFour" % ID, {"Content-Type": "application/json"}, b"{}")[0], 201)
        time.sleep(0.3)
        text = "\n".join(self.lines())
        for secret_word in ("Secret", "QueryOne", "QueryTwo", "QueryThree", "QueryFour", "kind=classic", "?"):
            self.assertNotIn(secret_word, text, secret_word)
        self.assertEqual(len(re.findall(r'"POST /submissions HTTP/1\.1" 201 ', text)), 1, text)
        self.assertIn('"GET /submissions HTTP/1.1" 200 ', text)
        self.assertIn('"GET /admin/api/maps HTTP/1.1" 200 ', text)
        self.assertIn('"POST /admin/api/maps/%s/decline HTTP/1.1" 201 ' % ID, text)
        self.assertIn('"GET /submissions/%s HTTP/1.1" 404 ' % ID, text)


if __name__ == "__main__":
    unittest.main()
