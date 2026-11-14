#!/usr/bin/env python3
"""The settings menu (Esc, or a Settings button) in a REAL browser with the REAL game (opt-in; see tests/scripts/test_web_game_menu.sh, docs/GAME_MENU.md).

The quick tier runs the game's side (tests/test_app/test_game_menu.cpp: the table, Esc's rule, the settings, the input that the game does not take) and the page's script on a fake of the DOM
(tests/scripts/web_game_menu_check.js); nothing but a real page shows the chain: the real Esc key, the real game's answers, a real fullscreen and pointer lock, real touches, real sizes.
Needs the web page of this tree (`tools/web_without_docker.py -- tests/scripts/test_web_game_menu.sh`), a Chromium-based browser and Python 3; a recording (--replay, or ANTS_CAMERA_REPLAY: as
for tests/scripts/test_web_camera.sh) makes the parts that watch a replay run. No game server is needed (the games are on this computer, against a computer player or alone). What it checks:

  * outside a match (profile `outside`; every step runs against the real game, and no step is a stand-in): the gear on the quick help, the setup screen, the results and under a dialog opens a small form with the
    page's own rows; nothing typed into it reaches the game (the quick help stays, no match starts, no chat line opens, the results stay, a dialog still waits); a key held past the close (Esc, Enter) is not a new
    press for the game; Space and Enter press the focused buttons; a focused gear opens it by Enter without the game getting the key; a match that begins under it closes it; the full menu of a match is as before
    (Esc, Enter, Space, a held Esc, Forfeit by a held Enter). The steps are the acceptance procedure of docs/GAME_MENU.md;
  * the computer, 1280 x 800 (play.html?map=tiny&bots=easy,none,none): the Settings button is in the header (and the narrow one and the fullscreen one are not shown); Esc with nothing selected (a first press) opens the
    panel inside the stage, over the whole window, with the focus on Resume; what is typed meanwhile is not the game's chat line (and is, once it is closed); Ctrl+L does not turn the health marks;
    Tab stays inside it; a click on the dimmed area does nothing; a held Esc (a repeat) does not close it and the next Esc does, the focus is on the canvas; Esc with a hill selected only deselects and the
    next one opens it; Esc closes the quick help and asks for nothing; a slider moved with the arrow key is the game's value and the settings file's (master=); Mute and the ant health marks are the game's
    and remembered (mute=, ant_health=, ant_health_style=); the picture's shape is the selector's (the stage, the game's canvas, the address); the Fullscreen row makes the stage fullscreen with the panel inside it, the pointer lock
    is let go while it is up and asked for again by Resume; in fullscreen no Settings button is on the screen and Esc opens the panel; a dialog that comes up (the quick help, from the panel's own act) closes it;
    Leave game asks (a forfeit with one opponent left), Esc and Stay stay, Forfeit ends the match (the results); in a game for one it asks "Leave the game?" and leaves for the front page;
  * a phone, 390 x 844 with touch: the button is under the picture (44 px), a tap opens the panel inside a gutter of 16 px, every button and slider is at least 44 px high, the text is at least 16 px
    (hints 13), the page does not scroll sideways, the touch model sees nothing of what is tapped on the panel, the cross closes it, the Controls list is the touch list, the question of Leave game fits;
    a phone on its side (844 x 390): the panel is the screen's height less the gutter and every button is on the screen; in fullscreen on a touch screen no Settings button is on the screen (there is no Esc either: fullscreen is left first), and it is back when it is left;
  * scrolling: in every section whose list is taller than the pane, at 1280 x 720, 1024 x 600 and 800 x 500 with a wheel, in fullscreen with a wheel (the stage cancels the wheel over its bars: not over the panel),
    and at 390 x 844 and 844 x 390 with a finger and a wheel, the pane can be moved to its end and the last row is then whole inside it (the Video section is taller than the pane on every computer);
  * a recording (play.html?replay=...): the button is in the bar, Esc opens the panel with no Game section, the replay goes on meanwhile, "Back to the matches" asks nothing and leaves for the list; in the
    frame of a match page (embed=1) there is no panel, no button and no script.

Exit status 0: every check passed; 1: a check failed, or the browser or the page broke down during the check; 3: the check could not be made because the environment is not there; 2: a bad command line.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_aspect_check as aspect                                            # noqa: E402
from web_aspect_check import Browser, NotReachable, SHAPE_ENTRIES, Tab                # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

LOCAL = "play?map=tiny&bots=easy,none,none"                             # a game for you and one computer player: Leave game is a forfeit
ALONE = "play?map=tiny"                                                 # a game for one: Leave game ends the match

STATE = r"""
JSON.stringify((function () {
  var $ = function (id) { return document.getElementById(id); };
  var r = function (e) { if (!e) return null; var b = e.getBoundingClientRect(); return [b.x, b.y, b.width, b.height]; };
  var menu = $('game-menu'), panel = menu && menu.querySelector('.gm-panel'), ae = document.activeElement;
  var fs = document.fullscreenElement || document.webkitFullscreenElement, lock = document.pointerLockElement || document.webkitPointerLockElement;
  return { open: !!menu, parent: menu ? menu.parentNode.id : null, menu: r(menu), panel: r(panel), active: ae ? (ae.id || ae.className || ae.tagName) : null, activeIn: !!(menu && ae && menu.contains(ae)),
           fullscreen: fs ? fs.id : null, locked: lock ? lock.id : null, aspect: $('game-stage').getAttribute('data-aspect'), where: Module._ants_menu_get(100), needs: Module._ants_menu_get(101), leave: Module._ants_menu_get(102),
           chat: Module._ants_chat_probe(5), modal: Module._ants_probe(5), running: Module._ants_match_running(), taps: Module._ants_probe(30), shape: Module._ants_probe(42),
           master: Module._ants_menu_get(1), mute: Module._ants_menu_get(4), hp: Module._ants_menu_get(8), style: Module._ants_menu_get(15), music: Module._ants_menu_get(2),
           settings: (function () { try { return localStorage.getItem('ants.settings') || ''; } catch (e) { return ''; } })(), path: location.pathname, scrollW: document.documentElement.scrollWidth, innerW: innerWidth, innerH: innerHeight };
})())"""

BUTTONS = r"""
JSON.stringify([].map.call(document.querySelectorAll('.gm-btn'), function (b) { var r = b.getBoundingClientRect(), s = getComputedStyle(b); return { id: b.id, hidden: b.hidden, shown: s.display !== 'none' && r.width > 0, disabled: b.disabled, w: r.width, h: r.height, x: r.x, y: r.y }; }))"""

VISIBLE = r"""
(function () {
  var seen = 0;
  [].forEach.call(document.querySelectorAll('.gm-btn'), function (b) {
    var r = b.getBoundingClientRect();
    if (r.width < 1 || r.height < 1 || b.hidden || getComputedStyle(b).display === 'none') return;
    var top = document.elementFromPoint(r.x + r.width / 2, r.y + r.height / 2);
    if (top && (top === b || b.contains(top))) seen++;
  });
  return seen;
})()"""

PANEL_SIZES = r"""
JSON.stringify((function () {
  var menu = document.getElementById('game-menu'); if (!menu) return null;
  var small = [], text = [];
  [].forEach.call(menu.querySelectorAll('button, input'), function (b) { var r = b.getBoundingClientRect(); if (r.width > 0 && r.height < 43.5) small.push([b.className || b.tagName, r.height]); });
  [].forEach.call(menu.querySelectorAll('.gm-name, .gm-hint, .gm-note, .gm-ask-b'), function (b) { var r = b.getBoundingClientRect(); if (r.width > 0) text.push([b.className, parseFloat(getComputedStyle(b).fontSize)]); });
  return { small: small, text: text };
})())"""


PANE = r"""
JSON.stringify((function () {
  var pane = document.querySelector('.gm-pane'), panel = document.querySelector('.gm-panel');
  if (!pane || !panel) return null;
  var box = pane.getBoundingClientRect(), p = panel.getBoundingClientRect(), low = box.top;
  [].forEach.call(pane.querySelectorAll('*'), function (e) { var r = e.getBoundingClientRect(); if (r.height > 0 && r.bottom > low) low = r.bottom; });
  var rb = (document.querySelector('.gm-resume') || pane).getBoundingClientRect();
  return { x: box.x, y: box.y, w: box.width, h: box.height, top: pane.scrollTop, resume: [rb.x, rb.y, rb.right, rb.bottom], over: pane.scrollHeight - pane.clientHeight, low: low, bottom: box.bottom - 2,
           panel: [p.x, p.y, p.right, p.bottom], inner: [innerWidth, innerHeight] };
})())"""


def any_visible(tab):
    """a Settings button that is really on the screen (not covered: in fullscreen the page around the stage lies under it)"""
    return tab.ev(VISIBLE) > 0


def wait_for(fn, seconds, step=0.2):
    end = time.time() + seconds
    while time.time() < end:
        try:
            value = fn()
        except (RuntimeError, TimeoutError):
            value = None
        if value:
            return value
        time.sleep(step)
    return None


class Page:
    """A tab with the game page: the state, the real keys and the real pointer."""

    def __init__(self, tab, web):
        self.tab = tab
        self.web = web if web.endswith("/") else web + "/"

    def state(self):
        return json.loads(self.tab.ev(STATE))

    def buttons(self):
        return {b["id"]: b for b in json.loads(self.tab.ev(BUTTONS))}

    def key(self, key, code, vk, text=None, modifiers=0, repeat=False):
        for kind in ("keyDown", "keyUp"):
            params = {"type": kind, "key": key, "code": code, "windowsVirtualKeyCode": vk, "modifiers": modifiers}
            if text is not None and kind == "keyDown":
                params["text"] = text
            if repeat:
                params["autoRepeat"] = True
            self.tab.call("Input.dispatchKeyEvent", params)
        time.sleep(0.35)

    def ctrl(self, key, code, vk):
        """Ctrl and a key, as a keyboard sends them: the game reads the Ctrl from the key that went down before (the flag of the event alone is not what its window system keeps)"""
        for kind, k, c, v, m in (("keyDown", "Control", "ControlLeft", 17, 2), ("keyDown", key, code, vk, 2), ("keyUp", key, code, vk, 2), ("keyUp", "Control", "ControlLeft", 17, 0)):
            self.tab.call("Input.dispatchKeyEvent", {"type": kind, "key": k, "code": c, "windowsVirtualKeyCode": v, "modifiers": m})
        time.sleep(0.35)

    def hold_esc(self, repeats=4):
        """Esc held down: a first press, the browser's repeats (autoRepeat, no lift in between), the lift"""
        params = {"key": "Escape", "code": "Escape", "windowsVirtualKeyCode": 27}
        self.tab.call("Input.dispatchKeyEvent", dict(params, type="keyDown"))
        for _ in range(repeats):
            time.sleep(0.08)
            self.tab.call("Input.dispatchKeyEvent", dict(params, type="keyDown", autoRepeat=True))
        self.tab.call("Input.dispatchKeyEvent", dict(params, type="keyUp"))
        time.sleep(0.4)

    def hold(self, key, code, vk, repeats=4, text=None):
        """a key held down: a first press, the keyboard's repeats (autoRepeat, no lift in between), then the lift"""
        params = {"key": key, "code": code, "windowsVirtualKeyCode": vk}
        down = dict(params, type="keyDown")
        if text is not None:
            down["text"] = text
        self.tab.call("Input.dispatchKeyEvent", down)
        for _ in range(repeats):
            time.sleep(0.08)
            self.tab.call("Input.dispatchKeyEvent", dict(down, autoRepeat=True))
        self.tab.call("Input.dispatchKeyEvent", dict(params, type="keyUp"))
        time.sleep(0.5)

    def esc(self, repeat=False):
        self.key("Escape", "Escape", 27, repeat=repeat)

    def type(self, text):
        for ch in text:
            self.key(ch, "Key" + ch.upper(), ord(ch.upper()), text=ch)

    def click_element(self, selector):
        x, y = json.loads(self.tab.ev("JSON.stringify((function(){var b=document.querySelector(%s).getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())" % json.dumps(selector)))
        self.tab.click(x, y)
        time.sleep(0.4)

    def tap_element(self, selector):
        x, y = json.loads(self.tab.ev("JSON.stringify((function(){var b=document.querySelector(%s).getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())" % json.dumps(selector)))
        self.tab.tap(x, y)
        time.sleep(0.6)

    def open_game(self, address, width, height, mobile=False, fresh=True):
        self.tab.emulate(width, height, 1, mobile=mobile)
        if fresh:
            self.tab.open("about:blank", wait=False)
            self.tab.open(self.web + address, settle=0.5)
            self.tab.ev("localStorage.clear(); 1")
        self.tab.open(self.web + address, settle=2.0)

    def start_match(self):
        for _ in range(2):                                                   # the quick help's Enter, the setup screen's Enter (START)
            self.key("Enter", "Enter", 13, text="\r")
            time.sleep(1.2)
        return bool(wait_for(lambda: self.tab.ev("Module._ants_match_running()") == 1, 25, 0.5)) and bool(wait_for(lambda: self.tab.ev("!!document.querySelector('#settings-btn')") and self.tab.ev("Module._ants_menu_get(101)") == 0, 10, 0.3))

    def click_text(self, selector, text):
        """a real click on the first element of `selector` with this text"""
        x, y = json.loads(self.tab.ev("JSON.stringify((function(){var l=[].filter.call(document.querySelectorAll(%s),function(b){return b.textContent===%s;});if(!l.length)return null;var b=l[0].getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())" % (json.dumps(selector), json.dumps(text))))
        self.tab.click(x, y)
        time.sleep(0.4)

    def wheel(self, x, y, delta):
        self.tab.call("Input.dispatchMouseEvent", {"type": "mouseMoved", "x": x, "y": y, "button": "none"})
        self.tab.call("Input.dispatchMouseEvent", {"type": "mouseWheel", "x": x, "y": y, "deltaX": 0, "deltaY": delta})
        time.sleep(0.2)

    def swipe_up(self, x, y_from, y_to, steps=12):
        """a finger that goes up the screen: the list moves up with it"""
        self.tab.call("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": x, "y": y_from}]})
        for i in range(1, steps + 1):
            self.tab.call("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": [{"x": x, "y": y_from + (y_to - y_from) * i / steps}]})
            time.sleep(0.016)
        self.tab.call("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
        time.sleep(0.7)                                                      # (the fling is over)

    def pane(self):
        return json.loads(self.tab.ev(PANE))

    def tab_to(self, section):
        self.tab.ev("document.getElementById('gm-tab-%s').click(); 1" % section)
        time.sleep(0.3)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the image, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--replay", default=os.environ.get("ANTS_CAMERA_REPLAY", ""), help="a recording of a match (default: ANTS_CAMERA_REPLAY); without one the parts that watch a replay are left out")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--ready-timeout", type=float, default=180.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 180)")
    ap.add_argument("--profile", default="all", help="desktop, phone, scroll, replay, outside or all (default)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout
    if args.profile not in ("all", "desktop", "phone", "scroll", "replay", "outside"):
        print("--profile is desktop, phone, scroll, replay, outside or all")
        return 2
    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what), flush=True)
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
            page = Page(tab, args.web)
            if args.profile in ("all", "desktop"):
                computer(page, args, check)
            if args.profile in ("all", "phone"):
                phone(page, args, check)
            if args.profile in ("all", "scroll"):
                scrolling(page, args, check)
            if args.profile in ("all", "outside"):
                outside(page, args, check)
            if args.profile in ("all", "replay"):
                if args.replay and os.path.isfile(args.replay):
                    recording(page, args, check)
                else:
                    print("  note: no recording (--replay or ANTS_CAMERA_REPLAY): the parts that watch a replay are left out")
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
    print("[web game menu] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


def computer(p, args, check):
    print("[web game menu] a computer, 1280 x 800: a game against one computer player")
    tab = p.tab
    p.open_game(LOCAL, 1280, 800)
    check(p.start_match(), "a match is running (the quick help's Enter and the setup screen's Enter)")
    tab.ev("window.__lockSeen = false; window.addEventListener('pointerlockchange', function () { if (document.pointerLockElement) window.__lockSeen = true; }, true); 1")      # (the check's own witness that the browser grants a lock, however quickly the panel lets go of it: it is put in before the panel's own listener, which is made when it opens)
    wait_for(lambda: p.buttons().get("settings-btn", {}).get("shown") and not p.buttons()["settings-btn"]["disabled"], 10)       # (the page polls the game every quarter second: shown and on are two moments)
    b = p.buttons()
    check(b.get("settings-btn", {}).get("shown") and not b["settings-btn"]["disabled"] and "settings-btn-fs" not in b and "settings-btn-narrow" not in b,
          "the Settings button is in the header and on, and there is none over the picture or under it (%s)" % {k: v["shown"] for k, v in b.items()})
    header = tab.ev("document.getElementById('settings-btn').previousElementSibling.id")
    check(header == "menu-btn", "the header button stands right after Menu (%s)" % header)
    tab.ev("document.getElementById('canvas').focus(); 1")
    p.type("abc")
    check(p.state()["chat"] == 3, "(the control: three letters typed with the panel closed are in the game's chat line: %s)" % p.state()["chat"])
    tab.ev("Module._ants_chat_do(0, 0); 1")

    # ---- Esc opens it
    p.esc()
    s = p.state()
    check(s["open"] and s["parent"] == "game-stage", "Esc with nothing selected opens the panel, a child of the stage (%s)" % s["parent"])
    check(s["menu"] and abs(s["menu"][0]) < 1 and abs(s["menu"][1]) < 1 and s["menu"][2] >= s["innerW"] - 20 and s["menu"][3] >= s["innerH"] - 1, "the dimmed area covers the window (%s of %s x %s)" % ([round(v) for v in s["menu"] or []], s["innerW"], s["innerH"]))
    check(s["panel"] and 700 <= s["panel"][2] <= 781 and s["panel"][0] > 100, "the panel is 780 px wide, in the middle (%s)" % [round(v) for v in s["panel"] or []])
    check(tab.ev("document.activeElement.textContent") == "Resume", "the focus is on Resume")
    tab.save_shot(args.shots, "menu-1-sound")
    p.type("xyz")
    p.ctrl("l", "KeyL", 76)
    s = p.state()
    check(s["chat"] == 0 and s["hp"] == 0, "what is typed is not the game's: no chat line, Ctrl+L did not turn the health marks off (chat %s, ant health %s)" % (s["chat"], s["hp"]))
    check(s["open"], "(and the panel is still up)")
    for _ in range(14):
        p.key("Tab", "Tab", 9)
    check(p.state()["activeIn"], "Tab stays inside the panel (14 presses: %s)" % p.state()["active"])
    tab.click(6, 400)
    time.sleep(0.4)
    check(p.state()["open"], "a click on the dimmed area does nothing")
    p.esc(repeat=True)
    check(p.state()["open"], "a held Esc (a repeat) does not close it")
    p.esc()
    s = p.state()
    check(not s["open"] and s["active"] == "canvas", "the next Esc closes it, the focus is on the canvas (%s)" % s["active"])
    p.type("abc")
    check(p.state()["chat"] == 3, "the game takes the keys again (%s letters in the chat line)" % p.state()["chat"])
    tab.ev("Module._ants_chat_do(0, 0); 1")

    # ---- Esc cancels first
    p.ctrl("h", "KeyH", 72)                                      # (Ctrl+H: the home hill is selected)
    p.esc()
    check(not p.state()["open"], "Esc with a hill selected only deselects")
    p.esc()
    check(p.state()["open"], "...and the next one opens the panel")
    p.esc()
    p.key("F1", "F1", 112)
    check(p.state()["modal"] == 1, "(F1 opens the original's quick help)")
    p.esc()
    s = p.state()
    check(s["modal"] == 0 and not s["open"], "Esc closes the quick help and does not open the panel")
    p.key("F1", "F1", 112)
    p.hold_esc()
    s = p.state()
    check(s["modal"] == 0 and not s["open"], "holding Esc: the first press closes the quick help and its repeats do not open the panel")
    p.hold_esc()
    check(p.state()["open"], "holding Esc with nothing to cancel opens the panel once, and the repeats leave it be")
    p.esc()
    check(not p.state()["open"], "(Esc closes it)")
    p.esc()
    check(p.state()["open"], "...and the next Esc opens it again")

    # ---- a pointer lock that comes while the panel is up (the page's own handler of the event stops it before the panel's: the panel lets it go itself)
    tab.ev("window.__lockSeen = false; 1")
    engaged = None
    for _ in range(10):                                                              # (the browser refuses a lock that is asked for just after one was let go: it is asked for again, with a gesture, until it is granted)
        tab.call("Runtime.evaluate", {"expression": "document.getElementById('canvas').requestPointerLock(); 1", "userGesture": True, "returnByValue": True})
        engaged = wait_for(lambda: tab.ev("window.__lockSeen === true"), 0.7, 0.05)    # (granted a moment after the call: a state that is waited for, not a time that is slept)
        if engaged:
            break
    released = wait_for(lambda: p.state()["locked"] is None, 5, 0.05)               # (...and let go of by the panel within its poll)
    s = p.state()
    check(bool(engaged) and s["open"] and released and s["locked"] is None, "a lock that the page is given while the panel is up is let go (granted %s, locked %s)" % (bool(engaged), s["locked"]))
    p.click_element(".gm-resume")
    check(bool(wait_for(lambda: not p.state()["open"], 5, 0.05)), "...so the mouse still reaches the panel (a click on Resume closes it)")
    p.esc()
    check(bool(wait_for(lambda: p.state()["open"], 5, 0.05)), "(the panel again)")

    # ---- the values
    p.tab_to("sound")
    tab.ev("document.querySelector('.gm-panel input').focus(); 1")
    p.key("ArrowLeft", "ArrowLeft", 37)
    s = p.state()
    check(s["master"] == 98 and "master=98\n" in s["settings"], "the arrow key on Master volume is the game's value and the settings file's (master %s)" % s["master"])
    p.click_text(".gm-opt", "On")
    s = p.state()
    check(s["mute"] == 1 and "mute=on\n" in s["settings"] and s["master"] == 98, "Mute is the game's, remembered, and Master stays (mute %s, master %s)" % (s["mute"], s["master"]))
    p.click_text(".gm-opt", "Off")
    check(p.state()["mute"] == 0, "and Off unmutes")
    p.tab_to("video")
    tab.save_shot(args.shots, "menu-2-video")
    groups = json.loads(tab.ev("JSON.stringify([].map.call(document.querySelectorAll('.gm-seg'), function (g) { return [].map.call(g.children, function (b) { return b.textContent; }); }))"))
    check(groups == [["Locked", "Free"], ["Hurt ants", "All ants", "Off"], ["Bar", "Number"], ["On", "Off"]], "the Video section has the mouse, the two rows of the ant health and the damage and heal numbers as button groups (%s)" % groups)
    dd = ".gm-panel .pick-select"
    list_open = lambda: tab.ev("document.querySelector('.gm-panel [role=listbox]').hidden") is False
    shown_in_menu = lambda: tab.ev("(document.querySelector('.gm-panel [role=option][aria-selected=true]') || document.body).getAttribute('data-shape')")
    entries = json.loads(tab.ev("JSON.stringify([].map.call(document.querySelectorAll('.gm-panel [role=option]'), function (o) { return [o.getAttribute('data-shape'), o.textContent]; }))"))
    check(entries[1:] == SHAPE_ENTRIES and entries[0][0] == "auto" and entries[0][1].startswith("Automatic (now "), "...and the picture's shape is a dropdown (the page's own button and list: no native select in the panel) with one entry for each shape of the table: the shape and its canvas (%s)" % entries)
    check(tab.ev("document.querySelector('.gm-panel select') === null") is True and tab.ev("document.querySelector('%s').tagName" % dd) == "BUTTON", "...(a button that opens a list inside the panel)")
    start = aspect.first_visit_of(1280, 800, 1280, 800)                              # (the shape that a first visit has on this screen: the dropdown shows it)
    at = aspect.SHAPE_ORDER.index(start)
    check(shown_in_menu() == "auto" and entries[0][1] == "Automatic (now %s)" % start and (tab.ev("document.querySelector('.gm-fits') ? document.querySelector('.gm-fits').textContent : ''") or "").endswith(" fills your screen"),
          "...Automatic is chosen and says %s (the shape on a 1280 x 800 screen), and a quiet line under it names the shape that fills the screen" % start)
    tab.ev("antsPickShape('%s'); 1" % start)                                         # (the keys below start from a hand pick of the same shape: the marked row is its own)
    tab.ev("document.querySelector('%s').focus(); 1" % dd)
    shape_of = lambda k: aspect.SHAPE_ORDER[min(max(k, 0), len(aspect.SHAPE_ORDER) - 1)]
    p.key("ArrowDown", "ArrowDown", 40)
    s = p.state()
    check(list_open() and s["open"] and s["aspect"] == start, "ArrowDown on the closed dropdown opens its list and changes nothing (the picture is still %s, the menu is open)" % s["aspect"])
    p.key("ArrowDown", "ArrowDown", 40)
    s = p.state()
    check(s["aspect"] == start and tab.ev("document.querySelector('%s').getAttribute('aria-activedescendant')" % dd) == "gm-picture-o%d" % (at + 1), "ArrowDown in the list only moves the mark (aria-activedescendant names the row; the picture is still %s)" % s["aspect"])
    p.key("Enter", "Enter", 13)
    s = p.state()
    want = shape_of(at + 1)
    check(not list_open() and s["open"] and s["aspect"] == want and s["shape"] == aspect.SHAPE_ORDER.index(want) and tab.ev("Module._ants_probe(40)") == aspect.SHAPE_CANVAS[want][0], "Enter chooses the marked row: it changes the picture of the running game at once and the menu stays open (%s, shape %s)" % (s["aspect"], s["shape"]))
    check(tab.ev("document.activeElement === document.querySelector('%s')" % dd) and tab.ev("Module._ants_match_running()") == 1, "...the focus stays on the dropdown, the match goes on and the game did not take the arrow as a scroll")
    for _ in range(2):
        p.key("Enter", "Enter", 13)
        p.key("ArrowDown", "ArrowDown", 40)
        p.key("Enter", "Enter", 13)
    s = p.state()
    want = shape_of(at + 3)
    check(s["aspect"] == want and s["shape"] == aspect.SHAPE_ORDER.index(want) and ("aspect=" + want) in tab.ev("location.search"), "the shape is the page's own: the stage, the game's canvas and the address (%s, shape %s)" % (s["aspect"], s["shape"]))
    check(tab.ev("(document.querySelector('#gm-picture-list [aria-selected=true]') || document.body).getAttribute('data-shape')") == want and tab.ev("localStorage.getItem('ants.aspect.v3')") == aspect.pick(want), "the menu's dropdown follows, and the choice is remembered")
    p.key("Enter", "Enter", 13)
    p.key("End", "End", 35)
    p.key("Enter", "Enter", 13)
    check(p.state()["aspect"] == "32:9" and p.state()["shape"] == 6 and tab.ev("Module._ants_probe(40)") == 1920, "End, Enter: the last entry is 32:9 (the game takes its number: %s)" % p.state()["shape"])
    p.key("Enter", "Enter", 13)
    p.key("ArrowDown", "ArrowDown", 40)
    p.key("Enter", "Enter", 13)
    check(p.state()["aspect"] == "32:9" and not list_open(), "ArrowDown on the last entry stays there")
    # Esc with the list open closes the list ALONE: this is the 'Esc with the list open' that a native select could not be shown to do
    p.key("Enter", "Enter", 13)
    check(list_open(), "(the list again)")
    p.key("Escape", "Escape", 27)
    check(not list_open() and p.state()["open"] and p.state()["aspect"] == "32:9" and tab.ev("document.activeElement === document.querySelector('%s')" % dd), "Esc with the dropdown's list open closes the list alone: the menu stays open, the focus stays on the dropdown, nothing is chosen")
    # the mouse: a click on the dropdown opens the list, a click on a row chooses it (the real presses of the browser at the middle of each)
    spot = lambda sel: json.loads(tab.ev("JSON.stringify((function () { var r = document.querySelector('%s').getBoundingClientRect(); return [r.x + r.width / 2, r.y + r.height / 2]; })())" % sel))
    tab.click(*spot(dd))
    time.sleep(0.2)
    check(list_open(), "a click on the dropdown opens its list")
    tab.save_shot(args.shots, "menu-2-video-list")
    inside = tab.ev("(function () { var l = document.querySelector('.gm-panel [role=listbox]').getBoundingClientRect(); return l.left >= 0 && l.right <= window.innerWidth && l.top >= 0 && l.bottom <= window.innerHeight; })()")
    check(inside is True, "...inside the window")
    tab.click(*spot(".gm-panel #gm-picture-o2"))
    time.sleep(0.3)
    check(not list_open() and p.state()["open"] and p.state()["aspect"] == "16:10" and tab.ev("Module._ants_probe(41)") == 600, "a click on 16:10 in the list chooses it: the picture is 16:10 (%s), the menu stays open" % p.state()["aspect"])
    tab.click(*spot(dd))
    time.sleep(0.2)
    tab.click(*spot(".gm-panel .gm-name"))
    time.sleep(0.2)
    check(not list_open() and p.state()["open"] and p.state()["aspect"] == "16:10", "a click elsewhere in the panel closes the list and chooses nothing")
    tab.ev("document.querySelector('%s').focus(); 1" % dd)
    p.key("Tab", "Tab", 9)
    check(p.state()["activeIn"] and tab.ev("document.activeElement !== document.querySelector('%s')" % dd), "Tab leaves the dropdown inside the panel (the page behind stays inert)")
    for _ in range(40):                                                              # (Tab goes round: the focus returns to the dropdown and the panel never loses it)
        if tab.ev("document.activeElement === document.querySelector('%s')" % dd):
            break
        p.key("Tab", "Tab", 9)
    check(tab.ev("document.activeElement === document.querySelector('%s')" % dd) and p.state()["activeIn"], "Tab wraps round the panel and reaches the dropdown again")
    p.key("Enter", "Enter", 13)
    p.key("Home", "Home", 36)
    p.key("Enter", "Enter", 13)
    check(p.state()["aspect"] == "4:3" and p.state()["open"], "Home, Enter goes to Classic 4:3 (the menu stays open)")
    p.key("Enter", "Enter", 13)
    for _ in range(2):
        p.key("ArrowDown", "ArrowDown", 40)
    p.key("Enter", "Enter", 13)
    check(p.state()["aspect"] == "16:10" and tab.ev("Module._ants_probe(41)") == 600, "ArrowDown twice from 4:3 is 16:10, 960 x 600 (%s)" % p.state()["aspect"])
    p.esc()
    check(not p.state()["open"], "Esc on the closed dropdown closes the menu")
    p.esc()
    check(bool(wait_for(lambda: p.state()["open"], 5, 0.05)), "(the panel again)")
    p.tab_to("video")
    p.click_text(".gm-seg .gm-opt", "Off")
    s = p.state()
    check(s["hp"] == 2 and "ant_health=off\n" in s["settings"], "the ant health marks go off and are remembered (ant health %s)" % s["hp"])
    p.click_text(".gm-seg .gm-opt", "Number")
    s = p.state()
    check(s["style"] == 1 and "ant_health_style=number\n" in s["settings"], "...the look is a number, remembered (style %s)" % s["style"])
    p.click_text(".gm-seg .gm-opt", "Hurt ants")
    check(p.state()["hp"] == 0, "...and the marks are on hurt ants again")

    # ---- fullscreen
    p.click_text(".gm-act", "Enter fullscreen")
    wait_for(lambda: p.state()["fullscreen"], 5, 0.2)
    time.sleep(1.2)
    s = p.state()
    check(s["fullscreen"] == "game-stage" and s["open"] and s["parent"] == "game-stage", "the Fullscreen row makes the stage fullscreen and the panel is inside it (%s)" % s["fullscreen"])
    check(s["menu"] and s["menu"][2] >= s["innerW"] - 1 and s["menu"][3] >= s["innerH"] - 1, "the dimmed area covers the whole screen (%s of %s x %s)" % ([round(v) for v in s["menu"] or []], s["innerW"], s["innerH"]))
    check(s["locked"] is None, "the pointer lock is not held while the panel is up (%s)" % s["locked"])
    tab.save_shot(args.shots, "menu-3-fullscreen")
    p.click_element(".gm-resume")
    time.sleep(1.0)
    s = p.state()
    check(not s["open"] and s["locked"] == "canvas", "Resume closes it and asks for the pointer lock again (open %s, locked %s)" % (s["open"], s["locked"]))
    b = p.buttons()
    check("settings-btn-fs" not in b and not any_visible(tab), "in fullscreen a computer has nothing of the menu on the screen: no button over the picture, none of the page's (%s)" % list(b))
    p.esc()
    s = p.state()
    check(s["open"] and s["locked"] is None and s["fullscreen"] == "game-stage", "Esc in fullscreen with the mouse locked opens the panel and lets the lock go (open %s, locked %s)" % (s["open"], s["locked"]))
    p.esc()
    check(not p.state()["open"], "(Esc closes it)")
    tab.ev("document.exitPointerLock(); 1")
    time.sleep(0.6)
    p.esc()
    s = p.state()
    check(s["open"] and s["parent"] == "game-stage" and s["fullscreen"] == "game-stage", "Esc opens the panel in fullscreen with the mouse free as well")
    p.esc()
    tab.ev("document.exitFullscreen(); 1")
    wait_for(lambda: not p.state()["fullscreen"], 5, 0.2)
    time.sleep(0.6)
    check(not p.state()["fullscreen"] and not p.state()["open"], "(back in the window, the panel closed)")

    # ---- a dialog that needs its answer
    p.esc()
    check(p.state()["open"], "(the panel again)")
    p.tab_to("game")
    p.click_text(".gm-act", "Open (F1)")
    time.sleep(0.8)
    s = p.state()
    check(not s["open"] and s["modal"] == 1, "the quick help, opened from the panel, closes it (the dialog has the screen)")
    p.esc()
    check(p.state()["modal"] == 0, "(the quick help is closed)")

    # ---- Leave game: a forfeit
    p.esc()
    p.click_element(".gm-leave")
    s = p.state()
    title = tab.ev("(document.querySelector('.gm-ask-t') || {}).textContent")
    check(s["leave"] == 2 and title == "Forfeit and leave?", "Leave game asks, with one opponent left, whether to forfeit (%s)" % title)
    check(tab.ev("document.activeElement.textContent") == "Stay in the game", "the focus is on Stay in the game")
    tab.save_shot(args.shots, "menu-4-forfeit")
    p.esc()
    check(p.state()["open"] and tab.ev("document.querySelector('.gm-ask')") is None, "Esc in the question means stay")
    p.click_element(".gm-leave")
    p.click_text(".gm-ask-btns .btn", "Stay in the game")
    check(p.state()["open"] and p.state()["running"] == 1, "Stay in the game goes back to the panel and the match runs on")
    p.click_element(".gm-leave")
    p.click_text(".gm-ask-btns .btn", "Forfeit")
    over = wait_for(lambda: p.state()["running"] == 0 and p.state()["where"] == -1, 15, 0.3)
    s = p.state()
    check(bool(over) and not s["open"], "Forfeit ends the match (the results are up, the panel is gone: running %s, where %s)" % (s["running"], s["where"]))
    tab.save_shot(args.shots, "menu-5-results")

    # ---- Leave game: a game for one
    p.open_game(ALONE, 1280, 800, fresh=False)
    check(p.start_match(), "a game for one is running")
    p.esc()
    p.click_element(".gm-leave")
    s = p.state()
    check(s["leave"] == 0 and tab.ev("(document.querySelector('.gm-ask-t') || {}).textContent") == "Leave the game?" and tab.ev("document.querySelector('.gm-ask-b').textContent") == "This match ends. Your game is not saved.",
          "Leave game in a game for one asks \"Leave the game?\" in the words of what happens (kind %s)" % s["leave"])
    p.click_text(".gm-ask-btns .btn", "Leave game")
    gone = wait_for(lambda: tab.ev("location.pathname") != "/play", 15, 0.3)
    check(bool(gone), "...and the game leaves for the front page (%s)" % tab.ev("location.pathname"))


def phone(p, args, check):
    tab = p.tab
    print("[web game menu] a phone, 390 x 844 with touch")
    p.open_game(LOCAL, 390, 844, mobile=True)
    check(p.start_match(), "a match is running")
    wait_for(lambda: p.buttons().get("settings-btn", {}).get("shown") and not p.buttons()["settings-btn"]["disabled"], 10)       # (shown and on are two moments of the page's quarter-second poll)
    b = p.buttons()
    check(b.get("settings-btn", {}).get("shown") and b["settings-btn"]["h"] >= 43.5 and b["settings-btn"]["w"] >= 43.5 and "settings-btn-narrow" not in b and p.state()["scrollW"] <= p.state()["innerW"],
          "the Settings button is in the header at 390 px, at least 44 x 44, with no strip under the picture and no sideways scroll (%s)" % {k: (v["shown"], round(v["h"])) for k, v in b.items()})
    taps = p.state()["taps"]
    p.tap_element("#settings-btn")
    s = p.state()
    check(s["open"] and s["parent"] == "game-stage", "a tap opens the panel")
    check(s["panel"] and s["panel"][0] >= 15.5 and s["panel"][1] >= 15.5 and s["panel"][0] + s["panel"][2] <= s["innerW"] - 15.5 and s["panel"][1] + s["panel"][3] <= s["innerH"] - 15.5,
          "it is inside a gutter of 16 px (%s on %s x %s)" % ([round(v) for v in s["panel"] or []], s["innerW"], s["innerH"]))
    check(s["scrollW"] <= s["innerW"], "the page does not scroll sideways (%s of %s)" % (s["scrollW"], s["innerW"]))
    for section in ("sound", "video", "controls", "game"):
        p.tab_to(section)
        sizes = json.loads(tab.ev(PANEL_SIZES))
        check(not sizes["small"], "%s: every button and slider is at least 44 px high (%s)" % (section, sizes["small"][:3]))
        names = [t for c, t in sizes["text"] if "gm-name" in c or "gm-note" in c or "gm-ask-b" in c]
        hints = [t for c, t in sizes["text"] if "gm-hint" in c]
        check(all(t >= 16 - 0.5 for t in names) and all(t >= 13 - 0.1 for t in hints), "%s: the text is at least 16 px, the hints 13 (%s, %s)" % (section, sorted(set(names))[:2], sorted(set(hints))[:2]))
        tab.save_shot(args.shots, "menu-phone-" + section)
    p.tab_to("controls")
    heads = tab.ev("[].map.call(document.querySelectorAll('.gm-gh'), function (h) { return h.textContent; }).join()")
    check(heads == "TOUCH,KEYS", "the Controls list is the touch list on a touch screen (%s)" % heads)
    p.tab_to("video")
    rows = tab.ev("[].map.call(document.querySelectorAll('.gm-name'), function (h) { return h.textContent; }).join()")
    check("Fullscreen mouse" not in rows, "a touch screen has no Fullscreen mouse row")
    p.tap_element(".gm-panel .gm-tab:nth-child(1)")
    p.tap_element(".gm-panel input")
    check(p.state()["taps"] == taps, "a tap on the panel is nothing for the game's touch model (taps %s, was %s)" % (p.state()["taps"], taps))
    p.tap_element(".gm-leave")
    title = tab.ev("(document.querySelector('.gm-ask-t') || {}).textContent")
    ask = json.loads(tab.ev("JSON.stringify((function () { var r = document.querySelector('.gm-ask').getBoundingClientRect(), p = document.querySelector('.gm-panel').getBoundingClientRect(); return [r.x, r.y, r.width, r.height, p.x, p.y, p.width, p.height]; })())"))
    check(title == "Forfeit and leave?" and ask[0] >= 0 and ask[0] + ask[2] <= 390 and ask[1] >= 0 and ask[1] + ask[3] <= 844, "the question of Leave game fits the screen (%s)" % [round(v) for v in ask[:4]])
    tab.save_shot(args.shots, "menu-phone-ask")
    p.tap_element(".gm-ask-btns .btn")
    check(p.state()["open"], "Stay in the game goes back to the panel")
    p.tap_element(".gm-x")
    s = p.state()
    check(not s["open"] and s["active"] == "canvas", "the cross closes it and the canvas has the focus")
    # a finger that is on the map when the panel opens (Esc, or a second hand) is ended with no act, and the next finger is the game's
    cx, cy = json.loads(tab.ev("JSON.stringify((function(){var b=document.getElementById('canvas').getBoundingClientRect();return [b.x+b.width*0.4,b.y+b.height*0.5];})())"))
    taps = p.state()["taps"]
    tab.call("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": cx, "y": cy}]})
    time.sleep(0.3)
    tab.ev("window.antsGameMenuAsk(); 1")
    time.sleep(0.5)
    tab.call("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
    time.sleep(0.4)
    s = p.state()
    check(s["open"] and s["taps"] == taps and tab.ev("Module._ants_probe(34)") == 0 and tab.ev("Module._ants_probe(35)") == 0, "a finger held on the map while the panel opens, lifted on the panel: no tap, nothing left held (taps %s, was %s)" % (s["taps"], taps))
    p.tap_element(".gm-x")
    tab.tap(cx, cy)
    time.sleep(0.6)
    s = p.state()
    check(not s["open"] and s["taps"] == taps + 1 and tab.ev("Module._ants_probe(34)") == 0, "...and after it closes the next finger on the map is a tap of the game's (taps %s, was %s)" % (s["taps"], taps))
    p.tap_element("#settings-btn")
    check(p.state()["open"], "(the button opens it again)")

    print("[web game menu] a phone on its side, 844 x 390")
    tab.emulate(844, 390, 1, mobile=True)
    time.sleep(1.2)
    s = p.state()
    check(s["panel"] and s["panel"][1] >= 7.5 and s["panel"][1] + s["panel"][3] <= 390 - 7.5, "the panel is the screen's height less the gutter (%s)" % [round(v) for v in s["panel"] or []])
    sizes = json.loads(tab.ev(PANEL_SIZES))
    check(not sizes["small"], "every button is at least 44 px high on its side too (%s)" % sizes["small"][:3])
    on_screen = json.loads(tab.ev("JSON.stringify([].map.call(document.querySelectorAll('.gm-head button, .gm-tab, .gm-resume, .gm-leave, .gm-note'), function (b) { var r = b.getBoundingClientRect(); return [b.className, r.y >= 0 && r.y + r.height <= innerHeight && r.x >= 0 && r.x + r.width <= innerWidth]; }))"))
    check(all(ok for _, ok in on_screen), "the cross, the sections, the note, Resume and Leave game are all on the screen (%s)" % [c for c, ok in on_screen if not ok])
    tab.save_shot(args.shots, "menu-phone-landscape")
    tab.emulate(390, 844, 1, mobile=True)
    p.tap_element(".gm-x")

    # ---- fullscreen on a touch screen: a phone has no Esc and, by the owner's word, nothing is laid over the picture either: no Settings button is on the screen until fullscreen is left
    tab.ev("enterFullscreen(); 1")
    wait_for(lambda: p.state()["fullscreen"], 5, 0.2)
    time.sleep(1.0)
    b = p.buttons()
    check(p.state()["fullscreen"] and "settings-btn-fs" not in b and not any_visible(tab), "in fullscreen on a touch screen nothing of the menu is on the screen: no button over the picture, none of the page's (%s)" % list(b))
    tab.save_shot(args.shots, "menu-phone-fullscreen")
    tab.ev("document.exitFullscreen(); 1")
    wait_for(lambda: not p.state()["fullscreen"], 5, 0.2)
    time.sleep(0.8)
    check(any_visible(tab), "(and the page's own Settings button is back when fullscreen is left)")


def scroll_every_section(p, check, label, how):
    """every section of the panel whose list is taller than the pane: moved to its end by `how` (a wheel or a finger), the last thing of the list is whole inside the pane. At least one list must be really cut
    off before it is moved (else the check showed nothing)."""
    tab = p.tab
    cut = 0
    for section in ("sound", "video", "controls", "game"):
        p.tab_to(section)
        tab.ev("document.querySelector('.gm-pane').scrollTop = 0; 1")
        time.sleep(0.2)
        m = p.pane()
        check(m["panel"][0] >= -0.5 and m["panel"][1] >= -0.5 and m["panel"][2] <= m["inner"][0] + 0.5 and m["panel"][3] <= m["inner"][1] + 0.5 and m["y"] + m["h"] <= m["panel"][3] + 0.5 and m["resume"][3] <= m["panel"][3] + 0.5,
              "%s, %s: the panel is inside the window and holds the pane and Resume (panel %s of %s, the pane ends at %s, Resume at %s)" % (label, section, [round(v) for v in m["panel"]], m["inner"], round(m["y"] + m["h"]), round(m["resume"][3])))
        if m["over"] < 1:
            continue
        cut += 1 if m["low"] > m["bottom"] + 0.5 else 0
        for _ in range(12):
            how(m)
            if p.pane()["top"] >= m["over"] - 1:
                break
        end = p.pane()
        check(end["top"] >= m["over"] - 1 and end["low"] <= end["bottom"] + 0.5, "%s, %s: moved to its end, the last row is whole in the pane (moved %s of %s px, its foot at %s, the pane's at %s)" % (label, section, round(end["top"]), round(m["over"]), round(end["low"]), round(end["bottom"])))
    check(cut >= 1, "%s: a list that was cut off before it was moved (%d of 4)" % (label, cut))


def scrolling(p, args, check):
    tab = p.tab
    print("[web game menu] scrolling a section's list: a wheel in three windows and in fullscreen, a finger on a phone")

    def wheel(m):
        p.wheel(m["x"] + 6, m["y"] + m["h"] / 2, 240)

    def finger(m):
        p.swipe_up(m["x"] + 6, m["y"] + m["h"] - 24, m["y"] + 24)

    p.open_game(LOCAL, 1280, 720)
    check(p.start_match(), "a match is running")
    wait_for(lambda: p.buttons().get("settings-btn", {}).get("shown") and not p.buttons()["settings-btn"]["disabled"], 10)
    tab.ev("document.getElementById('canvas').focus(); 1")
    p.esc()
    check(p.state()["open"], "Esc opens the panel")
    for width, height in ((1280, 720), (1024, 600), (800, 500)):
        tab.emulate(width, height, 1, touch=False)                          # (touch=False: a second "touch emulation off" makes the tab report `hover: none`, and the panel then leaves out the rows for a mouse)
        time.sleep(0.8)
        scroll_every_section(p, check, "%d x %d, a wheel" % (width, height), wheel)
        if (width, height) == (1280, 720):
            p.tab_to("video")
            tab.save_shot(args.shots, "menu-scroll-1-video-end")
    tab.emulate(1280, 720, 1, touch=False)
    time.sleep(0.8)
    p.tab_to("video")
    p.click_text(".gm-act", "Enter fullscreen")
    wait_for(lambda: p.state()["fullscreen"], 5, 0.2)
    time.sleep(1.2)
    check(p.state()["fullscreen"] == "game-stage" and p.state()["open"], "fullscreen, the panel inside it")
    scroll_every_section(p, check, "fullscreen, a wheel", wheel)
    tab.save_shot(args.shots, "menu-scroll-2-fullscreen")
    tab.ev("window.__wheels = []; document.addEventListener('wheel', function (e) { window.__wheels.push(e.defaultPrevented); }, { passive: true }); 1")      # (the document hears it after the stage's listener, which is the one that cancels)
    m = p.pane()
    p.wheel(6, 400, 120)                                                                    # the dimmed margin, outside the panel
    p.wheel(m["x"] + 6, m["y"] + m["h"] / 2, 120)                                           # the pane
    seen = tab.ev("JSON.stringify(window.__wheels)")
    check(seen == "[true,false]", "in fullscreen a wheel over the dimmed margin around the panel is cancelled like one over a bar, and one over the panel is not (cancelled: %s)" % seen)
    tab.ev("document.exitFullscreen(); 1")
    wait_for(lambda: not p.state()["fullscreen"], 5, 0.2)

    p.open_game(LOCAL, 390, 844, mobile=True)
    check(p.start_match(), "a match is running on a phone")
    wait_for(lambda: p.buttons().get("settings-btn", {}).get("shown") and not p.buttons()["settings-btn"]["disabled"], 10)
    p.tap_element("#settings-btn")
    check(p.state()["open"], "a tap opens the panel")
    scroll_every_section(p, check, "390 x 844, a finger", finger)
    scroll_every_section(p, check, "390 x 844, a wheel", wheel)
    tab.emulate(844, 390, 1, mobile=True)
    time.sleep(1.0)
    scroll_every_section(p, check, "844 x 390, a finger", finger)
    scroll_every_section(p, check, "844 x 390, a wheel", wheel)
    tab.save_shot(args.shots, "menu-scroll-3-phone-landscape-end")


OUTSIDE = r"""
JSON.stringify({ open: !!document.getElementById('game-menu'), small: !!document.querySelector('#game-menu .gm-small'), tabs: document.querySelectorAll('#game-menu [role=tab]').length,
                 rows: [].map.call(document.querySelectorAll('#game-menu .gm-name'), function (n) { return n.textContent; }), active: (document.activeElement.textContent || document.activeElement.id || '').slice(0, 14),
                 where: Module._ants_menu_get(100), needs: Module._ants_menu_get(101), running: Module._ants_match_running(), chat: Module._ants_chat_probe(5), path: location.pathname })"""


def outside(p, args, check):
    print("[web game menu] outside a match, 1280 x 800: the quick help, the setup screen, a match under a dialog, the results")
    tab = p.tab
    st = lambda: json.loads(tab.ev(OUTSIDE))
    gear = lambda: wait_for(lambda: p.buttons().get("settings-btn", {}).get("shown") and not p.buttons()["settings-btn"]["disabled"], 15)
    open_gear = lambda: (tab.ev("window.scrollTo(0, 0); 1"), p.click_element("#settings-btn"), wait_for(lambda: st()["open"], 4))
    on_locked = lambda: tab.ev("(function () { var m = document.querySelector('#game-menu .gm-panel'); m.setAttribute('tabindex', '-1'); m.focus(); })(); 1")      # (the focus on the panel itself, where no key presses a button)
    on_close = lambda: tab.ev("document.querySelector('.gm-resume').focus(); 1")
    S = ("s", "KeyS", 83)
    # 1 - 4: the quick help at the start, then the setup screen (no map in the address: the game's own screens)
    p.open_game("play", 1280, 800)
    time.sleep(2.5)                                                            # (the opening picture is over)
    check(bool(gear()), "the gear is on while the game shows its quick help (step 1)")
    open_gear()
    s = st()
    check(s["open"] and s["small"] and s["tabs"] == 0 and s["rows"][:2] == ["Fullscreen", "Picture shape"] and s["active"] == "Close", "it opens the small form with the page's rows and the focus on Close (step 2: %s)" % s["rows"])
    on_locked()
    for ch in "cxqs":
        p.key(ch, "Key" + ch.upper(), ord(ch.upper()), text=ch)
    p.key("Enter", "Enter", 13, text="\r")
    p.key(" ", "Space", 32, text=" ")
    s = st()
    check(s["open"] and s["running"] == 0, "c x q s, Enter and Space typed into it: the quick help is not closed by them (step 3)")
    p.esc()
    check(not st()["open"] and st()["running"] == 0, "a short Esc closes the form (step 4)")
    p.key(*S, text="s")
    time.sleep(1.5)
    check(st()["running"] == 0, "(the control: S on the quick help starts nothing, so the quick help is still up)")
    open_gear()
    p.hold("Escape", "Escape", 27)
    p.key(*S, text="s")
    check(not st()["open"] and st()["running"] == 0, "Esc held past the close does not close the quick help (step 5)")
    p.key("c", "KeyC", 67, text="c")                                           # (step 6: C closes the quick help: keys reach the game when no form is up; the setup screen follows)
    time.sleep(0.8)
    open_gear()
    on_locked()
    for ch in "sqxt":
        p.key(ch, "Key" + ch.upper(), ord(ch.upper()), text=ch)
    for ch in "hello":
        p.key(ch, "Key" + ch.upper(), ord(ch.upper()), text=ch)
    p.key("Enter", "Enter", 13, text="\r")
    p.key(" ", "Space", 32, text=" ")
    p.key("ArrowDown", "ArrowDown", 40)
    p.key("ArrowDown", "ArrowDown", 40)
    p.key("F1", "F1", 112)
    p.key("Backspace", "Backspace", 8)
    s = st()
    check(s["running"] == 0 and s["where"] == -1 and s["chat"] == 0, "on the setup screen s q x t hello, Enter, Space, Down, Down, F1, Backspace typed into the form: no match, no chat line (step 7)")
    p.esc()
    check(not st()["open"] and st()["where"] == -1 and st()["running"] == 0, "...a short Esc closes it and the screen is the same (step 7)")
    open_gear()
    before = tab.ev("document.getElementById('game-stage').getAttribute('data-aspect')")
    tab.ev("document.querySelector('#game-menu .pick-select').click(); 1")
    time.sleep(0.3)
    tab.ev("document.querySelector('#gm-picture-o0').click(); 1")
    time.sleep(0.6)
    check(tab.ev("document.getElementById('game-stage').getAttribute('data-aspect')") != before and st()["where"] == -1, "a shape picked from the form changes the picture and the setup screen stays (step 8)")
    p.click_element(".gm-resume")
    # 9: Space on Close; 10: Enter held on Close
    open_gear()
    on_close()
    p.key(" ", "Space", 32, text=" ")
    check(not st()["open"], "Space on a focused Close closes the form (the game cancels every key lift: the page keeps the lift of a key it kept) (step 9)")
    open_gear()
    on_close()
    p.hold("Enter", "Enter", 13, text="\r")
    time.sleep(1.5)
    check(st()["running"] == 0 and not st()["open"], "Enter held on Close does not start the match: the repeats after the close are not a new press for the game (step 10)")
    p.key(*S, text="s")
    check(bool(wait_for(lambda: st()["running"] == 1, 20, 0.5)), "S with no form starts the match (step 11: the control, it was the setup screen)")
    # 12 - 14: a match against one computer player
    p.open_game("play?map=tiny&bots=easy,none,none", 1280, 800)
    time.sleep(2.5)
    p.key("c", "KeyC", 67, text="c")
    check(bool(wait_for(lambda: st()["running"] == 1 and st()["where"] == 0 and st()["needs"] == 0, 40, 0.5)), "a match with nothing waiting")
    p.ctrl("q", "KeyQ", 81)
    check(st()["needs"] == 1, "Ctrl+Q: the quit question waits")
    open_gear()
    s = st()
    check(s["open"] and s["small"], "the gear opens the small form under the dialog (step 12)")
    on_locked()
    p.key("y", "KeyY", 89, text="y")
    p.key("Enter", "Enter", 13, text="\r")
    p.key("n", "KeyN", 78, text="n")
    p.esc()
    check(st()["needs"] == 1 and not st()["open"], "y, Enter, n typed into it and a short Esc: the question still waits (step 12)")
    p.key("n", "KeyN", 78, text="n")
    check(bool(wait_for(lambda: st()["needs"] == 0, 5, 0.3)), "N with no form answers it (step 12: the control)")
    p.esc()
    s = st()
    check(s["open"] and not s["small"] and s["tabs"] == 4 and s["active"] == "Resume", "Esc opens the full menu with its four tabs, the focus on Resume (step 13)")
    p.key("Enter", "Enter", 13, text="\r")
    check(not st()["open"], "Enter on Resume closes it")
    p.esc()
    p.key(" ", "Space", 32, text=" ")
    check(not st()["open"], "Space on Resume closes it too (the real game cancels the lift: the page keeps it)")
    p.esc()
    p.hold("Escape", "Escape", 27, repeats=6)
    time.sleep(0.5)
    check(not st()["open"], "Esc held past the close of the full menu: it does not come back (step 13)")
    p.esc()
    tab.ev("document.querySelector('.gm-leave').focus(); 1")
    p.hold("Enter", "Enter", 13, text="\r", repeats=6)
    check(bool(tab.ev("!!document.querySelector('.gm-ask')")) and st()["running"] == 1 and st()["where"] == 0, "Enter held on Leave game opens its question once and leaves it up (no flicker, nothing leaves) (step 13)")
    p.esc()
    p.esc()
    check(not st()["open"] and st()["running"] == 1, "(Esc says stay, Esc closes the menu)")
    p.hold("Escape", "Escape", 27, repeats=12)                                   # (13b: Esc held for a second: the game asks for the menu at the first press, the repeats come under it)
    check(st()["open"], "Esc held for a second opens the menu (step 13b)")
    p.click_element(".gm-resume")
    p.esc()
    check(st()["open"] and st()["tabs"] == 4, "...Resume closes it, and the next Esc press opens it again: the lift of a key that went down before the menu reached the game (step 13b)")
    p.esc()
    open_gear()
    check(st()["open"] and st()["tabs"] == 4, "the gear in a match opens the full menu, not the small form (step 14)")
    p.click_element(".gm-leave")
    tab.ev("document.querySelector('.gm-ask .gm-leave').focus(); 1")
    p.hold("Enter", "Enter", 13, text="\r")
    time.sleep(1.5)
    s = st()
    check(s["path"] == "/play" and s["where"] == -1 and s["running"] == 0, "Enter held on Forfeit: the results come and stay, the page does not leave (step 14b)")
    # 14: the results
    open_gear()
    s = st()
    check(s["open"] and s["small"], "the gear on the results opens the small form (step 14)")
    on_locked()
    for ch in "cqx":
        p.key(ch, "Key" + ch.upper(), ord(ch.upper()), text=ch)
    p.key("Enter", "Enter", 13, text="\r")
    p.esc()
    check(st()["path"] == "/play" and st()["where"] == -1, "c q x and Enter typed into it, a short Esc: the results stay (step 14)")
    open_gear()
    on_close()
    p.hold("Enter", "Enter", 13, text="\r")
    time.sleep(1.5)
    check(st()["path"] == "/play" and st()["where"] == -1, "Enter held on Close on the results: they stay (step 14)")
    p.key("c", "KeyC", 67, text="c")
    gone = wait_for(lambda: tab.ev("location.pathname") != "/play" or st()["where"] != -1, 8, 0.3)
    check(bool(gone), "C with no form leaves the results (the control)")
    # the gear by keyboard
    p.open_game("play", 1280, 800)
    time.sleep(2.5)
    gear()
    tab.ev("document.getElementById('settings-btn').focus(); 1")
    p.key("Enter", "Enter", 13, text="\r")
    s = st()
    check(s["open"] and s["small"], "a focused gear opens the form on Enter (the game does not get the key) (the gear by keyboard)")
    p.esc()
    p.key(*S, text="s")
    check(st()["running"] == 0, "...and the quick help is still up after it (S starts nothing)")
    # a match that begins under the form
    p.key("c", "KeyC", 67, text="c")
    time.sleep(0.8)
    open_gear()
    tab.ev("(function () { var o = { key: 's', code: 'KeyS', keyCode: 83, which: 83, bubbles: true, cancelable: true }; window.dispatchEvent(new KeyboardEvent('keydown', o)); window.dispatchEvent(new KeyboardEvent('keyup', o)); })(); 1")
    seen = wait_for(lambda: st()["running"] == 1 and st()["needs"] == 1 and st()["open"], 10, 0.2)
    check(bool(seen), "a match that begins under the form (its START key handed to the game's own listener): the form stays over the countdown")
    check(bool(wait_for(lambda: st()["needs"] == 0 and not st()["open"], 40, 0.3)), "...and closes by itself when the card goes")
    p.esc()
    check(st()["open"] and not st()["small"], "...and Esc then opens the full menu (the game takes keys again)")


def recording(p, args, check):
    from web_camera_check import Replay
    print("[web game menu] a recording, 1440 x 900")
    with open(args.replay, "rb") as f:
        data = f.read()
    tab = p.tab
    r = Replay(tab, args.web, data, False)
    r.open(1440, 900, 1)
    b = p.buttons()
    wait_for(lambda: p.buttons().get("settings-btn-bar", {}).get("shown"), 10)
    b = p.buttons()
    check(list(b) == ["settings-btn-bar"] and b["settings-btn-bar"]["shown"] and b["settings-btn-bar"]["h"] >= 30, "the replay's bar has the Settings button and the page has no other (%s)" % list(b))
    turn = tab.ev("Module._ants_replay_get(1)")
    p.esc()
    s = p.state()
    check(s["open"] and s["where"] == 1, "Esc opens the panel over a recording (where %s)" % s["where"])
    tabs = tab.ev("[].map.call(document.querySelectorAll('[role=tab]'), function (t) { return t.textContent; }).join()")
    check(tabs == "Sound,Video,Controls", "no Game section (%s)" % tabs)
    check(tab.ev("document.querySelector('.gm-tag').textContent") == "Watching a replay" and tab.ev("document.querySelector('.gm-note').textContent") == "The replay keeps playing. Pause it with the bar.", "the tag and the note say what is watched")
    check(tab.ev("[].map.call(document.querySelectorAll('.gm-btns button'), function (t) { return t.textContent; }).join()") == "Resume,Back to the matches", "Resume and Back to the matches")
    tab.save_shot(args.shots, "menu-replay")
    time.sleep(2.0)
    check(tab.ev("Module._ants_replay_get(1)") > turn, "the replay keeps playing while the panel is up (turn %s -> %s)" % (turn, tab.ev("Module._ants_replay_get(1)")))
    p.tab_to("video")
    check("Picture shape" in tab.ev("[].map.call(document.querySelectorAll('.gm-name'), function (t) { return t.textContent; }).join()"), "the Video section has the picture's shape")
    p.esc()
    check(not p.state()["open"], "Esc closes it")
    p.click_element("#settings-btn-bar")
    check(p.state()["open"], "the button in the bar opens it")
    p.click_element(".gm-leave")
    gone = wait_for(lambda: tab.ev("location.pathname") != "/play", 15, 0.3)
    check(bool(gone) and not tab.ev("document.querySelector('.gm-ask')"), "Back to the matches asks nothing and leaves for the list (%s)" % tab.ev("location.pathname"))

    print("[web game menu] the frame of a match page (embed=1)")
    tab.emulate(1000, 700, 1)
    tab.open("about:blank", wait=False)
    url = r.url + "&embed=1"
    tab.open(url, settle=1.5)
    time.sleep(3.0)
    s = tab.ev("JSON.stringify({ buttons: document.querySelectorAll('.gm-btn').length, ask: typeof window.antsGameMenuAsk, script: !!document.querySelector('script[src^=\"game_menu\"]'), fetched: performance.getEntriesByType('resource').filter(function (e) { return e.name.indexOf('game_menu') >= 0; }).length })")
    s = json.loads(s)
    check(s == {"buttons": 0, "ask": "undefined", "script": False, "fetched": 0}, "in a frame there is no button, no function for Esc, no script and no fetch (%s)" % s)
    p.esc()
    time.sleep(0.4)
    check(not p.state()["open"], "Esc opens nothing there")


if __name__ == "__main__":
    sys.exit(main())
