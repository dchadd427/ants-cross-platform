"""The front page's height in an installed window and the screen numbers of "/?vp" (web/home/home.js: fitHeight and heightGuard, web/home/vp.js, web/home/home.css; docs/HOME_SCREEN.md "The page's height").

  - the rule and the guard RUN in node against a small fake window (tests/scripts/web_home_height_check.js): the smallest reading wins, a pinch, a field with the focus and a drop of more than a third change
    nothing, the limit goes when the box fits, the Room gets none; "/?vp" loads the numbers' script and a plain address does not, the box has every number as text and asks for nothing;
  - the numbers' script is under its budget and has no network and no storage of its own;
  - tests/data/home_height_mutants.json changes one thing at a time in a COPY of the files: the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
The page itself in a real browser (the imitated fault, the keyboard, a turn of the phone, the four widths) is tests/scripts/web_home_height_browser.js (opt-in, with pictures).
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

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
CHECK = os.path.join(HERE, "web_home_height_check.js")
VP = os.path.join(REPO, "web", "home", "vp.js")
NODE = shutil.which("node")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import VP_BUDGET, VP_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


class TheNumbersScript(unittest.TestCase):
    def test_it_is_under_its_budget(self):
        with open(VP, "rb") as f:
            data = f.read()
        self.assertLess(len(data), VP_BUDGET, "vp.js is %d bytes" % len(data))
        self.assertLess(len(gzip.compress(data, 9)), VP_GZIP_BUDGET)

    def test_it_has_no_network_no_storage_and_no_markup_of_its_own(self):
        with open(VP, encoding="utf-8") as f:
            code = re.sub(r"//[^\n]*", "", f.read())
        for word in ("fetch", "XMLHttpRequest", "WebSocket", "sendBeacon", "localStorage", "sessionStorage", "indexedDB", "cookie", "innerHTML", "insertAdjacentHTML", "document.write", "eval("):
            self.assertNotIn(word, code)


@unittest.skipUnless(NODE, "node is not installed: the page's height was NOT run (tests/scripts/web_home_height_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_rule_the_guard_and_the_readout(self):
        done = subprocess.run([NODE, CHECK], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" failed", done.stdout)


@unittest.skipUnless(NODE, "node is not installed: the mutants were NOT run")
class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_height_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the check fail, and the
    copy with nothing changed must pass"""
    FILES = ["web/home/home.js", "web/home/vp.js", "tests/scripts/web_home_height_check.js"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_height_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_height_mutants.")
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
        status, out = self.run_command(self.spec["baseline"]["test"])
        self.assertEqual(status, 0, out[-3000:])

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
