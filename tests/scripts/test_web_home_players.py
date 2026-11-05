#!/usr/bin/env python3
"""The players list of Home (web/home/players.js and players.css, docs/HOME_SCREEN.md "The players list", docs/HUB.md "Presence"; run by ./run_tests.sh --fast and by the CI).

The list shows who has the front page open right now: a panel at the right of the Yard on a wide window, a button in the bar's first row and a sheet on a phone, a button in the Yard's corner and a drawer in any
other window. It speaks through the chat dock's socket, is loaded by web/home/home.js after the dock, and every word it shows is text from strangers. What this file checks:

  - the page and the other pages: web/lobby.html does not link the list, no other file names it, and the hub's door is still the dock's script alone;
  - the loader in home.js: the two files, once, after the dock is mounted, mounted when BOTH have come, given the parts of the bar that it needs, inert with the bars during the hand-off, and what goes wrong stays;
  - the folder: its own budgets (raw and gzipped), plain ES5, no markup made from text, no link, no storage, no socket of its own, the kinds of element it makes;
  - the style: the breakpoint of the script, the sizes of the approved pictures (260 px, 280 from 1600 x 900), the room that the Games list leaves for it (the arithmetic), nothing of Home's own changes until the
    list is there (every rule that reaches the bar or the Games list starts with the class that the script puts on Home), every target at least 44 px, the sheet clears a phone's system bar, the layers, nothing
    loaded from another site;
  - the way into the image: the CI's list of the image's files carries the two files, the Dockerfile and the local web build copy the folder;
  - the code RUNS in node: tests/scripts/web_home_players_check.js (a fake dock and a fake clock: the pure parts, the phases, what the page tells the hub, what is drawn, Join, the sheet, hostile frames, the page);
  - tests/data/home_players_mutants.json changes one thing at a time in a COPY of the files (a count that is zero, a status that is not checked, a code for a private room, a flood of rows, a second socket, markup,
    the loader, the style's arithmetic, ...): the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
The pictures that were approved are compared by hand with the page in a real browser (tests/scripts/web_home_players_browser.js, opt-in, `--shots DIR`).
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
WEB = os.path.join(REPO, "web")
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_players_check.js")
LOBBY = os.path.join(REPO, "web", "lobby.html")
NODE = shutil.which("node")
FILES = ("players.js", "players.css")
PLAYERS_BUDGET, PLAYERS_GZIP_BUDGET = 42 * 1000, 12600       # bytes, raw and gzipped, both files together (about 39,600 and 11,900: the panel, the sheet and the drawer, the groups and the rows, Join, the guests, the empty and the off notes, the phone's bar button); they load after the first paint, after the chat dock, so they cost the first look nothing


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def code_of(js):
    return re.sub(r"//.*", "", js)


class ThePageAndTheOtherPages(unittest.TestCase):
    def test_the_front_page_does_not_link_the_list_home_loads_it_later(self):
        page = read("web", "lobby.html")
        for word in ("home/players", "AntsHomePlayers", "players.js", "players.css"):
            self.assertNotIn(word, page, word)

    def test_no_other_file_of_the_site_names_the_list_and_the_hubs_door_is_still_the_docks_alone(self):
        naming_the_list, naming_the_door = set(), set()
        for base, _dirs, names in os.walk(WEB):
            for name in names:
                if os.path.splitext(name)[1] not in (".html", ".js", ".css"):
                    continue
                path = os.path.join(base, name)
                rel = os.path.relpath(path, REPO).replace(os.sep, "/")
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = f.read()
                if re.search(r"home/players\b", text):
                    naming_the_list.add(rel)
                if re.search(r"/hub/ws|/hub\b", text):
                    naming_the_door.add(rel)
        self.assertEqual(sorted(naming_the_list), ["web/home/home.js", "web/home/players.css", "web/home/players.js"], "only the front page's own script asks for the list (the two files name themselves)")
        self.assertEqual(sorted(naming_the_door), ["web/home/chat.js"], "the list opens no socket of its own: it speaks through the dock's")

    def test_the_game_server_and_the_game_never_hear_of_it(self):
        for rel in (("web", "shell.html"), ("web", "front", "lobby_net.js"), ("web", "front", "lobby_chat.js"), ("web", "front", "lobby_rules.js")):
            self.assertNotRegex(read(*rel), r"AntsHomePlayers|home/players", rel[-1])


class TheLoaderInHome(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "home.js")

    def test_it_asks_for_the_two_files_by_their_base_name_through_the_part_loader_and_only_after_the_dock(self):
        self.assertIn("lazy('home/players', function () {", self.js)
        self.assertEqual(self.js.count("wantPlayers();"), 1, "one call, where the dock has been mounted")
        self.assertIn("inert(held); dockShown(); wantPlayers(); } } catch (e) { dock = null; } }", self.js)
        self.assertIn("if (askedPlayers || !dock) return;\n            askedPlayers = true;", self.js, "once, and not before there is a dock to speak through")
        self.assertEqual(sorted(set(re.findall(r"home/players\w*", code_of(self.js)))), ["home/players"])

    def test_it_is_given_the_dock_and_the_parts_of_the_bar_that_it_uses_and_nothing_of_the_page_but_the_bridge(self):
        self.assertIn("players = win.AntsHomePlayers.mount({ doc: doc, win: win, host: host, yard: yard, bar: right, quickHost: quickHost, games: gamesBtn, dock: dock, page: page }); inert(held); });", self.js)

    def test_it_is_inert_with_the_bars_and_the_dock_and_a_list_that_comes_late_is_inert_from_the_start(self):
        self.assertIn("[bar, rbar, dock && dock.root, players && players.root].forEach(function (n) {", self.js)
        self.assertIn("inert(held); });", self.js.split("function wantPlayers")[1], "a list that is mounted while a dialog or the hand-off holds the page takes no press either")

    def test_the_part_loader_is_the_one_that_the_games_list_has_and_a_part_that_throws_stays_in_it(self):
        self.assertIn("function one() { if (--parts === 0) { try { onReady(); } catch (e) { /* the part is its own */ } } }", self.js)
        self.assertIn("if (win.AntsHomePlayers) players =", self.js, "a script that did not define the list mounts nothing")


class TheFolderAndTheScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "players.js")
        cls.code = code_of(cls.js)

    def test_it_is_under_its_budgets(self):
        raw = zipped = 0
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            raw += len(data)
            zipped += len(gzip.compress(data, 9))
        self.assertLess(raw, PLAYERS_BUDGET, "players.js and players.css are %d bytes" % raw)
        self.assertLess(zipped, PLAYERS_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_the_script_is_plain_es5(self):
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\b", r"\basync\b", r"\bawait\b"):
            self.assertIsNone(re.search(token, self.code), token)
        self.assertIn("'use strict';", self.js)

    def test_nothing_that_the_hub_says_becomes_markup_a_link_or_code(self):
        self.assertNotRegex(self.code, r"innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function|srcdoc|createContextualFragment|DOMParser|setTimeout\(\s*['\"]")
        self.assertNotRegex(self.code, r"\.href\b|\.src\b|setAttribute\(\s*['\"](href|src|style|on\w+|srcdoc)")
        made = sorted(set(re.findall(r"\bel\('(\w+)'", self.code)) | {"button"})
        self.assertEqual(made, ["bdi", "button", "div", "h2", "h3", "hr", "i", "li", "p", "section", "span", "ul"], "text and boxes: no a, img, script, iframe, style or link")

    def test_it_keeps_nothing_in_the_browser_and_opens_no_socket_of_its_own(self):
        self.assertNotRegex(self.code, r"localStorage|sessionStorage|indexedDB|document\.cookie|new WebSocket|WebSocket\(|XMLHttpRequest|fetch\(|sendBeacon")
        self.assertNotRegex(self.code, r"/hub\b", "the dock's script alone knows the hub's address")

    def test_it_reuses_the_docks_rules_for_text_and_the_pages_rules_for_a_map(self):
        self.assertIn("Chat.clip(Chat.plain(raw.name.slice(0, LIMIT.name * 4), true), LIMIT.name)", self.js, "a name is cleaned and cut as the chat's are (the invisible and bidirectional characters out, 32 code points)")
        self.assertIn("rules.validMapFile(raw.map)", self.js, "a map is a file name that the page's own rule accepts")
        self.assertIn("var STATUS = ['look', 'wait', 'play'];", self.js)
        self.assertIn("STATUS.indexOf(raw.at) === -1", self.js, "a status is one of the three (an object's own names are none)")

    def test_it_asks_for_nothing_but_the_list_and_says_nothing_but_where_the_visitor_is(self):
        self.assertEqual(sorted(set(re.findall(r"dock\.send\((.*?)\)\)", self.code))), ["frame", "{ t: 'who' }"])
        self.assertIn("var frame = { t: 'here' };", self.js)
        self.assertIn("if (!live || !supported || !you) return;", self.js, "a guest says nothing, nor does a page whose hub has no list")
        self.assertIn("if (live && supported && !asked && dock.send({ t: 'who' })) asked = true;", self.js)
        self.assertIn("MS = { poll: 1000, tell: 3000, note: 4000 }", self.js)

    def test_a_count_of_zero_is_a_count(self):
        self.assertIn("Chat.count(m.n) === null || Chat.count(m.g) === null || Chat.count(m.more) === null", self.js, "Chat.count says null for a number that is wrong and the number for one that is right: zero is right")


class TheStyle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.css = read("web", "home", "players.css")
        cls.js = read("web", "home", "players.js")
        cls.games = read("web", "home", "games.css")
        cls.chat = read("web", "home", "chat.css")
        cls.rules = re.findall(r"([^{}]+)\{([^{}]*)\}", re.sub(r"/\*.*?\*/", "", cls.css, flags=re.S))

    def rule(self, selector):
        return next(body for sel, body in self.rules if sel.strip() == selector)

    def test_the_breakpoint_of_the_docked_panel_is_the_scripts_and_the_panel_is_the_size_of_the_pictures(self):
        wide = re.search(r"var WIDE = '(\([^']*\) and \([^']*\))';", self.js).group(1)
        self.assertEqual(wide, "(min-width: 1180px) and (min-height: 501px)")
        self.assertIn("@media " + wide + " {", self.css)
        self.assertIn("width: 260px;", self.css.split("@media " + wide + " {")[1].split("}")[0])
        self.assertIn("@media (min-width: 1600px) and (min-height: 900px) { .pl { width: 280px; }", self.css)

    def test_the_games_list_that_the_live_site_docks_in_the_yard_keeps_clear_of_the_panel_under_1280_px_and_is_centered_from_there(self):
        rules = re.sub(r"/\*.*?\*/", "", self.games, flags=re.S)
        left, right, cap, taken = (int(g) for g in re.search(r"\.gdock \{ position: absolute; z-index: \d+; top: 12px; left: (\d+)px; right: (\d+)px; width: min\((\d+)px, calc\(100% - (\d+)px\)\);", rules).groups())
        gap, width = 16, 260
        self.assertIn("@media (min-width: 1180px) and (max-width: 1279px) and (min-height: 501px) { .pl-on .gdock { right: %dpx; width: min(%dpx, calc(100%% - %dpx)); } }" % (right + width + gap, cap, left + right + width + gap), self.css,
                      "from 1180 to 1279 px the list ends %d px before the panel (%d px wide, %d px from the Yard's edge)" % (gap, width, right))
        panel = re.search(r"\.pl \{ position: absolute; z-index: 6; display: flex; top: 14px; right: 14px; width: 260px;", self.css)
        self.assertIsNotNone(panel)
        self.assertEqual(right, 12, "the panel's 14 px from the edge and the list's 12 are why the gap is 16 and not 14: the panel's frame is 5 px, the list's 5")
        # from 1280 px the list's own rule holds (it is centered in the window and ends well before the panel): nothing here says where it is, whatever the window
        css = re.sub(r"/\*.*?\*/", "", self.css, flags=re.S)
        placed = [(query.strip(), body) for query, body in re.findall(r"@media ([^{]+)\{((?:[^{}]|\{[^{}]*\})*)\}", css) if re.search(r"\.pl-on \.gdock \{[^}]*\bwidth:", body)]
        self.assertEqual(len(placed), 1, "one rule of the list's width and place")
        self.assertIn("max-width: 1279px", placed[0][0])

    def test_the_games_list_goes_under_the_corner_button_or_beside_it(self):
        height = int(re.search(r"min-height: (\d+)px", self.rule(".pl-btn")).group(1))
        top = int(re.search(r"\.pl-btn \{ top: (\d+)px; right: 14px; \}", self.css).group(1))
        self.assertIn(".pl-on .gdock { top: %dpx; max-height: calc(100%% - 80px); }" % (top + height + 8), self.css, "the list starts 8 px under the button (14 + 46 + 8)")
        self.assertIn("(min-width: 721px) and (max-width: 760px) and (min-height: 501px) { .pl-on .gdock { max-height: calc(100% - 484px); } }", self.css, "above the chat panel, which the list's own rule keeps clear of (428 px) and the button's row takes 56 more")
        self.assertIn("(orientation: landscape) and (min-height: 501px) { .pl-on .gdock { max-height: calc(100% - 150px); } }", self.css)

    def test_nothing_of_homes_own_changes_until_the_list_is_there(self):
        for selector, _body in self.rules:
            for part in selector.split(","):
                part = part.strip()
                if re.search(r"\.(gdock|hbar|hright|hbrand|hlogo|yard|gc|gpop|gitem|ylabel)\b", part):
                    self.assertTrue(part.startswith(".pl-on "), "%r reaches a part of Home and is not under the class that the list puts on Home" % part)
        self.assertIn("host.classList.add('pl-on')", self.js)
        self.assertIn("host.classList.remove('pl-on')", self.js)

    def test_the_phone_gets_the_button_in_its_first_row_the_word_goes_before_the_picture_and_nothing_overlaps_the_logo(self):
        css = self.css
        self.assertIn("@media (max-width: 720px) and (orientation: portrait), (max-width: 639px) {", css, "the bar's three rows (home.css has the same condition)")
        phone = css.split("@media (max-width: 720px) and (orientation: portrait), (max-width: 639px) {")[1].split("\n}")[0]
        for part in (".pl-bar { display: inline-flex; }", ".pl-on .hbar .hbrand { grid-column: 1; grid-row: 1; }", ".pl-on .hbar .hright { grid-column: 1 / -1; min-width: 0; max-width: calc(100% - 102px); }", ".pl-btn { display: none; }"):
            self.assertIn(part, phone, part)
        self.assertIn("@media (max-width: 440px) { .pl-on .hbar .pl-bar { padding: 6px 10px; } }", css, "(the bar's own rule has two classes, so this one has three: Home's `.hbar .btn` padding is 16 px)")
        self.assertIn("@media (max-width: 410px) { .pl-bar .pl-ppl { display: none; } }", css, "from 411 px the picture, the word and the number; under that the word and the number")
        self.assertIn("@media (max-width: 380px) { .pl-bar .pl-ppl { display: inline-block; } .pl-bar .pl-word { display: none; } }", css, "from 381 px the word has room even with \"99+\"; under that the picture and the number")
        self.assertIn("@media (max-width: 359px) { .pl-on .hbar .hright { gap: 6px; } .pl-on .hbar .pl-bar { padding: 6px 8px; gap: 6px; } .pl-bar .pill { margin-left: 0; padding: 0 6px; font-size: 14px; } }", css, "a little closer under 360 px, so that \"99+\" fits at 320")
        self.assertIn("@media (max-width: 340px) { .pl-on .hbar .hlogo { width: 76px; } .pl-on .hbar .hright { max-width: calc(100% - 86px); } }", css)

    def test_the_sheet_clears_a_phones_system_bar_and_the_window_that_the_page_has(self):
        phone = self.css.split("@media (max-width: 720px) and (orientation: portrait), (max-width: 639px) {")[1].split("\n}")[0]
        self.assertIn("bottom: calc(12px + env(safe-area-inset-bottom, 0px));", phone)
        self.assertRegex(phone, r"max-height: calc\(100vh - 76px - env\(safe-area-inset-bottom, 0px\)\); max-height: calc\(100dvh - 76px - env\(safe-area-inset-bottom, 0px\)\);")

    def test_the_layers_the_sheet_is_over_the_menu_and_the_games_popover_the_docked_panel_beside_the_chat_panel(self):
        sheet = int(re.search(r"z-index: (\d+)", self.rule(".pl")).group(1))
        scrim = int(re.search(r"z-index: (\d+)", self.rule(".pl-scrim")).group(1))
        menu = int(re.search(r"\.hmenubox \{[^}]*z-index: (\d+)", read("web", "home", "home.css")).group(1))
        popover = int(re.search(r"\.gpop \{[^}]*z-index: (\d+)", self.games).group(1))
        chat = int(re.search(r"\.gc \{ position: absolute; left: \d+px; bottom: 22px; z-index: (\d+)", self.chat).group(1))
        self.assertGreater(scrim, menu)
        self.assertGreater(scrim, popover)
        self.assertGreater(sheet, scrim)
        docked = int(re.search(r"\.pl \{ position: absolute; z-index: (\d+)", self.css).group(1))
        self.assertEqual(docked, chat, "the docked panel stands as the chat panel does, under the popover and the cards")
        self.assertLess(docked, popover)

    def test_every_target_is_at_least_44_px_high_and_wide(self):
        heights = {}
        for selector, body in self.rules:
            for m in re.finditer(r"min-height: (\d+)px", body):
                heights.setdefault(selector.strip(), []).append(int(m.group(1)))
        for selector in (".pl-fold", ".pl-join", ".pl-btn", ".pl-bar", ".pl-host"):
            self.assertIn(selector, heights, selector)
        for selector, values in heights.items():
            if selector.startswith((".pl-card", ".pl-head", ".pl-you", ".pl-guests")):
                continue                                             # (a row is not a target; its Join is)
            for value in values:
                self.assertGreaterEqual(value, 44, "%s has a min-height of %d" % (selector, value))
        self.assertGreaterEqual(int(re.search(r"min-width: (\d+)px", self.rule(".pl-fold")).group(1)), 44)
        self.assertGreaterEqual(int(re.search(r"min-width: (\d+)px", self.rule(".pl-join")).group(1)), 44)

    def test_nothing_is_loaded_from_another_site_and_the_one_picture_is_data(self):
        self.assertNotRegex(self.css, r"https?://|@import")
        targets = re.findall(r"url\(\s*[\"']?([^\"')]+)", self.css)
        self.assertEqual(len(targets), 1)
        self.assertTrue(targets[0].startswith("data:image/svg+xml,"), targets[0])

    def test_nothing_scrolls_sideways_and_a_hidden_part_stays_hidden(self):
        self.assertNotRegex(self.css, r"overflow-x: *(scroll|auto)")
        hidden = next(sel for sel, body in self.rules if body.strip() == "display: none;" and ".pl-root[hidden]" in sel)
        for part in (".pl-root[hidden]", ".pl-bar[hidden]", ".pl-btn[hidden]", ".pl-scrim[hidden]", ".pill[hidden]", ".pl-dot[hidden]", ".pl-status:empty"):
            self.assertIn(part, hidden, part)

    def test_the_drawer_of_a_phone_held_sideways_is_320_px_and_the_list_of_games_is_narrower_by_the_button(self):
        self.assertIn(".pl { top: 10px; left: auto; right: 10px; bottom: auto; width: min(320px, calc(100vw - 20px)); max-height: calc(100% - 20px); }", self.css)
        self.assertIn(".pl-on .gdock { right: 186px; }", self.css, "8 px of the list's own edge, the button (165 px) and 13 px between")
        self.assertIn(".pl-on .gdock { right: 116px; }", self.css, "the button without its word (93 px)")

    def test_reduced_motion_needs_nothing_here(self):
        self.assertNotRegex(self.css, r"animation|transition")


class TheWayIntoTheImage(unittest.TestCase):
    def test_the_ci_looks_for_the_script_and_the_style_in_the_image(self):
        self.assertIn(" home/players.js home/players.css ", read(".github", "workflows", "ci.yml"))

    def test_the_dockerfile_copies_the_folder_to_where_nginx_serves_home_and_the_local_build_packs_it(self):
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/\n", read("Dockerfile"))
        self.assertIn('cp -f "${SCRIPT_DIR}/web/home"/* "${DIST_DIR}/home/"', read("build_web.sh"))


@unittest.skipUnless(NODE, "node is not installed: the players list was NOT run (tests/scripts/web_home_players_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_list_against_a_fake_dock_and_a_fake_clock(self):
        done = subprocess.run([NODE, CHECK, LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"players list: \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_players_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = (["web/lobby.html", "web/shapes.js", "web/home/home.js", "web/home/home.css", "web/home/chat.js", "web/home/chat.css", "web/home/games.css", "web/home/players.js", "web/home/players.css",
              "tests/scripts/test_web_home_players.py", "tests/scripts/web_home_players_check.js", "tests/scripts/web_home_chat_check.js", "tests/scripts/web_page_harness.js", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml",
              "web/matches.html", "web/live.html", "web/match.html", "web/legal.html", "web/mapmaker.html", "web/shell.html", "web/admin.html"]
             + ["web/front/" + n for n in ("lobby_rules.js", "lobby_net.js", "lobby_chat.js", "lobby_chat.css", "rematch.js", "game_modes.js", "game_type.js", "map_picker.js", "map_maker_link.js")])

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_players_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_players_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    def test_the_copy_with_nothing_changed_passes(self):
        for command in sorted({m["test"] for m in self.spec["mutants"]}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

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
