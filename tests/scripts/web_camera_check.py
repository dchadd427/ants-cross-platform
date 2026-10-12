#!/usr/bin/env python3
"""The camera of a replay in a REAL browser with the REAL game (opt-in; see tests/scripts/test_web_camera.sh and docs/REPLAYS.md "Watching a replay", the camera).

Needs the web page of this tree (`tools/web_without_docker.py -- tests/scripts/test_web_camera.sh` builds and serves it where there is no Docker), a Chromium-based browser, Python 3 and a recording of a
match with fights in it (ANTS_CAMERA_REPLAY or --replay: the match of four computer players that tests/test_app/test_replay_app.cpp makes writes one when ANTS_BOT_MATCH_OUT names a file).
No game server is needed: the check answers the page's `fetch('/replays/<file>')` itself and the page hands the file to the real game, as it does with the server's. The DevTools protocol is
spoken with the client of web_hidden_check.py (standard library only); the browser has its own profile and port.

Two profiles, each in a real page: a DESKTOP (1440 x 900, a mouse) and a PHONE (390 x 844, a touch screen, the DevTools protocol's multi-touch). What it checks:

  * the bar: the button sits between the clock and the speeds without crowding the timeline (a desktop); on a phone it has a row of its own under the speeds, as wide as the bar; in fullscreen
    only its icon is left;
  * Auto camera is the default (the game says so, the button says so, nothing is remembered yet) and the picture really moves to the places where things happen (the view's origin, read through
    the game's `ants_probe`);
  * zoom keeps Auto (the wheel on a desktop, two fingers that only pinch on a phone); the pointer at the edge of the screen (a desktop) and two fingers that pan (a phone) take the camera: the
    game says so, the button says Manual camera, the note comes with its words, the picture then stays where the viewer put it however much happens, the choice is not written down;
  * the button returns to Auto and glides back, writes `ants.cam` (and Manual too), a new visit starts as the device chose, in Manual with a picture that does not move;
  * nothing can be picked: a drag box over the whole picture, Ctrl+A, a click and a right click leave nothing picked (the game's probe 50).

Exit status 0: every check passed; 1: a check failed, or the browser or the page broke down during the check; 3: the check could not be made because the environment is not there (no browser,
no page at the address, no recording); 2 is the status of a bad command line.
"""
import argparse
import base64
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_aspect_check as aspect                                            # noqa: E402
from web_aspect_check import Browser, NotReachable, Tab, screen_of_canvas    # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402
from web_touch_check import Fingers, wait_for                                # noqa: E402

NAME = "ants-TREASURE-20261011-120000Z.antsrep"
CANVAS = (960, 540)
NOTE = "You have the camera. Press Auto camera to follow the action again."

# What runs in the page before its own scripts: the page's fetch of the recording is answered here (the file is the one of the check), every other fetch is the browser's
HOOK = r"""
(function () {
  var bytes = Uint8Array.from(atob(%s), function (c) { return c.charCodeAt(0); });
  var real = window.fetch;
  window.fetch = function (input, init) {
    var url = typeof input === 'string' ? input : input.url;
    if (url.indexOf('/replays/%s') !== -1) return Promise.resolve(new Response(bytes, { status: 200, headers: { 'content-type': 'application/octet-stream' } }));
    return real.apply(this, arguments);
  };
})();
"""

BAR = r"""
JSON.stringify((function () {
  var $ = function (id) { return document.getElementById(id); };
  var r = function (e) { var b = e.getBoundingClientRect(); return [b.x, b.y, b.width, b.height]; };
  var b = $('b-cam');
  return { mode: b.getAttribute('data-mode'), label: $('cam-lbl').textContent, aria: b.getAttribute('aria-pressed'), name: b.getAttribute('aria-label'), labelWidth: $('cam-lbl').getBoundingClientRect().width, cam: r(b), clock: r($('clock')), speed: r($('speed')),
           tl: r($('tl')), bar: r($('rbar')), over: $('rbar').classList.contains('over'), noteHidden: $('camnote').hidden, noteText: $('camnote').textContent, note: r($('camnote')),
           pref: (function () { try { return localStorage.getItem('ants.cam'); } catch (e) { return 'throws'; } })(), inner: [window.innerWidth, window.innerHeight],
           game: [Module._ants_replay_get(10), Module._ants_replay_get(11), Module._ants_replay_get(0), Module._ants_replay_get(1)],
           view: [Module._ants_probe(3), Module._ants_probe(4), Module._ants_probe(6)], picked: Module._ants_probe(50) };
})())
"""


class Replay:
    """A tab with the player of the check's recording, at a size, as a desktop or as a phone."""

    def __init__(self, tab, web, data, mobile):
        self.tab = tab
        self.url = (web if web.endswith("/") else web + "/") + "play.html?replay=" + NAME + "&aspect=16:9"
        self.hook = HOOK % (json.dumps(base64.b64encode(data).decode()), NAME)
        self.mobile = mobile
        self.hooked = False
        self.fingers = Fingers(tab)

    def open(self, width, height, dpr, storage=None):
        self.tab.emulate(width, height, dpr, mobile=self.mobile)
        if not self.hooked:
            self.tab.call("Page.addScriptToEvaluateOnNewDocument", {"source": self.hook})
            self.hooked = True
        if storage is not None:                                              # what the browser remembers before the page starts
            self.tab.open("about:blank", wait=False)
            self.tab.open(self.url, settle=0.5)
            self.tab.ev("localStorage.clear(); %s 1" % "".join("localStorage.setItem(%s, %s);" % (json.dumps(k), json.dumps(v)) for k, v in storage.items()))
        self.tab.open(self.url, settle=0.5)
        wait_for(lambda: self.tab.ev("Module._ants_replay_get(0)") == 2, 60, 0.25)           # (the replay plays)
        time.sleep(1.0)

    def bar(self):
        return json.loads(self.tab.ev(BAR))

    def speed(self, s):
        self.tab.ev("document.querySelector('#speed button[data-s=\"%s\"]').click(); 1" % s)

    def pause(self, on):
        self.tab.ev("Module._ants_replay_do(1, %d); 1" % (1 if on else 0))

    def geometry(self):
        return self.tab.geometry()

    def canvas_point(self, g, x, y):
        return screen_of_canvas(g, x, y, *CANVAS)


def norm(text):
    """The note's words as a person reads them: the wide spaces round the gold name (the page's minifier would take plain ones away) are spaces"""
    return text.replace("\u2005", " ").replace("\u00a0", " ")


def rect_inside(inner, outer, slack=1.5):
    return inner[0] >= outer[0] - slack and inner[1] >= outer[1] - slack and inner[0] + inner[2] <= outer[0] + outer[2] + slack and inner[1] + inner[3] <= outer[1] + outer[3] + slack


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the image, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--replay", default=os.environ.get("ANTS_CAMERA_REPLAY", ""), help="a recording of a match with fights (default: ANTS_CAMERA_REPLAY)")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--ready-timeout", type=float, default=180.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 180)")
    ap.add_argument("--profile", default="all", help="desktop, phone or all (default)")
    ap.add_argument("--follow-seconds", type=float, default=150.0, help="how long the Auto camera may take to visit three places at 8x (default 150)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout
    if args.profile not in ("all", "desktop", "phone", "widths"):
        print("unknown profile %r (desktop, phone or all)" % args.profile)
        return 2
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
        for name in ("desktop", "phone", "widths"):
            if args.profile not in ("all", name):
                continue
            print("[web camera] %s" % name)
            tab = Tab(browser)
            try:
                {"desktop": desktop, "phone": phone, "widths": widths}[name](Replay(tab, args.web, data, name == "phone"), check, args)
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
    print("[web camera] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


def follows(p, args, steps=3):
    """Plays at 8x until the Auto camera has been at `steps` different places (the view's origin, 128 px cells); returns how many, and whether the camera was ever taken."""
    p.speed("8")
    places = set()
    deadline = time.time() + args.follow_seconds
    taken = 0
    while time.time() < deadline and len(places) < steps:
        time.sleep(0.4)
        s = p.bar()
        taken = s["game"][1]
        if s["game"][2] != 2:
            break                                                           # (it ended: the results)
        places.add((s["view"][0] // 128, s["view"][1] // 128))
    return len(places), taken


def drag_everything(p, g):
    """A drag box over the whole picture with the left button, a click, a right click and Ctrl+A: what a match would answer with a pick"""
    left, top = p.canvas_point(g, 30, 40)
    right, bottom = p.canvas_point(g, 600, 500)
    mx, my = p.canvas_point(g, 300, 250)
    p.tab.mouse("mouseMoved", left, top, button="none")
    time.sleep(0.1)
    p.tab.mouse("mousePressed", left, top)
    for k in range(1, 6):
        p.tab.mouse("mouseMoved", left + (right - left) * k / 5, top + (bottom - top) * k / 5, button="left")
        time.sleep(0.05)
    p.tab.mouse("mouseReleased", right, bottom)
    time.sleep(0.3)
    first = p.bar()["picked"]
    p.tab.click(mx, my)
    time.sleep(0.2)
    p.tab.mouse("mousePressed", mx, my, button="right")
    p.tab.mouse("mouseReleased", mx, my, button="right")
    time.sleep(0.2)
    for kind in ("rawKeyDown", "keyUp"):
        p.tab.call("Input.dispatchKeyEvent", {"type": kind, "key": "a", "code": "KeyA", "windowsVirtualKeyCode": 65, "modifiers": 2})
    time.sleep(0.3)
    return first, p.bar()["picked"]


def desktop(p, check, args):
    p.open(1440, 900, 1)
    s = p.bar()
    check(s["game"][2] == 2 and s["game"][0] == 1, "the replay plays with the camera on Auto (the game says so)")
    check(s["mode"] == "auto" and s["label"] == "Auto camera" and s["aria"] == "true" and s["name"] == "Auto camera" and s["pref"] is None, "the button says Auto camera, and nothing is remembered yet")
    check(s["clock"][0] + s["clock"][2] <= s["cam"][0] + 1 and s["cam"][0] + s["cam"][2] <= s["speed"][0] + 1, "the button sits between the clock and the speeds (%s)" % [round(v) for v in (s["clock"][0] + s["clock"][2], s["cam"][0], s["cam"][0] + s["cam"][2], s["speed"][0])])
    check(s["labelWidth"] > 40 and s["tl"][2] > 200, "in a bar of 1280 px the word is there and the timeline has room (%d px)" % s["tl"][2])
    p.tab.save_shot(args.shots, "camera-desktop-auto")

    n, taken = follows(p, args)
    check(n >= 3 and taken == 0, "the Auto camera goes to the places where things happen: %d places in 8x play, never taken by itself" % n)
    p.pause(True)
    time.sleep(1.0)
    s = p.bar()
    check(s["mode"] == "auto" and s["game"][0] == 1, "still Auto")

    # zoom keeps Auto
    g = p.geometry()
    x, y = p.canvas_point(g, 300, 250)
    p.tab.mouse("mouseMoved", x, y, button="none")
    zoom0 = s["view"][2]
    p.tab.call("Input.dispatchMouseEvent", {"type": "mouseWheel", "x": x, "y": y, "deltaX": 0, "deltaY": -100})
    time.sleep(0.8)
    s = p.bar()
    check(s["view"][2] != zoom0 and s["game"][0] == 1 and s["game"][1] == 0 and s["mode"] == "auto", "the wheel zooms and keeps Auto (zoom %d -> %d)" % (zoom0, s["view"][2]))

    # the pointer at the edge of the picture takes the camera: any edge that can scroll
    taken = 0
    for ex, ey in ((480, 539), (959, 270), (480, 0), (0, 270)):
        ex_, ey_ = p.canvas_point(g, ex, ey)
        p.tab.mouse("mouseMoved", ex_ - (1 if ex == 959 else 0), ey_ - (1 if ey == 539 else 0), button="none")
        if wait_for(lambda: p.bar()["game"][1] >= 1, 2.5, 0.2):
            taken = 1
            break
    p.tab.mouse("mouseMoved", x, y, button="none")
    time.sleep(0.4)
    s = p.bar()
    held = (s["view"][0], s["view"][1])
    check(taken == 1 and s["game"][0] == 0 and s["mode"] == "manual" and s["label"] == "Manual camera", "the pointer at the edge takes the camera: the game says Manual, the button says Manual camera")
    check(not s["noteHidden"] and norm(s["noteText"]) == NOTE, "the note says \"%s\" (hidden: %s, text: %r)" % (NOTE, s["noteHidden"], s["noteText"]))
    check(s["pref"] is None, "taking the camera is not a choice: nothing is written down")
    check(s["note"][1] + s["note"][3] <= s["bar"][1] + 1, "the note is over the picture, above the bar")
    p.tab.save_shot(args.shots, "camera-desktop-manual-note")
    p.pause(False)
    p.speed("8")
    time.sleep(5.0)
    s = p.bar()
    check((s["view"][0], s["view"][1]) == held and s["game"][2] == 2, "the match plays on at 8x and the picture stays where the viewer put it")
    check(bool(wait_for(lambda: p.bar()["noteHidden"], 6.0, 0.25)), "the note has gone by itself (after seven seconds)")

    # the button: Auto again, remembered, gliding to the last action
    p.tab.ev("document.getElementById('b-cam').click(); 1")
    time.sleep(0.2)
    s = p.bar()
    check(s["game"][0] == 1 and s["mode"] == "auto" and s["label"] == "Auto camera" and s["pref"] == "auto", "the button returns to Auto and remembers it")
    moved = wait_for(lambda: (p.bar()["view"][0], p.bar()["view"][1]) != held, 4.0, 0.2)
    check(bool(moved), "the picture glides to the last action")
    p.tab.ev("document.getElementById('b-cam').click(); 1")
    time.sleep(0.3)
    s = p.bar()
    check(s["game"][0] == 0 and s["mode"] == "manual" and s["pref"] == "manual", "the button again is Manual camera, and remembers that")

    # a new visit starts as the device chose
    p.open(1440, 900, 1)
    s = p.bar()
    start = (s["view"][0], s["view"][1])
    check(s["mode"] == "manual" and s["label"] == "Manual camera" and s["game"][0] == 0 and s["pref"] == "manual", "a new visit starts in Manual camera, as the device chose")
    p.speed("8")
    time.sleep(6.0)
    s = p.bar()
    check((s["view"][0], s["view"][1]) == start and s["game"][2] == 2 and s["game"][3] > 40, "... and the picture does not move while the match plays (turn %d)" % s["game"][3])
    p.tab.ev("document.getElementById('b-cam').click(); 1")                                   # (Auto again, and remembered)
    time.sleep(0.3)

    # fullscreen: only the icon is left
    p.pause(True)
    p.tab.ev("document.getElementById('fullscreen-btn-r').click(); 1")
    time.sleep(2.5)
    s = p.bar()
    check(s["over"] and s["labelWidth"] == 0 and s["cam"][2] < 80 and s["cam"][2] > 20 and rect_inside(s["cam"], s["bar"]), "in fullscreen the bar floats over the picture and the button is an icon (%d px wide)" % s["cam"][2])
    p.tab.save_shot(args.shots, "camera-desktop-fullscreen")
    p.tab.ev("document.getElementById('b-fsx').click(); 1")
    time.sleep(1.5)

    # nothing can be picked
    p.pause(False)
    p.speed("1")
    time.sleep(1.0)
    g = p.geometry()
    first, second = drag_everything(p, g)
    check(first == 0 and second == 0, "nothing can be picked: a drag box over the picture, a click, a right click and Ctrl+A leave nothing picked (probe 50: %s, %s)" % (first, second))


def widths(p, check, args):
    """The bar at middle widths: a tablet's window (live too, with the Live box) and a short landscape phone in fullscreen, the Picture window shut and open: the timeline keeps a usable width"""
    MIN = 120
    cases = [(768, 1024, False, True), (768, 1024, True, True), (820, 1180, False, True), (820, 1180, True, True), (568, 320, True, False), (667, 375, True, False), (568, 320, True, True)]
    # (width, height, fullscreen, live)
    for w, h, fs, live in [(c[0], c[1], c[2], c[3]) for c in cases]:
        p.open(w, h, 1)
        p.pause(True)
        if live:
            p.tab.ev("var b = document.getElementById('rbar'); b.classList.add('islive'); document.getElementById('livenow').hidden = false; 1")
        if fs:
            p.tab.ev("document.getElementById('fullscreen-btn-r').click(); 1")
            time.sleep(2.0)
        for pop in (False, True):
            if pop:
                p.tab.ev("document.getElementById('picbtn').click(); 1")
                time.sleep(0.4)
            s = p.bar()
            name = "camera-%dx%d%s%s%s" % (w, h, "-fullscreen" if fs else "", "-live" if live else "", "-popover" if pop else "")
            p.tab.save_shot(args.shots, name)
            check(s["tl"][2] >= MIN and rect_inside(s["cam"], s["bar"]) and (s["cam"][0] >= s["clock"][0] + s["clock"][2] - 1 or s["cam"][1] >= s["speed"][1] + s["speed"][3] - 1), "%s: timeline %d px, button %d px wide (label %d px)" % (name, s["tl"][2], s["cam"][2], s["labelWidth"]))
        if fs:
            p.tab.ev("document.getElementById('b-fsx').click(); 1")
            time.sleep(1.0)


def phone(p, check, args):
    p.open(390, 844, 2, storage={})
    s = p.bar()
    check(s["game"][0] == 1 and s["mode"] == "auto" and s["label"] == "Auto camera", "the replay plays with the camera on Auto")
    check(s["cam"][1] >= s["speed"][1] + s["speed"][3] - 1 and s["cam"][2] >= 0.85 * s["bar"][2] and s["labelWidth"] > 40, "the button has a row of its own under the speeds, as wide as the bar, with its word (%d of %d px)" % (s["cam"][2], s["bar"][2]))
    check(s["tl"][2] > 200 and rect_inside(s["cam"], [0, 0, s["inner"][0], 10000]), "nothing is crowded or cut off")
    p.tab.save_shot(args.shots, "camera-phone-auto")
    p.tab.ev("window.scrollTo(0, 0); 1")
    n, taken = follows(p, args, steps=2)
    check(n >= 2 and taken == 0, "the Auto camera goes to the places where things happen: %d places" % n)
    p.pause(True)
    time.sleep(0.8)
    g = p.geometry()
    if g["box"][1] + g["box"][3] > g["inner"][1] or g["box"][1] < 0:
        p.tab.ev("window.scrollTo(0, 0); 1")
        g = p.geometry()
    s = p.bar()
    zoom0 = s["view"][2]
    ax, ay = p.canvas_point(g, 400, 250)
    f = p.fingers
    f.down(1, ax - 30, ay)
    f.down(2, ax + 30, ay)
    for k in range(1, 9):                                                    # a pinch that opens: the middle stays where it is
        f.move(a=(ax - 30 - 9 * k, ay), b=(ax + 30 + 9 * k, ay))
        time.sleep(0.05)
    f.up_all()
    time.sleep(0.6)
    s = p.bar()
    check(s["view"][2] != zoom0 and s["game"][0] == 1 and s["game"][1] == 0 and s["mode"] == "auto", "two fingers that pinch zoom and keep Auto (zoom %d -> %d)" % (zoom0, s["view"][2]))
    held = (s["view"][0], s["view"][1])
    f.down(1, ax - 30, ay)
    f.down(2, ax + 30, ay)
    for k in range(1, 9):                                                    # two fingers that pan the picture
        f.move(a=(ax - 30 + 9 * k, ay + 6 * k), b=(ax + 30 + 9 * k, ay + 6 * k))
        time.sleep(0.05)
    f.up_all()
    time.sleep(0.6)
    s = p.bar()
    check(s["game"][1] >= 1 and s["game"][0] == 0 and s["mode"] == "manual" and s["label"] == "Manual camera", "two fingers that pan take the camera: Manual camera")
    check((s["view"][0], s["view"][1]) != held and not s["noteHidden"] and norm(s["noteText"]) == NOTE, "the picture moved with the fingers (%s -> %s), and the note says what happened (hidden: %s)" % (held, s["view"][:2], s["noteHidden"]))
    check(s["note"][0] >= -1 and s["note"][0] + s["note"][2] <= s["inner"][0] + 1, "the note fits the width of the phone (%d px of %d)" % (s["note"][2], s["inner"][0]))
    p.tab.save_shot(args.shots, "camera-phone-manual-note")
    p.pause(False)
    p.speed("8")
    held = (s["view"][0], s["view"][1])
    time.sleep(4.0)
    s = p.bar()
    check((s["view"][0], s["view"][1]) == held, "the picture stays where it was put while the match plays on")
    bx, by = s["cam"][0] + s["cam"][2] / 2, s["cam"][1] + s["cam"][3] / 2
    p.tab.tap(bx, by)
    time.sleep(0.4)
    s = p.bar()
    check(s["game"][0] == 1 and s["mode"] == "auto" and s["pref"] == "auto", "a tap on the button returns to Auto and remembers it")
    # fullscreen of a phone: the icon in the second row
    p.pause(True)
    p.tab.ev("document.getElementById('fullscreen-btn-r').click(); 1")
    time.sleep(2.5)
    s = p.bar()
    if s["over"]:
        check(s["labelWidth"] == 0 and rect_inside(s["cam"], s["bar"]) and s["cam"][2] < 80, "in fullscreen the button is an icon inside the bar (%d px)" % s["cam"][2])
        p.tab.save_shot(args.shots, "camera-phone-fullscreen")
        p.tab.ev("document.getElementById('b-fsx').click(); 1")
    else:
        print("  (this browser did not enter fullscreen: the phone's fullscreen bar was not looked at)")


if __name__ == "__main__":
    sys.exit(main())
