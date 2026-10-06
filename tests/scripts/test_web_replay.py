#!/usr/bin/env python3
"""The web game offers the replay of a match for download (run by ./run_tests.sh --fast and by the CI). When a match has ended the game hands its file to the page, the page shows a "Download replay"
button under the game, and the next match takes the offer back (docs/REPLAYS.md).

  - the page's own functions are RUN (node, when it is installed): antsOfferReplay shows and hides the button, antsDownloadReplay saves the very bytes under a name that is safe on every system,
    and no failure of the page reaches the game (tests/scripts/web_replay_check.js);
  - the game's side is read from the source: the web build (and only the web build) calls the page's function, at the two places where it must (the file is made, a match begins), and what it
    calls is a function that the page defines once;
  - the pages that embed games (web/lobby.html) have no button and never define the function.
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHELL = os.path.join(REPO, "web", "shell.html")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_replay_check.js")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class TheGameSide(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = read("src", "ants_app", "application_replay.cpp")

    def test_the_hook_calls_the_page_function_and_ignores_what_it_cannot_do(self):
        em_js = re.search(r"EM_JS\(void, ants_offer_replay, \(([^)]*)\), \{(.*?)\n\}\);", self.source, re.S)
        self.assertIsNotNone(em_js)
        body = em_js.group(2)
        self.assertIn('typeof antsOfferReplay !== "function"', body)                        # a page without the function: nothing happens
        self.assertRegex(body, r"(?s)try \{.*\} catch \(e\) \{\}")                              # nothing can come back to the game
        self.assertIn("HEAPU8.slice(", body)                                                # the bytes are copied out of the game's memory
        self.assertIn("antsOfferReplay(null,", body)                                        # a size of 0 takes the offer back
        self.assertIn("antsOfferReplay(HEAPU8.slice(", body)

    def test_the_web_build_alone_has_the_hook(self):
        start = self.source.index("EM_JS(void, ants_offer_replay")
        opening = self.source.rindex("#if defined(__EMSCRIPTEN__)", 0, start)
        self.assertNotIn("#endif", self.source[opening:start])                              # the definition is inside a block of the web build
        calls = [m.start() for m in re.finditer(r"\bants_offer_replay\(", self.source)]
        self.assertEqual(len(calls), 2)                                                     # the two calls (the definition is written EM_JS(void, ants_offer_replay, (...)))
        for at in calls:
            block = self.source[self.source.rindex("#if", 0, at):at]
            self.assertIn("#if defined(__EMSCRIPTEN__)", block)
            self.assertNotIn("#endif", block)
            self.assertNotIn("#else", block)
        for name in ("application.cpp", "application_menu.cpp", "application_touch.cpp", "main.cpp"):
            self.assertNotIn("ants_offer_replay", read("src", "ants_app", name), name)

    def test_the_offer_is_made_when_the_file_is_made_and_taken_back_when_a_match_begins(self):
        offer = self.source[self.source.index("void Application::offer_replay()"):]
        offer = offer[:offer.index("\n}\n")]
        self.assertIn("ants_offer_replay(last_replay_.data(), static_cast<int>(last_replay_.size())", offer)
        withdraw = self.source[self.source.index("void Application::withdraw_replay()"):]
        withdraw = withdraw[:withdraw.index("\n}\n")]
        self.assertIn("ants_offer_replay(nullptr, 0, nullptr, 0);", withdraw)
        begin = self.source[self.source.index("void Application::begin_recording("):]
        begin = begin[:begin.index("\n}\n")]
        self.assertIn("withdraw_replay();", begin)                                          # every recording that begins takes the last offer back
        app = read("src", "ants_app", "application.cpp")
        check = app[app.index("void Application::check_match_over()"):]
        check = check[:check.index("\n}\n")]
        self.assertIn("finish_recording();", check)                                         # the file is made where the match is found over, once


class ThePage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read("web", "shell.html")

    def test_the_game_page_defines_the_functions_once_as_globals(self):
        self.assertEqual(self.page.count("ANTS_REPLAY_BEGIN"), 1)
        self.assertEqual(self.page.count("ANTS_REPLAY_END"), 1)
        self.assertEqual(len(re.findall(r"^        function antsOfferReplay\(bytes, name\) \{", self.page, re.M)), 1)      # top-level functions of the page's script, as the game's EM_JS code needs
        self.assertEqual(len(re.findall(r"^        function antsDownloadReplay\(\) \{", self.page, re.M)), 1)

    def test_the_button_is_in_the_view_bar_under_the_game_and_hidden_until_offered(self):
        bar = self.page[self.page.index('<div class="view-bar" id="view-bar">'):]
        bar = bar[:bar.index("<div class=\"mobile-tip-banner\"")]
        self.assertIn('id="replay-download"', bar)
        self.assertRegex(bar, r'<button[^>]*id="replay-download"[^>]*\bhidden\b')
        self.assertEqual(self.page.count('id="replay-download"'), 1)
        self.assertRegex(self.page, r"\.replay-part\[hidden\]\s*\{\s*display:\s*none;")

    def test_nothing_else_of_the_site_has_the_function_or_the_button(self):
        for root, _, files in os.walk(os.path.join(REPO, "web")):
            for f in files:
                if f.endswith((".html", ".js")) and f != "shell.html":
                    text = read(os.path.relpath(os.path.join(root, f), REPO))
                    self.assertNotIn("antsOfferReplay", text, f)
                    self.assertNotIn("replay-download", text, f)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the page's own code for the replay's download was NOT run (tests/scripts/web_replay_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_button_shows_and_hides_the_file_is_saved_and_no_failure_reaches_the_game(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
