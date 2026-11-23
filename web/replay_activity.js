/* The activity curve of the replay page (docs/REPLAYS.md "The activity curve"): a soft filled curve just above the timeline that says where the action is in a match, the height at each moment being how much happened
   then: deaths most of all, then bombs that went off, thefts, fires and fights (the Auto camera's own weights, include/ants_app/auto_camera.hpp). On a computer it shows only while the pointer is over the timeline, with a
   line and a small label such as "2:41 · 3 deaths, 1 bomb"; on a touch screen, which has no hover, it is always there, faint, and solid while a finger is on the timeline.
   The data is what the server's replay pass keeps under a record's `action` (one letter for each count of each kind in every few seconds: ants_server/history_action.hpp); a record without it (a match counted before it
   existed) gets a curve of its deaths alone, from its `deaths` field (nine letters for each ant: ants_server/history_deaths.hpp), and a record with neither gets no curve. The data came from a server and is trusted for
   nothing: a letter that is not one of the 64, a length that is not the bucket count and a number out of range make no curve (the timeline stays as it was), every number is checked, and nothing is put in as markup
   (no innerHTML anywhere). The page reads only; nothing here reaches the game or the recording. Plain functions with no page in them where they can be (tests/scripts/web_activity_check.js runs them, and the curve on a
   small fake of the DOM, with node). */
(function (root, factory) {
    'use strict';
    var api = factory(root);
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsActivity = api;
})(typeof self !== 'undefined' ? self : this, function (root) {
    'use strict';
    var NS = 'http://www.w3.org/2000/svg';
    var LETTERS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
    var KINDS = ['death', 'bomb', 'theft', 'fire', 'hit'];                                     // the keys of the field, in its order
    var WEIGHTS = { death: 14, bomb: 10, theft: 6, fire: 5, hit: 2 };                         // the Auto camera's: kDeath, kBomb, kTheft, kFire, kHit (include/ants_app/auto_camera.hpp)
    var NAMES = { death: ['death', 'deaths'], bomb: ['bomb', 'bombs'], theft: ['theft', 'thefts'], fire: ['fire', 'fires'] };
    var BASE = 5, MAX_BUCKETS = 240, MAX_SECONDS = 5 * 1048576, MAX_COUNT = 63, MAX_ANTS = 400, MAX_TOTAL = 4294967296 / 20;
    var KERNEL = [3, 2, 1], POWER = 0.8, TOP = 12, FIGHT = 3, WIDTH = 1000;                    // the blur (a peak is a hill, not a needle), how the height grows, the room above the highest point (percent), the hits that are "fighting"

    function whole(x, lo, hi) { return typeof x === 'number' && isFinite(x) && Math.floor(x) === x && x >= lo && x <= hi; }
    function obj(x) { return x && typeof x === 'object' && !Array.isArray(x) ? x : {}; }
    function clock(seconds) { var s = Math.max(0, Math.floor(typeof seconds === 'number' && isFinite(seconds) ? seconds : 0)); return Math.floor(s / 60) + ':' + (s % 60 < 10 ? '0' : '') + (s % 60); }
    function letter(text, at) { var c = text.charAt(at); return c === '' ? -1 : LETTERS.indexOf(c); }

    // ---- reading the data ----
    // The buckets that a match of `total` seconds has: 5 seconds each, and 5 times a power of two (10, 20 ...) for the smallest one that makes at most MAX_BUCKETS of them (the server's rule, history_action.cpp)
    function shape(total) {
        var base = Math.max(1, Math.ceil(total / BASE)), f = 1;
        while (Math.ceil(base / f) > MAX_BUCKETS) f *= 2;
        return { seconds: BASE * f, buckets: Math.ceil(base / f) };
    }
    // The field `action` of a record as { seconds, buckets, counts: { death: [..], ... } }; null for anything that is not a field of version 1
    function actionOf(a) {
        a = obj(a);
        if (a.v !== 1 || !whole(a.s, BASE, MAX_SECONDS) || a.s % BASE !== 0 || !whole(a.n, 1, MAX_BUCKETS)) return null;
        var counts = {};
        for (var k = 0; k < KINDS.length; k++) {
            var text = a[KINDS[k]], list = [];
            if (typeof text !== 'string' || text.length !== a.n) return null;
            for (var i = 0; i < text.length; i++) {
                var v = letter(text, i);
                if (v < 0) return null;
                list.push(v);
            }
            counts[KINDS[k]] = list;
        }
        return { seconds: a.s, buckets: a.n, counts: counts };
    }
    // The deaths of a record (the field `deaths`, one by one: k "x") counted in buckets, as an action with the deaths alone; null for squares (no second), a damaged field or none
    function deathsOf(d, total) {
        d = obj(d);
        if (d.v !== 1 || d.k !== 'x' || typeof d.d !== 'string' || d.d.length === 0 || d.d.length % 9 !== 0 || d.d.length > 9 * MAX_ANTS) return null;
        var sh = shape(total), death = [], i;
        for (i = 0; i < sh.buckets; i++) death.push(0);
        for (var at = 0; at < d.d.length; at += 9) {
            var a = letter(d.d, at + 4), b = letter(d.d, at + 5), c = letter(d.d, at + 6);
            if (a < 0 || b < 0 || c < 0) return null;
            var bucket = Math.min(sh.buckets - 1, Math.floor((a + 64 * b + 4096 * c) / sh.seconds));
            death[bucket] = Math.min(MAX_COUNT, death[bucket] + 1);
        }
        var zero = []; for (i = 0; i < sh.buckets; i++) zero.push(0);
        return { seconds: sh.seconds, buckets: sh.buckets, counts: { death: death, bomb: zero.slice(), theft: zero.slice(), fire: zero.slice(), hit: zero.slice() }, deathsOnly: true };
    }
    // What a record gives the curve: { seconds, buckets, counts, total (the match's seconds), weight: [by bucket], deathsOnly }; null when there is nothing to draw
    function seriesOf(rec) {
        rec = obj(rec);
        var total = whole(rec.seconds, 1, MAX_TOTAL) ? rec.seconds : (whole(rec.turns, 1, 4294967295) ? Math.floor(rec.turns / 20) : 0);
        var s = actionOf(rec.action);
        if (!s) s = deathsOf(rec.deaths, total || 5);
        if (!s) return null;
        if (!(total > 0)) total = s.buckets * s.seconds;
        var weight = [], any = false;
        for (var i = 0; i < s.buckets; i++) {
            var w = 0;
            for (var k = 0; k < KINDS.length; k++) w += WEIGHTS[KINDS[k]] * s.counts[KINDS[k]][i];
            weight.push(w);
            if (w > 0) any = true;
        }
        if (!any) return null;
        return { seconds: s.seconds, buckets: s.buckets, counts: s.counts, total: total, weight: weight, deathsOnly: !!s.deathsOnly };
    }

    // ---- the curve ----
    // A small blur by the kernel's weights, so that a peak is a hill and not a needle
    function smooth(a) {
        var out = [];
        for (var i = 0; i < a.length; i++) {
            var sum = 0, w = 0;
            for (var j = -KERNEL.length + 1; j < KERNEL.length; j++) {
                var x = a[i + j];
                if (x === undefined) continue;
                var kk = KERNEL[Math.abs(j)];
                sum += x * kk;
                w += kk;
            }
            out.push(sum / w);
        }
        return out;
    }
    // A smooth line through points that never overshoots (Fritsch and Carlson): the slopes at the points
    function slopes(xs, ys) {
        var n = xs.length, d = [], m = [], i;
        for (i = 0; i < n - 1; i++) d.push((ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]));
        m.push(d[0]);
        for (i = 1; i < n - 1; i++) m.push(d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2);
        m.push(d[n - 2]);
        for (i = 0; i < n - 1; i++) {
            if (d[i] === 0) { m[i] = 0; m[i + 1] = 0; continue; }
            var a = m[i] / d[i], b = m[i + 1] / d[i], h = a * a + b * b;
            if (h > 9) { var t = 3 / Math.sqrt(h); m[i] = t * a * d[i]; m[i + 1] = t * b * d[i]; }
        }
        return m;
    }
    function fix(n) { return n.toFixed(2); }
    function pathOf(xs, ys, m) {
        var p = 'M' + fix(xs[0]) + ',' + fix(ys[0]);
        for (var i = 0; i < xs.length - 1; i++) {
            var dx = (xs[i + 1] - xs[i]) / 3;
            p += ' C' + fix(xs[i] + dx) + ',' + fix(ys[i] + m[i] * dx) + ' ' + fix(xs[i + 1] - dx) + ',' + fix(ys[i + 1] - m[i + 1] * dx) + ' ' + fix(xs[i + 1]) + ',' + fix(ys[i + 1]);
        }
        return p;
    }
    // The curve of a series in a box WIDTH wide and 100 high (y grows downward: 100 is no action, TOP the most): { line, area, at(fraction) -> y }
    function curveOf(series) {
        var sm = smooth(series.weight), top = Math.max.apply(null, sm);
        if (!(top > 0)) return null;
        var xs = [0], ys = [], i, last = series.total > 0 ? series.total : series.buckets * series.seconds;
        for (i = 0; i < sm.length; i++) {
            xs.push(WIDTH * Math.min((i + 0.5) * series.seconds, last) / last);
            ys.push(100 - Math.pow(sm[i] / top, POWER) * (100 - TOP));
        }
        ys.unshift(ys[0]);
        xs.push(WIDTH);
        ys.push(ys[ys.length - 1]);
        for (i = 1; i < xs.length; i++) if (xs[i] <= xs[i - 1]) xs[i] = xs[i - 1] + 1e-6;       // (the last bucket may be short: its middle is the end; the points go on in order)
        var m = slopes(xs, ys), line = pathOf(xs, ys, m);
        return {
            line: line, area: line + ' L' + WIDTH + ',100 L0,100 Z',
            at: function (fraction) {                                                              // the line's height at a place along it (a cubic of the same slopes)
                var x = Math.max(0, Math.min(1, fraction)) * WIDTH, k = 0;
                while (k < xs.length - 2 && x > xs[k + 1]) k++;
                var h = xs[k + 1] - xs[k], t = (x - xs[k]) / h, t2 = t * t, t3 = t2 * t;
                return (2 * t3 - 3 * t2 + 1) * ys[k] + (t3 - 2 * t2 + t) * h * m[k] + (-2 * t3 + 3 * t2) * ys[k + 1] + (t3 - t2) * h * m[k + 1];
            }
        };
    }

    // ---- the label ----
    // "5:12 · 3 deaths, 1 bomb": the time and what the bucket of that moment holds (a quiet bucket says so; a bucket with only fights says "fighting" when there were enough of them)
    function tipOf(series, seconds) {
        var t = Math.max(0, Math.min(series.total, seconds)), i = Math.min(series.buckets - 1, Math.floor(t / series.seconds)), parts = [];
        Object.keys(NAMES).forEach(function (kind) {
            var n = series.counts[kind][i];
            if (n > 0) parts.push(n + ' ' + NAMES[kind][n === 1 ? 0 : 1]);
        });
        if (!parts.length) parts.push(series.deathsOnly ? 'no deaths' : series.counts.hit[i] >= FIGHT ? 'fighting' : 'quiet');
        return { time: clock(t), what: parts.join(', ') };
    }

    // ---- the page ----
    // env = { document, rail, bar, tl, series, touch (a function: does the page have a finger or no hover?) }. Puts the curve into the timeline's rail and lets it show: on a computer while the pointer is over `tl`, on a
    // touch screen always (faint) and solid while a finger is on it. Returns { played(percent) } (the part of the match played is drawn in color), or null when the series has no curve.
    function attach(env) {
        var doc = env.document, series = env.series, curve = series ? curveOf(series) : null;
        if (!curve || !env.rail || !env.tl) return null;
        function el(tag, cls) { var n = doc.createElement(tag); if (cls) n.className = cls; return n; }
        function svg(tag, attrs) { var n = doc.createElementNS(NS, tag); for (var k in attrs) n.setAttribute(k, attrs[k]); return n; }
        var heat = el('div', 'act'), line = el('i', 'act-line'), dot = el('i', 'act-dot'), tip = el('div', 'act-tip'), tb = el('b'), tw = doc.createTextNode('');
        var box = svg('svg', { viewBox: '0 0 ' + WIDTH + ' 100', preserveAspectRatio: 'none', focusable: 'false' });
        var defs = svg('defs', {}), cp = svg('clipPath', { id: 'act-played-clip' }), cu = svg('clipPath', { id: 'act-rest-clip' });
        var rp = svg('rect', { x: 0, y: -10, width: 0, height: 120 }), ru = svg('rect', { x: 0, y: -10, width: WIDTH, height: 120 });
        cp.appendChild(rp); cu.appendChild(ru); defs.appendChild(cp); defs.appendChild(cu); box.appendChild(defs);
        box.appendChild(svg('path', { 'class': 'act-rest', d: curve.area, 'clip-path': 'url(#act-rest-clip)' }));
        box.appendChild(svg('path', { 'class': 'act-played', d: curve.area, 'clip-path': 'url(#act-played-clip)' }));
        box.appendChild(svg('path', { 'class': 'act-curve', d: curve.line }));
        heat.setAttribute('aria-hidden', 'true');
        tip.appendChild(tb); tip.appendChild(tw);
        heat.appendChild(box); heat.appendChild(line); heat.appendChild(dot); heat.appendChild(tip);
        env.rail.appendChild(heat);
        if (env.bar) env.bar.classList.add('has-activity');
        var pressed = false, inside = false;

        function touching() { try { return !!(env.touch && env.touch()); } catch (e) { return false; } }
        function paint() {
            var live = pressed || inside;                                                           // (the pointer is on the timeline, or a finger is: solid, with the line and the label)
            heat.classList.remove('on'); heat.classList.remove('faint'); heat.classList.remove('hov');
            if (live) { heat.classList.add('on'); heat.classList.add('hov'); } else if (touching()) heat.classList.add('faint');          // (at rest a computer shows nothing and a touch screen, which has no hover, shows it faint)
            if (env.bar) { if (live) env.bar.classList.add('acthov'); else env.bar.classList.remove('acthov'); }
        }
        function place(clientX) {
            var r = env.rail.getBoundingClientRect(), w = r.width > 0 ? r.width : 1, x = Math.max(0, Math.min(w, clientX - r.left)), fraction = x / w, tp = tipOf(series, fraction * series.total);
            line.style.left = x + 'px'; dot.style.left = x + 'px'; dot.style.top = curve.at(fraction) + '%';
            tb.textContent = tp.time; tw.textContent = ' \u00b7 ' + tp.what;
            var half = tip.offsetWidth ? tip.offsetWidth / 2 : 60;
            tip.style.left = Math.max(half - 11, Math.min(w + 11 - half, x)) + 'px';              // (the label stays inside the bar, a little past the rail's ends)
        }
        function mouseLike(ev) { return ev.pointerType !== 'touch'; }
        env.tl.addEventListener('pointerenter', function (ev) { if (!mouseLike(ev)) return; inside = true; place(ev.clientX); paint(); });
        env.tl.addEventListener('pointermove', function (ev) { if (pressed || (inside && mouseLike(ev))) place(ev.clientX); });
        env.tl.addEventListener('pointerleave', function (ev) { if (!mouseLike(ev)) return; inside = false; paint(); });
        env.tl.addEventListener('pointerdown', function (ev) { pressed = true; place(ev.clientX); paint(); });
        function release() { pressed = false; paint(); }
        env.tl.addEventListener('pointerup', release);
        env.tl.addEventListener('pointercancel', release);
        paint();
        return {
            played: function (percent) { var p = Math.max(0, Math.min(100, typeof percent === 'number' && isFinite(percent) ? percent : 0)) * WIDTH / 100; rp.setAttribute('width', String(p)); ru.setAttribute('x', String(p)); ru.setAttribute('width', String(WIDTH - p)); }
        };
    }

    // The page's one call (web/shell.html): the curve of a record in the page's own timeline (the elements rbar, tl and rail) and the handle that the page's paint() keeps up to date (window.antsReplayCurve). `win` is the
    // window: it has matchMedia and antsFinger (does the screen have no hover, or has a finger touched it) and is told with a resize when the bar got taller (the phone's band: the page measures its height again).
    function mount(win, rec) {
        var doc = win.document, bar = doc.getElementById('rbar'), before = bar ? bar.offsetHeight : 0;
        var handle = attach({
            document: doc, bar: bar, tl: doc.getElementById('tl'), rail: doc.getElementById('rail'), series: seriesOf(rec),
            touch: function () { return !!win.antsFinger || !!win.matchMedia('(hover: none)').matches; }          // (a window with no matchMedia throws here: the curve takes that for a computer)
        });
        win.antsReplayCurve = handle;
        if (handle && bar && bar.offsetHeight !== before && win.Event && win.dispatchEvent) win.dispatchEvent(new win.Event('resize'));
        return handle;
    }

    return { mount: mount, attach: attach, seriesOf: seriesOf, actionOf: actionOf, deathsOf: deathsOf, shape: shape, curveOf: curveOf, tipOf: tipOf, smooth: smooth, clock: clock, WEIGHTS: WEIGHTS, KINDS: KINDS, LETTERS: LETTERS };
});
