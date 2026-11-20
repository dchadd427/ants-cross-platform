#!/usr/bin/env python3
"""The Room's "Direct connection" switch (web/front/lobby_direct.js, docs/P2P_HINTS.md; run by ./run_tests.sh --fast and by CI).

The switch is this player's own, off by default; turning it on the first time is explained in plain words (the true risk: the other players can see the IP address) and the switch changes only on the answer.
What this file checks:

  - the code RUNS in node (tests/scripts/web_lobby_direct_check.js: a small fake DOM and a fake storage);
  - the files, where no run of the page's code can look: the front page has the place for the switch, loads its script and its sheet, and the key that it writes is the one that the game page's p2p.js reads;
  - tests/data/lobby_direct_mutants.json changes one thing at a time in a COPY of the files: the node check must then fail (the last class of this file; tools/mutate.py runs the same list).
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "web", "front", "lobby_direct.js")
CHECK = os.path.join(REPO, "tests", "scripts", "web_lobby_direct_check.js")
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class ThePage(unittest.TestCase):
    def test_the_front_page_has_the_place_loads_the_script_and_the_sheet_and_makes_the_switch_from_the_players_storage(self):
        page = read("web", "lobby.html")
        self.assertEqual(page.count('<div id="direct"></div>'), 1)
        self.assertEqual(page.count('<script src="front/lobby_direct.js"></script>'), 1)
        self.assertEqual(page.count('href="front/lobby_direct.css"'), 1)
        call = re.search(r"AntsLobbyDirect\.create\(\{[^}]*\}\)", page)
        self.assertIsNotNone(call, "the page makes the switch")
        self.assertIn("storage: storage", call.group(0), "from the same storage as the leader's last game type")
        self.assertIn("rtc: !!window.RTCPeerConnection", call.group(0), "and only where the browser has WebRTC")

    def test_the_key_is_the_one_that_the_game_page_reads(self):
        self.assertIn("var KEY = 'ants.direct'", read("web", "front", "lobby_direct.js"))
        self.assertIn("var SETTING = 'ants.direct';", read("web", "p2p.js"))

    def test_the_switch_is_off_by_default_in_the_games_arguments(self):
        shell = read("web", "shell.html")
        self.assertIn("else if (window.AntsP2p && window.AntsP2p.directSetting()) out.args.push('--direct', 'on');", shell)
        self.assertNotIn("'--direct', 'off'", shell, "off is what nothing says: no argument")


@unittest.skipUnless(NODE, "node is not installed: the Room switch was NOT run (tests/scripts/web_lobby_direct_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_switch_against_a_fake_page_and_storage(self):
        done = subprocess.run([NODE, CHECK, SCRIPT], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"ok \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/lobby_direct_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the check fail, and the copy
    with nothing changed must pass"""
    FILES = ["web/front/lobby_direct.js", "tests/scripts/web_lobby_direct_check.js"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "lobby_direct_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_lobby_direct_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the Room switch was NOT run")
    def test_the_copy_with_nothing_changed_passes(self):
        for command in sorted({m["test"] for m in self.spec["mutants"]} | {self.spec["baseline"]["test"]}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the Room switch was NOT run")
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
