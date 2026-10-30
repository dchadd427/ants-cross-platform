"""The Bridge chip in the real browser (tests/scripts/web_mapmaker_browser.py, part `panel`; docs/MAP_EDITOR.md, row E2): kept in its own file because the main file is at the limit of lines.

At each size of the part (a desktop that is not tall, with classic scrollbars of 15 pixels forced on) the Objects palette has its 31 chips and every one is the game's picture, the Bridge's too (no flat
square); the Bridge chip's name line on an empty map sits inside the tools column; and with two pieces put on dry ground the whole sentence of the hint ("Bridge - 2 pieces are on dry ground. A bridge
needs water under it.", four lines in that narrow column) sits inside the column's visible area (the column scrolls just far enough, the page does not move), with the chosen chip still in view. Two
Undos at the end leave the map as it was, so that the part goes on with a clean history."""

HINT = "Bridge \u00b7 2 pieces are on dry ground. A bridge needs water under it."
FIT = """JSON.stringify((function () {
    var col = document.querySelector('.tools'), name = document.getElementById('palette-name'), chip = document.querySelector('#palette .swatch[data-tile="37"]');
    var c = col.getBoundingClientRect(), n = name.getBoundingClientRect(), k = chip.getBoundingClientRect(), top = c.top + col.clientTop, bottom = top + col.clientHeight;
    return {text: name.textContent, nameIn: n.height > 0 && n.top >= top - 0.5 && n.bottom <= bottom + 0.5, chipIn: k.top >= top - 0.5 && k.bottom <= bottom + 0.5, pageMoved: window.scrollY !== 0 || document.scrollingElement.scrollTop !== 0};
})())"""


def panel_case(page, tab, check, w, h, wait_for):
    import json
    import time
    where = "%d x %d" % (w, h)
    page.tool("objects")
    time.sleep(0.1)
    if (w, h) == (1280, 800):
        check(page.ev("document.querySelectorAll('#palette .swatch canvas').length") == 31 and page.ev("!!document.querySelector('#palette .swatch[data-tile=\"37\"] canvas')"), "the Objects palette has 31 chips and every one is the game's picture, the Bridge's too (no flat square)")
    page.tile(37)
    time.sleep(0.1)
    got = json.loads(page.ev(FIT))
    check(got["text"] == "Bridge" and got["nameIn"] and got["chipIn"] and not got["pageMoved"], "%s, the Bridge chip on an empty map: its name line is inside the tools column (%s)" % (where, got))
    for cell in ((14, 14), (14, 16)):
        tab.click(*page.cell_point(*cell))
        time.sleep(0.15)
    wait_for(lambda: page.ev("document.getElementById('palette-name').textContent") == HINT)
    time.sleep(0.2)
    got = json.loads(page.ev(FIT))
    check(got["text"] == HINT, "%s, two pieces on dry ground: the line says it in the approved words (%r)" % (where, got["text"]))
    check(got["nameIn"] and got["chipIn"] and not got["pageMoved"], "%s, the whole sentence is inside the tools column's visible area, the chosen chip is too, and the page has not moved (%s)" % (where, got))
    for _ in range(2):
        page.ctrl_key("z", "KeyZ", 90)
        time.sleep(0.2)
    check(page.ev("document.getElementById('palette-name').textContent") == "Bridge" and page.ev("document.getElementById('undo-btn').disabled") is True, "%s, two Undos take the pieces away and the line is the chip's name again" % where)
    page.tool("ground")                       # (as the part goes on: the Ants box takes the right-hand column over, so the summary below is read with another tool)
