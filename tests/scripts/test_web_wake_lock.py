#!/usr/bin/env python3
"""The screen that stays on while a match is played and while a recording plays (web/wake_lock.js, docs/PLAY_IN_BROWSER.md "The screen stays on"; run by ./run_tests.sh --fast and by the CI).

The game page asks the browser for a Screen Wake Lock (navigator.wakeLock) so that a phone does not turn its screen off in the middle of a match or a replay. What this file checks:

  - the code RUNS in node (tests/scripts/web_wake_lock_check.js: a fake wake lock, document, window and game, and the page's own call, the block of web/shell.html between WAKE_LOCK_BEGIN and
    WAKE_LOCK_END): wanted while a match runs or a recording plays, let go on pause, at the end and at the results, asked for again on play and when the page is visible again, nothing without the API,
    a refused request or release swallowed, a page that is left or comes back from the cache;
  - the files, where no run of the page's code can look: the page loads the script and names it once, the image, the local web build and the CI's list of the image's files carry it (a page that does not
    get the script turns the feature off without a word, by design, so nothing that runs the code notices: the browser check is the run), nginx serves it by the rule for scripts, and it is small and
    plain ES5 (tools/size_limits.py); the replay numbers that it holds are the game's (include/ants_app/replay_view.hpp);
  - the opt-in browser check (tests/scripts/web_wake_lock_check.py, tests/scripts/test_web_wake_lock.sh) takes a bad command line as one;
  - tests/data/wake_lock_mutants.json changes one thing at a time in a COPY of the files: the node check must then fail (the last class of this file; tools/mutate.py runs the same list).
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
import size_limits  # noqa: E402

SCRIPT = os.path.join(REPO, "web", "wake_lock.js")
CHECK = os.path.join(REPO, "tests", "scripts", "web_wake_lock_check.js")
BROWSER_CHECK = os.path.join(REPO, "tests", "scripts", "web_wake_lock_check.py")
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def location_of(conf, path):
    """The words and the body of the location that nginx picks for `path`: an exact one, else the longest prefix that is `^~`, else the first regular expression that matches, else the longest prefix."""
    blocks = []
    for m in re.finditer(r"^\s*location\s+(.*?)\s*\{", conf, re.M):
        depth, i = 1, m.end()
        while depth:
            depth += (conf[i] == "{") - (conf[i] == "}")
            i += 1
        blocks.append((m.group(1), conf[m.end():i - 1]))
    prefixes = []
    for words, body in blocks:
        parts = words.split()
        if parts[0] == "=" and parts[1] == path:
            return words, body
        if parts[0] == "^~" and path.startswith(parts[1]):
            prefixes.append((len(parts[1]), words, body, True))
        elif parts[0] not in ("=", "~", "~*") and not parts[0].startswith("@") and path.startswith(parts[0]):
            prefixes.append((len(parts[0]), words, body, False))
    best = max(prefixes, default=None, key=lambda p: p[0])
    if best and best[3]:
        return best[1], best[2]
    for words, body in blocks:
        parts = words.split(None, 1)
        if parts[0] in ("~", "~*"):
            pattern = parts[1].strip('"')
            try:
                if re.search(pattern, path, re.I if parts[0] == "~*" else 0):
                    return words, body
            except re.error:
                continue
    return (best[1], best[2]) if best else (None, None)


class ThePageAndTheImage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "wake_lock.js")

    def test_the_game_page_loads_the_script_once_and_calls_it_in_the_block_the_check_runs_and_no_other_page_names_it(self):
        shell = read("web", "shell.html")
        tag = '<script src="wake_lock.js"></script>'
        self.assertEqual(shell.count(tag), 1)
        self.assertEqual(shell.count("// WAKE_LOCK_BEGIN"), 1)
        self.assertEqual(shell.count("// WAKE_LOCK_END"), 1)
        self.assertLess(shell.index(tag), shell.index("// WAKE_LOCK_BEGIN"), "loaded before it is called")
        for rel in (("web", "lobby.html"), ("web", "matches.html"), ("web", "match.html"), ("web", "mapmaker.html"), ("web", "admin.html"), ("web", "legal.html")):
            self.assertNotIn("wake_lock", read(*rel), rel[-1])

    def test_it_is_in_the_image_the_local_build_and_the_ci_list(self):
        self.assertIn("COPY web/wake_lock.js /usr/share/nginx/html/wake_lock.js\n", read("Dockerfile"))
        self.assertIn('cp -f "${SCRIPT_DIR}/web/wake_lock.js" "${DIST_DIR}/"', read("build_web.sh"))
        self.assertIn(" game_bar.js wake_lock.js rejoin_key.js", read(".github", "workflows", "ci.yml"))

    def test_nginx_serves_it_by_the_rule_for_scripts_that_revalidates(self):
        words, body = location_of(re.sub(r"(?m)#.*$", "", read("docker", "nginx.conf")), "/wake_lock.js")
        self.assertIsNotNone(words)
        self.assertIn('add_header Cache-Control "no-cache, must-revalidate" always;', body, "the location %s" % words)
        self.assertNotIn("expires", body)

    @unittest.skipUnless(NODE, "node is not installed: the replay numbers of the script were NOT compared with the game's")
    def test_the_replay_numbers_are_the_games(self):
        header = read("include", "ants_app", "replay_view.hpp")
        game = {name: int(number) for name, number in re.findall(r"^\s*(Playing|Jumping)\s*=\s*(\d+)", header, re.M)}
        done = subprocess.run([NODE, "-e", "console.log(JSON.stringify(require(process.argv[1]).STATE))", SCRIPT], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(json.loads(done.stdout), {"playing": game["Playing"], "jumping": game["Jumping"]})

    def test_it_is_small_ascii_and_plain_es5(self):
        data = self.js.encode("utf-8")
        self.assertTrue(all(b < 128 for b in data), "ASCII")
        self.assertLess(len(data), size_limits.WAKE_LOCK_BUDGET, "wake_lock.js is %d bytes" % len(data))
        self.assertLess(len(gzip.compress(data, 9)), size_limits.WAKE_LOCK_GZIP_BUDGET, "and %d bytes gzipped" % len(gzip.compress(data, 9)))
        code = "\n".join(re.sub(r"\s//\s.*$", "", line) for line in self.js.split("\n") if not line.strip().startswith("//"))
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\b", r"\basync\b", r"\bawait\b"):
            self.assertIsNone(re.search(token, code), token)
        self.assertIn("'use strict';", self.js)


@unittest.skipUnless(NODE, "node is not installed: the screen lock was NOT run (tests/scripts/web_wake_lock_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_lock_against_fakes_for_the_browser_the_page_and_the_game(self):
        done = subprocess.run([NODE, CHECK, SCRIPT, os.path.join(REPO, "web", "shell.html")], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"wake_lock: \d+ checks, 0 failed")


class TheBrowserCheck(unittest.TestCase):
    def test_a_bad_command_line_is_status_2_and_no_browser_is_started_for_it(self):
        done = subprocess.run([sys.executable, BROWSER_CHECK, "--only", "no-such-part"], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/wake_lock_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the check fail, and the copy
    with nothing changed must pass"""
    FILES = ["web/wake_lock.js", "web/shell.html", "tests/scripts/web_wake_lock_check.js"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "wake_lock_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_wake_lock_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the screen lock were NOT run")
    def test_the_copy_with_nothing_changed_passes(self):
        for command in sorted({m["test"] for m in self.spec["mutants"]} | {self.spec["baseline"]["test"]}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the screen lock were NOT run")
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
