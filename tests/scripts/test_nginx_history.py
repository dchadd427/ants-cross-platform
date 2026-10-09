#!/usr/bin/env python3
"""docker/nginx.conf routes the match history: GET /history[?filters] and GET /history/<id> (run by ./run_tests.sh --fast and by the CI where docker works).

ants_server keeps one record for every match it recorded, for good (docs/REPLAYS.md "The match history"), and answers the list and one match on the same read-only port as the recorded matches and
the live ones (--replay-port: no secret; the records hold the names that the players typed, and no address, room code or chat). The stack file starts it that way, so both are PUBLIC; the page's nginx
forwards the two addresses like /replays and /live (tests/scripts/test_nginx_replays.py, test_nginx_live.py), as the one request each takes and nothing else:

  - /history is an exact location and /history/ a prefix location, the only ones of the name (the named location of /replays answers when the server has no such door);
  - only a GET goes through (405 otherwise: the owner's delete is the control interface's and is never offered here); the item takes no query and an id of the wrong shape is 404 here and never reaches the
    server (the history decides which exist); the list takes the query of its filters, of the characters that the filters are made of and 400 characters at most (400 otherwise), so that no other byte
    reaches the server or its log, and the server refuses by name what it does not know;
  - nothing of the visitor's request is passed on but Host and Connection, and the connection is not kept;
  - allowances of their own, sized for the whole site (behind a reverse proxy every visitor has the proxy's address) and not shared with the recorded or the live matches': ten a second for the list,
    twenty for single matches (503 beyond);
  - only the list WITHOUT a query is cached, for five seconds, in one entry for everybody in the cache of /stats, and an old list is served while a new one is fetched and when the server does not answer;
    a list with filters and a single match are asked of the server every time (made-up filters and ids must not fill the small cache that the numbers of the front page share); a server that has no such
    door is answered 404 with a JSON body.

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
LIST = block_of(CONF, "location = /history ")
ITEM = block_of(CONF, "location ^~ /history/ ")
OFF = block_of(CONF, "location @replays_off ")
REPLAYS_LIST = block_of(CONF, "location = /replays ")
REPLAYS_FILES = block_of(CONF, "location ^~ /replays/ ")
LIVE_LIST = block_of(CONF, "location = /live ")
LIVE_ITEM = block_of(CONF, "location ^~ /live/ ")
STATS = block_of(CONF, "location = /stats ")
BOTH = (("the list", LIST), ("the match", ITEM))

# An id of the history (history_store.hpp): the name of the recording without ".antsrep", ants-<map: 1 - 24 of A-Z a-z 0-9 _>-<YYYYMMDD>-<HHMMSS>Z[-<2 .. 9999>]
GOOD_IDS = ["ants-TREASURE-20261009-082104Z", "ants-TREASURE-20261009-082104Z-2", "ants-TINY-20261008-000000Z-9999", "ants-a_B9-19700101-000000Z", "ants-" + "A" * 24 + "-20261008-143209Z"]
BAD_IDS = ["", "x", "ants-", "ants--20261008-143209Z", "TREASURE-20261008-143209Z", "ants-" + "A" * 25 + "-20261008-143209Z", "ants-TREASURE-2026108-143209Z", "ants-TREASURE-20261008-14320Z",
           "ants-TREASURE-20261008-143209", "ants-TREASURE-20261008-143209z", "ants-TREASURE-20261008-143209Z-12345", "ants-TREASURE-20261008-143209Z-", "ants-TREASURE-20261008-143209Z.antsrep",
           "ants-TREASURE-20261008-143209Z.json", "ants-TREASURE-20261008-143209Z\n", "ants-TREA SURE-20261008-143209Z", "ants-TREA.SURE-20261008-143209Z", "ants-TREA-SURE-20261008-143209Z",
           "../ants-TREASURE-20261008-143209Z", "sub/ants-TREASURE-20261008-143209Z", "Ants-TREASURE-20261008-143209Z", "ants-TREASURE-20261008-143209Z/", "ants-TREASURE-20261008-143209Z/x",
           "ants-TREASURE-20261008-143209Z-2-3"]
ITEM_ID = GOOD_IDS[0]
OTHER_ID = GOOD_IDS[1]
GONE_ID = "ants-GONE-20261008-143209Z"                                                        # (the stand-in knows no such match and says so)
# The queries of the page: every word of every filter of the server's list (control.cpp parse_history_query), percent escapes and '+' as a browser writes a search
GOOD_QUERIES = ["", "limit=20", "limit=100&offset=40&sort=score", "format=1v1&result=decided&who=people&rec=watch&colour=green", "sort=hatched&format=ffa&who=computers&rec=removed&colour=black",
                "q=Bot+%28Hard%29", "q=%41%62%63", "q=Ann", "limit=50&offset=0&sort=new&format=team&result=draw&who=all&rec=old&colour=red&q=" + "a" * 120, "x", "a=b=c", "q=a-b.c_d"]
BAD_QUERIES = ["q=<x>", "q=a;b", 'q="x"', "q=a b", "q=a'b", "q=a|b", "q=a\\b", "q=a/b", "q=a?b", "q=a#b", "q=a:b", "q=a,b", "q=é", "q=*", "q=(x)", "q=a@b", "q=a!b", "q=a$b", "q=" + "a" * 500,
               "q=" + "%41" * 134]
LIST_REQUEST = "GET /history HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"


def item_pattern():
    """The regular expression of the item location, as Python reads it (nginx's \\z, the end of the text and not before a last line feed, is \\Z here)."""
    match = re.search(r'if \(\$uri !~ "([^"]+)"\) \{ return 404; \}', ITEM)
    return re.compile(match.group(1).replace("\\z", "\\Z")) if match else None


def query_pattern():
    """The regular expression that the list's query must match, as Python reads it"""
    match = re.search(r'if \(\$args !~ "([^"]+)"\) \{ return 400; \}', LIST)
    return re.compile(match.group(1).replace("\\z", "\\Z")) if match else None


class TheLocations(unittest.TestCase):
    def test_the_two_addresses_have_their_two_locations_and_no_other_location_could_catch_them(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual([l for l in locations if "history" in l], ["= /history", "^~ /history/"])     # (no regular-expression location could also catch /history/x)
        self.assertEqual(len(re.findall(r"location\s+= /history\s*\{", CONF)), 1)
        self.assertEqual(len(re.findall(r"location\s+\^~ /history/\s*\{", CONF)), 1)
        self.assertEqual([l for l in locations if "replays" in l], ["= /replays", "^~ /replays/", "@replays_off"])     # (the recorded matches are as they were)
        self.assertEqual([l for l in locations if "live" in l], ["= /live", "^~ /live/"])                 # (and the live ones)

    def test_only_a_get_goes_through_and_the_checks_come_before_the_allowance_the_cache_and_the_server(self):
        for what, block in BOTH:
            self.assertIn("if ($request_method != GET) { return 405; }", block, what)
            for check in ("$request_method", "$is_args" if block is ITEM else "$args !~"):
                self.assertLess(block.index(check), block.index("limit_req "), what)         # (a refused request costs nothing of the allowance)
                self.assertLess(block.index(check), block.index("proxy_pass"), what)         # (and nothing is passed on before the checks)
        self.assertIn("if ($is_args) { return 404; }", ITEM)                                  # (a match is one address: no query)
        self.assertNotIn("$is_args) { return", LIST)                                          # (the list is the one that takes a query)
        self.assertLess(LIST.index("$args !~"), LIST.index("proxy_cache "))                  # (a query that is refused is no key of the cache)

    def test_only_return_is_used_inside_an_if(self):
        for what, block in BOTH:
            bodies = re.findall(r"\bif\s*\([^)]*\)\s*\{([^}]*)\}", block)
            self.assertGreaterEqual(len(bodies), 2, what)
            for body in bodies:
                self.assertRegex(body.strip(), r"^return \d{3};$", what)                       # ("if" is only safe with a return: nothing else may sit in one)

    def test_the_list_takes_a_query_of_the_characters_of_the_filters_and_of_nothing_else(self):
        pattern = query_pattern()
        self.assertIsNotNone(pattern, "the list location has no check of the query")
        for query in GOOD_QUERIES:
            self.assertTrue(pattern.match(query), query)
        for query in BAD_QUERIES:
            self.assertFalse(pattern.match(query), repr(query))
        self.assertFalse(pattern.match("q=a\n"))                                              # (\z: the end of the query, not before a last line feed)
        text = re.search(r'if \(\$args !~ "([^"]+)"\)', LIST).group(1)
        self.assertTrue(text.endswith("\\z"), text)
        self.assertIn("{0,400}", text)

    def test_the_query_pattern_lets_every_word_of_every_filter_of_the_server_through(self):
        source = read("src", "ants_server", "control.cpp")
        parser = source[source.index("bool parse_history_query"):source.index("history_list_response")]
        words = set(re.findall(r'"([a-z0-9]+)"', parser))
        self.assertTrue({"limit", "offset", "sort", "format", "result", "who", "rec", "colour", "q", "1v1", "ffa", "team", "decided", "draw", "cut", "computers", "people", "removed", "watch", "old"} <= words)
        pattern = query_pattern()
        for word in words:
            self.assertTrue(pattern.match("k=" + word), word)

    def test_an_id_that_the_history_could_not_have_made_is_refused_here(self):
        self.assertNotIn("$uri !~", LIST)                                                     # (the list is one exact address)
        pattern = item_pattern()
        self.assertIsNotNone(pattern, "the item location has no check of the id")
        self.assertLess(ITEM.index("$uri !~"), ITEM.index("limit_req "))
        self.assertLess(ITEM.index("$uri !~"), ITEM.index("proxy_pass"))
        for name in GOOD_IDS:
            self.assertTrue(pattern.match("/history/" + name), name)
        for name in BAD_IDS:
            self.assertFalse(pattern.match("/history/" + name), repr(name))
        self.assertFalse(pattern.match("/history/"))
        self.assertFalse(pattern.match("/history"))
        self.assertFalse(pattern.match("/histories/" + ITEM_ID))
        self.assertFalse(pattern.match("/replays/" + ITEM_ID))

    def test_the_pattern_says_what_the_store_says(self):
        stem = int(re.search(r"kMaxStemChars = (\d+);", read("src", "ants_server", "replay_store.cpp")).group(1))
        same_second = int(re.search(r"kMaxSameSecond = (\d+);", read("src", "ants_server", "replay_store.cpp")).group(1))
        text = re.search(r'if \(\$uri !~ "([^"]+)"\)', ITEM).group(1)
        self.assertIn("[A-Za-z0-9_]{1,%d}" % stem, text)                                      # the stem of the map's name
        self.assertIn("(-[0-9]{1,%d})?" % len(str(same_second)), text)                       # a later match of the same second
        self.assertIn("[0-9]{8}-[0-9]{6}Z", text)
        self.assertTrue(text.endswith("\\z"), text)
        self.assertIn("/history/ants-", text)                                                 # (the recording's name without its extension: the replay store's files)
        header = read("include", "ants_server", "history_store.hpp")
        self.assertIn("ants-TREASURE-20261008-143209Z", header)                               # (the header of the store says the same shape)

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

    def test_the_blocks_are_the_replays_blocks_but_for_the_checks_the_allowance_and_the_cache(self):
        def lines(block, *own):
            return [l.strip() for l in block.splitlines() if l.strip() and not l.strip().startswith(own)]
        own = ("if (", "limit_req zone=", "proxy_cache", "proxy_no_cache", "proxy_ignore_headers")
        self.assertEqual(lines(LIST, *own), lines(REPLAYS_LIST, *own))                        # (one door, one set of timeouts, one set of headers, one fallback: they cannot drift apart)
        self.assertEqual(lines(ITEM, *own), lines(REPLAYS_FILES, *own))
        self.assertEqual(lines(LIST, *own), lines(LIVE_LIST, *own))
        self.assertEqual(lines(ITEM, *own), lines(LIVE_ITEM, *own))

    def test_they_go_to_the_replay_port_of_the_same_server_as_the_recorded_matches(self):
        for what, block in BOTH:
            target = re.search(r"set \$ants_replays ([a-z0-9.-]+):(\d+);", block)
            theirs = re.search(r"set \$ants_replays ([a-z0-9.-]+):(\d+);", REPLAYS_LIST)
            self.assertIsNotNone(target, what)
            self.assertEqual(target.groups(), theirs.groups(), what)                          # the replay port (4020 in the container, not published on the host): there is no door of its own
            self.assertIn("proxy_pass http://$ants_replays;", block, what)                    # (a variable without a uri: the name is resolved per request and the request goes on as it came, query included)
            self.assertIn("resolver 127.0.0.11", block, what)


class TheAllowancesAndTheCache(unittest.TestCase):
    def test_each_address_has_an_allowance_of_its_own_sized_for_the_whole_site_and_the_zones_are_outside_the_server(self):
        # (behind a reverse proxy every visitor has the proxy's address: docs/audit/site_stats_notes.md; an allowance per visitor would be one for everybody)
        for zone_name, block, rate, burst in (("ants_history", LIST, 10, 50), ("ants_history_items", ITEM, 20, 40)):
            zone = re.search(r"limit_req_zone \$binary_remote_addr zone=%s:(\d+)m rate=(\d+)r/([sm]);" % zone_name, CONF)
            self.assertIsNotNone(zone, zone_name)
            self.assertLess(CONF.index(zone.group(0)), CONF.index("server {"))              # http context: before the server block
            self.assertEqual((int(zone.group(1)), int(zone.group(2)), zone.group(3)), (1, rate, "s"), zone_name)
            self.assertIn("limit_req zone=%s burst=%d nodelay;" % (zone_name, burst), block, zone_name)
            self.assertIn("limit_req_status 503;", block, zone_name)
        for block in (LIST, ITEM):
            self.assertNotRegex(block, r"zone=ants_(replays|replay_files|live|live_files|stats|busy|local)\b")      # (the others' allowances are theirs: a page that filters does not use up the downloads')
        self.assertNotIn("zone=ants_history ", ITEM)
        self.assertNotIn("zone=ants_history_items ", LIST)
        for block in (REPLAYS_LIST, REPLAYS_FILES, LIVE_LIST, LIVE_ITEM, STATS):
            self.assertNotRegex(block, r"zone=ants_history")

    def test_only_the_list_without_a_query_is_cached_for_five_seconds_in_the_cache_of_the_numbers(self):
        for directive in ("proxy_cache ants_stats_cache;", "proxy_cache_valid 200 5s;", "proxy_ignore_headers Cache-Control;", "proxy_cache_lock on;", "proxy_no_cache $is_args;",
                          "proxy_cache_bypass $is_args;", "proxy_cache_use_stale updating error timeout;"):
            self.assertIn(directive, LIST, directive)
        self.assertEqual(len(re.findall(r"\bproxy_cache_valid\b", LIST)), 1)                  # (only a 200: the 404 of a server with no history is asked each time)
        self.assertLess(LIST.index("limit_req "), LIST.index("proxy_cache "))                # (a request over the limit never reaches the cache)
        self.assertLess(LIST.index("proxy_cache "), LIST.index("proxy_pass"))
        key = re.search(r'proxy_cache_key "([^"]+)";', LIST).group(1)
        self.assertNotRegex(key, r"\$")                                                       # (one entry for everybody: the key holds nothing of the visitor, nor of the query)
        for directive in ("proxy_cache", "proxy_cache_key", "proxy_cache_valid", "proxy_no_cache", "proxy_cache_bypass", "proxy_ignore_headers"):
            self.assertNotRegex(ITEM, r"\b%s\b" % directive, directive)                      # (a match is never kept here: one for every id would push the numbers of the front page out)

    def test_its_entry_is_its_own_and_the_cache_is_the_small_one_that_the_numbers_share(self):
        key = re.search(r'proxy_cache_key "([^"]+)";', LIST).group(1)
        others = [re.search(r'proxy_cache_key "([^"]+)";', b).group(1) for b in (STATS, REPLAYS_LIST, LIVE_LIST, LIVE_ITEM)]
        for other in others:
            self.assertFalse(other.startswith(key) or key.startswith(other), (other, key))   # (no other door's entry is this one's)
        path = re.search(r"proxy_cache_path (\S+) keys_zone=ants_stats_cache:(\d+)m max_size=(\d+)m", HEAD)
        self.assertIsNotNone(path)
        self.assertEqual(int(path.group(3)), 8)                                               # (one list is some tens of kilobytes: the cache stays as small as the numbers' test wants it)

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
        for needle in ("GET /history", "GET /history/<id>", "ants_history", "DELETE /history/<id>", "--history-dir", "--no-history"):
            self.assertIn(needle, server + read("docker", "nginx.conf"), needle)
        for needle in ("The match history", "`GET /history`", "`GET /history/<id>`", "kept for good", "marker"):
            self.assertIn(needle, replays, needle)

    def test_the_header_of_the_file_names_the_history_zones(self):
        text = read("docker", "nginx.conf")
        self.assertIn("(/history, below)", text.split("server {")[0])
        self.assertIn("what the replays and the live matches have", text.split("server {")[0])


# ------------------------------------------------------------------------------------------------------------------------------------------------------------ the blocks, run
# The stand-in of a server that has the replay port (4020): the list is JSON that says the query it was sent, a match is bytes, a match that it does not know is a 404 with the server's JSON; the first
# server only answers the rig's readiness probe
HEADERS = 'default_type %s; add_header Cache-Control "no-store" always; add_header X-Content-Type-Options "nosniff" always;'
STUB = stats.LOG_FORMAT + """server {
    listen 4002;
    location = /ready { return 200 'ready'; }
}
server {
    listen 4020;
    access_log /dev/stdout stub;
    location = /history { %s return 200 '{"history":[],"count":0,"query":"$args"}'; }
    location = /history/%s { %s return 404 '{"error":"no such match"}'; }
    location ~ "^/history/ants-[A-Za-z0-9_]+-[0-9]{8}-[0-9]{6}Z(-[0-9]+)?$" { %s return 200 'a stand-in record'; }
}
""" % (HEADERS % "application/json", GONE_ID, HEADERS % "application/json", HEADERS % "application/json")


@unittest.skipUnless(shutil.which("docker"), "docker is not installed: the blocks of docker/nginx.conf for /history were NOT run (tests/scripts/test_nginx_history.py)")
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

    def test_the_list_and_a_match_go_to_the_server_as_plain_gets_and_the_query_goes_on_as_it_came(self):
        before = (len(self.sent("/history")), len(self.sent("/history/" + ITEM_ID)))
        status, headers, body = self.ask("GET", "/history")
        self.assertEqual(status, 200)
        self.assertEqual(body, b'{"history":[],"count":0,"query":""}')
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])                  # the server's own line, once: no browser keeps the list
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])          # once: the block hides the server's and keeps the page's
        self.assertEqual(self.names(headers, "cross-origin-opener-policy"), ["same-origin"])
        self.assertEqual(self.names(headers, "cross-origin-embedder-policy"), ["require-corp"])
        self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"))
        status, headers, body = self.ask("GET", "/history/" + ITEM_ID)
        self.assertEqual((status, body), (200, b"a stand-in record"))
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])
        seen = self.sent("/history", before[0] + 1)[before[0]:] + self.sent("/history/" + ITEM_ID, before[1] + 1)[before[1]:]
        self.assertEqual(len(seen), 2)
        for one in seen:
            self.assertEqual((one["method"], one["args"], one["cl"], one["te"], one["up"], one["conn"], one["st"], one["host"]), ("GET", "-", "-", "-", "-", "close", "200", "127.0.0.1"))
        for query in ("limit=5&sort=old", "q=Bot+%28Hard%29&format=1v1", "limit=100&offset=40&sort=hatched&format=ffa&result=cut&who=people&rec=watch&colour=black"):
            before_list = len(self.sent("/history"))
            status, _, body = self.ask("GET", "/history?" + query)
            self.assertEqual(status, 200, query)
            self.assertEqual(body, ('{"history":[],"count":0,"query":"%s"}' % query).encode(), query)     # (the server is sent the query to the byte, and what it answers is not the entry of the list without one)
            self.assertEqual(self.sent("/history", before_list + 1)[before_list]["args"], query)

    def test_a_list_with_a_query_and_a_match_are_asked_of_the_server_every_time_and_the_list_without_one_is_kept_for_everybody(self):
        time.sleep(6.5)                                                                       # (an entry that an earlier test made is over: five seconds, counted in whole seconds)
        before = self.stub_lines()
        first = self.ask("GET", "/history")
        self.assertEqual(first[0], 200)
        crowd = [self.ask("GET", "/history", headers={"Cookie": "visitor=%d" % i, "User-Agent": "visitor-%d" % i}) for i in range(8)]
        for status, headers, body in crowd:
            self.assertEqual((status, body), (200, first[2]))                                 # (the same list for everybody)
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"])              # (and a browser is still told to keep nothing)
            self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(self.stub_lines(before + 1) - before, 1)                             # the server was asked once for the nine
        for _ in range(4):
            status, _, body = self.ask("GET", "/history?limit=1")
            self.assertEqual((status, body), (200, b'{"history":[],"count":0,"query":"limit=1"}'))      # never the list without a query, and never a stored one
        self.assertEqual(self.stub_lines(before + 5) - before, 5)                             # four more asks: a list with a query is not kept
        for _ in range(4):
            self.assertEqual(self.ask("GET", "/history/" + ITEM_ID)[1:], self.ask("GET", "/history/" + ITEM_ID)[1:])
        self.assertEqual(self.stub_lines(before + 13) - before, 13)                           # eight asks of one match: none came from a cache
        self.assertEqual(self.ask("GET", "/history")[2], first[2])                            # (the list without a query is still the one entry: the queries did not take its place)
        self.assertEqual(self.stub_lines() - before, 13)

    def test_the_404_of_a_match_that_the_server_does_not_know_passes_with_its_json_and_is_asked_every_time(self):
        before = self.stub_lines()
        for _ in range(3):
            status, headers, body = self.ask("GET", "/history/" + GONE_ID)
            self.assertEqual((status, body), (404, b'{"error":"no such match"}'))
            self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"))
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"])
        self.assertEqual(self.stub_lines(before + 3) - before, 3)

    def test_every_other_method_a_query_on_a_match_a_query_of_other_characters_and_an_id_of_the_wrong_shape_are_refused_before_the_server(self):
        before = self.stub_lines()
        for path in ("/history", "/history/" + ITEM_ID):
            for method in ("POST", "PUT", "DELETE", "PATCH", "OPTIONS"):
                self.assertEqual(self.ask(method, path, body=b"" if method in ("POST", "PUT", "PATCH") else None)[0], 405, (method, path))      # (the owner's delete is not offered to the visitors)
            self.assertEqual(self.ask("HEAD", path)[0], 405, path)
        for query in ("?x=1", "?x", "?limit=5", "?a=b&c=d"):
            self.assertEqual(self.ask("GET", "/history/" + ITEM_ID + query)[0], 404, query)    # a query is no request for a match
        for query in BAD_QUERIES:
            if not query.isascii():
                continue
            target = query.replace("#", "%23").replace(" ", "%20").replace("\\", "%5C").replace("?", "%3F")
            self.assertEqual(self.ask("GET", "/history?" + target)[0], 400, query)            # (the characters that no filter is made of)
        for name in ("", "x", "ants-TREASURE", "ants-" + "A" * 25 + "-20261008-143209Z", "ants-TREASURE-20261008-143209Z-12345", "ants-TREASURE-20261008-143209z", "ants-TREASURE-20261008-143209Z.antsrep",
                     "sub/" + ITEM_ID, ITEM_ID + "/", ITEM_ID + "/x", ITEM_ID + "%0A", ITEM_ID + "%20", ".", ".hidden", "TREASURE-20261008-143209Z"):
            self.assertEqual(self.ask("GET", "/history/" + name)[0], 404, name)               # (an address under /history/ of a shape that the history does not make: refused here)
        for name in ("../" + ITEM_ID, "%2e%2e/" + ITEM_ID, "sub/../../" + ITEM_ID):
            status, _, body = self.ask("GET", "/history/" + name)                             # (a path that leaves /history/ is another address of the site: the game page)
            self.assertNotEqual(body, b"a stand-in record", name)
        self.assertIn(self.ask("GET", "/history/" + ITEM_ID + "%00")[0], (400, 404))          # (a NUL in the address: nginx refuses it itself)
        self.assertEqual(self.stub_lines(), before)                                           # none of these reached the server (not a request, not a line of its log), and none used up the allowance
        self.assertEqual(self.ask("GET", "/history?")[0], 200)                                # (a "?" with nothing behind it carries no query: nginx's $args is empty)
        self.assertEqual(self.ask("GET", "/history/" + ITEM_ID + "?")[0], 200)
        self.assertEqual(self.stub_lines(before + 1) - before >= 1, True)

    def test_the_server_is_sent_a_host_and_a_connection_and_nothing_else_of_the_visitor(self):
        visitor = {"Cookie": "session=" + "c" * 8100, "User-Agent": "a-browser/1.0", "Authorization": "Bearer a-secret", "X-Forwarded-For": "203.0.113.9", "Referer": "https://example.org/page",
                   "Origin": "https://example.org", "Accept-Language": "de", "X-Custom": "yes", "Sec-Fetch-Site": "same-origin", "Accept": "*/*", "Pragma": "no-cache", "Cache-Control": "no-cache",
                   "If-None-Match": '"abc"', "Range": "bytes=0-5", "X-Real-IP": "203.0.113.9", "Forwarded": "for=203.0.113.9", "Accept-Encoding": "gzip, br"}
        before = (len(self.sent("/history")), len(self.sent("/history/" + OTHER_ID)))
        self.assertEqual(self.ask("GET", "/history?limit=3", headers=visitor)[0], 200)
        self.assertEqual(self.ask("GET", "/history/" + OTHER_ID, headers=visitor)[0], 200)
        listing = self.sent("/history", before[0] + 1)[before[0]:]
        record = self.sent("/history/" + OTHER_ID, before[1] + 1)[before[1]:]
        self.assertEqual((len(listing), len(record)), (1, 1))
        # the whole request that the server was sent, to the byte: the request line (the query with it), a Host (the visitor's, without a port) and a Connection
        self.assertEqual(listing[0]["len"], len("GET /history?limit=3 HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"))
        self.assertEqual(record[0]["len"], len("GET /history/%s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n" % OTHER_ID))
        for one in (listing[0], record[0]):
            for field in ("ck", "ua", "az", "xf", "rf", "og", "sf", "ae", "xc"):
                self.assertEqual(one[field], "-", field)                                      # (and the ones that could matter, by name)
            self.assertEqual((one["host"], one["conn"], one["up"], one["te"], one["cl"]), ("127.0.0.1", "close", "-", "-", "-"))

    def test_the_cache_of_the_numbers_the_recorded_matches_and_the_live_ones_is_not_the_historys(self):
        time.sleep(6.5)                                                                       # (an entry that an earlier test made is over)
        history, replays, live = len(self.stub_requests("/history")), len(self.stub_requests("/replays")), len(self.stub_requests("/live"))
        self.assertEqual(self.ask("GET", "/history")[0], 200)
        self.assertEqual(self.ask("GET", "/history")[0], 200)
        self.assertEqual(self.ask("GET", "/replays")[0], 404)                                 # (the stand-in has no /replays or /live: their 404s are not the history's entry)
        self.assertEqual(self.ask("GET", "/live")[0], 404)
        self.assertEqual(len(self.stub_requests("/replays", replays + 1)) - replays, 1)
        self.assertEqual(len(self.stub_requests("/live", live + 1)) - live, 1)
        self.assertEqual(len(self.stub_requests("/history", history + 1)) - history, 1)       # and the two asks of the history were one request

    def test_the_list_may_be_asked_ten_times_a_second_by_the_whole_site_with_a_burst_of_fifty_and_the_matches_have_an_allowance_of_their_own(self):
        started = time.monotonic()
        codes = [self.ask("GET", "/history?limit=1")[0] for _ in range(150)]
        took = time.monotonic() - started
        allowed = codes.count(200)
        self.assertTrue(set(codes) <= {200, 503}, set(codes))
        self.assertTrue(51 <= allowed <= 51 + int(10 * took) + 1, (allowed, took))            # the first request and the burst of 50, and ten a second while this goes on
        self.assertEqual(codes[:51], [200] * 51)                                              # (the refusals come after the allowance, none between)
        self.assertEqual(self.ask("GET", "/history/" + ITEM_ID)[0], 200)                      # the matches are another zone: a flood of the list does not use their allowance
        self.assertEqual(self.ask("GET", "/live")[0], 404)                                    # (nor the live matches': the stand-in has none, its 404 and not nginx's 503)

    def test_twenty_matches_a_second_for_the_whole_site_with_a_burst_of_forty_and_the_rest_is_503(self):
        time.sleep(6.5)                                                                       # (the list's flood of the test before is paid for)
        started = time.monotonic()
        codes = [self.ask("GET", "/history/" + ITEM_ID)[0] for _ in range(120)]
        took = time.monotonic() - started
        allowed = codes.count(200)
        self.assertTrue(set(codes) <= {200, 503}, set(codes))
        self.assertTrue(41 <= allowed <= 41 + int(20 * took) + 1, (allowed, took))            # the first request and the burst of 40, and twenty a second while this goes on
        self.assertEqual(codes[:41], [200] * 41)                                              # (the refusals come after the allowance, none between)
        self.assertEqual(self.ask("GET", "/history")[0], 200)                                 # the list is another zone: a flood of matches does not use its allowance
        self.assertEqual(self.ask("GET", "/replays/ants-TREASURE-20261008-143209Z.antsrep")[0], 404)       # (nor the recorded matches' files'; the stand-in has none: its 404, not nginx's 503)

    def stub_up(self):
        return subprocess.run(["docker", "exec", self.stub, "wget", "-q", "-O", "/dev/null", "-T", "2", "http://127.0.0.1:4002/ready"], capture_output=True).returncode == 0

    def wait_stub(self, up):
        for _ in range(100):
            if self.stub_up() == up:
                return
            time.sleep(0.1)
        self.fail("the stand-in did not %s" % ("come back" if up else "stop"))

    def test_when_the_server_is_gone_the_old_list_is_served_and_nothing_else_the_rest_is_the_404_with_the_json_body(self):
        time.sleep(6.5)
        self.assertEqual(self.ask("GET", "/history")[0], 200)                                 # (the entry is made)
        time.sleep(6.5)                                                                       # ... and is old now
        subprocess.run(["docker", "exec", self.stub, "nginx", "-s", "stop"], capture_output=True)
        try:
            self.wait_stub(False)
            status, _, body = self.ask("GET", "/history")
            self.assertEqual((status, body), (200, b'{"history":[],"count":0,"query":""}'))    # the list that grows slowly: an old one is a list
            for path in ("/history?limit=1", "/history/" + ITEM_ID):                           # a list with filters and a match are not kept: the truth is the 404
                started = time.monotonic()
                status, headers, body = self.ask("GET", path)
                self.assertEqual(status, 404, path)
                self.assertLess(time.monotonic() - started, 3.0, path)                        # (at once: the connection is refused)
                self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"), path)
                self.assertIn(b'"error"', body, path)
                self.assertNotIn(b"stand-in", body, path)
        finally:
            subprocess.run(["docker", "exec", "-d", self.stub, "sh", "-c", stats.START_NGINX], capture_output=True)
            self.wait_stub(True)
        self.assertEqual(self.ask("GET", "/history?limit=1")[0], 200)                         # the server is back: the next answer is its own

    def test_the_neighbours_of_the_two_addresses_are_the_game_page_and_nothing_is_passed_on(self):
        for path in ("/histor", "/history2", "/history.json", "/historyx/", "/histories"):
            status, _, body = self.ask("GET", path)
            self.assertEqual(status, 200, path)                                               # (the try_files of the site: the game page, as any unknown address)
            self.assertIn(b"a stand-in page", body, path)
            self.assertEqual(self.sent(path), [], path)


@unittest.skipUnless(shutil.which("docker"), "docker is not installed: the answer of /history without a door was NOT run (tests/scripts/test_nginx_history.py)")
class TheDoorIsOff(stats.Rig, unittest.TestCase):
    """A server that was started without --replay-port, or is not there: the stand-in only has the WebSocket port, so the connection to the replay port is refused."""
    STUB = stats.STUB

    @classmethod
    def setUpClass(cls):
        cls.start_rig()

    @classmethod
    def tearDownClass(cls):
        cls.stop_rig()

    def test_the_list_and_a_match_are_404_with_a_json_body_and_the_rest_of_the_site_goes_on(self):
        for path in ("/history", "/history?limit=5", "/history/" + ITEM_ID):
            started = time.monotonic()
            status, headers, body = self.ask("GET", path)
            self.assertEqual(status, 404, path)
            self.assertLess(time.monotonic() - started, 3.0)                                  # (refused at once, not the timeout of a server that says nothing)
            self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"), path)
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"], path)
            self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"], path)
            self.assertIn(b'"error"', body, path)
            self.assertNotIn(b"nginx", body, path)                                            # (not nginx's own page)
        self.assertEqual(self.ask("POST", "/history", body=b"")[0], 405)                      # the checks are the same
        self.assertEqual(self.ask("GET", "/history?q=<x>")[0], 400)
        self.assertEqual(self.ask("GET", "/history/" + ITEM_ID + "?x=1")[0], 404)
        status, _, body = self.ask("GET", "/stats")                                           # the WebSocket port's doors are not touched
        self.assertEqual((status, body), (200, b'{"stub":true}'))


if __name__ == "__main__":
    unittest.main()
