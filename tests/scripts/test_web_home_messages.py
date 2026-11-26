#!/usr/bin/env python3
"""The private messages of Home (web/home/messages.js and messages.css, the loader in web/home/home.js; docs/HUB_MESSAGES.md; run by ./run_tests.sh --fast and by the CI).

The Messages button in the chat's head, the panel that it opens (a sheet on a phone), the list of conversations, one conversation, Block, Delete and the report of a message. What this file checks:
  - tests/scripts/web_home_messages_check.js runs in node against a fake chat dock (the hub's frames come from the test, what the page sends goes into a list): who has the button, what the hub may send, every frame that the page
    sends, nothing drawn as sent until the hub's echo, what is on the screen is read, Block and Unblock, Delete asks twice, the report card, the words of every refusal, the keys, a phone's sheet;
  - the folder: its budget (raw and gzipped), ASCII, no markup, no storage, no network;
  - the files are in the Docker image's check, and the front page loads the part only for a person who is logged in;
  - tests/data/home_messages_mutants.json changes one thing at a time in a COPY of the files: the check must then fail (the last class of this file; tools/mutate.py runs the same list).
The pictures are made with the page in a real browser (tests/scripts/web_home_follows_dms_browser.js, opt-in, `--shots DIR`).
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
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_messages_check.js")
NODE = shutil.which("node")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import MESSAGES_BUDGET, MESSAGES_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class TheScripts(unittest.TestCase):
    def test_they_run_in_node(self):
        if not NODE:
            raise unittest.SkipTest("node is not installed")
        done = subprocess.run([NODE, CHECK, os.path.join(WEB, "lobby.html")], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"all \d+ checks hold")

    def test_they_are_under_their_budget_and_plain_ascii_and_never_write_markup_or_ask_the_browser_for_anything(self):
        raw = zipped = 0
        for path in ("web/home/messages.js", "web/home/messages.css"):
            with open(os.path.join(REPO, path), "rb") as f:
                data = f.read()
            self.assertIsNone(re.search(rb"[^\x00-\x7f]", data), path + " is not ASCII")
            raw += len(data)
            zipped += len(gzip.compress(data, 9))
        self.assertLess(raw, MESSAGES_BUDGET, "messages.js and messages.css are %d bytes" % raw)
        self.assertLess(zipped, MESSAGES_GZIP_BUDGET, "and %d bytes gzipped" % zipped)
        code = re.sub(r"//.*", "", read("web", "home", "messages.js"))
        for banned in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "localStorage", "sessionStorage", "indexedDB", "fetch(", "XMLHttpRequest", "WebSocket", "Notification", "serviceWorker", "eval("):
            self.assertNotIn(banned, code, "messages.js uses " + banned)


class TheWiring(unittest.TestCase):
    def test_the_front_page_loads_the_part_for_a_person_who_is_logged_in_only(self):
        home = read("web", "home", "home.js")
        self.assertIn("home/messages", home)
        self.assertRegex(home, r"function wantMessages\(\)")

    def test_the_files_are_in_the_image_check(self):
        ci = read(".github", "workflows", "ci.yml")
        for name in ("home/messages.js", "home/messages.css"):
            self.assertIn(name, ci)
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/", read("Dockerfile"))


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_messages_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the check fail, and the
    copy with nothing changed must pass"""
    FILES = (["web/lobby.html", "web/shell.html", "web/shapes.js", "web/rejoin_key.js", "web/alert_core.js", "web/home/home.js", "web/home/games.js", "web/home/alerts.js", "web/home/alerts.css", "web/home/chat.js", "web/home/messages.js", "web/home/messages.css",
              "tests/scripts/web_home_messages_check.js", "tests/scripts/web_page_harness.js"]
             + ["web/front/" + n for n in ("name_letters.js", "lobby_rules.js", "lobby_net.js", "lobby_chat.js", "rematch.js", "game_modes.js", "game_type.js", "room_fog.js", "room_sheet.js", "seat_menu.js", "map_picker.js", "player_name.js", "map_maker_link.js")])

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_messages_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_messages_mutants.")
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
