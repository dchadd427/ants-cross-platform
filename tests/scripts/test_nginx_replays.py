#!/usr/bin/env python3
"""docker/nginx.conf routes the recorded matches of the game server: GET /replays and GET /replays/<file> (run by ./run_tests.sh --fast and by the CI where docker works).

ants_server keeps the matches that its rooms play (docs/REPLAYS.md "On the game server") and, when it is started with --replay-port, answers the list and the files on a port of its own: read
only, no secret; the files and the list hold the names that the players typed, and no address, room code or chat. The stack file starts it that way, so the list and the files of the site are PUBLIC; the page's nginx forwards the two addresses like /busy,
as the one request each takes and nothing else:

  - /replays is an exact location and /replays/ a prefix location, the only ones of the name (and a named location that answers when the server has no such door);
  - only a GET without a query goes through (405 and 404 otherwise); a file name of the wrong shape is 404 here and never reaches the server (the store decides which names exist);
  - nothing of the visitor's request is passed on but Host and Connection: no cookie, no body, no upgrade; the connection is not kept; the browser is not told to keep an answer, and the LIST is
    cached for five seconds in nginx (one entry for everybody, in the cache of /stats), so a crowd asks the game server once in five seconds;
  - the allowances are the whole site's, as behind a reverse proxy every visitor has the proxy's address: twenty a second for the list, ten for the files, a zone each (503 beyond), short timeouts;
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


def published_port_lines(text):
    """Every line of a compose file that is in a `ports:` block (the short list, the long form with `published:` and `target:`, a `ports: [..]` on one line): what the host is given"""
    lines = text.splitlines()
    found = []
    for at, line in enumerate(lines):
        match = re.match(r"^(\s*)ports:\s*(.*)$", line)
        if not match:
            continue
        indent = len(match.group(1))
        if match.group(2).strip():
            found.append(line)                                                                # (ports: ["4020:4020"])
        for follow in lines[at + 1:]:
            if follow.strip() and not follow.lstrip().startswith("#") and len(follow) - len(follow.lstrip()) <= indent and not follow.lstrip().startswith("- "):
                break
            if follow.strip() and not follow.lstrip().startswith("#"):
                found.append(follow)
    return found


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

    def test_the_two_blocks_are_the_same_but_for_the_name_check_the_allowance_and_the_cache(self):
        def lines(block, *own):
            return [l.strip() for l in block.splitlines() if l.strip() and not l.strip().startswith(own)]
        own = ("if ($uri", "limit_req zone=", "proxy_cache", "proxy_ignore_headers")
        self.assertEqual(lines(LIST, *own), lines(FILES, *own))                               # (one door, one set of timeouts, one set of checks: they cannot drift apart)

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

    def test_each_address_has_an_allowance_of_its_own_sized_for_the_whole_site_and_the_zones_are_outside_the_server(self):
        # (behind a reverse proxy every visitor has the proxy's address: docs/audit/site_stats_notes.md; an allowance per visitor would be one for everybody)
        for zone_name, block, low, high, burst in (("ants_replays", LIST, 10.0, 30.0, 100), ("ants_replay_files", FILES, 5.0, 20.0, 20)):
            zone = re.search(r"limit_req_zone \$binary_remote_addr zone=%s:(\d+)m rate=(\d+)r/([sm]);" % zone_name, CONF)
            self.assertIsNotNone(zone, zone_name)
            self.assertLess(CONF.index(zone.group(0)), CONF.index("server {"))              # http context: before the server block
            per_second = int(zone.group(2)) / (1.0 if zone.group(3) == "s" else 60.0)
            self.assertGreaterEqual(per_second, low, zone_name)                               # more than the visitors of a busy site need ...
            self.assertLessEqual(per_second, high, zone_name)                                 # ... and less than the game server's loop can serve
            self.assertIn("limit_req zone=%s burst=%d nodelay;" % (zone_name, burst), block, zone_name)
            self.assertIn("limit_req_status 503;", block, zone_name)
        self.assertEqual(sorted(re.findall(r"limit_req_zone [^;]*\bzone=(\w+):", HEAD)), ["ants_busy", "ants_local", "ants_replay_files", "ants_replays", "ants_stats"])      # (a flood of one is not another's)
        self.assertNotIn("zone=ants_replays ", FILES)
        self.assertNotIn("zone=ants_replay_files ", LIST)

    def test_the_list_is_cached_for_five_seconds_in_the_cache_of_the_numbers_and_a_file_is_not(self):
        stats_block = block_of(CONF, "location = /stats ")
        for directive in ("proxy_cache ants_stats_cache;", "proxy_cache_valid 200 5s;", "proxy_ignore_headers Cache-Control;", "proxy_cache_lock on;", "proxy_cache_use_stale updating error timeout;"):
            self.assertIn(directive, LIST, directive)
            self.assertIn(directive, stats_block, directive)                                  # (the same cache, the same rules as the numbers')
        key = re.search(r'proxy_cache_key "([^"]+)";', LIST).group(1)
        self.assertNotEqual(key, re.search(r'proxy_cache_key "([^"]+)";', stats_block).group(1))     # (an entry of its own in that cache)
        self.assertNotRegex(key, r"\$")                                                       # (one entry for everybody: the key holds nothing of the visitor)
        self.assertLess(LIST.index("limit_req "), LIST.index("proxy_cache "))
        self.assertLess(LIST.index("proxy_cache "), LIST.index("proxy_pass"))
        for directive in ("proxy_cache", "proxy_cache_key", "proxy_cache_valid", "proxy_ignore_headers"):
            self.assertNotRegex(FILES, r"\b%s\b" % directive, directive)                     # (a file is read by the server each time: nothing of it is kept here)
        path = re.search(r"proxy_cache_path (\S+) keys_zone=ants_stats_cache:(\d+)m max_size=(\d+)m", HEAD)
        self.assertIsNotNone(path)
        self.assertGreaterEqual(int(path.group(3)), 4)                                        # (room for the numbers and the list: a list of 200 matches is about 60 KB)

    def test_it_has_short_timeouts_and_adds_nothing_of_its_own(self):
        for what, block in BOTH:
            for directive, limit in (("proxy_connect_timeout", 5), ("proxy_read_timeout", 15), ("proxy_send_timeout", 15)):
                value = re.search(r"%s (\d+)s;" % directive, block)
                self.assertIsNotNone(value, what + directive)
                self.assertLessEqual(int(value.group(1)), limit, what + directive)
            for forbidden in ("add_header", "expires", "alias", "root", "try_files", "rewrite", "sub_filter", "proxy_intercept_errors"):
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

    def test_the_check_of_the_published_ports_sees_every_way_to_write_one(self):
        compose = "services:\n  a:\n    ports:\n      - \"1:1\"\n      - 4020:4020\n      - '4020:4020'\n      - target: 4020\n        published: 4020\n    other: x\n  b:\n    ports: [\"4020:4020\"]\n  c:\n    image: x\n"
        found = published_port_lines(compose)
        self.assertEqual(len([l for l in found if "4020" in l]), 5)
        self.assertEqual(published_port_lines("services:\n  a:\n    command: [\"--replay-port\", \"4020\"]\n    ports:\n      - \"1:1\"\n    environment:\n      A: 4020\n"), ['      - "1:1"'])

    def test_the_stack_gives_the_door_and_the_documents_say_so(self):
        stack = read("docker-compose.stack.yml")
        staging = read("docker-compose.staging.yml")
        for text in (stack, staging):
            self.assertIn('"--replay-demo", "--replays-days", "${ANTS_REPLAY_DAYS:-30}", "--replays-max-mb", "${ANTS_REPLAY_MAX_MB:-100}", "--replay-port", "${ANTS_REPLAY_PORT:-4020}"]', text)
        for name, text in (("docker-compose.stack.yml", stack), ("docker-compose.staging.yml", staging)):
            for line in published_port_lines(text):
                self.assertNotIn("4020", line, name)                                          # (the replay port is not published on the host, whatever the way to write it)
        for variable in ("ANTS_REPLAY_DAYS", "ANTS_REPLAY_MAX_MB", "ANTS_REPLAY_PORT"):
            self.assertIn(variable, stack.split("services:")[0], variable)                    # the header of the stack file names them
            self.assertIn(variable, read("docs", "SERVER.md"), variable)
        dockerfile = read("Dockerfile.server")
        entry = re.findall(r"(?m)^ENTRYPOINT \[(.*)\]$", dockerfile)[-1]                       # (the last stage is the server; the stage before it is the bot arena's)
        self.assertIn('"--replay-any-interface"', entry)                                      # (a published port does not reach the loopback address of a container: nginx does not either)
        self.assertNotIn("--replay-port", entry)                                              # (off in the program and in the image: the stack file gives the port)
        main = read("src", "ants_server", "main.cpp")
        for option in ("--replay-port", "--replay-any-interface", "--replay-demo", "--replays-days", "--replays-max-mb", "--replays-dir", "--no-replays"):
            self.assertIn('"%s"' % option, main, option)
        for document, needles in (("REPLAYS.md", ("public", "30 days", "/replays")), ("SERVER.md", ("ANTS_REPLAY_PORT", "30 days", "--replay-port")), ("NETWORK_PORT.md", ("/replays", "replay-port"))):
            for needle in needles:
                self.assertIn(needle, read("docs", document), document + ": " + needle)           # (each document says its own part, not all of them together)

    def test_the_pages_that_ask_for_a_name_tell_players_the_days_that_the_stack_keeps_the_matches(self):
        # the owner chose "Add the line" (2026-10-08): the front page and the game page's name card say that online matches are recorded, kept for N days and public with the players' names. The pages are
        # static, so N is the stack's default (ANTS_REPLAY_DAYS:-N); an operator who changes the variable changes the line too (docs/SERVER.md says so)
        days = re.search(r'"--replays-days", "\$\{ANTS_REPLAY_DAYS:-(\d+)\}"', read("docker-compose.stack.yml")).group(1)
        for page in ("lobby.html", "shell.html"):
            self.assertIn("Online matches are recorded and kept for %s days. Anybody can watch them, live or later, and they show the players&rsquo; names." % days, read("web", page), page)
        self.assertIn("`ANTS_REPLAY_DAYS`). A server with other settings", read("docs", "SERVER.md"))     # (the document that names the variable says that the pages repeat its number and what to change)
        self.assertIn("changes the words in `web/lobby.html` and `web/shell.html`", read("docs", "SERVER.md"))

    def test_the_stack_files_say_what_is_kept_which_port_to_leave_alone_and_who_can_reach_the_door(self):
        for name, needles in (("docker-compose.stack.yml", ("ran 30 seconds or more", "Leave it at 4020", "or set 0", "restarts again and again", "the replay port (read only, no secret)", "do not put a container there that you do not trust")),
                              ("docker-compose.server.yml", ("ran 30 seconds or more", "the replay port when it is switched on (read only, no secret", "do not put a container there that you do not trust")),
                              ("docker-compose.staging.yml", ("ANTS_REPLAY_PORT", "not published")),
                              ("Dockerfile.server", ("do not publish it either", "five-second cache"))):
            text = " ".join(re.sub(r"(?m)^\s*#\s?", "", read(name)).split())                    # (a comment broken over lines reads as one)
            for needle in needles:
                self.assertIn(needle, text, name + ": " + needle)
            self.assertNotRegex(text, r"(?i)\bevery match (that ends )?(of its rooms )?is kept\b", name)       # (a match under 30 seconds is not: the old claim must not come back)


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

    def sent(self, path, at_least=0):
        return self.stub_requests(path, at_least)

    def counts(self, *names):
        """How many requests the stand-in has been sent so far for the list and for each of the files: its log goes back to the first test of the class, so a test looks at what came after this."""
        return dict((path, len(self.sent(path))) for path in ["/replays"] + ["/replays/" + name for name in names])

    def since(self, before, owed=0):
        """What came after `before`; `owed`: how many requests each of its paths has been sent since (see logged())"""
        return [one for path, count in before.items() for one in self.sent(path, count + owed)[count:]]

    def test_the_list_and_a_file_go_to_the_server_as_plain_gets(self):
        before = self.counts(*GOOD_NAMES[:3])
        status, headers, body = self.ask("GET", "/replays")
        self.assertEqual(status, 200)
        self.assertEqual(body, b'{"replays":[],"count":0,"keep_days":30}')
        self.assertEqual(self.names(headers, "cache-control"), ["no-store"])                  # the server's own line, once: no browser keeps the list
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
        seen = self.since(before, 1)
        self.assertEqual(len(seen), 4)
        for one in seen:
            self.assertEqual((one["method"], one["args"], one["cl"], one["te"], one["up"], one["conn"], one["st"], one["host"]), ("GET", "-", "-", "-", "-", "close", "200", "127.0.0.1"))

    def test_every_other_method_a_query_and_a_name_of_the_wrong_shape_are_refused_before_the_server(self):
        before = self.stub_lines()
        for path in ("/replays", "/replays/" + FILE):
            for method in ("POST", "PUT", "DELETE", "PATCH", "OPTIONS"):
                self.assertEqual(self.ask(method, path, body=b"" if method in ("POST", "PUT", "PATCH") else None)[0], 405, (method, path))
            self.assertEqual(self.ask("HEAD", path)[0], 405, path)
            for query in ("?x=1", "?x", "?limit=5", "?a=b&c=d"):
                self.assertEqual(self.ask("GET", path + query)[0], 404, path + query)         # a query is no request for the list or a file
        for name in ("", "x.antsrep", "ants-.antsrep", "ants-" + "A" * 25 + "-20261008-143209Z.antsrep", "ants-TREASURE-20261008-143209Z-12345.antsrep", "ants-TREASURE-20261008-143209z.antsrep",
                     "ants-TREASURE-20261008-143209Z.ANTSREP", "ants-TREASURE-20261008-143209Z.antsrep.tmp", "sub/" + FILE, FILE + "/", FILE + "/x", FILE + "%0A", FILE + "%20", ".", ".hidden"):
            self.assertEqual(self.ask("GET", "/replays/" + name)[0], 404, name)               # (a name under /replays/ of a shape that the store does not make: refused here)
        for name in ("../" + FILE, "%2e%2e/" + FILE, "sub/../../" + FILE):
            status, _, body = self.ask("GET", "/replays/" + name)                             # (a path that leaves /replays/ is another address of the site: the game page)
            self.assertNotEqual(body, b"a stand-in file", name)
        self.assertIn(self.ask("GET", "/replays/" + FILE + "%00")[0], (400, 404))             # (a NUL in the address: nginx refuses it itself)
        self.assertEqual(self.stub_lines(), before)                                           # none of these reached the server (not a request, not a line of its log), and none used up the allowance
        for path in ("/replays", "/replays/" + FILE):
            self.assertEqual(self.ask("GET", path + "?")[0], 200, path)                       # (a "?" with nothing behind it carries no query: nginx's $args is empty)
        self.assertEqual(self.stub_lines(before + 2) - before, 2)                             # (and these two did reach the server: the first requests it was sent)

    def test_the_server_is_sent_a_host_and_a_connection_and_nothing_else_of_the_visitor(self):
        visitor = {"Cookie": "session=" + "c" * 8100, "User-Agent": "a-browser/1.0", "Authorization": "Bearer a-secret", "X-Forwarded-For": "203.0.113.9", "Referer": "https://example.org/page",
                   "Origin": "https://example.org", "Accept-Language": "de", "X-Custom": "yes", "Sec-Fetch-Site": "same-origin", "Accept": "*/*", "Pragma": "no-cache", "Cache-Control": "no-cache",
                   "If-None-Match": '"abc"', "Range": "bytes=0-5", "X-Real-IP": "203.0.113.9", "Forwarded": "for=203.0.113.9", "Accept-Encoding": "gzip, br"}
        before = self.counts(FILE)
        self.assertEqual(self.ask("GET", "/replays", headers=visitor)[0], 200)
        self.assertEqual(self.ask("GET", "/replays/" + FILE, headers=visitor)[0], 200)
        listing = self.sent("/replays", before["/replays"] + 1)[before["/replays"]:]
        one_file = self.sent("/replays/" + FILE, before["/replays/" + FILE] + 1)[before["/replays/" + FILE]:]
        self.assertEqual((len(listing), len(one_file)), (1, 1))
        # the whole request that the server was sent, to the byte: the request line, a Host (the visitor's, without a port) and a Connection
        self.assertEqual(listing[0]["len"], len(LIST_REQUEST))
        self.assertEqual(one_file[0]["len"], len("GET /replays/%s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n" % FILE))
        for one in (listing[0], one_file[0]):
            for field in ("ck", "ua", "az", "xf", "rf", "og", "sf", "ae", "xc"):
                self.assertEqual(one[field], "-", field)                                      # (and the ones that could matter, by name)
            self.assertEqual((one["host"], one["conn"], one["up"], one["te"], one["cl"]), ("127.0.0.1", "close", "-", "-", "-"))

    def test_the_list_is_cached_for_five_seconds_for_everybody_and_a_file_is_asked_of_the_server_every_time(self):
        before = self.stub_lines()
        first = self.ask("GET", "/replays")
        self.assertEqual(first[0], 200)
        crowd = [self.ask("GET", "/replays", headers={"Cookie": "visitor=%d" % i, "User-Agent": "visitor-%d" % i}) for i in range(8)]
        for status, headers, body in crowd:
            self.assertEqual((status, body), (200, first[2]))                                 # (the same list for everybody)
            self.assertEqual(self.names(headers, "cache-control"), ["no-store"])              # (and a browser is still told to keep nothing)
            self.assertEqual(self.names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(self.stub_lines(before + 1) - before, 1)                             # the server was asked once for the nine
        for _ in range(3):
            self.assertEqual(self.ask("GET", "/replays/" + FILE)[0], 200)
        self.assertEqual(self.stub_lines(before + 4) - before, 4)                             # a file is asked of it each time
        time.sleep(6.2)                                                                       # five seconds are over (nginx counts whole seconds: an entry made at the start of a second is good for the whole of the fifth one, so 5.5 s is not always enough): the next one asks again, the others after it are served from that
        self.assertEqual(self.ask("GET", "/replays")[0], 200)
        self.assertEqual(self.ask("GET", "/replays")[0], 200)
        self.assertEqual(self.stub_lines(before + 5) - before, 5)

    def test_the_list_may_be_asked_twenty_times_a_second_by_the_whole_site_with_a_burst_of_a_hundred_and_the_files_have_an_allowance_of_their_own(self):
        before = self.stub_lines()
        started = time.monotonic()
        codes = [self.ask("GET", "/replays")[0] for _ in range(160)]
        took = time.monotonic() - started
        allowed = codes.count(200)
        self.assertTrue(set(codes) <= {200, 503}, set(codes))
        self.assertTrue(101 <= allowed <= 101 + int(20 * took) + 1, (allowed, took))          # the first request and the burst of 100, and twenty a second while this goes on
        self.assertEqual(codes[:101], [200] * 101)                                            # (the refusals come after the allowance, none between)
        self.assertLessEqual(self.stub_lines() - before, 1 + int(took / 5))                   # the cache: the server was asked once (and again every five seconds), not 100 times
        self.assertEqual(self.ask("GET", "/replays/" + FILE)[0], 200)                         # the files are another zone: a flood of the list does not use their allowance

    def test_ten_files_a_second_for_the_whole_site_with_a_burst_of_twenty_and_the_rest_is_503_and_never_reaches_the_server(self):
        before = self.stub_lines()
        started = time.monotonic()
        codes = [self.ask("GET", "/replays/" + FILE)[0] for _ in range(60)]
        took = time.monotonic() - started
        allowed = codes.count(200)
        self.assertTrue(set(codes) <= {200, 503}, set(codes))
        self.assertTrue(21 <= allowed <= 21 + int(10 * took) + 1, (allowed, took))            # the first request and the burst of 20, and ten a second while this goes on
        self.assertEqual(codes[:21], [200] * 21)                                              # (the refusals come after the allowance, none between)
        self.assertEqual(self.stub_lines(before + allowed) - before, allowed)                 # what was refused never reached the server
        self.assertEqual(self.ask("GET", "/replays")[0], 200)                                 # the list is another zone: a flood of files does not use its allowance
        flood_over = time.monotonic()
        time.sleep(0.8)                                                                       # ten a second: some tokens are back (not one a second, not a hundred)
        again = [self.ask("GET", "/replays/" + FILE)[0] for _ in range(14)]
        self.assertTrue(1 <= again.count(200) <= int(10 * (time.monotonic() - flood_over)) + 2, again)

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
