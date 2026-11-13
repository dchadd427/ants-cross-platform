// The screen numbers of a window (web/home/home.js loads this only for the address "/?vp", docs/HOME_SCREEN.md): a small box over the front page with the readings that the page's height
// depends on, for one screenshot from a phone. Text only (textContent); no request, nothing stored; a tap puts the box away. ES5: a page gets AntsHomeVp, node gets module.exports.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeVp = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var MODES = ['standalone', 'fullscreen', 'minimal-ui', 'browser'];
    var UNITS = { vh: '100vh', dvh: '100dvh', svh: '100svh', lvh: '100lvh' };       // probe boxes that are this high
    var SIDES = { top: 't', right: 'r', bottom: 'b', left: 'l' };                  // probe boxes that have this safe-area inset as padding

    // The lines of the box: w is the window, d its document, probe the probe heights and insets (px as the page measured them)
    function lines(w, d, probe) {
        var v = w.visualViewport, r = d.body.getBoundingClientRect(), ua = w.navigator.userAgent || '', c = /Chrome\/([\d.]+)/.exec(ua), a = /Android ([\d.]+)/.exec(ua), mode = 'browser';
        for (var i = 0; i < MODES.length; i++) if (w.matchMedia('(display-mode: ' + MODES[i] + ')').matches) { mode = MODES[i]; break; }
        return ['display-mode ' + mode, 'inner ' + w.innerWidth + ' x ' + w.innerHeight, 'client height ' + d.documentElement.clientHeight,
            'visual viewport ' + (v ? v.width + ' x ' + v.height + ', top ' + v.offsetTop + ', scale ' + v.scale : 'none'),
            'screen ' + w.screen.width + ' x ' + w.screen.height + ', avail ' + w.screen.availWidth + ' x ' + w.screen.availHeight, 'pixel ratio ' + w.devicePixelRatio,
            'vh ' + probe.vh + ', dvh ' + probe.dvh + ', svh ' + probe.svh + ', lvh ' + probe.lvh, 'safe top ' + probe.t + ', right ' + probe.r + ', bottom ' + probe.b + ', left ' + probe.l,
            'home top ' + r.top + ', bottom ' + r.bottom + ', limit ' + (d.documentElement.style.getPropertyValue('--home-h') || 'none'), 'Chrome ' + (c ? c[1] : '-') + ', Android ' + (a ? a[1] : '-')];
    }

    // env: { doc, win }. Returns the box.
    function mount(env) {
        var doc = env.doc, win = env.win, box = doc.createElement('pre'), probes = {};
        box.id = 'vp-readout';
        box.setAttribute('style', 'position:fixed;left:6px;top:6px;z-index:9999;margin:0;padding:8px;max-width:calc(100% - 12px);overflow:auto;font:12px/1.35 monospace;white-space:pre-wrap;color:#fff;background:rgba(0,0,0,.88);border:1px solid #fff');
        function probe(key, style, read) {
            var p = probes[key];
            if (!p) { p = probes[key] = doc.createElement('div'); doc.body.appendChild(p); }
            p.setAttribute('style', 'position:fixed;left:0;top:0;width:1px;visibility:hidden;pointer-events:none;' + style);
            return read(p);
        }
        function paint() {
            var got = {}, k;
            for (k in UNITS) got[k] = probe(k, 'height:' + UNITS[k], function (p) { return p.getBoundingClientRect().height; });
            for (k in SIDES) got[SIDES[k]] = probe(SIDES[k], 'height:0;padding-top:env(safe-area-inset-' + k + ',0px)', function (p) { return win.getComputedStyle(p).paddingTop; });
            box.textContent = lines(win, doc, got).join('\n');
        }
        box.addEventListener('click', function () { box.hidden = true; });
        doc.body.appendChild(box);
        paint();
        win.addEventListener('resize', function () { setTimeout(paint, 50); });
        return box;
    }

    return { lines: lines, mount: mount };
});
