"""The footer of the map maker's page in the real browser (tests/scripts/web_mapmaker_browser.py, part `foot`; docs/MAP_EDITOR.md, "The footer"): kept in its own file so that the main file stays under the line limit.
The screen has a frame (classic.css, --screen-frame) that lies over the last 10 pixels of the page, and the line "Map Maker v... - build ... - Lobby - Match history - Changelog - Legal" used to sit on it, its lower half
cut by the red line. For desktop windows of several sizes and zooms (1280 x 720 to 1920 x 1080, at 1 and 1.25 pixels a pixel) and phones (390 x 844, 360 x 640), as the page opens, after a stroke with Ground, and with the Pick up bar
open, every line of the footer (the words are measured with a range, so a wrapped line counts) lies inside the frame on all four sides, the footer is inside the screen, and the page has no scrollbar that it did not need. A phone
held sideways has no footer (the rule is kept). The page helpers of the main file (wait_for, the Page class) are handed in by `run_foot`."""
import time

import web_mapmaker_browser_pickup as pickup

wait_for = None
DESKTOPS = ((1280, 720, 1), (1366, 768, 1), (1536, 864, 1), (1920, 1080, 1), (1280, 800, 1), (1000, 700, 1), (1280, 720, 1.25), (1536, 864, 1.25), (1920, 1080, 1.25), (800, 500, 1))
PHONES = ((390, 844, 2), (360, 640, 3), (320, 568, 2))
MEASURE = """JSON.stringify((function () {
    var f = document.getElementById('edfoot'), s = document.querySelector('.screen.ed'), frame = 0, m = getComputedStyle(s, '::after').boxShadow.match(/(\\d+(?:\\.\\d+)?)px inset/g) || [];
    m.forEach(function (t) { frame = Math.max(frame, parseFloat(t)); });
    var r = document.createRange(); r.selectNodeContents(f);
    var rects = [].filter.call(r.getClientRects(), function (q) { return q.width > 0 && q.height > 0; }), sb = s.getBoundingClientRect(), fb = f.getBoundingClientRect();
    var top = Math.min.apply(null, rects.map(function (q) { return q.top; })), bottom = Math.max.apply(null, rects.map(function (q) { return q.bottom; }));
    var left = Math.min.apply(null, rects.map(function (q) { return q.left; })), right = Math.max.apply(null, rects.map(function (q) { return q.right; }));
    return {shown: f.offsetParent !== null, frame: frame, text: [left, top, right, bottom], foot: [fb.left, fb.top, fb.right, fb.bottom], screen: [sb.left, sb.top, sb.right, sb.bottom],
            win: [innerWidth, innerHeight], sw: document.documentElement.scrollWidth, sh: document.documentElement.scrollHeight, lines: rects.length};
})())"""


def measure(page):
    import json
    return json.loads(page.ev(MEASURE))


def judge(page, check, name, state, phone):
    """the footer's words are inside the frame (10 pixels in from the screen's four sides), the footer is inside the screen, and nothing grew a scrollbar that it did not have"""
    if phone:
        page.ev("window.scrollTo(0, document.documentElement.scrollHeight)")
        time.sleep(0.2)
    m = measure(page)
    left, top, right, bottom = m["text"]
    sl, st, sr, sb = m["screen"]
    fl, ft, fr, fb = m["foot"]
    fr_w = m["frame"]
    label = "%s, %s" % (name, state)
    check(m["shown"] and fr_w >= 10 and m["lines"] >= 1, "%s: the footer is there and the frame is read from the page (%s pixels)" % (label, fr_w))
    check(bottom <= sb - fr_w and left >= sl + fr_w and right <= sr - fr_w, "%s: the words (%.1f to %.1f down, %.1f to %.1f across) lie inside the frame (the screen's foot is at %.1f, the frame %s thick)" % (label, top, bottom, left, right, sb, fr_w))
    check(top >= ft - 0.5 and bottom <= fb + 0.5 and fb <= sb + 0.5 and sb <= m["win"][1] + 0.5, "%s: the words are inside the footer (%.1f to %.1f in %.1f to %.1f) and the footer is inside the window (%.1f of %d)" % (label, top, bottom, ft, fb, fb, m["win"][1]))
    if not phone:
        check(m["sh"] <= m["win"][1] and m["sw"] <= m["win"][0], "%s: the page has no scrollbar (%d x %d in %d x %d)" % (label, m["sw"], m["sh"], m["win"][0], m["win"][1]))
    else:
        check(m["sw"] <= m["win"][0], "%s: no sideways scroll" % label)
        page.ev("window.scrollTo(0, 0)")


def run_foot(page, tab, args, check, env, edges_bytes, edges_js):
    global wait_for
    wait_for = pickup.wait_for = env["wait_for"]
    print("[web map maker] the footer line is inside the screen's frame")
    for width, height, scale in DESKTOPS + PHONES:
        phone = width < 600
        name = "%d x %d at %s" % (width, height, scale)
        tab.emulate(width, height, scale, mobile=phone)
        pickup.setup(page, args, check, edges_js, edges_bytes, name)
        page.ev("(function () { var b = [].filter.call(document.querySelectorAll('#dialogs button'), function (n) { return n.textContent === 'Throw it away'; })[0]; if (b) b.click(); })()")
        time.sleep(0.4)
        judge(page, check, name, "as the page opens", phone)
        h = pickup.Pick(page, tab, phone)
        page.tool("ground")
        x, y = h.point(20, 20)
        x2, y2 = h.point(22, 26)
        h.drag_points(x, y, x2, y2)
        time.sleep(0.3)
        judge(page, check, name, "after a stroke", phone)
        page.tool("pickup")
        check(wait_for(lambda: page.ev("!!document.querySelector('.pk-bar')"), 10), "%s: Pick up is chosen" % name)
        h.focus_on(6, 6, 20, 0.5, 0.25)
        h.tap(6, 6)
        check("Red hill" in h.line(), "%s: a tap picks the hill" % name)
        judge(page, check, name, "with the Pick up bar open", phone)
        page.shot(args.shots, "mapmaker-footer-%dx%d-at-%s" % (width, height, scale))
    # a phone held sideways has no footer (the rule is kept), and no scrollbar either
    tab.emulate(844, 390, 2, mobile=True)
    pickup.setup(page, args, check, edges_js, edges_bytes, "phone sideways 844 x 390")
    check(page.ev("document.getElementById('edfoot').offsetParent === null") and page.ev("document.documentElement.scrollHeight <= innerHeight"), "phone sideways 844 x 390: no footer and nothing to scroll")
