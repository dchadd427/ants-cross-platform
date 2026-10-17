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
  * draw     1280 x 800, the tools: a click and a drag of the mouse paint ground and objects (the canvas is read pixel by pixel), only the cells that changed are painted again, a brush of 3, Fill, Erase,
             undo and redo (the buttons and the keys), the Move tool, the space key held down and the middle button pan instead of painting, the check of an opened map is run again after the eraser takes
             a hill away and again after undo, a map that was drawn on is handed over as the core writes it, the page asks before a map that was drawn on is left, and on a 512 x 512 map with the
             processor slowed down six times every frame of a stroke is only a few cells (its time is measured);
  * phone    390 x 844 (portrait) and 844 x 390 (landscape), a touch screen: no sideways scroll, the buttons and the New map dialog are inside the screen, two fingers zoom and one finger pans (Move) or
             paints (a tool), a tap paints a cell, two fingers do not paint;
  * older    a map core of another interface version (the page is given a module that says it is version 1): the page says so and offers a reload, and no button works.

Exit status 0: every check passed; 1: a check failed, or the browser or the page broke down; 3: the check could not be made because the environment is not there (no browser, nothing answers at
the address); 2 is the status of a bad command line.
"""
import argparse
import base64
import json
import os
import struct
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from web_aspect_check import Browser, NotReachable, Tab                      # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PARTS = ("desktop", "draw", "phone", "older")
DIALOGS = []                      # the questions that the browser asked (the page asks before it is left with a map that was drawn on)


def tiny_hill():
    """(row, column) of a cell of a hill in TINY.LVL, read here with struct (the layers hold six bytes a cell; a hill is tile 245 to 248 on layer 2)"""
    with open(os.path.join(REPO, "Original-Ants", "Maps", "TINY.LVL"), "rb") as f:
        data = f.read()
    at = 42 + 11 * (struct.unpack_from("<H", data, 40)[0] + 1)
    rows, columns = struct.unpack_from("<II", data, at)
    layer2 = at + 8 + 6 * rows * columns
    for i in range(rows * columns):
        if 245 <= struct.unpack_from("<H", data, layer2 + 6 * i)[0] <= 248:
            return i // columns, i % columns
    raise RuntimeError("TINY has no hill")


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

    def pixel(self, x, y):
        """the color [r, g, b] of the canvas under the page point (x, y)"""
        return json.loads(self.ev("JSON.stringify((function (x, y) { var c = document.getElementById('map'), b = c.getBoundingClientRect(), k = c.width / b.width, "
                                  "d = c.getContext('2d').getImageData(Math.round((x - b.left) * k), Math.round((y - b.top) * k), 1, 1).data; return [d[0], d[1], d[2]]; })(%s, %s))" % (x, y)))

    def color_of(self, tile1, tile2=0x7FFE):
        """the color that the page's own table gives a tile, as [r, g, b]"""
        h = self.ev("MapTiles.css(%d, %d)" % (tile1, tile2))
        return [int(h[i:i + 2], 16) for i in (1, 3, 5)]

    def paint_info(self):
        """the last frame of the canvas: (way, cells, milliseconds)"""
        way, cells, ms = self.ev("document.getElementById('map').getAttribute('data-paint')").split(":")
        return way, int(cells), float(ms)

    def button_off(self, selector):
        return bool(self.ev("document.querySelector(%s).disabled" % json.dumps(selector)))

    def tool(self, name):
        self.ev("document.querySelector('[data-tool=%s]').click()" % name)

    def tile(self, tile):
        self.ev("document.querySelector('[data-tile=\"%d\"]').click()" % tile)

    def cell_point(self, row, column):
        """where the middle of a cell is on the page, when the whole map is shown (the middle of the canvas is the middle of the map)"""
        box = self.box("#map")
        rows, columns = int(self.ev("document.getElementById('s-rows').textContent")), int(self.ev("document.getElementById('s-cols').textContent"))
        scale = float(self.ev("document.getElementById('map').getAttribute('data-scale')"))
        return box["x"] + box["w"] / 2 + (column + 0.5 - columns / 2) * scale, box["y"] + box["h"] / 2 + (row + 0.5 - rows / 2) * scale

    def ctrl_key(self, key, code, vk, shift=False):
        params = {"key": key, "code": code, "windowsVirtualKeyCode": vk, "nativeVirtualKeyCode": vk, "modifiers": 2 | (8 if shift else 0)}
        self.tab.call("Input.dispatchKeyEvent", dict(params, type="keyDown"))
        self.tab.call("Input.dispatchKeyEvent", dict(params, type="keyUp"))


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
            DIALOGS[:] = []

            def on_dialog(msg, tab=tab):
                # a page that holds a map that was drawn on asks before it is left (a "beforeunload" question): the check answers yes, and notes it
                if msg.get("method") == "Page.javascriptDialogOpening" and msg.get("sessionId") == tab.session:
                    DIALOGS.append(msg["params"].get("type", ""))
                    try:
                        tab.call("Page.handleJavaScriptDialog", {"accept": True})
                    except (RuntimeError, TimeoutError):
                        pass

            tab.dt.handlers.append(on_dialog)
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
        page.tool("move")
        check(page.ev("document.querySelector('[data-tool=move]').getAttribute('aria-pressed')") == "true", "the Move tool is chosen: a drag looks around")
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
    elif part == "draw":
        print("[web map maker] drawing, desktop, 1280 x 800")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        check(page.open() and page.ready(), "the page starts with its map core")
        tools = json.loads(page.ev("JSON.stringify([].map.call(document.querySelectorAll('[data-tool]'), function (b) { return [b.getAttribute('data-tool'), b.disabled, b.getAttribute('aria-pressed')]; }))"))
        check(tools == [["ground", False, "true"], ["objects", False, "false"], ["hills", True, "false"], ["ants", True, "false"], ["food", True, "false"], ["erase", False, "false"], ["move", False, "false"]],
              "Ground is chosen, Hills, Ants and Food wait: %s" % tools)
        check(page.button_off("#undo-btn") and page.button_off("#redo-btn"), "Undo and Redo are off with nothing drawn")
        grass, dirt, water, rock, sand = page.color_of(485), page.color_of(487), page.color_of(476), page.color_of(0, 302), page.color_of(571)
        page.tile(487)
        check(page.ev("document.getElementById('palette-name').textContent") == "Dirt", "the palette names the tile that is chosen")
        a, b = page.cell_point(3, 3), page.cell_point(3, 20)
        check(page.pixel(*a) == grass, "the map starts as grass")
        page.drag(a[0], a[1], b[0], b[1], steps=14)
        time.sleep(0.2)
        mid = page.cell_point(3, 12)
        check(page.pixel(*a) == dirt and page.pixel(*mid) == dirt and page.pixel(*b) == dirt, "a drag of the mouse paints a line of dirt, with no gap")
        check(page.pixel(*page.cell_point(4, 12)) == grass and page.pixel(*page.cell_point(2, 12)) == grass, "and only the cells it went over")
        way, cells, ms = page.paint_info()
        check(way == "cells" and 0 < cells < 80, "only the cells that changed were painted again (%s: %d cells, %.1f ms)" % (way, cells, ms))
        check(not page.button_off("#undo-btn") and page.button_off("#redo-btn"), "Undo is on, Redo is off")
        page.shot(args.shots, "mapmaker-draw-ground")
        page.ev("document.getElementById('undo-btn').click()")
        time.sleep(0.2)
        check(page.pixel(*a) == grass and page.pixel(*mid) == grass and page.pixel(*b) == grass, "Undo takes the whole stroke back at once")
        check(page.button_off("#undo-btn") and not page.button_off("#redo-btn"), "Undo is off again and Redo is on")
        page.ev("document.getElementById('redo-btn').click()")
        time.sleep(0.2)
        check(page.pixel(*mid) == dirt, "Redo puts it back")
        page.ev("document.getElementById('map').focus()")
        page.ctrl_key("z", "KeyZ", 90)
        time.sleep(0.2)
        check(page.pixel(*mid) == grass, "Control+Z undoes")
        page.ctrl_key("z", "KeyZ", 90, shift=True)
        time.sleep(0.2)
        check(page.pixel(*mid) == dirt, "Control+Shift+Z redoes")
        # a brush of 3 and water
        page.tile(476)
        page.ev("document.querySelector('[data-brush=\"3\"]').click()")
        c = page.cell_point(10, 10)
        page.tab.click(*c)
        time.sleep(0.2)
        check(all(page.pixel(*page.cell_point(10 + dr, 10 + dc)) == water for dr in (-1, 0, 1) for dc in (-1, 0, 1)), "a click with a brush of 3 paints 3 x 3 cells")
        check(page.pixel(*page.cell_point(12, 10)) == grass and page.pixel(*page.cell_point(10, 8)) == grass, "and not the cells beside them")
        # the keys choose the tools
        page.ev("document.getElementById('map').focus()")
        page.key("o")
        check(page.ev("document.querySelector('[data-tool=objects]').getAttribute('aria-pressed')") == "true", "the letter O chooses Objects")
        page.ev("document.querySelector('[data-brush=\"1\"]').click()")
        page.tile(302)
        r = page.cell_point(20, 5)
        page.tab.click(*r)
        time.sleep(0.2)
        check(page.pixel(*r) == rock, "a rock is put on the map")
        page.key("e")
        page.tab.click(*r)
        time.sleep(0.2)
        check(page.pixel(*r) == grass, "the eraser takes it away (the letter E)")
        # fill
        page.key("g")
        page.tile(571)
        page.key("b")
        check(page.ev("document.getElementById('fill-btn').getAttribute('aria-pressed')") == "true", "the letter B turns Fill on")
        f = page.cell_point(25, 25)
        page.tab.click(*f)
        time.sleep(0.3)
        check(page.pixel(*f) == sand and page.pixel(*page.cell_point(30, 0)) == sand and page.pixel(*page.cell_point(10, 10)) == water and page.pixel(*mid) == dirt,
              "Fill paints the field of grass round the click and stops at the water and the dirt")
        page.shot(args.shots, "mapmaker-draw-fill")
        page.ctrl_key("z", "KeyZ", 90)
        time.sleep(0.2)
        check(page.pixel(*f) == grass, "and one Undo takes the whole fill back")
        page.key("b")
        # looking around instead of painting
        page.ev("document.querySelector('[data-tile=\"487\"]').click()")
        box = page.box("#map")
        mx, my = box["x"] + box["w"] / 2, box["y"] + box["h"] / 2
        for _ in range(6):
            page.wheel(mx, my, -300)
        time.sleep(0.4)
        before_center = page.state()["center"]
        strokes = page.ev("document.getElementById('undo-btn').disabled")
        page.tool("move")
        page.drag(mx, my, mx - 150, my - 90)
        time.sleep(0.2)
        check(page.state()["center"] != before_center and page.ev("document.getElementById('undo-btn').disabled") == strokes, "the Move tool pans and paints nothing (%s to %s)" % (before_center, page.state()["center"]))
        page.tool("ground")
        center = page.state()["center"]
        page.ev("document.getElementById('map').focus()")
        space = {"key": " ", "code": "Space", "windowsVirtualKeyCode": 32, "nativeVirtualKeyCode": 32}
        page.tab.call("Input.dispatchKeyEvent", dict(space, type="keyDown", text=" "))
        page.drag(mx, my, mx + 150, my + 90)
        page.tab.call("Input.dispatchKeyEvent", dict(space, type="keyUp"))
        time.sleep(0.2)
        check(page.state()["center"] != center and page.ev("document.getElementById('undo-btn').disabled") == strokes, "a drag with the space key held down pans and paints nothing")
        center = page.state()["center"]
        page.tab.mouse("mouseMoved", mx, my, button="none")
        page.tab.mouse("mousePressed", mx, my, button="middle", buttons=4)
        for i in range(1, 9):
            page.tab.mouse("mouseMoved", mx - i * 15, my - i * 8, button="middle", buttons=4)
        page.tab.mouse("mouseReleased", mx - 120, my - 64, button="middle")
        time.sleep(0.2)
        check(page.state()["center"] != center and page.ev("document.getElementById('undo-btn').disabled") == strokes, "the middle button pans and paints nothing")
        # the position line while the mouse is over the map
        page.tab.mouse("mouseMoved", mx + 30, my + 30, button="none")
        time.sleep(0.2)
        check(page.ev("document.getElementById('where').textContent").startswith("Row "), "the position line says Row and column under the mouse: %s" % page.ev("document.getElementById('where').textContent"))
        # a map that is left after it was drawn on: the page asks first
        page.press_act("new")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg h2') && document.querySelector('.dlg h2').textContent === 'Leave this map?'")), "New map asks before it leaves a map that was drawn on")
        page.shot(args.shots, "mapmaker-draw-leave")
        page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Stay here'; })[0].click()")
        check(not page.ev("!!document.querySelector('.dlg')") and not page.button_off("#undo-btn"), "and Stay here keeps it")
        # an opened map: the eraser takes a hill away whole, and the check says so; undo gives it back
        page.choose_file(tiny)                      # (the file box is used directly here: the question above is asked by the Open button)
        wait_for(lambda: page.state()["name"] == "TINY")
        check(wait_for(lambda: page.state()["check"] == "Works in the original game"), "TINY works in the original game")
        hill = tiny_hill()
        page.tool("erase")
        h = page.cell_point(hill[0], hill[1])
        page.tab.click(*h)
        check(wait_for(lambda: page.state()["check"] == "Does not work in the original game", 4), "the eraser takes a hill away: the check is run again by itself (%s)" % page.state()["check"])
        page.click_selector(".chkbtn [data-act=check]")
        check(wait_for(lambda: page.ev("!!document.querySelector('.ccard')")) and "no hill" in page.ev("document.querySelector('.ccard').textContent"), "and the card says which team has no hill")
        page.shot(args.shots, "mapmaker-draw-checks")
        page.key("Escape")
        page.ev("window.__blobs = []; var make = URL.createObjectURL.bind(URL); URL.createObjectURL = function (b) { window.__blobs.push(b); return make(b); }")
        page.press_act("download")
        check(wait_for(lambda: page.ev("window.__blobs.length === 1")), "Download hands over the map that was drawn on")
        head = page.ev("new Promise(function (ok) { window.__blobs[0].slice(0, 8).arrayBuffer().then(function (b) { ok(Array.prototype.join.call(new Uint8Array(b), ',')); }); })")
        check(head.startswith("8,0,0,0,1,0,0,0") and page.ev("window.__blobs[0].size") > 1000, "written by the core: version 8, mode 1 (%s)" % head)
        page.ev("document.getElementById('map').focus()")
        page.ctrl_key("z", "KeyZ", 90)
        check(wait_for(lambda: page.state()["check"] == "Works in the original game", 4), "Undo gives the hill back and the check says it works again")
        # 512 x 512 with the processor slowed down: a stroke is a few cells a frame
        page.press_act("new")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg h2') && document.querySelector('.dlg h2').textContent === 'Leave this map?'")), "a map that changed again after it was downloaded is asked about")
        page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Leave without saving'; })[0].click()")
        check(wait_for(lambda: page.ev("!!document.querySelector('.kinds')")), "Leave without saving goes on to New map")
        page.ev("document.querySelectorAll('.kind input')[1].click()")
        page.type_into("#size-rows", "512")
        page.type_into("#size-columns", "512")
        page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
        check(wait_for(lambda: page.state()["rows"] == "512", 15), "an Extended map of 512 x 512 is made")
        page.ev("document.getElementById('map').focus()")
        page.key("0")
        page.tool("ground")
        page.tile(487)
        time.sleep(0.4)
        page.tab.call("Emulation.setCPUThrottlingRate", {"rate": 6})
        page.ev("window.__frames = []; new MutationObserver(function () { window.__frames.push(document.getElementById('map').getAttribute('data-paint')); }).observe(document.getElementById('map'), {attributes: true, attributeFilter: ['data-paint']})")
        box = page.box("#map")
        mx, my = box["x"] + box["w"] / 2, box["y"] + box["h"] / 2
        page.tab.mouse("mouseMoved", mx - 250, my - 150, button="none")
        page.tab.mouse("mousePressed", mx - 250, my - 150)
        for i in range(1, 61):
            page.tab.mouse("mouseMoved", mx - 250 + i * 8, my - 150 + i * 4 + (i % 7) * 3, buttons=1)
        page.tab.mouse("mouseReleased", mx + 230, my + 90)
        time.sleep(0.5)
        frames = page.ev("window.__frames")
        page.tab.call("Emulation.setCPUThrottlingRate", {"rate": 1})
        parsed = [(f.split(":")[0], int(f.split(":")[1]), float(f.split(":")[2])) for f in frames]
        cells = [ms for way, n, ms in parsed if way == "cells"]
        check(len(parsed) >= 20 and len(cells) >= len(parsed) - 2, "the frames of a stroke on the whole 512 x 512 map are cells, not whole frames (%d frames, %d of cells)" % (len(parsed), len(cells)))
        check(cells and sum(cells) / len(cells) < 12 and max(cells) < 80, "with the processor 6 times slower a frame takes %.1f ms on average and %.1f ms at the most" % (sum(cells) / max(1, len(cells)), max(cells or [0])))
        page.shot(args.shots, "mapmaker-draw-512")
        DIALOGS[:] = []
        page.open()
        check(DIALOGS == ["beforeunload"], "the page asks before it is closed or reloaded with a map that was drawn on (%s)" % DIALOGS)
        DIALOGS[:] = []
        page.open()
        check(DIALOGS == [], "and does not ask when nothing was drawn")
    elif part == "phone":
        for label, w, h in (("portrait", 390, 844), ("landscape", 844, 390), ("narrow", 340, 700)):
            print("[web map maker] phone, %s, %d x %d" % (label, w, h))
            tab.emulate(w, h, 2, mobile=True)
            page = Page(tab, args.web)
            check(page.open() and page.ready(), "the page starts")
            check(page.ev("document.documentElement.scrollWidth <= window.innerWidth"), "no sideways scroll")
            page.choose_file(tiny)
            wait_for(lambda: page.state()["name"] == "TINY")
            page.shot(args.shots, "mapmaker-%s-classic" % label)
            small = page.ev("JSON.stringify([].filter.call(document.querySelectorAll('button:not([disabled]), input:not([hidden])'), function (n) { if (n.offsetParent === null || n.type === 'file') return false; var b = n.getBoundingClientRect(); return b.width < 43.5 || b.height < 43.5; }).map(function (n) { return (n.textContent || n.id || n.getAttribute('data-act')) + ' ' + Math.round(n.getBoundingClientRect().width) + 'x' + Math.round(n.getBoundingClientRect().height); }))")
            check(small == "[]", "every control that works is at least 44 pixels each way (%s)" % small)
            check(page.ev("document.querySelector('.toolnote').offsetParent !== null"), "the note about the tool that is chosen shows")
            check(page.ev("(function () { var b = document.querySelector('.swatches').getBoundingClientRect(); return b.left >= -1 && b.right <= window.innerWidth + 1; })()"), "the row of swatches is inside the screen (it scrolls sideways inside itself)")
            check(page.ev("[].every.call(document.querySelectorAll('.swatch'), function (n) { var b = n.getBoundingClientRect(); return b.width >= 43.5 && b.height >= 43.5; })"), "every swatch is at least 44 pixels each way")
            bad = page.ev("JSON.stringify([].filter.call(document.querySelectorAll('button, input, a'), function (n) { if (n.offsetParent === null || n.closest('.swatches')) return false; var b = n.getBoundingClientRect(); return b.right > window.innerWidth + 1 || b.bottom > window.innerHeight + 1 || b.left < -1; }).map(function (n) { return n.textContent || n.id; }))")
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
            cx, cy = box["x"] + box["w"] * 0.35, box["y"] + box["h"] * 0.7           # (left and low: the small map covers the top right)
            scale = page.state()["scale"]
            fingers = {}

            def touch(kind, points):
                tab.call("Input.dispatchTouchEvent", {"type": kind, "touchPoints": [{"x": x, "y": y, "id": i, "radiusX": 10, "radiusY": 10, "force": .5} for i, (x, y) in points.items()]})
            touch("touchStart", {1: (cx - 40, cy), 2: (cx + 40, cy)})
            for i in range(1, 9):
                touch("touchMove", {1: (cx - 40 - i * 4, cy), 2: (cx + 40 + i * 10, cy)})
            touch("touchEnd", {})
            time.sleep(0.3)
            check(page.state()["scale"] > scale * 1.5, "two fingers moving apart zoom in (%s to %s)" % (scale, page.state()["scale"]))
            page.tool("move")
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
            check(page.button_off("#undo-btn"), "two fingers, and one finger with the Move tool, painted nothing")
            # one finger paints with a tool, and a tap paints a cell
            page.tool("ground")
            page.tile(487)
            dirt, water = page.color_of(487), page.color_of(476)
            bx, by, bw, bh = box["x"], box["y"], box["w"], box["h"]                # (the canvas can be small on a narrow phone: the points are fractions of it, not near the small map)
            x0, y0, x1, y1 = bx + 0.12 * bw, by + 0.3 * bh, bx + 0.5 * bw, by + 0.5 * bh
            touch("touchStart", {1: (x0, y0)})
            for i in range(1, 11):
                touch("touchMove", {1: (x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10)})
            touch("touchEnd", {})
            time.sleep(0.3)
            check(page.pixel(x0, y0) == dirt and page.pixel((x0 + x1) / 2, (y0 + y1) / 2) == dirt and page.pixel(x1, y1) == dirt, "one finger with the Ground tool paints a line of dirt")
            check(not page.button_off("#undo-btn") and page.paint_info()[0] == "cells", "and Undo is on; only cells were painted again")
            page.tile(476)
            tx, ty = bx + 0.3 * bw, by + 0.8 * bh
            touch("touchStart", {1: (tx, ty)})
            touch("touchEnd", {})
            time.sleep(0.3)
            check(page.pixel(tx, ty) == water, "a tap paints a cell with the tile that is chosen")
            page.ev("document.getElementById('undo-btn').click()")
            time.sleep(0.2)
            check(page.pixel(tx, ty) != water and page.pixel(x1, y1) == dirt, "Undo takes the tap back and keeps the line")
            zx, zy = bx + 0.5 * bw, by + 0.8 * bh
            touch("touchStart", {1: (zx - 20, zy), 2: (zx + 20, zy)})
            for i in range(1, 6):
                touch("touchMove", {1: (zx - 20 - i * 6, zy), 2: (zx + 20 + i * 6, zy)})
            touch("touchEnd", {})
            time.sleep(0.2)
            check(page.pixel(zx - 20, zy) != dirt and page.pixel(zx + 20, zy) != dirt and page.pixel(zx, zy) != dirt, "two fingers with a tool zoom and do not paint")
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
