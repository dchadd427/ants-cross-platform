#!/usr/bin/env python3
"""The touch buttons for the game's hotkeys (docs/TOUCH.md "The hotkey buttons"; run by ./run_tests.sh --fast and by the CI). A phone has no Ctrl+A, so the game page makes a few real buttons
(web/touch_keys.js, web/touch_keys.css, called by web/shell.html) that send the key events of a keyboard. What the page can say about them without a browser is checked:

  - the script RUNS with fakes for the page (node, when it is installed: tests/scripts/web_touch_keys_check.js): every button's events in a keyboard's order, one press one key, the cases that hide
    the buttons (and send nothing), the room that stays while they hide, a key that is physically down, None's dimming, the list under More, the remembered setting, the placement classes;
  - the page's call RUNS as the page writes it (tests/scripts/web_touch_keys_wiring_check.js): the touch rule, the recording mode, the game's probes, the storage and what the settings menu is handed;
  - the mutants (tests/data/touch_keys_mutants.json): each broken copy of the script, the page's call or the menu is noticed by those checks;
  - the page's layout round (run as the page writes it, in the wiring check): the buttons first, then the chrome, then the fit in what the stage's padding leaves;
  - the size budgets and the files that the image and the build carry (a list of names: no run can tell a missing COPY line);
  - the layer under the list, run in Chromium when the machine has it (tests/scripts/web_touch_keys_shield_check.js): it covers the whole window after the page is scrolled;
  - the browser check (tests/scripts/web_touch_keys_browser_check.js, opt-in: tests/scripts/test_web_touch_keys.sh): its skip without a page.
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
import size_limits  # noqa: E402

SHELL = os.path.join(REPO, "web", "shell.html")
SCRIPT = os.path.join(REPO, "web", "touch_keys.js")
STYLE = os.path.join(REPO, "web", "touch_keys.css")
CHECK = os.path.join(REPO, "tests", "scripts", "web_touch_keys_check.js")
WIRING = os.path.join(REPO, "tests", "scripts", "web_touch_keys_wiring_check.js")
BROWSER_RUNNER = os.path.join(REPO, "tests", "scripts", "test_web_touch_keys.sh")
MUTANTS = os.path.join(REPO, "tests", "data", "touch_keys_mutants.json")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


class ThePage(unittest.TestCase):
    def setUp(self):
        self.page = read(SHELL)

    def test_the_image_and_the_build_carry_the_files(self):
        for name in ("touch_keys.js", "touch_keys.css"):
            self.assertIn(name, self.page)
        dockerfile = read(os.path.join(REPO, "Dockerfile"))
        for name in ("touch_keys.js", "touch_keys.css"):
            self.assertIn("COPY web/%s /usr/share/nginx/html/%s" % (name, name), dockerfile)
        self.assertIn("changelog.html touch_keys.js touch_keys.css", read(os.path.join(REPO, ".github", "workflows", "ci.yml")))
        self.assertIn('web/touch_keys.js" "${SCRIPT_DIR}/web/touch_keys.css" "${DIST_DIR}/"', read(os.path.join(REPO, "build_web.sh")))

    def test_the_budgets(self):
        raw = len(read_bytes(SCRIPT)) + len(read_bytes(STYLE))
        zipped = len(gzip.compress(read_bytes(SCRIPT), 9)) + len(gzip.compress(read_bytes(STYLE), 9))
        self.assertLess(raw, size_limits.TOUCH_KEYS_BUDGET, "the hotkey buttons' script and style are %d bytes" % raw)
        self.assertLess(zipped, size_limits.TOUCH_KEYS_GZIP_BUDGET, "and %d bytes gzipped" % zipped)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the page's code for the hotkey buttons was NOT run (tests/scripts/web_touch_keys_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_buttons_with_fakes_for_the_page(self):
        done = subprocess.run([shutil.which("node"), CHECK, SCRIPT], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failures")

    def test_the_pages_call_as_the_page_writes_it(self):
        done = subprocess.run([shutil.which("node"), WIRING, SHELL], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failures", done.stdout)


SHIELD = os.path.join(REPO, "tests", "scripts", "web_touch_keys_shield_check.js")


@unittest.skipUnless(shutil.which("node"), "node is not installed: the layer under the list was NOT run in a browser")
class TheLayerInABrowser(unittest.TestCase):
    def test_the_layer_covers_the_window_after_a_scroll(self):
        done = subprocess.run([shutil.which("node"), SHIELD, SCRIPT, STYLE], capture_output=True, text=True, timeout=180)
        if done.returncode == 3:
            self.skipTest(done.stdout.strip() or "no browser")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failures")


@unittest.skipUnless(shutil.which("node"), "node is not installed: the mutants of the hotkey buttons were NOT run")
class TheMutants(unittest.TestCase):
    def test_the_checks_fail_on_every_broken_copy(self):
        """tests/data/touch_keys_mutants.json (tools/mutate.py runs it too): each mutant on a copy of its file, noticed by the node check that its spec names"""
        spec = json.loads(read(MUTANTS))
        self.assertGreaterEqual(len(spec["mutants"]), 20)
        with tempfile.TemporaryDirectory() as tmp:
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    text = read(os.path.join(REPO, m["file"]))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in the file exactly once" % m["id"])
                    copy = os.path.join(tmp, os.path.basename(m["file"]))
                    with open(copy, "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    command = [copy if part == m["file"] else (os.path.join(REPO, part) if part.startswith(("tests/", "web/")) else part) for part in m["test"].split()]
                    done = subprocess.run([shutil.which("node")] + command[1:], capture_output=True, text=True, timeout=120)
                    if done.returncode == 3:
                        self.skipTest("no browser: " + m["id"])
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"])


class TheBrowserCheck(unittest.TestCase):
    def test_the_runner_says_skip_without_a_page(self):
        env = dict(os.environ)
        env.pop("ANTS_WEB_URL", None)
        done = subprocess.run(["bash", BROWSER_RUNNER], capture_output=True, text=True, timeout=60, env=env)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("SKIP", done.stdout)


if __name__ == "__main__":
    unittest.main()
