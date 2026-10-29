#!/usr/bin/env python3
"""docker/nginx.conf routes the public list of games: GET /lobbies (run by ./run_tests.sh --fast; the blocks are RUN where the program nginx is installed).

ants_server lists the lobby rooms that a newcomer can join on the same read-only replay port as the recorded and the live matches (docs/SERVER.md "Lobby rooms": nine fields for each row, and no
address, key or chat); the page's nginx forwards it like /live (tests/scripts/test_nginx_live.py), as the one request it takes and nothing else:

  - /lobbies is an exact location, the only one of its name: only a GET without a query goes through (405 and 404 otherwise), the checks come before the allowance, and the server is always asked
    for /lobbies, however the visitor spelled the address;
  - nothing of the visitor's request is passed on but Host and Connection, and the connection is not kept;
  - an allowance of its own, a hundred a second with a burst of two hundred, 503 beyond (the key is the address nginx sees: the whole site's while the reverse proxy is not named in
    docker/trusted-proxies.conf);
  - one cache entry for everybody, TWO seconds long, in the cache of /stats: the server is asked about once in two seconds however many look; only a 200 is kept; an answer is served old only while
    a new one is being fetched, never because the server fails, hangs up or refuses (a game that is listed after it has begun or was closed is a card that fails when it is pressed); a server that
    HANGS holds one request for the read timeout (3 seconds) and the others get the old list meanwhile;
  - a server that has no such door (it is not there, or it was started without the replay port) is answered 404 with {"error":"lobbies_off"}, a JSON type and Cache-Control: no-store.

The text of the file is checked everywhere. Where the program nginx is installed the real file is RUN: a copy with the folders and the address of the stand-in server put in, in a temporary folder,
so that what reaches the server, and what does not, is seen. (The real ants_server with a real room is tests/scripts/test_ants_server.sh: its public door, asked directly.) The broken copies of the
block that this must notice are tests/data/nginx_lobbies_mutants.json.
"""
import concurrent.futures
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
import quick_http  # noqa: E402
import test_nginx_maps as maps  # noqa: E402  (the helpers of the rigs that run the program nginx)

read = maps.read
block_of = maps.block_of
RAW = maps.RAW
CONF = maps.CONF
HEAD = CONF[:CONF.index("server {")]                                                          # what is declared outside the server block: the zones and the cache
BLOCK = block_of(CONF, "location = /lobbies ")
OFF = block_of(CONF, "location @lobbies_off ")
LIVE = block_of(CONF, "location = /live ")
REPLAYS_OFF = block_of(CONF, "location @replays_off ")
# What the stand-in answers on the other doors that share the cache of /stats (the page of the front, the recorded matches, the maps): each has an entry of its own there
OTHER_DOORS = {"/stats": b'{"games":7}', "/replays": b'{"replays":[],"count":0}', "/maps": b'{"format":1,"maps":[]}'}
ROW = ("code", "host", "map", "mode", "people", "bots", "free", "size", "age")             # the nine fields of a row (docs/SERVER.md "Lobby rooms")
NO_NGINX = "nginx is not installed: the blocks of docker/nginx.conf for /lobbies were NOT run (tests/scripts/test_nginx_lobbies.py)"
OFF_BODY = b'{"error":"lobbies_off"}'
# Spellings that nginx reads as the very address /lobbies (a doubled slash, a dot segment, a percent escape, an empty query, a fragment): each is a request for the list
SPELLINGS = ("/lobbies", "/lobbies?", "//lobbies", "///lobbies", "/./lobbies", "/a/../lobbies", "/%6cobbies", "/%6Cobbies", "/%2flobbies", "/lobbies#fragment", "/lo%62bies")


class TheText(unittest.TestCase):
    def test_the_location_is_there_once_exact_and_the_only_one_of_its_name(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual([l for l in locations if "lobbies" in l], ["= /lobbies", "@lobbies_off"])     # (no prefix and no regular-expression location could also catch /lobbies/x)
        self.assertEqual(len(re.findall(r"location\s+= /lobbies\s*\{", CONF)), 1)
        self.assertEqual(len(re.findall(r"location\s+@lobbies_off\s*\{", CONF)), 1)
        self.assertNotRegex(CONF, r"location\s+(\^~|~\*?)\s+[^\n]*lobbies")
        self.assertEqual([l for l in locations if "live" in l or "replays" in l], ["= /replays", "^~ /replays/", "= /live", "^~ /live/", "@replays_off"])      # (the neighbours are as they were)

    def test_only_a_plain_get_without_a_query_goes_through_and_the_checks_come_first(self):
        self.assertIn("if ($request_method != GET) { return 405; }", BLOCK)
        self.assertIn("if ($is_args) { return 404; }", BLOCK)
        for check in ("$request_method", "$is_args"):
            self.assertLess(BLOCK.index(check), BLOCK.index("limit_req "))               # (a refused request costs nothing of the allowance)
            self.assertLess(BLOCK.index(check), BLOCK.index("proxy_cache "))             # (and never reaches the cache)
            self.assertLess(BLOCK.index(check), BLOCK.index("proxy_pass"))
        conditions = [l.strip() for l in BLOCK.splitlines() if l.strip().startswith("if (")]
        self.assertEqual(len(conditions), 2)
        for line in conditions:
            self.assertRegex(line, r"\{ return \d{3}; \}$")                              # ("if" is only safe with a return: nothing else may sit in one)

    def test_nothing_of_the_visitor_goes_on_and_the_connection_is_not_kept(self):
        for directive in ("proxy_pass_request_headers off;", "proxy_pass_request_body off;", 'proxy_set_header Content-Length "";', 'proxy_set_header Connection "close";',
                          "proxy_set_header Host $host;", "proxy_http_version 1.1;"):
            self.assertIn(directive, BLOCK)
        self.assertEqual(sorted(h.lower() for h in re.findall(r"proxy_set_header\s+(\S+)", BLOCK)), ["connection", "content-length", "host"])      # (no cookie, however big, reaches the server)
        self.assertNotIn("upgrade", BLOCK.lower())

    def test_the_server_is_always_asked_for_the_same_path_at_the_replay_port_with_the_name_resolved_per_request(self):
        self.assertIn("proxy_pass http://$ants_replays/lobbies;", BLOCK)                  # (a constant path: no spelling of the visitor's, no query, goes on)
        self.assertNotRegex(BLOCK, r"proxy_pass[^;]*\$(uri|request_uri|is_args|args)")
        theirs = re.search(r"set \$ants_replays ([a-z0-9.-]+):(\d+);", LIVE)
        mine = re.search(r"set \$ants_replays ([a-z0-9.-]+):(\d+);", BLOCK)
        self.assertIsNotNone(mine)
        self.assertEqual(mine.groups(), theirs.groups())                                  # the replay port (4020 in the container, not published on the host): there is no door of its own
        self.assertIn("resolver 127.0.0.11", BLOCK)                                       # (a variable and a resolver: the name is looked up per request, so the page starts without the server)
        self.assertEqual(len(re.findall(r"\bproxy_pass\b", BLOCK)), 1)

    def test_the_allowance_is_the_whole_sites_and_a_zone_of_its_own_outside_the_server(self):
        zone = re.search(r"limit_req_zone \$binary_remote_addr zone=ants_lobbies:(\d+)m rate=(\d+)r/([sm]);", CONF)
        self.assertIsNotNone(zone)
        self.assertLess(CONF.index(zone.group(0)), CONF.index("server {"))                # http context: before the server block
        self.assertEqual((int(zone.group(1)), int(zone.group(2)), zone.group(3)), (1, 100, "s"))
        self.assertIn("limit_req zone=ants_lobbies burst=200 nodelay;", BLOCK)
        self.assertIn("limit_req_status 503;", BLOCK)                                     # (like the other zones: a flood is 503)
        self.assertEqual(len(re.findall(r"\bants_lobbies\b", CONF)), 2)                    # the zone and its one use: no other block counts against it
        self.assertEqual(re.findall(r"limit_req zone=(\w+)", BLOCK), ["ants_lobbies"])     # (and this block counts against no other zone)

    def test_the_list_is_one_entry_for_two_seconds_in_the_cache_of_the_numbers_and_only_a_200_is_kept(self):
        for directive in ("proxy_cache ants_stats_cache;", 'proxy_cache_key "ants-lobbies";', "proxy_cache_valid 200 2s;", "proxy_ignore_headers Cache-Control;", "proxy_cache_lock on;"):
            self.assertIn(directive, BLOCK)
        self.assertEqual(len(re.findall(r"\bproxy_cache_valid\b", BLOCK)), 1)             # (only a 200: an error of the server is asked again at the next request)
        self.assertEqual(sorted(re.findall(r"^\s*(proxy_cache\w*|proxy_no_cache|proxy_ignore_headers)\b", BLOCK, re.M)),
                         sorted(["proxy_cache", "proxy_cache_key", "proxy_cache_valid", "proxy_ignore_headers", "proxy_cache_lock", "proxy_cache_use_stale"]))      # (no bypass, no revalidation, no background update)
        self.assertLess(BLOCK.index("limit_req "), BLOCK.index("proxy_cache "))           # (a request over the limit never reaches the cache)
        self.assertLess(BLOCK.index("proxy_cache "), BLOCK.index("proxy_pass"))
        mine = "ants-lobbies"
        others = [k for k in re.findall(r'proxy_cache_key "([^"]+)";', CONF) if k != mine]
        self.assertEqual(re.findall(r'proxy_cache_key "([^"]+)";', BLOCK), [mine])
        self.assertNotRegex(mine, r"\$")                                                  # (one entry for everybody: the key holds nothing of the visitor)
        for key in others:
            self.assertFalse(key.startswith(mine) or mine.startswith(key), key)           # (no other door's entry is this one's)
        path = re.search(r"proxy_cache_path (\S+) keys_zone=ants_stats_cache:(\d+)m max_size=(\d+)m", HEAD)
        self.assertIsNotNone(path)
        self.assertEqual(int(path.group(3)), 8)                                           # (the list is 6 kB at the most: the cache of the numbers has the room)

    def test_an_old_answer_is_served_only_while_a_new_one_is_fetched_never_because_the_server_is_silent(self):
        self.assertIn("proxy_cache_use_stale updating;", BLOCK)
        self.assertEqual(len(re.findall(r"proxy_cache_use_stale", BLOCK)), 1)
        self.assertNotRegex(BLOCK, r"proxy_cache_use_stale[^;]*\b(error|timeout|http_\d+|invalid_header|off)\b")

    def test_a_server_without_the_door_is_answered_404_with_the_json_of_its_own(self):
        self.assertEqual(re.findall(r"error_page\s+[^;]*;", BLOCK), ["error_page 502 504 = @lobbies_off;"])
        self.assertIn("default_type application/json;", OFF)
        self.assertIn('add_header Cache-Control "no-store" always;', OFF)
        self.assertEqual(len(re.findall(r"add_header X-Content-Type-Options", OFF)), 1)     # (a location with a header of its own does not inherit the server's)
        self.assertNotIn("proxy_pass", OFF)
        body = re.search(r"return 404 '(\{.*\})';", OFF)
        self.assertIsNotNone(body)
        self.assertEqual(json.loads(body.group(1)), {"error": "lobbies_off"})
        self.assertNotEqual(body.group(1), re.search(r"return 404 '(\{.*\})';", REPLAYS_OFF).group(1))      # (the page tells the two apart by the word)

    def test_the_block_is_the_live_block_but_for_the_zone_the_key_the_path_and_the_fallback(self):
        def lines(block, *own):
            return [l.strip() for l in block.splitlines() if l.strip() and not l.strip().startswith(own)]
        own = ("limit_req zone=", "proxy_cache_key", "proxy_pass ", "error_page", "proxy_read_timeout")
        self.assertEqual(lines(BLOCK, *own), lines(LIVE, *own))                           # (one door, one set of headers, checks, cache rules and the other timeouts: they cannot drift apart)

    def test_it_has_short_timeouts_and_adds_nothing_of_its_own(self):
        for directive, limit in (("proxy_connect_timeout", 5), ("proxy_read_timeout", 3), ("proxy_send_timeout", 15)):
            value = re.search(r"%s (\d+)s;" % directive, BLOCK)
            self.assertIsNotNone(value, directive)
            self.assertLessEqual(int(value.group(1)), limit, directive)               # (the read timeout is how long one request waits for a server that hangs, and how long the others see the old list)
        for forbidden in ("add_header", "expires", "alias", "root", "try_files", "rewrite", "sub_filter", "proxy_intercept_errors", "proxy_next_upstream"):
            self.assertNotRegex(BLOCK, r"\b%s\b" % forbidden, forbidden)                   # (the server's own Cache-Control: no-store is the answer's header)
        self.assertIn("proxy_hide_header X-Content-Type-Options;", BLOCK)                  # (the server-level nosniff line applies once)


class TheDocumentsSayWhatIsBuilt(unittest.TestCase):
    def test_the_documents_name_the_route_the_zone_and_the_tests(self):
        server = read("docs", "SERVER.md")
        for needle in ("`/lobbies`", "ants_lobbies", "lobbies_off"):
            self.assertIn(needle, server, needle)
        testing = read("docs", "TESTING.md")
        for needle in ("test_nginx_lobbies.py", "nginx_lobbies_mutants.json"):
            self.assertIn(needle, testing, needle)

    def test_the_header_of_the_file_names_the_zone(self):
        self.assertIn("(/lobbies, below)", RAW.split("server {")[0])


# ------------------------------------------------------------------------------------------------------------------------------------------------------------ the blocks, run
def list_body(n):
    """The list that the stand-in's n-th request is answered with: the nine fields of a row, and a host name that tells the answers apart."""
    row = {"code": "k7m2xq", "host": "Ada%d" % n, "map": "TINY.LVL", "mode": "highest-score", "people": 2, "bots": 0, "free": 2, "size": 4, "age": n}
    return json.dumps({"format": 1, "lobbies": [row], "count": 1}, separators=(",", ":")).encode()


class Handler(BaseHTTPRequestHandler):
    """The replay port of the game server, as far as /lobbies goes: the list for a GET, a 404 for any other path and a 405 for any other method (like the real door), and it remembers what it was sent."""
    stand_in = None

    def handle_any(self):
        stand_in = self.stand_in
        length = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(length) if length else b""
        with stand_in.lock:
            stand_in.seen.append({"method": self.command, "path": self.path, "version": self.request_version, "headers": dict((k.lower(), v) for k, v in self.headers.items()), "body": len(body)})
            number = len([1 for s in stand_in.seen if s["method"] == "GET" and s["path"] == "/lobbies"])
            mode, status, delay = stand_in.mode, stand_in.status, stand_in.delay
        try:
            if self.command != "GET":
                self.answer(405, b'{"error":"method not allowed"}', {"Allow": "GET"})
            elif self.path in OTHER_DOORS:
                self.answer(200, OTHER_DOORS[self.path])
            elif self.path != "/lobbies":
                self.answer(404, b'{"error":"not found"}')
            elif mode == "reset":
                self.connection.shutdown(socket.SHUT_RDWR)                                 # (the server hangs up without a word)
            elif mode == "status":
                self.answer(status, b'{"error":"stand-in"}')
            else:
                time.sleep(delay)                                                           # (mode "slow" answers late; mode "ok" has no delay)
                self.answer(200, list_body(number))
        except (BrokenPipeError, ConnectionResetError):                                     # (nginx gave up and went: not this test's business)
            pass

    def answer(self, status, data, more=None):
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")                                       # (the real door's headers: no browser keeps an answer)
        self.send_header("X-Content-Type-Options", "nosniff")
        for key, value in (more or {}).items():
            self.send_header(key, value)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    do_GET = do_POST = do_PUT = do_DELETE = do_PATCH = do_HEAD = do_OPTIONS = handle_any

    def log_message(self, *args):
        pass


class Upstream:
    """The stand-in server: `mode` is "ok", "slow" (answers after `delay` seconds), "status" (answers `status`) or "reset" (hangs up); `seen` is what it was sent, in order."""
    def __init__(self):
        self.lock = threading.Lock()
        self.reset()
        handler = type("StandIn", (Handler,), {"stand_in": self})
        self.port = maps.free_port()
        self.server = quick_http.QuickThreadingHTTPServer(("127.0.0.1", self.port), handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def reset(self):
        with self.lock:
            self.seen = []
            self.mode, self.status, self.delay = "ok", 500, 0.0

    def close(self):
        self.server.shutdown()
        self.server.server_close()

    def lists(self):
        """the requests for the list that it has been sent so far"""
        with self.lock:
            return [s for s in self.seen if s["path"] == "/lobbies"]

    def count(self):
        with self.lock:
            return len(self.seen)

    def wait_for(self, count, wait=10.0):
        deadline = time.monotonic() + wait
        while self.count() < count:
            if time.monotonic() > deadline:
                raise AssertionError("the stand-in was sent %d requests, %d were expected within %.0f s" % (self.count(), count, wait))
            time.sleep(0.01)


class Rig:
    """The real file in front of the stand-in server, in a temporary folder. restart() starts nginx again with an empty cache and the whole allowance, so that no test depends on another."""
    def __init__(self, stub_port):
        self.work = tempfile.mkdtemp(prefix="ants_nginx_lobbies_")
        self.stub_port = stub_port
        self.proc = None
        self.port = 0
        try:
            self.start()
        except BaseException:                                                               # (an nginx that does not start leaves no folder behind)
            self.close()
            raise

    def folders(self):
        for sub in ("srv/previews", "srv/files", "cache/ants_stats", "cache/ants_history", "tmp", "html"):
            os.makedirs(os.path.join(self.work, sub), exist_ok=True)
        for name in ("index.html", "lobby.html"):
            with open(os.path.join(self.work, "html", name), "w", encoding="utf-8") as f:
                f.write("<html>the page of the site</html>")

    def config(self, port):
        text = maps.make_config(self.work, port, self.stub_port).replace("/usr/share/nginx/html", self.work + "/html")
        return text.replace("set $ants_server ants-server:4002;", "set $ants_server 127.0.0.1:%d;" % self.stub_port)        # (/stats is the WebSocket port's: the stand-in answers it too)

    def start(self):
        self.folders()

        def write_conf(port):
            conf = os.path.join(self.work, "nginx.conf")
            with open(conf, "w", encoding="utf-8") as f:
                f.write(self.config(port))
            return conf
        self.proc, self.port = maps.start_nginx(self.work, write_conf)

    def stop_nginx(self):
        if self.proc is not None:
            self.proc.terminate()
            self.proc.wait(timeout=10)
            self.proc = None

    def restart(self):
        self.stop_nginx()
        shutil.rmtree(os.path.join(self.work, "cache"), ignore_errors=True)               # (the cache's files go with its memory)
        self.start()

    def close(self):
        self.stop_nginx()
        shutil.rmtree(self.work, ignore_errors=True)


def ask(port, method, target, headers=None, body=None, timeout=20):
    """(status, headers as a list of (name, value), body) of one request with the target exactly as written (no normalizing)"""
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    try:
        conn.putrequest(method, target, skip_host=False, skip_accept_encoding=True)
        for k, v in (headers or {}).items():
            conn.putheader(k, v)
        conn.endheaders(body)
        r = conn.getresponse()
        return r.status, [(k.lower(), v) for k, v in r.getheaders()], r.read()
    finally:
        conn.close()


class Running:
    """What the classes below share: a stand-in server and the real file in front of it, started again (empty cache, whole allowance) for every test."""
    @classmethod
    def setUpClass(cls):
        cls.upstream = Upstream()
        try:
            cls.rig = Rig(cls.upstream.port)
        except BaseException:
            cls.upstream.close()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.rig.close()
        cls.upstream.close()

    def setUp(self):
        self.upstream.reset()
        self.rig.restart()

    def get(self, target="/lobbies", headers=None, method="GET", body=None):
        return ask(self.rig.port, method, target, headers, body)

    def names(self, headers, name):
        return [v for k, v in headers if k == name]

    def sleep_until(self, started, seconds):
        time.sleep(max(0.0, started + seconds - time.monotonic()))

    def align(self):
        """Waits until the wall clock is a little way into a second. nginx counts the life of an entry in whole seconds (an entry of "2s" made at 10.9 is good until 13.0, one made at 10.0 until 13.0 too,
        so it lives between two and three seconds): a test that asks whether it is still there after 1.95 s must know where in its second the entry was made."""
        while not 0.05 <= time.time() % 1.0 <= 0.3:
            time.sleep(0.02)

    def expired(self):
        """One list is fetched and kept; returns once its two seconds are over (nginx counts whole seconds: an entry made at the end of a second is good for the rest of the next one, so 2.5 s is not always
        enough) and the entry is old. Returns the body that was kept."""
        started = time.monotonic()
        status, _, body = self.get()
        self.assertEqual(status, 200)
        self.sleep_until(started, 3.2)
        return body


@unittest.skipUnless(shutil.which("nginx"), NO_NGINX)
class TheBlockRuns(Running, unittest.TestCase):
    def test_a_get_goes_to_the_server_as_a_plain_get_and_nothing_of_the_visitor_goes_on(self):
        visitor = {"Cookie": "session=" + "c" * 8100, "User-Agent": "a-browser/1.0", "Authorization": "Bearer a-secret", "X-Forwarded-For": "203.0.113.9", "Referer": "https://example.org/page",
                   "Origin": "https://example.org", "Accept-Language": "de", "X-Custom": "yes", "Sec-Fetch-Site": "same-origin", "Accept": "*/*", "Pragma": "no-cache", "Cache-Control": "no-cache",
                   "If-None-Match": '"abc"', "Range": "bytes=0-5", "X-Real-IP": "203.0.113.9", "Forwarded": "for=203.0.113.9", "Accept-Encoding": "gzip, br", "X-Client-Address": "6.6.6.6", "Upgrade": "websocket"}
        status, headers, body = self.get(headers=visitor)
        self.assertEqual(status, 200)
        self.assertEqual(body, list_body(1))
        self.assertEqual(json.loads(body)["lobbies"][0].keys(), set(ROW))                  # (nine fields a row, as the server makes them)
        self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"))
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])                # the server's own line, once: no browser keeps the list
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])        # once: the block hides the server's and keeps the page's
        self.assertEqual(self.names(headers, "cross-origin-opener-policy"), ["same-origin"])
        self.assertEqual(self.names(headers, "cross-origin-embedder-policy"), ["require-corp"])
        self.assertEqual(self.names(headers, "set-cookie"), [])
        sent = self.upstream.seen
        self.assertEqual(len(sent), 1)
        self.assertEqual((sent[0]["method"], sent[0]["path"], sent[0]["version"], sent[0]["body"]), ("GET", "/lobbies", "HTTP/1.1", 0))
        self.assertEqual(sorted(sent[0]["headers"]), ["connection", "host"])                # Host and Connection, and not one more (no cookie, token, address or type)
        self.assertEqual(sent[0]["headers"]["connection"], "close")
        self.assertEqual(sent[0]["headers"]["host"], "127.0.0.1")                           # (the visitor's host, without its port)

    def test_a_body_on_a_get_is_not_passed_on(self):
        status, _, body = self.get(headers={"Content-Length": "11", "Content-Type": "text/plain"}, body=b"hello world")
        self.assertEqual((status, body), (200, list_body(1)))
        sent = self.upstream.seen[0]
        self.assertEqual((sent["body"], sorted(sent["headers"])), (0, ["connection", "host"]))

    def test_every_other_method_is_405_and_never_reaches_the_server_even_with_a_query_or_a_list_in_the_cache(self):
        self.assertEqual(self.get()[0], 200)                                                # (the list is kept now)
        before = self.upstream.count()
        for method in ("POST", "PUT", "DELETE", "PATCH", "OPTIONS"):
            self.assertEqual(self.get(method=method, body=b"", headers={"Content-Length": "0"})[0], 405, method)
            self.assertEqual(self.get("/lobbies?x=1", method=method, body=b"", headers={"Content-Length": "0"})[0], 405, method)     # (the method is judged first)
        self.assertEqual(self.get(method="POST", body=b"x" * 5000, headers={"Content-Length": "5000"})[0], 405)
        self.assertEqual(self.get(method="HEAD")[0], 405)                                   # (only a GET: not even a HEAD)
        self.assertEqual(self.upstream.count(), before)
        status, _, body = self.get(method="POST", body=b"", headers={"Content-Length": "0"})
        self.assertNotIn(b"lobbies", body)                                                  # (and a refusal is not the list)

    def test_a_query_is_404_even_when_the_list_is_in_the_cache_and_never_reaches_the_server(self):
        self.assertEqual(self.get()[0], 200)
        before = self.upstream.count()
        for query in ("?x=1", "?x", "?limit=5", "?a=b&c=d", "?cache=bust", "?%00", "?x=" + "a" * 3000, "?=", "?&"):
            status, _, body = self.get("/lobbies" + query)
            self.assertEqual(status, 404, query[:20])                                       # a query is no request for the list
            self.assertNotIn(b"Ada", body, query[:20])                                      # (and the kept list is not the answer to it)
        self.assertEqual(self.upstream.count(), before)
        self.assertEqual(self.get("/lobbies?")[0], 200)                                     # (a "?" with nothing behind it carries no query: nginx's $args is empty)

    def test_the_neighbours_of_the_address_are_the_game_page_and_are_not_the_list_or_passed_on(self):
        self.assertEqual(self.get()[0], 200)                                                # (the list is kept: a prefix location would answer these from it)
        before = self.upstream.count()
        for path in ("/lobbies/", "/lobbies/x", "/lobbies/x/y", "/lobbies/..%2fx", "/lobbies.json", "/lobbies%2f", "/lobbies%20", "/lobbies%2e", "/lobbies;x=1", "/Lobbies", "/LOBBIES", "/lobbie",
                     "/lobbiess", "/lobbies_", "/x/lobbies", "/lobbies-off", "/lobby"):
            status, _, body = self.get(path)
            self.assertEqual(status, 200, path)                                             # (the try_files of the site: the game page, as any unknown address)
            self.assertIn(b"the page of the site", body, path)
            self.assertNotIn(b"Ada", body, path)
        self.assertIn(self.get("/lobbies%00")[0], (400, 404))                              # (a NUL in the address: nginx refuses it itself)
        self.assertEqual(self.upstream.count(), before)                                     # none of these reached the server

    def test_however_the_address_is_spelled_the_server_is_asked_for_lobbies_and_nothing_else(self):
        for spelling in SPELLINGS:                                                          # (a fresh cache for each, so that the server sees every one)
            self.rig.restart()
            self.upstream.reset()
            status, _, body = self.get(spelling)
            self.assertEqual((status, body), (200, list_body(1)), spelling)
            self.assertEqual([(s["method"], s["path"]) for s in self.upstream.seen], [("GET", "/lobbies")], spelling)

    def test_the_lists_entry_is_the_lists_and_not_the_numbers_the_recorded_matches_the_maps_or_the_live_matches(self):
        # (they share one cache: each has a key of its own, and none may be answered with another's entry, whichever was asked first)
        for first in ("the list", "the others"):
            self.rig.restart()
            self.upstream.reset()
            if first == "the list":
                self.assertEqual(self.get()[2], list_body(1))
            for path, data in OTHER_DOORS.items():
                self.assertEqual(self.get(path)[2], data, path)
            self.assertEqual(self.get()[2], list_body(1))                                    # the list, and not the entry of /stats, /replays or /maps
            for path, data in OTHER_DOORS.items():
                self.assertEqual(self.get(path)[2], data, path)                             # (and each of those is still its own)
            status, _, body = self.get("/live")                                             # (the stand-in has no live board: its 404, not the list from the entry of /lobbies)
            self.assertEqual((status, body), (404, b'{"error":"not found"}'))
            self.assertEqual(self.get()[2], list_body(1))                                    # (and the 404 of /live is no entry of /lobbies)
            paths = [s["path"] for s in self.upstream.seen]
            for path in ["/lobbies", "/live"] + list(OTHER_DOORS):
                self.assertEqual(paths.count(path), 1, (path, paths))                       # each was asked of the server once: the rest came from the entry of its own

    def test_a_404_of_a_server_that_has_no_such_list_passes_as_it_is_and_is_asked_again(self):
        self.upstream.mode, self.upstream.status = "status", 404                            # (an older build of the game server: the door answers 404 for the path)
        for _ in range(3):
            status, headers, body = self.get()
            self.assertEqual((status, body), (404, b'{"error":"stand-in"}'))
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"])
        self.assertEqual(len(self.upstream.lists()), 3)                                      # (only a 200 is kept)


@unittest.skipUnless(shutil.which("nginx"), NO_NGINX)
class TheCacheRuns(Running, unittest.TestCase):
    def test_the_server_is_asked_once_in_two_seconds_for_everybody(self):
        self.align()
        started = time.monotonic()
        first = self.get()
        self.assertEqual((first[0], first[2]), (200, list_body(1)))
        for i in range(8):
            status, headers, body = self.get(headers={"Cookie": "visitor=%d" % i, "User-Agent": "visitor-%d" % i, "X-Forwarded-For": "203.0.113.%d" % i})
            self.assertEqual((status, body), (200, first[2]))                               # the same list for everybody
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"])            # (and a browser is still told to keep nothing)
            self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(len(self.upstream.lists()), 1)                                      # the server was asked once for the nine
        self.sleep_until(started, 1.95)
        self.assertEqual(self.get()[2], first[2])                                           # still there after nearly two seconds (an entry of one second would be gone)
        self.assertEqual(len(self.upstream.lists()), 1)
        self.sleep_until(started, 3.2)                                                      # two seconds are over
        renewed = self.get()
        self.assertEqual((renewed[0], renewed[2]), (200, list_body(2)))                     # asked again, and the answer is the server's new one
        self.assertEqual(self.get()[2], renewed[2])
        self.assertEqual(len(self.upstream.lists()), 2)                                      # the next ones came from that

    def test_every_spelling_of_the_address_is_the_one_entry(self):
        self.assertEqual(self.get()[2], list_body(1))
        for spelling in SPELLINGS:
            self.assertEqual(self.get(spelling)[2], list_body(1), spelling)                 # (a made-up spelling is no entry of its own: it cannot push the numbers out of the small cache)
        self.assertEqual(len(self.upstream.lists()), 1)

    def test_a_crowd_at_an_empty_cache_asks_the_server_once(self):
        self.upstream.mode, self.upstream.delay = "slow", 0.6                                # (the first answer is slow: the others arrive while it is fetched, and wait for it)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            answers = list(pool.map(lambda i: self.get(headers={"Cookie": "visitor=%d" % i}), range(8)))
        for status, _, body in answers:
            self.assertEqual((status, body), (200, list_body(1)))
        self.assertEqual(len(self.upstream.lists()), 1)

    def test_while_the_list_is_renewed_the_old_one_is_served_at_once_and_nobody_waits(self):
        old = self.expired()
        self.upstream.mode, self.upstream.delay = "slow", 2.0                                # (the renewal takes two seconds)
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            renewing = pool.submit(self.get)
            self.upstream.wait_for(2)                                                       # the renewal is on its way
            started = time.monotonic()
            status, headers, body = self.get()
            took = time.monotonic() - started
            self.assertEqual((status, body), (200, old))                                    # the old list, because a new one is being fetched
            self.assertLess(took, 1.0)                                                      # and at once
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"])
            status, _, renewed = renewing.result(timeout=15)
        self.assertEqual((status, renewed), (200, list_body(2)))
        self.assertEqual(self.get()[2], list_body(2))                                       # the next one is the new list, from the cache
        self.assertEqual(len(self.upstream.lists()), 2)


class TheServerFailsRuns(Running, unittest.TestCase):
    """A list that is old is never served because the server fails or hangs up: the entry is there, and the answer is the truth."""
    def assert_off(self, answer):
        status, headers, body = answer
        self.assertEqual((status, body), (404, OFF_BODY))
        self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"))
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])

    def test_an_error_of_the_server_is_passed_on_and_is_never_covered_by_the_old_list_and_is_not_kept(self):
        old = self.expired()
        for code in (500, 503):
            self.upstream.mode, self.upstream.status = "status", code
            before = len(self.upstream.lists())
            for _ in range(2):
                status, headers, body = self.get()
                self.assertEqual((status, body), (code, b'{"error":"stand-in"}'), code)     # the server's own answer (the old list is not served, and not 404 lobbies_off either)
                self.assertNotEqual(body, old)
            self.assertEqual(len(self.upstream.lists()) - before, 2, code)                  # (and an error is not kept: it is asked again)
        self.upstream.mode = "ok"
        status, _, body = self.get()
        self.assertEqual(status, 200)
        self.assertNotEqual(body, old)                                                      # the server is back: the next answer is its own
        self.assertEqual(self.get()[2], body)

    def test_a_server_that_hangs_up_is_answered_404_lobbies_off_at_once_to_everybody_and_not_the_old_list(self):
        old = self.expired()
        self.upstream.mode = "reset"
        for _ in range(3):
            started = time.monotonic()
            answer = self.get()
            self.assert_off(answer)
            self.assertNotEqual(answer[2], old)
            self.assertLess(time.monotonic() - started, 2.0)                                # (at once, each time: nobody waits for a server that has hung up)
        self.upstream.mode = "ok"
        status, _, body = self.get()
        self.assertEqual(status, 200)                                                       # the server is back, and the 404 was not kept
        self.assertEqual(json.loads(body)["count"], 1)


@unittest.skipUnless(shutil.which("nginx"), NO_NGINX)
class TheServerHangsRuns(Running, unittest.TestCase):
    """A server that takes the connection and says nothing: the request that renews waits for the read timeout (3 seconds in the file) and is answered 404 lobbies_off, never the old list; the others
    are served the old list meanwhile (the cost of "an old answer while a new one is fetched"), and the next one to find the entry old waits in its turn."""
    def test_the_request_that_waits_gets_lobbies_off_after_the_read_timeout_and_the_others_get_the_old_list_meanwhile(self):
        old = self.expired()
        self.upstream.mode, self.upstream.delay = "slow", 8.0                               # (the stand-in answers after eight seconds: the file gives up after three)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            started = time.monotonic()
            renewing = pool.submit(self.get)
            self.upstream.wait_for(2)                                                       # the renewal is on its way, and hangs
            began = time.monotonic()
            status, _, body = self.get()
            self.assertEqual((status, body), (200, old))                                    # the old list for the second request,
            self.assertLess(time.monotonic() - began, 1.0)                                  # at once
            answer = renewing.result(timeout=15)
            took = time.monotonic() - started
            self.assertEqual((answer[0], answer[2]), (404, OFF_BODY))                       # and the one that waited gets the truth, not the old list
            self.assertTrue(self.names(answer[1], "content-type")[0].startswith("application/json"))
            self.assertEqual(self.names(answer[1], "cache-control"), ["no-store"])
            self.assertGreater(took, 2.5)                                                   # after the read timeout of the file (3 seconds)
            self.assertLess(took, 5.0)                                                      # and not after ten
            # the next request finds the entry old and no renewal going: it waits in its turn, and is not given the old list either
            started = time.monotonic()
            waiting = pool.submit(self.get)
            self.upstream.wait_for(3)
            self.assertEqual(self.get()[2], old)                                            # (the one beside it is, meanwhile)
            answer = waiting.result(timeout=15)
            self.assertEqual((answer[0], answer[2]), (404, OFF_BODY))
            self.assertGreater(time.monotonic() - started, 2.5)
        self.upstream.mode, self.upstream.delay = "ok", 0.0                                 # the server is back: the next answer is its own, and the 404 was not kept
        status, _, body = self.get()
        self.assertEqual(status, 200)
        self.assertNotEqual(body, old)


@unittest.skipUnless(shutil.which("nginx"), NO_NGINX)
class TheAllowanceRuns(Running, unittest.TestCase):
    def flood(self, count, connection):
        codes = []
        for i in range(count):
            connection.request("GET", "/lobbies", headers={"X-Forwarded-For": "198.51.100.%d" % (i % 250), "Cookie": "visitor=%d" % i, "User-Agent": "visitor-%d" % i})
            r = connection.getresponse()
            body = r.read()
            codes.append(r.status)
            if r.status == 503:
                self.assertNotIn(b"Ada", body)                                              # (a refusal is not the list)
        return codes

    def test_a_hundred_a_second_with_a_burst_of_two_hundred_and_the_rest_is_503(self):
        # (every request has an address, a cookie and an agent of its own: the key of the allowance is the address of the connection, which a visitor cannot choose)
        connection = http.client.HTTPConnection("127.0.0.1", self.rig.port, timeout=30)    # (one connection: the flood is as fast as it can be)
        try:
            started = time.monotonic()
            codes = self.flood(201, connection)
            burst_took = time.monotonic() - started
            codes += self.flood(449, connection)
            took = time.monotonic() - started
            allowed = codes.count(200)
            self.assertTrue(set(codes) <= {200, 503}, set(codes))
            self.assertEqual(codes[:201], [200] * 201)                                      # the first request and the burst of 200 (none refused, none between)
            self.assertLess(burst_took, 1.5)                                                # ("nodelay": a burst that is spread over the rate would take two seconds)
            self.assertTrue(201 <= allowed <= 201 + int(100 * took) + 1, (allowed, took))    # and a hundred a second while this goes on
            self.assertIn(503, codes)                                                       # 650 at once are more than the allowance (503: the file's own limit_req_status)
            self.assertLessEqual(len(self.upstream.lists()), 2)                              # (a flood is the cache's: the server is asked once, or twice when the entry ran out)
            # the allowance is refilled at a hundred a second: after a second of quiet about a hundred more go through
            quiet = time.monotonic()
            time.sleep(1.0)
            second = self.flood(150, connection)
            over = time.monotonic() - quiet
            self.assertGreaterEqual(second.count(200), 93, second.count(200))
            self.assertLessEqual(second.count(200), int(100 * over) + 4, (second.count(200), over))
        finally:
            connection.close()
        for path in ("/live", "/replays", "/maps"):                                         # a zone of its own: the other doors of the same address are not out of allowance
            self.assertNotEqual(self.get(path)[0], 503, path)


@unittest.skipUnless(shutil.which("nginx"), NO_NGINX)
class TheDoorIsOff(unittest.TestCase):
    """A server that was started without --replay-port (ANTS_REPLAY_PORT=0), or is not there: nothing listens at the replay port, so the connection is refused."""
    @classmethod
    def setUpClass(cls):
        cls.rig = Rig(maps.free_port())                                                      # (a port that nobody listens at)

    @classmethod
    def tearDownClass(cls):
        cls.rig.close()

    def get(self, target="/lobbies", **kw):
        return ask(self.rig.port, kw.pop("method", "GET"), target, **kw)

    def names(self, headers, name):
        return [v for k, v in headers if k == name]

    def test_the_list_is_404_with_a_json_body_at_once_and_each_time(self):
        for _ in range(3):
            started = time.monotonic()
            status, headers, body = self.get()
            self.assertEqual((status, body), (404, OFF_BODY))
            self.assertLess(time.monotonic() - started, 3.0)                                 # (refused at once, not the timeout of a server that says nothing)
            self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"))
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"])
            self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])
            self.assertNotIn(b"nginx", body)                                                 # (not nginx's own page)

    def test_the_fallback_is_the_lists_own_and_the_rest_of_the_site_goes_on(self):
        status, _, body = self.get("/live")
        self.assertEqual(status, 404)
        self.assertNotEqual(body, OFF_BODY)                                                  # (the recorded matches have a word of their own: the page tells them apart)
        self.assertIn(b"recorded matches", body)
        status, _, body = self.get("/")
        self.assertEqual((status, body), (200, b"<html>the page of the site</html>"))

    def test_the_checks_are_the_same_with_no_server(self):
        self.assertEqual(self.get(method="POST", body=b"", headers={"Content-Length": "0"})[0], 405)
        self.assertEqual(self.get(method="DELETE")[0], 405)
        self.assertEqual(self.get("/lobbies?x=1")[0], 404)
        status, _, body = self.get("/lobbies?x=1")
        self.assertNotEqual(body, OFF_BODY)                                                  # (a query is refused by the block, not by the missing server)


if __name__ == "__main__":
    unittest.main()
