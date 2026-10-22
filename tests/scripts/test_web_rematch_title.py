#!/usr/bin/env python3
"""The title of a draft map that the Map Maker's "With other players" uploads (web/front/rematch.js, run by ./run_tests.sh --fast, with the other python tests of tests/scripts, and by the CI).

tests/scripts/web_rematch_title_check.js drives frontStrip().begin() with a small fake of the DOM and of the opener, and reads what the page puts in `name=` of POST /submissions?test=1: the name of the draft
cleaned of control characters and white space and cut at 45 characters ("Test Map" when nothing is left), the kind from the version word of the bytes. The server cuts and cleans the name again (test_submissions_api
API2.11); this holds the page's own cut. The rest of the page's side of the rematch and of the test room is test_web_rematch.py. The check needs node; without it the test is skipped and says so.
"""
import os
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECK = os.path.join(REPO, "tests", "scripts", "web_rematch_title_check.js")
REMATCH = os.path.join(REPO, "web", "front", "rematch.js")
NODE = shutil.which("node")


@unittest.skipUnless(NODE, "node is not installed: the title of a test map was NOT checked")
class TheTitleOfATestMap(unittest.TestCase):
    def test_it_is_cleaned_and_cut_at_45_characters(self):
        done = subprocess.run([NODE, CHECK, REMATCH], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)


if __name__ == "__main__":
    unittest.main()
