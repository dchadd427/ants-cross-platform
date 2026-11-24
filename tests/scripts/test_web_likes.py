#!/usr/bin/env python3
"""The plays and the hearts of the match pages (web/replay_likes.js, the sorts of web/history_page.js, and their wiring in web/matches.html and web/match.html; docs/REPLAYS.md "The match pages",
docs/REPLAY_COUNTS.md; run by ./run_tests.sh --fast and by the CI).

A card of the list and the result of a match page show how often a replay was played and how many people like it; a logged-in visitor can like it with a heart. What this file checks:

  - the code RUNS in node (tests/scripts/web_likes_check.js, on a strict fake of the DOM that refuses innerHTML, a fake door and a fake clock): the counts (0, 1, 1,234, "1 play", whatever is not a whole number is
    0), the cards and the result (a heart that is a button, a plain count for a recording that is gone or older, the dim zeros, the count that a sort brightens), the one look at the door (ids, at most 50, likes
    that are off), a tap (the heart changes at once, the token, the answer's count is the one shown, one call at a time, the heart goes back with one plain line for every way a call can fail, a token that is
    refused is asked for once more), and a visitor who is not logged in (the card with the way in, and no call); and the sorts "Most watched" and "Most liked" (tests/scripts/web_history_check.js);
  - the files, where no run of the page's code can look: both pages load the script before the code that calls it, the image has it, the local web build and the CI's list of the image's files name it, and it is
    within its byte budget (tools/size_limits.py); that every file a page loads is in the image is also checked by test_web_image_files.py;
  - what a real browser shows (the look of the cards, the match page, the Sort box that is not cut, the way in, the sorts' addresses) is in the opt-in check tests/scripts/web_matches_check.py, part "likes"
    (tests/scripts/test_web_matches.sh --only likes);
  - tests/data/replay_likes_mutants.json changes one thing at a time in a COPY of the files: the node checks must then fail (the last class of this file; tools/mutate.py runs the same list).
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
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import size_limits  # noqa: E402
from test_web_wake_lock import Scripts, image_holds, words_of  # noqa: E402  (the one place of these helpers: the page's scripts, the image replayed from the Dockerfile)

SCRIPT = os.path.join(REPO, "web", "replay_likes.js")
LIKES_COMMAND = "node tests/scripts/web_likes_check.js web/replay_likes.js web/history_page.js"
HISTORY_COMMAND = "node tests/scripts/web_history_check.js web/history_page.js"
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class ThePagesAndTheImage(unittest.TestCase):
    def test_both_pages_load_the_script_once_before_the_inline_script_that_calls_it(self):
        for name in ("matches.html", "match.html"):
            scripts = Scripts()
            scripts.feed(read("web", name))
            loaded = [i for i, (kind, what) in enumerate(scripts.found) if kind == "src" and what.split("?")[0] == "replay_likes.js"]
            called = [i for i, (kind, what) in enumerate(scripts.found) if kind == "inline" and "AntsLikes" in what]
            self.assertEqual(len(loaded), 1, name + " loads replay_likes.js once")
            self.assertEqual(len(called), 1, name + " calls it in one script")
            self.assertLess(loaded[0], called[0], name + ": loaded before it is called")

    def test_no_other_page_loads_it(self):
        for name in sorted(os.listdir(os.path.join(REPO, "web"))):
            if name.endswith(".html") and name not in ("matches.html", "match.html"):
                scripts = Scripts()
                scripts.feed(read("web", name))
                self.assertEqual([what for kind, what in scripts.found if kind == "src" and "replay_likes" in what], [], name)

    def test_the_image_has_the_file(self):
        with open(SCRIPT, "rb") as f:
            source = f.read()
        self.assertEqual(image_holds("replay_likes.js"), source, "the Dockerfile's COPY steps of web/ put web/replay_likes.js in /usr/share/nginx/html")

    def test_the_local_web_build_copies_it_and_the_ci_check_of_the_image_lists_it(self):
        copied = []
        for line in read("build_web.sh").splitlines():
            words = words_of(line)
            if words[:2] == ["cp", "-f"] and len(words) > 3:
                copied += [(word, words[-1]) for word in words[2:-1]]
        self.assertIn(("${SCRIPT_DIR}/web/replay_likes.js", "${DIST_DIR}/"), copied)
        listed = []
        for line in read(".github", "workflows", "ci.yml").splitlines():
            if "ls -l index.html" in line:
                listed += [word for command in words_of(line)[-1:] for word in command.split()]
        self.assertIn("replay_likes.js", listed, "the image check of the CI lists the files that the page needs")

    def test_it_is_within_its_download_budget(self):
        data = read("web", "replay_likes.js").encode("utf-8")
        self.assertLess(len(data), size_limits.LIKES_BUDGET, "replay_likes.js is %d bytes" % len(data))
        self.assertLess(len(gzip.compress(data, 9)), size_limits.LIKES_GZIP_BUDGET, "and %d bytes gzipped" % len(gzip.compress(data, 9)))


@unittest.skipUnless(NODE, "node is not installed: the plays and hearts were NOT run (tests/scripts/web_likes_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_hearts_against_a_strict_dom_a_fake_door_and_a_fake_clock(self):
        done = subprocess.run([NODE, os.path.join(REPO, "tests", "scripts", "web_likes_check.js"), SCRIPT, os.path.join(REPO, "web", "history_page.js")], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"likes: \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/replay_likes_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make a check fail, and the copy
    with nothing changed must pass"""
    FILES = ["web/replay_likes.js", "web/history_page.js", "tests/scripts/web_likes_check.js", "tests/scripts/web_history_check.js"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "replay_likes_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_likes_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    def test_the_list_is_the_one_of_the_checks(self):
        self.assertEqual({m["test"] for m in self.spec["mutants"]} | {b["test"] for b in self.spec["baseline"]}, {LIKES_COMMAND, HISTORY_COMMAND})

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the hearts were NOT run")
    def test_the_copy_with_nothing_changed_passes(self):
        for command in (LIKES_COMMAND, HISTORY_COMMAND):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the hearts were NOT run")
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
