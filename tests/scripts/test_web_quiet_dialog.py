#!/usr/bin/env python3
"""The game's sound while a box of the browser's own is open (web/shell.html, antsQuietly; docs/PLAY_IN_BROWSER.md; run by ./run_tests.sh --fast and by the CI).

A confirm or a prompt stops the page until it is answered, and with it the page's feed of SDL's Web Audio, which then plays its last piece of sound over and over. The page pauses the sound while such a
box is open. tests/scripts/web_quiet_dialog_check.js runs the page's own block (between ANTS_QUIET_BEGIN and ANTS_QUIET_END) on a page of fakes: paused and resumed around the box, the answer passed back,
a context that does not play left alone, no context at all, a box or a context that throws or refuses.
"""
import os
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECK = os.path.join(REPO, "tests", "scripts", "web_quiet_dialog_check.js")
NODE = shutil.which("node")


@unittest.skipUnless(NODE, "node is not installed: the game page's sound around a box was NOT run (tests/scripts/web_quiet_dialog_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_sound_is_paused_while_a_box_is_open(self):
        done = subprocess.run([NODE, CHECK, os.path.join(REPO, "web", "shell.html")], capture_output=True, text=True, timeout=120, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"quiet_dialog: \d+ checks, 0 failed")


if __name__ == "__main__":
    unittest.main()
