#!/usr/bin/env python3
"""The moderation sheet of the chat (web/home/mod.js and mod.css, docs/HOME_CHAT.md "Roles and commands", docs/HUB.md "Moderation"; run by ./run_tests.sh --fast and by the CI).

A staff member (the hub's `you.role` is mod or admin) acts on a person's line from the chat itself: a right click on the line, a long press on a touch screen, or the keys (Up and Down choose a line, Enter, the menu key or Shift
and F10 open it). A sheet offers what the viewer may do to that line's author (Delete the line, Mute, Kick, Ban), and types the hub's own commands by the NUMBER of the line, so there is no new power. It is loaded by
web/home/home.js only for a visitor with a role. What this file checks:
  - the page and the other pages: web/lobby.html does not link it, nothing but home.js asks for it, the hub's door is still the dock's script alone, the game and the other pages never hear of it;
  - the folder: its own budgets (raw and gzipped), plain ES5, ASCII, no markup made from text, no storage, no network of its own, the kinds of element it makes, no way to send but the dock's `command`;
  - the style: nothing loaded from another site, no sideways scroll, and the phone's callout and text selection are held back on a line that has an action ONLY on a touch screen (a desktop keeps its text selection);
  - the code RUNS in node: tests/scripts/web_home_mod_check.js (the real dock, a fake socket, a fake clock and a fake page);
  - tests/data/home_mod_mutants.json changes one thing at a time in a COPY of the files: the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
The sizes of the sheet's targets and its places on a screen are measured in a real browser (tests/scripts/web_home_mod_browser.js, opt-in, `--shots DIR`).
"""
import gzip
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HOME = os.path.join(REPO, "web", "home")
WEB = os.path.join(REPO, "web")
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_mod_check.js")
LOBBY = os.path.join(REPO, "web", "lobby.html")
NODE = shutil.which("node")
FILES = ("mod.js", "mod.css")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import MOD_BUDGET, MOD_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def code_of(js):
    return re.sub(r"//.*", "", js)


class ThePageAndTheOtherPages(unittest.TestCase):
    def test_the_front_page_does_not_link_the_sheet_home_loads_it_later(self):
        page = read("web", "lobby.html")
        for word in ("home/mod", "AntsHomeMod", "mod.js", "mod.css"):
            self.assertNotIn(word, page, word)

    def test_only_home_asks_for_it_and_the_hubs_door_is_still_the_docks_alone(self):
        naming_it, naming_the_socket = set(), set()
        for base, _dirs, names in os.walk(WEB):
            for name in names:
                if os.path.splitext(name)[1] not in (".html", ".js", ".css"):
                    continue
                path = os.path.join(base, name)
                rel = os.path.relpath(path, REPO).replace(os.sep, "/")
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = f.read()
                code = re.sub(r"/\*.*?\*/", "", re.sub(r"(?m)^\s*//.*|\s//.*", "", text), flags=re.S) if name.endswith((".js", ".css")) else text       # (a comment may name a file; the code may not)
                if re.search(r"home/mod\b|AntsHomeMod", code):
                    naming_it.add(rel)
                if re.search(r"/hub/ws", text):
                    naming_the_socket.add(rel)
        self.assertEqual(sorted(naming_it), ["web/home/home.js", "web/home/mod.js"], "home.js asks for it; its own header names its global")
        self.assertEqual(sorted(naming_the_socket), ["web/home/chat.js", "web/hub_here.js"])

    def test_the_game_and_the_other_pages_never_hear_of_it(self):
        for rel in (("web", "shell.html"), ("web", "front", "lobby_net.js"), ("web", "front", "lobby_rules.js"), ("web", "front", "lobby_chat.js")):
            self.assertNotRegex(read(*rel), r"AntsHomeMod|home/mod", rel[-1])


class TheFolderAndTheScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "mod.js")
        cls.code = code_of(cls.js)

    def test_it_is_under_its_budgets(self):
        raw = zipped = 0
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            raw += len(data)
            zipped += len(gzip.compress(data, 9))
        self.assertLess(raw, MOD_BUDGET, "mod.js and mod.css are %d bytes" % raw)
        self.assertLess(zipped, MOD_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_the_sources_are_ascii_only(self):
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            self.assertIsNone(re.search(rb"[^\x00-\x7f]", data), name + " holds a character that is not ASCII")

    def test_the_script_is_plain_es5(self):
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\s+\w+\s*[{(]", r"\basync\b", r"\bawait\b"):
            self.assertIsNone(re.search(token, self.code), token)
        self.assertIn("'use strict';", self.js)

    def test_nothing_that_is_said_becomes_markup_a_link_or_code(self):
        self.assertNotRegex(self.code, r"innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function|srcdoc|createContextualFragment|DOMParser|setTimeout\(\s*['\"]")
        self.assertNotRegex(self.code, r"\.href\b|\.src\b|setAttribute\(\s*['\"](href|src|style|on\w+|srcdoc)")
        made = sorted(set(re.findall(r"\bel\('(\w+)'", self.code)) | set(re.findall(r"createElement\('(\w+)'", self.code)))
        self.assertEqual(made, ["b", "button", "div", "h2", "input", "p", "small", "span", "svg"], "text, boxes, the buttons and the custom field: no a, img, script, iframe, style or link")

    def test_it_keeps_nothing_in_the_browser_and_has_no_network_of_its_own(self):
        self.assertNotRegex(self.code, r"localStorage|sessionStorage|indexedDB|document\.cookie|new WebSocket|WebSocket\(|XMLHttpRequest|fetch\(|sendBeacon|\bcaches\b")
        self.assertNotRegex(self.code, r"/hub\b|https?:")

    def test_the_only_way_it_sends_is_the_docks_command(self):
        self.assertNotRegex(self.code, r"dock\.(send|say)\b|\.submit\(|requestSubmit|dispatchEvent")
        self.assertEqual(len(re.findall(r"dock\.command\(", self.code)), 1, "one place that sends")
        self.assertNotRegex(self.code, r"/mute [a-z]|/kick [a-z]|/ban [a-z]", "never a command by a name")


class TheStyle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.css = read("web", "home", "mod.css")

    def test_the_callout_and_the_text_selection_are_held_back_only_on_a_touch_screen_and_only_on_a_line_with_actions(self):
        text = re.sub(r"/\*.*?\*/", "", self.css, flags=re.S)
        media = re.compile(r"@media\s*([^{]+)\{((?:[^{}]|\{[^{}]*\})*)\}")
        outside = media.sub("", text)
        self.assertNotRegex(outside, r"user-select|touch-callout", "held back with no media condition, a desktop would lose copying")
        held = 0
        for m in media.finditer(text):
            for selector, body in re.findall(r"([^{}]+)\{([^{}]*)\}", m.group(2)):
                if re.search(r"user-select|touch-callout", body):
                    held += 1
                    self.assertIn("pointer: coarse", m.group(1), "only on a touch screen")
                    self.assertIn("[data-act]", selector, "only on the lines that have an action")
        self.assertGreaterEqual(held, 1, "the style holds them back where a long press opens the sheet")

    def test_nothing_is_loaded_from_another_site_and_there_is_no_picture(self):
        self.assertNotRegex(self.css, r"https?://|@import")
        self.assertEqual(re.findall(r"url\(", self.css), [])
        self.assertIsNone(re.search(r"overflow-x: *(scroll|auto)", self.css))


class TheWayIntoTheImage(unittest.TestCase):
    def test_the_ci_looks_for_the_script_and_the_style_in_the_image(self):
        self.assertIn(" home/mod.js home/mod.css ", read(".github", "workflows", "ci.yml"))

    def test_the_dockerfile_copies_the_folder_to_where_nginx_serves_home_and_the_local_build_packs_it(self):
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/\n", read("Dockerfile"))
        self.assertIn('cp -f "${SCRIPT_DIR}/web/home"/* "${DIST_DIR}/home/"', read("build_web.sh"))


@unittest.skipUnless(NODE, "node is not installed: the sheet was NOT run (tests/scripts/web_home_mod_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_sheet_against_the_real_dock_and_a_fake_socket(self):
        done = subprocess.run([NODE, CHECK, LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"OK: \d+ checks")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_mod_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = (["tools/size_limits.py", "web/lobby.html", "web/rejoin_key.js", "web/hub_here.js", "web/front/player_name.js", "web/shapes.js", "web/home/home.js", "web/home/home.css", "web/home/chat.js", "web/home/chat.css", "web/home/mod.js", "web/home/mod.css",
              "tests/scripts/test_web_home_mod.py", "tests/scripts/web_home_mod_check.js", "tests/scripts/web_page_harness.js", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml",
              "web/matches.html", "web/match.html", "web/legal.html", "web/mapmaker.html", "web/shell.html", "web/admin.html"]
             + ["web/front/" + n for n in ("classic.css", "lobby_rules.js", "lobby_net.js", "lobby_chat.js", "lobby_chat.css", "rematch.js", "game_modes.js", "game_type.js", "map_picker.js", "map_maker_link.js")])

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_mod_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_mod_mutants.")
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
