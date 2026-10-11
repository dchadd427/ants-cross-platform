#!/usr/bin/env python3
"""A phone's keyboard for the match's chat box (run by ./run_tests.sh --fast and by the CI). The page (web/shell.html) keeps an invisible text field that a TAP on the chat box focuses, so that a
phone's own keyboard comes up, and mirrors what is typed into the game's chat line (src/ants_app/application_touch.cpp, native test: tests/test_app/test_web_chat.cpp). A phone cannot be driven
here, so everything that the page can say about it without one is checked:

  - the field: a real text input of the game's box (opacity 0, not display: none, which cannot be focused), at least 16px (iOS Safari zooms the page into a smaller field), out of the tab order,
    taking no tap itself, with the attributes that keep a phone's keyboard from correcting a chat line;
  - the script: the block exists once and is RUN with fakes for the page (node, when it is installed: tests/scripts/web_chat_check.js): the tap that opens the keyboard and the touches that do
    not, the mirror of the typing into the game, Enter and Esc, the other ways to close it, and the field's keys that never reach the game's own handler;
  - the page's other code: it adds no touchmove listener and cancels no touch (the touch guards' rules), and only the game's two exports are used;
  - the exports of the game that the page reads, and the browser check (tests/scripts/web_chat_check.py, opt-in) that drives the page in a real browser: its profiles and exit status.
"""
import os
import re
import shutil
import subprocess
import sys
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHELL = os.path.join(REPO, "web", "shell.html")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_chat_check.js")
BROWSER_CHECK = os.path.join(REPO, "tests", "scripts", "web_chat_check.py")
BROWSER_RUNNER = os.path.join(REPO, "tests", "scripts", "test_web_chat.sh")
EXPORTS = os.path.join(REPO, "src", "ants_app", "application_touch.cpp")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def block_of(page):
    return page[page.index("// ANTS_CHAT_BEGIN"):page.index("// ANTS_CHAT_END")]


class TheField(unittest.TestCase):
    def setUp(self):
        self.page = read(SHELL)
        style = re.sub(r"/\*.*?\*/", "", "\n".join(re.findall(r"<style>(.*?)</style>", self.page, re.S)), flags=re.S)
        found = re.search(r"#chat-keys\s*\{(.*?)\}", style, re.S)
        self.assertIsNotNone(found, "the field has a rule")
        self.rule = found.group(1)

    def test_it_is_invisible_but_can_be_focused(self):
        self.assertIn("opacity: 0;", self.rule)
        self.assertNotIn("display: none", self.rule)
        self.assertNotIn("visibility: hidden", self.rule)
        self.assertIn("position: absolute;", self.rule)
        self.assertIn("pointer-events: none;", self.rule, "a tap on the box is the game's, not the field's")

    def test_its_font_is_at_least_16px_so_that_ios_does_not_zoom(self):
        size = re.search(r"font:\s*(\d+)px", self.rule) or re.search(r"font-size:\s*(\d+)px", self.rule)
        self.assertIsNotNone(size)
        self.assertGreaterEqual(int(size.group(1)), 16)

    def test_the_element_is_in_the_game_box_and_keeps_a_keyboard_from_correcting_the_line(self):
        found = re.search(r'<input type="text" id="chat-keys"([^>]*)>', self.page)
        self.assertIsNotNone(found)
        attributes = found.group(1)
        for part in ('maxlength="100"', 'tabindex="-1"', 'autocomplete="off"', 'autocapitalize="off"', 'autocorrect="off"', 'spellcheck="false"', 'enterkeyhint="send"', 'aria-label="Chat message"'):
            self.assertIn(part, attributes)
        box = self.page[self.page.index('<div id="game-container">'):]
        self.assertLess(box.index('id="canvas"'), box.index('id="chat-keys"'))
        self.assertLess(box.index('id="chat-keys"'), box.index('id="splash-overlay"'), "inside the game's box (it works in the page's fullscreen too)")


class TheScript(unittest.TestCase):
    def setUp(self):
        self.page = read(SHELL)
        self.block = block_of(self.page)

    def test_the_block_is_there_once(self):
        self.assertEqual(len(re.findall(r"// ANTS_CHAT_BEGIN", self.page)), 1)
        self.assertEqual(len(re.findall(r"// ANTS_CHAT_END", self.page)), 1)

    def test_the_field_is_focused_in_the_touchend_handler_not_later(self):
        handler = re.search(r"canvas\.addEventListener\('touchend', function \(e\) \{(.*?)\}, \{ passive: true \}\);", self.block, re.S)
        self.assertIsNotNone(handler)
        self.assertIn("open()", handler.group(1))
        self.assertNotRegex(handler.group(1), r"setTimeout|requestAnimationFrame|Promise|then\(")
        for name in ("touchstart", "touchend", "touchcancel"):
            for listener in re.findall(r"canvas\.addEventListener\('%s',(.*?)\n\s*\}, \{ passive: true \}\);" % name, self.block, re.S):
                self.assertNotRegex(listener, r"preventDefault|stopPropagation", "the page never cancels a touch (the sound's unlock needs it)")

    def test_no_touchmove_listener_and_nothing_stops_a_touch(self):
        self.assertNotIn("touchmove", self.block)
        self.assertNotRegex(self.block, r"touch\w*['\"][^\n]*stopPropagation")

    def test_only_the_games_chat_exports_are_used(self):
        used = set(re.findall(r"Module\.(_\w+)", self.block))
        self.assertEqual(used, {"_ants_chat_probe", "_ants_chat_do", "_ants_probe"})

    def test_the_game_exports_what_the_page_reads(self):
        source = read(EXPORTS)
        for name in ("ants_chat_probe", "ants_chat_do"):
            self.assertRegex(source, r'extern "C" EMSCRIPTEN_KEEPALIVE int %s\(int' % name)
        self.assertIn("ants_probe", read(os.path.join(REPO, "src", "ants_app", "application.cpp")))


@unittest.skipUnless(shutil.which("node"), "node is not installed: the page's code for a phone's keyboard was NOT run (tests/scripts/web_chat_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_keyboard_with_fakes_for_the_page(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failures", done.stdout)


class TheBrowserCheck(unittest.TestCase):
    def test_a_bad_command_line_is_status_2_and_asks_for_no_browser(self):
        done = subprocess.run([sys.executable, BROWSER_CHECK, "--web", "http://127.0.0.1:9/", "--profile", "android-tablet"], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertIn("unknown profile", done.stdout)

    def test_the_runner_says_skip_without_a_page(self):
        env = dict(os.environ)
        env.pop("ANTS_WEB_URL", None)
        done = subprocess.run(["bash", BROWSER_RUNNER], capture_output=True, text=True, timeout=60, env=env)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("SKIP", done.stdout)


if __name__ == "__main__":
    unittest.main()
