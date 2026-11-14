#!/usr/bin/env python3
"""The clean addresses of the pages (/play, /matches, /match, /legal, /changelog, /mapmaker) and the web server that names no version (run by ./run_tests.sh --fast and by the CI;
tests/data/nginx_clean_urls_mutants.json breaks one rule at a time, for tools/mutate.py).

The text of docker/nginx.conf is checked everywhere:
  - every page has its own exact location, which serves the page's file by an internal rewrite and has no header of its own (the headers are those of the file's address);
  - the old .html address of every page, and the same clean address with a slash, is in one map that the server block's `if` turns into a 302 that keeps the query (a browser does not keep a 302: going back needs nothing from a visitor); /index.html and /lobby.html go to "/"; changelog.html, legal.html and matches.html are the exception: /changelog, /legal and /matches were redirects to those files before, so the files answer 200 and are not in the map;
  - no other location can catch a page's address, and no page's address can catch another location's: a small model of nginx's choice of a location (exact, then the longest prefix, ^~ stops it,
    then the regular expressions in order) is asked about the pages and about every path of the game server, the hub and the admin page;
  - the old addresses of the page of the matches that run now (/live-now, /live-now/, /live.html) are a 302 to the front page, "/", with no query, from locations of their own (not the map); /live and /live/<id> are still the game server's data;
  - `server_tokens off` is in the http part of the file and in the server block, and nothing turns it on;
  - no page of the site, script or style builds an address with .html for one of these pages.
Where an nginx program is installed the REAL file RUNS with the real pages of web/ (and stand-ins for the files that the image's build makes) and tests/scripts/web_routes_check.py asks it for every
route, with the query kept, the headers of both forms, HEAD, a slash after a page, and the Server header and an error page without a version. Skipped, with the reason, where there is no nginx.
"""
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import http.client

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_nginx_headers as H  # noqa: E402  (the helpers that start the file's nginx)
import test_nginx_stats as stats  # noqa: E402  (the helpers that read the file)

REPO = stats.REPO
RAW = stats.read("docker", "nginx.conf")
CONF = stats.strip_comments(RAW)
HEAD = CONF[:CONF.index("server {")]
SERVER = CONF[CONF.index("server {"):]
CHECK = os.path.join(REPO, "tests", "scripts", "web_routes_check.py")

# (the clean address, the file, the old address)
KEPT_FILES = ("/changelog", "/legal", "/matches")        # (their files answer 200 at the old address: before this change these three clean addresses redirected to the files, and a browser may have kept that)
PAGES = [("/play", "play.html"), ("/changelog", "changelog.html"), ("/legal", "legal.html"), ("/matches", "matches.html"), ("/match", "match.html"), ("/mapmaker", "mapmaker.html")]
# every path that the file hands to the game server, the chat hub or the admin page, and one under each prefix: none of them may be a page
OTHER_PATHS = ["/ws", "/busy", "/stats", "/stats/local", "/replays", "/replays/x.antsrep", "/live", "/live/R-20260101-000000Z", "/lobbies", "/history", "/history/deaths", "/history/x", "/maps", "/maps/all", "/maps/files/A.LVL",
               "/maps/previews/a.webp", "/submissions", "/submissions/abc", "/test-maps", "/admin", "/admin/", "/admin/api/me", "/admin/api/", "/hub/ws", "/hub/login", "/hub/callback", "/hub/me", "/hub/logout", "/hub/forget",
               "/sprites/a.png", "/sounds/a.wav", "/asset_catalog/", "/asset_catalog/index.html", "/mapmaker_atlas/atlas.json", "/favicon.ico", "/favicon.png", "/catalog", "/viewer", "/front/classic.css", "/home/home.js",
               "/index.wasm", "/index.data", "/index.js", "/lvl_core.wasm"]


def location_headers():
    """[(modifier, path or pattern, body)] of every location of the server block, in the order of the file."""
    out = []
    for words in re.findall(r"^\s*location\s+(.*?)\s*\{", SERVER, re.M):
        if words.startswith("@"):
            continue
        m = re.match(r"(=|\^~|~\*|~)?\s*(.+)$", words)
        out.append((m.group(1) or "", m.group(2).strip(), stats.block_of(SERVER, "location %s " % words)))
    return out


def chosen(path):
    """Which location nginx uses for a path: an exact one; else the longest prefix, and when that prefix is ^~ it stops there; else the first regular expression that matches; else the longest prefix."""
    locs = location_headers()
    for mod, pattern, _ in locs:
        if mod == "=" and pattern == path:
            return (mod, pattern)
    best = None
    for mod, pattern, _ in locs:
        if mod in ("", "^~") and path.startswith(pattern) and (best is None or len(pattern) > len(best[1])):
            best = (mod, pattern)
    if best and best[0] == "^~":
        return best
    for mod, pattern, _ in locs:
        if mod in ("~", "~*") and re.search(pattern, path, re.I if mod == "~*" else 0):
            return (mod, pattern)
    return best


class TheText(unittest.TestCase):
    def test_every_page_has_its_exact_location_that_serves_its_file_and_nothing_else(self):
        for clean, name in PAGES:
            blocks = re.findall(r"location = %s \{([^{}]*)\}" % re.escape(clean), CONF)
            self.assertEqual(len(blocks), 1, clean)
            self.assertEqual(blocks[0].split(), ["rewrite", "^", "/%s" % name, "last;"], clean)       # (no header of its own: a location with an add_header drops the server's, and the file's address says the page's)
        for clean, _ in PAGES:
            self.assertEqual(len([m for m in location_headers() if m[1] == clean]), 1, clean)

    def test_a_clean_address_is_served_by_its_own_location_and_by_no_other(self):
        for clean, _ in PAGES:
            self.assertEqual(chosen(clean), ("=", clean), clean)
            self.assertEqual(chosen(clean + "/")[1] in ("/",), True, clean + "/")                      # (a slash after a page is the catch-all's, which the redirect of the map has already answered)

    def test_no_page_name_catches_a_path_of_the_game_server_the_hub_or_the_admin_page(self):
        names = {clean for clean, _ in PAGES}
        for path in OTHER_PATHS:
            self.assertNotIn(path, names, path)
            mod, pattern = chosen(path)
            self.assertFalse(mod == "=" and pattern in names, path)
            self.assertNotIn(pattern, names, path)
        # the paths that the locations of the doors name are still there, as they were
        locations = {(m, p) for m, p, _ in location_headers()}
        for door in (("=", "/live"), ("^~", "/live/"), ("=", "/replays"), ("^~", "/replays/"), ("=", "/history"), ("^~", "/history/"), ("=", "/lobbies"), ("=", "/maps"), ("=", "/submissions"), ("=", "/admin"), ("^~", "/admin/api/")):
            self.assertIn(door, locations, door)

    def test_the_old_addresses_of_the_page_of_the_matches_that_run_now_are_locations_that_redirect_to_the_front_page_and_are_not_in_the_map(self):
        # (the server block's `if` runs before any location and keeps the query: an address in the map could not drop it. The old page took no query, and the front page acts on some, so none is passed on.)
        self.assertNotIn("/live-now", [c for c, _ in PAGES])
        for old in ("/live-now", "/live-now/", "/live.html"):
            self.assertEqual(stats.block_of(CONF, "location = %s " % old).split(), ["return", "302", "/;"], old)
        body = stats.block_of(HEAD, "map $ants_path $ants_clean_address ")
        self.assertNotIn("live", body)
        self.assertIn("proxy_pass http://$ants_replays;", stats.block_of(CONF, "location = /live "))   # (/live is still the server's list)

    def test_the_old_addresses_are_one_map_of_exact_paths_that_the_servers_if_turns_into_a_redirect_with_the_query(self):
        body = stats.block_of(HEAD, "map $ants_path $ants_clean_address ")
        rules = dict(re.findall(r"^\s*(/\S*)\s+(/\S*);", body, re.M))
        want = {"/%s" % name: clean for clean, name in PAGES if clean not in KEPT_FILES}
        want.update({"/index.html": "/", "/lobby.html": "/"})
        self.assertEqual(rules, want)
        self.assertEqual(re.findall(r"^\s*default\s+(\S+);", body, re.M), ['""'])
        regex = re.findall(r'^\s*"(~[^"]*)"\s+(\S+);', body, re.M)
        self.assertEqual(len(regex), 1)
        alternatives = re.search(r"\(\?<ants_page_name>([^)]*)\)", regex[0][0]).group(1).split("|")
        self.assertEqual(sorted(alternatives), sorted(c[1:] for c, _ in PAGES))                         # (exactly the pages' names: never /live, /live-now, /maps, /history, or any other)
        self.assertTrue(regex[0][0].endswith("/\\z"))
        self.assertEqual(regex[0][1], "/$ants_page_name")
        self.assertEqual(len(re.findall(r"^map \S+ \$ants_clean_address \{", HEAD, re.M)), 1)
        rule = re.findall(r"^\s*if \(\$ants_clean_address\) \{\s*return 302 \$ants_clean_address\$is_args\$args;\s*\}", SERVER, re.M)
        self.assertEqual(len(rule), 1)                                                                  # (the server block's own `if`, once: before any location; the query goes with it)
        self.assertLess(SERVER.index("if ($ants_clean_address)"), SERVER.index("location "))
        self.assertIn("map $request_uri $ants_path {", HEAD)                                           # (the key is the path as it was sent: never the path after a rewrite or a fallback)
        self.assertNotRegex(HEAD[HEAD.index("map $ants_path $ants_clean_address"):HEAD.index("map $ants_path $ants_clean_address") + 40], r"\$uri")

    def test_the_old_lists_of_matches_and_the_play_online_page_redirect_with_their_query(self):
        self.assertRegex(CONF, r"location = /watch\.html \{\s*return 302 /matches\$is_args\$args;\s*\}")
        self.assertRegex(CONF, r"location = /four\.html \{\s*return 301 /\$is_args\$args;\s*\}")
        for gone in ("location = /changelog {\n        return", "location = /legal {\n        return", "location = /matches {\n        return"):
            self.assertNotIn(gone, CONF)

    def test_the_page_headers_still_come_from_the_file_s_address(self):
        # (the clean address is an internal rewrite to the file, so the maps that read $uri see the file: the policy, the framing rule, the referrer policy and the permissions policy of a page)
        self.assertIn('"~^/(?:play|index)\\.html\\z" game;', HEAD)
        self.assertIn('"~\\.html\\z" page;', HEAD)
        self.assertEqual(len(re.findall(r"location = /mapmaker\.html \{", CONF)), 1)
        self.assertIn("location ~* \\.(html|css|js)$ {", CONF)

    def test_server_tokens_off_in_the_http_part_and_in_the_server_block_and_nothing_turns_it_on(self):
        self.assertEqual(len(re.findall(r"^server_tokens off;", HEAD, re.M)), 1)
        self.assertEqual(len(re.findall(r"^    server_tokens off;", SERVER, re.M)), 1)
        self.assertNotRegex(CONF, r"server_tokens\s+(on|build)")
        self.assertEqual(len(re.findall(r"\bserver_tokens\b", CONF)), 2)

    def test_the_other_nginx_configuration_of_the_repository_is_none_the_stack_files_mount_only_lists_of_proxies(self):
        found = []
        for base, dirs, files in os.walk(REPO):
            dirs[:] = [d for d in dirs if d not in (".git", "build", "third_party", "node_modules")]
            for name in files:
                if name.endswith(".conf"):
                    found.append(os.path.relpath(os.path.join(base, name), REPO))
        self.assertEqual(sorted(found), ["docker/nginx.conf", "docker/trusted-proxies.conf"])
        self.assertNotIn("server {", stats.read("docker", "trusted-proxies.conf"))

    def test_no_page_script_or_style_builds_an_address_with_dot_html_for_a_page_that_has_a_clean_one(self):
        pat = re.compile(r"""["'`(=]/?(?:play|matches|match|live|legal|changelog|mapmaker|lobby|index)\.html\b""")
        bad = []
        roots = [os.path.join(REPO, "web"), os.path.join(REPO, "src", "ants_app")]
        for root in roots:
            for base, _dirs, files in os.walk(root):
                for name in files:
                    if not name.endswith((".js", ".html", ".css", ".cpp", ".hpp")):
                        continue
                    path = os.path.join(base, name)
                    with open(path, encoding="utf-8") as f:
                        text = f.read()
                    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)       # (a comment names a file: web/lobby.html; a quoted address is what the page makes)
                    for number, line in enumerate(text.splitlines(), 1):
                        code = re.sub(r"(?<!:)//.*$", "", line)
                        if pat.search(code) and not re.search(r"web/(?:play|matches|match|legal|changelog|mapmaker|lobby|index|shell)\.html", code):
                            bad.append("%s:%d: %s" % (os.path.relpath(path, REPO), number, line.strip()[:120]))
        self.assertEqual(bad, [])

    def test_the_watch_the_share_the_room_and_the_game_links_are_built_in_the_clean_form(self):
        def read(*parts):
            return stats.read(*parts)
        self.assertIn("'/play?live=' + encodeURIComponent(e.id)", read("web", "home", "games.js"))                   # (the Watch button of the front page's list)
        shell = read("web", "shell.html")
        self.assertIn("window.location.origin + (FILE ? '/play?replay=' + encodeURIComponent(FILE) : '/play?live=' + encodeURIComponent(LIVE_ID || ''))", shell)      # (the share link of a replay and of a live match)
        self.assertIn("'/play?replay=' + encodeURIComponent(file) + '&embed=1'", read("web", "match.html"))           # (the replay in its frame)
        self.assertIn("'/play?local=' + r.id", read("web", "replay_page.js"))
        lobby = read("web", "lobby.html")
        self.assertIn("var LOCAL_PAGE = 'play';", lobby)                                                          # (START for a game on this computer)
        self.assertIn("function roomLink() { return window.location.origin + window.location.pathname.replace(/^\\/+/, '/') + '?room='", lobby)      # (the room link: the address of the front page, which is /)
        self.assertIn("'/match?id=' + encodeURIComponent(rec.id)", read("web", "matches.html"))
        self.assertIn("return '/matches' + (parts.length", read("web", "history_page.js"))
        self.assertIn("'/?testroom=' + rec.id", read("web", "front", "mapmaker_play.js"))
        self.assertIn("'/play?testmap=' + rec.id", read("web", "front", "mapmaker_play.js"))
        self.assertIn("window.location.assign('/matches')", read("src", "ants_app", "application_replay.cpp"))

    def test_the_old_addresses_that_a_page_may_still_name_are_files_of_the_image_and_the_image_copies_every_page(self):
        dockerfile = stats.read("Dockerfile")
        for _, name in PAGES:
            self.assertIn(name, dockerfile, name)
        self.assertIn("lobby.html", dockerfile)


# ------------------------------------------------------------------------------------------------------------------------------------------------------------ the run
def fill(text):
    return text.replace("@@GAME_VERSION@@", "0.0.0").replace("@@BUILD_ID@@", "test").replace("@@BUILD_DATE@@", "today").replace("@@BUILD_TIMESTAMP@@", "1").replace("@@SITE_TITLE@@", "").replace("@@SITE_FOOTER@@", "")


class TheRealPagesRun(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not H.nginx_binary():
            raise unittest.SkipTest("no nginx program on this machine: the file was NOT run (tests/scripts/test_nginx_clean_urls.py)")
        cls.tmp = tempfile.mkdtemp(prefix="ants_clean.")
        cls.proc = None
        try:
            docroot = os.path.join(cls.tmp, "html")
            shutil.copytree(os.path.join(REPO, "web"), docroot, ignore=shutil.ignore_patterns("*.map"))
            for name in ("lobby.html", "matches.html", "match.html", "mapmaker.html", "shell.html"):
                path = os.path.join(docroot, name)
                with open(path, encoding="utf-8") as f:
                    text = fill(f.read())
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
            shutil.copyfile(os.path.join(docroot, "shell.html"), os.path.join(docroot, "index.html"))      # (the image's build makes index.html from the shell; the page of the game is a copy of it)
            shutil.copyfile(os.path.join(docroot, "index.html"), os.path.join(docroot, "play.html"))
            classic = '<!DOCTYPE html><html><head><title>a stand-in page</title><link rel="stylesheet" href="/front/classic.css"></head><body><p>a stand-in page of the site</p></body></html>\n'
            with open(os.path.join(docroot, "changelog.html"), "w", encoding="utf-8") as f:
                f.write(classic)
            os.makedirs(os.path.join(docroot, "asset_catalog"))
            with open(os.path.join(docroot, "asset_catalog", "index.html"), "w", encoding="utf-8") as f:
                f.write(classic)
            os.makedirs(os.path.join(docroot, "mapmaker_atlas"))
            for name, data in (("atlas.json", b'{"v":1}'), ("ground.png", b"\x89PNG\r\n\x1a\n a stand-in"), ("objects.png", b"\x89PNG\r\n\x1a\n a stand-in")):
                with open(os.path.join(docroot, "mapmaker_atlas", name), "wb") as f:
                    f.write(data)
            folder = os.path.join(cls.tmp, "run")
            for sub in ("tmp", "cache_stats", "cache_history", "cache_hub_pics"):
                os.makedirs(os.path.join(folder, sub))
            maps = os.path.join(cls.tmp, "maps")
            os.makedirs(os.path.join(maps, "previews"))
            os.makedirs(os.path.join(maps, "files"))
            cls.port = H.free_port()
            dead = H.free_port()
            text = RAW.replace("    listen 80;", "    listen 127.0.0.1:%d;" % cls.port).replace("    listen [::]:80;", "")
            text = text.replace("/usr/share/nginx/html", docroot).replace("/srv/community-maps", maps)
            text = text.replace("/var/cache/nginx/ants_stats", folder + "/cache_stats").replace("/var/cache/nginx/ants_history", folder + "/cache_history").replace("/var/cache/nginx/ants_hub_pics", folder + "/cache_hub_pics")
            text = re.sub(r"set \$(\w+) [a-z-]+:\d+;", lambda m: "set $%s 127.0.0.1:%d;" % (m.group(1), dead), text)
            text = text.replace("access_log /dev/stdout ants_noquery;", "access_log %s/access.log ants_noquery;" % folder).replace("error_log /dev/stderr crit;", "error_log %s/loc.log crit;" % folder)
            with open(os.path.join(folder, "site.conf"), "w", encoding="utf-8") as f:
                f.write(text)
            with open(os.path.join(folder, "nginx.conf"), "w", encoding="utf-8") as f:
                f.write("daemon off;\nmaster_process off;\npid %(t)s/nginx.pid;\nerror_log %(t)s/error.log;\nevents { worker_connections 256; }\nhttp {\n  %(mime)s\n  default_type application/octet-stream;\n"
                        "  sendfile on;\n  client_body_temp_path %(t)s/tmp/b;\n  proxy_temp_path %(t)s/tmp/p;\n  fastcgi_temp_path %(t)s/tmp/f;\n  uwsgi_temp_path %(t)s/tmp/u;\n  scgi_temp_path %(t)s/tmp/s;\n  include %(t)s/site.conf;\n}\n"
                        % {"t": folder, "mime": H.mime_line()})
            cls.proc = subprocess.Popen([H.nginx_binary(), "-c", os.path.join(folder, "nginx.conf"), "-p", folder], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            for _ in range(100):
                try:
                    socket.create_connection(("127.0.0.1", cls.port), timeout=0.5).close()
                    return
                except OSError:
                    if cls.proc.poll() is not None:
                        raise AssertionError("nginx would not start: " + cls.proc.stderr.read().decode(errors="replace")[:600])
                    time.sleep(0.05)
            raise AssertionError("nginx did not come up")
        except BaseException:
            cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, "proc", None):
            cls.proc.send_signal(signal.SIGTERM)
            try:
                cls.proc.wait(5)
            except subprocess.TimeoutExpired:
                cls.proc.kill()
            if cls.proc.stderr:
                cls.proc.stderr.close()
        shutil.rmtree(getattr(cls, "tmp", ""), ignore_errors=True)

    def ask(self, path, method="GET", headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            conn.request(method, path, headers=headers or {})
            r = conn.getresponse()
            return r.status, {k.lower(): v for k, v in r.getheaders()}, r.read().decode("utf-8", "replace")
        finally:
            conn.close()

    def test_every_route_of_the_site_with_the_real_pages(self):
        done = subprocess.run([sys.executable, CHECK, "--web", "http://127.0.0.1:%d/" % self.port], capture_output=True, text=True, timeout=300)
        self.assertEqual(done.returncode, 0, done.stdout[-3000:] + done.stderr[-1000:])
        self.assertIn(" 0 failed", done.stdout)

    def test_the_headers_of_a_page_are_the_same_at_the_clean_address_and_at_the_file_it_comes_from(self):
        names = ("content-security-policy", "x-frame-options", "referrer-policy", "permissions-policy", "cross-origin-opener-policy", "cross-origin-embedder-policy", "x-content-type-options", "strict-transport-security", "cache-control")
        for clean, name in PAGES:
            status, headers, _ = self.ask(clean)
            self.assertEqual(status, 200, clean)
            for header in names:
                self.assertIn(header, headers, "%s %s" % (clean, header))
        # the game page: the page's own policy (eval and WebAssembly), may be framed by this site
        game = self.ask("/play")[1]
        self.assertIn("'unsafe-eval'", game["content-security-policy"])
        self.assertEqual(game["x-frame-options"], "SAMEORIGIN")
        self.assertIn("frame-ancestors 'self'", game["content-security-policy"])
        for clean in ("/matches", "/match", "/legal", "/changelog"):
            page = self.ask(clean)[1]
            self.assertEqual(page["x-frame-options"], "DENY", clean)
            self.assertIn("frame-ancestors 'none'", page["content-security-policy"], clean)
            self.assertNotIn("'unsafe-eval'", page["content-security-policy"], clean)
        maker = self.ask("/mapmaker")[1]
        self.assertNotIn("'unsafe-inline'", re.search(r"script-src ([^;]*)", maker["content-security-policy"]).group(1))
        self.assertEqual(maker["referrer-policy"], "no-referrer")
        # the old address is a redirect with the site's cross-origin headers, nothing of a page
        old = self.ask("/play.html?live=abc")
        self.assertEqual((old[0], old[1]["location"]), (302, "/play?live=abc"))
        self.assertEqual(old[1]["cross-origin-opener-policy"], "same-origin")
        self.assertNotIn("content-security-policy", old[1])
        self.assertNotIn("cache-control", old[1])                                                      # (a 302 with no freshness: a browser asks again, so a later change back needs nothing from a visitor)
        for clean, name in PAGES:                                                                      # (every other redirect of a page is a 302 too, and /watch.html)
            if clean not in KEPT_FILES:
                self.assertEqual(self.ask("/" + name + "?x=1")[0], 302, name)
            self.assertEqual(self.ask(clean + "/")[0], 302, clean + "/")
        self.assertEqual(self.ask("/watch.html")[0], 302)

    def test_the_three_files_that_an_earlier_redirect_led_to_answer_200_at_their_old_address_and_the_clean_address_answers_200_too(self):
        for clean, name in PAGES:
            if clean in KEPT_FILES:
                file_answer, clean_answer = self.ask("/" + name + "?x=1"), self.ask(clean + "?x=1")
                self.assertEqual((file_answer[0], clean_answer[0]), (200, 200), clean)
                self.assertNotIn("location", file_answer[1], clean)
                self.assertNotIn("location", clean_answer[1], clean)
                self.assertEqual(file_answer[1]["content-security-policy"], clean_answer[1]["content-security-policy"], clean)

    def follow(self, path):
        """What a browser ends at when it opens an address: (the final address, the status, the body), following up to five redirects."""
        for _ in range(5):
            status, headers, body = self.ask(path)
            if status in (301, 302, 307, 308):
                path = headers["location"]
                continue
            return path, status, body
        self.fail("too many redirects at " + path)

    def test_links_that_are_already_out_there_open_the_same_thing_at_the_clean_address(self):
        game, lobby = re.compile(r'id="?game-stage"?'), re.compile(r'id="?lobby"?[ >]')
        live = "ants-TREASURE-20261009-082104Z"
        links = [("the Watch button of an earlier page", "/play.html?live=" + live, "/play?live=" + live, game),
                 ("a shared replay", "/play.html?replay=%s.antsrep" % live, "/play?replay=%s.antsrep" % live, game),
                 ("the frame of the match page", "/play.html?replay=%s.antsrep&embed=1" % live, "/play?replay=%s.antsrep&embed=1" % live, game),
                 ("a game on this computer", "/play.html?map=treasure&bots=medium&name=Bob&aspect=16:9", "/play?map=treasure&bots=medium&name=Bob&aspect=16:9", game),
                 ("a test game from the map maker", "/play.html?testmap=abcdefghijkl&seats=ymmn", "/play?testmap=abcdefghijkl&seats=ymmn", game),
                 ("a shared room link", "/?room=k7m2xq", "/?room=k7m2xq", lobby),
                 ("a shared room link of the front page's file", "/lobby.html?room=k7m2xq", "/?room=k7m2xq", lobby),
                 ("a test room from the map maker", "/lobby.html?testroom=abcdefghijkl", "/?testroom=abcdefghijkl", lobby),
                 ("a link with a code", "/lobby.html?code=ab12cd&room=k7m2xq", "/?code=ab12cd&room=k7m2xq", lobby),
                 ("a rejoin or START link", "/?join=/ws&room=k7m2xq&seat=1&name=Bob&aspect=16:9", "/?join=/ws&room=k7m2xq&seat=1&name=Bob&aspect=16:9", game),
                 ("a rejoin link through the game page's file", "/index.html?join=/ws&room=k7m2xq&seat=1", "/?join=/ws&room=k7m2xq&seat=1", game),
                 ("the list of matches with a search, by the file (it is not redirected: an earlier /matches was a redirect to it)", "/matches.html?q=ann&page=2", "/matches.html?q=ann&page=2", None),
                 ("a match", "/match.html?id=" + live, "/match?id=" + live, None),
                 ("the old list of recordings", "/watch.html", "/matches", None),
                 ("the old page of the matches that run now, by its file", "/live.html", "/", lobby),
                 ("the old page of the matches that run now, by its address", "/live-now", "/", lobby)]
        for what, old, final, marker in links:
            path, status, body = self.follow(old)
            self.assertEqual((path, status), (final, 200), what)
            if marker is not None:
                self.assertTrue(marker.search(body), "%s opens the wrong page" % what)
                self.assertFalse((lobby if marker is game else game).search(body), "%s opens the wrong page" % what)

    def test_the_old_addresses_of_the_matches_that_run_now_are_a_temporary_redirect_to_the_front_page_with_no_query(self):
        for old in ("/live-now", "/live-now/", "/live.html", "/live-now?x=1", "/live.html?join=/ws&room=k7m2xq", "/live-now/?room=k7m2xq"):
            status, headers, _ = self.ask(old)
            self.assertEqual((status, headers.get("location")), (302, "/"), old)                       # (a 302: a browser does not keep it; "/", whatever the query was)
            self.assertNotIn("content-security-policy", headers, old)
        for old in ("/live-now", "/live.html"):
            self.assertEqual(self.ask(old, "HEAD")[0], 302, old)

    def test_only_get_and_head_are_served_as_before(self):
        for clean, _ in PAGES:
            self.assertEqual(self.ask(clean, "HEAD")[0], 200, clean)
            self.assertEqual(self.ask(clean, "POST")[0], 405, clean)
            self.assertEqual(self.ask(clean, "PUT")[0], 405, clean)
            self.assertEqual(self.ask(clean, "DELETE")[0], 405, clean)

    def test_the_server_names_no_version_in_its_header_or_on_an_error_page(self):
        for path in ("/", "/play", "/nothing.html", "/play.html", "/stats?x=1", "/live?x=1", "/maps/", "/admin.html", "/front/classic.css"):
            status, headers, body = self.ask(path)
            self.assertNotRegex(headers.get("server", ""), r"\d", path)
            self.assertNotRegex(body, r"nginx/\d", path)
        status, headers, body = self.ask("/nothing.html")
        self.assertEqual(status, 404)
        self.assertIn("nginx", body)                                                                   # (an error page that names the program and no number)
        self.assertNotRegex(body, r"\d+\.\d+\.\d+")
        raw = socket.create_connection(("127.0.0.1", self.port), timeout=5)
        try:
            raw.sendall(b"GET /%ff%zz HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")           # (a request that nginx itself refuses: its own error page, 400)
            got = b""
            while True:
                chunk = raw.recv(4096)
                if not chunk:
                    break
                got += chunk
        finally:
            raw.close()
        self.assertIn(b" 400 ", got.split(b"\r\n", 1)[0])
        self.assertNotRegex(got, rb"nginx/\d")
        self.assertNotRegex(got, rb"(?i)server: nginx/")


if __name__ == "__main__":
    unittest.main()
