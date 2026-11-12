#!/usr/bin/env python3
"""The players list of Home (web/home/players.js and players.css, docs/HOME_SCREEN.md "The players list", docs/HUB.md "Presence"; run by ./run_tests.sh --fast and by the CI).

The list shows who has the front page open right now: a narrow column at the right of the Games list, or the Players view of a phone. It speaks through the chat dock's socket, is loaded by web/home/home.js after the dock, and every word it shows is text from strangers. What this file checks:

  - the page and the other pages: web/lobby.html does not link the list, no other file names it, and the hub's door is still the dock's script alone;
  - the loader in home.js: the two files, once, after the dock is mounted, mounted when BOTH have come, given the parts of the bar that it needs, inert with the bars during the hand-off, and what goes wrong stays;
  - the folder: its own budgets (raw and gzipped), plain ES5, no markup made from text, no link, no storage, no socket of its own, the kinds of element it makes;
  - the style: the breakpoint of the script, the sizes of the approved pictures (260 px, 280 from 1600 x 900), the room that the Games list leaves for it under 1280 px (the arithmetic; from 1280 px the
    list's own rule centers it, and no rule of this file says where it is), the labels of the bar's button (the picture, the word and the number from 411 px, the word and the number from 381 px, the picture and
    the number under that, closer under 360 px: so that \"99+\" is cut off nowhere from 320 to 639 px), nothing of Home's own changes until the list is there (every rule that reaches the bar or the Games list
    starts with the class that the script puts on Home), every target at least 44 px, the layers, nothing loaded from another site;
  - the way into the image: the CI's list of the image's files carries the two files, the Dockerfile and the local web build copy the folder;
  - the code RUNS in node: tests/scripts/web_home_players_check.js (a fake dock and a fake clock: the pure parts, the phases, what the page tells the hub, what is drawn, Join, the sheet, malformed frames, the page);
  - tests/data/home_players_mutants.json changes one thing at a time in a COPY of the files (a count that is zero, a status that is not checked, a code for a private room, a flood of rows, a second socket, markup,
    the loader, the style's arithmetic, ...): the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
The pictures that were approved are compared by hand with the page in a real browser (tests/scripts/web_home_players_browser.js, opt-in, `--shots DIR`).
"""
import gzip
import json
import os
import re
import shutil
import sys
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
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import PLAYERS_BUDGET, PLAYERS_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


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
        naming_the_list, naming_the_socket = set(), set()
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
                if re.search(r"/hub/ws", text):
                    naming_the_socket.add(rel)
        self.assertEqual(sorted(naming_the_list), ["web/home/home.js", "web/home/players.css", "web/home/players.js"], "only the front page's own script asks for the list (the two files name themselves)")
        self.assertEqual(sorted(naming_the_socket), ["web/home/chat.js"], "the list opens no socket of its own: it speaks through the dock's (the one other script that names the hub asks its one page, GET /hub/me, for the name to play under)")

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
        self.assertIn("inert(held); dockShown(); pick(tab); wantPlayers(); wantTalk(); } } catch (e) { dock = null; } }", self.js)
        self.assertIn("if (askedPlayers || !dock) return;\n            askedPlayers = true;", self.js, "once, and not before there is a dock to speak through")
        self.assertEqual(sorted(set(re.findall(r"home/players\w*", code_of(self.js)))), ["home/players"])

    def test_it_is_given_the_dock_and_the_parts_of_the_bar_that_it_uses_and_nothing_of_the_page_but_the_bridge(self):
        self.assertIn("players = win.AntsHomePlayers.mount({ doc: doc, win: win, host: host, yard: main, quickHost: quickHost, games: gamesBtn, dock: dock, page: page }); inert(held);", self.js)

    def test_it_is_inert_with_the_bars_and_the_dock_and_a_list_that_comes_late_is_inert_from_the_start(self):
        self.assertIn("[bar, rbar, tabs, dock && dock.root, players && players.root].forEach(function (n) {", self.js)
        self.assertIn("inert(held); var c = ", self.js.split("function wantPlayers")[1], "a list that is mounted while a dialog or the hand-off holds the page takes no press either")

    def test_the_part_loader_is_the_one_that_the_games_list_has_and_a_part_that_throws_stays_in_it(self):
        self.assertIn("function one() { if (--parts === 0) { try { onReady(); } catch (e) { /* the part is its own */ } } }", self.js)
        self.assertIn("if (win.AntsHomePlayers) players =", self.js, "a script that did not define the list mounts nothing")
        self.assertIn("if (c) mirror(c, tabBtns.players.pill);", read("web", "home", "home.js"), "the phone's Players tab shows the list's count")


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

    def test_the_panel_is_always_docked_a_column_of_homes_grid_or_the_phones_view(self):
        for name in ("WIDE", "docked(", "data-sheet", "pl-scrim", "pl-btn", "pl-bar", "pl-close", "tabbable"):
            self.assertNotIn(name, self.js, "there is no sheet, drawer or button to open the panel: " + name)
        body = self.rule(".pl")
        self.assertIn("display: flex; flex-direction: column; min-height: 0;", body)
        for name in ("position", "z-index", "top:", "right:", "width"):
            self.assertNotIn(name, body, "the panel is a box of Home's grid (home.css, .pl-on .hmain: 220 to 280 px), not a placed layer: " + name)
        self.assertIn(".pl-on .hmain { grid-template-columns: minmax(0, 1fr) clamp(220px, 24vw, 280px); }", read("web", "home", "home.css"))

    def test_the_panel_has_no_fold_at_any_width(self):
        for name in (".pl-btn", ".pl-bar", ".pl-scrim", ".pl-close", ".pl-x", ".pl-ppl"):
            self.assertNotIn(name, self.css, name)
        self.assertNotIn("fold", self.css, "the panel is always open: no fold button, no folded state")
        self.assertNotIn("pl-mark", self.css)
        self.assertNotIn("z-index", self.css, "nothing here is a layer over the page")

    def test_nothing_of_homes_own_changes_until_the_list_is_there(self):
        for selector, _body in self.rules:
            for part in selector.split(","):
                part = part.strip()
                if re.search(r"\.(gdock|hbar|hright|hbrand|hlogo|yard|gc|gpop|gitem|ylabel)\b", part):
                    self.assertTrue(part.startswith(".pl-on "), "%r reaches a part of Home and is not under the class that the list puts on Home" % part)
        self.assertIn("host.classList.add('pl-on')", self.js)
        self.assertIn("host.classList.remove('pl-on')", self.js)

    def test_every_target_is_at_least_44_px_high_and_wide(self):
        heights = {}
        for selector, body in self.rules:
            for m in re.finditer(r"min-height: (\d+)px", body):
                heights.setdefault(selector.strip(), []).append(int(m.group(1)))
        for selector in (".pl-join",):
            self.assertIn(selector, heights, selector)
        for selector, values in heights.items():
            if selector.startswith((".pl-card", ".pl-head", ".pl-you", ".pl-guests")):
                continue                                             # (a row is not a target; its Join is)
            for value in values:
                self.assertGreaterEqual(value, 44, "%s has a min-height of %d" % (selector, value))
        self.assertGreaterEqual(int(re.search(r"min-width: (\d+)px", self.rule(".pl-join")).group(1)), 44)

    def test_nothing_is_loaded_from_another_site_and_the_one_picture_is_data(self):
        self.assertNotRegex(self.css, r"https?://|@import")
        self.assertEqual(re.findall(r"url\(", self.css), [], "no picture at all: the sheet's button picture is gone with the sheet")

    def test_nothing_scrolls_sideways_and_a_hidden_part_stays_hidden(self):
        self.assertNotRegex(self.css, r"overflow-x: *(scroll|auto)")
        hidden = next(sel for sel, body in self.rules if body.strip() == "display: none;" and ".pl-root[hidden]" in sel)
        for part in (".pl-root[hidden]", ".pill[hidden]", ".pl-dot[hidden]", ".pl-status:empty"):
            self.assertIn(part, hidden, part)

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
    FILES = (["tools/size_limits.py", "web/lobby.html", "web/rejoin_key.js", "web/front/player_name.js", "web/shapes.js", "web/home/home.js", "web/home/home.css", "web/home/chat.js", "web/home/chat.css", "web/home/games.css", "web/home/players.js", "web/home/players.css",
              "tests/scripts/test_web_home_players.py", "tests/scripts/web_home_players_check.js", "tests/scripts/web_home_chat_check.js", "tests/scripts/web_page_harness.js", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml",
              "web/matches.html", "web/live.html", "web/match.html", "web/legal.html", "web/mapmaker.html", "web/shell.html", "web/admin.html", "docs/HOME_SCREEN.md"]
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
