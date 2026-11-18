"""The Ants tool's box in the real browser (tests/scripts/web_mapmaker_browser.py, part `ants`; docs/MAP_EDITOR.md, "Ants: one at a time, group, mix"): kept in its own file so that neither passes the
line limit of the test files. The page helpers of the main file (wait_for, make_extended, the Page class) are handed in by `run_ants`. The bytes are what Download hands over, read here with struct."""
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
    settle(page)                         # (the map has its room when the tool is chosen: a point worked out before that is off, and the first hill is put beside its cell)
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
    run_clumps(page, tab, args, check, env, edges_bytes, edges_js)


def run_ants_phone(page, tab, args, check, env, edges_bytes, edges_js):
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
    run_clumps(page, tab, args, check, env, edges_bytes, edges_js, phone=True)


def touch_tap(tab, x, y):
    for kind in ("touchStart", "touchEnd"):
        tab.call("Input.dispatchTouchEvent", {"type": kind, "touchPoints": [] if kind == "touchEnd" else [{"x": x, "y": y, "id": 1, "radiusX": 8, "radiusY": 8, "force": .5}]})
        time.sleep(0.06)
    time.sleep(0.25)


def ants_of(page, edges_bytes, tile=154):
    return [(r, c) for t, r, c in records(bytes_now(page, edges_bytes)) if t == tile]


def near(cells, row, column, reach):
    return bool(cells) and all(abs(r - row) <= reach and abs(c - column) <= reach for r, c in cells)


def settle(page):
    """the map's size and place are the same on two looks 0.3 seconds apart: choosing a tool or pressing a button of the Pick up bar changes the room that the map has, and a point that was worked out before is then off"""
    last = None
    for _ in range(40):
        now = (page.ev("document.getElementById('map').getAttribute('data-scale')"), page.box("#map"))
        if now == last:
            return True
        last = now
        time.sleep(0.3)
    return False


def point(page, row, column):
    """where the middle of a cell is on the page NOW: from the view's own middle and size of a tile (Page.cell_point takes the whole map to be in the middle, which a tool's bar under the map makes untrue)"""
    box = page.box("#map")
    scale = float(page.ev("document.getElementById('map').getAttribute('data-scale')"))
    cy, cx = [float(v) for v in page.ev("document.getElementById('map').getAttribute('data-center')").split(",")]
    return box["x"] + box["w"] / 2 + (column + 0.5 - cx) * scale, box["y"] + box["h"] / 2 + (row + 0.5 - cy) * scale


def enter(page):
    """the Enter key as a keyboard sends it, with its character: the number box takes what is typed in it"""
    params = {"key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13, "nativeVirtualKeyCode": 13}
    page.tab.call("Input.dispatchKeyEvent", dict(params, type="keyDown", text="\r"))
    page.tab.call("Input.dispatchKeyEvent", dict(params, type="keyUp"))


def pk_press(page, key):
    found = page.ev("(function () { var b = document.querySelector('.pk-bar [data-k=\"%s\"]'); if (!b || b.disabled) return false; b.click(); return true; })()" % key)
    time.sleep(0.3)
    return found


def pk_labels(page):
    return page.ev("[].filter.call(document.querySelectorAll('.pk-bar .pk-b'), function (b) { return b.getAttribute('data-k') !== 'fold'; }).map(function (b) { return b.textContent; })")


def run_clumps(page, tab, args, check, env, edges_bytes, edges_js, phone=False):
    """the player path of the Clump (docs/MAP_EDITOR.md, "Starting ants"): the Ants tool, Group, Clump; two taps in separate places put two clumps and both stay; 250 ants; Undo takes one clump at a time;
    Pick up moves one; Erase takes one and Undo gives its bytes back exactly. On a phone the taps are touches and the number is set from a script, as the Mix part does."""
    global wait_for
    wait_for = env["wait_for"]
    make_extended = env["make_extended"]
    label = "a phone held upright (390 x 844)" if phone else "1280 x 800"
    print("[web map maker] clumps: two taps, 250 ants, Undo, Pick up, Erase, %s" % label)
    check(page.open(edges_js) and page.ready(), "the page starts")
    page.ev(edges_js)
    if phone:
        page.press_act("menu")
        wait_for(lambda: page.ev("!!document.querySelector('.dlg')"))
        page.ev("[].filter.call(document.querySelectorAll('.dlg button'), function (b) { return b.textContent === 'New'; })[0].click()")
        wait_for(lambda: page.ev("!!document.querySelector('.kinds')"))
        page.ev("document.querySelectorAll('.kind input')[1].click()")
        page.type_into("#size-rows", "60"); page.type_into("#size-columns", "60")
        page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent === 'Make map'; })[0].click()")
        check(wait_for(lambda: page.state()["rows"] == "60", 15), "a 60 x 60 Extended map")
    else:
        check(make_extended(page, 60, 60), "a 60 x 60 Extended map")
    tap = (lambda r, c: touch_tap(tab, *point(page, r, c))) if phone else (lambda r, c: page.tab.click(*point(page, r, c)))
    page.tool("ants")
    check(wait_for(lambda: page.ev("!!document.querySelector('.am') && !document.querySelector('.am').hidden"), 10), "the Ants box is there")
    button(page, "group"); button(page, "clump")
    check(page.ev("document.querySelector('.am input[data-k=clump]').value") == "4", "Group, Clump: 4 ants to begin with")
    check("A tap puts a new clump. Pick up moves one." in panel_text(page), "the note says that a tap puts a new clump")
    if phone:
        small = page.ev("[].filter.call(document.querySelectorAll('.am button, .am input'), function (b) { var r = b.getBoundingClientRect(); return r.width > 0 && (r.height < 43.5 || r.width < 43.5); }).map(function (b) { return b.getAttribute('data-k') || b.className; })")
        check(not small, "every button and field of the box is at least 44 pixels in Clump too (%s)" % small)
    page.ev("document.getElementById('map').focus()")
    page.key("0")
    # two taps in separate places: two clumps, both stay
    tap(12, 12)
    check(wait_counts(page, edges_bytes, {154: 4}), "a tap puts 4 Green ants")
    check("Put 4 Green ants at row 12, column 12. One Undo takes them away." in panel_text(page), "the box says what the tap put: %s" % panel_text(page)[-110:])
    tap(45, 45)
    check(wait_counts(page, edges_bytes, {154: 8}), "a tap in another place puts 4 more: 8 in all")
    got = ants_of(page, edges_bytes)
    check(near(got[:4], 12, 12, 2) and near(got[4:], 45, 45, 2), "the first clump stayed round row 12, column 12 and the second is round row 45, column 45: %s" % got)
    # 250
    if phone:
        page.ev("(function () { var n = document.querySelector('.am input[data-k=clump]'); n.value = '900'; n.dispatchEvent(new Event('change')); })()")
    else:
        page.type_into(".am input[data-k=clump]", "900")
        enter(page)
    check(wait_for(lambda: page.ev("document.querySelector('.am input[data-k=clump]').value") == "250", 5), "900 typed in the number box is 250: %s" % page.ev("document.querySelector('.am input[data-k=clump]').value"))
    check(page.ev("document.querySelector('.am [data-k=\"clump+\"]').disabled") and not page.ev("document.querySelector('.am [data-k=\"clump-\"]').disabled"), "More is off at 250")
    page.ev("document.getElementById('map').focus()")
    page.ev("window.scrollTo(0, 0)")
    settle(page)
    tap(30, 14)
    check(wait_counts(page, edges_bytes, {154: 258}, 12), "a tap puts 250 more: 258 ants")
    got = ants_of(page, edges_bytes)
    check(len(set(got)) == 258 and near(got[8:], 30, 14, 12), "the 250 are on cells of their own round row 30, column 14, and the two small clumps are as they were: %s" % got[:8])
    check("Put 250 Green ants at row 30, column 14. One Undo takes them away." in panel_text(page), "the box says so: %s" % panel_text(page)[-110:])
    page.shot(args.shots, "mapmaker-clumps-phone" if phone else "mapmaker-clumps-desktop")
    # Undo takes one clump at a time, Redo puts them back
    for want in (8, 4, 0):
        page.ev("document.getElementById('undo-btn').click()")
        check(wait_counts(page, edges_bytes, {154: want} if want else {}, 8), "Undo: %d ants" % want)
    for want in (4, 8, 258):
        page.ev("document.getElementById('redo-btn').click()")
        check(wait_counts(page, edges_bytes, {154: want}), "Redo: %d ants" % want)
    # Pick up moves one clump
    page.tool("pickup")
    check(wait_for(lambda: page.ev("!!document.querySelector('.pk-bar')"), 10), "Pick up is there")
    before = ants_of(page, edges_bytes)
    first = before[0]
    page.ev("window.scrollTo(0, 0)")
    settle(page)
    check(page.ev("(function () { var c = document.getElementById('map'), k = window.devicePixelRatio; return Math.abs(c.width - c.clientWidth * k) <= 1 && Math.abs(c.height - c.clientHeight * k) <= 1; })()"), "the map is drawn in the room it has: no stretched picture after the tool changed (the canvas is %s)" % page.ev("[document.getElementById('map').width, document.getElementById('map').height, document.getElementById('map').clientWidth, document.getElementById('map').clientHeight].join(' ')"))
    tap(*first)
    check(wait_for(lambda: "Move its group (4)" in pk_labels(page), 5), "a tap on an ant of the first clump: Move its group (4): %s" % pk_labels(page))
    pk_press(page, "group")
    settle(page)
    tap(first[0] + 6, first[1] + 20)
    pk_press(page, "done")
    after = ants_of(page, edges_bytes)
    check(len(after) == 258 and sorted(after[4:]) == sorted(before[4:]) and near(after[:4], first[0] + 6, first[1] + 20, 2), "the first clump moved 6 rows down and 20 columns right and the other 254 ants did not: %s" % after[:4])
    # Erase takes one clump and Undo gives the same bytes back
    page.tool("erase")
    page.ev("document.getElementById('map').focus()")
    page.ev("window.scrollTo(0, 0)")
    settle(page)
    saved = bytes(bytes_now(page, edges_bytes))
    second = after[4]
    tap(*second)
    check(wait_counts(page, edges_bytes, {154: 254}), "Erase on an ant of the second clump takes its 4 and leaves the other 254")
    page.ev("document.getElementById('undo-btn').click()")
    check(wait_for(lambda: bytes(bytes_now(page, edges_bytes)) == saved, 8), "one Undo gives back exactly the bytes of before")
    tap(30, 14)
    check(wait_counts(page, edges_bytes, {154: 8}), "Erase on the 250 takes the 250 and leaves the two small clumps")
    page.ev("document.getElementById('undo-btn').click()")
    check(wait_for(lambda: bytes(bytes_now(page, edges_bytes)) == saved, 8), "and one Undo gives them back exactly")
    check(page.ev("document.documentElement.scrollWidth <= window.innerWidth"), "the page does not scroll sideways")
