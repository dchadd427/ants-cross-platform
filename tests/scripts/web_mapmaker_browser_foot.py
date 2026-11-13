"""The foot of the map maker's page in the real browser (tests/scripts/web_mapmaker_browser.py, part `foot`; docs/SITE_NAV.md, the map maker): kept in its own file so that the main file stays under the line limit.
The page has no footer line (the bar at the top has the version, in its Menu). The screen has a frame (classic.css, --screen-frame) that lies over the last 10 pixels of the page, and the editor's panels must end
inside it. For desktop windows of several sizes and zooms (1280 x 720 to 1920 x 1080, at 1 and 1.25 pixels a pixel) and phones (390 x 844, 360 x 640), as the page opens, after a stroke with Ground, and with the Pick up bar
open, there is no footer element, the panels' last edge lies inside the frame on all four sides and inside the window, and the page has no scrollbar that it did not need. The page helpers of the main file (wait_for, the
Page class) are handed in by `run_foot`."""
import time

import web_mapmaker_browser_pickup as pickup

wait_for = None
DESKTOPS = ((1280, 720, 1), (1366, 768, 1), (1536, 864, 1), (1920, 1080, 1), (1280, 800, 1), (1000, 700, 1), (1280, 720, 1.25), (1536, 864, 1.25), (1920, 1080, 1.25), (800, 500, 1))
PHONES = ((390, 844, 2), (360, 640, 3), (320, 568, 2))
MEASURE = """JSON.stringify((function () {
    var ps = [].slice.call(document.querySelectorAll('.edbody > .panel')), s = document.querySelector('.screen.ed'), frame = 0, m = getComputedStyle(s, '::after').boxShadow.match(/(\\d+(?:\\.\\d+)?)px inset/g) || [];
    m.forEach(function (t) { frame = Math.max(frame, parseFloat(t)); });
    var sb = s.getBoundingClientRect(), fb = {left: Infinity, top: Infinity, right: -Infinity, bottom: -Infinity};
    ps.forEach(function (n) { var r = n.getBoundingClientRect(); fb.left = Math.min(fb.left, r.left); fb.top = Math.min(fb.top, r.top); fb.right = Math.max(fb.right, r.right); fb.bottom = Math.max(fb.bottom, r.bottom); });
    return {shown: ps.length > 0 && ps[0].offsetParent !== null, footers: document.querySelectorAll('footer').length, frame: frame, foot: [fb.left, fb.top, fb.right, fb.bottom], screen: [sb.left, sb.top, sb.right, sb.bottom],
            win: [innerWidth, innerHeight], sw: document.documentElement.scrollWidth, sh: document.documentElement.scrollHeight};
})())"""


def measure(page):
    import json
    return json.loads(page.ev(MEASURE))


def judge(page, check, name, state, phone):
    """the page has no footer, the editor's panels end inside the frame (10 pixels in from the screen's sides), inside the screen, and nothing grew a scrollbar that it did not have"""
    if phone:
        page.ev("window.scrollTo(0, document.documentElement.scrollHeight)")
        time.sleep(0.2)
    m = measure(page)
    sl, st, sr, sb = m["screen"]
    fl, ft, fr, fb = m["foot"]
    fr_w = m["frame"]
    label = "%s, %s" % (name, state)
    check(m["shown"] and fr_w >= 10 and m["footers"] == 0, "%s: the editor is there, the page has no footer, and the frame is read from the page (%s pixels)" % (label, fr_w))
    check(fb <= sb - fr_w and fl >= sl + fr_w and fr <= sr - fr_w, "%s: the panels (%.1f to %.1f down, %.1f to %.1f across) lie inside the frame (the screen's foot is at %.1f, the frame %s thick)" % (label, ft, fb, fl, fr, sb, fr_w))
    check(sb <= m["win"][1] + 0.5 or phone, "%s: the screen is inside the window (%.1f of %d)" % (label, sb, m["win"][1]))
    if not phone:
        check(m["sh"] <= m["win"][1] and m["sw"] <= m["win"][0], "%s: the page has no scrollbar (%d x %d in %d x %d)" % (label, m["sw"], m["sh"], m["win"][0], m["win"][1]))
    else:
        check(m["sw"] <= m["win"][0], "%s: no sideways scroll" % label)
        page.ev("window.scrollTo(0, 0)")


def run_foot(page, tab, args, check, env, edges_bytes, edges_js):
    global wait_for
    wait_for = pickup.wait_for = env["wait_for"]
    print("[web map maker] the editor ends inside the screen's frame and the page has no footer")
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
    # a phone held sideways: no footer, and no scrollbar either
    tab.emulate(844, 390, 2, mobile=True)
    pickup.setup(page, args, check, edges_js, edges_bytes, "phone sideways 844 x 390")
    check(page.ev("document.querySelectorAll('footer').length === 0") and page.ev("document.documentElement.scrollHeight <= innerHeight"), "phone sideways 844 x 390: no footer and nothing to scroll")
