#!/usr/bin/env python3
"""The web page's picture in a REAL browser (widescreen milestone M5; opt-in; see tests/scripts/test_web_aspect.sh and docs/NETWORK_PORT.md).

Needs a running web page (the web image of this tree: `docker build -t ants-beta .` and run it, or `docker-compose.stack.yml`), a Chromium-based browser and Python 3; nothing
else (the DevTools protocol is spoken with the WebSocket client of web_hidden_check.py, standard library only). The check opens the page in a throwaway headless browser (its own
profile, its own port; nothing of yours is touched) and looks at what only a browser can show:

  * the page's own logic that needs no layout (`ANTS_PAGE` in web/shell.html): the address's `aspect` (only 16:9 and 4:3 count, anything else is ignored), the order address >
    remembered choice > portrait phone > default, the box that is the largest whole number of canvas steps for a given area and device ratio and whose CSS size makes the browser's
    canvas exactly that many pixels even after the layout's rounding, and the frame cap (60, 75 and 90 Hz are not touched, 120 / 144 / 165 / 240 Hz are held to 60 a second);
  * the page at desktop sizes (1280 x 720, 1920 x 1080, 1440 x 900, a 21:9 window, a small one, a very narrow one) and on a phone, at device ratios 1, 2 and 3: the canvas the game
    makes has EXACTLY the shape of the picture (16:9, or 4:3 for the classic picture), fills the game's box, the box fits the window (no scrolling to see the whole picture and the bar
    under it) and is the largest that fits, the page has no sideways scroll;
  * the address and the selector: `?aspect=4:3`, `?aspect=16:9`, a bad value, the selector's click (remembered, reload with the parameter), a portrait phone gets the classic picture;
  * the picture follows the window when it is resized, and fullscreen (the browser's, and the page's own where there is no Fullscreen API: an iPhone) enters and leaves with the
    canvas the right shape;
  * the pointer: the game draws its own cursor at the position that it reads from the browser; two screenshots with the pointer at two places show the cursor at those places.

Exit status 0: every check passed; 1: a check failed; 3: the check could not be made (no browser, the page did not come up); 2 is the status of a bad command line.
"""
import argparse
import base64
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from web_hidden_check import DevTools, find_browser, free_port  # noqa: E402  (the DevTools client of the hidden-tab check)

# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
# a PNG reader (8 bit RGB / RGBA, not interlaced: what a screenshot of the browser is)
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------


def read_png(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos = 8
    idat = b""
    width = height = depth = ctype = 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and ctype in (2, 6) and interlace == 0, "unsupported PNG"
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    channels = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(height):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, channels, bytes(out)


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
# the browser
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

GEOMETRY = r"""
JSON.stringify((function () {
  var r = function (e) { var b = e.getBoundingClientRect(); return [b.x, b.y, b.width, b.height]; };
  var stage = document.getElementById('game-stage'), box = document.getElementById('game-container'), cv = document.getElementById('canvas'), bar = document.getElementById('view-bar');
  var de = document.documentElement;
  return { dpr: window.devicePixelRatio, inner: [window.innerWidth, window.innerHeight], aspect: stage.getAttribute('data-aspect'), args: ANTS_ARGS,
           fullscreen: !!(document.fullscreenElement || document.webkitFullscreenElement), pseudo: stage.classList.contains('pseudo-fullscreen'),
           stage: r(stage), box: r(box), canvas: r(cv), bar: r(bar), backing: [cv.width, cv.height], ready: !!window.isReadyToPlay,
           scroll: [de.scrollWidth, de.clientWidth, de.scrollHeight, de.clientHeight], note: document.getElementById('aspect-note').style.display,
           checked: Array.prototype.map.call(document.querySelectorAll('.seg button'), function (b) { return b.getAttribute('data-aspect') + '=' + b.getAttribute('aria-checked'); }) };
})())
"""

# ANTS_PAGE's own logic: a table of what it must answer. Returns the list of failures.
PURE_CHECKS = r"""
(function () {
  var bad = [], n = 0;
  function eq(a, b, what) { n++; if (JSON.stringify(a) !== JSON.stringify(b)) bad.push(what + ': ' + JSON.stringify(a) + ' != ' + JSON.stringify(b)); }
  var P = ANTS_PAGE;
  ['16:9', '4:3'].forEach(function (v) { eq(P.parseAspect(v), v, 'parse ' + v); });
  [null, undefined, '', '21:9', '16:10', ' 16:9', '16:9 ', '16x9', '16/9', '4:3:2', '0:0', 'wide', 'toString', '__proto__', 'constructor', 'hasOwnProperty', 16, {}].forEach(function (v) {
    eq(P.parseAspect(v), null, 'parse ' + String(v) + ' is nothing');
  });
  eq(P.resolveAspect('4:3', '16:9', false), { aspect: '4:3', source: 'address' }, 'the address beats the remembered choice');
  eq(P.resolveAspect('16:9', '4:3', true), { aspect: '16:9', source: 'address' }, 'the address beats a portrait phone');
  eq(P.resolveAspect('21:9', '4:3', false), { aspect: '4:3', source: 'remembered' }, 'a bad address value is ignored: the remembered choice');
  eq(P.resolveAspect(null, '16:9', true), { aspect: '16:9', source: 'remembered' }, 'the remembered choice beats a portrait phone');
  eq(P.resolveAspect(null, 'junk', true), { aspect: '4:3', source: 'portrait' }, 'a portrait phone: classic');
  eq(P.resolveAspect('', null, false), { aspect: '16:9', source: 'default' }, 'the default is 16:9');
  eq(P.resolveAspect(undefined, undefined, false), { aspect: '16:9', source: 'default' }, 'nothing at all: 16:9');
  // the box: exact shape, the largest whole number of steps that fits, and a CSS size that the layout cannot round into a smaller canvas
  var dprs = [1, 1.1, 1.25, 4 / 3, 1.5, 1.75, 2, 2.25, 2.5, 2.625, 3, 3.5, 4, 0.75, 1.100000023841858, 2.0000000298023224];
  var areas = [];
  for (var w = 300; w <= 2600; w += 37) for (var h = 200; h <= 1500; h += 41) areas.push([w + (w % 7) / 7, h + (h % 5) / 5]);
  var shapes = { '16:9': [16, 9], '4:3': [4, 3] };
  var fails = 0, sample = '';
  Object.keys(shapes).forEach(function (a) {
    var sw = shapes[a][0], sh = shapes[a][1];
    dprs.forEach(function (dpr) {
      areas.forEach(function (ar) {
        var f = P.fitBox(ar[0], ar[1], dpr, a);
        n++;
        var ok = f.bw * sh === f.bh * sw && f.bw % sw === 0 && f.k >= 1;
        var small = ar[0] * dpr < sw || ar[1] * dpr < sh;           // an area smaller than one step: the box is one step (it cannot be smaller)
        if (!small) {
          ok = ok && f.bw <= ar[0] * dpr + 1e-9 && f.bh <= ar[1] * dpr + 1e-9;                                // it fits
          ok = ok && ((f.k + 1) * sw > ar[0] * dpr + 1e-9 || (f.k + 1) * sh > ar[1] * dpr + 1e-9);          // and one more step would not
        }
        // what SDL makes of the box (floor of the CSS size x ratio) after the layout has rounded the CSS size to 1/64 (Chrome, Safari) or 1/60 (Firefox) of a pixel: to the nearest
        // or up, always the box's pixels; down, only where the size is exact (device ratios 1 and 2 promise that)
        [64, 60].forEach(function (q) {
          [Math.round, Math.ceil, Math.floor].forEach(function (rounding) {
            if (rounding === Math.floor && !(dpr === 1 || dpr === 2)) return;
            var cw = rounding(f.cssW * q) / q, ch = rounding(f.cssH * q) / q;
            if (Math.floor(cw * dpr) !== f.bw || Math.floor(ch * dpr) !== f.bh) ok = false;
          });
        });
        if (!ok) { fails++; if (!sample) sample = a + ' ' + JSON.stringify(ar) + ' @' + dpr + ' -> ' + JSON.stringify(f); }
      });
    });
  });
  if (fails) bad.push(fails + ' boxes broke a rule, for example ' + sample);
  // dpr 1 and 2 give sizes that are exact (no extra quarter pixel)
  eq(P.fitBox(1280, 720, 1, '16:9'), { k: 80, bw: 1280, bh: 720, cssW: 1280, cssH: 720 }, '1280 x 720 at 1x is exact');
  eq(P.fitBox(1280, 720, 2, '16:9'), { k: 160, bw: 2560, bh: 1440, cssW: 1280, cssH: 720 }, '1280 x 720 at 2x is exact');
  eq(P.fitBox(1066.67, 600, 1, '16:9').bw, 1056, '1066.67 x 600 at 1x: 1056 x 594');
  eq(P.fitBox(500, 300, 1, '4:3').bw, 400, 'the classic box in 500 x 300: 400 x 300');
  eq(P.fitBox(-5, 0, 1, '16:9').k, 1, 'no area: one step, never nothing');
  eq(P.fitBox(800, 450, 0, '16:9').bw, 800, 'a ratio of 0 is taken as 1');
  eq(P.fitBox(800, 450, NaN, '16:9').bw, 800, 'a ratio that is not a number is taken as 1');
  eq(P.fitBox(800, 450, 1, 'junk').bw, 800, 'a shape that is nothing is 16:9');
  // the frame cap: frames accepted in 10 s at various displays
  function run(hz, seconds, jitter, skip) {
    var acc = P.makeFrameLimiter(1000 / 60), t = 1000, count = 0, step = 1000 / hz, end = t + seconds * 1000, i = 0, seed = 12345;
    function rnd() { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; }
    while (t < end) {
      if (skip && t > skip[0] && t < skip[1]) { t = skip[1]; continue; }
      var ts = t + (jitter ? (rnd() - 0.5) * jitter : 0);
      if (acc(ts)) count++;
      t += step; i++;
    }
    return { rate: count / seconds, capped: acc.isCapped(), measured: acc.isMeasured() };
  }
  [[30, false], [50, false], [60, false], [75, false], [90, false], [110, true], [120, true], [144, true], [165, true], [240, true], [360, true]].forEach(function (c) {
    var res = run(c[0], 10, 0.8);
    n++;
    var ok = res.capped === c[1] && (c[1] ? (res.rate >= 55 && res.rate <= 62.5) : Math.abs(res.rate - c[0]) < 1.5);
    if (!ok) bad.push('frame cap at ' + c[0] + ' Hz: ' + JSON.stringify(res));
  });
  var gap = run(144, 20, 0.5, [6000, 12000]);                                  // a hidden page for six seconds: no burst after it, the cap goes on
  n++;
  if (!(gap.rate > 30 && gap.rate < 45)) bad.push('frame cap over a hidden gap: ' + JSON.stringify(gap));
  return JSON.stringify({ checks: n, bad: bad });
})()
"""


def screen_of_canvas(g, lx, ly, canvas_w, canvas_h):
    """Where a point of the game's canvas (canvas_w x canvas_h logical pixels) is on the page, from the box's rectangle."""
    bx = g["box"]
    return bx[0] + lx / canvas_w * bx[2], bx[1] + ly / canvas_h * bx[3]


class Browser:
    def __init__(self, browser_path):
        self.profile = tempfile.mkdtemp(prefix="ants_aspect_profile.")
        self.port = free_port()
        self.process = subprocess.Popen(
            [browser_path, "--headless=new", "--remote-debugging-port=%d" % self.port, "--user-data-dir=" + self.profile, "--no-first-run", "--no-default-browser-check",
             "--disable-extensions", "--autoplay-policy=no-user-gesture-required", "about:blank"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.devtools = None
        for _ in range(100):
            try:
                self.devtools = DevTools(self.port)
                break
            except (OSError, ValueError, ConnectionError):
                time.sleep(0.2)
        if self.devtools is None:
            raise ConnectionError("the browser did not start")
        for t in self.devtools.call("Target.getTargets")["targetInfos"]:
            if t["type"] == "page":
                self.devtools.close_tab(t["targetId"])

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
        shutil.rmtree(self.profile, ignore_errors=True)


class Tab:
    def __init__(self, browser):
        self.dt = browser.devtools
        self.target, self.session = self.dt.new_tab("")
        self.dt.call("Emulation.setFocusEmulationEnabled", {"enabled": True}, session=self.session)

    def call(self, method, params=None):
        return self.dt.call(method, params or {}, session=self.session)

    def ev(self, expression):
        return self.dt.evaluate(self.session, expression)

    def emulate(self, width, height, dpr, mobile=False):
        self.call("Emulation.setDeviceMetricsOverride", {"width": width, "height": height, "deviceScaleFactor": dpr, "mobile": mobile, "screenWidth": width, "screenHeight": height})
        self.call("Emulation.setTouchEmulationEnabled", {"enabled": bool(mobile), "maxTouchPoints": 5})

    def open(self, url, wait=True, settle=1.5, timeout=120):
        self.call("Page.navigate", {"url": url})
        if not wait:
            return
        deadline = time.time() + timeout
        while time.time() < deadline:
            time.sleep(0.5)
            try:
                if self.ev("!!window.isReadyToPlay"):
                    time.sleep(settle)
                    return
            except (RuntimeError, TimeoutError):
                pass                                                # the page is not there yet
        raise TimeoutError("the game did not become ready at " + url)

    def geometry(self):
        return json.loads(self.ev(GEOMETRY))

    def shot(self):
        return read_png(base64.b64decode(self.call("Page.captureScreenshot", {"format": "png"})["data"]))

    def save_shot(self, directory, name):
        if not directory:
            return
        os.makedirs(directory, exist_ok=True)
        with open(os.path.join(directory, name + ".png"), "wb") as f:
            f.write(base64.b64decode(self.call("Page.captureScreenshot", {"format": "png"})["data"]))

    def mouse(self, kind, x, y, **extra):
        params = {"type": kind, "x": x, "y": y, "button": "left", "clickCount": 1}
        params.update(extra)
        self.call("Input.dispatchMouseEvent", params)

    def click(self, x, y):
        self.mouse("mouseMoved", x, y, button="none")
        time.sleep(0.15)
        self.mouse("mousePressed", x, y)
        time.sleep(0.1)
        self.mouse("mouseReleased", x, y)

    def tap(self, x, y):
        self.call("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": x, "y": y}]})
        time.sleep(0.08)
        self.call("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})

    def close(self):
        self.dt.close_tab(self.target)


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the image, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save the screenshots in")
    ap.add_argument("--quick", action="store_true", help="fewer sizes (1280 x 720, a phone)")
    ap.add_argument("--four", action="store_true", help="also check web/four.html (the page that plays seats in frames); needs the game server behind /ws (the stack)")
    args = ap.parse_args()

    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    web = args.web if args.web.endswith("/") else args.web + "/"
    browser = None
    try:
        browser = Browser(path)
        tab = Tab(browser)

        print("[web aspect] the page's own logic (ANTS_PAGE) in %s" % os.path.basename(path))
        tab.emulate(1280, 720, 1)
        tab.open(web, settle=0.5)
        res = json.loads(tab.ev(PURE_CHECKS))
        check(not res["bad"], "ANTS_PAGE: %d checks%s" % (res["checks"], "" if not res["bad"] else ": " + "; ".join(res["bad"][:6])))

        def layout_checks(label, g, want_aspect):
            dpr = g["dpr"]
            bw, bh = g["backing"]
            bx = g["box"]
            cv = g["canvas"]
            st = g["stage"]
            shape = (16, 9) if want_aspect == "16:9" else (4, 3)
            asked = g["args"][-2:] == ["--aspect", want_aspect]
            check(g["aspect"] == want_aspect and asked, "%s: the picture is %s and the game is asked for it (%s)" % (label, want_aspect, g["args"]))
            check(bw * shape[1] == bh * shape[0], "%s: the canvas the game made is exactly %s (%d x %d device pixels)" % (label, want_aspect, bw, bh))
            check(abs(cv[2] * dpr - bw) < 1.0 + 0.3 * dpr and abs(cv[3] * dpr - bh) < 1.0 + 0.3 * dpr, "%s: the canvas has the box's pixels (CSS %.2f x %.2f at %sx = %.1f x %.1f; canvas %d x %d)" % (label, cv[2], cv[3], dpr, cv[2] * dpr, cv[3] * dpr, bw, bh))
            check(abs(cv[0] - bx[0]) < 0.5 and abs(cv[1] - bx[1]) < 0.5 and abs(cv[2] - bx[2]) < 0.5 and abs(cv[3] - bx[3]) < 0.5, "%s: the canvas fills the box exactly (no bar inside it)" % label)
            if not g["fullscreen"] and not g["pseudo"]:
                check(bx[0] >= st[0] - 0.5 and bx[0] + bx[2] <= st[0] + st[2] + 0.5 and bx[1] >= st[1] - 0.5 and bx[1] + bx[3] <= st[1] + st[3] + 0.5, "%s: the box is inside its slot" % label)
                # the largest whole number of steps that fits the slot: one more step would not fit
                step_w = shape[0] / dpr
                step_h = shape[1] / dpr
                check(bx[2] + step_w > st[2] + 0.01 or bx[3] + step_h > st[3] + 0.01, "%s: the box is the largest that fits its slot (slot %.1f x %.1f, box %.2f x %.2f)" % (label, st[2], st[3], bx[2], bx[3]))
                check(g["bar"][1] + g["bar"][3] <= g["inner"][1] + 1.0, "%s: the whole picture and the bar under it fit the window without scrolling (bar ends at %.0f of %d)" % (label, g["bar"][1] + g["bar"][3], g["inner"][1]))
            check(bx[0] >= -0.5 and bx[0] + bx[2] <= g["inner"][0] + 0.5, "%s: the box is inside the window's width" % label)
            check(g["scroll"][0] <= g["scroll"][1] + 1, "%s: no sideways scroll (%d of %d)" % (label, g["scroll"][0], g["scroll"][1]))

        print("[web aspect] the page at several sizes (16:9 unless the address or a phone says otherwise)")
        sizes = [("1280x720@1", 1280, 720, 1, False), ("phone portrait 390x844@3", 390, 844, 3, True), ("phone landscape 844x390@3", 844, 390, 3, True)]
        if not args.quick:
            sizes = [("1280x720@1", 1280, 720, 1, False), ("1280x720@2", 1280, 720, 2, False), ("1920x1080@1", 1920, 1080, 1, False), ("1920x1080@2", 1920, 1080, 2, False),
                     ("1440x900@2 (16:10)", 1440, 900, 2, False), ("1536x864@1.25", 1536, 864, 1.25, False), ("1500x844@1.5", 1500, 844, 1.5, False),
                     ("2560x1080@1 (21:9)", 2560, 1080, 1, False), ("1366x768@1", 1366, 768, 1, False), ("800x600@1 (small)", 800, 600, 1, False),
                     ("600x900@1 (narrow, portrait)", 600, 900, 1, False), ("360x640@2 (narrow desktop window)", 360, 640, 2, False), ("700x500@1.5", 700, 500, 1.5, False),
                     ("phone portrait 390x844@3", 390, 844, 3, True), ("phone portrait 390x844@1", 390, 844, 1, True), ("phone portrait 390x844@2", 390, 844, 2, True),
                     ("phone landscape 844x390@3", 844, 390, 3, True), ("phone landscape 844x390@1", 844, 390, 1, True), ("phone landscape 844x390@2", 844, 390, 2, True)]
        for label, w, h, dpr, mobile in sizes:
            tab.emulate(w, h, dpr, mobile)
            tab.open(web)
            g = tab.geometry()
            portrait_phone = w <= 768 and h > w                      # the page's rule: (max-width: 768px) and (orientation: portrait)
            layout_checks(label, g, "4:3" if portrait_phone else "16:9")
            tab.save_shot(args.shots, "page_" + label.replace(" ", "_").replace("(", "").replace(")", "").replace("@", "_dpr"))

        print("[web aspect] touch: a tap lands where it should (a phone, both shapes)")
        for label, w, h, query, shape in (("portrait phone, classic picture", 390, 844, "", (640, 480)), ("landscape phone, 16:9 picture", 844, 390, "", (960, 540))):
            tab.emulate(w, h, 3, True)
            tab.open(web + query, settle=2.5)
            g = tab.geometry()
            before = tab.shot()
            sx, sy = screen_of_canvas(g, (shape[0] - 640) / 2 + 576, (shape[1] - 480) / 2 + 450, shape[0], shape[1])
            tab.tap(sx, sy)
            time.sleep(1.8)
            after = tab.shot()
            diff = sum(1 for i in range(0, len(before[3]), before[2] * 97) if abs(before[3][i] - after[3][i]) > 40)
            check(diff > 60, "%s: a tap on the quick help's START button leaves the quick help (%d sampled pixels changed)" % (label, diff))

        print("[web aspect] the address and the selector")
        tab.emulate(1280, 720, 1)
        for query, want in (("?aspect=4:3", "4:3"), ("?aspect=16:9", "16:9"), ("?aspect=4%3A3", "4:3"), ("?aspect=21:9", "16:9"), ("?aspect=", "16:9"), ("?aspect=16:9&aspect=4:3", "16:9"),
                            ("?aspect=wide", "16:9"), ("?aspect=%00", "16:9"), ("?aspect=4:3%20", "16:9"), ("?x=1&aspect=4:3&y=2", "4:3")):
            tab.open(web + query)
            g = tab.geometry()
            check(g["aspect"] == want and g["args"][-2:] == ["--aspect", want], "%s: %s" % (query, want))
            if want == "4:3":
                bw, bh = g["backing"]
                check(bw * 3 == bh * 4, "%s: the canvas is exactly 4:3 (%d x %d)" % (query, bw, bh))
        # the selector (a fresh profile has nothing remembered)
        tab.ev("try { localStorage.removeItem('ants.aspect'); } catch (e) {} 1")
        tab.open(web)
        g = tab.geometry()
        check(g["checked"] == ["16:9=true", "4:3=false"], "the selector shows 16:9 (%s)" % g["checked"])
        button = json.loads(tab.ev("JSON.stringify((function(){var b=document.getElementById('aspect-4-3').getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())"))
        tab.click(button[0], button[1])
        deadline = time.time() + 120
        while time.time() < deadline:
            time.sleep(0.5)
            try:
                if tab.ev("!!window.isReadyToPlay && location.search.indexOf('aspect=4:3') !== -1"):
                    break
            except (RuntimeError, TimeoutError):
                pass
        time.sleep(1.0)
        g = tab.geometry()
        check(g["aspect"] == "4:3" and g["checked"] == ["16:9=false", "4:3=true"], "the selector's click reloads the page with the classic picture (%s)" % g["checked"])
        check(tab.ev("location.search") == "?aspect=4:3", "the address now says ?aspect=4:3 (%s)" % tab.ev("location.search"))
        check(tab.ev("localStorage.getItem('ants.aspect')") == "4:3", "the browser remembered the choice")
        tab.save_shot(args.shots, "selector_classic")
        tab.open(web)                                                 # no parameter: the remembered choice
        g = tab.geometry()
        check(g["aspect"] == "4:3", "a visit without a parameter uses the remembered choice")
        tab.open(web + "?aspect=16:9")
        g = tab.geometry()
        check(g["aspect"] == "16:9" and tab.ev("localStorage.getItem('ants.aspect')") == "4:3", "the address beats the remembered choice and does not change it")
        tab.ev("localStorage.setItem('ants.aspect', 'junk'); 1")
        tab.open(web)
        check(tab.geometry()["aspect"] == "16:9", "a remembered value that is no shape is ignored")
        tab.ev("localStorage.removeItem('ants.aspect'); 1")

        print("[web aspect] resizing, fullscreen and the pointer (1280 x 720)")
        tab.emulate(1280, 720, 1)
        tab.open(web)
        for w, h, dpr in ((1000, 700, 1), (1600, 900, 1), (900, 500, 2), (1280, 720, 1), (1280, 720, 2), (1280, 720, 1.5), (1280, 720, 1)):      # (the same window at other device ratios: a zoom, a move to another screen)
            tab.emulate(w, h, dpr)
            time.sleep(1.5)
            g = tab.geometry()
            layout_checks("resized to %dx%d@%s" % (w, h, dpr), g, "16:9")
        tab.save_shot(args.shots, "resized_back")
        # the pointer: the game draws its own cursor where the browser says the pointer is
        def cursor_error(tab, at_a, at_b, label):
            g = tab.geometry()
            bx = g["box"]
            tab.mouse("mouseMoved", at_a[0], at_a[1], button="none")
            time.sleep(0.8)
            w1, h1, c1, p1 = tab.shot()
            tab.mouse("mouseMoved", at_b[0], at_b[1], button="none")
            time.sleep(0.8)
            w2, h2, c2, p2 = tab.shot()
            dpr = g["dpr"]
            lo_x, hi_x = int(bx[0] * dpr) + 4, int((bx[0] + bx[2]) * dpr) - 4
            lo_y, hi_y = int(bx[1] * dpr) + 4, int((bx[1] + bx[3]) * dpr) - 4
            blobs = {"a": [10 ** 9, 10 ** 9, -1, -1], "b": [10 ** 9, 10 ** 9, -1, -1]}
            mid_x = (at_a[0] + at_b[0]) / 2 * dpr
            for y in range(max(0, lo_y), min(h1, hi_y)):
                if y > (hi_y - 40 * dpr):
                    continue                                                   # (the frame rate's corner changes by itself)
                row1 = y * w1 * c1
                row2 = y * w2 * c2
                for x in range(max(0, lo_x), min(w1, hi_x)):
                    i1 = row1 + x * c1
                    i2 = row2 + x * c2
                    if abs(p1[i1] - p2[i2]) + abs(p1[i1 + 1] - p2[i2 + 1]) + abs(p1[i1 + 2] - p2[i2 + 2]) > 60:
                        key = "a" if x < mid_x else "b"
                        bl = blobs[key]
                        bl[0] = min(bl[0], x)
                        bl[1] = min(bl[1], y)
                        bl[2] = max(bl[2], x)
                        bl[3] = max(bl[3], y)
            ok = True
            detail = []
            for key, at in (("a", at_a), ("b", at_b)):
                bl = blobs[key]
                if bl[2] < 0:
                    ok = False
                    detail.append("no cursor found near %s" % key)
                    continue
                err_x = bl[0] / dpr - at[0]
                err_y = bl[1] / dpr - at[1]
                detail.append("%s: the cursor's corner is %.1f, %.1f css px from the pointer" % (key, err_x, err_y))
                if abs(err_x) > 6 or abs(err_y) > 6:
                    ok = False
            check(ok, "%s: the game's cursor is where the pointer is (%s)" % (label, "; ".join(detail)))

        g = tab.geometry()
        bx = g["box"]
        cursor_error(tab, (bx[0] + bx[2] * 0.25, bx[1] + bx[3] * 0.25), (bx[0] + bx[2] * 0.75, bx[1] + bx[3] * 0.7), "1280x720")
        # a click on a button: the quick help's START (the first page of the game) is at (576, 450) of the original's 640 x 480 page, which sits at (160, 30) of the 960 x 540 canvas
        tab.open(web, settle=2.5)
        g = tab.geometry()
        before = tab.shot()
        sx, sy = screen_of_canvas(g, 160 + 576, 30 + 450, 960, 540)
        tab.click(sx, sy)
        time.sleep(1.5)
        after = tab.shot()
        diff = sum(1 for i in range(0, len(before[3]), before[2] * 97) if abs(before[3][i] - after[3][i]) > 40)
        check(diff > 100, "a click on the quick help's START button leaves the quick help (%d sampled pixels changed)" % diff)
        tab.save_shot(args.shots, "after_start_click")

        # fullscreen: the browser's, then the page's own
        tab.open(web)
        fs_button = json.loads(tab.ev("JSON.stringify((function(){var b=document.getElementById('fullscreen-btn').getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())"))
        tab.click(fs_button[0], fs_button[1])
        time.sleep(2.0)
        g = tab.geometry()
        if g["fullscreen"]:
            layout_checks("fullscreen", g, "16:9")
            check(abs(g["box"][2] / g["box"][3] - 16 / 9) < 0.01, "fullscreen: the picture is 16:9 (%.1f x %.1f)" % (g["box"][2], g["box"][3]))
            check(g["box"][2] >= g["inner"][0] - 16 / g["dpr"] - 0.5 or g["box"][3] >= g["inner"][1] - 9 / g["dpr"] - 0.5, "fullscreen: the picture is as large as the screen allows (%.1f x %.1f of %d x %d)" % (g["box"][2], g["box"][3], g["inner"][0], g["inner"][1]))
            tab.save_shot(args.shots, "fullscreen")
            tab.ev("document.exitFullscreen(); 1")
            time.sleep(2.0)
            g = tab.geometry()
            check(not g["fullscreen"], "fullscreen is left")
            layout_checks("after fullscreen", g, "16:9")
        else:
            print("  (this browser did not enter fullscreen from a click: that part is skipped)")
        tab.ev("Element.prototype.requestFullscreen = undefined; Element.prototype.webkitRequestFullscreen = undefined; 1")
        tab.click(fs_button[0], fs_button[1])
        time.sleep(1.5)
        g = tab.geometry()
        check(g["pseudo"] and not g["fullscreen"], "without the Fullscreen API the page's own fullscreen opens")
        layout_checks("the page's own fullscreen", g, "16:9")
        check(g["stage"][2] >= g["inner"][0] - 1 and g["stage"][3] >= g["inner"][1] - 1, "the page's own fullscreen covers the window")
        tab.save_shot(args.shots, "pseudo_fullscreen")
        exit_button = json.loads(tab.ev("JSON.stringify((function(){var b=document.getElementById('pseudo-exit').getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())"))
        tab.click(exit_button[0], exit_button[1])
        time.sleep(1.0)
        g = tab.geometry()
        check(not g["pseudo"], "its button leaves it")
        layout_checks("after the page's own fullscreen", g, "16:9")
        # screens of other shapes (the page's own fullscreen takes the window's whole size, as the browser's takes the screen's): the picture is the largest 16:9 that fits, centred, the rest is black
        for w, h, dpr in ((1440, 900, 2), (2560, 1080, 1), (1920, 1080, 1), (1024, 1366, 2), (844, 390, 3)):
            tab.emulate(w, h, dpr)
            time.sleep(1.5)
            fs_now = json.loads(tab.ev("JSON.stringify((function(){var b=document.getElementById('fullscreen-btn').getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())"))
            tab.click(fs_now[0], fs_now[1])
            time.sleep(1.2)
            g = tab.geometry()
            label = "fullscreen on a %dx%d@%s screen" % (w, h, dpr)
            check(g["pseudo"], "%s: the page's own fullscreen is open" % label)
            layout_checks(label, g, "16:9")
            bx = g["box"]
            check(bx[2] >= min(w, h * 16 / 9) - 16 / dpr - 0.6, "%s: the picture is as large as the screen allows (%.1f x %.1f of %d x %d)" % (label, bx[2], bx[3], w, h))
            check(abs((bx[0] + bx[2] / 2) - w / 2) < 1.0 and abs((bx[1] + bx[3] / 2) - h / 2) < 1.0, "%s: the picture is centred" % label)
            tab.save_shot(args.shots, "pseudo_fullscreen_%dx%d_dpr%s" % (w, h, dpr))
            tab.ev("document.getElementById('pseudo-exit').click(); 1")
            time.sleep(0.8)

        if args.four:
            print("[web aspect] web/four.html: the games' frames (the game server must be behind /ws)")
            for query, want, label in (("?map=tiny&players=2&play=here", "16:9", "default"), ("?map=tiny&players=2&play=here&aspect=4:3", "4:3", "?aspect=4:3")):
                tab.emulate(1500, 900, 1)
                tab.call("Page.navigate", {"url": web + "four.html" + query})
                time.sleep(14)
                frames = json.loads(tab.ev(r"""JSON.stringify(Array.prototype.map.call(document.querySelectorAll('iframe'), function (f) {
                    var d = f.contentDocument, c = d && d.getElementById('canvas'), b = d && d.getElementById('game-container');
                    var fr = f.getBoundingClientRect(), br = b && b.getBoundingClientRect();
                    return { frame: [fr.width, fr.height], box: br && [br.width, br.height], backing: c && [c.width, c.height], aspect: d && d.getElementById('game-stage').getAttribute('data-aspect'), src: f.src };
                }))"""))
                check(len(frames) == 2, "%s: two games on the page (%d frames)" % (label, len(frames)))
                shape = (16, 9) if want == "16:9" else (4, 3)
                for f in frames:
                    ok = f["backing"] is not None and f["backing"][0] * shape[1] == f["backing"][1] * shape[0] and f["aspect"] == want and ("aspect=" + want) in f["src"]
                    ratio = f["frame"][0] / f["frame"][1]
                    check(ok and abs(ratio - shape[0] / shape[1]) < 0.01, "%s: a frame of %.0f x %.0f holds a %s picture (canvas %s)" % (label, f["frame"][0], f["frame"][1], want, f["backing"]))
                tab.save_shot(args.shots, "four_" + want.replace(":", "x"))
        tab.close()
    except (RuntimeError, TimeoutError, ConnectionError, OSError, ValueError, KeyError, AssertionError) as error:
        print("  %s: the browser or the page could not be driven (%s: %s)" % ("FAIL" if failures else "SKIP", type(error).__name__, error))
        return 1 if failures else 3
    finally:
        if browser is not None:
            browser.close()
    print("[web aspect] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
