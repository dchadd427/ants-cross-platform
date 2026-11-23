#!/usr/bin/env python3
"""docker/nginx.conf's doors for the AI keys (docs/MAP_AI.md): the key pages, the socket of an armed tab, the two doors that an AI uses with a key and the OpenAPI file. Run by ./run_tests.sh --fast and by the CI; the
blocks are RUN where the program nginx is installed.

  - the text: an exact location for each path that the hub has under /hub/ai/ (read from hub/aidoor.py, so a path that the hub gains is not forgotten) and a last one that answers 404 for the rest; the
    checks come before the allowance and before anything is passed on; the key pages get the login's cookie and the page's token and nothing of an AI's; the doors of an AI get the key, the Origin, the type and the
    address and NEVER a cookie; no block adds a header (so there is no CORS header, and a preflight is a 405); the bodies are the hub's own sizes;
  - the real blocks in front of a stand-in hub that writes down what arrives: each path goes through with its headers and body; every other name, method and query is a 404 or a 405; a request that the page did
    not make is a 403 at the key pages; a body over the limit is a 413 and the hub is not asked; a visitor's cookie, forwarded address, referrer and the other headers never reach the doors of an AI; a flood is a
    429; a hub that is not there is the JSON 503 of the chat door; nothing is written to the access log;
  - the whole way, in front of the real hub: a key made in the hub's store is judged by the real hub behind the real blocks.
"""
import json
import os
import re
import shutil
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import hub_rig  # noqa: E402
import test_nginx_hub_admin_api as adm  # noqa: E402
import test_nginx_hub_login as hl  # noqa: E402
import test_nginx_stats as stats  # noqa: E402
import test_nginx_submissions as sub  # noqa: E402
from hub import aidoor, aikeys, aitools, limits as limitmod  # noqa: E402

CONF = stats.strip_comments(stats.read("docker", "nginx.conf"))
HEAD = CONF[:CONF.index("server {")]
COOKIE = "__Host-ants_hub=" + "a" * 32
LIMITS = limitmod.Limits()
PAGES = ("/hub/ai/keys", "/hub/ai/keys/revoke")
DOORS = ("/hub/ai/v1/call", "/hub/ai/mcp")
FILE = "/hub/ai/v1/openapi.json"
SOCKET = "/hub/ai/ws"
BLOCKS = {path: stats.block_of(CONF, "location = %s " % path) for path in PAGES + DOORS + (FILE, SOCKET)}
KEY = "antsai_00000000_" + "a" * 43


class TheText(unittest.TestCase):
    def test_every_path_of_the_hub_has_an_exact_location_and_the_rest_of_the_prefix_is_a_404(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        ai = sorted(l for l in locations if "/hub/ai" in l)
        self.assertEqual(ai, sorted(["= %s" % p for p in PAGES + DOORS + (FILE, SOCKET)] + ["^~ /hub/ai/", "= /hub/ai"]))
        self.assertEqual(set(PAGES + DOORS + (FILE, SOCKET)), set(aidoor.ROUTES) | {aidoor.WS_PATH})
        for rest in ("location ^~ /hub/ai/ ", "location = /hub/ai "):
            self.assertEqual(re.sub(r"\s+", " ", stats.block_of(CONF, rest).strip()), "access_log off; return 404;", rest)
        self.assertNotRegex(CONF, r"location\s+~\*?\s+[^\n]*hub/ai")                        # (a regular-expression location would win over them)
        for location in locations:
            if "/hub/ai" not in location:
                self.assertNotIn("hub/ai", stats.block_of(CONF, "location %s " % location), location)

    def test_the_checks_come_before_the_allowance_and_before_anything_is_passed_on(self):
        for path, block in BLOCKS.items():
            self.assertIn("if ($is_args) { return 404; }", block, path)
            self.assertRegex(block, r'if \(\$request_method (?:!= (?:GET|POST)|!~ "\^\(GET\|POST\)\\z")\) \{ return 405; \}', path)
            self.assertIn("access_log off;", block, path)
            self.assertLess(block.index("$is_args"), block.index("limit_req "), path)
            self.assertLess(block.index("limit_req "), block.index("proxy_pass "), path)
            self.assertIn("limit_req_status ", block, path)
            self.assertIn("error_page 502 504 = @hub_off;", block, path)
        for path in PAGES:
            self.assertIn('if ($http_sec_fetch_site != "same-origin") { return 403; }', BLOCKS[path])
            self.assertLess(BLOCKS[path].index("$http_sec_fetch_site"), BLOCKS[path].index("limit_req "))

    def test_the_key_pages_pass_the_login_and_the_token_and_the_doors_of_an_ai_pass_the_key_and_never_a_cookie(self):
        pages = sorted(["Connection", "Host", "Origin", "Cookie", "Sec-Fetch-Site", "Content-Type", "X-Ants-Csrf", "X-Client-Address"])
        doors = sorted(["Connection", "Host", "Origin", "Authorization", "Content-Type", "X-Client-Address"])
        for path in PAGES:
            self.assertEqual(hl.headers_set(BLOCKS[path]), pages, path)
            self.assertIn("proxy_set_header Cookie $hub_cookie_session;", BLOCKS[path])
        for path in DOORS:
            self.assertEqual(hl.headers_set(BLOCKS[path]), doors, path)
            self.assertNotIn("Cookie", BLOCKS[path])
            self.assertIn("proxy_set_header Authorization $http_authorization;", BLOCKS[path])
        self.assertEqual(hl.headers_set(BLOCKS[FILE]), sorted(["Connection", "Host", "X-Client-Address", "Content-Length"]))
        self.assertNotIn("Cookie", BLOCKS[FILE])
        self.assertEqual(hl.headers_set(BLOCKS[SOCKET]), sorted(["Upgrade", "Connection", "Host", "Sec-WebSocket-Key", "Sec-WebSocket-Version", "Origin", "Cookie", "Sec-Fetch-Site", "X-Client-Address", "Content-Length"]))
        for path, block in BLOCKS.items():
            for line in ("proxy_pass_request_headers off;", "proxy_set_header X-Client-Address $remote_addr;"):
                self.assertIn(line, block, (path, line))
            for forbidden in ("proxy_cache", "add_header", "rewrite", "alias", "root ", "try_files", "sub_filter", "X-Forwarded", "$http_cookie", "$args", "$request_uri", "Access-Control", "4002", "4030", "ants-server"):
                self.assertNotIn(forbidden, block, (path, forbidden))
            if path != SOCKET:
                self.assertIn("proxy_pass http://$ants_hub$uri;", block, path)                      # (the normalized path, never the raw request line)
                self.assertIn('proxy_set_header Connection "close";', block, path)
                self.assertIn("proxy_next_upstream off;", block, path)

    def test_the_bodies_are_the_hubs_own_sizes(self):
        for path in PAGES:
            self.assertEqual(int(re.search(r"client_max_body_size (\d+);", BLOCKS[path]).group(1)), LIMITS.ai_page_body_max, path)
        for path in DOORS:
            self.assertEqual(int(re.search(r"client_max_body_size (\d+)k;", BLOCKS[path]).group(1)) * 1024, LIMITS.ai_body_max, path)
        self.assertIn("proxy_pass_request_body off;", BLOCKS[FILE])
        self.assertIn("proxy_pass_request_body off;", BLOCKS[SOCKET])

    def test_the_socket_is_the_chat_doors_upgrade_with_its_own_connection_zone(self):
        block = BLOCKS[SOCKET]
        for line in ("limit_conn ants_hub_aiconn 8;", "limit_req zone=ants_hub_req burst=10 nodelay;", "proxy_http_version 1.1;", 'proxy_set_header Connection "upgrade";', "proxy_buffering off;", "proxy_read_timeout 120s;"):
            self.assertIn(line, block, line)
        self.assertRegex(HEAD, r"limit_conn_zone \$binary_remote_addr zone=ants_hub_aiconn:1m;")

    def test_the_zones_are_there_and_the_doors_wait_as_long_as_the_hub_does(self):
        for zone in ("ants_hub_aikeys", "ants_hub_ai"):
            self.assertRegex(HEAD, r"limit_req_zone \$binary_remote_addr zone=%s:1m rate=\d+r/[sm];" % zone)
        for path in DOORS:
            wait = int(re.search(r"proxy_read_timeout (\d+)s;", BLOCKS[path]).group(1))
            self.assertGreater(wait, LIMITS.ai_call_seconds + LIMITS.ai_queue_seconds, path)
        self.assertEqual(aikeys.DAYS, (7, 30, 90))


PAGE = {"Sec-Fetch-Site": "same-origin"}


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the doors of the AI keys were NOT run (tests/scripts/test_nginx_hub_ai.py)")
class TheBlocksRun(unittest.TestCase):
    relaxed = True

    @classmethod
    def setUpClass(cls):
        cls.stand, cls.server = adm.start_stand()
        cls.rig = sub.Rig(hub_port=cls.server.server_address[1], relaxed=cls.relaxed, log=True)

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()
        cls.server.shutdown()
        cls.server.server_close()

    def count(self):
        with self.stand.lock:
            return len(self.stand.seen)

    def newest(self):
        with self.stand.lock:
            return self.stand.seen[-1]

    def ask(self, method, path, headers=None, body=None):
        return self.rig.ask(method, path, headers, body)

    def visitor(self, **more):
        return dict({"Cookie": "theme=dark; " + COOKIE + "; track=1", "Origin": "https://site.example", "X-Ants-Csrf": "t" * 64, "Content-Type": "application/json", "Authorization": "Bearer " + KEY,
                     "X-Forwarded-For": "7.7.7.7", "X-Real-IP": "8.8.8.8", "X-Client-Address": "6.6.6.6", "Referer": "https://evil.example/", "User-Agent": "agent/1", "Mcp-Session-Id": "s", "Accept": "application/json"}, **more)

    def test_the_key_pages_go_through_with_the_headers_that_the_hub_needs(self):
        for method, path, body in (("GET", "/hub/ai/keys", None), ("POST", "/hub/ai/keys", b'{"name":"x"}'), ("POST", "/hub/ai/keys/revoke", b'{"id":"00000000"}')):
            before = self.count()
            status, headers, answer = self.ask(method, path, self.visitor(**PAGE), body)
            self.assertEqual((status, json.loads(answer)), (200, {"stand": "hub"}), path)
            self.assertEqual(self.count(), before + 1)
            m, p, raw, got, version = self.newest()
            self.assertEqual((m, p, version, got), (method, path, "HTTP/1.1", body or b""))
            seen = dict((k.lower(), v) for k, v in raw)
            self.assertEqual(sorted(seen), sorted(["connection", "host", "origin", "cookie", "sec-fetch-site", "x-ants-csrf", "x-client-address", "content-type"] + (["content-length"] if body else [])), path)
            self.assertEqual(seen["cookie"], COOKIE)
            self.assertEqual((seen["x-client-address"], seen["origin"], seen["x-ants-csrf"]), ("127.0.0.1", "https://site.example", "t" * 64))
            self.assertEqual([v for k, v in headers if k == "cache-control"], ["no-store"])
            self.assertEqual([v for k, v in headers if k == "x-content-type-options"], ["nosniff"])

    def test_the_doors_of_an_ai_pass_the_key_and_never_a_cookie(self):
        for path in DOORS:
            before = self.count()
            body = b'{"tool":"map_info"}'
            status, headers, answer = self.ask("POST", path, self.visitor(), body)
            self.assertEqual((status, json.loads(answer)), (200, {"stand": "hub"}), path)
            m, p, raw, got, version = self.newest()
            self.assertEqual((m, p, got), ("POST", path, body))
            seen = dict((k.lower(), v) for k, v in raw)
            self.assertEqual(sorted(seen), sorted(["connection", "host", "origin", "authorization", "content-type", "content-length", "x-client-address"]), path)
            self.assertEqual((seen["authorization"], seen["x-client-address"], seen["content-type"]), ("Bearer " + KEY, "127.0.0.1", "application/json"))
            self.assertNotIn("cookie", seen)
            self.assertFalse([k for k, _ in headers if k.startswith("access-control")])

    def test_no_origin_is_made_up_and_the_openapi_file_passes_nothing_of_the_visitor(self):
        self.ask("POST", "/hub/ai/mcp", {"Authorization": "Bearer " + KEY, "Content-Type": "application/json"}, b"{}")
        self.assertEqual(sorted(k.lower() for k, v in self.newest()[2]), ["authorization", "connection", "content-length", "content-type", "host", "x-client-address"])
        self.ask("GET", FILE, self.visitor())
        self.assertEqual(sorted(k.lower() for k, v in self.newest()[2]), ["connection", "host", "x-client-address"])          # (the empty Content-Length of the block is a header that is not sent)

    def test_every_other_name_method_and_query_is_refused_and_the_hub_is_not_asked(self):
        before = self.count()
        cases = [("PUT", p) for p in PAGES + DOORS] + [("DELETE", p) for p in PAGES + DOORS] + [("OPTIONS", p) for p in PAGES + DOORS + (FILE,)] + [("GET", p) for p in DOORS + ("/hub/ai/keys/revoke",)]
        cases += [("POST", p) for p in (FILE,)] + [("HEAD", p) for p in DOORS]
        for method, path in cases:
            status = self.ask(method, path, self.visitor(**PAGE), b"{}" if method in ("POST", "PUT", "DELETE") else None)[0]
            self.assertEqual(status, 405, (method, path))
        for method, path in (("GET", "/hub/ai"), ("GET", "/hub/ai/"), ("POST", "/hub/ai/x"), ("GET", "/hub/ai/keys/"), ("GET", "/hub/ai/keys/x"), ("POST", "/hub/ai/keys/revoke/x"), ("POST", "/hub/ai/v1/call/x"), ("GET", "/hub/ai/v1/"),
                             ("GET", "/hub/ai/KEYS"), ("GET", "/hub/ai/keys%00"), ("POST", "/hub/ai/mcp/"), ("GET", "/hub/ai/v1/openapi.json/"), ("GET", "/hub/ai/ws/")):
            status = self.ask(method, path, self.visitor(**PAGE), b"{}" if method == "POST" else None)[0]
            self.assertIn(status, (400, 404), (method, path))
        for method, path in (("GET", "/hub/ai/keys?x=1"), ("POST", "/hub/ai/mcp?x=1"), ("POST", "/hub/ai/v1/call?key=" + KEY), ("GET", FILE + "?x"), ("GET", SOCKET + "?x=1")):
            self.assertEqual(self.ask(method, path, self.visitor(**PAGE), b"{}" if method == "POST" else None)[0], 404, (method, path))
        self.assertEqual(self.count(), before, "none of them reached the hub")

    def test_a_request_that_the_page_did_not_make_is_a_403_at_the_key_pages(self):
        before = self.count()
        for site in (None, "cross-site", "same-site", "none", "", "same-origin, cross-site", "SAME-ORIGIN"):
            headers = {"Cookie": COOKIE, "Content-Type": "application/json", "X-Ants-Csrf": "t" * 64}
            if site is not None:
                headers["Sec-Fetch-Site"] = site
            for method, path in (("GET", "/hub/ai/keys"), ("POST", "/hub/ai/keys"), ("POST", "/hub/ai/keys/revoke")):
                self.assertEqual(self.ask(method, path, headers, b"{}" if method == "POST" else None)[0], 403, (site, method, path))
        self.assertEqual(self.count(), before)

    def test_the_doors_of_an_ai_take_a_request_from_any_site_that_has_a_key_and_no_other_body_than_64_kb(self):
        before = self.count()
        for site in (None, "cross-site", "same-site", "none"):
            headers = {"Authorization": "Bearer " + KEY, "Content-Type": "application/json"}
            if site:
                headers["Sec-Fetch-Site"] = site
            self.assertEqual(self.ask("POST", "/hub/ai/v1/call", headers, b"{}")[0], 200, site)             # (a key is the right to ask: the hub judges it, and the Origin)
        self.assertEqual(self.count(), before + 4)
        for path in DOORS:
            headers = {"Authorization": "Bearer " + KEY, "Content-Type": "application/json"}
            before = self.count()
            self.assertEqual(self.ask("POST", path, headers, b"x" * 65536)[0], 200)
            self.assertEqual(len(self.newest()[3]), 65536)
            self.assertEqual(self.ask("POST", path, headers, b"x" * 65537)[0], 413)
            self.assertEqual(self.count(), before + 1, "the big one was not passed on")

    def test_a_body_of_512_bytes_goes_through_to_a_key_page_and_one_of_513_does_not(self):
        headers = dict(PAGE, Cookie=COOKIE, **{"Content-Type": "application/json", "X-Ants-Csrf": "t" * 64, "Origin": "https://site.example"})
        before = self.count()
        self.assertEqual(self.ask("POST", "/hub/ai/keys", headers, b"x" * 512)[0], 200)
        self.assertEqual(self.ask("POST", "/hub/ai/keys", headers, b"x" * 513)[0], 413)
        self.assertEqual(self.count(), before + 1)

    def test_a_cookie_of_another_shape_is_not_passed_on_to_the_key_pages(self):
        for cookie in ("__Host-ants_hub=" + "A" * 32, "__Host-ants_hub=" + "a" * 31, "x__Host-ants_hub=" + "a" * 32, "ants_hub=" + "a" * 32, "__Host-ants_oauth=" + "a" * 32):
            self.ask("GET", "/hub/ai/keys", dict(PAGE, Cookie=cookie))
            self.assertNotIn("cookie", [k.lower() for k, v in self.newest()[2]], cookie)

    def log_text(self):
        try:
            with open(self.rig.log_file, encoding="utf-8") as f:
                return f.read()
        except OSError:
            return ""

    def test_nothing_is_written_to_the_access_log(self):
        before = len(self.log_text())
        self.ask("GET", "/hub/ai/keys", dict(PAGE, Cookie=COOKIE))
        self.ask("POST", "/hub/ai/v1/call", {"Authorization": "Bearer " + KEY, "Content-Type": "application/json"}, b'{"tool":"canary-tool-3f7a"}')
        self.ask("POST", "/hub/ai/mcp", {"Authorization": "Bearer " + KEY, "Content-Type": "application/json"}, b"{}")
        self.ask("GET", "/hub/ai/nothing?canary-query-5b81e3", dict(PAGE, Cookie=COOKIE))
        text = self.log_text()[before:]
        for word in ("/hub/ai", "canary", "antsai_"):
            self.assertNotIn(word, text, word)


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the allowance of the doors of the AI keys was NOT run (tests/scripts/test_nginx_hub_ai.py)")
class TheAllowance(unittest.TestCase):
    def test_a_flood_at_the_key_pages_is_a_429_and_is_not_passed_on(self):
        stand, server = adm.start_stand()
        rig = sub.Rig(hub_port=server.server_address[1], relaxed=False)
        try:
            codes = [rig.ask("GET", "/hub/ai/keys", dict(PAGE, Cookie=COOKIE))[0] for _ in range(40)]
            self.assertEqual(codes[:21], [200] * 21)
            self.assertIn(429, codes[21:])
            self.assertEqual(len(stand.seen), codes.count(200))
        finally:
            rig.stop()
            server.shutdown()
            server.server_close()

    def test_a_flood_at_the_doors_of_an_ai_is_a_429_and_is_not_passed_on(self):
        stand, server = adm.start_stand()
        rig = sub.Rig(hub_port=server.server_address[1], relaxed=False)
        try:
            headers = {"Authorization": "Bearer " + KEY, "Content-Type": "application/json"}
            codes = [rig.ask("POST", "/hub/ai/mcp", headers, b"{}")[0] for _ in range(80)]
            self.assertEqual(codes[:41], [200] * 41)
            self.assertIn(429, codes[41:])
            self.assertEqual(len(stand.seen), codes.count(200))
        finally:
            rig.stop()
            server.shutdown()
            server.server_close()


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: a hub that is not there was NOT run (tests/scripts/test_nginx_hub_ai.py)")
class TheHubIsNotThereRuns(unittest.TestCase):
    def test_a_hub_that_is_not_there_is_the_json_503_of_the_chat_door(self):
        rig = sub.Rig(hub_port=hl.DEAD_PORT)
        try:
            for method, path, body in (("GET", "/hub/ai/keys", None), ("POST", "/hub/ai/keys/revoke", b"{}"), ("POST", "/hub/ai/v1/call", b"{}"), ("POST", "/hub/ai/mcp", b"{}"), ("GET", FILE, None)):
                status, headers, answer = rig.ask(method, path, dict(PAGE, Cookie=COOKIE, Authorization="Bearer " + KEY, **{"Content-Type": "application/json"}), body)
                self.assertEqual(status, 503, path)
                self.assertEqual(json.loads(answer), {"error": "hub_off", "message": "Chat is not available right now."})
                self.assertEqual(sub.names(headers, "content-type"), ["application/json"])
                self.assertEqual(sub.names(headers, "cache-control"), ["no-store"])
        finally:
            rig.stop()


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the real hub behind the doors of the AI keys was NOT run (tests/scripts/test_nginx_hub_ai.py)")
class TheWholeWay(unittest.TestCase):
    """A key that is made in the real hub's store, judged by the real hub behind the real blocks: through nginx a key that is good reaches the hub's `not_armed` (nobody has turned a tab on), a key that is wrong is a
    401, the cookie of a visitor does nothing at a door, and the OpenAPI file is the hub's."""

    def test_the_real_hub_behind_the_real_blocks(self):
        with hub_rig.Rig(env={"ANTS_HUB_AI_KEYS": "on"}) as hub:
            account = "9100000000000031"
            made = hub.call(hub.store.ai_key_add, account, "0a1b2c3d", "x", "edit", aikeys.digest("s" * 43), 86400, 3, 5)
            self.assertEqual(made.id, "0a1b2c3d")
            rig = sub.Rig(hub_port=hub.port)
            try:
                headers = {"Authorization": "Bearer antsai_0a1b2c3d_" + "s" * 43, "Content-Type": "application/json"}
                status, answered, text = rig.ask("POST", "/hub/ai/v1/call", headers, b'{"tool":"map_info","args":{}}')
                self.assertEqual((status, json.loads(text)["error"]), (409, "not_armed"))
                status, answered, text = rig.ask("POST", "/hub/ai/v1/call", dict(headers, Authorization="Bearer antsai_0a1b2c3d_" + "t" * 43), b'{"tool":"map_info"}')
                self.assertEqual((status, json.loads(text)["error"]), (401, "bad_key"))
                status, answered, text = rig.ask("POST", "/hub/ai/v1/call", {"Content-Type": "application/json", "Cookie": COOKIE}, b'{"tool":"map_info"}')
                self.assertEqual((status, json.loads(text)["error"]), (401, "bad_key"))
                status, answered, text = rig.ask("GET", FILE)
                self.assertEqual((status, json.loads(text)["openapi"]), (200, "3.1.0"))
                status, answered, text = rig.ask("POST", "/hub/ai/mcp", headers, json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/list"}).encode())
                self.assertEqual((status, len(json.loads(text)["result"]["tools"])), (200, len(aitools.TOOLS)))
                status, answered, text = rig.ask("GET", "/hub/ai/keys", PAGE)
                self.assertEqual((status, json.loads(text)["error"]), (401, "login"))
            finally:
                rig.stop()


if __name__ == "__main__":
    unittest.main()
