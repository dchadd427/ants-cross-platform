#!/usr/bin/env python3
"""The sheet "Your name", the guest's chip and its note (web/home/names.js and names.css, web/front/name_parts.css, the loader in web/home/home.js, the chat menu's button in web/home/chat.js,
docs/HUB_NICKNAMES.md "The page"; run by ./run_tests.sh --fast and by the CI).

A person who is logged in chooses a nickname and a tag; a guest types a name that this browser keeps. What this file checks:
  - tests/scripts/web_home_names_check.js runs in node, in the fake browser of tests/scripts/web_page_harness.js with a fake dock: the words under a field for every word that the hub says, the guest's
    chip and its note (shown once, never again after a name or a close), the guest's sheet and the login's sheet (what is asked, what Save sends, what is heard, what is locked), and that nothing is markup;
  - the folder: its budgets (raw and gzipped) and that the scripts are plain ASCII (a letter is written as an escape);
  - the wiring: the front page loads the sheet after the dock, the chat menu says which button opens it, both pages that show names load the letters first, the files are in the image and its check;
  - tests/data/home_names_mutants.json changes one thing at a time in a COPY of the files: the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
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
WEB = os.path.join(REPO, "web")
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_names_check.js")
NODE = shutil.which("node")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import NAMES_BUDGET, NAMES_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def sizes(paths):
    raw = zipped = 0
    for path in paths:
        with open(os.path.join(REPO, path), "rb") as f:
            data = f.read()
        assert re.search(rb"[^\x00-\x7f]", data) is None, path + " is not ASCII"
        raw += len(data)
        zipped += len(gzip.compress(data, 9))
    return raw, zipped


class TheScripts(unittest.TestCase):
    def test_they_run_in_node(self):
        if not NODE:
            raise unittest.SkipTest("node is not installed")
        done = subprocess.run([NODE, CHECK, os.path.join(WEB, "lobby.html")], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")

    def test_they_are_under_their_budgets_and_plain_ascii(self):
        raw, zipped = sizes(["web/home/names.js", "web/home/names.css"])
        self.assertLess(raw, NAMES_BUDGET, "names.js and names.css are %d bytes" % raw)
        self.assertLess(zipped, NAMES_GZIP_BUDGET, "and %d bytes gzipped" % zipped)
        sizes(["web/front/name_parts.css", "web/front/name_letters.js"])

    def test_the_check_file_is_plain_ascii(self):
        sizes(["tests/scripts/web_home_names_check.js"])

    def test_the_sheet_makes_no_markup_and_asks_the_browser_for_nothing_but_its_own_storage(self):
        code = read("web", "home", "names.js")
        self.assertNotRegex(code, r"innerHTML|outerHTML|insertAdjacentHTML|\beval\b|new Function|document\.write|XMLHttpRequest|\bfetch\(|WebSocket|sessionStorage|document\.cookie")
        self.assertEqual(re.findall(r"""setItem\((\w+),""", code), ["NOTE_KEY"], "the one key that the sheet writes: the note that never comes back (the guest's name is kept by front/player_name.js)")


class TheWiring(unittest.TestCase):
    def test_the_front_page_loads_the_sheet_after_the_dock_and_the_letters_before_the_scripts_that_read_them(self):
        home, lobby = read("web", "home", "home.js"), read("web", "lobby.html")
        self.assertEqual(home.count("lazy('home/names'"), 1)
        self.assertLess(home.index("wantMessages(); wantNames();"), home.index("function wantNames()"))
        self.assertLess(lobby.index('<script src="front/name_letters.js"></script>'), lobby.index('<script src="front/lobby_net.js"></script>'))
        self.assertLess(lobby.index('<script src="front/name_letters.js"></script>'), lobby.index('<script src="front/player_name.js"></script>'))
        self.assertIn('<link rel="stylesheet" href="front/name_parts.css">', lobby)

    def test_the_game_page_and_the_history_pages_load_the_letters_too(self):
        shell = read("web", "shell.html")
        self.assertIn('<script src="front/name_letters.js"></script>', shell)
        for page in ("match.html", "matches.html"):
            self.assertIn('<script src="front/name_letters.js"></script>', read("web", page), page)

    def test_the_chat_menu_has_a_button_that_the_sheet_hears(self):
        chat = read("web", "home", "chat.js")
        self.assertIn("tapped('names')", chat)
        self.assertIn("if (kind === 'names') open();", read("web", "home", "names.js"))

    def test_the_files_are_in_the_image_and_its_check(self):
        ci = read(".github", "workflows", "ci.yml")
        for name in ("home/names.js", "home/names.css", "front/name_parts.css", "front/name_letters.js"):
            self.assertIn(name, ci)
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/", read("Dockerfile"))


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_names_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = (["tools/size_limits.py", "tools/ci_light.sh", "web/lobby.html", "web/shell.html", "web/shapes.js", "web/rejoin_key.js", "web/alert_core.js", "web/home/home.js", "web/home/games.js", "web/home/alerts.js", "web/home/alerts.css", "web/home/chat.js",
              "web/home/names.js", "web/home/names.css", "tests/scripts/test_web_home_names.py", "tests/scripts/web_home_names_check.js", "tests/scripts/web_page_harness.js", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml"]
             + ["web/front/" + n for n in ("name_letters.js", "lobby_rules.js", "lobby_net.js", "lobby_chat.js", "rematch.js", "game_modes.js", "game_type.js", "room_fog.js", "room_sheet.js", "seat_menu.js", "map_picker.js", "player_name.js", "map_maker_link.js", "lobby_direct.js")])

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_names_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_names_mutants.")
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
