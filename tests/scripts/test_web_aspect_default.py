#!/usr/bin/env python3
"""16:9 is the default picture of the web pages (run by ./run_tests.sh --fast and by the CI). The owner: 16:9 is to be the default everywhere. The pages already show 16:9 unless the
address says ?aspect=4:3 or the selector remembered a Classic 4:3, and a browser that once tried Classic 4:3 stayed on it for good (the choice was remembered under `ants.aspect`). The
pages now remember the choice under `ants.aspect.v2` and do not read the old key, so a Classic 4:3 that an earlier page remembered is forgotten once: every browser starts 16:9 until its
player picks Classic 4:3 again.

  - the pages' own code is RUN (node, when it is installed) on a table of addresses and of what a browser had stored, among them an old `ants.aspect` = 4:3 that must not give 4:3, and
    the selectors' clicks, which must write the new key and nothing else (tests/scripts/web_aspect_key_check.js);
  - what needs no browser is read from the files: the new key is the one constant of each page and no call reads or writes the old key; the markup and the style of both pages start with
    the 16:9 picture (the game page's box, its selector, the Play online frames and its form).
The same cases in a real browser: tests/scripts/web_aspect_check.py (opt-in, against a running page).
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHELL = os.path.join(REPO, "web", "shell.html")
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_aspect_key_check.js")
NEW_KEY = "ants.aspect.v2"


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class PagesStartWith16x9(unittest.TestCase):
    def test_the_game_page_box_selector_and_code_start_with_16_9(self):
        page = read(SHELL)
        self.assertIn('<div id="game-stage" data-aspect="16:9">', page)
        self.assertRegex(page, r'id="aspect-16-9" data-aspect="16:9" aria-checked="true"')
        self.assertRegex(page, r'id="aspect-4-3" data-aspect="4:3" aria-checked="false"')
        self.assertRegex(page, r"#game-stage \{\s*--ar-w: 16;\s*--ar-h: 9;")              # the slot's own shape; only data-aspect="4:3" changes it
        self.assertRegex(page, r'#game-stage\[data-aspect="4:3"\] \{\s*--ar-w: 4;\s*--ar-h: 3;')
        self.assertIn("var ANTS_ASPECT = '16:9';", page)
        self.assertIn("return { aspect: '16:9', source: 'default' };", page)                 # nothing says otherwise: 16:9 on every device, a phone held upright included

    def test_the_play_online_page_frames_and_form_start_with_16_9(self):
        page = read(LOBBY)
        self.assertIn(".frame { position: relative; aspect-ratio: 16 / 9; }", page)
        self.assertIn('body[data-aspect="4:3"] .frame { aspect-ratio: 4 / 3; }', page)
        self.assertIn("var aspect = '16:9';", page)
        self.assertRegex(page, r'<select id="aspect-select"[^>]*>\s*<option value="16:9">16:9</option>')


class TheChoiceLivesUnderTheNewKey(unittest.TestCase):
    def test_each_page_names_the_new_key_once_and_no_call_uses_the_old_one(self):
        for path, constant in ((SHELL, "ANTS_ASPECT_KEY"), (LOBBY, "ASPECT_KEY")):
            page = read(path)
            name = os.path.basename(path)
            self.assertEqual(len(re.findall(r"var " + constant + r" = '" + re.escape(NEW_KEY) + r"';", page)), 1, name)
            for access in ("getItem", "setItem", "removeItem", "recall", "remember"):
                self.assertNotRegex(page, access + r"\(\s*['\"]ants\.aspect['\"]", name + ": " + access + " uses the old key")
            self.assertEqual(len(re.findall(r"\b" + constant + r"\b", page)) >= 3, True, name)      # (the constant, its read and its write)

    def test_the_two_pages_use_the_same_key_and_the_browser_check_does_too(self):
        self.assertIn("var ANTS_ASPECT_KEY = '" + NEW_KEY + "';", read(SHELL))
        self.assertIn("var ASPECT_KEY = '" + NEW_KEY + "';", read(LOBBY))
        browser = read(os.path.join(REPO, "tests", "scripts", "web_aspect_check.py"))
        self.assertIn("localStorage.setItem('" + NEW_KEY + "', '4:3')", browser)
        self.assertIn("localStorage.setItem('ants.aspect', '4:3')", browser)                  # (the old key is set on purpose: it must not give 4:3)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the pages' own code for the picture's shape was NOT run (tests/scripts/web_aspect_key_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_a_table_of_addresses_and_stored_values_and_the_selectors(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL, LOBBY], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
