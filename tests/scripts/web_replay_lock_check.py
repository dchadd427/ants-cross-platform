#!/usr/bin/env python3
"""The mouse in the FULLSCREEN of a replay, in a REAL browser with the REAL game (opt-in; see tests/scripts/test_web_replay_lock.sh, docs/REPLAYS.md "Watching a replay" and
docs/PLAY_IN_BROWSER.md "The mouse in fullscreen").

The game page takes the pointer lock when the player's click makes fullscreen, lets it go when fullscreen ends, and moves its own cursor by the mouse's motion, held to the picture, so that
every edge scrolls the map. A replay (and a live match watched: the same page) was left with a free mouse; it must do what the game does, and its bar must stay clickable while the mouse
is held. Needs the web page of this tree (`tools/web_without_docker.py -- tests/scripts/test_web_replay_lock.sh`), a Chromium-based browser, Python 3 and a recording (ANTS_CAMERA_REPLAY or
--replay, as for tests/scripts/test_web_camera.sh). No game server is needed: the page's `fetch('/replays/<file>')` is answered here. Headless Chrome grants the lock and its input gives the
page the real locked motion (`movementX / movementY`, the position frozen). What it checks:

  * the GAME page (play.html): the click on Fullscreen makes fullscreen AND the lock on the canvas, and leaving fullscreen lets the lock go (the baseline that the replay must match);
  * the REPLAY page (play.html?replay=...), 1440 x 900: the same click gives fullscreen AND the lock on the canvas, Esc is locked as a key (navigator.keyboard.lock), the game's cursor
    starts in the middle of the picture and moves by the mouse's motion, an edge of the picture scrolls the map (and takes the camera), the cursor stays on the picture;
  * the BAR under the lock: the bar shows when the mouse moves; the game's cursor moved onto Play (the page draws a small arrow there, the game is told that its pointer left) and a click
    plays, a second pauses; a click on the timeline jumps; a click on a speed changes it; the Picture button opens its window; the lock is still there after all of it; the cursor off the bar
    is the game's again (the arrow is gone); a bar that has gone away is not hit;
  * the lock is let go with fullscreen (exitFullscreen: what Esc does), the lock events are one 'locked' and one 'released', the arrow is gone;
  * "Fullscreen mouse: Free" (the remembered key ants.pointerlock = off): a replay asks for no lock, and the bar is clickable by the free pointer as ever.

What headless Chrome cannot show: the browser's own bubble ("Press Esc to show your cursor") and whether a desktop browser asks the user first (the lock is asked for in the click's gesture, as the
game's is), the Mac's Dock and menu bar, Safari and Firefox.

Exit status 0: every check passed; 1: a check failed, or the browser or the page broke down; 3: the check could not be made because the environment is not there; 2: a bad command line.
"""
import argparse
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_aspect_check as aspect                                            # noqa: E402
from web_aspect_check import Browser, NotReachable, Tab                      # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402
from web_camera_check import Replay                                          # noqa: E402  (the tab with the recording, answered by the check)

SPY = """
    window.__keys = [];
    if (navigator.keyboard && navigator.keyboard.lock) {
      var realLock = navigator.keyboard.lock.bind(navigator.keyboard), realUnlock = navigator.keyboard.unlock.bind(navigator.keyboard);
      navigator.keyboard.lock = function (keys) { window.__keys.push('lock:' + JSON.stringify(keys)); return realLock(keys); };
      navigator.keyboard.unlock = function () { window.__keys.push('unlock'); return realUnlock(); };
    }
    window.__lockEvents = [];
    window.addEventListener('pointerlockchange', function () { window.__lockEvents.push(document.pointerLockElement ? 'locked' : 'released'); }, true);
    1"""

STATE = r"""
JSON.stringify((function () {
  var $ = function (id) { return document.getElementById(id); };
  var r = function (e) { var b = e.getBoundingClientRect(); return [b.x, b.y, b.width, b.height]; };
  var lock = document.pointerLockElement || document.webkitPointerLockElement;
  var cur = $('rcursor');
  return { fullscreen: !!(document.fullscreenElement || document.webkitFullscreenElement), locked: !!lock && lock.id === 'canvas', canvas: r($('canvas')),
           play: r($('b-play')), tl: r($('tl')), rail: r($('tl').querySelector('.rail')), speed4: r($('speed').querySelector('button[data-s="4"]')), pic: r($('picbtn')), bar: r($('rbar')), sel: r($('pic-select')), o5: r($('pic-select-o5')), listOpen: !$('pic-select-list').hidden, shape: $('game-stage').getAttribute('data-aspect'), focus: (document.activeElement || {}).id || '',
           over: $('rbar').classList.contains('over'), idle: $('rbar').classList.contains('idle'), mark: cur ? !cur.hidden : null, markAt: cur ? cur.style.transform : null,
           picOpen: !$('picpop').hidden, events: window.__lockEvents || [], keys: window.__keys || [],
           game: [Module._ants_replay_get(0), Module._ants_replay_get(1)], pointer: [Module._ants_probe(0), Module._ants_probe(1), Module._ants_probe(2)],
           view: [Module._ants_probe(3), Module._ants_probe(4)], pref: (function () { try { return localStorage.getItem('ants.pointerlock'); } catch (e) { return 'throws'; } })() };
})())
"""


def wait_for(condition, timeout=4.0, step=0.05):
    deadline = time.time() + timeout
    value = condition()
    while not value and time.time() < deadline:
        time.sleep(step)
        value = condition()
    return value


class Cursor:
    """The game's cursor of a locked page, driven through a headless browser that reports the DevTools pointer's motion in a way of its own: each `mouseMoved` to (x, y) of a locked page gives the page
    a motion of (x, y) (measured from the window's corner, not from the last position) followed by the motion (-x, -y) that undoes it. The page's cursor is held to the picture after each of the two, so the
    pair is not nothing at an edge: with x > 0 the cursor ends at min(cursor, width - x), with x < 0 at max(cursor, -x), and the same for y. That is real locked motion all the same (the page's own
    code moves the cursor by it), and it is what `push` uses; `place` puts the cursor on a point (the page's variable, then a one pixel motion that makes the page look at where the cursor is)."""

    def __init__(self, tab, canvas):
        self.tab = tab
        self.canvas = canvas
        self.at = 1                                                         # (where the DevTools pointer is, on the diagonal: it moves at each `place`, a move to where it is gives the page no event)

    def mouse(self, kind, x, y, **extra):
        """(a long time to answer: a playing match under a loaded machine takes its time to acknowledge an input event)"""
        params = {"type": kind, "x": x, "y": y, "button": "left", "clickCount": 1}
        params.update(extra)
        self.tab.dt.call("Input.dispatchMouseEvent", params, session=self.tab.session, timeout=300)

    def place(self, x, y):
        self.tab.ev("lockCursor = { x: %r, y: %r }; 1" % (float(x), float(y)))
        self.at = 2 if self.at == 1 else 1
        self.mouse("mouseMoved", self.at, self.at, button="none")
        time.sleep(0.2)

    def cursor(self):
        c = json.loads(self.tab.ev("JSON.stringify(lockCursor)"))
        return c["x"], c["y"]

    def push(self, side):
        """Locked motion of the mouse against one side of the picture: the cursor ends at that edge"""
        x, y = {"left": (1439, 1), "top": (1, 899), "right": (-1439, 1), "bottom": (1, -899)}[side]
        self.mouse("mouseMoved", x, y, button="none")
        time.sleep(0.3)

    def press(self):
        self.mouse("mousePressed", self.at, self.at)
        time.sleep(0.08)
        self.mouse("mouseReleased", self.at, self.at)
        time.sleep(0.3)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the image, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--replay", default=os.environ.get("ANTS_CAMERA_REPLAY", ""), help="a recording of a match (default: ANTS_CAMERA_REPLAY)")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--ready-timeout", type=float, default=180.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 180)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout
    if not args.replay or not os.path.isfile(args.replay):
        print("  SKIP: no recording to watch: ANTS_BOT_MATCH_OUT=/tmp/match.antsrep ANTS_TEST_FILTER=RA8.8 ./build/tests/test_app/test_replay_app writes one; give it as ANTS_CAMERA_REPLAY")
        return 3
    with open(args.replay, "rb") as f:
        data = f.read()
    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    browser = None
    try:
        browser = Browser(path)
        tab = Tab(browser)
        try:
            game_page(tab, args, check)
            replay_page(Replay(tab, args.web, data, False), args, check)
            for shape in (("21:9", 1440, 900), ("4:3", 1440, 900), ("16:10", 1440, 900)):
                letterbox(Replay(tab, args.web, data, False), args, check, *shape)
            free_setting(Replay(tab, args.web, data, False), args, check)
        finally:
            tab.close()
    except NotReachable as e:
        if not count[0]:
            print("  SKIP: %s" % e)
            return 3
        print("  FAIL: %s" % e)
        return 1
    except (RuntimeError, TimeoutError, ConnectionError, OSError, ValueError) as e:
        print("  FAIL: the browser or the page broke down during the check: %r" % (e,))
        return 1
    finally:
        if browser is not None:
            browser.close()
    print("[web replay lock] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


def state(tab):
    return json.loads(tab.ev(STATE))


def click_element(tab, element_id):
    """The player's click (a real one: the pointer is free) on a page element"""
    x, y = json.loads(tab.ev("JSON.stringify((function(){var b=document.getElementById(%s).getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())" % json.dumps(element_id)))
    tab.click(x, y)
    return x, y


def game_page(tab, args, check):
    print("[web replay lock] the game page (play.html): the baseline")
    web = args.web if args.web.endswith("/") else args.web + "/"
    tab.emulate(1440, 900, 1)
    tab.open(web + "play.html?aspect=16:9", settle=0.5)
    tab.ev("localStorage.clear(); 1")
    tab.open(web + "play.html?aspect=16:9", settle=2.5)
    tab.ev(SPY)
    click_element(tab, "fullscreen-btn")
    time.sleep(3.0)
    locked = bool(tab.ev("(document.pointerLockElement || {}).id === 'canvas'"))
    check(bool(tab.ev("!!document.fullscreenElement")) and locked, "the game: the click on Fullscreen gives fullscreen and the pointer lock on the canvas (locked %s)" % locked)
    tab.ev("document.exitFullscreen(); 1")
    time.sleep(1.5)
    check(not tab.ev("!!document.pointerLockElement") and not tab.ev("!!document.fullscreenElement"), "the game: leaving fullscreen lets the lock go")
    check(json.loads(tab.ev("JSON.stringify(window.__lockEvents)")) == ["locked", "released"], "the game: one lock and one release (%s)" % tab.ev("JSON.stringify(window.__lockEvents)"))


def replay_page(p, args, check):
    print("[web replay lock] the replay page: Fullscreen mouse Locked (the default), the browser's own fullscreen on a 1440 x 900 screen")
    tab = p.tab
    p.open(1440, 900, 1, storage={})
    tab.ev(SPY)
    p.pause(True)
    time.sleep(0.5)
    s = state(tab)
    check(not s["fullscreen"] and not s["locked"] and s["pref"] is None, "before: a window, no lock, nothing remembered")
    click_element(tab, "fullscreen-btn-r")
    time.sleep(3.0)
    s = state(tab)
    check(s["fullscreen"] and s["locked"], "the click on Fullscreen gives the browser's fullscreen AND the pointer lock on the canvas, as in the game (fullscreen %s, locked %s)" % (s["fullscreen"], s["locked"]))
    check('lock:["Escape"]' in s["keys"] or not tab.ev("!!(navigator.keyboard && navigator.keyboard.lock)"), "Esc is locked as a key (the Chromium browsers): %s" % s["keys"])
    check(s["events"] == ["locked"] and s["over"], "one lock event, and the bar lies over the picture (%s)" % s["events"])
    tab.save_shot(args.shots, "replay-lock-fullscreen")
    cur = Cursor(tab, s["canvas"])
    canvas = s["canvas"]
    scale = canvas[2] / 960
    mx, my = cur.cursor()
    check(0 <= mx < canvas[2] and 0 <= my < canvas[3] and s["pointer"][2] == 0, "the game's cursor is on the picture and the game has it: (%.0f, %.0f) of %.0f x %.0f, the game reads %s" % (mx, my, canvas[2], canvas[3], s["pointer"]))
    # locked motion moves it (the ceiling of a motion of 1000 px to the right: width - 1000)
    cur.place(canvas[2] / 2, canvas[3] / 2)
    cur.mouse("mouseMoved", 1000, 1, button="none")
    time.sleep(0.3)
    mx, my = cur.cursor()
    s = state(tab)
    check(abs(mx - (canvas[2] - 1000)) <= 1 and s["pointer"][2] == 0 and abs(s["pointer"][0] - int(mx / scale)) <= 1, "locked motion moves the game's cursor: x = %.0f (width - 1000 = %.0f), the game reads %s" % (mx, canvas[2] - 1000, s["pointer"][:2]))

    # an edge of the picture scrolls the map and the cursor stays on the picture: the left one by real locked motion against that side, the bottom one (where the bar lies) with the cursor put on its last row
    scrolls = []
    for side in ("left", "bottom"):
        cur.place(canvas[2] / 2, canvas[3] / 2)
        before = state(tab)["view"]
        if side == "left":
            cur.push("left")
        else:
            cur.place(canvas[2] / 2, canvas[3] - 0.5)
        time.sleep(1.2)
        after = state(tab)
        mx, my = cur.cursor()
        edge = {"left": mx < 3, "bottom": my > canvas[3] - 3}[side]
        moved = abs(after["view"][0] - before[0]) + abs(after["view"][1] - before[1])
        check(edge and after["locked"] and (side == "bottom" or after["pointer"][2] == 0), "the cursor at the %s edge: it is held there (%.0f, %.0f) and the game reads it (%s)" % (side, mx, my, after["pointer"]))
        scrolls.append(moved >= 20)
        print("        (view %s -> %s)" % (before, after["view"]))
        if side == "left":
            tab.save_shot(args.shots, "replay-lock-edge-left")
    check(any(scrolls), "the map scrolls at an edge, with the bar up (left, bottom: %s; a view that is at the map's own end cannot scroll that way)" % scrolls)

    # the bar: shown by the mouse, reached with the game's cursor
    cur.place(canvas[2] / 2, canvas[3] / 2 - 100)
    s = state(tab)
    check(s["over"] and not s["idle"], "the bar is there (paused: it stays)")

    def centre(key):
        return s[key][0] + s[key][2] / 2 - canvas[0], s[key][1] + s[key][3] / 2 - canvas[1]

    cur.place(*centre("play"))
    s = state(tab)
    check(s["mark"] is True and s["pointer"][2] == 1, "the cursor on Play: the arrow is drawn there (%s) and the game's pointer is gone from its picture (out %d)" % (s["markAt"], s["pointer"][2]))
    tab.save_shot(args.shots, "replay-lock-cursor-on-bar")
    paused = s["game"][0]
    cur.press()
    s = state(tab)
    check(s["game"][0] != paused and s["locked"], "a click there plays the match (state %s -> %s) and the mouse is still held" % (paused, s["game"][0]))
    cur.place(*centre("play"))                                              # (the motion calls the bar back if it has gone away in the meantime)
    cur.press()
    s2 = state(tab)
    check(s2["game"][0] == paused and s2["locked"], "a second click pauses (state %s)" % s2["game"][0])
    turn0 = s2["game"][1]
    s = s2
    cur.place(s["rail"][0] + s["rail"][2] * 0.7 - canvas[0], s["tl"][1] + s["tl"][3] / 2 - canvas[1])
    cur.press()
    time.sleep(1.5)
    s = state(tab)
    check(s["game"][1] > turn0 + 100 and s["locked"], "a click on the timeline at 70 percent jumps there (turn %d -> %d)" % (turn0, s["game"][1]))
    cur.place(*centre("speed4"))
    cur.press()
    ticked = tab.ev("document.querySelector('#speed button[data-s=\"4\"]').getAttribute('aria-checked')")
    check(ticked == "true", "a click on 4x chooses it (aria-checked %s)" % ticked)
    p.speed("1")                                                            # (a playing match is slow to answer in a loaded machine: back to 1x)
    s = state(tab)
    cur.place(*centre("pic"))
    cur.press()
    s = state(tab)
    check(s["picOpen"] and s["locked"], "the Picture button opens its window with the mouse held")
    tab.save_shot(args.shots, "replay-lock-picture-window")
    # the window's dropdown is the page's own list, not the browser's: a native select cannot be opened by the page's cursor (the real press goes to the canvas, the bar gets synthetic events)
    cur.place(*centre("sel"))
    cur.press()
    s = state(tab)
    check(s["listOpen"] and s["picOpen"] and s["locked"] and s["focus"] == "pic-select", "the dropdown's list opens with the mouse held (the window stays, the focus is on the dropdown)")
    tab.save_shot(args.shots, "replay-lock-picture-list")
    cur.place(*centre("o5"))
    cur.press()
    s = state(tab)
    check(not s["listOpen"] and s["shape"] == "21:9" and s["picOpen"] and s["locked"] and s["focus"] == "pic-select", "a click on 21:9 in the list chooses it with the mouse held: the picture is 21:9 (%s), the window stays, the lock stays" % s["shape"])
    key = lambda name, vk: [tab.call("Input.dispatchKeyEvent", {"type": kind, "key": name, "code": name, "windowsVirtualKeyCode": vk}) for kind in ("keyDown", "keyUp")]
    key("Enter", 13)
    key("Escape", 27)
    s = state(tab)
    check(not s["listOpen"] and s["picOpen"] and s["locked"] and s["fullscreen"] and s["focus"] == "pic-select", "Enter opens the list and Esc closes the list ALONE: the Picture window stays, and so do the lock and fullscreen")
    cur.place(*centre("pic"))
    cur.press()                                                             # (closes the window again)
    cur.place(canvas[2] / 2, canvas[3] / 2 - 100)
    s = state(tab)
    check(s["mark"] is False and s["pointer"][2] == 0 and s["locked"], "the cursor back on the picture: the arrow is gone and the game has its pointer again (out %d)" % s["pointer"][2])

    # a bar that has gone away is not hit: play, rest, and the bar idles
    p.pause(False)
    p.speed("1")
    idle = wait_for(lambda: state(tab)["idle"], 6.0, 0.25)
    s = state(tab)
    check(bool(idle) and s["mark"] is False, "the bar goes away by itself while the match plays, the mouse held and at rest (the arrow with it): idle %s, arrow %s" % (bool(idle), s["mark"]))
    cur.place(canvas[2] / 2, canvas[3] - 40)                                # (the motion calls the bar back)
    s = state(tab)
    check(not s["idle"] and s["locked"], "the mouse calls the bar back while held")

    # leaving fullscreen (what Esc does) lets the lock go
    p.pause(True)
    tab.ev("document.exitFullscreen(); 1")
    time.sleep(1.5)
    s = state(tab)
    check(not s["fullscreen"] and not s["locked"] and s["mark"] is False and s["events"] == ["locked", "released"] and "unlock" in s["keys"],
          "leaving fullscreen: the lock is let go, the keys are unlocked, the arrow is gone (events %s, keys %s)" % (s["events"], s["keys"]))
    tab.save_shot(args.shots, "replay-lock-left")


def letterbox(p, args, check, shape, width, height):
    """A picture that is not the screen's shape has black bars (the stage is the whole screen, the picture a smaller box): the locked cursor is held to the picture, so the bar must lie inside it to be reachable."""
    print("[web replay lock] the replay page locked on a %dx%d screen with a %s picture (black bars)" % (width, height, shape))
    tab = p.tab
    p.url = p.url.replace("aspect=16:9", "aspect=" + shape)
    p.open(width, height, 1, storage={})
    tab.ev(SPY)
    p.pause(True)
    click_element(tab, "fullscreen-btn-r")
    time.sleep(3.0)
    s = state(tab)
    canvas = s["canvas"]
    inside = s["bar"][0] >= canvas[0] - 1 and s["bar"][1] >= canvas[1] - 1 and s["bar"][0] + s["bar"][2] <= canvas[0] + canvas[2] + 1 and s["bar"][1] + s["bar"][3] <= canvas[1] + canvas[3] + 1
    check(s["fullscreen"] and s["locked"] and s["over"] and inside, "%s: fullscreen and locked, and the whole bar lies inside the picture (bar %s, picture %s)" % (shape, [round(v) for v in s["bar"]], [round(v) for v in canvas]))
    tab.save_shot(args.shots, "replay-lock-%s" % shape.replace(":", "x"))
    cur = Cursor(tab, canvas)

    def centre(key):
        return s[key][0] + s[key][2] / 2 - canvas[0], s[key][1] + s[key][3] / 2 - canvas[1]

    for key, label in (("play", "Play"), ("speed4", "4x"), ("pic", "Picture")):
        cur.place(*centre(key))
        before = state(tab)
        cur.press()
        after = state(tab)
        if key == "play":
            ok = after["game"][0] != before["game"][0]
        elif key == "speed4":
            ok = tab.ev("document.querySelector('#speed button[data-s=\"4\"]').getAttribute('aria-checked')") == "true"
        else:
            ok = after["picOpen"]
        check(ok and after["locked"], "%s: the cursor on %s and a click: it works with the mouse held" % (shape, label))
        if key == "play":
            cur.place(*centre("play"))
            cur.press()
        s = state(tab)
    p.pause(True)
    tab.ev("document.exitFullscreen(); 1")
    time.sleep(1.0)


def free_setting(p, args, check):
    print("[web replay lock] the replay page: Fullscreen mouse Free (ants.pointerlock = off)")
    tab = p.tab
    p.open(1440, 900, 1, storage={"ants.pointerlock": "off"})
    tab.ev(SPY)
    p.pause(True)
    s = state(tab)
    check(s["pref"] == "off", "the device remembers Free")
    click_element(tab, "fullscreen-btn-r")
    time.sleep(3.0)
    s = state(tab)
    check(s["fullscreen"] and not s["locked"] and s["events"] == [], "fullscreen without the lock (fullscreen %s, locked %s)" % (s["fullscreen"], s["locked"]))
    paused = s["game"][0]
    click_element(tab, "b-play")
    time.sleep(0.4)
    check(state(tab)["game"][0] != paused, "the bar is clickable by the free pointer, as ever")
    tab.ev("document.exitFullscreen(); 1")
    time.sleep(1.0)


if __name__ == "__main__":
    sys.exit(main())
