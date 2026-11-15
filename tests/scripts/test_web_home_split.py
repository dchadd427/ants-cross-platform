#!/usr/bin/env python3
"""The drag handles of Home (web/home/split.js and split.css, the loader in web/home/home.js, docs/HOME_SCREEN.md "The splitters"; run by ./run_tests.sh --fast and by the CI).

Two handles on a wide window (701 px and up): one between the Games list and the players list, one along the top of the chat dock; a phone has none. What this file checks:
  - tests/scripts/web_home_split_check.js runs in node (the limits, the keys, a drag with a fake pointer, the kept sizes, a storage that throws, a window that changes, a phone);
  - the folder: its budgets (raw and gzipped), ASCII, plain ES5, no markup, one storage key;
  - the style: wide windows only, the sizes are two properties of Home, the id of Home makes the chat's size win over the dock's own height, forced colors, reduced motion, a target wider than the line;
  - the files are in the Docker image's check and the hygiene of the folder (test_web_home_screen.py);
  - tests/data/home_split_mutants.json changes one thing at a time in a COPY of the files: the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
The pictures are made by hand with the page in a real browser (tests/scripts/web_home_split_browser.js, opt-in, `--shots DIR`).
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
HOME = os.path.join(REPO, "web", "home")
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_split_check.js")
NODE = shutil.which("node")
FILES = ("split.js", "split.css")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import SPLIT_BUDGET, SPLIT_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class TheScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "split.js")
        cls.css = read("web", "home", "split.css")
        cls.code = re.sub(r"//.*", "", cls.js)

    def test_it_runs_in_node(self):
        if not NODE:
            raise unittest.SkipTest("node is not installed")
        done = subprocess.run([NODE, CHECK], capture_output=True, text=True, timeout=120, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")

    def test_it_is_under_its_budgets(self):
        raw = zipped = 0
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            raw += len(data)
            zipped += len(gzip.compress(data, 9))
            self.assertIsNone(re.search(rb"[^\x00-\x7f]", data), name + " is not ASCII")
        self.assertLess(raw, SPLIT_BUDGET, "split.js and split.css are %d bytes" % raw)
        self.assertLess(zipped, SPLIT_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_the_script_is_plain_es5_with_no_markup_and_one_storage_key(self):
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\s+\w+\s*[{(]", r"\basync\b", r"\bawait\b"):
            self.assertIsNone(re.search(token, self.code), token)
        self.assertIn("'use strict';", self.js)
        self.assertNotRegex(self.code, r"innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function|XMLHttpRequest|fetch\(|WebSocket")
        self.assertEqual(re.findall(r"'(ants\.[a-z.0-9]+)'", self.code), ["ants.home.split.v1"])
        self.assertEqual(self.code.count("localStorage"), 1)

    def test_the_handles_are_separators_with_the_values_a_reader_needs(self):
        for word in ("'role', 'separator'", "'tabindex', '0'", "aria-orientation", "aria-valuenow", "aria-valuemin", "aria-valuemax", "aria-controls", "aria-label"):
            self.assertIn(word, self.js)
        self.assertIn("setPointerCapture", self.js)
        self.assertIn("var STEP = 16, BIG = 64;", self.js)
        self.assertIn("var SIDE_MIN = 240, CHAT_MIN = 120, CHAT_SHARE = 0.7;", self.js)
        self.assertIn("var WIDE = '(min-width: 701px)';", self.js)


class TheStyle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.css = read("web", "home", "split.css")

    def test_every_rule_that_moves_a_size_is_for_wide_windows_only(self):
        wide = self.css.split("@media (min-width: 701px) {", 1)[1]
        for rule in ("grid-template-columns: minmax(0, 1fr) var(--pl-w)", "height: var(--gc-h)", "cursor: col-resize", "cursor: row-resize"):
            self.assertIn(rule, wide)
            self.assertNotIn(rule, self.css.split("@media (min-width: 701px) {", 1)[0])
        self.assertIn(".sp { display: none; }", self.css, "a phone shows no handle")

    def test_the_size_of_the_chat_beats_the_docks_own_height_by_the_id_of_home(self):
        self.assertIn('#home[data-split-h] .gc[data-open="true"] { height: var(--gc-h); }', self.css)
        talk = read("web", "home", "talk.css")
        self.assertIn('.gc[data-open="true"]:has(.gc-form.gt-on.gt-tall) { height: clamp(', talk, "while the box has two rows, the dock's own height is the first size: the handle's rule has an id and wins")

    def test_the_target_is_wider_than_the_line_and_the_line_shows_on_hover_focus_and_drag(self):
        self.assertIn(".sp::before", self.css)
        self.assertIn(".sp:hover .sp-line, .sp:focus-visible .sp-line, .sp.sp-live .sp-line { background: var(--gold, #f6d058); }", self.css)
        self.assertIn("touch-action: none", self.css)
        self.assertIn("user-select: none", self.css)

    def test_forced_colors_and_reduced_motion(self):
        self.assertIn("@media (forced-colors: active)", self.css)
        self.assertIn("Highlight", self.css)
        self.assertIn("@media (prefers-reduced-motion: reduce)", self.css)
        self.assertNotRegex(self.css, r"https?://|url\(")


class TheLoader(unittest.TestCase):
    def test_the_files_are_in_the_image_check_and_the_folder_is_copied_whole(self):
        ci = read(".github", "workflows", "ci.yml")
        self.assertIn("home/split.js home/split.css", ci)
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/", read("Dockerfile"))


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_split_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = ["tools/size_limits.py", "web/home/home.js", "web/home/split.js", "web/home/split.css", "web/home/talk.css", "tests/scripts/test_web_home_split.py", "tests/scripts/web_home_split_check.js",
             "Dockerfile", ".github/workflows/ci.yml"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_split_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_split_mutants.")
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
