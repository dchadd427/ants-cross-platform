#!/usr/bin/env python3
"""The PHONE'S KEYBOARD for the chat box in a REAL browser (opt-in; see tests/scripts/test_web_chat.sh and docs/TOUCH.md "The chat box and the on-screen keyboard").

Needs a running web page (the web image of this tree, or tools/web_without_docker.py), a Chromium-based browser and Python 3. The check opens the page in a throwaway headless browser as a
PHONE (the profiles of web_touch_check.py: a Pixel-like and an iPhone-like one; touch, mobile metrics, the phone's user agent), starts a match, and drives the page with the DevTools
protocol's touch and key events, reading what the GAME believes through its exports (`ants_chat_probe`: whether the box takes text, where it is, the game's chat line; `ants_probe`: the view,
the zoom, whether a dialog is open). It checks:

  * a tap on the map opens nothing; a TAP on the chat box focuses the page's field (inside the touch's own handler), and the field is a real, focusable, invisible text field of at least 16px
    (iOS zooms the page into a smaller one), over the box;
  * opening it moves nothing: the game's box, the page's scroll and the page's zoom are what they were;
  * what is typed (text inserted as a keyboard does, single key presses, Backspace) is the game's own chat line, once (a key press is not typed a second time by the game's own key handler),
    and no key of it is a command: the view, the zoom and the dialogs are as they were;
  * Enter sends the line (the game's line is emptied by the game's own Enter, the field is emptied and the keyboard closes); Esc closes without sending; a tap on the map closes it; a dialog
    that opens (the Options window) closes it and the dialog gets the keys;
  * the desktop is as it was: a mouse click on the chat box focuses nothing, a keyboard types into the game's line as always.
What headless Chrome cannot show, and a real phone must: that the operating system's keyboard really comes up for this focus on iOS Safari (a focus made inside a touchend) and on Android
Chrome, what the keyboard covers (the chat box is at the bottom of the picture and a phone's keyboard may hide it), the keyboard's own suggestions and composition, Safari's zoom rule
(the field is 16px; the rule itself is Safari's), the visual viewport's resize when the keyboard opens.

Exit status: 0 every check passed; 1 a check failed, or the browser or the page broke down; 3 the environment is not there (no browser, nothing at the page's address); 2 a bad command line.
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
import web_touch_check as touch                                              # noqa: E402

DESKTOP = {"label": "a desktop with a mouse and a keyboard", "size": (1280, 900), "dpr": 1.0, "user_agent": None, "platform": None, "fullscreen_api": True, "vibrate": False, "desktop": True}


class Phone(touch.Game):
    """touch.Game with the chat box's numbers, a keyboard, and (for the desktop profile) no touch screen"""

    def open(self, query="?aspect=16:9"):
        p = self.profile
        if not p.get("desktop"):
            return super().open(query)
        width, height = p["size"]
        self.tab.emulate(width, height, p["dpr"], mobile=False, touch=False)
        self.tab.open(self.web + query, settle=0.5)
        time.sleep(2.0)

    def chat(self, what):
        return self.tab.ev("Module._ants_chat_probe(%d)" % what)

    def line(self):
        return self.tab.ev("(function () { var n = Module._ants_chat_probe(5), s = ''; for (var i = 0; i < n; ++i) s += String.fromCharCode(Module._ants_chat_probe(100 + i)); return s; })()")

    def box(self):
        return [self.chat(i) for i in (1, 2, 3, 4)]

    def active(self):
        return self.tab.ev("document.activeElement && document.activeElement.id || ''")

    def state(self):
        return self.tab.ev("(function () { var p = Module._ants_probe, b = document.getElementById('game-container').getBoundingClientRect(); return {x: p(3), y: p(4), zoom: p(6), modal: p(5), taps: p(30), "
                           "box: [b.x, b.y, b.width, b.height], scroll: [window.scrollX, window.scrollY], scale: window.visualViewport ? window.visualViewport.scale : 1}; })()")

    def key(self, key, code, vk, text=None, kind="both"):
        for k in (["keyDown", "keyUp"] if kind == "both" else [kind]):
            params = {"type": k, "key": key, "code": code, "windowsVirtualKeyCode": vk}
            if text is not None and k == "keyDown":
                params["text"] = text
            self.tab.call("Input.dispatchKeyEvent", params)

    def type_keys(self, text):
        for ch in text:
            self.key(ch, "Key" + ch.upper() if ch.isalpha() else "", ord(ch.upper()), text=ch)
            time.sleep(0.03)

    def box_point(self, g):
        x, y, w, h = self.box()
        return self.at(g, x + w / 2, y + h - 8)                              # the input box at the bottom of the chat box

    def map_point(self, g):
        return self.at(g, 300, 250)


def run_profile(name, profile, tab, game, check, args):
    desktop = bool(profile.get("desktop"))
    game.open()
    g = game.scroll_to_box()
    check(bool(g["ready"]), "%s: the page is ready" % name)
    ok = game.start_match()
    check(ok, "%s: a match starts" % name)
    if not ok:
        return
    g = game.scroll_to_box()
    time.sleep(0.5)
    check(game.chat(0) == 1, "%s: the game says that the chat box takes text" % name)
    bx = game.box()
    check(bx[2] > 100 and bx[3] > 60 and bx[0] + bx[2] <= touch.CANVAS_W and bx[1] + bx[3] <= touch.CANVAS_H, "%s: the chat box is in the picture (%s)" % (name, bx))
    field = tab.ev("(function () { var e = document.getElementById('chat-keys'), s = getComputedStyle(e); return {tag: e.tagName, type: e.type, font: parseFloat(s.fontSize), opacity: s.opacity, display: s.display, "
                   "visibility: s.visibility, pointer: s.pointerEvents, focusable: e.tabIndex >= -1}; })()")
    check(field["tag"] == "INPUT" and field["type"] == "text" and field["display"] != "none" and field["visibility"] != "hidden" and field["opacity"] == "0",
          "%s: the field is a real text input that is invisible (opacity 0, not display: none) %s" % (name, field))
    check(field["font"] >= 16, "%s: its font is at least 16px (iOS zooms the page into a smaller field): %s" % (name, field["font"]))
    before = game.state()
    if desktop:
        x, y = game.box_point(g)
        tab.click(x, y)
        time.sleep(0.4)
        check(game.active() != "chat-keys", "%s: a mouse click on the chat box focuses nothing of the page's (%r)" % (name, game.active()))
        game.tab.ev("document.getElementById('canvas').focus(); 1")
        game.type_keys("hi")
        time.sleep(0.4)
        check(game.line() == "hi", "%s: the keyboard types into the game's line as it always did (%r)" % (name, game.line()))
        game.key("Backspace", "Backspace", 8)
        game.key("Backspace", "Backspace", 8)
        time.sleep(0.3)
        check(game.line() == "", "%s: Backspace takes it back (%r)" % (name, game.line()))
        return

    # ---- a tap on the map opens nothing
    mx, my = game.map_point(g)
    game.tap(mx, my)
    time.sleep(0.4)
    check(game.active() != "chat-keys", "%s: a tap on the map does not open the keyboard (%r)" % (name, game.active()))

    # ---- a tap on the chat box does
    x, y = game.box_point(g)
    game.tap(x, y)
    check(bool(touch.wait_for(lambda: game.active() == "chat-keys", 3.0)), "%s: a tap on the chat box focuses the page's field (%r)" % (name, game.active()))
    after = game.state()
    check(after["box"] == before["box"] and after["scroll"] == before["scroll"] and after["scale"] == before["scale"],
          "%s: opening it moves nothing: the game's box, the page's scroll and zoom are as they were (%s -> %s)" % (name, before, after))
    placed = tab.ev("(function () { var e = document.getElementById('chat-keys').getBoundingClientRect(), b = document.getElementById('game-container').getBoundingClientRect(); return [e.x - b.x, e.y - b.y, e.width, e.height, b.width, b.height]; })()")
    check(placed[2] > 20 and placed[3] > 20 and placed[0] >= -1 and placed[1] >= -1 and placed[0] + placed[2] <= placed[4] + 1 and placed[1] + placed[3] <= placed[5] + 1,
          "%s: the field lies over the chat box, inside the game's box (%s)" % (name, [round(v) for v in placed]))

    # ---- typing
    tab.call("Input.insertText", {"text": "Rush the base"})
    check(bool(touch.wait_for(lambda: game.line() == "Rush the base", 2.0)), "%s: text that the keyboard inserts is the game's chat line (%r)" % (name, game.line()))
    game.key("Backspace", "Backspace", 8)
    check(bool(touch.wait_for(lambda: game.line() == "Rush the bas", 2.0)), "%s: Backspace takes one off (%r)" % (name, game.line()))
    tab.call("Input.insertText", {"text": " éè"})
    time.sleep(0.4)
    check(game.line() == "Rush the bas " and tab.ev("document.getElementById('chat-keys').value") == "Rush the bas ", "%s: a character the game does not take (an accent) is taken out (%r)" % (name, game.line()))
    game.type_keys("wasd")
    time.sleep(0.4)
    line = game.line()
    check(line == "Rush the bas wasd", "%s: single key presses are typed once, not twice (%r)" % (name, line))
    during = game.state()
    check((during["x"], during["y"], during["zoom"], during["modal"]) == (before["x"], before["y"], before["zoom"], 0), "%s: no key of the typing was a command (view, zoom, dialogs as they were)" % name)

    # ---- Enter sends
    game.key("Enter", "Enter", 13, text="\r")
    check(bool(touch.wait_for(lambda: game.line() == "", 3.0)), "%s: Enter: the game sent the line and emptied it (%r)" % (name, game.line()))
    check(game.active() != "chat-keys" and tab.ev("document.getElementById('chat-keys').value") == "", "%s: the keyboard is closed and the field is empty after Enter" % name)

    # ---- Esc closes without sending; the line stays out of the way
    game.tap(x, y)
    touch.wait_for(lambda: game.active() == "chat-keys", 3.0)
    tab.call("Input.insertText", {"text": "draft"})
    touch.wait_for(lambda: game.line() == "draft", 2.0)
    game.key("Escape", "Escape", 27)
    time.sleep(0.4)
    check(game.active() != "chat-keys", "%s: Esc closes the keyboard" % name)
    check(game.line() == "draft", "%s: and the line stays in the game's box as typed (%r)" % (name, game.line()))
    game.tap(x, y)
    touch.wait_for(lambda: game.active() == "chat-keys", 3.0)
    check(tab.ev("document.getElementById('chat-keys').value") == "draft", "%s: the next tap opens with the line the game has" % name)

    # ---- a tap on the map closes it
    game.tap(mx, my)
    time.sleep(0.5)
    check(game.active() != "chat-keys", "%s: a tap on the map closes the keyboard" % name)
    tab.ev("1")
    game.key("Backspace", "Backspace", 8)

    # ---- a dialog closes it
    game.tap(x, y)
    touch.wait_for(lambda: game.active() == "chat-keys", 3.0)
    ox, oy = game.at(g, *touch.OPTIONS_BUTTON)
    tab.ev("document.getElementById('chat-keys').blur(); 1")
    game.tap(ox, oy)
    check(bool(touch.wait_for(lambda: game.probe(5) == 1, 3.0)), "%s: the Options button opens its window" % name)
    check(game.chat(0) == 0, "%s: with a dialog open the chat box takes no text" % name)
    game.tap(x, y)
    time.sleep(0.4)
    check(game.active() != "chat-keys", "%s: and a tap on the box opens no keyboard then" % name)
    game.clean(g)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the image, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 120)")
    ap.add_argument("--profile", default="all", help="pixel, iphone, desktop or all (default)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout
    profiles = dict(touch.PROFILES, desktop=DESKTOP)
    if args.profile != "all" and args.profile not in profiles:
        print("unknown profile %r (pixel, iphone, desktop or all)" % args.profile)
        return 2
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
        for name in (sorted(profiles) if args.profile == "all" else [args.profile]):
            profile = profiles[name]
            print("[web chat] %s: %s" % (name, profile["label"]))
            tab = Tab(browser)
            game = Phone(tab, args.web, profile)
            try:
                run_profile(name, profile, tab, game, check, args)
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
    print("[web chat] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
