#!/usr/bin/env python3
"""The map maker's Fill a box (run by ./run_tests.sh --fast, with the other python tests of tests/scripts, and by the CI; docs/MAP_EDITOR.md, "Fill a box"): the patterns, the cells that cannot take the
thing, the cap of a fill, the change that puts it all in the history as one step, the box in the page and the switch in the tools. The AI's `fill_area` is held by test_web_mapmaker_ai.py and test_hub_ai.py.

  - the code is RUN (node, when it is installed): tests/scripts/web_mapmaker_fill_check.js, on real library maps with the real reader, blocks, editor and history and a small fake of the DOM;
  - the checks are shown able to fail: broken copies of the scripts (tests/data/mapmaker_fill_mutants.json, which tools/mutate.py runs too, with `--keep-going --jobs 1`) are each refused by them.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRONT = os.path.join(REPO, "web", "front")
CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_fill_check.js")
BAIL = os.path.join(REPO, "tests", "scripts", "web_mapmaker_bail.js")
NODE = shutil.which("node")
WATCHDOG_S = 60


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_check(front=None):
    """node runs the check on the real scripts (front=None: it must pass) or on a folder of BROKEN copies (front=DIR: it must fail, and the run ends at the first line that says so)"""
    args = [NODE] + (["--require", BAIL] if front else []) + [CHECK] + (["--front", front] if front else [])
    env = dict(os.environ)
    if front:
        env["ANTS_BAIL_WATCHDOG_S"] = str(WATCHDOG_S)
    return subprocess.run(args, capture_output=True, text=True, timeout=300 if not front else 3 * WATCHDOG_S, env=env)


@unittest.skipUnless(NODE, "node is not installed")
class TheFillChecks(unittest.TestCase):
    def test_the_checks_pass(self):
        done = run_check()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"fill check: \d+ checks, 0 failed")
        for part in ("A the patterns", "B which cells are skipped", "C the cap of one fill", "D one step", "E the box in the page", "F the switch in the tools"):
            self.assertIn("RUNNING: " + part, done.stdout)


@unittest.skipUnless(NODE, "node is not installed")
class TheChecksAreNotVacuous(unittest.TestCase):
    def test_the_checks_can_fail_on_the_broken_copies(self):
        """tests/data/mapmaker_fill_mutants.json (tools/mutate.py runs it too): each broken copy must fail the check"""
        with open(os.path.join(REPO, "tests", "data", "mapmaker_fill_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        self.assertGreaterEqual(len(ids), 50)
        with tempfile.TemporaryDirectory() as tmp:
            for name in os.listdir(FRONT):
                if name.endswith(".js"):
                    shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    name = os.path.basename(m["file"])
                    text = read(*m["file"].split("/"))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in %s exactly once" % (m["id"], name))
                    self.assertEqual(m["test"], "node tests/scripts/web_mapmaker_fill_check.js")
                    with open(os.path.join(tmp, name), "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    try:
                        done = run_check(tmp)
                    finally:
                        shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"] + "\n" + done.stdout[-600:] + done.stderr[-600:])
                    if "WATCHDOG:" in done.stderr:
                        self.assertRegex(done.stdout, r"\b(FAILED|ERROR|CRASHED): ", "the run hung with no failing line of the check for " + m["id"])


class ThePagesOwnCode(unittest.TestCase):
    def test_no_tag_in_the_page_and_the_image_carries_the_two_files(self):
        # nothing to run: the part is loaded by the app when it is needed, so a tag in the page would load it for everybody; and the CI's check of the image looks for both files
        html, ci = read("web", "mapmaker.html"), read(".github", "workflows", "ci.yml")
        for name in ("mapmaker_fill.js", "mapmaker_fill.css"):
            self.assertNotIn(name, html, "a lazily loaded file has no tag in the page")
            self.assertIn("front/" + name, ci, "the CI's check of the image names " + name)

    def test_words_are_put_in_with_text_only(self):
        # nothing to run: a name that comes from a map is only ever put in with textContent (docs/MAP_EDITOR.md); no run shows the one call that nobody thought of
        text = re.sub(r"(?m)^\s*//.*$|\s//\s.*$", "", read("web", "front", "mapmaker_fill.js"))
        self.assertNotRegex(text, r"innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\(|new Function|localStorage|sessionStorage|indexedDB|XMLHttpRequest|sendBeacon|fetch\(")


if __name__ == "__main__":
    unittest.main()
