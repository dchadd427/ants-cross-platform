#!/usr/bin/env python3
"""The game page's part of the bar at the top (web/game_bar.js and web/game_bar.css; docs/SITE_NAV.md "The game page"): run by ./run_tests.sh --fast and by the CI.

  - the files are in the image and in the build, are ASCII and plain ES5, and are within their byte budget (tools/size_limits.py);
  - the page's own look is tests/scripts/test_web_game.py; the site bar's, tests/scripts/test_web_site_nav.py;
  - what the bar does is run in a real Chromium when the machine has it (tests/scripts/web_game_bar_check.js, on the real site_nav.js and the real markup of the bar): the links that ask or open a new tab while
    a match runs, the keys that are the bar's, the focus after a press, the Menu's own lines, the room under the bar;
  - every mutant of tests/data/game_bar_mutants.json (tools/mutate.py runs them too) is noticed by that check.
"""
import gzip
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import size_limits  # noqa: E402

SCRIPT = os.path.join(REPO, "web", "game_bar.js")
STYLE = os.path.join(REPO, "web", "game_bar.css")
CHECK = os.path.join(REPO, "tests", "scripts", "web_game_bar_check.js")
MUTANTS = os.path.join(REPO, "tests", "data", "game_bar_mutants.json")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class TheFiles(unittest.TestCase):
    def test_the_image_and_the_build_carry_the_files(self):
        docker = read(os.path.join(REPO, "Dockerfile"))
        for name in ("game_bar.css", "game_bar.js"):
            self.assertEqual(docker.count("COPY web/%s /usr/share/nginx/html/%s\n" % (name, name)), 1, name)
        self.assertIn("site_nav.css site_nav.js game_bar.css game_bar.js", read(os.path.join(REPO, ".github", "workflows", "ci.yml")))
        self.assertIn('web/game_bar.css" "${SCRIPT_DIR}/web/game_bar.js" "${DIST_DIR}/"', read(os.path.join(REPO, "build_web.sh")))

    def test_they_are_ascii_plain_es5_and_within_their_budget(self):
        raw = gzipped = 0
        for path in (SCRIPT, STYLE):
            data = open(path, "rb").read()
            self.assertTrue(all(b < 128 for b in data), path + " is ASCII")
            raw += len(data)
            gzipped += len(gzip.compress(data, 9))
        for bad in ("=>", "let ", "const ", "`", "class ", "async ", "await ", "innerHTML", "document.write", "eval(", "new Function", "localStorage", "cookie", "fetch("):
            self.assertNotIn(bad, read(SCRIPT), bad)
        self.assertNotIn("@import", read(STYLE))
        self.assertLess(raw, size_limits.GAME_BAR_BUDGET, "the bar's script and style are %d bytes" % raw)
        self.assertLess(gzipped, size_limits.GAME_BAR_GZIP_BUDGET, "and %d bytes gzipped" % gzipped)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the game page's bar was NOT run in a browser (tests/scripts/web_game_bar_check.js)")
class TheBarInABrowser(unittest.TestCase):
    def test_links_keys_focus_menu_and_room_under_the_bar(self):
        done = subprocess.run([shutil.which("node"), CHECK, SCRIPT, STYLE], capture_output=True, text=True, timeout=300)
        if done.returncode == 3:
            self.skipTest(done.stdout.strip() or "no browser")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failures")


@unittest.skipUnless(shutil.which("node"), "node is not installed: the mutants of the game page's bar were NOT run")
class TheMutants(unittest.TestCase):
    def test_the_check_fails_on_every_broken_copy(self):
        """tests/data/game_bar_mutants.json: each mutant on a copy of its file, noticed by the browser check"""
        spec = json.loads(read(MUTANTS))
        self.assertGreaterEqual(len(spec["mutants"]), 40)
        with tempfile.TemporaryDirectory() as tmp:
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    text = read(os.path.join(REPO, m["file"]))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in the file exactly once" % m["id"])
                    copy = os.path.join(tmp, os.path.basename(m["file"]))
                    with open(copy, "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    rest = [os.path.join(REPO, part) if part.startswith(("tests/", "web/")) and part != m["file"] else part for part in m["test"].split()]
                    command = [copy if part == m["file"] else part for part in rest]
                    done = subprocess.run([shutil.which("node")] + command[1:], capture_output=True, text=True, timeout=180)
                    if done.returncode == 3:
                        self.skipTest("no browser: " + m["id"])
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"])


if __name__ == "__main__":
    unittest.main()
