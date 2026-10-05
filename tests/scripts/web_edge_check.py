#!/usr/bin/env python3
"""The pointer of a FULLSCREEN game page in a REAL browser (opt-in; see tests/scripts/test_web_edge.sh and docs/NETWORK_PORT.md, "The pointer in fullscreen").

Needs a running web page (the web image of this tree: `docker build -t ants-beta .` and run it on a port; the game server is not needed), a Chromium-based browser and Python 3; nothing
else (the DevTools protocol is spoken with the client of web_hidden_check.py, standard library only). The check opens the page in a throwaway headless browser (its own profile, its own
port; nothing of yours is touched), starts a match (Enter at the quick help, Enter at the setup screen) and reads what the GAME believes through the game's `ants_probe` (the
pointer's position on the picture, whether it takes the pointer as gone, the map view's origin, whether a dialog is open, the zoom). The browser is a 1440 x 900 window (a 16:10 screen)
whose fullscreen is REAL (headless Chrome enters it): the 16:9 picture is 1440 x 810 with a bar of 45 px above and below. What it checks:

  * the BARS (the owner: "if I go off the edge of the game, it no longer pans"): a pointer over the top or the bottom bar is, for the game, at the picture's nearest edge pixel and the map
    scrolls up or down; a corner of the bars scrolls diagonally; a pointer that moves from the picture into a bar goes on scrolling (the game is not told that the pointer left); back in
    the picture the scrolling stops; a click, a wheel and a ctrl + wheel over a bar reach the game as nothing (the canvas gets no event, the game does not zoom, the page does not either);
    the WINDOWED page is unchanged (a pointer that leaves the game's box is gone for the game and the scrolling stops); a pointer that goes on over a bar and then the fullscreen
    is left, or the stage is left, is gone for the game; the page's own fullscreen (no Fullscreen API: an iPhone) does the same as the browser's;
  * a screen of another shape: the pillarbox of a 4:3 picture on a wide screen (bars at the sides);
  * the POINTER LOCK (Fullscreen mouse: Locked, the default): the click on the Fullscreen button makes fullscreen AND takes the lock (headless Chrome grants it, and its input gives the
    page the real locked motion, `movementX / movementY` with the position frozen); the game's cursor starts in the middle of the picture, moves by the distance of the mouse's motion
    and stays on the picture (every edge and every corner scrolls the map, the way back from an edge is at once), a slow mouse is not lost (fractions of a pixel add up, at a device ratio
    of 1 and of 2), the game's window system is not told of the lock (the game's pointer is the page's, and the game is not told of a pointer that leaves), a click goes where the game's
    cursor is (the cursor is moved onto the Options button and a click opens the options), Esc is locked as a key (navigator.keyboard.lock: the call is seen) and unlocked with fullscreen;
    the lock let go (exitPointerLock, as the browser does for Esc, a lost focus) leaves the game's cursor where it is (nothing happens until the pointer moves: no jump, no click) and
    the pointer is the normal one; leaving fullscreen lets the lock go; a click on the game takes the lock again where the browser let it go, the cursor staying where the click is;
    a browser that REFUSES the lock still gets the bars' rules; the setting Free (and the control under the game: its markup, its click, the remembered key `ants.pointerlock`) asks
    for no lock and no Esc; the page's own fullscreen asks for none either.
  * the MARGIN of the WINDOWED page (the owner: "go off the screen with the mouse and it not screw up the scrolling"; agreed: the map keeps scrolling while the pointer is just past the
    game's edge, about an inch, and stops when it goes farther): in a 1800 x 1000 window the pointer 1, 40, 90 and 96 CSS px beyond the left, right, top and bottom edges and 90 px beyond each
    corner is the game's pointer at the picture's edge pixel and the map scrolls; 97 and 120 px beyond it is gone and the view stays; over the picture's and the mouse's selectors under the
    game and over the links above it (a control of the page) it is gone at once, from the margin and from the middle of the picture; a click, a right click, a wheel and a ctrl + wheel in the
    margin reach the page (the canvas gets none of them, the wheel scrolls the page, the page's menu opens, the game does not zoom or open anything); the pointer that leaves the browser window
    is gone; back on the picture the scrolling stops and the edges work as before; a page that scrolls under a pointer that did not move looks again; a tap in the margin does nothing; the
    first leave of a page's life (which SDL 2.28.4 drops) is delivered.
What headless Chrome cannot show, and the owner's Mac must: the Dock and the menu bar that appear at the screen's edge (a real screen, not a headless one), that Esc is the game's and
HOLDING it leaves fullscreen (the browser's own interface does that, not the page), the browser's bubble ("Press Esc to show your cursor"), Safari and Firefox (no Keyboard Lock API:
Esc gives the pointer back first), a real mouse's own acceleration and its fractional movement on a Retina screen.

Exit status 0: every check passed; 1: a check failed, or the browser or the page broke down during the check (a page that came up and then misbehaved is a failure, never a skip); 3: the
check could not be made because the environment is not there (no browser, nothing answers at the page's address); 2 is the status of a bad command line.
"""
import argparse
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_aspect_check as aspect                                            # noqa: E402  (the DevTools driver of the picture check)
from web_aspect_check import Browser, NotReachable, Tab                      # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

PICTURE_W, PICTURE_H = 960, 540                                              # the game's canvas
OPTIONS_BUTTON = (845 + 26, 7 + 11)                                          # the Options button of the 16:9 picture (the classic (525, 7, 52, 23) moved by the 320 pixels that the picture is wider), its middle


def wait_for(condition, timeout=4.0, step=0.05):
    deadline = time.time() + timeout
    value = condition()
    while not value and time.time() < deadline:
        time.sleep(step)
        value = condition()
    return value


class Game:
    """A tab with the game page, and what the game believes (ants_probe)."""

    def __init__(self, tab, web):
        self.tab = tab
        self.web = (web if web.endswith("/") else web + "/") + "play.html"            # the game page at its own path ("/" is the front page, the lobby)
        self.pointer = [0.0, 0.0]                                            # where the DevTools pointer is (CSS pixels): locked motion is the difference between two of its positions

    def probe(self, what):
        return self.tab.ev("Module._ants_probe(%d)" % what)

    def state(self):
        return {"px": self.probe(0), "py": self.probe(1), "out": self.probe(2), "vx": self.probe(3), "vy": self.probe(4), "modal": self.probe(5), "zoom": self.probe(6)}

    def geometry(self):
        return self.tab.geometry()

    def open(self, width=1440, height=900, dpr=1, query="?aspect=16:9", storage=None):
        """The page at a window size; `storage` (a dict) is what the browser remembers before the page starts."""
        self.tab.emulate(width, height, dpr)
        self.tab.open(self.web + query, settle=0.5)
        if storage is not None:
            self.tab.ev("localStorage.clear(); %s 1" % "".join("localStorage.setItem(%s, %s);" % (json.dumps(k), json.dumps(v)) for k, v in storage.items()))
            self.tab.open(self.web + query, settle=2.5)
        else:
            time.sleep(2.0)
        self.pointer = [0.0, 0.0]

    def start_match(self):
        self.tab.ev("document.getElementById('canvas').focus(); 1")
        key = lambda kind: self.tab.call("Input.dispatchKeyEvent", {"type": kind, "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13, "text": "\r" if kind == "keyDown" else ""})
        for _ in range(2):                                                   # the quick help's Enter (the setup screen), the setup screen's Enter (START)
            key("keyDown")
            key("keyUp")
            time.sleep(1.5)
        if not wait_for(lambda: self.tab.ev("Module._ants_match_running()") == 1, 20):
            return False
        return bool(wait_for(lambda: self.probe(5) == 0, 60, 0.5))             # the "get ready" dialog closes by itself: the edges do not scroll while it is up

    def move(self, x, y):
        self.tab.mouse("mouseMoved", x, y, button="none")
        self.pointer = [x, y]

    def move_by(self, dx, dy):
        self.move(self.pointer[0] + dx, self.pointer[1] + dy)

    def button_centre(self):
        return json.loads(self.tab.ev("JSON.stringify((function(){var b=document.getElementById('fullscreen-btn').getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())"))

    def enter_fullscreen(self, wait=2.5):
        """The player's click on the Fullscreen button (the pointer is on it afterwards)."""
        x, y = self.button_centre()
        self.tab.click(x, y)
        self.pointer = [x, y]
        time.sleep(wait)
        return self.geometry()

    def locked(self):
        return bool(self.tab.ev("!!(document.pointerLockElement || document.webkitPointerLockElement) && (document.pointerLockElement || document.webkitPointerLockElement).id === 'canvas'"))

    def park(self, box, want_x=(300, 850), want_y=(450, 1000)):
        """Put the map view in the middle of the map, with the pointer on the picture's own edge rows (what always worked), and the pointer in the middle of the picture."""
        deadline = time.time() + 25
        while time.time() < deadline:
            s = self.state()
            dx = 0 if want_x[0] <= s["vx"] <= want_x[1] else (1 if s["vx"] < want_x[0] else -1)
            dy = 0 if want_y[0] <= s["vy"] <= want_y[1] else (1 if s["vy"] < want_y[0] else -1)
            if dx == 0 and dy == 0:
                break
            x = box[0] + box[2] / 2 + dx * (box[2] / 2 - 2)
            y = box[1] + box[3] / 2 + dy * (box[3] / 2 - 2)
            self.move(x, y)
            time.sleep(0.06)
        self.move(box[0] + box[2] / 2, box[1] + box[3] / 2)
        time.sleep(0.4)
        return self.state()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the image, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 120)")
    ap.add_argument("--only", default="", help="run only the parts whose name contains this text (bars, windowed, margin, pillar, pseudo, lock, release, setting, slow)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout

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
        tab = Tab(browser)
        game = Game(tab, args.web)

        def shot(name):
            tab.save_shot(args.shots, name)

        def started(label):
            ok = game.start_match()
            check(ok, "%s: Enter at the quick help and at the setup screen start a match, and its 'get ready' dialog closes" % label)
            return ok

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        # what the pointer does over the bars of a fullscreen page (the browser's own fullscreen, the lock off so that only the bars' rules are at work)
        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        def expect_scroll(label, box, x, y, want_px, want_py, sx, sy, seconds=1.0):
            """The map view is in the middle of the map; the pointer goes from the middle of the picture to (x, y): the game's pointer is (want_px, want_py) (+- 1), not gone, and the view
            scrolls by the sign (sx, sy) (0: not at all, +- 4 px) by at least 80 px along an axis that scrolls."""
            s0 = game.park(box)
            game.move(x, y)
            time.sleep(0.12)
            mid = game.state()
            time.sleep(seconds)
            s1 = game.state()
            moved_x, moved_y = s1["vx"] - s0["vx"], s1["vy"] - s0["vy"]
            ok = mid["out"] == 0 and abs(mid["px"] - want_px) <= 1 and abs(mid["py"] - want_py) <= 1
            ok = ok and (moved_x * sx >= 80 if sx else abs(moved_x) <= 4) and (moved_y * sy >= 80 if sy else abs(moved_y) <= 4)
            check(ok, "%s: the pointer at (%d, %d) is the game's pointer at (%d, %d), not gone (%s), and the view moved by (%+d, %+d) px" % (label, x, y, mid["px"], mid["py"], "gone" if mid["out"] else "here", moved_x, moved_y))
            game.move(box[0] + box[2] / 2, box[1] + box[3] / 2)
            time.sleep(0.5)
            return s0, mid, s1

        def bars_checks(label, box, instrument=True):
            """The bars of a screen of 1440 x 900 (the picture 1440 x 810 at (0, 45): scale 1.5)."""
            top, bottom = box[1], box[1] + box[3]
            expect_scroll(label + ", the picture's own top edge row (it always worked)", box, 720, top + 2, 480, 1, 0, -1)
            expect_scroll(label + ", the top bar", box, 720, top - 25, 480, 0, 0, -1)
            expect_scroll(label + ", the first row of the top bar", box, 720, top - 1, 480, 0, 0, -1)
            expect_scroll(label + ", the bottom bar", box, 720, bottom + 25, 480, PICTURE_H - 1, 0, 1)
            expect_scroll(label + ", the top left corner of the bars", box, 3, top - 30, 2, 0, -1, -1)
            expect_scroll(label + ", the top right corner of the bars", box, box[0] + box[2] - 4, top - 30, 958, 0, 1, -1)
            expect_scroll(label + ", the bottom left corner of the bars", box, 3, bottom + 30, 2, PICTURE_H - 1, -1, 1)
            expect_scroll(label + ", the bottom right corner of the bars", box, box[0] + box[2] - 4, bottom + 30, 958, PICTURE_H - 1, 1, 1)
            # the left and right edge of the picture on a screen that is as wide as the picture: the picture's own pixel
            expect_scroll(label + ", the left edge of the screen (the picture's own pixel, it always worked)", box, 2, 450, 1, 270, -1, 0)
            expect_scroll(label + ", the right edge of the screen", box, box[0] + box[2] - 2, 450, 958, 270, 1, 0)

        def walk_into_a_bar(label, box):
            """The pointer goes from the picture INTO a bar in small steps and stays: the scrolling goes on (the game is not told that the pointer left), in two windows of time."""
            top = box[1]
            game.park(box, want_y=(1100, 1420))
            game.move(720, top + 300)
            time.sleep(0.2)
            y = top + 300
            while y > top - 30:
                y -= 12
                game.move(720, y)
                time.sleep(0.02)
            time.sleep(0.1)
            a = game.state()
            time.sleep(0.45)
            b = game.state()
            time.sleep(0.45)
            c = game.state()
            check(a["out"] == 0 and b["out"] == 0 and c["out"] == 0 and a["py"] == 0 and b["vy"] < a["vy"] - 80 and c["vy"] < b["vy"] - 80,
                  "%s: walked from the picture into the top bar, the game's pointer stays on (%d, %d), not gone, and the view goes on scrolling up (%d, %d, %d)" % (label, a["px"], a["py"], a["vy"], b["vy"], c["vy"]))
            game.move(box[0] + box[2] / 2, box[1] + box[3] / 2)
            time.sleep(0.5)

        def leave_after_the_motion(label, box):
            """The order of the browser's events differs: Chrome sends the canvas's leave BEFORE the pointer's motion over the bar, Safari and Firefox send the pointer events first. The page
            must not let that leave end the pointer's stay (SDL would be told that the pointer went away AFTER it was handed the edge position): a leave towards the stage that comes after
            the motion is not passed on."""
            game.park(box, want_y=(1100, 1420))
            game.move(720, box[1] - 20)
            time.sleep(0.15)
            before = game.state()
            tab.ev("document.getElementById('canvas').dispatchEvent(new MouseEvent('mouseleave', {bubbles: false, relatedTarget: document.getElementById('game-stage'), clientX: 720, clientY: %d})); 1" % (box[1] - 20))
            time.sleep(0.4)
            after = game.state()
            check(before["out"] == 0 and after["out"] == 0 and after["vy"] < before["vy"] - 40, "%s: the canvas's leave towards the stage that comes AFTER the motion over the top bar (Safari's and Firefox's order) is not passed on: the pointer stays on the edge (%s -> %s) and the view goes on scrolling (%d -> %d)" % (label, before["out"], after["out"], before["vy"], after["vy"]))
            game.move(box[0] + box[2] / 2, box[1] + box[3] / 2)
            time.sleep(0.4)

        def back_in_the_picture(label, box):
            game.park(box)
            game.move(720, box[1] - 20)
            time.sleep(0.5)
            game.move(720, box[1] + box[3] / 2)
            time.sleep(0.25)
            a = game.state()
            time.sleep(0.6)
            b = game.state()
            check(a["out"] == 0 and b["out"] == 0 and abs(b["vx"] - a["vx"]) <= 2 and abs(b["vy"] - a["vy"]) <= 2 and b["py"] > 100,
                  "%s: back in the picture the scrolling stops (the view stays at (%d, %d), the pointer at (%d, %d))" % (label, b["vx"], b["vy"], b["px"], b["py"]))

        def inert_over_a_bar(label, box):
            """A click, a wheel and a ctrl + wheel over a bar are the stage's: the canvas gets none of them, the game does not zoom, the page does not zoom either."""
            tab.ev("""window.__log = []; ['mousedown', 'mouseup', 'click', 'wheel', 'contextmenu'].forEach(function (n) {
                window.addEventListener(n, function (e) { window.__log.push([n, e.target.id || e.target.tagName, e.defaultPrevented, e.ctrlKey]); }, false); }); 1""")
            game.park(box)
            before = game.state()
            x, y = 600, box[1] - 20
            game.move(x, y)
            time.sleep(0.2)
            tab.click(x, y)
            tab.call("Input.dispatchMouseEvent", {"type": "mouseWheel", "x": x, "y": y, "deltaX": 0, "deltaY": -120})
            tab.call("Input.dispatchMouseEvent", {"type": "mouseWheel", "x": x, "y": y, "deltaX": 0, "deltaY": -120, "modifiers": 2})
            tab.call("Input.dispatchMouseEvent", {"type": "mousePressed", "x": x, "y": y, "button": "right", "clickCount": 1, "buttons": 2})
            tab.call("Input.dispatchMouseEvent", {"type": "mouseReleased", "x": x, "y": y, "button": "right", "clickCount": 1})
            time.sleep(0.6)
            log = json.loads(tab.ev("JSON.stringify(window.__log)"))
            after = game.state()
            on_canvas = [e for e in log if e[1] == "canvas"]
            wheels = [e for e in log if e[0] == "wheel"]
            check(not on_canvas and any(e[0] == "mousedown" and e[1] == "game-stage" for e in log),
                  "%s: a click and a right click over the top bar reach the stage and not the canvas (the game's window system listens on the canvas: %s)" % (label, [e[:2] for e in log if e[0] in ("mousedown", "click", "contextmenu")]))
            check(len(wheels) == 2 and all(e[1] == "game-stage" and e[2] for e in wheels) and any(e[3] for e in wheels),
                  "%s: a wheel and a ctrl + wheel over the top bar are cancelled (no zoom of the page, no scroll): %s" % (label, wheels))
            check(after["zoom"] == 100 and before["zoom"] == 100 and after["modal"] == 0, "%s: the game did not zoom or open anything (zoom %s%%, dialog %s)" % (label, after["zoom"], after["modal"]))
            contexts = [e for e in log if e[0] == "contextmenu"]
            check(contexts and all(e[2] for e in contexts), "%s: the browser's own menu does not open over a bar (%s)" % (label, contexts))
            # the control: the same wheel over the picture zooms the game
            game.move(720, box[1] + box[3] / 2)
            time.sleep(0.2)
            tab.call("Input.dispatchMouseEvent", {"type": "mouseWheel", "x": 720, "y": box[1] + box[3] / 2, "deltaX": 0, "deltaY": -120})
            time.sleep(0.6)
            zoomed = game.state()["zoom"]
            check(zoomed != 100, "%s: (control) a wheel over the picture zooms the game (%s%%)" % (label, zoomed))
            tab.mouse("mousePressed", 720, box[1] + box[3] / 2, button="middle", clickCount=1)
            tab.mouse("mouseReleased", 720, box[1] + box[3] / 2, button="middle", clickCount=1)
            time.sleep(0.5)

        def spot(selector):
            """The middle of the first element that matches `selector`, in CSS pixels of the window (the page's own controls: a button, a link)."""
            return json.loads(tab.ev("JSON.stringify((function(){var b=document.querySelector(%s).getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())" % json.dumps(selector)))

        def header_control_spot(box):
            return spot("header a.btn")                                                    # (the first link of the page's header, above the box)

        def expect_gone(label, box, x, y, seconds=0.8):
            """The view is in the middle of the map; the pointer goes from the middle of the picture to (x, y): the game takes it as gone and the view stays (+- 4 px)."""
            s0 = game.park(box)
            game.move(x, y)
            time.sleep(0.15)
            mid = game.state()
            time.sleep(seconds)
            s1 = game.state()
            moved_x, moved_y = s1["vx"] - s0["vx"], s1["vy"] - s0["vy"]
            check(mid["out"] == 1 and abs(moved_x) <= 4 and abs(moved_y) <= 4,
                  "%s: the pointer at (%d, %d) is gone for the game (%s) and the view stays (%+d, %+d px)" % (label, x, y, "gone" if mid["out"] else "STILL HERE", moved_x, moved_y))
            game.move(box[0] + box[2] / 2, box[1] + box[3] / 2)
            time.sleep(0.5)

        if wanted("bars"):
            print("[web edge] a 1440 x 900 screen in the browser's own fullscreen (the lock off): the picture 1440 x 810, bars of 45 px above and below")
            game.open(storage={"ants.pointerlock": "off"})
            if started("bars"):
                g = game.enter_fullscreen()
                check(g["fullscreen"] and not g["pseudo"] and [round(v) for v in g["box"]] == [0, 45, 1440, 810], "the click on the Fullscreen button gives the browser's fullscreen with the picture at (0, 45), 1440 x 810 (%s)" % [round(v, 1) for v in g["box"]])
                check(not game.locked() and tab.ev("getComputedStyle(document.getElementById('game-stage')).cursor") == "none", "the setting Free: no lock, and the browser's arrow is hidden over the stage (the game draws its own cursor)")
                shot("edge_fullscreen")
                time.sleep(1.0)
                bars_checks("fullscreen", g["box"])
                walk_into_a_bar("fullscreen", g["box"])
                leave_after_the_motion("fullscreen", g["box"])
                back_in_the_picture("fullscreen", g["box"])
                inert_over_a_bar("fullscreen", g["box"])
                # the pointer leaves the SCREEN from a bar (another monitor: the browser says the stage was left): the game's pointer is gone and the view stops
                box = g["box"]
                s_park = game.park(box, want_y=(1100, 1420))                                   # (the view low in the map: the way up is long, the scrolling is still going 0.15 s after the pointer is over the bar)
                game.move(720, box[1] - 20)
                time.sleep(0.15)
                before = game.state()
                tab.ev("document.getElementById('game-stage').dispatchEvent(new MouseEvent('mouseleave', {bubbles: false, clientX: 720, clientY: -3})); 1")
                time.sleep(0.3)
                gone = game.state()
                time.sleep(0.6)
                later = game.state()
                check(before["out"] == 0 and before["vy"] < s_park["vy"] - 20 and gone["out"] == 1 and abs(later["vy"] - gone["vy"]) <= 2 and later["vy"] > 100,
                      "the pointer leaves the screen from the top bar (the stage's leave): the game's pointer is gone (%s -> %s) and the view that was scrolling (%d -> %d) stops at %d (%d later)" % (before["out"], gone["out"], s_park["vy"], before["vy"], gone["vy"], later["vy"]))
                game.move(box[0] + box[2] / 2, box[1] + box[3] / 2)
                time.sleep(0.4)
                check(game.state()["out"] == 0, "it comes back with its next motion")
                # fullscreen left with the pointer over a bar, the FIRST leave of this page's life that the game gets (SDL drops that one when its window's flag is clear: see handToGame)
                s_park = game.park(box, want_y=(1100, 1420))
                game.move(720, box[1] - 20)
                time.sleep(0.15)
                tab.ev("document.exitFullscreen(); 1")
                time.sleep(1.5)
                s_out = game.state()
                time.sleep(0.6)
                s_end = game.state()
                check(not game.geometry()["fullscreen"] and s_out["out"] == 1 and abs(s_end["vy"] - s_out["vy"]) <= 2 and s_end["vy"] > 100,
                      "fullscreen left with the pointer over a bar: the game's pointer is gone (%s) and the view that was scrolling stops at %d (%d later)" % (s_out["out"], s_out["vy"], s_end["vy"]))

        if wanted("windowed"):
            print("[web edge] the WINDOWED page: the pointer on the box's edge scrolls, over the page's own header (a control) it is gone, fullscreen left with the pointer over a bar")
            game.open(storage={"ants.pointerlock": "off"})
            if started("windowed"):
                g = game.geometry()
                box = g["box"]
                # (SDL drops the FIRST leave of a page's life, windowed or not: its window starts with the mouse focus but with the window's flag clear; one enter and leave first, as the
                # page does for the game's own pointer in fullscreen. This is the page's old behaviour, not a change of this version)
                game.move(box[0] + box[2] / 2, box[1] + box[3] / 2)
                time.sleep(0.3)
                game.move(box[0] + box[2] / 2, box[1] - 30)
                time.sleep(0.3)
                s0 = game.park(box)
                game.move(box[0] + box[2] / 2, box[1] + 2)                                      # the picture's own top edge row
                time.sleep(0.8)
                s1 = game.state()
                check(s1["out"] == 0 and s1["vy"] < s0["vy"] - 80, "windowed: the pointer on the box's top edge scrolls the map up (%d -> %d)" % (s0["vy"], s1["vy"]))
                hx, hy = header_control_spot(box)                                                # a button of the page's own header, above the box (inside the margin, but a control)
                game.move(hx, hy)
                time.sleep(0.3)
                s2 = game.state()
                time.sleep(0.8)
                s3 = game.state()
                check(s2["out"] == 1 and abs(s3["vy"] - s2["vy"]) <= 2, "windowed: the pointer over a button of the page's header, above the box, is gone for the game and the view stops (gone %s, view %d -> %d)" % (s2["out"], s2["vy"], s3["vy"]))
                game.move(box[0] + box[2] / 2, box[1] + box[3] / 2)
                time.sleep(0.4)
                check(game.state()["out"] == 0, "windowed: back over the picture the game has its pointer again")
                check(tab.ev("getComputedStyle(document.getElementById('game-stage')).cursor") != "none", "windowed: the stage keeps the browser's own cursor rule (nothing is hidden outside fullscreen)")
                g2 = game.enter_fullscreen()
                # in fullscreen with the pointer over a bar, then leaving fullscreen: the game's pointer is gone and the view stays
                game.park(g2["box"])
                game.move(720, 20)
                time.sleep(0.5)
                s4 = game.state()
                tab.ev("document.exitFullscreen(); 1")
                time.sleep(1.5)
                s5 = game.state()
                time.sleep(0.8)
                s6 = game.state()
                g3 = game.geometry()
                check(not g3["fullscreen"] and s4["out"] == 0 and s5["out"] == 1 and abs(s6["vy"] - s5["vy"]) <= 2,
                      "fullscreen left with the pointer over a bar: the game had it at the edge (%s), now it is gone (%s) and the view stays (%d -> %d)" % ("here" if s4["out"] == 0 else "gone", "gone" if s5["out"] else "here", s5["vy"], s6["vy"]))

        if wanted("windowed margin"):
            print("[web edge] the WINDOWED page's margin (1800 x 1000 window): about an inch (96 CSS px) beyond the game's box still counts as the picture's edge")
            MARGIN = 96
            game.open(width=1800, height=1000, storage={})
            if started("margin"):
                g = game.geometry()
                box = g["box"]
                left, top, right, bottom = box[0], box[1], box[0] + box[2], box[1] + box[3]
                cx, cy = box[0] + box[2] / 2, box[1] + box[3] / 2
                check(not g["fullscreen"] and abs(box[2] / PICTURE_W - 4 / 3) < 0.01 and left > 2 * MARGIN and g["inner"][0] - right > 2 * MARGIN and g["inner"][1] - bottom > MARGIN + 20,
                      "the window shows the box %.0f x %.0f at (%.0f, %.0f) with room for the whole margin to its left, right and below" % (box[2], box[3], left, top))
                shot("edge_margin")
                for d in (1, 40, 90, MARGIN):
                    expect_scroll("margin, %d px beyond the left edge" % d, box, left - d, cy, 0, 270, -1, 0)
                for d in (1, 40, 90, MARGIN):
                    expect_scroll("margin, %d px beyond the right edge" % d, box, right + d, cy, PICTURE_W - 1, 270, 1, 0)
                for d in (1, 40, 90, MARGIN):
                    expect_scroll("margin, %d px beyond the bottom edge" % d, box, cx, bottom + d, 480, PICTURE_H - 1, 0, 1)
                expect_scroll("margin, the bottom left corner (90 px beyond on both axes)", box, left - 90, bottom + 90, 0, PICTURE_H - 1, -1, 1)
                expect_scroll("margin, the bottom right corner (90 px beyond on both axes)", box, right + 90, bottom + 90, PICTURE_W - 1, PICTURE_H - 1, 1, 1)
                for d in (MARGIN + 1, 120):
                    expect_gone("margin, %d px beyond the left edge" % d, box, left - d, cy)
                    expect_gone("margin, %d px beyond the right edge" % d, box, right + d, cy)
                    expect_gone("margin, %d px beyond the bottom edge" % d, box, cx, bottom + d)
                expect_gone("margin, a corner is a square: 97 px beyond on both axes", box, left - 97, bottom + 97)
                expect_gone("margin, beside the box but 120 px below its bottom edge (one axis is far enough)", box, left - 40, bottom + 120)

                # the page's own controls: a pointer over one is on the page, whatever its distance from the box (all of these are inside the margin)
                for what, selector in (("the picture's selector 16:9", "#aspect-16-9"), ("the picture's selector Classic 4:3", "#aspect-4-3"), ("the mouse setting Locked", "#lock-on"),
                                       ("the mouse setting Free", "#lock-off"), ("a link of the header above the game", "header a.btn"), ("the Fullscreen button of the header", "#fullscreen-btn")):
                    x, y = spot(selector)
                    dx = max(left - x, x - right, 0)
                    dy = max(top - y, y - bottom, 0)
                    check(dx <= MARGIN and dy <= MARGIN and (dx > 0 or dy > 0), "%s is outside the box and inside the margin (%.0f px, %.0f px beyond): the control's rule is what is tested" % (what, dx, dy))
                    s0 = game.park(box)
                    game.move(x, y)
                    time.sleep(0.15)
                    mid = game.state()
                    time.sleep(0.6)
                    s1 = game.state()
                    check(mid["out"] == 1 and abs(s1["vx"] - s0["vx"]) <= 4 and abs(s1["vy"] - s0["vy"]) <= 4,
                          "%s: the pointer that goes there from the middle of the picture is gone for the game (%s) and the view stays (%+d, %+d px)" % (what, "gone" if mid["out"] else "STILL HERE", s1["vx"] - s0["vx"], s1["vy"] - s0["vy"]))
                    s0 = game.park(box)
                    game.move(left - 40, cy)
                    time.sleep(0.3)
                    scrolling = game.state()
                    game.move(x, y)
                    time.sleep(0.15)
                    mid = game.state()
                    time.sleep(0.6)
                    s1 = game.state()
                    check(scrolling["out"] == 0 and scrolling["vx"] < s0["vx"] - 20 and mid["out"] == 1 and abs(s1["vx"] - mid["vx"]) <= 4,
                          "%s: the pointer that goes there from the margin, where the map scrolls (view %d -> %d), is gone at once (%s) and the scrolling stops (%d -> %d)" % (what, s0["vx"], scrolling["vx"], "gone" if mid["out"] else "STILL HERE", mid["vx"], s1["vx"]))
                    game.move(cx, cy)
                    time.sleep(0.4)
                # ... and off the control, back into the plain margin, the map scrolls again
                x, y = spot("#lock-on")
                s0 = game.park(box)
                game.move(x, y)
                time.sleep(0.2)
                game.move(cx, bottom + 4)
                time.sleep(0.9)
                s1 = game.state()
                check(s1["out"] == 0 and s1["vy"] > s0["vy"] + 80, "off a control and into the margin below the box: the pointer is the game's again and the map scrolls down (%d -> %d)" % (s0["vy"], s1["vy"]))
                game.move(cx, cy)
                time.sleep(0.4)

                # the order of the browser's events differs: Chrome sends the canvas's leave BEFORE the pointer's motion over the margin, Safari and Firefox the motion first. A leave towards the margin that
                # comes after the motion must not end the pointer's stay (SDL would be told that the pointer went away AFTER it was handed the edge position); one towards a control must
                game.park(box)
                game.move(cx, bottom + 40)                                                        # (down: the map has room below the middle of it; the left and right edges are reached in a quarter of a second)
                time.sleep(0.15)
                before = game.state()
                tab.ev("document.getElementById('canvas').dispatchEvent(new MouseEvent('mouseleave', {bubbles: false, relatedTarget: document.body, clientX: %d, clientY: %d})); 1" % (cx, bottom + 40))
                time.sleep(0.4)
                after = game.state()
                check(before["out"] == 0 and after["out"] == 0 and after["vy"] > before["vy"] + 40,
                      "the canvas's leave towards the margin that comes AFTER the motion (Safari's and Firefox's order) is not passed on: the pointer stays (%s -> %s) and the view goes on scrolling (%d -> %d)" % (before["out"], after["out"], before["vy"], after["vy"]))
                lx, ly = spot("#lock-on")
                tab.ev("document.getElementById('canvas').dispatchEvent(new MouseEvent('mouseleave', {bubbles: false, relatedTarget: document.getElementById('lock-on'), clientX: %d, clientY: %d})); 1" % (lx, ly))
                time.sleep(0.3)
                check(game.state()["out"] == 1, "... a leave towards a control is passed on: the pointer is gone")
                game.move(cx, cy)
                time.sleep(0.4)

                # a click, a right click, a wheel and a ctrl + wheel in the margin belong to the page
                tab.ev("""window.__log = []; ['mousedown', 'mouseup', 'click', 'wheel', 'contextmenu'].forEach(function (n) {
                    window.addEventListener(n, function (e) { window.__log.push([n, e.target.id || e.target.tagName, e.defaultPrevented, e.ctrlKey]); }, false); }); 1""")
                game.park(box)
                x, y = left - 40, cy
                game.move(x, y)
                time.sleep(0.25)
                tab.click(x, y)
                tab.call("Input.dispatchMouseEvent", {"type": "mousePressed", "x": x, "y": y, "button": "right", "clickCount": 1, "buttons": 2})
                tab.call("Input.dispatchMouseEvent", {"type": "mouseReleased", "x": x, "y": y, "button": "right", "clickCount": 1})
                tab.call("Input.dispatchMouseEvent", {"type": "mouseWheel", "x": x, "y": y, "deltaX": 0, "deltaY": -120, "modifiers": 2})        # (headless Chrome has no page zoom: this one scrolls up, which the top of the page ignores)
                tab.call("Input.dispatchMouseEvent", {"type": "mouseWheel", "x": x, "y": y, "deltaX": 0, "deltaY": 120})
                time.sleep(0.9)
                log = json.loads(tab.ev("JSON.stringify(window.__log)"))
                after = game.state()
                scrollable = g["scroll"][2] > g["scroll"][3]
                scrolled = tab.ev("window.scrollY")
                wheels = [e for e in log if e[0] == "wheel"]
                check(not [e for e in log if e[1] == "canvas"] and any(e[0] == "mousedown" and e[1] in ("BODY", "HTML") for e in log),
                      "a click and a right click in the margin reach the page and not the canvas (the game's window system listens on the canvas: %s)" % [e[:2] for e in log if e[0] in ("mousedown", "click", "contextmenu")])
                check(len(wheels) == 2 and all(e[1] in ("BODY", "HTML") and not e[2] for e in wheels) and any(e[3] for e in wheels),
                      "a wheel and a ctrl + wheel in the margin are the page's: not cancelled by the page's code (%s)" % wheels)
                check(not scrollable or scrolled > 0, "the wheel in the margin scrolls the page (the page is %d px tall in a window of %d: scrollY %s)" % (g["scroll"][2], g["scroll"][3], scrolled))
                contexts = [e for e in log if e[0] == "contextmenu"]
                check(contexts and not any(e[2] for e in contexts), "the browser's own menu opens in the margin (not cancelled: %s)" % contexts)
                check(after["zoom"] == 100 and after["modal"] == 0, "the game did not zoom or open anything (zoom %s%%, dialog %s)" % (after["zoom"], after["modal"]))
                tab.ev("window.scrollTo(0, 0); 1")
                time.sleep(0.5)

                # the pointer leaves the browser window while the map scrolls
                game.park(box)
                game.move(left - 40, cy)
                time.sleep(0.3)
                scrolling = game.state()
                tab.ev("document.documentElement.dispatchEvent(new MouseEvent('mouseleave', {bubbles: false, relatedTarget: null, clientX: -3, clientY: %d})); 1" % int(cy))
                time.sleep(0.2)
                gone = game.state()
                time.sleep(0.7)
                later = game.state()
                check(scrolling["out"] == 0 and gone["out"] == 1 and abs(later["vx"] - gone["vx"]) <= 4,
                      "the pointer leaves the browser window from the margin (the page's leave): the game's pointer is gone (%s -> %s) and the view that was scrolling stops at %d (%d later)" % (scrolling["out"], gone["out"], gone["vx"], later["vx"]))
                game.move(cx, cy)
                time.sleep(0.4)

                # back on the picture: the scrolling stops, the pointer is the browser's own again, and every edge works as before
                game.park(box)
                game.move(left - 40, cy)
                time.sleep(0.3)
                game.move(cx, cy)
                time.sleep(0.3)
                a = game.state()
                time.sleep(0.6)
                b = game.state()
                check(a["out"] == 0 and b["out"] == 0 and abs(b["vx"] - a["vx"]) <= 2 and abs(b["px"] - 480) <= 1 and abs(b["py"] - 270) <= 1,
                      "back on the picture the scrolling stops (the view stays at (%d, %d)) and the game's pointer is the browser's own (%d, %d)" % (b["vx"], b["vy"], b["px"], b["py"]))
                expect_scroll("after all of that the picture's own left edge column still scrolls", box, left + 2, cy, 1, 270, -1, 0)
                expect_scroll("... and its own bottom edge row", box, cx, bottom - 2, 480, PICTURE_H - 2, 0, 1)

                # the page scrolls under a pointer that did not move: it is looked at again
                game.park(box)
                game.move(cx, bottom + 60)
                time.sleep(0.4)
                before = game.state()
                tab.ev("window.scrollTo(0, 100); 1")
                time.sleep(0.5)
                after = game.state()
                time.sleep(0.5)
                end = game.state()
                tab.ev("window.scrollTo(0, 0); 1")
                check(not scrollable or (before["out"] == 0 and after["out"] == 1 and abs(end["vy"] - after["vy"]) <= 4),
                      "the page scrolls 100 px under a pointer that did not move (it was 60 px below the box, now 160): the pointer is gone for the game (%s -> %s) and the view stops (%d -> %d)" % (before["out"], after["out"], after["vy"], end["vy"]))
                game.move(cx, cy)
                time.sleep(0.4)

                # a touch is left alone: neither a tap nor a finger that is dragged in the margin (the page scrolls under it) is a pointer at the picture's edge (the touch's own pointer events say
                # pointerType "touch"; a tap has none of motion, a drag has)
                tab.call("Emulation.setTouchEmulationEnabled", {"enabled": True, "maxTouchPoints": 5})
                game.park(box)
                s0 = game.state()
                tab.tap(left - 40, cy)
                time.sleep(0.9)
                s1 = game.state()
                check(s1["out"] == s0["out"] and s1["px"] == s0["px"] and s1["py"] == s0["py"] and abs(s1["vx"] - s0["vx"]) <= 4 and abs(s1["vy"] - s0["vy"]) <= 4,
                      "a tap in the margin is not a pointer at the picture's edge: the game's pointer stays (%d, %d, gone %d -> %d, %d, gone %d) and the view too" % (s0["px"], s0["py"], s0["out"], s1["px"], s1["py"], s1["out"]))
                tab.ev("window.__touchMoves = 0; window.addEventListener('pointermove', function (e) { if (e.pointerType === 'touch') window.__touchMoves++; }, true); 1")
                x, y = left - 40, cy
                tab.call("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": x, "y": y}]})
                for step in range(1, 6):
                    tab.call("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": [{"x": x - 3 * step, "y": y}]})
                    time.sleep(0.06)
                time.sleep(0.6)
                s2 = game.state()
                tab.call("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
                moves = tab.ev("window.__touchMoves")
                tab.call("Emulation.setTouchEmulationEnabled", {"enabled": False})
                check(moves >= 1, "(control) the finger dragged in the margin did make touch pointer events of motion (%d), which the margin must not hand to the game" % moves)
                check(s2["out"] == s0["out"] and s2["px"] == s0["px"] and s2["py"] == s0["py"] and abs(s2["vx"] - s0["vx"]) <= 4 and abs(s2["vy"] - s0["vy"]) <= 4,
                      "a finger dragged in the margin is not a pointer at the picture's edge: the game's pointer stays (%d, %d, gone %d -> %d, %d, gone %d) and the view too (%d -> %d)" % (s0["px"], s0["py"], s0["out"], s2["px"], s2["py"], s2["out"], s0["vx"], s2["vx"]))

            # the top edge: the page's header is only 66 px tall, so the room above the box is made for the test (a taller header: nothing above the box is a control but the buttons at the very top)
            game.open(width=1800, height=1000, storage={})
            tab.ev("var h = document.querySelector('header'); h.style.minHeight = '300px'; h.style.alignItems = 'flex-start'; 1")
            time.sleep(1.0)
            if started("margin, top"):
                g = game.geometry()
                box = g["box"]
                left, top, right, bottom = box[0], box[1], box[0] + box[2], box[1] + box[3]
                cx, cy = box[0] + box[2] / 2, box[1] + box[3] / 2
                check(top > 2 * MARGIN, "with the taller header the box stands at (%.0f, %.0f), %.0f x %.0f: room for the whole margin above it" % (left, top, box[2], box[3]))
                # the FIRST excursion of this page's life: the pointer goes from the picture's bottom row onto a control. SDL (2.28.4) drops the first leave of a page's life; the page primes SDL's focus with the
                # first motion over the game, so this one is delivered as the others are
                game.move(cx, cy)
                time.sleep(0.4)
                s_start = game.state()
                game.move(cx, bottom - 2)
                time.sleep(0.5)
                scrolling = game.state()
                x, y = spot("#lock-on")
                game.move(x, y)
                time.sleep(0.2)
                gone = game.state()
                time.sleep(0.7)
                later = game.state()
                check(scrolling["out"] == 0 and scrolling["vy"] > s_start["vy"] + 20 and gone["out"] == 1 and abs(later["vy"] - gone["vy"]) <= 4,
                      "the first leave of a page's life (the picture's bottom row, then a control) is delivered: gone %s -> %s, the view that was scrolling (%d -> %d) stops at %d (%d later)" % (scrolling["out"], gone["out"], s_start["vy"], scrolling["vy"], gone["vy"], later["vy"]))
                game.move(cx, cy)
                time.sleep(0.4)
                for d in (1, 40, 90, MARGIN):
                    expect_scroll("margin, %d px beyond the top edge" % d, box, cx, top - d, 480, 0, 0, -1)
                for d in (MARGIN + 1, 120):
                    expect_gone("margin, %d px beyond the top edge" % d, box, cx, top - d)
                expect_scroll("margin, the top left corner (90 px beyond on both axes)", box, left - 90, top - 90, 0, 0, -1, -1)
                expect_scroll("margin, the top right corner (90 px beyond on both axes)", box, right + 90, top - 90, PICTURE_W - 1, 0, 1, -1)
                expect_gone("margin, a corner is a square: 97 px beyond on both axes (top left)", box, left - 97, top - 97)

        if wanted("pillar"):
            print("[web edge] a wide screen (2560 x 1080, 21:9) and the classic 4:3 picture on a 1440 x 900 screen: bars at the sides")
            game.open(width=2560, height=1080, storage={"ants.pointerlock": "off"})
            if started("21:9"):
                g = game.enter_fullscreen()
                box = g["box"]
                check(g["fullscreen"] and box[0] > 300 and abs(box[3] - 1080) < 1, "the 21:9 screen: the picture %.0f x %.0f stands in the middle with bars of %.0f px at the sides" % (box[2], box[3], box[0]))
                time.sleep(1.0)
                expect_scroll("21:9", box, 20, 540, 0, 270, -1, 0)
                expect_scroll("21:9", box, 2540, 540, PICTURE_W - 1, 270, 1, 0)
                expect_scroll("21:9, the top left corner (the left bar, the top)", box, 20, 2, 0, 0, -1, -1)
                expect_scroll("21:9, the bottom right corner", box, 2540, 1078, PICTURE_W - 1, PICTURE_H - 1, 1, 1)
            game.open(width=1440, height=900, query="?aspect=4:3", storage={"ants.pointerlock": "off"})
            if started("classic 4:3"):
                g = game.enter_fullscreen()
                box = g["box"]
                check(g["fullscreen"] and abs(box[0] - 120) < 1 and abs(box[2] - 1200) < 1 and abs(box[3] - 900) < 1, "the classic 4:3 picture on the 1440 x 900 screen: %.0f x %.0f with bars of %.0f px at the sides" % (box[2], box[3], box[0]))
                time.sleep(1.0)
                expect_scroll("4:3 pillarbox, the left bar", box, 40, 450, 0, 240, -1, 0)
                expect_scroll("4:3 pillarbox, the right bar", box, 1400, 450, 639, 240, 1, 0)
                expect_scroll("4:3 pillarbox, the top left corner of the window", box, 10, 2, 0, 1, -1, -1)
                expect_scroll("4:3 pillarbox, the bottom right corner of the window", box, 1430, 897, 639, 478, 1, 1)

        if wanted("pseudo"):
            print("[web edge] the page's own fullscreen (the browser has no Fullscreen API for an element, as on an iPhone): the same bars, and no lock")
            game.open(storage={})
            tab.ev("Element.prototype.requestFullscreen = undefined; Element.prototype.webkitRequestFullscreen = undefined; 1")
            if started("pseudo"):
                tab.ev("Element.prototype.requestFullscreen = undefined; Element.prototype.webkitRequestFullscreen = undefined; 1")
                g = game.enter_fullscreen(1.5)
                check(g["pseudo"] and not g["fullscreen"] and [round(v) for v in g["box"]] == [0, 45, 1440, 810], "the page's own fullscreen is open with the picture at (0, 45), 1440 x 810 (%s)" % [round(v, 1) for v in g["box"]])
                check(not game.locked(), "no pointer lock in the page's own fullscreen (the lock is for the browser's fullscreen)")
                time.sleep(1.0)
                expect_scroll("pseudo", g["box"], 720, 20, 480, 0, 0, -1)
                expect_scroll("pseudo", g["box"], 720, 880, 480, PICTURE_H - 1, 0, 1)
                expect_scroll("pseudo", g["box"], 3, 893, 2, PICTURE_H - 1, -1, 1)
                walk_into_a_bar("pseudo", g["box"])
                # leaving the page's own fullscreen with the pointer over a bar: the game's pointer is gone
                game.park(g["box"])
                game.move(720, 880)
                time.sleep(0.4)
                s_in = game.state()
                tab.ev("document.getElementById('pseudo-exit').click(); 1")
                time.sleep(1.2)
                s_out = game.state()
                time.sleep(0.6)
                s_end = game.state()
                check(s_in["out"] == 0 and s_out["out"] == 1 and abs(s_end["vy"] - s_out["vy"]) <= 2,
                      "the page's own fullscreen left with the pointer over a bar: the game's pointer is gone (%s -> %s) and the view stays (%d -> %d)" % (s_in["out"], s_out["out"], s_out["vy"], s_end["vy"]))

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        # the pointer lock
        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        SPY = """
            window.__keys = [];
            if (navigator.keyboard && navigator.keyboard.lock) {
              var realLock = navigator.keyboard.lock.bind(navigator.keyboard), realUnlock = navigator.keyboard.unlock.bind(navigator.keyboard);
              navigator.keyboard.lock = function (keys) { window.__keys.push('lock:' + JSON.stringify(keys)); return realLock(keys); };
              navigator.keyboard.unlock = function () { window.__keys.push('unlock'); return realUnlock(); };
            }
            window.__lockEvents = [];
            window.addEventListener('pointerlockchange', function () { window.__lockEvents.push(document.pointerLockElement ? 'locked' : 'released'); }, true);       // (window capture: the page swallows the event for the game's window system, which listens on the document)
            1"""

        class Model:
            """The game's cursor of a locked pointer as the check expects it (an independent copy of the rule: the locked motion moves it by the same CSS pixels, held to the picture)."""

            def __init__(self, box):
                self.w, self.h = math.floor(box[2]), math.floor(box[3])
                self.x, self.y = box[2] / 2, box[3] / 2

            def moved(self, dx, dy):
                self.x = min(max(self.x + dx, 0), self.w - 0.001)
                self.y = min(max(self.y + dy, 0), self.h - 0.001)

            def pointer(self, scale):                                         # what the game reads: the whole pixel, then the picture's pixel
                return int(math.trunc(self.x) / scale), int(math.trunc(self.y) / scale)

        def locked_move(model, to_x, to_y):
            """The DevTools pointer goes to (to_x, to_y): a LOCKED browser reports only the difference, which the model applies too."""
            dx, dy = to_x - game.pointer[0], to_y - game.pointer[1]
            game.move(to_x, to_y)
            model.moved(dx, dy)

        def push(model, side, width, height):
            """Push the cursor against an edge (a corner with two sides): the DevTools pointer goes to the opposite end of the window first and then right across it."""
            for axis, sign in side:
                if axis == "x":
                    locked_move(model, 0 if sign > 0 else width - 1, game.pointer[1])
                    locked_move(model, width - 1 if sign > 0 else 0, game.pointer[1])
                else:
                    locked_move(model, game.pointer[0], 0 if sign > 0 else height - 1)
                    locked_move(model, game.pointer[0], height - 1 if sign > 0 else 0)

        def cursor_to(model, box, target_x, target_y):
            """Move the cursor to a point of the picture (CSS pixels of the canvas) with locked motion, however far away the DevTools pointer is."""
            for _ in range(4):
                dx, dy = target_x - model.x, target_y - model.y
                nx, ny = game.pointer[0] + dx, game.pointer[1] + dy
                if 0 <= nx <= 1439 and 0 <= ny <= 899:
                    locked_move(model, nx, ny)
                    return
                # the DevTools pointer would leave the window: bring it to the nearest point of the window first (the cursor moves by that, the model with it) and ask again
                locked_move(model, min(max(nx, 0), 1439), min(max(ny, 0), 899))

        def lock_checks():
            print("[web edge] the pointer lock: Fullscreen mouse Locked (the default) in the browser's own fullscreen on a 1440 x 900 screen")
            game.open(storage=None)
            tab.ev("localStorage.clear(); 1")
            tab.open(game.web + "?aspect=16:9", settle=2.5)
            game.pointer = [0.0, 0.0]
            tab.ev(SPY)
            if not started("lock"):
                return
            check(tab.ev("ANTS_POINTER_LOCK") is True and tab.ev("document.getElementById('lock-on').getAttribute('aria-checked')") == "true", "nothing remembered: Fullscreen mouse is Locked")
            g = game.enter_fullscreen(3.0)
            box = g["box"]
            check(g["fullscreen"] and game.locked(), "the click on the Fullscreen button gives the browser's fullscreen AND the pointer lock on the canvas (fullscreen %s, locked %s)" % (g["fullscreen"], game.locked()))
            keys = json.loads(tab.ev("JSON.stringify(window.__keys)"))
            check('lock:["Escape"]' in keys or not tab.ev("!!(navigator.keyboard && navigator.keyboard.lock)"), "Esc is locked as a key in fullscreen (navigator.keyboard.lock(['Escape']), the Chromium browsers): %s" % keys)
            check(tab.ev("JSON.stringify(window.__lockEvents)") == '["locked"]', "one lock event: %s" % tab.ev("JSON.stringify(window.__lockEvents)"))
            scale = box[2] / PICTURE_W
            model = Model(box)
            s = game.state()
            check(s["out"] == 0 and (s["px"], s["py"]) == model.pointer(scale), "the game's cursor starts in the middle of the picture: (%d, %d) = %s" % (s["px"], s["py"], model.pointer(scale)))
            shot("edge_locked_start")
            time.sleep(6.0)
            # moved by the distance of the mouse's motion
            locked_move(model, 900, 600)
            locked_move(model, 800, 300)
            locked_move(model, 810, 303)
            time.sleep(0.25)
            s = game.state()
            check((s["px"], s["py"]) == model.pointer(scale), "locked motion moves the game's cursor by its distance: at (%d, %d), the rule says %s" % (s["px"], s["py"], model.pointer(scale)))
            # the lock is the page's: the game is handed ordinary absolute positions, and the browser's real events never reach it
            tab.ev("window.__seen = []; document.getElementById('canvas').addEventListener('mousemove', function (e) { window.__seen.push(e.isTrusted); }, true); 1")
            locked_move(model, 700, 500)
            time.sleep(0.2)
            seen = json.loads(tab.ev("JSON.stringify(window.__seen)"))
            check(seen and not any(seen), "the canvas receives the page's own motion events only: none of the browser's locked motion reaches the game's window system (%s)" % seen)

            def edge_case(label, side, sx, sy, want_px, want_py):
                before = game.state()
                push(model, side, 1440, 900)
                time.sleep(0.15)
                mid = game.state()
                time.sleep(0.9)
                after = game.state()
                dx, dy = after["vx"] - before["vx"], after["vy"] - before["vy"]
                ok = mid["out"] == 0 and (mid["px"], mid["py"]) == model.pointer(scale) and (want_px is None or mid["px"] == want_px) and (want_py is None or mid["py"] == want_py)
                ok = ok and (dx * sx >= 40 if sx else True) and (dy * sy >= 40 if sy else True)
                check(ok, "%s: the cursor is pushed against the edge: (%d, %d), the view moved by (%+d, %+d)" % (label, mid["px"], mid["py"], dx, dy))
                return after

            def to_middle_of_map():
                for _ in range(2):
                    cursor_to(model, box, box[2] / 2, box[3] / 2)
                    time.sleep(0.4)
                    s_ = game.state()
                    if 300 <= s_["vx"] <= 850 and 450 <= s_["vy"] <= 1000:
                        return s_
                    # scroll towards the middle with the cursor on the matching edges
                    for _ in range(40):
                        s_ = game.state()
                        if 300 <= s_["vx"] <= 850 and 450 <= s_["vy"] <= 1000:
                            break
                        sides = []
                        if s_["vx"] < 300:
                            sides.append(("x", 1))
                        elif s_["vx"] > 850:
                            sides.append(("x", -1))
                        if s_["vy"] < 450:
                            sides.append(("y", 1))
                        elif s_["vy"] > 1000:
                            sides.append(("y", -1))
                        push(model, sides, 1440, 900)
                        time.sleep(0.08)
                    cursor_to(model, box, box[2] / 2, box[3] / 2)
                    time.sleep(0.5)
                return game.state()

            to_middle_of_map()
            edge_case("pushed up", [("y", -1)], 0, -1, None, 0)
            to_middle_of_map()
            edge_case("pushed down", [("y", 1)], 0, 1, None, PICTURE_H - 1)
            to_middle_of_map()
            edge_case("pushed left", [("x", -1)], -1, 0, 0, None)
            to_middle_of_map()
            edge_case("pushed right", [("x", 1)], 1, 0, PICTURE_W - 1, None)
            to_middle_of_map()
            edge_case("pushed into the top left corner", [("x", -1), ("y", -1)], -1, -1, 0, 0)
            to_middle_of_map()
            edge_case("pushed into the bottom right corner", [("x", 1), ("y", 1)], 1, 1, PICTURE_W - 1, PICTURE_H - 1)
            to_middle_of_map()
            edge_case("pushed into the top right corner", [("x", 1), ("y", -1)], 1, -1, PICTURE_W - 1, 0)
            to_middle_of_map()
            edge_case("pushed into the bottom left corner", [("x", -1), ("y", 1)], -1, 1, 0, PICTURE_H - 1)
            # the way back from an edge is at once: no cursor beyond the picture to bring back
            push(model, [("y", -1)], 1440, 900)
            push(model, [("y", -1)], 1440, 900)
            time.sleep(0.2)
            locked_move(model, game.pointer[0], min(game.pointer[1] + 60, 899))
            time.sleep(0.8)
            a = game.state()
            time.sleep(0.5)
            b = game.state()
            check(a["py"] > 12 and abs(b["vy"] - a["vy"]) <= 2, "pushed against the top for good and moved back by 60 px: the cursor is off the edge at once (y %d) and the scrolling has stopped (%d -> %d)" % (a["py"], a["vy"], b["vy"]))
            # a click goes where the game's cursor is: the cursor is moved onto the Options button and the click opens the options
            to_middle_of_map()
            target = (OPTIONS_BUTTON[0] * scale, OPTIONS_BUTTON[1] * scale)
            cursor_to(model, box, target[0], target[1])
            time.sleep(0.3)
            s = game.state()
            check((s["px"], s["py"]) == model.pointer(scale) and abs(s["px"] - OPTIONS_BUTTON[0]) <= 1 and abs(s["py"] - OPTIONS_BUTTON[1]) <= 1 and s["modal"] == 0,
                  "the cursor is on the Options button at (%d, %d), nothing is open yet" % (s["px"], s["py"]))
            tab.call("Input.dispatchMouseEvent", {"type": "mousePressed", "x": game.pointer[0], "y": game.pointer[1], "button": "left", "clickCount": 1, "buttons": 1})
            time.sleep(0.1)
            tab.call("Input.dispatchMouseEvent", {"type": "mouseReleased", "x": game.pointer[0], "y": game.pointer[1], "button": "left", "clickCount": 1})
            time.sleep(0.6)
            s = game.state()
            check(s["modal"] == 1, "a click goes where the game's cursor is: the options opened (dialog %s)" % s["modal"])
            shot("edge_locked_options")
            # the lock let go (as the browser does for Esc, a lost focus): nothing happens until the pointer moves
            before = game.state()
            events_before = json.loads(tab.ev("JSON.stringify(window.__lockEvents)"))
            tab.ev("window.__seen.length = 0; 1")
            tab.ev("document.exitPointerLock(); 1")
            time.sleep(1.0)
            after = game.state()
            check(not game.locked() and json.loads(tab.ev("JSON.stringify(window.__lockEvents)"))[-1] == "released", "the lock is let go: the pointer is the normal one (%s)" % events_before)
            seen = json.loads(tab.ev("JSON.stringify(window.__seen)"))
            check((after["px"], after["py"], after["out"]) == (before["px"], before["py"], before["out"]) and not seen,
                  "... and the game's cursor stays where it was: no jump, no event for the game while the pointer rests (%s -> %s, events %s)" % ((before["px"], before["py"]), (after["px"], after["py"]), seen))
            game.move(900, 600)
            time.sleep(0.3)
            s = game.state()
            check((s["px"], s["py"]) == (int((900 - box[0]) / scale), int((600 - box[1]) / scale)), "the first move of the free pointer puts the game's cursor under it, at (%d, %d)" % (s["px"], s["py"]))
            # a click on the game takes the lock again, the cursor staying where the click is
            click_at = (700, 500)
            game.move(*click_at)
            tab.click(*click_at)
            game.pointer = list(click_at)
            time.sleep(0.8)
            s = game.state()
            check(game.locked() and (s["px"], s["py"]) == (int((click_at[0] - box[0]) / scale), int((click_at[1] - box[1]) / scale)),
                  "a click on the game takes the lock again (locked %s), the cursor staying at the click (%d, %d)" % (game.locked(), s["px"], s["py"]))
            # leaving fullscreen lets the lock go and unlocks Esc, the cursor staying where it is
            before = game.state()
            tab.ev("document.exitFullscreen(); 1")
            time.sleep(1.5)
            keys = json.loads(tab.ev("JSON.stringify(window.__keys)"))
            after = game.state()
            g = game.geometry()
            check(not g["fullscreen"] and not game.locked() and (not tab.ev("!!(navigator.keyboard && navigator.keyboard.lock)") or keys[-1] == "unlock"),
                  "fullscreen left: the lock is let go and Esc is unlocked (fullscreen %s, locked %s, keys %s)" % (g["fullscreen"], game.locked(), keys[-3:]))
            check((after["px"], after["py"]) == (before["px"], before["py"]), "... and the game's cursor stays (%s -> %s)" % ((before["px"], before["py"]), (after["px"], after["py"])))

        if wanted("lock"):
            lock_checks()

        def release_and_refusal_checks():
            print("[web edge] a browser that refuses the lock, and the lock that is asked for when the player enters fullscreen again")
            game.open(storage=None)
            tab.ev("localStorage.clear(); 1")
            tab.open(game.web + "?aspect=16:9", settle=2.5)
            tab.ev("window.__errors = []; window.addEventListener('error', function (e) { window.__errors.push(String(e.message)); }); Element.prototype.requestPointerLock = function () { return Promise.reject(new Error('refused')); }; 1")
            if not started("refused"):
                return
            g = game.enter_fullscreen(2.5)
            check(g["fullscreen"] and not game.locked() and not json.loads(tab.ev("JSON.stringify(window.__errors)")), "the browser refuses the lock: fullscreen works all the same, no error (%s)" % tab.ev("JSON.stringify(window.__errors)"))
            time.sleep(1.0)
            expect_scroll("refused lock", g["box"], 720, 20, 480, 0, 0, -1)
            expect_scroll("refused lock", g["box"], 720, 880, 480, PICTURE_H - 1, 0, 1)

        if wanted("release"):
            release_and_refusal_checks()

        def relock_checks():
            print("[web edge] the lock taken again by a click: only that click when the page held the lock before, an ordinary click when it never did; the free pointer is quiet after a release")
            box = [0, 45, 1440, 810]
            quit_at = (899 + 23, 7 + 11)                                           # the Quit button of the 16:9 picture, its middle (the classic (579, 7, 46, 23) moved by 320)
            options_at = OPTIONS_BUTTON
            on_screen = lambda c: (c[0] * 1.5, 45 + c[1] * 1.5)
            # 1. the page held the lock, the browser let it go
            game.open(storage=None)
            tab.ev("localStorage.clear(); 1")
            tab.open(game.web + "?aspect=16:9", settle=2.5)
            game.pointer = [0.0, 0.0]
            tab.ev(SPY)
            if not started("relock"):
                return
            g = game.enter_fullscreen(3.0)
            check(g["fullscreen"] and game.locked(), "(setup) fullscreen and the lock")
            time.sleep(5.0)
            tab.ev("document.exitPointerLock(); 1")
            time.sleep(0.8)
            check(not game.locked(), "the browser let the lock go")
            before = game.state()
            game.move(720, 20)                                                     # the free pointer appears over the top bar, where the Fullscreen button was: the first report of it
            time.sleep(0.4)
            quiet = game.state()
            game.move(726, 20)                                                     # ... and it moves
            time.sleep(0.4)
            moved = game.state()
            check((quiet["px"], quiet["py"], quiet["out"]) == (before["px"], before["py"], before["out"]) and (moved["px"], moved["py"]) == (484, 0) and moved["out"] == 0,
                  "after the release the first report of the free pointer over a bar hands the game nothing (%s -> %s: no jump), the pointer that moves does (%s)" % ((before["px"], before["py"]), (quiet["px"], quiet["py"]), (moved["px"], moved["py"])))
            x, y = on_screen(quit_at)
            game.move(x, y)
            tab.click(x, y)                                                         # (the click that takes the lock again, over the Quit button)
            game.pointer = [x, y]
            time.sleep(0.8)
            s_ = game.state()
            check(game.locked() and s_["modal"] == 0, "the click on the game takes the lock again and is only that: the Quit button under it was not pressed (locked %s, dialog %s)" % (game.locked(), s_["modal"]))
            tab.call("Input.dispatchMouseEvent", {"type": "mousePressed", "x": x, "y": y, "button": "left", "clickCount": 1, "buttons": 1})
            tab.call("Input.dispatchMouseEvent", {"type": "mouseReleased", "x": x, "y": y, "button": "left", "clickCount": 1})
            time.sleep(0.6)
            check(game.state()["modal"] == 1, "... and the next click, with the lock held, presses it: the quit dialog opens")
            # 2. the page never held the lock (the first request was refused): the click that gets it is an ordinary click too
            game.open(storage=None)
            tab.ev("localStorage.clear(); 1")
            tab.open(game.web + "?aspect=16:9", settle=2.5)
            game.pointer = [0.0, 0.0]
            tab.ev("window.__realLock = Element.prototype.requestPointerLock; Element.prototype.requestPointerLock = function () { document.dispatchEvent(new Event('pointerlockerror')); return Promise.reject(new Error('refused')); }; 1")
            if not started("never held"):
                return
            g = game.enter_fullscreen(2.5)
            check(g["fullscreen"] and not game.locked(), "(setup) fullscreen, the first request of the lock refused")
            tab.ev("Element.prototype.requestPointerLock = window.__realLock; 1")
            time.sleep(4.0)
            x, y = on_screen(options_at)
            game.move(x, y)
            tab.click(x, y)
            game.pointer = [x, y]
            time.sleep(0.8)
            s_ = game.state()
            check(game.locked() and s_["modal"] == 1, "a click on the game when the page never held the lock gets the lock and is an ordinary click: the options opened (locked %s, dialog %s)" % (game.locked(), s_["modal"]))

        if wanted("relock"):
            relock_checks()

        def setting_checks():
            print("[web edge] the control under the game (Fullscreen mouse: Locked / Free) and its key")
            game.open(storage=None)
            tab.ev("localStorage.clear(); 1")
            tab.open(game.web + "?aspect=16:9", settle=2.5)
            shown = lambda: tab.ev("[document.getElementById('lock-on').getAttribute('aria-checked'), document.getElementById('lock-off').getAttribute('aria-checked')].join('/')")
            check(shown() == "true/false" and tab.ev("localStorage.getItem('ants.pointerlock')") is None, "a fresh browser: Locked, nothing stored (%s)" % shown())
            box = tab.ev("(function(){var r = document.getElementById('lock-off').getBoundingClientRect(); return JSON.stringify([r.x + r.width / 2, r.y + r.height / 2, r.width, r.height]);})()")
            x, y, w, h = json.loads(box)
            check(w > 20 and h > 20, "the Free button is on the page (%.0f x %.0f px)" % (w, h))
            shot("edge_setting_locked")
            tab.click(x, y)
            time.sleep(0.4)
            check(shown() == "false/true" and tab.ev("localStorage.getItem('ants.pointerlock')") == "off", "Free clicked: shown, remembered as 'off' (%s)" % shown())
            tab.open(game.web + "?aspect=16:9", settle=2.5)
            check(shown() == "false/true" and tab.ev("ANTS_POINTER_LOCK") is False, "the next visit comes back Free (%s)" % shown())
            tab.ev(SPY)
            game.pointer = [0.0, 0.0]
            g = game.enter_fullscreen(2.5)
            keys = json.loads(tab.ev("JSON.stringify(window.__keys)"))
            check(g["fullscreen"] and not game.locked() and tab.ev("JSON.stringify(window.__lockEvents)") == "[]" and 'lock:["Escape"]' not in keys,
                  "Free: fullscreen without the pointer lock and without locking Esc (locked %s, lock events %s, keys %s)" % (game.locked(), tab.ev("JSON.stringify(window.__lockEvents)"), keys))
            check(tab.ev("getComputedStyle(document.getElementById('game-stage')).cursor") == "none", "Free: the browser's arrow is hidden over the stage in fullscreen all the same")
            tab.ev("document.exitFullscreen(); 1")
            time.sleep(1.2)
            lock_button = json.loads(tab.ev("(function(){var r = document.getElementById('lock-on').getBoundingClientRect(); return JSON.stringify([r.x + r.width / 2, r.y + r.height / 2]);})()"))
            tab.click(lock_button[0], lock_button[1])
            time.sleep(0.4)
            check(shown() == "true/false" and tab.ev("localStorage.getItem('ants.pointerlock')") == "on", "Locked clicked again: shown, remembered as 'on' (%s)" % shown())
            tab.ev("localStorage.setItem('ants.pointerlock', 'junk'); 1")
            tab.open(game.web + "?aspect=16:9", settle=2.5)
            check(shown() == "true/false", "a stored value that is no choice is Locked (%s)" % shown())
            tab.ev("localStorage.removeItem('ants.pointerlock'); 1")
            # a phone has no use for it (no hovering pointer): the control is not shown there, and is on a desktop
            visible = lambda: tab.ev("getComputedStyle(document.getElementById('lock-on').parentElement).display !== 'none' && getComputedStyle(document.getElementById('lock-bar-label')).display !== 'none'")
            check(visible() is True, "(control) on a desktop window the control is shown")
            tab.emulate(844, 390, 3, True)
            tab.open(game.web + "?aspect=16:9", settle=1.5)
            check(visible() is False, "a phone (no pointer that hovers) is not shown the control")
            tab.emulate(1440, 900, 1)

        if wanted("setting"):
            setting_checks()

        def slow_checks():
            print("[web edge] a slow mouse: the game's cursor travels as far as the mouse does (steps of a quarter of a pixel, at device ratios 1 and 2; the browser reports whole pixels of movement and keeps the rest)")
            for dpr in (1, 2):
                game.open(width=1440 // dpr, height=900 // dpr, dpr=dpr, storage=None)
                tab.ev("localStorage.clear(); 1")
                tab.open(game.web + "?aspect=16:9", settle=2.5)
                game.pointer = [0.0, 0.0]
                if not started("slow @%d" % dpr):
                    continue
                g = game.enter_fullscreen(3.0)
                if not (g["fullscreen"] and game.locked()):
                    check(False, "slow @%d: fullscreen and the lock" % dpr)
                    continue
                time.sleep(5.0)
                tab.ev("window.__mv = []; window.addEventListener('mousemove', function (e) { if (e.isTrusted) window.__mv.push([e.movementX, e.movementY]); }, true); 1")
                start = json.loads(tab.ev("JSON.stringify(lockCursor)"))
                x0 = game.pointer[0]
                steps = 80
                for i in range(steps):
                    game.move(x0 + 0.25 * (i + 1), game.pointer[1])
                    time.sleep(0.02)
                time.sleep(0.4)
                end = json.loads(tab.ev("JSON.stringify(lockCursor)"))
                mv = json.loads(tab.ev("JSON.stringify(window.__mv)"))
                total = sum(m[0] for m in mv)
                check(abs((end["x"] - start["x"]) - total) < 1e-6 and abs(total - 0.25 * steps) <= 1.0,
                      "device ratio %s: %d steps of 0.25 css px (the browser reported a movementX of %s in all: %s): the game's cursor travelled %.2f px, as far as the mouse" % (dpr, steps, total, mv[:6], end["x"] - start["x"]))

        if wanted("slow"):
            slow_checks()

        tab.close()
    except NotReachable as error:
        print("  SKIP: the check could not be made (%s)" % error)
        return 1 if failures else 3
    except Exception as error:                                                # (a hang, a crash, a script that raised: the page or the browser broke down while it was checked)
        import traceback
        traceback.print_exc()
        print("  FAIL: the browser or the page broke down during the check (%s: %s)" % (type(error).__name__, error))
        return 1
    finally:
        if browser is not None:
            browser.close()
    print("[web edge] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
