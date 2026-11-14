#!/usr/bin/env python3
"""The game page's small hub socket (web/hub_here.js, docs/HOME_SCREEN.md "What the page tells the hub", docs/HUB.md "Presence"; run by ./run_tests.sh --fast and by the CI).

When a person is in a match, the players list of Home shows them under Playing. The front page's socket closes when it hands the visitor over to the game page, so the game page opens a small socket of its own to the
same door (/hub/ws, the same login and limits) and says only where its visitor is. What this file checks:

  - the page and the image: web/shell.html loads the script once and starts it once (guarded: a page without it is the page as it was), no other page names it, the Dockerfile, the local web build and the CI's list
    of the image's files carry it, it is small and plain ES5;
  - the code RUNS in node: tests/scripts/web_hub_here_check.js (a fake socket, a fake clock and a fake page: the note of the front page, a room that is in the public list or not, a guest, a game against the computer,
    the match over, the backoff, the pace, the page that leaves);
  - tests/data/hub_here_mutants.json changes one thing at a time in a COPY of the files: the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
"""
import gzip
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "web", "hub_here.js")
CHECK = os.path.join(REPO, "tests", "scripts", "web_hub_here_check.js")
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class ThePageAndTheImage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "hub_here.js")

    def test_the_game_page_loads_it_once_and_starts_it_once_and_no_other_page_names_it(self):
        shell = read("web", "shell.html")
        self.assertEqual(shell.count('<script src="hub_here.js"></script>'), 1)
        self.assertEqual(len(re.findall(r"AntsHubHere\.begin\(", shell)), 1)
        self.assertRegex(shell, r"if \(window\.AntsHubHere\) AntsHubHere\.begin\(\{ win: window, args: ANTS_ARGS, replay: ANTS_REPLAY_MODE, test: !!ANTS_TEST \}\)", "guarded, and given what the page has checked")
        self.assertLess(shell.index('<script src="hub_here.js"></script>'), shell.index("AntsHubHere.begin("), "loaded before it is started")
        for rel in (("web", "lobby.html"), ("web", "matches.html"), ("web", "match.html"), ("web", "mapmaker.html"), ("web", "admin.html"), ("web", "legal.html")):
            self.assertNotIn("hub_here", read(*rel), rel[-1])

    def test_the_start_comes_after_the_rematch_so_that_the_match_over_of_the_page_is_called_first(self):
        shell = read("web", "shell.html")
        self.assertLess(shell.index("AntsRematch.gamePage("), shell.index("AntsHubHere.begin("))

    def test_it_is_in_the_image_the_local_build_and_the_ci_list(self):
        self.assertIn("COPY web/hub_here.js /usr/share/nginx/html/hub_here.js\n", read("Dockerfile"))
        self.assertIn('cp -f "${SCRIPT_DIR}/web/hub_here.js" "${DIST_DIR}/"', read("build_web.sh"))
        self.assertIn(" rejoin_key.js hub_here.js death_map.js", read(".github", "workflows", "ci.yml"))

    def test_it_is_small_and_plain_es5(self):
        data = self.js.encode("utf-8")
        self.assertLess(len(data), 14000, "hub_here.js is %d bytes" % len(data))
        self.assertLess(len(gzip.compress(data, 9)), 5000)
        code = "\n".join(re.sub(r"\s//\s.*$", "", l) for l in self.js.split("\n") if not l.strip().startswith("//"))
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\b", r"\basync\b", r"\bawait\b"):
            self.assertIsNone(re.search(token, code), token)
        self.assertIn("'use strict';", self.js)

    def test_it_says_nothing_but_here_and_names_only_the_hubs_door_and_the_public_list(self):
        code = "\n".join(re.sub(r"\s//\s.*$", "", l) for l in self.js.split("\n") if not l.strip().startswith("//"))
        self.assertEqual(re.findall(r"t: '([a-z]+)'", code), ["here"])
        self.assertEqual(sorted(re.findall(r"'(/[a-z/]+)'", code)), ["/hub/ws", "/lobbies"])
        self.assertNotRegex(code, r"innerHTML|document\.|setItem|removeItem|localStorage|document\.cookie|sendBeacon|XMLHttpRequest|\beval\b|new Function")


@unittest.skipUnless(NODE, "node is not installed: the game page's hub socket was NOT run (tests/scripts/web_hub_here_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_socket_against_a_fake_socket_a_fake_clock_and_a_fake_page(self):
        done = subprocess.run([NODE, CHECK, SCRIPT], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"hub_here: \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/hub_here_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the copy with
    nothing changed must pass"""
    FILES = ["web/hub_here.js", "web/shell.html", "web/lobby.html", "web/matches.html", "web/match.html", "web/mapmaker.html", "web/admin.html", "web/legal.html", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml", "tests/scripts/test_web_hub_here.py", "tests/scripts/web_hub_here_check.js"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "hub_here_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_hub_here_mutants.")
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
        for command in sorted({m["test"] for m in self.spec["mutants"]} | {self.spec["baseline"]["test"]}):
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
        self.assertEqual(missed, [], "the checks did not see these changes")


if __name__ == "__main__":
    unittest.main()
