#!/usr/bin/env python3
"""The settings menu (Esc, or a Settings button) in a REAL browser with the REAL game (opt-in; see tests/scripts/test_web_game_menu.sh, docs/GAME_MENU.md).

The quick tier runs the game's side (tests/test_app/test_game_menu.cpp: the table, Esc's rule, the settings, the input that the game does not take) and the page's script on a fake of the DOM
(tests/scripts/web_game_menu_check.js); nothing but a real page shows the chain: the real Esc key, the real game's answers, a real fullscreen and pointer lock, real touches, real sizes.
Needs the web page of this tree (`tools/web_without_docker.py -- tests/scripts/test_web_game_menu.sh`), a Chromium-based browser and Python 3; a recording (--replay, or ANTS_CAMERA_REPLAY: as
for tests/scripts/test_web_camera.sh) makes the parts that watch a replay run. No game server is needed (the games are on this computer, against a computer player or alone). What it checks:

  * the computer, 1280 x 800 (play.html?map=tiny&bots=easy,none,none): the Settings button is in the header (and the narrow one and the fullscreen one are not shown); Esc with nothing selected (a first press) opens the
    panel inside the stage, over the whole window, with the focus on Resume; what is typed meanwhile is not the game's chat line (and is, once it is closed); Ctrl+L does not turn the hit point numbers;
    Tab stays inside it; a click on the dimmed area does nothing; a held Esc (a repeat) does not close it and the next Esc does, the focus is on the canvas; Esc with a hill selected only deselects and the
    next one opens it; Esc closes the quick help and asks for nothing; a slider moved with the arrow key is the game's value and the settings file's (master=); Mute and the hit point numbers are the game's
    and remembered (mute=, hp_numbers=); the picture's shape is the selector's (the stage, the game's canvas, the address); the Fullscreen row makes the stage fullscreen with the panel inside it, the pointer lock
    is let go while it is up and asked for again by Resume; in fullscreen the Settings button is over the picture and opens it; a dialog that comes up (the quick help, from the panel's own act) closes it;
    Leave game asks (a forfeit with one opponent left), Esc and Stay stay, Forfeit ends the match (the results); in a game for one it asks "Leave the game?" and leaves for the front page;
  * a phone, 390 x 844 with touch: the button is under the picture (44 px), a tap opens the panel inside a gutter of 16 px, every button and slider is at least 44 px high, the text is at least 16 px
    (hints 13), the page does not scroll sideways, the touch model sees nothing of what is tapped on the panel, the cross closes it, the Controls list is the touch list, the question of Leave game fits;
    a phone on its side (844 x 390): the panel is the screen's height less the gutter and every button is on the screen;
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
from web_aspect_check import Browser, NotReachable, Tab                      # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

LOCAL = "play.html?map=tiny&bots=easy,none,none"                             # a game for you and one computer player: Leave game is a forfeit
ALONE = "play.html?map=tiny"                                                 # a game for one: Leave game ends the match

STATE = r"""
JSON.stringify((function () {
  var $ = function (id) { return document.getElementById(id); };
  var r = function (e) { if (!e) return null; var b = e.getBoundingClientRect(); return [b.x, b.y, b.width, b.height]; };
  var menu = $('game-menu'), panel = menu && menu.querySelector('.gm-panel'), ae = document.activeElement;
  var fs = document.fullscreenElement || document.webkitFullscreenElement, lock = document.pointerLockElement || document.webkitPointerLockElement;
  return { open: !!menu, parent: menu ? menu.parentNode.id : null, menu: r(menu), panel: r(panel), active: ae ? (ae.id || ae.className || ae.tagName) : null, activeIn: !!(menu && ae && menu.contains(ae)),
           fullscreen: fs ? fs.id : null, locked: lock ? lock.id : null, aspect: $('game-stage').getAttribute('data-aspect'), where: Module._ants_menu_get(100), needs: Module._ants_menu_get(101), leave: Module._ants_menu_get(102),
           chat: Module._ants_chat_probe(5), modal: Module._ants_probe(5), running: Module._ants_match_running(), taps: Module._ants_probe(30), shape: Module._ants_probe(42),
           master: Module._ants_menu_get(1), mute: Module._ants_menu_get(4), hp: Module._ants_menu_get(8), music: Module._ants_menu_get(2),
           settings: (function () { try { return localStorage.getItem('ants.settings') || ''; } catch (e) { return ''; } })(), path: location.pathname, scrollW: document.documentElement.scrollWidth, innerW: innerWidth, innerH: innerHeight };
})())"""

BUTTONS = r"""
JSON.stringify([].map.call(document.querySelectorAll('.gm-btn'), function (b) { var r = b.getBoundingClientRect(), s = getComputedStyle(b); return { id: b.id, hidden: b.hidden, shown: s.display !== 'none' && r.width > 0, disabled: b.disabled, w: r.width, h: r.height, x: r.x, y: r.y }; }))"""

PANEL_SIZES = r"""
JSON.stringify((function () {
  var menu = document.getElementById('game-menu'); if (!menu) return null;
  var small = [], text = [];
  [].forEach.call(menu.querySelectorAll('button, input'), function (b) { var r = b.getBoundingClientRect(); if (r.width > 0 && r.height < 43.5) small.push([b.className || b.tagName, r.height]); });
  [].forEach.call(menu.querySelectorAll('.gm-name, .gm-hint, .gm-note, .gm-ask-b'), function (b) { var r = b.getBoundingClientRect(); if (r.width > 0) text.push([b.className, parseFloat(getComputedStyle(b).fontSize)]); });
  return { small: small, text: text };
})())"""


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
        return bool(wait_for(lambda: self.tab.ev("Module._ants_match_running()") == 1, 25, 0.5)) and bool(wait_for(lambda: self.tab.ev("!!document.querySelector('#settings-btn-narrow, #settings-btn')") and self.tab.ev("Module._ants_menu_get(101)") == 0, 10, 0.3))

    def click_text(self, selector, text):
        """a real click on the first element of `selector` with this text"""
        x, y = json.loads(self.tab.ev("JSON.stringify((function(){var l=[].filter.call(document.querySelectorAll(%s),function(b){return b.textContent===%s;});if(!l.length)return null;var b=l[0].getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())" % (json.dumps(selector), json.dumps(text))))
        self.tab.click(x, y)
        time.sleep(0.4)

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
    ap.add_argument("--profile", default="all", help="desktop, phone, replay or all (default)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout
    if args.profile not in ("all", "desktop", "phone", "replay"):
        print("--profile is desktop, phone, replay or all")
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
    wait_for(lambda: p.buttons().get("settings-btn", {}).get("shown") and not p.buttons()["settings-btn"]["disabled"], 10)       # (the page polls the game every quarter second: shown and on are two moments)
    b = p.buttons()
    check(b.get("settings-btn", {}).get("shown") and not b["settings-btn"]["disabled"] and not b["settings-btn-narrow"]["shown"] and not b["settings-btn-fs"]["shown"],
          "the Settings button is in the header and on, the narrow one and the fullscreen one are not shown (%s)" % {k: v["shown"] for k, v in b.items()})
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
    check(s["chat"] == 0 and s["hp"] == 1, "what is typed is not the game's: no chat line, Ctrl+L did not turn the numbers (chat %s, numbers %s)" % (s["chat"], s["hp"]))
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
    check(groups == [["Classic 4:3", "16:10", "16:9", "21:9"], ["Locked", "Free"], ["On", "Off"]], "the Video section has the picture's shapes, the mouse and the numbers (%s)" % groups)
    p.click_text(".gm-seg .gm-opt", "21:9")
    s = p.state()
    check(s["aspect"] == "21:9" and s["shape"] == 3 and "aspect=21:9" in tab.ev("location.search"), "the shape is the selector's: the stage, the game's canvas and the address (%s, shape %s)" % (s["aspect"], s["shape"]))
    check(tab.ev("document.getElementById('aspect-21-9').getAttribute('aria-checked')") == "true", "the selector under the picture follows")
    p.click_text(".gm-seg .gm-opt", "16:9")
    p.click_text(".gm-seg .gm-opt", "Off")
    s = p.state()
    check(s["hp"] == 0 and "hp_numbers=off\n" in s["settings"], "the hit point numbers go off and are remembered (hp %s)" % s["hp"])
    p.click_text(".gm-seg .gm-opt", "On")
    check(p.state()["hp"] == 1, "...and on")

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
    check(b["settings-btn-fs"]["shown"] and b["settings-btn-fs"]["y"] < 20 and b["settings-btn-fs"]["h"] >= 43.5, "in fullscreen the Settings button is over the picture, at the top, 44 px high (%s)" % {k: round(v) if isinstance(v, float) else v for k, v in b["settings-btn-fs"].items()})
    p.esc()
    s = p.state()
    check(s["open"] and s["locked"] is None and s["fullscreen"] == "game-stage", "Esc in fullscreen with the mouse locked opens the panel and lets the lock go (open %s, locked %s)" % (s["open"], s["locked"]))
    p.esc()
    check(not p.state()["open"], "(Esc closes it)")
    tab.ev("document.exitPointerLock(); 1")
    time.sleep(0.6)
    p.click_element("#settings-btn-fs")
    s = p.state()
    check(s["open"] and s["parent"] == "game-stage" and s["fullscreen"] == "game-stage", "the fullscreen button opens the panel in fullscreen")
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
    gone = wait_for(lambda: tab.ev("location.pathname") != "/play.html", 15, 0.3)
    check(bool(gone), "...and the game leaves for the front page (%s)" % tab.ev("location.pathname"))


def phone(p, args, check):
    tab = p.tab
    print("[web game menu] a phone, 390 x 844 with touch")
    p.open_game(LOCAL, 390, 844, mobile=True)
    check(p.start_match(), "a match is running")
    wait_for(lambda: p.buttons().get("settings-btn-narrow", {}).get("shown"), 10)
    b = p.buttons()
    check(b.get("settings-btn-narrow", {}).get("shown") and b["settings-btn-narrow"]["h"] >= 43.5 and not b["settings-btn"]["shown"],
          "the Settings button is under the picture, at least 44 px high, and the header's is not shown (%s)" % {k: (v["shown"], round(v["h"])) for k, v in b.items()})
    taps = p.state()["taps"]
    p.tap_element("#settings-btn-narrow")
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
    p.tap_element("#settings-btn-narrow")
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
    gone = wait_for(lambda: tab.ev("location.pathname") != "/play.html", 15, 0.3)
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
