#!/usr/bin/env python3
"""docker/nginx.conf routes the recorded matches of the game server: GET /replays and GET /replays/<file> (run by ./run_tests.sh --fast and by the CI where docker works).

ants_server keeps the matches that its rooms play (docs/REPLAYS.md "On the game server") and, when it is started with --replay-port, answers the list and the files on a port of its own: read
only, no secret, nothing a person typed in them. The stack file starts it that way, so the list and the files of the site are PUBLIC; the page's nginx forwards the two addresses like /busy,
as the one request each takes and nothing else:

  - /replays is an exact location and /replays/ a prefix location, the only ones of the name (and a named location that answers when the server has no such door);
  - only a GET without a query goes through (405 and 404 otherwise); a file name that the server's store could not have made is 404 here and never reaches the server;
  - nothing of the visitor's request is passed on but Host and Connection: no cookie, no body, no upgrade; the connection is not kept; the answer is never cached;
  - two requests a second for each client address with a burst of ten, the same allowance for both addresses (503 beyond), short timeouts;
  - it goes to the replay port that the stack gives the server (4020 inside the container, not published on the host), and a server that has no such door, or is not there, is answered
    404 with a JSON body (never nginx's 502 page).

The text of the file is checked everywhere. Where docker works the blocks are RUN: the real file in front of a stand-in server (a second nginx that logs what it was sent), so that what reaches
the server, and what does not, is seen. (The real ants_server with a real match is tests/scripts/test_ants_server.sh, part replays: its public door, asked directly.)
"""
import os
import re
import shutil
import subprocess
import sys
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_nginx_stats as stats  # noqa: E402  (the stand-in rig of the stats tests: a page's nginx in front of a second nginx that logs what it is sent)

read = stats.read
strip_comments = stats.strip_comments
block_of = stats.block_of

CONF = strip_comments(read("docker", "nginx.conf"))
HEAD = CONF[:CONF.index("server {")]                                                          # what is declared outside the server block: the zones
LIST = block_of(CONF, "location = /replays ")
FILES = block_of(CONF, "location ^~ /replays/ ")
OFF = block_of(CONF, "location @replays_off ")
BUSY = block_of(CONF, "location = /busy ")
BOTH = (("the list", LIST), ("the files", FILES))

# A file name of the server's store (replay_store.hpp): ants-<map: 1 - 24 of A-Z a-z 0-9 _>-<YYYYMMDD>-<HHMMSS>Z[-<2 .. 9999>].antsrep
GOOD_NAMES = ["ants-TREASURE-20261008-143209Z.antsrep", "ants-TREASURE-20261008-143209Z-2.antsrep", "ants-TINY-20261008-000000Z-9999.antsrep", "ants-a_B9-19700101-000000Z.antsrep",
              "ants-" + "A" * 24 + "-20261008-143209Z.antsrep"]
BAD_NAMES = ["", "x.antsrep", "ants-.antsrep", "ants--20261008-143209Z.antsrep", "ants-" + "A" * 25 + "-20261008-143209Z.antsrep", "ants-TREASURE-2026108-143209Z.antsrep",
             "ants-TREASURE-20261008-14320Z.antsrep", "ants-TREASURE-20261008-143209.antsrep", "ants-TREASURE-20261008-143209z.antsrep", "ants-TREASURE-20261008-143209Z-12345.antsrep",
             "ants-TREASURE-20261008-143209Z-.antsrep", "ants-TREASURE-20261008-143209Z.ANTSREP", "ants-TREASURE-20261008-143209Z.antsrep.tmp", "ants-TREASURE-20261008-143209Z.antsrep\n",
             "ants-TREA SURE-20261008-143209Z.antsrep", "ants-TREA.SURE-20261008-143209Z.antsrep", "ants-TREA-SURE-20261008-143209Z.antsrep", "../ants-TREASURE-20261008-143209Z.antsrep",
             "sub/ants-TREASURE-20261008-143209Z.antsrep", "Ants-TREASURE-20261008-143209Z.antsrep", "ants-TREASURE-20261008-143209Z.antsrep/"]


def name_pattern():
    """The regular expression of the files location, as Python reads it (nginx's \\z, the end of the text and not before a last line feed, is \\Z here)."""
    match = re.search(r'if \(\$uri !~ "([^"]+)"\) \{ return 404; \}', FILES)
    return re.compile(match.group(1).replace("\\z", "\\Z")) if match else None


class TheLocations(unittest.TestCase):
    def test_the_two_addresses_have_their_two_locations_and_no_other_location_could_catch_them(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual([l for l in locations if "replays" in l], ["= /replays", "^~ /replays/", "@replays_off"])      # (no regular-expression location could also catch /replays/x)
        self.assertEqual(len(re.findall(r"location\s+= /replays\s*\{", CONF)), 1)
        self.assertEqual(len(re.findall(r"location\s+\^~ /replays/\s*\{", CONF)), 1)

    def test_only_a_plain_get_without_a_query_goes_through(self):
        for what, block in BOTH:
            self.assertIn("if ($request_method != GET) { return 405; }", block, what)
            self.assertIn("if ($is_args) { return 404; }", block, what)
            for check in ("$request_method", "$is_args"):
                self.assertLess(block.index(check), block.index("limit_req "), what)         # (a refused request costs nothing of the allowance)
                self.assertLess(block.index(check), block.index("proxy_pass"), what)         # (and nothing is passed on before the checks)

    def test_a_file_name_that_the_store_could_not_have_made_is_refused_here(self):
        self.assertNotIn("$uri", LIST)                                                        # (the list is one exact address)
        pattern = name_pattern()
        self.assertIsNotNone(pattern, "the files location has no check of the name")
        self.assertLess(FILES.index("$uri !~"), FILES.index("limit_req "))
        self.assertLess(FILES.index("$uri !~"), FILES.index("proxy_pass"))
        for name in GOOD_NAMES:
            self.assertTrue(pattern.match("/replays/" + name), name)
        for name in BAD_NAMES:
            self.assertFalse(pattern.match("/replays/" + name), repr(name))
        self.assertFalse(pattern.match("/replays/"))
        self.assertFalse(pattern.match("/replay/" + GOOD_NAMES[0]))
        self.assertFalse(pattern.match("/replays/ants-TREASURE-20261008-143209Z.antsrep/x"))

    def test_the_pattern_says_what_the_store_says(self):
        source = read("src", "ants_server", "replay_store.cpp")
        stem = int(re.search(r"kMaxStemChars = (\d+);", source).group(1))
        same_second = int(re.search(r"kMaxSameSecond = (\d+);", source).group(1))
        text = re.search(r'if \(\$uri !~ "([^"]+)"\)', FILES).group(1)
        self.assertIn("[A-Za-z0-9_]{1,%d}" % stem, text)                                      # the stem of the map's name
        self.assertIn("(-[0-9]{1,%d})?" % len(str(same_second)), text)                       # a later match of the same second
        self.assertIn("[0-9]{8}-[0-9]{6}Z", text)
        self.assertIn("\\.antsrep\\z", text)                                                  # (\z: the end of the name, not before a last line feed)
        header = read("include", "ants_server", "replay_store.hpp")
        self.assertIn('kExtension = ".antsrep"', header)

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

    def test_the_two_blocks_are_the_same_but_for_the_name_check(self):
        listing = [l.strip() for l in LIST.splitlines() if l.strip()]
        files = [l.strip() for l in FILES.splitlines() if l.strip() and not l.strip().startswith("if ($uri")]
        self.assertEqual(listing, files)                                                      # (one allowance, one door, one set of timeouts: they cannot drift apart)

    def test_they_go_to_the_replay_port_that_the_stack_gives_the_server(self):
        for what, block in BOTH:
            target = re.search(r"set \$ants_replays ([a-z0-9.-]+):(\d+);", block)
            self.assertIsNotNone(target, what)
            self.assertIn("proxy_pass http://$ants_replays;", block, what)                    # (a variable: the name is resolved per request, so the page starts without the server)
            self.assertIn("resolver 127.0.0.11", block, what)
            door = re.search(r"set \$ants_server ([a-z0-9.-]+):(\d+);", BUSY)
            self.assertEqual(target.group(1), door.group(1), what)                            # the same server (its name on the network of the stack) as /ws and /busy ...
            self.assertNotEqual(target.group(2), door.group(2), what)                         # ... and not the WebSocket port: a door of its own
            for stack in ("docker-compose.stack.yml", "docker-compose.staging.yml"):
                command = re.search(r'command: \[.*?"--replay-port", "\$\{ANTS_REPLAY_PORT:-(\d+)\}"', read(stack))
                self.assertIsNotNone(command, stack)
                self.assertEqual(command.group(1), target.group(2), stack)
            self.assertIn("EXPOSE", read("Dockerfile.server"))
            self.assertRegex(read("Dockerfile.server"), r"(?m)^EXPOSE [0-9 ]*\b%s\b" % target.group(2))

    def test_it_is_rate_limited_with_one_allowance_for_both_addresses_and_the_zone_is_outside_the_server(self):
        zone = re.search(r"limit_req_zone \$binary_remote_addr zone=ants_replays:(\d+)m rate=(\d+)r/([sm]);", CONF)
        self.assertIsNotNone(zone)
        self.assertLess(CONF.index(zone.group(0)), CONF.index("server {"))                  # http context: before the server block
        per_second = int(zone.group(2)) / (1.0 if zone.group(3) == "s" else 60.0)
        self.assertGreaterEqual(per_second, 1.0)                                              # a page that lists and fetches is not held up ...
        self.assertLessEqual(per_second, 10.0)                                                # ... and a flood is not let through
        for what, block in BOTH:
            self.assertIn("limit_req zone=ants_replays burst=10 nodelay;", block, what)
            self.assertIn("limit_req_status 503;", block, what)
        self.assertEqual(sorted(re.findall(r"limit_req_zone [^;]*\bzone=(\w+):", HEAD)), ["ants_busy", "ants_local", "ants_replays", "ants_stats"])      # (a zone of its own: a flood of one is not another's)

    def test_it_has_short_timeouts_and_adds_nothing_of_its_own(self):
        for what, block in BOTH:
            for directive, limit in (("proxy_connect_timeout", 5), ("proxy_read_timeout", 15), ("proxy_send_timeout", 15)):
                value = re.search(r"%s (\d+)s;" % directive, block)
                self.assertIsNotNone(value, what + directive)
                self.assertLessEqual(int(value.group(1)), limit, what + directive)
            for forbidden in ("add_header", "expires", "proxy_cache", "proxy_ignore_headers", "alias", "root", "try_files", "rewrite", "sub_filter", "proxy_intercept_errors"):
                self.assertNotRegex(block, r"\b%s\b" % forbidden, what + forbidden)           # (the server's own Cache-Control: no-store is the answer's header, and its 404 JSON passes)
            self.assertIn("proxy_hide_header X-Content-Type-Options;", block, what)          # (the server-level nosniff line applies once)

    def test_a_server_without_the_door_is_answered_404_with_a_json_body(self):
        for what, block in BOTH:
            self.assertIn("error_page 502 504 = @replays_off;", block, what)
        self.assertIn("return 404 '{", OFF)
        self.assertIn("default_type application/json;", OFF)
        self.assertIn('add_header Cache-Control "no-store" always;', OFF)
        self.assertEqual(len(re.findall(r"add_header X-Content-Type-Options", OFF)), 1)      # (a location with a header of its own does not inherit the server's)
        self.assertNotIn("proxy_pass", OFF)
        body = re.search(r"return 404 '(\{.*\})';", OFF)
        self.assertIsNotNone(body)
        self.assertIn('"error"', body.group(1))

    def test_the_stack_gives_the_door_and_the_documents_say_so(self):
        stack = read("docker-compose.stack.yml")
        staging = read("docker-compose.staging.yml")
        for text in (stack, staging):
            self.assertIn('"--replay-demo", "--replays-days", "${ANTS_REPLAY_DAYS:-30}", "--replays-max-mb", "${ANTS_REPLAY_MAX_MB:-100}", "--replay-port", "${ANTS_REPLAY_PORT:-4020}"]', text)
        self.assertNotRegex(stack, r"(?m)^\s*- \"[^\"]*4020[^\"]*\"")                      # (the replay port is not published on the host)
        self.assertNotRegex(staging, r"(?m)^\s*- \"[^\"]*4020[^\"]*\"")
        for variable in ("ANTS_REPLAY_DAYS", "ANTS_REPLAY_MAX_MB", "ANTS_REPLAY_PORT"):
            self.assertIn(variable, stack.split("services:")[0], variable)                    # the header of the stack file names them
            self.assertIn(variable, read("docs", "SERVER.md"), variable)
        dockerfile = read("Dockerfile.server")
        entry = re.search(r"(?m)^ENTRYPOINT \[(.*)\]$", dockerfile).group(1)
        self.assertIn('"--replay-any-interface"', entry)                                      # (a published port does not reach the loopback address of a container: nginx does not either)
        self.assertNotIn("--replay-port", entry)                                              # (off in the program and in the image: the stack file gives the port)
        main = read("src", "ants_server", "main.cpp")
        for option in ("--replay-port", "--replay-any-interface", "--replay-demo", "--replays-days", "--replays-max-mb", "--replays-dir", "--no-replays"):
            self.assertIn('"%s"' % option, main, option)
        text = read("docs", "REPLAYS.md") + read("docs", "SERVER.md") + read("docs", "NETWORK_PORT.md")
        for needle in ("/replays", "public", "30 days", "ANTS_REPLAY_PORT"):
            self.assertIn(needle, text, needle)


# ------------------------------------------------------------------------------------------------------------------------------------------------------------ the blocks, run
LOG_FORMAT = stats.LOG_FORMAT
# The stand-in of a server that has the replay port (4020): the list is JSON, a file is bytes; the first server only answers the rig's readiness probe
STUB = LOG_FORMAT + """server {
    listen 4002;
    location = /ready { return 200 'ready'; }
}
server {
    listen 4020;
    access_log /dev/stdout stub;
    location = /replays { default_type application/json; add_header Cache-Control "no-store" always; add_header X-Content-Type-Options "nosniff" always; return 200 '{"replays":[],"count":0,"keep_days":30}'; }
    location ~ "^/replays/ants-[A-Za-z0-9_]+-[0-9]{8}-[0-9]{6}Z(-[0-9]+)?\\.antsrep$" { default_type application/octet-stream; add_header Cache-Control "no-store" always; add_header X-Content-Type-Options "nosniff" always; return 200 'a stand-in file'; }
}
"""
LIST_REQUEST = "GET /replays HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
FILE = GOOD_NAMES[0]


@unittest.skipUnless(shutil.which("docker"), "docker is not installed: the blocks of docker/nginx.conf for /replays were NOT run (tests/scripts/test_nginx_replays.py)")
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

    def sent(self, path):
        return self.stub_requests(path)

    def counts(self, *names):
        """How many requests the stand-in has been sent so far for the list and for each of the files: its log goes back to the first test of the class, so a test looks at what came after this."""
        return dict((path, len(self.sent(path))) for path in ["/replays"] + ["/replays/" + name for name in names])

    def since(self, before):
        return [one for path, count in before.items() for one in self.sent(path)[count:]]

    def test_the_list_and_a_file_go_to_the_server_as_plain_gets(self):
        before = self.counts(*GOOD_NAMES[:3])
        status, headers, body = self.ask("GET", "/replays")
        self.assertEqual(status, 200)
        self.assertEqual(body, b'{"replays":[],"count":0,"keep_days":30}')
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])                  # the server's own line, once: nothing downstream may keep the list
        self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])          # once: the block hides the server's and keeps the page's
        self.assertEqual(self.names(headers, "cross-origin-opener-policy"), ["same-origin"])
        self.assertEqual(self.names(headers, "cross-origin-embedder-policy"), ["require-corp"])
        self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"))
        for name in GOOD_NAMES[:3]:
            status, headers, body = self.ask("GET", "/replays/" + name)
            self.assertEqual((status, body), (200, b"a stand-in file"), name)
            self.assertEqual(self.names(headers, "content-type"), ["application/octet-stream"])
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"])
            self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])
        seen = self.since(before)
        self.assertEqual(len(seen), 4)
        for one in seen:
            self.assertEqual((one["method"], one["args"], one["cl"], one["te"], one["up"], one["conn"], one["st"], one["host"]), ("GET", "-", "-", "-", "-", "close", "200", "127.0.0.1"))

    def test_every_other_method_a_query_and_a_name_the_store_cannot_make_are_refused_before_the_server(self):
        before = self.counts(*GOOD_NAMES)
        for path in ("/replays", "/replays/" + FILE):
            for method in ("POST", "PUT", "DELETE", "PATCH", "OPTIONS"):
                self.assertEqual(self.ask(method, path, body=b"" if method in ("POST", "PUT", "PATCH") else None)[0], 405, (method, path))
            self.assertEqual(self.ask("HEAD", path)[0], 405, path)
            for query in ("?x=1", "?x", "?limit=5", "?a=b&c=d"):
                self.assertEqual(self.ask("GET", path + query)[0], 404, path + query)         # a query is no request for the list or a file
        for name in ("", "x.antsrep", "ants-.antsrep", "ants-" + "A" * 25 + "-20261008-143209Z.antsrep", "ants-TREASURE-20261008-143209Z-12345.antsrep", "ants-TREASURE-20261008-143209z.antsrep",
                     "ants-TREASURE-20261008-143209Z.ANTSREP", "ants-TREASURE-20261008-143209Z.antsrep.tmp", "sub/" + FILE, FILE + "/", FILE + "/x", FILE + "%0A", FILE + "%20", ".", ".hidden"):
            self.assertEqual(self.ask("GET", "/replays/" + name)[0], 404, name)               # (a name under /replays/ that the store could not have made: refused here)
        for name in ("../" + FILE, "%2e%2e/" + FILE, "sub/../../" + FILE):
            status, _, body = self.ask("GET", "/replays/" + name)                             # (a path that leaves /replays/ is another address of the site: the game page)
            self.assertNotEqual(body, b"a stand-in file", name)
        self.assertIn(self.ask("GET", "/replays/" + FILE + "%00")[0], (400, 404))             # (a NUL in the address: nginx refuses it itself)
        self.assertEqual(self.since(before), [])                                              # none of these reached the server, and none used up the allowance
        for path in ("/replays", "/replays/" + FILE):
            self.assertEqual(self.ask("GET", path + "?")[0], 200, path)                       # (a "?" with nothing behind it carries no query: nginx's $args is empty)
        self.assertEqual(len(self.since(before)), 2)                                          # (and these two did reach the server: the first requests it was sent)

    def test_the_server_is_sent_a_host_and_a_connection_and_nothing_else_of_the_visitor(self):
        visitor = {"Cookie": "session=" + "c" * 8100, "User-Agent": "a-browser/1.0", "Authorization": "Bearer a-secret", "X-Forwarded-For": "203.0.113.9", "Referer": "https://example.org/page",
                   "Origin": "https://example.org", "Accept-Language": "de", "X-Custom": "yes", "Sec-Fetch-Site": "same-origin", "Accept": "*/*", "Pragma": "no-cache", "Cache-Control": "no-cache",
                   "If-None-Match": '"abc"', "Range": "bytes=0-5", "X-Real-IP": "203.0.113.9", "Forwarded": "for=203.0.113.9", "Accept-Encoding": "gzip, br"}
        before = self.counts(FILE)
        self.assertEqual(self.ask("GET", "/replays", headers=visitor)[0], 200)
        self.assertEqual(self.ask("GET", "/replays/" + FILE, headers=visitor)[0], 200)
        listing, one_file = self.sent("/replays")[before["/replays"]:], self.sent("/replays/" + FILE)[before["/replays/" + FILE]:]
        self.assertEqual((len(listing), len(one_file)), (1, 1))
        # the whole request that the server was sent, to the byte: the request line, a Host (the visitor's, without a port) and a Connection
        self.assertEqual(listing[0]["len"], len(LIST_REQUEST))
        self.assertEqual(one_file[0]["len"], len("GET /replays/%s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n" % FILE))
        for one in (listing[0], one_file[0]):
            for field in ("ck", "ua", "az", "xf", "rf", "og", "sf", "ae", "xc"):
                self.assertEqual(one[field], "-", field)                                      # (and the ones that could matter, by name)
            self.assertEqual((one["host"], one["conn"], one["up"], one["te"], one["cl"]), ("127.0.0.1", "close", "-", "-", "-"))

    def test_a_burst_of_ten_goes_through_at_two_a_second_and_the_rest_is_503_for_both_addresses(self):
        before = self.counts()
        started = time.monotonic()
        codes = [self.ask("GET", "/replays")[0] for _ in range(40)]
        took = time.monotonic() - started
        allowed = codes.count(200)
        self.assertTrue(set(codes) <= {200, 503}, set(codes))
        self.assertTrue(11 <= allowed <= 11 + int(2 * took) + 1, (allowed, took))             # the first request and the burst of 10, and two a second while this goes on
        self.assertEqual(codes[:allowed], [200] * allowed)                                    # (the refusals come after the allowance, none between)
        self.assertEqual(len(self.since(before)), allowed)                                    # what was refused never reached the server
        status = self.ask("GET", "/replays/" + FILE)[0]
        self.assertIn(status, (200, 503))                                                     # the files share the allowance of the list (one zone): after a flood of the list they wait too
        if took < 2.0:
            self.assertEqual(status, 503)
        time.sleep(1.2)                                                                       # two a second: a token or two are back (one a second would give one, ten a second a dozen)
        again = [self.ask("GET", "/replays")[0] for _ in range(6)]
        self.assertTrue(1 <= again.count(200) <= 4, again)

    def test_the_neighbours_of_the_two_addresses_are_the_game_page_and_nothing_is_passed_on(self):
        for path in ("/replay", "/replays2", "/replays.json", "/replaysx/"):
            status, _, body = self.ask("GET", path)
            self.assertEqual(status, 200, path)                                               # (the try_files of the site: the game page, as any unknown address)
            self.assertIn(b"a stand-in page", body, path)
            self.assertEqual(self.sent(path), [], path)


@unittest.skipUnless(shutil.which("docker"), "docker is not installed: the answer of /replays without a door was NOT run (tests/scripts/test_nginx_replays.py)")
class TheDoorIsOff(stats.Rig, unittest.TestCase):
    """A server that was started without --replay-port, or is not there: the stand-in only has the WebSocket port, so the connection to the replay port is refused."""
    STUB = stats.STUB

    @classmethod
    def setUpClass(cls):
        cls.start_rig()

    @classmethod
    def tearDownClass(cls):
        cls.stop_rig()

    def test_the_list_and_a_file_are_404_with_a_json_body_and_the_rest_of_the_site_goes_on(self):
        for path in ("/replays", "/replays/" + FILE):
            started = time.monotonic()
            status, headers, body = self.ask("GET", path)
            self.assertEqual(status, 404, path)
            self.assertLess(time.monotonic() - started, 3.0)                                  # (refused at once, not the timeout of a server that says nothing)
            self.assertTrue(self.names(headers, "content-type")[0].startswith("application/json"), path)
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"], path)
            self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"], path)
            self.assertIn(b'"error"', body, path)
            self.assertNotIn(b"nginx", body, path)                                            # (not nginx's own page)
        self.assertEqual(self.ask("POST", "/replays", body=b"")[0], 405)                      # the checks are the same
        self.assertEqual(self.ask("GET", "/replays?x=1")[0], 404)
        status, _, body = self.ask("GET", "/stats")                                           # the WebSocket port's doors are not touched
        self.assertEqual((status, body), (200, b'{"stub":true}'))


if __name__ == "__main__":
    unittest.main()
