#!/usr/bin/env python3
"""docker/nginx.conf routes the list of maps and serves the players' maps: GET /maps, /maps/previews/<name>.webp and /maps/files/<name>.lvl (run by ./run_tests.sh --fast).

ants_server answers GET /maps on its read-only replay port (docs/SERVER.md "The list of maps"); the page's nginx forwards it like /history, and serves the preview pictures and the level files of the
players' maps itself, from a folder of the image outside the web root:

  - /maps is an exact location: a plain GET without a query (405 and 404 otherwise), twenty a second with a burst of 100 (503 beyond), cached for five seconds in one entry for everybody, nothing of the
    visitor's request passed on but Host and Connection, a server with no such door answered 404 with a JSON body;
  - /maps/previews/ and /maps/files/ are prefix locations that serve static files: GET and HEAD only, no query, a name of printable ASCII without a slash or a backslash, 1 to 80 characters and the right
    ending, checked before the disk is asked (nginx has decoded %XX by then, so %2F, %5C, %2e%2e, %00 and a double encoding cannot make a path), no directory listing, their own allowances (503 beyond),
    cacheable, and the headers of the server repeated (a location that sets a header does not inherit them); everything else under /maps/ is a plain 404, never the page of the site;
  - every one of the 534 community maps and their pictures is served under its percent-encoded name (spaces and ! # $ & ' ~ in the names), byte for byte.

The text of the file is checked everywhere. Where the program nginx is installed the real file is RUN (a copy of it with the folders and the port of the stand-in server put in, in a temporary folder), so
that what is served and what is refused, and what reaches the stand-in server, is seen.
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
import urllib.parse
from http.server import BaseHTTPRequestHandler

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import quick_http  # noqa: E402
import test_nginx_stats as stats  # noqa: E402

read = stats.read
strip_comments = stats.strip_comments
block_of = stats.block_of
REPO = stats.REPO

RAW = read("docker", "nginx.conf")
CONF = strip_comments(RAW)
HEAD = CONF[:CONF.index("server {")]
LIST = block_of(CONF, "location = /maps ")
PREVIEWS = block_of(CONF, "location ^~ /maps/previews/ ")
FILES = block_of(CONF, "location ^~ /maps/files/ ")
REST = block_of(CONF, "location ^~ /maps/ ")
OFF = block_of(CONF, "location @maps_off ")
HISTORY_LIST = block_of(CONF, "location = /history ")
STATIC = (("previews", PREVIEWS), ("files", FILES))

MAPS = json.loads(read("Community-Maps", "maps.json"))["maps"]
NAMES = [m["file"] for m in MAPS]
PICTURES = json.loads(read("Community-Maps", "previews.json"))


def name_pattern(block, prefix):
    """The regular expression of a static location's check of $uri, as Python reads it (nginx's \\z is \\Z here)."""
    match = re.search(r'if \(\$uri !~ "([^"]+)"\) \{ return 404; \}', block)
    assert match, "no check of the name in " + prefix
    return re.compile(match.group(1).replace("\\z", "\\Z"))


def quote(name):
    return urllib.parse.quote(name, safe="")


class TheLocations(unittest.TestCase):
    def test_the_four_locations_of_the_name_are_the_only_ones_and_no_pattern_location_could_catch_them(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual([l for l in locations if "maps" in l], ["= /maps", "^~ /maps/previews/", "^~ /maps/files/", "^~ /maps/", "@maps_off", "= /maps/all", "= /test-maps", "@test_maps_off"])       # (/maps/all: the newest version's list, docs/MAP_SUBMISSIONS.md; the two last are the test-map door's, tests/scripts/test_nginx_test_maps.py)
        # (a regular-expression location would win over a prefix one only without ^~; the three prefix locations all have it, and an extension rule such as the one of images cannot answer first)
        for header in ("location ^~ /maps/previews/ {", "location ^~ /maps/files/ {", "location ^~ /maps/ {"):
            self.assertEqual(CONF.count(header), 1)

    def test_the_list_is_the_history_blocks_twin_but_for_its_own_checks_allowance_cache_and_fallback(self):
        def lines(block, *own):
            return [l.strip() for l in block.splitlines() if l.strip() and not l.strip().startswith(own)]
        own = ("if (", "limit_req zone=", "proxy_cache", "proxy_no_cache", "proxy_ignore_headers", "error_page")
        self.assertEqual(lines(LIST, *own), lines(HISTORY_LIST, *own))          # (one door, one set of timeouts, headers and nothing-of-the-visitor rules: they cannot drift apart)
        self.assertIn("if ($request_method != GET) { return 405; }", LIST)
        self.assertIn("if ($is_args) { return 404; }", LIST)
        self.assertLess(LIST.index("$is_args"), LIST.index("limit_req "))
        self.assertLess(LIST.index("$request_method"), LIST.index("limit_req "))
        self.assertLess(LIST.index("limit_req "), LIST.index("proxy_cache "))
        self.assertLess(LIST.index("proxy_cache "), LIST.index("proxy_pass"))
        self.assertIn("error_page 502 504 = @maps_off;", LIST)
        self.assertIn("set $ants_replays ants-server:4020;", LIST)             # the replay port, as /history
        self.assertEqual(re.findall(r"proxy_set_header\s+(\S+)", LIST), ["Connection", "Host", "Content-Length"])
        for forbidden in ("alias", "root", "try_files", "add_header", "rewrite"):
            self.assertNotRegex(LIST, r"\b%s\b" % forbidden)

    def test_the_list_is_cached_for_five_seconds_in_one_entry_of_the_numbers_cache(self):
        for directive in ("proxy_cache ants_stats_cache;", 'proxy_cache_key "ants-maps";', "proxy_cache_valid 200 5s;", "proxy_ignore_headers Cache-Control;", "proxy_cache_lock on;",
                          "proxy_cache_use_stale updating error timeout;"):
            self.assertIn(directive, LIST)
        keys = re.findall(r'proxy_cache_key "([^"]+)";', CONF)
        self.assertEqual(keys.count("ants-maps"), 1)
        for other in keys:
            if other != "ants-maps":
                self.assertFalse(other.startswith("ants-maps") or "ants-maps".startswith(other), other)    # (no other entry is this one's)
        self.assertEqual(len(re.findall(r"\bproxy_cache_valid\b", LIST)), 1)     # (a 404 of a server with no door is asked every time)

    def test_only_return_is_used_inside_an_if(self):
        for what, block in (("list", LIST),) + STATIC:
            conditions = [l.strip() for l in block.splitlines() if l.strip().startswith("if (")]
            self.assertGreaterEqual(len(conditions), 2, what)
            for line in conditions:
                self.assertRegex(line, r"\{ return \d{3}; \}$", what)                          # ("if" is only safe with a return: nothing else may sit in one)

    def test_the_static_blocks_check_method_query_and_name_before_the_allowance_and_the_disk(self):
        for what, block in STATIC:
            self.assertIn('if ($request_method !~ "^(GET|HEAD)\\z") { return 405; }', block, what)
            self.assertIn("if ($is_args) { return 404; }", block, what)
            for check in ("$request_method", "$is_args", "$uri !~"):
                self.assertLess(block.index(check), block.index("limit_req "), what)     # (a refused request costs nothing of the allowance)
                self.assertLess(block.index(check), block.index("alias "), what)         # (and the disk is not asked before the checks)
            self.assertIn("limit_req_status 503;", block, what)
            self.assertIn("autoindex off;", block, what)
            self.assertNotRegex(block, r"\b(proxy_pass|try_files|rewrite|root|index|autoindex on)\b", what)
        self.assertIn("alias /srv/community-maps/previews/;", PREVIEWS)
        self.assertIn("alias /srv/community-maps/files/;", FILES)
        self.assertEqual(REST.strip(), "return 404;")

    def test_the_headers_of_the_server_are_repeated_where_a_location_sets_its_own(self):
        for what, block in STATIC:
            for header in ("Cross-Origin-Opener-Policy", "Cross-Origin-Embedder-Policy", "X-Content-Type-Options"):
                self.assertEqual(len(re.findall(r"add_header %s\b" % header, block)), 1, what + header)
            self.assertRegex(block, r'add_header Cache-Control "public, max-age=\d+";')       # (no "always": an error is never publicly cacheable)
            self.assertNotRegex(block, r'Cache-Control[^\n]*always')
        self.assertIn("default_type image/webp;", PREVIEWS)
        self.assertIn("default_type application/octet-stream;", FILES)
        self.assertEqual(len(re.findall(r"add_header X-Content-Type-Options", OFF)), 1)
        self.assertIn("return 404 '{", OFF)
        self.assertIn("default_type application/json;", OFF)
        self.assertIn('add_header Cache-Control "no-store" always;', OFF)

    def test_the_allowances_are_their_own_sized_for_the_whole_site_and_declared_outside_the_server(self):
        for zone, rate in (("ants_maps", 20), ("ants_map_previews", 200), ("ants_map_files", 20)):
            found = re.search(r"limit_req_zone \$binary_remote_addr zone=%s:(\d+)m rate=(\d+)r/s;" % zone, CONF)
            self.assertIsNotNone(found, zone)
            self.assertLess(CONF.index(found.group(0)), CONF.index("server {"))
            self.assertEqual((int(found.group(1)), int(found.group(2))), (1, rate), zone)
        self.assertIn("limit_req zone=ants_maps burst=100 nodelay;", LIST)
        self.assertIn("limit_req zone=ants_map_previews burst=400 nodelay;", PREVIEWS)
        self.assertIn("limit_req zone=ants_map_files burst=40 nodelay;", FILES)
        for block in (LIST, PREVIEWS, FILES):
            self.assertEqual(len(re.findall(r"limit_req zone=", block)), 1)
        for other in ("ants_history", "ants_replays", "ants_stats", "ants_live", "ants_busy"):
            self.assertNotRegex(LIST + PREVIEWS + FILES, r"zone=%s\b" % other)

    def test_the_name_patterns_pass_every_map_and_picture_of_the_repository_and_nothing_that_could_name_a_path(self):
        previews = name_pattern(PREVIEWS, "previews")
        files = name_pattern(FILES, "files")
        for name in NAMES:
            self.assertTrue(files.match("/maps/files/" + name), name)
            self.assertTrue(previews.match("/maps/previews/" + PICTURES[name]["picture"]), name)
        for pattern, folder, ending in ((files, "/maps/files/", ".lvl"), (previews, "/maps/previews/", ".webp")):
            bad = ["", ending, "a/b" + ending, "../a" + ending, "a/../b" + ending, "a\\b" + ending, "a" * 81 + ending, "a" + ending + "\n", "a\nb" + ending, "a\x00b" + ending, "a\x7fb" + ending,
                   "aéb" + ending, "a" + ending + "x", "a.txt", "a", "a" + ending + "/", "/a" + ending, "a" + ending + "/b" + ending]
            for name in bad:
                self.assertFalse(pattern.match(folder + name), repr(name))
            self.assertTrue(pattern.match(folder + "a" * 76 + ending) is not None)       # (80 characters with the ending)
            self.assertFalse(pattern.match("/maps/other/a" + ending))
            self.assertFalse(pattern.match("/maps" + folder[5:] + "x" + ending + "/"))
        self.assertTrue(files.match("/maps/files/UPPER.LVL"))                              # (21 community maps end in .LVL)
        self.assertFalse(files.match("/maps/files/a.Lvl"))
        self.assertFalse(previews.match("/maps/previews/a.WEBP"))
        self.assertFalse(previews.match("/maps/previews/a.lvl"))

    def test_the_name_pattern_of_the_files_is_the_one_the_servers_protocol_allows(self):
        # the server refuses names that the pattern would have let through only for their characters (':' '*' '?' '"' '<' '>' '|' are Windows-forbidden): the list never offers them, and a request for one is a 404
        # from the disk; what the pattern must never let through is a slash, a backslash or a control character, which the tests above pin
        names = [m["file"] for m in MAPS]
        self.assertTrue(all(len(n) <= 64 for n in names))                                  # (the protocol's longest map name)
        self.assertTrue(all(32 <= ord(c) < 127 and c not in '/\\:*?"<>|' for n in names for c in n))


class TheBuildPutsThePicturesAndTheMapsWhereNginxLooks(unittest.TestCase):
    def test_the_web_image_copies_the_previews_and_the_level_files_outside_the_web_root(self):
        text = read("Dockerfile")
        self.assertIn("COPY Community-Maps/previews/ /srv/community-maps/previews/", text)
        self.assertRegex(text, r"COPY Community-Maps/\*\.lvl Community-Maps/\*\.LVL /srv/community-maps/files/")
        self.assertNotIn("/usr/share/nginx/html/maps", text)                                 # (no copy under the web root: only the two locations reach them)
        self.assertNotIn("COPY Community-Maps/ ", text)                                       # (the lists and the README are not served)

    def test_the_game_server_image_has_the_maps_in_a_folder_of_their_own_that_no_volume_hides(self):
        text = read("Dockerfile.server")
        self.assertRegex(text, r"COPY Community-Maps/\*\.lvl Community-Maps/\*\.LVL Community-Maps/maps\.json Community-Maps/previews\.json /community-maps/")
        self.assertIn('"--community-maps", "/community-maps"', text)
        for compose in ("docker-compose.stack.yml", "docker-compose.server.yml", "docker-compose.staging.yml"):
            body = read(compose)
            self.assertNotRegex(body, r":/community-maps")
            self.assertNotIn("entrypoint:", body)                                              # (the image's --community-maps is kept)
        self.assertNotRegex(read("docker-compose.stack.yml"), r"/maps/community")

    def test_every_picture_and_map_is_in_the_repository_and_small(self):
        folder = os.path.join(REPO, "Community-Maps")
        for name in NAMES:
            self.assertTrue(os.path.isfile(os.path.join(folder, name)), name)
            self.assertTrue(os.path.isfile(os.path.join(folder, "previews", PICTURES[name]["picture"])), name)
        total = sum(os.path.getsize(os.path.join(folder, "previews", f)) for f in os.listdir(os.path.join(folder, "previews")))
        self.assertLess(total, 16 * 1024 * 1024)                                              # (about 9 MB)


# ------------------------------------------------------------------------------------------------------------------------------------------------------------ the blocks, run
LIST_BODY = b'{"format":1,"maps":[{"file":"TINY.LVL","width":31,"height":31,"minutes":6,"players":4,"description":"stand-in","plays":0,"original":true}]}'


class StandIn(BaseHTTPRequestHandler):
    """The replay port of the game server, as far as /maps goes: answers the list and remembers what it was sent."""
    seen = []
    lock = threading.Lock()
    mode = "ok"

    def do_GET(self):
        with StandIn.lock:
            StandIn.seen.append((self.path, dict((k.lower(), v) for k, v in self.headers.items())))
        if self.path == "/maps":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Length", str(len(LIST_BODY)))
            self.end_headers()
            self.wfile.write(LIST_BODY)
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *args):
        pass


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def start_nginx(work, write_conf, attempts=6, wait=15.0):
    """Start the program nginx on a free port and return (process, port) once THIS nginx listens there.

    A port that free_port() hands out is free only for the moment: while the tests of several files run side by side (./run_tests.sh), another one can take it before nginx binds it, and a
    wait that only connects to the port then talks to the other test's stand-in (its answer is a 404 or a 200 of a door that is not this rig's, and the rig's own door sees nothing). So the
    wait is for nginx's own pid file, which it writes after it has bound its sockets; a master that exits (the port was taken) is started again on a new port, up to `attempts` times, each
    with a deadline of `wait` seconds. `write_conf(port)` writes the configuration for that port and returns its path."""
    pid_file = os.path.join(work, "nginx.pid")
    why = ""
    for _ in range(attempts):
        port = free_port()
        conf = write_conf(port)
        test = subprocess.run(["nginx", "-t", "-p", work, "-c", conf], capture_output=True, text=True)
        if test.returncode != 0:
            raise AssertionError("nginx -t: " + test.stderr)
        if os.path.exists(pid_file):
            os.remove(pid_file)
        proc = subprocess.Popen(["nginx", "-p", work, "-c", conf, "-g", "daemon off;"], stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + wait
        while proc.poll() is None and time.monotonic() < deadline:
            if os.path.exists(pid_file):
                try:
                    socket.create_connection(("127.0.0.1", port), timeout=1).close()
                    return proc, port
                except OSError:
                    pass
            time.sleep(0.02)
        if proc.poll() is None:
            proc.kill()
        proc.wait()
        try:
            with open(os.path.join(work, "error.log"), encoding="utf-8", errors="replace") as f:
                why = f.read()[-400:]
        except OSError:
            why = "(no error log)"
    raise AssertionError("nginx did not start in %d tries: %s" % (attempts, why))


def make_config(workdir, port, stub_port, relaxed=False):
    """The real docker/nginx.conf with the folders, the port and the game server's address of the rig put in."""
    text = RAW
    text = text.replace("listen 80;", "listen 127.0.0.1:%d;" % port).replace("listen [::]:80;", "")
    text = text.replace("/srv/community-maps/", workdir + "/srv/")
    text = text.replace("/var/cache/nginx/", workdir + "/cache/")
    text = text.replace("access_log /dev/stdout ants_noquery;", "access_log off;")                      # (the image logs to its output; a rig is quiet)
    text = text.replace("set $ants_replays ants-server:4020;", "set $ants_replays 127.0.0.1:%d;" % stub_port)
    text = text.replace("set $ants_replays ants-server:4002;", "set $ants_replays 127.0.0.1:%d;" % stub_port)
    if relaxed:                                                                                # (the tests that fetch 534 files one after the other are not the flood; the strict rig is)
        text = re.sub(r"(zone=ants_map\w*:1m rate=)\d+r/s", r"\g<1>100000r/s", text)
    user = "user root;\n" if hasattr(os, "geteuid") and os.geteuid() == 0 else ""       # (the workers read the rig's 0700 folder)
    return user + "pid %s/nginx.pid;\nerror_log %s/error.log;\nevents { worker_connections 256; }\nhttp {\n access_log off;\n client_body_temp_path %s/tmp/body;\n proxy_temp_path %s/tmp/proxy;\n fastcgi_temp_path %s/tmp/fcgi;\n uwsgi_temp_path %s/tmp/uwsgi;\n scgi_temp_path %s/tmp/scgi;\n%s\n}\n" % ((workdir,) * 7 + (text,))


class Rig:
    """The real file in front of the stand-in server, in a temporary folder with the community maps and pictures copied in; `relaxed` lifts the three allowances of the maps."""
    def __init__(self, relaxed):
        self.work = tempfile.mkdtemp(prefix="ants_nginx_maps_")
        for sub in ("srv/previews", "srv/files", "cache/ants_stats", "cache/ants_history", "tmp"):      # (the aliases of the real file end in previews/ and files/: /srv/community-maps/ becomes <work>/srv/)
            os.makedirs(os.path.join(self.work, sub), exist_ok=True)
        folder = os.path.join(REPO, "Community-Maps")
        for name in os.listdir(folder):
            if name.lower().endswith(".lvl"):
                shutil.copyfile(os.path.join(folder, name), os.path.join(self.work, "srv", "files", name))
        for name in os.listdir(os.path.join(folder, "previews")):
            shutil.copyfile(os.path.join(folder, "previews", name), os.path.join(self.work, "srv", "previews", name))
        os.makedirs(os.path.join(self.work, "srv", "files", "subdir.lvl"))                     # (a directory that looks like a map)
        with open(os.path.join(self.work, "secret.txt"), "w") as f:                              # (files next to the folders that a traversal would reach)
            f.write("SECRET")
        with open(os.path.join(self.work, "srv", "files", "secret.lvl"), "w") as f:
            f.write("SECRET2")
        self.stub_port = free_port()
        self.stub = quick_http.QuickHTTPServer(("127.0.0.1", self.stub_port), StandIn)
        threading.Thread(target=self.stub.serve_forever, daemon=True).start()

        def write_conf(port):
            conf = os.path.join(self.work, "nginx.conf")
            with open(conf, "w") as f:
                f.write(make_config(self.work, port, self.stub_port, relaxed))
            return conf
        self.proc, self.port = start_nginx(self.work, write_conf)

    def stop(self):
        self.proc.terminate()
        self.proc.wait(timeout=10)
        self.stub.shutdown()
        shutil.rmtree(self.work, ignore_errors=True)


class Asking:
    def ask(self, method, target, headers=None, body=None):
        """A request with the target exactly as written (no normalising); (status, headers, body)."""
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            conn.putrequest(method, target, skip_host=False, skip_accept_encoding=True)
            for k, v in (headers or {}).items():
                conn.putheader(k, v)
            conn.endheaders(body)
            r = conn.getresponse()
            return r.status, [(k.lower(), v) for k, v in r.getheaders()], r.read()
        finally:
            conn.close()

    def names(self, headers, name):
        return [v for k, v in headers if k == name]


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the blocks of docker/nginx.conf for /maps were NOT run (tests/scripts/test_nginx_maps.py)")
class TheBlocksRun(Asking, unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rig = Rig(relaxed=True)
        cls.port = cls.rig.port

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()

    # ---- the files
    def test_every_community_map_and_picture_is_served_under_its_encoded_name_byte_for_byte(self):
        folder = os.path.join(REPO, "Community-Maps")
        for name in NAMES:
            with open(os.path.join(folder, name), "rb") as f:
                want = f.read()
            status, headers, body = self.ask("GET", "/maps/files/" + quote(name))
            self.assertEqual(status, 200, name)
            self.assertEqual(body, want, name)
            picture = PICTURES[name]["picture"]
            with open(os.path.join(folder, "previews", picture), "rb") as f:
                want = f.read()
            status, headers, body = self.ask("GET", "/maps/previews/" + quote(picture))
            self.assertEqual(status, 200, picture)
            self.assertEqual(body, want, picture)

    def test_the_odd_spellings_of_a_name_all_reach_the_same_file(self):
        name = "!!!!!!!!!!!!!!!! Danger_World!_II!!!.lvl"
        with open(os.path.join(REPO, "Community-Maps", name), "rb") as f:
            want = f.read()
        for target in ("/maps/files/" + quote(name),                                          # %21 %20
                       "/maps/files/" + urllib.parse.quote(name, safe="!"),                    # only the space written as %20
                       "/maps/files/" + name.replace(" ", "%20"),
                       "/maps/files/" + name.replace(" ", "%20").replace("!", "%21")):
            status, _, body = self.ask("GET", target)
            self.assertEqual((status, body == want), (200, True), target)
        for target in ("/maps/files/" + name,                                                  # a raw space is not a request
                       "/maps/files/" + name.replace(" ", "+"),                                # '+' is a plus in a path, not a space
                       "/maps/files/" + name.upper()):                                         # the disk is case-sensitive
            try:
                status = self.ask("GET", target)[0]
            except (http.client.HTTPException, OSError):
                status = 400
            self.assertIn(status, (400, 404), target)
        for name in ("A'B#C$D&E~F.lvl", "UPPER.LVL"):
            self.assertIn(self.ask("GET", "/maps/files/" + quote(name))[0], (404,), name)      # (not there in the rig: a plain 404)

    def test_the_headers_of_a_file_make_it_cacheable_and_keep_the_pages_security_headers_once(self):
        name = NAMES[0]
        status, headers, body = self.ask("GET", "/maps/files/" + quote(name))
        self.assertEqual(status, 200)
        self.assertEqual(self.names(headers, "cache-control"), ["public, max-age=3600"])
        self.assertEqual(self.names(headers, "content-type"), ["application/octet-stream"])
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(self.names(headers, "cross-origin-opener-policy"), ["same-origin"])
        self.assertEqual(self.names(headers, "cross-origin-embedder-policy"), ["require-corp"])
        self.assertTrue(self.names(headers, "etag"))
        etag = self.names(headers, "etag")[0]
        status, _, body = self.ask("GET", "/maps/files/" + quote(name), {"If-None-Match": etag})
        self.assertEqual((status, body), (304, b""))                                           # (revalidation costs a header)
        picture = PICTURES[name]["picture"]
        status, headers, _ = self.ask("GET", "/maps/previews/" + quote(picture))
        self.assertEqual(self.names(headers, "content-type"), ["image/webp"])
        self.assertEqual(self.names(headers, "cache-control"), ["public, max-age=86400"])
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])
        status, headers, body = self.ask("HEAD", "/maps/files/" + quote(name))
        self.assertEqual((status, body), (200, b""))
        self.assertTrue(self.names(headers, "content-length"))

    def test_an_error_answer_is_never_publicly_cacheable(self):
        missing = ["/maps/files/Nothing%20Here.lvl", "/maps/previews/Nothing%20Here.webp", "/maps/files/x.txt", "/maps/previews/x.png", "/maps/files/subdir.lvl/", "/maps/files/a?x=1"]
        for target in missing:
            status, headers, _ = self.ask("GET", target)
            self.assertIn(status, (404, 403), target)
            self.assertEqual(self.names(headers, "cache-control"), [], target)
        for target in ("/maps/files/" + quote(NAMES[0]), "/maps/previews/" + quote(PICTURES[NAMES[0]]["picture"])):
            status, headers, _ = self.ask("POST", target, {"Content-Length": "0"})
            self.assertEqual(status, 405)
            self.assertEqual(self.names(headers, "cache-control"), [], target)
            status, headers, _ = self.ask("GET", target)
            self.assertTrue(self.names(headers, "cache-control")[0].startswith("public, max-age="), target)       # (a good answer still is)

    def test_nothing_that_could_name_another_path_is_served(self):
        for target in ("/maps/files/..%2f..%2fsecret.txt", "/maps/files/..%2fsecret.lvl", "/maps/files/%2e%2e%2fsecret.lvl", "/maps/files/%2e%2e/secret.lvl", "/maps/files/../secret.lvl",
                       "/maps/files/..%5csecret.lvl", "/maps/files/a%2fb.lvl", "/maps/files/%252e%252e%252fsecret.lvl", "/maps/files/%2e%2e%2f%2e%2e%2fsecret.lvl", 
                       "/maps/files/subdir.lvl/", "/maps/files/secret.lvl%00", "/maps/files/%00.lvl", "/maps/files/a%0ab.lvl", "/maps/files/%ff.lvl",
                       "/maps/previews/..%2f..%2fsecret.txt", "/maps/previews/%2e%2e%2fp/x.webp", "/maps/previews/a%2fb.webp", "/maps/previews/..%5cx.webp",
                       "/maps/files/" + "a" * 81 + ".lvl", "/maps/files/x.txt", "/maps/files/secret.lvl.txt", "/maps/previews/x.png", "/maps/previews/x.lvl"):
            status, _, body = self.ask("GET", target)
            self.assertNotIn(b"SECRET", body, target)
            # (nginx resolves a dot-dot that is written with %2f BEFORE it picks the location: the request then leaves /maps/ and is the page's own business, here the site's fallback page; inside /maps/ it is a 404)
            self.assertTrue(status in (400, 404) or (status == 200 and b"<html" in body.lower() and not body.startswith(b"SECRET")), (target, status))
        status, headers, _ = self.ask("GET", "/maps/files/subdir.lvl")                         # (a folder that looks like a map: nginx adds the slash, and that address is a 404)
        self.assertIn(status, (301, 404))
        if status == 301:
            self.assertEqual(self.names(headers, "location"), ["/maps/files/subdir.lvl/"])
        # the file that IS in the folder is served by its own name (the rig's control)
        status, _, body = self.ask("GET", "/maps/files/secret.lvl")
        self.assertEqual((status, body), (200, b"SECRET2"))

    def test_there_is_no_listing_no_query_and_only_get_and_head(self):
        for target in ("/maps/files/", "/maps/previews/", "/maps/files", "/maps/previews", "/maps/", "/maps/x", "/maps/files/subdir.lvl/", "/maps/files/?x=1"):
            status, _, body = self.ask("GET", target)
            self.assertIn(status, (301, 404), target)
            self.assertNotIn(b"<title>Index of", body, target)
            self.assertNotIn(b"index.html", body, target)
            if status == 301:
                self.fail("a redirect to a listing or to the page: " + target)
        self.assertEqual(self.ask("GET", "/maps/files/" + quote(NAMES[0]) + "?x=1")[0], 404)
        self.assertEqual(self.ask("GET", "/maps/previews/" + quote(PICTURES[NAMES[0]]["picture"]) + "?x=1")[0], 404)
        for method in ("POST", "PUT", "DELETE", "PATCH"):
            self.assertEqual(self.ask(method, "/maps/files/" + quote(NAMES[0]), {"Content-Length": "0"})[0], 405, method)
            self.assertEqual(self.ask(method, "/maps/previews/" + quote(PICTURES[NAMES[0]]["picture"]), {"Content-Length": "0"})[0], 405, method)
            self.assertEqual(self.ask(method, "/maps", {"Content-Length": "0"})[0], 405, method)

    # ---- the list
    def test_the_list_comes_from_the_game_server_as_a_plain_get_and_nothing_of_the_visitor_goes_on(self):
        with StandIn.lock:
            StandIn.seen.clear()
        status, headers, body = self.ask("GET", "/maps", {"Cookie": "a=" + "b" * 3000, "Authorization": "Bearer x", "X-Forwarded-For": "1.2.3.4", "Accept-Encoding": "gzip"})
        self.assertEqual(status, 200)
        self.assertEqual(body, LIST_BODY)
        self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"))
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])          # once: the block hides the server's and keeps the page's
        self.assertEqual(self.names(headers, "cross-origin-embedder-policy"), ["require-corp"])
        with StandIn.lock:
            mine = [(p, h) for p, h in StandIn.seen if p == "/maps"]
        self.assertGreaterEqual(len(mine), 1)
        for _, sent in mine:
            self.assertEqual(sorted(sent), ["connection", "host"])
            self.assertEqual(sent["connection"], "close")

    def test_the_list_is_asked_of_the_server_once_in_five_seconds_for_everybody(self):
        time.sleep(7.1)                                                                        # (the entry of an earlier test has run out: nginx counts whole seconds and keeps an entry up to one more)
        with StandIn.lock:
            StandIn.seen.clear()
        for _ in range(25):
            self.assertEqual(self.ask("GET", "/maps")[0], 200)
        with StandIn.lock:
            asked = len([1 for p, _ in StandIn.seen if p == "/maps"])
        self.assertEqual(asked, 1)

    def test_a_query_a_method_or_a_path_below_the_list_never_reaches_the_server(self):
        with StandIn.lock:
            StandIn.seen.clear()
        self.assertEqual(self.ask("GET", "/maps?x=1")[0], 404)
        self.assertEqual(self.ask("POST", "/maps", {"Content-Length": "0"})[0], 405)
        self.assertEqual(self.ask("DELETE", "/maps")[0], 405)
        self.assertEqual(self.ask("GET", "/maps/")[0], 404)
        self.assertEqual(self.ask("GET", "/maps/TINY.LVL")[0], 404)
        time.sleep(0.1)
        with StandIn.lock:
            self.assertLessEqual(len([1 for p, _ in StandIn.seen if p != "/maps"]), 0)

    def test_the_picture_of_an_original_is_the_front_pages_and_is_not_served_here(self):
        # the six originals' pictures are /front/preview_*.png (web/front/, the web image's own folder); nothing of them is under /maps/
        for name in ("tiny", "small", "medium", "gauntlet", "treasure", "islands"):
            self.assertTrue(os.path.isfile(os.path.join(REPO, "web", "front", "preview_%s.png" % name)), name)
            self.assertEqual(self.ask("GET", "/maps/previews/preview_%s.png" % name)[0], 404)


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the allowances of /maps were NOT run (tests/scripts/test_nginx_maps.py)")
class TheAllowancesRun(Asking, unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rig = Rig(relaxed=False)
        cls.port = cls.rig.port

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()

    def test_a_flood_is_answered_503_by_the_allowance_of_each_kind_and_the_others_stay_free(self):
        name = "/maps/files/" + quote(NAMES[0])
        codes = [self.ask("GET", name)[0] for _ in range(90)]
        self.assertIn(503, codes)                                                              # (20 a second, a burst of 40: 90 at once is more)
        self.assertEqual(codes[0], 200)
        self.assertEqual(self.ask("GET", "/maps/previews/" + quote(PICTURES[NAMES[0]]["picture"]))[0], 200)      # (the pictures have their own)
        self.assertEqual(self.ask("GET", "/maps")[0], 200)                                      # (so has the list)
        codes = [self.ask("GET", "/maps/previews/" + quote(PICTURES[NAMES[0]]["picture"]))[0] for _ in range(600)]
        self.assertIn(503, codes)                                                              # (200 a second, a burst of 400)
        self.assertGreater(codes.count(200), 300)
        codes = [self.ask("GET", "/maps")[0] for _ in range(150)]
        self.assertIn(503, codes)                                                              # (20 a second, a burst of 100)


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the answer of /maps without a door was NOT run (tests/scripts/test_nginx_maps.py)")
class TheListWithoutADoor(unittest.TestCase):
    def test_a_server_that_is_not_there_is_answered_404_with_a_json_body(self):
        work = tempfile.mkdtemp(prefix="ants_nginx_maps_off_")
        try:
            for sub in ("srv/previews", "srv/files", "cache/ants_stats", "cache/ants_history", "tmp"):
                os.makedirs(os.path.join(work, sub))
            dead = free_port()

            def write_conf(port):
                conf = os.path.join(work, "nginx.conf")
                with open(conf, "w") as f:
                    f.write(make_config(work, port, dead))
                return conf
            proc, port = start_nginx(work, write_conf)
            try:
                conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
                conn.request("GET", "/maps")
                r = conn.getresponse()
                body = r.read()
                self.assertEqual(r.status, 404)
                self.assertTrue(r.getheader("Content-Type").startswith("application/json"))
                self.assertEqual(r.getheader("Cache-Control"), "no-store")
                self.assertIn("error", json.loads(body))
                conn.close()
            finally:
                proc.terminate()
                proc.wait(timeout=10)
        finally:
            shutil.rmtree(work, ignore_errors=True)


class TheDocumentsSayWhatIsBuilt(unittest.TestCase):
    def test_the_server_document_names_the_routes_the_folders_and_the_option(self):
        server = read("docs", "SERVER.md")
        for needle in ("GET /maps", "--community-maps", "/community-maps", "/maps/previews/", "/maps/files/", "The list of maps"):
            self.assertIn(needle, server, needle)

    def test_the_header_of_the_file_names_the_zones(self):
        head = RAW.split("server {")[0]
        self.assertIn("(/maps, below)", head)
        self.assertIn("/maps/previews/", head)


if __name__ == "__main__":
    unittest.main()
