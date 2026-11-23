#!/usr/bin/env python3
"""The Room on a phone held upright: the four colors as tiles and the leader's sheet for a color (web/lobby.html, web/front/seat_menu.js, web/home/room.css, docs/HOME_SCREEN.md "The Room"; run by ./run_tests.sh --fast and by CI).

On a phone upright (a window up to 700 px wide and taller than 500 px) a color is a tile of 56 px or more with its ant, name, team and status, the header is one row, and the bar has no Quick host and Quick join while a room is held; the leader taps
a tile for the color's sheet (Team 1 and Team 2, the color to move to, Remove with its question, or the five choices of a color that no person holds); a guest's tile is a picture. Every other window keeps the rows. What this file checks:

  - the page RUNS in the fake browser of tests/scripts/web_page_harness.js (tests/scripts/web_room_tiles_check.js: which window has tiles, the tiles, the sheet's presses against the desktop's controls byte for byte, the sheet when the room
    changes); what the tile and the sheet do by themselves is in tests/scripts/web_room_menus_check.js (test_web_room_menus.py), and how the room looks in a real browser at 390 x 844 and 390 x 664 in the opt-in
    tests/scripts/web_room_in_home_browser.js;
  - tests/data/room_tiles_mutants.json changes one thing at a time in a COPY of the files: the node check must then fail (the last class of this file; `python3 tools/mutate.py tests/data/room_tiles_mutants.json --keep-going --jobs 1` runs
    the same list).
"""
import json
import os
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECK = os.path.join(REPO, "tests", "scripts", "web_room_tiles_check.js")
NODE = shutil.which("node")


@unittest.skipUnless(NODE, "node is not installed: the Room on a phone upright was NOT run (tests/scripts/web_room_tiles_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_tiles_and_the_sheet_in_the_fake_browser(self):
        done = subprocess.run([NODE, CHECK, os.path.join(REPO, "web", "lobby.html")], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/room_tiles_mutants.json on a COPY of the files, one at a time: each must make the check fail, and the copy with nothing changed must pass"""

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "room_tiles_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_room_tiles_mutants.")
        shutil.copytree(os.path.join(REPO, "web"), os.path.join(cls.folder, "web"), ignore=shutil.ignore_patterns("*.png", "*.webp", "*.ttf", "*.wasm", "*.data", "__pycache__"))
        shutil.copytree(os.path.join(REPO, "tests", "scripts"), os.path.join(cls.folder, "tests", "scripts"), ignore=shutil.ignore_patterns("__pycache__"))
        os.makedirs(os.path.join(cls.folder, "tests", "data"))
        for name in ("tests/data/lobby_messages.txt", "tests/data/home_games_thumbs.json", "Community-Maps/previews.json", "tools/size_limits.py", "include/ants_net/flood.hpp"):
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the Room on a phone upright were NOT run")
    def test_the_copy_with_nothing_changed_passes(self):
        status, out = self.run_command(self.spec["baseline"]["test"])
        self.assertEqual(status, 0, out[-3000:])

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the Room on a phone upright were NOT run")
    def test_every_mutant_is_seen(self):
        mutants = self.spec["mutants"]
        ids = [m["id"] for m in mutants]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        missed = []
        for m in mutants:
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
        self.assertEqual(missed, [], "the check did not see these changes")


if __name__ == "__main__":
    unittest.main()
