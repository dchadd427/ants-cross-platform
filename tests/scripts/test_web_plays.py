#!/usr/bin/env python3
"""The play counter of the game page's replay mode (web/replay_plays.js and its call in web/shell.html; docs/REPLAYS.md "Watching a replay", docs/REPLAY_COUNTS.md; run by ./run_tests.sh --fast and by the CI).

A recording of the server's own store is one play when the page has played it for 10 seconds; the page sends one POST /counts/play/<id> for it, once a day for each recording in each browser. What this file checks:

  - the code RUNS in node (tests/scripts/web_plays_check.js: a fake clock, window, storage and fetch, and the page's own call, the block of web/shell.html between PLAYS_BEGIN and PLAYS_END): 10 seconds
    of playing and not 9.9; a pause, a jump, the end, a hidden tab, a computer that slept and a game that is not ready add nothing; once a day (two page views share the memory, a day later it counts again, the
    memory is written before the play is sent, 200 recordings at the most, none a day old, nothing trusted); a refused storage counts once for each page view and never throws; what is sent (a POST with no body
    and no cookies), a play that does not arrive is not sent again; nothing is sent or fetched for a match that is being played, for a file of your own or for an address that is no recording; the replay numbers that
    the page holds are the game's (include/ants_app/replay_view.hpp);
  - the files, where no run of the page's code can look: the page FETCHES the script (it is no <script> tag of any page: a visitor who does not watch a kept recording is not charged for it), the image has it,
    the local web build and the CI's list of the image's files name it, nginx serves it by the rule for scripts, and it is within its byte budget (tools/size_limits.py);
  - tests/data/replay_plays_mutants.json changes one thing at a time in a COPY of the files: the node check must then fail (the last class of this file; tools/mutate.py runs the same list).
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
sys.path.insert(0, os.path.join(REPO, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import size_limits  # noqa: E402
from test_web_wake_lock import Scripts, image_holds, location_of, words_of  # noqa: E402  (the one place of these helpers: the page's scripts, the image replayed from the Dockerfile, nginx's choice of a location)

SCRIPT = os.path.join(REPO, "web", "replay_plays.js")
CHECK = os.path.join(REPO, "tests", "scripts", "web_plays_check.js")
COMMAND = "node tests/scripts/web_plays_check.js web/replay_plays.js web/shell.html web/history_page.js include/ants_app/replay_view.hpp"
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class ThePageAndTheImage(unittest.TestCase):
    def test_no_page_loads_the_script_with_a_tag_and_the_game_page_has_the_block_that_fetches_it(self):
        for name in sorted(os.listdir(os.path.join(REPO, "web"))):
            if name.endswith(".html"):
                scripts = Scripts()
                scripts.feed(read("web", name))
                self.assertEqual([what for kind, what in scripts.found if kind == "src" and "replay_plays" in what], [], name + " loads the counter with a tag")
        shell = Scripts()
        shell.feed(read("web", "shell.html"))
        blocks = [what for kind, what in shell.found if kind == "inline" and "PLAYS_BEGIN" in what and "PLAYS_END" in what]
        self.assertEqual(len(blocks), 1, "the game page has the marked block that the node check runs, in one script")

    def test_the_image_has_the_file(self):
        with open(SCRIPT, "rb") as f:
            source = f.read()
        self.assertEqual(image_holds("replay_plays.js"), source, "the Dockerfile's COPY steps of web/ put web/replay_plays.js in /usr/share/nginx/html")

    def test_the_local_web_build_copies_it_and_the_ci_check_of_the_image_lists_it(self):
        copied = []
        for line in read("build_web.sh").splitlines():
            words = words_of(line)
            if words[:2] == ["cp", "-f"] and len(words) > 3:
                copied += [(word, words[-1]) for word in words[2:-1]]
        self.assertIn(("${SCRIPT_DIR}/web/replay_plays.js", "${DIST_DIR}/"), copied)
        listed = []
        for line in read(".github", "workflows", "ci.yml").splitlines():
            if "ls -l index.html" in line:
                listed += [word for command in words_of(line)[-1:] for word in command.split()]
        self.assertIn("replay_plays.js", listed, "the image check of the CI lists the files that the page needs")

    def test_nginx_serves_it_by_the_rule_for_scripts_that_revalidates(self):
        words, body = location_of(re.sub(r"(?m)#.*$", "", read("docker", "nginx.conf")), "/replay_plays.js")
        self.assertIsNotNone(words)
        self.assertIn('add_header Cache-Control "no-cache, must-revalidate" always;', body, "the location %s" % words)
        self.assertNotIn("expires", body)

    def test_it_is_within_its_download_budget(self):
        data = read("web", "replay_plays.js").encode("utf-8")
        self.assertLess(len(data), size_limits.PLAYS_BUDGET, "replay_plays.js is %d bytes" % len(data))
        self.assertLess(len(gzip.compress(data, 9)), size_limits.PLAYS_GZIP_BUDGET, "and %d bytes gzipped" % len(gzip.compress(data, 9)))


@unittest.skipUnless(NODE, "node is not installed: the play counter was NOT run (tests/scripts/web_plays_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_counter_against_fakes_for_the_clock_the_storage_the_door_and_the_game(self):
        done = subprocess.run([NODE, CHECK, SCRIPT, os.path.join(REPO, "web", "shell.html"), os.path.join(REPO, "web", "history_page.js"), os.path.join(REPO, "include", "ants_app", "replay_view.hpp")],
                              capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"plays: \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/replay_plays_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the check fail, and the copy
    with nothing changed must pass"""
    FILES = ["web/replay_plays.js", "web/shell.html", "web/history_page.js", "include/ants_app/replay_view.hpp", "tests/scripts/web_plays_check.js"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "replay_plays_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_plays_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    def test_the_list_is_the_one_of_the_check(self):
        self.assertEqual({m["test"] for m in self.spec["mutants"]} | {self.spec["baseline"]["test"]}, {COMMAND})

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the play counter were NOT run")
    def test_the_copy_with_nothing_changed_passes(self):
        status, out = self.run_command(COMMAND)
        self.assertEqual(status, 0, out[-3000:])

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the play counter were NOT run")
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
