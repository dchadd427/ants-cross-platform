#!/usr/bin/env python3
"""docker/nginx.conf run as a whole: the headers of every kind of address, who may open a game connection, what the game server is given, what the access log keeps, and the compressed game files
(run by ./run_tests.sh --fast and by the CI; tests/data/nginx_headers_mutants.json breaks one rule at a time, for tools/mutate.py).

The text of the file is checked everywhere. Where an nginx program is installed the REAL file RUNS (only the listen address, the folders and the names of the other servers are changed) in front of a
stand-in for the game server, its replay and submission doors and the chat hub: one small program that answers every path with a JSON body and a 101 to an upgrade, and remembers the headers of each request.
A second nginx has no server behind it, for the answers of the named locations (a game server that is not there). Skipped, with the reason, where there is no nginx. What is asked of it:

  - HEADERS: a table of every kind of address (the front page, the game page and its addresses, the other pages, the map maker, the admin page, scripts and styles, pictures, sounds, the game files, the
    players' maps, the doors of the game server and the hub, the answers for a server that is not there, a refused connection) and what each sends: the Content-Security-Policy, the rule for framing, the referrer
    policy, the permissions policy, Strict-Transport-Security and the cross-origin headers, none of them twice. The two policies are read apart, directive by directive. The page headers are sent for a document
    only; the cross-origin headers and nosniff are what they always were on every answer that had them.
  - THE ORIGIN OF A GAME CONNECTION: real handshakes at /ws with no Origin, the site's own (http, https, with a port, the host in capital letters, an address in brackets, a fully qualified name with its final dot) and every kind of other (another site, "null",
    the name with something added before or after it, a path, a user name, a second Origin, another scheme, a capital letter, two dots, a dot next to a port): 101 or 403, and the stand-in is asked only when the answer is 101. The chat hub's door
    still passes the Origin on, for the hub to judge.
  - WHAT THE GAME SERVER IS GIVEN: the headers of the handshake and the address nginx sees, and nothing else, whatever the visitor sent (cookies, Authorization, forwarding headers, an address header of the
    visitor's own); /busy is given the Host.
  - THE ACCESS LOG: the path that was asked for and no query, no referrer, no cookie, no Authorization header; the image's stock log gets nothing.
  - THE COMPRESSED FILES: a browser that accepts gzip is given the .gz beside a file as it is (its size, a strong ETag, ranges, 304, the cross-origin headers, no compressing as the request comes); one that does
    not is given the plain file; a file with no .gz is compressed as the request comes. The Dockerfile makes the .gz files of the three big files, once.
  - Any Host name is still served (the site's public names, the staging site and a self-hosted site's name are all different).
"""
import base64
import gzip
import hashlib
import http.client
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import quick_http  # noqa: E402  (the stdlib server asks the network for a name when it binds)
import test_nginx_stats as stats  # noqa: E402  (the helpers that read the file)

RAW = stats.read("docker", "nginx.conf")
CONF = stats.strip_comments(RAW)
HEAD = CONF[:CONF.index("server {")]
DOCKERFILE = stats.read("Dockerfile")

SITE = "beta.playants.org"
HSTS = "max-age=604800"
HSTS_LINE = 'add_header Strict-Transport-Security "max-age=604800" always;'
PERMISSIONS = ("camera=(), microphone=(), geolocation=(), payment=(), usb=(), serial=(), hid=(), midi=(), magnetometer=(), gyroscope=(), accelerometer=(), display-capture=(), interest-cohort=()")
ADMIN_PERMISSIONS = "camera=(), microphone=(), geolocation=(), payment=(), usb=(), interest-cohort=()"
PAGE_HEADERS = ("content-security-policy", "x-frame-options", "referrer-policy", "permissions-policy")
POLICED = PAGE_HEADERS + ("strict-transport-security", "cross-origin-opener-policy", "cross-origin-embedder-policy", "x-content-type-options", "cache-control")


def locations():
    """[(the words after `location`, the block's text)] of the server block, in the order of the file."""
    out = []
    for words in re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M):
        out.append((words, stats.block_of(CONF, "location %s " % words)))
    return out


def server_level():
    """The server block without its locations: the directives that every location without a header of its own inherits."""
    text = CONF[CONF.index("server {"):]
    for words, body in locations():
        text = text.replace("location %s {%s}" % (words, body), "")
    return text


class TheText(unittest.TestCase):
    """What the file says (what it does is run below)."""

    def test_every_block_that_sets_a_header_of_its_own_says_strict_transport_security_once_and_the_server_says_it_too(self):
        # (a location with an add_header of its own does not inherit the server's: the line is repeated in each, and a block that forgets it sends none)
        self.assertEqual(server_level().count(HSTS_LINE), 1)
        checked = 0
        for words, body in locations():
            if "add_header" in body:
                checked += 1
                self.assertEqual(body.count(HSTS_LINE), 1, words)
            else:
                self.assertNotIn("Strict-Transport-Security", body, words)
        self.assertGreaterEqual(checked, 27)

    def test_the_value_is_for_this_host_only(self):
        self.assertEqual(set(re.findall(r'add_header Strict-Transport-Security "([^"]*)"', CONF)), {HSTS})
        for word in ("includeSubDomains", "preload"):
            self.assertNotIn(word, CONF)

    def test_the_two_page_blocks_take_the_headers_of_a_page_from_the_maps_and_the_map_maker_and_the_admin_page_keep_their_own(self):
        for words in ("^~ /asset_catalog/", "~* \\.(html|css|js)$"):
            block = dict(locations())[words]
            for header, variable in (("Content-Security-Policy", "$ants_csp"), ("X-Frame-Options", "$ants_frame_options"), ("Referrer-Policy", "$ants_referrer_policy"), ("Permissions-Policy", "$ants_permissions_policy")):
                self.assertIn("add_header %s %s always;" % (header, variable), block, words)
        maker = stats.block_of(CONF, "location = /mapmaker.html ")
        self.assertIn("add_header Permissions-Policy $ants_permissions_policy always;", maker)
        for header in ("Content-Security-Policy", "X-Frame-Options", "Referrer-Policy"):
            self.assertEqual(len(re.findall(r"add_header %s " % header, maker)), 1, header)
        admin = stats.block_of(CONF, "location = /admin ")
        self.assertIn('add_header Permissions-Policy "%s" always;' % ADMIN_PERMISSIONS, admin)
        # the maps are declared once, outside the server block
        for name in ("ants_page", "ants_csp", "ants_csp_common", "ants_ws_sources", "ants_frame_options", "ants_referrer_policy", "ants_permissions_policy", "ants_ws_origin_ok", "ants_path"):
            self.assertEqual(len(re.findall(r"^map \S+ \$%s \{" % name, HEAD, re.M)), 1, name)

    def test_the_game_door_checks_the_origin_first_and_names_what_it_passes_on(self):
        ws = stats.block_of(CONF, "location = /ws ")
        self.assertTrue(ws.strip().startswith("if ($ants_ws_origin_ok = 0) { return 403; }"))
        self.assertLess(ws.index("$ants_ws_origin_ok"), ws.index("proxy_pass "))
        self.assertIn("proxy_pass_request_headers off;", ws)
        sent = re.findall(r"proxy_set_header (\S+) (.+?);", ws)
        self.assertEqual(sorted(h for h, _ in sent), sorted(["Upgrade", "Connection", "Host", "Sec-WebSocket-Key", "Sec-WebSocket-Version", "Sec-WebSocket-Protocol", "Origin", "X-Client-Address"]))
        self.assertIn(("X-Client-Address", "$remote_addr"), sent)                              # (nginx's own: never the visitor's header of that name)
        self.assertNotRegex(ws, r"(?i)proxy_set_header (Cookie|Authorization|X-Forwarded-For|X-Real-IP|Forwarded)\b")
        self.assertNotIn("$http_cookie", ws)
        busy = stats.block_of(CONF, "location = /busy ")
        self.assertIn("proxy_pass_request_headers off;", busy)
        self.assertEqual(sorted(re.findall(r"proxy_set_header (\S+) ", busy)), ["Connection", "Content-Length", "Host"])
        hub = stats.block_of(CONF, "location = /hub/ws ")
        self.assertNotIn("ants_ws_origin_ok", hub)                                             # (the hub judges the Origin itself, exactly: its own door is as it was)
        self.assertIn("proxy_set_header Origin $http_origin;", hub)

    def test_the_origin_map_is_one_map_of_two_rules(self):
        body = stats.block_of(HEAD, 'map "$http_origin|$host" $ants_ws_origin_ok ')
        rules = re.findall(r'^\s*("~[^"]*"|default)\s+(\S+);', body, re.M)
        self.assertEqual(rules[0], ("default", "0"))
        self.assertEqual([v for _, v in rules], ["0", "1", "1"])
        self.assertEqual(len(rules), 3)

    def test_the_server_logs_every_path_without_its_query(self):
        self.assertEqual(server_level().count("access_log /dev/stdout ants_noquery;"), 1)
        fmt = re.search(r"log_format\s+ants_noquery\s+(.+?);", HEAD, re.S).group(1)
        for needed in ("$remote_addr", "$time_local", "$request_method", "$ants_path", "$status", "$body_bytes_sent"):
            self.assertIn(needed, fmt)
        for forbidden in ("$request ", "$request\"", "$request_uri", "$uri", "$args", "$query_string", "$is_args", "$http_referer", "$http_cookie", "$http_authorization", "$http_x_forwarded_for", "$http_x_client_address"):
            self.assertNotIn(forbidden, fmt)
        self.assertEqual(stats.block_of(HEAD, "map $request_uri $ants_path ").split(), ["default", "$request_uri;", '"~^(?<ants_p>[^?]*)"', "$ants_p;"])

    def test_the_big_game_files_are_compressed_once_by_the_image_and_served_as_they_are(self):
        self.assertEqual(server_level().count("gzip_static on;"), 1)
        self.assertEqual(len(re.findall(r"\bgzip -9 -n -k\b", DOCKERFILE)), 1)
        step = re.search(r"RUN cd build_web/src/ants_app && for f in (.*?); do gzip -9 -n -k \"\$f\" && test -s \"\$f\.gz\" \|\| exit 1; done", DOCKERFILE)
        self.assertIsNotNone(step)
        self.assertEqual(step.group(1).split(), ["index.data", "index.wasm", "index.js"])          # (-k: the plain files stay; test -s: a missing or empty copy stops the build)
        builder, runner = DOCKERFILE.split("FROM nginx:alpine")
        self.assertLess(builder.rindex("cmake --build build_web"), builder.index(step.group(0)))
        self.assertIn("COPY --from=builder /src/build_web/src/ants_app/index.* /usr/share/nginx/html/", runner)       # (index.* takes the .gz files along)
        self.assertNotIn("gzip ", runner.replace("gzip_", ""))                                    # (nothing is compressed in the last stage: its gzip is busybox's, and a file there is changed no more)

    def test_every_host_name_is_served(self):
        self.assertEqual(len(re.findall(r"^\s*server\s*\{", CONF, re.M)), 1)
        self.assertNotIn("default_server", CONF)                                                   # (the public names, the staging site and a self-hosted name differ, and the health check asks for 127.0.0.1)

    def test_the_documents_name_what_is_built(self):
        text = stats.read("docs", "BUILD_AND_RUN.md")
        for needle in ("Strict-Transport-Security", "ants_noquery", "gzip_static", "test_nginx_headers.py"):
            self.assertIn(needle, text, needle)
        self.assertIn("`Strict-Transport-Security: %s`" % HSTS, text)                              # (the time that the file sends is the one that the document names)


# ------------------------------------------------------------------------------------------------------------------------------------------------------------ the run
def nginx_binary():
    return shutil.which("nginx") or ("/usr/sbin/nginx" if os.path.exists("/usr/sbin/nginx") else "")


MIME_FILES = ("/etc/nginx/mime.types", "/usr/local/etc/nginx/mime.types", "/opt/homebrew/etc/nginx/mime.types", "/usr/local/nginx/conf/mime.types")
FEW_TYPES = "types { text/html html; text/css css; text/javascript js; image/png png; image/webp webp; image/x-icon ico; audio/x-wav wav; application/json json; application/wasm wasm; }"


def mime_line():
    found = next((p for p in MIME_FILES if os.path.isfile(p)), "")
    return ("include %s;" % found) if found else FEW_TYPES


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class StandHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def answer(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length:
            self.rfile.read(length)
        with self.server.lock:
            self.server.seen.append((self.command, self.path, [(k.lower(), v) for k, v in self.headers.items()]))
        if self.headers.get("Upgrade", "").lower() == "websocket":
            accept = base64.b64encode(hashlib.sha1((self.headers.get("Sec-WebSocket-Key", "") + GUID).encode()).digest()).decode()
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", accept)
            self.end_headers()
            self.close_connection = True
            return
        body = b'{"stub":true}'
        self.send_response(200)                                # (what the real doors add to every answer: the type, no-store and nosniff, which nginx must not send twice)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    do_GET = do_POST = do_HEAD = answer


class Stand(quick_http.QuickThreadingHTTPServer):
    allow_reuse_address = True

    def __init__(self):
        self.seen, self.lock = [], threading.Lock()
        super().__init__(("127.0.0.1", 0), StandHandler)
        threading.Thread(target=self.serve_forever, daemon=True).start()


class Answer:
    def __init__(self, status, headers, body):
        self.status, self.headers, self.body = status, headers, body

    def get(self, name):
        return next((v for k, v in self.headers if k == name), None)

    def names(self):
        return [k for k, _ in self.headers]


def fetch(port, path, method="GET", headers=(), host=SITE, body=None):
    """One request, with the headers exactly as given (a name may come twice); the answer's headers keep their order and their repeats."""
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    try:
        conn.putrequest(method, path, skip_host=True, skip_accept_encoding=True)
        conn.putheader("Host", host)
        for name, value in headers:
            conn.putheader(name, value)
        if method == "POST":
            conn.putheader("Content-Length", str(len(body or b"")))
        conn.endheaders(body if method == "POST" else None)
        r = conn.getresponse()
        data = r.read() if method != "HEAD" else b""
        return Answer(r.status, [(k.lower(), v) for k, v in r.getheaders()], data)
    finally:
        conn.close()


def handshake(port, host=SITE, origins=(), path="/ws", extra=()):
    """A WebSocket handshake by hand: (the status, the headers). Every Origin in `origins` is sent, one header each."""
    key = base64.b64encode(os.urandom(16)).decode()
    lines = ["GET %s HTTP/1.1" % path, "Host: " + host, "Upgrade: websocket", "Connection: Upgrade", "Sec-WebSocket-Key: " + key, "Sec-WebSocket-Version: 13"]
    lines += ["Origin: " + o for o in origins] + ["%s: %s" % kv for kv in extra]
    with socket.create_connection(("127.0.0.1", port), timeout=10) as s:
        s.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("latin-1"))
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    head = data.partition(b"\r\n\r\n")[0].decode("latin-1").split("\r\n")
    headers = [(h.partition(":")[0].strip().lower(), h.partition(":")[2].strip()) for h in head[1:]]
    return int(head[0].split()[1]), headers, key


def parse_csp(text):
    out = {}
    for part in text.split(";"):
        words = part.split()
        if words:
            assert words[0] not in out, "a directive twice: " + words[0]
            out[words[0]] = sorted(words[1:])
    return out


def common_policy(host):
    sockets = ["ws://" + host, "wss://" + host] if host else []
    return {"default-src": ["'self'"], "style-src": sorted(["'self'", "'unsafe-inline'"]), "img-src": sorted(["'self'", "data:", "blob:", "https://cdn.buymeacoffee.com"]), "font-src": ["'self'"],
            "media-src": sorted(["'self'", "blob:", "data:"]), "connect-src": sorted(["'self'"] + sockets), "worker-src": sorted(["'self'", "blob:"]), "frame-src": ["'self'"],
            "object-src": ["'none'"], "base-uri": ["'none'"], "form-action": ["'self'"]}


def page_policy(host):
    return dict(common_policy(host), **{"script-src": sorted(["'self'", "'unsafe-inline'"]), "frame-ancestors": ["'none'"]})


def game_policy(host):
    return dict(common_policy(host), **{"script-src": sorted(["'self'", "'unsafe-inline'", "'wasm-unsafe-eval'", "'unsafe-eval'"]), "frame-ancestors": ["'self'"]})


def csp_is(want):
    return lambda value: value is not None and parse_csp(value) == want


def expected(kind, host=SITE):
    """The headers a kind of address must send (None: must not be sent at all; a function: tested on the value)."""
    cross = {"cross-origin-opener-policy": "same-origin", "cross-origin-embedder-policy": "require-corp", "x-content-type-options": "nosniff", "strict-transport-security": HSTS}
    no_page = {name: None for name in PAGE_HEADERS}
    if kind == "page":
        return dict(cross, **{"content-security-policy": csp_is(page_policy(host)), "x-frame-options": "DENY", "referrer-policy": "same-origin", "permissions-policy": PERMISSIONS, "cache-control": "no-cache, must-revalidate"})
    if kind == "game":
        return dict(cross, **{"content-security-policy": csp_is(game_policy(host)), "x-frame-options": "SAMEORIGIN", "referrer-policy": "same-origin", "permissions-policy": PERMISSIONS, "cache-control": "no-cache, must-revalidate"})
    if kind == "mapmaker":
        return dict(cross, **{"content-security-policy": lambda v: v is not None and parse_csp(v).get("frame-ancestors") == ["'none'"] and "'unsafe-inline'" not in parse_csp(v)["script-src"] and "'unsafe-eval'" not in parse_csp(v)["script-src"],
                              "x-frame-options": "DENY", "referrer-policy": "no-referrer", "permissions-policy": PERMISSIONS, "cache-control": "no-cache, must-revalidate"})
    if kind == "admin":
        return dict(cross, **{"content-security-policy": lambda v: v is not None and parse_csp(v).get("default-src") == ["'none'"] and parse_csp(v).get("frame-ancestors") == ["'none'"],
                              "x-frame-options": "DENY", "referrer-policy": "no-referrer", "permissions-policy": ADMIN_PERMISSIONS, "cache-control": "no-store"})
    if kind in ("plain", "door"):
        return dict(cross, **no_page)
    if kind == "open":                                       # (the sprites and sounds that any site may use: their own cross-origin headers, none of the page's)
        return dict(no_page, **{"cross-origin-resource-policy": "cross-origin", "access-control-allow-origin": "*", "x-content-type-options": "nosniff", "strict-transport-security": HSTS,
                                "cross-origin-opener-policy": None, "cross-origin-embedder-policy": None})
    if kind == "named":                                      # (the JSON answers for a server that is not there)
        return dict(no_page, **{"x-content-type-options": "nosniff", "strict-transport-security": HSTS, "cache-control": "no-store", "cross-origin-opener-policy": None, "cross-origin-embedder-policy": None})
    if kind == "hubpage":                                    # (the small pages of the login)
        return {"content-security-policy": lambda v: v is not None and v.startswith("default-src 'none'") and "frame-ancestors 'none'" in v, "x-frame-options": None, "referrer-policy": "no-referrer",
                "permissions-policy": None, "x-content-type-options": "nosniff", "strict-transport-security": HSTS, "cache-control": "no-store"}
    raise AssertionError(kind)


HEXID = "a" * 32
SAME = (("Sec-Fetch-Site", "same-origin"),)
# (what it is, method, address, request headers, the status, the kind) for the nginx with a stand-in behind it
LIVE_ROWS = [
    ("the front page", "GET", "/", (), 200, "page"),
    ("the front page, a room's link", "GET", "/?room=k7m2xq", (), 200, "page"),
    ("the front page's file", "GET", "/lobby.html", (), 200, "page"),
    ("the old front page's address", "GET", "/four.html?room=k7m2xq", (), 301, "plain"),
    ("the game page", "GET", "/play.html", (), 200, "game"),
    ("the game page's file", "GET", "/index.html", (), 200, "game"),
    ("a joined game", "GET", "/?join=/ws&room=abc&name=Pebble", (), 200, "game"),
    ("the replay in its frame", "GET", "/play.html?replay=ants-X-20260101-000000Z.antsrep&embed=1", (), 200, "game"),
    ("an address that is nothing", "GET", "/no/such/page", (), 200, "game"),
    ("the list of matches", "GET", "/matches.html?q=ann", (), 200, "page"),
    ("a match", "GET", "/match.html?id=x", (), 200, "page"),
    ("the matches that run now", "GET", "/live.html", (), 200, "page"),
    ("the legal page", "GET", "/legal.html", (), 200, "page"),
    ("the changelog page", "GET", "/changelog.html", (), 200, "page"),
    ("a page that is not there", "GET", "/nothing.html", (), 404, "page"),
    ("the asset catalog", "GET", "/asset_catalog/", (), 200, "page"),
    ("the asset catalog's file", "GET", "/asset_catalog/index.html", (), 200, "page"),
    ("the asset catalog's data script", "GET", "/asset_catalog/catalog_data.js", (), 200, "plain"),
    ("the map maker", "GET", "/mapmaker.html", (), 200, "mapmaker"),
    ("the admin page", "GET", "/admin", (), 200, "admin"),
    ("the admin page's file", "GET", "/admin.html", (), 404, "plain"),
    ("the admin page with a slash", "GET", "/admin/", (), 301, "plain"),
    ("a style sheet", "GET", "/front/classic.css", (), 200, "plain"),
    ("a script", "GET", "/shapes.js", (), 200, "plain"),
    ("the home screen's script", "GET", "/home/home.js", (), 200, "plain"),
    ("a picture", "GET", "/front/logo.png", (), 200, "plain"),
    ("the favicon", "GET", "/favicon.png", (), 200, "plain"),
    ("the old favicon", "GET", "/favicon.ico", (), 200, "plain"),
    ("a sound", "GET", "/front/click.wav", (), 200, "plain"),
    ("the game's code", "GET", "/index.wasm", (), 200, "plain"),
    ("the game's data", "GET", "/index.data", (), 200, "plain"),
    ("the game's script", "GET", "/index.js", (), 200, "plain"),
    ("the map core", "GET", "/lvl_core.wasm", (), 200, "plain"),
    ("the map maker's atlas", "GET", "/mapmaker_atlas/atlas.json", (), 200, "plain"),
    ("the map maker's sheet", "GET", "/mapmaker_atlas/ground.png", (), 200, "plain"),
    ("a player's map picture", "GET", "/maps/previews/a.webp", (), 200, "plain"),
    ("a player's map file", "GET", "/maps/files/A.LVL", (), 200, "plain"),
    ("an approved map that the folder lacks", "GET", "/maps/files/Gone.LVL", (), 200, "plain"),
    ("a folder under maps", "GET", "/maps/", (), 404, "plain"),
    ("sprites", "GET", "/sprites/s1.png", (), 200, "open"),
    ("sounds", "GET", "/sounds/a.wav", (), 200, "open"),
    ("the catalog's sprites", "GET", "/asset_catalog/sprites/s1.png", (), 200, "open"),
    ("the catalog's sounds", "GET", "/asset_catalog/sounds/a.wav", (), 200, "open"),
    ("the numbers", "GET", "/stats", (), 200, "door"),
    ("the status", "GET", "/busy", (), 200, "door"),
    ("a report of a game for one", "POST", "/stats/local", SAME, 200, "door"),
    ("the list of public games", "GET", "/lobbies", (), 200, "door"),
    ("the list of recordings", "GET", "/replays", (), 200, "door"),
    ("a recording", "GET", "/replays/ants-X-20260101-000000Z.antsrep", (), 200, "door"),
    ("the matches that run now (data)", "GET", "/live", (), 200, "door"),
    ("a live match", "GET", "/live/X-20260101-000000Z", (), 200, "door"),
    ("the history", "GET", "/history", (), 200, "door"),
    ("the map of deaths", "GET", "/history/deaths?map=X", (), 200, "door"),
    ("a match of the history", "GET", "/history/ants-X-20260101-000000Z", (), 200, "door"),
    ("the list of maps", "GET", "/maps", (), 200, "door"),
    ("the list of all maps", "GET", "/maps/all", (), 200, "door"),
    ("the submissions", "GET", "/submissions", (), 200, "door"),
    ("one submission", "GET", "/submissions/" + HEXID, (), 200, "door"),
    ("the test maps", "POST", "/test-maps?name=x", (), 200, "door"),
    ("the admin API", "GET", "/admin/api/me", (), 200, "door"),
    ("a path under the admin page", "GET", "/admin/nothing", (), 404, "plain"),
    ("the chat's state", "GET", "/hub/me", SAME, 200, "door"),
    ("the chat's logout", "POST", "/hub/logout", SAME, 200, "door"),
    ("the chat's forget", "POST", "/hub/forget", SAME, 200, "door"),
    ("the login", "GET", "/hub/login", (), 200, "door"),
    ("the login's way back", "GET", "/hub/callback?code=x&state=y", (), 200, "door"),
    ("the game door, refused", "GET", "/ws", (("Origin", "https://other.example"),), 403, "plain"),
]
# the same nginx with nothing behind it: the answers for a server that is not there
DEAD_ROWS = [
    ("the list of public games", "GET", "/lobbies", (), 404, "named"),
    ("the list of recordings", "GET", "/replays", (), 404, "named"),
    ("the history", "GET", "/history", (), 404, "named"),
    ("the list of maps", "GET", "/maps", (), 404, "named"),
    ("the submissions", "GET", "/submissions", (), 503, "named"),
    ("the test maps", "POST", "/test-maps", (), 503, "named"),
    ("the chat's state", "GET", "/hub/me", SAME, 503, "named"),
    ("the login", "GET", "/hub/login", (), 503, "hubpage"),
    ("the login's way back", "GET", "/hub/callback?code=x&state=y", (), 503, "hubpage"),
    ("the login, from a link of another site", "GET", "/hub/login", (("Sec-Fetch-Site", "cross-site"),), 403, "hubpage"),
]


def docroot_files():
    page = "<!doctype html><html><head><title>%s</title></head><body>" + "<p>a stand-in page of the site, long enough to be compressed on its way</p>" * 40 + "</body></html>\n"
    big = (b"ANTS " + bytes(range(32, 120))) * 4000
    files = {name: page % name for name in ("lobby.html", "index.html", "play.html", "matches.html", "match.html", "live.html", "legal.html", "changelog.html", "mapmaker.html", "admin.html", "asset_catalog/index.html")}
    files.update({"front/classic.css": "body { color: #222; }\n" * 80, "front/big.css": ".a { color: red; }\n" * 400, "shapes.js": "var shapes = 1;\n" * 80, "home/home.js": "var home = 1;\n" * 80, "asset_catalog/catalog_data.js": "var data = 1;\n" * 80,
                  "front/logo.png": "\x89PNG stand-in", "favicon.png": "\x89PNG stand-in", "favicon.ico": "ico stand-in", "front/click.wav": "RIFF stand-in", "mapmaker_atlas/atlas.json": '{"v":1}', "mapmaker_atlas/ground.png": "\x89PNG stand-in",
                  "asset_catalog/sprites/s1.png": "\x89PNG stand-in", "asset_catalog/sounds/a.wav": "RIFF stand-in", "lvl_core.wasm": "\0asm stand-in", "index.js": big[:50000], "index.wasm": big[:120000], "index.data": big})
    return files


class TheFileRuns(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not nginx_binary():
            raise unittest.SkipTest("no nginx program on this machine: the file was NOT run (tests/scripts/test_nginx_headers.py)")
        cls.tmp = tempfile.mkdtemp(prefix="ants_headers.")
        cls.procs = []
        try:
            cls.docroot = os.path.join(cls.tmp, "html")
            for name, data in docroot_files().items():
                path = os.path.join(cls.docroot, name)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "wb") as f:
                    f.write(data if isinstance(data, bytes) else data.encode("utf-8"))
            for name in ("index.data", "index.wasm", "index.js"):                  # (what the Dockerfile's step makes: gzip -9 -n -k)
                with open(os.path.join(cls.docroot, name), "rb") as f:
                    raw = f.read()
                with open(os.path.join(cls.docroot, name + ".gz"), "wb") as f:
                    f.write(gzip.compress(raw, 9, mtime=0))
            cls.maps = os.path.join(cls.tmp, "maps")
            for name, data in (("previews/a.webp", b"RIFF stand-in"), ("files/A.LVL", b"level stand-in")):
                os.makedirs(os.path.dirname(os.path.join(cls.maps, name)), exist_ok=True)
                with open(os.path.join(cls.maps, name), "wb") as f:
                    f.write(data)
            cls.stand = Stand()
            cls.live = cls.start("live", cls.stand.server_address[1])
            cls.dead = cls.start("dead", free_port())
            cls.limited = cls.start("limited", cls.stand.server_address[1])                    # (its own allowances: the test that uses up the login's does not spoil the others)
        except BaseException:
            cls.tearDownClass()
            raise

    @classmethod
    def start(cls, name, upstream):
        port = free_port()
        folder = os.path.join(cls.tmp, name)
        for sub in ("tmp", "cache_stats", "cache_history"):
            os.makedirs(os.path.join(folder, sub), exist_ok=True)
        log = os.path.join(folder, "access.log")
        text = RAW
        for old, new, count in (("    listen 80;", "    listen 127.0.0.1:%d;" % port, 1), ("    listen [::]:80;", "", 1)):
            assert text.count(old) == count, old
            text = text.replace(old, new)
        text = text.replace("/usr/share/nginx/html", cls.docroot).replace("/srv/community-maps", cls.maps)
        text = text.replace("/var/cache/nginx/ants_stats", folder + "/cache_stats").replace("/var/cache/nginx/ants_history", folder + "/cache_history")
        text, n = re.subn(r"set \$(\w+) [a-z-]+:\d+;", lambda m: "set $%s 127.0.0.1:%d;" % (m.group(1), upstream), text)
        assert n >= 10, n
        assert text.count("access_log /dev/stdout ants_noquery;") >= 5
        text = text.replace("access_log /dev/stdout ants_noquery;", "access_log %s ants_noquery;" % log).replace("error_log /dev/stderr crit;", "error_log %s/loc.log crit;" % folder)
        with open(os.path.join(folder, "site.conf"), "w", encoding="utf-8") as f:
            f.write(text)
        # the main file is the image's own, as far as these tests see: the stock log, which the server block must replace
        with open(os.path.join(folder, "nginx.conf"), "w", encoding="utf-8") as f:
            f.write("daemon off;\nmaster_process off;\npid %(t)s/nginx.pid;\nerror_log %(t)s/error.log;\nevents { worker_connections 256; }\nhttp {\n  %(mime)s\n  default_type application/octet-stream;\n"
                    "  log_format main '$remote_addr - $remote_user [$time_local] \"$request\" $status $body_bytes_sent \"$http_referer\" \"$http_user_agent\" \"$http_x_forwarded_for\"';\n  access_log %(t)s/stock.log main;\n"
                    "  sendfile on;\n  client_body_temp_path %(t)s/tmp/b;\n  proxy_temp_path %(t)s/tmp/p;\n  fastcgi_temp_path %(t)s/tmp/f;\n  uwsgi_temp_path %(t)s/tmp/u;\n  scgi_temp_path %(t)s/tmp/s;\n  include %(t)s/site.conf;\n}\n" % {"t": folder, "mime": mime_line()})
        proc = subprocess.Popen([nginx_binary(), "-c", os.path.join(folder, "nginx.conf"), "-p", folder], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        cls.procs.append(proc)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
                return type("Front", (), {"port": port, "log": log, "stock": os.path.join(folder, "stock.log")})
            except OSError:
                if proc.poll() is not None:
                    message = proc.stderr.read().decode(errors="replace")
                    if "unknown directive" in message:
                        raise unittest.SkipTest("this nginx lacks a module that the file needs: " + message.strip()[:200])
                    raise AssertionError("nginx would not start: " + message[:600])
                time.sleep(0.05)
        raise AssertionError("nginx did not come up")

    @classmethod
    def tearDownClass(cls):
        for proc in getattr(cls, "procs", []):
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(5)
            except subprocess.TimeoutExpired:
                proc.kill()
            if proc.stderr:
                proc.stderr.close()
        if getattr(cls, "stand", None):
            cls.stand.shutdown()
            cls.stand.server_close()
        shutil.rmtree(getattr(cls, "tmp", ""), ignore_errors=True)

    def check(self, what, answer, kind, host=SITE):
        want = expected(kind, host)
        for name in POLICED:
            self.assertLessEqual(answer.names().count(name), 1, "%s: %s comes %d times" % (what, name, answer.names().count(name)))
        for name, rule in want.items():
            got = answer.get(name)
            if rule is None:
                self.assertIsNone(got, "%s: %s must not be sent (%s)" % (what, name, got))
            elif callable(rule):
                self.assertTrue(rule(got), "%s: %s is wrong (%s)" % (what, name, got))
            else:
                self.assertEqual(got, rule, "%s: %s" % (what, name))

    # ---- headers ----------------------------------------------------------------------------------------------------------------------------------------
    def test_the_table_of_headers_of_every_kind_of_address(self):
        for what, method, path, headers, status, kind in LIVE_ROWS:
            answer = fetch(self.live.port, path, method, headers, body=b"x" if method == "POST" and path.startswith("/test-maps") else b"")
            self.assertEqual(answer.status, status, "%s (%s %s)" % (what, method, path))
            self.check("%s (%s %s)" % (what, method, path), answer, kind)

    def test_the_named_answers_for_a_server_that_is_not_there_carry_theirs(self):
        for what, method, path, headers, status, kind in DEAD_ROWS:
            answer = fetch(self.dead.port, path, method, headers, body=b"x" if method == "POST" else b"")
            self.assertEqual(answer.status, status, "%s (%s %s)" % (what, method, path))
            self.check("%s (%s %s)" % (what, method, path), answer, kind)

    def test_a_login_page_over_the_limit_carries_the_headers_of_the_login_pages(self):
        answers = [fetch(self.limited.port, "/hub/login?next=home", "GET", (("Sec-Fetch-Site", "same-origin"),)) for _ in range(12)]
        over = [a for a in answers if a.status == 429]
        self.assertTrue(over, [a.status for a in answers])
        self.check("the login over its limit", over[0], "hubpage")

    def test_the_policy_of_a_page_is_read_directive_by_directive(self):
        for path in ("/", "/matches.html", "/asset_catalog/", "/legal.html"):
            self.assertEqual(parse_csp(fetch(self.live.port, path).get("content-security-policy")), page_policy(SITE), path)
        for path in ("/play.html", "/index.html", "/?join=/ws&room=x"):
            self.assertEqual(parse_csp(fetch(self.live.port, path).get("content-security-policy")), game_policy(SITE), path)
        page, game = page_policy(SITE), game_policy(SITE)
        for policy in (page, game):
            for sources in policy.values():
                for source in sources:
                    self.assertNotIn(source, ("*", "http:", "https:", "ws:", "wss:"), policy)          # (no wildcard and no whole scheme)
            self.assertNotIn("data:", policy["script-src"] + policy["connect-src"] + policy["frame-src"] + policy["default-src"])
        self.assertEqual([k for k, v in page.items() if "https://cdn.buymeacoffee.com" in v], ["img-src"])         # (the one host that is not the site's: the button's picture)
        self.assertNotIn("'unsafe-eval'", page["script-src"])
        self.assertNotIn("'wasm-unsafe-eval'", page["script-src"])
        self.assertIn("'unsafe-eval'", game["script-src"])
        self.assertIn("'wasm-unsafe-eval'", game["script-src"])
        self.assertEqual(page["frame-ancestors"], ["'none'"])
        self.assertEqual(game["frame-ancestors"], ["'self'"])          # (the match page frames the replay player, which is the game page)

    def test_the_sockets_of_the_policy_are_the_host_that_was_asked_for_and_only_when_it_is_made_of_the_characters_of_a_name(self):
        for host, sockets in (("beta.playants.org", "beta.playants.org"), ("localhost:19980", "localhost:19980"), ("[::1]:8080", "[::1]:8080"), ("Staging.Example.org", "Staging.Example.org"), ("127.0.0.1", "127.0.0.1")):
            got = parse_csp(fetch(self.live.port, "/", host=host).get("content-security-policy"))
            self.assertEqual(got["connect-src"], sorted(["'self'", "ws://" + sockets, "wss://" + sockets]), host)
        for host in ("under_score.example", "quote'.example", "a.example:123456"):
            got = parse_csp(fetch(self.live.port, "/", host=host).get("content-security-policy"))
            self.assertEqual(got["connect-src"], ["'self'"], host)                                        # (nothing is added for a host that is not a name or an address)
            self.assertEqual(len(got), 13, host)                                                          # (and no directive can be added by it)
        refused = []
        for host in ("a.example;frame-ancestors *", "a.example\tb", "a b"):
            try:
                refused.append(fetch(self.live.port, "/", host=host).status)
            except (http.client.HTTPException, OSError):
                refused.append(400)
        self.assertEqual(set(refused), {400})                                                              # (nginx itself refuses these)

    def test_a_scripts_or_styles_answer_has_no_page_header_and_a_pages_has_all_four(self):
        for path in ("/front/classic.css", "/shapes.js", "/home/home.js", "/asset_catalog/catalog_data.js", "/index.js"):
            answer = fetch(self.live.port, path)
            for name in PAGE_HEADERS:
                self.assertIsNone(answer.get(name), "%s %s" % (path, name))
        for path in ("/", "/play.html", "/legal.html", "/asset_catalog/", "/nothing.html"):
            answer = fetch(self.live.port, path)
            for name in PAGE_HEADERS:
                self.assertIsNotNone(answer.get(name), "%s %s" % (path, name))

    def test_an_unchanged_page_is_still_answered_304_with_the_headers_of_the_page(self):
        first = fetch(self.live.port, "/matches.html")
        self.assertIn("etag", first.names())
        again = fetch(self.live.port, "/matches.html", headers=(("If-None-Match", first.get("etag")),))
        self.assertEqual(again.status, 304)
        self.check("the 304", again, "page")

    # ---- the origin of a game connection ---------------------------------------------------------------------------------------------------------------
    def test_the_origin_of_a_game_connection(self):
        site = "https://" + SITE
        rows = [                                                     # (what it is, the host asked for, the Origin headers sent, the status)
            ("no Origin: a program", SITE, [], 101),
            ("the site, https", SITE, [site], 101),
            ("the site, http", SITE, ["http://" + SITE], 101),
            ("the site with a port that is not the default", SITE, [site + ":8443"], 101),
            ("the site asked for with a port, the Origin written with it", SITE + ":19980", ["http://%s:19980" % SITE], 101),
            ("the site asked for with a port, the Origin written without it", SITE + ":19980", [site], 101),
            ("the host in capital letters in the request, the Origin as a browser writes it", "Beta.PlayAnts.org", [site], 101),
            ("localhost, as a developer runs the site", "localhost:19980", ["http://localhost:19980"], 101),
            ("an address in brackets", "[::1]:19980", ["http://[::1]:19980"], 101),
            ("a fully qualified name: the final dot in the request and in the Origin", SITE + ".", [site + "."], 101),
            ("the final dot in the Origin only", SITE, [site + "."], 101),
            ("the final dot in the request only", SITE + ".", [site], 101),
            ("the final dot, over http", SITE + ".", ["http://" + SITE + "."], 101),
            ("the final dot, the host in capital letters in the request", "Beta.PlayAnts.org.", [site + "."], 101),
            ("the final dot of localhost", "localhost.", ["http://localhost."], 101),
            ("another site", SITE, ["https://evil.example"], 403),
            ("another site, with a final dot", SITE + ".", ["https://evil.example."], 403),
            ("the final dot, but another host asked for", "other.example.", [site + "."], 403),
            ("two dots at the end, one in the request", SITE + ".", [site + ".."], 403),
            ("two dots at the end, none in the request", SITE, [site + ".."], 403),
            ("a dot before the host", SITE, ["https://." + SITE], 403),
            ("a dot before a port", SITE + ":8443", [site + ".:8443"], 403),
            ("a dot after a port", SITE + ":8443", [site + ":8443."], 403),
            ("the final dot in capital letters", SITE + ".", ["https://" + SITE.upper() + "."], 403),
            ("the final dot with a path", SITE + ".", [site + "./"], 403),
            ("another site that has the name inside its own", SITE, ["https://" + SITE + ".evil.example"], 403),
            ("another site that begins with the name", SITE, ["https://" + SITE + "-evil.example"], 403),
            ("a site that ends with the name", SITE, ["https://evil" + SITE], 403),
            ("a subdomain", SITE, ["https://app." + SITE], 403),
            ("the null origin (a sandboxed page, a file)", SITE, ["null"], 403),
            ("the site written with capital letters", SITE, ["https://" + SITE.upper()], 403),
            ("the scheme in capital letters", SITE, ["HTTPS://" + SITE], 403),
            ("the site with a path", SITE, [site + "/"], 403),
            ("the site with a path after a port", SITE, [site + ":443/x"], 403),
            ("a user name before the host", SITE, ["https://" + SITE + "@evil.example"], 403),
            ("a user name that is the host", SITE, ["https://" + SITE + ":x@evil.example"], 403),
            ("a fragment", SITE, ["https://evil.example#" + SITE], 403),
            ("a fragment before", SITE, ["#evil.example"], 403),
            ("two Origin headers, the second foreign", SITE, [site, "https://evil.example"], 403),
            ("two Origin headers, the first foreign", SITE, ["https://evil.example", site], 403),
            ("the same Origin twice", SITE, [site, site], 403),
            ("another scheme", SITE, ["ftp://" + SITE], 403),
            ("a file page", SITE, ["file://"], 403),
            ("a space inside", SITE, ["https://evil.example " + SITE], 403),
            ("a port that is not a number", SITE, [site + ":abc"], 403),
            ("an address in brackets that is another one", "[::1]:19980", ["http://[::2]:19980"], 403),
            ("another host than the one asked for", "other.example", [site], 403),
            ("a host asked for that begins with the Origin's host", SITE + ".evil.example", [site], 403),
            ("a host asked for that ends with the Origin's host", "evil-" + SITE, [site], 403),
            ("something before the Origin", SITE, ["x " + site], 403),
            ("the separator of the key inside the Origin", SITE, ["https://evil.example|" + SITE], 403),
            ("an Origin that begins with the separator", SITE, ["|evil.example"], 403),
            ("an Origin that is only the separator", SITE, ["|"], 403),
        ]
        for what, host, origins, status in rows:
            before = len(self.stand.seen)
            got, headers, key = handshake(self.live.port, host, origins)
            self.assertEqual(got, status, "%s: Host %s, Origin %s" % (what, host, origins))
            asked = len(self.stand.seen) - before
            self.assertEqual(asked, 1 if status == 101 else 0, "%s: the stand-in was asked %d times" % (what, asked))
            if status == 101:
                accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
                self.assertEqual(dict(headers).get("sec-websocket-accept"), accept, what)                  # (the key arrived whole)
            else:
                self.assertEqual(dict(headers).get("strict-transport-security"), HSTS, what)               # (a refusal carries the server's headers)

    def test_a_plain_request_for_the_game_door_from_another_site_is_refused_too(self):
        self.assertEqual(fetch(self.live.port, "/ws", headers=(("Origin", "https://evil.example"),)).status, 403)
        self.assertEqual(fetch(self.live.port, "/ws", headers=(("Origin", "null"),)).status, 403)
        self.assertEqual(fetch(self.live.port, "/ws", headers=(("Origin", "null"), ("Sec-Fetch-Site", "same-origin"))).status, 403)      # (a header of the visitor's does not make the Origin good)

    def test_the_chat_hubs_door_still_passes_the_origin_on_for_the_hub_to_judge(self):
        before = len(self.stand.seen)
        status, _, _ = handshake(self.live.port, SITE, ["https://evil.example"], path="/hub/ws", extra=[("Sec-Fetch-Site", "same-origin")])
        self.assertEqual(status, 101)                                # (the stand-in is not the hub: the hub says 403 itself, tests/scripts/test_hub_ws.py)
        (method, path, headers), = self.stand.seen[before:]
        self.assertEqual(dict(headers)["origin"], "https://evil.example")

    # ---- what the game server is given ------------------------------------------------------------------------------------------------------------------
    HOSTILE = (("Cookie", "a=b; __Host-ants_hub=" + "ab" * 16), ("Authorization", "Bearer canary-token"), ("X-Client-Address", "9.9.9.9"), ("X-Forwarded-For", "8.8.8.8"), ("X-Real-IP", "7.7.7.7"),
               ("Forwarded", "for=6.6.6.6"), ("Proxy-Authorization", "Basic canary"), ("User-Agent", "canary-agent"), ("Referer", "https://canary.example/?room=abc"), ("Accept-Language", "en"),
               ("Sec-WebSocket-Extensions", "permessage-deflate"), ("X-Custom", "1"), ("Sec-Fetch-Site", "same-origin"), ("Cache-Control", "no-cache"))

    def seen_by_the_stand_in(self, run):
        before = len(self.stand.seen)
        run()
        new = self.stand.seen[before:]
        self.assertEqual(len(new), 1, new)
        return new[0]

    def test_the_game_server_is_given_the_headers_of_the_handshake_and_the_address_nginx_sees_and_nothing_else(self):
        method, path, headers = self.seen_by_the_stand_in(lambda: handshake(self.live.port, SITE, ["https://" + SITE], extra=self.HOSTILE + (("Sec-WebSocket-Protocol", "ants"),)))
        self.assertEqual((method, path), ("GET", "/ws"))
        self.assertEqual(sorted(k for k, _ in headers), sorted(["host", "upgrade", "connection", "sec-websocket-key", "sec-websocket-version", "sec-websocket-protocol", "origin", "x-client-address"]))
        got = dict(headers)
        self.assertEqual(got["x-client-address"], "127.0.0.1")                              # (the address of the connection: never the visitor's own header)
        self.assertEqual((got["host"], got["origin"], got["sec-websocket-protocol"], got["sec-websocket-version"]), (SITE, "https://" + SITE, "ants", "13"))
        self.assertEqual((got["upgrade"].lower(), got["connection"].lower()), ("websocket", "upgrade"))
        for canary in ("canary", "9.9.9.9", "8.8.8.8", "7.7.7.7", "6.6.6.6", "__Host", "permessage"):
            self.assertNotIn(canary, str(headers))

    def test_without_an_origin_or_a_subprotocol_none_is_made_up(self):
        method, path, headers = self.seen_by_the_stand_in(lambda: handshake(self.live.port, SITE, []))
        self.assertEqual(sorted(k for k, _ in headers), sorted(["host", "upgrade", "connection", "sec-websocket-key", "sec-websocket-version", "x-client-address"]))

    def test_the_status_door_is_given_the_host_and_nothing_else(self):
        method, path, headers = self.seen_by_the_stand_in(lambda: fetch(self.live.port, "/busy", headers=self.HOSTILE))
        self.assertEqual((method, path), ("GET", "/busy"))
        self.assertEqual(sorted(k for k, _ in headers), ["connection", "host"])
        self.assertEqual(dict(headers), {"connection": "close", "host": SITE})

    # ---- the access log ---------------------------------------------------------------------------------------------------------------------------------
    def log_lines(self, front, marker, count=None):
        """The lines of the log that hold `marker` (the browser name that a test gives its own requests: a connection that was upgraded is logged when it ends, which may be later than the test that opened it).
        With `count`, as soon as that many are there (nginx writes a line when the answer is sent), else after a pause."""
        deadline = time.monotonic() + 5.0
        while True:
            if count is None:
                time.sleep(0.5)
            with open(front.log, encoding="utf-8") as f:
                lines = [line for line in f.read().splitlines() if marker in line]
            if count is None or len(lines) >= count or time.monotonic() > deadline:
                return lines
            time.sleep(0.05)

    def test_the_log_has_the_path_and_not_the_query_and_not_the_referrer_the_cookie_or_the_authorization(self):
        with open(self.live.stock, encoding="utf-8") as f:
            stock_before = f.read()
        secrets = ("ROOMSECRET1", "ROOMSECRET2", "NAMESECRET", "SEARCHSECRET", "HISTSECRET", "UNKNOWNSECRET", "REFSECRET", "COOKIESECRET", "AUTHSECRET", "XFFSECRET")
        extra = (("Referer", "https://other.example/?room=REFSECRET"), ("Cookie", "session=COOKIESECRET"), ("Authorization", "Bearer AUTHSECRET"), ("User-Agent", "agent-visible/1.0"), ("X-Forwarded-For", "1.2.3.4, XFFSECRET"))
        asked = [("/?room=ROOMSECRET1", "/", 200), ("/play.html?join=/ws&room=ROOMSECRET2&name=NAMESECRET", "/play.html", 200), ("/matches.html?q=SEARCHSECRET", "/matches.html", 200),
                 ("/history?q=HISTSECRET", "/history", 200), ("/nowhere/at/all?x=UNKNOWNSECRET", "/nowhere/at/all", 200), ("/wp-login.php?x=UNKNOWNSECRET", "/wp-login.php", 200), ("/busy", "/busy", 200)]
        for target, _, _ in asked:
            fetch(self.live.port, target, headers=extra)
        lines = self.log_lines(self.live, "agent-visible/1.0", len(asked))
        text = "\n".join(lines)
        for secret in secrets:
            self.assertNotIn(secret, text, secret)
        self.assertNotIn("?", text)
        self.assertNotIn("other.example", text)
        self.assertEqual(len(lines), len(asked), lines)
        shape = re.compile(r'^127\.0\.0\.1 \[\d\d/[A-Z][a-z]{2}/\d{4}:\d\d:\d\d:\d\d [+-]\d{4}\] "GET (\S+) HTTP/1\.1" (\d{3}) (\d+) "agent-visible/1\.0"$')
        for line, (_, path, status) in zip(lines, asked):
            m = shape.match(line)
            self.assertIsNotNone(m, line)
            self.assertEqual((m.group(1), int(m.group(2))), (path, status), line)                  # (the path that was asked for, not the page that a fallback served)
            self.assertGreater(int(m.group(3)), 0)
        with open(self.live.stock, encoding="utf-8") as f:
            self.assertEqual(f.read(), stock_before, "the image's stock log is not used for this server")

    def test_the_doors_that_keep_no_log_still_keep_none(self):
        quiet = (("User-Agent", "agent-quiet/1.0"),)
        fetch(self.live.port, "/hub/me", headers=SAME + quiet)
        handshake(self.live.port, SITE, ["https://" + SITE], path="/hub/ws", extra=[("Sec-Fetch-Site", "same-origin")] + list(quiet))
        fetch(self.live.port, "/hub/login", headers=SAME + quiet)
        fetch(self.live.port, "/stats", headers=quiet)                                          # (a control: a door that logs is in the log, so the log is looked at)
        self.assertEqual(len(self.log_lines(self.live, "agent-quiet/1.0", 1)), 1)
        self.assertEqual(len(self.log_lines(self.live, "agent-quiet/1.0")), 1)

    # ---- the compressed files ---------------------------------------------------------------------------------------------------------------------------
    def test_a_browser_that_accepts_gzip_is_given_the_compressed_file_as_it_is(self):
        for name in ("index.data", "index.wasm", "index.js"):
            with open(os.path.join(self.docroot, name + ".gz"), "rb") as f:
                packed = f.read()
            with open(os.path.join(self.docroot, name), "rb") as f:
                plain = f.read()
            got = fetch(self.live.port, "/" + name, headers=(("Accept-Encoding", "gzip, deflate, br"),))
            self.assertEqual(got.status, 200, name)
            self.assertEqual(got.get("content-encoding"), "gzip", name)
            self.assertEqual(got.body, packed, name)                                                  # (the file as it is: nothing was compressed for this request)
            self.assertEqual(got.get("content-length"), str(len(packed)), name)
            self.assertNotIn("transfer-encoding", got.names(), name)
            self.assertFalse(got.get("etag").startswith("W/"), name)                                   # (a strong tag: the copy is a file)
            self.assertIn("accept-encoding", got.get("vary").lower(), name)
            self.assertEqual(got.get("accept-ranges"), "bytes", name)
            self.assertEqual(gzip.decompress(got.body), plain, name)
            self.assertLess(len(packed), len(plain) * 0.6, name)
            kind = {"index.data": "application/octet-stream", "index.wasm": "application/wasm", "index.js": "text/javascript"}[name]
            self.assertTrue(got.get("content-type").startswith(kind) or name == "index.js" and "javascript" in got.get("content-type"), (name, got.get("content-type")))
            self.check(name, got, "plain")
            self.assertEqual(got.get("cache-control"), "no-cache, must-revalidate", name)             # (AGENTS.md rule 6: revalidated, and only that)

    def test_a_browser_that_does_not_is_given_the_plain_file(self):
        for headers in ((), (("Accept-Encoding", "identity"),), (("Accept-Encoding", "br"),)):
            for name in ("index.data", "index.wasm", "index.js"):
                with open(os.path.join(self.docroot, name), "rb") as f:
                    plain = f.read()
                got = fetch(self.live.port, "/" + name, headers=headers)
                self.assertEqual((got.status, got.body), (200, plain), (name, headers))
                self.assertIsNone(got.get("content-encoding"), (name, headers))
                self.assertEqual(got.get("content-length"), str(len(plain)))
                self.assertFalse(got.get("etag").startswith("W/"))
                self.check(name, got, "plain")

    def test_the_two_copies_have_their_own_tags_and_each_is_revalidated_with_its_own(self):
        packed = fetch(self.live.port, "/index.data", headers=(("Accept-Encoding", "gzip"),))
        plain = fetch(self.live.port, "/index.data")
        self.assertNotEqual(packed.get("etag"), plain.get("etag"))
        for got, headers in ((packed, (("Accept-Encoding", "gzip"),)), (plain, ())):
            again = fetch(self.live.port, "/index.data", headers=headers + (("If-None-Match", got.get("etag")),))
            self.assertEqual((again.status, again.body), (304, b""))
            self.check("the 304 of " + got.get("etag"), again, "plain")
        wrong = fetch(self.live.port, "/index.data", headers=(("Accept-Encoding", "gzip"), ("If-None-Match", plain.get("etag"))))
        self.assertEqual(wrong.status, 200)                                                            # (the tag of the other copy is not this copy's)

    def test_a_range_of_the_compressed_file_is_a_range_of_the_file_as_it_is(self):
        with open(os.path.join(self.docroot, "index.data.gz"), "rb") as f:
            packed = f.read()
        got = fetch(self.live.port, "/index.data", headers=(("Accept-Encoding", "gzip"), ("Range", "bytes=10-109")))
        self.assertEqual(got.status, 206)
        self.assertEqual(got.body, packed[10:110])
        self.assertEqual(got.get("content-range"), "bytes 10-109/%d" % len(packed))
        self.assertEqual(got.get("content-encoding"), "gzip")
        self.check("the range", got, "plain")
        tail = fetch(self.live.port, "/index.wasm", headers=(("Accept-Encoding", "gzip"), ("Range", "bytes=-50")))
        with open(os.path.join(self.docroot, "index.wasm.gz"), "rb") as f:
            self.assertEqual(tail.body, f.read()[-50:])

    def test_a_head_request_has_the_size_of_the_compressed_file(self):
        with open(os.path.join(self.docroot, "index.wasm.gz"), "rb") as f:
            size = len(f.read())
        got = fetch(self.live.port, "/index.wasm", "HEAD", (("Accept-Encoding", "gzip"),))
        self.assertEqual((got.status, got.get("content-length"), got.get("content-encoding")), (200, str(size), "gzip"))
        self.check("HEAD", got, "plain")

    def test_a_file_with_no_copy_is_compressed_as_the_request_comes_and_the_copies_are_not_asked_for_again(self):
        got = fetch(self.live.port, "/front/big.css", headers=(("Accept-Encoding", "gzip"),))
        self.assertEqual((got.status, got.get("content-encoding")), (200, "gzip"))
        self.assertTrue(got.get("etag").startswith("W/"))                                              # (nginx's own gzip makes the tag weak)
        self.assertNotIn("content-length", got.names())                                                # (and sends it in chunks)
        with open(os.path.join(self.docroot, "front/big.css"), "rb") as f:
            self.assertEqual(gzip.decompress(got.body), f.read())
        plain = fetch(self.live.port, "/front/big.css")
        self.assertIsNone(plain.get("content-encoding"))
        self.check("big.css", got, "plain")
        direct = fetch(self.live.port, "/index.data.gz")
        self.assertEqual((direct.status, direct.get("strict-transport-security")), (200, HSTS))     # (the copy is a file of the site like any other, with the server's headers)

    # ---- every Host name ----------------------------------------------------------------------------------------------------------------------------------
    def test_any_host_name_is_served_like_the_public_one(self):
        for host in (SITE, "localhost", "127.0.0.1", "staging.example.org", "ants.my-own-server.example", "unknown.example:8080"):
            for path, kind in (("/", "page"), ("/play.html", "game"), ("/stats", "door"), ("/lobbies", "door")):
                answer = fetch(self.live.port, path, host=host)
                self.assertEqual(answer.status, 200, (host, path))
                self.check("%s at %s" % (path, host), answer, kind, host=host)


if __name__ == "__main__":
    unittest.main()
