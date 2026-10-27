"""The small map of the map maker's page in the real browser (tests/scripts/web_mapmaker_browser.py, part `minimap`; docs/MAP_EDITOR.md, "The small map"): kept in its own file so that the main file
stays under the limit of lines. On a 100 x 100 map zoomed in, so that the small map shows: its hills are drawn with a white or black edge, at least 10 pixels wide, in the team's color (the box's canvas
is read pixel by pixel); Hide takes the box away and leaves a Show map button in its place, where the map under it can be reached, and what is chosen is kept for the next visit; the grip drags the box
(mouse, and a finger on a phone) and keeps it inside the map, a drag starts no stroke and does not look around, the arrow keys move it 16 pixels, Home and two quick taps bring it back to the corner;
Lock stops all of that and is kept; a window that is smaller brings the box inside again, a locked box too; a stored place that does not fit is dropped; on a phone the box is 108 pixels wide and its
small buttons are 44 pixels high to a finger. The page helpers of the main file (wait_for, the Page class) are handed in by `run_minimap`."""
import json
import time

import web_mapmaker_browser_pickup as pickup

wait_for = None
KEY = "ants_mapmaker_minimap"
HILL_RED = (255, 45, 134)


def box(page, selector):
    return page.box(selector)


def shown(page, selector):
    return bool(page.ev("(function () { var n = document.querySelector(%s); return !!n && n.offsetParent !== null && !n.hidden; })()" % json.dumps(selector)))


def inside_map(page, selector, slack=1.5):
    """the element lies inside the map's box (#cv) on all four sides"""
    return bool(page.ev("(function () { var n = document.querySelector(%s).getBoundingClientRect(), c = document.getElementById('cv').getBoundingClientRect(); return n.left >= c.left - %s && n.top >= c.top - %s && n.right <= c.right + %s && n.bottom <= c.bottom + %s; })()" % (json.dumps(selector), slack, slack, slack, slack)))


def red_pixels(page):
    """how many pixels of the box's canvas are the red hill's own color, and whether a black edge pixel lies next to one"""
    return json.loads(page.ev("""JSON.stringify((function (want) {
        var c = document.getElementById('mini'), d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data, n = 0, edge = 0, i, W = c.width;
        function at(x, y) { var j = 4 * (y * W + x); return [d[j], d[j + 1], d[j + 2]]; }
        for (i = 0; i < d.length; i += 4) if (d[i] === want[0] && d[i + 1] === want[1] && d[i + 2] === want[2]) {
            n++;
            var p = i / 4, x = p % W, y = (p / W) | 0, l = x > 0 ? at(x - 1, y) : [9, 9, 9];
            if (l[0] === 0 && l[1] === 0 && l[2] === 0) edge++;
        }
        return {fill: n, edge: edge, width: c.width};
    })(WANT))""".replace("WANT", json.dumps(list(HILL_RED)))))


def touch(tab, kind, x, y):
    tab.call("Input.dispatchTouchEvent", {"type": kind, "touchPoints": [] if kind == "touchEnd" else [{"x": x, "y": y, "id": 1, "radiusX": 6, "radiusY": 6, "force": .5}]})


def touch_drag(tab, x0, y0, x1, y1, steps=8):
    touch(tab, "touchStart", x0, y0)
    for i in range(1, steps + 1):
        touch(tab, "touchMove", x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)
        time.sleep(0.02)
    touch(tab, "touchEnd", x1, y1)
    time.sleep(0.3)


def zoom_in(page, times=2):
    for _ in range(times):
        page.ev("document.querySelector('[data-act=zoom-in]').click()")
        time.sleep(0.2)
    time.sleep(0.3)


def fresh(page, tab, args, check, edges_js, edges_bytes, name, w, h, dpr, mobile):
    tab.emulate(w, h, dpr, mobile=mobile)
    pickup.setup(page, args, check, edges_js, edges_bytes, name)
    page.ev("try { localStorage.removeItem('%s'); } catch (e) {}" % KEY)          # (what an earlier visit kept: the page is loaded again without it)
    reopen(page, tab, args, check, edges_js, edges_bytes, name)


def reopen(page, tab, args, check, edges_js, edges_bytes, name):
    """a new visit: the page again (what the browser kept stays), the map again, zoomed in"""
    pickup.setup(page, args, check, edges_js, edges_bytes, name)
    page.ev("(function () { var b = [].filter.call(document.querySelectorAll('#dialogs button'), function (n) { return n.textContent === 'Throw it away'; })[0]; if (b) b.click(); })()")
    zoom_in(page)


def stroke_free(page):
    """no stroke was made and the view did not move: Undo is off and the view is the one it was"""
    return page.ev("document.getElementById('undo-btn').disabled")


def run_minimap(page, tab, args, check, env, edges_bytes, edges_js):
    global wait_for
    wait_for = pickup.wait_for = env["wait_for"]
    print("[web map maker] the small map, 1280 x 800")
    fresh(page, tab, args, check, edges_js, edges_bytes, "desktop", 1280, 800, 1, False)
    check(shown(page, "#minimap") and not shown(page, "#mini-show"), "zoomed in: the small map shows and the Show map button does not")
    mm = box(page, "#minimap")
    check(abs(mm["w"] - 168) < 1 and inside_map(page, "#minimap"), "a desktop's small map is 168 pixels wide and inside the map (%s)" % mm)
    px = red_pixels(page)
    check(px["fill"] >= 64 and px["edge"] >= 8, "the red hill is drawn in its color, at least 8 x 8 inside an edge (%s)" % px)
    page.shot(args.shots, "mapmaker-minimap-open-1280x800")
    check(page.ev("document.getElementById('mini-lock').getAttribute('aria-pressed')") == "false", "it starts unlocked")
    # Hide and Show: the button is where the box was, the map under the box is reachable, and the choice is kept
    corner = (mm["x"] + mm["w"] - 5, mm["y"] + 5)
    page.click_selector("#mini-hide")
    time.sleep(0.2)
    check(not shown(page, "#minimap") and shown(page, "#mini-show"), "Hide: the small map is gone and the Show map button is there")
    ms = box(page, "#mini-show")
    check(abs((ms["x"] + ms["w"]) - (mm["x"] + mm["w"])) < 2 and abs(ms["y"] - mm["y"]) < 2 and ms["h"] >= 43.5, "the Show map button is in the corner of the box, 44 pixels high (%s)" % ms)
    check(page.ev("document.activeElement && document.activeElement.id") == "mini-show", "the focus went to the Show map button")
    check(page.ev("document.elementFromPoint(%s, %s).id" % (mm["x"] + 5, mm["y"] + mm["h"] - 5)) == "map", "the map under where the box was can be reached")
    page.tab.click(ms["x"] + ms["w"] / 2, ms["y"] + ms["h"] / 2)
    time.sleep(0.2)
    check(shown(page, "#minimap") and not shown(page, "#mini-show") and page.ev("document.activeElement && document.activeElement.id") == "mini-hide", "Show map: the small map is back, the focus on Hide")
    page.click_selector("#mini-hide")
    reopen(page, tab, args, check, edges_js, edges_bytes, "desktop, a new visit")
    check(not shown(page, "#minimap") and shown(page, "#mini-show"), "a new visit finds the small map hidden, with its button")
    page.click_selector("#mini-show")
    time.sleep(0.2)
    # the grip drags the box; no stroke, no looking around; kept inside the map; kept for the next visit
    page.tool("ground")
    view = page.ev("document.getElementById('map').getAttribute('data-view')")
    g = box(page, "#mini-grip")
    mm = box(page, "#minimap")
    cv = box(page, "#cv")
    page.drag(g["x"] + 8, g["y"] + g["h"] / 2, g["x"] + 8 - 300, g["y"] + g["h"] / 2 + 250, steps=12)
    time.sleep(0.2)
    now = box(page, "#minimap")
    check(abs((now["x"] - mm["x"]) + 300) < 2 and abs((now["y"] - mm["y"]) - 250) < 2 and inside_map(page, "#minimap"), "a drag of the grip moves the box by what the pointer moved, inside the map (%s to %s)" % (mm, now))
    check(stroke_free(page) and page.ev("document.getElementById('map').getAttribute('data-view')") == view, "the drag painted nothing and did not look around")
    page.shot(args.shots, "mapmaker-minimap-moved-1280x800")
    g = box(page, "#mini-grip")
    page.drag(g["x"] + 8, g["y"] + g["h"] / 2, -500, 5000, steps=10)
    now = box(page, "#minimap")
    check(inside_map(page, "#minimap") and abs(now["x"] - cv["x"]) < 2 and abs(now["y"] + now["h"] - (cv["y"] + cv["h"])) < 2, "a drag far past the corner stops at the map's edge (%s)" % now)
    kept = json.loads(page.ev("localStorage.getItem('%s')" % KEY))
    check(len(kept) == 4 and kept[2] == 0 and kept[3] == 0 and kept[0] is not None, "the place is kept in this browser (%s)" % kept)
    reopen(page, tab, args, check, edges_js, edges_bytes, "desktop, a new visit")
    again = box(page, "#minimap")
    check(abs(again["x"] - now["x"]) < 2 and abs(again["y"] - now["y"]) < 2, "a new visit finds it where it was left (%s)" % again)
    # the keyboard
    page.ev("document.getElementById('mini-grip').focus()")
    x0 = box(page, "#minimap")["x"]
    page.key("ArrowRight")
    check(abs(box(page, "#minimap")["x"] - (x0 + 16)) < 1.5 or abs(box(page, "#minimap")["x"] - x0) < 1.5, "an arrow key moves the box 16 pixels (or it is at the edge)")
    page.key("ArrowUp")
    y1 = box(page, "#minimap")["y"]
    page.key("ArrowUp")
    check(abs(box(page, "#minimap")["y"] - (y1 - 16)) < 1.5, "the up arrow moves it 16 pixels up")
    page.key("Home")
    mm = box(page, "#minimap")
    check(abs((mm["x"] + mm["w"]) - (cv["x"] + cv["w"] - 12)) < 2 and abs(mm["y"] - (cv["y"] + 12)) < 2, "Home puts it back in its corner (%s)" % mm)
    check(page.ev("localStorage.getItem('%s')" % KEY) == "[null,null,0,0]", "and the kept place is the corner")
    # two quick taps
    g = box(page, "#mini-grip")
    page.drag(g["x"] + 8, g["y"] + 10, g["x"] - 150, g["y"] + 150, steps=8)
    g = box(page, "#mini-grip")
    page.tab.click(g["x"] + 8, g["y"] + g["h"] / 2)
    page.tab.click(g["x"] + 8, g["y"] + g["h"] / 2)
    time.sleep(0.2)
    mm = box(page, "#minimap")
    check(abs(mm["y"] - (cv["y"] + 12)) < 2, "two quick clicks on the grip bring it back to the corner (%s)" % mm)
    # Lock
    g = box(page, "#mini-grip")
    page.drag(g["x"] + 8, g["y"] + 10, g["x"] - 100, g["y"] + 100, steps=8)
    moved = box(page, "#minimap")
    page.click_selector("#mini-lock")
    check(page.ev("document.getElementById('mini-lock').getAttribute('aria-pressed')") == "true" and page.ev("document.getElementById('mini-lock').getAttribute('aria-label')") == "Unlock minimap position", "Lock: pressed, and named Unlock")
    g = box(page, "#mini-grip")
    page.drag(g["x"] + 8, g["y"] + 10, g["x"] + 50, g["y"] + 90, steps=8)
    page.ev("document.getElementById('mini-grip').focus()")
    page.key("ArrowLeft")
    page.key("Home")
    now = box(page, "#minimap")
    check(abs(now["x"] - moved["x"]) < 1.5 and abs(now["y"] - moved["y"]) < 1.5, "locked: a drag, an arrow key and Home leave the box where it is")
    page.click_selector("#mini-hide")
    check(shown(page, "#mini-show") and not shown(page, "#minimap"), "locked: Hide still works")
    page.click_selector("#mini-show")
    reopen(page, tab, args, check, edges_js, edges_bytes, "desktop, a new visit")
    check(page.ev("document.getElementById('mini-lock').getAttribute('aria-pressed')") == "true", "a new visit finds it locked")
    page.shot(args.shots, "mapmaker-minimap-locked-1280x800")
    # a window that is smaller brings the box inside again, locked or not; one that is bigger does not move it
    tab.emulate(1000, 680, 1)
    time.sleep(0.6)
    squeezed = box(page, "#minimap")
    check(shown(page, "#minimap") and inside_map(page, "#minimap") and (abs(squeezed["x"] - moved["x"]) > 1.5 or abs(squeezed["y"] - moved["y"]) > 1.5), "a smaller window: the locked box is brought inside the map again (%s, it was %s)" % (squeezed, moved))
    tab.emulate(1280, 800, 1)
    time.sleep(0.5)
    # a place that does not fit is dropped
    page.ev("localStorage.setItem('%s', '[5000,5000,0,0]')" % KEY)
    reopen(page, tab, args, check, edges_js, edges_bytes, "desktop, a place that does not fit")
    cv = box(page, "#cv")
    mm = box(page, "#minimap")
    check(abs((mm["x"] + mm["w"]) - (cv["x"] + cv["w"] - 12)) < 2 and abs(mm["y"] - (cv["y"] + 12)) < 2, "a stored place outside the map is dropped: the corner (%s)" % mm)
    page.ev("localStorage.setItem('%s', 'rubbish')" % KEY)
    reopen(page, tab, args, check, edges_js, edges_bytes, "desktop, rubbish kept")
    check(shown(page, "#minimap"), "rubbish in the browser's storage is ignored")

    # phones: upright and sideways, a finger
    for name, w, h in (("phone upright 390 x 844", 390, 844), ("phone sideways 844 x 390", 844, 390)):
        print("[web map maker] the small map, " + name)
        fresh(page, tab, args, check, edges_js, edges_bytes, name, w, h, 2, True)
        mm = box(page, "#minimap")
        check(shown(page, "#minimap") and mm["w"] <= 108.5 and inside_map(page, "#minimap"), "%s: the small map is at most 108 pixels wide and inside the map (%s)" % (name, mm))
        for sel in ("#mini-hide", "#mini-lock"):
            b = box(page, sel)
            cx, cy = b["x"] + b["w"] / 2, b["y"] + b["h"] / 2
            check(all(page.ev("document.elementFromPoint(%s, %s) === document.querySelector(%s)" % (cx + dx, cy + dy, json.dumps(sel))) for dx, dy in ((0, -20), (0, 20))), "%s: %s can be hit within 20 pixels above and below its middle (44 pixels high)" % (name, sel))
        px = red_pixels(page)
        check(px["fill"] >= 64 * 4 and px["edge"] >= 8, "%s: the red hill is at least 10 CSS pixels wide (%s)" % (name, px))
        g = box(page, "#mini-grip")
        cv = box(page, "#cv")
        view = page.ev("document.getElementById('map').getAttribute('data-view')")
        touch_drag(tab, g["x"] + g["w"] / 2, g["y"] + g["h"] / 2, cv["x"] + 40, cv["y"] + cv["h"] - 20)
        now = box(page, "#minimap")
        check(inside_map(page, "#minimap") and now["x"] < mm["x"] - 20 and now["y"] > mm["y"] + 20 and stroke_free(page) and page.ev("document.getElementById('map').getAttribute('data-view')") == view, "%s: a finger on the grip drags the box inside the map, nothing is painted and the view stays (%s)" % (name, now))
        page.shot(args.shots, "mapmaker-minimap-moved-%dx%d" % (w, h))
        page.click_selector("#mini-hide")
        ms = box(page, "#mini-show")
        check(shown(page, "#mini-show") and not shown(page, "#minimap") and ms["h"] >= 43.5 and inside_map(page, "#mini-show"), "%s: Hide leaves a 44 pixel Show map button inside the map (%s)" % (name, ms))
        page.shot(args.shots, "mapmaker-minimap-hidden-%dx%d" % (w, h))
        page.click_selector("#mini-show")
        time.sleep(0.2)
        check(shown(page, "#minimap"), "%s: Show map brings it back" % name)
        page.shot(args.shots, "mapmaker-minimap-%dx%d" % (w, h))
