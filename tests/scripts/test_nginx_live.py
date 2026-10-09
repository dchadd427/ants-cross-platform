#!/usr/bin/env python3
"""docker/nginx.conf routes the matches that run now: GET /live and GET /live/<id> (run by ./run_tests.sh --fast and by the CI where docker works).

ants_server lists the matches of its rooms that run now and hands out the snapshot of one (docs/REPLAYS.md "On the game server", "Live") on the same read-only port as the recorded matches
(--replay-port: no secret; the list holds the names that the players typed, and no address, room code or chat). The stack file starts it that way, so both are PUBLIC; the page's nginx forwards
the two addresses like /replays (tests/scripts/test_nginx_replays.py), as the one request each takes and nothing else:

  - /live is an exact location and /live/ a prefix location, the only ones of the name (the named location of /replays answers when the server has no such door);
  - only a GET without a query goes through (405 and 404 otherwise); an id of the wrong shape is 404 here and never reaches the server (the board decides which exist);
  - nothing of the visitor's request is passed on but Host and Connection, and the connection is not kept;
  - allowances of their own, sized for the whole site (behind a reverse proxy every visitor has the proxy's address) and not shared with the recorded matches': twenty a second for the list,
    thirty for the snapshots (503 beyond);
  - both are cached for TWO seconds in the cache of /stats (the list in one entry, a snapshot in one entry for each id), only a 200 is kept, an answer is served old only while a new one is being fetched
    (never because the server does not answer: a snapshot that is repeated for minutes looks like a match that goes on), and a server that has no such door is answered 404 with a JSON body.

The text of the file is checked everywhere. Where docker works the blocks are RUN: the real file in front of a stand-in server (a second nginx that logs what it was sent), so that what reaches the
server, and what does not, is seen. (The real ants_server with a real match is tests/scripts/test_ants_server.sh, part replays: its public door, asked directly.)
"""
import os
import re
import shutil
import subprocess
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_nginx_stats as stats  # noqa: E402  (the stand-in rig of the stats tests: a page's nginx in front of a second nginx that logs what it was sent)

read = stats.read
strip_comments = stats.strip_comments
block_of = stats.block_of

CONF = strip_comments(read("docker", "nginx.conf"))
HEAD = CONF[:CONF.index("server {")]                                                          # what is declared outside the server block: the zones and the cache
LIST = block_of(CONF, "location = /live ")
SNAPSHOT = block_of(CONF, "location ^~ /live/ ")
OFF = block_of(CONF, "location @replays_off ")
REPLAYS_LIST = block_of(CONF, "location = /replays ")
REPLAYS_FILES = block_of(CONF, "location ^~ /replays/ ")
STATS = block_of(CONF, "location = /stats ")
BOTH = (("the list", LIST), ("the snapshot", SNAPSHOT))

# An id of the server's board (live_board.hpp): <map: 1 - 24 of A-Z a-z 0-9 _>-<YYYYMMDD>-<HHMMSS>Z[-<2 .. 9999>]
GOOD_IDS = ["TREASURE-20261009-082104Z", "TREASURE-20261009-082104Z-2", "TINY-20261008-000000Z-9999", "a_B9-19700101-000000Z", "A" * 24 + "-20261008-143209Z"]
BAD_IDS = ["", "x", "-20261008-143209Z", "A" * 25 + "-20261008-143209Z", "TREASURE-2026108-143209Z", "TREASURE-20261008-14320Z", "TREASURE-20261008-143209", "TREASURE-20261008-143209z",
           "TREASURE-20261008-143209Z-12345", "TREASURE-20261008-143209Z-", "TREASURE-20261008-143209Z.antsrep", "TREASURE-20261008-143209Z\n", "TREA SURE-20261008-143209Z",
           "TREA.SURE-20261008-143209Z", "TREA-SURE-20261008-143209Z", "../TREASURE-20261008-143209Z", "sub/TREASURE-20261008-143209Z", "TREASURE-20261008-143209Z/", "TREASURE-20261008-143209Z/x",
           "TREASURE-20261008-143209Z-2-3"]
SNAPSHOT_ID = GOOD_IDS[0]
OTHER_ID = GOOD_IDS[1]
GONE_ID = "GONE-20261008-143209Z"                                                             # (the stand-in knows no such match and says so)
LIST_REQUEST = "GET /live HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"


def id_pattern():
    """The regular expression of the snapshot location, as Python reads it (nginx's \\z, the end of the text and not before a last line feed, is \\Z here)."""
    match = re.search(r'if \(\$uri !~ "([^"]+)"\) \{ return 404; \}', SNAPSHOT)
    return re.compile(match.group(1).replace("\\z", "\\Z")) if match else None


class TheLocations(unittest.TestCase):
    def test_the_two_addresses_have_their_two_locations_and_no_other_location_could_catch_them(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual([l for l in locations if "live" in l], ["= /live", "^~ /live/"])     # (no regular-expression location could also catch /live/x)
        self.assertEqual(len(re.findall(r"location\s+= /live\s*\{", CONF)), 1)
        self.assertEqual(len(re.findall(r"location\s+\^~ /live/\s*\{", CONF)), 1)
        self.assertEqual([l for l in locations if "replays" in l], ["= /replays", "^~ /replays/", "@replays_off"])     # (the recorded matches are as they were)

    def test_only_a_plain_get_without_a_query_goes_through(self):
        for what, block in BOTH:
            self.assertIn("if ($request_method != GET) { return 405; }", block, what)
            self.assertIn("if ($is_args) { return 404; }", block, what)
            for check in ("$request_method", "$is_args"):
                self.assertLess(block.index(check), block.index("limit_req "), what)         # (a refused request costs nothing of the allowance)
                self.assertLess(block.index(check), block.index("proxy_pass"), what)         # (and nothing is passed on before the checks)

    def test_an_id_that_the_board_could_not_have_made_is_refused_here(self):
        self.assertNotIn("$uri !~", LIST)                                                     # (the list is one exact address)
        pattern = id_pattern()
        self.assertIsNotNone(pattern, "the snapshot location has no check of the id")
        self.assertLess(SNAPSHOT.index("$uri !~"), SNAPSHOT.index("limit_req "))
        self.assertLess(SNAPSHOT.index("$uri !~"), SNAPSHOT.index("proxy_cache "))            # (an id that is refused is no key of the cache)
        self.assertLess(SNAPSHOT.index("$uri !~"), SNAPSHOT.index("proxy_pass"))
        for name in GOOD_IDS:
            self.assertTrue(pattern.match("/live/" + name), name)
        for name in BAD_IDS:
            self.assertFalse(pattern.match("/live/" + name), repr(name))
        self.assertFalse(pattern.match("/live/"))
        self.assertFalse(pattern.match("/live"))
        self.assertFalse(pattern.match("/lives/" + SNAPSHOT_ID))
        self.assertFalse(pattern.match("/replays/" + SNAPSHOT_ID))

    def test_the_pattern_says_what_the_board_and_the_store_say(self):
        board = read("include", "ants_server", "live_board.hpp")
        same_second = int(re.search(r"kMaxSameSecond = (\d+);", board).group(1))
        stem = int(re.search(r"kMaxStemChars = (\d+);", read("src", "ants_server", "replay_store.cpp")).group(1))
        text = re.search(r'if \(\$uri !~ "([^"]+)"\)', SNAPSHOT).group(1)
        self.assertIn("[A-Za-z0-9_]{1,%d}" % stem, text)                                      # the stem of the map's name
        self.assertIn("(-[0-9]{1,%d})?" % len(str(same_second)), text)                       # a later match of the same second
        self.assertIn("[0-9]{8}-[0-9]{6}Z", text)
        self.assertTrue(text.endswith("\\z"), text)                                           # (\z: the end of the id, not before a last line feed)
        self.assertIn("Z[-<1 to 4 digits>]", board)                                           # (the header of the board says the same shape in words)

    def test_nothing_of_the_visitor_goes_on_and_the_connection_is_not_kept(self):
        for what, block in BOTH:
            self.assertIn("proxy_pass_request_headers off;", block, what)                     # (no cookie, however big, reaches the server)
            self.assertIn("proxy_pass_request_body off;", block, what)
            self.assertIn('proxy_set_header Content-Length "";', block, what)
            self.assertIn('proxy_set_header Connection "close";', block, what)
            self.assertIn("proxy_set_header Host $host;", block, what)
            self.assertEqual(sorted(h.lower() for h in re.findall(r"proxy_set_header\s+(\S+)", block)), ["connection", "content-length", "host"], what)
            self.assertNotIn("upgrade", block.lower(), what)
            self.assertIn("proxy_http_version 1.1;", block, what)

    def test_the_blocks_are_the_replays_blocks_but_for_the_id_check_the_allowance_and_the_cache(self):
        def lines(block, *own):
            return [l.strip() for l in block.splitlines() if l.strip() and not l.strip().startswith(own)]
        own = ("if ($uri", "limit_req zone=", "proxy_cache", "proxy_ignore_headers")
        self.assertEqual(lines(LIST, *own), lines(REPLAYS_LIST, *own))                        # (one door, one set of timeouts, one set of checks, one fallback: they cannot drift apart)
        self.assertEqual(lines(SNAPSHOT, *own), lines(REPLAYS_FILES, *own))
        self.assertEqual(lines(LIST, *own), lines(SNAPSHOT, *own))

    def test_they_go_to_the_replay_port_of_the_same_server_as_the_recorded_matches(self):
        for what, block in BOTH:
            target = re.search(r"set \$ants_replays ([a-z0-9.-]+):(\d+);", block)
            theirs = re.search(r"set \$ants_replays ([a-z0-9.-]+):(\d+);", REPLAYS_LIST)
            self.assertIsNotNone(target, what)
            self.assertEqual(target.groups(), theirs.groups(), what)                          # the replay port (4020 in the container, not published on the host): there is no door of its own
            self.assertIn("proxy_pass http://$ants_replays;", block, what)                    # (a variable: the name is resolved per request, so the page starts without the server)
            self.assertIn("resolver 127.0.0.11", block, what)


class TheAllowancesAndTheCache(unittest.TestCase):
    def test_each_address_has_an_allowance_of_its_own_sized_for_the_whole_site_and_the_zones_are_outside_the_server(self):
        # (behind a reverse proxy every visitor has the proxy's address: docs/audit/site_stats_notes.md; an allowance per visitor would be one for everybody)
        for zone_name, block, rate, burst in (("ants_live", LIST, 20, 100), ("ants_live_files", SNAPSHOT, 30, 60)):
            zone = re.search(r"limit_req_zone \$binary_remote_addr zone=%s:(\d+)m rate=(\d+)r/([sm]);" % zone_name, CONF)
            self.assertIsNotNone(zone, zone_name)
            self.assertLess(CONF.index(zone.group(0)), CONF.index("server {"))              # http context: before the server block
            self.assertEqual((int(zone.group(1)), int(zone.group(2)), zone.group(3)), (1, rate, "s"), zone_name)
            self.assertIn("limit_req zone=%s burst=%d nodelay;" % (zone_name, burst), block, zone_name)
            self.assertIn("limit_req_status 503;", block, zone_name)
        self.assertEqual(sorted(re.findall(r"limit_req_zone [^;]*\bzone=(\w+):", HEAD)),
                         ["ants_busy", "ants_history", "ants_history_items", "ants_live", "ants_live_files", "ants_local", "ants_replay_files", "ants_replays", "ants_stats"])      # (a flood of one is not another's)
        for block in (LIST, SNAPSHOT):
            self.assertNotRegex(block, r"zone=ants_(replays|replay_files|stats|busy|local)\b")      # (the recorded matches' allowances are theirs: a crowd that watches does not use up the downloads')
        self.assertNotIn("zone=ants_live ", SNAPSHOT)
        self.assertNotIn("zone=ants_live_files ", LIST)

    def test_both_are_cached_for_two_seconds_in_the_cache_of_the_numbers_and_only_a_200_is_kept(self):
        for what, block in BOTH:
            for directive in ("proxy_cache ants_stats_cache;", "proxy_cache_valid 200 2s;", "proxy_ignore_headers Cache-Control;", "proxy_cache_lock on;"):
                self.assertIn(directive, block, what + directive)
            self.assertEqual(len(re.findall(r"\bproxy_cache_valid\b", block)), 1, what)       # (only a 200: the 404 of a match that is over, or of an id that never was, is asked of the server each time)
            self.assertLess(block.index("limit_req "), block.index("proxy_cache "), what)     # (a request over the limit never reaches the cache)
            self.assertLess(block.index("proxy_cache "), block.index("proxy_pass"), what)

    def test_an_old_answer_is_served_only_while_a_new_one_is_fetched_never_because_the_server_is_silent(self):
        for what, block in BOTH:
            self.assertIn("proxy_cache_use_stale updating;", block, what)                     # (the numbers and the recorded matches' list also serve one on error and timeout: a list that is old is a list)
            self.assertNotRegex(block, r"proxy_cache_use_stale[^;]*\b(error|timeout|http_\d+|invalid_header)\b", what)
            self.assertEqual(len(re.findall(r"proxy_cache_use_stale", block)), 1, what)

    def test_the_list_is_one_entry_and_a_snapshot_has_one_for_each_id_in_keys_that_no_other_door_has(self):
        list_key = re.search(r'proxy_cache_key "([^"]+)";', LIST).group(1)
        snapshot_key = re.search(r'proxy_cache_key "([^"]+)";', SNAPSHOT).group(1)
        self.assertNotRegex(list_key, r"\$")                                                  # (one entry for everybody: the key holds nothing of the visitor)
        self.assertEqual(snapshot_key, list_key + "$uri")                                     # (the uri of a request that passed the check of the id: one entry for each match)
        others = [re.search(r'proxy_cache_key "([^"]+)";', b).group(1) for b in (STATS, REPLAYS_LIST)]
        for key in others:
            self.assertFalse(key.startswith(list_key) or list_key.startswith(key), (key, list_key))      # (no other door's entry is this one's)
        self.assertTrue(snapshot_key.replace("$uri", "/live/x").startswith(list_key))
        self.assertNotIn(list_key, "".join(others))
        for block in (STATS, REPLAYS_LIST):
            self.assertNotIn("$uri", block)

    def test_the_cache_has_room_for_snapshots_and_the_files_are_not_cached(self):
        path = re.search(r"proxy_cache_path (\S+) keys_zone=ants_stats_cache:(\d+)m max_size=(\d+)m", HEAD)
        self.assertIsNotNone(path)
        self.assertEqual(int(path.group(3)), 8)                                               # (a snapshot is some tens of kilobytes and 1 MiB at the most; the numbers' test allows 8 MB at the most)
        for directive in ("proxy_cache", "proxy_cache_key", "proxy_cache_valid", "proxy_ignore_headers"):
            self.assertNotRegex(REPLAYS_FILES, r"\b%s\b" % directive, directive)             # (the files of the recorded matches are as they were: nothing of them is kept here)

    def test_it_has_short_timeouts_and_adds_nothing_of_its_own(self):
        for what, block in BOTH:
            for directive, limit in (("proxy_connect_timeout", 5), ("proxy_read_timeout", 15), ("proxy_send_timeout", 15)):
                value = re.search(r"%s (\d+)s;" % directive, block)
                self.assertIsNotNone(value, what + directive)
                self.assertLessEqual(int(value.group(1)), limit, what + directive)
            for forbidden in ("add_header", "expires", "alias", "root", "try_files", "rewrite", "sub_filter", "proxy_intercept_errors"):
                self.assertNotRegex(block, r"\b%s\b" % forbidden, what + forbidden)           # (the server's own Cache-Control: no-store is the answer's header, and its 404 JSON passes)
            self.assertIn("proxy_hide_header X-Content-Type-Options;", block, what)          # (the server-level nosniff line applies once)

    def test_a_server_without_the_door_is_answered_404_with_a_json_body_by_the_named_location_of_the_replays(self):
        for what, block in BOTH:
            self.assertIn("error_page 502 504 = @replays_off;", block, what)
        self.assertIn("return 404 '{", OFF)
        self.assertIn("default_type application/json;", OFF)
        self.assertIn('add_header Cache-Control "no-store" always;', OFF)
        self.assertEqual(len(re.findall(r"add_header X-Content-Type-Options", OFF)), 1)      # (a location with a header of its own does not inherit the server's)
        self.assertNotIn("proxy_pass", OFF)
        self.assertIn('"error"', re.search(r"return 404 '(\{.*\})';", OFF).group(1))


class TheDocumentsSayWhatIsBuilt(unittest.TestCase):
    def test_the_documents_name_the_routes_and_the_numbers(self):
        server = read("docs", "SERVER.md")
        replays = read("docs", "REPLAYS.md")
        for needle in ("GET /live", "GET /live/<id>", "two seconds", "ants_live"):
            self.assertIn(needle, server + read("docker", "nginx.conf"), needle)
        for needle in ("`GET /live`", "`GET /live/<id>`", "snapshot", "no delay"):
            self.assertIn(needle, replays.replace("**There is no delay, on purpose:**", "no delay"), needle)

    def test_the_header_of_the_file_names_the_live_zones(self):
        text = read("docker", "nginx.conf")
        self.assertIn("(/live, below)", text.split("server {")[0])
        self.assertIn("what the downloads of replays have", text.split("server {")[0])


# ------------------------------------------------------------------------------------------------------------------------------------------------------------ the blocks, run
# The stand-in of a server that has the replay port (4020): the list is JSON, a snapshot is bytes, a match that it does not know is a 404 with the board's JSON; the first server only answers the rig's
# readiness probe
HEADERS = 'default_type %s; add_header Cache-Control "no-store" always; add_header X-Content-Type-Options "nosniff" always;'
STUB = stats.LOG_FORMAT + """server {
    listen 4002;
    location = /ready { return 200 'ready'; }
}
server {
    listen 4020;
    access_log /dev/stdout stub;
    location = /live { %s return 200 '{"live":[],"count":0,"sim_rules":1}'; }
    location = /live/%s { %s return 404 '{"error":"no such live match","ended":false,"replay":""}'; }
    location ~ "^/live/[A-Za-z0-9_]+-[0-9]{8}-[0-9]{6}Z(-[0-9]+)?$" { %s return 200 'a stand-in snapshot'; }
}
""" % (HEADERS % "application/json", GONE_ID, HEADERS % "application/json", HEADERS % "application/octet-stream")


@unittest.skipUnless(shutil.which("docker"), "docker is not installed: the blocks of docker/nginx.conf for /live were NOT run (tests/scripts/test_nginx_live.py)")
class TheBlocksRun(stats.Rig, unittest.TestCase):
    STUB = STUB

    @classmethod
    def setUpClass(cls):
        cls.start_rig()

    @classmethod
    def tearDownClass(cls):
        cls.stop_rig()

    def test_the_configuration_is_accepted_by_nginx(self):
        done = subprocess.run(["docker", "exec", self.front, "nginx", "-t"], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)

    def sent(self, path, at_least=0):
        return self.stub_requests(path, at_least)

    def test_the_list_and_a_snapshot_go_to_the_server_as_plain_gets(self):
        before = (len(self.sent("/live")), len(self.sent("/live/" + SNAPSHOT_ID)))
        status, headers, body = self.ask("GET", "/live")
        self.assertEqual(status, 200)
        self.assertEqual(body, b'{"live":[],"count":0,"sim_rules":1}')
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])                  # the server's own line, once: no browser keeps the list
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])          # once: the block hides the server's and keeps the page's
        self.assertEqual(self.names(headers, "cross-origin-opener-policy"), ["same-origin"])
        self.assertEqual(self.names(headers, "cross-origin-embedder-policy"), ["require-corp"])
        self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"))
        status, headers, body = self.ask("GET", "/live/" + SNAPSHOT_ID)
        self.assertEqual((status, body), (200, b"a stand-in snapshot"))
        self.assertEqual(self.names(headers, "content-type"), ["application/octet-stream"])
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])
        seen = self.sent("/live", before[0] + 1)[before[0]:] + self.sent("/live/" + SNAPSHOT_ID, before[1] + 1)[before[1]:]
        self.assertEqual(len(seen), 2)
        for one in seen:
            self.assertEqual((one["method"], one["args"], one["cl"], one["te"], one["up"], one["conn"], one["st"], one["host"]), ("GET", "-", "-", "-", "-", "close", "200", "127.0.0.1"))

    def test_the_404_of_a_match_that_the_server_does_not_know_passes_with_its_json_and_is_asked_every_time(self):
        before = self.stub_lines()
        for _ in range(3):
            status, headers, body = self.ask("GET", "/live/" + GONE_ID)
            self.assertEqual((status, body), (404, b'{"error":"no such live match","ended":false,"replay":""}'))
            self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"))
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"])
        self.assertEqual(self.stub_lines(before + 3) - before, 3)                             # (only a 200 is kept: an answer that says "over" must be the truth the moment the match is over)

    def test_every_other_method_a_query_and_an_id_of_the_wrong_shape_are_refused_before_the_server(self):
        before = self.stub_lines()
        for path in ("/live", "/live/" + SNAPSHOT_ID):
            for method in ("POST", "PUT", "DELETE", "PATCH", "OPTIONS"):
                self.assertEqual(self.ask(method, path, body=b"" if method in ("POST", "PUT", "PATCH") else None)[0], 405, (method, path))
            self.assertEqual(self.ask("HEAD", path)[0], 405, path)
            for query in ("?x=1", "?x", "?limit=5", "?a=b&c=d"):
                self.assertEqual(self.ask("GET", path + query)[0], 404, path + query)         # a query is no request for the list or a snapshot
        for name in ("", "x", "TREASURE", "A" * 25 + "-20261008-143209Z", "TREASURE-20261008-143209Z-12345", "TREASURE-20261008-143209z", "TREASURE-20261008-143209Z.antsrep",
                     "sub/" + SNAPSHOT_ID, SNAPSHOT_ID + "/", SNAPSHOT_ID + "/x", SNAPSHOT_ID + "%0A", SNAPSHOT_ID + "%20", ".", ".hidden"):
            self.assertEqual(self.ask("GET", "/live/" + name)[0], 404, name)                  # (an address under /live/ of a shape that the board does not make: refused here)
        for name in ("../" + SNAPSHOT_ID, "%2e%2e/" + SNAPSHOT_ID, "sub/../../" + SNAPSHOT_ID):
            status, _, body = self.ask("GET", "/live/" + name)                                # (a path that leaves /live/ is another address of the site: the game page)
            self.assertNotEqual(body, b"a stand-in snapshot", name)
        self.assertIn(self.ask("GET", "/live/" + SNAPSHOT_ID + "%00")[0], (400, 404))         # (a NUL in the address: nginx refuses it itself)
        self.assertEqual(self.stub_lines(), before)                                           # none of these reached the server (not a request, not a line of its log), and none used up the allowance
        for path in ("/live", "/live/" + SNAPSHOT_ID):
            self.assertEqual(self.ask("GET", path + "?")[0], 200, path)                       # (a "?" with nothing behind it carries no query: nginx's $args is empty)
        self.assertEqual(self.stub_lines(before + 2) - before, 2)                             # (and these two did reach the server: the first requests it was sent)

    def test_the_server_is_sent_a_host_and_a_connection_and_nothing_else_of_the_visitor(self):
        visitor = {"Cookie": "session=" + "c" * 8100, "User-Agent": "a-browser/1.0", "Authorization": "Bearer a-secret", "X-Forwarded-For": "203.0.113.9", "Referer": "https://example.org/page",
                   "Origin": "https://example.org", "Accept-Language": "de", "X-Custom": "yes", "Sec-Fetch-Site": "same-origin", "Accept": "*/*", "Pragma": "no-cache", "Cache-Control": "no-cache",
                   "If-None-Match": '"abc"', "Range": "bytes=0-5", "X-Real-IP": "203.0.113.9", "Forwarded": "for=203.0.113.9", "Accept-Encoding": "gzip, br"}
        before = (len(self.sent("/live")), len(self.sent("/live/" + SNAPSHOT_ID)))
        self.assertEqual(self.ask("GET", "/live", headers=visitor)[0], 200)
        self.assertEqual(self.ask("GET", "/live/" + SNAPSHOT_ID, headers=visitor)[0], 200)
        listing = self.sent("/live", before[0] + 1)[before[0]:]
        snapshot = self.sent("/live/" + SNAPSHOT_ID, before[1] + 1)[before[1]:]
        self.assertEqual((len(listing), len(snapshot)), (1, 1))
        # the whole request that the server was sent, to the byte: the request line, a Host (the visitor's, without a port) and a Connection
        self.assertEqual(listing[0]["len"], len(LIST_REQUEST))
        self.assertEqual(snapshot[0]["len"], len("GET /live/%s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n" % SNAPSHOT_ID))
        for one in (listing[0], snapshot[0]):
            for field in ("ck", "ua", "az", "xf", "rf", "og", "sf", "ae", "xc"):
                self.assertEqual(one[field], "-", field)                                      # (and the ones that could matter, by name)
            self.assertEqual((one["host"], one["conn"], one["up"], one["te"], one["cl"]), ("127.0.0.1", "close", "-", "-", "-"))

    def test_the_list_and_each_snapshot_are_cached_for_two_seconds_for_everybody(self):
        before = self.stub_lines()
        first = self.ask("GET", "/live")
        self.assertEqual(first[0], 200)
        crowd = [self.ask("GET", "/live", headers={"Cookie": "visitor=%d" % i, "User-Agent": "visitor-%d" % i}) for i in range(8)]
        for status, headers, body in crowd:
            self.assertEqual((status, body), (200, first[2]))                                 # (the same list for everybody)
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"])              # (and a browser is still told to keep nothing)
            self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(self.stub_lines(before + 1) - before, 1)                             # the server was asked once for the nine
        for match in (SNAPSHOT_ID, OTHER_ID):
            for _ in range(4):
                status, _, body = self.ask("GET", "/live/" + match)
                self.assertEqual((status, body), (200, b"a stand-in snapshot"))
        self.assertEqual(self.stub_lines(before + 3) - before, 3)                             # a snapshot of its own for each match: asked once each (the other three of each came from the cache)
        time.sleep(3.2)                                                                       # two seconds are over (nginx counts whole seconds: an entry made at the start of a second is good for the whole of the second one, so 2.5 s is not always enough)
        self.assertEqual(self.ask("GET", "/live")[0], 200)
        self.assertEqual(self.ask("GET", "/live")[0], 200)
        self.assertEqual(self.ask("GET", "/live/" + SNAPSHOT_ID)[0], 200)
        self.assertEqual(self.stub_lines(before + 5) - before, 5)                             # asked again once each, and the others after them came from that
        self.assertEqual(self.ask("GET", "/live/" + OTHER_ID)[0], 200)
        self.assertEqual(self.stub_lines(before + 6) - before, 6)                             # (the other match's entry is its own: it was not renewed with the first's)

    def test_the_cache_of_the_numbers_and_the_recorded_matches_is_not_the_lives(self):
        time.sleep(3.2)                                                                       # (an entry that an earlier test made is over)
        live, replays = len(self.stub_requests("/live")), len(self.stub_requests("/replays"))
        self.assertEqual(self.ask("GET", "/live")[0], 200)
        self.assertEqual(self.ask("GET", "/live")[0], 200)
        self.assertEqual(self.ask("GET", "/replays")[0], 404)                                 # (the stand-in has no /replays: its 404 is not the list's entry, and /replays/<file> is not a live id)
        self.assertEqual(len(self.stub_requests("/replays", replays + 1)) - replays, 1)       # the list of the recorded matches reached the server: it is not answered from the live list's entry
        self.assertEqual(len(self.stub_requests("/live", live + 1)) - live, 1)                # and the two asks of the live list were one request (the requests are counted by their access lines: the stand-in's error line for the file it has not is no request)

    def test_the_list_may_be_asked_twenty_times_a_second_by_the_whole_site_with_a_burst_of_a_hundred_and_the_snapshots_have_an_allowance_of_their_own(self):
        started = time.monotonic()
        codes = [self.ask("GET", "/live")[0] for _ in range(160)]
        took = time.monotonic() - started
        allowed = codes.count(200)
        self.assertTrue(set(codes) <= {200, 503}, set(codes))
        self.assertTrue(101 <= allowed <= 101 + int(20 * took) + 1, (allowed, took))          # the first request and the burst of 100, and twenty a second while this goes on
        self.assertEqual(codes[:101], [200] * 101)                                            # (the refusals come after the allowance, none between)
        self.assertEqual(self.ask("GET", "/live/" + SNAPSHOT_ID)[0], 200)                     # the snapshots are another zone: a flood of the list does not use their allowance

    def test_thirty_snapshots_a_second_for_the_whole_site_with_a_burst_of_sixty_and_the_rest_is_503(self):
        before = self.stub_lines()
        started = time.monotonic()
        codes = [self.ask("GET", "/live/" + SNAPSHOT_ID)[0] for _ in range(120)]
        took = time.monotonic() - started
        allowed = codes.count(200)
        self.assertTrue(set(codes) <= {200, 503}, set(codes))
        self.assertTrue(61 <= allowed <= 61 + int(30 * took) + 1, (allowed, took))            # the first request and the burst of 60, and thirty a second while this goes on
        self.assertEqual(codes[:61], [200] * 61)                                              # (the refusals come after the allowance, none between)
        self.assertLessEqual(self.stub_lines() - before, 1 + int(took / 2))                   # the cache: the server was asked once (and again every two seconds), not 61 times
        self.assertEqual(self.ask("GET", "/live")[0], 200)                                    # the list is another zone: a flood of snapshots does not use its allowance
        self.assertEqual(self.ask("GET", "/replays/ants-TREASURE-20261008-143209Z.antsrep")[0], 404)     # (nor the recorded matches' files'; the stand-in has none: its 404, not nginx's 503)

    def stub_up(self):
        return subprocess.run(["docker", "exec", self.stub, "wget", "-q", "-O", "/dev/null", "-T", "2", "http://127.0.0.1:4002/ready"], capture_output=True).returncode == 0

    def wait_stub(self, up):
        for _ in range(100):
            if self.stub_up() == up:
                return
            time.sleep(0.1)
        self.fail("the stand-in did not %s" % ("come back" if up else "stop"))

    def test_when_the_server_is_gone_no_old_answer_is_served_but_the_404_with_the_json_body(self):
        # (the numbers and the recorded matches' list are served old when the server does not answer; a snapshot that was repeated for minutes would look like a match that goes on)
        self.assertEqual(self.ask("GET", "/live")[0], 200)
        self.assertEqual(self.ask("GET", "/live/" + SNAPSHOT_ID)[0], 200)
        time.sleep(3.2)                                                                       # both entries are old now
        subprocess.run(["docker", "exec", self.stub, "nginx", "-s", "stop"], capture_output=True)
        try:
            self.wait_stub(False)
            for path in ("/live", "/live/" + SNAPSHOT_ID):
                started = time.monotonic()
                status, headers, body = self.ask("GET", path)
                self.assertEqual(status, 404, path)
                self.assertLess(time.monotonic() - started, 3.0, path)                       # (at once: the connection is refused)
                self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"), path)
                self.assertIn(b'"error"', body, path)
                self.assertNotIn(b"stand-in", body, path)
        finally:
            subprocess.run(["docker", "exec", "-d", self.stub, "sh", "-c", stats.START_NGINX], capture_output=True)
            self.wait_stub(True)
        self.assertEqual(self.ask("GET", "/live")[0], 200)                                    # the server is back: the next answer is its own

    def test_the_neighbours_of_the_two_addresses_are_the_game_page_and_nothing_is_passed_on(self):
        for path in ("/liv", "/live2", "/live.json", "/livex/", "/lives"):
            status, _, body = self.ask("GET", path)
            self.assertEqual(status, 200, path)                                               # (the try_files of the site: the game page, as any unknown address)
            self.assertIn(b"a stand-in page", body, path)
            self.assertEqual(self.sent(path), [], path)


@unittest.skipUnless(shutil.which("docker"), "docker is not installed: the answer of /live without a door was NOT run (tests/scripts/test_nginx_live.py)")
class TheDoorIsOff(stats.Rig, unittest.TestCase):
    """A server that was started without --replay-port, or is not there: the stand-in only has the WebSocket port, so the connection to the replay port is refused."""
    STUB = stats.STUB

    @classmethod
    def setUpClass(cls):
        cls.start_rig()

    @classmethod
    def tearDownClass(cls):
        cls.stop_rig()

    def test_the_list_and_a_snapshot_are_404_with_a_json_body_and_the_rest_of_the_site_goes_on(self):
        for path in ("/live", "/live/" + SNAPSHOT_ID):
            started = time.monotonic()
            status, headers, body = self.ask("GET", path)
            self.assertEqual(status, 404, path)
            self.assertLess(time.monotonic() - started, 3.0)                                  # (refused at once, not the timeout of a server that says nothing)
            self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"), path)
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"], path)
            self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"], path)
            self.assertIn(b'"error"', body, path)
            self.assertNotIn(b"nginx", body, path)                                            # (not nginx's own page)
        self.assertEqual(self.ask("POST", "/live", body=b"")[0], 405)                         # the checks are the same
        self.assertEqual(self.ask("GET", "/live?x=1")[0], 404)
        status, _, body = self.ask("GET", "/stats")                                           # the WebSocket port's doors are not touched
        self.assertEqual((status, body), (200, b'{"stub":true}'))


if __name__ == "__main__":
    unittest.main()
