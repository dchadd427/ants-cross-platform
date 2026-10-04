#!/usr/bin/env python3
"""The pointer of a FULLSCREEN game page: the black bars, and the pointer lock (run by ./run_tests.sh --fast and by the CI). The owner: on a 16:10 screen the 16:9 picture has bars above and
below, and a pointer that went over a bar no longer scrolled the map (the browser sends the game nothing there and says that the pointer LEFT the canvas); and on a Mac a pointer at the
screen's edge makes the Dock and the menu bar appear, which a web page can only prevent by locking the pointer.

  - the page's own code is RUN (node, when it is installed): the pixel that the game reads for a pointer over a bar and the position that makes the game read it, the cursor of a locked
    pointer (its start, the motion by the mouse's distance, the edges, the corners, the slow mouse), the choice "Fullscreen mouse: Locked / Free" and its storage key (`ants.pointerlock`,
    only "off" turns the lock off) with fakes for the page (tests/scripts/web_edge_check.js);
  - what needs no browser is read from the files: the control and its markup, the listeners that the bars and the lock need, the cursor rule of the fullscreen stage, the guide's text, and
    the game's `ants_probe` that the browser check reads.
The same things in a real browser: tests/scripts/web_edge_check.py (opt-in, against a running page).
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHELL = os.path.join(REPO, "web", "shell.html")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_edge_check.js")
APPLICATION = os.path.join(REPO, "src", "ants_app", "application.cpp")
BROWSER_CHECK = os.path.join(REPO, "tests", "scripts", "web_edge_check.py")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class PageCase(unittest.TestCase):
    """assertRegex / assertNotRegex without printing the whole page when they fail"""

    def found(self, text, pattern, msg=None):
        self.assertIsNotNone(re.search(pattern, text), msg or ("lacks: " + pattern))

    def not_found(self, text, pattern, msg=None):
        self.assertIsNone(re.search(pattern, text), msg or ("has: " + pattern))


class TheControlAndTheGuide(PageCase):
    def test_the_control_starts_locked_and_has_one_key(self):
        page = read(SHELL)
        self.found(page, r'id="lock-on" data-lock="on" aria-checked="true"')                          # Locked is the default
        self.found(page, r'id="lock-off" data-lock="off" aria-checked="false"')
        self.assertIn("var ANTS_LOCK_KEY = 'ants.pointerlock';", page)
        self.assertEqual(len(re.findall(r"getItem\(ANTS_LOCK_KEY\)", page)), 1, "the key is read once")
        self.assertIn("var ANTS_POINTER_LOCK = true;", page)
        self.assertEqual(len(re.findall(r"window\.localStorage\.setItem\(ANTS_LOCK_KEY", page)), 1)
        self.not_found(page, r"ants\.pointerlock['\"]\s*\)", "nothing else reads the key by its text")

    def test_the_guide_says_what_fullscreen_does(self):
        page = read(SHELL)
        self.assertIn("Fullscreen mouse: Locked", page)
        self.found(page, r"<strong>Esc in fullscreen:</strong>")
        self.assertIn("Dock and menu bar", page)
        self.assertIn("black bars", page)

    def test_the_fullscreen_stage_hides_the_browsers_arrow_over_the_bars(self):
        page = read(SHELL)
        rule = re.search(r"#game-stage:fullscreen,\s*#game-stage:-webkit-full-screen,\s*#game-stage\.pseudo-fullscreen \{(.*?)\}", page, re.S)
        self.assertIsNotNone(rule)
        self.assertIn("cursor: none;", rule.group(1))


class TheListeners(PageCase):
    """Each listener that the bars and the lock stand on (a browser check says whether they work; here they cannot silently disappear)."""

    def setUp(self):
        self.page = read(SHELL)

    def test_the_bars_are_handed_to_the_game_and_the_leave_towards_one_is_not_passed_on(self):
        self.assertIn("window.addEventListener('pointermove'", self.page)
        self.found(self.page, r"e\.pointerType === 'touch'")                                          # touch is left alone
        self.assertIn("window.addEventListener('mouseleave'", self.page)
        self.found(self.page, r"stageElement\.contains\(e\.relatedTarget\)")
        self.assertIn("stageElement.addEventListener('mouseleave'", self.page)                              # a pointer that leaves the screen from a bar is reported as gone
        self.assertIn("stageElement.addEventListener('wheel'", self.page)
        self.assertIn("stageElement.addEventListener('contextmenu'", self.page)
        for call in ("ANTS_PAGE.edgePixel(", "ANTS_PAGE.clientFor("):
            self.assertIn(call, self.page)

    def test_the_lock_is_asked_for_by_the_fullscreen_click_and_released_with_fullscreen(self):
        enter = re.search(r"function enterFullscreen\(\) \{(.*?)\n        \}", self.page, re.S)
        self.assertIsNotNone(enter)
        self.assertIn("requestLock(null);", enter.group(1))
        self.assertEqual(len(re.findall(r"requestLock\(", self.page)), 4, "the definition, the fullscreen click, the page's own retry, the click on the game, and nothing else asks")
        self.assertIn("function fullscreenEnded()", self.page)
        self.found(self.page, r"function fullscreenEnded\(\) \{[^\n]*\n\s*releaseLock\(\);\s*lockKeys\(false\);")
        self.assertIn("keyboard.lock(['Escape'])", self.page)                                                # Esc reaches the game in the Chromium browsers
        self.assertNotIn("emscripten_request_pointerlock", self.page)

    def test_the_game_is_not_told_of_the_lock_and_gets_the_pages_cursor(self):
        self.found(self.page, r"\['pointerlockchange', 'webkitpointerlockchange', 'mozpointerlockchange'\]\.forEach")
        self.found(self.page, r"window\.addEventListener\(name, function \(e\) \{\s*e\.stopPropagation\(\);")           # (window capture: the game's window system never hears of the lock)
        self.assertIn("ANTS_PAGE.moveLocked(", self.page)
        self.assertIn("ANTS_PAGE.lockStart(", self.page)

    def test_a_windowed_page_is_left_alone(self):
        """Every listener of the bars and the lock starts with a test of fullscreen or of the lock itself: a windowed page does exactly what it did."""
        for name in ("pointermove", "mouseleave"):
            body = re.search(r"window\.addEventListener\('" + name + r"', function \(e\) \{\n(.*?)\n        \}, true\);", self.page, re.S)
            self.assertIsNotNone(body, name)
            self.assertIn("isFullscreen()", body.group(1), name)
        wheel = re.search(r"stageElement\.addEventListener\('wheel', function \(e\) \{ (.*?) \}, \{ passive: false \}\);", self.page)
        self.assertIsNotNone(wheel)
        self.assertIn("isFullscreen()", wheel.group(1))


class TheBrowserCheckAndTheGame(PageCase):
    def test_the_game_exports_what_the_browser_check_reads(self):
        source = read(APPLICATION)
        self.found(source, r'extern "C" EMSCRIPTEN_KEEPALIVE int ants_probe\(int what\)')
        check = read(BROWSER_CHECK)
        self.assertIn("Module._ants_probe(", check)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the page's code for the pointer in fullscreen was NOT run (tests/scripts/web_edge_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_pixel_over_a_bar_the_locked_cursor_and_the_control(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
