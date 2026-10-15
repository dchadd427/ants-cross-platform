#!/usr/bin/env python3
"""The pointer of a FULLSCREEN game page: the black bars, and the pointer lock, and the margin of a WINDOWED one (run by ./run_tests.sh --fast and by the CI). On a 16:10 screen the
16:9 picture has bars above and below, and a pointer that went over a bar no longer scrolled the map (the browser sends the game nothing there and says that the pointer LEFT the canvas); and on
a Mac a pointer at the screen's edge makes the Dock and the menu bar appear, which a web page can only prevent by locking the pointer. In a window the same loss: going off the screen with the
mouse must not spoil the scrolling; a pointer within 96 CSS pixels (about an inch) beyond the game's box still scrolls the map at the box's edge.

  - the page's own code is RUN (node, when it is installed): the pixel that the game reads for a pointer over a bar and the position that makes the game read it, the cursor of a locked
    pointer (its start, the motion by the mouse's distance, the edges, the corners, the slow mouse), the choice "Fullscreen mouse: Locked / Free" and its storage key (`ants.pointerlock`,
    only "off" turns the lock off) with fakes for the page, and the margin of a windowed page (the band around the box, the corners, 97 px beyond, the page's own controls: tests/scripts/web_edge_check.js);
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
        self.assertIn("releasesMouseLock: releasesMouseLock", self.page)
        self.assertIn("if (!isLocked() || !ANTS_PAGE.releasesMouseLock(e)) return;", self.page)
        self.assertIn("if (!isRealFullscreen() && !inMatch) return;", self.page)

    def test_the_game_is_not_told_of_the_lock_and_gets_the_pages_cursor(self):
        self.found(self.page, r"\['pointerlockchange', 'webkitpointerlockchange', 'mozpointerlockchange'\]\.forEach")
        self.found(self.page, r"window\.addEventListener\(name, function \(e\) \{\s*e\.stopPropagation\(\);")           # (window capture: the game's window system never hears of the lock)
        self.assertIn("ANTS_PAGE.moveLocked(", self.page)
        self.assertIn("ANTS_PAGE.lockStart(", self.page)

    def test_a_windowed_page_is_left_alone_by_the_bars_and_the_lock(self):
        """Every listener of the bars and the lock starts with a test of fullscreen or of the lock itself (the margin's own listeners, which come after them, are checked below)."""
        for name in ("pointermove", "mouseleave"):
            body = re.search(r"window\.addEventListener\('" + name + r"', function \(e\) \{\n(.*?)\n        \}, true\);", self.page, re.S)
            self.assertIsNotNone(body, name)
            self.assertIn("isFullscreen()", body.group(1), name)
        wheel = re.search(r"stageElement\.addEventListener\('wheel', function \(e\) \{ (.*?) \}, \{ passive: false \}\);", self.page)
        self.assertIsNotNone(wheel)
        self.assertIn("isFullscreen()", wheel.group(1))


class TheReplayIsLockedLikeTheGame(PageCase):
    """A replay (and a live match watched, the same page) takes the pointer lock in fullscreen as the game does, and its bar is reached through the game's cursor (the browser check says whether
    it works; here the pieces cannot silently disappear)."""

    def setUp(self):
        self.page = read(SHELL)

    def test_replay_mode_no_longer_switches_the_lock_off(self):
        request = re.search(r"function requestLock\(startAt\) \{[^\n]*\n(.*?)\n        \}", self.page, re.S)
        self.assertIsNotNone(request)
        self.assertNotIn("ANTS_REPLAY_MODE", request.group(1))
        self.assertIn("if (!ANTS_POINTER_LOCK || !request || pageFullscreen || isLocked()) return false;", request.group(1))
        change = re.search(r"document\.addEventListener\(name, function \(\) \{\n\s*if \(!isRealFullscreen\(\)\) \{ fullscreenEnded\(\); return; \}\n(.*?)\n            \}\);", self.page, re.S)
        self.assertIsNotNone(change)
        self.assertNotIn("ANTS_REPLAY_MODE", change.group(1))
        self.assertIn("lockKeys(true);", change.group(1))
        self.assertIn("requestLock(null);", change.group(1))

    def test_the_bar_is_reached_through_the_locked_cursor(self):
        """The logic is web/replay_page.js (barPointer, run on fakes by tests/scripts/web_replay_check.js); here the page's glue cannot silently disappear."""
        self.assertIn("var barLock = window.AntsReplay.barPointer({", self.page)
        self.assertIn("lockCursor = ANTS_PAGE.moveLocked(lockCursor, e.movementX, e.movementY, rect.width, rect.height); if (ANTS_REPLAY_MODE && barLock.move(e.buttons)) return;", self.page)
        self.assertIn("if (!barLock.mode()) handToGame('mousemove', { x: Math.trunc(lockCursor.x), y: Math.trunc(lockCursor.y) }, 0);", self.page)       # (a resize does not hand the game a cursor that is on the bar)
        self.assertIn("lockCursor = null; lockSize = null; barLock.reset();", self.page)                                                          # the lock let go clears it (and cancels a drag)
        self.assertIn("barLock.hideMark();", self.page)                                                                                        # the bar going away takes the arrow with it
        self.assertIn("ev.type === 'pointermove' && ev.movementX === 0 && ev.movementY === 0 && isLocked()", self.page)                       # a locked browser's repeated places are not a touch: the bar still goes away
        self.assertIn("home = over ? (isLocked() ? boxElement : stageElement) : null", self.page)                                              # a locked pointer reaches the bar inside the picture, on any screen shape
        self.assertIn("window.antsReplayPlaceBar();", self.page.split("['pointerlockchange', 'webkitpointerlockchange', 'mozpointerlockchange'].forEach")[1][:600])
        self.assertIn("barPointer: barPointer", read(os.path.join(REPO, "web", "replay_page.js")))

    def edge_rule(self, page):
        """The page's own expression for 'the locked cursor is at the picture's outermost pixels', made a function of (x, y, width, height)."""
        found = re.search(r"edge: ([^}]*?) \}; \} \}\);", page)
        self.assertIsNotNone(found)
        code = re.sub(r"lockCursor\.(x|y)", r"\1", found.group(1)).replace("r.width", "w").replace("r.height", "h").replace("||", " or ")
        return lambda x, y, w, h: eval(code, {"x": x, "y": y, "w": w, "h": h})

    def test_the_outermost_pixel_on_every_side_is_the_games_even_where_the_bar_lies(self):
        w, h = 960, 540
        for rule in (self.edge_rule(self.page),):
            self.assertFalse(rule(480, 270, w, h))
            for x, y, edge in ((0.5, 270, True), (1, 270, False), (w - 1.5, 270, False), (w - 1, 270, True), (w - 0.25, 270, True),       # left and right
                               (480, 0.5, True), (480, 1, False), (480, h - 1.5, False), (480, h - 1, True), (480, h - 0.25, True)):         # top and bottom
                self.assertEqual(rule(x, y, w, h), edge, (x, y))
        # the same checks refuse a rule that lost a side or moved a bound
        for old, new in (("lockCursor.x >= r.width - 1", "lockCursor.x > r.width"), ("lockCursor.y >= r.height - 1", "lockCursor.y > r.height"), ("lockCursor.x < 1 ||", "lockCursor.x < 0 ||"),
                         ("lockCursor.y < 1 ||", "lockCursor.y < 0 ||"), ("lockCursor.x >= r.width - 1", "lockCursor.x >= r.width - 2"), ("lockCursor.y >= r.height - 1", "lockCursor.y >= r.height - 2")):
            self.assertEqual(self.page.count(old), 1, old)
            rule = self.edge_rule(self.page.replace(old, new))
            wrong = [(x, y) for x, y, edge in ((0.5, 270, True), (1, 270, False), (w - 1.5, 270, False), (w - 1, 270, True), (480, 0.5, True), (480, 1, False), (480, h - 1.5, False), (480, h - 1, True))
                     if rule(x, y, w, h) != edge]
            self.assertTrue(wrong, "the check accepted a rule where " + old + " became " + new)

    def test_the_guides_say_so(self):
        self.assertNotIn("pointer lock is off in a replay", read(os.path.join(REPO, "docs", "REPLAYS.md")))
        self.assertIn("locked", read(os.path.join(REPO, "docs", "REPLAYS.md")).split("**The same game in the same browser as ever:**")[1].split("\n")[0])


class TheWindowedMargin(PageCase):
    """The margin of a windowed page: a pointer outside the game's box but within 96 CSS pixels of it still counts as at the picture's edge (a browser check says whether it works; here the
    pieces that it stands on cannot silently disappear)."""

    def setUp(self):
        self.page = read(SHELL)

    def listener(self, name, nth):
        found = re.findall(r"window\.addEventListener\('" + name + r"', function \(e\) \{[^\n]*\n(.*?)\n        \}, true\);", self.page, re.S)
        self.assertEqual(len(found), 2, name + ": the bars' listener and the margin's")
        return found[nth]

    def test_the_margin_is_one_inch_in_one_constant(self):
        self.assertEqual(len(re.findall(r"var ANTS_EDGE_MARGIN = 96;", self.page)), 1)
        self.assertGreaterEqual(len(re.findall(r"\bANTS_EDGE_MARGIN\b", self.page)), 4)           # (the constant, the motion, the leave, the comment)
        self.assertIn("ANTS_PAGE.marginPixel(rect.left, rect.top, rect.width, rect.height, x, y, ANTS_EDGE_MARGIN)", self.page)
        self.assertIn("ANTS_PAGE.marginPixel(rect.left, rect.top, rect.width, rect.height, e.clientX, e.clientY, ANTS_EDGE_MARGIN)", self.page)

    def test_the_two_rules_are_exported_and_the_controls_are_named(self):
        self.assertIn("marginPixel: marginPixel, overControl: overControl", self.page)
        controls = re.search(r"var CONTROLS = '([^']*)';", self.page)
        self.assertIsNotNone(controls)
        for tag in ("a", "button", "input", "select", "textarea", "summary", "label", '[role="radio"]'):
            self.assertIn(tag, [part.strip() for part in controls.group(1).split(",")], tag)
        self.assertEqual(len(re.findall(r"ANTS_PAGE\.overControl\(", self.page)), 2)               # (the motion's target and the leave's relatedTarget)

    def test_the_motion_is_handed_over_and_a_leave_beyond_the_margin_or_over_a_control_is_too(self):
        move = self.listener("pointermove", 1)
        for needle in ("handingOver", "e.pointerType === 'touch'", "isLocked()", "isFullscreen()", "windowedPointer(", "primeFocusOverTheGame(e)"):
            self.assertIn(needle, move)
        function = re.search(r"function windowedPointer\(x, y, target, buttons\) \{\n(.*?)\n        \}\n", self.page, re.S)
        self.assertIsNotNone(function)
        self.assertIn("handToGame('mousemove', pixel, buttons);", function.group(1))
        self.assertIn("edgeForwarded = true;", function.group(1))
        self.assertIn("handLeaveToGame();", function.group(1))                                   # beyond the margin, over a control: the pointer is gone

    def test_the_canvas_leave_towards_the_margin_is_not_passed_on_but_a_leave_out_of_the_window_is(self):
        leave = self.listener("mouseleave", 1)
        for needle in ("handingOver", "e.target !== canvas", "!e.relatedTarget", "isLocked()", "isFullscreen()", "e.stopPropagation()"):
            self.assertIn(needle, leave)
        self.assertIn("ANTS_PAGE.overControl(e.relatedTarget)", leave)
        self.assertIn("document.documentElement.addEventListener('mouseleave'", self.page)       # out of the browser window: gone
        self.found(self.page, r"documentElement\.addEventListener\('mouseleave', function \(\) \{[^\n]*\n[^\n]*\n\s*if \(!isFullscreen\(\) && edgeForwarded\) handLeaveToGame\(\);")

    def test_a_scroll_or_a_resize_under_a_pointer_that_did_not_move_is_looked_at_again(self):
        self.assertIn("window.addEventListener('scroll', windowedAgain, { passive: true });", self.page)
        self.assertIn("window.addEventListener('resize', windowedAgain);", self.page)
        again = re.search(r"function windowedAgain\(\) \{[^\n]*\n(.*?)\n        \}\n", self.page, re.S)
        self.assertIsNotNone(again)
        self.assertIn("document.elementFromPoint(", again.group(1))

    def test_the_first_motion_over_the_game_is_preceded_by_a_leave_so_that_the_first_real_leave_is_delivered(self):
        function = re.search(r"function primeFocusOverTheGame\(e\) \{\n(.*?)\n        \}\n", self.page, re.S)
        self.assertIsNotNone(function)
        self.assertIn("if (focusPrimed || !isReadyToPlay) return;", function.group(1))
        self.assertIn("focusPrimed = true;", function.group(1))
        self.assertIn("new MouseEvent('mouseleave'", function.group(1))

    def test_a_click_the_wheel_and_the_menu_in_the_margin_are_left_to_the_page(self):
        """Nothing in the margin's code cancels an event (the page keeps its clicks, its wheel and its menu): no preventDefault in the margin's section."""
        start = self.page.index("THE MARGIN OF A WINDOWED PAGE (requested: the mouse can go off the screen without breaking the scrolling; agreed")
        end = self.page.index("// THE POINTER LOCK. On the Mac")
        section = self.page[start:end]
        self.not_found(section, r"preventDefault\(\)")
        for name in ("wheel", "contextmenu", "mousedown", "click"):
            self.assertNotIn("'" + name + "'", section, name)

    def test_the_guide_tells_the_player(self):
        self.assertIn("In a window the map keeps scrolling while the pointer is just past the edge of the game (about an inch)", self.page)

    def test_the_browser_page_and_the_notes_say_what_the_margin_is(self):
        page = read(os.path.join(REPO, "docs", "PLAY_IN_BROWSER.md"))
        self.assertIn("The margin of a windowed page", page)
        self.assertIn("ANTS_EDGE_MARGIN", page)
        self.assertIn("test_web_edge.sh --only margin", page)
        self.assertIn("up to 96 CSS pixels (about an inch) beyond the box", page)


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
