"""The straight line in the real browser (tests/scripts/web_mapmaker_browser.py, part `line`; docs/MAP_EDITOR.md, "Painting"): kept in its own file so that neither passes the line limit of the test files.
Real mouse events with the Shift and Ctrl modifiers of the DevTools protocol (pointer events with shiftKey and ctrlKey) drive Ground, Objects and Erase; the bytes are what Download hands over, read
here with struct. A stroke with Shift or Ctrl keeps to one line through its first cell, across or down; with no key it is free; Command and Alt alone change nothing; Hills, Pick up and Pan are
untouched (a drag looks around); a zoomed and panned view; the edge of the map; no text is selected by a Shift drag. The page helpers of the main file (wait_for, the Page class) are handed in."""
import struct
import time

wait_for = None
MODS = {"": 0, "alt": 1, "ctrl": 2, "meta": 4, "shift": 8, "both": 10}
DIRT, ROCK, GRAVEL, EMPTY2 = 487, 302, 485, 0x7FFE


def where(page, r, c, fr=0.5, fc=0.5):
    """the page point of a place in cell (r, c) (a row or column outside the map is a point outside it), for the view as it is: the canvas says its view exactly (data-view: row, column, pixels a cell)"""
    box = page.box("#map")
    cy, cx, scale = [float(v) for v in page.ev("document.getElementById('map').getAttribute('data-view')").split(",")]
    return box["x"] + box["w"] / 2 + (c + fc - cx) * scale, box["y"] + box["h"] / 2 + (r + fr - cy) * scale


def cells_with(page, edges_bytes, layer, tile):
    """the set of (row, column) of a layer that hold `tile`, in the bytes that Download hands over"""
    got = edges_bytes(page)
    data, rows, columns = got["bytes"], int(page.state()["rows"]), int(page.state()["columns"])
    base = got["layer1"] + (6 * rows * columns if layer == 2 else 0)
    return {(i // columns, i % columns) for i in range(rows * columns) if struct.unpack_from("<H", data, base + 6 * i)[0] == tile}


def stroke(page, tab, start, steps, edges_bytes, layer, tile, interpolate=6, keep_down=False):
    """a real stroke: the mouse goes down in cell `start` (a modifier as the third item), moves in a hand's small steps to each of `steps` ([row, column, modifier]) and comes up where it ended; the cells
    of the layer that hold `tile` afterwards (the page is given a moment to take the last move)"""
    sx, sy = where(page, start[0], start[1])
    tab.mouse("mouseMoved", sx, sy, button="none")
    tab.mouse("mousePressed", sx, sy, modifiers=MODS[start[2] if len(start) > 2 else ""])
    x0, y0 = sx, sy
    mod = start[2] if len(start) > 2 else ""
    for step in steps:
        mod = step[2] if len(step) > 2 else ""
        fr, fc = (step[3] if len(step) > 3 else (0.5, 0.5))
        x1, y1 = where(page, step[0], step[1], fr, fc)
        for i in range(1, interpolate + 1):
            tab.mouse("mouseMoved", x0 + (x1 - x0) * i / interpolate, y0 + (y1 - y0) * i / interpolate, buttons=1, modifiers=MODS[mod])
        x0, y0 = x1, y1
    if not keep_down:
        tab.mouse("mouseReleased", x0, y0, modifiers=MODS[mod])
    time.sleep(0.25)
    return cells_with(page, edges_bytes, layer, tile)


def row_of(r, c0, c1):
    return {(r, c) for c in range(min(c0, c1), max(c0, c1) + 1)}


def col_of(c, r0, r1):
    return {(r, c) for r in range(min(r0, r1), max(r0, r1) + 1)}


def run_line(page, tab, args, check, env, edges_bytes, edges_js):
    global wait_for
    wait_for = env["wait_for"]
    print("[web map maker] the straight line: Shift or Ctrl held in a stroke, 1280 x 800")
    tab.emulate(1280, 800, 1)
    check(page.open(edges_js) and page.ready(), "the page starts with its map core")
    page.ev(edges_js)
    rows, columns = int(page.state()["rows"]), int(page.state()["columns"])
    check(rows == 31 and columns == 31, "a 31 x 31 map, whole on the screen: %d x %d" % (rows, columns))
    # Ground
    page.tile(DIRT)
    got = stroke(page, tab, (3, 3), [(4, 8, "shift"), (5, 15, "shift")], edges_bytes, 1, DIRT)
    check(got == row_of(3, 3, 15), "Ground, Shift: a drag down and across (the hand goes 2 rows down over 12 columns) paints the row of the first cell only: %s" % sorted(got))
    got = stroke(page, tab, (10, 5), [(14, 6, "ctrl"), (20, 5, "ctrl")], edges_bytes, 1, DIRT)
    check(got - row_of(3, 3, 15) == col_of(5, 10, 20), "Ground, Ctrl: a drag down the page paints the column of the first cell only: %s" % sorted(got - row_of(3, 3, 15)))
    page.ev("document.getElementById('undo-btn').click()")
    time.sleep(0.2)
    check(cells_with(page, edges_bytes, 1, DIRT) == row_of(3, 3, 15), "one Undo takes the whole column back, the row stays")
    page.ev("document.getElementById('undo-btn').click()")
    time.sleep(0.2)
    check(cells_with(page, edges_bytes, 1, DIRT) == set(), "and one more takes the row back")
    got = stroke(page, tab, (8, 8), [(12, 12, "shift")], edges_bytes, 1, DIRT)
    check(got == row_of(8, 8, 12), "a tie (as far down as across) goes across: %s" % sorted(got))
    page.ev("document.getElementById('undo-btn').click()")
    time.sleep(0.2)
    got = stroke(page, tab, (8, 8), [(8, 8, "shift", (0.1, 0.9)), (8, 8, "shift", (0.9, 0.1)), (14, 8, "shift")], edges_bytes, 1, DIRT)
    check(got == col_of(8, 8, 14), "the pointer moves about in the first cell and then goes down: down, and no cell but the first until it left: %s" % sorted(got))
    page.ev("document.getElementById('undo-btn').click()")
    time.sleep(0.2)
    got = stroke(page, tab, (10, 10), [(10, 15, "shift"), (10, 10, "shift"), (14, 10, "shift")], edges_bytes, 1, DIRT)
    check(got == row_of(10, 10, 15), "a stroke that comes back to its first cell keeps its line (across): %s" % sorted(got))
    page.ev("document.getElementById('undo-btn').click()")
    time.sleep(0.2)
    # with no key, and with the keys that mean nothing here, the stroke is free
    for mod in ("", "meta", "alt"):
        got = stroke(page, tab, (4, 4), [(9, 9, mod)], edges_bytes, 1, DIRT)
        check({(i, i) for i in range(4, 10)} <= got and len(got) >= 6 and not row_of(4, 4, 9) <= got, "a free diagonal stroke with %s: the diagonal is painted (%d cells)" % (mod or "no key", len(got)))
        page.ev("document.getElementById('undo-btn').click()")
        time.sleep(0.2)
    # Shift let go in the middle, and pressed in the middle
    got = stroke(page, tab, (5, 5), [(5, 10, "shift"), (9, 12, "")], edges_bytes, 1, DIRT)
    check(row_of(5, 5, 10) <= got and (9, 12) in got and (7, 11) in got and (5, 12) not in got, "Shift let go in the middle: the line to where it ended, then free to the pointer: %s" % sorted(got))
    page.ev("document.getElementById('undo-btn').click()")
    time.sleep(0.2)
    got = stroke(page, tab, (5, 5), [(5, 10, ""), (7, 12, ""), (11, 13, "shift")], edges_bytes, 1, DIRT)
    check(col_of(12, 7, 11) <= got and (11, 13) not in got and (7, 12) in got, "Shift pressed in the middle: a line down through the cell where the stroke was: %s" % sorted(got))
    page.ev("document.getElementById('undo-btn').click()")
    time.sleep(0.2)
    # the next stroke is free again
    got1 = stroke(page, tab, (2, 2), [(2, 9, "shift")], edges_bytes, 1, DIRT)
    got2 = stroke(page, tab, (12, 12), [(16, 15, "")], edges_bytes, 1, DIRT, interpolate=7) - got1       # (7 steps: 6 would put the pointer exactly on the edges between columns, 13.0, 14.0 and 15.0, and a rounding of the canvas size would decide the cell)
    check(got1 == row_of(2, 2, 9) and {(13, 13), (14, 14), (15, 15)} <= got2 and (12, 15) not in got2, "the next stroke begins free, whatever the one before did")
    for _ in range(2):
        page.ev("document.getElementById('undo-btn').click()")
        time.sleep(0.2)
    # the edge of the map
    got = stroke(page, tab, (15, 25), [(15, 32, "shift"), (15, 40, "shift")], edges_bytes, 1, DIRT)
    check(got == row_of(15, 25, 30), "a stroke that goes past the right edge of the map stops at its last column: %s" % sorted(got))
    page.ev("document.getElementById('undo-btn').click()")
    time.sleep(0.2)
    got = stroke(page, tab, (3, 20), [(-3, 21, "ctrl")], edges_bytes, 1, DIRT)
    check(got == col_of(20, 0, 3), "past the top edge: %s" % sorted(got))
    page.ev("document.getElementById('undo-btn').click()")
    time.sleep(0.2)
    # no text is selected by a drag with Shift held
    stroke(page, tab, (6, 6), [(6, 14, "shift")], edges_bytes, 1, DIRT)
    check(page.ev("String(window.getSelection())") == "", "a drag with Shift held selects no text on the page")
    page.ev("document.getElementById('undo-btn').click()")
    time.sleep(0.2)
    # a zoomed and panned view
    page.ev("document.querySelector('[data-act=zoom-in]').click(); document.querySelector('[data-act=zoom-in]').click(); document.querySelector('[data-act=zoom-in]').click()")
    box = page.box("#map")
    page.tool("pan")
    page.drag(box["x"] + 600, box["y"] + 400, box["x"] + 540, box["y"] + 330)
    page.tool("ground")
    time.sleep(0.3)
    scale = float(page.ev("document.getElementById('map').getAttribute('data-scale')"))
    cy, cx = [float(v) for v in page.ev("document.getElementById('map').getAttribute('data-view')").split(",")[:2]]
    r0, c0 = int(cy), int(cx)
    check(scale > 30, "zoomed in: %.1f pixels a cell" % scale)
    got = stroke(page, tab, (r0, c0 - 2), [(r0 + 1, c0 + 1, "shift"), (r0 + 2, c0 + 3, "shift")], edges_bytes, 1, DIRT)
    check(got == row_of(r0, c0 - 2, c0 + 3), "zoomed and panned, Shift across: %s" % sorted(got))
    got = stroke(page, tab, (r0 - 2, c0 + 1), [(r0 + 2, c0, "ctrl")], edges_bytes, 1, DIRT) - got
    check(got == col_of(c0 + 1, r0 - 2, r0 + 2) - row_of(r0, c0 - 2, c0 + 3), "and Ctrl down: %s" % sorted(got))
    page.shot(args.shots, "mapmaker-line-zoomed")
    # Objects and Erase
    page.ev("document.querySelector('[data-act=zoom-out]').click(); document.querySelector('[data-act=zoom-out]').click(); document.querySelector('[data-act=zoom-out]').click(); document.querySelector('[data-act=zoom-out]').click()")
    time.sleep(0.3)
    page.tool("objects")
    page.tile(ROCK)
    got = stroke(page, tab, (20, 4), [(21, 9, "shift"), (23, 14, "shift")], edges_bytes, 2, ROCK)
    check(got == row_of(20, 4, 14), "Objects, Shift: a row of rocks: %s" % sorted(got))
    got = stroke(page, tab, (22, 20), [(25, 21, "ctrl"), (28, 20, "ctrl")], edges_bytes, 2, ROCK) - row_of(20, 4, 14)
    check(got == col_of(20, 22, 28), "Objects, Ctrl: a column of rocks: %s" % sorted(got))
    page.tool("erase")
    before = cells_with(page, edges_bytes, 2, ROCK)
    stroke(page, tab, (20, 6), [(21, 8, "shift")], edges_bytes, 2, ROCK)
    after = cells_with(page, edges_bytes, 2, ROCK)
    check(before - after == row_of(20, 6, 8), "Erase, Shift: only the cells of the line are taken away: %s" % sorted(before - after))
    stroke(page, tab, (23, 20), [(26, 21, "ctrl")], edges_bytes, 2, ROCK)
    again = cells_with(page, edges_bytes, 2, ROCK)
    check(after - again == col_of(20, 23, 26), "Erase, Ctrl: a column: %s" % sorted(after - again))
    # the tools that are not strokes: a drag looks around, with the key or without it
    snapshot = cells_with(page, edges_bytes, 2, ROCK)
    for tool in ("hills", "pan", "pickup"):
        page.tool(tool)
        time.sleep(0.4)
        centers = []
        for mod in ("", "shift", "ctrl"):
            page.ev("for (var i = 0; i < 4; i++) document.querySelector('[data-act=zoom-out]').click(); for (var j = 0; j < 2; j++) document.querySelector('[data-act=zoom-in]').click();")
            time.sleep(0.2)
            box = page.box("#map")
            x, y = box["x"] + 500, box["y"] + 300
            before_center = page.state()["center"]
            tab.mouse("mouseMoved", x, y, button="none")
            tab.mouse("mousePressed", x, y, modifiers=MODS[mod])
            for i in range(1, 9):
                tab.mouse("mouseMoved", x + 8 * i, y + 5 * i, buttons=1, modifiers=MODS[mod])
            tab.mouse("mouseReleased", x + 64, y + 40, modifiers=MODS[mod])
            time.sleep(0.2)
            centers.append(page.state()["center"] != before_center)
        check(all(centers), "%s: a drag looks around (the view moves), with no key, Shift or Ctrl" % tool)
        check(cells_with(page, edges_bytes, 2, ROCK) == snapshot and not any(cells_with(page, edges_bytes, 2, hill) for hill in (245, 246, 247, 248)), "%s: and nothing on the map changed" % tool)
    page.shot(args.shots, "mapmaker-line-done")
