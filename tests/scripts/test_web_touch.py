#!/usr/bin/env python3
"""The game page on a TOUCH SCREEN (run by ./run_tests.sh --fast and by the CI). The game reads its fingers itself (SDL's touch events: a tap is a left click, a hold a right click, two
fingers move and zoom the map: src/ants_app/application_touch.cpp, include/ants_app/touch_control.hpp); what the page does is keep the browser out of the way. It cannot be run on a phone
here, so everything that the page can say about it without one is checked:

  - the style: the canvas has touch-action none, the page has manipulation (no double-tap zoom) and every control too, a pull down or a swipe past the end of the page does not reload it or
    chain to another scroller (overscroll-behavior-y: none, which does not stop the page's own scrolling), a long press makes no callout or selection over the game, the viewport still
    forbids scaling (Chrome on Android obeys it; iOS Safari does not, which is what the gesture listeners are for);
  - the script: the block of the guards exists once and is RUN with fakes for the page (node, when it is installed: tests/scripts/web_touch_check.js): the listeners that it registers and how
    (the browser's pinch on the canvas AND the document, not passive; the touch listeners passive: the page never cancels, stops or delays a touch), the cancel of the context menu, the
    game told of a touchcancel, the trackpad's pinch still zooming the game through the wheel, and the count of fingers that cannot stick;
  - the page's other code: no touchmove listener anywhere, the sound's unlock still on touchend (a hold that ends in a right click still unlocks it), nothing in the page stops a touch event;
  - the guide's text for touch, and what the game exports for the page and for the browser check.
The same things in a real browser: tests/scripts/test_web_touch.sh (opt-in, against a running page: web_touch_check.py drives it with a Pixel-like and an iPhone-like phone); what can be checked of that
script without a browser (the protocol of its fingers, the probes it reads, the phones it has, its exit statuses) is checked here.
"""
import os
import re
import shutil
import subprocess
import sys
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHELL = os.path.join(REPO, "web", "shell.html")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_touch_check.js")
APPLICATION = os.path.join(REPO, "src", "ants_app", "application.cpp")
APPLICATION_TOUCH = os.path.join(REPO, "src", "ants_app", "application_touch.cpp")
BROWSER_CHECK = os.path.join(REPO, "tests", "scripts", "web_touch_check.py")
BROWSER_RUNNER = os.path.join(REPO, "tests", "scripts", "test_web_touch.sh")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class PageCase(unittest.TestCase):
    """assertRegex / assertNotRegex without printing the whole page when they fail"""

    def found(self, text, pattern, msg=None):
        self.assertIsNotNone(re.search(pattern, text), msg or ("lacks: " + pattern))

    def not_found(self, text, pattern, msg=None):
        self.assertIsNone(re.search(pattern, text), msg or ("has: " + pattern))


def css_rule(page, selector_pattern):
    """The body of the first rule of the page's style whose selector matches"""
    style = re.sub(r"/\*.*?\*/", "", "\n".join(re.findall(r"<style>(.*?)</style>", page, re.S)), flags=re.S)
    match = re.search(r"(?:^|\})\s*" + selector_pattern + r"\s*\{(.*?)\}", style, re.S)
    return match.group(1) if match else None


def script_of(page):
    return "\n".join(re.findall(r"<script>(.*?)</script>", page, re.S))


def without_comments(js):
    return "\n".join(re.sub(r"//.*$", "", line) for line in js.splitlines())


class TheStyle(PageCase):
    def setUp(self):
        self.page = read(SHELL)

    def test_the_canvas_takes_the_browsers_touch_away_and_the_page_keeps_panning_without_double_tap_zoom(self):
        canvas = css_rule(self.page, r"canvas\.emscripten")
        self.assertIsNotNone(canvas)
        self.assertIn("touch-action: none;", canvas)
        self.assertIn("-webkit-touch-callout: none;", canvas)
        root = css_rule(self.page, r"html")
        self.assertIsNotNone(root)
        self.assertIn("touch-action: manipulation;", root)           # the whole page: panning and pinch stay, double-tap zoom goes (iOS Safari honours it from iOS 13)
        self.assertIn("overscroll-behavior-y: none;", root)

    def test_a_swipe_past_the_end_of_the_page_does_not_reload_it(self):
        body = css_rule(self.page, r"body")
        self.assertIsNotNone(body)
        self.assertIn("overscroll-behavior-y: none;", body)
        self.not_found(css_rule(self.page, r"html") or "", r"overscroll-behavior(?!-y)", "only the vertical axis (no shorthand, no -x): a desktop trackpad's horizontal swipe (back, forward) stays as it was")
        self.not_found(body, r"overscroll-behavior(?!-y)", "only the vertical axis (no shorthand, no -x): a desktop trackpad's horizontal swipe (back, forward) stays as it was")
        self.not_found(css_rule(self.page, r"html") or "", r"overflow\s*:\s*hidden", "the page itself must scroll: the guide is below the game")

    def test_a_long_press_on_the_game_is_no_callout_no_selection_no_flash(self):
        stage = css_rule(self.page, r"#game-stage")
        self.assertIsNotNone(stage)
        for declaration in ("-webkit-touch-callout: none;", "-webkit-user-select: none;", "user-select: none;", "-webkit-tap-highlight-color: transparent;"):
            self.assertIn(declaration, stage)

    def test_every_control_has_manipulation(self):
        style = "\n".join(re.findall(r"<style>(.*?)</style>", self.page, re.S))
        rules = re.findall(r"([^{}]+)\{([^{}]*)\}", style)
        controls = [(sel.strip(), body) for sel, body in rules if "cursor: pointer" in body]
        self.assertGreaterEqual(len(controls), 6, "the page has controls that this test must see")
        for selector, body in controls:
            if selector == ".tl":                                                                          # (the replay timeline is dragged with a finger: touch-action: none, by design)
                self.assertIn("touch-action: none", body)
                continue
            self.assertIn("touch-action: manipulation", body, selector + " is a control without touch-action: manipulation (no double-tap zoom on a phone)")
        # the buttons that the script makes itself
        for text in re.findall(r"cursor:pointer[^'\"]*", self.page):
            self.assertIn("touch-action:manipulation", text, "a button of the script's has no touch-action: " + text[:60])

    def test_the_viewport_still_forbids_scaling(self):
        meta = re.search(r'<meta name="viewport" content="([^"]*)">', self.page)
        self.assertIsNotNone(meta)
        for part in ("width=device-width", "initial-scale=1.0", "maximum-scale=1.0", "user-scalable=no", "viewport-fit=cover"):
            self.assertIn(part, meta.group(1))


class TheScript(PageCase):
    def setUp(self):
        self.page = read(SHELL)
        self.script = script_of(self.page)

    def test_the_block_of_the_guards_is_there_once(self):
        self.assertEqual(len(re.findall(r"// ANTS_TOUCH_BEGIN", self.page)), 1)
        self.assertEqual(len(re.findall(r"// ANTS_TOUCH_END", self.page)), 1)
        self.assertLess(self.page.index("// ANTS_TOUCH_BEGIN"), self.page.index("// ANTS_TOUCH_END"))

    def test_the_browsers_pinch_is_cancelled_on_the_canvas_and_the_document_and_not_passively(self):
        block = self.page[self.page.index("// ANTS_TOUCH_BEGIN"):self.page.index("// ANTS_TOUCH_END")]
        self.found(block, r"\['gesturestart', 'gesturechange', 'gestureend'\]\.forEach\(function \(name\) \{\s*document\.addEventListener\(name, .*\{ passive: false \}\);")
        for name in ("gesturestart", "gesturechange", "gestureend"):
            self.assertIsNotNone(re.search(r"canvas\.addEventListener\('%s',.*?\}, \{ passive: false \}\);" % name, block, re.S), "the canvas's %s is cancelled, not passively" % name)

    def test_the_game_is_told_of_a_touchcancel(self):
        block = self.page[self.page.index("// ANTS_TOUCH_BEGIN"):self.page.index("// ANTS_TOUCH_END")]
        self.found(block, r"window\.addEventListener\('touchcancel'")
        self.assertIn("Module._ants_touch_cancel", block)

    def test_a_touch_that_lands_alone_makes_the_game_forget_the_fingers_whose_lift_never_came(self):
        block = self.page[self.page.index("// ANTS_TOUCH_BEGIN"):self.page.index("// ANTS_TOUCH_END")]
        self.found(block, r"function othersOnTheBox\(e\)")
        self.assertIn("var all = e && e.touches;", block)                               # (the browser's own list of touches: a lift that was never delivered cannot corrupt it)
        handler = re.search(r"boxElement\.addEventListener\('touchstart', function \(e\) \{(.*?)\}, \{ capture: true, passive: true \}\);", block, re.S)
        self.assertIsNotNone(handler, "the box's touchstart listener is there, passive, capture")
        self.assertIsNotNone(re.search(r"if \(othersOnTheBox\(e\) === 0\) \{.*?live = \{\};.*?count = 0;.*?tellGame\(\);", handler.group(1), re.S), "a touch with no other touch of the game down starts the count again and tells the game")

    def test_the_context_menu_is_cancelled_over_the_game(self):
        block = self.page[self.page.index("// ANTS_TOUCH_BEGIN"):self.page.index("// ANTS_TOUCH_END")]
        self.found(block, r"boxElement\.addEventListener\('contextmenu', function \(e\) \{ e\.preventDefault\(\); \}\);")
        self.assertIn('oncontextmenu="event.preventDefault()"', self.page)                    # (the canvas's own, as before)

    def test_no_touchmove_listener_and_nothing_stops_a_touch(self):
        code = without_comments(self.script)
        self.not_found(code, r"addEventListener\(\s*['\"]touchmove['\"]", "the page adds no touchmove listener (it would have to be passive: false to cancel, and it cancels nothing)")
        for name in ("touchstart", "touchend", "touchcancel"):
            for listener in re.findall(r"addEventListener\(\s*['\"]%s['\"],(.*?)\n\s*\}\s*(?:,\s*\{[^}]*\})?\s*\);" % name, code, re.S):
                self.not_found(listener, r"stopPropagation|stopImmediatePropagation|preventDefault", "a listener of %s stops or cancels the touch" % name)

    def test_the_sounds_unlock_is_still_on_touchend(self):
        self.found(self.script, r"var events = \['pointerdown', 'mousedown', 'touchend', 'click', 'keydown'\];")
        self.found(self.script, r"events\.forEach\(function\(name\) \{ window\.addEventListener\(name, onPress, true\); \}\);")

    def test_the_gesture_conversion_to_the_wheel_stands_down_while_fingers_are_on_the_game(self):
        block = self.page[self.page.index("// ANTS_TOUCH_BEGIN"):self.page.index("// ANTS_TOUCH_END")]
        self.found(block, r"if \(count > 0\) return;")
        self.assertIn("new WheelEvent('wheel'", block)

    def test_the_fullscreen_button_is_the_pages_own_where_the_browser_has_none(self):
        # (what it does today, on an iPhone: no Fullscreen API for an element, so the page's own fullscreen with its button to leave it; nothing here changed it)
        self.assertIn("var request = stageElement.requestFullscreen || stageElement.webkitRequestFullscreen;", self.script)
        self.assertIn("if (!request) { setPageFullscreen(true); return; }", self.script)
        self.assertIn('id="pseudo-exit"', self.page)


class TheGuide(PageCase):
    def setUp(self):
        self.page = read(SHELL)
        panel = re.search(r'<details class="info-col" id="guide-touch" open>(.*?)</details>', self.page, re.S)
        self.assertIsNotNone(panel)
        self.touch = panel.group(1)

    def test_it_says_how_to_play_with_fingers(self):
        for text in ("<strong>Tap</strong>", "left click", "<strong>Hold</strong>", "right click", "the ring closes", "when you lift the finger", "Hold on the minimap",
                     "<strong>Drag</strong>", "selection box", "<strong>Move the map:</strong>", "drag with two fingers", "<strong>Zoom:</strong>", "pinch with two fingers",
                     "<strong>Minimap:</strong>", "<strong>Fullscreen:</strong>"):
            self.assertIn(text, self.touch, "the touch guide lacks: " + text)

    def test_the_old_advice_that_there_is_no_right_click_is_gone(self):
        self.not_found(self.touch, r"no right click")

    def test_the_rest_of_the_guide_is_as_it_was(self):
        self.assertIn("the mouse wheel (a trackpad's two-finger scroll or pinch) over the map zooms in and out towards the pointer", self.page)
        self.assertIn("<strong>Right click:</strong>", self.page)


class TheGame(PageCase):
    def test_the_game_exports_what_the_page_and_the_browser_check_use(self):
        source = read(APPLICATION)
        self.found(source, r'extern "C" EMSCRIPTEN_KEEPALIVE void ants_touch_cancel\(\)')
        for case in range(30, 37):
            self.found(source, r"case %d: return " % case, "ants_probe has no case %d for the touch model" % case)
        self.assertIn('SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS, "0", SDL_HINT_OVERRIDE);', source)       # (an environment variable of the player's must not bring the emulation back)

    def test_the_buzz_is_never_required(self):
        source = read(APPLICATION_TOUCH)
        self.assertIn("if (navigator.vibrate) navigator.vibrate(milliseconds);", source)
        self.found(source, r"try \{ if \(navigator\.vibrate\)")


class TheBrowserCheck(PageCase):
    """The opt-in script that drives the page in a real browser (tests/scripts/web_touch_check.py): what can be checked without a browser."""

    @classmethod
    def setUpClass(cls):
        import importlib.util
        spec = importlib.util.spec_from_file_location("web_touch_check", BROWSER_CHECK)
        cls.module = importlib.util.module_from_spec(spec)
        sys.path.insert(0, os.path.dirname(BROWSER_CHECK))
        try:
            spec.loader.exec_module(cls.module)
        finally:
            sys.path.pop(0)
        cls.source = read(BROWSER_CHECK)

    def test_it_reads_only_what_the_game_exports(self):
        game = read(APPLICATION)
        wanted = set(int(n) for n in re.findall(r"(?:probe|\bp)\((\d+)\)", self.source))
        self.assertTrue({3, 4, 5, 6, 30, 31, 32, 33, 34, 35, 36} <= wanted, "the check reads the view, the zoom and the touch model: %s" % sorted(wanted))
        for n in sorted(wanted):
            self.found(game, r"case %d: return " % n, "the check reads ants_probe(%d), which the game does not answer" % n)
        for name in ("_ants_touch_cancel", "_ants_match_running"):
            self.assertIn("Module." + name, self.source)

    def test_both_phones_are_there_with_what_makes_them_different(self):
        profiles = self.module.PROFILES
        self.assertEqual(sorted(profiles), ["iphone", "pixel"])
        pixel, iphone = profiles["pixel"], profiles["iphone"]
        self.assertIn("Android", pixel["user_agent"])
        self.assertTrue(pixel["fullscreen_api"] and pixel["vibrate"] and pixel["dpr"] == 2.625)
        self.assertIn("iPhone", iphone["user_agent"])
        self.assertFalse(iphone["fullscreen_api"] or iphone["vibrate"])

    def test_it_says_that_chromium_is_not_webkit(self):
        self.assertIn("Chromium, not WebKit", self.module.__doc__)
        self.assertIn("THIS ONLY PROVES THE PAGE'S OWN CODE PATHS FOR AN iPHONE", self.module.__doc__)

    def test_the_fingers_speak_the_protocol_as_chromium_understands_it(self):
        class FakeTab:
            def __init__(self):
                self.sent = []

            def call(self, method, params=None):
                assert method == "Input.dispatchTouchEvent"
                self.sent.append((params["type"], [(p["id"], p["x"], p["y"]) for p in params["touchPoints"]]))

        tab = FakeTab()
        fingers = self.module.Fingers(tab)
        fingers.down(1, 10, 20)
        fingers.down(2, 30, 40)
        fingers.move(a=(11, 21), b=(31, 41))
        fingers.up(1)
        fingers.up(2)
        fingers.down(1, 5, 6)
        fingers.cancel()
        self.assertEqual(tab.sent, [
            ("touchStart", [(1, 10, 20)]),
            ("touchStart", [(1, 10, 20), (2, 30, 40)]),                  # (the new point and the ones that are down)
            ("touchMove", [(1, 11, 21), (2, 31, 41)]),                   # (all of them, in one event)
            ("touchEnd", [(1, 11, 21)]),                                 # (only the point that lifts: the other stays)
            ("touchEnd", [(2, 31, 41)]),
            ("touchStart", [(1, 5, 6)]),
            ("touchCancel", []),                                         # (no points: all of them are cancelled)
        ])
        self.assertEqual(fingers.points, {})

    def test_the_runner_skips_without_a_page_and_says_so(self):
        if not shutil.which("bash"):
            self.skipTest("bash is not installed")
        env = dict(os.environ)
        env.pop("ANTS_WEB_URL", None)
        done = subprocess.run([shutil.which("bash"), BROWSER_RUNNER], capture_output=True, text=True, timeout=60, env=env)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("SKIP", done.stdout)
        self.assertIn("ANTS_WEB_URL", done.stdout)

    def test_the_options_window_is_closed_by_its_return_button_and_not_by_esc(self):
        # (the original's windows close with their own button: the first run of this check on the real game found that Esc does nothing there)
        x, y = self.module.options_return_button()
        self.assertEqual((x, y), (560.0, 468.0), "the card (16, 21, 442, 440) is centred in the 16:9 map view (16, 21, 762, 500): moved by (160, 30); the button is (351, 425, 98, 26) in it")
        card_x, card_y = 160 + 17, 30 + 20                                    # (the card at the window's own (17, 20), 442 x 440)
        self.assertTrue(card_x < x < card_x + 442 and card_y < y < card_y + 440, "the Return button is inside the options card")
        code = without_comments(self.source)
        self.not_found(code, r"Escape", "the check never leaves a window with Esc")
        self.assertIn("close_options", self.source)
        self.assertGreaterEqual(len(re.findall(r'begin\("', self.source)), 6, "every part of the gestures starts from a clean state")
        self.assertEqual([line for line in self.source.splitlines() if "check(" in line and "linger" in line], [], "no real-browser check of the 400 - 450 ms boundary (a loaded machine is late by more than its margin)")

    def test_the_hold_is_judged_only_where_our_clock_decides_it(self):
        # (the first run on a loaded machine found that the DevTools call that sends a touch takes hundreds of milliseconds to return, so sleeps say little of the game's own clock)
        judge = self.module.hold_expectation
        self.assertEqual(judge(0.20, 0.10), "waiting", "a finger that went down at the most 200 ms ago cannot have fired")
        self.assertEqual(judge(0.399, 0.0), "waiting")
        self.assertIsNone(judge(0.40, 0.0), "at 400 ms by the longest reckoning it may or may not have fired: no judgement")
        self.assertIsNone(judge(0.80, 0.15), "a touch that took 650 ms to send and was looked at 150 ms later: the same finger may be 150 or 800 ms old")
        self.assertIsNone(judge(0.80, 0.60), "at the least 600 ms is not yet 'surely fired' (the margin is the frame)")
        self.assertEqual(judge(0.90, 0.601), "fired")
        self.assertEqual(judge(1.5, 1.2), "fired")
        self.assertLess(self.module.SURELY_NOT_YET, self.module.HOLD_SECONDS)
        self.assertLess(self.module.HOLD_SECONDS, self.module.SURELY_BY_NOW)
        self.assertLessEqual(self.module.QUICK, 0.40, "a timed tap or drag must have been up or begun by the game's clock before the tap time (400 ms)")

    def test_the_counters_are_read_in_one_round_trip(self):
        calls = []

        class FakeTab:
            def ev(self, expression):
                calls.append(expression)
                return {}

        game = self.module.Game(FakeTab(), "http://example.invalid/", self.module.PROFILES["pixel"])
        game.counters()
        game.view()
        self.assertEqual(len(calls), 2, "one evaluation for each look (six of them were not a look at one moment)")
        for expression in calls:
            self.assertEqual(expression.count("_ants_probe"), 1)

    def test_a_bad_command_line_is_status_2_and_asks_for_no_browser(self):
        done = subprocess.run([sys.executable, BROWSER_CHECK, "--web", "http://127.0.0.1:9/", "--profile", "android-tablet"], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertIn("unknown profile", done.stdout)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the page's guards for a touch screen were NOT run (tests/scripts/web_touch_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_guards_with_fakes_for_the_page(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
