#!/usr/bin/env python3
"""The map maker's page in a REAL browser (opt-in; see tests/scripts/test_web_mapmaker.sh and docs/MAP_EDITOR.md).

Needs the page served (the web image of this tree, or a folder that holds web/mapmaker.html, web/front/, web/lvl_core_api.js and the built lvl_core.js and lvl_core.wasm), a Chromium-based
browser and Python 3; the DevTools protocol is spoken with the client of web_aspect_check.py (standard library only). A throwaway headless browser (its own profile) opens the page like a person.
Parts:

  * desktop  1280 x 800: the page starts with its map core and a 31 x 31 Classic map; the Classic file TINY is opened from the disk (kind, size, "Works in the original game"); a file that is
             no map is refused and the open map stays; New map makes a Classic map, refuses 100 rows with the checker's sentence and a button for an Extended map, makes an Extended map of
             512 x 512 (the most) and refuses 513 x 513; the 512 x 512 map is panned with the mouse (the point under the pointer moves with it), zoomed with the wheel (the point under the pointer
             stays, and a whole map at 512 x 512 paints in a moment), moved by the keyboard (a marked square, plus, minus, zero), and its small map shows while only a part is on the screen;
             Check map shows the card of the kind (the neutral badge for an Extended map) and Escape closes it with the focus back where it was; Download hands over the map's bytes;
  * phone    390 x 844 (portrait) and 844 x 390 (landscape), a touch screen: no sideways scroll, the buttons and the New map dialog are inside the screen, two fingers zoom and one finger pans;
  * older    a map core of another interface version (the page is given a module that says it is version 1): the page says so and offers a reload, and no button works.

Exit status 0: every check passed; 1: a check failed, or the browser or the page broke down; 3: the check could not be made because the environment is not there (no browser, nothing answers at
the address); 2 is the status of a bad command line.
"""
import argparse
import base64
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from web_aspect_check import Browser, NotReachable, Tab                      # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PARTS = ("desktop", "phone", "older")


def wait_for(condition, timeout=8.0, step=0.1):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            if condition():
                return True
        except (RuntimeError, TimeoutError):
            pass
        time.sleep(step)
    return False


class Page:
    def __init__(self, tab, url):
        self.tab, self.url = tab, url.rstrip("/") + "/mapmaker.html"

    def ev(self, expression):
        return self.tab.ev(expression)

    def open(self, init_script=None, timeout=30):
        if init_script:
            self.tab.call("Page.addScriptToEvaluateOnNewDocument", {"source": init_script})
        navigation = self.tab.call("Page.navigate", {"url": self.url})
        if navigation.get("errorText"):
            raise NotReachable("nothing answers at %s (%s)" % (self.url, navigation["errorText"]))
        return wait_for(lambda: self.ev("!!document.getElementById('boot') && (document.getElementById('boot').hidden || document.getElementById('boot').textContent.length > 30)"), timeout, 0.3)

    def ready(self):
        return bool(self.ev("document.getElementById('boot').hidden && !document.querySelector('[data-act=check]').disabled"))

    def state(self):
        return json.loads(self.ev("""JSON.stringify({
            name: document.getElementById('map-name').value, rows: document.getElementById('s-rows').textContent, columns: document.getElementById('s-cols').textContent,
            tags: Array.prototype.map.call(document.getElementById('top-tags').querySelectorAll('.tag, .badge-nv'), function (n) { return getComputedStyle(n).display === 'none' ? '' : n.textContent; }).filter(Boolean),
            check: document.getElementById('check-line').textContent, where: document.getElementById('where').textContent,
            mini: !document.getElementById('minimap').hidden, scale: +document.getElementById('map').getAttribute('data-scale'), center: document.getElementById('map').getAttribute('data-center')
        })"""))

    def box(self, selector):
        return json.loads(self.ev("JSON.stringify((function () { var b = document.querySelector(%s).getBoundingClientRect(); return {x: b.left, y: b.top, w: b.width, h: b.height}; })())" % json.dumps(selector)))

    def click_selector(self, selector):
        b = self.box(selector)
        self.tab.click(b["x"] + b["w"] / 2, b["y"] + b["h"] / 2)

    def press_act(self, act):
        self.ev("(function () { var l = document.querySelectorAll('[data-act=%s]'); for (var i = 0; i < l.length; i++) if (l[i].offsetParent !== null) { l[i].click(); return; } })()" % act)

    def choose_file(self, path):
        document = self.tab.call("DOM.getDocument", {"depth": 0})["root"]["nodeId"]
        node = self.tab.call("DOM.querySelector", {"nodeId": document, "selector": "#file-in"})["nodeId"]
        self.tab.call("DOM.setFileInputFiles", {"files": [path], "nodeId": node})

    def key(self, key, code=None, text=None):
        codes = {"ArrowRight": 39, "ArrowLeft": 37, "ArrowDown": 40, "ArrowUp": 38, "Escape": 27, "Enter": 13, "Tab": 9, "0": 48, "+": 187, "-": 189}
        params = {"key": key, "code": code or key, "windowsVirtualKeyCode": codes.get(key, ord(key[0].upper())), "nativeVirtualKeyCode": codes.get(key, ord(key[0].upper()))}
        self.tab.call("Input.dispatchKeyEvent", dict(params, type="keyDown", text=text if text is not None else (key if len(key) == 1 else "")))
        self.tab.call("Input.dispatchKeyEvent", dict(params, type="keyUp"))

    def wheel(self, x, y, dy):
        self.tab.call("Input.dispatchMouseEvent", {"type": "mouseWheel", "x": x, "y": y, "deltaX": 0, "deltaY": dy})

    def drag(self, x0, y0, x1, y1, steps=8):
        self.tab.mouse("mouseMoved", x0, y0, button="none")
        self.tab.mouse("mousePressed", x0, y0)
        for i in range(1, steps + 1):
            self.tab.mouse("mouseMoved", x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, buttons=1)
        self.tab.mouse("mouseReleased", x1, y1)

    def type_into(self, selector, text):
        self.ev("(function () { var n = document.querySelector(%s); n.focus(); n.select(); })()" % json.dumps(selector))
        self.tab.call("Input.insertText", {"text": text})

    def shot(self, directory, name):
        self.tab.save_shot(directory, name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the address of the folder that holds mapmaker.html, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save the screenshots in")
    ap.add_argument("--only", default="", help="run only this part: " + ", ".join(PARTS))
    args = ap.parse_args()
    if args.only and args.only not in PARTS:
        print("unknown part %r (%s)" % (args.only, ", ".join(PARTS)))
        return 2
    failures, count = [], [0]

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
    work = tempfile.mkdtemp(prefix="ants_mapmaker_check.")
    junk = os.path.join(work, "words.lvl")
    with open(junk, "wb") as f:
        f.write(b"this is not a map, only some words that make a file of a few dozen bytes. " * 3)
    tiny = os.path.join(REPO, "Original-Ants", "Maps", "TINY.LVL")
    try:
        browser = Browser(path)
        for part in PARTS:
            if args.only and args.only != part:
                continue
            tab = Tab(browser)
            try:
                run_part(part, tab, args, check, tiny, junk)
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
    print("[web map maker] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


def run_part(part, tab, args, check, tiny, junk):
    if part == "desktop":
        print("[web map maker] desktop, 1280 x 800")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        check(page.open() and page.ready(), "the page starts with its map core")
        s = page.state()
        check(s["rows"] == "31" and s["columns"] == "31" and s["tags"] == ["Classic map"] and s["where"] == "Whole map" and not s["mini"], "a 31 x 31 Classic map, whole, no small map: %s" % s)
        page.shot(args.shots, "mapmaker-desktop-start")
        check(page.ev("document.documentElement.scrollWidth <= window.innerWidth && document.documentElement.scrollHeight <= window.innerHeight"), "the page does not scroll")
        page.choose_file(tiny)
        check(wait_for(lambda: page.state()["name"] == "TINY"), "a file from the disk is opened, named after the file")
        s = page.state()
        check(s["check"] == "Works in the original game" and s["tags"] == ["Classic map"], "TINY is a Classic map that works in the original game: %s" % s)
        page.shot(args.shots, "mapmaker-desktop-classic")
        page.choose_file(junk)
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg h2')")) and page.ev("document.querySelector('.dlg h2').textContent") == "This file cannot be opened", "a file that is no map is refused")
        page.key("Escape")
        check(page.state()["name"] == "TINY", "and the open map stays")
        # New map
        page.press_act("new")
        check(wait_for(lambda: page.ev("!!document.querySelector('.kinds')")), "New map opens")
        check(page.ev("document.querySelector('.kind.on .kt').textContent") == "Classic map" and page.ev("document.activeElement.value") == "classic", "with the Classic map chosen and the focus on it")
        check(page.ev("Array.prototype.map.call(document.querySelectorAll('.kx'), function (n) { return n.textContent; }).join('|')") ==
              "Up to 90 rows and 130 columns, 10,000 cells in all.|Up to 512 x 512 cells (262,144 in all). The longest side can be 1,024.", "the two cards give the limits of the core")
        page.type_into("#size-rows", "100")
        page.type_into("#size-columns", "80")
        check(wait_for(lambda: page.ev("!!document.querySelector('.note.bad')")), "100 rows is refused")
        check(page.ev("document.querySelector('.note.bad').textContent").startswith("!A Classic map made now has at most 90 rows (ants cannot enter or cross the 91st row of a taller map, in the original game too)."), "with the checker's sentence")
        check(page.ev("document.querySelector('.live').textContent") == "8,000 cells of 10,000 · the file will be about 99 KB", "and the live line")
        check(page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].disabled"), "Make map is off")
        page.shot(args.shots, "mapmaker-desktop-too-tall")
        page.ev("[].filter.call(document.querySelectorAll('.note .btn'), function (b) { return b.textContent === 'Make it an Extended map'; })[0].click()")
        check(page.ev("document.querySelector('.kind.on .kt').textContent").startswith("Extended map") and not page.ev("!!document.querySelector('.note.bad')"), "the button chooses the Extended map, and 100 x 80 is fine there")
        page.type_into("#size-rows", "513")
        page.type_into("#size-columns", "513")
        check(wait_for(lambda: page.ev("!!document.querySelector('.note.bad')")), "513 x 513 is refused")
        page.type_into("#size-rows", "512")
        page.type_into("#size-columns", "512")
        check(wait_for(lambda: page.ev("document.querySelector('.live').textContent.indexOf('262,144 cells of 262,144') === 0")), "512 x 512 is the most")
        page.shot(args.shots, "mapmaker-desktop-new-extended")
        page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
        check(wait_for(lambda: page.state()["rows"] == "512", 15), "the Extended map of 512 x 512 is made")
        s = page.state()
        check(s["tags"] == ["Extended map", "New version only"] and s["mini"], "with the two tags and its small map: %s" % s)
        # the canvas
        box = page.box("#map")
        cx, cy = box["x"] + box["w"] / 2 + 7, box["y"] + box["h"] / 2 + 7           # (not on the edge between two tiles)
        page.tab.mouse("mouseMoved", cx, cy, button="none")
        time.sleep(0.2)
        before = page.state()["where"]
        page.drag(cx, cy, cx - 200, cy - 120)
        time.sleep(0.2)
        page.tab.mouse("mouseMoved", cx - 200, cy - 120, button="none")
        time.sleep(0.2)
        check(before.startswith("Row") and page.state()["where"] == before, "a drag takes the point under the pointer along with it (%s)" % before)
        scale = page.state()["scale"]
        page.wheel(cx, cy, -300)
        time.sleep(0.3)
        check(page.state()["scale"] > scale, "the wheel zooms in (%s to %s)" % (scale, page.state()["scale"]))
        for _ in range(14):
            page.wheel(cx, cy, 400)
        time.sleep(0.4)
        s = page.state()
        check(not s["mini"] and s["scale"] < 1.5, "and out to the whole 512 x 512 map: the small map goes (tile %s pixels)" % s["scale"])
        page.ev("(function () { var c = document.getElementById('map'), t = performance.now(), n = 0; window.__took = -1; function step() { c.dispatchEvent(new WheelEvent('wheel', {deltaY: n % 2 ? 40 : -40, clientX: 400, clientY: 300, bubbles: true, cancelable: true})); if (++n < 6) requestAnimationFrame(step); else requestAnimationFrame(function () { window.__took = performance.now() - t; }); } step(); })()")
        check(wait_for(lambda: page.ev("window.__took") >= 0) and page.ev("window.__took") < 1500, "six wheel steps over the whole map, each drawn, take %d ms" % page.ev("window.__took"))
        page.shot(args.shots, "mapmaker-desktop-whole-512")
        page.ev("document.getElementById('map').focus()")
        page.key("+")
        page.key("+")
        page.key("+")
        page.key("+")
        time.sleep(0.2)
        check(page.state()["mini"], "plus zooms in from the keyboard")
        page.key("ArrowRight")
        page.key("ArrowDown")
        time.sleep(0.2)
        a = page.state()["where"]
        page.key("ArrowRight")
        time.sleep(0.2)
        b = page.state()["where"]
        check(a.startswith("Row") and b != a, "the arrow keys move the marked square (%s, then %s)" % (a, b))
        page.key("0")
        time.sleep(0.2)
        check(not page.state()["mini"], "zero shows the whole map")
        # checks and download
        page.click_selector(".chkbtn [data-act=check]")
        check(wait_for(lambda: page.ev("!!document.querySelector('.ccard')")), "Check map opens the card")
        check(page.ev("document.querySelector('.ccard .tag').textContent") == "Extended map" and page.ev("document.querySelector('.ccard .badge-nv').textContent") == "New version only", "of an Extended map, with the neutral badge")
        check(page.ev("document.querySelector('.ccard .hl').textContent") == "!Does not work in the new version", "a new map has no hills yet")
        page.shot(args.shots, "mapmaker-desktop-checks")
        page.key("Escape")
        check(not page.ev("!!document.querySelector('.dlg')") and page.ev("document.activeElement.getAttribute('data-act')") == "check", "Escape closes it and the focus is back on the button")
        page.ev("window.__blobs = []; var make = URL.createObjectURL.bind(URL); URL.createObjectURL = function (b) { window.__blobs.push(b); return make(b); }")
        page.press_act("download")
        check(wait_for(lambda: page.ev("window.__blobs.length === 1")), "Download makes a file")
        size = page.ev("window.__blobs[0].size")
        check(size > 3000000 and size < 3300000, "of the 512 x 512 map's bytes (%s)" % size)
    elif part == "phone":
        for label, w, h in (("portrait", 390, 844), ("landscape", 844, 390)):
            print("[web map maker] phone, %s, %d x %d" % (label, w, h))
            tab.emulate(w, h, 2, mobile=True)
            page = Page(tab, args.web)
            check(page.open() and page.ready(), "the page starts")
            check(page.ev("document.documentElement.scrollWidth <= window.innerWidth"), "no sideways scroll")
            page.choose_file(tiny)
            wait_for(lambda: page.state()["name"] == "TINY")
            page.shot(args.shots, "mapmaker-%s-classic" % label)
            bad = page.ev("JSON.stringify([].filter.call(document.querySelectorAll('button, input, a'), function (n) { if (n.offsetParent === null) return false; var b = n.getBoundingClientRect(); return b.right > window.innerWidth + 1 || b.bottom > window.innerHeight + 1 || b.left < -1; }).map(function (n) { return n.textContent || n.id; }))")
            check(bad == "[]", "every visible control is inside the screen (%s)" % bad)
            page.press_act("menu")
            check(wait_for(lambda: page.ev("!!document.querySelector('.menulist')")), "the Menu button opens the menu")
            page.ev("[].filter.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent === 'New'; })[0].click()")
            check(wait_for(lambda: page.ev("!!document.querySelector('.kinds')")), "New map opens from the menu")
            page.ev("document.querySelectorAll('.kind input')[1].click()")
            time.sleep(0.3)
            inside = page.ev("JSON.stringify([].filter.call(document.querySelectorAll('.dlg button, .dlg input'), function (n) { var b = n.getBoundingClientRect(); return n.offsetParent !== null && (b.bottom > window.innerHeight + 1 || b.right > window.innerWidth + 1); }).map(function (n) { return n.textContent || n.id; }))")
            check(inside == "[]", "the dialog's buttons and boxes are on the screen (%s)" % inside)
            page.shot(args.shots, "mapmaker-%s-new-extended" % label)
            page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
            check(wait_for(lambda: page.state()["rows"] == "300"), "an Extended map of 300 x 400 is made")
            # touch: two fingers zoom, one finger pans
            box = page.box("#map")
            cx, cy = box["x"] + box["w"] / 2, box["y"] + box["h"] / 2
            scale = page.state()["scale"]
            fingers = {}

            def touch(kind, points):
                tab.call("Input.dispatchTouchEvent", {"type": kind, "touchPoints": [{"x": x, "y": y, "id": i, "radiusX": 10, "radiusY": 10, "force": .5} for i, (x, y) in points.items()]})
            touch("touchStart", {1: (cx - 40, cy), 2: (cx + 40, cy)})
            for i in range(1, 9):
                touch("touchMove", {1: (cx - 40 - i * 10, cy), 2: (cx + 40 + i * 10, cy)})
            touch("touchEnd", {})
            time.sleep(0.3)
            check(page.state()["scale"] > scale * 1.5, "two fingers moving apart zoom in (%s to %s)" % (scale, page.state()["scale"]))
            center = page.state()["center"]
            touch("touchStart", {1: (cx, cy)})
            for i in range(1, 9):
                touch("touchMove", {1: (cx - i * 12, cy - i * 6)})
            touch("touchEnd", {})
            time.sleep(0.3)
            check(page.state()["center"] != center, "one finger pans (%s to %s)" % (center, page.state()["center"]))
            touch("touchStart", {1: (cx, cy)})
            touch("touchEnd", {})
            time.sleep(0.3)
            check(page.state()["where"].startswith("Row"), "a tap marks a cell (%s)" % page.state()["where"])
            page.press_act("check")
            check(wait_for(lambda: page.ev("!!document.querySelector('.ccard')")), "the checks open")
            page.shot(args.shots, "mapmaker-%s-checks" % label)
            fits = page.ev("(function () { var b = document.querySelector('.dlg').getBoundingClientRect(); return b.left >= -1 && b.right <= window.innerWidth + 1 && b.bottom <= window.innerHeight + 1; })()")
            check(fits, "the card is inside the screen")
    else:
        print("[web map maker] a map core of another interface version")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        old = ("(function () { var real; Object.defineProperty(window, 'createLvlCore', { configurable: true, get: function () { return real && function () { "
               "return real.apply(this, arguments).then(function (m) { m._lvl_interface_version = function () { return 1; }; return m; }); }; }, set: function (v) { real = v; } }); })();")
        check(page.open(old), "the page answers")
        text = page.ev("document.getElementById('boot').textContent")
        check(not page.ev("document.getElementById('boot').hidden") and "out of date" in text and "Reload the page" in text, "it says that the map code is out of date and offers a reload: %r" % text)
        check(page.ev("[].every.call(document.querySelectorAll('[data-act]'), function (b) { return b.disabled; })"), "and no button works")
        page.shot(args.shots, "mapmaker-older-module")


if __name__ == "__main__":
    sys.exit(main())
