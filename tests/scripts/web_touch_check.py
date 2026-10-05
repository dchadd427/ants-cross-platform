#!/usr/bin/env python3
"""The TOUCH CONTROLS of the game page in a REAL browser (opt-in; see tests/scripts/test_web_touch.sh and docs/TOUCH.md).

Needs a running web page (the web image of this tree: `docker build -t ants-beta .` and run it on a port; the game server is not needed), a Chromium-based browser and Python 3; nothing
else (the DevTools protocol is spoken with the client of web_hidden_check.py, standard library only). The check opens the page in a throwaway headless browser (its own profile, its
own port; nothing of yours is touched) as a PHONE: a touch screen (the DevTools protocol's multi-touch: Input.dispatchTouchEvent with several touch points), mobile metrics, the
device pixel ratio and the user agent of the phone. It starts a match (Enter at the quick help, Enter at the setup screen: a keyboard is not what is tested) and reads what the GAME
believes through the game's `ants_probe` (the map view's origin, the zoom, whether a dialog is open, and the touch model's counters: taps, holds, drags, two-finger gestures, the fingers
that are tracked, what it is doing, the slop). Two profiles, each in portrait (the whole game box is on screen) and then in the page's FULLSCREEN:

  * pixel: a Pixel-like phone, Android Chrome (412 x 915, ratio 2.625, a touch UA; the Fullscreen API and navigator.vibrate exist and vibrate is recorded);
  * iphone: an iPhone-like one (390 x 844, ratio 3, the iPhone's UA and platform; NO Fullscreen API for an element and NO navigator.vibrate, as on Safari: the page's own fullscreen
    and a hold that must not need the buzz). THIS ONLY PROVES THE PAGE'S OWN CODE PATHS FOR AN iPHONE (what the page does where those two things are missing); the engine under
    it is Chromium, not WebKit: Safari's own touch events, its gesture events, double-tap zoom and callout are NOT tested here (the guards for them are in the page: web/shell.html,
    ANTS_TOUCH_BEGIN, and listed as unverified in docs/TOUCH.md).

What is checked, for each profile:
  * the PAGE's guards, in a real browser: the canvas takes the browser's touch away (touch-action: none: a swipe on it does not scroll the page) and the page keeps its own scrolling (a
    swipe on the guide does); nothing zooms (no page zoom by a pinch or a double tap); the browser's pinch (gesturestart / gesturechange / gestureend) and the context menu are cancelled
    over the game and left alone elsewhere; a touchcancel reaches the game (Module._ants_touch_cancel) and the game drops its fingers; the slop the game uses is 8 CSS pixels of the box
    in picture pixels (never under 6 device pixels);
  * the GAME's gestures through the page: a TAP on the map (a click), on a HUD button (the Options button opens its window; a tap on its Return button closes it: the original's windows
    close with their own button, not Esc) with the game's own model counting the taps, a HOLD (the right click: counted at 450 ms, its feedback is the buzz where the browser has one,
    nothing where it has not), a hold that moves becomes a drag (counted as one), a DRAG, TWO FINGERS that pan the map by the distance they move (the picture follows them: the view's
    origin moves by the distance in picture pixels) and pinch it a level at a time (200 % when the fingers spread to twice, 50 % back), nothing pans or zooms under a dialog, a cancel
    leaves nothing held. A touch is timed by the game's clock, not by our sleeps (the DevTools call that sends a touch returns when the page has acknowledged it, which takes hundreds of milliseconds on a loaded machine
    or at a phone's device ratio of 3): a check that depends on the hold time is made only when our wall clock proves what the game's must say (a tap, a drag or a cancel is tried again unless the whole
    touch took under 360 ms; the first look at a resting finger says "waiting" only if it can have been down at the most 400 ms, and "fired" only if at least 600), and a note says what was seen when it
    does not. The boundaries (a lift between 400 and 450 ms is neither) are the model's tests'. Every part starts from a clean state (no finger down, no window open),
    so that one failure cannot make the next parts fail;
  * the same in the page's FULLSCREEN (the browser's own on a Pixel; the page's own where there is no Fullscreen API: an iPhone), where the box is another size.
What headless Chrome cannot show, and the owner's phone must: that the first touch of a page grants the sound (the autoplay policy is switched off for the check), the system's own gestures
(Android's back swipe from an edge, iOS's swipe from the left edge, pull to refresh: the page's overscroll-behavior-y only asks for none), a finger's real width (a thumb's resting
touch that holds, a palm), the real multi-touch glass (a finger's jitter within the slop), Safari and Firefox (WebKit's gesture events and callout), the vibration motor.

Exit status 0: every check passed; 1: a check failed, or the browser or the page broke down during the check (a page that came up and then misbehaved is a failure, never a skip); 3: the
check could not be made because the environment is not there (no browser, nothing answers at the page's address); 2 is the status of a bad command line.
"""
import argparse
import json
import math
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_aspect_check as aspect                                            # noqa: E402  (the DevTools driver of the picture check)
from web_aspect_check import Browser, NotReachable, Tab, screen_of_canvas    # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

CANVAS_W, CANVAS_H = 960, 540                                                # the game's canvas (?aspect=16:9)
OPTIONS_BUTTON = (845 + 26, 7 + 11)                                          # the Options button of the 16:9 picture, its middle
MAP_VIEW = (16, 21, 762, 500)                                                # the map view of the 16:9 picture: x, y, width, height



def options_return_button():
    """Where a person taps the Return button of the options window, in the canvas's own pixels (the middle of its rectangle), for the 16:9 picture: the window is the original's 442 x 440 card
    centred in the map view (ScreenLayout::options_offset), and it closes by that button and by nothing else (not Esc: the original's windows close with their own button). The numbers are read
    from the headers, so that a change of the layout moves the check with it."""
    include = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "include", "ants_app")
    with open(os.path.join(include, "options_screen.hpp"), encoding="utf-8") as f:
        button = re.search(r"RETURN_X = (\d+), RETURN_Y = (\d+), RETURN_W = (\d+), RETURN_H = (\d+);", f.read())
    with open(os.path.join(include, "screen_layout.hpp"), encoding="utf-8") as f:
        layout = f.read()
    card = [re.search(r"kClassicView%s = (\d+);" % axis, layout) for axis in "XYWH"]
    if button is None or None in card:
        raise RuntimeError("the options window's Return button or its card is not in include/ants_app any more: the check cannot find where to tap")
    cx, cy, cw, ch = (int(m.group(1)) for m in card)
    x, y, w, h = (int(v) for v in button.groups())
    vx, vy, vw, vh = MAP_VIEW
    offset_x = vx + vw // 2 - (cx + cw // 2)                                 # (integer divisions, as the layout's)
    offset_y = vy + vh // 2 - (cy + ch // 2)
    return offset_x + x + w / 2, offset_y + y + h / 2


# TIME. The game stamps a touch when the page hands it over, and the DevTools call that sends a touch returns when the page has acknowledged it, which on a loaded machine (the game at 13 frames a
# second, a phone's device ratio of 3) takes hundreds of milliseconds: so our sleeps say little about how long the game thinks a finger has been down. What can be said for sure is a pair of
# bounds on the game's clock for a finger: it has run at the most from before the touch was sent to the end of the look at the game, and at the least from the end of the sending to the start of
# the look. A check is made only where the bounds decide it; else it is not made and a note says what was seen (the boundaries themselves are the model's tests').
HOLD_SECONDS = 0.45                 # touch::kHoldMs
SURELY_NOT_YET = 0.40               # a finger whose hold clock has run at the most this long cannot have fired its hold
SURELY_BY_NOW = 0.60                # one whose clock has run at least this long has (a frame later: the margin is the frame)
QUICK = 0.36                        # a tap or a drag that is timed is tried again unless the whole touch (sent, held, lifted) took less than this by our clock (a tap is up before 400 ms)


def hold_expectation(latest, earliest):
    """What the game must say of a finger that rests on the map: "waiting" (no hold yet), "fired" (the hold has fired: mode 3, one more hold), or None when our clock cannot tell.
    `latest` is the most that the finger's hold clock can have run, `earliest` the least (seconds)."""
    if latest < SURELY_NOT_YET:
        return "waiting"
    if earliest > SURELY_BY_NOW:
        return "fired"
    return None


PROFILES = {
    "pixel": {
        "label": "Pixel-like (Android Chrome)",
        "size": (412, 915), "dpr": 2.625,
        "user_agent": "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Mobile Safari/537.36",
        "platform": "Linux armv8l",
        "fullscreen_api": True, "vibrate": True,
    },
    "iphone": {
        "label": "iPhone-like (the page's code paths for Safari's size, user agent and missing APIs: Chromium underneath, not WebKit)",
        "size": (390, 844), "dpr": 3.0,
        "user_agent": "Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1",
        "platform": "iPhone",
        "fullscreen_api": False, "vibrate": False,
    },
}

# What runs in the page before its own scripts: the recorders and the missing APIs of the profile
BEFORE_THE_PAGE = r"""
(function () {
  window.__buzz = [];
  window.__errors = [];
  window.__cancels = 0;
  window.addEventListener('error', function (e) { if (!/^ResizeObserver loop/.test(String(e.message))) window.__errors.push(String(e.message)); });      // (that one is the browser's own note on a layout that settles in two steps)
  window.addEventListener('unhandledrejection', function (e) { window.__errors.push('rejection: ' + e.reason); });
  var PROFILE = %s;
  // No phone has the Keyboard Lock API (the page asks for it in fullscreen, for Esc); headless desktop Chrome's keyboard lock stalls the DevTools touch events once a touch made the fullscreen
  Object.defineProperty(Navigator.prototype, 'keyboard', { value: undefined, configurable: true });
  if (PROFILE.vibrate) navigator.vibrate = function (ms) { window.__buzz.push(ms); return true; };
  else Object.defineProperty(Navigator.prototype, 'vibrate', { value: undefined, configurable: true, writable: true });
  if (!PROFILE.fullscreen_api) {
    Element.prototype.requestFullscreen = undefined;
    Element.prototype.webkitRequestFullscreen = undefined;
    Element.prototype.requestPointerLock = undefined;                 // (nor does Safari on an iPhone have a pointer lock)
    try { Object.defineProperty(document, 'fullscreenEnabled', { get: function () { return false; } }); } catch (e) { /* stays */ }
  }
})();
"""


def wait_for(condition, timeout=4.0, step=0.05):
    deadline = time.time() + timeout
    value = condition()
    while not value and time.time() < deadline:
        time.sleep(step)
        value = condition()
    return value


class Fingers:
    """Touch points on the phone's glass (Input.dispatchTouchEvent): every call carries the points that changed, as the protocol says: a touchStart with the new point and the ones that
    are down, a touchMove with all of them, a touchEnd with the points that lift (the others stay), a touchCancel with none."""

    def __init__(self, tab):
        self.tab = tab
        self.points = {}

    def _point(self, i):
        x, y = self.points[i]
        return {"x": x, "y": y, "id": i, "radiusX": 12, "radiusY": 12, "force": 0.5}

    def down(self, i, x, y):
        self.points[i] = (x, y)
        self.tab.call("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [self._point(k) for k in sorted(self.points)]})

    def move(self, **where):
        """move(a=(x, y), b=(x, y)): the named fingers (ids are the numbers 1, 2, 3: a=1, b=2, c=3) go there, all in one event"""
        names = {"a": 1, "b": 2, "c": 3}
        for name, (x, y) in where.items():
            self.points[names[name]] = (x, y)
        self.tab.call("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": [self._point(k) for k in sorted(self.points)]})

    def up(self, i):
        point = self._point(i)
        del self.points[i]
        self.tab.call("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": [point]})

    def up_all(self):
        for i in sorted(self.points):
            self.up(i)

    def cancel(self):
        self.tab.call("Input.dispatchTouchEvent", {"type": "touchCancel", "touchPoints": []})
        self.points.clear()


class Game:
    """A tab with the game page as a phone, and what the game believes (ants_probe)."""

    def __init__(self, tab, web, profile):
        self.tab = tab
        self.web = (web if web.endswith("/") else web + "/") + "play.html"
        self.profile = profile
        self.fingers = Fingers(tab)

    def probe(self, what):
        return self.tab.ev("Module._ants_probe(%d)" % what)

    def counters(self):
        """The touch model's state, read in ONE round trip (a look at the game that takes six is not a look at one moment)"""
        return self.tab.ev("(function () { var p = Module._ants_probe; return {taps: p(30), holds: p(31), drags: p(32), two: p(33), fingers: p(34), mode: p(35)}; })()")

    def view(self):
        return self.tab.ev("(function () { var p = Module._ants_probe; return {x: p(3), y: p(4), zoom: p(6), modal: p(5)}; })()")

    def open(self, query="?aspect=16:9"):
        p = self.profile
        width, height = p["size"]
        self.tab.emulate(width, height, p["dpr"], mobile=True)
        self.tab.call("Emulation.setUserAgentOverride", {"userAgent": p["user_agent"], "platform": p["platform"]})
        self.tab.call("Page.addScriptToEvaluateOnNewDocument", {"source": BEFORE_THE_PAGE % json.dumps({"vibrate": p["vibrate"], "fullscreen_api": p["fullscreen_api"]})})
        self.tab.open(self.web + query, settle=0.5)
        time.sleep(2.0)

    def start_match(self):
        self.tab.ev("document.getElementById('canvas').focus(); 1")
        key = lambda kind: self.tab.call("Input.dispatchKeyEvent", {"type": kind, "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13, "text": "\r" if kind == "keyDown" else ""})
        for _ in range(2):                                                   # the quick help's Enter (the setup screen), the setup screen's Enter (START)
            key("keyDown")
            key("keyUp")
            time.sleep(1.5)
        if not wait_for(lambda: self.tab.ev("Module._ants_match_running()") == 1, 20):
            return False
        return bool(wait_for(lambda: self.probe(5) == 0, 60, 0.5))             # the "get ready" dialog closes by itself

    def geometry(self):
        g = self.tab.geometry()
        return g

    def scroll_to_box(self):
        """The game's box fully inside the window (a touch outside the window is nobody's)"""
        self.tab.ev("window.scrollTo(0, 0); 1")
        g = self.geometry()
        box = g["box"]
        if box[1] + box[3] > g["inner"][1] or box[1] < 0:
            self.tab.ev("window.scrollTo(0, Math.max(0, document.getElementById('game-container').getBoundingClientRect().top + window.scrollY - 8)); 1")
            time.sleep(0.3)
            g = self.geometry()
        return g

    def at(self, g, lx, ly):
        """Where a point of the canvas (the picture's pixel) is in the window (CSS pixels)"""
        return screen_of_canvas(g, lx, ly, CANVAS_W, CANVAS_H)

    def tap(self, x, y, hold=0.08):
        self.fingers.down(1, x, y)
        time.sleep(hold)
        self.fingers.up(1)

    def close_options(self, g):
        """Close the options window the way the game does: a tap on its Return button. True when the game says that no window is open any more."""
        x, y = self.at(g, *options_return_button())
        self.tap(x, y)
        return bool(wait_for(lambda: self.probe(5) == 0, 3.0))

    def clean(self, g):
        """Nothing is down and no window is open: a part that failed must not leave the next one under a window that refuses its fingers."""
        if self.fingers.points:
            self.fingers.cancel()
        if self.probe(5) == 1:
            self.close_options(g)
        wait_for(lambda: self.probe(34) == 0 and self.probe(35) == 0, 3.0, 0.05)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the image, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 120)")
    ap.add_argument("--profile", default="all", help="pixel, iphone or all (default)")
    ap.add_argument("--only", default="", help="run only the parts whose name contains this text (page, scroll, taps, hold, drag, pan, pinch, gates, cancel, slop, fullscreen)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout
    if args.profile != "all" and args.profile not in PROFILES:
        print("unknown profile %r (pixel, iphone or all)" % args.profile)
        return 2

    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    def wanted(name):
        return not args.only or args.only in name

    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    browser = None
    try:
        browser = Browser(path)
        for name in (sorted(PROFILES) if args.profile == "all" else [args.profile]):
            profile = PROFILES[name]
            print("[web touch] %s: %s, %d x %d, ratio %g" % (name, profile["label"], profile["size"][0], profile["size"][1], profile["dpr"]))
            tab = Tab(browser)
            game = Game(tab, args.web, profile)
            fingers = game.fingers
            try:
                run_profile(name, profile, tab, game, fingers, check, wanted, args)
            finally:
                tab.close()
    except NotReachable as e:
        if not count[0]:
            print("  SKIP: %s" % e)
            return 3
        print("  FAIL: %s" % e)
        return 1
    except (RuntimeError, TimeoutError, ConnectionError, OSError, ValueError) as e:
        print("  FAIL: the browser or the page broke down during the check: %s" % e)
        return 1
    finally:
        if browser is not None:
            browser.close()
    print("[web touch] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


def run_profile(name, profile, tab, game, fingers, check, wanted, args):
    def shot(label):
        tab.save_shot(args.shots, "touch_%s_%s" % (name, label))

    game.open()
    g = game.scroll_to_box()
    check(bool(g["ready"]) and g["dpr"] == profile["dpr"] and g["inner"][0] == profile["size"][0], "%s: the page is ready as a phone (a window of %s CSS pixels, ratio %g)" % (name, g["inner"], g["dpr"]))
    check(tab.ev("navigator.maxTouchPoints > 0 && 'ontouchstart' in window") is True, "%s: the browser has a touch screen (maxTouchPoints, ontouchstart)" % name)
    check(tab.ev("navigator.userAgent") == profile["user_agent"], "%s: the user agent is the phone's" % name)
    if not profile["fullscreen_api"]:
        check(tab.ev("typeof Element.prototype.requestFullscreen") == "undefined", "%s: the browser has no Fullscreen API for an element (as an iPhone's)" % name)
    ok = game.start_match()
    check(ok, "%s: Enter at the quick help and at the setup screen start a match, and its 'get ready' dialog closes" % name)
    if not ok:
        return
    g = game.scroll_to_box()
    box = g["box"]
    shot("match")

    def pic_per_css():
        return CANVAS_W / game.geometry()["box"][2]

    # ------------------------------------------------------------------------------------------------------------------------------------------------------------
    # the page's guards
    # ------------------------------------------------------------------------------------------------------------------------------------------------------------
    if wanted("page"):
        print("[web touch] %s: the page's guards" % name)
        styles = json.loads(tab.ev("""JSON.stringify((function () {
            var cs = function (e) { return getComputedStyle(e); };
            var canvas = document.getElementById('canvas'), stage = document.getElementById('game-stage');
            return { canvas: cs(canvas).touchAction, html: cs(document.documentElement).touchAction, htmlOver: cs(document.documentElement).overscrollBehaviorY, bodyOver: cs(document.body).overscrollBehaviorY,
                     htmlOverX: cs(document.documentElement).overscrollBehaviorX, bodyOverX: cs(document.body).overscrollBehaviorX,
                     stageSelect: cs(stage).userSelect, stageTap: cs(stage).webkitTapHighlightColor, scale: window.visualViewport.scale };
        })())"""))
        check(styles["canvas"] == "none", "%s: the canvas has touch-action: none (%s)" % (name, styles["canvas"]))
        check(styles["html"] == "manipulation", "%s: the page has touch-action: manipulation (panning and pinch stay, double-tap zoom goes: %s)" % (name, styles["html"]))
        check(styles["htmlOver"] == "none" and styles["bodyOver"] == "none", "%s: overscroll-behavior-y is none on the page (%s, %s)" % (name, styles["htmlOver"], styles["bodyOver"]))
        check(styles["htmlOverX"] == "auto" and styles["bodyOverX"] == "auto", "%s: the horizontal axis is left as it was (a desktop trackpad's swipe back and forward: %s, %s)" % (name, styles["htmlOverX"], styles["bodyOverX"]))
        check(styles["stageSelect"] == "none", "%s: the game's stage is not selectable (%s)" % (name, styles["stageSelect"]))
        check(styles["scale"] == 1, "%s: the page is not scaled (%s)" % (name, styles["scale"]))
        buttons = json.loads(tab.ev("""JSON.stringify(Array.prototype.map.call(document.querySelectorAll('button'), function (b) { return [b.id || b.className || b.textContent.slice(0, 20), getComputedStyle(b).touchAction, b.offsetParent !== null]; }))"""))
        wrong = [b for b in buttons if b[2] and b[1] != "manipulation"]
        check(len(buttons) > 3 and not wrong, "%s: every visible button has touch-action: manipulation (%d buttons; wrong: %s)" % (name, len(buttons), wrong))
        # the browser's own pinch and the menu: cancelled over the game, left alone elsewhere
        events = json.loads(tab.ev("""JSON.stringify((function () {
            var out = {};
            function fire(label, target, type, ctor) {
                var e = new (ctor || Event)(type, { bubbles: true, cancelable: true });
                target.dispatchEvent(e);
                out[label] = e.defaultPrevented;
            }
            var canvas = document.getElementById('canvas'), stage = document.getElementById('game-container');
            var elsewhere = document.querySelector('footer, #guide-touch, .info-col') || document.body;
            ['gesturestart', 'gesturechange', 'gestureend'].forEach(function (t) { fire(t + ' on the canvas', canvas, t); fire(t + ' elsewhere', elsewhere, t); });
            fire('contextmenu on the canvas', canvas, 'contextmenu', MouseEvent);
            fire('contextmenu on the stage', stage, 'contextmenu', MouseEvent);
            fire('contextmenu elsewhere', elsewhere, 'contextmenu', MouseEvent);
            return out;
        })())"""))
        for t in ("gesturestart", "gesturechange", "gestureend"):
            check(events[t + " on the canvas"] is True and events[t + " elsewhere"] is False, "%s: %s is cancelled over the canvas (%s) and left alone elsewhere (%s)" % (name, t, events[t + " on the canvas"], events[t + " elsewhere"]))
        check(events["contextmenu on the canvas"] is True and events["contextmenu on the stage"] is True and events["contextmenu elsewhere"] is False,
              "%s: the context menu is cancelled over the game's box (%s, %s) and left alone elsewhere (%s)" % (name, events["contextmenu on the canvas"], events["contextmenu on the stage"], events["contextmenu elsewhere"]))
        check(not tab.ev("window.__errors.length"), "%s: the page has raised no error so far (%s)" % (name, tab.ev("JSON.stringify(window.__errors)")))

    if wanted("slop"):
        print("[web touch] %s: the slop is a size on the glass" % name)
        g = game.geometry()
        per_css = CANVAS_W / g["box"][2]
        want = max(1.0, min(64.0, max(8.0 * per_css, 6.0 * per_css / g["dpr"])))
        got = game.probe(36) / 100.0
        check(abs(got - want) <= 0.02 * want + 0.02, "%s: the slop is 8 CSS pixels of the box in picture pixels: %.2f (the box is %.1f CSS pixels wide: %.3f picture pixels each, so %.2f)" % (name, got, g["box"][2], per_css, want))

    # ------------------------------------------------------------------------------------------------------------------------------------------------------------
    # the page's scrolling and the canvas's touch-action, in a real browser
    # ------------------------------------------------------------------------------------------------------------------------------------------------------------
    if wanted("scroll"):
        print("[web touch] %s: a swipe on the canvas does not scroll the page, a swipe on the page does" % name)
        g = game.scroll_to_box()
        game.tab.ev("window.scrollTo(0, 0); 1")
        g = game.geometry()
        cx, cy = game.at(g, 480, 270)
        before = tab.ev("window.scrollY")
        fingers.down(1, cx, cy)
        for i in range(1, 9):
            fingers.move(a=(cx, cy - 12 * i))
            time.sleep(0.02)
        fingers.up(1)
        time.sleep(0.3)
        check(tab.ev("window.scrollY") == before, "%s: a swipe up over the canvas does not scroll the page (scroll %s)" % (name, tab.ev("window.scrollY")))
        check(tab.ev("window.visualViewport.scale") == 1, "%s: nor does it zoom it" % name)
        # a swipe on the page's text (below the game, or at the bottom of the window)
        room = g["inner"][1] - (g["box"][1] + g["box"][3])
        if room > 120:
            x, y = g["inner"][0] / 2, g["box"][1] + g["box"][3] + 100
            fingers.down(1, x, y)
            for i in range(1, 9):
                fingers.move(a=(x, y - 20 * i))
                time.sleep(0.02)
            fingers.up(1)
            time.sleep(0.5)
            check(tab.ev("window.scrollY") > before + 20, "%s: a swipe up over the page under the game scrolls it as before (scroll %s)" % (name, tab.ev("window.scrollY")))
            tab.ev("window.scrollTo(0, 0); 1")
            # a pinch on the page: no page zoom (the viewport forbids it; iOS Safari ignores that, and the gesture guards are for it)
            fingers.down(1, x - 20, y)
            fingers.down(2, x + 20, y)
            for i in range(1, 9):
                fingers.move(a=(x - 20 - 8 * i, y), b=(x + 20 + 8 * i, y))
                time.sleep(0.02)
            fingers.up_all()
            time.sleep(0.3)
            check(tab.ev("window.visualViewport.scale") == 1, "%s: a pinch on the page does not zoom it (scale %s)" % (name, tab.ev("window.visualViewport.scale")))
            # a double tap on the page: no zoom
            for _ in range(2):
                fingers.down(1, x, y)
                time.sleep(0.05)
                fingers.up(1)
                time.sleep(0.08)
            time.sleep(0.4)
            check(tab.ev("window.visualViewport.scale") == 1, "%s: a double tap on the page does not zoom it (scale %s)" % (name, tab.ev("window.visualViewport.scale")))
        else:
            print("  (no room under the game in this window: the swipe on the page is not tried)")
        game.scroll_to_box()

    # the gestures of the game, in a box that is wholly in the window; run in the page and again in its fullscreen
    def gestures(label, gx):
        g = gx
        scale = CANVAS_W / g["box"][2]

        def settle():
            return wait_for(lambda: game.probe(34) == 0 and game.probe(35) == 0, 3.0, 0.05)

        def begin(part):
            print("[web touch] %s: %s" % (label, part))
            game.clean(g)

        def note(text):
            print("  note: %s: %s" % (label, text))

        def sample(run, tries=5):
            """`run()` makes a gesture and returns (the most that its timed part can have lasted by the game's clock = our wall time from before the first touch was sent to after the last one,
            what it saw). The sample is used when that is under QUICK; else it is tried again (the DevTools call that sends a touch was slow: see TIME above). (None, the last time) when no try
            was quick enough."""
            lasted = 0.0
            for _ in range(tries):
                game.clean(g)
                lasted, seen = run()
                if lasted < QUICK:
                    return seen, lasted
            return None, lasted

        mid = game.at(g, 400, 270)                                          # (a point of the map view that no button covers)
        if wanted("taps"):
            begin("taps")
            def tapped():
                before = game.counters()
                t0 = time.monotonic()
                fingers.down(1, mid[0], mid[1])
                time.sleep(0.03)
                fingers.up(1)
                lasted = time.monotonic() - t0
                settle()
                return lasted, (before, game.counters())

            seen, lasted = sample(tapped)
            if seen is None:
                note("no tap could be timed: sending a touch took more than %d ms each time (the machine is loaded); the tap is the model's tests'" % (lasted * 1000))
            else:
                before, after = seen
                check(after["taps"] == before["taps"] + 1 and after["holds"] == before["holds"] and after["drags"] == before["drags"],
                      "%s: a tap on the map (a touch of at most %d ms) is a tap, and only a tap (%s -> %s)" % (label, lasted * 1000, before, after))
            # (the boundary between a tap and a hold, and the finger that lingers between them, is not for a browser: see TIME above; the model's tests pin it)
            # a tap on a HUD button: the Options button opens its window, a tap on the window's Return button closes it (a tap works on a window too)
            bx, by = game.at(g, *OPTIONS_BUTTON)
            game.tap(bx, by)
            check(wait_for(lambda: game.probe(5) == 1, 3.0), "%s: a tap on the Options button opens the options window (the game says %s)" % (label, game.probe(5)))
            settle()
            check(game.close_options(g), "%s: a tap on the options window's Return button closes it (the game says %s)" % (label, game.probe(5)))
            settle()
        if wanted("gates"):
            begin("two fingers under a window do nothing")
            bx, by = game.at(g, *OPTIONS_BUTTON)
            game.tap(bx, by)
            check(wait_for(lambda: game.probe(5) == 1, 3.0), "%s: (the options window is open: the game says %s)" % (label, game.probe(5)))
            settle()
            view = game.view()
            two = game.counters()["two"]
            fingers.down(1, mid[0] - 30, mid[1])
            fingers.down(2, mid[0] + 30, mid[1])
            for i in range(1, 7):
                fingers.move(a=(mid[0] - 30 - 12 * i, mid[1] + 6 * i), b=(mid[0] + 30 + 12 * i, mid[1] + 6 * i))
                time.sleep(0.03)
            fingers.up_all()
            settle()
            now = game.view()
            check(now["x"] == view["x"] and now["y"] == view["y"] and now["zoom"] == view["zoom"] and game.counters()["two"] == two, "%s: two fingers under the options window do not pan or zoom (%s -> %s)" % (label, view, now))
            check(game.close_options(g), "%s: and a tap on its Return button closes it (the game says %s)" % (label, game.probe(5)))
            settle()
        if wanted("hold"):
            begin("hold")
            buzzes = tab.ev("window.__buzz.length")
            before = game.counters()
            t_sent = time.monotonic()
            fingers.down(1, mid[0], mid[1])
            t_down = time.monotonic()
            time.sleep(0.15)
            t_look = time.monotonic()
            mid_state = game.counters()
            t_seen = time.monotonic()
            time.sleep(0.65)                                               # 800 ms in all (by our clock: the game's has run at least that, from the end of the sending)
            held = game.counters()
            fingers.up(1)
            settle()
            after = game.counters()
            latency = "sending the touch took %d ms, the look %d ms" % ((t_down - t_sent) * 1000, (t_seen - t_look) * 1000)
            expected = hold_expectation(t_seen - t_sent, t_look - t_down)
            if expected == "waiting":
                check(mid_state["holds"] == before["holds"] and mid_state["mode"] == 1, "%s: a finger that went down at most %d ms ago still waits (nothing is held: mode %s) (%s)" % (label, (t_seen - t_sent) * 1000, mid_state["mode"], latency))
            elif expected == "fired":
                check(mid_state["holds"] == before["holds"] + 1 and mid_state["mode"] == 3, "%s: a finger that went down at least %d ms ago has fired its hold (mode %s) (%s)" % (label, (t_look - t_down) * 1000, mid_state["mode"], latency))
            else:
                note("the first look at the hold (mode %s, holds %s -> %s) is not judged: our clock cannot tell if 450 ms had passed (%s)" % (mid_state["mode"], before["holds"], mid_state["holds"], latency))
            check(held["holds"] == before["holds"] + 1 and held["mode"] == 3, "%s: at 800 ms the hold has fired (counted once) and the right button is held (mode %s)" % (label, held["mode"]))
            check(after["holds"] == before["holds"] + 1 and after["taps"] == before["taps"] and after["fingers"] == 0 and after["mode"] == 0, "%s: the lift ends it: one hold, no tap, nothing held (%s -> %s)" % (label, before, after))
            if profile["vibrate"]:
                check(tab.ev("window.__buzz.length") == buzzes + 1 and tab.ev("window.__buzz[window.__buzz.length - 1]") == 12, "%s: the hold buzzed once for 12 ms (%s)" % (label, tab.ev("JSON.stringify(window.__buzz)")))
            else:
                check(tab.ev("window.__buzz.length") == buzzes and not tab.ev("window.__errors.length"), "%s: the hold needed no buzz and raised no error where the browser has none (%s)" % (label, tab.ev("JSON.stringify(window.__errors)")))
            # a hold that moves away before its time is a drag (the first move comes at once after the touch: the game has the finger's down stamp and the move's, and decides by them)
            def moved_away():
                before = game.counters()
                t0 = time.monotonic()
                fingers.down(1, mid[0], mid[1])
                time.sleep(0.05)
                fingers.move(a=(mid[0] + 12, mid[1] + 4))
                lasted = time.monotonic() - t0
                for i in range(2, 7):
                    time.sleep(0.02)
                    fingers.move(a=(mid[0] + 9 * i, mid[1] + 3 * i))
                time.sleep(0.3)
                fingers.up(1)
                settle()
                return lasted, (before, game.counters())

            seen, lasted = sample(moved_away)
            if seen is None:
                note("no hold-then-drag could be timed: sending the touch and its first move took more than %d ms each time (the machine is loaded)" % (lasted * 1000))
            else:
                before, after = seen
                check(after["drags"] == before["drags"] + 1 and after["holds"] == before["holds"],
                      "%s: a finger that moves away at most %d ms after it went down is a drag, never a hold (%s -> %s)" % (label, lasted * 1000, before, after))
        if wanted("drag"):
            begin("drag")
            start = game.at(g, 300, 200)

            def dragged():
                before = game.counters()
                t0 = time.monotonic()
                fingers.down(1, start[0], start[1])
                fingers.move(a=(start[0] + 12, start[1] + 7))
                lasted = time.monotonic() - t0
                for i in range(2, 11):
                    time.sleep(0.02)
                    fingers.move(a=(start[0] + 8 * i, start[1] + 5 * i))
                fingers.up(1)
                settle()
                return lasted, (before, game.counters())

            seen, lasted = sample(dragged)
            if seen is None:
                note("no drag could be timed: sending the touch and its first move took more than %d ms each time (the machine is loaded)" % (lasted * 1000))
            else:
                before, after = seen
                check(after["drags"] == before["drags"] + 1 and after["taps"] == before["taps"] and after["holds"] == before["holds"], "%s: a drag on the map is a drag (%s -> %s)" % (label, before, after))
        if wanted("pan"):
            begin("two fingers pan the map")
            view = game.view()
            two = game.counters()["two"]
            scroll_before = tab.ev("window.scrollY")
            # which way there is room to scroll: the view's origin must not run into the map's edge
            sx = 1 if view["x"] > 200 else -1                                # fingers going right = the view going left (its origin falls)
            sy = 1 if view["y"] > 200 else -1
            dx, dy = sx * 70.0, sy * 40.0                                     # CSS pixels
            a0 = (mid[0] - 25, mid[1])
            b0 = (mid[0] + 25, mid[1])
            fingers.down(1, *a0)
            time.sleep(0.05)
            fingers.down(2, *b0)
            time.sleep(0.05)
            steps = 10
            for i in range(1, steps + 1):
                fingers.move(a=(a0[0] + dx * i / steps, a0[1] + dy * i / steps), b=(b0[0] + dx * i / steps, b0[1] + dy * i / steps))
                time.sleep(0.03)
            time.sleep(0.15)
            fingers.up(1)
            fingers.up(2)
            settle()
            now = game.view()
            counters = game.counters()
            want_x, want_y = -dx * scale, -dy * scale
            got_x, got_y = now["x"] - view["x"], now["y"] - view["y"]
            check(abs(got_x - want_x) <= 3 and abs(got_y - want_y) <= 3 and now["zoom"] == view["zoom"],
                  "%s: two fingers that move (%+.0f, %+.0f) CSS pixels move the view's origin by (%+d, %+d) picture pixels (%.3f each: wanted %+.1f, %+.1f) and do not zoom" % (label, dx, dy, got_x, got_y, scale, want_x, want_y))
            check(counters["two"] == two + 1 and counters["taps"] == game.counters()["taps"] and counters["fingers"] == 0 and counters["mode"] == 0, "%s: it is one two-finger gesture and nothing is left held (%s)" % (label, counters))
            check(tab.ev("window.scrollY") == scroll_before and tab.ev("window.visualViewport.scale") == 1, "%s: the page neither scrolled nor zoomed under the fingers (scroll %s, scale %s)" % (label, tab.ev("window.scrollY"), tab.ev("window.visualViewport.scale")))
        if wanted("pinch"):
            begin("two fingers pinch the map")
            view = game.view()
            two = game.counters()["two"]

            def pinch(from_half, to_half, steps=10):
                """The fingers are `from_half` CSS pixels from the middle each, and go to `to_half` (a straight line through the point mid)"""
                a0 = (mid[0] - from_half, mid[1])
                b0 = (mid[0] + from_half, mid[1])
                fingers.down(1, *a0)
                time.sleep(0.05)
                fingers.down(2, *b0)
                time.sleep(0.05)
                for i in range(1, steps + 1):
                    half = from_half + (to_half - from_half) * i / steps
                    fingers.move(a=(mid[0] - half, mid[1]), b=(mid[0] + half, mid[1]))
                    time.sleep(0.03)
                time.sleep(0.15)
                fingers.up(1)
                fingers.up(2)
                settle()

            pinch(35, 100)
            zoomed_in = game.view()["zoom"]
            check(zoomed_in == 200, "%s: the fingers spread to 2.9 times the distance: the game zooms in as far as it goes, 200 %% (it says %s %%)" % (label, zoomed_in))
            pinch(100, 28)
            zoomed_out = game.view()["zoom"]
            check(zoomed_out < 100 and zoomed_out >= 39, "%s: the fingers close to 0.28 of the distance from 200 %%: the game zooms out (%s %%: below 100 %%, not past the map's limit)" % (label, zoomed_out))
            pinch(60, 60.5, 3)                                              # a pair that barely moves does not change the level
            check(game.view()["zoom"] == zoomed_out, "%s: fingers that hardly move change no level (%s %%)" % (label, game.view()["zoom"]))
            check(game.counters()["two"] == two + 3, "%s: three pairs, three gestures (%s)" % (label, game.counters()["two"] - two))
            # back to 100 %: the middle button is not on a phone: pinch in the other way to the first level that is 100
            for _ in range(4):
                if game.view()["zoom"] >= 100:
                    break
                pinch(40, 70)
            check(game.view()["zoom"] >= 100, "%s: spreading goes back up (%s %%)" % (label, game.view()["zoom"]))
        if wanted("cancel"):
            begin("the browser takes the touch away")
            tab.ev("(function () { var orig = Module._ants_touch_cancel; window.__cancelled = 0; Module._ants_touch_cancel = function () { window.__cancelled++; return orig.apply(this, arguments); }; })(); 1")
            before = game.counters()
            fingers.down(1, mid[0], mid[1])
            time.sleep(0.9)                                                 # the hold has fired (by the game's clock too: it is at least this long since the sending ended): the right button is held
            held = game.counters()
            fingers.cancel()
            settle()
            after = game.counters()
            check(held["mode"] == 3 and after["fingers"] == 0 and after["mode"] == 0, "%s: a touchcancel while a hold is held leaves no finger and nothing held (mode %s -> %s)" % (label, held["mode"], after["mode"]))
            check(tab.ev("window.__cancelled") >= 1, "%s: the page told the game of the touchcancel (%s calls of Module._ants_touch_cancel)" % (label, tab.ev("window.__cancelled")))

            def cancelled():
                before = game.counters()
                t0 = time.monotonic()
                fingers.down(1, mid[0], mid[1])
                time.sleep(0.05)
                fingers.cancel()
                lasted = time.monotonic() - t0
                time.sleep(0.8)                                             # (longer than a hold takes)
                return lasted, (before, game.counters())

            seen, lasted = sample(cancelled)
            if seen is None:
                note("no cancel could be timed: sending the touch and its cancel took more than %d ms each time (the machine is loaded)" % (lasted * 1000))
            else:
                before, after = seen
                check(after["holds"] == before["holds"] and after["fingers"] == 0, "%s: a finger that was cancelled at most %d ms after it went down never becomes a hold (%s -> %s)" % (label, lasted * 1000, before, after))

    if wanted("taps") or wanted("hold") or wanted("drag") or wanted("pan") or wanted("pinch") or wanted("cancel") or wanted("gates"):
        gestures(name, game.scroll_to_box())
    shot("after_gestures")

    # ------------------------------------------------------------------------------------------------------------------------------------------------------------
    # the page's fullscreen: the browser's own where there is the API, the page's own where there is none; the box is another size
    # ------------------------------------------------------------------------------------------------------------------------------------------------------------
    if wanted("fullscreen"):
        print("[web touch] %s: the Fullscreen button, by touch" % name)
        g = game.scroll_to_box()
        game.clean(g)
        button = json.loads(tab.ev("JSON.stringify((function () { var b = document.getElementById('fullscreen-btn').getBoundingClientRect(); return [b.x + b.width / 2, b.y + b.height / 2, b.width, b.height]; })())"))
        check(button[2] >= 24 and button[3] >= 24, "%s: the Fullscreen button is big enough for a finger (%.0f x %.0f CSS pixels)" % (name, button[2], button[3]))
        fingers.down(1, button[0], button[1])
        time.sleep(0.08)
        fingers.up(1)
        time.sleep(2.5)
        g = game.geometry()
        if profile["fullscreen_api"]:
            check(g["fullscreen"] and not g["pseudo"], "%s: a tap on the Fullscreen button gives the browser's fullscreen (%s)" % (name, {k: g[k] for k in ("fullscreen", "pseudo")}))
        else:
            check(g["pseudo"] and not g["fullscreen"], "%s: with no Fullscreen API a tap on the button gives the page's own fullscreen (%s)" % (name, {k: g[k] for k in ("fullscreen", "pseudo")}))
        shot("fullscreen")
        box = g["box"]
        check(box[0] >= -0.5 and box[0] + box[2] <= g["inner"][0] + 0.5 and box[1] >= -0.5 and box[1] + box[3] <= g["inner"][1] + 0.5, "%s: the game's box is inside the window in fullscreen (%s of %s)" % (name, [round(v, 1) for v in box], g["inner"]))
        slop = game.probe(36) / 100.0
        want = max(1.0, min(64.0, max(8.0 * CANVAS_W / box[2], 6.0 * CANVAS_W / box[2] / g["dpr"])))
        check(abs(slop - want) <= 0.02 * want + 0.02, "%s: the slop follows the box in fullscreen: %.2f (wanted %.2f for a box %.1f CSS pixels wide)" % (name, slop, want, box[2]))
        if wanted("taps") or wanted("hold") or wanted("pan") or wanted("pinch"):
            gestures(name + " fullscreen", g)
        # leave it
        if profile["fullscreen_api"]:
            tab.ev("document.exitFullscreen(); 1")
        else:
            exit_button = json.loads(tab.ev("JSON.stringify((function () { var b = document.getElementById('pseudo-exit').getBoundingClientRect(); return [b.x + b.width / 2, b.y + b.height / 2]; })())"))
            fingers.down(1, *exit_button)
            time.sleep(0.08)
            fingers.up(1)
        time.sleep(1.0)
        g = game.geometry()
        check(not g["fullscreen"] and not g["pseudo"], "%s: fullscreen is left (%s)" % (name, {k: g[k] for k in ("fullscreen", "pseudo")}))
    check(not tab.ev("window.__errors.length"), "%s: the page raised no error during the check (%s)" % (name, tab.ev("JSON.stringify(window.__errors)")))


if __name__ == "__main__":
    sys.exit(main())
