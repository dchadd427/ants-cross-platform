"""The Pick up tool in the real browser (tests/scripts/web_mapmaker_browser.py, part `pickup`; docs/MAP_EDITOR.md, "Pick up"): kept in its own file so that neither passes the line limit of the test files.
The page helpers of the main file (wait_for, the Page class) are handed in by `run_pickup`. A map of 100 x 100 cells (the pictures' Mosh Pit: a red hill, a mushroom, grass, a rock, a pretzel pile, a
group of six Red ants) is made here with struct and opened from the disk; the bytes are what Download hands over, read here with struct again. With --shots DIR the pictures of the tool are saved
(`mapmaker-pickup-M1-picked-desktop` and so on: the names of the pictures M1 to M7 that the owner approved)."""
import json
import math
import os
import re
import struct
import tempfile
import time

wait_for = None
EMPTY2 = 0x7FFE
SQUARE4 = [(-2, -2), (-2, -1), (-2, 0), (-2, 1), (-1, -2), (-1, -1), (-1, 0), (-1, 1), (0, -2), (0, -1), (0, 0), (0, 1), (1, -2), (1, -1), (1, 0), (1, 1)]
GRASS = [(-1, -2), (-1, -1), (-1, 0), (-1, 1), (-1, 2), (0, -2), (0, -1), (0, 0), (0, 1), (0, 2)]
RED_HILL, MUSH, ROCK, GRASS_TILE = 247, 329, 302, 1324
GOLD = (242, 194, 48)


def mosh_pit():
    """the bytes of a 100 x 100 Extended map (version 9): everything of the pictures M1 to M7, written with struct"""
    rows = columns = 100
    names = 1334
    out = bytearray()
    out += struct.pack("<IIH", 9, 1, 10) + struct.pack("<I", 0) + struct.pack("<HH", 1, 0)          # (version, mode, minutes, the eggs' word, rules 1, an empty description)
    out += struct.pack("<H", names - 1)
    for i in range(names):
        name = b"g01a" if i == 485 else b"."
        out += name + bytes(11 - len(name))
    out += struct.pack("<II", rows, columns)
    l1 = bytearray()
    for i in range(rows * columns):
        l1 += struct.pack("<HHH", 485, 0, 0xCDCD)
    l2 = [(EMPTY2, 0, 0)] * (rows * columns)

    def put2(r, c, tile, flag, word):
        l2[r * columns + c] = (tile, flag, word)

    def rel(r, c, orow, ocol):
        return ((orow - r) & 255) | (((ocol - c) & 255) << 8)

    def object_at(tile, row, column, cells, solid=False):
        for dr, dc in cells:
            put2(row + dr, column + dc, tile, 1 if (dr, dc) == (0, 0) else 0, rel(row + dr, column + dc, row, column))
            if solid:
                at = 6 * ((row + dr) * columns + column + dc) + 2
                l1[at] |= 1
    for r in range(5, 9):
        for c in range(5, 9):
            put2(r, c, RED_HILL, 1 if (r, c) == (6, 6) else 0, rel(r, c, 6, 6))
    object_at(MUSH, 14, 18, SQUARE4, True)
    object_at(ROCK, 60, 47, [(0, 0)], True)
    object_at(GRASS_TILE, 62, 52, GRASS)
    layer2 = b"".join(struct.pack("<HHH", *cell) for cell in l2)
    ants = [(155, 62, 60), (155, 62, 62), (155, 63, 61), (155, 64, 60), (155, 64, 62), (155, 65, 61), (155, 70, 70), (153, 45, 30), (153, 38, 27), (152, 75, 50), (154, 15, 70)]
    block1 = struct.pack("<H", len(ants)) + b"".join(struct.pack("<HHH", *a) for a in ants)
    block2 = struct.pack("<H", 1) + struct.pack("<HHHHH", 66, 44, 20, 25, 3) + struct.pack("<HHHHHH", 20, 264, 10, 265, 0, EMPTY2)
    block3 = struct.pack("<HH", 0, EMPTY2)
    block4 = struct.pack("<H", 0)
    return bytes(out) + bytes(l1) + layer2 + block1 + block2 + block3 + block4 + struct.pack("<H", 6)


def layer2_tile(data, row, column):
    """the tile of layer 2 at a cell of a version 9 file, read with struct"""
    at = 18 + struct.unpack_from("<H", data, 16)[0]
    names = struct.unpack_from("<H", data, at)[0] + 1
    at += 2 + 11 * names
    rows, columns = struct.unpack_from("<II", data, at)
    return struct.unpack_from("<H", data, at + 8 + 6 * rows * columns + 6 * (row * columns + column))[0]


def records(data):
    """[(tile, row, column)] of block 1"""
    at = 18 + struct.unpack_from("<H", data, 16)[0]
    names = struct.unpack_from("<H", data, at)[0] + 1
    at += 2 + 11 * names
    rows, columns = struct.unpack_from("<II", data, at)
    at += 8 + 12 * rows * columns
    n = struct.unpack_from("<H", data, at)[0]
    return [struct.unpack_from("<HHH", data, at + 2 + 6 * i) for i in range(n)]


class Pick:
    """the helpers of one run: where a cell is on the screen, taps and drags (a mouse on a desktop, a finger on a phone), the bar's words"""

    def __init__(self, page, tab, touch):
        self.page, self.tab, self.touch = page, tab, touch

    def view(self):
        cy, cx, scale = [float(v) for v in self.page.ev("document.getElementById('map').getAttribute('data-view')").split(",")]
        return cy, cx, scale

    def point(self, row, column):
        cy, cx, scale = self.view()
        b = self.page.box("#map")
        return b["x"] + b["w"] / 2 + (column + 0.5 - cx) * scale, b["y"] + b["h"] / 2 + (row + 0.5 - cy) * scale

    def tap(self, row, column):
        x, y = self.point(row, column)
        (self.tab.tap if self.touch else self.tab.click)(x, y)
        time.sleep(0.35)

    def drag_points(self, x0, y0, x1, y1, steps=6):
        if not self.touch:
            self.page.drag(x0, y0, x1, y1, steps)
        else:
            def t(kind, x, y):
                self.tab.call("Input.dispatchTouchEvent", {"type": kind, "touchPoints": [] if kind == "touchEnd" else [{"x": x, "y": y, "id": 1, "radiusX": 8, "radiusY": 8, "force": .5}]})
            t("touchStart", x0, y0)
            time.sleep(0.05)
            for i in range(1, steps + 1):
                t("touchMove", x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)
                time.sleep(0.03)
            t("touchEnd", x1, y1)
        time.sleep(0.35)

    def held(self, start, mid, release, pen=False, during=None):
        """press at the page point `start`, move to `mid`, (call `during` with the button still down), move to `release` and let go there; a mouse, a pen or a finger"""
        def run(kind, x, y, **extra):
            if self.touch:
                self.tab.call("Input.dispatchTouchEvent", {"type": {"down": "touchStart", "move": "touchMove", "up": "touchEnd"}[kind], "touchPoints": [] if kind == "up" else [{"x": x, "y": y, "id": 1, "radiusX": 8, "radiusY": 8, "force": .5}]})
            else:
                more = {"pointerType": "pen"} if pen else {}
                if kind == "down":
                    self.tab.mouse("mouseMoved", x, y, button="none", **more)
                    self.tab.mouse("mousePressed", x, y, **more)
                elif kind == "move":
                    self.tab.mouse("mouseMoved", x, y, buttons=1, **more)
                else:
                    self.tab.mouse("mouseReleased", x, y, **more)
            time.sleep(0.03)
        run("down", *start)
        for i in range(1, 7):
            run("move", start[0] + (mid[0] - start[0]) * i / 6, start[1] + (mid[1] - start[1]) * i / 6)
        time.sleep(0.2)
        if during:
            during()
        for i in range(1, 4):
            run("move", mid[0] + (release[0] - mid[0]) * i / 3, mid[1] + (release[1] - mid[1]) * i / 3)
        run("up", *release)
        time.sleep(0.4)

    def drag_cells(self, r0, c0, r1, c1):
        x0, y0 = self.point(r0, c0)
        x1, y1 = self.point(r1, c1)
        self.drag_points(x0, y0, x1, y1)

    def drag_cells_settled(self, r0, c0, r1, c1, steps=6):
        """a drag whose end is aimed after the drag has begun: a press on a thing that is not picked picks it, and with the first move the bar comes and the view settles (a map that is whole on the screen moves by a
        part of a cell), so the centre of the last cell is looked for where it is then, as a hand that sees the map does, not where it was before the press (the aim of a knife edge: the shift decides the row)"""
        x0, y0 = self.point(r0, c0)

        def send(kind, x, y):
            if self.touch:
                self.tab.call("Input.dispatchTouchEvent", {"type": kind, "touchPoints": [] if kind == "touchEnd" else [{"x": x, "y": y, "id": 1, "radiusX": 8, "radiusY": 8, "force": .5}]})
            elif kind == "touchStart":
                self.tab.mouse("mouseMoved", x, y, button="none")
                self.tab.mouse("mousePressed", x, y)
            elif kind == "touchMove":
                self.tab.mouse("mouseMoved", x, y, buttons=1)
            else:
                self.tab.mouse("mouseReleased", x, y)
        send("touchStart", x0, y0)
        time.sleep(0.05)
        x1, y1 = self.point(r1, c1)
        x, y = x0 + (x1 - x0) / steps, y0 + (y1 - y0) / steps
        send("touchMove", x, y)                           # (the first step of the hand)
        time.sleep(0.5)
        x1, y1 = self.point(r1, c1)                       # (the map as it is now)
        for i in range(1, steps + 1):
            send("touchMove", x + (x1 - x) * i / steps, y + (y1 - y) * i / steps)
            time.sleep(0.03)
        send("touchEnd", x1, y1)
        time.sleep(0.35)

    def bar(self):
        return self.page.ev("(function () { var b = document.querySelector('.pk-bar'); return b && !b.hidden ? b.textContent : ''; })()")

    def line(self):
        return self.page.ev("(function () { var n = document.querySelector('.pk-bar .pk-line'); return n ? n.textContent : ''; })()")

    def warn(self):
        return self.page.ev("(function () { var n = document.querySelector('.pk-bar .pk-warn'); return n ? n.textContent : ''; })()")

    def labels(self):
        return self.page.ev("[].filter.call(document.querySelectorAll('.pk-bar .pk-b'), function (b) { return b.getAttribute('data-k') !== 'fold'; }).map(function (b) { return b.textContent; })")          # (the Hide button is for short screens)

    def press(self, key):
        found = self.page.ev("(function () { var b = document.querySelector('.pk-bar [data-k=\"%s\"]'); if (!b || b.disabled) return false; b.click(); return true; })()" % key)
        time.sleep(0.3)
        return found

    def disabled(self, key):
        return self.page.ev("(function () { var b = document.querySelector('.pk-bar [data-k=\"%s\"]'); return !b || b.disabled; })()" % key)

    def overlay_has(self, x, y, color, reach=4):
        """the overlay canvas has a pixel of about this color within `reach` pixels of the page point (x, y)"""
        return self.page.ev("""(function (x, y, want, reach) {
            var c = document.querySelector('.pk-over'), b = c.getBoundingClientRect(), k = c.width / b.width, ctx = c.getContext('2d');
            for (var dx = -reach; dx <= reach; dx++) for (var dy = -reach; dy <= reach; dy++) {
                var d = ctx.getImageData(Math.round((x - b.left + dx) * k), Math.round((y - b.top + dy) * k), 1, 1).data;
                if (d[3] > 160 && Math.abs(d[0] - want[0]) < 40 && Math.abs(d[1] - want[1]) < 40 && Math.abs(d[2] - want[2]) < 40) return true;
            }
            return false;
        })(%s, %s, %s, %d)""" % (x, y, json.dumps(list(color)), reach))

    def focus_on(self, row, column, scale=30, ax=0.5, ay=0.5):
        """zoom to about `scale` pixels a cell (the zoom buttons: 1.5 a step) round a cell and bring it to the fraction (ax, ay) of the canvas: the small map jumps there, a drag on Pan finishes"""
        page = self.page
        page.tool("pan")                    # (a drag from a thing would pick it up: the way round the map is Pan's)
        _, _, sc = self.view()
        steps = int(round(math.log(scale / sc) / math.log(1.5)))
        for _ in range(abs(steps)):                         # (the plus and minus buttons zoom by exactly 1.5 about the middle)
            page.ev("document.querySelector('[data-act=%s]').click()" % ("zoom-in" if steps > 0 else "zoom-out"))
            time.sleep(0.12)
        mini = page.box("#mini")
        mx, my = mini["x"] + mini["w"] * (column + 0.5) / 100.0, mini["y"] + mini["h"] * (row + 0.5) / 100.0
        (self.tab.tap if self.touch else self.tab.click)(mx, my)
        time.sleep(0.4)
        b = page.box("#map")
        for _ in range(10):
            x, y = self.point(row, column)
            tx, ty = b["x"] + b["w"] * ax, b["y"] + b["h"] * ay
            dx, dy = max(-b["w"] * 0.4, min(b["w"] * 0.4, tx - x)), max(-b["h"] * 0.4, min(b["h"] * 0.4, ty - y))
            if abs(tx - x) < 2 and abs(ty - y) < 2:
                break
            sx, sy = b["x"] + b["w"] * 0.5 - dx / 2, b["y"] + b["h"] * 0.5 - dy / 2
            self.drag_points(sx, sy, sx + dx, sy + dy)
        page.tool("pickup")


def setup(page, args, check, edges_js, edges_bytes, name):
    check(page.open(edges_js) and page.ready(), "%s: the page starts" % name)
    page.ev(edges_js)
    time.sleep(1.2)
    path = os.path.join(tempfile.mkdtemp(prefix="ants_pickup."), "Mosh.lvl")
    with open(path, "wb") as f:
        f.write(mosh_pit())
    page.choose_file(path)
    check(wait_for(lambda: page.state()["name"] == "Mosh", 15), "%s: the Mosh Pit (100 x 100, Extended) is opened from the disk" % name)
    return path


def run_pickup(page, tab, args, check, env, edges_bytes, edges_js):
    global wait_for
    wait_for = env["wait_for"]
    shots = args.shots
    print("[web map maker] the Pick up tool, 1280 x 800")
    setup(page, args, check, edges_js, edges_bytes, "desktop")
    h = Pick(page, tab, False)
    names = page.ev("[].map.call(document.querySelectorAll('[data-tool]'), function (b) { return b.getAttribute('data-tool') + ':' + b.textContent.trim(); }).join(' ')")
    check(names.endswith("erase:Erase pickup:Pick up pan:Pan") and "move" not in names, "the tools end with Pick up and Pan (the old Move is Pan): %s" % names)
    check(not page.ev("!!document.querySelector('.pk-bar')") and not page.ev("!!document.querySelector('link[href=\"front/mapmaker_pickup.css\"]')"), "the Pick up part is not loaded before the tool is chosen")
    page.ev("document.getElementById('map').focus()")
    # the style fails to load once (a flaky network): the person is told, and the next try asks for it again (S1 of the review: the script alone is not enough)
    tab.call("Network.enable")
    tab.call("Network.setBlockedURLs", {"urls": ["*mapmaker_pickup.css*"]})
    page.key("p")
    check(wait_for(lambda: page.ev("document.body.textContent.indexOf('This part did not load') >= 0"), 10) and page.ev("document.querySelector('[data-tool=pan]').getAttribute('aria-pressed')") == "true", "the style did not load: the page says so in plain words, and the tool is Pan")
    page.key("Escape")
    tab.call("Network.setBlockedURLs", {"urls": []})
    page.ev("document.getElementById('map').focus()")
    page.key("p")
    check(wait_for(lambda: page.ev("!!document.querySelector('.pk-bar')"), 10), "the letter P chooses Pick up and loads its part (script and style)")
    check(page.ev("document.querySelectorAll('link[href=\"front/mapmaker_pickup.css\"]').length") == 1 and page.ev("document.querySelectorAll('script[src=\"front/mapmaker_pickup.js\"]').length") == 1, "each file has one tag in the head (a failed one was taken away before the next was added)")
    h0 = Pick(page, tab, False)
    h0.focus_on(9, 11, 20, 0.4, 0.4)
    h0.tap(6, 6)
    check(page.ev("getComputedStyle(document.querySelector('.pk-bar')).position") == "relative" and page.ev("getComputedStyle(document.querySelector('.pk-bar')).backgroundColor") == "rgb(8, 19, 15)", "and the style is there: the bar is styled, not a bare list of buttons")
    page.key("Escape")
    check(page.ev("!!document.querySelector('link[href=\"front/mapmaker_pickup.css\"]')") and page.ev("document.querySelector('[data-tool=pickup]').getAttribute('aria-pressed')") == "true", "its style comes with it, and the button is pressed")
    check(h.bar() == "", "with nothing picked there is no bar")
    check(edges_bytes(page)["bytes"] == mosh_pit(), "the map that Download hands over is the one that was made (the file's own bytes)")
    # M1: the hill
    h.focus_on(9, 11, 20, 0.4, 0.4)
    before = page.state()["center"]
    b = page.box("#map")
    page.tool("pan")
    view = h.view()
    page.drag(b["x"] + 330, b["y"] + 120, b["x"] + 300, b["y"] + 100)
    check(h.view() != view, "a drag looks around (Pan)")
    page.tool("pickup")
    h.focus_on(9, 11, 20, 0.4, 0.4)
    h.tap(6, 6)
    check("Red hill" in h.line() and "4 x 4 cells, with its door" in h.line(), "a tap on the hill picks it: %s" % h.line())
    check(h.labels() == ["Move", "Copy here", "Delete", "Select an area...", "Cancel"] and h.disabled("copy"), "its five buttons, Copy here off for a hill (a team has one hill): %s" % h.labels())
    left, top = h.point(5, 5)
    cy, cx, sc = h.view()
    check(h.overlay_has(left - sc / 2, top, GOLD) and h.overlay_has(left + 2 * sc, top - sc / 2 - 1, GOLD), "the hill has a gold ring round its whole square")
    check(page.ev("document.querySelector('#map').getBoundingClientRect().bottom <= document.querySelector('.pk-bar').getBoundingClientRect().top + 1"), "on a desktop the bar is a row of its own under the map: the map ends where the bar starts (run_pickup_sheet has the rest)")
    page.shot(shots, "mapmaker-pickup-M1-picked-desktop")
    # M2: Move, tap a free place
    h.press("move")
    check(h.disabled("done") and "Tap the new place" in h.line(), "Move: Done is off where the hill stands: %s" % h.line())
    h.tap(12, 12)
    check(not h.disabled("done") and "It lands on row 11, column 11" in h.line(), "a tap on a free place: it lands on row 11, column 11: %s" % h.line())
    x, y = h.point(11, 11)
    check(h.overlay_has(x - h.view()[2] / 2, y, (139, 224, 176)), "the ghost has a green outline")
    page.shot(shots, "mapmaker-pickup-M2-move-desktop")
    # M3: onto the mushroom
    h.tap(13, 16)
    check(h.disabled("done") and "the mushroom is in the way" in h.warn() and "free 4 x 4 square" in h.warn(), "onto the mushroom: red, Done off, the sentence says why: %s" % h.warn())
    x, y = h.point(12, 15)
    check(h.overlay_has(x - h.view()[2] / 2, y, (240, 138, 132)), "the ghost has a red outline")
    page.shot(shots, "mapmaker-pickup-M3-blocked-desktop")
    h.tap(12, 12)
    # the ghost follows the mouse (a desktop)
    x, y = h.point(20, 20)
    tab.mouse("mouseMoved", x, y, button="none")
    time.sleep(0.3)
    check("It lands on row 19, column 19" in h.line() and not h.disabled("done") and h.overlay_has(x - 1.5 * h.view()[2], y, (139, 224, 176), 6), "the ghost follows the mouse: green under it, 'It lands on row 19, column 19': %s" % h.line())
    x, y = h.point(13, 16)
    tab.mouse("mouseMoved", x, y, button="none")
    time.sleep(0.3)
    check(h.disabled("done") and "the mushroom is in the way" in h.warn(), "over the mushroom the ghost under the mouse is red, with the reason: %s" % h.warn())
    page.shot(shots, "mapmaker-pickup-M3b-hover-desktop")
    page.ev("document.querySelector('.pk-bar [data-k=nudge]').focus()")
    x, y = h.point(21, 21)
    tab.mouse("mouseMoved", x, y, button="none")
    time.sleep(0.3)
    check(page.ev("document.activeElement.getAttribute('data-k')") == "nudge", "the bar is made again as the ghost follows the mouse, and the button that had the keyboard's focus keeps it: %s" % page.ev("document.activeElement.tagName + '#' + (document.activeElement.getAttribute('data-k') || document.activeElement.id)"))
    tab.mouse("mouseMoved", 5, 5, button="none")
    time.sleep(0.3)
    check("It lands on row 11, column 11" in h.line() and not h.disabled("done"), "the mouse leaving the map puts the ghost back where it was put: %s" % h.line())
    # nudge
    h.press("nudge")
    check(page.ev("document.querySelectorAll('.pk-arrow').length") == 4, "Nudge arrows: four arrows")
    h.press("right")
    check("Moved 7 right, 6 down" in h.line(), "one arrow moves one cell: %s" % h.line())
    h.press("left")
    page.shot(shots, "mapmaker-pickup-M4-nudge-desktop")
    h.press("done")
    data = edges_bytes(page)["bytes"]
    check(layer2_tile(data, 11, 11) == RED_HILL and layer2_tile(data, 6, 6) == EMPTY2 and "Moved it" in h.bar(), "Done moves the hill (the file says so) and says how far: %s" % h.bar()[:80])
    page.ev("document.getElementById('undo-btn').click()")
    check(edges_bytes(page)["bytes"] == mosh_pit() and h.bar() == "", "ONE Undo puts it back, byte for byte, and lets the pick go")
    page.ev("document.getElementById('redo-btn').click()")
    check(layer2_tile(edges_bytes(page)["bytes"], 11, 11) == RED_HILL, "Redo moves it again")
    page.ev("document.getElementById('undo-btn').click()")
    # the keyboard
    page.ev("document.getElementById('map').focus()")
    h.tap(6, 6)
    page.key("ArrowRight")
    page.key("ArrowDown")
    check("Moving the red hill" in h.line() and "row 6, column 6" in h.line(), "an arrow key nudges the hill (Moving the red hill, row 6, column 6): %s" % h.line())
    page.key("Enter")
    check(layer2_tile(edges_bytes(page)["bytes"], 7, 6) == RED_HILL, "Enter puts it down")
    page.key("Escape")
    check(h.bar() == "", "Escape lets go")
    h.tap(6, 6)
    page.ev("document.querySelector('.pk-bar [data-k=move]').focus()")
    page.key("Escape")
    check(h.bar() == "" and page.ev("document.activeElement.id") == "map", "Escape on a button of the bar lets go and the map has the focus (S3 of the review): %s" % page.ev("document.activeElement.tagName + '#' + document.activeElement.id"))
    page.ev("document.getElementById('undo-btn').click()")
    h.tap(6, 6)
    page.key("Delete")
    data = edges_bytes(page)["bytes"]
    check(layer2_tile(data, 6, 6) == EMPTY2 and h.bar() == "", "Delete takes the picked hill away")
    page.ev("document.getElementById('undo-btn').click()")
    check(edges_bytes(page)["bytes"] == mosh_pit(), "and Undo brings it back")
    # M5: an ant and its group
    h.focus_on(65, 61, 20, 0.5, 0.3)
    h.tap(63, 61)
    check(h.labels() == ["Move this ant", "Move its group (6)", "Copy here", "Delete", "Cancel"] and "one of 7" in h.line(), "an ant of the group: Move this ant, Move its group (6): %s" % h.line())
    page.shot(shots, "mapmaker-pickup-M5-ant-desktop")
    h.press("copy")
    check(h.labels() == ["Copy this ant", "Copy its group (6)", "Back"] and "whole group of 6" in h.bar(), "Copy here asks: this ant or its group: %s" % h.labels())
    page.shot(shots, "mapmaker-pickup-M5b-copy-asks-desktop")
    h.press("cancel")
    h.press("delete")
    check(h.labels() == ["Delete this ant", "Delete its group (6)", "Back"], "Delete asks the same way: %s" % h.labels())
    h.press("cancel")
    h.press("group")
    h.tap(69, 61)
    h.press("done")
    got = [r for r in records(edges_bytes(page)["bytes"]) if r[0] == 155]
    check(sum(1 for r in got if r[1] >= 67) == 7 and sum(1 for r in got if r[1] < 67) == 0, "the six Red ants moved together (down to row 67 and below), the lone one stayed")
    page.ev("document.getElementById('undo-btn').click()")
    # M6: an area
    h.focus_on(63, 54, 20, 0.5, 0.3)
    h.tap(66, 44)
    check("Food pile" in h.line() and "Pretzel" in h.line(), "the pile says what it is: %s" % h.line())
    h.press("area")
    check("Select an area" in h.line(), "Select an area...: %s" % h.line())
    cy, cx, sc = h.view()
    x0, y0 = h.point(58, 42)
    x1, y1 = h.point(68, 66)
    h.drag_points(x0 - sc / 2, y0 - sc / 2, x1 + sc / 2, y1 + sc / 2)
    check(h.labels() == ["Move them", "Copy here", "Delete", "Cancel"] and "Area: 9 items" in h.line() and "6 Red ants" in h.line(), "the box takes the things inside it: %s" % h.line())
    page.shot(shots, "mapmaker-pickup-M6-area-desktop")
    h.press("move")
    h.tap(73, 54)
    h.press("done")
    after = edges_bytes(page)["bytes"]
    check(layer2_tile(after, 60, 47) == EMPTY2 and any(layer2_tile(after, r, c) == ROCK for r in range(66, 90) for c in range(40, 60)), "the area moved in one step (the rock left its place)")
    page.ev("document.getElementById('undo-btn').click()")
    check(edges_bytes(page)["bytes"] == mosh_pit(), "ONE Undo puts the whole area back, byte for byte")
    h.press("cancel") if h.bar() else None
    # M7: what can be picked up
    page.ev("document.querySelector('.pk-help').click()")
    check(wait_for(lambda: page.ev("document.querySelectorAll('.pk-card').length") == 7, 5), "What can be picked up? opens the seven cards")
    page.shot(shots, "mapmaker-pickup-M7-help-desktop")
    page.key("Escape")
    # Pan does not pick things up
    page.tool("pan")
    before = page.state()["center"]
    x, y = h.point(63, 61)
    page.drag(x, y, x + 40, y + 30)
    check(page.state()["center"] != before and edges_bytes(page)["bytes"] == mosh_pit() and h.bar() == "", "Pan: a drag on an ant looks around and moves nothing")
    check(page.ev("document.querySelector('[data-tool=pan]').textContent.trim()") == "Pan", "the old Move is called Pan")


def run_pickup_phone(page, tab, args, check, env, edges_bytes, edges_js):
    global wait_for
    wait_for = env["wait_for"]
    shots = args.shots
    print("[web map maker] the Pick up tool, a phone held upright (390 x 844)")
    setup(page, args, check, edges_js, edges_bytes, "phone")
    h = Pick(page, tab, True)
    page.tool("pickup")
    check(wait_for(lambda: page.ev("!!document.querySelector('.pk-bar')"), 10), "Pick up is chosen (its part loads)")
    h.focus_on(9, 11, 18, 0.4, 0.35)
    h.tap(6, 6)
    check("Red hill" in h.line(), "a tap picks the hill: %s" % h.line())
    box = page.box(".pk-bar")
    check(abs(box["y"] + box["h"] - page.ev("window.innerHeight")) < 1.5 and box["w"] >= page.ev("window.innerWidth") - 1, "the bar is at the foot of the screen, as wide as the screen: %s" % box)
    small = page.ev("[].filter.call(document.querySelectorAll('.pk-bar button'), function (b) { var r = b.getBoundingClientRect(); return r.width > 0 && (r.height < 43.5 || r.width < 43.5); }).map(function (b) { return b.textContent; })")
    check(not small, "every button of the bar is at least 44 pixels each way (%s)" % small)
    check(page.ev("document.documentElement.scrollWidth <= window.innerWidth"), "the page does not scroll sideways")
    page.shot(shots, "mapmaker-pickup-M1-picked-phone")
    h.press("move")
    h.tap(12, 12)
    check("It lands on row 11, column 11" in h.line() and not h.disabled("done"), "a tap puts the ghost: %s" % h.line())
    page.shot(shots, "mapmaker-pickup-M2-move-phone")
    h.tap(13, 16)
    check(h.disabled("done") and "the mushroom is in the way" in h.warn(), "onto the mushroom: blocked, with the words: %s" % h.warn())
    page.shot(shots, "mapmaker-pickup-M3-blocked-phone")
    h.tap(12, 12)
    h.press("nudge")
    arrows = page.ev("[].map.call(document.querySelectorAll('.pk-arrow'), function (b) { var r = b.getBoundingClientRect(); return [r.width, r.height]; })")
    check(len(arrows) == 4 and all(w >= 47.5 and hh >= 47.5 for w, hh in arrows), "the four nudge arrows are 48 pixels: %s" % arrows)
    page.shot(shots, "mapmaker-pickup-M4-nudge-phone")
    h.press("right")
    h.press("done")
    check(layer2_tile(edges_bytes(page)["bytes"], 11, 12) == RED_HILL, "Done puts the hill down, nudged one cell")
    page.ev("document.getElementById('undo-btn').click()")
    check(edges_bytes(page)["bytes"] == mosh_pit(), "ONE Undo takes it back")
    # a drag of a finger on the hill moves it
    h.tap(6, 6)
    x0, y0 = h.point(6, 6)
    x1, y1 = h.point(14, 8)
    h.drag_points(x0, y0, x1, y1)
    check(layer2_tile(edges_bytes(page)["bytes"], 13, 7) == RED_HILL and layer2_tile(edges_bytes(page)["bytes"], 6, 6) == EMPTY2 and "Moved it" in h.bar(), "a finger dragging the picked hill puts it down where the finger is lifted (row 13, column 7), the file says so: %s" % h.bar()[:60])
    page.ev("document.getElementById('undo-btn').click()")
    check(edges_bytes(page)["bytes"] == mosh_pit(), "and ONE Undo takes it back")
    h.focus_on(65, 61, 18, 0.5, 0.3)
    h.tap(63, 61)
    check(h.labels() == ["Move this ant", "Move its group (6)", "Copy here", "Delete", "Cancel"], "an ant of a group: %s" % h.labels())
    page.shot(shots, "mapmaker-pickup-M5-ant-phone")
    h.press("cancel")
    h.focus_on(63, 55, 14, 0.5, 0.3)
    h.tap(60, 47)
    check("Rock" in h.line() or "rock" in h.line().lower(), "a tap on the rock picks it: %s" % h.line())
    h.press("area")
    cy, cx, sc = h.view()
    x0, y0 = h.point(58, 46)
    x1, y1 = h.point(67, 64)
    h.drag_points(x0 - sc / 2, y0 - sc / 2, x1 + sc / 2, y1 + sc / 2)
    check("Area: 8 items" in h.line() and "6 Red ants" in h.line(), "a box takes the 8 things inside it (the rock, the grass, the six ants): %s" % h.line())
    page.shot(shots, "mapmaker-pickup-M6-area-phone")
    h.press("cancel")
    page.ev("document.querySelector('.pk-help').click()")
    check(wait_for(lambda: page.ev("document.querySelectorAll('.pk-card').length") == 7, 5), "What can be picked up? opens the seven cards")
    page.shot(shots, "mapmaker-pickup-M7b-help-phone")


def run_pickup_short(page, tab, args, check, env, edges_bytes, edges_js, name, touch):
    """a short screen (a phone held sideways, a window 600 high): the bar is one row, the map and the ghost stay in view, the Hide button folds it (S2 of the review)"""
    global wait_for
    wait_for = env["wait_for"]
    shots = args.shots
    print("[web map maker] the Pick up tool, %s" % name)
    setup(page, args, check, edges_js, edges_bytes, name)
    h = Pick(page, tab, touch)
    page.tool("pickup")
    check(wait_for(lambda: page.ev("!!document.querySelector('.pk-bar')"), 10), "%s: Pick up is chosen" % name)
    h.focus_on(9, 11, 14, 0.4, 0.3)
    mapbox = page.box("#map")
    seen = min(mapbox["y"] + mapbox["h"], page.ev("window.innerHeight")) - mapbox["y"]          # (the part of the map that is on the screen, before the bar takes a row of it)
    h.tap(6, 6)
    check("Red hill" in h.line(), "%s: a tap picks the hill: %s" % (name, h.line()))

    def bar_share():
        b = page.box(".pk-bar")
        return b["h"] / seen, b

    share, b = bar_share()
    check(share < 0.45, "%s: the bar takes %d%% of the map (one row, not a block): %s" % (name, share * 100, b))
    small = page.ev("[].filter.call(document.querySelectorAll('.pk-bar button'), function (b) { var r = b.getBoundingClientRect(); return r.width > 0 && (r.height < 43.5 || r.width < 43.5); }).map(function (b) { return b.textContent; })")
    check(not small and h.labels()[:5] == ["Move", "Copy here", "Delete", "Select an area...", "Cancel"], "%s: five buttons, each at least 44 pixels each way (%s)" % (name, small))
    check(page.ev("document.documentElement.scrollWidth <= window.innerWidth"), "%s: no sideways scroll" % name)
    page.shot(shots, "mapmaker-pickup-S2-picked-%s" % name)
    h.press("move")
    h.tap(12, 12)
    share, b = bar_share()
    x, y = h.point(11, 11)
    check(h.line().startswith("Moving the red hill") or "It lands" in h.line(), "%s: the ghost line: %s" % (name, h.line()))
    check(y < b["y"] - 2 and h.overlay_has(x - h.view()[2] / 2, y, (139, 224, 176), 5) and share < 0.45, "%s: the ghost is in view above the bar (%d%% of the map is the bar) and green" % (name, share * 100))
    page.shot(shots, "mapmaker-pickup-S2-move-%s" % name)
    h.press("nudge")
    share, b = bar_share()
    arrows = page.ev("[].map.call(document.querySelectorAll('.pk-arrow'), function (b) { var r = b.getBoundingClientRect(); return [r.width, r.height, r.top]; })")
    check(len(arrows) == 4 and all(w >= 47.5 and hh >= 47.5 for w, hh, _ in arrows) and share < 0.5, "%s: the four arrows are 48 pixels, in a row, the bar takes %d%% of the map: %s" % (name, share * 100, arrows))
    page.shot(shots, "mapmaker-pickup-S2-nudge-%s" % name)
    h.press("fold")
    share, b = bar_share()
    check(share < 0.25 and page.ev("document.querySelector('.pk-bar [data-k=fold]').textContent") == "Controls" and page.ev("[].filter.call(document.querySelectorAll('.pk-bar button'), function (b) { return b.getBoundingClientRect().width > 0; }).length") == 1 and page.ev("document.querySelector('.pk-bar [data-k=fold]').getBoundingClientRect().height") >= 43.5, "%s: folded, the bar is one 44 pixel button (%d%% of the map)" % (name, share * 100))
    page.shot(shots, "mapmaker-pickup-S2-folded-%s" % name)
    h.press("fold")
    check("Done" in h.labels() or "Nudge arrows" in h.labels(), "%s: and it opens again: %s" % (name, h.labels()))
    h.press("right")
    h.press("done")
    check(layer2_tile(edges_bytes(page)["bytes"], 12, 13) == RED_HILL, "%s: Done puts the hill down, nudged one cell" % name)
    page.ev("document.getElementById('undo-btn').click()")
    check(edges_bytes(page)["bytes"] == mosh_pit(), "%s: ONE Undo takes it back" % name)


def run_pickup_sheet(page, tab, args, check, env, edges_bytes, edges_js, name, touch):
    """the bar is a row of its own under the map (a phone's is fixed to the screen, with the page's room below it): the map ends above it, so the last row can be brought above the bar and a thing picked at the foot of the
    map is brought into view (a hill at the foot of the map used to be under the bar, and no pan could bring it out); the map keeps its top when the bar comes and goes"""
    global wait_for
    wait_for = env["wait_for"]
    shots = args.shots
    print("[web map maker] the Pick up tool, the bar does not cover the map (%s)" % name)
    setup(page, args, check, edges_js, edges_bytes, name)
    page.ev("(function () { var b = [].filter.call(document.querySelectorAll('#dialogs button'), function (n) { return n.textContent === 'Throw it away'; })[0]; if (b) b.click(); })()")          # (the work that an earlier part left in the browser: the page asks, and a tap elsewhere would pick it up and show the whole map)
    time.sleep(0.4)
    h = Pick(page, tab, touch)
    page.tool("pickup")
    check(wait_for(lambda: page.ev("!!document.querySelector('.pk-bar')"), 10), "%s: Pick up is chosen" % name)

    def top():
        cy, _, sc = h.view()
        return cy - page.box("#map")["h"] / 2 / sc          # (the row at the top edge of the map)

    def above_bar(y):
        return y <= page.box(".pk-bar")["y"] + 1

    h.focus_on(6, 6, 20, 0.5, 0.25)
    was, high = top(), page.box("#map")["h"]
    h.tap(6, 6)
    check("Red hill" in h.line(), "%s: a tap picks the hill: %s" % (name, h.line()))
    box, bar = page.box("#map"), page.box(".pk-bar")
    check(above_bar(box["y"] + box["h"]), "%s: the map ends above the bar: its foot is at %d, the bar starts at %d" % (name, box["y"] + box["h"], bar["y"]))
    fixed = page.ev("getComputedStyle(document.querySelector('.pk-bar')).position") == "fixed"
    check(fixed or box["h"] < high - 20, "%s: the map has a smaller height while the bar is there (%d, it was %d)" % (name, box["h"], high))
    check(abs(top() - was) < 0.02, "%s: the map keeps its top row when the bar comes (row %.2f, it was %.2f)" % (name, top(), was))
    if fixed:
        check(abs(float(page.ev("parseFloat(getComputedStyle(document.body).paddingBottom)")) - bar["h"]) < 1.5, "%s: the page has as much room below it as the bar is high (%s)" % (name, page.ev("getComputedStyle(document.body).paddingBottom")))
        page.ev("window.scrollTo(0, document.documentElement.scrollHeight)")
        time.sleep(0.2)
        check(page.ev("document.getElementById('edfoot').getBoundingClientRect().bottom") <= page.box(".pk-bar")["y"] + 1, "%s: the page scrolls until its last line is above the bar" % name)
        page.ev("window.scrollTo(0, 0)")
    h.press("cancel")
    check(abs(page.box("#map")["h"] - high) < 1.5 and abs(top() - was) < 0.02, "%s: the bar goes: the map has its height again (%d) and keeps its top row" % (name, page.box("#map")["h"]))
    # a thing at the foot of the map: picking it brings it above the bar
    h.focus_on(75, 50, 20, 0.5, 0.8)
    x, y = h.point(75, 50)
    box = page.box("#map")
    check(y > box["y"] + box["h"] * 0.7, "%s: before the pick the ant is at the foot of the map, where the bar will be (%d of %d)" % (name, y - box["y"], box["h"]))
    h.tap(75, 50)
    check("ant" in h.line().lower(), "%s: a tap picks the ant: %s" % (name, h.line()))
    x, y = h.point(75, 50)
    box, sc = page.box("#map"), h.view()[2]
    check(box["y"] <= y - sc / 2 and above_bar(y + sc / 2) and box["x"] <= x - sc / 2 and x + sc / 2 <= box["x"] + box["w"], "%s: the ant is in view above the bar (its cell is at %d..%d, the bar starts at %d)" % (name, y - sc / 2, y + sc / 2, page.box(".pk-bar")["y"]))
    check(h.overlay_has(x - sc / 2, y, GOLD, 3), "%s: and its gold ring is drawn there" % name)
    page.shot(shots, "mapmaker-pickup-sheet-%s" % name.replace(" ", "-"))
    # the last row can be brought above the bar by panning (a drag on empty ground; the pick stays)
    for _ in range(14):
        before = h.view()
        box = page.box("#map")
        h.drag_points(box["x"] + 24, box["y"] + box["h"] * 0.45, box["x"] + 24, box["y"] + 8)
        if h.view() == before:
            break
    cy, _, sc = h.view()
    box, bar = page.box("#map"), page.box(".pk-bar")
    edge = box["y"] + box["h"] / 2 + (100 - cy) * sc
    check(h.bar() != "" and above_bar(edge) and edge > box["y"] + box["h"] - 3, "%s: panned as far as it goes, the last row ends at %d: above the bar (it starts at %d) and at the foot of the map (%d)" % (name, edge, bar["y"], box["y"] + box["h"]))
    page.shot(shots, "mapmaker-pickup-sheet-end-%s" % name.replace(" ", "-"))
    h.press("cancel")


def run_pickup_strobe(page, tab, args, check, env, edges_bytes, edges_js, name, touch):
    """the map's height does not change at every pointer event: the bar is higher when a place says "cannot go here" than when it says where the thing lands, and the map is as high as the bar leaves it. A hill at
    the foot of the map is moved with the mouse 2 pixels at a time over the last rows (the places there are red: a hill is four rows high) and over the bar: the map must not grow and shrink with each step"""
    global wait_for
    wait_for = env["wait_for"]
    print("[web map maker] the Pick up tool, the map does not strobe under a pointer that moves over the last rows (%s)" % name)
    setup(page, args, check, edges_js, edges_bytes, name)
    page.ev("(function () { var b = [].filter.call(document.querySelectorAll('#dialogs button'), function (n) { return n.textContent === 'Throw it away'; })[0]; if (b) b.click(); })()")
    time.sleep(0.4)
    h = Pick(page, tab, touch)
    page.tool("pickup")
    check(wait_for(lambda: page.ev("!!document.querySelector('.pk-bar')"), 10), "%s: Pick up is chosen" % name)
    h.focus_on(6, 6, 20, 0.5, 0.25)
    h.tap(6, 6)
    check("Red hill" in h.line(), "%s: a tap picks the hill: %s" % (name, h.line()))
    check(h.press("move") and "Moving" in h.line(), "%s: Move: %s" % (name, h.line()))
    for _ in range(90):                                                # (the arrow keys take the hill down to rows 95 to 98, and Done puts it there: it is the picked thing, brought into view above the bar)
        page.key("ArrowDown")
    check(h.press("done") and "Red hill" in h.line(), "%s: Done puts the hill at the foot of the map: %s" % (name, h.line()))
    for _ in range(40):                                                # (the last row is brought to the foot of the map by panning, the pick stays)
        before = h.view()
        box = page.box("#map")
        h.drag_points(box["x"] + 24, box["y"] + box["h"] * 0.45, box["x"] + 24, box["y"] + 8)
        if h.view() == before:
            break
    check(h.press("move") and "row 95" in h.line(), "%s: Move again: %s" % (name, h.line()))
    x, y98 = h.point(98, 6)
    box = page.box("#map")
    check(y98 < box["y"] + box["h"] - 2, "%s: row 98, where the hill is red, is on the map (at %d, the map ends at %d)" % (name, y98, box["y"] + box["h"]))
    top, heights, warned = int(h.point(94, 6)[1]), [], []
    for _ in range(2):
        for y in list(range(top, int(box["y"] + box["h"]) + 30, 2)) + list(range(int(box["y"] + box["h"]) + 30, top, -2)):
            tab.mouse("mouseMoved", x, y, button="none")
            time.sleep(0.04)
            heights.append(round(page.box("#map")["h"]))
            warned.append(h.warn() != "")
    changes = sum(1 for a, b in zip(heights, heights[1:]) if a != b)
    flips = sum(1 for a, b, c in zip(heights, heights[1:], heights[2:]) if a == c and a != b)
    check(any(warned) and len(set(heights)) >= 2, "%s: the pointer went over places that are red and over the bar: the map was %s high" % (name, sorted(set(heights))))
    check(flips == 0 and changes <= 8, "%s: the map's height changed %d times in %d steps of 2 pixels, %d of them back at once (a few changes where the pointer enters the bar or leaves the last row; not at every step): %s" % (name, changes, len(heights), flips, heights))
    tab.mouse("mouseMoved", 5, 5, button="none")
    h.press("cancel")
    h.press("cancel")


def run_pickup_resize(page, tab, args, check, env, edges_bytes, edges_js, name, touch):
    """a window that changes its width while a thing is on its way: the page follows the bar, and the browser reports no "ResizeObserver loop completed with undelivered notifications" (a window 'error' event, one at each
    resize when the bar's own height was changed inside the observer's call)"""
    global wait_for
    wait_for = env["wait_for"]
    print("[web map maker] the Pick up tool, a window resized while a thing is on its way gives no ResizeObserver error (%s)" % name)
    setup(page, args, check, edges_js, edges_bytes, name)
    page.ev("(function () { var b = [].filter.call(document.querySelectorAll('#dialogs button'), function (n) { return n.textContent === 'Throw it away'; })[0]; if (b) b.click(); })()")
    time.sleep(0.4)
    h = Pick(page, tab, touch)
    page.tool("pickup")
    check(wait_for(lambda: page.ev("!!document.querySelector('.pk-bar')"), 10), "%s: Pick up is chosen" % name)
    h.focus_on(6, 6, 20, 0.5, 0.25)
    h.tap(6, 6)
    check(h.press("move") and "Moving" in h.line(), "%s: a thing is picked and on its way: %s" % (name, h.line()))
    page.ev("window.__errors = []; window.addEventListener('error', function (e) { window.__errors.push(String(e.message)); })")
    for width, height in ((700, 650), (1000, 700), (900, 600), (1200, 800), (700, 650), (1000, 700)):
        tab.emulate(width, height, 1)
        time.sleep(0.5)
        room, bar = float(page.ev("parseFloat(document.body.style.getPropertyValue('--pk-h'))")), page.box(".pk-bar")["h"]
        check(abs(room - bar) < 1.5, "%s: at %dx%d the page has the bar's height in --pk-h (%d, the bar is %d)" % (name, width, height, room, bar))
    errors = page.ev("window.__errors")
    check(errors == [], "%s: six resizes while a thing is on its way gave %d window errors: %s" % (name, len(errors), errors[:2]))
    h.press("cancel")
    h.press("cancel")


def run_pickup_turn(page, tab, args, check, env, edges_bytes, edges_js, name, touch):
    """a bar that is open when the phone is turned: the page's room below it (--pk-h) is the bar's height as it is now, not the one it had"""
    global wait_for
    wait_for = env["wait_for"]
    print("[web map maker] the Pick up tool, the room below the page follows the bar when the phone is turned (%s)" % name)
    tab.emulate(844, 390, 2, mobile=True)
    setup(page, args, check, edges_js, edges_bytes, name)
    page.ev("(function () { var b = [].filter.call(document.querySelectorAll('#dialogs button'), function (n) { return n.textContent === 'Throw it away'; })[0]; if (b) b.click(); })()")
    time.sleep(0.4)
    h = Pick(page, tab, touch)
    page.tool("pickup")
    check(wait_for(lambda: page.ev("!!document.querySelector('.pk-bar')"), 10), "%s: Pick up is chosen" % name)
    h.focus_on(6, 6, 20, 0.5, 0.25)
    h.tap(6, 6)
    check("Red hill" in h.line(), "%s: a tap picks the hill: %s" % (name, h.line()))

    def room():
        return float(page.ev("parseFloat(document.body.style.getPropertyValue('--pk-h'))")), page.box(".pk-bar")["h"]

    sideways = room()
    check(abs(sideways[0] - sideways[1]) < 1.5, "%s: sideways the page has the bar's height in --pk-h (%d, the bar is %d)" % (name, sideways[0], sideways[1]))
    tab.emulate(390, 844, 2, mobile=True)
    time.sleep(0.8)
    upright = room()
    check(page.ev("getComputedStyle(document.querySelector('.pk-bar')).position") == "fixed" and upright[1] > sideways[1] + 20, "%s: upright the bar is fixed to the screen and higher (%d, it was %d)" % (name, upright[1], sideways[1]))
    check(abs(upright[0] - upright[1]) < 1.5, "%s: turned upright with the bar open, --pk-h is the bar's height now (%d, the bar is %d)" % (name, upright[0], upright[1]))
    page.ev("window.scrollTo(0, document.documentElement.scrollHeight)")
    time.sleep(0.3)
    check(page.ev("document.getElementById('edfoot').getBoundingClientRect().bottom") <= page.box(".pk-bar")["y"] + 1, "%s: and the page scrolls until its last line is above the bar" % name)
    page.ev("window.scrollTo(0, 0)")
    tab.emulate(844, 390, 2, mobile=True)
    time.sleep(0.8)
    back = room()
    check(abs(back[0] - back[1]) < 1.5, "%s: turned sideways again, --pk-h is the bar's height (%d, the bar is %d)" % (name, back[0], back[1]))
    h.press("cancel")


def run_pickup_drag(page, tab, args, check, env, edges_bytes, edges_js, name, touch):
    """letting go of the mouse or the finger after a drag puts the thing down where the ghost is (reads the file's own bytes): a drag, a blocked place, Escape, a lift outside the map, an area, a click that only places"""
    global wait_for
    wait_for = env["wait_for"]
    print("[web map maker] the Pick up tool, a drag is put down when it is let go (%s)" % name)
    setup(page, args, check, edges_js, edges_bytes, name)
    h = Pick(page, tab, touch)
    page.tool("pickup")
    check(wait_for(lambda: page.ev("!!document.querySelector('.pk-bar')"), 10), "%s: Pick up is chosen" % name)
    h.focus_on(9, 11, 20, 0.4, 0.4)
    undo = lambda: page.ev("document.getElementById('undo-btn').click()")
    data = lambda: edges_bytes(page)["bytes"]
    # 1. a drag of the picked hill: the lift puts it down
    h.tap(6, 6)
    h.drag_cells(6, 6, 12, 12)
    d = data()
    check(layer2_tile(d, 11, 11) == RED_HILL and layer2_tile(d, 5, 5) == EMPTY2 and "Moved it" in h.bar() and h.labels()[0] == "Move", "%s: the lift puts the hill down where the ghost was (row 11, column 11); it is picked still: %s" % (name, h.bar()[:70]))
    undo()
    check(data() == mosh_pit(), "%s: ONE Undo takes it back, byte for byte" % name)
    # 2. a drag that starts on a thing that is not picked picks it and puts it down too
    h.drag_cells_settled(6, 6, 12, 12)
    check(layer2_tile(data(), 11, 11) == RED_HILL, "%s: a press on a thing that is not picked, a drag and a lift: down" % name)
    undo()
    # 3. a red place: the lift puts the thing back where it was picked up (nothing is written, no Undo step) and the bar says why
    h.tap(6, 6)
    h.drag_cells(6, 6, 13, 16)
    check(data() == mosh_pit() and "Put back: it needs a free 4 x 4 square, and the mushroom is in the way" in h.bar() and h.labels()[0] == "Move" and "Done" not in h.labels(), "%s: let go on the mushroom: the hill is back where it was, nothing is written, the bar says why: %s" % (name, h.bar()[:140]))
    x, y = h.point(13, 16)
    check(not h.overlay_has(x - 2 * h.view()[2], y, (240, 138, 132), 8), "%s: and no red ghost is left" % name)
    h.drag_cells(6, 6, 12, 12)
    check(layer2_tile(data(), 11, 11) == RED_HILL and "Put back" not in h.bar(), "%s: the next drag, to a free place, puts it down, and the old note is gone" % name)
    undo()
    check(data() == mosh_pit(), "%s: that was one Undo step, and the put-back made none" % name)
    # 4. a click on a thing only picks it; a click on the ghost only places it (Done puts it down)
    h.tap(6, 6)
    check(data() == mosh_pit() and "Red hill" in h.line(), "%s: a tap picks and writes nothing" % name)
    h.press("move")
    h.tap(12, 12)
    check(data() == mosh_pit() and not h.disabled("done") and "It lands on row 11, column 11" in h.line(), "%s: a tap on a free place puts the ghost there, nothing is written: %s" % (name, h.line()))
    h.press("done")
    check(layer2_tile(data(), 11, 11) == RED_HILL, "%s: Done still puts it down" % name)
    undo()
    h.press("cancel") if h.bar() else None
    # 5. an area: dragged by one of its things, put down as a whole, one Undo
    if touch:
        h.focus_on(63, 55, 14, 0.5, 0.3)
        h.tap(60, 47)
        h.press("area")
        cy, cx, sc = h.view()
        x0, y0 = h.point(58, 46)
        x1, y1 = h.point(67, 64)
        pick_at, to, things = (60, 47), (70, 47), "Area: 8 items"
    else:
        h.focus_on(63, 54, 20, 0.5, 0.3)
        h.tap(66, 44)
        h.press("area")
        cy, cx, sc = h.view()
        x0, y0 = h.point(58, 42)
        x1, y1 = h.point(68, 66)
        pick_at, to, things = (66, 44), (73, 54), "Area: 9 items"
    h.drag_points(x0 - sc / 2, y0 - sc / 2, x1 + sc / 2, y1 + sc / 2)
    check(things in h.line(), "%s: an area is picked (%s): %s" % (name, things, h.line()))
    if touch:
        dr, dc = 5, 0                                              # (the map of a phone is small: a short way, and the lift stays on the map)
    else:
        h.press("move")
        h.tap(to[0], to[1])
        h.press("nudge")
        moved = re.search(r"Moved (\d+) (right|left), (\d+) (down|up)", h.line())                  # (how far a tap on that cell takes the area: a place that is free)
        h.press("cancel")
        dc = int(moved.group(1)) * (1 if moved.group(2) == "right" else -1) if moved else 0
        dr = int(moved.group(3)) * (1 if moved.group(4) == "down" else -1) if moved else 0
        check(moved is not None and (dr or dc) and "Area" in h.line(), "%s: the area is picked again after Cancel (the place is %d, %d away): %s" % (name, dr, dc, h.line()))
    h.drag_cells(pick_at[0], pick_at[1], pick_at[0] + dr, pick_at[1] + dc)
    after = data()
    check(after != mosh_pit() and re.search(r"Moved (8|9) things", h.bar()) is not None, "%s: the lift puts the whole area down: %s" % (name, h.bar()[:80]))
    check(h.labels()[0] == "Move them", "%s: it is picked still, as an area" % name)
    undo()
    check(data() == mosh_pit(), "%s: ONE Undo gives the whole area back" % name)
    h.press("cancel") if h.bar() else None
    # 5b. let go off the map or over the bar: the thing goes back (a finger; a mouse is tried below)
    if touch:
        h.focus_on(9, 11, 18, 0.4, 0.35)
        h.tap(6, 6)
        start, mid = h.point(6, 6), h.point(10, 9)
        box = page.box("#map")
        barbox = page.box(".pk-bar")
        for what, where in (("above the map", (box["x"] + 40, 12)), ("the bar", (barbox["x"] + barbox["w"] / 2, barbox["y"] + barbox["h"] / 2))):
            h.held(start, mid, where)
            check(data() == mosh_pit() and "Red hill" in h.line() and h.labels()[0] == "Move", "a finger lifted %s: the hill goes back, nothing is written: %s" % (what, h.line()))
            h.tap(6, 6) if "Red hill" not in h.line() else None
        h.held(start, mid, mid, during=lambda: page.key("Enter"))
        check(layer2_tile(data(), 5, 5) == EMPTY2 and "Nothing changed" not in h.bar(), "and a finger lifted on the map puts it down: %s" % h.bar()[:50])
        undo()
        return
    # 6. Escape while the button is down: the thing goes back and the lift puts nothing down
    h.focus_on(9, 11, 20, 0.4, 0.4)
    h.tap(6, 6)
    x0, y0 = h.point(6, 6)
    x1, y1 = h.point(12, 12)
    tab.mouse("mouseMoved", x0, y0, button="none")
    tab.mouse("mousePressed", x0, y0)
    for i in range(1, 7):
        tab.mouse("mouseMoved", x0 + (x1 - x0) * i / 6, y0 + (y1 - y0) * i / 6, buttons=1)
    time.sleep(0.3)
    check("Moving the red hill" in h.line() or "It lands" in h.line(), "the ghost follows the held mouse: %s" % h.line())
    page.key("Escape")
    tab.mouse("mouseMoved", x1 + 20, y1 + 20, buttons=1)
    tab.mouse("mouseReleased", x1 + 20, y1 + 20)
    time.sleep(0.3)
    check(data() == mosh_pit() and "Red hill" in h.line() and h.labels()[0] == "Move", "Escape during the drag takes the hill back, and the lift writes nothing: %s" % h.line())
    # 7. let go off the map (over the side panel), over the bar, outside the window, with a pen: the hill goes back and nothing is written
    box = page.box("#map")
    start, mid = h.point(6, 6), h.point(12, 12)
    barbox = page.box(".pk-bar")
    inwin = page.ev("window.innerWidth")
    for what, where, pen in (("the side panel", (box["x"] + box["w"] + 70, box["y"] + 60), False), ("the bar", (barbox["x"] + barbox["w"] / 2, barbox["y"] + barbox["h"] / 2), False), ("outside the window", (inwin + 90, box["y"] + 60), False), ("the side panel with a pen", (box["x"] + box["w"] + 70, box["y"] + 60), True)):
        h.held(start, mid, where, pen=pen)
        check(data() == mosh_pit() and "Red hill" in h.line() and h.labels()[0] == "Move", "let go over %s: the hill goes back, nothing is written: %s" % (what, h.line()))
        h.tap(6, 6) if "Red hill" not in h.line() else None
    # while the pointer is off the map the ghost is gone and the bar says so
    h.held(start, mid, mid, during=lambda: (tab.mouse("mouseMoved", box["x"] + box["w"] + 70, box["y"] + 60, buttons=1), time.sleep(0.25), check("goes back where it was" in h.line(), "off the map the bar says that a lift takes it back: %s" % h.line())))
    # a pen on the map puts it down, Enter while the button is down waits for the lift
    check("Moved it" in h.bar() and layer2_tile(data(), 5, 5) == EMPTY2, "and on the map the lift puts it down: %s" % h.bar()[:50])
    undo()
    h.tap(6, 6)
    h.held(start, mid, mid, pen=True)
    check(layer2_tile(data(), 5, 5) == EMPTY2 and "Moved it" in h.bar(), "a pen lifted on the map puts the hill down: %s" % h.bar()[:60])
    undo()
    h.tap(6, 6)
    h.held(start, mid, mid, during=lambda: page.key("Enter"))
    check(layer2_tile(data(), 5, 5) == EMPTY2 and "Moved it" in h.bar() and "Nothing changed" not in h.bar(), "Enter pressed while the button is down does not throw the pick away: the lift puts it down: %s" % h.bar()[:60])
    undo()
    check(data() == mosh_pit(), "and ONE Undo takes it back")
    # 8. the hover ghost follows the mouse while Move is on, starts no drag, and a lift with the ghost under it puts nothing down by itself
    h.tap(6, 6)
    h.press("move")
    for r, c in ((10, 10), (12, 12), (14, 14)):
        x, y = h.point(r, c)
        tab.mouse("mouseMoved", x, y, button="none")
        time.sleep(0.15)
    x, y = h.point(12, 12)
    tab.mouse("mouseMoved", x, y, button="none")
    time.sleep(0.2)
    check(data() == mosh_pit() and "It lands on row 11, column 11" in h.line(), "the hover ghost writes nothing and follows the mouse: %s" % h.line())
    tab.mouse("mousePressed", x, y)
    time.sleep(0.1)
    tab.mouse("mouseReleased", x, y)
    time.sleep(0.3)
    check(data() == mosh_pit() and not h.disabled("done") and "Done" in h.labels(), "a click on the hover ghost only places it, Done is on")
    h.press("done")
    check(layer2_tile(data(), 11, 11) == RED_HILL, "and Done puts it down")
    undo()
