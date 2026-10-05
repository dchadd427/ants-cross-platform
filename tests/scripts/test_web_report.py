#!/usr/bin/env python3
"""The web game counts a single-player game for the front page (run by ./run_tests.sh --fast and by the CI). The owner chose to count online matches AND single-player games; a single-player
game runs in the browser tab, so the page tells the server when one begins (docs/NETWORK_PORT.md "Site statistics").

  - the page's own function is RUN (node, when it is installed): antsReportLocalGame makes one POST of /stats/local with nothing in it for each call, and no failure of any kind reaches the
    game or is retried (tests/scripts/web_report_check.js);
  - the game's side is read from the source: the web build (and only the web build) installs a hook that calls that page function, the hook is called in one place (the first tick of a game
    on this computer, never in a match of the network: tests/test_app/test_app_integration.cpp 7.6h and test_network_app.cpp N5.90 run it);
  - the front page itself never reports (its frames are matches of the network, and a game on this computer opens the game page, which reports it): web/lobby.html has no POST to /stats/local.
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHELL = os.path.join(REPO, "web", "shell.html")
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_report_check.js")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class TheGameSide(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = read("src", "ants_app", "application.cpp")
        cls.header = read("include", "ants_app", "application.hpp")

    def test_the_web_build_installs_the_hook_and_nothing_else_does(self):
        installs = [m.start() for m in re.finditer(r"set_on_local_match_started\(", self.app)]
        self.assertEqual(len(installs), 1)                                                  # one place in the program sets it ...
        web_block = self.app[self.app.rindex("#if defined(__EMSCRIPTEN__)", 0, installs[0]):installs[0]]
        self.assertNotIn("#endif", web_block)                                               # ... and it is inside a block of the web build
        self.assertIn("set_on_local_match_started([]() { ants_report_local_game(); });", self.app)
        self.assertNotIn("set_on_local_match_started", read("src", "ants_app", "main.cpp"))

    def test_the_hook_calls_the_page_function_and_ignores_what_it_cannot_do(self):
        em_js = re.search(r"EM_JS\(void, ants_report_local_game, \(\), \{(.*?)\}\);", self.app, re.S)
        self.assertIsNotNone(em_js)
        body = em_js.group(1)
        self.assertIn("antsReportLocalGame", body)
        self.assertIn('typeof antsReportLocalGame === "function"', body)                    # a page without the function: nothing happens
        self.assertRegex(body, r"try \{.*\} catch \(e\) \{\}")                              # nothing can come back to the game

    def test_it_is_told_in_one_place_once_for_each_match_and_never_in_a_match_of_the_network(self):
        self.assertEqual(len(re.findall(r"on_local_match_started_\(\)", self.app)), 1)      # (the call: one place)
        place = self.app.index("on_local_match_started_()")
        around = self.app[place - 400:place + 120]
        self.assertIn("!local_match_reported_ && !network_active()", around)
        self.assertIn("local_match_reported_ = true;", around)
        self.assertLess(around.index("local_match_reported_ = true;"), around.index("on_local_match_started_()"))
        tick = self.app.index("sim_.tick();", place)
        self.assertLess(place, tick)                                                        # it is told before the tick that it is told of ...
        self.assertLess(tick - place, 200)                                                  # ... in the branch that runs a local game's ticks
        enter = self.app[re.search(r"void Application::enter_match\([^)]*\) \{", self.app).start():]       # (enter_match(bool rejoin) since the way back: the signature is not the point)
        enter = enter[:enter.index("\n}\n")]
        self.assertIn("local_match_reported_ = false;", enter)                              # every match starts with it not told

    def test_the_header_says_it_is_for_a_game_on_this_computer(self):
        self.assertRegex(self.header, r"void set_on_local_match_started\(std::function<void\(\)> hook\)")
        self.assertIn("never for a match of the network", self.header)


class ThePage(unittest.TestCase):
    def test_the_game_page_has_the_function_once_as_a_global_and_the_front_page_does_not_report(self):
        page = read("web", "shell.html")
        self.assertEqual(page.count("ANTS_REPORT_BEGIN"), 1)
        self.assertEqual(page.count("ANTS_REPORT_END"), 1)
        self.assertEqual(len(re.findall(r"^        function antsReportLocalGame\(\) \{", page, re.M)), 1)      # a top-level function of the page's script, as the game's EM_JS code needs it
        self.assertEqual(page.count("/stats/local"), 1)                                     # one place posts a report
        self.assertIn("fetch('/stats/local', { method: 'POST', keepalive: true })", page)
        lobby = read("web", "lobby.html") if os.path.exists(LOBBY) else ""
        self.assertNotIn("/stats/local", lobby)

    def test_nothing_else_of_the_site_posts_a_report(self):
        for name in ("Dockerfile", os.path.join("docker", "nginx.conf")):
            text = read(name)
            self.assertNotRegex(text, r"(?i)antsReportLocalGame")
        for root, _, files in os.walk(os.path.join(REPO, "web")):
            for f in files:
                if f.endswith((".html", ".js")) and f != "shell.html":
                    self.assertNotIn("antsReportLocalGame", read(os.path.relpath(os.path.join(root, f), REPO)), f)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the page's own code for counting a game was NOT run (tests/scripts/web_report_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_one_post_for_each_call_and_no_failure_reaches_the_game(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
