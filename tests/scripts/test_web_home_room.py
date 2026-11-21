#!/usr/bin/env python3
"""The room inside Home and the room's tab of the chat box (web/home/room.css, web/home/roomchat.js and roomchat.css, docs/HOME_SCREEN.md "The Room", docs/HOME_CHAT.md "The Room tab"; run by ./run_tests.sh --fast and by the CI).

What this file checks (the room's own behavior is in the other suites of its topics: the page's rules in test_web_lobby.py and test_web_front.py, the screens in test_web_home_screen.py, the real browser at
the sizes in tests/scripts/web_room_in_home_browser.js, opt-in):
  - tests/scripts/web_home_roomchat_check.js runs in node (the tabs, the dots, a folded box, the keys, the room that goes, the parts that are missing);
  - the files: their budgets (raw and gzipped, the three together), ASCII, plain ES5 in the script, no markup, no network, no storage;
  - the style: a tab is 44 px high at least, the site's chat gives its place to the room's while the Room tab shows;
  - the files are in the Docker image's check and the folder is copied whole;
  - tests/data/home_room_mutants.json changes one thing at a time in a COPY of the files: the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
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
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_roomchat_check.js")
NODE = shutil.which("node")
FILES = ("room.css", "roomchat.js", "roomchat.css")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import ROOM_BUDGET, ROOM_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class TheFiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "roomchat.js")
        cls.code = re.sub(r"//.*", "", cls.js)

    def test_the_tabs_run_in_node(self):
        if not NODE:
            raise unittest.SkipTest("node is not installed")
        done = subprocess.run([NODE, CHECK], capture_output=True, text=True, timeout=120, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")

    def test_they_are_under_their_budgets(self):
        raw = zipped = 0
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            raw += len(data)
            zipped += len(gzip.compress(data, 9))
            self.assertIsNone(re.search(rb"[^\x00-\x7f]", data), name + " is not ASCII")
        self.assertLess(raw, ROOM_BUDGET, "room.css, roomchat.js and roomchat.css are %d bytes" % raw)
        self.assertLess(zipped, ROOM_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_the_folder_has_the_three_files_of_the_room(self):
        for name in FILES:
            self.assertTrue(os.path.isfile(os.path.join(HOME, name)), name)

    def test_the_script_is_plain_es5_with_no_markup_no_network_and_no_storage(self):
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\s+\w+\s*[{(]", r"\basync\b", r"\bawait\b"):
            self.assertIsNone(re.search(token, self.code), token)
        self.assertIn("'use strict';", self.js)
        self.assertNotRegex(self.code, r"innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function|XMLHttpRequest|fetch\(|WebSocket|localStorage|sessionStorage|https?://")

    def test_the_room_sheet_has_no_address_or_picture(self):
        css = read("web", "home", "room.css")
        self.assertNotRegex(css.replace("http://www.w3.org/2000/svg", ""), r"https?://")


class TheStyle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        css = re.sub(r"/\*.*?\*/", "", read("web", "home", "roomchat.css"), flags=re.S)
        cls.rules = re.findall(r"([^{}]+)\{([^{}]*)\}", css)

    def declarations(self, selector_part):
        return [body for sel, body in self.rules if selector_part in sel]

    def test_every_tab_is_44_px_high_at_least(self):
        heights = [int(h) for body in self.declarations(".rc-tab") for h in re.findall(r"min-height:\s*(\d+)px", body)]
        self.assertTrue(heights and min(heights) >= 44, heights)

    def test_the_sites_chat_gives_its_place_to_the_rooms_while_the_room_tab_shows(self):
        hidden = [body for sel, body in self.rules if 'data-rc="room"' in sel and "> .gc-body" in sel]
        self.assertTrue(any("display: none" in body for body in hidden))

    def test_forced_colors_keep_the_selected_tab_and_the_dot_visible(self):
        css = read("web", "home", "roomchat.css")
        self.assertIn("@media (forced-colors: active)", css)
        self.assertIn("Highlight", css)


class TheLoader(unittest.TestCase):
    def test_the_files_are_in_the_image_check_and_the_folder_is_copied_whole(self):
        ci = read(".github", "workflows", "ci.yml")
        self.assertIn("home/room.css home/roomchat.js home/roomchat.css", ci)
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/", read("Dockerfile"))

    def test_the_page_links_the_sheet_of_the_room_and_home_asks_for_the_tab_after_the_dock(self):
        page = read("web", "lobby.html")
        self.assertIn('href="home/room.css"', page)
        js = read("web", "home", "home.js")
        self.assertIn("home/roomchat", js)
        self.assertLess(js.index("function wantDock"), js.index("function wantRoomChat"))


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_room_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = ["tools/size_limits.py", "web/lobby.html", "web/home/home.js", "web/home/room.css", "web/home/roomchat.js", "web/home/roomchat.css", "tests/scripts/test_web_home_room.py",
             "tests/scripts/web_home_roomchat_check.js", "Dockerfile", ".github/workflows/ci.yml"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_room_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_room_mutants.")
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
        self.assertEqual(missed, [], "mutants that no check saw")


if __name__ == "__main__":
    unittest.main()
