"""The Ants tool's box in the real browser (tests/scripts/web_mapmaker_browser.py, part `ants`; docs/MAP_EDITOR.md, "Ants: one at a time, group, mix"): kept in its own file so that neither passes the
line limit of the test files. The page helpers of the main file (wait_for, make_extended, the Page class) are handed in by `run_ants`. The bytes are what Download hands over, read here with struct."""
import json
import os
import struct
import time

wait_for = None
TEAM_TILES = {"black": 152, "blue": 153, "green": 154, "red": 155}


def records(data):
    """[(tile, row, column)] of block 1 of a file of version 8 or 9, read with struct"""
    version = struct.unpack_from("<I", data, 0)[0]
    at = 40 if version == 8 else 18 + struct.unpack_from("<H", data, 16)[0]
    names = struct.unpack_from("<H", data, at)[0] + 1
    at += 2 + 11 * names
    rows, columns = struct.unpack_from("<II", data, at)
    if version == 8:
        rows, columns = rows & 0xFFFF, columns & 0xFFFF
    at += 8 + 12 * rows * columns
    n = struct.unpack_from("<H", data, at)[0]
    return [struct.unpack_from("<HHH", data, at + 2 + 6 * i) for i in range(n)]


def counts(data):
    out = {}
    for tile, _, _ in records(data):
        out[tile] = out.get(tile, 0) + 1
    return out


def bytes_now(page, edges_bytes):
    return edges_bytes(page)["bytes"]


def wait_counts(page, edges_bytes, want, timeout=8.0):
    """true once the bytes that Download hands over hold exactly these ants (a dict of tile: count); the page needs a moment after a tap"""
    last = [None]

    def same():
        last[0] = counts(bytes_now(page, edges_bytes))
        return last[0] == want
    return wait_for(same, timeout, 0.2)


def button(page, key):
    return page.ev("(function () { var b = document.querySelector('.am [data-k=\"%s\"]'); if (!b) return false; b.click(); return true; })()" % key)


def panel_text(page):
    return page.ev("document.querySelector('.am').textContent")


def tap_cell(page, row, column):
    x, y = page.cell_point(row, column)
    page.tab.click(x, y)


def touch_drag(tab, x0, y0, x1, y1, steps=8):
    def touch(kind, x, y):
        tab.call("Input.dispatchTouchEvent", {"type": kind, "touchPoints": [] if kind == "touchEnd" else [{"x": x, "y": y, "id": 1, "radiusX": 8, "radiusY": 8, "force": .5}]})
    touch("touchStart", x0, y0)
    time.sleep(0.05)
    for i in range(1, steps + 1):
        touch("touchMove", x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)
        time.sleep(0.03)
    touch("touchEnd", x1, y1)


def run_ants(page, tab, args, check, env, edges_bytes, edges_js):
    global wait_for
    wait_for = env["wait_for"]
    make_extended, REPO = env["make_extended"], env["REPO"]
    print("[web map maker] the Ants tool: one at a time, group and mix, 1280 x 800")
    check(page.open(edges_js) and page.ready(), "the page starts")
    page.ev(edges_js)
    check(make_extended(page, 100, 100), "a 100 x 100 Extended map (the Mosh Pit) is made")
    check(not page.ev("!!document.querySelector('.am')"), "the Ants box is not loaded until the tool is chosen")
    page.tool("ants")
    check(wait_for(lambda: page.ev("!!document.querySelector('.am') && !document.querySelector('.am').hidden"), 10), "choosing the Ants tool loads the box")
    check(page.ev("!!document.querySelector('link[href=\"front/mapmaker_ants.css\"]')"), "with its style")
    check(page.ev("document.querySelector('.side').contains(document.querySelector('.am'))"), "on a desktop it is in the right-hand column")
    text = panel_text(page)
    check("Starting ants" in text and "One at a time" in text and "Black0" in text.replace(" ", "") and "have no ants" in text, "One at a time is on, four teams show their counts (0), and the Check's warning is there: %s" % text[:200])
    # One at a time: a tap puts one ant of the picked team
    button(page, "t3")                                                  # Black
    tap_cell(page, 20, 20)
    check(wait_counts(page, edges_bytes, {TEAM_TILES["black"]: 1}), "a tap puts exactly one Black ant")
    check("more than 90 rows" in panel_text(page), "a 100-row map says in the box that the game cannot play more than 90 rows yet")
    check("Black1" in panel_text(page).replace(" ", ""), "the count of Black is 1 at once")
    button(page, "t2")
    tap_cell(page, 20, 22)
    check(wait_counts(page, edges_bytes, {152: 1, 153: 1}), "a Blue ant on another cell")
    tap_cell(page, 20, 22)
    check(wait_counts(page, edges_bytes, {152: 1}), "a tap on an ant again takes it away, whatever team is picked")
    page.ev("document.getElementById('undo-btn').click()")
    check(wait_counts(page, edges_bytes, {152: 1, 153: 1}), "one Undo brings it back")
    button(page, "take")
    tap_cell(page, 20, 20)
    check(wait_counts(page, edges_bytes, {153: 1}), "Take one away takes the Black ant")
    tap_cell(page, 30, 30)
    check(wait_counts(page, edges_bytes, {153: 1}), "Take one away on an empty cell changes nothing")
    button(page, "put")
    page.ev("document.querySelector('[data-act=zoom-in]').click(); document.querySelector('[data-act=zoom-in]').click()")
    x, y = page.box("#map")["x"] + 300, page.box("#map")["y"] + 300
    before = page.state()["center"]
    page.drag(x, y, x + 60, y + 40)
    check(wait_counts(page, edges_bytes, {153: 1}) and page.state()["center"] != before, "a drag looks around and puts nothing")
    page.shot(args.shots, "mapmaker-ants-one-desktop")
    # Mix: the Mosh Pit
    button(page, "group"); button(page, "mix")
    box = page.box("#map")
    x0, y0 = box["x"] + box["w"] / 2 - 120, box["y"] + box["h"] / 2 - 110
    page.drag(x0, y0, x0 + 250, y0 + 230)
    check(wait_for(lambda: page.ev("document.querySelector('.am [data-k=put-mix]') && !document.querySelector('.am [data-k=put-mix]').disabled"), 5), "a dragged box gives a preview and turns on Put 100 ants: %s" % panel_text(page)[-260:])
    check(wait_counts(page, edges_bytes, {153: 1}), "nothing is put before the button")
    page.shot(args.shots, "mapmaker-ants-mix-desktop")
    button(page, "put-mix")
    check(wait_counts(page, edges_bytes, {152: 25, 153: 26, 154: 25, 155: 25}), "Put makes 25 ants of each team")
    page.ev("document.getElementById('undo-btn').click()")
    check(wait_counts(page, edges_bytes, {153: 1}), "one Undo takes all 100 away")
    page.ev("document.getElementById('redo-btn').click()")
    check(wait_counts(page, edges_bytes, {152: 25, 153: 26, 154: 25, 155: 25}), "Redo puts them back")
    # a box that is too small
    page.drag(x0, y0 + 300, x0 + 40, y0 + 330)
    check(page.ev("document.querySelector('.am [data-k=put-mix]').disabled") and "too small" in panel_text(page), "a box that is too small is refused politely and the button is off")
    button(page, "cancel")
    # Clear Red
    button(page, "clump"); button(page, "one"); button(page, "t1")
    button(page, "clear")
    check(wait_for(lambda: page.ev("!!document.querySelector('.dlg')")) and "Clear the Red ants?" in page.ev("document.querySelector('.dlg h2').textContent"), "Clear Red ants asks first")
    page.shot(args.shots, "mapmaker-ants-clear-desktop")
    page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Keep them'; })[0].click()")
    check(wait_counts(page, edges_bytes, {152: 25, 153: 26, 154: 25, 155: 25}), "Keep them changes nothing")
    button(page, "clear")
    wait_for(lambda: page.ev("!!document.querySelector('.dlg')"))
    page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Take away 25 Red ants'; })[0].click()")
    check(wait_counts(page, edges_bytes, {152: 25, 153: 26, 154: 25}), "the other button takes the 25 Red ants away, one step")
    page.ev("document.getElementById('undo-btn').click()")
    check(wait_counts(page, edges_bytes, {152: 25, 153: 26, 154: 25, 155: 25}), "one Undo brings them back")
    # the Mosh Pit with its four hills: the check's verdict on a 100 x 100 Extended map with 4 x 25 mixed ants in the middle
    page.ev("document.getElementById('map').focus()")
    page.key("0")
    page.tool("hills")
    for team, (r, c) in ((0, (10, 10)), (1, (10, 85)), (2, (85, 10)), (3, (85, 85))):
        page.ev("document.querySelector('[data-tile=\"%d\"]').click()" % team)
        tap_cell(page, r, c)
    page.ev("document.querySelector('[data-act=check]').click()")
    wait_for(lambda: page.ev("!!document.querySelector('.dlg')"))
    verdict = page.ev("document.querySelector('.dlg').textContent")
    print("  (the check's verdict on the Mosh Pit, 100 x 100, four hills and 4 x 25 mixed ants in the middle: %s)" % verdict[:300])
    check("Every team has a hill and starting ants" in verdict, "the check says that every team has a hill and starting ants")
    check("Works in the new version" in verdict or "rows" in verdict.lower(), "the verdict is sensible: it works in the new version, or says plainly about the rows")
    page.key("Escape")
    # a Classic map from the library: one ant more and "Works in the original game" stays
    page.choose_file(os.path.join(REPO, "Original-Ants", "Maps", "TINY.LVL"))
    check(wait_for(lambda: page.state()["name"] == "TINY"), "TINY is opened")
    page.tool("ants")
    n0 = len(records(bytes_now(page, edges_bytes)))
    check(wait_for(lambda: page.state()["check"] == "Works in the original game"), "TINY works in the original game")
    button(page, "t1")
    page.ev("document.getElementById('map').focus()")
    page.key("0")
    tap_cell(page, 2, 2)
    check(wait_for(lambda: len(records(bytes_now(page, edges_bytes))) in (n0 + 1, n0), 3), "a tap on TINY is answered")
    check(wait_for(lambda: page.state()["check"] == "Works in the original game", 10), "the map still works in the original game (%s)" % page.state()["check"])


def run_ants_phone(page, tab, args, check, env, edges_bytes, edges_js):
    make_extended = env["make_extended"]
    print("[web map maker] the Ants tool, a phone held upright (390 x 844)")
    check(page.open(edges_js) and page.ready(), "the page starts")
    page.ev(edges_js)
    page.press_act("menu")
    wait_for(lambda: page.ev("!!document.querySelector('.dlg')"))
    page.ev("[].filter.call(document.querySelectorAll('.dlg button'), function (b) { return b.textContent === 'New'; })[0].click()")
    wait_for(lambda: page.ev("!!document.querySelector('.kinds')"))
    page.ev("document.querySelectorAll('.kind input')[1].click()")
    page.type_into("#size-rows", "60"); page.type_into("#size-columns", "60")
    page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
    check(wait_for(lambda: page.state()["rows"] == "60", 15), "a 60 x 60 Extended map")
    page.tool("ants")
    check(wait_for(lambda: page.ev("!!document.querySelector('.am') && !document.querySelector('.am').hidden"), 10), "the box is there")
    check(page.ev("document.querySelector('.tools').contains(document.querySelector('.am'))"), "on a phone it is under the tool buttons")
    small = page.ev("[].filter.call(document.querySelectorAll('.am button, .am input'), function (b) { var r = b.getBoundingClientRect(); return r.width > 0 && (r.height < 43.5 || r.width < 43.5); }).map(function (b) { return b.getAttribute('data-k') + ':' + Math.round(b.getBoundingClientRect().height); })")
    check(not small, "every button and field of the box is at least 44 pixels (%s)" % small)
    button(page, "group"); button(page, "mix")
    small = page.ev("[].filter.call(document.querySelectorAll('.am button, .am input'), function (b) { var r = b.getBoundingClientRect(); return r.width > 0 && (r.height < 43.5 || r.width < 43.5); }).map(function (b) { return b.getAttribute('data-k') + ':' + Math.round(b.getBoundingClientRect().height); })")
    check(not small, "and so in Mix (%s)" % small)
    check(page.ev("document.documentElement.scrollWidth <= window.innerWidth"), "the page does not scroll sideways")
    box = page.box("#map")
    cx, cy = box["x"] + box["w"] / 2, box["y"] + box["h"] / 2
    touch_drag(tab, cx - 50, cy - 45, cx + 50, cy + 45)          # (a 60 x 60 map, whole: a box of about 30 x 27 cells)
    page.ev("(function () { var n = document.querySelector('.am .am-n'); n.value = '3'; n.dispatchEvent(new Event('change')); })()")
    check(wait_for(lambda: page.ev("document.querySelector('.am [data-k=put-mix]') && !document.querySelector('.am [data-k=put-mix]').disabled"), 5), "a box and 3 for each team gives Put 12 ants: %s" % panel_text(page)[-200:])
    page.ev("document.querySelector('.am [data-k=put-mix]').click()")
    check(wait_counts(page, edges_bytes, {152: 3, 153: 3, 154: 3, 155: 3}), "12 ants, 3 of each team")
    visible = page.ev("(function () { var b = document.querySelector('.am [data-k=put-mix]'); var r = b.getBoundingClientRect(); return r.top >= 0 && r.bottom <= window.innerHeight && r.height >= 43.5; })()")
    check(visible, "Put is in view on a phone without scrolling, and 44 pixels high")
    page.shot(args.shots, "mapmaker-ants-phone")
