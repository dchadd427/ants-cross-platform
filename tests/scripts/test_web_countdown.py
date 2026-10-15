#!/usr/bin/env python3
"""The countdown's Fullscreen button (protocol 21; run by ./run_tests.sh --fast and by the CI). The game's countdown card (src/ants_app/hud.cpp, countdown_card.hpp) leaves a place for a Fullscreen
button, and the page lays a real button over it (web/countdown_button.js, called by web/shell.html), because a browser grants fullscreen only to the click of a real button. What the page can say
about it without a browser is checked:

  - the page: loads the script before the page's own code, calls it once with the pieces of the page that it needs, and has no button of its own (the script makes it, a child of the stage, not of the
    picture's box: it is not the game's touch, and it goes with the stage into the browser's fullscreen);
  - the script: RUNS with fakes for the page (node, when it is installed: tests/scripts/web_countdown_check.js): shown only while the game says that a countdown runs, over the game's place, kept inside
    the picture, one request for fullscreen per click, hidden in fullscreen, and the poll;
  - the exports: the page reads only the game's `_ants_probe`, whose codes 60 - 65 (the seconds left, the button's place, the lines of the chat log) the game answers;
  - the browser check (tests/scripts/web_countdown_check.py, opt-in: tests/scripts/test_web_countdown.sh): its command line and its skip without a page.
"""
import os
import re
import shutil
import subprocess
import sys
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHELL = os.path.join(REPO, "web", "shell.html")
SCRIPT = os.path.join(REPO, "web", "countdown_button.js")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_countdown_check.js")
BROWSER_CHECK = os.path.join(REPO, "tests", "scripts", "web_countdown_check.py")
BROWSER_RUNNER = os.path.join(REPO, "tests", "scripts", "test_web_countdown.sh")
APPLICATION = os.path.join(REPO, "src", "ants_app", "application.cpp")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class ThePage(unittest.TestCase):
    def setUp(self):
        self.page = read(SHELL)

    def test_the_script_is_loaded_before_the_pages_own_code_and_called_once(self):
        self.assertEqual(len(re.findall(r'<script src="countdown_button\.js"></script>', self.page)), 1)
        self.assertEqual(len(re.findall(r"AntsCountdown\.forPage\(", self.page)), 1)
        self.assertLess(self.page.index('src="countdown_button.js"'), self.page.index("AntsCountdown.forPage("))
        self.assertLess(self.page.index('src="countdown_button.js"'), self.page.index("var ANTS_BUILD_VER"))

    def test_the_page_has_no_button_of_its_own_the_script_makes_it_in_the_stage(self):
        self.assertNotIn("countdown-fs", self.page, "the button, its look and its place are the script's: the page only calls it")
        call = self.page[self.page.index("AntsCountdown.forPage("):]
        call = call[:call.index("\n")]
        for part in ("stage: stageElement", "canvas: canvas", "isFullscreen: isFullscreen", "enter: enterFullscreen", "isReadyToPlay"):
            self.assertIn(part, call)
        stage = self.page[self.page.index('<div id="game-stage"'):self.page.index('<div class="name-modal" id="name-step"')]
        self.assertIn('id="pseudo-exit"', stage)
        self.assertNotIn("countdown", stage)

    def test_only_the_probe_of_the_game_is_used(self):
        call = self.page[self.page.index("AntsCountdown.forPage("):]
        call = call[:call.index("\n")]
        self.assertEqual(set(re.findall(r"Module\.(_\w+)", call)), {"_ants_probe"})

    def test_a_click_on_the_game_takes_no_pointer_lock_while_the_card_is_up(self):
        # a locked pointer cannot click the DOM button: with the card up, the page's pointer lock waits for the match itself (the real-browser proof is test_web_countdown.sh: "does not take the pointer lock")
        start = self.page.index("canvas.addEventListener('mousedown', function (e) {")
        handler = self.page[start:self.page.index("}, true);", start)]
        in_match = re.search(r"var inMatch = false; try \{ inMatch = (.*?); \} catch", handler)
        self.assertIsNotNone(in_match, "the handler's test for a running match")
        self.assertIn("Module._ants_match_running() === 1", in_match.group(1))
        self.assertRegex(in_match.group(1), r"&& !\(Module\._ants_probe && Module\._ants_probe\(60\) >= 0\)$", "a running match whose countdown card is up is not a match to lock the pointer for")
        self.assertIn("if (!isRealFullscreen() && !inMatch) return;", handler, "the browser's fullscreen takes the lock as ever")

    def test_the_image_carries_the_script(self):
        self.assertIn("COPY web/countdown_button.js /usr/share/nginx/html/countdown_button.js", read(os.path.join(REPO, "Dockerfile")))
        self.assertIn("countdown_button.js changelog.html", read(os.path.join(REPO, ".github", "workflows", "ci.yml")))
        self.assertIn('web/countdown_button.js" "${DIST_DIR}/"', read(os.path.join(REPO, "build_web.sh")))


class TheScript(unittest.TestCase):
    def setUp(self):
        self.script = read(SCRIPT)

    def test_it_makes_one_button_with_the_id_the_browser_check_knows(self):
        self.assertEqual(len(re.findall(r"createElement\(", self.script)), 1)
        self.assertIn("button.id = 'countdown-fs'", self.script)
        self.assertIn("page.stage.appendChild(button)", self.script)

    def test_the_click_is_the_headers_fullscreen_and_the_page_is_asked_before(self):
        self.assertIn("page.enter()", self.script)
        self.assertIn("page.isFullscreen()", self.script)

    def test_the_script_listens_to_nothing_but_the_button(self):
        self.assertEqual([m for m in re.findall(r"(\w+)\.addEventListener\(", self.script)], ["button"])

    def test_the_game_answers_the_probes_the_script_reads(self):
        source = read(APPLICATION)
        self.assertIn("case 60: case 61: case 62: case 63: case 64: case 65: return g_web_app->countdown_probe(what);", source)
        net = read(os.path.join(REPO, "src", "ants_app", "application_net.cpp"))
        for code in range(60, 66):
            self.assertRegex(net, r"case %d: return " % code)
        self.assertRegex(self.script, r"ask\(60\)")
        for code in (61, 62, 63, 64):
            self.assertRegex(self.script, r"ask\(%d\)" % code)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the page's code for the countdown's button was NOT run (tests/scripts/web_countdown_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_button_with_fakes_for_the_page(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SCRIPT], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failures", done.stdout)


class TheBrowserCheck(unittest.TestCase):
    def test_a_missing_page_address_is_status_2(self):
        done = subprocess.run([sys.executable, BROWSER_CHECK], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)

    def test_the_runner_says_skip_without_a_page(self):
        env = dict(os.environ)
        env.pop("ANTS_WEB_URL", None)
        done = subprocess.run(["bash", BROWSER_RUNNER], capture_output=True, text=True, timeout=60, env=env)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("SKIP", done.stdout)

    def test_the_check_names_its_probes_and_its_page(self):
        text = read(BROWSER_CHECK)
        for part in ("Module._ants_probe(60)", "Module._ants_probe(65)", "countdown-fs", "countdown_seconds"):
            self.assertIn(part, text)


if __name__ == "__main__":
    unittest.main()
