#!/usr/bin/env python3
"""docker/nginx.conf routes the Map Maker's test maps to the TEST-MAP door and to nothing else (run by ./run_tests.sh --fast; the blocks are RUN where the program nginx is installed).

The game server's TEST-MAP door (ants_server --test-map-port 4031, docs/MAP_SUBMISSIONS.md "Test maps") is reached by the site's nginx only, for one path:

  - POST /test-maps, exactly: no other method, no path below or beside it; the path that goes on is always /test-maps (never a raw request line);
  - what is passed on: the type of the body, Sec-Fetch-Site and the real client address in X-Client-Address (a header of that name from a visitor never gets through); no cookie, no token, no referer, no cookie
    set by the game server;
  - the limits that nginx holds before the game server is asked: 2 MiB of body, ten a minute for each address (a burst of three), the methods;
  - a game server that is not there, or runs with ANTS_TEST_MAP_PORT=0, is a JSON 503 test_maps_off, never a page of the site; the submissions door is never asked for a test map;
  - the access log has the path and not the query (the name of the map is in it).
"""
import json
import os
import re
import shutil
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_nginx_submissions as rig_module  # noqa: E402

Rig = rig_module.Rig
CONF = rig_module.CONF
RAW = rig_module.RAW
REPO = rig_module.REPO
block_of = rig_module.block_of
names = rig_module.names
HEAD = CONF[:CONF.index("server {")]
MAP = {"Content-Type": "application/octet-stream"}


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()

QUERY = "/test-maps?kind=classic&name=Popcorn%20Remix"


class TheText(unittest.TestCase):
    def test_the_location_is_there_once_and_exact(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual(locations.count("= /test-maps"), 1)
        self.assertEqual(locations.count("@test_maps_off"), 1)
        self.assertEqual([l for l in locations if "test" in l and "map" in l], ["= /test-maps", "@test_maps_off"])      # (no prefix and no regular expression catches a path of the name)
        self.assertEqual(CONF.count("location = /test-maps {"), 1)
        self.assertNotRegex(CONF, r"location\s+(\^~|~\*?)\s+[^\n]*test-maps")

    def test_the_block_checks_before_it_passes_and_names_what_it_passes_on(self):
        block = block_of(CONF, "location = /test-maps ")
        for check in ("$request_method", "limit_req zone=ants_test_maps burst=3 nodelay;", "client_max_body_size 2m;"):
            self.assertIn(check, block)
            self.assertLess(block.index(check), block.index("proxy_pass"), check)
        for line in [l.strip() for l in block.splitlines() if l.strip().startswith("if (")]:
            self.assertRegex(line, r"\{ return \d{3}; \}$")
        self.assertIn("if ($request_method != POST) { return 405; }", block)
        self.assertIn("proxy_pass http://$ants_test_maps$uri$is_args$args;", block)                   # (the normalized path goes on, never the raw request line)
        self.assertIn("set $ants_test_maps ants-server:4031;", block)
        self.assertNotIn("ants_submit", block)                                                        # (the submissions door is nowhere in it)
        self.assertNotIn("4030", block)
        self.assertIn("proxy_pass_request_headers off;", block)
        self.assertIn('proxy_set_header Connection "close";', block)
        self.assertIn("proxy_hide_header Set-Cookie;", block)
        self.assertEqual(re.findall(r"proxy_set_header\s+(\S+)", block), ["Connection", "Host", "Content-Type", "Sec-Fetch-Site", "X-Client-Address"])
        self.assertIn("proxy_set_header X-Client-Address $remote_addr;", block)
        self.assertIn("error_page 502 504 = @test_maps_off;", block)
        self.assertIn("access_log /dev/stdout ants_noquery;", block)
        off = block_of(CONF, "location @test_maps_off ")
        self.assertIn('return 503 \'{"error":"test_maps_off"', off)
        self.assertIn('add_header Cache-Control "no-store" always;', off)

    def test_the_zone_is_its_own_and_only_a_post_counts(self):
        self.assertRegex(HEAD, r"map \$request_method \$test_map_post_key \{\s+default \"\";\s+POST \$binary_remote_addr;\s+\}")
        self.assertIn("limit_req_zone $test_map_post_key zone=ants_test_maps:1m rate=10r/m;", HEAD)
        for other in ("ants_submit_post", "ants_submit_read", "ants_admin_api"):
            self.assertNotIn("zone=" + other, block_of(CONF, "location = /test-maps "))

    def test_main_wires_the_test_maps_through_the_two_functions_that_a_test_holds(self):
        main = read("src", "ants_server", "main.cpp")
        self.assertEqual(main.count("connect_test_maps("), 1)                                           # (test_test_maps TM3.1 holds what they do; main() must call them, once each)
        self.assertEqual(main.count("upkeep_test_maps("), 1)
        for line in ("set_in_use(", "set_on_changed(", "set_test_maps("):
            self.assertNotIn(line, main, line)                                                          # (no wiring of its own in main(): that would not be held by a test)
        self.assertLess(main.index("connect_test_maps("), main.index("while (!g_stop)"))               # (wired before the loop, upkept inside it)
        self.assertGreater(main.index("upkeep_test_maps("), main.index("while (!g_stop)"))

    def test_the_stacks_and_the_image_start_the_door_and_the_documents_name_it(self):
        for name in ("docker-compose.stack.yml", "docker-compose.staging.yml"):
            text = "\n".join(l for l in read(name).splitlines() if not l.lstrip().startswith("#"))
            self.assertIn('"--test-map-port", "${ANTS_TEST_MAP_PORT:-4031}"', text, name)                  # (on by default: nothing for the owner to switch)
            self.assertNotIn("4031:4031", text, name)                                                      # (not published on the host)
        self.assertIn("ANTS_TEST_MAP_PORT", read("docker-compose.staging.yml"))
        self.assertIn("--test-map-any-interface", read("Dockerfile.server"))
        self.assertIn("4031", RAW)
        doc = read("docs", "MAP_SUBMISSIONS.md")
        for word in ("/test-maps", "ANTS_TEST_MAP_PORT", "4031", "test_maps_off"):
            self.assertIn(word, doc)
        self.assertEqual(doc.count("test=1"), 1)                                                      # (the old form is named once, as gone)
        self.assertRegex(doc, r"The old form `POST /submissions\?test=1` is gone")


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the test-map block of docker/nginx.conf was NOT run (tests/scripts/test_nginx_test_maps.py)")
class TheBlockRuns(unittest.TestCase):
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

    def test_a_map_goes_to_the_test_door_with_the_few_headers_and_nothing_else_of_the_visitor(self):
        rig = self.rig
        before_real = self.count(rig.door)
        status, headers, body = rig.ask("POST", QUERY, {"Content-Type": "application/octet-stream", "Cookie": "ants_admin=abc", "Authorization": "Bearer secret", "X-Client-Address": "6.6.6.6",
                                                         "X-Forwarded-For": "7.7.7.7", "Referer": "http://evil.example/", "User-Agent": "attacker/1", "Sec-Fetch-Site": "same-origin", "X-Real-IP": "8.8.8.8"}, b"m" * 1000)
        self.assertEqual(status, 201)
        self.assertEqual(names(headers, "set-cookie"), [])                                            # (a player's call never gets a cookie, whatever the game server sent)
        method, path, seen, length = self.last(rig.test_door)
        self.assertEqual((method, path, length), ("POST", QUERY, 1000))
        self.assertEqual((seen.get("content-type"), seen.get("x-client-address"), seen.get("sec-fetch-site"), seen.get("connection")), ("application/octet-stream", "127.0.0.1", "same-origin", "close"))
        for gone in ("cookie", "authorization", "x-forwarded-for", "referer", "user-agent", "x-real-ip"):
            self.assertNotIn(gone, seen)
        self.assertEqual(self.count(rig.door), before_real)                                           # (the submissions door is never asked for a test map)

    def test_only_a_post_is_let_through_and_only_to_this_path(self):
        rig = self.rig
        before = self.count(rig.test_door)
        for method in ("GET", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"):
            body = b"" if method in ("PUT", "PATCH") else None
            self.assertEqual(rig.ask(method, QUERY, MAP, body)[0], 405, method)
        for target in ("/test-maps/", "/test-maps/x", "/test-maps%2f", "/TEST-MAPS", "/test-map", "/test-mapsx", "/test-maps.html", "/test-maps/x/y"):
            status, headers, body = rig.ask("POST", target + "?kind=classic&name=X", MAP, b"m")
            self.assertIn(status, (404, 405), target)
            self.assertNotIn(b"stub", body, target)
        rig.ask("POST", "/test-maps/..%2fsubmissions?kind=classic&name=X", MAP, b"m")                 # (a way up and out is cleaned by nginx and is another route's business: the test door never sees it)
        self.assertEqual(self.count(rig.test_door), before)                                           # (nothing else of the name ever reaches the test door)

    def test_the_path_that_goes_on_is_always_the_one_path(self):
        rig = self.rig
        for target in ("//test-maps?kind=classic&name=A", "/./test-maps?kind=classic&name=A", "/x/../test-maps?kind=classic&name=A", "/%74est-maps?kind=classic&name=A"):         # (cleaned up by nginx: the game server sees the clean path)
            self.assertEqual(rig.ask("POST", target, MAP, b"m")[0], 201, target)
            self.assertEqual(self.last(rig.test_door)[1], "/test-maps?kind=classic&name=A", target)

    def test_the_size_is_held_before_the_game_server_is_asked(self):
        rig = self.rig
        before = self.count(rig.test_door)
        self.assertEqual(rig.ask_head_only("POST", QUERY, 2 * 1024 * 1024 + 1, MAP), 413)
        self.assertEqual(rig.ask_head_only("POST", "/test-maps?kind=extended&name=A", 10 * 1024 * 1024, MAP), 413)
        self.assertEqual(rig.ask("POST", QUERY, MAP, b"x" * (2 * 1024 * 1024 + 1))[0], 413)
        self.assertEqual(self.count(rig.test_door), before)
        self.assertEqual(rig.ask("POST", "/test-maps?kind=extended&name=A", MAP, b"e" * (2 * 1024 * 1024))[0], 201)          # (2 MiB is the most)
        self.assertEqual(self.last(rig.test_door)[3], 2 * 1024 * 1024)
        self.assertEqual(rig.ask("POST", QUERY, MAP, b"")[0], 201)                                    # (an empty body is the game server's to refuse)


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the allowance of docker/nginx.conf for the test maps was NOT run (tests/scripts/test_nginx_test_maps.py)")
class TheAllowance(unittest.TestCase):
    def test_sending_has_an_allowance_of_its_own_that_the_real_sends_do_not_use(self):
        rig = Rig(relaxed=False)
        try:
            results = [rig.ask("POST", QUERY, MAP, b"m" * 100)[0] for _ in range(12)]
            self.assertEqual(results[:4], [201, 201, 201, 201])                                       # (ten a minute with a burst of three: the fourth in a row still passes ...)
            self.assertEqual(set(results[4:]), {503})                                                  # ... and a flood is turned away here
            self.assertEqual(rig.ask("POST", "/submissions?kind=classic&name=Abc&maker=Mia", MAP, b"m" * 100)[0], 201)          # (the real door's allowance is a zone of its own)
            self.assertEqual(rig.ask("GET", QUERY)[0], 405)                                           # (a GET is no try)
        finally:
            rig.stop()


class TheDoorIsDown(unittest.TestCase):
    @unittest.skipUnless(shutil.which("nginx"), "nginx is not installed")
    def test_a_game_server_without_the_door_is_a_json_503_never_a_page_of_the_site(self):
        rig = Rig()
        try:
            rig.test_door_server.shutdown()
            rig.test_door_server.server_close()
            status, headers, text = rig.ask("POST", QUERY, MAP, b"m")
            self.assertEqual(status, 503)
            self.assertEqual(json.loads(text)["error"], "test_maps_off")
            self.assertEqual(names(headers, "cache-control"), ["no-store"])
            self.assertEqual(names(headers, "x-content-type-options"), ["nosniff"])
            self.assertEqual(names(headers, "content-type"), ["application/json"])
            status, headers, text = rig.ask("POST", "/submissions?kind=classic&name=Abc&maker=Mia", MAP, b"m")          # (the real door is another door: it answers as it did)
            self.assertEqual(status, 201)
        finally:
            rig.stop()

    @unittest.skipUnless(shutil.which("nginx"), "nginx is not installed")
    def test_the_real_door_off_does_not_switch_test_rooms_off(self):
        rig = Rig()
        try:
            rig.door_server.shutdown()
            rig.door_server.server_close()
            self.assertEqual(json.loads(rig.ask("POST", "/submissions?kind=classic&name=Abc&maker=Mia", MAP, b"m")[2])["error"], "submissions_off")
            self.assertEqual(rig.ask("POST", QUERY, MAP, b"m")[0], 201)
        finally:
            rig.stop()


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the access log of the test maps was NOT run (tests/scripts/test_nginx_test_maps.py)")
class TheLogHasNoQuery(unittest.TestCase):
    def test_the_name_of_a_map_is_not_in_the_log_and_the_path_is(self):
        rig = Rig(log=True)
        try:
            self.assertEqual(rig.ask("POST", "/test-maps?kind=classic&name=Secret%20Name", MAP, b"m" * 100)[0], 201)
            time.sleep(0.3)
            for _ in range(50):
                with open(rig.log_file, encoding="utf-8") as f:
                    text = f.read()
                if text:
                    break
                time.sleep(0.05)
            self.assertNotIn("Secret", text)
            self.assertNotIn("?", text)
            self.assertIn('"POST /test-maps HTTP/1.1" 201 ', text)
        finally:
            rig.stop()


if __name__ == "__main__":
    unittest.main()
