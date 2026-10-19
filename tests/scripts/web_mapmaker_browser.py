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
  * blocks   1280 x 800, Hills, Ants and Food: a click puts a whole hill for the team that is chosen (the canvas is read pixel by pixel), moving it leaves one, the check says "Blue has no hill" until
             the hill is there and "Works in the original game" when the four hills, their ants and a pile are, undo and redo bring the check back, the eraser takes a hill, a team's ants and a pile
             away, a drag looks around and puts nothing, the letters H, A and F choose the tools, and Download hands over what the core writes; then a touch screen (390 x 844): a tap puts, a drag
             and two fingers do not, and the sheet of a phone has the teams, the stepper and the numbers;
  * bigger   1280 x 800, the Bigger tool: the letter K chooses it, its palette is one button and a sheet of 29 objects with the game's names (on a desktop too), a click puts an object whole (the canvas is read
             pixel by pixel, and the marked box is the object's), the eraser takes it whole, undo and redo, an opened map keeps its verdict ("Works in the original game") with an object put on it, an
             Extended map takes one, and Download hands over what the core writes; then a touch screen (390 x 844): a tap puts one whole, a drag and two fingers do not, the sheet has 44 pixel buttons;
  * props    1280 x 800, the properties of the map: the Properties button of the top bar and the summary of the side panel (with its Edit link) open one dialog; the mouse and the keyboard (Tab, the arrow
             keys on the ant types, Enter, Escape) work it; the counter counts bytes ("letters like e acute count double"), a 30 byte description of a Classic map shows the core's sentence and Cut it to 29,
             an Extended map has the larger box that stops at 255; Apply is ONE step of the history (Undo gives the old map back and the summary with it), Cancel and Escape change nothing, the
             check line is run again, and Download hands over the map; then a touch screen (390 x 844): Menu, Properties..., the sheet, the line under the map with its Edit link;
  * convert  1280 x 800, Copy, convert and crop: Copy... makes a copy of the same kind under a new name (a name that is the open map's is refused), "Start an Extended map from this one" makes an Extended
             map and "Make a Classic map from this one" brings it back, the open map is never written over (a download of it is still the file it was); an Extended map of 120 x 400 gives the
             reasons, Crop it to fit opens the screen with the frame (dragged with the mouse, pulled at a handle: wider is shorter by itself, the arrow keys, the readout counts from 0), Use this
             part gives a Classic map of the part; then a touch screen (390 x 844 and 844 x 390): the sheets are inside the screen with their buttons, every control is 44 pixels, the crop screen has
             no sideways scroll and its corners and buttons are 44 pixels;
  * save     1280 x 800 and a touch screen (390 x 844, 844 x 390), Save as a new map and the drafts (the page's own list of maps is stood in for by a fake of /maps): the name offered is the next free
             one; the open map's name, a draft's and a library map's are refused (capitals and blanks do not count) with the one-click next name; a free name is saved as a NEW draft in the browser's
             database (its bytes are the file's) and the saved map takes the screen, so the next Save offers the next number; a name that exists is never written over; drawing is kept in the
             browser's one slot (a reload asks to pick it up or throw it away, and the browser's own question is not asked while the work is safe); the question before a map is left offers Save as a
             new map first; a browser that keeps nothing says so, offers Download, and the browser's question comes back; the sheets are inside the screen and every control is 44 pixels;
  * mine     1280 x 800 and a touch screen (390 x 844, 844 x 390), My maps: the list of the maps saved in this browser (the empty state, a saved Classic and a saved Extended map with their kind tags and the
             line of the check, the filters and their counts); Open puts a saved map on the screen as the working map (a new history, the question about unsaved drawing first, the next Save
             offers the next number), Copy... starts from the saved map, Delete... asks first (Keep it changes nothing, Delete it removes that one saved map from the browser's database);
             Escape and Back to the map return; a browser that keeps nothing says so; the list is inside the screen, every control is 44 pixels, and nothing scrolls sideways;
  * submit   1280 x 800 and a touch screen (390 x 844), Submit for approval and My submissions (the server's door is stood in for by a small fake of /submissions in the page): the script that sends
             maps is loaded when it is first needed; a map that does not work cannot be sent (the reasons, the button off); a name that the game's list has is told, not refused; a map that works
             is sent as ONE call with the file's own bytes, a %20 for a space, no token the first time and the token the next, and "Sent. Waiting for approval." follows; My submissions (the
             second tab of My maps) shows Waiting with Withdraw, Approved, Approved as a new name (renamed by the owner) and Declined with the owner's reason as text, Open my copy puts the copy
             on the screen, Withdraw asks first and withdraws; a door that is off and the pages of nginx are said in plain words; a status link opens the list and loses its fragment; the
             sheets are inside the screen, every control is 44 pixels and nothing scrolls sideways;
  * narrow   desktop windows of 620 to 1280 pixels wide (620 x 700 to 1366 x 768, the bar wraps up to 1200): the top bar's eight buttons are all inside the window, none covers the name box or the kind tags (and My maps has its two tabs), none
             covers the name box, and the page does not scroll sideways;
  * panel    a desktop that is not tall (1280 x 800, 1366 x 768, 1280 x 720), with classic scrollbars of 15 pixels forced on: for every tool the tools column has no scrollbar at all (nothing is cut or
             hidden under one), its steppers' buttons are inside it, and the note under the palette is there when the screen is tall enough;
  * layout   nine phone sizes held upright and sideways (667 x 375 to 320 x 568): every control that works lies inside the card it is in and inside the screen, is at least 44 pixels each way, the map
             keeps a usable height, and the palette of a phone is one button that opens a sheet of tiles with their names (for Hills, Ants and Food: the teams, the steppers); the properties
             sheet is inside the screen with Cancel and Apply in view, its controls are 44 pixels, and what does not fit scrolls inside it;
  * phone    390 x 844 (portrait) and 844 x 390 (landscape), a touch screen: no sideways scroll, the buttons and the New map dialog are inside the screen, two fingers zoom and one finger pans (Move) or
             paints (a tool), a tap paints a cell, two fingers do not paint;
  * sprites  1280 x 800, a touch screen (390 x 844), the game's own pictures (the other parts run with the atlas refused, so that their pixel checks read the flat colors): the atlas comes in, a gravel cell
             shows a texture, the Ground tool has five chips (Gravel, Dirt, Slate, Mud, Water), a second tap on a kind opens "all its tiles" (the plain tile and 3 x 8 edge pieces, the brush row, the gold ring),
             a tile of the sheet paints, Undo takes it back, a map under 10 pixels a cell is drawn in the pictures' average colors, a 512 x 512 map is panned with the processor 4 times slower and frames keep
             the screen's pace; with the atlas refused the page says so in a plain line and draws flat colors; the tools column has no scrollbar; a phone has the sheets with 52 pixel chips and 44 pixel buttons;
  * layer    the kept layer of the sprite canvas against a frame made from nothing, pixel for pixel (also run alone by the mutants of tests/data/mapmaker_sprites_mutants.json);
  * edges    1280 x 800, Fix edges on the original map MEDIUM (60 x 60) in the real browser, with the atlas: the button is on when the lazy scripts are there; the dialog offers the part you can see
             and the whole map, counts the cells (the part is fewer than the whole) and draws before and after from the pictures (the two canvases differ); Cancel changes nothing (the bytes
             that Download hands over and every pixel of the map are as before); Fix edges on the visible part changes only tile words of layer 1 inside it and the check line stays "Works in the
             original game"; a second one on the whole map brings the total to the count that the rule says; ONE Undo takes back each fix (the bytes are the exact ones that went before, and the
             pixels too), Redo makes it again, and the fixed map has nothing left to fix (also run alone by the mutants of tests/data/mapmaker_edges_mutants.json);
  * play     1280 x 800 and a touch screen (390 x 844), Play this map (tests/scripts/web_mapmaker_play_browser.py, which says what it covers): a game is started from the map maker against bots in a tab of its own,
             really played (the roster, the ants, the clock on the screen), with the open file's own bytes; the way back asks once and closes the tab; the map comes back as it was, history included;
             the refusals are plain words
  * older    a map core of another interface version (the page is given a module that says it is version 3, the one before the crop): the page says so and offers a reload, and no button works.

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
PARTS = ("desktop", "draw", "blocks", "bigger", "props", "convert", "save", "mine", "submit", "narrow", "panel", "layout", "phone", "older", "sprites", "layer", "edges", "play")
WITH_ATLAS = ("panel", "sprites", "layer", "edges", "play")                  # the other parts read the canvas pixel by pixel and compare colors: they run with the atlas refused, so the page draws its flat colors (the "sprites" part has the pictures and the refusal)
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


def tiny_free(size=6):
    """(row, column) of the top left cell of a square of `size` cells in TINY.LVL where layer 2 is empty and no cell of layer 1 is solid, read here with struct"""
    with open(os.path.join(REPO, "Original-Ants", "Maps", "TINY.LVL"), "rb") as f:
        data = f.read()
    at = 42 + 11 * (struct.unpack_from("<H", data, 40)[0] + 1)
    rows, columns = struct.unpack_from("<II", data, at)
    layer1 = at + 8
    layer2 = layer1 + 6 * rows * columns
    blocks = layer2 + 6 * rows * columns
    taken = set()
    count = struct.unpack_from("<H", data, blocks)[0]
    for i in range(count):                              # (the starting ants and plants of block 1)
        _, r0, c0 = struct.unpack_from("<HHH", data, blocks + 2 + 6 * i)
        taken.add((r0, c0))
    at = blocks + 2 + 6 * count
    for i in range(struct.unpack_from("<H", data, at)[0]):          # (and the food piles of block 2)
        r0, c0, _, _, stages = struct.unpack_from("<HHHHH", data, at + 2)
        taken.add((r0, c0))
        at += 10 + 4 * stages
    for r in range(3, rows - size - 1):
        for c in range(3, columns - size - 1):
            if any((r + i, c + j) in taken for i in range(-1, size + 1) for j in range(-1, size + 1)):
                continue
            if all(struct.unpack_from("<H", data, layer2 + 6 * ((r + i) * columns + c + j))[0] == 0x7FFE and not struct.unpack_from("<H", data, layer1 + 6 * ((r + i) * columns + c + j) + 2)[0] & 1
                   for i in range(size) for j in range(size)):
                return r, c
    raise RuntimeError("TINY has no free square")


def idb_read(page, store, key):
    """what the page's own database holds under a key, read from the page: None, or {name, kind, length} (the bytes are not carried over, only their length)"""
    page.ev("window.__idb = undefined; (function () { var q = indexedDB.open('ants-mapmaker', 1); q.onerror = function () { window.__idb = null; }; q.onsuccess = function () { var g = q.result.transaction(%s, 'readonly').objectStore(%s).get(%s); "
            "g.onsuccess = function () { var r = g.result; window.__idb = r ? {name: r.name, kind: r.kind, length: r.bytes.length} : null; }; g.onerror = function () { window.__idb = null; }; }; })()" % (json.dumps(store), json.dumps(store), json.dumps(key)))
    wait_for(lambda: page.ev("window.__idb !== undefined"), 5, 0.05)
    return page.ev("window.__idb") or None


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
        return bool(self.ev("!!document.getElementById('boot') && document.getElementById('boot').hidden && !document.querySelector('[data-act=check]').disabled"))

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
        """choose a tile's chip; a kind of ground that is chosen already is left alone (a second tap on it opens the sheet of its tiles)"""
        self.ev("(function () { var b = document.querySelector('[data-tile=\"%d\"]'); if (!(b.classList.contains('gk') && b.getAttribute('aria-checked') === 'true')) b.click(); })()" % tile)

    def tap(self, tile):
        """a tap on a tile's chip, whatever is chosen (on a kind of ground that is chosen it opens the sheet of its tiles)"""
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
            origin = "/".join(args.web.split("/")[:3])
            tab.call("Storage.clearDataForOrigin", {"origin": origin, "storageTypes": "indexeddb"})          # (the work that an earlier part left in the browser's database is not this part's to pick up)

            def on_dialog(msg, tab=tab):
                # a page that holds a map that was drawn on asks before it is left (a "beforeunload" question): the check answers yes, and notes it
                if msg.get("method") == "Page.javascriptDialogOpening" and msg.get("sessionId") == tab.session:
                    DIALOGS.append(msg["params"].get("type", ""))
                    try:
                        tab.call("Page.handleJavaScriptDialog", {"accept": True})
                    except (RuntimeError, TimeoutError):
                        pass

            tab.dt.handlers.append(on_dialog)
            if part not in WITH_ATLAS:
                tab.call("Network.enable")
                tab.call("Network.setBlockedURLs", {"urls": ["*mapmaker_atlas/*"]})
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


def spread(page, row, column, size=8):
    """how many different colors the canvas shows in a small square of pixels inside a cell (a flat color: one; a picture: many)"""
    x, y = page.cell_point(row, column)
    return len({tuple(page.pixel(x + dx, y + dy)) for dx in range(-size // 2, size // 2, 2) for dy in range(-size // 2, size // 2, 2)})


def make_extended(page, rows, columns):
    page.press_act("new")
    wait_for(lambda: page.ev("!!document.querySelector('.kinds')"))
    page.ev("document.querySelectorAll('.kind input')[1].click()")
    page.type_into("#size-rows", str(rows))
    page.type_into("#size-columns", str(columns))
    page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
    return wait_for(lambda: page.state()["rows"] == str(rows), 15)


def run_sprites(tab, args, check):
    """the game's own pictures (docs/MAP_EDITOR.md, "Sprites"): the atlas loads, the canvas draws pictures from 10 pixels a cell up and average colors below, the Ground tool has five kinds and a sheet of every
    tile of a kind, the page works and says so when the atlas does not load, a phone has the sheets, the tools column has no scrollbar, and a pan of a big map does not stutter"""
    print("[web map maker] sprites, 1280 x 800")
    tab.emulate(1280, 800, 1)
    page = Page(tab, args.web)
    check(page.open() and page.ready(), "the page starts")
    check(wait_for(lambda: page.ev("document.querySelectorAll('#palette .gk canvas').length") == 5, 10), "the atlas comes in: the five kinds of ground show their pictures")
    check(page.ev("[].map.call(document.querySelectorAll('#palette .gk'), function (b) { return b.textContent; }).join('|')") == "Gravel|Dirt|Slate|Mud|Water", "Gravel, Dirt, Slate, Mud, Water: five chips, no grass, no sand")
    check(page.ev("document.getElementById('atlas-note').hidden") is True, "no notice when the pictures are there")
    page.choose_file(os.path.join(REPO, "Original-Ants", "Maps", "TREASURE.LVL"))
    check(wait_for(lambda: page.state()["name"] == "TREASURE"), "Treasure is opened")
    time.sleep(0.5)
    s = page.state()
    check(s["scale"] >= 10, "the whole map is 10 pixels a cell or more (%s)" % s["scale"])
    gravel_cell = (5, 5)
    check(spread(page, *gravel_cell) >= 4, "a gravel cell shows the picture (a texture, not one flat color: %d colors)" % spread(page, *gravel_cell))
    page.shot(args.shots, "mapmaker-sprites-desktop")
    # the Ground tool: a kind's chip chooses it, the same chip again opens the sheet of its tiles
    page.tap(487)
    check(page.ev("document.getElementById('palette-name').textContent") == "Dirt: tap it again for all its tiles" and not page.ev("!!document.querySelector('.dlg')"), "the first tap on a kind chooses its plain tile")
    page.tap(487)
    check(wait_for(lambda: page.ev("!!document.querySelector('.dlg.ksheetbox')")) and page.ev("document.querySelector('.dlg h2').textContent") == "Dirt: all its tiles", "the second tap opens Dirt: all its tiles")
    got = json.loads(page.ev("""JSON.stringify((function () {
        var d = document.querySelector('.dlg'), t = d.querySelectorAll('.gt:not(.mid)'), g = d.querySelectorAll('.kgroup h3');
        return {chips: t.length, pictures: d.querySelectorAll('.gt canvas').length, heads: [].map.call(g, function (h) { return h.textContent; }), brush: [].map.call(d.querySelectorAll('.kbrush button'), function (b) { return b.textContent; }).join(''),
                small: [].filter.call(t, function (b) { var r = b.querySelector('i').getBoundingClientRect(); return r.width < 51 || r.height < 51; }).length, on: d.querySelectorAll('.gt[aria-checked=true]').length};
    })())"""))
    check(got["chips"] == 1 + 3 * 8 and got["pictures"] == got["chips"] + 3 and got["heads"] == ["Plain dirt", "Dirt next to Gravel", "Dirt next to Slate", "Dirt next to Mud"], "the sheet: the plain tile and 3 x 8 edge pieces, each with its picture: %s" % got)
    check(got["brush"] == "1359" and got["small"] == 0 and got["on"] == 1, "the brush row, chips of 52 pixels, and the chosen tile has the gold ring (%s)" % got)
    page.shot(args.shots, "mapmaker-sprites-sheet-dirt")
    page.ev("document.querySelector('.dlg .gt[data-tile=\"541\"]').click()")              # dg03a: dirt next to gravel, the gravel on the north side
    check(page.ev("document.querySelector('.dlg .gt[aria-checked=true]').getAttribute('data-tile')") == "541", "a tile of the sheet is chosen")
    page.key("Escape")
    check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")), "Escape closes the sheet")
    check(page.ev("document.getElementById('palette-name').textContent") == "Dirt next to gravel: north edge (dg03a)", "the line under the palette names it: %s" % page.ev("document.getElementById('palette-name').textContent"))
    before = spread(page, *gravel_cell)
    a = page.cell_point(*gravel_cell)
    page.tab.click(*a)
    time.sleep(0.4)
    check(page.ev("document.getElementById('undo-btn').disabled") is False, "a tap paints the chosen tile (Undo is on)")
    page.ctrl_key("z", "KeyZ", 90)
    time.sleep(0.3)
    check(page.ev("document.getElementById('undo-btn').disabled") is True, "and Undo takes it back")
    # the Bigger tool: the picture of the object follows the pointer
    page.tool("bigger")
    bx = page.box("#map")
    page.tab.mouse("mouseMoved", bx["x"] + 300, bx["y"] + 300, button="none")
    time.sleep(0.4)
    page.shot(args.shots, "mapmaker-sprites-bigger-ghost")
    # a map that is whole at under 10 pixels a cell is drawn in the pictures' average colors
    page.tool("ground")
    page.tile(485)
    check(make_extended(page, 100, 100), "an Extended map of 100 x 100 is made")
    time.sleep(0.4)
    s = page.state()
    check(s["scale"] < 10, "whole, it is under 10 pixels a cell (%s)" % s["scale"])
    check(spread(page, 50, 50, 4) == 1 and page.pixel(*page.cell_point(50, 50)) == page.color_of(485), "so it is drawn in the average color of the picture, one color for a gravel cell")
    page.shot(args.shots, "mapmaker-sprites-zoomed-out")
    page.ev("document.querySelector('[data-act=zoom-in]').click()"); page.ev("document.querySelector('[data-act=zoom-in]').click()")
    time.sleep(0.4)
    s = page.state()
    check(s["scale"] >= 10 and spread(page, 50, 50) >= 3, "zoomed in again it draws the pictures (%s pixels a cell)" % s["scale"])
    # a map of 512 x 512, panned: frames come at the screen's pace even with the processor slowed down 4 times
    check(make_extended(page, 512, 512), "an Extended map of 512 x 512 is made")
    page.ev("document.getElementById('map').focus()")
    page.ev("window.__fr = []; (function () { var l = performance.now(); function f(n) { window.__fr.push(n - l); l = n; requestAnimationFrame(f); } requestAnimationFrame(f); })()")
    page.tab.call("Emulation.setCPUThrottlingRate", {"rate": 4})
    cx, cy = bx["x"] + bx["w"] / 2, bx["y"] + bx["h"] / 2
    for i in range(60):
        page.wheel(cx, cy, -40)
        if page.state()["scale"] >= 14:
            break
        time.sleep(0.03)
    time.sleep(0.4)
    page.ev("window.__fr = []")
    page.tab.mouse("mouseMoved", cx, cy, button="none")
    page.tab.mouse("mousePressed", cx, cy)
    for i in range(1, 61):
        page.tab.mouse("mouseMoved", cx - i * 5, cy - i * 2, buttons=1)
        time.sleep(0.016)
    page.tab.mouse("mouseReleased", cx - 300, cy - 120)
    frames = sorted(json.loads(page.ev("JSON.stringify(window.__fr)")))
    page.tab.call("Emulation.setCPUThrottlingRate", {"rate": 1})
    s = page.state()
    check(s["scale"] >= 10 and len(frames) >= 30 and frames[len(frames) // 2] <= 34 and frames[int(len(frames) * 0.9)] <= 100, "a pan of the 512 x 512 map at %.1f pixels a cell with the processor 4 times slower: %d frames, median %.1f ms, 90%% under %.1f ms" % (s["scale"], len(frames), frames[len(frames) // 2], frames[int(len(frames) * 0.9)]))
    # the same page when the atlas does not load: flat colors, a plain notice, nothing else changes
    print("[web map maker] sprites, the atlas refused")
    tab2 = Tab(tab_browser(tab))
    try:
        tab2.emulate(1280, 800, 1)
        tab2.call("Network.enable")
        tab2.call("Network.setBlockedURLs", {"urls": ["*mapmaker_atlas/*"]})
        flat = Page(tab2, args.web)
        check(flat.open() and flat.ready(), "the page starts without its atlas")
        check(wait_for(lambda: flat.ev("document.getElementById('atlas-note').hidden") is False, 10), "a plain notice says the pictures could not be loaded: %s" % flat.ev("document.getElementById('atlas-note').textContent"))
        flat.choose_file(os.path.join(REPO, "Original-Ants", "Maps", "TREASURE.LVL"))
        wait_for(lambda: flat.state()["name"] == "TREASURE")
        time.sleep(0.4)
        check(spread(flat, 5, 5, 4) == 1 and flat.pixel(*flat.cell_point(5, 5)) == flat.color_of(485), "the map is drawn in flat colors")
        check(flat.ev("document.querySelectorAll('#palette .gk').length") == 5 and flat.ev("document.querySelectorAll('#palette .gk canvas').length") == 0, "the five kinds are there as flat chips")
        flat.tap(487); flat.tap(487)
        check(wait_for(lambda: flat.ev("!!document.querySelector('.dlg.ksheetbox')")), "and the sheet of a kind opens, with flat tiles")
        flat.shot(args.shots, "mapmaker-sprites-atlas-refused")
    finally:
        tab2.close()
    run_layer(page, check)
    # the tools column has no scrollbar with the pictures (the other parts run without them)
    for w, h in ((1280, 800), (1366, 768), (1280, 720)):
        tab.emulate(w, h, 1)
        page = Page(tab, args.web)
        page.open()
        page.ready()
        wait_for(lambda: page.ev("document.querySelectorAll('#palette .gk canvas').length") == 5, 10)
        page.ev("(function () { var s = document.createElement('style'); s.textContent = '*::-webkit-scrollbar { width: 15px; height: 15px; }'; document.head.appendChild(s); })()")
        found = json.loads(page.ev("JSON.stringify((function () { var p = document.querySelector('.tools'); return {tall: p.scrollHeight > p.clientHeight, wide: p.scrollWidth > p.clientWidth}; })())"))
        check(not found["tall"] and not found["wide"], "%d x %d with the pictures: the tools column has no scrollbar (%s)" % (w, h, found))
    # a phone
    print("[web map maker] sprites, 390 x 844, a touch screen")
    tab.emulate(390, 844, 2, mobile=True)
    page = Page(tab, args.web)
    page.open()
    page.ready()
    wait_for(lambda: page.ev("document.querySelectorAll('#palette .gk canvas').length") == 5, 10)
    page.choose_file(os.path.join(REPO, "Original-Ants", "Maps", "TREASURE.LVL"))
    wait_for(lambda: page.state()["name"] == "TREASURE")
    page.shot(args.shots, "mapmaker-sprites-phone")
    page.click_selector("#tile-btn")
    check(wait_for(lambda: page.ev("!!document.querySelector('.dlg')")) and page.ev("document.querySelectorAll('.dlg .pick canvas').length") == 5, "the phone's one button opens the sheet of five kinds, with their pictures")
    check(page.ev("[].every.call(document.querySelectorAll('.dlg .pick'), function (b) { return b.getBoundingClientRect().height >= 44; })"), "every kind is a target of 44 pixels or more")
    page.shot(args.shots, "mapmaker-sprites-phone-ground")
    page.ev("document.querySelector('.dlg .pick[aria-checked=true]').click()")
    check(wait_for(lambda: page.ev("!!document.querySelector('.dlg.ksheetbox')")), "the chosen kind again opens its sheet")
    got = json.loads(page.ev("""JSON.stringify((function () {
        var d = document.querySelector('.dlg').getBoundingClientRect(), i = document.querySelector('.dlg-in'), t = document.querySelectorAll('.dlg .gt:not(.mid)'),
            b = [].map.call(document.querySelectorAll('.dlg .kbrush button'), function (n) { var r = n.getBoundingClientRect(); return Math.min(r.width, r.height); }),
            l = [].map.call(document.querySelectorAll('.dlg .gl'), function (n) { return n.getBoundingClientRect().height; });
        return {inside: d.top >= 0 && d.bottom <= window.innerHeight + 1 && d.left >= -1 && d.right <= window.innerWidth + 1, scrolls: i.scrollHeight > i.clientHeight, wide: document.documentElement.scrollWidth > window.innerWidth,
                chip: Math.min.apply(null, [].map.call(t, function (n) { return n.querySelector('i').getBoundingClientRect().width; })), brush: Math.min.apply(null, b), look: Math.min.apply(null, l)};
    })())"""))
    check(got["inside"] and got["scrolls"] and not got["wide"] and got["chip"] >= 52 and got["brush"] >= 44 and got["look"] >= 44, "the phone's sheet is inside the screen and scrolls, chips of 52 pixels, buttons of 44 or more (%s)" % got)
    page.shot(args.shots, "mapmaker-sprites-phone-sheet")


LAYER_JS = r"""
(function (b64) {
  var bin = atob(b64), bytes = new Uint8Array(bin.length), i;
  for (i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  var file = MapFile.parse(bytes), W = 600, H = 400;
  function make() { var c = document.createElement('canvas'); c.width = W; c.height = H; return c; }
  function rig(done) {
    var s = MapSprites.create({ doc: document, win: window, onReady: function () { done(s); }, onFail: function () { done(null); }, onChange: function () {} });
    s.load();
  }
  function draw(s, c, view) { var ctx = c.getContext('2d'); ctx.setTransform(1, 0, 0, 1, 0, 0); MapView.draw(ctx, view, file, W, H, { sprites: s, dpr: 1, scratch: make() }); }
  function diff(a, b) { var x = a.getContext('2d').getImageData(0, 0, W, H).data, y = b.getContext('2d').getImageData(0, 0, W, H).data, n = 0, k, pts = [], box = [W, H, -1, -1]; for (k = 0; k < x.length; k += 4) if (x[k] !== y[k] || x[k + 1] !== y[k + 1] || x[k + 2] !== y[k + 2]) { n++; if (pts.length < 12 && n % 61 === 1) pts.push([(k / 4) % W, Math.floor(k / 4 / W), x[k], y[k]]); var px = (k / 4) % W, py = Math.floor(k / 4 / W); box = [Math.min(box[0], px), Math.min(box[1], py), Math.max(box[2], px), Math.max(box[3], py)]; } return n ? [n, box, pts] : 0; }
  function set(layer, r, c, t, f) { var at = (layer === 1 ? file.layer1 : file.layer2) + 6 * (r * file.columns + c); bytes[at] = t & 255; bytes[at + 1] = t >> 8; if (f !== undefined) { bytes[at + 2] = f; } }
  return new Promise(function (resolve) {
    rig(function (kept) {
      rig(function (fresh) {
        if (!kept || !fresh) return resolve({ error: 'no atlas' });
        var out = [], T = 20, view = { scale: T, cx: 30, cy: 30 }, ck = make(), cf = make();
        function compare(name, v) { draw(kept, ck, v); fresh.reset(); draw(fresh, cf, v); var dd = diff(ck, cf); out.push([name, dd]); if (dd && !window.__cv) window.__cv = [ck.toDataURL(), cf.toDataURL()]; }
        // the toys that are fetched alone must be in before the frames are compared (they come when they come): look at the whole map from every side once, wait, start again
        [[30, 30], [15, 15], [15, 45], [45, 15], [45, 45]].forEach(function (c) { draw(fresh, cf, { scale: T, cx: c[0], cy: c[1] }); draw(kept, ck, { scale: T, cx: c[0], cy: c[1] }); });
        draw(fresh, cf, { scale: 6, cx: 30, cy: 30 });
        setTimeout(function () {
          kept.reset(); draw(kept, ck, view);
          next();
        }, 2500);
        function next() {
        compare('start', view);
        var steps = [[3, 5], [-4, 2], [T, 0], [0, 3 * T], [-2 * T, T], [1, -1], [37, 29], [-13, -41]];
        steps.forEach(function (d) { view = { scale: T, cx: view.cx + d[0] / T, cy: view.cy + d[1] / T }; compare('pan ' + d, view); });
        var shapes = [[[20, 20]], [[20, 21], [20, 22], [20, 23], [20, 24]], [[10, 10], [10, 11], [11, 10], [11, 11], [12, 10], [12, 11]], [[0, 0]], [[0, 5], [1, 5]], [[30, 30], [30, 31], [31, 30], [31, 31]], [[22, 40], [23, 40]], [[44, 20], [44, 21]]];
        shapes.forEach(function (cells, n) {
          var list = [];
          cells.forEach(function (rc) { set(1, rc[0], rc[1], n % 2 ? 476 : 571); list.push(rc[0] * file.columns + rc[1]); });
          kept.invalidate(list);
          compare('stroke ' + JSON.stringify(cells), view);
        });
        var rocks = 0;
        for (var r = 0; r < file.rows; r++) for (var c = 0; c < file.columns && rocks < 6; c++) { var at = file.layer2 + 6 * (r * file.columns + c); if ((bytes[at] | (bytes[at + 1] << 8)) !== 0x7FFE && (bytes[at + 2] & 1) && r > 8 && r < 50) { set(2, r, c, 0x7FFE, 0); kept.invalidate([r * file.columns + c]); compare('erase object at ' + r + ',' + c, view); rocks++; } }
        set(2, 25, 25, 1324, 1); kept.invalidate([25 * file.columns + 25]); compare('put a big grass at 25,25', view);
        resolve({ out: out });
        }
      });
    });
  });
})
"""


def run_layer(page, check):
    """the kept layer of the sprite canvas against a fresh whole frame, pixel for pixel, in the real browser: after every pan (a few pixels, whole cells, both ways) and every stroke (several shapes, at the map's edge, an object taken away or put) the picture is the same as a frame that is made from nothing"""
    with open(os.path.join(REPO, "Original-Ants", "Maps", "TREASURE.LVL"), "rb") as f:
        b64 = base64.b64encode(f.read()).decode()
    page.ev("window.__layer = undefined; (" + LAYER_JS.strip() + ")(%s).then(function (r) { window.__layer = r; })" % json.dumps(b64))
    wait_for(lambda: page.ev("window.__layer !== undefined"), 60, 0.2)
    got = json.loads(page.ev("JSON.stringify(window.__layer)"))
    check("out" in got and len(got["out"]) >= 15, "the layer check ran (%s)" % (got.get("error") or len(got.get("out", []))))
    bad = [o for o in got.get("out", []) if o[1]]
    check(not bad, "the kept layer is pixel for pixel the frame that is made from nothing, after every pan and stroke (%d of %d differ: %s)" % (len(bad), len(got.get("out", [])), bad[:4]))


EDGES_JS = r"""
window.__blobs = []; (function () { var make = URL.createObjectURL.bind(URL); URL.createObjectURL = function (b) { window.__blobs.push(b); return make(b); }; })();
window.__bytes = function () {                    // Download, and what it handed over: the digest and the bytes (base64) of the map
    var n = window.__blobs.length; document.querySelector('[data-act=download]').click();
    return new Promise(function (ok) {
        (function wait() { if (window.__blobs.length === n) return setTimeout(wait, 20); window.__blobs[n].arrayBuffer().then(function (buf) {
            var b = new Uint8Array(buf), s = '', i; for (i = 0; i < b.length; i += 4096) s += String.fromCharCode.apply(null, b.subarray(i, i + 4096));
            var f = MapFile.parse(b);
            crypto.subtle.digest('SHA-256', buf).then(function (d) { ok({ sha: Array.prototype.map.call(new Uint8Array(d), function (x) { return ('0' + x.toString(16)).slice(-2); }).join(''), b64: btoa(s), layer1: f.layer1, dictAt: f.dictAt, names: f.names, rows: f.rows, columns: f.columns, left: MapEdges.plan(f, null, MapGround).count }); });
        }); })();
    });
};
window.__pixels = function (id) {                  // a digest of every pixel of a canvas (FNV-1a)
    var c = document.getElementById(id), d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data, h = 2166136261, i;
    for (i = 0; i < d.length; i++) h = Math.imul(h ^ d[i], 16777619) >>> 0;
    return h + ':' + c.width + 'x' + c.height;
};
"""


def edges_bytes(page):
    page.ev("window.__got = undefined; window.__bytes().then(function (r) { window.__got = r; })")
    wait_for(lambda: page.ev("window.__got !== undefined"), 20, 0.05)
    got = json.loads(page.ev("JSON.stringify(window.__got)"))
    got["bytes"] = base64.b64decode(got["b64"])
    return got


def still_pixels(page):
    """a digest of the map's pixels once they have stopped changing (a toy's picture that was fetched alone may arrive late)"""
    last = None
    for _ in range(40):
        now = page.ev("window.__pixels('map')")
        if now == last:
            return now
        last = now
        time.sleep(0.25)
    return last


def edges_open(page, whole=False):
    page.ev("document.getElementById('edges-btn').click()")
    ok = wait_for(lambda: page.ev("!!document.querySelector('.dlg.edges')"), 8)
    if ok and whole:
        page.ev("document.querySelector('.dlg.edges input[value=all]').click()")
    return ok


def edges_count(page):
    text = page.ev("document.querySelectorAll('.dlg.edges figcaption')[1].textContent")
    digits = "".join(ch for ch in text if ch.isdigit())
    return int(digits) if digits else 0


def run_edges(page, tab, args, check):
    """Fix edges (docs/MAP_EDITOR.md, "Fix edges") on a real map in the real browser: the real bytes (what Download hands over), the real pixels (every pixel of the canvas), one Undo"""
    print("[web map maker] Fix edges, 1280 x 800")
    check(page.open() and page.ready(), "the page starts")
    check(wait_for(lambda: page.ev("!!window.MapEdges && !!window.MapGround && !document.getElementById('edges-btn').disabled"), 15), "the Fix edges button is on once the lazy scripts are there")
    page.ev(EDGES_JS)
    page.choose_file(os.path.join(REPO, "Original-Ants", "Maps", "MEDIUM.LVL"))
    check(wait_for(lambda: page.state()["name"] == "MEDIUM"), "Medium is opened")
    check(wait_for(lambda: spread(page, 5, 5) >= 4, 10), "its pictures are drawn")
    first = edges_bytes(page)
    check(first["left"] == 147, "the rule has 147 cells to fix on the original map (%s)" % first["left"])
    pixels = still_pixels(page)
    check(page.button_off("#undo-btn"), "nothing to undo yet")
    # the dialog on the whole map, then Cancel
    check(edges_open(page), "the Fix edges dialog opens")
    check(page.ev("document.querySelector('.dlg.edges h2').textContent") == "Fix edges" and page.ev("document.querySelector('.dlg.edges input:checked').value") == "part", "titled Fix edges, with the part you can see chosen")
    check(edges_count(page) == 147, "the whole map is on the screen, so the part is the 147 cells (%d)" % edges_count(page))
    differ = page.ev("(function () { var c = document.querySelectorAll('.dlg.edges canvas'), d = [0, 1].map(function (i) { return c[i].getContext('2d').getImageData(0, 0, c[i].width, c[i].height).data; }), n = 0, i; for (i = 0; i < d[0].length; i += 4) if (d[0][i] !== d[1][i] || d[0][i + 1] !== d[1][i + 1] || d[0][i + 2] !== d[1][i + 2]) n++; var seen = {}; for (i = 0; i < d[0].length; i += 4) seen[d[0][i] + ',' + d[0][i + 1] + ',' + d[0][i + 2]] = 1; return [n, c[0].width, c[0].height, c[1].width, c[1].height, Object.keys(seen).length]; })()")
    check(differ[0] > 500 and differ[1:3] == differ[3:5][:2] and differ[5] >= 300, "before and after are drawn from the pictures (%d colors: not flat) and differ in %d pixels, in the same size" % (differ[5], differ[0]))
    page.shot(args.shots, "mapmaker-fix-edges-dialog")
    page.ev("[].filter.call(document.querySelectorAll('.dlg.edges .btn'), function (b) { return b.textContent === 'Cancel'; })[0].click()")
    check(not page.ev("!!document.querySelector('.dlg')") and still_pixels(page) == pixels and page.button_off("#undo-btn"), "Cancel closes it and changes nothing (the pixels, the history)")
    check(edges_bytes(page)["sha"] == first["sha"], "and the bytes are the same")
    # zoom in so that only a part is on the screen: the part is fewer cells than the whole
    box = page.box("#map")
    for _ in range(4):
        page.wheel(box["x"] + box["w"] * 0.3, box["y"] + box["h"] * 0.3, -300)
    time.sleep(0.4)
    s = page.state()
    check(s["scale"] > 20 and s["mini"], "zoomed in: only a part of the map is on the screen (%s pixels a cell)" % s["scale"])
    before_part = still_pixels(page)
    check(edges_open(page), "the dialog opens again")
    part = edges_count(page)
    check(page.ev("document.querySelector('.dlg.edges input:checked').value") == "part", "with the part you can see chosen")
    page.ev("document.querySelector('.dlg.edges input[value=all]').click()")
    whole = edges_count(page)
    page.ev("document.querySelector('.dlg.edges input[value=part]').click()")
    check(0 < part < whole == 147, "the part is fewer cells than the whole map (%d of %d)" % (part, whole))
    page.shot(args.shots, "mapmaker-fix-edges-part")
    page.ev("[].filter.call(document.querySelectorAll('.dlg.edges .btn'), function (b) { return b.textContent === 'Fix edges'; })[0].click()")
    check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")), "Fix edges closes the dialog")
    done = edges_bytes(page)
    a, b, w = first["bytes"], done["bytes"], 6
    # Download writes the map as the core does (the garbage after a name in the dictionary becomes zeros), so the dictionary is looked at by its names and the rest byte for byte
    same_head = a[:first["dictAt"]] == b[:first["dictAt"]]
    ta, tb = a[first["layer1"] - 8:], b[done["layer1"] - 8:]
    check(same_head and len(ta) == len(tb), "everything before the dictionary is the same bytes, and the part from the grid on is the same length")
    changed = [i - 8 for i in range(len(ta)) if ta[i] != tb[i]] if len(ta) == len(tb) else [-1]
    words = sorted({i // w for i in changed})
    check(all(0 <= i < w * first["rows"] * first["columns"] and i % w < 2 for i in changed), "only the tile words of layer 1 changed (flags, solid bits, layer 2 and the rest are the same bytes)")
    entry = lambda bb, info, k: bb[info["dictAt"] + 2 + 11 * k: info["dictAt"] + 13 + 11 * k].split(b"\0")[0].decode("latin1")
    old_names = [entry(a, first, k) for k in range(first["names"])]
    new_names = [entry(b, done, k) for k in range(done["names"])]
    put = [new_names[struct.unpack("<H", tb[8 + 6 * i: 10 + 6 * i])[0]] for i in words]
    check(all(n == o or o == "." for n, o in zip(new_names, old_names)) and all(n != "." for n in put), "the dictionary keeps its names, and every piece that was put has its name there (%d new names: the original draws no tile named \".\")" % sum(1 for n, o in zip(new_names, old_names) if n != o))
    check(len(words) == part, "as many cells changed as the dialog said (%d of %d)" % (len(words), part))
    st = page.state()
    center_row, center_col = [int(v) for v in st["center"].split(",")]
    half_c, half_r = box["w"] / 2 / st["scale"] + 1.5, box["h"] / 2 / st["scale"] + 1.5
    inside = all(abs(i // first["columns"] - center_row) <= half_r and abs(i % first["columns"] - center_col) <= half_c for i in words)
    check(inside, "and all of them are in the part that was on the screen")
    check(wait_for(lambda: page.state()["check"] == "Works in the original game", 15), "the check line still says: Works in the original game (%s)" % page.state()["check"])
    check(not page.button_off("#undo-btn") and still_pixels(page) != before_part, "Undo is on and the picture of the map changed")
    page.shot(args.shots, "mapmaker-fix-edges-after-part")
    # the whole map on top of it: the rest of the 147
    check(edges_open(page, whole=True), "the dialog opens, and the whole map is chosen")
    rest = edges_count(page)
    check(rest + part == 147, "the rest is what is left of the 147 (%d and %d)" % (rest, part))
    page.ev("[].filter.call(document.querySelectorAll('.dlg.edges .btn'), function (b) { return b.textContent === 'Fix edges'; })[0].click()")
    check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")), "Fix edges again")
    every = edges_bytes(page)
    check(every["left"] == 0 and every["layer1"] == first["layer1"], "the fixed map has nothing left to fix")
    te = every["bytes"][every["layer1"] - 8:]
    check(len(te) == len(ta) and len({(i - 8) // w for i in range(len(ta)) if ta[i] != te[i]}) == 147, "exactly 147 cells differ from the map as it was opened")
    check(wait_for(lambda: page.state()["check"] == "Works in the original game", 15), "and the check line says: Works in the original game (%s)" % page.state()["check"])
    after_all = still_pixels(page)
    page.shot(args.shots, "mapmaker-fix-edges-after-whole")
    # ONE Undo each
    page.ev("document.getElementById('undo-btn').click()")
    back = edges_bytes(page)
    check(back["sha"] == done["sha"], "one Undo takes back the second fix: the exact bytes of before it")
    page.ev("document.getElementById('undo-btn').click()")
    back = edges_bytes(page)
    check(back["sha"] == first["sha"] and still_pixels(page) == before_part, "one more Undo takes back the first: the exact bytes of the map as it was opened, and its pixels")
    check(page.button_off("#undo-btn"), "nothing is left to undo")
    page.ev("document.getElementById('redo-btn').click()")
    page.ev("document.getElementById('redo-btn').click()")
    check(edges_bytes(page)["sha"] == every["sha"] and still_pixels(page) == after_all, "Redo twice makes both again: the same bytes and the same pixels")
    check(edges_open(page, whole=True) and edges_count(page) == 0 and page.button_off(".dlg.edges .dlg-foot .btn:last-child"), "the dialog says that nothing changes, and Fix edges is off")
    page.key("Escape")
    # a phone: the box is a sheet inside the screen with both buttons in view, and nothing scrolls sideways
    tab.emulate(390, 844, 2, mobile=True)
    time.sleep(0.5)
    check(edges_open(page, whole=True), "on a phone (390 x 844) the dialog opens")
    got = json.loads(page.ev("""JSON.stringify((function () { var d = document.querySelector('.dlg.edges'), r = d.getBoundingClientRect(), out = [];
        [].forEach.call(d.querySelectorAll('.dlg-foot .btn, .kind, canvas'), function (n) { var b = n.getBoundingClientRect(); if (b.left < -0.5 || b.right > innerWidth + 0.5 || b.bottom > innerHeight + 0.5) out.push(n.textContent || n.tagName); });
        return {inside: r.left >= 0 && r.right <= innerWidth + 0.5 && r.bottom <= innerHeight + 0.5, out: out, sideways: document.documentElement.scrollWidth > innerWidth,
                small: [].filter.call(d.querySelectorAll('.dlg-foot .btn, .kind'), function (n) { return n.getBoundingClientRect().height < 43.5; }).length}; })())"""))
    check(got["inside"] and not got["out"] and not got["sideways"] and got["small"] == 0, "the sheet, its choices, pictures and buttons are inside the screen, 44 pixels high, no sideways scroll (%s)" % got)
    page.shot(args.shots, "mapmaker-fix-edges-phone-check")


def tab_browser(tab):
    """the Browser object of a Tab's DevTools connection (a second tab of the same browser)"""
    class Same:
        pass
    same = Same()
    same.devtools = tab.dt
    return same


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
        check(tools == [["ground", False, "true"], ["objects", False, "false"], ["bigger", False, "false"], ["hills", False, "false"], ["ants", False, "false"], ["food", False, "false"], ["erase", False, "false"], ["move", False, "false"]],
              "Ground is chosen, the other seven tools are on: %s" % tools)
        check(page.button_off("#undo-btn") and page.button_off("#redo-btn"), "Undo and Redo are off with nothing drawn")
        gravel, dirt, water, rock, slate = page.color_of(485), page.color_of(487), page.color_of(476), page.color_of(0, 302), page.color_of(571)
        page.tile(487)
        check(page.ev("document.getElementById('palette-name').textContent") == "Dirt: tap it again for all its tiles", "the palette names the tile that is chosen")
        a, b = page.cell_point(3, 3), page.cell_point(3, 20)
        check(page.pixel(*a) == gravel, "the map starts as gravel")
        page.drag(a[0], a[1], b[0], b[1], steps=14)
        time.sleep(0.2)
        mid = page.cell_point(3, 12)
        check(page.pixel(*a) == dirt and page.pixel(*mid) == dirt and page.pixel(*b) == dirt, "a drag of the mouse paints a line of dirt, with no gap")
        check(page.pixel(*page.cell_point(4, 12)) == gravel and page.pixel(*page.cell_point(2, 12)) == gravel, "and only the cells it went over")
        way, cells, ms = page.paint_info()
        check(way == "cells" and 0 < cells < 80, "only the cells that changed were painted again (%s: %d cells, %.1f ms)" % (way, cells, ms))
        check(not page.button_off("#undo-btn") and page.button_off("#redo-btn"), "Undo is on, Redo is off")
        page.shot(args.shots, "mapmaker-draw-ground")
        page.ev("document.getElementById('undo-btn').click()")
        time.sleep(0.2)
        check(page.pixel(*a) == gravel and page.pixel(*mid) == gravel and page.pixel(*b) == gravel, "Undo takes the whole stroke back at once")
        check(page.button_off("#undo-btn") and not page.button_off("#redo-btn"), "Undo is off again and Redo is on")
        page.ev("document.getElementById('redo-btn').click()")
        time.sleep(0.2)
        check(page.pixel(*mid) == dirt, "Redo puts it back")
        page.ev("document.getElementById('map').focus()")
        page.ctrl_key("z", "KeyZ", 90)
        time.sleep(0.2)
        check(page.pixel(*mid) == gravel, "Control+Z undoes")
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
        check(page.pixel(*page.cell_point(12, 10)) == gravel and page.pixel(*page.cell_point(10, 8)) == gravel, "and not the cells beside them")
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
        check(page.pixel(*r) == gravel, "the eraser takes it away (the letter E)")
        # fill
        page.key("g")
        page.tile(571)
        page.key("b")
        check(page.ev("document.getElementById('fill-btn').getAttribute('aria-pressed')") == "true", "the letter B turns Fill on")
        f = page.cell_point(25, 25)
        page.tab.click(*f)
        time.sleep(0.3)
        check(page.pixel(*f) == slate and page.pixel(*page.cell_point(30, 0)) == slate and page.pixel(*page.cell_point(10, 10)) == water and page.pixel(*mid) == dirt,
              "Fill paints the field of gravel round the click and stops at the water and the dirt")
        page.shot(args.shots, "mapmaker-draw-fill")
        page.ctrl_key("z", "KeyZ", 90)
        time.sleep(0.2)
        check(page.pixel(*f) == gravel, "and one Undo takes the whole fill back")
        check(page.ev("document.getElementById('fill-btn').getAttribute('aria-pressed')") == "false", "Fill is for one click: it is off again")
        # looking around instead of painting
        page.tile(487)
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

    elif part == "blocks":
        print("[web map maker] hills, ants and food, desktop, 1280 x 800")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        check(page.open() and page.ready(), "the page starts with its map core")
        rgb = lambda hexa: [int(hexa[i:i + 2], 16) for i in (1, 3, 5)]
        team_color = {k: rgb(page.ev("MapTiles.TEAMS.%s" % k)) for k in ("green", "red", "blue", "black")}
        food_color = rgb(page.ev("MapTiles.FOOD"))
        gravel = page.color_of(485)
        card = lambda: (page.press_act("check"), wait_for(lambda: page.ev("!!document.querySelector('.ccard')")), page.ev("document.querySelector('.ccard').textContent"), page.key("Escape"))[2]
        page.ev("document.getElementById('map').focus()")
        page.key("h")
        check(page.ev("document.querySelector('[data-tool=hills]').getAttribute('aria-pressed')") == "true", "the letter H chooses Hills")
        names = page.ev("[].map.call(document.querySelectorAll('#palette .swatch'), function (b) { return b.textContent; }).join('|')")
        check(names == "Green|Red|Blue|Black" and page.ev("document.getElementById('palette-title').textContent") == "Hills", "its palette has the four teams with their names: %s" % names)
        check(page.ev("[].every.call(document.querySelectorAll('#palette .swatch'), function (b) { var r = b.getBoundingClientRect(); return r.height >= 30 && r.width >= 100; })"), "each a button that is easy to hit")
        text = card()
        check("Blue has no hill" in text and "Black has no starting ants" in text, "a new map: the check says Blue has no hill and Black has no starting ants")
        page.ev("document.getElementById('map').focus()")
        page.ev("document.querySelector('#palette .swatch:nth-child(3)').click()")
        check(page.ev("document.getElementById('palette-name').textContent") == "Blue: no hill yet", "Blue is chosen: the line under the palette says no hill yet")
        spots = {"green": (5, 5), "red": (5, 24), "blue": (24, 5), "black": (24, 24)}
        order = ("green", "red", "blue", "black")
        a = page.cell_point(*spots["blue"])
        page.tab.mouse("mouseMoved", a[0], a[1], button="none")
        time.sleep(0.2)
        page.shot(args.shots, "mapmaker-blocks-hill-ghost")
        page.tab.click(*a)
        time.sleep(0.3)
        page.tab.mouse("mouseMoved", 5, 5, button="none")
        time.sleep(0.2)
        # (the cell that was clicked is the hill's door: it holds the marked square's dot, so it is not read)
        check(all(page.pixel(*page.cell_point(24 + dr, 5 + dc)) == team_color["blue"] for dr in (-1, 0, 1, 2) for dc in (-1, 0, 1, 2) if (dr, dc) != (0, 0)), "a click puts the Blue hill: 4 x 4 cells in blue, the clicked cell its door")
        check(page.pixel(*page.cell_point(22, 5)) == gravel and page.pixel(*page.cell_point(24, 8)) == gravel and page.pixel(*page.cell_point(27, 5)) == gravel, "and nothing round it")
        check(page.ev("document.getElementById('palette-name').textContent") == "Blue: has a hill" and not page.button_off("#undo-btn"), "the line says that Blue has a hill; Undo is on")
        # moving it
        m = page.cell_point(14, 14)
        page.tab.click(*m)
        time.sleep(0.3)
        page.tab.mouse("mouseMoved", 5, 5, button="none")
        time.sleep(0.2)
        check(page.pixel(*page.cell_point(15, 15)) == team_color["blue"] and page.pixel(*page.cell_point(25, 6)) == gravel and page.pixel(*page.cell_point(26, 7)) == gravel, "a click somewhere else moves the Blue hill: only one for a team")
        # at the edge: no room, in words, nothing changes
        e = page.cell_point(0, 0)
        page.tab.click(*e)
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg h2')")) and "no room" in page.ev("document.querySelector('.dlg').textContent"), "a click at the top left corner says that there is no room for a whole hill")
        page.key("Escape")
        check(page.pixel(*page.cell_point(3, 3)) == gravel and page.pixel(*page.cell_point(1, 1)) == gravel, "and nothing was put")
        # a drag looks around
        box = page.box("#map")
        mx, my = box["x"] + box["w"] / 2, box["y"] + box["h"] / 2
        d0 = page.cell_point(10, 18)
        page.drag(d0[0], d0[1], d0[0] + 60, d0[1] + 40)
        page.tab.mouse("mouseMoved", 5, 5, button="none")
        time.sleep(0.2)
        check(page.pixel(*d0) == gravel and page.pixel(*page.cell_point(10, 20)) == gravel, "a drag of the mouse looks around and puts no hill where it began")
        for team in order:
            page.ev("document.querySelector('#palette .swatch:nth-child(%d)').click()" % (order.index(team) + 1))
            p = page.cell_point(*spots[team])
            page.tab.click(*p)
            time.sleep(0.15)
        page.tab.mouse("mouseMoved", 5, 5, button="none")
        time.sleep(0.2)
        check(all(page.pixel(*page.cell_point(spots[t][0] + 1, spots[t][1] + 1)) == team_color[t] for t in order), "the four hills are put, each in its team's color")
        page.shot(args.shots, "mapmaker-blocks-hills")
        check(wait_for(lambda: page.state()["check"] == "Does not work in the original game", 4), "the check says it is not ready (the ants are missing)")
        text = card()
        check("no hill" not in text and "Green has no starting ants" in text, "no team lacks a hill any more; the ants are still missing: %s" % text[:120])
        # ants
        page.ev("document.getElementById('map').focus()")
        page.key("a")
        check(page.ev("document.querySelector('[data-tool=ants]').getAttribute('aria-pressed')") == "true" and page.ev("document.getElementById('stepper').hidden") is False, "the letter A chooses Ants: a stepper shows")
        check(page.ev("document.getElementById('step-val').textContent") == "4", "4 ants to begin with")
        page.ev("document.getElementById('step-more').click()")
        page.ev("document.getElementById('step-less').click()")
        page.ev("document.getElementById('step-less').click()")
        check(page.ev("document.getElementById('step-val').textContent") == "3", "the stepper makes it 3")
        for team in order:
            page.ev("document.querySelector('#palette .swatch:nth-child(%d)').click()" % (order.index(team) + 1))
            r, c = spots[team]
            page.tab.click(*page.cell_point(r + (5 if r < 14 else -3), c + 1))
            time.sleep(0.15)
        dots = [page.pixel(*page.cell_point(spots[t][0] + (5 if spots[t][0] < 14 else -3), spots[t][1] + 1)) for t in order]
        check(dots == [team_color[t] for t in order], "a click puts the team's ants: a dot in its color on the cell that was clicked")
        r, c = spots["green"]
        page.tab.mouse("mouseMoved", 5, 5, button="none")
        time.sleep(0.2)
        near = [page.pixel(*page.cell_point(r + 5 + dr, c + 1 + dc)) for dr, dc in ((-1, 0), (0, -1), (0, 1))]
        check(sum(1 for px in near if px == team_color["green"]) >= 2, "the others of the three stand next to it")
        page.shot(args.shots, "mapmaker-blocks-ants")
        check(wait_for(lambda: page.state()["check"] == "Works in the original game", 4), "four hills and four starts of ants: the check says it works in the original game (%s)" % page.state()["check"])
        # food
        page.ev("document.getElementById('map').focus()")
        page.key("f")
        check(page.ev("document.getElementById('palette-title').textContent") == "Food" and page.ev("document.getElementById('palette').offsetParent === null"), "the letter F chooses Food: a stepper, no teams")
        for _ in range(2):
            page.ev("document.getElementById('step-more').click()")
        check(page.ev("document.getElementById('step-val').textContent") == "6" and page.ev("document.getElementById('step-more').disabled"), "the stepper goes up to 6 stages and stops")
        page.tab.click(*page.cell_point(15, 15))
        time.sleep(0.3)
        check(page.pixel(*page.cell_point(15, 15)) == food_color, "a click puts a food pile (a gold square)")
        check(page.ev("document.getElementById('palette-name').textContent").endswith("1 pile on the map."), "the line says one pile is on the map: %s" % page.ev("document.getElementById('palette-name').textContent"))
        page.shot(args.shots, "mapmaker-blocks-food")
        check(wait_for(lambda: page.state()["check"] == "Works in the original game", 4), "and the map still works")
        # undo and redo through the same history: pile, ants, hills
        page.ev("document.getElementById('map').focus()")
        page.ctrl_key("z", "KeyZ", 90)
        time.sleep(0.2)
        check(page.pixel(*page.cell_point(15, 15)) != food_color, "Control+Z takes the pile away")
        for _ in range(4):
            page.ctrl_key("z", "KeyZ", 90)
        check(wait_for(lambda: page.state()["check"] == "Does not work in the original game", 4) and "Green has no starting ants" in card(), "four more undos take the ants away: the check says so again")
        for _ in range(4):
            page.ctrl_key("y", "KeyY", 89)
        check(wait_for(lambda: page.state()["check"] == "Works in the original game", 4), "four redos put them back: it works")
        # erase: a hill, a team's ants, a pile
        page.tab.click(*page.cell_point(15, 15))                 # (Food is still chosen: the pile is put again)
        time.sleep(0.2)
        page.key("e")
        page.tab.click(*page.cell_point(15, 15))
        time.sleep(0.2)
        check(page.pixel(*page.cell_point(15, 15)) == gravel, "the eraser takes the pile away")
        page.tab.click(*page.cell_point(spots["red"][0] + 5, spots["red"][1] + 1))
        time.sleep(0.2)
        check(page.pixel(*page.cell_point(spots["red"][0] + 5, spots["red"][1] + 1)) == gravel and page.pixel(*page.cell_point(spots["red"][0] + 4, spots["red"][1] + 1)) == gravel, "the eraser on one ant of Red takes all of Red's ants away")
        page.tab.click(*page.cell_point(spots["black"][0] + 1, spots["black"][1] + 1))
        time.sleep(0.2)
        check(all(page.pixel(*page.cell_point(24 + dr, 24 + dc)) == gravel for dr in (-1, 0, 1, 2) for dc in (-1, 0, 1, 2)), "the eraser on a cell of the Black hill takes the whole hill")
        check(wait_for(lambda: "Does not work" in page.state()["check"], 4), "the check is run again by itself")
        text = card()
        check("Red has no starting ants" in text and "Black has no hill" in text, "and the card says which: %s" % text[:140])
        page.shot(args.shots, "mapmaker-blocks-checks")
        # download: what the core writes
        page.ev("window.__blobs = []; var make = URL.createObjectURL.bind(URL); URL.createObjectURL = function (b) { window.__blobs.push(b); return make(b); }")
        page.press_act("download")
        check(wait_for(lambda: page.ev("window.__blobs.length === 1")), "Download hands over the map")
        head = page.ev("new Promise(function (ok) { window.__blobs[0].slice(0, 8).arrayBuffer().then(function (b) { ok(Array.prototype.join.call(new Uint8Array(b), ',')); }); })")
        check(head.startswith("8,0,0,0,1,0,0,0") and page.ev("window.__blobs[0].size") > 1000, "written by the core: version 8, mode 1 (%s)" % head)
        # an Extended map: the same
        page.press_act("new")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg h2') && document.querySelector('.dlg h2').textContent === 'Leave this map?'")) or page.ev("!!document.querySelector('.kinds')"), "New map")
        if page.ev("!!document.querySelector('.dlg h2') && document.querySelector('.dlg h2').textContent === 'Leave this map?'"):
            page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Leave without saving'; })[0].click()")
        check(wait_for(lambda: page.ev("!!document.querySelector('.kinds')")), "New map opens")
        page.ev("document.querySelectorAll('.kind input')[1].click()")
        page.type_into("#size-rows", "40")
        page.type_into("#size-columns", "60")
        page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
        check(wait_for(lambda: page.state()["rows"] == "40"), "an Extended map of 40 x 60 is made")
        page.tool("hills")
        for i, team in enumerate(order):
            page.ev("document.querySelector('#palette .swatch:nth-child(%d)').click()" % (i + 1))
            page.tab.click(*page.cell_point(8 + 20 * (i // 2), 10 + 30 * (i % 2)))
            time.sleep(0.15)
        page.tool("ants")
        for i, team in enumerate(order):
            page.ev("document.querySelector('#palette .swatch:nth-child(%d)').click()" % (i + 1))
            page.tab.click(*page.cell_point(14 + 20 * (i // 2) - (i // 2) * 2, 10 + 30 * (i % 2)))
            time.sleep(0.15)
        check(wait_for(lambda: page.state()["check"] == "Works in the new version", 4), "an Extended map with four hills and four starts: Works in the new version (%s)" % page.state()["check"])
        page.shot(args.shots, "mapmaker-blocks-extended")
        # a touch screen: a tap puts, a drag and two fingers do not
        print("[web map maker] hills, ants and food, a touch screen, 390 x 844")
        tab.emulate(390, 844, 2, mobile=True)
        page = Page(tab, args.web)
        check(page.open() and page.ready(), "the page starts")

        def touch(kind, points):
            tab.call("Input.dispatchTouchEvent", {"type": kind, "touchPoints": [{"x": x, "y": y, "id": i, "radiusX": 10, "radiusY": 10, "force": .5} for i, (x, y) in points.items()]})
        page.tool("hills")
        page.ev("document.getElementById('tile-btn').click()")
        check(wait_for(lambda: page.ev("document.querySelectorAll('.pick').length") == 4), "the button opens the sheet of the four teams")
        page.ev("document.querySelector('.pick:nth-child(3)').click()")
        check(page.ev("document.getElementById('tile-name').textContent") == "Blue", "Blue is chosen")
        p = page.cell_point(24, 5)
        touch("touchStart", {1: p})
        touch("touchEnd", {})
        time.sleep(0.3)
        check(page.pixel(*page.cell_point(26, 7)) == team_color["blue"] and page.pixel(*page.cell_point(23, 4)) == team_color["blue"], "a tap puts the Blue hill")
        q = page.cell_point(14, 14)
        touch("touchStart", {1: q})
        for i in range(1, 9):
            touch("touchMove", {1: (q[0] + i * 6, q[1] + i * 3)})
        touch("touchEnd", {})
        time.sleep(0.3)
        check(page.pixel(*q) == gravel and page.pixel(*page.cell_point(14, 14)) == gravel, "a finger that is dragged looks around: no hill")
        cx, cy = q
        touch("touchStart", {1: (cx - 30, cy), 2: (cx + 30, cy)})
        for i in range(1, 6):
            touch("touchMove", {1: (cx - 30 - i * 5, cy), 2: (cx + 30 + i * 5, cy)})
        touch("touchEnd", {})
        time.sleep(0.2)
        cell14 = page.cell_point(14, 14)
        check(page.pixel(cell14[0] - 0.3 * page.state()["scale"], cell14[1] - 0.3 * page.state()["scale"]) == gravel and page.pixel(cx - 35, cy) == gravel, "two fingers (a pinch) put nothing")
        page.ev("document.getElementById('map').focus()")
        page.key("0")                                            # (the pinch zoomed in: the whole map again, so that the cells are where cell_point says)
        time.sleep(0.2)
        page.tool("ants")
        page.ev("document.getElementById('tile-btn').click()")
        check(wait_for(lambda: page.ev("document.querySelectorAll('.stepper.sheet .stp').length") == 2), "the sheet of Ants has the teams and the stepper")
        page.ev("document.querySelector('.stepper.sheet .stbtns .stp:last-child').click()")
        check(page.ev("document.querySelector('.stepper.sheet output').textContent") == "5" and page.ev("document.getElementById('tile-name').textContent") == "Blue · 5 ants", "its More makes 5 ants: the button says so")
        page.shot(args.shots, "mapmaker-blocks-sheet-ants")
        page.key("Escape")
        g = page.cell_point(20, 12)
        touch("touchStart", {1: g})
        touch("touchEnd", {})
        time.sleep(0.3)
        check(page.pixel(*page.cell_point(19, 12)) == team_color["blue"] or page.pixel(*page.cell_point(20, 11)) == team_color["blue"] or page.pixel(*page.cell_point(20, 13)) == team_color["blue"], "a tap puts 5 ants of Blue: a dot in its color next to the cell that was tapped")
        page.ev("document.getElementById('undo-btn').click()")
        time.sleep(0.2)
        check(page.pixel(*page.cell_point(19, 12)) != team_color["blue"] and page.pixel(*page.cell_point(20, 13)) != team_color["blue"], "Undo takes the ants away")
        page.tool("food")
        h = page.cell_point(10, 20)
        touch("touchStart", {1: h})
        touch("touchEnd", {})
        time.sleep(0.3)
        check(page.pixel(*page.cell_point(10, 20)) == food_color or page.pixel(*page.cell_point(10, 20))[0] > 200, "a tap puts a food pile (a gold square under the marked square)")
        page.shot(args.shots, "mapmaker-blocks-phone")
    elif part == "bigger":
        print("[web map maker] bigger objects, desktop, 1280 x 800")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        check(page.open() and page.ready(), "the page starts with its map core")
        gravel = page.color_of(485)
        # (the anchor's cell has a dot in its middle: cells are read a little above and to the left of it)

        def at(r, c):
            x, y = page.cell_point(r, c)
            k = float(page.ev("document.getElementById('map').getAttribute('data-scale')")) * 0.3
            return page.pixel(x - k, y - k)

        def goldish(x, y, along):
            return any((lambda px: px[0] > 200 and px[1] > 150 and px[2] < 140)(page.pixel(x + (d if along == "x" else 0), y + (d if along == "y" else 0))) for d in (-1, 0, 1))
        page.ev("document.getElementById('map').focus()")
        page.key("k")
        check(page.ev("document.querySelector('[data-tool=bigger]').getAttribute('aria-pressed')") == "true", "the letter K chooses Bigger")
        check(page.ev("(function () { var b = document.getElementById('tile-btn'); return !b.hidden && b.offsetParent !== null; })()") and page.ev("document.getElementById('palette').offsetParent === null"),
              "its palette is the one button and no row of tiles, on a desktop too")
        check(page.ev("document.getElementById('tile-name').textContent") == "4 cells, 2 x 2grass4", "the button draws the shape, says the cells and, small, the game's name: %s" % page.ev("document.getElementById('tile-name').textContent"))
        page.ev("document.getElementById('tile-btn').click()")
        check(wait_for(lambda: page.ev("document.querySelectorAll('.pick').length") == 29), "the button opens a sheet of the 29 bigger objects")
        names = page.ev("[].map.call(document.querySelectorAll('.pick'), function (b) { return b.textContent; }).join('|')")
        check(names.startswith("4 cells, 2 x 2grass4|10 cells, 5 x 2grassmed1|6 cells, 2 x 3grass2") and "30 cells, 6 x 6bearbod" in names, "with the game's names and their cells: %s" % names[:80])
        page.shot(args.shots, "mapmaker-bigger-sheet")
        page.ev("document.querySelector('.pick[data-tile=\"1324\"]').click()")
        check(not page.ev("!!document.querySelector('.dlg')") and page.ev("document.getElementById('tile-name').textContent") == "10 cells, 5 x 2grassmed1", "a pick is chosen and the sheet closes")
        item = page.color_of(0, 1324)
        check(item != gravel, "an object's color is not the ground's")
        # the marked box: 2 rows and 5 columns round the cell (rows -1 to 0, columns -2 to 2)
        scale = float(page.ev("document.getElementById('map').getAttribute('data-scale')"))
        c10 = page.cell_point(10, 10)
        page.tab.mouse("mouseMoved", c10[0], c10[1], button="none")
        time.sleep(0.3)
        left, right, top, bottom = c10[0] - 2.5 * scale - 1.5, c10[0] + 2.5 * scale + 1.5, c10[1] - 1.5 * scale - 1.5, c10[1] + 0.5 * scale + 1.5
        check(goldish(left, c10[1], "x") and goldish(right, c10[1], "x") and goldish(c10[0] - scale, top, "y") and goldish(c10[0] - scale, bottom, "y"),
              "the marked box is the object's: 2 rows and 5 columns round the cell (the gold line at its four sides)")
        check(not goldish(left - 2 * scale, c10[1], "x") and not goldish(c10[0] - scale, bottom + 2 * scale, "y"), "and not wider")
        page.shot(args.shots, "mapmaker-bigger-hover")
        page.tab.click(*c10)
        page.tab.mouse("mouseMoved", 5, 5, button="none")
        time.sleep(0.3)
        cells = [(r, c) for r in (9, 10) for c in range(8, 13)]
        check(all(at(r, c) == item for r, c in cells), "a click puts the object whole: its 10 cells are its color")
        check(all(at(r, c) == gravel for r, c in ((8, 10), (11, 10), (10, 7), (10, 13), (9, 13))), "and nothing round it")
        check(not page.button_off("#undo-btn"), "Undo is on")
        page.shot(args.shots, "mapmaker-bigger-placed")
        page.key("e")
        page.tab.click(*page.cell_point(9, 12))
        time.sleep(0.2)
        check(all(at(r, c) == gravel for r, c in cells), "the eraser on one cell takes the whole object away")
        page.ctrl_key("z", "KeyZ", 90)
        time.sleep(0.2)
        check(all(at(r, c) == item for r, c in cells), "Control+Z gives it back whole")
        page.ctrl_key("z", "KeyZ", 90)
        time.sleep(0.2)
        check(all(at(r, c) == gravel for r, c in cells), "and the next one takes it away")
        page.ctrl_key("y", "KeyY", 89)
        time.sleep(0.2)
        check(all(at(r, c) == item for r, c in cells), "Control+Y puts it back")
        # an opened map keeps its verdict
        page.choose_file(tiny)
        wait_for(lambda: page.state()["name"] == "TINY")
        check(wait_for(lambda: page.state()["check"] == "Works in the original game", 6), "TINY works in the original game")
        free = tiny_free(6)
        page.tool("bigger")
        page.ev("document.getElementById('tile-btn').click()")
        wait_for(lambda: page.ev("document.querySelectorAll('.pick').length") == 29)
        page.ev("document.querySelector('.pick[data-tile=\"346\"]').click()")          # bearbod: 30 cells
        wait_for(lambda: not page.ev("!!document.querySelector('.dlg')"))
        page.tab.click(*page.cell_point(free[0] + 3, free[1] + 3))
        time.sleep(0.3)
        bear = page.color_of(0, 346)
        check(sum(1 for r in range(free[0], free[0] + 6) for c in range(free[1], free[1] + 6) if at(r, c) == bear) >= 25, "an object of 30 cells is put on TINY (%d of the 36 cells of its box are its color)" % sum(1 for r in range(free[0], free[0] + 6) for c in range(free[1], free[1] + 6) if at(r, c) == bear))
        check(wait_for(lambda: page.state()["check"] == "Works in the original game", 6), "and TINY still works in the original game: an object does not change the checker's verdict")
        page.ev("window.__blobs = []; var make = URL.createObjectURL.bind(URL); URL.createObjectURL = function (b) { window.__blobs.push(b); return make(b); }")
        page.press_act("download")
        check(wait_for(lambda: page.ev("window.__blobs.length === 1")), "Download hands over the map")
        head = page.ev("new Promise(function (ok) { window.__blobs[0].slice(0, 8).arrayBuffer().then(function (b) { ok(Array.prototype.join.call(new Uint8Array(b), ',')); }); })")
        check(head.startswith("8,0,0,0,1,0,0,0"), "written by the core: version 8, mode 1 (%s)" % head)
        # an Extended map takes one too
        page.press_act("new")
        if wait_for(lambda: page.ev("!!document.querySelector('.dlg h2') && document.querySelector('.dlg h2').textContent === 'Leave this map?'"), 2):
            page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Leave without saving'; })[0].click()")
        check(wait_for(lambda: page.ev("!!document.querySelector('.kinds')")), "New map opens")
        page.ev("document.querySelectorAll('.kind input')[1].click()")
        page.type_into("#size-rows", "40")
        page.type_into("#size-columns", "60")
        page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
        check(wait_for(lambda: page.state()["rows"] == "40"), "an Extended map of 40 x 60 is made")
        before = page.state()["check"]
        page.tool("bigger")
        page.ev("document.getElementById('tile-btn').click()")
        wait_for(lambda: page.ev("document.querySelectorAll('.pick').length") == 29)
        page.ev("document.querySelector('.pick[data-tile=\"1324\"]').click()")
        wait_for(lambda: not page.ev("!!document.querySelector('.dlg')"))
        page.tab.click(*page.cell_point(20, 30))
        time.sleep(0.3)
        check(all(at(r, c) == item for r, c in ((19, 28), (19, 32), (20, 28), (20, 32), (20, 30))), "an Extended map takes the object whole")
        check(wait_for(lambda: page.state()["check"] == before, 4), "and its verdict is as it was (%s)" % before)
        # a touch screen: a tap puts one whole, a drag and two fingers do not
        print("[web map maker] bigger objects, a touch screen, 390 x 844")
        tab.emulate(390, 844, 2, mobile=True)
        page = Page(tab, args.web)
        check(page.open() and page.ready(), "the page starts")

        def touch(kind, points):
            tab.call("Input.dispatchTouchEvent", {"type": kind, "touchPoints": [{"x": x, "y": y, "id": i, "radiusX": 10, "radiusY": 10, "force": .5} for i, (x, y) in points.items()]})
        page.tool("bigger")
        page.ev("document.getElementById('tile-btn').click()")
        check(wait_for(lambda: page.ev("document.querySelectorAll('.pick').length") == 29), "the button opens the sheet of 29 objects")
        small = page.ev("JSON.stringify([].filter.call(document.querySelectorAll('.dlg button'), function (b) { var r = b.getBoundingClientRect(); return r.width < 43.5 || r.height < 43.5; }).length)")
        check(small == "0", "every button of the sheet is at least 44 pixels each way")
        page.shot(args.shots, "mapmaker-bigger-sheet-phone")
        page.ev("document.querySelector('.pick[data-tile=\"1324\"]').click()")
        check(page.ev("document.getElementById('tile-name').textContent") == "10 cells, 5 x 2grassmed1", "grassmed1 is chosen")
        item = page.color_of(0, 1324)
        gravel = page.color_of(485)

        def at(r, c):
            x, y = page.cell_point(r, c)
            k = float(page.ev("document.getElementById('map').getAttribute('data-scale')")) * 0.3
            return page.pixel(x - k, y - k)
        p = page.cell_point(14, 14)
        touch("touchStart", {1: p})
        touch("touchEnd", {})
        time.sleep(0.3)
        check(all(at(r, c) == item for r, c in ((13, 12), (13, 16), (14, 12), (14, 16))), "a tap puts the object whole %s %s" % ([at(r, c) for r, c in ((13, 12), (13, 16), (14, 12), (14, 16))], item))
        q = page.cell_point(22, 14)
        touch("touchStart", {1: q})
        for i in range(1, 9):
            touch("touchMove", {1: (q[0] + i * 6, q[1] + i * 3)})
        touch("touchEnd", {})
        time.sleep(0.3)
        check(page.pixel(*page.cell_point(22, 14)) == gravel, "a finger that is dragged looks around: nothing is put")
        cx, cy = q
        touch("touchStart", {1: (cx - 30, cy), 2: (cx + 30, cy)})
        for i in range(1, 6):
            touch("touchMove", {1: (cx - 30 - i * 5, cy), 2: (cx + 30 + i * 5, cy)})
        touch("touchEnd", {})
        time.sleep(0.2)
        page.ev("document.getElementById('map').focus()")
        page.key("0")
        time.sleep(0.2)
        check(at(22, 14) == gravel, "two fingers (a pinch) put nothing")
        page.ev("document.getElementById('undo-btn').click()")
        time.sleep(0.2)
        check(all(at(r, c) == gravel for r, c in ((13, 12), (14, 12), (14, 16))), "Undo takes the object away whole")
        page.shot(args.shots, "mapmaker-bigger-phone")
    elif part == "props":
        print("[web map maker] the properties of the map, 1280 x 800")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        check(page.open() and page.ready(), "the page starts")
        page.choose_file(tiny)
        wait_for(lambda: page.state()["name"] == "TINY")
        check(wait_for(lambda: page.state()["check"] == "Works in the original game", 6), "TINY works in the original game")
        summary = lambda: page.ev("JSON.stringify([document.getElementById('s-desc').textContent, document.getElementById('s-time').textContent, document.getElementById('s-ant').textContent])")
        check(json.loads(summary()) == ["“Tiny map with no PowerUps”", "6 minutes · 3 eggs for each team", "No default ant type"], "the side panel says what the map has: %s" % summary())
        top = page.ev("JSON.stringify([].map.call(document.querySelectorAll('.edmenu.dsk .btn'), function (b) { return b.textContent; }))")
        check(json.loads(top) == ["New", "Open", "My maps", "Copy...", "Save as new...", "Properties", "Download", "Submit for approval..."], "the top bar: New, Open, My maps, Copy..., Save as new..., Properties, Download, Submit for approval... (%s)" % top)
        check(page.ev("document.querySelector('#sumbox [data-act=props]').offsetParent !== null && document.querySelector('#sumbox [data-act=props]').textContent === 'Edit'"), "the summary has its Edit link")
        page.shot(args.shots, "mapmaker-props-entry")
        # the mouse: the button opens the dialog
        page.click_selector(".edmenu.dsk [data-act=props]")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg.props h2')")), "the Properties button opens the dialog")
        check(page.ev("document.querySelector('.dlg h2').textContent") == "Map properties" and page.ev("document.activeElement.id") == "p-desc", "named Map properties, with the focus in the description")
        counter = lambda: page.ev("document.getElementById('p-count').firstChild.textContent")
        check(counter() == "25 of 29 characters", "the counter: %s" % counter())
        check(page.ev("[document.getElementById('p-minutes').value, document.getElementById('p-eggs').value, document.querySelector('.ant[aria-checked=true]').textContent].join('|')") == "6|3|None", "the numbers and the ant type of the map")
        page.shot(args.shots, "mapmaker-props-classic")
        box = page.ev("JSON.stringify((function () { var d = document.querySelector('.dlg.props').getBoundingClientRect(); return [d.top >= 0, d.bottom <= innerHeight, Math.round(d.width), document.querySelector('.dlg-in').scrollHeight <= document.querySelector('.dlg-in').clientHeight]; })())")
        check(json.loads(box)[:2] == [True, True] and 700 <= json.loads(box)[2] <= 722 and json.loads(box)[3], "the box is inside the screen, 720 wide, and needs no scrollbar at 1280 x 800 (%s)" % box)
        # typed text: bytes, not letters
        page.type_into("#p-desc", "Café été des fourmis")
        check(counter() == "23 of 29 characters (letters like é count double)", "accented letters count double: %s" % counter())
        page.shot(args.shots, "mapmaker-props-accents")
        page.type_into("#p-desc", "x" * 40)
        check(counter() == "29 of 29 characters" and page.ev("document.getElementById('p-desc').value.length") == 29, "the box stops at 29: %s" % counter())
        check(page.ev("var n = document.querySelector('.cutnote'); n.offsetParent !== null && n.textContent.indexOf('Cut to fit') === 0"), "and says Cut to fit")
        page.type_into("#p-desc", "Café des fourmis")
        # a number box, the plus button, the minimum
        page.click_selector("#p-minutes + .stp")
        check(page.ev("document.getElementById('p-minutes').value") == "7", "the plus button adds a minute")
        page.type_into("#p-eggs", "999")
        page.ev("document.getElementById('p-eggs').blur()")
        check(page.ev("document.getElementById('p-eggs').value") == "255", "a number typed above the range goes to 255")
        page.type_into("#p-eggs", "4")
        page.ev("document.getElementById('p-eggs').blur()")
        # the keyboard on the ant types: Tab to the group, the arrow keys choose
        page.ev("document.querySelector('.ant[aria-checked=true]').focus()")
        page.key("ArrowRight")
        page.key("ArrowRight")
        check(page.ev("document.querySelector('.ant[aria-checked=true]').textContent") == "Thief ant" and page.ev("document.activeElement.textContent") == "Thief ant", "the arrow keys choose the ant type and move the focus: %s" % page.ev("document.querySelector('.ant[aria-checked=true]').textContent"))
        page.key("Tab")
        check(page.ev("document.activeElement.textContent") in ("Cancel",), "Tab goes on to the buttons (the group is one stop): %s" % page.ev("document.activeElement.textContent"))
        page.click_selector(".dlg-foot .btn:last-child")
        check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")), "Apply closes the dialog")
        check(wait_for(lambda: json.loads(summary()) == ["“Café des fourmis”", "7 minutes · 4 eggs for each team", "Thief ant by default"]), "the side panel says what was set: %s" % summary())
        check(page.state()["check"] in ("Checking…", "Works in the original game") and wait_for(lambda: page.state()["check"] == "Works in the original game", 6), "the check is run again: %s" % page.state()["check"])
        check(not page.button_off("#undo-btn") and page.button_off("#redo-btn"), "Undo is on")
        page.ev("document.getElementById('undo-btn').click()")
        check(wait_for(lambda: json.loads(summary())[0] == "“Tiny map with no PowerUps”") and json.loads(summary())[1:] == ["6 minutes · 3 eggs for each team", "No default ant type"], "ONE Undo gives all four back: %s" % summary())
        check(page.button_off("#undo-btn") and not page.button_off("#redo-btn"), "and the history is where it was")
        page.ev("document.getElementById('redo-btn').click()")
        check(wait_for(lambda: json.loads(summary())[2] == "Thief ant by default"), "Redo sets them again")
        # the properties are a change: the page asks before the map is left (Stay here changes nothing)
        page.press_act("new")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg h2') && document.querySelector('.dlg h2').textContent === 'Leave this map?'")), "a map whose properties were set is asked about before it is left")
        page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Stay here'; })[0].click()")
        wait_for(lambda: not page.ev("!!document.querySelector('.dlg')"))
        # Download hands over the file as the core writes it
        page.ev("window.__blobs = []; var make = URL.createObjectURL.bind(URL); URL.createObjectURL = function (b) { window.__blobs.push(b); return make(b); }")
        page.press_act("download")
        check(wait_for(lambda: page.ev("window.__blobs.length === 1")), "Download hands over the map")
        head = page.ev("new Promise(function (ok) { window.__blobs[0].slice(0, 12).arrayBuffer().then(function (b) { ok(Array.prototype.join.call(new Uint8Array(b), ',')); }); })")
        check(head.startswith("8,0,0,0,1,0,0,0,7,0"), "written by the core with 7 minutes: %s" % head)
        # Cancel and Escape change nothing
        page.press_act("props")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg.props')")), "the dialog opens again")
        page.type_into("#p-desc", "Not saved")
        page.ev("document.querySelectorAll('.ant')[4].click()")
        page.click_selector(".dlg-foot .btn:first-child")
        check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")) and json.loads(summary())[0] == "“Café des fourmis”" and json.loads(summary())[2] == "Thief ant by default", "Cancel changes nothing")
        page.ev("document.querySelector('#sumbox [data-act=props]').focus()")
        page.key("Enter", text="\r")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg.props')")), "Enter on the Edit link of the summary opens it: the keyboard works")
        page.type_into("#p-desc", "Not saved either")
        page.key("Escape")
        check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")) and json.loads(summary())[0] == "“Café des fourmis”", "Escape changes nothing")
        check(page.ev("document.activeElement.getAttribute('data-act')") == "props", "and the focus goes back to the Edit link")
        # a Classic map that came with 30 bytes: the core's sentence, Cut it to 29
        party = os.path.join(REPO, "Community-Maps", "ANTS_PARTY-V.3.0.5.lvl")
        page.choose_file(party)
        if wait_for(lambda: page.ev("!!document.querySelector('.dlg h2') && document.querySelector('.dlg h2').textContent === 'Leave this map?'"), 2):
            page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Leave without saving'; })[0].click()")
            page.choose_file(party)
        wait_for(lambda: page.state()["name"].upper().startswith("ANTS_PARTY"))
        wait_for(lambda: page.state()["check"] != "Checking…", 6)
        page.press_act("props")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg.props')")), "the properties of a map with 30 bytes open")
        sentence = page.ev("(document.querySelector('.note.bad') || {textContent: ''}).textContent")
        check(counter() == "30 of 29 characters" and sentence.startswith("!The description is 30 bytes long: at most 29 fit, because the original reads 30 bytes") and sentence.endswith("Cut it to 29"), "30 of 29, in red, with the core's sentence and Cut it to 29: %r" % sentence)
        page.shot(args.shots, "mapmaker-props-too-long")
        page.click_selector(".note.bad .btn")
        check(counter() == "29 of 29 characters" and page.ev("!document.querySelector('.note.bad')"), "Cut it to 29 cuts the text and the note goes")
        page.click_selector(".dlg-foot .btn:last-child")
        wait_for(lambda: not page.ev("!!document.querySelector('.dlg')"))
        page.press_act("check")
        check(wait_for(lambda: page.ev("!!document.querySelector('.ccard')")), "the checks card opens")
        check("bytes long" not in page.ev("document.querySelector('.ccard').textContent"), "and it no longer warns about the description")
        page.key("Escape")
        # an Extended map: the larger box, 255
        page.press_act("new")
        if wait_for(lambda: page.ev("!!document.querySelector('.dlg h2') && document.querySelector('.dlg h2').textContent === 'Leave this map?'"), 2):
            page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Leave without saving'; })[0].click()")
        check(wait_for(lambda: page.ev("!!document.querySelector('.kinds')")), "New map opens")
        page.ev("document.querySelectorAll('.kind input')[1].click()")
        page.type_into("#size-rows", "40")
        page.type_into("#size-columns", "60")
        page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
        check(wait_for(lambda: page.state()["rows"] == "40"), "an Extended map of 40 x 60 is made")
        page.press_act("props")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg.props')")), "its properties open")
        check(page.ev("document.getElementById('p-desc').tagName") == "TEXTAREA" and counter() == "0 of 255 characters", "the description is the larger box, room for 255: %s" % counter())
        page.type_into("#p-desc", "y" * 300)
        check(counter() == "255 of 255 characters" and page.ev("document.getElementById('p-desc').value.length") == 255, "the box stops at 255")
        page.shot(args.shots, "mapmaker-props-extended")
        page.click_selector(".dlg-foot .btn:last-child")
        wait_for(lambda: not page.ev("!!document.querySelector('.dlg')"))
        check(wait_for(lambda: page.ev("document.getElementById('s-desc').textContent.length") == 257), "the summary has the 255 letters (in quotes)")
        check(wait_for(lambda: page.state()["check"] == "Does not work in the new version" or page.state()["check"] == "Works in the new version", 6), "the check is the new version's: %s" % page.state()["check"])
        page.ev("document.getElementById('undo-btn').click()")
        check(wait_for(lambda: page.ev("document.getElementById('s-desc').textContent") == "No description"), "Undo takes the description away")
        # a touch screen
        print("[web map maker] the properties of the map, a touch screen, 390 x 844")
        tab.emulate(390, 844, 2, mobile=True)
        page = Page(tab, args.web)
        check(page.open() and page.ready(), "the page starts")
        page.choose_file(tiny)
        wait_for(lambda: page.state()["name"] == "TINY")
        check(page.ev("document.getElementById('phone-sum').offsetParent !== null") and page.ev("document.getElementById('phone-sum').textContent") == "“Tiny map with no PowerUps” · 6 min", "the line under the map: %s" % page.ev("document.getElementById('phone-sum').textContent"))
        check(page.ev("document.querySelector('.phsum [data-act=props]').offsetParent !== null") and page.ev("document.querySelector('.edmenu.dsk [data-act=props]').offsetParent") is None, "with its Edit link; the desktop's button is not there")
        page.shot(args.shots, "mapmaker-props-phone-row")

        def tap_selector(selector):
            b = page.box(selector)
            tab.tap(b["x"] + b["w"] / 2, b["y"] + b["h"] / 2)
        tap_selector("[data-act=menu]")
        check(wait_for(lambda: page.ev("!!document.querySelector('.menulist')")), "Menu opens")
        names = page.ev("[].map.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent; }).join('|')")
        check(names == "New|Open|My maps|Copy...|Save as new...|Properties...|Download|Submit for approval...|My submissions", "the menu: %s" % names)
        page.shot(args.shots, "mapmaker-props-phone-menu")
        page.ev("document.querySelectorAll('.menulist .btn')[5].click()")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg.props')")), "Properties... opens the sheet")
        check(page.ev("document.querySelector('.dlg').getBoundingClientRect().bottom") <= 844.5 and page.ev("[].every.call(document.querySelectorAll('.dlg-foot button'), function (b) { return b.getBoundingClientRect().bottom <= innerHeight + 1 && b.getBoundingClientRect().height >= 43.5; })"), "Cancel and Apply are in view, 44 pixels high")
        small = page.ev("JSON.stringify([].filter.call(document.querySelectorAll('.dlg button, .dlg input, .dlg textarea'), function (b) { var r = b.getBoundingClientRect(); return b.offsetParent !== null && !b.disabled && (r.width < 43.5 || r.height < 43.5); }).map(function (b) { return b.id || b.textContent; }))")
        check(small == "[]", "every control of the sheet is at least 44 pixels each way (%s)" % small)
        page.shot(args.shots, "mapmaker-props-phone-sheet")
        tap_selector("#p-minutes + .stp")
        tap_selector(".ant:nth-child(4)")
        tap_selector(".dlg-foot .btn:last-child")
        check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")), "tapping Apply closes the sheet")
        check(wait_for(lambda: page.ev("document.getElementById('phone-sum').textContent") == "“Tiny map with no PowerUps” · 7 min"), "the line under the map says 7 min: %s" % page.ev("document.getElementById('phone-sum').textContent"))
        tap_selector(".phsum [data-act=props]")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg.props')")) and page.ev("document.querySelector('.ant[aria-checked=true]').textContent") == "Bomber ant", "the Edit link opens the same sheet, with what was set")
        tap_selector(".dlg-foot .btn:first-child")
        check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")), "Cancel closes it")
        page.ev("document.getElementById('undo-btn').click()")
        check(wait_for(lambda: page.ev("document.getElementById('phone-sum').textContent") == "“Tiny map with no PowerUps” · 6 min"), "Undo gives it back on a phone too")
    elif part == "convert":
        print("[web map maker] copy, convert and crop, 1280 x 800")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        check(page.open() and page.ready(), "the page starts")
        page.choose_file(tiny)
        check(wait_for(lambda: page.state()["name"] == "TINY"), "TINY is open")
        check(page.ev("document.getElementById('convert-link').textContent") == "Start an Extended map from this one...", "the side panel offers an Extended map")
        page.press_act("copy")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg .srccard')")), "Copy... opens the dialog with the map that is copied")
        page.shot(args.shots, "mapmaker-convert-copy")
        check(page.ev("document.querySelector('.dlg h2').textContent") == "Copy this map" and page.ev("document.querySelector('.cname input').value") == "TINY 2", "the name of the copy is TINY 2")
        page.type_into(".cname input", "tiny")
        check(page.ev("!!document.querySelector('.cname .note.bad')") and page.button_off(".dlg-foot .btn:last-child"), "the open map's own name is refused (capitals do not matter) and Make new map is off")
        page.type_into(".cname input", "Little Tiny")
        page.click_selector(".dlg-foot .btn:last-child")
        check(wait_for(lambda: page.state()["name"] == "Little Tiny"), "a copy of the same kind is open under the new name")
        s = page.state()
        check(s["rows"] == "31" and s["tags"] == ["Classic map"] and s["check"] == "Works in the original game", "it is the same Classic map: %s" % s)
        page.ev("document.getElementById('convert-link').click()")
        check(wait_for(lambda: page.ev("!!document.querySelector('.cboxes')")) and page.ev("document.querySelector('.dlg h2').textContent") == "Start an Extended map from this one?", "the link opens the Extended map's dialog")
        page.shot(args.shots, "mapmaker-convert-start-extended")
        check(page.ev("document.querySelector('.cname input').value") == "Little Tiny Extended", "its name is Little Tiny Extended")
        page.click_selector(".dlg-foot .btn:last-child")
        check(wait_for(lambda: page.state()["name"] == "Little Tiny Extended"), "the Extended map is open")
        s = page.state()
        check(s["tags"] == ["Extended map", "New version only"] and s["check"] == "Works in the new version" and page.ev("document.getElementById('convert-link').textContent") == "Make a Classic map from this one...", "an Extended map with its badge, and the link back: %s" % s)
        page.ev("document.getElementById('convert-link').click()")
        check(wait_for(lambda: page.ev("!!document.querySelector('.cboxes')")) and "fits a Classic map" in page.ev("document.querySelector('.dlg .lead').textContent"), "it fits a Classic map again")
        page.shot(args.shots, "mapmaker-convert-fits")
        page.click_selector(".dlg-foot .btn:last-child")
        check(wait_for(lambda: page.state()["tags"] == ["Classic map"]), "the Classic map is made")
        check(page.state()["name"] == "Little Tiny Extended Classic", "under a name of its own: %s" % page.state()["name"])
        # an Extended map of 120 x 400
        page.press_act("new")
        wait_for(lambda: page.ev("!!document.querySelector('.kinds')"))
        page.ev("document.querySelectorAll('.kind input')[1].click()")
        page.type_into("#size-rows", "120")
        page.type_into("#size-columns", "400")
        page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
        check(wait_for(lambda: page.state()["rows"] == "120" and page.state()["columns"] == "400"), "an Extended map of 120 x 400")
        page.press_act("copy")
        wait_for(lambda: page.ev("!!document.querySelector('.kind.off')"))
        check(page.ev("document.querySelectorAll('.kind')[1].textContent").startswith("Classic map (convert)Not yet: this map has 120 rows and a Classic map made now has at most 90."), "the Classic choice is dimmed, with the reason: %s" % page.ev("document.querySelectorAll('.kind')[1].textContent"))
        page.shot(args.shots, "mapmaker-convert-copy-cannot")
        page.ev("[].filter.call(document.querySelectorAll('.dlg button'), function (b) { return b.textContent === 'See why and what to do'; })[0].click()")
        check(wait_for(lambda: page.ev("!!document.querySelector('.creasons')")) and page.ev("document.querySelectorAll('.creasons .note').length") == 2, "See why and what to do lists the reasons (rows, then columns)")
        check(page.ev("document.querySelector('.creasons .note').textContent.indexOf('This map has 120 rows') === 1"), "the rows first")
        page.shot(args.shots, "mapmaker-convert-reasons")
        page.ev("[].filter.call(document.querySelectorAll('.dlg button'), function (b) { return b.textContent === 'Crop it to fit...'; })[0].click()")
        check(wait_for(lambda: page.ev("!document.getElementById('crop').hidden")) and page.ev("document.getElementById('editor').hidden"), "Crop it to fit opens the screen and hides the editor")
        check(page.ev("document.getElementById('crop-big').textContent") == "90 rows x 111 columns" and page.ev("document.getElementById('crop-range').textContent") == "Rows 15 to 104 · Columns 144 to 254", "the frame starts in the center: 90 rows x 111 columns")
        page.shot(args.shots, "mapmaker-convert-crop-start")
        stage = page.box("#crop-stage")
        frame = page.box("#crop-frame")
        check(stage["w"] > 400 and abs((frame["x"] + frame["w"] / 2) - (stage["x"] + stage["w"] / 2)) < 3, "the picture is wide and the frame is in the middle of it: %s" % stage)
        page.drag(frame["x"] + frame["w"] / 2, frame["y"] + frame["h"] / 2, frame["x"] + frame["w"] / 2 + 60, frame["y"] + frame["h"] / 2)
        moved = page.ev("document.getElementById('crop-range').textContent")
        check(moved != "Rows 15 to 104 · Columns 144 to 254" and moved.startswith("Rows 15 to 104 · Columns 1"), "dragging the frame with the mouse moves it: %s" % moved)
        handle = page.box("#crop-frame .h.e")
        page.drag(handle["x"] + handle["w"] / 2, handle["y"] + handle["h"] / 2, handle["x"] + handle["w"] / 2 + 80, handle["y"] + handle["h"] / 2)
        check(page.ev("document.getElementById('crop-big').textContent") == "76 rows x 130 columns" and "shortened by itself" in page.ev("document.getElementById('crop-note').textContent"), "pulled wider: 76 rows x 130 columns, shortened by itself: %s" % page.ev("document.getElementById('crop-big').textContent"))
        page.shot(args.shots, "mapmaker-convert-crop-wider")
        before = page.ev("document.getElementById('crop-range').textContent")
        page.ev("document.getElementById('crop-frame').focus()")
        page.key("ArrowLeft")
        check(page.ev("document.getElementById('crop-range').textContent") != before, "an arrow key moves the frame")
        page.ev("document.getElementById('crop-use').click()")
        check(wait_for(lambda: page.ev("!!document.querySelector('.cboxes')")) and page.ev("document.getElementById('crop').hidden") and not page.ev("document.getElementById('editor').hidden"), "Use this part brings the editor back with the dialog of the part")
        check("cropped to 76 x 130" in page.ev("document.querySelector('.dlg .lead').textContent") and "Only the part you chose is kept" in page.ev("document.querySelector('.cboxes').textContent"), "it says what was kept")
        page.shot(args.shots, "mapmaker-convert-crop-fits")
        page.click_selector(".dlg-foot .btn:last-child")
        check(wait_for(lambda: page.state()["rows"] == "76" and page.state()["columns"] == "130"), "the Classic map is the part: 76 x 130")
        check(page.state()["tags"] == ["Classic map"], "a Classic map")
        # Escape on the crop screen comes back to the reasons
        page.press_act("new")
        wait_for(lambda: page.ev("!!document.querySelector('.kinds')"))
        page.ev("document.querySelectorAll('.kind input')[1].click()")
        page.type_into("#size-rows", "120")
        page.type_into("#size-columns", "400")
        page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
        wait_for(lambda: page.state()["rows"] == "120")
        page.ev("document.getElementById('convert-link').click()")
        wait_for(lambda: page.ev("!!document.querySelector('.creasons')"))
        page.ev("[].filter.call(document.querySelectorAll('.dlg button'), function (b) { return b.textContent === 'Crop it to fit...'; })[0].click()")
        wait_for(lambda: page.ev("!document.getElementById('crop').hidden"))
        page.key("Escape")
        check(wait_for(lambda: page.ev("!!document.querySelector('.creasons')")) and page.ev("document.getElementById('crop').hidden"), "Escape leaves the crop screen and shows the reasons again")
        page.key("Escape")
        check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")) and page.state()["rows"] == "120", "and the Extended map is as it was")
        # a touch screen
        for w, h in ((390, 844), (844, 390)):
            print("[web map maker] copy and crop on a touch screen, %d x %d" % (w, h))
            tab.emulate(w, h, 2, mobile=True)
            page = Page(tab, args.web)
            check(page.open() and page.ready(), "the page starts")
            page.choose_file(tiny)
            wait_for(lambda: page.state()["name"] == "TINY")
            page.ev("document.querySelector('[data-act=menu]').click()")
            wait_for(lambda: page.ev("!!document.querySelector('.menulist')"))
            page.ev("[].filter.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent === 'Copy...'; })[0].click()")
            check(wait_for(lambda: page.ev("!!document.querySelector('.dlg .srccard')")), "Menu, Copy... opens the sheet")
            fit = json.loads(page.ev("""JSON.stringify((function () {
                var d = document.querySelector('.dlg').getBoundingClientRect(), bad = [];
                [].forEach.call(document.querySelectorAll('.dlg-foot button'), function (b) { var r = b.getBoundingClientRect(); if (r.bottom > innerHeight + 1 || r.right > innerWidth + 1 || r.left < -1) bad.push(b.textContent); });
                var small = [].filter.call(document.querySelectorAll('.dlg button, .dlg input[type=text]'), function (b) { var r = b.getBoundingClientRect(); return b.offsetParent !== null && !b.disabled && (r.width < 43.5 || r.height < 43.5); }).map(function (b) { return b.textContent; });
                return {bottom: d.bottom, right: d.right, bad: bad, small: small, scrollX: document.documentElement.scrollWidth > innerWidth};
            })())"""))
            check(fit["bad"] == [] and fit["bottom"] <= h + 1 and fit["right"] <= w + 1 and not fit["scrollX"], "%d x %d: the sheet is inside the screen with Cancel and Make new map in view (%s)" % (w, h, fit))
            check(fit["small"] == [], "%d x %d: every control of the sheet is at least 44 pixels each way (%s)" % (w, h, fit["small"]))
            page.shot(args.shots, "mapmaker-convert-copy-phone-%dx%d" % (w, h))
            page.ev("document.querySelector('.dlg-foot .btn:first-child').click()")
            # an Extended map that does not fit: the reasons sheet and the crop screen
            page.press_act("new") if False else None
            page.ev("document.querySelector('[data-act=menu]').click()")
            wait_for(lambda: page.ev("!!document.querySelector('.menulist')"))
            page.ev("[].filter.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent === 'New'; })[0].click()")
            wait_for(lambda: page.ev("!!document.querySelector('.kinds')"))
            page.ev("document.querySelectorAll('.kind input')[1].click()")
            page.type_into("#size-rows", "120")
            page.type_into("#size-columns", "400")
            page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
            wait_for(lambda: page.state()["rows"] == "120")
            page.ev("document.querySelector('[data-act=menu]').click()")
            wait_for(lambda: page.ev("!!document.querySelector('.menulist')"))
            page.ev("[].filter.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent === 'Copy...'; })[0].click()")
            wait_for(lambda: page.ev("!!document.querySelector('.kind.off')"))
            page.ev("[].filter.call(document.querySelectorAll('.dlg button'), function (b) { return b.textContent === 'See why and what to do'; })[0].click()")
            check(wait_for(lambda: page.ev("!!document.querySelector('.creasons')")), "%d x %d: See why and what to do opens the reasons" % (w, h))
            fit = json.loads(page.ev("""JSON.stringify((function () {
                var d = document.querySelector('.dlg').getBoundingClientRect(), bad = [];
                [].forEach.call(document.querySelectorAll('.dlg-foot button'), function (b) { var r = b.getBoundingClientRect(); if (r.bottom > innerHeight + 1 || r.right > innerWidth + 1) bad.push(b.textContent); });
                var small = [].filter.call(document.querySelectorAll('.dlg button'), function (b) { var r = b.getBoundingClientRect(); return b.offsetParent !== null && (r.width < 43.5 || r.height < 43.5); }).map(function (b) { return b.textContent; });
                return {bottom: d.bottom, bad: bad, small: small};
            })())"""))
            check(fit["bad"] == [] and fit["bottom"] <= h + 1 and fit["small"] == [], "%d x %d: the reasons sheet is inside the screen and its buttons are 44 pixels (%s)" % (w, h, fit))
            page.shot(args.shots, "mapmaker-convert-reasons-phone-%dx%d" % (w, h))
            page.ev("[].filter.call(document.querySelectorAll('.dlg button'), function (b) { return b.textContent === 'Crop it to fit...'; })[0].click()")
            check(wait_for(lambda: page.ev("!document.getElementById('crop').hidden")), "%d x %d: the crop screen opens" % (w, h))
            time.sleep(0.2)
            found = json.loads(page.ev("""JSON.stringify((function () {
                var small = [], corners = [].map.call(document.querySelectorAll('#crop-frame .h'), function (n) { var r = n.getBoundingClientRect(), shown = getComputedStyle(n).display !== 'none'; return {h: n.getAttribute('data-h'), shown: shown, w: r.width, hh: r.height}; });
                [].forEach.call(document.querySelectorAll('#crop button'), function (b) { var r = b.getBoundingClientRect(); if (r.width < 43.5 || r.height < 43.5) small.push(b.textContent); });
                var s = document.getElementById('crop-stage').getBoundingClientRect();
                return {scrollX: document.documentElement.scrollWidth > innerWidth, corners: corners, small: small, stageW: s.width, stageH: s.height, right: s.right, bodyW: document.getElementById('crop').scrollWidth, innerW: innerWidth};
            })())"""))
            shown = [c["h"] for c in found["corners"] if c["shown"]]
            check(not found["scrollX"] and found["bodyW"] <= found["innerW"] and found["right"] <= w + 1 and found["stageH"] > 60, "%d x %d: the crop screen has no sideways scroll and a usable picture (%s)" % (w, h, {k: found[k] for k in ("scrollX", "bodyW", "right", "stageW", "stageH")}))
            check(shown == ["nw", "ne", "sw", "se"] and all(c["w"] >= 43.5 and c["hh"] >= 43.5 for c in found["corners"] if c["shown"]) and found["small"] == [], "%d x %d: the four corners are the handles, 44 pixels each, and so are the buttons (%s %s)" % (w, h, shown, found["small"]))
            page.shot(args.shots, "mapmaker-convert-crop-phone-%dx%d" % (w, h))
            vis = json.loads(page.ev("JSON.stringify((function () { var u = document.getElementById('crop-use').getBoundingClientRect(), c = document.getElementById('crop-cancel').getBoundingClientRect(); return {use: u.bottom <= innerHeight || innerHeight < 500, cancel: c.right <= innerWidth + 1, ur: u.right <= innerWidth + 1}; })())"))
            check(vis["cancel"] and vis["ur"], "%d x %d: Use this part and Cancel are inside the width (%s)" % (w, h, vis))
            page.ev("document.getElementById('crop-cancel').click()")
            check(wait_for(lambda: page.ev("!!document.querySelector('.creasons')")), "%d x %d: Cancel comes back to the reasons" % (w, h))
    elif part == "save":
        fake = ("window.__real = window.fetch; window.fetch = function (u) { if (String(u) === '/maps') return Promise.resolve(new Response(JSON.stringify({format: 1, maps: [{file: 'POPcOrN.lvl'}, {file: 'TINY.LVL'}]}), {status: 200})); "
                "return window.__real.apply(this, arguments); };")
        blocked = "Object.defineProperty(window, 'indexedDB', { get: function () { throw new Error('blocked'); } });"
        title = lambda page: page.ev("(document.querySelector('.dlg h2') || {}).textContent || ''")
        dialog = lambda page, text: wait_for(lambda: title(page) == text)
        draft = lambda page, key: idb_read(page, "drafts", key)
        print("[web map maker] save as a new map and the drafts, 1280 x 800")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        check(page.open(fake) and page.ready(), "the page starts")
        wait_for(lambda: page.ev("!!window.__listed") or True)
        page.press_act("save")
        check(dialog(page, "Save as a new map") and page.ev("document.querySelector('.cname input').value") == "My map 2", "Save as new... opens the dialog with the next free name (My map 2)")
        page.press_act("save") if False else None
        page.ev("document.querySelector('.dlg-foot .btn:first-child').click()")
        page.choose_file(tiny)
        check(wait_for(lambda: page.state()["name"] == "TINY"), "TINY is open")
        page.press_act("save")
        dialog(page, "Save as a new map")
        check(page.ev("document.querySelector('.cname input').value") == "TINY 2" and "is free" in page.ev("document.querySelector('.cname').textContent"), "the name offered is TINY 2, and it is free")
        check(page.ev("document.querySelector('.dlg .lead').textContent") == "Your changes are saved as a new map. The map you opened, \u201cTINY\u201d, stays exactly as it was: no map is ever replaced.", "the words of the picture")
        page.shot(args.shots, "mapmaker-save-free")
        page.type_into(".cname input", "tiny")
        check(page.ev("!!document.querySelector('.cname .note.bad')") and page.button_off(".dlg-foot .btn:last-child") and "already exists. A map is never replaced" in page.ev("document.querySelector('.cname .note').textContent"), "the open map's name is refused (capitals do not matter) and Save is off")
        check(page.ev("document.querySelector('.cname .note .btn').textContent") == "Use \u201ctiny 2\u201d", "with the next free name in one click")
        page.shot(args.shots, "mapmaker-save-taken")
        page.click_selector(".cname .note .btn")
        check(page.ev("document.querySelector('.cname input').value") == "tiny 2" and not page.button_off(".dlg-foot .btn:last-child"), "the button puts it in the box and Save is on")
        page.type_into(".cname input", "  popcorn ")
        check(wait_for(lambda: page.ev("!!document.querySelector('.cname .note.bad')")) and page.button_off(".dlg-foot .btn:last-child"), "a library map's name is refused (the site's list of maps)")
        page.type_into(".cname input", "Picnic")
        page.click_selector(".dlg-foot .btn:last-child")
        check(wait_for(lambda: page.state()["name"] == "Picnic") and not page.ev("!!document.querySelector('.dlg')"), "a free name is saved and the saved map is on the screen")
        s = page.state()
        check(s["tags"] == ["Classic map"] and s["check"] == "Works in the original game", "it is the same Classic map: %s" % s)
        saved = draft(page, "picnic")
        check(saved and saved["name"] == "Picnic" and saved["kind"] == "classic" and saved["length"] == os.path.getsize(tiny), "the draft is in the browser's database with the file's bytes: %s" % saved)
        page.press_act("save")
        dialog(page, "Save as a new map")
        check(page.ev("document.querySelector('.cname input').value") == "Picnic 2", "the next Save offers the next number")
        page.type_into(".cname input", "PICNIC")
        check(page.button_off(".dlg-foot .btn:last-child") and page.ev("document.querySelector('.cname .note .btn').textContent") == "Use \u201cPICNIC 2\u201d", "the name of the draft is taken")
        page.key("Escape")
        check(draft(page, "picnic")["length"] == os.path.getsize(tiny) and not draft(page, "picnic 2"), "nothing else was saved, and the draft is as it was")
        # drawing is kept in the browser's one slot
        page.tool("ground")
        page.tile(487)
        a, b = page.cell_point(3, 3), page.cell_point(3, 12)
        page.drag(a[0], a[1], b[0], b[1], steps=8)
        check(not page.button_off("#undo-btn"), "a line is drawn")
        check(wait_for(lambda: bool(idb_read(page, "work", "current")), 6), "after a rest the drawing is kept in the browser's slot")
        DIALOGS[:] = []
        tab.call("Page.reload", {})
        check(wait_for(page.ready, 20), "the page starts again")
        check(DIALOGS == [], "the browser's own question was not asked: the work is safe in the browser (%s)" % DIALOGS)
        check(dialog(page, "Pick up your unsaved work?") and "Last time you drew on \u201cPicnic\u201d" in page.ev("document.querySelector('.dlg').textContent"), "the next visit asks to pick the work up")
        page.shot(args.shots, "mapmaker-save-pick-up")
        page.ev("[].filter.call(document.querySelectorAll('.dlg button'), function (b) { return b.textContent === 'Pick it up'; })[0].click()")
        check(wait_for(lambda: page.state()["name"] == "Picnic") and page.pixel(*page.cell_point(3, 7)) == page.color_of(487), "the work is back, with the line")
        # the question before a map is left offers Save as a new map first
        page.press_act("new")
        check(dialog(page, "Leave this map?") and page.ev("[].map.call(document.querySelectorAll('.dlg-foot button'), function (b) { return b.textContent; }).join('|')") == "Save as a new map first|Download it first|Leave without saving|Stay here", "picked-up work is not saved: the question offers Save as a new map first")
        page.shot(args.shots, "mapmaker-save-leave")
        page.ev("document.querySelector('.dlg-foot button').click()")
        check(dialog(page, "Save as a new map") and page.ev("document.querySelector('.cname input').value") == "Picnic 2", "it opens Save as a new map")
        page.type_into(".cname input", "Picnic Lines")
        page.click_selector(".dlg-foot .btn:last-child")
        check(dialog(page, "New map") and page.state()["name"] == "Picnic Lines", "the map is saved first, then what was asked for goes on (New map)")
        lines = draft(page, "picnic lines")
        check(lines and lines["length"] > 0 and draft(page, "picnic")["length"] == os.path.getsize(tiny), "a second draft; the first is as it was")
        page.key("Escape")
        check(wait_for(lambda: not idb_read(page, "work", "current"), 6), "a saved map leaves nothing in the slot")
        DIALOGS[:] = []
        tab.call("Page.reload", {})
        wait_for(page.ready, 20)
        check(not page.ev("!!document.querySelector('.dlg')") and DIALOGS == [], "the next visit asks nothing")
        # throw it away
        page.tile(487)
        page.drag(a[0], a[1], b[0], b[1], steps=8)
        wait_for(lambda: bool(idb_read(page, "work", "current")), 6)
        tab.call("Page.reload", {})
        wait_for(page.ready, 20)
        dialog(page, "Pick up your unsaved work?")
        page.ev("[].filter.call(document.querySelectorAll('.dlg button'), function (b) { return b.textContent === 'Throw it away'; })[0].click()")
        check(wait_for(lambda: not idb_read(page, "work", "current"), 6) and page.state()["name"] == "My map", "Throw it away empties the slot and the new map stays")
        # a touch screen
        for w, h in ((390, 844), (844, 390)):
            print("[web map maker] save as a new map on a touch screen, %d x %d" % (w, h))
            tab.emulate(w, h, 2, mobile=True)
            page = Page(tab, args.web)
            check(page.open() and page.ready(), "the page starts")
            page.choose_file(tiny)
            wait_for(lambda: page.state()["name"] == "TINY")
            page.ev("document.querySelector('[data-act=menu]').click()")
            wait_for(lambda: page.ev("!!document.querySelector('.menulist')"))
            page.ev("[].filter.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent === 'Save as new...'; })[0].click()")
            check(dialog(page, "Save as a new map"), "Menu, Save as new... opens the sheet")
            for what in ("free", "taken"):
                if what == "taken":
                    page.type_into(".cname input", "TINY")
                    wait_for(lambda: page.ev("!!document.querySelector('.cname .note.bad')"))
                fit = json.loads(page.ev("""JSON.stringify((function () {
                    var d = document.querySelector('.dlg').getBoundingClientRect(), bad = [];
                    [].forEach.call(document.querySelectorAll('.dlg-foot button'), function (b) { var r = b.getBoundingClientRect(); if (r.bottom > innerHeight + 1 || r.right > innerWidth + 1 || r.left < -1) bad.push(b.textContent); });
                    var small = [].filter.call(document.querySelectorAll('.dlg button, .dlg input[type=text]'), function (b) { var r = b.getBoundingClientRect(); return b.offsetParent !== null && !b.disabled && (r.width < 43.5 || r.height < 43.5); }).map(function (b) { return b.textContent; });
                    return {bottom: d.bottom, right: d.right, bad: bad, small: small, scrollX: document.documentElement.scrollWidth > innerWidth};
                })())"""))
                check(fit["bad"] == [] and fit["bottom"] <= h + 1 and fit["right"] <= w + 1 and not fit["scrollX"], "%d x %d, name %s: the sheet is inside the screen with Cancel and Save in view (%s)" % (w, h, what, fit))
                check(fit["small"] == [], "%d x %d, name %s: every control of the sheet is at least 44 pixels each way (%s)" % (w, h, what, fit["small"]))
                page.shot(args.shots, "mapmaker-save-phone-%s-%dx%d" % (what, w, h))
            page.key("Escape")
        # a browser that keeps nothing
        print("[web map maker] a browser that keeps nothing")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        check(page.open(blocked) and page.ready(), "the page starts although the database is refused")
        page.press_act("save")
        check(dialog(page, "This browser cannot keep maps") and page.ev("[].map.call(document.querySelectorAll('.dlg-foot button'), function (b) { return b.textContent; }).join('|')") == "Download|Close", "Save as new... says so and offers Download")
        page.shot(args.shots, "mapmaker-save-unavailable")
        page.key("Escape")
        a, b = page.cell_point(3, 3), page.cell_point(3, 12)
        page.tool("ground")
        page.tile(487)
        page.drag(a[0], a[1], b[0], b[1], steps=8)
        page.press_act("new")
        check(dialog(page, "Leave this map?") and page.ev("[].map.call(document.querySelectorAll('.dlg-foot button'), function (b) { return b.textContent; }).join('|')") == "Download it first|Leave without saving|Stay here", "the question has no Save as a new map first")
        page.key("Escape")
        DIALOGS[:] = []
        tab.call("Page.reload", {})
        wait_for(lambda: bool(DIALOGS), 6)
        check(DIALOGS == ["beforeunload"], "the browser's own question comes back when nothing can be kept (%s)" % DIALOGS)
    elif part == "mine":
        title = lambda page: page.ev("(document.querySelector('.dlg h2') || {}).textContent || ''")
        dialog = lambda page, text: wait_for(lambda: title(page) == text)
        rows = lambda page: json.loads(page.ev("JSON.stringify([].map.call(document.querySelectorAll('#mine-list .mrow .t1'), function (n) { return n.firstChild.textContent.trim(); }))"))
        shown = lambda page: page.ev("!document.getElementById('mine').hidden")
        click_text = lambda page, scope, text: page.ev("[].filter.call(document.querySelectorAll(%s), function (b) { return b.textContent === %s; })[0].click()" % (json.dumps(scope), json.dumps(text)))
        row_button = lambda page, name, text: page.ev("(function () { var r = [].filter.call(document.querySelectorAll('#mine-list .mrow'), function (n) { return n.querySelector('.t1').firstChild.textContent.trim() === %s; })[0]; "
                                                      "[].filter.call(r.querySelectorAll('button'), function (b) { return b.textContent === %s; })[0].click(); })()" % (json.dumps(name), json.dumps(text)))
        draft = lambda page, key: idb_read(page, "drafts", key)
        print("[web map maker] My maps, 1280 x 800")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        check(page.open() and page.ready(), "the page starts")
        page.press_act("mine")
        check(wait_for(lambda: shown(page)) and not page.ev("document.getElementById('top').offsetParent"), "My maps opens over the editor (the top bar is put away)")
        for _ in range(40):                                              # (the drafts of the other parts of this check: deleted here, through the list's own Delete)
            wait_for(lambda: page.ev("document.getElementById('mine-note').textContent.indexOf('Looking') < 0"), 4)
            if not rows(page):
                break
            row_button(page, rows(page)[0], "Delete\u2026")
            dialog(page, "Delete this saved map?")
            click_text(page, ".dlg-foot button", "Delete it")
            wait_for(lambda: not page.ev("!!document.querySelector('.dlg')"), 4)
        check(rows(page) == [] and "no saved maps yet" in page.ev("document.getElementById('mine-note').textContent") and page.ev("document.getElementById('mine-chips').offsetParent") is None, "nothing saved: a plain sentence and no filters (%s)" % page.ev("document.getElementById('mine-note').textContent"))
        page.shot(args.shots, "mapmaker-mine-empty")
        page.key("Escape")
        check(wait_for(lambda: not shown(page)) and page.ev("!!document.getElementById('top').offsetParent"), "Escape goes back to the map")
        check(page.ev("document.activeElement.getAttribute('data-act')") == "mine", "and the focus is on My maps again")
        page.choose_file(tiny)
        check(wait_for(lambda: page.state()["name"] == "TINY"), "TINY is open")
        size = "%s x %s" % (page.state()["rows"], page.state()["columns"])
        page.press_act("save")
        dialog(page, "Save as a new map")
        page.type_into(".cname input", "Mine A")
        page.click_selector(".dlg-foot .btn:last-child")
        check(wait_for(lambda: page.state()["name"] == "Mine A"), "Mine A is saved")
        page.press_act("convert")
        check(dialog(page, "Start an Extended map from this one?"), "an Extended map from it")
        page.click_selector(".dlg-foot .btn:last-child")
        check(wait_for(lambda: page.state()["tags"] == ["Extended map", "New version only"]), "the Extended map is on the screen")
        page.press_act("save")
        dialog(page, "Save as a new map")
        page.type_into(".cname input", "Mine B")
        page.click_selector(".dlg-foot .btn:last-child")
        check(wait_for(lambda: page.state()["name"] == "Mine B"), "Mine B is saved")
        page.press_act("mine")
        check(wait_for(lambda: len(rows(page)) == 2 and page.ev("[].every.call(document.querySelectorAll('#mine-list .t3'), function (n) { return n.textContent.length > 0; })")), "the list has the two saved maps")
        check(rows(page) == ["Mine B", "Mine A"], "newest first: %s" % rows(page))
        check(page.ev("[].map.call(document.querySelectorAll('#mine-chips button'), function (b) { return b.textContent; }).join('|')") == "All (2)|Classic (1)|Extended (1)", "the filters count the kinds")
        check(page.ev("document.querySelector('#mine-list .mrow .t3').textContent") == "Works in the new version" and page.ev("!!document.querySelector('#mine-list .mrow .badge-nv')") and page.ev("document.querySelector('#mine-list .mrow .t2').textContent").startswith(size), "a saved Extended map has its badge, its size and its verdict")
        check(page.ev("[].every.call(document.querySelectorAll('#mine-list .mthumb'), function (n) { var c = n.querySelector('canvas'); return !!c && c.width > 0; })"), "each has the small picture of its map")
        page.shot(args.shots, "mapmaker-mine-list")
        click_text(page, "#mine-chips button", "Extended (1)")
        check(rows(page) == ["Mine B"] and page.ev("document.querySelector('#mine-chips [aria-pressed=true]').textContent") == "Extended (1)", "the Extended filter shows the Extended map only")
        click_text(page, "#mine-chips button", "All (2)")
        page.key("e")
        check(page.ev("document.querySelector('[data-tool=ground]').getAttribute('aria-pressed')") == "true", "a tool's letter does nothing while the list is open")
        # Open: the saved map becomes the working map, with its own history; drawing first asks
        row_button(page, "Mine A", "Open")
        check(wait_for(lambda: page.state()["name"] == "Mine A" and not shown(page)) and page.button_off("#undo-btn"), "Open puts the saved map on the screen as the working map (no history), and the list closes")
        page.press_act("save")
        dialog(page, "Save as a new map")
        check(page.ev("document.querySelector('.cname input').value") == "Mine A 2", "saving it again is always a new map: the next number is offered (and the name Mine A is taken)")
        page.type_into(".cname input", "mine a")
        check(page.button_off(".dlg-foot .btn:last-child") and "Use \u201cmine a 2\u201d" in page.ev("document.querySelector('.cname .note .btn').textContent"), "its own name is refused, with the next free name")
        page.key("Escape")
        page.tool("ground")
        page.tile(487)
        a, b = page.cell_point(3, 3), page.cell_point(3, 12)
        page.drag(a[0], a[1], b[0], b[1], steps=8)
        check(not page.button_off("#undo-btn"), "a line is drawn on it")
        page.press_act("mine")
        wait_for(lambda: len(rows(page)) == 2)
        row_button(page, "Mine B", "Open")
        check(dialog(page, "Leave this map?") and shown(page), "Open asks about the unsaved drawing first, and the list stays behind the question")
        click_text(page, ".dlg-foot button", "Stay here")
        check(shown(page) and page.state()["name"] == "Mine A" and not page.button_off("#undo-btn"), "Stay here changes nothing")
        # Delete: the question first
        row_button(page, "Mine B", "Delete\u2026")
        check(dialog(page, "Delete this saved map?") and page.ev("document.activeElement.textContent") == "Keep it", "Delete... asks first, and the focus is on Keep it")
        check("\u201cMine B\u201d will be removed from this browser" in page.ev("document.querySelector('.dlg').textContent"), "it names the map")
        page.shot(args.shots, "mapmaker-mine-delete")
        click_text(page, ".dlg-foot button", "Keep it")
        check(rows(page) == ["Mine B", "Mine A"] and draft(page, "mine b"), "Keep it deletes nothing")
        row_button(page, "Mine B", "Delete\u2026")
        dialog(page, "Delete this saved map?")
        click_text(page, ".dlg-foot button", "Delete it")
        check(wait_for(lambda: rows(page) == ["Mine A"]) and not draft(page, "mine b") and draft(page, "mine a"), "Delete it removes that one saved map from the browser's database, and no other")
        check(page.ev("document.getElementById('mine-status').textContent").endswith("was deleted.") and page.ev("document.querySelector('#mine-chips button').textContent") == "All (1)", "the deletion is said and the counts follow")
        # Copy... starts from the saved map, not from the map on the screen
        row_button(page, "Mine A", "Copy\u2026")
        check(dialog(page, "Copy this map") and page.ev("document.querySelector('.srccard .sn').textContent") == "Mine A" and page.ev("document.querySelector('.cname input').value") == "Mine A 2", "Copy... starts from the saved map, under the next free name")
        click_text(page, ".dlg-foot button", "Make new map")
        check(dialog(page, "Leave this map?"), "the question about the unsaved drawing comes before the copy takes the screen")
        click_text(page, ".dlg-foot button", "Leave without saving")
        check(wait_for(lambda: page.state()["name"] == "Mine A 2" and not shown(page)), "the copy is on the screen and the list is closed")
        # a touch screen
        for w, h, name in ((390, 844, "phone-390x844"), (844, 390, "phone-844x390"), (340, 700, "phone-340x700")):
            print("[web map maker] My maps, a touch screen, %d x %d" % (w, h))
            tab.emulate(w, h, 2, mobile=True)
            page = Page(tab, args.web)
            check(page.open() and page.ready(), "the page starts")
            page.press_act("menu")
            check(wait_for(lambda: page.ev("!!document.querySelector('.menulist')")), "Menu opens")
            check(page.ev("[].map.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent; }).join('|')") == "New|Open|My maps|Copy...|Save as new...|Properties...|Download|Submit for approval...|My submissions", "the Menu has My maps after Open, and the two ways to send a map at its end")
            click_text(page, ".menulist .btn", "My maps")
            check(wait_for(lambda: shown(page) and len(rows(page)) >= 1 and page.ev("[].every.call(document.querySelectorAll('#mine-list .t3'), function (n) { return n.textContent.length > 0; })")), "My maps opens")
            bad = json.loads(page.ev("""JSON.stringify([].filter.call(document.querySelectorAll('#mine button'), function (b) { return b.offsetParent !== null; }).map(function (b) {
                var r = b.getBoundingClientRect(); return {t: b.textContent, w: Math.round(r.width), h: Math.round(r.height), out: r.left < -0.5 || r.right > innerWidth + 0.5}; }).filter(function (x) { return x.h < 44 || x.w < 44 || x.out; }))"""))
            check(bad == [] and page.ev("document.documentElement.scrollWidth <= innerWidth"), "every control is 44 pixels and inside the screen, and nothing scrolls sideways (%s)" % bad)
            page.shot(args.shots, "mapmaker-mine-%s" % name)
            row_button(page, rows(page)[0], "Delete\u2026")
            check(dialog(page, "Delete this saved map?") and page.ev("(function () { var d = document.querySelector('.dlg').getBoundingClientRect(); return d.left >= -0.5 && d.right <= innerWidth + 0.5 && d.bottom <= innerHeight + 0.5; })()"), "the question is a sheet inside the screen")
            page.shot(args.shots, "mapmaker-mine-delete-%s" % name)
            click_text(page, ".dlg-foot button", "Keep it")
        # a browser that keeps nothing
        tab.emulate(1280, 800, 1)
        blocked = "Object.defineProperty(window, 'indexedDB', { get: function () { throw new Error('blocked'); } });"
        page = Page(tab, args.web)
        check(page.open(blocked) and page.ready(), "the page starts with the browser's database blocked")
        page.press_act("mine")
        check(wait_for(lambda: "keeps no saved maps" in page.ev("document.getElementById('mine-note').textContent")) and "Download" in page.ev("document.getElementById('mine-note').textContent"), "a browser that keeps nothing says so plainly")
        page.shot(args.shots, "mapmaker-mine-unavailable")
        click_text(page, "#mine-new", "New map")
        check(dialog(page, "New map"), "and the page still works: New map opens")
        page.key("Escape")
    elif part == "submit":
        fake = """(function () {
            var real = window.fetch, srv = window.__srv = { log: [], list: [], off: false, html: false, token: 'T'.repeat(43), n: 0, mapsList: {format: 1, maps: [{file: 'POPcOrN.lvl'}, {file: 'TINY.LVL'}]} };
            function reply(status, json, html) { return Promise.resolve(new Response(html ? '<html><body>' + status + ' page of nginx</body></html>' : JSON.stringify(json), {status: status, headers: {'Content-Type': html ? 'text/html' : 'application/json'}})); }
            window.fetch = function (u, init) {
                u = String(u); init = init || {};
                if (u === '/maps') return reply(200, srv.mapsList);
                if (u.indexOf('/submissions') !== 0) return real.apply(this, arguments);
                var method = init.method || 'GET', auth = init.headers && init.headers.Authorization, body = init.body;
                srv.log.push({method: method, url: u, auth: auth || '', type: (init.headers || {})['Content-Type'] || '', length: body ? body.length : 0, first: body ? body[0] : -1});
                if (srv.off) return reply(503, {error: 'submissions_off', message: 'Sending maps is not switched on right now.'});
                if (srv.html && method === 'POST') return reply(503, null, true);
                if (method === 'GET') return auth ? reply(200, {submissions: srv.list}) : reply(401, {error: 'no_token', message: 'x'});
                var w = /^\\/submissions\\/([0-9a-f]{32})\\/withdraw$/.exec(u);
                if (w) { srv.list = srv.list.filter(function (s) { return s.id !== w[1]; }); return reply(200, {submission: {id: w[1], state: 'withdrawn'}}); }
                var q = {}; u.split('?')[1].split('&').forEach(function (p) { var kv = p.split('='); q[kv[0]] = decodeURIComponent(kv[1] || ''); });
                var id = ('0' + (++srv.n)).repeat(16), s = {id: id, state: 'waiting', kind: q.kind, name: q.name, width: 31, height: 31, sent_at: Math.floor(Date.now() / 1000)};
                srv.list.unshift(s);
                var out = {submission: s, name_taken: q.name.toLowerCase() === 'tiny', token_is_new: !auth};
                if (!auth) out.token = srv.token;
                return reply(201, out);
            };
        })();"""
        title = lambda page: page.ev("(document.querySelector('.dlg h2') || {}).textContent || ''")
        dialog = lambda page, text: wait_for(lambda: title(page) == text)
        srv = lambda page, expr: page.ev("JSON.stringify(window.__srv.%s)" % expr)
        log = lambda page: json.loads(srv(page, "log"))
        text_of = lambda page, selector: page.ev("(document.querySelector(%s) || {textContent: ''}).textContent" % json.dumps(selector))
        click_text = lambda page, scope, text: page.ev("[].filter.call(document.querySelectorAll(%s), function (b) { return b.textContent === %s; })[0].click()" % (json.dumps(scope), json.dumps(text)))
        bad_controls = lambda page, scope: json.loads(page.ev("""JSON.stringify([].filter.call(document.querySelectorAll(%s + ' button, ' + %s + ' input, ' + %s + ' textarea'), function (b) { return b.offsetParent !== null; }).map(function (b) {
            var r = b.getBoundingClientRect(); return {t: b.textContent || b.id, w: Math.round(r.width), h: Math.round(r.height), out: r.left < -0.5 || r.right > innerWidth + 0.5}; }).filter(function (x) { return x.h < 44 || x.out; }))""" % ((json.dumps(scope),) * 3)))
        inside = lambda page: page.ev("(function () { var d = document.querySelector('.dlg').getBoundingClientRect(); return d.left >= -0.5 && d.right <= innerWidth + 0.5 && d.top >= -0.5 && d.bottom <= innerHeight + 0.5; })()")
        send_off = lambda page: page.ev("[].filter.call(document.querySelectorAll('.dlg-foot button'), function (b) { return b.textContent === 'Submit for approval'; })[0].disabled")
        rows = lambda page: json.loads(page.ev("JSON.stringify([].map.call(document.querySelectorAll('#mine-list .mrow'), function (r) { return r.textContent; }))"))
        tab_click = lambda page, prefix: page.ev("[].filter.call(document.querySelectorAll('#mine-tabs button'), function (b) { return b.textContent.indexOf(%s) === 0; })[0].click()" % json.dumps(prefix))
        tiny_size = os.path.getsize(tiny)
        print("[web map maker] Submit for approval and My submissions, 1280 x 800")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        check(page.open(fake) and page.ready(), "the page starts")
        check(page.ev("typeof MapSubmit") == "undefined" and page.ev("!document.querySelector('script[src*=mapmaker_submit]')"), "the script that sends maps is not loaded yet")
        check(page.ev("!!document.querySelector('[data-act=submit]') && document.querySelector('[data-act=submit]').textContent") == "Submit for approval...", "the top bar has Submit for approval...")
        page.press_act("submit")
        check(dialog(page, "Submit for approval") and page.ev("typeof MapSubmit") == "object", "the button loads the script and opens the dialog")
        blank_works = page.ev("document.getElementById('check-dot').classList.contains('ok')")
        if not blank_works:
            check(send_off(page) and page.ev("!!document.querySelector('.dlg .subfin li') && document.querySelector('.dlg .note.bad') !== null"), "a map that does not work: the reasons are listed and the button is off")
            check("cannot be submitted" in text_of(page, ".dlg .note.bad"), "with the sentence of the picture: %s" % text_of(page, ".dlg .note.bad"))
            page.shot(args.shots, "mapmaker-submit-blocked")
        page.key("Escape")
        page.choose_file(tiny)
        check(wait_for(lambda: page.state()["name"] == "TINY"), "TINY is open")
        page.press_act("submit")
        check(dialog(page, "Submit for approval") and wait_for(lambda: not page.ev("document.querySelector('.dlg #sub-name').disabled")), "the dialog opens for a map that works")
        check(page.ev("document.activeElement.id") == "sub-name" and page.ev("document.querySelector('#sub-name').value") == "TINY", "the focus is on the name, which is the map's")
        check(wait_for(lambda: "already in the game" in text_of(page, ".dlg .note.warn")) and page.ev("document.querySelector('.dlg .note.bad') === null"), "TINY is a name of the game's list: told in a yellow note, not an error (%s)" % text_of(page, ".dlg"))
        check(send_off(page), "no player name yet: the button waits")
        page.type_into("#sub-maker", "Mia")
        check(wait_for(lambda: not send_off(page)), "with a name the button works")
        page.shot(args.shots, "mapmaker-submit-name-taken")
        page.type_into("#sub-name", "Sunny Picnic")
        check(wait_for(lambda: "No map in the list has this name." in text_of(page, ".dlg .cname")), "a free name: 'No map in the list has this name.'")
        page.type_into("#sub-note", "A map with a lake + a hill & 'quotes'")
        check(inside(page) and bad_controls(page, ".dlg") == [] and page.ev("document.documentElement.scrollWidth <= innerWidth"), "the dialog is inside the screen, every control is 44 pixels (%s)" % bad_controls(page, ".dlg"))
        page.shot(args.shots, "mapmaker-submit-dialog")
        click_text(page, ".dlg-foot button", "Submit for approval")
        check(dialog(page, "Submitted"), "after sending: the dialog 'Submitted'")
        posts = [c for c in log(page) if c["method"] == "POST"]
        check(len(posts) == 1 and posts[0]["url"] == "/submissions?kind=classic&name=Sunny%20Picnic&maker=Mia&note=A%20map%20with%20a%20lake%20%2B%20a%20hill%20%26%20'quotes'", "ONE call, a space is %%20, a plus %%2B, an ampersand %%26 (%s)" % [c["url"] for c in posts])
        check(posts[0]["length"] == tiny_size and posts[0]["type"] == "application/octet-stream" and posts[0]["auth"] == "", "the body is the file's own bytes (%d), as a file, with no token the first time (%s)" % (tiny_size, posts[0]))
        check("Sent. Waiting for approval." in text_of(page, ".dlg") and "\u201cSunny Picnic\u201d is with the owner now." in text_of(page, ".dlg"), "'Sent. Waiting for approval.' and the name: %s" % text_of(page, ".dlg"))
        page.shot(args.shots, "mapmaker-submit-sent")
        stored = page.ev("(function () { window.__k = undefined; var q = indexedDB.open('ants-mapmaker-sent', 1); q.onsuccess = function () { var g = q.result.transaction('sent', 'readonly').objectStore('sent').getAll(); g.onsuccess = function () { window.__k = g.result.map(function (r) { return r.id.charAt(0) !== '#' ? 'map:' + r.name + ':' + (r.bytes && r.bytes.length) : r.id; }).sort().join('|'); }; }; })()")
        wait_for(lambda: page.ev("window.__k !== undefined"), 5, 0.05)
        check(page.ev("window.__k") == "#maker|#tokens|map:Sunny Picnic:%d" % tiny_size, "the browser keeps the token, the player's name and the player's own copy: %s" % page.ev("window.__k"))
        click_text(page, ".dlg-foot button", "See my submissions")
        check(wait_for(lambda: not page.ev("document.getElementById('mine').hidden") and page.ev("document.getElementById('mine-title').textContent") == "My submissions"), "See my submissions opens My submissions")
        check(wait_for(lambda: len(rows(page)) == 1), "one row")
        check(page.ev("[].map.call(document.querySelectorAll('#mine-tabs button'), function (b) { return b.textContent; }).join('|')") == "My maps|My submissions (1)", "two tabs: %s" % page.ev("[].map.call(document.querySelectorAll('#mine-tabs button'), function (b) { return b.textContent; }).join('|')"))
        check("Waiting for approval" in rows(page)[0] and "Sunny Picnic" in rows(page)[0] and "Classic map" in rows(page)[0] and "31 x 31" in rows(page)[0] and "Sent today" in rows(page)[0], "Waiting, with its name, kind, size and day: %s" % rows(page)[0])
        check(page.ev("[].map.call(document.querySelectorAll('#mine-list .mrow button'), function (b) { return b.textContent; }).join('|')") == "Withdraw" and "never replaced" in text_of(page, "#mine-note"), "Withdraw only, and the line 'An approved map is never replaced'")
        # the four answers
        first = json.loads(srv(page, "list[0]"))
        page.ev("""(function () { var s = window.__srv, a = s.list[0], now = Math.floor(Date.now() / 1000);
            s.list = [a,
              {id: 'b'.repeat(32), state: 'approved', kind: 'extended', name: 'Big Lake', width: 400, height: 300, sent_at: now - 86400 * 3, renamed_by_owner: true, approved_name: 'Big Lake 2', message: 'The name was taken. <b>Thank you</b> for the map!'},
              {id: 'c'.repeat(32), state: 'approved', kind: 'classic', name: 'Hidden Ponds', width: 40, height: 40, sent_at: now - 86400 * 4},
              {id: 'd'.repeat(32), state: 'declined', kind: 'classic', name: 'Gauntlet practice', width: 60, height: 60, sent_at: now - 86400 * 5, message: 'Too much like the Gauntlet map. Please change the middle.'}]; })()""")
        tab_click(page, "My maps")
        wait_for(lambda: page.ev("document.getElementById('mine-title').textContent") == "My maps")
        tab_click(page, "My submissions")
        check(wait_for(lambda: len(rows(page)) == 4), "four rows after the owner has answered")
        r = rows(page)
        check("Approved as \u201cBig Lake 2\u201d (renamed by the owner)" in r[1] and "Extended map" in r[1] and "300 x 400" in r[1], "renamed by the owner: %s" % r[1])
        check("The owner wrote: The name was taken. <b>Thank you</b> for the map!" in r[1] and page.ev("document.querySelectorAll('#mine-list b').length") == 0, "the owner's words are text, never markup")
        check("Approved" in r[2] and "renamed" not in r[2] and "Declined" in r[3] and "The owner wrote: Too much like the Gauntlet map." in r[3], "Approved and Declined with the owner's reason")
        check(page.ev("[].map.call(document.querySelectorAll('#mine-list .mrow'), function (r) { return [].map.call(r.querySelectorAll('button'), function (b) { return b.textContent; }).join('+'); }).join('|')") == "Withdraw|||", "buttons: Withdraw for the waiting one, none for the answered ones that came from elsewhere (this browser has no copy of them)")
        page.shot(args.shots, "mapmaker-submit-my-submissions")
        check(bad_controls(page, "#mine") == [] and page.ev("document.documentElement.scrollWidth <= innerWidth"), "every control is 44 pixels, nothing scrolls sideways (%s)" % bad_controls(page, "#mine"))
        # the status link
        click_text(page, "#mine-link", "Copy my status link")
        check(wait_for(lambda: page.ev("document.getElementById('mine-link').textContent") == "Link copied" or page.ev("!!document.querySelector('.dlg input')")), "Copy my status link copies it, or shows it to copy by hand")
        if page.ev("!!document.querySelector('.dlg input')"):
            check(page.ev("document.querySelector('.dlg input').value").endswith("#sent=" + "T" * 43) and "keep it to yourself" in text_of(page, ".dlg"), "the link has the token in the fragment, with the warning")
            page.key("Escape")
        # Withdraw asks first
        click_text(page, "#mine-list button", "Withdraw")
        check(dialog(page, "Withdraw this map?") and not [c for c in log(page) if "withdraw" in c["url"]], "Withdraw asks first and sends nothing")
        click_text(page, ".dlg-foot button", "Keep it waiting")
        click_text(page, "#mine-list button", "Withdraw")
        dialog(page, "Withdraw this map?")
        click_text(page, ".dlg-foot button", "Withdraw it")
        check(wait_for(lambda: len(rows(page)) == 3) and len([c for c in log(page) if "withdraw" in c["url"]]) == 1 and log(page)[-1]["auth"] == "Bearer " + "T" * 43, "Withdraw is one call with the token, and the row goes")
        # a second map carries the token
        page.key("Escape")
        check(wait_for(lambda: page.ev("document.getElementById('mine').hidden")), "Escape goes back to the map")
        page.press_act("submit")
        check(dialog(page, "Submit for approval") and page.ev("document.querySelector('#sub-maker') === null") and "Mia" in text_of(page, ".dlg .subwho"), "the player's name is remembered (and can be changed)")
        page.type_into("#sub-name", "Sunny Picnic Two")
        check(wait_for(lambda: not send_off(page)), "the button works")
        click_text(page, ".dlg-foot button", "Submit for approval")
        check(dialog(page, "Submitted") and log(page)[-1]["auth"] == "Bearer " + "T" * 43 and log(page)[-1]["method"] == "POST", "the next send carries the token")
        check(all((c["method"], c["url"].split("?")[0]) in (("GET", "/submissions"), ("POST", "/submissions")) or c["url"].endswith("/withdraw") for c in log(page)), "only GET /submissions, POST /submissions and the withdraw of a waiting map were called")
        click_text(page, ".dlg-foot button", "Back to the editor")
        # Open my copy: the own copy of the map comes back as the working map (the owner has answered the second map)
        page.ev("window.__srv.list[0].state = 'approved'")
        page.press_act("mine")
        wait_for(lambda: not page.ev("document.getElementById('mine').hidden"))
        tab_click(page, "My submissions")
        check(wait_for(lambda: page.ev("[].some.call(document.querySelectorAll('#mine-list button'), function (b) { return b.textContent === 'Open my copy'; })")), "an answered map has Open my copy")
        click_text(page, "#mine-list button", "Open my copy")
        check(wait_for(lambda: page.state()["name"] == "Sunny Picnic Two" and page.ev("document.getElementById('mine').hidden")) and page.state()["check"] == "Works in the original game", "Open my copy puts the player's own copy on the screen as the working map")
        # the pages of nginx and a door that is off
        page.ev("window.__srv.html = true")
        page.press_act("submit")
        dialog(page, "Submit for approval")
        wait_for(lambda: not send_off(page))
        click_text(page, ".dlg-foot button", "Submit for approval")
        check(wait_for(lambda: "too many tries or the server is busy" in text_of(page, ".dlg")) and title(page) == "Submit for approval" and not send_off(page), "a page of nginx (503 HTML) is said in plain words, and the player can try again: %s" % text_of(page, "#sub-err"))
        page.key("Escape")
        page.ev("window.__srv.html = false; window.__srv.off = true")
        page.press_act("submit")
        check(dialog(page, "Submit for approval") and wait_for(lambda: "not switched on right now" in text_of(page, ".dlg")) and send_off(page), "a door that is off: a plain line and the button off")
        check(page.ev("document.querySelector('.dlg .note.bad') === null") and page.ev("!!document.querySelector('.dlg #sub-name') && !!document.querySelector('.dlg-foot')"), "not an error, and nothing else is hidden")
        page.shot(args.shots, "mapmaker-submit-off")
        page.key("Escape")
        page.press_act("mine")
        wait_for(lambda: not page.ev("document.getElementById('mine').hidden"))
        tab_click(page, "My submissions")
        check(wait_for(lambda: "not switched on right now" in text_of(page, "#mine-list")) and len(rows(page)) >= 3, "My submissions with the door off: one plain line, and the list this browser last knew")
        # a status link on another device: only the token comes along
        tab.call("Storage.clearDataForOrigin", {"origin": "/".join(args.web.split("/")[:3]), "storageTypes": "indexeddb"})
        tab.call("Page.navigate", {"url": "about:blank"})               # (a link that only adds a fragment to the open page would not start it again)
        time.sleep(0.5)
        page2 = Page(tab, args.web)
        page2.url += "#sent=" + "T" * 43
        check(page2.open(fake) and page2.ready(), "the page starts from a status link")
        page2.ev("window.__srv.list = [{id: 'e'.repeat(32), state: 'approved', kind: 'classic', name: 'Elsewhere', width: 20, height: 20, sent_at: Math.floor(Date.now() / 1000)}]")
        check(wait_for(lambda: not page2.ev("document.getElementById('mine').hidden") and page2.ev("document.getElementById('mine-title').textContent") == "My submissions"), "it opens My submissions")
        check(wait_for(lambda: page2.ev("location.hash") == "") and page2.ev("location.href").endswith("/mapmaker.html"), "and the address has lost the token (%s)" % page2.ev("location.href"))
        # the phone
        print("[web map maker] Submit for approval and My submissions, a touch screen, 390 x 844")
        tab.emulate(390, 844, 2, mobile=True)
        tab.call("Storage.clearDataForOrigin", {"origin": "/".join(args.web.split("/")[:3]), "storageTypes": "indexeddb"})
        page = Page(tab, args.web)
        check(page.open(fake) and page.ready(), "the page starts")
        page.choose_file(tiny)
        check(wait_for(lambda: page.state()["name"] == "TINY"), "TINY is open")
        page.press_act("menu")
        check(wait_for(lambda: page.ev("!!document.querySelector('.menulist')")) and page.ev("[].map.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent; }).slice(-2).join('|')") == "Submit for approval...|My submissions", "the Menu ends with Submit for approval... and My submissions")
        page.shot(args.shots, "mapmaker-submit-menu-phone")
        click_text(page, ".menulist .btn", "Submit for approval...")
        check(dialog(page, "Submit for approval"), "the dialog opens")
        page.type_into("#sub-maker", "Mia")
        wait_for(lambda: not send_off(page))
        check(inside(page) and page.ev("document.documentElement.scrollWidth <= innerWidth") and bad_controls(page, ".dlg") == [], "the sheet is inside the width, every control is 44 pixels, nothing scrolls sideways (%s)" % bad_controls(page, ".dlg"))
        page.shot(args.shots, "mapmaker-submit-dialog-phone")
        page.ev("document.querySelector('.dlg-in').scrollTop = 99999")
        check(page.ev("(function () { var f = document.querySelector('.dlg-foot').getBoundingClientRect(); return f.bottom <= innerHeight + 0.5 && f.top >= 0; })()"), "Cancel and Submit stay in view (what does not fit scrolls inside the sheet)")
        click_text(page, ".dlg-foot button", "Submit for approval")
        check(dialog(page, "Submitted") and inside(page), "Sent: the sheet is inside the screen")
        page.shot(args.shots, "mapmaker-submit-sent-phone")
        click_text(page, ".dlg-foot button", "See my submissions")
        check(wait_for(lambda: len(rows(page)) == 1) and page.ev("document.documentElement.scrollWidth <= innerWidth") and bad_controls(page, "#mine") == [], "My submissions on a phone: inside the width, every control is 44 pixels (%s)" % bad_controls(page, "#mine"))
        page.ev("(function () { var s = window.__srv, a = s.list[0]; s.list = [a, {id: 'b'.repeat(32), state: 'approved', kind: 'extended', name: 'Big Lake', width: 400, height: 300, sent_at: 1700000000, renamed_by_owner: true, approved_name: 'Big Lake 2', message: 'The name was taken, so I added a number. Thank you for the map!'}, {id: 'd'.repeat(32), state: 'declined', kind: 'classic', name: 'Gauntlet practice', width: 60, height: 60, sent_at: 1699000000, message: 'Too much like the Gauntlet map that is already in the list. Please change the middle and send it again.'}]; })()")
        tab_click(page, "My maps")
        wait_for(lambda: page.ev("document.getElementById('mine-title').textContent") == "My maps")
        tab_click(page, "My submissions")
        check(wait_for(lambda: len(rows(page)) == 3) and page.ev("document.documentElement.scrollWidth <= innerWidth") and bad_controls(page, "#mine") == [], "three rows on a phone: nothing scrolls sideways, every control is 44 pixels (%s)" % bad_controls(page, "#mine"))
        page.shot(args.shots, "mapmaker-submit-my-submissions-phone")
        print("[web map maker] Submit for approval, a phone held sideways, 844 x 390")
        tab.emulate(844, 390, 2, mobile=True)
        tab.call("Storage.clearDataForOrigin", {"origin": "/".join(args.web.split("/")[:3]), "storageTypes": "indexeddb"})
        page = Page(tab, args.web)
        check(page.open(fake) and page.ready(), "the page starts")
        page.choose_file(tiny)
        check(wait_for(lambda: page.state()["name"] == "TINY"), "TINY is open")
        page.press_act("menu")
        wait_for(lambda: page.ev("!!document.querySelector('.menulist')"))
        click_text(page, ".menulist .btn", "Submit for approval...")
        check(dialog(page, "Submit for approval"), "the dialog opens")
        page.type_into("#sub-maker", "Mia")
        check(wait_for(lambda: not send_off(page)), "the button works")
        check(page.ev("(function () { var d = document.querySelector('.dlg').getBoundingClientRect(), f = document.querySelector('.dlg-foot').getBoundingClientRect(); return d.left >= -0.5 && d.right <= innerWidth + 0.5 && d.bottom <= innerHeight + 0.5 && f.bottom <= innerHeight + 0.5 && f.top >= 0; })()") and bad_controls(page, ".dlg-foot") == [], "the sheet is inside the screen with Cancel and Submit in view, 44 pixels (%s)" % bad_controls(page, ".dlg-foot"))
        page.shot(args.shots, "mapmaker-submit-dialog-landscape")
    elif part == "narrow":
        for w, h in ((620, 700), (700, 600), (767, 700), (1024, 768), (1100, 700), (1150, 768), (1200, 768), (1201, 768), (1240, 768), (1280, 800), (1330, 800), (1366, 768)):
            print("[web map maker] the top bar, %d x %d" % (w, h))
            tab.emulate(w, h, 1)
            page = Page(tab, args.web)
            check(page.open() and page.ready(), "the page starts")
            found = json.loads(page.ev("""JSON.stringify((function () {
                var name = document.getElementById('map-name').getBoundingClientRect(), tags = document.getElementById('top-tags').getBoundingClientRect(), out = [], hit = [];
                [].forEach.call(document.querySelectorAll('.edmenu .btn'), function (b) {
                    if (b.offsetParent === null) return;
                    var r = b.getBoundingClientRect();
                    if (r.left < -0.5 || r.right > innerWidth + 0.5) out.push(b.textContent);
                    [name, tags].forEach(function (n) { if (r.left < n.right && r.right > n.left && r.top < n.bottom && r.bottom > n.top) hit.push(b.textContent); });
                });
                return {out: out, hit: hit, scrollX: document.documentElement.scrollWidth > innerWidth, n: [].filter.call(document.querySelectorAll('.edmenu .btn'), function (b) { return b.offsetParent !== null; }).length};
            })())"""))
            check(found["n"] == 8 and found["out"] == [] and found["hit"] == [] and not found["scrollX"], "%d x %d: eight buttons (Submit for approval... is the last), none cut, none over the name box or the tags, no sideways scroll (%s)" % (w, h, found))
            page.shot(args.shots, "mapmaker-topbar-%dx%d" % (w, h))
            # My submissions is the second tab of My maps: both tabs, the buttons of the screen and the page stay inside the window
            page.press_act("mine")
            wait_for(lambda: page.ev("!document.getElementById('mine').hidden && document.querySelectorAll('#mine-tabs button').length === 2"))
            tabs = json.loads(page.ev("""JSON.stringify({n: document.querySelectorAll('#mine-tabs button').length, out: [].filter.call(document.querySelectorAll('#mine-tabs button, #mine-back, #mine-new'), function (b) { var r = b.getBoundingClientRect(); return r.left < -0.5 || r.right > innerWidth + 0.5; }).length, scrollX: document.documentElement.scrollWidth > innerWidth})"""))
            check(tabs == {"n": 2, "out": 0, "scrollX": False}, "%d x %d: My maps has its two tabs, inside the window, and nothing scrolls sideways (%s)" % (w, h, tabs))
            page.key("Escape")
    elif part == "sprites":
        run_sprites(tab, args, check)
    elif part == "edges":
        tab.emulate(1280, 800, 1)
        run_edges(Page(tab, args.web), tab, args, check)
    elif part == "play":
        import web_mapmaker_play_browser as play                      # (Play this map: its own file, this one is at the limit of lines)
        tab.emulate(1280, 800, 1)
        play.run_play(Page(tab, args.web), tab, args, check, sys.modules[__name__])
        phone = Tab(tab_browser(tab))
        try:
            play.run_play_phone(Page(phone, args.web), phone, args, check, sys.modules[__name__])
        finally:
            phone.close()
    elif part == "layer":
        print("[web map maker] the kept layer of the sprite canvas, 1280 x 800")
        tab.emulate(1280, 800, 1)
        page = Page(tab, args.web)
        check(page.open() and page.ready(), "the page starts")
        check(wait_for(lambda: page.ev("!!window.MapSprites"), 10), "the sprite script is loaded")
        run_layer(page, check)
    elif part == "panel":
        for w, h in ((1280, 800), (1366, 768), (1280, 720)):
            print("[web map maker] the tools column, %d x %d, classic scrollbars" % (w, h))
            tab.emulate(w, h, 1)
            page = Page(tab, args.web)
            check(page.open() and page.ready(), "the page starts")
            page.ev("(function () { var s = document.createElement('style'); s.textContent = '*::-webkit-scrollbar { width: 15px; height: 15px; }'; document.head.appendChild(s); })()")
            for tool in ("ground", "objects", "bigger", "hills", "ants", "food", "erase", "move"):
                page.tool(tool)
                time.sleep(0.1)
                found = json.loads(page.ev("""JSON.stringify((function () {
                    var p = document.querySelector('.tools'), inner = p.getBoundingClientRect().left + p.clientLeft + p.clientWidth, outside = [];
                    [].forEach.call(p.querySelectorAll('button'), function (b) { if (b.offsetParent === null) return; var r = b.getBoundingClientRect(); if (r.right > inner + 0.5 || r.left < p.getBoundingClientRect().left) outside.push(b.id || b.textContent); });
                    var note = document.getElementById('tool-hint');
                    return {wide: p.scrollWidth > p.clientWidth, tall: p.scrollHeight > p.clientHeight, outside: outside, note: note.offsetParent !== null};
                })())"""))
                check(not found["wide"] and not found["tall"] and found["outside"] == [], "%d x %d, %s: the tools column has no scrollbar and every button is inside it (%s)" % (w, h, tool, found))
                if h >= 800:
                    check(found["note"], "%d x %d, %s: the note under the palette shows" % (w, h, tool))
            if (w, h) == (1280, 720):
                page.tool("ants")
                page.shot(args.shots, "mapmaker-panel-1280x720-ants")
            # the side panel with its summary, with the longest description a map can have: nothing is cut or hidden under a scrollbar, the Check button is inside it
            page.press_act("new")
            wait_for(lambda: page.ev("!!document.querySelector('.kinds')"))
            page.ev("document.querySelectorAll('.kind input')[1].click()")
            page.type_into("#size-rows", "40")
            page.type_into("#size-columns", "60")
            page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
            wait_for(lambda: page.state()["rows"] == "40")
            page.press_act("props")
            check(wait_for(lambda: page.ev("!!document.querySelector('.dlg.props')")), "%d x %d: the properties open" % (w, h))
            dlg = json.loads(page.ev("JSON.stringify((function () { var d = document.querySelector('.dlg').getBoundingClientRect(), i = document.querySelector('.dlg-in'); return {top: d.top, bottom: d.bottom, tall: i.scrollHeight > i.clientHeight}; })())"))
            check(dlg["top"] >= 0 and dlg["bottom"] <= h + 1 and not dlg["tall"], "%d x %d: the properties box fits the screen with no scrollbar (%s)" % (w, h, dlg))
            page.type_into("#p-desc", "The longest text that an Extended map can hold is two hundred and fifty five bytes, so this one goes on and on and on: lots of ants, lots of food, a river with two bridges, and a hill for every team in each corner of a very big garden. It ends right here!")
            page.click_selector(".dlg-foot .btn:last-child")
            wait_for(lambda: not page.ev("!!document.querySelector('.dlg')"))
            side = json.loads(page.ev("""JSON.stringify((function () {
                var s = document.querySelector('.side'), r = s.getBoundingClientRect(), inside = [];
                [].forEach.call(s.querySelectorAll('button'), function (b) { if (b.offsetParent === null) return; var q = b.getBoundingClientRect(); if (q.bottom > r.bottom || q.top < r.top) inside.push(b.textContent); });
                return {tall: s.scrollHeight > s.clientHeight, wide: s.scrollWidth > s.clientWidth, outside: inside, len: document.getElementById('s-desc').textContent.length};
            })())"""))
            check(side["len"] > 200 and not side["tall"] and not side["wide"] and side["outside"] == [], "%d x %d: the side panel with a long description has no scrollbar and every button inside (%s)" % (w, h, side))
            if (w, h) == (1280, 720):
                page.shot(args.shots, "mapmaker-panel-1280x720-summary")
    elif part == "layout":
        for w, h in ((667, 375), (740, 360), (812, 375), (844, 390), (390, 844), (375, 667), (360, 640), (340, 700), (320, 568)):
            print("[web map maker] layout, %d x %d" % (w, h))
            tab.emulate(w, h, 2, mobile=True)
            page = Page(tab, args.web)
            check(page.open() and page.ready(), "the page starts")
            for tool in ("ground", "objects", "bigger", "hills", "ants", "food", "erase", "move"):
                page.tool(tool)
                time.sleep(0.2)
                found = json.loads(page.ev("""JSON.stringify((function () {
                    var bad = [], small = [];
                    [].forEach.call(document.querySelectorAll('button, input'), function (n) {
                        if (n.offsetParent === null || n.type === 'file' || n.disabled) return;
                        var b = n.getBoundingClientRect(), card = n.closest('.panel, header'), c = card ? card.getBoundingClientRect() : {left: 0, right: innerWidth, top: 0, bottom: 1e9}, name = (n.id || n.textContent || n.className).slice(0, 14);
                        if (b.left < c.left - 1 || b.right > c.right + 1 || b.top < c.top - 1 || b.bottom > c.bottom + 1 || b.right > innerWidth + 1 || b.left < -1) bad.push(name + ' ' + Math.round(b.right - c.right) + ',' + Math.round(b.bottom - c.bottom));
                        if (b.width < 43.5 || b.height < 43.5) small.push(name + ' ' + Math.round(b.width) + 'x' + Math.round(b.height));
                    });
                    var cv = document.getElementById('map').getBoundingClientRect();
                    return {bad: bad, small: small, canvas: Math.round(cv.height), sideways: document.documentElement.scrollWidth > innerWidth};
                })())"""))
                check(found["bad"] == [] and not found["sideways"], "%d x %d, %s tool: every control lies inside its card and the screen (%s)" % (w, h, tool, found["bad"]))
                check(found["small"] == [], "%d x %d, %s tool: every control is at least 44 pixels each way (%s)" % (w, h, tool, found["small"]))
                check(found["canvas"] >= (190 if h > w else 150), "%d x %d: the map is %d pixels high" % (w, h, found["canvas"]))
            page.tool("objects")
            check(page.ev("!document.getElementById('tile-btn').hidden && document.getElementById('palette').offsetParent === null"), "a phone has the one palette button and not the row of tiles")
            page.ev("document.getElementById('tile-btn').click()")
            check(wait_for(lambda: page.ev("document.querySelectorAll('.pick').length") == 11), "the button opens a sheet of the 11 objects")
            names = page.ev("[].map.call(document.querySelectorAll('.pick'), function (b) { return b.textContent; }).join('|')")
            check(names.startswith("Rock 1|Rock 2") and "Fire ant power-up" in names, "with their names: %s" % names)
            inside = page.ev("[].every.call(document.querySelectorAll('.dlg-foot button'), function (b) { var r = b.getBoundingClientRect(); return r.left >= -1 && r.right <= innerWidth + 1 && r.bottom <= innerHeight + 1 && r.top >= -1; })")
            check(inside, "the sheet's Close button is on the screen (the tiles scroll inside it)")
            page.ev("document.querySelector('.pick[data-tile=\"304\"]').click()")
            check(not page.ev("!!document.querySelector('.dlg')") and page.ev("document.getElementById('tile-name').textContent") == "Rock 3", "a tile of the sheet is chosen and the sheet closes")
            if (w, h) in ((667, 375), (375, 667)):
                page.shot(args.shots, "mapmaker-layout-%dx%d" % (w, h))
            # the sheets of Hills, Ants and Food: the teams, the stepper, the number that the button says
            for tool, picks, stepper in (("bigger", 29, False), ("hills", 4, False), ("ants", 4, True), ("food", 0, True)):
                page.tool(tool)
                time.sleep(0.1)
                page.ev("document.getElementById('tile-btn').click()")
                check(wait_for(lambda: page.ev("!!document.querySelector('.dlg h2')")), "%d x %d: the button of %s opens its sheet" % (w, h, tool))
                found = json.loads(page.ev("""JSON.stringify((function () {
                    var bad = [], small = [], dlg = document.querySelector('.dlg').getBoundingClientRect();
                    [].forEach.call(document.querySelectorAll('.dlg button'), function (n) {
                        if (n.disabled) return;
                        var b = n.getBoundingClientRect(), name = (n.id || n.textContent || n.className).slice(0, 14);
                        if (b.width < 43.5 || b.height < 43.5) small.push(name + ' ' + Math.round(b.width) + 'x' + Math.round(b.height));
                        if (n.closest('.dlg-in') === null && (b.bottom > innerHeight + 1 || b.right > innerWidth + 1)) bad.push(name);
                    });
                    return {picks: document.querySelectorAll('.pick').length, stepper: document.querySelectorAll('.stepper.sheet .stp').length, bad: bad, small: small, inside: dlg.bottom <= innerHeight + 1 && dlg.right <= innerWidth + 1 && dlg.left >= -1};
                })())"""))
                check(found["picks"] == picks and found["stepper"] == (2 if stepper else 0), "%d x %d: the sheet of %s has %d teams and %s stepper (%s)" % (w, h, tool, picks, "a" if stepper else "no", found))
                check(found["small"] == [] and found["bad"] == [] and found["inside"], "%d x %d: the sheet of %s is inside the screen and its buttons are 44 pixels (%s %s)" % (w, h, tool, found["small"], found["bad"]))
                if stepper:
                    page.ev("document.querySelector('.stepper.sheet .stp:last-child, .stepper.sheet .stbtns .stp:last-child').click()")
                    check(page.ev("document.getElementById('tile-name').textContent") in ("Green \u00b7 5 ants", "5 stages"), "%d x %d: the stepper of the sheet changes the number that the button says: %s" % (w, h, page.ev("document.getElementById('tile-name').textContent")))
                page.key("Escape")
                check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")), "%d x %d: the sheet of %s closes" % (w, h, tool))
            # the properties: Menu, Properties..., a sheet that is inside the screen with Cancel and Apply in view, 44 pixel controls, and what does not fit scrolls inside it
            page.press_act("menu")
            check(wait_for(lambda: page.ev("!!document.querySelector('.menulist')")), "%d x %d: the Menu opens" % (w, h))
            page.ev("[].filter.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent === 'Properties...'; })[0].click()")
            check(wait_for(lambda: page.ev("!!document.querySelector('.dlg.props')")), "%d x %d: Properties... opens the sheet" % (w, h))
            found = json.loads(page.ev("""JSON.stringify((function () {
                var bad = [], small = [], dlg = document.querySelector('.dlg').getBoundingClientRect(), inner = document.querySelector('.dlg-in'), box = inner.getBoundingClientRect(), foot = [];
                [].forEach.call(document.querySelectorAll('.dlg button, .dlg input, .dlg textarea'), function (n) {
                    if (n.disabled || n.offsetParent === null) return;
                    var b = n.getBoundingClientRect(), name = (n.id || n.textContent || n.className).slice(0, 14);
                    if (b.width < 43.5 || b.height < 43.5) small.push(name + ' ' + Math.round(b.width) + 'x' + Math.round(b.height));
                    if (b.left < box.left - 1 || b.right > box.right + 1 || b.right > innerWidth + 1 || b.left < -1) bad.push(name + ' sideways');
                    if (n.closest('.dlg-foot') && (b.bottom > innerHeight + 1 || b.top < -1)) foot.push(name);
                });
                var before = inner.scrollTop; inner.scrollTop = inner.scrollHeight;
                var last = document.querySelectorAll('.ant'), end = last[last.length - 1].getBoundingClientRect(), field = document.querySelector('.ants').getBoundingClientRect();
                var reach = end.bottom <= box.bottom + 1 && end.right <= field.right + 1;
                inner.scrollTop = before;
                return {bad: bad, small: small, foot: foot, inside: dlg.bottom <= innerHeight + 1 && dlg.top >= -1 && dlg.right <= innerWidth + 1 && dlg.left >= -1, scrolls: inner.scrollHeight > inner.clientHeight + 1, reach: reach, sideways: document.documentElement.scrollWidth > innerWidth};
            })())"""))
            check(found["inside"] and found["foot"] == [] and not found["sideways"], "%d x %d: the properties sheet is inside the screen with Cancel and Apply in view (%s)" % (w, h, found))
            check(found["small"] == [] and found["bad"] == [], "%d x %d: every control of the sheet is 44 pixels and inside it (%s %s)" % (w, h, found["small"], found["bad"]))
            check(found["reach"], "%d x %d: the last row, the ant types, can be scrolled into view (the sheet %s)" % (w, h, "scrolls inside itself" if found["scrolls"] else "needs no scrolling"))
            if h <= 400:
                check(found["scrolls"], "%d x %d: a short screen scrolls inside the sheet" % (w, h))
            if (w, h) in ((844, 390), (390, 844)):
                page.shot(args.shots, "mapmaker-props-layout-%dx%d" % (w, h))
            page.key("Escape")
            check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")), "%d x %d: Escape closes it" % (w, h))
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
            if w < 601:
                check(page.ev("document.querySelector('.toolnote').offsetParent !== null"), "the note about the tool that is chosen shows")
            check(page.ev("(function () { var b = document.getElementById('tile-btn'); return b.offsetParent !== null && !b.hidden && b.getBoundingClientRect().height >= 43.5; })()"), "the palette is one button of at least 44 pixels")
            bad = page.ev("JSON.stringify([].filter.call(document.querySelectorAll('button, input, a'), function (n) { if (n.offsetParent === null) return false; var b = n.getBoundingClientRect(); return b.right > window.innerWidth + 1 || (window.innerWidth > 600 && b.bottom > window.innerHeight + 1) || b.left < -1; }).map(function (n) { return n.textContent || n.id; }))")
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
            check(any(page.pixel(tx + d, ty + e) == water for d in (-8, -3, 0, 3, 8) for e in (-8, -3, 0, 3, 8)), "a tap paints a cell with the tile that is chosen (the point can be on the edge of a cell: the cells beside it are looked at)")
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
               "return real.apply(this, arguments).then(function (m) { m._lvl_interface_version = function () { return 3; }; return m; }); }; }, set: function (v) { real = v; } }); })();")
        check(page.open(old), "the page answers")
        text = page.ev("document.getElementById('boot').textContent")
        check(not page.ev("document.getElementById('boot').hidden") and "out of date" in text and "Reload the page" in text, "it says that the map code is out of date and offers a reload: %r" % text)
        check(page.ev("[].every.call(document.querySelectorAll('[data-act]'), function (b) { return b.disabled; })"), "and no button works")
        page.shot(args.shots, "mapmaker-older-module")


if __name__ == "__main__":
    sys.exit(main())
