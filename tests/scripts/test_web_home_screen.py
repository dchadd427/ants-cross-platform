#!/usr/bin/env python3
"""The two screens of the front page, Home and the Room (web/home/, docs/HOME_SCREEN.md; run by ./run_tests.sh --fast and by the CI).

The front page (web/lobby.html) is one page with one WebSocket: <body data-screen="home|room"> says whether Home (a top bar over a stand-in picture of the Yard) or the Room (today's room card under a slim
bar) shows, and going from one to the other is a class and one render of the page, never a request. Everything new is in web/home/ (home.js, home.css), whose size has a cap of its own because web/front/ is
at its cap and web/lobby.html at its line limit.

  - the page: the strips (the notices and the Rejoin button) are above both screens, Home's mount is before the Room, the stylesheet and the script are linked, the page opens on Home, a plain visit makes no
    room, the page calls Home from render(), from the hand-off to the game and from the dialog's inert, and it is still under its line limit;
  - the folder: exactly home.js and home.css, under their budgets (raw and gzipped), nothing of it in web/front/;
  - the style: which screen shows what, every target at least 44 px, nothing loaded from another site, the pictures are data;
  - the way into the image: the Dockerfile, the local web build and the CI's list of the image's files carry the folder;
  - the code RUNS in the fake browser of tests/scripts/web_page_harness.js (node, when it is installed: tests/scripts/web_home_screen_check.js: a plain visit, My room, Home, Back and Forward, a link, a reload,
    Leave room, START while a guest sits on Home, the strips, the Menu);
  - tests/data/home_screen_mutants.json changes one thing at a time in a COPY of the files (the room left on Home, the hand-off ignored while Home is up, Back, a reload on Home, ...): the checks must then
    fail (the last class of this file; tools/mutate.py runs the same list).
The pictures that were approved are compared by hand with the page in a browser (docs/HOME_SCREEN.md); the real server and the hand-off to a real game are tests/scripts/web_lobby_server_check.js.
"""
import gzip
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HOME = os.path.join(REPO, "web", "home")
FRONT = os.path.join(REPO, "web", "front")
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_screen_check.js")
NODE = shutil.which("node")
FILES = ("home.js", "home.css")
LATER = ("games.js", "games.css")                    # the files that load after the first paint (tests/scripts/test_web_home_games.py has their budgets)
CHAT_FILES = ("chat.js", "chat.css")                  # the chat dock, loaded after the first paint: its own budgets and tests are tests/scripts/test_web_home_chat.py
HOME_BUDGET, HOME_GZIP_BUDGET = 38 * 1000, 11300     # bytes, raw and gzipped, both files together (about 36,600 and 10,900, with the loaders of the dock and of the Games list, the timers of the hand-off and the narrow logo): web/front/ has no room left, and the Games list and the chat dock load later, with caps of their own
LINES = 1500                                          # tools/check_hygiene.py: the page's line limit


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_function(page, name):
    """the text of function `name` of the page's script, up to its closing brace at the same indent"""
    at = page.index("        function %s(" % name)
    end = page.index("\n        }\n", at) + len("\n        }\n")
    return page[at:end]


class ThePage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read("web", "lobby.html")

    def test_the_page_opens_on_home_and_has_the_strips_above_both_screens(self):
        page = self.page
        self.assertIn('<body data-screen="home">', page)
        strips = page.index('<div class="wrap strips">')
        home = page.index('<div id="home"></div>')
        room = page.index('<main class="wrap">')
        banner, rejoin = page.index('id="banner"'), page.index('id="rejoin"')
        self.assertLess(page.index('<div class="screen" id="screen">'), strips)
        self.assertTrue(strips < banner < rejoin < home < room, "the notices and the Rejoin button, then Home's mount, then the Room")
        self.assertEqual(page[rejoin:home].count("</div>"), 2, "the strips' own box closes before Home")
        self.assertEqual(page.count('id="home"'), 1)

    def test_the_script_and_the_style_are_linked_once_and_the_script_comes_before_the_page_s_own(self):
        page = self.page
        self.assertEqual(page.count('<link rel="stylesheet" href="home/home.css">'), 1)
        self.assertEqual(page.count('<script src="home/home.js"></script>'), 1)
        self.assertLess(page.index('<script src="home/home.js"></script>'), page.index("<script>\n"), "the page's own script finds AntsHome")
        self.assertLess(page.index('<link rel="stylesheet" href="front/game_type.css">'), page.index('<link rel="stylesheet" href="home/home.css">'), "Home's style comes after the page's others")
        self.assertIn("|| !window.AntsHome) {", page, "a page whose Home script did not come says that it could not load all of its parts, as for its other scripts")

    def test_a_plain_visit_makes_no_room(self):
        run = run_function(self.page, "runLobby")
        self.assertNotRegex(run, r"else \{\s*connect\(\{ code: newCode\(\)", "the old last branch (no seat, no link: a room of its own) is gone: My room (the page's make()) makes it")
        self.assertEqual(run.count("newCode()"), 1, "the only room that runLobby makes is the one of a test room that is ready")
        self.assertIn("Home.start(test || wantedCode, stored);", run)
        self.assertIn("connect({ code: stored.code, key: stored.key, name: stored.name || myName(), own: stored.own });", run, "a tab that holds a seat takes it back, also on Home")
        self.assertIn("make: function () { connect({ code: newCode(), name: myName(), own: true }); }", self.page, "My room makes the room, a private one with a new code")

    def test_the_page_calls_home_where_the_room_changes_the_hand_off_starts_and_a_dialog_opens(self):
        page = self.page
        self.assertIn("            Home.sync();\n        }\n        function paintCard", page, "render() ends with Home's look at the room (the count of My room)")
        self.assertIn("handing = true; Home.opening();", page, "the hand-off to the game says it on Home, in the same breath as it starts")
        self.assertIn("Home.inert(on); }", page, "the dialog of the map list makes Home's bars inert too")
        self.assertIn("window.history.replaceState(window.history.state, '', window.location.pathname.replace(/^\\/+/, '/') + (Home.screen() === 'room' ? '#room' : ''))", page, "stripping the room's code from the address keeps the screen's entry and names the screen that is up")
        self.assertEqual(page.count("AntsHome.mount("), 1)

    def test_the_hand_off_does_not_look_at_the_screen(self):
        starting = run_function(self.page, "onStarting")
        for word in ("data-screen", "Home.screen", "screen()"):
            self.assertNotIn(word, starting, "the hand-off to the game is the same on Home and on the Room (START must reach a guest on Home)")
        self.assertIn("if (handing || !client) return;", starting)

    def test_leaving_the_room_forgets_it_and_the_page_asks_for_nothing_else_of_home(self):
        mount = self.page[self.page.index("AntsHome.mount("):]
        mount = mount[:mount.index("// ---- go ----")]
        self.assertIn("leave: function () { if (handing) return; var old = client;", mount, "a page that is on its way to the game gives its seat up to nobody")
        self.assertIn("if (old) old.leave(); forgetSession(); leaveBack(); hideBanner(); render();", mount)
        self.assertEqual(sorted(set(re.findall(r"\b(info|make|leave|how|stalled):", mount))), ["how", "info", "leave", "make", "stalled"])
        self.assertIn("client: function () { return client; }, join: probe, typed: joinTyped, name: myName,", mount, "what the Games list reads of the page (docs/HOME_SCREEN.md): its client, the look at a room, a typed code and the name")

    def test_the_words_that_said_that_a_page_makes_a_room_when_it_opens_are_corrected(self):
        page = self.page
        self.assertNotIn("Open the page and your room is ready", page)
        self.assertNotIn("where a new room is ready", page)
        self.assertNotIn("Back finds a room again", page)
        self.assertNotIn("A plain visit makes a room", page)
        self.assertNotIn("ready when the page opens", read("docker", "nginx.conf"))
        self.assertNotIn("ready when the page opens", read("docs", "PLAY_IN_BROWSER.md"))

    def test_how_it_works_says_where_my_room_is_because_the_room_shows_it_too(self):
        self.assertIn("<h3>The room</h3><p>From Home, press My room and your room is ready, with the first color yours.", self.page)
        self.assertNotIn("<p>Press My room and your room", self.page, "the list opens from the Room too, where there is no My room to press")
        check = read("tests", "scripts", "web_home_check.py")
        self.assertNotIn("the front page makes a room again", check, "a plain visit of the front page makes no room: My room does")
        self.assertIn("the front page (Home), after My room, makes a room again", check)

    def test_the_coffee_picture_of_the_footer_that_is_not_shown_on_home_is_lazy(self):
        self.assertRegex(self.page, r'<img src="https://cdn\.buymeacoffee\.com/[^"]*" alt="Buy Me a Coffee" width="217" height="61" crossorigin="anonymous" loading="lazy">')

    def test_the_page_is_under_its_line_limit(self):
        self.assertLessEqual(len(self.page.split("\n")) - 1, LINES, "tools/check_hygiene.py: the page's line limit")


class TheFolder(unittest.TestCase):
    def test_it_holds_the_two_screens_the_games_list_and_the_chat_dock_and_nothing_else(self):
        self.assertEqual(sorted(n for n in os.listdir(HOME) if not n.endswith(".mutbak")), sorted(FILES + LATER + CHAT_FILES))      # (tools/mutate.py keeps FILE.mutbak while it changes a file)

    def test_it_is_under_its_budgets(self):
        raw = zipped = 0
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            raw += len(data)
            zipped += len(gzip.compress(data, 9))
        self.assertLess(raw, HOME_BUDGET, "home.js and home.css are %d bytes" % raw)
        self.assertLess(zipped, HOME_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_a_hand_off_that_does_not_come_is_given_up_after_15_seconds(self):
        js = read("web", "home", "home.js")
        self.assertIn("var OPENING_MS = 15000, GIVE_UP_MS = 60000;", js)
        self.assertIn("goingTimer = null; going = false; stalled = true; opening.hidden = true; leave.disabled = true; leave.setAttribute('aria-disabled', 'true'); inert(held);", js, "the card goes, Home is free, Leave room stays off (the seat may still be the game's), and what a dialog holds stays held")
        self.assertIn("try { if (page.stalled) page.stalled(words); } catch (e) { /* what the page says is its own */ }", js, "the page says that the game is slow to open, and a minute after START that it did not")
        self.assertIn("failTimer = setTimeout(function () { failTimer = null; tell(TEXT.failed); }, GIVE_UP_MS - OPENING_MS);", js, "the wording of a failure comes a minute after START, not at 15 s")
        self.assertIn("if (failTimer !== null) clearTimeout(failTimer);", js, "a second hand-off starts both times again")
        self.assertIn("tell(TEXT.slow);", js)
        self.assertIn("slow: 'The game is slow to open. Reload this page to get your seat back.', failed: 'The game did not open. Reload this page to get your seat back.',", js)
        self.assertIn("function () { if (stalled) return; page.leave(); show('home', 'push'); }", js, "a Leave room that reaches the page all the same does nothing")
        self.assertIn("if (goingTimer !== null) clearTimeout(goingTimer);", js, "a second hand-off starts the time again")

    def test_nothing_of_it_is_in_the_folder_that_has_its_cap_already(self):
        for name in FILES + LATER + CHAT_FILES + ("home",):
            self.assertNotIn(name, os.listdir(FRONT))

    def test_the_script_is_plain_es5_and_names_no_other_site(self):
        js = read("web", "home", "home.js")
        code = re.sub(r"//.*", "", js)
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\b"):
            self.assertIsNone(re.search(token, code), token)
        self.assertEqual(sorted(set(re.findall(r"https?://[^'\" )]+", js))), ["https://discord.gg/WaWrXQnKPj", "https://github.com/dchadd427/ants-cross-platform", "https://github.com/dchadd427/ants-cross-platform/issues"], "the Menu's links that leave the site, and no other address")
        self.assertIn("'use strict';", js)


class TheStyle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.css = read("web", "home", "home.css")
        cls.rules = re.findall(r"([^{}]+)\{([^{}]*)\}", re.sub(r"/\*.*?\*/", "", cls.css, flags=re.S))

    def test_each_screen_shows_its_own_things(self):
        css = self.css
        self.assertIn('body[data-screen="home"] main.wrap, body[data-screen="home"] .bar { display: none; }', css)
        self.assertIn('body[data-screen="room"] #home { display: none; }', css)
        self.assertRegex(css, r"\.mast \{ display: none; \}")
        self.assertRegex(css, r'body\[data-screen="home"\] \{ display: flex; flex-direction: column; height: 100vh; height: 100dvh; overflow: hidden; \}')
        self.assertRegex(css, r'body\[data-screen="home"\] \.screen \{ flex: 1 1 auto; min-height: 0; overflow: hidden; \}')

    def test_the_rooms_bar_gives_the_logo_what_is_left_on_a_narrow_phone(self):
        match = re.search(r"@media \(max-width: 389px\) \{(.*?)\n\}", self.css, re.S)
        self.assertIsNotNone(match, "a rule for 360 and 320 px (the bar does not fit at its size for a wider phone)")
        self.assertIn(".rbar .hbrand { flex: 1 1 0; min-width: 40px; max-width: 76px; } .rbar .hlogo { width: 100%; }", match.group(1))

    def test_the_rooms_bar_leaves_the_logo_picture_out_below_340_px_and_the_h1_keeps_its_name(self):
        match = re.search(r"@media \(max-width: 339px\) \{ \.rbar \.hbrand \{([^}]*)\} \}", self.css)
        self.assertIsNotNone(match, "a rule for 320 px (the logo is 40 px wide at 360 and 320: too small to read)")
        body = match.group(1)
        self.assertIn("position: absolute;", body)
        self.assertIn("clip: rect(0 0 0 0);", body)
        self.assertNotRegex(body, r"display: *none|visibility: *hidden", "the heading and its name (the picture's alt) stay for a screen reader")
        self.assertLess(self.css.index("max-width: 389px"), self.css.index("max-width: 339px"), "the narrower rule comes after the wider one")

    def test_the_menu_scrolls_inside_itself_when_the_window_is_short(self):
        box = next(body for selector, body in self.rules if selector.strip() == ".hmenubox")
        self.assertIn("overflow-y: auto;", box)
        self.assertIn("overscroll-behavior: contain;", box)
        self.assertRegex(box, r"max-height: calc\(100vh - \d+px\); max-height: calc\(100dvh - \d+px\);")
        self.assertIn("position: fixed;", box)

    def test_every_target_is_at_least_44_px_high_and_wide(self):
        heights = {}
        for selector, body in self.rules:
            for m in re.finditer(r"min-height: (\d+)px", body):
                heights.setdefault(selector.strip(), []).append(int(m.group(1)))
        for selector in (".hbar .btn", ".hbar .quick", ".rbar .btn", ".hmenubox a, .hmenubox button.mitem", ".hmenubox .mlink"):
            self.assertIn(selector, heights, selector)
        for selector, values in heights.items():
            for value in values:
                self.assertGreaterEqual(value, 44, "%s has a min-height of %d" % (selector, value))
        for selector in (r"\.hmenu, \.hlogin", r"\.hmenubox \.mlink"):
            widths = re.search(selector + r" \{[^}]*min-width: (\d+)px", self.css)
            self.assertGreaterEqual(int(widths.group(1)), 44, selector)
        self.assertNotRegex(self.css, r"(?<!min-)height: [1-3]\d px; *}", "no fixed height under 40 px on a control")

    def test_nothing_is_loaded_from_another_site_and_the_pictures_are_data(self):
        self.assertNotRegex(self.css, r"https?://|@import")
        for target in re.findall(r"url\(\s*[\"']?([^\"')]+)", self.css):
            self.assertTrue(target.startswith("data:image/svg+xml,") or target.startswith("front/"), target)
        self.assertNotRegex(self.css, r"url\(\s*[\"']?front/(?!clay)")

    def test_the_ants_of_the_stand_in_hold_still_for_a_person_who_asks_for_it(self):
        self.assertIn("@media (prefers-reduced-motion: reduce) { .yant .ant { animation: none; } }", self.css)

    def test_the_bars_have_a_layout_for_a_phone_upright_and_for_a_short_window(self):
        self.assertIn("@media (max-width: 720px) and (orientation: portrait) {", self.css)
        self.assertIn("@media (max-height: 500px) and (orientation: landscape) {", self.css)
        self.assertNotRegex(self.css, r"overflow-x: *(scroll|auto)", "nothing scrolls sideways")


class TheWayIntoTheImage(unittest.TestCase):
    def test_the_dockerfile_copies_the_folder_to_where_nginx_serves_home(self):
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/\n", read("Dockerfile"))

    def test_the_local_web_build_packs_it_too(self):
        script = read("build_web.sh")
        self.assertIn('mkdir -p "${DIST_DIR}/front" "${DIST_DIR}/home"', script)
        self.assertIn('cp -f "${SCRIPT_DIR}/web/home"/* "${DIST_DIR}/home/"', script)

    def test_the_ci_looks_for_the_script_and_the_style_in_the_image(self):
        self.assertIn(" home/home.js home/home.css ", read(".github", "workflows", "ci.yml"))


@unittest.skipUnless(NODE, "node is not installed: the page's two screens were NOT run (tests/scripts/web_home_screen_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_two_screens_in_the_fake_browser(self):
        done = subprocess.run([NODE, CHECK, LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"home screens: \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_screen_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = ["web/lobby.html", "web/home/home.js", "web/home/home.css", "web/home/chat.js", "web/home/chat.css", "web/home/games.js", "web/home/games.css", "web/shapes.js", "tests/scripts/test_web_home_screen.py", "tests/scripts/web_home_screen_check.js", "tests/scripts/web_page_harness.js",
             "tests/scripts/web_home_check.py", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml", "docker/nginx.conf", "docs/PLAY_IN_BROWSER.md"] + ["web/front/" + n for n in ("lobby_rules.js", "lobby_net.js", "lobby_chat.js", "rematch.js", "game_modes.js", "game_type.js", "map_picker.js", "map_maker_link.js")]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_screen_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=300)
        return done.returncode, done.stdout + done.stderr

    def test_the_copy_with_nothing_changed_passes(self):
        status, out = self.run_command(self.spec["mutants"][0]["test"])
        self.assertEqual(status, 0, out[-3000:])

    def test_every_mutant_is_seen(self):
        ids = [m["id"] for m in self.spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        missed = []
        for m in self.spec["mutants"]:
            path = os.path.join(self.folder, *m["file"].split("/"))
            with open(path, encoding="utf-8", newline="") as f:
                text = f.read()
            self.assertEqual(text.count(m["old"]), 1, "%s: %r is not in %s exactly once" % (m["id"], m["old"][:60], m["file"]))
            try:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(text.replace(m["old"], m["new"]))
                status, _ = self.run_command(m["test"])
            finally:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(text)
            if status == 0:
                missed.append(m["id"])
        self.assertEqual(missed, [], "the checks did not see these changes")


if __name__ == "__main__":
    unittest.main()
