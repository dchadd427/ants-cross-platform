#!/usr/bin/env python3
"""The Room's two small overlays of the front page (web/front/seat_menu.js and web/front/room_sheet.js, web/lobby.html, docs/PLAY_IN_BROWSER.md "Your room"; run by ./run_tests.sh --fast and by CI).

The leader chooses a color's player in a short menu under the color's button (Open, Easy bot, Medium bot, Hard bot, Closed), and the room's rules (who can join, Fog of War, Direct connection) are in a card that the
Rules button opens. Neither knows the room. What this file checks:

  - the code RUNS in node (tests/scripts/web_room_menus_check.js: a small fake DOM; the room's own use of the two is run by the page checks, e.g. web_room_fog_check.js and web_name_check.js);
  - the files, where no run can look: the page loads each script and sheet in its place and makes each once, and the scripts are ES5 and put no text into markup;
  - tests/data/room_menus_mutants.json changes one thing at a time in a COPY of the files: the node check must then fail (the last class of this file; tools/mutate.py runs the same list).
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECK = os.path.join(REPO, "tests", "scripts", "web_room_menus_check.js")
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class ThePage(unittest.TestCase):
    def test_the_front_page_loads_each_script_and_sheet_once_and_makes_each_once(self):
        page = read("web", "lobby.html")
        for name in ("front/seat_menu.js", "front/room_sheet.js"):
            self.assertEqual(page.count('<script src="%s"></script>' % name), 1, name)
            self.assertLess(page.index(name), page.index("AntsHome.mount"), name + " comes before the page's own code")
        for name in ("front/seat_menu.css", "front/room_sheet.css"):
            self.assertEqual(page.count('href="%s"' % name), 1, name)
        self.assertEqual(len(re.findall(r"SeatMenu\.create\(", page)), 1, "one menu for all the colors")
        self.assertEqual(len(re.findall(r"RoomSheet\.create\(", page)), 1, "one sheet")

    def test_the_scripts_are_es5_and_put_no_text_into_markup(self):
        for name in ("seat_menu.js", "room_sheet.js"):
            code = read("web", "front", name)
            for token, why in ((r"=>", "an arrow function"), (r"\blet\b", "let"), (r"\bconst\b", "const"), (r"`", "a template literal"), (r"\.innerHTML", "innerHTML")):
                self.assertIsNone(re.search(token, code), name + " is ES5 and sets text with textContent only: it has " + why)



@unittest.skipUnless(NODE, "node is not installed: the Room's menu and sheet were NOT run (tests/scripts/web_room_menus_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_menu_and_the_sheet_against_a_fake_page(self):
        done = subprocess.run([NODE, CHECK, os.path.join(REPO, "web", "front")], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"ok \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/room_menus_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the check fail, and the copy
    with nothing changed must pass"""
    FILES = ["web/front/seat_menu.js", "web/front/room_sheet.js", "tests/scripts/web_room_menus_check.js"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "room_menus_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_room_menus_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the Room's menu and sheet were NOT run")
    def test_the_copy_with_nothing_changed_passes(self):
        for command in sorted({m["test"] for m in self.spec["mutants"]} | {self.spec["baseline"]["test"]}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the Room's menu and sheet were NOT run")
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
        self.assertEqual(missed, [], "the check did not see these changes")


if __name__ == "__main__":
    unittest.main()
