"""The Fix edges part of the real-browser check (tests/scripts/web_mapmaker_browser.py, part `edges`): kept in its own file so that neither passes the line limit of the test files. The page helpers of
the main file (wait_for, spread, REPO, the screenshot and click helpers of Page) are handed in by `run_edges`. After Fix edges and the maintainer's scene it runs Auto edges with the pointer held down (no cell
is drawn black while the stroke goes on: `run_midstroke`, at 1 and 2 device pixels a pixel) and the places that the game has no piece for (the dashed line, the note, the box in Fix edges: `run_places`)."""
import base64
import json
import os
import shutil
import struct
import tempfile
import time

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


wait_for = None
spread = None
REPO = None


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


PLAIN_OF = {"g": 485, "d": 487, "s": 571, "m": 439, "w": 476}


def plain_copy(source, target):
    """a copy of an original map (read only) with every edge piece of layer 1 put back to its plain tile, written to `target` (a scratch file): a map that needs its edges fixed (version 8 maps)"""
    with open(source, "rb") as f:
        b = bytearray(f.read())
    names = struct.unpack_from("<H", b, 40)[0] + 1
    entry = lambda k: bytes(b[42 + 11 * k: 53 + 11 * k]).split(b"\0")[0].decode("latin1")
    at = 42 + 11 * names
    rows, columns = struct.unpack_from("<II", b, at)
    base = at + 8
    for i in range(rows * columns):
        t = struct.unpack_from("<H", b, base + 6 * i)[0]
        name = entry(t) if t < names else ""
        if len(name) == 5 and name[0] in "gdsmMw" and name[1] in "gdsmMwW" and name[2:4] not in ("01",) and name[2:4].isdigit():
            struct.pack_into("<H", b, base + 6 * i, PLAIN_OF[name[0].lower()])
    with open(target, "wb") as f:
        f.write(bytes(b))


def run_edges(page, tab, args, check, env):
    scratch = tempfile.mkdtemp(prefix="ants_edges_map.")
    try:
        run_edges_in(page, tab, args, check, scratch, env)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def run_edges_in(page, tab, args, check, scratch, env):
    global wait_for, spread, REPO
    wait_for, spread, REPO = env["wait_for"], env["spread"], env["REPO"]
    """Fix edges (docs/MAP_EDITOR.md, "Fix edges") on a real map in the real browser: the real bytes (what Download hands over), the real pixels (every pixel of the canvas), one Undo"""
    print("[web map maker] Fix edges, 1280 x 800")
    check(page.open() and page.ready(), "the page starts")
    check(wait_for(lambda: page.ev("!!window.MapEdges && !!window.MapGround && !document.getElementById('edges-btn').disabled"), 15), "the Fix edges button is on once the lazy scripts are there")
    page.ev(EDGES_JS)
    plain_copy(os.path.join(REPO, "Original-Ants", "Maps", "MEDIUM.LVL"), os.path.join(scratch, "MEDIUMPLAIN.LVL"))
    page.choose_file(os.path.join(scratch, "MEDIUMPLAIN.LVL"))
    check(wait_for(lambda: page.state()["name"] == "MEDIUMPLAIN"), "Medium with all its edge pieces put back to plain tiles is opened")
    check(wait_for(lambda: spread(page, 5, 5) >= 4, 10), "its pictures are drawn")
    first = edges_bytes(page)
    total = first["left"]
    check(total > 500, "the rule has cells to fix on it (%s)" % total)
    pixels = still_pixels(page)
    check(page.button_off("#undo-btn"), "nothing to undo yet")
    # the dialog on the whole map, then Cancel
    check(edges_open(page), "the Fix edges dialog opens")
    check(page.ev("document.querySelector('.dlg.edges h2').textContent") == "Fix edges" and page.ev("document.querySelector('.dlg.edges input:checked').value") == "part", "titled Fix edges, with the part you can see chosen")
    check(edges_count(page) == total, "the whole map is on the screen, so the part is all the %d cells (%d)" % (total, edges_count(page)))
    differ = page.ev("(function () { var c = document.querySelectorAll('.dlg.edges canvas'), d = [0, 1].map(function (i) { return c[i].getContext('2d').getImageData(0, 0, c[i].width, c[i].height).data; }), n = 0, i; for (i = 0; i < d[0].length; i += 4) if (d[0][i] !== d[1][i] || d[0][i + 1] !== d[1][i + 1] || d[0][i + 2] !== d[1][i + 2]) n++; var seen = {}; for (i = 0; i < d[0].length; i += 4) seen[d[0][i] + ',' + d[0][i + 1] + ',' + d[0][i + 2]] = 1; return [n, c[0].width, c[0].height, c[1].width, c[1].height, Object.keys(seen).length]; })()")
    check(differ[0] > 500 and differ[1:3] == differ[3:5][:2] and differ[5] >= 120, "before and after are drawn from the pictures (%d colors: not flat) and differ in %d pixels, in the same size" % (differ[5], differ[0]))
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
    check(0 < part < whole == total, "the part is fewer cells than the whole map (%d of %d)" % (part, whole))
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
    # the whole map on top of it: the rest
    check(edges_open(page, whole=True), "the dialog opens, and the whole map is chosen")
    rest = edges_count(page)
    check(rest + part == total, "the rest is what is left (%d and %d)" % (rest, part))
    page.ev("[].filter.call(document.querySelectorAll('.dlg.edges .btn'), function (b) { return b.textContent === 'Fix edges'; })[0].click()")
    check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")), "Fix edges again")
    every = edges_bytes(page)
    check(every["left"] == 0 and every["layer1"] == first["layer1"], "the fixed map has nothing left to fix")
    te = every["bytes"][every["layer1"] - 8:]
    check(len(te) == len(ta) and len({(i - 8) // w for i in range(len(ta)) if ta[i] != te[i]}) == total, "exactly all the cells differ from the map as it was opened")
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
    # the game's own maps are the gold standard: TREASURE needs nothing, ISLANDS two cells that the author left plain; no boundary gets a piece on both facing cells (a dark seam)
    page.choose_file(os.path.join(REPO, "Original-Ants", "Maps", "TREASURE.LVL"))
    check(wait_for(lambda: page.state()["name"] == "TREASURE"), "Treasure is opened")
    check(wait_for(lambda: spread(page, 5, 5) >= 4, 10), "its pictures are drawn")
    gold = still_pixels(page)
    check(edges_open(page, whole=True) and edges_count(page) == 0 and page.button_off(".dlg.edges .dlg-foot .btn:last-child"), "Fix edges on the original Treasure map: nothing changes, and the button is off")
    page.key("Escape")
    check(still_pixels(page) == gold and page.button_off("#undo-btn"), "and its pixels and history are as they were")
    page.choose_file(os.path.join(REPO, "Original-Ants", "Maps", "ISLANDS.LVL"))
    check(wait_for(lambda: page.state()["name"] == "ISLANDS"), "Islands is opened")
    check(wait_for(lambda: spread(page, 5, 5) >= 4, 10), "its pictures are drawn")
    isl = edges_bytes(page)
    isl_pixels = still_pixels(page)
    check(edges_open(page, whole=True) and edges_count(page) == 2, "Fix edges on the original Islands map: the two plain cells that the author left (%d)" % edges_count(page))
    page.ev("[].filter.call(document.querySelectorAll('.dlg.edges .btn'), function (b) { return b.textContent === 'Fix edges'; })[0].click()")
    check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")), "Fix edges closes the dialog")
    fixed = edges_bytes(page)
    ta, tb = isl["bytes"][isl["layer1"] - 8:], fixed["bytes"][fixed["layer1"] - 8:]
    cells = sorted({(i - 8) // 6 for i in range(len(ta)) if ta[i] != tb[i]}) if len(ta) == len(tb) else []
    names = [tb[8 + 6 * c] | (tb[9 + 6 * c] << 8) for c in cells]
    entry = lambda k: fixed["bytes"][fixed["dictAt"] + 2 + 11 * k: fixed["dictAt"] + 13 + 11 * k].split(b"\0")[0].decode("latin1")
    columns = fixed["columns"]
    check([(c // columns, c % columns, entry(n)) for c, n in zip(cells, names)] == [(34, 53, "wm06a"), (46, 17, "gm03a")], "exactly those two cells have the expected pieces (%s)" % [(c // columns, c % columns, entry(n)) for c, n in zip(cells, names)])
    check(fixed["left"] == 0 and still_pixels(page) != isl_pixels, "nothing is left to fix, and the picture changed")
    page.ev("document.getElementById('undo-btn').click()")
    check(edges_bytes(page)["sha"] == isl["sha"] and still_pixels(page) == isl_pixels, "one Undo gives back the exact bytes and pixels")
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
    page.shot(args.shots, "mapmaker-fix-edges-phone-check")
    page.key("Escape")
    tab.emulate(1280, 800, 1)
    time.sleep(0.5)
    run_scene(page, tab, args, check)
    run_midstroke(page, tab, args, check)
    run_places(page, tab, args, check)


# the owner's picture: a blob of mud on gravel with two strips of water one cell wide inside it, the lower one bending (g gravel, m mud, w water), and what Auto edges must leave
SCENE = ["gggggggggggg", "gmmmmmmmmmmg", "gmmmwmmmmmmg", "gmmmwmmmmmmg", "gmmmwmmmmmmg", "gmmmwmmmmmmg", "gmmmmmmmmmmg", "gmmmwwmmmmmg", "gmmmmwmmmmmg", "gmmmmwmmmmmg", "gmmmmmmmmmmg", "gggggggggggg"]
SCENE_AT = {(1, 4): "MW08a", (2, 3): "MW06a", (2, 4): "w01a", (2, 5): "MW05a", (5, 3): "MW06a", (5, 5): "MW05a", (6, 4): "M01a", (7, 3): "MW06a", (7, 4): "w01a", (7, 5): "wm04a", (8, 4): "MW04a", (8, 6): "MW05a",
            (9, 4): "MW06a", (9, 5): "w01a", (9, 6): "MW05a", (10, 5): "MW03a", (0, 4): "gm08a", (11, 4): "gm03a"}
LETTER_TILE = {"g": 485, "m": 439, "w": 476}


def paint_letter(page, tab, letter):
    """the cells of SCENE that hold `letter`, one drag for each run in a row, with a brush of 1"""
    page.tile(LETTER_TILE[letter])
    for r, row in enumerate(SCENE):
        c = 0
        while c < len(row):
            if row[c] != letter:
                c += 1
                continue
            e = c
            while e + 1 < len(row) and row[e + 1] == letter:
                e += 1
            x0, y0 = page.cell_point(r + 8, c + 8)            # (the picture is put 8 cells down and to the right on the new map of 31 x 31)
            x1, y1 = page.cell_point(r + 8, e + 8)
            page.drag(x0, y0, x1, y1, steps=max(2, 2 * (e - c)))
            c = e + 1


def fresh_map(page, check):
    """a new Classic map (31 x 31 of gravel) with Auto edges on"""
    page.press_act("new")
    wait_for(lambda: page.ev("!!document.querySelector('.kinds')"), 8)
    page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
    wait_for(lambda: page.state()["rows"] == "31" and not page.ev("!!document.querySelector('.dlg')"), 8)
    if page.ev("document.getElementById('auto-edges-btn').getAttribute('aria-checked')") != "true":
        page.ev("document.getElementById('auto-edges-btn').click()")
    check(page.ev("document.getElementById('auto-edges-btn').getAttribute('aria-checked')") == "true", "Auto edges is On")


def scene_names(page):
    got = edges_bytes(page)
    b, d, at, columns = got["bytes"], got["dictAt"], got["layer1"], got["columns"]
    name = lambda r, c: b[d + 2 + 11 * struct.unpack_from("<H", b, at + 6 * (r * columns + c))[0]: d + 13 + 11 * struct.unpack_from("<H", b, at + 6 * (r * columns + c))[0]].split(b"\0")[0].decode("latin1")
    return name, got


def run_scene(page, tab, args, check):
    """Auto edges on the owner's picture, in both orders of painting, with the mouse: every boundary of the water and the mud has its piece (the strips of water one cell wide have none of their own: the mud
    beside them wears), the map is the one that Fix edges makes (nothing is left to fix), and the pictures are drawn from the game's tiles"""
    for order in (("m", "w"), ("w", "m")):
        fresh_map(page, check)
        page.ev("(function () { var r = document.getElementById('brush-range'); r.value = 1; r.dispatchEvent(new Event('input', {bubbles: true})); })()")
        for letter in order:
            paint_letter(page, tab, letter)
        time.sleep(0.4)
        name, got = scene_names(page)
        wrong = [(r, c, name(r + 8, c + 8), want) for (r, c), want in sorted(SCENE_AT.items()) if name(r + 8, c + 8) != want]
        check(not wrong, "Auto edges, %s first: the water and the mud have their pieces (%s)" % ("mud" if order[0] == "m" else "water", wrong[:3]))
        check(got["left"] == 0, "Auto edges, %s first: Fix edges finds nothing left to fix (%d)" % ("mud" if order[0] == "m" else "water", got["left"]))
        page.shot(args.shots, "mapmaker-auto-edges-water-in-mud-" + ("mud-first" if order[0] == "m" else "water-first"))


BLACK_JS = r"""
window.__black = function (r0, r1, c0, c1) {          // the cells whose middle pixel is the page's own back color: a ground cell drawn with no picture
    var cv = document.getElementById('map'), b = cv.getBoundingClientRect(), k = cv.width / b.width, v = cv.getAttribute('data-view').split(','), cy = +v[0], cx = +v[1], s = +v[2], ctx = cv.getContext('2d');
    var bk = MapTiles.BACK.replace('#', ''), br = parseInt(bk.slice(0, 2), 16), bg = parseInt(bk.slice(2, 4), 16), bb = parseInt(bk.slice(4, 6), 16), out = [], r, c;
    for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) {
        var d = ctx.getImageData(Math.round((b.width / 2 + (c + 0.5 - cx) * s) * k), Math.round((b.height / 2 + (r + 0.5 - cy) * s) * k), 1, 1).data;
        if (Math.abs(d[0] - br) < 6 && Math.abs(d[1] - bg) < 6 && Math.abs(d[2] - bb) < 6) out.push([r, c]);
    }
    return out;
};
window.__gold = function (x0, y0, x1, y1) {           // how many pixels of the part of the page (x0, y0, x1, y1) are the gold of the dashed line
    var cv = document.getElementById('map'), b = cv.getBoundingClientRect(), k = cv.width / b.width, ctx = cv.getContext('2d'), n = 0, i;
    var x = Math.max(0, Math.round((x0 - b.left) * k)), y = Math.max(0, Math.round((y0 - b.top) * k)), w = Math.max(1, Math.round((x1 - x0) * k)), h = Math.max(1, Math.round((y1 - y0) * k));
    var d = ctx.getImageData(x, y, Math.min(w, cv.width - x), Math.min(h, cv.height - y)).data;
    for (i = 0; i < d.length; i += 4) if (d[i] > 225 && d[i + 1] > 180 && d[i + 2] < 120) n++;
    return n;
};
"""


def hold_stroke(page, tab, row, c0, c1, then):
    """the pointer goes down at (row, c0), moves along the row to c1 and is still down when `then()` is called; it is lifted after"""
    x0, y0 = page.cell_point(row, c0)
    tab.mouse("mouseMoved", x0, y0, button="none")
    tab.mouse("mousePressed", x0, y0)
    x1 = y1 = 0
    for i in range(1, 25):
        x1, y1 = page.cell_point(row, c0 + (c1 - c0) * i / 24.0)
        tab.mouse("mouseMoved", x1, y1, buttons=1)
    time.sleep(0.3)
    result = then()
    tab.mouse("mouseReleased", x1, y1)
    time.sleep(0.3)
    return result


def run_midstroke(page, tab, args, check):
    """Auto edges with the pointer still down (docs/MAP_EDITOR.md, "Fix edges"): the edge pieces are named in the dictionary as they appear, so no cell is drawn black before the stroke ends; the water
    has its ring of mud (the diagonals stay gravel); one Undo takes the lake and its ring; at 1 and at 2 device pixels a pixel"""
    for dpr in (1, 2):
        print("[web map maker] Auto edges with the pointer down, 1280 x 800 at %d pixel%s a pixel" % (dpr, "" if dpr == 1 else "s"))
        tab.emulate(1280, 800, dpr)
        time.sleep(0.6)
        fresh_map(page, check)
        page.ev(BLACK_JS)
        check(wait_for(lambda: spread(page, 5, 5) >= 4, 10), "the pictures are drawn")
        page.tile(476)
        page.set_brush(3)
        before = scene_names(page)[1]
        mid = hold_stroke(page, tab, 15, 8, 18, lambda: json.loads(page.ev("JSON.stringify(window.__black(10, 20, 4, 24))")))
        check(mid == [], "in the middle of a stroke of water on gravel (the pointer is down) no cell is drawn black (%s)" % mid[:4])
        check(not page.ev("document.getElementById('gap-note') && !document.getElementById('gap-note').hidden"), "and no place is marked while the pointer is down")
        after_stroke = json.loads(page.ev("JSON.stringify(window.__black(10, 20, 4, 24))"))
        check(after_stroke == [], "after the stroke no cell is black either (%s)" % after_stroke[:4])
        name, got = scene_names(page)
        kinds = lambda r, c: name(r, c)[:1]
        check(kinds(13, 12) in "Mm" and kinds(17, 12) in "Mm" and kinds(15, 6) in "Mm" and kinds(15, 20) in "Mm", "the lake (rows 14 to 16) has its ring of mud on its four sides")
        check(kinds(13, 6) == "g" and kinds(17, 20) == "g" and kinds(12, 12) == "g", "and the diagonals and the cells two away stay gravel")
        check(got["left"] == 0, "Fix edges finds nothing left to fix (%d)" % got["left"])
        page.shot(args.shots, "mapmaker-auto-edges-ring-%dx" % dpr)
        page.ev("document.getElementById('undo-btn').click()")
        name2, back = scene_names(page)
        tiles = lambda g: [struct.unpack_from("<H", g["bytes"], g["layer1"] + 6 * i)[0] for i in range(g["rows"] * g["columns"])]
        old_names = lambda g, f: [g["bytes"][g["dictAt"] + 2 + 11 * k: g["dictAt"] + 13 + 11 * k].split(b"\0")[0] for k in range(g["names"])]
        a, b = old_names(before, None), old_names(back, None)
        check(tiles(back) == tiles(before) and len(a) == len(b) and all(x == y or k == 476 for k, (x, y) in enumerate(zip(a, b))), "one Undo takes the lake, its ring and its pieces: the tiles and the dictionary are as before (the water's own name stays)")
    tab.emulate(1280, 800, 1)
    time.sleep(0.5)


def run_places(page, tab, args, check):
    """The places that the game has no piece for (docs/MAP_EDITOR.md, "Cells with no piece"): strip, gap, strip painted with the mouse: a dashed gold line and the note on the map, not while a stroke
    goes on, gone after Undo; the box in Fix edges lists the place and Show brings it to the middle; on a phone the box is inside the screen with 44 pixel buttons"""
    print("[web map maker] the places that have no piece in the game")
    fresh_map(page, check)
    page.ev(BLACK_JS)
    check(wait_for(lambda: spread(page, 5, 5) >= 4, 10), "the pictures are drawn")
    page.tile(476)
    page.set_brush(1)
    note = lambda: page.ev("(function () { var n = document.getElementById('gap-note'); return !!n && !n.hidden && n.getBoundingClientRect().width > 0; })()")
    check(not note(), "a map with no such place shows no note")
    page.drag(*(page.cell_point(8, 6) + page.cell_point(16, 6)), steps=16)
    check(not note(), "one strip of water has a piece for every boundary: no note")
    page.drag(*(page.cell_point(8, 8) + page.cell_point(16, 8)), steps=16)
    tab.mouse("mouseMoved", 5, 5, button="none")
    check(wait_for(note, 5), "two strips of water one cell apart: the note says that the game has no piece for them")
    check("strip" in page.ev("document.getElementById('gap-note').textContent"), "and it speaks of the strip")
    x0, y0 = page.cell_point(8, 5)
    x1, y1 = page.cell_point(16, 9)
    gold = page.ev("window.__gold(%f, %f, %f, %f)" % (x0, y0, x1, y1))
    check(gold >= 40, "a dashed gold line is drawn round the three columns (%d gold pixels)" % gold)
    page.shot(args.shots, "mapmaker-places-strip-gap-strip")
    # the line is not drawn while a stroke goes on
    seen = hold_stroke(page, tab, 22, 4, 12, lambda: [note(), page.ev("window.__gold(%f, %f, %f, %f)" % (x0, y0, x1, y1))])
    check(seen[0] is False and seen[1] <= 5, "while the pointer is down the note and the line are gone (%s)" % seen)
    # the box in Fix edges lists it; Show closes the box and brings the place to the middle
    tab.mouse("mouseMoved", 5, 5, button="none")
    check(edges_open(page, whole=True), "the Fix edges dialog opens")
    text = page.ev("(function () { var b = document.querySelector('.dlg.edges .edgaps'); return b && !b.hidden ? b.textContent : ''; })()")
    check("1 place has no piece in the game" in text and "Rows 7 to 17, columns 6 to 8" in text, "its box lists the place with where it is (%s)" % text[:140])
    page.shot(args.shots, "mapmaker-places-fix-edges-box")
    page.ev("document.querySelector('.dlg.edges .edgaps button').click()")
    check(wait_for(lambda: not page.ev("!!document.querySelector('.dlg')")), "Show closes the box")
    look = "(function () { var cv = document.getElementById('map'), b = cv.getBoundingClientRect(), v = cv.getAttribute('data-view').split(','), cy = +v[0], cx = +v[1], s = +v[2]; return Math.abs(12.5 - cy) * s < b.height / 2 && Math.abs(7.5 - cx) * s < b.width / 2 && s >= 22; })()"
    check(wait_for(lambda: page.ev(look), 5), "and the middle of the place is on the screen, at 22 pixels a cell or more")
    # Undo (the last stroke is the lake on row 22, then the second strip): the note goes with the places
    page.ev("document.getElementById('undo-btn').click()")
    page.ev("document.getElementById('undo-btn').click()")
    tab.mouse("mouseMoved", 5, 5, button="none")
    check(wait_for(lambda: not note(), 5), "after Undo the strip is gone and so is the note")
    page.ev("document.getElementById('redo-btn').click()")
    check(wait_for(note, 5), "and Redo brings both back")
    # a phone: the box is inside the screen, its buttons are 44 pixels, nothing scrolls sideways
    tab.emulate(390, 844, 2, mobile=True)
    time.sleep(0.5)
    check(edges_open(page, whole=True), "on a phone (390 x 844) the dialog opens")
    got = json.loads(page.ev("""JSON.stringify((function () { var d = document.querySelector('.dlg.edges'), b = d.querySelector('.edgaps'), out = [];
        if (!b || b.hidden) return {box: false};
        b.scrollIntoView({block: 'center'});
        [].forEach.call(b.querySelectorAll('li, button'), function (n) { var r = n.getBoundingClientRect(); if (r.left < -0.5 || r.right > innerWidth + 0.5) out.push(n.textContent); });
        return {box: true, out: out, sideways: document.documentElement.scrollWidth > innerWidth,
                small: [].filter.call(b.querySelectorAll('button'), function (n) { return n.getBoundingClientRect().height < 43.5; }).length}; })())"""))
    check(got.get("box") and not got["out"] and not got["sideways"] and got["small"] == 0, "the box and its Show button are inside the screen, 44 pixels high, no sideways scroll (%s)" % got)
    page.shot(args.shots, "mapmaker-places-phone-box")
    page.key("Escape")
    tab.emulate(1280, 800, 1)
    time.sleep(0.5)
