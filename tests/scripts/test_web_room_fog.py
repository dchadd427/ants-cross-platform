#!/usr/bin/env python3
"""The Room's "Fog of War" switch (web/front/room_fog.js, web/lobby.html, docs/PLAY_MATCH_SETUP.md "Fog of War"; network protocol 28; run by ./run_tests.sh --fast and by CI).

The leader of a lobby room switches Fog of War on and off in the Rules sheet; the plan that the page sends carries it (the last byte), a guest reads the switch and the Rules button says Fog, a room with computer players is asked first
(the Games list's own notice, with a button), and the colors' menus have their bot choices off while fog is on. What this file checks:

  - the code RUNS in the fake browser of tests/scripts/web_page_harness.js (tests/scripts/web_room_fog_check.js: the switch, the plan bytes, the guest, the question, the menus, the pending pick);
  - the files, where no run can look: the page loads the script in its place and makes the switch once;
  - tests/data/room_fog_mutants.json changes one thing at a time in a COPY of the files: the node checks must then fail (the last class of this file runs the page's mutants, those that need only node;
    tools/mutate.py runs the whole list, with the C++ ones: `python3 tools/mutate.py tests/data/room_fog_mutants.json --keep-going --jobs 1`).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
CHECK = os.path.join(REPO, "tests", "scripts", "web_room_fog_check.js")
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class ThePage(unittest.TestCase):
    def test_the_front_page_loads_the_script_once_and_makes_the_switch_in_the_sheets_fog_row(self):
        page = read("web", "lobby.html")
        self.assertEqual(page.count('<script src="front/room_fog.js"></script>'), 1)
        self.assertNotIn('room_fog.css', page, "the switch is the Direct connection switch of front/lobby_direct.css, in a row of the Rules sheet: it has no sheet of its own")
        self.assertLess(page.index("front/game_type.js"), page.index("front/room_fog.js"), "the script comes after the game type's, before the page's own code")
        self.assertLess(page.index("front/room_fog.js"), page.index("AntsRoomFog.create"))
        self.assertEqual(len(re.findall(r"AntsRoomFog\.create\(", page)), 1, "the switch is made once")

    def test_the_script_is_es5_and_puts_no_text_into_markup(self):
        code = read("web", "front", "room_fog.js")
        for token, why in ((r"=>", "an arrow function"), (r"\blet\b", "let"), (r"\bconst\b", "const"), (r"`", "a template literal"), (r"\.innerHTML", "innerHTML")):
            self.assertIsNone(re.search(token, code), "room_fog.js is ES5 and sets text with textContent only: it has " + why)


@unittest.skipUnless(NODE, "node is not installed: the Room's Fog of War switch was NOT run (tests/scripts/web_room_fog_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_switch_in_the_fake_browser(self):
        done = subprocess.run([NODE, CHECK, os.path.join(REPO, "web", "lobby.html")], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the page's mutants of tests/data/room_fog_mutants.json (those whose test is a node command) on a COPY of the files, one at a time: each must make its check fail, and the copy with nothing changed
    must pass"""
    @classmethod
    def setUpClass(cls):
        import mutant_specs  # noqa: E402  (here and not at the top: the copies in which the mutants run have this file but not that one)
        spec = mutant_specs.load(os.path.join(REPO, "tests", "data", "room_fog_mutants.json"))
        # the page's mutants whose check is a node command that needs no map of the library (the rules check reads Original-Ants: tools/mutate.py runs those, from the repository)
        cls.mutants = [m for m in spec["mutants"] if "build" not in m and "web_lobby_rules_check" not in m["test"]]
        cls.folder = tempfile.mkdtemp(prefix="ants_room_fog_mutants.")
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

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the Room's Fog of War switch were NOT run")
    def test_the_copy_with_nothing_changed_passes(self):
        for command in sorted({m["test"] for m in self.mutants}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the Room's Fog of War switch were NOT run")
    def test_every_mutant_is_seen(self):
        ids = [m["id"] for m in self.mutants]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        self.assertGreaterEqual(len(self.mutants), 20)
        missed = []
        for m in self.mutants:
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
