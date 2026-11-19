#!/usr/bin/env python3
"""The HUD's Fullscreen button on the web page (run by ./run_tests.sh --fast and by the CI). The game draws the button right of Quit and the page does the fullscreen (web/hud_fullscreen.js, called by
web/shell.html), because a browser grants fullscreen only inside the handler of a real click. What the page can say about it without a browser is checked:

  - the page: loads the script before its own code and calls it once with the pieces of the page that it needs (the stage, the page's own enter and leave, and the game's probe and export);
  - the script: RUNS with fakes for the page (node, when it is installed: tests/scripts/web_hud_fullscreen_check.js): one click on the stage, the game's own pointer decides, one request per click, the
    page's own enter and leave, the game told whoever changes the fullscreen;
  - the game: answers the probes 70 - 76 and takes the page's fullscreen (src/ants_app/application.cpp);
  - the image and the pages that the web build writes carry the script.
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHELL = os.path.join(REPO, "web", "shell.html")
SCRIPT = os.path.join(REPO, "web", "hud_fullscreen.js")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_hud_fullscreen_check.js")


def read(*path):
    with open(os.path.join(REPO, *path), encoding="utf-8") as f:
        return f.read()


class ThePage(unittest.TestCase):
    def setUp(self):
        self.page = read("web", "shell.html")

    def test_the_script_is_loaded_before_the_pages_own_code_and_called_once(self):
        self.assertEqual(len(re.findall(r'<script src="hud_fullscreen\.js"></script>', self.page)), 1)
        self.assertEqual(len(re.findall(r"AntsHudFullscreen\.forPage\(", self.page)), 1)
        self.assertLess(self.page.index('src="hud_fullscreen.js"'), self.page.index("AntsHudFullscreen.forPage("))
        self.assertLess(self.page.index('src="hud_fullscreen.js"'), self.page.index("var ANTS_BUILD_VER"))

    def test_the_call_gives_the_pages_own_fullscreen_and_the_games_two_functions(self):
        start = self.page.index("AntsHudFullscreen.forPage(")
        call = self.page[start:self.page.index("\n", start)]
        for part in ("stage: stageElement", "isFullscreen: isFullscreen", "enter: enterFullscreen", "leave: leaveFullscreen", "ready: function () { return isReadyToPlay; }"):
            self.assertIn(part, call)
        self.assertEqual(set(re.findall(r"Module\.(_\w+)", call)), {"_ants_probe", "_ants_set_page_fullscreen"})

    def test_the_game_answers_the_probes_and_takes_the_pages_fullscreen(self):
        source = read("src", "ants_app", "application.cpp")
        self.assertRegex(source, r"case 70: case 71: case 72: case 73: case 74: case 75: case 76: return g_web_app->hud_probe\(what\);")
        self.assertIn("ants_set_page_fullscreen(int on)", source)
        net = read("src", "ants_app", "application_net.cpp")
        for code in range(70, 77):
            self.assertRegex(net, r"case %d: return " % code)

    def test_the_image_and_the_web_build_carry_the_script(self):
        self.assertIn("COPY web/hud_fullscreen.js /usr/share/nginx/html/hud_fullscreen.js", read("Dockerfile"))
        self.assertIn("game_bar.js hud_fullscreen.js wake_lock.js", read(".github", "workflows", "ci.yml"))
        self.assertIn('web/hud_fullscreen.js" "${DIST_DIR}/"', read("build_web.sh"))


@unittest.skipUnless(shutil.which("node"), "node is not installed: the page's code for the HUD's Fullscreen button was NOT run (tests/scripts/web_hud_fullscreen_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_button_with_fakes_for_the_page(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SCRIPT], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
