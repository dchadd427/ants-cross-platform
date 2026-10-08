#!/usr/bin/env python3
"""The web page's picture in a REAL browser (widescreen milestone M5; opt-in; see tests/scripts/test_web_aspect.sh and docs/NETWORK_PORT.md).

Needs a running web page (the web image of this tree: `docker build -t ants-beta .` and run it, or `docker-compose.stack.yml`; --web is the site's address: the game page is opened at its own
path /play.html, "/" being the front page, and the Play online checks of --four use "/"), a Chromium-based browser and Python 3; nothing
else (the DevTools protocol is spoken with the WebSocket client of web_hidden_check.py, standard library only). The check opens the page in a throwaway headless browser (its own
profile, its own port; nothing of yours is touched) and looks at what only a browser can show:

  * the page's own logic that needs no layout (`ANTS_PAGE` in web/shell.html): the address's `aspect` (only 16:9 and 4:3 count, anything else is ignored), the order address >
    remembered choice > default (16:9 on every device, a phone held upright included), the box that is the largest whole number of canvas steps for a given area and device ratio and whose CSS size makes the browser's
    canvas exactly that many pixels even after the layout's rounding, where the box must stand so that the game's pointer is exact (`snapOffset`), the frame cap (60, 75 and 90 Hz
    are not touched, a jittery 60 Hz display is not capped, 120 / 144 / 165 / 240 Hz are held to 60 a second, decided by the median of the last 24 gaps: slow frames at the start do
    not decide, and a window that moves to a display of another rate is measured again), the arguments of the address (`joinArguments`: the door of a game server on this site only,
    a room code, a seat, a name, embed) and what is not the game's data (`badDownload`: a web page, a size that is not the package's);
  * the page at desktop sizes (1280 x 720, 1920 x 1080, 1440 x 900, a 21:9 window, a small one, a very narrow one) and on a phone, at device ratios 1, 2 and 3: the canvas the game
    makes has EXACTLY the shape of the picture (16:9, or 4:3 for the classic picture), fills the game's box, the box fits the window (no scrolling to see the whole picture and the bar
    under it) and is the largest that fits, the page has no sideways scroll;
  * the address and the selector: `?aspect=4:3`, `?aspect=16:9`, a bad value, the selector's click (remembered under `ants.aspect.v2`, reload with the parameter), a portrait phone gets the 16:9 picture like every other device (and a remembered 4:3 still wins there);
    a Classic 4:3 that the first versions of the pages remembered under `ants.aspect` is not read any more (every browser starts 16:9 once), on the game page and on web/lobby.html (the pages' own code
    runs on a table of such values without a browser in tests/scripts/web_aspect_key_check.js, which ./run_tests.sh --fast runs);
  * the picture follows the window when it is resized, and fullscreen (the browser's, and the page's own where there is no Fullscreen API: an iPhone) enters and leaves with the
    canvas the right shape;
  * the pointer: the game draws its own cursor at the position that it reads from the browser; two screenshots with the pointer at two places show the cursor at those places; the box
    stands where the pointer is exact (a whole pixel for a whole size, 1/64 short of one for any other), and the edges of the setup screen's START button, found by moving the real
    mouse over it (a hover lights the button), are where the game's arithmetic puts them at six layouts (a fractional position or size used to put them up to a pixel off);
  * the selector asks "Leave the match to change the picture?" in a running match (and not at the quick help or the setup screen), and answered No the match goes on;
  * the wheel and the pinch (milestone M4, the mouse-wheel zoom): over the game's canvas the page cancels the wheel (the page does not scroll) and the ctrl + wheel of a trackpad's
    pinch (the browser's page zoom) and Safari's gesture events (turned into wheel events for the game); over the title, the selector and the guide the browser behaves as
    usual (the page scrolls, nothing is cancelled); the middle button's press and click (mousedown, auxclick) are cancelled over the canvas only (an unprevented press over a page
    that scrolls starts the browser's autoscroll on Windows) and a left press is not; and the game really zooms, one level a notch (the series 2^(k/4) of view_zoom.hpp, read from the game's
    own number through ants_probe): in a running match four notches away from 1 reach 2 and show every world pixel as a 2 x 2 square, eight toward reach 0.5, two toward from 1 reach 0.71, the
    series goes on to the map's limit and stops there (0.5, 0.71 and 1 and 2 are pictures that differ, the frame around the map view does not), the middle button goes back to 1, and a
    wheel over the minimap does not zoom (screenshots in --shots);
  * the download of the game's data (index.data): the retry rule, a download that the browser fails once (injected with the DevTools Fetch domain) is retried and the game starts, a
    download that fails for good shows the message with a Reload button; the loading of the game itself: index.js or index.wasm that fail, a game that never starts (the watchdog), a
    body that is a web page / too small / an error status / cut short / a byte short (each is a failed try, the game starts on the next good one), the package that is handed to the
    file packager once and only for index.data, a promise that fails after the game runs (no card over it), and index.js asked for once with one ?v= (`--load-only`);
    and (`--downloads N`, needs the game server behind /ws) N cold-cache runs of web/lobby.html (the front page, at /) with 2 and with
    4 games on the page, every frame of which must start (the browser's cache refuses one of several equal downloads at the same moment: net::ERR_CACHE_WRITE_FAILURE).

Exit status 0: every check passed; 1: a check failed, or the browser or the page broke down during the check (a page that hangs, a browser that crashes, a script that raises:
a page that came up and then misbehaved is a failure, never a skip); 3: the check could not be made because the environment is not there (no browser, the browser did not start,
nothing answers at the page's address); 2 is the status of a bad command line. `--exit-codes` checks these statuses themselves (a closed port, a page that hangs).
"""
import argparse
import base64
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

READY_TIMEOUT = 120                                                           # seconds to wait for a game to become ready (--ready-timeout)
PAGE_REACHED = [False]                                                        # a page was loaded at least once (after that, a failure to load is a failure of the check)


class NotReachable(Exception):
    """The environment is not there: no browser, the browser did not start, nothing answers at the page's address. The only exception that ends the check as a skip (exit status 3)."""


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
           scroll: [de.scrollWidth, de.clientWidth, de.scrollHeight, de.clientHeight],
           checked: Array.prototype.map.call(document.querySelectorAll('.seg button[data-aspect]'), function (b) { return b.getAttribute('data-aspect') + '=' + b.getAttribute('aria-checked'); }) };
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
  eq(P.resolveAspect('4:3', '16:9'), { aspect: '4:3', source: 'address' }, 'the address beats the remembered choice');
  eq(P.resolveAspect('16:9', '4:3'), { aspect: '16:9', source: 'address' }, 'the address beats the remembered choice (16:9 asked, 4:3 remembered)');
  eq(P.resolveAspect('21:9', '4:3'), { aspect: '4:3', source: 'remembered' }, 'a bad address value is ignored: the remembered choice');
  eq(P.resolveAspect(null, '4:3'), { aspect: '4:3', source: 'remembered' }, 'a remembered 4:3 wins over the default');
  eq(P.resolveAspect(null, 'junk'), { aspect: '16:9', source: 'default' }, 'a remembered value that is no shape is ignored: 16:9');
  eq(P.resolveAspect(null, 'junk', true), { aspect: '16:9', source: 'default' }, 'a portrait phone (a third argument that the page does not know any more) gets 16:9 too');
  eq(P.resolveAspect('', null), { aspect: '16:9', source: 'default' }, 'the default is 16:9');
  eq(P.resolveAspect(undefined, undefined), { aspect: '16:9', source: 'default' }, 'nothing at all: 16:9');
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
            if (rounding === Math.floor && Math.abs(f.cssW * 64 - Math.round(f.cssW * 64)) > 1e-9) return;           // (a size that is not a multiple of 1/64 has its quarter pixel: it may be rounded to the nearest or up)
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
  eq(P.fitBox(1280, 720, 1.25, '16:9'), { k: 100, bw: 1600, bh: 900, cssW: 1280, cssH: 720 }, '1280 x 720 at 1.25x is exact (the pointer scale is 1)');
  eq(P.fitBox(1280, 720, 1.5, '16:9'), { k: 120, bw: 1920, bh: 1080, cssW: 1280, cssH: 720 }, '1280 x 720 at 1.5x is exact');
  eq(P.fitBox(544.5, 306.5, 3, '16:9'), { k: 102, bw: 1632, bh: 918, cssW: 544, cssH: 306 }, '544 x 306 at 3x is exact');
  eq(P.fitBox(1066.7, 700, 2.625, '16:9').cssW > 1066.7 - 6.2 && P.fitBox(1066.7, 700, 2.625, '16:9').cssW % 1 !== 0, true, 'a size that is not a multiple of 1/64 keeps its extra quarter device pixel (2.625x)');
  eq(P.cssFor(1264, 1.25) > 1011.2, true, 'a ratio that makes no exact size (1264 / 1.25 = 1011.2) gets the quarter pixel');
  eq(P.cssFor(1600, 1.100000023841858) > 1600 / 1.100000023841858, true, 'a ratio of the browser that is no short binary fraction (1.1) gets the quarter pixel');
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
  // two users of requestAnimationFrame on one page: the callbacks of one display frame share its timestamp and get the same answer, so the second does not take the game's frames
  (function () {
    var acc = P.makeFrameLimiter(1000 / 60), t = 1000, a = 0, b = 0, disagree = 0, step = 1000 / 144;
    for (var i = 0; i < 144 * 10; i++, t += step) {
      var x = acc(t), y = acc(t);
      if (x !== y) disagree++;
      if (x) a++;
      if (y) b++;
    }
    n++;
    if (disagree || a / 10 < 55 || a / 10 > 62.5 || a !== b) bad.push('two users of one frame: ' + JSON.stringify({ disagree: disagree, first: a / 10, second: b / 10 }));
  })();
  var gap = run(144, 20, 0.5, [6000, 12000]);                                  // a hidden page for six seconds: no burst after it, the cap goes on
  n++;
  if (!(gap.rate > 30 && gap.rate < 45)) bad.push('frame cap over a hidden gap: ' + JSON.stringify(gap));
  // the cap is decided by the MEDIAN of the last 24 gaps: not by the smallest gap, not by the first frames, and again when the display changes
  function trace(gaps, tail) {                                               // gaps: the display's gaps in ms, in order; the frames accepted among the last `tail` of them
    var acc = P.makeFrameLimiter(1000 / 60), t = 1000, accepted = 0;
    acc(t);
    for (var i = 0; i < gaps.length; i++) { t += gaps[i]; var a = acc(t); if (i >= gaps.length - tail && a) accepted++; }
    return { accepted: accepted, tail: tail, capped: acc.isCapped(), measured: acc.isMeasured() };
  }
  function seeded(seed) { return function () { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; }; }
  function series(count, mean, spread, rnd) { var out = []; for (var i = 0; i < count; i++) out.push(Math.max(1, mean + (rnd() - 0.5) * spread)); return out; }
  var r1 = seeded(7);
  var jitter60 = series(600, 1000 / 60, 16, r1);                              // a jittery 60 Hz display: gaps from 8.7 to 24.7 ms, many of them under the 10.3 ms that a 97 Hz display has
  var shortest = jitter60.reduce(function (m, g) { return Math.min(m, g); }, 1e9);
  n++;
  if (!(shortest < 10.3)) bad.push('(setup) the jittery 60 Hz trace has a short gap: ' + shortest);
  var res60 = trace(jitter60, 300);
  eq(res60.capped === false && res60.accepted === 300, true, 'a jittery 60 Hz display is not capped, every frame is drawn: ' + JSON.stringify(res60));
  var alt60 = []; for (var k = 0; k < 300; k++) alt60.push(k % 7 === 3 ? 4 : 18);   // a 60 Hz display whose compositor now and then delivers a frame early
  var resAlt = trace(alt60, 150);
  eq(resAlt.capped === false && resAlt.accepted === 150, true, 'a 60 Hz display with early frames is not capped: ' + JSON.stringify(resAlt));
  var res120 = trace(series(600, 1000 / 120, 0, seeded(3)), 240);
  eq(res120.capped === true && res120.accepted >= 112 && res120.accepted <= 125, true, 'a steady 120 Hz display is capped to about 60 a second: ' + JSON.stringify(res120));
  var res120j = trace(series(1200, 1000 / 120, 3, seeded(5)), 480);
  eq(res120j.capped === true && res120j.accepted >= 232 && res120j.accepted <= 250, true, 'a jittery 120 Hz display (3 ms) is capped to about 60 a second: ' + JSON.stringify(res120j));
  var slowStart = []; for (var q = 0; q < 40; q++) slowStart.push(45); slowStart = slowStart.concat(series(1000, 1000 / 144, 0.6, seeded(11)));
  var resSlow = trace(slowStart, 432);
  eq(resSlow.capped === true && resSlow.accepted >= 160 && resSlow.accepted <= 185, true, 'slow frames at the start (the game loading) do not decide: a 144 Hz display is capped afterwards: ' + JSON.stringify(resSlow));
  var outl = []; for (var j = 0; j < 800; j++) outl.push(j % 8 === 7 ? 25 : 1000 / 144);          // a 144 Hz display with a long frame (a pause of the browser) every eighth frame: the median is 6.9 ms
  var resOutl = trace(outl, 288);
  eq(resOutl.capped === true && resOutl.accepted >= 120 && resOutl.accepted <= 190, true, 'a 144 Hz display with a long frame every eighth frame is capped (a slow frame decides nothing): ' + JSON.stringify(resOutl));
  var change = series(400, 1000 / 144, 0.6, seeded(13)).concat(series(200, 1000 / 60, 1, seeded(17)));
  var resChange = trace(change, 60);
  eq(resChange.capped === false && resChange.accepted === 60, true, 'a window that moves from a 144 Hz display to a 60 Hz one is measured again: every frame is drawn: ' + JSON.stringify(resChange));
  var change2 = series(200, 1000 / 60, 1, seeded(19)).concat(series(700, 1000 / 144, 0.6, seeded(23)));
  var resChange2 = trace(change2, 288);
  eq(resChange2.capped === true && resChange2.accepted >= 112 && resChange2.accepted <= 128, true, 'and from a 60 Hz display to a 144 Hz one it is capped: ' + JSON.stringify(resChange2));
  // where the box stands for an exact pointer: a whole size on a whole pixel, any other size 1/64 short of a whole pixel; never moved by more than half a pixel
  eq(P.snapOffset(68.984375, 594), 0.015625, 'a box of a whole height at 68.984 is moved to 69');
  eq(P.snapOffset(66, 720), 0, 'a whole size on a whole pixel stays');
  eq(P.snapOffset(127.90625, 1280.1875), 0.078125, 'a box of a fractional width at 127.906 stands at 127.984');
  eq(P.snapOffset(66, 720.1875), -0.015625, 'a box of a fractional height on a whole pixel stands 1/64 short of it');
  eq(P.snapOffset(65.984375, 720.1875), 0, 'and there it stays');
  eq(P.snapOffset(100.5, 800), 0.5, 'half a pixel is moved by half a pixel (never more)');
  (function () {
    var wrong = 0, sample = '', total = 0;
    for (var pos = 20; pos < 24; pos += 1 / 64) {
      [594, 720, 540.09375, 600.09375, 306.078125, 1280.1875, 4.5 * 3, 720.5].forEach(function (size) {
        total++;
        var d = P.snapOffset(pos, size), at = pos + d, whole = Math.abs(size - Math.round(size)) < 1 / 128;
        var ok = Math.abs(d) <= 0.5 + 1e-9 && (whole ? Math.abs(at - Math.round(at)) < 1e-9 : Math.abs((at + 1 / 64) - Math.round(at + 1 / 64)) < 1e-9);
        if (!ok) { wrong++; if (!sample) sample = pos + ' / ' + size + ' -> ' + d; }
      });
    }
    n++;
    if (wrong) bad.push(wrong + ' of ' + total + ' positions are not snapped as the rule says, for example ' + sample);
  })();
  // the arguments of the address (?join= &room= &seat= &name= &embed=)
  var J = P.joinArguments;
  function ja(search, secure) { return J(search, !!secure, 'play.example').args; }
  eq(J('', false, 'play.example'), { embed: false, args: [] }, 'no query: nothing is passed on');
  eq(J('?join=/ws&room=abc&seat=2&name=Alice', false, 'play.example:8080'), { embed: false, args: ['--join-url', 'ws://play.example:8080/ws', '--room', 'abc', '--seat', '2', '--name', 'Alice'] }, 'a complete join');
  eq(ja('?join=/ws', true), ['--join-url', 'wss://play.example/ws'], 'https: a secure WebSocket');
  ['/ws', '/ws/', '/ws/room-1', '/ws/a/b.c_d~e'].forEach(function (v) { eq(ja('?join=' + encodeURIComponent(v)), ['--join-url', 'ws://play.example' + v], 'the door ' + v + ' is accepted'); });
  ['/', '', '/w', '/wsx', '//ws', '//evil.example/ws', 'ws://evil.example/ws', 'http://evil.example/', 'evil.example/ws', '/ws//x', '/ws/../x', '/ws/..', '/ws?x=1', '/ws#f', '/ws/a b', '/ws\n', '/ws/%2e%2e/x', '/ws\\x', '/other/ws', '/WS'].forEach(function (v) {
    eq(ja('?join=' + encodeURIComponent(v) + '&room=abc&seat=1&name=Bob'), [], 'the door ' + JSON.stringify(v) + ' is refused, and with it the room, the seat and the name');
  });
  eq(ja('?room=abc&seat=1&name=Bob'), [], 'a room, a seat and a name without a door are not passed on');
  ['abc', 'A_b-9', 'x'.repeat(32)].forEach(function (v) { eq(ja('?join=/ws&room=' + v), ['--join-url', 'ws://play.example/ws', '--room', v], 'the room code ' + v + ' is accepted'); });
  ['', 'x'.repeat(33), 'a b', 'a.b', '../x', 'a/b', 'a%2Fb', 'é', 'a;b', 'a\nb'].forEach(function (v) { eq(ja('?join=/ws&room=' + encodeURIComponent(v)), ['--join-url', 'ws://play.example/ws'], 'the room code ' + JSON.stringify(v) + ' is refused'); });
  ['0', '1', '2', '3'].forEach(function (v) { eq(ja('?join=/ws&seat=' + v), ['--join-url', 'ws://play.example/ws', '--seat', v], 'seat ' + v + ' is accepted'); });
  ['', '4', '9', '-1', '00', '01', '1 ', 'a', '1.5', '10'].forEach(function (v) { eq(ja('?join=/ws&seat=' + encodeURIComponent(v)), ['--join-url', 'ws://play.example/ws'], 'seat ' + JSON.stringify(v) + ' is refused'); });
  eq(ja('?join=/ws&name=Alice'), ['--join-url', 'ws://play.example/ws', '--name', 'Alice'], 'a name');
  eq(ja('?join=/ws&name=%20%20Bob%20%20'), ['--join-url', 'ws://play.example/ws', '--name', 'Bob'], 'a name is trimmed');
  eq(ja('?join=/ws&name=Zo%C3%AB'), ['--join-url', 'ws://play.example/ws', '--name', 'Zo'], 'a name keeps printable ASCII only');
  eq(ja('?join=/ws&name=a%09b%0Ac'), ['--join-url', 'ws://play.example/ws', '--name', 'abc'], 'control characters are dropped from a name');
  eq(ja('?join=/ws&name=' + 'n'.repeat(40)), ['--join-url', 'ws://play.example/ws', '--name', 'n'.repeat(32)], 'a name is cut at 32 characters');
  eq(ja('?join=/ws&name=%C3%AB%C3%AB'), ['--join-url', 'ws://play.example/ws'], 'a name with nothing printable left is left out');
  eq(ja('?join=/ws&name=%20%20'), ['--join-url', 'ws://play.example/ws'], 'a name of blanks is left out');
  eq(J('?embed=1', false, 'h'), { embed: true, args: ['--audio-focus'] }, 'embed=1: a frame of another page (the sound follows its focus)');
  ['0', 'true', '', '2', 'yes'].forEach(function (v) { eq(J('?embed=' + v, false, 'h'), { embed: false, args: [] }, 'embed=' + v + ' is not a frame'); });
  eq(ja('?embed=1&join=/ws&room=r'), ['--audio-focus', '--join-url', 'ws://play.example/ws', '--room', 'r'], 'embed and join together');
  // the room's create block (protocol 15: the first Hello of a code that has no room makes the room out of it) and the platform word: each through its own test, only with a valid door, in the game's order: the door, the
  // room, --room-map, --room-seats, --room-teams, --room-leader-start, --platform, then the seat; the tested lower case text comes back, never the text of the address
  var BASE = ['--join-url', 'ws://play.example/ws', '--room', 'k7m2xq'];
  eq(ja('?join=/ws&room=k7m2xq&roommap=small&roomseats=2&roomteams=0%2B1&roomleaderstart=1&platform=browser-linux&seat=1'), BASE.concat(['--room-map', 'small', '--room-seats', '2', '--room-teams', '0+1', '--room-leader-start', '--platform', 'browser-linux', '--seat', '1']), 'the create block of a room and the platform, in the order of the game');
  eq(ja('?join=/ws&room=k7m2xq&roommap=TINY&roomseats=4'), BASE.concat(['--room-map', 'tiny', '--room-seats', '4']), 'the map comes back in lower case');
  eq(ja('?roommap=small&roomseats=2&roomteams=0%2B1&roomleaderstart=1&platform=linux'), [], 'a create block and a platform without a door are not passed on');
  ['nowhere', '', 'small ', 'TINY.LVL', '../x', '--room-map'].forEach(function (v) { eq(ja('?join=/ws&room=k7m2xq&roommap=' + encodeURIComponent(v)), BASE, 'the map ' + JSON.stringify(v) + ' is no --room-map'); });
  ['2', '3', '4'].forEach(function (v) { eq(ja('?join=/ws&room=k7m2xq&roomseats=' + v), BASE.concat(['--room-seats', v]), 'the seats ' + v + ' are --room-seats'); });
  ['1', '5', '0', '04', '', '3 ', 'x'].forEach(function (v) { eq(ja('?join=/ws&room=k7m2xq&roomseats=' + encodeURIComponent(v)), BASE, 'the seats ' + JSON.stringify(v) + ' are no --room-seats'); });
  eq(ja('?join=/ws&room=k7m2xq&roomteams=1%2B2'), BASE.concat(['--room-teams', '1+2']), 'a pair of teams');
  eq(ja('?join=/ws&room=k7m2xq&roomteams=1+2'), BASE.concat(['--room-teams', '1+2']), 'the same pair where the plus came as a blank');
  ['1%2B1', '4%2B1', 'ffa', '0%2B1%2B2', ''].forEach(function (v) { eq(ja('?join=/ws&room=k7m2xq&roomteams=' + v), BASE, 'the teams ' + v + ' are no --room-teams'); });
  eq(ja('?join=/ws&room=k7m2xq&roomleaderstart=1'), BASE.concat(['--room-leader-start']), 'the flag that a full room waits for its leader is exactly 1');
  ['0', 'true', 'yes', '', '11', '1 '].forEach(function (v) { eq(ja('?join=/ws&room=k7m2xq&roomleaderstart=' + encodeURIComponent(v)), BASE, 'the flag ' + JSON.stringify(v) + ' is no --room-leader-start'); });
  ['windows', 'macos', 'linux', 'android', 'ios', 'other', 'browser-windows', 'browser-macos', 'browser-linux', 'browser-android', 'browser-ios', 'browser-other', 'Linux'].forEach(function (v) { eq(ja('?join=/ws&platform=' + v), ['--join-url', 'ws://play.example/ws', '--platform', v.toLowerCase()], 'the platform ' + v + ' is passed on in lower case'); });
  ['', 'plan9', 'browser-', 'browser-browser-linux', 'linux ', '--platform', 'win32'].forEach(function (v) { eq(ja('?join=/ws&platform=' + encodeURIComponent(v)), ['--join-url', 'ws://play.example/ws'], 'the platform ' + JSON.stringify(v) + ' is not passed on'); });
  // is a download the game data? (a captive portal answers every address with "200 OK")
  var B = P.badDownload, SIZE = 14650810;
  eq(B('application/octet-stream', SIZE, SIZE), null, 'the data: accepted');
  eq(B('application/octet-stream', SIZE, 0), null, 'the data, the build does not know its size: accepted');
  eq(B(null, SIZE, 0), null, 'no type at all, a big body: accepted');
  eq(B('text/plain', SIZE, SIZE), null, 'only a web page is refused by its type');
  ['text/html', 'text/html; charset=utf-8', 'TEXT/HTML', ' text/html', 'Text/Html;charset=UTF-8'].forEach(function (t) { eq(typeof B(t, SIZE, SIZE), 'string', 'a web page (' + t + ') is not the data, however big'); eq(typeof B(t, null, SIZE), 'string', 'a web page (' + t + ') is refused before its body is read'); });
  eq(B('application/octet-stream', null, SIZE), null, 'before the body is read only the type counts');
  eq(typeof B('application/octet-stream', 60, SIZE), 'string', 'a body of 60 bytes is not the data');
  eq(typeof B('application/octet-stream', SIZE - 1, SIZE), 'string', 'one byte short is not the data (the exact size is known)');
  eq(typeof B('application/octet-stream', SIZE + 1, SIZE), 'string', 'one byte too many is not the data');
  eq(typeof B('application/octet-stream', 0, SIZE), 'string', 'nothing is not the data');
  eq(typeof B('application/octet-stream', 60, 0), 'string', 'a body of 60 bytes is not the data (the size is not known)');
  eq(typeof B('application/octet-stream', 999999, 0), 'string', 'under a megabyte is not the data (the size is not known)');
  eq(B('application/octet-stream', 1000000, 0), null, 'a megabyte may be (the size is not known)');
  return JSON.stringify({ checks: n, bad: bad });
})()
"""


def count_changed_pixels(a, b, x0, y0, x1, y1):
    """How many pixels of the rectangle differ between two screenshots (read_png tuples of the same size)."""
    w, h, ch, da = a
    _, _, _, db = b
    x0, y0, x1, y1 = max(0, int(x0)), max(0, int(y0)), min(w, int(x1)), min(h, int(y1))
    changed = 0
    for y in range(y0, y1):
        ra = da[(y * w + x0) * ch:(y * w + x1) * ch]
        rb = db[(y * w + x0) * ch:(y * w + x1) * ch]
        if ra != rb:
            for i in range(0, len(ra), ch):
                if ra[i:i + 3] != rb[i:i + 3]:
                    changed += 1
    return changed


def screen_of_canvas(g, lx, ly, canvas_w, canvas_h):
    """Where a point of the game's canvas (canvas_w x canvas_h logical pixels) is on the page, from the box's rectangle."""
    bx = g["box"]
    return bx[0] + lx / canvas_w * bx[2], bx[1] + ly / canvas_h * bx[3]


def quick_help_start(shape):
    """Where a person clicks the quick help's START! button, in the canvas's own pixels (the middle of its rectangle), for the picture of this shape: (640, 480) is the original's own page,
    (960, 540) the wide page, whose START! is the same button in the bottom right corner (its place moved by the canvas's size less the page's). The rectangle is read from the layout's own
    source (the classic one, src/ants_app/page_layout.cpp), so that a change of the layout moves the check with it. The check used to tap the place that the original's page has in the middle of
    the wide canvas, which the wide pages of v0.2.0 (every screen composed for 16:9) leave empty: the game was right, the tap was in the clay."""
    source = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "ants_app", "page_layout.cpp")
    with open(source, encoding="utf-8") as f:
        found = re.search(r"kQuickHelpClassic\{LayoutRect\{[^}]*\}, LayoutRect\{[^}]*\}, LayoutRect\{(\d+), (\d+), (\d+), (\d+)\}", f.read())
    if found is None:
        raise RuntimeError("the quick help's rectangle is not in src/ants_app/page_layout.cpp any more: the check cannot find START")
    x, y, w, h = (int(v) for v in found.groups())
    return x + (shape[0] - 640) + w / 2, y + (shape[1] - 480) + h / 2


# The retry rule of the data download (ANTS_PAGE.downloadWithRetries) with a fake download that fails a given number of times
RETRY_CHECKS = r"""
(async function () {
  var bad = [], n = 0;
  function eq(a, b, what) { n++; if (JSON.stringify(a) !== JSON.stringify(b)) bad.push(what + ': ' + JSON.stringify(a) + ' != ' + JSON.stringify(b)); }
  var P = ANTS_PAGE;
  async function run(failures, delays) {
    var calls = [], waits = [], reports = [];
    var fetchOnce = function (attempt) { calls.push(attempt); return attempt < failures ? Promise.reject(new Error('fail ' + attempt)) : Promise.resolve('bytes'); };
    var wait = function (ms) { waits.push(ms); return Promise.resolve(); };
    var out;
    try { out = { value: await P.downloadWithRetries(fetchOnce, delays, wait, function (a, e) { reports.push(a + ':' + e.message); }) }; } catch (e) { out = { error: e.message }; }
    return { out: out, calls: calls, waits: waits, reports: reports };
  }
  var d = [400, 1200, 3000];
  eq(await run(0, d), { out: { value: 'bytes' }, calls: [0], waits: [], reports: [] }, 'no failure: one try');
  eq(await run(1, d), { out: { value: 'bytes' }, calls: [0, 1], waits: [400], reports: ['0:fail 0'] }, 'one failure: a second try after 0.4 s');
  eq(await run(3, d), { out: { value: 'bytes' }, calls: [0, 1, 2, 3], waits: [400, 1200, 3000], reports: ['0:fail 0', '1:fail 1', '2:fail 2'] }, 'three failures: the fourth try succeeds');
  eq(await run(4, d), { out: { error: 'fail 3' }, calls: [0, 1, 2, 3], waits: [400, 1200, 3000], reports: ['0:fail 0', '1:fail 1', '2:fail 2'] }, 'four failures: it fails with the last error after 3 retries');
  eq(await run(99, []), { out: { error: 'fail 0' }, calls: [0], waits: [], reports: [] }, 'no retries allowed: one try');
  return JSON.stringify({ checks: n, bad: bad });
})()
"""


class EventDevTools(DevTools):
    """DevTools that also hands the events (messages without an id) to handlers, which run in threads of their own (a handler may call the browser)."""

    def __init__(self, port):
        self.handlers = []
        super().__init__(port)

    def _pump(self):
        while True:
            text = self.ws.receive()
            if text is None:
                for q in list(self.waiting.values()):
                    q.put({"error": {"message": "connection closed"}})
                return
            msg = json.loads(text)
            if "id" in msg:
                q = self.waiting.pop(msg["id"], None)
                if q is not None:
                    q.put(msg)
            else:
                for handler in list(self.handlers):
                    threading.Thread(target=handler, args=(msg,), daemon=True).start()


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
                self.devtools = EventDevTools(self.port)
                break
            except (OSError, ValueError, ConnectionError):
                time.sleep(0.2)
        if self.devtools is None:
            raise NotReachable("the browser did not start")
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

    def open(self, url, wait=True, settle=1.5, timeout=None):
        navigation = self.call("Page.navigate", {"url": url})
        if navigation.get("errorText"):
            if not PAGE_REACHED[0]:
                raise NotReachable("nothing answers at %s (%s)" % (url, navigation["errorText"]))
            raise ConnectionError("the page could not be loaded again at %s (%s)" % (url, navigation["errorText"]))
        PAGE_REACHED[0] = True
        if not wait:
            return
        deadline = time.time() + (timeout if timeout else READY_TIMEOUT)
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


def exit_code_checks(args):
    """The statuses of this script itself: a page that is not there is a skip (3), a page that loads and whose game never gets ready is a failure (1), whatever the browser reports. It runs
    the script on a closed port and on a throwaway page of its own that never starts a game."""
    import http.server
    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    print("[web aspect] the script's exit statuses: a page that is not there is a skip, a page that hangs is a failure")
    bad = []

    def check(ok, what):
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            bad.append(what)

    base = [sys.executable, os.path.abspath(__file__), "--browser", path, "--ready-timeout", "6", "--quick"]
    closed = free_port()                                                     # (nothing listens there)
    r = subprocess.run(base + ["--web", "http://127.0.0.1:%d/" % closed], capture_output=True, text=True, timeout=180)
    check(r.returncode == 3 and "SKIP" in r.stdout, "a closed port: the check is skipped, exit status 3 (status %d)" % r.returncode)

    class Hang(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            body = b"<!DOCTYPE html><html><body>this page never starts a game</body></html>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Hang)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        r = subprocess.run(base + ["--web", "http://127.0.0.1:%d/" % server.server_address[1]], capture_output=True, text=True, timeout=180)
        check(r.returncode == 1 and "FAIL" in r.stdout and "SKIP" not in r.stdout, "a page that loads and never gets its game ready: a failure, exit status 1 (status %d)" % r.returncode)
    finally:
        server.shutdown()
    print("[web aspect] %d checks, %d failed" % (2, len(bad)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the image, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save the screenshots in")
    ap.add_argument("--quick", action="store_true", help="fewer sizes (1280 x 720, a phone)")
    ap.add_argument("--logic-only", action="store_true", help="only the page's own logic (ANTS_PAGE): no game, no layout")
    ap.add_argument("--runs-only", action="store_true", help="only the cold-cache runs of the front page (with --downloads N)")
    ap.add_argument("--downloads-only", action="store_true", help="only the page's logic and the data download's faults (retry, failure for good)")
    ap.add_argument("--pointer", action="store_true", help="also the exactness of the game's pointer at six layouts (the full run does it too; --pointer-only does nothing else)")
    ap.add_argument("--pointer-only", action="store_true", help="only the exactness of the game's pointer (the game's START button's edges, found with the mouse, at six layouts)")
    ap.add_argument("--wheel", action="store_true", help="also the wheel and the pinch: cancelled over the canvas only, and the game zooms (the full run does it too; --wheel-only does nothing else)")
    ap.add_argument("--wheel-only", action="store_true", help="only the wheel and the pinch of the page (cancelled over the canvas only; the game zooms a level a notch: 2, 0.71, 0.5, the map's limit; the middle button)")
    ap.add_argument("--load-only", action="store_true", help="only the page's logic and the faults of the loading of the game (index.js, index.wasm, a game that never starts, a body that is not the data)")
    ap.add_argument("--downloads", type=int, default=0, metavar="N", help="also N cold-cache runs of web/lobby.html (the front page, at /) with 2 and with 4 games on the page (needs the game server behind /ws)")
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 120)")
    ap.add_argument("--exit-codes", action="store_true", help="only check this script's own exit statuses: a closed port is a skip (3), a page that hangs is a failure (1)")
    ap.add_argument("--four", action="store_true", help="also check web/lobby.html (the front page, at /) (the page that plays seats in frames: the frames' shape, a bad ?aspect ignored, the remembered choice, frames of the picture's own size in a wide window); needs the game server behind /ws (the stack)")
    args = ap.parse_args()
    global READY_TIMEOUT
    READY_TIMEOUT = args.ready_timeout
    if args.exit_codes:
        return exit_code_checks(args)

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
    site = args.web if args.web.endswith("/") else args.web + "/"
    web = site + "play.html"                                                      # the game page, at its own path ("/" is the front page, the lobby, since the Play online page became it)
    browser = None
    try:
        browser = Browser(path)
        tab = Tab(browser)

        print("[web aspect] the page's own logic (ANTS_PAGE) in %s" % os.path.basename(path))
        tab.emulate(1280, 720, 1)
        tab.open(web, settle=0.5)
        def cold_runs():
            print("[web aspect] web/lobby.html (the front page, at /) with 2 and with 4 games on the page, cold cache, %d runs each: every frame must start" % args.downloads)
            for seats in (2, 4):
                for run in range(args.downloads):
                    fresh = Browser(path)                                       # a browser of its own: nothing is cached
                    try:
                        t = Tab(fresh)
                        t.emulate(1500, 900, 1)
                        t.call("Page.navigate", {"url": site + "?map=tiny&players=%d&play=here" % seats})
                        deadline = time.time() + 120
                        states = []
                        while time.time() < deadline:
                            time.sleep(1.0)
                            try:
                                states = json.loads(t.ev(r"""JSON.stringify(Array.prototype.map.call(document.querySelectorAll('iframe'), function (f) {
                                    try { var w = f.contentWindow; return { ready: !!w.isReadyToPlay, log: w.antsDownloadLog || [] }; } catch (e) { return { ready: false, log: [] }; } }))"""))
                            except (RuntimeError, TimeoutError):
                                states = []
                            if len(states) == seats and all(x["ready"] for x in states):
                                break
                        retries = sum(1 for x in states for e in x["log"] if not e.get("ok"))
                        check(len(states) == seats and all(x["ready"] for x in states), "%d seats, run %d: every frame started (%d of %d; %d download(s) were retried)" % (seats, run + 1, sum(1 for x in states if x["ready"]), seats, retries))
                        t.close()
                    finally:
                        fresh.close()

        if args.runs_only:
            tab.close()
            cold_runs()
            print("[web aspect] %d checks, %d failed" % (count[0], len(failures)))
            return 1 if failures else 0
        res = json.loads(tab.ev(PURE_CHECKS))
        check(not res["bad"], "ANTS_PAGE: %d checks%s" % (res["checks"], "" if not res["bad"] else ": " + "; ".join(res["bad"][:6])))
        retry = json.loads(tab.ev(RETRY_CHECKS))
        check(not retry["bad"], "the download's retry rule: %d checks%s" % (retry["checks"], "" if not retry["bad"] else ": " + "; ".join(retry["bad"][:6])))
        def download_fault_checks():
            print("[web aspect] the game's data: a failed download is retried, a failed one for good says so (faults injected with the DevTools Fetch domain)")

            def inject(tab_, fail_first, fail_all=False):
                state = {"seen": 0}

                def respond(p):
                    state["seen"] += 1
                    try:
                        if fail_all or state["seen"] <= fail_first:
                            tab_.call("Fetch.failRequest", {"requestId": p["requestId"], "errorReason": "Failed"})
                        else:
                            tab_.call("Fetch.continueRequest", {"requestId": p["requestId"]})
                    except (RuntimeError, TimeoutError):
                        pass

                def handler(msg):
                    if msg.get("method") == "Fetch.requestPaused" and msg.get("sessionId") == tab_.session:
                        respond(msg["params"])

                tab_.dt.handlers.append(handler)
                tab_.call("Fetch.enable", {"patterns": [{"urlPattern": "*index.data*", "requestStage": "Request"}]})
                return state, handler

            tab.emulate(1280, 720, 1)
            state, handler = inject(tab, 1)
            tab.open(web, settle=1.0)
            log = json.loads(tab.ev("JSON.stringify(window.antsDownloadLog)"))
            check(state["seen"] >= 2 and [e["ok"] for e in log] == [False, True], "the first request of index.data is failed: the page tries again and the game starts (requests seen: %d, log: %s)" % (state["seen"], log))
            check(tab.ev("window.isReadyToPlay") is True and tab.ev("document.getElementById('status-text').querySelector('button')") is None, "the game runs and no error card is shown")
            g = tab.geometry()
            check(g["backing"][0] * 9 == g["backing"][1] * 16, "the game that started after a retry has its 16:9 canvas (%s)" % g["backing"])
            tab.call("Fetch.disable")
            tab.dt.handlers.remove(handler)
            state, handler = inject(tab, 3)
            tab.open(web, settle=1.0)
            log = json.loads(tab.ev("JSON.stringify(window.antsDownloadLog)"))
            check([e["ok"] for e in log] == [False, False, False, True], "three failures in a row: the fourth try (the last of the three retries) starts the game (log: %s)" % [e["ok"] for e in log])
            tab.call("Fetch.disable")
            tab.dt.handlers.remove(handler)
            state, handler = inject(tab, 0, fail_all=True)
            tab.open(web, wait=False)
            deadline = time.time() + 60
            shown = None
            while time.time() < deadline:
                time.sleep(1.0)
                try:
                    shown = tab.ev("(function(){var b=document.getElementById('status-text').querySelector('button');return b?document.getElementById('status-text').innerText:null;})()")
                except (RuntimeError, TimeoutError):
                    shown = None
                if shown:
                    break
            check(shown is not None and "could not be downloaded" in shown and "Reload" in shown, "a download that fails for good shows the message and a Reload button (%s)" % (repr(shown)[:120],))
            check(state["seen"] == 4, "it tried four times (the first and three retries), not more (%d requests)" % state["seen"])
            check(tab.ev("window.isReadyToPlay") is not True, "the game did not start")
            tab.save_shot(args.shots, "download_failed_for_good")
            card = json.loads(tab.ev("JSON.stringify((function(){var c=document.getElementById('splash-overlay').getBoundingClientRect(),b=document.getElementById('status-text').querySelector('button').getBoundingClientRect();return {overlay:getComputedStyle(document.getElementById('splash-overlay')).pointerEvents, button:[b.x+b.width/2,b.y+b.height/2], inside:b.x>=c.x&&b.right<=c.right&&b.y>=c.y&&b.bottom<=c.bottom};})())"))
            check(card["overlay"] == "auto" and card["inside"], "the card takes clicks and its button is on the card (%s)" % card)
            before = state["seen"]
            tab.call("Fetch.disable")
            tab.dt.handlers.remove(handler)
            tab.click(card["button"][0], card["button"][1])
            deadline = time.time() + 120
            ready = False
            while time.time() < deadline and not ready:
                time.sleep(1.0)
                try:
                    ready = bool(tab.ev("!!window.isReadyToPlay"))
                except (RuntimeError, TimeoutError):
                    ready = False
            check(ready, "the Reload button loads the page again and, the download working, the game starts")


        def load_fault_checks():
            """What the page does when the game's files, not only its data, do not come: index.js that fails, index.wasm that fails, a game that never starts (index.js held), the data that
            is a web page / too small / an error status / cut short, and a promise that fails when the game already runs."""
            print("[web aspect] the loading of the game: a file that fails, a game that never starts, a body that is not the data (faults injected with the DevTools Fetch domain)")
            real = {}

            def real_data():
                if "bytes" not in real:
                    import urllib.request
                    with urllib.request.urlopen(site + "index.data", timeout=120) as r:
                        real["bytes"] = r.read()
                return real["bytes"]

            def install(pattern, rule, init_script=None):
                """Fetch.enable with `rule(state, params)` answering each paused request with ("fail",), ("continue",), ("hold",) or ("fulfill", code, content_type, body, headers)."""
                state = {"seen": 0, "requests": [], "held": []}

                def handler(msg):
                    if msg.get("method") != "Fetch.requestPaused" or msg.get("sessionId") != tab.session:
                        return
                    p_ = msg["params"]
                    state["seen"] += 1
                    state["requests"].append({"url": p_["request"]["url"], "headers": p_["request"].get("headers", {})})
                    action = rule(state, p_)
                    try:
                        if action[0] == "fail":
                            tab.call("Fetch.failRequest", {"requestId": p_["requestId"], "errorReason": "Failed"})
                        elif action[0] == "hold":
                            state["held"].append(p_["requestId"])
                        elif action[0] == "fulfill":
                            headers = [{"name": "Content-Type", "value": action[2]}] + [{"name": k, "value": v} for k, v in (action[4] if len(action) > 4 else {}).items()]
                            tab.call("Fetch.fulfillRequest", {"requestId": p_["requestId"], "responseCode": action[1], "responseHeaders": headers, "body": base64.b64encode(action[3]).decode()})
                        else:
                            tab.call("Fetch.continueRequest", {"requestId": p_["requestId"]})
                    except (RuntimeError, TimeoutError):
                        pass

                tab.dt.handlers.append(handler)
                script_id = None
                if init_script:
                    script_id = tab.call("Page.addScriptToEvaluateOnNewDocument", {"source": init_script})["identifier"]
                tab.call("Fetch.enable", {"patterns": [{"urlPattern": pattern, "requestStage": "Request"}]})
                return state, handler, script_id

            def uninstall(handler, script_id):
                tab.call("Fetch.disable")
                tab.dt.handlers.remove(handler)
                if script_id:
                    tab.call("Page.removeScriptToEvaluateOnNewDocument", {"identifier": script_id})

            def wait_for(expression, seconds):
                deadline = time.time() + seconds
                value = None
                while time.time() < deadline:
                    time.sleep(0.5)
                    try:
                        value = tab.ev(expression)
                    except (RuntimeError, TimeoutError):
                        value = None
                    if value:
                        return value
                return value

            CARD = "(function(){var b=document.getElementById('status-text').querySelector('button');return b?document.getElementById('status-text').innerText:null;})()"
            tab.emulate(1280, 720, 1)

            # index.js and index.wasm that fail: the card with the Reload button, not "Downloading data" for ever
            for name, pattern, what in (("index.js", "*index.js*", "the script"), ("index.wasm", "*index.wasm*", "the program")):
                state, handler, sid = install(pattern, lambda st, p_: ("fail",))
                tab.open(web + "?aspect=16:9", wait=False)
                shown = wait_for(CARD, 40)
                check(shown is not None and "could not be started" in shown and "Reload" in shown, "%s fails: the card says the game could not be started and has a Reload button (%s)" % (name, repr(shown)[:110]))
                check(tab.ev("window.isReadyToPlay") is not True, "%s fails: the game did not start" % name)
                tab.save_shot(args.shots, "load_failed_" + name.replace(".", "_"))
                uninstall(handler, sid)

            # index.wasm that is a web page (a captive portal, a missing file answered with the site's page) or an error: Emscripten aborts (the cache message with its button), never the loading screen
            for name, rule in (("a web page", lambda st, p_: ("fulfill", 200, "text/html", b"<html><body>Please log in</body></html>")), ("status 404", lambda st, p_: ("fulfill", 404, "text/plain", b"not found"))):
                state, handler, sid = install("*index.wasm*", rule)
                tab.open(web + "?aspect=16:9", wait=False)
                shown = wait_for(CARD, 40)
                check(shown is not None and "Reload" in shown or shown is not None and "Clear Cache" in shown, "index.wasm is %s: a card with a button is up, not the loading screen (%s)" % (name, repr(shown)[:100]))
                check(tab.ev("window.isReadyToPlay") is not True, "index.wasm is %s: the game did not start" % name)
                uninstall(handler, sid)

            # a game that never starts (index.js is asked for and never answered): the watchdog says so, the package is handed to its own file only, and a late start takes the card away
            state, handler, sid = install("*index.js*", lambda st, p_: ("hold",), "window.ANTS_START_TIMEOUT_MS = 3000;")
            tab.open(web + "?aspect=16:9", wait=False)
            shown = wait_for(CARD, 40)
            check(shown is not None and "did not start" in shown and "Reload" in shown, "the game never starts (index.js is never answered): the watchdog puts the card up (%s)" % (repr(shown)[:110],))
            tab.save_shot(args.shots, "load_hung")
            size = tab.ev("ANTS_DATA_SIZE > 0 ? ANTS_DATA_SIZE : antsDataPackage ? antsDataPackage.byteLength : 0")
            got = json.loads(tab.ev("""JSON.stringify((function(size){
                var held = antsDataPackage ? antsDataPackage.byteLength : null;
                var other = Module.getPreloadedPackage('other.data', size) === null;
                var wrong = Module.getPreloadedPackage('index.data', size + 1) === null;
                var still = antsDataPackage !== null;
                var b = Module.getPreloadedPackage('index.data', size);
                return { held: held, other: other, wrong: wrong, still: still, right: b ? b.byteLength : null, after: antsDataPackage };
            })(%d))""" % (size or 0)))
            check(got["held"] == size and got["other"] and got["wrong"] and got["still"], "the downloaded package is kept for index.data of the size that the packager says, and for nothing else (%s)" % got)
            check(got["right"] == size and got["after"] is None, "... it is handed over once and then released (%s)" % got)
            tab.call("Fetch.disable")                                      # (the held requests are let go: the game starts late)
            for rid in state["held"]:
                try:
                    tab.call("Fetch.continueRequest", {"requestId": rid})
                except (RuntimeError, TimeoutError):
                    pass
            ready = wait_for("!!window.isReadyToPlay", 60)
            time.sleep(1.0)
            hidden = tab.ev("(function(){var o=document.getElementById('splash-overlay');return getComputedStyle(o).display==='none'&&!o.classList.contains('failed');})()")
            check(bool(ready) and bool(hidden), "a game that starts after the card was up takes the card away (ready %s, card gone %s)" % (ready, hidden))
            tab.dt.handlers.remove(handler)
            tab.call("Page.removeScriptToEvaluateOnNewDocument", {"identifier": sid})

            # the data is a web page (a captive portal), too small, an error status with the real bytes, cut short, one byte short
            page = b"<!DOCTYPE html><html><body>Please log in to the hotel wifi</body></html>"
            for name, rule, want in (("a web page with status 200 (a captive portal)", lambda st, p_: ("fulfill", 200, "text/html; charset=utf-8", page), "web page"),
                                     ("60 bytes of octet-stream with status 200", lambda st, p_: ("fulfill", 200, "application/octet-stream", b"x" * 60), "bytes")):
                state, handler, sid = install("*index.data*", rule)
                tab.open(web + "?aspect=16:9", wait=False)
                shown = wait_for(CARD, 60)
                log = json.loads(tab.ev("JSON.stringify(window.antsDownloadLog || [])"))
                check(shown is not None and "could not be downloaded" in shown and "Reload" in shown, "%s: every try is refused and the card says so (%s)" % (name, repr(shown)[:90]))
                check(state["seen"] == 4 and [e["ok"] for e in log] == [False] * 4 and all(want in e.get("error", "") for e in log), "%s: four tries, none accepted (%d requests; %s)" % (name, state["seen"], [e.get("error", "")[:50] for e in log]))
                check(tab.ev("window.isReadyToPlay") is not True, "%s: the game did not start on it" % name)
                tab.save_shot(args.shots, "load_" + ("html200" if "web page" in name else "small200"))
                uninstall(handler, sid)

            def first_bad(name, make_first, want, extra_check=None):
                data = real_data()

                def rule(st, p_):
                    if st["seen"] == 1:
                        return make_first(data)
                    return ("continue",)

                state, handler, sid = install("*index.data*", rule)
                net = {"urls": {}, "extra": {}}

                def watch_net(msg):
                    if msg.get("sessionId") != tab.session:
                        return
                    method, params = msg.get("method"), msg.get("params", {})
                    if method == "Network.requestWillBeSent":
                        net["urls"][params["requestId"]] = params["request"]["url"]
                    elif method == "Network.requestWillBeSentExtraInfo":
                        net["extra"][params["requestId"]] = params.get("headers", {})

                tab.dt.handlers.append(watch_net)
                tab.call("Network.enable")
                tab.open(web + "?aspect=16:9", settle=1.0)
                log = json.loads(tab.ev("JSON.stringify(window.antsDownloadLog)"))
                check([e["ok"] for e in log] == [False, True] and want in log[0].get("error", ""), "%s: the first try is refused (%s), the second works and the game starts (%s)" % (name, log[0].get("error", "")[:60] if log else None, [e["ok"] for e in log]))
                if extra_check:
                    time.sleep(0.5)
                    extra_check(net)
                tab.call("Network.disable")
                tab.dt.handlers.remove(watch_net)
                uninstall(handler, sid)

            def retry_headers(net):
                # the headers that the browser really sent (the first request was answered by the fault, never sent): the retry asks for no cache at all (fetch's cache: 'no-store')
                sent = [{k.lower(): v for k, v in headers.items()} for rid, headers in net["extra"].items() if "index.data" in net["urls"].get(rid, "")]
                check(len(sent) >= 1 and all("no-cache" in h.get("cache-control", "") and "if-none-match" not in h and "if-modified-since" not in h for h in sent),
                      "the retry asks for no cache at all (the headers it sent: %s)" % [{k: h.get(k) for k in ("cache-control", "pragma", "if-none-match")} for h in sent])

            first_bad("an error status with the real bytes (404)", lambda d: ("fulfill", 404, "application/octet-stream", d), "HTTP 404", retry_headers)
            first_bad("a body cut short that says its full length", lambda d: ("fulfill", 200, "application/octet-stream", d[:len(d) // 3], {"Content-Length": str(len(d))}), "")
            if tab.ev("ANTS_DATA_SIZE") > 0:
                first_bad("one byte short (the exact size is known)", lambda d: ("fulfill", 200, "application/octet-stream", d[:-1], {"Content-Length": str(len(d) - 1)}), "bytes")
            else:
                print("  (this page does not know the exact size of the data: the one-byte-short check is skipped)")

            # a promise that fails once the game runs is only logged: it must not cover the game with the card
            tab.open(web + "?aspect=16:9", settle=1.0)
            tab.ev("Promise.reject(new Error('a late failure')); 1")
            time.sleep(1.0)
            covered = tab.ev("(function(){var o=document.getElementById('splash-overlay');return getComputedStyle(o).display!=='none'||!!document.getElementById('load-failure')&&getComputedStyle(o).display!=='none';})()")
            check(not covered and tab.ev("window.isReadyToPlay") is True, "a promise that fails when the game runs does not put the card over it")

            # index.js is asked for once, with one ?v= (the Dockerfile adds the build's ?v= to every src=index.js of the page: a second one was added to the page's own)
            tab.call("Network.enable")
            seen = []

            def watch(msg):
                if msg.get("method") == "Network.requestWillBeSent" and msg.get("sessionId") == tab.session and "index.js" in msg["params"]["request"]["url"]:
                    seen.append(msg["params"]["request"]["url"])

            tab.dt.handlers.append(watch)
            tab.open(web + "?aspect=16:9", settle=1.0)
            tab.dt.handlers.remove(watch)
            tab.call("Network.disable")
            check(len(seen) == 1 and seen[0].count("?v=") == 1 and "?v=" in seen[0], "index.js is asked for once, with one ?v= (%s)" % seen)

        def pointer_checks(cases):
            """The game's pointer is EXACT: the edges of the setup screen's START button are found by moving the real mouse (a hover lights the button) and compared with the edges that the
            game's own arithmetic gives (floor(whole CSS offset x device ratio / scale)). Before the box was placed (ANTS_PAGE.snapOffset) the reading was up to a whole CSS pixel off at
            fractional layouts: a box at 68.98 read one pixel high, a box of a fractional size read one pixel low."""
            print("[web aspect] the pointer is exact: the edges of START on the setup screen, found with the real mouse, against the game's own arithmetic")
            edges = {"left": ("x", 846, True), "right": ("x", 944, False), "top": ("y", 499, True), "bottom": ("y", 526, False)}
            for label, w, h, dpr, mobile in cases:
                tab.emulate(w, h, dpr, mobile)
                tab.open(web + "?aspect=16:9", settle=1.5)
                tab.mouse("mouseMoved", 5, 5, button="none")
                tab.call("Input.dispatchKeyEvent", {"type": "keyDown", "key": "Enter", "code": "Enter", "text": "\r", "windowsVirtualKeyCode": 13})
                tab.call("Input.dispatchKeyEvent", {"type": "keyUp", "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13})
                time.sleep(1.5)
                g = tab.geometry()
                b = g["box"]
                scale = g["backing"][0] / 960.0
                tab.mouse("mouseMoved", 5, 5, button="none")
                time.sleep(0.3)
                ref = tab.shot()

                def at(lx, ly):
                    return b[0] + lx / 960 * b[2], b[1] + ly / 540 * b[3]

                def hovered(ox, oy, left_probe):
                    tab.mouse("mouseMoved", b[0] + ox, b[1] + oy, button="none")
                    time.sleep(0.12)
                    shot = tab.shot()
                    a0, a1 = at(905, 504) if left_probe else at(852, 504)
                    a2, a3 = at(940, 520) if left_probe else at(888, 520)
                    return count_changed_pixels(ref, shot, a0 * dpr, a1 * dpr, a2 * dpr, a3 * dpr) > 150

                worst = 0.0
                detail = []
                for name, (axis, edge, hover_high) in edges.items():
                    want = math.ceil(edge * scale / dpr - 1e-9)                    # the first whole CSS offset whose reading reaches the edge
                    while math.floor(want * dpr / scale + 1e-9) < edge:
                        want += 1
                    while want > 0 and math.floor((want - 1) * dpr / scale + 1e-9) >= edge:
                        want -= 1
                    lo, hi = float(want - 4), float(want + 4)
                    for _ in range(6):
                        mid = (lo + hi) / 2
                        on = hovered(mid, 512 / 540 * b[3], name == "left") if axis == "x" else hovered(910 / 960 * b[2], mid, False)
                        if on == hover_high:
                            hi = mid
                        else:
                            lo = mid
                    delta = (lo + hi) / 2 - want
                    worst = max(worst, abs(delta))
                    detail.append("%s %+.2f" % (name, delta))
                check(worst <= 0.2, "%s: the START button's four edges are where the game's arithmetic puts them (box %s, canvas %d: %s CSS px off)" % (label, [round(v, 3) for v in b], g["backing"][0], ", ".join(detail)))

        def wheel_checks():
            """The wheel and a trackpad's pinch over the game zoom the GAME (the mouse-wheel zoom): over the canvas the page cancels them (no scroll, no page zoom), elsewhere it does not
            (the title, the selector and the guide scroll the page as usual), and in a running match the game really zooms, one level of the series a notch. The game says its level
            (ants_probe(6), times 100) and it is seen in the picture: at the zoom 2 every world pixel of the map view is a 2 x 2 square (in a window whose box is exactly the canvas's
            960 x 540), the frame around the view never changes."""
            print("[web aspect] the wheel and the pinch: cancelled over the canvas only, and they zoom the game a level a notch (2, 0.71, 0.5, the map's limit); the middle button goes back to 1")

            def wheel(x, y, dy, modifiers=0):
                tab.call("Input.dispatchMouseEvent", {"type": "mouseWheel", "x": x, "y": y, "deltaX": 0, "deltaY": dy, "modifiers": modifiers})

            def settle_scroll():
                tab.ev("window.scrollTo(0, 0); 1")
                for _ in range(20):
                    if tab.ev("window.scrollY") == 0:
                        return
                    time.sleep(0.1)

            def centre_of(element_id):
                return json.loads(tab.ev("JSON.stringify((function(){var b=document.getElementById('%s').getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())" % element_id))

            def start_match():
                key = lambda kind: tab.call("Input.dispatchKeyEvent", {"type": kind, "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13, "text": "\r" if kind == "keyDown" else ""})
                for _ in range(2):                                           # the quick help's Enter (the setup screen), the setup screen's Enter (START)
                    key("keyDown")
                    key("keyUp")
                    time.sleep(1.5)
                deadline = time.time() + 20
                while time.time() < deadline and tab.ev("Module._ants_match_running()") != 1:
                    time.sleep(0.5)
                return tab.ev("Module._ants_match_running()") == 1

            def pixels_of(shot, g, cw, ch, x0, y0, w, h):
                """The rows of a rectangle of the game's canvas (canvas pixels) in a screenshot: the screen pixel at the middle of each canvas pixel (a scale of exactly 1 where the box is the canvas's size)"""
                sw, sh, nch, data = shot
                bx = g["box"]
                scale = bx[2] / cw
                rows = []
                for y in range(h):
                    sy = int(bx[1] + (y0 + y + 0.5) * scale)
                    row = bytearray()
                    for x in range(w):
                        sx = int(bx[0] + (x0 + x + 0.5) * scale)
                        i = (sy * sw + sx) * nch
                        row += data[i:i + nch]
                    rows.append(bytes(row))
                return rows, nch

            def differ_fraction(a, b, nch):
                total = 0
                changed = 0
                for ra, rb in zip(a, b):
                    for i in range(0, len(ra), nch * 3):
                        total += 1
                        if ra[i:i + 3] != rb[i:i + 3]:
                            changed += 1
                return changed / max(1, total)

            def block_fraction(rows, nch):
                """The share of the 2 x 2 blocks (on the best of the four alignments) that are one colour: a picture that was enlarged 2 times with the nearest filter is all of them"""
                best = 0.0
                for oy in (0, 1):
                    for ox in (0, 1):
                        total = 0
                        same = 0
                        for y in range(oy, len(rows) - 1, 6):
                            r0, r1 = rows[y], rows[y + 1]
                            for x in range(ox, len(r0) // nch - 1, 6):
                                i = x * nch
                                a = r0[i:i + 3]
                                total += 1
                                if a == r0[i + nch:i + nch + 3] and a == r1[i:i + 3] and a == r1[i + nch:i + nch + 3]:
                                    same += 1
                        best = max(best, same / max(1, total))
                return best

            def orange_fraction(rows, nch):
                total = 0
                orange = 0
                for y in range(0, len(rows), 4):
                    r = rows[y]
                    for x in range(0, len(r) // nch, 4):
                        i = x * nch
                        total += 1
                        if r[i] > 180 and r[i + 1] < 110 and r[i + 2] < 60:
                            orange += 1
                return orange / max(1, total)

            def wait_for_play(g, cw, ch, view):
                """The match's start dialog (\"Get ready to play!\", orange) closes by itself after some seconds: the wheel does nothing while it is up"""
                deadline = time.time() + 40
                while time.time() < deadline:
                    rows, nch = pixels_of(tab.shot(), g, cw, ch, *view)
                    if orange_fraction(rows, nch) < 0.02:
                        return True
                    time.sleep(1.0)
                return False

            def exact_width():
                """The width of a window whose box is exactly the canvas (960 CSS pixels): 960 plus the space that the page leaves at its sides plus the browser's scroll bar (none with overlay scroll
                bars), both read from a window that is narrower than the box can be (there the stage fills the page's width)"""
                tab.emulate(1000, 900, 1)
                tab.open(web + "?aspect=16:9", settle=1.0)
                narrow = tab.geometry()
                return int(round(960 + narrow["inner"][0] - narrow["stage"][2]))

            # ---- the page: what is cancelled and what is not
            exact_w = exact_width()
            tab.emulate(exact_w, 900, 1)                                      # a window whose box is exactly the canvas: 960 x 540, a scale of 1
            tab.open(web + "?aspect=16:9", settle=1.5)
            g = tab.geometry()
            check(abs(g["box"][2] - 960) < 0.01 and abs(g["box"][3] - 540) < 0.01 and g["dpr"] == 1 and g["scroll"][2] > g["scroll"][3], "the window of %d x 900 (960, the page's side space and the scroll bar) gives the canvas exactly 960 x 540 CSS pixels and the page scrolls (box %s, scroll %s)" % (exact_w, g["box"], g["scroll"]))
            tab.ev("window.__w = []; window.addEventListener('wheel', function (e) { window.__w.push({dp: e.defaultPrevented, ctrl: e.ctrlKey, cancelable: e.cancelable}); }); 1")
            cx, cy = g["box"][0] + 400, g["box"][1] + 300
            title = (300, 30)
            guide = centre_of("guide-mouse")
            guide[1] = tab.ev("document.getElementById('guide-mouse').getBoundingClientRect().y + 20")
            selector = centre_of("aspect-4-3")
            for label, point in (("the page's title", title), ("the selector's bar", selector), ("the guide", guide)):
                tab.ev("window.__w.length = 0; 1")
                tab.mouse("mouseMoved", point[0], point[1], button="none")
                wheel(point[0], point[1], 120)
                time.sleep(0.7)
                scrolled = tab.ev("window.scrollY")
                check(scrolled > 0, "a wheel over %s scrolls the page as usual (the page moved %s CSS pixels)" % (label, scrolled))
                settle_scroll()
                tab.ev("window.__w.length = 0; 1")
                wheel(point[0], point[1], -120, modifiers=2)                  # ctrl + wheel: the browser's page zoom (or a trackpad's pinch)
                time.sleep(0.5)
                seen = json.loads(tab.ev("JSON.stringify(window.__w)"))
                check(len(seen) >= 1 and all(not e["dp"] for e in seen), "a ctrl + wheel over %s is left to the browser: nothing cancels it, it is not even cancelable (the browser's own thread zooms the page without waiting for the page: %s)" % (label, seen))
            tab.ev("window.__w.length = 0; 1")
            tab.mouse("mouseMoved", cx, cy, button="none")
            wheel(cx, cy, 120)
            time.sleep(0.7)
            seen = json.loads(tab.ev("JSON.stringify(window.__w)"))
            check(tab.ev("window.scrollY") == 0 and len(seen) >= 1 and all(e["dp"] for e in seen), "a wheel over the canvas does not scroll the page: the page cancels it (scroll %s, %s)" % (tab.ev("window.scrollY"), seen))
            tab.ev("window.__w.length = 0; 1")
            wheel(cx, cy, -120, modifiers=2)
            time.sleep(0.5)
            seen = json.loads(tab.ev("JSON.stringify(window.__w)"))
            check(len(seen) >= 1 and all(e["dp"] and e["ctrl"] for e in seen), "a ctrl + wheel over the canvas (a trackpad's pinch in Chrome and Firefox) is cancelled too: no page zoom (%s)" % seen)

            # ---- the middle button (back to the zoom 1): SDL cancels the mouseup only, so on a page that scrolls an unprevented press would start the browser's autoscroll (Windows: Chrome,
            # Edge, Firefox) and defeat the game's use of the button. The page cancels the press (mousedown) and the click (auxclick) of the MIDDLE button over the canvas, and nothing elsewhere.
            tab.ev("window.__m = []; ['mousedown', 'auxclick'].forEach(function (n) { window.addEventListener(n, function (e) { window.__m.push([n, e.button, e.defaultPrevented]); }); }); 1")

            def press(point, name):
                tab.ev("window.__m.length = 0; 1")
                tab.mouse("mouseMoved", point[0], point[1], button="none")
                time.sleep(0.15)
                bits = {"left": 1, "middle": 4}[name]
                tab.call("Input.dispatchMouseEvent", {"type": "mousePressed", "x": point[0], "y": point[1], "button": name, "clickCount": 1, "buttons": bits})
                time.sleep(0.1)
                tab.call("Input.dispatchMouseEvent", {"type": "mouseReleased", "x": point[0], "y": point[1], "button": name, "clickCount": 1})
                time.sleep(0.3)
                return json.loads(tab.ev("JSON.stringify(window.__m)"))

            seen = press((cx, cy), "middle")
            check(["mousedown", 1, True] in seen and ["auxclick", 1, True] in seen and all(e[2] for e in seen if e[1] == 1),
                  "a middle press over the canvas is cancelled (mousedown and auxclick defaultPrevented: no browser autoscroll; %s)" % seen)
            for label, point in (("the page's title", title), ("the guide", guide)):
                seen = press(point, "middle")
                check(["mousedown", 1, False] in seen and all(not e[2] for e in seen),
                      "a middle press over %s is left to the browser: nothing cancels it (%s)" % (label, seen))
                settle_scroll()
            seen = press((cx, cy), "left")
            check(["mousedown", 0, False] in seen and all(not e[2] for e in seen),
                  "a left press over the canvas is not cancelled (the game keeps its focus and its clicks; %s)" % seen)
            settle_scroll()
            gestures = json.loads(tab.ev(r"""(function () {
                var c = document.getElementById('canvas'), got = [], out = [];
                c.addEventListener('wheel', function (e) { got.push([Math.round(e.deltaY), e.ctrlKey]); });
                function fire(target, type, scale) {
                    var e = new Event(type, {bubbles: true, cancelable: true});
                    Object.defineProperty(e, 'scale', {value: scale}); Object.defineProperty(e, 'clientX', {value: 100}); Object.defineProperty(e, 'clientY', {value: 100});
                    target.dispatchEvent(e);
                    return e.defaultPrevented;
                }
                out.push(fire(c, 'gesturestart', 1)); out.push(fire(c, 'gesturechange', 1.25)); out.push(fire(c, 'gesturechange', 1.5625)); out.push(fire(c, 'gesturechange', 1.25)); out.push(fire(c, 'gestureend', 1));
                var elsewhere = fire(document.getElementById('guide-mouse'), 'gesturechange', 2);
                return JSON.stringify({prevented: out, wheels: got, elsewhere: elsewhere});
            })()"""))
            check(all(gestures["prevented"]) and gestures["wheels"] == [[-100, True], [-100, True], [100, True]] and not gestures["elsewhere"],
                  "Safari's pinch (gesture events) over the canvas is cancelled and becomes wheel events for the game (a pinch of 25 %% is a notch: %s); on the guide it is left alone (%s)" % (gestures["wheels"], gestures["elsewhere"]))

            # ---- one page with ?fill= and the wheel (v0.1.0: the two features met in this page): the address gives the game its arguments (the leader's fill among them) and the wheel is
            # cancelled over the canvas only, exactly as on the page without them; the chat input of the setup screen is drawn by the game on that canvas, nothing else of the page changes
            tab.emulate(exact_w, 900, 1)
            tab.open(web + "?join=/ws&room=MEET-1&fill=HaRd&name=Zed&aspect=16:9", settle=1.5)
            joined = json.loads(tab.ev("JSON.stringify(window.ANTS_ARGS || null)") or "null")
            pairs = [(joined[i], joined[i + 1]) for i in range(len(joined) - 1)] if joined else []
            check(("--fill-bots", "hard") in pairs and ("--room", "MEET-1") in pairs and ("--name", "Zed") in pairs and ("--aspect", "16:9") in pairs and "HaRd" not in joined,
                  "?join=/ws&room=MEET-1&fill=HaRd&name=Zed&aspect=16:9: the game's arguments carry the fill in lower case, the room, the name and the aspect (%s)" % joined)
            g = tab.geometry()
            tab.ev("window.__w = []; window.addEventListener('wheel', function (e) { window.__w.push({dp: e.defaultPrevented, ctrl: e.ctrlKey}); }); window.scrollTo(0, 0); 1")
            cx, cy = g["box"][0] + 400, g["box"][1] + 300
            guide = centre_of("guide-mouse")
            guide[1] = tab.ev("document.getElementById('guide-mouse').getBoundingClientRect().y + 20")
            tab.mouse("mouseMoved", cx, cy, button="none")
            wheel(cx, cy, 120)
            time.sleep(0.7)
            seen = json.loads(tab.ev("JSON.stringify(window.__w)"))
            check(tab.ev("window.scrollY") == 0 and len(seen) >= 1 and all(e["dp"] for e in seen), "the page with ?fill=: a wheel over the canvas is cancelled and does not scroll the page (scroll %s, %s)" % (tab.ev("window.scrollY"), seen))
            tab.ev("window.__w.length = 0; 1")
            wheel(cx, cy, -120, modifiers=2)
            time.sleep(0.5)
            seen = json.loads(tab.ev("JSON.stringify(window.__w)"))
            check(len(seen) >= 1 and all(e["dp"] and e["ctrl"] for e in seen), "the page with ?fill=: a ctrl + wheel over the canvas is cancelled too (%s)" % seen)
            tab.ev("window.__w.length = 0; 1")
            tab.mouse("mouseMoved", guide[0], guide[1], button="none")
            wheel(guide[0], guide[1], 120)
            time.sleep(0.7)
            seen = json.loads(tab.ev("JSON.stringify(window.__w)"))
            check(tab.ev("window.scrollY") > 0 and len(seen) >= 1 and all(not e["dp"] for e in seen), "the page with ?fill=: a wheel over the guide scrolls the page as usual (scroll %s, %s)" % (tab.ev("window.scrollY"), seen))
            settle_scroll()

            # ---- the game: it zooms, one level a notch. The levels are the series 2^(k/4) (view_zoom.hpp): four notches from 1 reach 2, two notches out of 1 reach 0.71, eight out of 2 reach 0.5,
            # and the last level is the map's limit. The game says its own level (ants_probe(6): the zoom times 100), so the check does not rest on a picture alone. The match is the setup
            # screen's default map, TREASURE: every level down to 0.5 in both pictures, then 0.42 and the limit (0.397 in 16:9; 0.35, 0.30, 0.25 and the limit 0.230 in 4:3).
            BELOW_HALF = {"16:9": [42, 40], "4:3": [42, 35, 30, 25, 23]}                  # (times 100, from the level below 0.5 to the map's limit)

            def zoom_now(expected, wait=3.0):
                """The game's zoom times 100, read until it is `expected` or the time is up (the game takes the wheel at its next frame)"""
                deadline = time.time() + wait
                seen = tab.ev("Module._ants_probe(6)")
                while seen != expected and time.time() < deadline:
                    time.sleep(0.1)
                    seen = tab.ev("Module._ants_probe(6)")
                return seen

            def walk(label, px, py, path):
                """The wheel, a notch at a time, through the levels of `path` (the game's zoom times 100: the first is where the game is, every next one is one notch away; a notch toward the user
                zooms out, a notch away zooms in). The game must say the next level after every notch. The pause is longer than the wheel accumulator's 500 ms, so that the 0.2 left over from a
                delta of 120 (1.2 notches) is forgotten and a notch is exactly one level."""
                for before, after in zip(path, path[1:]):
                    wheel(px, py, 120 if after < before else -120)
                    seen = zoom_now(after)
                    if seen != after:
                        check(False, "%s: a notch %s from %d %% gives %d %% (the game says %s %%)" % (label, "toward" if after < before else "away", before, after, seen))
                        return False
                    time.sleep(0.65)
                check(True, "%s: %d notches, one level each (%s %%)" % (label, len(path) - 1, ", ".join(str(v) for v in path)))
                return True

            for aspect, cw, ch_, view, label in (("16:9", 960, 540, (16, 21, 762, 500), "16:9"), ("4:3", 640, 480, (16, 21, 442, 440), "classic 4:3")):
                tab.emulate(exact_w, 900, 1)
                tab.open(web + "?aspect=" + aspect, settle=1.5)
                g = tab.geometry()
                exact = abs(g["box"][2] - cw) < 0.01 and abs(g["box"][3] - ch_) < 0.01      # (the classic picture's box here is 960 x 720: a canvas pixel is 1.5 screen pixels, so no 2 x 2 squares to count there)
                tab.ev("document.getElementById('canvas').focus(); 1")
                check(start_match(), "%s: Enter at the quick help and Enter at the setup screen start a match" % label)
                check(wait_for_play(g, cw, ch_, view), "%s: the match's start dialog closes and the map view is clear" % label)
                px, py = screen_of_canvas(g, view[0] + view[2] / 2 + 40, view[1] + view[3] / 2 + 20, cw, ch_)
                tab.mouse("mouseMoved", px, py, button="none")
                time.sleep(0.4)
                shots = {}

                def take(name):
                    shot = tab.shot()
                    shots[name] = shot
                    tab.save_shot(args.shots, "zoom_%s_%s" % ("wide" if aspect == "16:9" else "classic", name))
                    return shot

                def view_rows(name):
                    return pixels_of(shots[name], g, cw, ch_, *view)

                def strip(name):
                    return pixels_of(shots[name], g, cw, ch_, 0, 24, 14, ch_ - 48)

                def differs(a, b):
                    return differ_fraction(view_rows(a)[0], view_rows(b)[0], view_rows(a)[1])

                def middle_button():
                    tab.mouse("mousePressed", px, py, button="middle", clickCount=1)
                    tab.mouse("mouseReleased", px, py, button="middle", clickCount=1)
                    time.sleep(0.8)

                check(zoom_now(100, wait=0.5) == 100, "%s: a match starts at the zoom 1 (the game says %s %%)" % (label, tab.ev("Module._ants_probe(6)")))
                take("1")
                walk("%s: four notches away from 1 reach 2" % label, px, py, [100, 119, 141, 168, 200])
                time.sleep(0.8)
                take("2")
                d21 = differs("1", "2")
                check(d21 > 0.2, "%s: a wheel rolled away over the map view zooms in: the map view is another picture (%.0f %% of its pixels changed)" % (label, d21 * 100))
                if exact:
                    rows1, nch = view_rows("1")
                    rows2, _ = view_rows("2")
                    b1, b2 = block_fraction(rows1, nch), block_fraction(rows2, nch)
                    check(b2 > 0.95 and b1 < 0.85, "%s: at the zoom 2 every world pixel is a 2 x 2 square (%.0f %% of the 2 x 2 blocks are one colour, %.0f %% at the zoom 1)" % (label, b2 * 100, b1 * 100))
                    s1, nch = strip("1")
                    s2, _ = strip("2")
                    check(s1 == s2, "%s: the frame around the map view (its left strip) is the same picture at the zoom 1 and 2" % label)
                walk("%s: eight notches toward from 2 reach 0.5" % label, px, py, [200, 168, 141, 119, 100, 84, 71, 59, 50])
                time.sleep(0.8)
                take("05")
                d51, d52 = differs("1", "05"), differs("2", "05")
                check(d51 > 0.2 and d52 > 0.2, "%s: the zoom 0.5 is a third picture of the map view (%.0f %% of its pixels differ from the zoom 1, %.0f %% from the zoom 2)" % (label, d51 * 100, d52 * 100))
                if exact:
                    s5, nch = strip("05")
                    check(s5 == s1, "%s: the frame around the map view is the same picture at the zoom 0.5" % label)
                    rows5, _ = view_rows("05")
                    check(block_fraction(rows5, nch) < 0.85, "%s: the zoom 0.5 is not a picture of 2 x 2 squares (%.0f %%)" % (label, block_fraction(rows5, nch) * 100))
                # the end of the range: the series goes on below 0.5 and the last level is the map's limit; a notch more changes nothing
                to_limit = [50] + BELOW_HALF[aspect]
                walk("%s: on from 0.5 to the map's limit (%d %%)" % (label, to_limit[-1]), px, py, to_limit)
                wheel(px, py, 120)
                time.sleep(0.8)
                check(tab.ev("Module._ants_probe(6)") == to_limit[-1], "%s: a notch toward at the map's limit changes nothing (the game says %s %%)" % (label, tab.ev("Module._ants_probe(6)")))
                walk("%s: back from the limit to 2, a level a notch" % label, px, py, list(reversed(to_limit)) + [59, 71, 84, 100, 119, 141, 168, 200])
                time.sleep(0.8)
                take("2b")
                if exact:
                    rows2b, nch = view_rows("2b")
                    check(block_fraction(rows2b, nch) > 0.95, "%s: the notches back from the limit are at 2 again (%.0f %% blocks)" % (label, block_fraction(rows2b, nch) * 100))
                middle_button()
                check(zoom_now(100, wait=1.0) == 100, "%s: the middle button goes back to the zoom 1 (the game says %s %%)" % (label, tab.ev("Module._ants_probe(6)")))
                take("1b")
                if exact:
                    rows1b, nch = view_rows("1b")
                    check(block_fraction(rows1b, nch) < 0.85, "%s: the middle button goes back to the zoom 1 (%.0f %% blocks)" % (label, block_fraction(rows1b, nch) * 100))
                else:
                    check(differs("2b", "1b") > 0.2, "%s: the middle button goes back to the zoom 1: the map view changes" % label)
                # a level between the exact ones (0.71, two notches out of 1) is smoothed: a picture of its own, not squares
                walk("%s: two notches toward from 1 reach 0.71" % label, px, py, [100, 84, 71])
                time.sleep(0.8)
                take("071")
                d71 = (differs("1", "071"), differs("2", "071"), differs("05", "071"))
                check(min(d71) > 0.2, "%s: the zoom 0.71 is a picture of its own (%.0f %% of its pixels differ from the zoom 1, %.0f %% from 2, %.0f %% from 0.5)" % ((label,) + tuple(v * 100 for v in d71)))
                if exact:
                    s71, nch = strip("071")
                    check(s71 == s1, "%s: the frame around the map view is the same picture at the zoom 0.71" % label)
                    rows71, _ = view_rows("071")
                    check(block_fraction(rows71, nch) < 0.85, "%s: the zoom 0.71 is not a picture of 2 x 2 squares (%.0f %%)" % (label, block_fraction(rows71, nch) * 100))
                middle_button()
                check(zoom_now(100, wait=1.0) == 100, "%s: the middle button goes back to the zoom 1 from 0.71 (the game says %s %%)" % (label, tab.ev("Module._ants_probe(6)")))
                # a wheel over the minimap does not zoom (and does not scroll the page either)
                mx, my = screen_of_canvas(g, (480 if aspect == "4:3" else 800) + 40, 60, cw, ch_)
                tab.mouse("mouseMoved", mx, my, button="none")
                time.sleep(0.3)
                take("m0")
                wheel(mx, my, -120)
                time.sleep(0.8)
                take("m1")
                check(tab.ev("window.scrollY") == 0, "%s: a wheel over the minimap does not scroll the page" % label)
                check(tab.ev("Module._ants_probe(6)") == 100, "%s: a wheel over the minimap does not zoom the map view (the game says %s %%)" % (label, tab.ev("Module._ants_probe(6)")))
                if exact:
                    rows_m, nch = view_rows("m1")
                    check(block_fraction(rows_m, nch) < 0.85, "%s: a wheel over the minimap does not zoom the map view (%.0f %% blocks)" % (label, block_fraction(rows_m, nch) * 100))

        POINTER_CASES = [("1280x720@1", 1280, 720, 1, False), ("1000x640@1", 1000, 640, 1, False), ("1536x864@1.25", 1536, 864, 1.25, False), ("1500x844@1.5", 1500, 844, 1.5, False),
                         ("1280x720@2.625", 1280, 720, 2.625, False), ("844x390@3 (phone)", 844, 390, 3, True)]
        if args.pointer_only:
            pointer_checks(POINTER_CASES)
            tab.close()
            print("[web aspect] %d checks, %d failed" % (count[0], len(failures)))
            return 1 if failures else 0
        if args.wheel_only:
            wheel_checks()
            tab.close()
            print("[web aspect] %d checks, %d failed" % (count[0], len(failures)))
            return 1 if failures else 0
        if args.load_only:
            load_fault_checks()
            tab.close()
            print("[web aspect] %d checks, %d failed" % (count[0], len(failures)))
            return 1 if failures else 0
        if args.downloads_only:
            download_fault_checks()
            load_fault_checks()
            tab.close()
            if args.downloads > 0:
                cold_runs()
            print("[web aspect] %d checks, %d failed" % (count[0], len(failures)))
            return 1 if failures else 0
        if args.logic_only:
            tab.close()
            print("[web aspect] %d checks, %d failed" % (count[0], len(failures)))
            return 1 if failures else 0

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
            # the pointer: a box whose CSS size is a whole number stands on a whole pixel, any other 1/64 px short of one (Emscripten cuts the box's position and SDL scales by floor(size) / size:
            # web/shell.html, ANTS_PAGE.snapOffset)
            for axis, pos, size in (("x", bx[0], bx[2]), ("y", bx[1], bx[3])):
                whole_size = abs(size - round(size)) < 1.0 / 128
                at = pos if whole_size else pos + 1.0 / 64
                check(abs(at - round(at)) < 1e-6, "%s: the box's %s position %.4f is where the pointer is exact (size %.4f is %s)" % (label, axis, pos, size, "whole: a whole pixel" if whole_size else "not whole: 1/64 short of a whole pixel"))
            check(bx[0] >= -0.5 and bx[0] + bx[2] <= g["inner"][0] + 0.5, "%s: the box is inside the window's width" % label)
            check(g["scroll"][0] <= g["scroll"][1] + 1, "%s: no sideways scroll (%d of %d)" % (label, g["scroll"][0], g["scroll"][1]))

        print("[web aspect] the page at several sizes (16:9 unless the address or the remembered choice says otherwise: a phone held upright gets 16:9 too)")
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
            layout_checks(label, g, "16:9")                          # (16:9 on every device, a phone held upright included)
            tab.save_shot(args.shots, "page_" + label.replace(" ", "_").replace("(", "").replace(")", "").replace("@", "_dpr"))

        print("[web aspect] touch: a tap lands where it should (a phone, both shapes)")
        for label, w, h, query, shape in (("portrait phone, 16:9 picture", 390, 844, "", (960, 540)), ("portrait phone, classic picture asked for", 390, 844, "?aspect=4:3", (640, 480)),
                                          ("landscape phone, 16:9 picture", 844, 390, "", (960, 540))):
            tab.emulate(w, h, 3, True)
            tab.open(web + query, settle=2.5)
            g = tab.geometry()
            before = tab.shot()
            qx, qy = quick_help_start(shape)
            sx, sy = screen_of_canvas(g, qx, qy, shape[0], shape[1])
            tab.tap(sx, sy)
            time.sleep(1.8)
            after = tab.shot()
            diff = sum(1 for i in range(0, len(before[3]), before[2] * 97) if abs(before[3][i] - after[3][i]) > 40)
            check(diff > 60, "%s: a tap on the quick help's START button leaves the quick help (%d sampled pixels changed)" % (label, diff))

        # a phone held upright gets the 16:9 picture (no exception for phones: the whole picture is visible, no sideways scroll), keeps it when it is turned to landscape (the box follows and
        # is bigger), and a remembered 4:3 still wins on a portrait phone
        tab.emulate(390, 844, 3, True)
        tab.ev("try { localStorage.removeItem('ants.aspect.v2'); } catch (e) {} 1")
        tab.open(web, settle=1.0)
        g = tab.geometry()
        check(g["aspect"] == "16:9" and g["args"][-2:] == ["--aspect", "16:9"] and g["checked"] == ["16:9=true", "4:3=false"], "a phone held upright: the 16:9 picture (%s, %s)" % (g["aspect"], g["checked"]))
        layout_checks("the phone held upright (390 x 844 at 3)", g, "16:9")
        check(g["box"][2] > 300 and g["box"][0] + g["box"][2] <= g["inner"][0] + 0.5, "the whole picture is visible: the box is %.0f px wide inside the window's %d" % (g["box"][2], g["inner"][0]))
        portrait_box = g["box"][2]
        tab.emulate(844, 390, 3, True)
        time.sleep(1.5)
        g = tab.geometry()
        layout_checks("the phone turned to landscape", g, "16:9")
        check(g["aspect"] == "16:9" and g["box"][2] > portrait_box, "turned to landscape the picture stays 16:9 and the box is bigger (%.0f px, upright %.0f px)" % (g["box"][2], portrait_box))
        tab.save_shot(args.shots, "phone_turned_to_landscape")
        tab.emulate(390, 844, 3, True)
        tab.open(web, settle=0.5)                                                         # (the page's own origin: its local storage is what the selector writes)
        tab.ev("try { localStorage.setItem('ants.aspect.v2', '4:3'); } catch (e) {} 1")
        tab.open(web, settle=1.0)
        g = tab.geometry()
        check(g["aspect"] == "4:3" and g["checked"] == ["16:9=false", "4:3=true"], "a portrait phone with a remembered 4:3 keeps the classic picture (%s)" % g["aspect"])
        layout_checks("the phone held upright, classic picture remembered", g, "4:3")
        tab.ev("try { localStorage.removeItem('ants.aspect.v2'); } catch (e) {} 1")
        tab.save_shot(args.shots, "phone_portrait_remembered_classic")

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
        tab.ev("try { localStorage.removeItem('ants.aspect.v2'); } catch (e) {} 1")
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
        check(tab.ev("localStorage.getItem('ants.aspect.v2')") == "4:3", "the browser remembered the choice")
        tab.save_shot(args.shots, "selector_classic")
        tab.open(web)                                                 # no parameter: the remembered choice
        g = tab.geometry()
        check(g["aspect"] == "4:3", "a visit without a parameter uses the remembered choice")
        tab.open(web + "?aspect=16:9")
        g = tab.geometry()
        check(g["aspect"] == "16:9" and tab.ev("localStorage.getItem('ants.aspect.v2')") == "4:3", "the address beats the remembered choice and does not change it")
        tab.ev("localStorage.setItem('ants.aspect.v2', 'junk'); 1")
        tab.open(web)
        check(tab.geometry()["aspect"] == "16:9", "a remembered value that is no shape is ignored")
        tab.ev("localStorage.removeItem('ants.aspect.v2'); 1")
        # the key of the first versions of the pages ('ants.aspect') is not read any more: a Classic 4:3 that a browser remembered under it is forgotten once, and the address still asks for 4:3
        tab.ev("try { localStorage.setItem('ants.aspect', '4:3'); } catch (e) {} 1")
        tab.open(web)
        g = tab.geometry()
        check(g["aspect"] == "16:9" and g["args"][-2:] == ["--aspect", "16:9"] and g["checked"] == ["16:9=true", "4:3=false"],
              "a 4:3 that the old key 'ants.aspect' remembers is not read: the page is 16:9 (%s, %s)" % (g["aspect"], g["checked"]))
        tab.open(web + "?aspect=4:3")
        check(tab.geometry()["aspect"] == "4:3", "... and ?aspect=4:3 still gives the classic picture with the old key in the browser")
        tab.ev("try { localStorage.removeItem('ants.aspect'); } catch (e) {} 1")

        # the selector asks before it leaves a match that is being played (it restarts the game); at the quick help or the setup screen it does not ask
        print("[web aspect] the selector and a running match: it asks first")
        dialogs = []
        answer = {"accept": False}

        def on_dialog(msg):
            if msg.get("method") == "Page.javascriptDialogOpening" and msg.get("sessionId") == tab.session:
                dialogs.append(msg["params"].get("message", ""))
                try:
                    tab.call("Page.handleJavaScriptDialog", {"accept": answer["accept"]})
                except (RuntimeError, TimeoutError):
                    pass

        tab.dt.handlers.append(on_dialog)
        tab.emulate(1280, 720, 1)
        tab.open(web + "?aspect=16:9", settle=2.0)
        g = tab.geometry()
        qx, qy = quick_help_start((960, 540))
        quick_start = screen_of_canvas(g, qx, qy, 960, 540)
        tab.click(quick_start[0], quick_start[1])                          # the quick help's START: the setup screen
        time.sleep(2.0)
        check(tab.ev("Module._ants_match_running()") == 0, "on the setup screen no match is running")
        button = json.loads(tab.ev("JSON.stringify((function(){var b=document.getElementById('aspect-4-3').getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})())"))
        start = screen_of_canvas(g, 895, 512, 960, 540)                    # the setup screen's START (single player: 846 .. 944 x 499 .. 526)
        tab.click(start[0], start[1])
        deadline = time.time() + 15
        while time.time() < deadline and tab.ev("Module._ants_match_running()") != 1:
            time.sleep(0.5)
        check(tab.ev("Module._ants_match_running()") == 1, "START begins a match and the game says one is running")
        tab.save_shot(args.shots, "match_running")
        tab.ev("try { localStorage.removeItem('ants.aspect.v2'); } catch (e) {} 1")
        answer["accept"] = False
        tab.click(button[0], button[1])
        time.sleep(1.5)
        check(len(dialogs) == 1 and "Leave the match to change the picture?" in dialogs[0], "the selector's click in a running match asks \"Leave the match to change the picture?\" (%s)" % dialogs)
        check(tab.ev("location.search.indexOf('aspect=4:3')") == -1 and tab.ev("Module._ants_match_running()") == 1 and tab.ev("localStorage.getItem('ants.aspect.v2')") is None,
              "answered No, the match goes on, the address is the same and the choice is not remembered")
        answer["accept"] = True
        tab.click(button[0], button[1])
        deadline = time.time() + 120
        while time.time() < deadline:
            time.sleep(0.5)
            try:
                if tab.ev("!!window.isReadyToPlay && location.search.indexOf('aspect=4:3') !== -1"):
                    break
            except (RuntimeError, TimeoutError):
                pass
        check(len(dialogs) == 2 and tab.ev("location.search.indexOf('aspect=4:3')") != -1 and tab.geometry()["aspect"] == "4:3", "answered Yes, the page reloads with the classic picture (%d questions)" % len(dialogs))
        tab.dt.handlers.remove(on_dialog)
        tab.ev("localStorage.removeItem('ants.aspect.v2'); 1")

        print("[web aspect] resizing, fullscreen and the pointer (1280 x 720)")
        tab.emulate(1280, 720, 1)
        tab.open(web)
        for w, h, dpr in ((1000, 700, 1), (1600, 900, 1), (900, 500, 2), (1280, 720, 1), (1280, 720, 2), (1280, 720, 1.5), (1280, 720, 1)):      # (the same window at other device ratios: a zoom, a move to another screen)
            tab.emulate(w, h, dpr)
            time.sleep(1.5)
            g = tab.geometry()
            layout_checks("resized to %dx%d@%s" % (w, h, dpr), g, "16:9")
        tab.save_shot(args.shots, "resized_back")
        # a page that scrolls (a short window: the guide is below the fold) at a ratio that makes scroll offsets fractional in CSS pixels: the box is placed again where the pointer is exact
        tab.emulate(800, 420, 1.25)
        tab.open(web)
        tab.ev("window.scrollTo(0, 10.4); 1")
        time.sleep(0.8)
        g = tab.geometry()
        check(tab.ev("window.pageYOffset") > 5, "(setup) the short page is scrolled (by %s CSS px)" % tab.ev("window.pageYOffset"))
        for axis, pos, size in (("x", g["box"][0], g["box"][2]), ("y", g["box"][1], g["box"][3])):
            whole_size = abs(size - round(size)) < 1.0 / 128
            at = pos if whole_size else pos + 1.0 / 64
            check(abs(at - round(at)) < 1e-6, "scrolled by a fractional amount: the box's %s position %.4f is where the pointer is exact (size %.4f)" % (axis, pos, size))
        # where a browser's scroll offset is not a whole number of CSS pixels the position changes with a scroll: the page places the box again on every scroll event (here: moved off its place
        # by hand, then a scroll event)
        tab.ev("document.getElementById('game-container').style.left = '0.3px'; window.dispatchEvent(new Event('scroll')); 1")
        time.sleep(0.6)
        g2 = tab.geometry()
        check(abs(g2["box"][0] - g["box"][0]) < 0.02 and tab.ev("document.getElementById('game-container').style.left") != "0.3px", "a scroll event places the box again (x %.4f after being moved by hand to %.4f)" % (g2["box"][0], g["box"][0] + 0.3))
        tab.emulate(1280, 720, 1)
        tab.open(web)
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
        # a click on a button: the quick help's START (the first page of the game), where the layout puts it on the 960 x 540 canvas (quick_help_start)
        tab.open(web, settle=2.5)
        g = tab.geometry()
        before = tab.shot()
        qx, qy = quick_help_start((960, 540))
        sx, sy = screen_of_canvas(g, qx, qy, 960, 540)
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

        download_fault_checks()
        load_fault_checks()
        if args.pointer or not args.quick:
            pointer_checks(POINTER_CASES)
        if args.wheel or not args.quick:
            wheel_checks()

        if args.four:
            print("[web aspect] web/lobby.html (the front page, at /): the games' frames (the game server must be behind /ws)")

            def four_frames(query, w=1500, h=900, dpr=1, seconds=14):
                tab.emulate(w, h, dpr)
                tab.call("Page.navigate", {"url": site + query})
                time.sleep(seconds)
                return json.loads(tab.ev(r"""JSON.stringify(Array.prototype.map.call(document.querySelectorAll('iframe'), function (f) {
                    var d = f.contentDocument, c = d && d.getElementById('canvas'), b = d && d.getElementById('game-container');
                    var fr = f.getBoundingClientRect(), br = b && b.getBoundingClientRect();
                    return { frame: [fr.width, fr.height], box: br && [br.width, br.height], backing: c && [c.width, c.height], aspect: d && d.getElementById('game-stage').getAttribute('data-aspect'), src: f.src,
                             page: document.body.getAttribute('data-aspect'), select: document.querySelector('input[name=aspect]:checked').value };
                }))"""))

            def frames_check(label, frames, want, size=None):
                check(len(frames) == 2, "%s: two games on the page (%d frames)" % (label, len(frames)))
                shape = (16, 9) if want == "16:9" else (4, 3)
                for f in frames:
                    ok = f["backing"] is not None and f["backing"][0] * shape[1] == f["backing"][1] * shape[0] and f["aspect"] == want and ("aspect=" + want) in f["src"] and "junk" not in f["src"]
                    ratio = f["frame"][0] / f["frame"][1]
                    check(ok and abs(ratio - shape[0] / shape[1]) < 0.01 and f["page"] == want and f["select"] == want,
                          "%s: a frame of %.0f x %.0f holds a %s picture (canvas %s, the page says %s)" % (label, f["frame"][0], f["frame"][1], want, f["backing"], f["page"]))
                    if size:
                        check(abs(f["frame"][0] - size[0]) < 0.5 and abs(f["frame"][1] - size[1]) < 0.5 and f["backing"] == list(size),
                              "%s: the frame is exactly %d x %d CSS pixels and so is the canvas, one canvas pixel for each pixel of the screen, sharp (frame %s, canvas %s)" % (label, size[0], size[1], f["frame"], f["backing"]))

            tab.emulate(1500, 900, 1)
            tab.open(web)                                                  # (the page's origin, to reach its localStorage)
            tab.ev("try { localStorage.removeItem('ants.aspect.v2'); } catch (e) {} 1")
            frames = four_frames("?map=tiny&players=2&play=here")
            frames_check("default", frames, "16:9")
            check(all(f["frame"][0] < 700 for f in frames), "default in a window of 1500: the frames fill their column, about 651 CSS pixels (%s)" % [f["frame"] for f in frames])
            tab.save_shot(args.shots, "four_16x9")
            frames_check("?aspect=4:3", four_frames("?map=tiny&players=2&play=here&aspect=4:3"), "4:3", (640, 480))
            tab.save_shot(args.shots, "four_4x3")
            frames_check("a window of 2560 x 1440", four_frames("?map=tiny&players=2&play=here", 2560, 1440), "16:9", (960, 540))
            tab.save_shot(args.shots, "four_16x9_native")
            frames_check("?aspect=junk (not a shape: ignored)", four_frames("?map=tiny&players=2&play=here&aspect=junk"), "16:9")
            frames_check("?aspect=21:9 (not a shape: ignored)", four_frames("?map=tiny&players=2&play=here&aspect=21:9"), "16:9")
            tab.ev("localStorage.setItem('ants.aspect.v2', '4:3'); 1")
            frames_check("the choice that the game page remembered (4:3), no parameter", four_frames("?map=tiny&players=2&play=here"), "4:3", (640, 480))
            frames_check("... and ?aspect=16:9 beats it", four_frames("?map=tiny&players=2&play=here&aspect=16:9"), "16:9")
            tab.ev("localStorage.setItem('ants.aspect.v2', 'junk'); 1")
            frames_check("a remembered value that is no shape is ignored", four_frames("?map=tiny&players=2&play=here"), "16:9")
            tab.ev("localStorage.removeItem('ants.aspect.v2'); 1")
            tab.ev("localStorage.setItem('ants.aspect', '4:3'); 1")
            frames_check("a 4:3 that the old key 'ants.aspect' remembers is not read", four_frames("?map=tiny&players=2&play=here"), "16:9")
            frames_check("... and ?aspect=4:3 still gives the classic picture", four_frames("?map=tiny&players=2&play=here&aspect=4:3"), "4:3", (640, 480))
            tab.ev("localStorage.removeItem('ants.aspect'); 1")
        tab.close()
        if args.downloads > 0:
            cold_runs()
    except NotReachable as error:
        print("  SKIP: the check could not be made (%s)" % error)
        return 1 if failures else 3
    except Exception as error:                                                # (a hang, a crash, a script that raised: the page or the browser broke down while it was checked)
        print("  FAIL: the browser or the page broke down during the check (%s: %s)" % (type(error).__name__, error))
        return 1
    finally:
        if browser is not None:
            browser.close()
    print("[web aspect] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
