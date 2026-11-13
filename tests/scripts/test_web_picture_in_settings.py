#!/usr/bin/env python3
"""The picture's shape and the fullscreen mouse are rows of the settings menu only (docs/GAME_MENU.md): the game page has no strip under the picture, the box has the strip's height, the header has the one
Settings button at every width, and the menu's rows work. The page is run in a real Chromium with its game stood in for (tests/scripts/web_picture_in_settings_check.js); a machine without Playwright
skips it (the quick checks of the same change are in test_web_aspect_default.py, test_web_game_menu.py and the node checks they run)."""
import os
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECK = os.path.join(REPO, "tests", "scripts", "web_picture_in_settings_check.js")


class TheGamePageInAStandInBrowser(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "node is not installed: the page was NOT run in a browser")
    def test_the_box_the_header_button_and_the_menu_rows(self):
        done = subprocess.run([shutil.which("node"), CHECK, os.path.join(REPO, "web")], capture_output=True, text=True, timeout=300)
        if done.returncode == 3:
            self.skipTest(done.stdout.strip() or "no browser")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failures")


if __name__ == "__main__":
    unittest.main()
