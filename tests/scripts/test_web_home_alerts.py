#!/usr/bin/env python3
"""The alerts that need no permission (web/alert_core.js, web/home/alerts.js and alerts.css, the loader in web/home/home.js, the join hook in web/lobby.html, the countdown block of web/shell.html,
docs/HOME_SCREEN.md "Alerts"; run by ./run_tests.sh --fast and by the CI).

An alert for a person who joins my room, a blinking tab while my match starts in the background, and the sheet that turns each one off. What this file checks:
  - tests/scripts/web_home_alerts_check.js runs in node, with the real front page in the fake browser of tests/scripts/web_page_harness.js (a person joins on Home, in the Room, in the background; a computer
    player does not count; each switch; the Menu's row and the sheet, the same for a guest) and the game page's block on a page of fakes (the countdown card while the page is hidden, a recording);
  - the folder: its budgets (raw and gzipped), ASCII, and that the scripts never ask the browser for anything;
  - the files are in the Docker image's check, the local web build, and both pages load the shared script;
  - tests/data/home_alerts_mutants.json changes one thing at a time in a COPY of the files: the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
The pictures are made by hand with the page in a real browser (tests/scripts/web_home_alerts_browser.js, opt-in, `--shots DIR`).
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
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_alerts_check.js")
NODE = shutil.which("node")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import ALERTS_BUDGET, ALERTS_GZIP_BUDGET, ALERT_CORE_BUDGET, ALERT_CORE_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def sizes(paths):
    raw = zipped = 0
    for path in paths:
        with open(os.path.join(REPO, path), "rb") as f:
            data = f.read()
        assert re.search(rb"[^\x00-\x7f]", data) is None, path + " is not ASCII"
        raw += len(data)
        zipped += len(gzip.compress(data, 9))
    return raw, zipped


class TheScripts(unittest.TestCase):
    def test_they_run_in_node(self):
        if not NODE:
            raise unittest.SkipTest("node is not installed")
        done = subprocess.run([NODE, CHECK, os.path.join(WEB, "lobby.html")], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")

    def test_they_are_under_their_budgets_and_plain_ascii(self):
        raw, zipped = sizes(["web/home/alerts.js", "web/home/alerts.css"])
        self.assertLess(raw, ALERTS_BUDGET, "alerts.js and alerts.css are %d bytes" % raw)
        self.assertLess(zipped, ALERTS_GZIP_BUDGET, "and %d bytes gzipped" % zipped)
        raw, zipped = sizes(["web/alert_core.js"])
        self.assertLess(raw, ALERT_CORE_BUDGET, "alert_core.js is %d bytes" % raw)
        self.assertLess(zipped, ALERT_CORE_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_no_sound_is_assigned_until_the_maintainer_has_approved_it(self):
        if not NODE:
            raise unittest.SkipTest("node is not installed")
        done = subprocess.run([NODE, "-e", "const c = require(process.argv[1]); console.log(JSON.stringify([c.SOUNDS, c.wired(), c.title()]))", os.path.join(WEB, "alert_core.js")], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(json.loads(done.stdout), [{"join": "", "start": ""}, [], "Alerts"])


class TheWiring(unittest.TestCase):
    def test_both_pages_load_the_shared_script_and_the_front_page_loads_it_before_its_own_part(self):
        lobby, shell = read("web", "lobby.html"), read("web", "shell.html")
        self.assertIn('<script src="alert_core.js"></script>', shell)
        self.assertLess(lobby.index('<script src="alert_core.js"></script>'), lobby.index('<script src="home/home.js"></script>'))

    def test_the_files_are_in_the_image_the_local_web_build_and_the_image_check(self):
        ci = read(".github", "workflows", "ci.yml")
        for name in ("home/alerts.js", "home/alerts.css", "alert_core.js"):
            self.assertIn(name, ci)
        self.assertIn("COPY web/alert_core.js /usr/share/nginx/html/alert_core.js", read("Dockerfile"))
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/", read("Dockerfile"))
        self.assertIn("web/alert_core.js", read("build_web.sh"))
        self.assertIn("web/alert_core.js", read("tools", "ci_light.sh"), "a change of it must run the native jobs: the page harness loads it")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_alerts_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = (["tools/size_limits.py", "tools/ci_light.sh", "web/lobby.html", "web/shell.html", "web/shapes.js", "web/rejoin_key.js", "web/alert_core.js", "web/home/home.js", "web/home/games.js", "web/home/alerts.js", "web/home/alerts.css",
              "tests/scripts/test_web_home_alerts.py", "tests/scripts/web_home_alerts_check.js", "tests/scripts/web_page_harness.js", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml"]
             + ["web/front/" + n for n in ("lobby_rules.js", "lobby_net.js", "lobby_chat.js", "rematch.js", "game_modes.js", "game_type.js", "map_picker.js", "player_name.js", "map_maker_link.js")])

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_alerts_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_alerts_mutants.")
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
