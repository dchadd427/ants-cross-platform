#!/usr/bin/env python3
"""docker/nginx.conf routes the map submissions and the admin page safely (run by ./run_tests.sh --fast; the blocks are RUN where the program nginx is installed).

The game server's SUBMISSION door (ants_server --submit-port 4030, docs/MAP_SUBMISSIONS.md) is reached by the site's nginx only:

  - the paths: /submissions and /submissions/<32 hex digits>[/withdraw] (a player), /admin/api/ (the admin's calls), /admin (the page, a static file that nothing links to), /maps/all (the
    list of the newest version) and the fallback that asks the game server for the file and the picture of an APPROVED map; everything else under /admin/ is a 404 (there is no helper, no callback);
  - what is passed on: nothing the visitor sent but the few headers that each block names (the token of a maker, the CSRF token of the admin, the type of the body), the real client address in
    X-Client-Address (a header of that name from a visitor never gets through), and no cookie in either direction;
  - the admin's door (docs/HUB.md "The admin page's door"): the chat hub's gate is asked once for each request of /admin/api/ (a stand-in hub plays it below), only its signed pass goes to the game server, a visitor
    whom it does not let in is answered here in JSON, a gate in trouble is a 503, a request of another site is turned away first, and the allowance is for each address;
  - the limits that nginx holds before the game server is asked: the size of a map (under a million bytes for a Classic one, 4 MiB for an Extended one), the methods, an allowance for each address;
  - a game server that is not there is a JSON 503, never a page of the site; the admin page is never kept by a cache, has a strict content security policy and cannot be framed.
"""
import http.client
import json
import os
import re
import shutil
import socket
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import admin_pass  # noqa: E402
import quick_http  # noqa: E402
import test_nginx_maps as maps  # noqa: E402

REPO = maps.REPO
RAW = maps.RAW
CONF = maps.CONF
block_of = maps.block_of
PNG = b"\x89PNG\r\n\x1a\n" + b"stand-in picture"
LVL = b"stand-in map file"
ID = "0123456789abcdef0123456789abcdef"
HUB_COOKIE = "__Host-ants_hub="
ADMIN_SESSION, MOD_SESSION, ENDED_SESSION, BROKEN_SESSION, BUSY_SESSION, REDIRECT_SESSION, UNKNOWN_SESSION, SHUT_SESSION = ("a" * 32, "b" * 32, "c" * 32, "d" * 32, "e" * 32, "f" * 32, "0" * 32, "1" * 32)       # (what the stand-in gate answers for each, below)
ADMIN = {"Cookie": HUB_COOKIE + ADMIN_SESSION}                                                  # (the login of an admin: the stand-in gate lets it in with a pass)
DEVNULL = object()                                                                              # (Rig(own=DEVNULL): the file that the stack files mount when ANTS_TRUSTED_PROXIES_FILE is not set)


class Door(BaseHTTPRequestHandler):
    """The submission door, the test-map door and the replay port of the rig: each answers JSON (or a file) and remembers what it was sent."""
    seen = []
    raw = []                                                                                    # (the headers of each request as they came: every line, in order, with its own letter case)
    lock = threading.Lock()
    name = "door"

    def handle_any(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""
        with self.lock:
            self.seen.append((self.command, self.path, dict((k.lower(), v) for k, v in self.headers.items()), len(body)))
            self.raw.append(list(self.headers.items()))
        answer = re.match(r"/admin/api/answer-(\d{3})\Z", self.path) if self.name == "door" else None
        if answer:                                                                                # (the game server says a status of its own: nginx must pass it on as it is)
            data = json.dumps({"from": "game_server", "code": int(answer.group(1))}).encode()
            self.send_response(int(answer.group(1)))
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
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
            self.send_header("Set-Cookie", "evil=1")                  # (a game server that sets a cookie: the site never passes one on)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    do_GET = do_POST = do_PUT = do_DELETE = handle_any

    def log_message(self, *args):
        pass


class Gate(BaseHTTPRequestHandler):
    """A stand-in for the chat hub's gate (GET /hub/admin/gate, docs/HUB.md): what it answers depends on the login cookie that it is shown, and it writes down every request exactly as it came.
    No cookie, or a login that ended: 401. An admin's login: 204 with a pass that tests/scripts/admin_pass.py makes (the game server's checks are not this file's business). A moderator's: 403.
    The other values are a gate that is in trouble (500, 429, a redirect, 404) and a hub whose door is shut for want of a good key (503, whatever the cookie): nginx must call all of them a door that is off."""
    seen = []                                                                                   # (method, request line, headers as they came, length of the body, request line's version)
    passes = []                                                                                 # (the pass of each 204, in order)
    lock = threading.Lock()
    name = "gate"

    def handle_any(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""
        cookie = self.headers.get("Cookie", "")
        session = cookie.split(HUB_COOKIE, 1)[1][:32] if HUB_COOKIE in cookie else ""
        status = {ADMIN_SESSION: 204, MOD_SESSION: 403, BROKEN_SESSION: 500, BUSY_SESSION: 429, REDIRECT_SESSION: 302, UNKNOWN_SESSION: 404, SHUT_SESSION: 503}.get(session, 401)
        made = admin_pass.make_pass() if status == 204 else None
        with self.lock:
            self.seen.append((self.command, self.path, list(self.headers.items()), len(body), self.request_version))
            if made:
                self.passes.append(made)
        self.send_response(status)
        if made:
            self.send_header(admin_pass.PASS_HEADER, made)
        if status == 302:
            self.send_header("Location", "/somewhere")
        self.send_header("Content-Length", "0")
        self.end_headers()

    do_GET = do_POST = do_PUT = do_DELETE = handle_any

    def log_message(self, *args):
        pass


def stub(name, base=Door):
    cls = type("Stub_" + name, (base,), {"seen": [], "raw": [], "passes": [], "lock": threading.Lock(), "name": name})
    for attempt in range(20):
        port = maps.free_port()
        try:
            server = quick_http.QuickThreadingHTTPServer(("127.0.0.1", port), cls)
        except OSError:                                                                        # (another test took the port between the choice and the bind: choose again)
            if attempt == 19:
                raise
            continue
        break
    threading.Thread(target=lambda: server.serve_forever(poll_interval=0.05), daemon=True).start()          # (shutdown() waits for one poll: the default half second, three times for each rig, is most of a rig's life)
    return cls, server, port


class Rig:
    def __init__(self, trusted="", relaxed=True, log=False, real_ports=None, own=None, hub_port=None):
        """`real_ports` = (submission door, replay door[, test-map door]) of a running ants_server: the doors are then not the stand-ins of the rig but the real program (test_nginx_big_maps.py, test_nginx_test_maps.py).
        `trusted` is the text of the image's list of reverse proxies (docker/trusted-proxies.conf; nothing by default: tests/scripts/test_nginx_trusted_proxies.py runs the real file). `own` is an operator's list
        in the folder that the stack files mount ANTS_TRUSTED_PROXIES_FILE into: its text, DEVNULL (the stack's default mount) or None (no file). `hub_port` = the port of a running chat hub (/hub/ws, the pages of the login,
        and the gate of the admin's door). Without it the rig has a stand-in gate of its own (`rig.gate`, class Gate): a request of /admin/api/ must carry `ADMIN` (rig.admin(...) adds it) to be passed on."""
        self.work = tempfile.mkdtemp(prefix="ants_nginx_submit_")
        for sub in ("srv/previews", "srv/files", "cache/ants_stats", "cache/ants_history", "tmp", "html", "trusted", "trusted.d"):
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
        if own is DEVNULL:                                                                       # (a mount of /dev/null is a file that holds nothing: here a link to it, which nginx reads the same way)
            os.symlink(os.devnull, os.path.join(self.work, "trusted.d", "own.conf"))
        elif own is not None:
            with open(os.path.join(self.work, "trusted.d", "own.conf"), "w") as f:
                f.write(own)
        if hub_port is None:
            self.gate, self.gate_server, self.gate_port = stub("gate", Gate)
        else:
            self.gate = self.gate_server = self.gate_port = None
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
        text = text.replace("/etc/nginx/ants-trusted-proxies/", self.work + "/trusted/").replace("/etc/nginx/ants-trusted-proxies.d/", self.work + "/trusted.d/")
        if hub_port is not None:
            text = text.replace("set $ants_hub ants-hub:4040;", "set $ants_hub 127.0.0.1:%d;" % hub_port)
        else:                                                                                    # (the gate's block only: the other doors of the hub stay as they were, a hub that is not there)
            at = text.index("location = /hub/admin/gate {")
            text = text[:at] + text[at:].replace("set $ants_hub ants-hub:4040;", "set $ants_hub 127.0.0.1:%d;" % self.gate_port, 1)
        text = text.replace("set $ants_submit ants-server:4030;", "set $ants_submit 127.0.0.1:%d;" % self.door_port)
        text = text.replace("set $ants_test_maps ants-server:4031;", "set $ants_test_maps 127.0.0.1:%d;" % self.test_door_port)
        text = text.replace("set $ants_replays ants-server:4020;", "set $ants_replays 127.0.0.1:%d;" % self.replay_port)
        text = text.replace("set $ants_replays ants-server:4002;", "set $ants_replays 127.0.0.1:%d;" % self.replay_port)
        text = text.replace("set $ants_server ants-server:4002;", "set $ants_server 127.0.0.1:%d;" % self.replay_port)
        text = text.replace("listen [::]:80;", "")
        if relaxed:                                                                              # (the tests that send many requests are not the flood; the test of the allowance is)
            text = re.sub(r"(zone=ants_submit_post:1m rate=)10r/m", r"\g<1>100000r/s", text)
            text = re.sub(r"(zone=ants_submit_read:1m rate=)10r/s", r"\g<1>100000r/s", text)
            text = re.sub(r"(zone=ants_test_maps:1m rate=)10r/m", r"\g<1>100000r/s", text)
            text = re.sub(r"(zone=ants_admin_api:1m rate=)1r/s", r"\g<1>100000r/s", text)
        text = text.replace("/srv/community-maps/", self.work + "/srv/").replace("/var/cache/nginx/", self.work + "/cache/")
        self.log_file = os.path.join(self.work, "access.log")
        text = text.replace("access_log /dev/stdout ants_noquery;", ("access_log %s ants_noquery;" % self.log_file) if log else "access_log off;")          # (the image logs to its output)
        user = "user root;\n" if hasattr(os, "geteuid") and os.geteuid() == 0 else ""
        full = user + "pid %s/nginx.pid;\nerror_log %s/error.log;\nevents { worker_connections 256; }\nhttp {\n access_log off;\n client_body_temp_path %s/tmp/body;\n proxy_temp_path %s/tmp/proxy;\n fastcgi_temp_path %s/tmp/fcgi;\n uwsgi_temp_path %s/tmp/uwsgi;\n scgi_temp_path %s/tmp/scgi;\n%s\n}\n" % ((self.work,) * 7 + (text,))

        def write_conf(port):                                                                  # (the port of the site is chosen by maps.start_nginx, which waits for THIS nginx to hold it)
            conf = os.path.join(self.work, "nginx.conf")
            with open(conf, "w") as f:
                f.write(full.replace("listen 80;", "listen 127.0.0.1:%d;" % port))
            return conf
        self.proc, self.port = maps.start_nginx(self.work, write_conf)

    def stop(self):
        self.proc.terminate()
        self.proc.wait(timeout=10)
        for server in (self.door_server, self.replay_server, self.test_door_server, self.gate_server):
            if server is not None:
                server.shutdown()
                server.server_close()
        shutil.rmtree(self.work, ignore_errors=True)

    def ask(self, method, target, headers=None, body=None, source=None):
        """One request; `source` = the address of this machine that the connection comes from (127.0.0.2 is another peer for nginx than 127.0.0.1; a Linux machine has all of 127.0.0.0/8)."""
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=20, source_address=(source, 0) if source else None)
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

    def admin(self, method, target, headers=None, body=None, source=None):
        """A request of /admin/api/ from an admin: the login's cookie of the stand-in gate is added to `headers`."""
        return self.ask(method, target, dict(ADMIN, **(headers or {})), body, source)

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


def seen_after(stub, before, wait=10.0):
    """The newest request that the fake door has recorded once it holds more than `before` of them: poll, with a deadline, instead of reading the list at once (the door writes down what it was
    sent while it is answering, so the list is only ever ahead of the answer, but a read that does not wait is a read of whatever happened to be there)."""
    deadline = time.monotonic() + wait
    while True:
        with stub.lock:
            if len(stub.seen) > before:
                return stub.seen[-1]
        if time.monotonic() >= deadline:
            raise AssertionError("the door has recorded %d requests, %d were expected within %.0f s" % (len(stub.seen), before + 1, wait))
        time.sleep(0.02)


def log_lines(rig, count, wait=10.0):
    """The lines of the access log once there are at least `count` of them (the log is written when the answer has gone: wait for the last line, not for the first)."""
    deadline = time.monotonic() + wait
    while True:
        try:
            with open(rig.log_file, encoding="utf-8") as f:
                lines = f.read().splitlines()
        except OSError:
            lines = []
        if len(lines) >= count or time.monotonic() >= deadline:
            return lines
        time.sleep(0.02)


def if_bodies(block):
    """What each `if (...) { ... }` of a block does, as a list of its directives: the one return of an if on a single line, or the lines of an if that has a block of its own."""
    out, lines, at = [], [l.strip() for l in block.splitlines()], 0
    while at < len(lines):
        line = lines[at]
        at += 1
        if not line.startswith("if ("):
            continue
        if line.endswith("}"):
            out.append([re.search(r"\{ (return [^}]*) \}$", line).group(1)])
        else:
            body = []
            while lines[at] != "}":
                body.append(lines[at])
                at += 1
            out.append(body)
    return out


class TheText(unittest.TestCase):
    def test_every_new_location_is_there_once_and_nothing_else_can_catch_the_paths(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        mine = [l for l in locations if "admin" in l or "submi" in l or "approved" in l]
        self.assertEqual(sorted(mine), sorted(["= /submissions", "^~ /submissions/", "@submit_off", "= /admin", "= /admin.html", "= /admin/", "^~ /admin/api/", "= /hub/admin/gate", "@admin_not_signed_in", "@admin_not_allowed",
                                               "@admin_door_off", "^~ /admin/", "@approved_files", "^~ /hub/admin/api/", "= /hub/admin/api"]))
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
        self.assertNotIn("login_off", CONF)                                                               # (no helper: nothing else answers for a sign-in)
        for gone in ("/admin/login", "/admin/callback", "/admin/logout", "ants-admin-login", "admin_login", "admin_cookie", "ants_admin="):          # (the game server's own sign-in and its cookie are gone)
            self.assertNotIn(gone, CONF)
        # the size of a map is decided from the raw query exactly as the game server reads it, and the small limit is the Classic one
        head = CONF[:CONF.index("server {")]
        self.assertIn("map $args $submit_extended", head)
        self.assertNotIn("$arg_kind", head)
        self.assertIn("52428[0-8]", head)

    def test_the_zones_are_outside_the_server(self):
        head = CONF[:CONF.index("server {")]
        for zone, rate in (("ants_submit_post", "10r/m"), ("ants_submit_read", "10r/s"), ("ants_admin_api", "1r/s")):
            self.assertRegex(head, r"limit_req_zone \$[a-z_]+ zone=%s:1m rate=%s;" % (zone, rate))
        for zone in ("ants_submit_post", "ants_submit_read", "ants_admin_api"):
            self.assertIn("zone=" + zone + " ", CONF[CONF.index("server {"):])
        self.assertIn("map $request_method $submit_post_key", head)                     # (only a POST counts against the allowance of sending)
        self.assertIn("POST $binary_remote_addr;", head)
        self.assertNotIn("ants_admin_login", CONF)                                         # (the game server's sign-in has no allowance any more: there is no sign-in)

    def test_only_return_is_used_inside_an_if_and_the_checks_come_before_the_pass(self):
        for header in ("location = /submissions ", "location ^~ /submissions/ ", "location = /admin ", "location ^~ /admin/api/ ", "location @approved_files "):
            block = block_of(CONF, header)
            for body in if_bodies(block):
                self.assertRegex(body[-1], r"return \d{3}(?: '(?:[^'\\]|\\.)*')?;\Z", header)                    # (a return, and before it only the headers of its answer)
                for directive in body[:-1]:
                    self.assertTrue(directive.startswith("add_header "), header + directive)
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
            self.assertIn("proxy_hide_header Set-Cookie;", block, header)                              # (no call sets a cookie: the admin's door is the hub's login)
        self.assertEqual(re.findall(r"proxy_set_header\s+(\S+)", block_of(CONF, "location ^~ /admin/api/ ")),
                         ["Connection", "Host", "Content-Type", "X-Ants-Admin-Pass", "X-Ants-Csrf", "Sec-Fetch-Site", "X-Client-Address"])
        self.assertEqual(re.findall(r"proxy_set_header\s+(\S+)", block_of(CONF, "location = /hub/admin/gate ")), ["Connection", "Host", "Cookie", "X-Client-Address", "Content-Length"])
        self.assertEqual(re.findall(r"proxy_set_header\s+(\S+)", block_of(CONF, "location = /submissions ")),
                         ["Connection", "Host", "Content-Type", "Authorization", "Sec-Fetch-Site", "X-Client-Address"])
        self.assertNotIn("proxy_set_header Cookie", block_of(CONF, "location = /submissions "))           # (no cookie goes to the game server at all: the hub's login is shown to the hub's gate only)
        self.assertNotIn("proxy_set_header Cookie", block_of(CONF, "location ^~ /admin/api/ "))
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
        self.assertIn("$ants_path", fmt)                                                      # (the map above it cuts $request_uri at the ?)
        self.assertRegex(head, r'map \$request_uri \$ants_path \{\s+default \$request_uri;\s+"~\^\(\?<ants_p>\[\^\?\]\*\)" \$ants_p;\s+\}')
        for query in ("$request ", "$request\"", "$request_uri", "$args", "$query_string", "$is_args", "$uri", "$http_referer", "$http_cookie", "$http_authorization"):
            self.assertNotIn(query, fmt)
        for header in ("location = /submissions ", "location ^~ /submissions/ ", "location ^~ /admin/api/ "):
            self.assertIn("access_log /dev/stdout ants_noquery;", block_of(CONF, header), header)
        self.assertIn("access_log /dev/stdout ants_noquery;", block_of(CONF, "location = /test-maps "))          # (the name of a test map is in the query: tests/scripts/test_nginx_test_maps.py)
        self.assertEqual(CONF.count("access_log /dev/stdout ants_noquery;"), 5)             # (these four, and the server block, which gives every other path the same format: tests/scripts/test_nginx_headers.py)
        self.assertNotIn("$request_body", RAW)                                                # (nothing of a body is written down: the access log holds the request line only)

    def test_the_stacks_the_image_and_the_documents_agree_with_the_file(self):
        for name in ("docker-compose.stack.yml",):
            text = "\n".join(l for l in open(os.path.join(REPO, name), encoding="utf-8").read().splitlines() if not l.lstrip().startswith("#"))
            self.assertIn('"--submit-port", "${ANTS_SUBMIT_PORT:-0}"', text, name)                  # (off until the stack says so)
            self.assertNotIn("ants-admin-login", text, name)                                       # (no helper container: the hub's gate is the admin's door)
            self.assertNotIn("DISCORD", re.sub(r"ANTS_HUB_DISCORD_(?:CLIENT_ID|CLIENT_SECRET|BOT_TOKEN|GUILD)", "", text), name)       # (the hub's login and roles settings are the hub's: test_hub_stack.py)
            self.assertNotIn("ANTS_PUBLIC_URL", text, name)
        self.assertIn("4030", RAW)
        self.assertIn("--submit-any-interface", open(os.path.join(REPO, "Dockerfile.server"), encoding="utf-8").read())
        dockerfile = open(os.path.join(REPO, "Dockerfile"), encoding="utf-8").read()
        for name in ("admin.css", "admin_page.js"):
            self.assertIn("COPY web/%s /usr/share/nginx/html/%s" % (name, name), dockerfile)
        self.assertIn("COPY --from=builder /src/web/admin.html /usr/share/nginx/html/admin.html", dockerfile)           # (the page with the bar's stamps, as the builder made it)
        self.assertIn("COPY docker/trusted-proxies.conf /etc/nginx/ants-trusted-proxies/proxies.conf", dockerfile)


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
        status, headers, body = self.rig.ask("GET", "/mapmaker")
        self.assertEqual(status, 200)
        self.assertIn(b"Map maker", body)
        old = self.rig.ask("GET", "/mapmaker.html?x=1")                                    # (the old address is a 302 to the clean one, with the query)
        self.assertEqual(old[0], 302)
        self.assertEqual(names(old[1], "location"), ["/mapmaker?x=1"])
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
        self.assertEqual(self.rig.ask("HEAD", "/mapmaker")[0], 200)
        status, headers, body = self.rig.ask("GET", "/front/mapmaker_app.js")
        self.assertEqual(status, 200)
        self.assertEqual(names(headers, "cache-control"), ["no-cache, must-revalidate"])                 # (a script keeps the headers it had)
        self.assertEqual(names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(names(headers, "x-frame-options"), [])

    def test_the_admin_api_passes_the_token_the_pass_and_the_address_and_nothing_else_of_the_visitor_and_no_cookie_either_way(self):
        status, headers, body = self.rig.admin("POST", "/admin/api/maps/%s/approve" % ID, {"X-Ants-Csrf": "tok", "Content-Type": "application/json", "Authorization": "Bearer secret",
                                                                                    "X-Client-Address": "6.6.6.6", "X-Forwarded-For": "7.7.7.7", "Referer": "http://evil.example/", "User-Agent": "agent/1",
                                                                                    "Sec-Fetch-Site": "same-origin", "X-Real-IP": "8.8.8.8", "Cookie": ADMIN["Cookie"] + "; ants_admin=abc; other=1"}, b"{}")
        self.assertEqual(status, 201)
        self.assertEqual(names(headers, "set-cookie"), [])                                      # (the game server sets one: it never gets through)
        method, path, seen, length = self.last(self.rig.door)
        self.assertEqual((method, path, length), ("POST", "/admin/api/maps/%s/approve" % ID, 2))
        self.assertEqual(seen.get("x-ants-csrf"), "tok")
        self.assertEqual(seen.get("x-client-address"), "127.0.0.1")                           # (the real address of the connection; the visitor's own header is gone)
        self.assertEqual(seen.get("sec-fetch-site"), "same-origin")
        self.assertEqual(sorted(seen), ["connection", "content-length", "content-type", "host", "sec-fetch-site", "x-ants-admin-pass", "x-ants-csrf", "x-client-address"])          # (nothing else of the visitor's, no cookie)
        self.assertEqual(seen.get("connection"), "close")
        self.assertEqual(self.rig.admin("GET", "/admin/api/me")[0], 200)
        self.assertEqual(self.rig.admin("PUT", "/admin/api/me", body=b"")[0], 405)
        self.assertEqual(self.rig.admin("DELETE", "/admin/api/maps")[0], 405)
        self.assertEqual(self.rig.admin("POST", "/admin/api/maps/x/approve", {"Content-Type": "application/json"}, b"x" * 9000)[0], 413)
        before = self.count(self.rig.door)
        self.assertEqual(self.rig.admin("POST", "/admin/api/maps/x/approve", {"Content-Type": "application/json"}, b"x" * 9000)[0], 413)
        self.assertEqual(self.count(self.rig.door), before)                                    # (a body that is too large never reaches the game server)

    def test_a_player_sends_a_map_with_the_token_in_the_authorization_header_and_the_cookie_does_not_go_along(self):
        status, headers, body = self.rig.ask("POST", "/submissions?kind=classic&name=Abc&maker=Mia", {"Authorization": "Bearer tok", "Cookie": "session=abc", "Content-Type": "application/octet-stream",
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
        for target in ("/admin/api/maps/%41%42/approve", "/admin/api/./maps", "/admin/api/maps//x"):          # (decoded and cleaned up by nginx: the game server sees the clean path)
            self.assertIn(rig.admin("GET", target)[0], (200, 404), target)
        method, path, seen, length = self.last(rig.door)
        self.assertEqual(path, "/admin/api/maps/x")                                                       # (the last of them: "//" is one "/")
        rig.admin("GET", "/admin/api/maps/%41%42/approve")
        self.assertEqual(self.last(rig.door)[1], "/admin/api/maps/AB/approve")
        after = self.count(rig.door)
        for target in ("/admin/api/a%20b", "/admin/api/a%0d%0ab", "/admin/api/a%00b", "/admin/api/a.b", "/admin/api/maps?state=x%0d%0a", "/admin/api/" + "a" * 130, "/admin/api/%2e%2e/x"):
            status = rig.admin("GET", target)[0]
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


    def test_the_admin_api_has_an_allowance_for_each_address_and_the_gate_is_asked_only_for_what_passes_it(self):
        rig = Rig(relaxed=False, trusted="set_real_ip_from 127.0.0.1;\nreal_ip_header X-Forwarded-For;\n")                # (the visitors are told apart by X-Forwarded-For, as a proxy of the stack tells them)
        try:
            results = [rig.admin("GET", "/admin/api/me", {"X-Forwarded-For": "198.51.100.20"})[0] for _ in range(70)]
            self.assertEqual(results[:51], [200] * 51)                                             # (one a second with a burst of fifty: the fifty-first in a row still passes ...)
            self.assertLessEqual(results.count(200), 53)                                          # ... and then one more for each second that went by
            self.assertEqual(set(results[51:]) - {200}, {429})                                      # (429: the hub's own 503 and nginx's 429 are not the same words)
            self.assertEqual(results[-1], 429)
            self.assertEqual(len(rig.gate.seen), results.count(200))                                # (the gate is asked for the requests that passed the allowance, and for no other)
            self.assertEqual(len(rig.door.seen), results.count(200))
            self.assertEqual(rig.admin("GET", "/admin/api/me", {"X-Forwarded-For": "198.51.100.21"})[0], 200)       # (another address has its own: the first one's flood does not touch it)
            self.assertEqual(dict((k.lower(), v) for k, v in rig.gate.seen[-1][2]).get("x-client-address"), "198.51.100.21")
            self.assertEqual(rig.admin("POST", "/admin/api/maps/x/approve", {"X-Forwarded-For": "198.51.100.22", "Content-Type": "application/json"}, b"{}")[0], 201)
            self.assertEqual(rig.ask("GET", "/submissions", {"X-Forwarded-For": "198.51.100.20", "Authorization": "Bearer t"})[0], 200)       # (the players' doors have their own zones)
        finally:
            rig.stop()


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the real-address rule of docker/nginx.conf was NOT run (tests/scripts/test_nginx_submissions.py)")
class TheRealAddress(unittest.TestCase):
    def test_the_address_is_the_connections_unless_the_proxy_is_named_and_then_the_one_it_saw(self):
        plain = Rig()
        try:
            plain.admin("GET", "/admin/api/me", {"X-Forwarded-For": "198.51.100.7"})
            self.assertEqual(seen_after(plain.door, 0)[2].get("x-client-address"), "127.0.0.1")        # (nothing is named: a header that anyone can write is not believed)
            self.assertEqual(dict((k.lower(), v) for k, v in seen_after(plain.gate, 0)[2]).get("x-client-address"), "127.0.0.1")        # (the gate is told the same address)
        finally:
            plain.stop()
        proxied = Rig(trusted="set_real_ip_from 127.0.0.1;\nreal_ip_header X-Forwarded-For;\nreal_ip_recursive on;\n")
        try:
            proxied.admin("GET", "/admin/api/me", {"X-Forwarded-For": "198.51.100.7"})
            self.assertEqual(seen_after(proxied.door, 0)[2].get("x-client-address"), "198.51.100.7")
            self.assertEqual(dict((k.lower(), v) for k, v in seen_after(proxied.gate, 0)[2]).get("x-client-address"), "198.51.100.7")
            proxied.admin("GET", "/admin/api/me", {"X-Forwarded-For": "1.2.3.4, 198.51.100.8"})     # (a visitor who put his own address first: the proxy's own entry is the last one)
            self.assertEqual(seen_after(proxied.door, 1)[2].get("x-client-address"), "198.51.100.8")
            self.assertEqual(dict((k.lower(), v) for k, v in seen_after(proxied.gate, 1)[2]).get("x-client-address"), "198.51.100.8")
            proxied.ask("POST", "/submissions?kind=classic&name=Abc&maker=Mia", {"X-Forwarded-For": "198.51.100.9", "X-Client-Address": "6.6.6.6", "Content-Type": "application/octet-stream"}, b"m")
            self.assertEqual(seen_after(proxied.door, 2)[2].get("x-client-address"), "198.51.100.9")
        finally:
            proxied.stop()


def lowered(raw):
    """The headers of a request as the stand-in recorded them (a list of lines), as a dict with lower-case names."""
    return dict((k.lower(), v) for k, v in raw)


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the admin's door of docker/nginx.conf was NOT run (tests/scripts/test_nginx_submissions.py)")
class TheAdminDoor(unittest.TestCase):
    """/admin/api/ asks the hub's gate (the stand-in `Gate`, which answers by the login cookie) before the game server (the stand-in `Door`) is called, and the answers that nginx makes for the gate's refusals are JSON."""

    @classmethod
    def setUpClass(cls):
        cls.rig = Rig()

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()

    def gate_count(self, rig=None):
        with (rig or self.rig).gate.lock:
            return len((rig or self.rig).gate.seen)

    def door_count(self, rig=None):
        with (rig or self.rig).door.lock:
            return len((rig or self.rig).door.seen)

    def newest(self, stub):
        with stub.lock:
            return stub.seen[-1]

    def says(self, answer, status, error, message):
        """An answer that nginx itself makes: this status, this JSON, the headers of the named answers, and no cookie."""
        code, headers, body = answer
        self.assertEqual(code, status)
        self.assertEqual(json.loads(body), {"error": error, "message": message})
        self.assertEqual(names(headers, "content-type"), ["application/json"])
        self.assertEqual(names(headers, "cache-control"), ["no-store"])
        self.assertEqual(names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(names(headers, "strict-transport-security"), ["max-age=604800"])
        self.assertEqual(names(headers, "set-cookie"), [])

    def test_the_gate_is_asked_once_for_each_request_and_is_shown_the_login_cookie_the_real_address_and_nothing_else(self):
        rig = self.rig
        visitor = {"Cookie": "theme=dark; " + HUB_COOKIE + ADMIN_SESSION + "; ants_admin=old; track=1", "Authorization": "Bearer secret", "Origin": "https://evil.example", "Referer": "http://evil.example/",
                   "User-Agent": "agent/1", "X-Client-Address": "6.6.6.6", "X-Forwarded-For": "7.7.7.7", "X-Real-IP": "8.8.8.8", "X-Ants-Csrf": "tok", "Content-Type": "application/json",
                   "Sec-Fetch-Site": "same-origin", "X-Ants-Admin-Pass": "v1.forged.forged", "Connection": "keep-alive"}
        for method, body in (("GET", None), ("POST", b'{"reason":"a body that the gate must not be sent"}'), ("GET", None)):
            gate_before, door_before = self.gate_count(), self.door_count()
            status = rig.ask(method, "/admin/api/maps/%s/approve?x=1&y=two" % ID, visitor, body)[0]
            self.assertEqual(status, 201 if method == "POST" else 200)
            self.assertEqual((self.gate_count(), self.door_count()), (gate_before + 1, door_before + 1), method)           # (once, and then the game server)
            command, path, raw, length, version = self.newest(rig.gate)
            self.assertEqual((command, path, version, length), ("GET", "/hub/admin/gate", "HTTP/1.1", 0), method)           # (a GET of the gate's own address: no query, no body, whatever the request was)
            self.assertEqual(sorted(k.lower() for k, v in raw), ["connection", "cookie", "host", "x-client-address"], method)        # (no Content-Length, no Transfer-Encoding, no Authorization, no Origin, ...)
            seen = lowered(raw)
            self.assertEqual(seen["cookie"], HUB_COOKIE + ADMIN_SESSION)                              # (the hub's login only: the visitor's other cookies are not passed on)
            self.assertEqual(seen["x-client-address"], "127.0.0.1")                                  # (the connection's address: nothing the visitor wrote, X-Forwarded-For included)
            self.assertEqual(seen["connection"], "close")
        # a cookie that is not of the shape the hub makes is not passed on at all: the gate sees no cookie and says 401
        for cookie in (HUB_COOKIE + ADMIN_SESSION.upper(), HUB_COOKIE + ADMIN_SESSION[:-1], HUB_COOKIE + ADMIN_SESSION + "00", "x" + HUB_COOKIE + ADMIN_SESSION, "ants_hub=" + ADMIN_SESSION):
            before = self.door_count()
            answer = rig.ask("GET", "/admin/api/me", {"Cookie": cookie})
            self.says(answer, 401, "not_signed_in", "Log in with Discord.")
            self.assertNotIn("cookie", lowered(self.newest(rig.gate)[2]), cookie)
            self.assertEqual(self.door_count(), before, cookie)

    def test_a_pass_that_a_visitor_sends_never_reaches_the_game_server_and_the_gates_own_pass_does_byte_for_byte(self):
        rig = self.rig
        forged = admin_pass.make_pass(name="someone-else")                                           # (a pass that the game server would believe: only the gate's own answer may carry one)
        letters = {"X-Ants-Admin-Pass": forged, "x-ants-admin-pass": forged, "X-ANTS-ADMIN-PASS": forged, "X-Ants-Admin-Pass-Extra": forged}
        for method, body in (("GET", None), ("POST", b"{}")):
            status = rig.admin(method, "/admin/api/me", dict(letters, **{"Content-Type": "application/json"}), body)[0]
            self.assertEqual(status, 201 if method == "POST" else 200)
            with rig.door.lock:
                raw = rig.door.raw[-1]
            with rig.gate.lock:
                made = rig.gate.passes[-1]
            self.assertEqual([v for k, v in raw if k.lower() == "x-ants-admin-pass"], [made], method)       # (one line, the gate's pass, and not the visitor's)
            self.assertNotIn(forged, [v for k, v in raw])
            self.assertNotIn("x-ants-admin-pass-extra", [k.lower() for k, v in raw])
        # the visitor's header with a gate that says no: the game server is not called at all
        before = self.door_count()
        for cookie in (None, MOD_SESSION, ENDED_SESSION, BROKEN_SESSION, BUSY_SESSION, REDIRECT_SESSION, UNKNOWN_SESSION, SHUT_SESSION):
            headers = dict(letters, **({"Cookie": HUB_COOKIE + cookie} if cookie else {}))
            for method, body in (("GET", None), ("POST", b"{}")):
                status = rig.ask(method, "/admin/api/maps/x/approve", dict(headers, **{"Content-Type": "application/json"}), body)[0]
                self.assertIn(status, (401, 403, 503), (cookie, method))
        self.assertEqual(self.door_count(), before)

    def test_a_visitor_who_is_not_logged_in_or_not_allowed_is_answered_in_json_and_the_game_server_is_not_called(self):
        rig = self.rig
        before = self.door_count()
        for cookie, status, error, message in ((None, 401, "not_signed_in", "Log in with Discord."), (ENDED_SESSION, 401, "not_signed_in", "Log in with Discord."),
                                               (MOD_SESSION, 403, "not_allowed", "This page is for the site's admins.")):
            headers = {"Cookie": HUB_COOKIE + cookie} if cookie else {}
            self.says(rig.ask("GET", "/admin/api/me", headers), status, error, message)
            self.says(rig.ask("POST", "/admin/api/maps/x/approve", dict(headers, **{"Content-Type": "application/json"}), b'{"a":1}'), status, error, message)
        self.assertEqual(self.door_count(), before)

    def test_a_gate_that_is_in_trouble_is_a_503_the_admin_page_can_read_and_the_game_server_is_not_called(self):
        rig = self.rig
        before = self.door_count()
        for cookie in (BROKEN_SESSION, BUSY_SESSION, REDIRECT_SESSION, UNKNOWN_SESSION, SHUT_SESSION):      # (a 500, a 429, a redirect and a 404 of the gate, and the 503 of a hub whose door is shut: none of them is a yes or a no, and a shut door is never "not an admin")
            for method, body in (("GET", None), ("POST", b"{}")):
                self.says(rig.ask(method, "/admin/api/me", {"Cookie": HUB_COOKIE + cookie, "Content-Type": "application/json"}, body), 503, "door_off", "The admin page is not available right now.")
        self.assertEqual(self.door_count(), before)

    def test_a_gate_that_is_not_there_is_the_same_503(self):
        rig = Rig()
        try:
            rig.gate_server.shutdown()
            rig.gate_server.server_close()
            for method, body in (("GET", None), ("POST", b"{}")):
                answer = rig.admin(method, "/admin/api/me", {"Content-Type": "application/json"}, body)
                self.says(answer, 503, "door_off", "The admin page is not available right now.")
            self.assertEqual(len(rig.door.seen), 0)
            self.assertEqual(rig.ask("GET", "/admin")[0], 200)                                      # (the page is a file: it still opens)
        finally:
            rig.stop()

    def test_what_the_game_server_answers_for_itself_is_passed_on_untouched_whatever_the_status(self):
        rig = self.rig
        for code in (401, 403, 404, 500, 502, 503):
            for method, body in (("GET", None), ("POST", b"{}")):
                status, headers, text = rig.admin(method, "/admin/api/answer-%d" % code, {"Content-Type": "application/json"}, body)
                self.assertEqual(status, code, (code, method))                                       # (a 401 of the game server is not the "log in" of the gate, a 500 is not the door that is off)
                self.assertEqual(json.loads(text), {"from": "game_server", "code": code}, (code, method))
                self.assertEqual(names(headers, "set-cookie"), [])
        self.assertEqual(rig.admin("GET", "/admin/api/nothing-here")[0], 200)                       # (the stand-in answers any other path with a 200)

    def test_a_browser_cannot_ask_the_gate_itself(self):
        rig = self.rig
        gate_before, door_before = self.gate_count(), self.door_count()
        for method in ("GET", "POST", "HEAD", "PUT"):
            for target in ("/hub/admin/gate", "/hub/admin/gate?x=1"):
                for headers in ({}, ADMIN):
                    status, answer, body = rig.ask(method, target, headers, b"" if method in ("POST", "PUT") else None)
                    self.assertEqual(status, 404, (method, target))
                    self.assertEqual(names(answer, "x-ants-admin-pass"), [])
        self.assertEqual(rig.ask("GET", "/hub/admin/gate/", ADMIN)[0] in (200, 301, 302, 404), True)      # (never the gate: whatever else the site answers for an address that it has no door for)
        self.assertEqual(rig.ask("GET", "/hub/admin/gate/", ADMIN)[2].count(b"stub"), 0)
        self.assertEqual((self.gate_count(), self.door_count()), (gate_before, door_before))             # (the stand-in hub and the game server were not asked)

    def test_a_request_of_another_site_is_turned_away_first_and_spends_nothing(self):
        rig = Rig(relaxed=False)
        try:
            for site in ("cross-site", "same-site"):
                for method, body in (("GET", None), ("POST", b"{}")):
                    for headers in ({}, ADMIN, {"Cookie": HUB_COOKIE + MOD_SESSION}):
                        answer = rig.ask(method, "/admin/api/me", dict(headers, **{"Sec-Fetch-Site": site, "Content-Type": "application/json"}), body)
                        self.says(answer, 403, "cross_site", "This request must come from the site's own page.")
            for i in range(120):                                                                    # (more than the whole allowance: none of it is used up by requests of other sites)
                self.assertEqual(rig.ask("GET", "/admin/api/me", dict(ADMIN, **{"Sec-Fetch-Site": ("cross-site", "same-site")[i % 2]}))[0], 403)
            self.assertEqual((len(rig.gate.seen), len(rig.door.seen)), (0, 0))                       # (the gate was not asked, the game server was not called)
            fine = [{"Sec-Fetch-Site": "same-origin"}, {"Sec-Fetch-Site": "none"}, {}]
            results = [rig.admin("GET", "/admin/api/me", fine[i % 3])[0] for i in range(51)]
            self.assertEqual(results, [200] * 51)                                                    # (the allowance is whole: a request of the site itself is let in, a header or none)
            self.assertEqual((len(rig.gate.seen), len(rig.door.seen)), (51, 51))
        finally:
            rig.stop()

    def test_no_cookie_goes_to_the_game_server_and_none_comes_back(self):
        rig = self.rig
        status, headers, body = rig.ask("POST", "/admin/api/maps/%s/approve" % ID, {"Cookie": HUB_COOKIE + ADMIN_SESSION + "; ants_admin=abc; other=1", "Content-Type": "application/json"}, b"{}")
        self.assertEqual(status, 201)
        with rig.door.lock:
            raw = rig.door.raw[-1]
        self.assertNotIn("cookie", [k.lower() for k, v in raw])
        self.assertEqual(names(headers, "set-cookie"), [])                                           # (the stand-in game server sends "Set-Cookie: evil=1")

    def test_a_body_that_is_too_large_is_refused_before_the_gate_is_asked(self):
        rig = self.rig
        gate_before, door_before = self.gate_count(), self.door_count()
        self.assertEqual(rig.admin("POST", "/admin/api/maps/x/approve", {"Content-Type": "application/json"}, b"x" * 9000)[0], 413)
        self.assertEqual(rig.ask_head_only("POST", "/admin/api/maps/x/approve", 9000, ADMIN), 413)         # (said on the head alone: the body is never read)
        self.assertEqual(rig.ask("POST", "/admin/api/maps/x/approve", {"Content-Type": "application/json"}, b"x" * 9000)[0], 413)        # (without the cookie too)
        self.assertEqual((self.gate_count(), self.door_count()), (gate_before, door_before))
        self.assertEqual(rig.admin("POST", "/admin/api/maps/x/approve", {"Content-Type": "application/json"}, b"x" * 8192)[0], 201)       # (8 KB is the limit, and it is let through)
        self.assertEqual((self.gate_count(), self.door_count()), (gate_before + 1, door_before + 1))

    def test_the_methods_the_path_and_the_game_servers_absence_are_checked_as_they_were(self):
        rig = self.rig
        gate_before = self.gate_count()
        self.assertEqual(rig.admin("PUT", "/admin/api/me", body=b"")[0], 405)
        self.assertEqual(rig.admin("DELETE", "/admin/api/me")[0], 405)
        self.assertEqual(rig.admin("HEAD", "/admin/api/me")[0], 405)
        self.assertEqual(rig.admin("GET", "/admin/api/a.b")[0], 404)
        self.assertEqual(self.gate_count(), gate_before)                                             # (a request that nginx turns away for its method or its path does not ask the gate)
        door = Rig()
        try:
            door.door_server.shutdown()
            door.door_server.server_close()
            answer = door.admin("GET", "/admin/api/me")
            self.assertEqual(answer[0], 503)
            self.assertEqual(json.loads(answer[2])["error"], "submissions_off")                       # (the gate said yes and the game server is not there: its own answer)
            self.assertEqual(len(door.gate.seen), 1)
        finally:
            door.stop()


class TheDoorIsDown(unittest.TestCase):
    @unittest.skipUnless(shutil.which("nginx"), "nginx is not installed")
    def test_a_game_server_that_is_not_there_is_a_json_503_never_a_page_of_the_site(self):
        rig = Rig()
        try:
            rig.door_server.shutdown()
            rig.door_server.server_close()
            for method, target, body in (("GET", "/admin/api/me", None), ("POST", "/admin/api/maps/x/approve", b"{}"), ("POST", "/submissions?kind=classic&name=Abc&maker=Mia", b"m"), ("GET", "/submissions", None)):
                status, headers, text = rig.ask(method, target, dict(ADMIN, **({"Content-Type": "application/octet-stream"} if body else {})), body)
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

    def test_a_note_a_name_and_a_maker_are_not_in_the_log_and_the_path_is(self):
        secret = "SecretNote%20from%20a%20player"
        self.assertEqual(self.rig.ask("POST", "/submissions?kind=classic&name=Secret%20Name&maker=Secret%20Maker&note=" + secret, {"Content-Type": "application/octet-stream"}, b"m" * 100)[0], 201)
        self.assertEqual(self.rig.ask("GET", "/submissions?x=QueryOne")[0], 200)
        self.assertEqual(self.rig.ask("GET", "/submissions/%s?x=QueryTwo" % ID)[0], 404)                  # (a query there is a 404: still logged without it)
        self.assertEqual(self.rig.admin("GET", "/admin/api/maps?state=QueryThree")[0], 200)
        self.assertEqual(self.rig.admin("POST", "/admin/api/maps/%s/decline?reason=QueryFour" % ID, {"Content-Type": "application/json"}, b"{}")[0], 201)
        log_lines(self.rig, 5)
        time.sleep(0.3)                                                                                    # (a line that came late would be a sixth: the gate's question is not a line of its own)
        lines = log_lines(self.rig, 5)
        self.assertEqual(len(lines), 5, lines)                                                             # (five requests, five lines)
        text = "\n".join(lines)
        for secret_word in ("Secret", "QueryOne", "QueryTwo", "QueryThree", "QueryFour", "kind=classic", "?"):
            self.assertNotIn(secret_word, text, secret_word)
        self.assertEqual(len(re.findall(r'"POST /submissions HTTP/1\.1" 201 ', text)), 1, text)
        self.assertIn('"GET /submissions HTTP/1.1" 200 ', text)
        self.assertIn('"GET /admin/api/maps HTTP/1.1" 200 ', text)
        self.assertIn('"POST /admin/api/maps/%s/decline HTTP/1.1" 201 ' % ID, text)
        self.assertIn('"GET /submissions/%s HTTP/1.1" 404 ' % ID, text)


if __name__ == "__main__":
    unittest.main()
