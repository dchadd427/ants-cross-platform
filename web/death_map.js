/* The map "Where ants died" (docs/REPLAYS.md "The map of deaths"): a card of the match page, a smaller one in the Stats panel of the replay page, and the same map added up over all the matches of a game map. It reads
   what the server's replay pass keeps under a record's `deaths` (a packed field of its own: base64url letters, nine for each ant, or eight for each square of tiles when a match had too many deaths to keep one by one),
   and draws the match's own map picture with a soft glow where many ants died (Heat) or a mark on the tile of each ant (Dots, and squares with their counts when the data is in squares); chips pick one team's ants, a switch
   hides the ants that drowned, two rings mark the first blood and the last kill, + and - and the mouse wheel and two fingers zoom, a drag moves, and a tap or a hover on a mark names the tile, the time, the kind of ant and
   who killed it. The field is data that came from a server and is trusted for nothing: a letter that is not one of the 64, a length that is not a whole number of entries, a number out of its range make no map (the card says
   so in one line), every position is put on the map, a name is put in as text, never as markup (no innerHTML anywhere), and the sizes of everything are capped. Plain functions with no page in them where they can be
   (tests/scripts/web_death_map_check.js runs them, and the cards on a small fake of the DOM, with node). */
(function (root, factory) {
    'use strict';
    var api = factory(root);
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsDeathMap = api;
})(typeof self !== 'undefined' ? self : this, function (root) {
    'use strict';
    var H = root.AntsHistory || (typeof require === 'function' ? require('./history_page.js') : null);
    var SEATS = ['Green', 'Red', 'Blue', 'Black'], SHAPES = ['circle', 'square', 'triangle', 'diamond'], TYPES = ['Worker', 'Bomber', 'Fire', 'Thief', 'Combat', 'Swimmer'], NS = 'http://www.w3.org/2000/svg';
    var FALLBACK = [[121, 191, 147], [212, 112, 127], [143, 143, 214], [179, 175, 185]];
    var LETTERS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
    var MAX_ANTS = 400, MAX_CELLS = 4000, MAX_SIDE = 4096, MAX_TOTAL = 100000000, MAX_MATCHES = 1000000, HUGE = 256, MODE_KEY = 'ants.deathmap.mode';

    // ---- reading the field ----
    function whole(x, lo, hi) { return typeof x === 'number' && isFinite(x) && Math.floor(x) === x && x >= lo && x <= hi; }
    function obj(x) { return x && typeof x === 'object' && !Array.isArray(x) ? x : {}; }
    function clock(seconds) { var s = Math.max(0, Math.floor(typeof seconds === 'number' && isFinite(seconds) ? seconds : 0)); return Math.floor(s / 60) + ':' + (s % 60 < 10 ? '0' : '') + (s % 60); }
    function n0(n) { return H.numberText(n); }
    function plural(n, one, many) { return n0(n) + ' ' + (n === 1 ? one : many); }
    // Text from a server or a player, for the page: no control, zero-width or bidirectional character, at most `max` characters
    function clean(s, max) { return typeof s === 'string' ? s.replace(/[\u0000-\u001f\u007f-\u009f\u00ad\u200b-\u200f\u2028-\u202e\u2060-\u206f\ufeff\ufff9-\ufffb]/g, '').slice(0, max || 40) : ''; }
    function shortName(s) { return String(s).replace(/ \(bot, [A-Za-z]+\)$/, ''); }
    // `n` letters of `text` from `at`, low letter first: the number, or -1 for a letter that is not one of the 64
    function take(text, at, n) {
        var v = 0;
        for (var i = 0; i < n; i++) {
            var k = LETTERS.indexOf(text.charAt(at + i));
            if (k < 0 || text.charAt(at + i) === '') return -1;
            v += k * Math.pow(64, i);
        }
        return v;
    }
    // One ant of nine letters, or null; its position is put on the map (w x h)
    function antAt(text, at, w, h) {
        var x = take(text, at, 2), y = take(text, at + 2, 2), t = take(text, at + 4, 3), f = take(text, at + 7, 2);
        if (x < 0 || y < 0 || t < 0 || f < 0) return null;
        var type = (f >> 2) & 7, killer = (f >> 6) & 7;
        if (type > 5 || (killer > 3 && killer !== 7)) return null;
        return { x: Math.min(x, w - 1), y: Math.min(y, h - 1), t: t, team: f & 3, type: type, drowned: (f & 32) !== 0, killer: killer === 7 ? -1 : killer };
    }
    // The field of a record (`deaths`, a parsed object) as a model, or null when it is not a field of this version: { w, h, total, of, side, squares, ants, cells, first, last, matches, map }
    function decode(d) {
        d = obj(d);
        if (d.v !== 1 || !whole(d.w, 1, MAX_SIDE) || !whole(d.h, 1, MAX_SIDE) || !whole(d.n, 1, MAX_TOTAL) || typeof d.d !== 'string') return null;
        var m = { w: d.w, h: d.h, total: 0, of: d.n, side: 1, squares: false, ants: [], cells: [], first: null, last: null, matches: whole(d.matches, 1, MAX_MATCHES) ? d.matches : 0, map: clean(d.map, 64) }, text = d.d, i;
        if (d.k === 'x') {
            if (text.length % 9 !== 0 || text.length > 9 * MAX_ANTS) return null;
            for (i = 0; i < text.length; i += 9) {
                var a = antAt(text, i, m.w, m.h);
                if (!a) return null;
                m.ants.push(a);
                if (a.killer >= 0) { if (!m.first) m.first = a; m.last = a; }
            }
            m.total = m.ants.length;
        } else if (d.k === 'b') {
            if (!whole(d.s, 1, MAX_SIDE) || text.length % 8 !== 0 || text.length > 8 * MAX_CELLS) return null;
            m.squares = true; m.side = d.s;
            var cx = Math.floor((m.w - 1) / m.side), cy = Math.floor((m.h - 1) / m.side);
            for (i = 0; i < text.length; i += 8) {
                var x = take(text, i, 2), y = take(text, i + 2, 2), f = take(text, i + 4, 1), c = take(text, i + 5, 3);
                if (x < 0 || y < 0 || f < 0 || f > 7 || c < 1) return null;
                m.cells.push({ x: Math.min(x, cx), y: Math.min(y, cy), team: f & 3, drowned: (f & 4) !== 0, count: c });
                m.total += c;
            }
            if (typeof d.f === 'string' && d.f.length === 9) m.first = antAt(d.f, 0, m.w, m.h);
            if (typeof d.l === 'string' && d.l.length === 9) m.last = antAt(d.l, 0, m.w, m.h);
            if (m.first && m.first.killer < 0) m.first = null;       // (the first blood and the last kill are kills: a team is to blame)
            if (m.last && m.last.killer < 0) m.last = null;
        } else return null;
        if (m.total < 1) return null;
        if (m.of < m.total) m.of = m.total;           // (the text holds more than the header says: the text is what is drawn)
        return m;
    }
    // Whether a model is one that gets colored squares and not the glow first: a very big map, or a match kept in squares
    function isHuge(m) { return m.squares || Math.max(m.w, m.h) > HUGE; }

    // What is shown: the ants (or squares) of one team (st.team -1: all) without the ones that drowned when st.drowned is false; the count of each team under the same switch
    function select(m, st) {
        var by = [0, 0, 0, 0], ants = [], cells = [], total = 0;
        m.ants.forEach(function (a) { if (a.drowned && st.drowned === false) return; by[a.team]++; if (st.team < 0 || a.team === st.team) { ants.push(a); total++; } });
        m.cells.forEach(function (c) { if (c.drowned && st.drowned === false) return; by[c.team] += c.count; if (st.team < 0 || c.team === st.team) { cells.push(c); total += c.count; } });
        return { ants: ants, cells: cells, by: by, total: total, all: by[0] + by[1] + by[2] + by[3] };
    }
    // The points of the glow and of the busiest place: [x, y, weight] in tiles (the center of a tile or of a square)
    function pointsOf(m, sel) {
        if (m.squares || m.side > 1) return sel.cells.map(function (c) { return [c.x * m.side + (m.side - 1) / 2, c.y * m.side + (m.side - 1) / 2, c.count]; });
        return sel.ants.map(function (a) { return [a.x, a.y, 1]; }).concat(sel.cells.map(function (c) { return [c.x, c.y, c.count]; }));
    }
    // The busiest place: the point with the most ants within `r` tiles (a box), found with a grid of buckets, not by looking at every pair
    function busiest(points, r) {
        var grid = {}, best = null;
        points.forEach(function (p) { var k = Math.floor(p[0] / r) + ',' + Math.floor(p[1] / r); (grid[k] = grid[k] || []).push(p); });
        points.forEach(function (p) {
            var bx = Math.floor(p[0] / r), by = Math.floor(p[1] / r), n = 0;
            for (var dx = -1; dx <= 1; dx++) for (var dy = -1; dy <= 1; dy++) (grid[(bx + dx) + ',' + (by + dy)] || []).forEach(function (q) { if (Math.abs(q[0] - p[0]) <= r && Math.abs(q[1] - p[1]) <= r) n += q[2]; });
            if (!best || n > best.n) best = { x: Math.round(p[0]), y: Math.round(p[1]), n: n };
        });
        return best;
    }
    function radiusOf(m) { return Math.max(2, Math.round(m.w / 30), m.squares ? m.side : 0); }

    // ---- colors ----
    function mix(a, b, t) { return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]; }
    function css(c) { return 'rgb(' + Math.round(c[0]) + ',' + Math.round(c[1]) + ',' + Math.round(c[2]) + ')'; }
    // the colors of the glow: all teams = the site's red, orange, gold, cream; one team = that team's color from dark to light (rgb: the team's color)
    function ramp(team, rgb) {
        if (team < 0) return [[0, [157, 13, 23]], [0.3, [219, 75, 19]], [0.62, [246, 208, 88]], [1, [255, 244, 207]]];
        var c = rgb[team] || FALLBACK[team];
        return [[0, mix(c, [0, 0, 0], 0.55)], [0.35, mix(c, [0, 0, 0], 0.1)], [0.7, c], [1, mix(c, [255, 255, 255], 0.78)]];
    }
    function rampAt(r, v) { for (var i = 1; i < r.length; i++) if (v <= r[i][0]) return mix(r[i - 1][1], r[i][1], (v - r[i - 1][0]) / (r[i][0] - r[i - 1][0])); return r[r.length - 1][1]; }
    function rampCss(team, rgb) { var r = ramp(team, rgb), st = []; for (var i = 0; i <= 10; i++) st.push(css(rampAt(r, i / 10)) + ' ' + (i * 10) + '%'); return 'linear-gradient(90deg, #0d171f 0%, ' + st.join(', ') + ')'; }
    // The glow: the sum of a soft round blob at every point on a canvas of `ppt` pixels a tile (at most 2,400 pixels a side), as floats; heatPixels turns it into colors with an alpha that is 0 where nothing died
    function heatField(points, w, h, ppt, sigma) {
        var cw = Math.max(1, Math.min(2400, Math.round(w * ppt))), ch = Math.max(1, Math.min(2400, Math.round(h * ppt))), f = new Float32Array(cw * ch), s = Math.max(0.5, sigma * ppt), R = Math.min(600, Math.ceil(s * 2.6)), kern = new Float32Array(R + 1), i;
        for (i = 0; i <= R; i++) kern[i] = Math.exp(-(i * i) / (2 * s * s));           // (the blob is round: its row and its column are one table, a point is put on a whole pixel)
        points.forEach(function (p) {
            var px = Math.round((p[0] + 0.5) * cw / w), py = Math.round((p[1] + 0.5) * ch / h), x0 = Math.max(0, px - R), x1 = Math.min(cw - 1, px + R), y0 = Math.max(0, py - R), y1 = Math.min(ch - 1, py + R);
            for (var y = y0; y <= y1; y++) { var ky = p[2] * kern[Math.abs(y - py)], row = y * cw; for (var x = x0; x <= x1; x++) f[row + x] += ky * kern[Math.abs(x - px)]; }
        });
        return { cw: cw, ch: ch, data: f };
    }
    function heatPixels(field, team, rgb, top) {
        var f = field.data, max = 0, i;
        for (i = 0; i < f.length; i++) if (f[i] > max) max = f[i];
        var cap = Math.max(max, top || 2.2), r = ramp(team, rgb), out = new Uint8ClampedArray(f.length * 4);
        for (i = 0; i < f.length; i++) {
            var v = Math.min(1, f[i] / cap), a = Math.max(0, Math.min(1, (v - 0.035) / 0.22)) * 0.92, c = rampAt(r, Math.pow(v, 0.8));
            out[i * 4] = c[0]; out[i * 4 + 1] = c[1]; out[i * 4 + 2] = c[2]; out[i * 4 + 3] = Math.round(a * 255);
        }
        return out;
    }
    function tickStep(span) { return span <= 14 ? 2 : span <= 34 ? 5 : span <= 84 ? 10 : span <= 220 ? 25 : span <= 600 ? 50 : 250; }
    // The picture of a map for the card: the original's own, else the picker's (when the page has one); '' for none (the dark ground stays)
    function pictureUrl(map, env) {
        env = env || {};
        var key = env.replay && env.replay.mapKey ? env.replay.mapKey(map) : '';
        if (key) return 'front/preview_' + key + '.png';
        return env.picker && env.picker.thumbFor ? env.picker.thumbFor(map, env.maps ? env.maps() : null) || '' : '';
    }
    function modeFor(m, saved) { return isHuge(m) ? 'dots' : saved === 'dots' ? 'dots' : 'heat'; }

    // ---- the card ----
    // doc: the page's document (or a fake); getStyle(name): a css value of the page; env: { replay, picker, maps (a function), storage, sum (a function: map name -> Promise of the parsed sum or null), compact }
    function create(doc, getStyle, env) {
        env = env || {};
        var rgbs = null;
        function rgb() {
            if (!rgbs) rgbs = SEATS.map(function (c, i) {
                var v = (getStyle ? getStyle('--c-' + c.toLowerCase()) : '') || '', m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(String(v).trim());
                return m ? [parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16)] : FALLBACK[i];
            });
            return rgbs;
        }
        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function sv(tag, attrs, parent) { var n = doc.createElementNS(NS, tag); Object.keys(attrs || {}).forEach(function (k) { n.setAttribute(k, attrs[k]); }); if (parent) parent.appendChild(n); return n; }
        function empty(n) { while (n.firstChild) n.removeChild(n.firstChild); }
        function saved() { try { return env.storage ? env.storage.getItem(MODE_KEY) : null; } catch (e) { return null; } }
        function keep(mode) { try { if (env.storage) env.storage.setItem(MODE_KEY, mode); } catch (e) { /* the choice is not remembered */ } }
        function mark(seat) { var m = el('span', 'mk ' + SHAPES[seat]); m.setAttribute('data-c', SEATS[seat]); return m; }
        function shape(kind, x, y, r, seat, sw, parent) {      // a seat's shape on the map, in tile units
            var n;
            if (kind === 'circle') n = sv('circle', { cx: x, cy: y, r: r });
            else if (kind === 'square') n = sv('rect', { x: x - r * 0.88, y: y - r * 0.88, width: r * 1.76, height: r * 1.76 });
            else if (kind === 'triangle') n = sv('polygon', { points: [x, y - r * 1.12, x + r * 1.08, y + r * 0.85, x - r * 1.08, y + r * 0.85].join(',') });
            else n = sv('polygon', { points: [x, y - r * 1.2, x + r * 1.2, y, x, y + r * 1.2, x - r * 1.2, y].join(',') });
            n.setAttribute('class', 'mkp'); n.setAttribute('data-c', SEATS[seat]); n.setAttribute('stroke-width', sw);
            if (parent) parent.appendChild(n);
            return n;
        }
        function seatName(ctx, seat) { return clean(ctx.by[seat] ? H.seatLabel(ctx.by[seat]) : SEATS[seat], 40) || SEATS[seat]; }
        function contextOf(rec, all) {
            var by = {};
            if (!all) H.seatsOf(rec).forEach(function (s) { by[s.seat] = s; });
            return { by: by, all: !!all };
        }
        function quiet(compact) {
            var s = el(compact ? 'div' : 'section', 'dm ' + (compact ? 'compact' : 'panel stat')); if (!compact) { s.id = 'deaths'; s.setAttribute('aria-labelledby', 'deaths-h'); var head = el('div', 'head'), h2 = el('h2', '', 'Where ants died'); h2.id = 'deaths-h'; head.appendChild(h2); s.appendChild(head); }
            var n = el('div', 'note'), p = el('p'); p.appendChild(doc.createTextNode('No map of deaths for this match.')); n.appendChild(p); s.appendChild(n);
            return s;
        }

        // The card of a record: `rec` is a match (its `deaths`), o.compact the small one of the replay page. null for a compact card that has no data (nothing is added to the Stats panel then).
        function card(rec, o) {
            o = o || {};
            var compact = !!o.compact, ctx = contextOf(rec, false), model = decode(obj(rec).deaths), map = typeof obj(rec).map === 'string' ? clean(rec.map, 64) : '';
            if (!model) return compact ? null : quiet(false);
            var sec = el(compact ? 'div' : 'section', 'dm ' + (compact ? 'compact' : 'panel stat')), st = { mode: modeFor(model, saved()), team: -1, first: true, last: true, drowned: true, zoom: null, pick: null, scope: 'match', sum: null, sumState: '' };
            if (!compact) { sec.id = 'deaths'; sec.setAttribute('aria-labelledby', 'deaths-h'); }
            var draw = function () {}, tags = [];
            function active() { return st.scope === 'all' && st.sum ? st.sum : model; }
            function chipName(seat) { return shortName(st.scope === 'all' ? SEATS[seat] : seatName(ctx, seat)); }
            function render() {
                empty(sec); tags = [];
                sec.setAttribute('class', 'dm ' + (compact ? 'compact' : 'panel stat') + ' ' + st.mode);
                var m = active(), isAll = m !== model, sel = select(m, st);
                // the head: the title and how many died
                var count;
                if (isAll) count = plural(sel.total, 'ant', 'ants') + ' died in ' + plural(m.matches || 1, 'match', 'matches');
                else if (st.team < 0) count = plural(sel.total, 'ant', 'ants') + ' died';
                else count = n0(sel.total) + ' of ' + chipName(st.team) + '\u2019s ants died (of ' + n0(sel.all) + ')';
                if (st.drowned === false) count += ', not counting the ants that drowned';
                if (!isAll && m.of > m.total) count += ' (showing ' + n0(m.total) + ' of ' + n0(m.of) + ')';
                if (compact) { var h3 = el('h3', '', 'Where ants died'); h3.appendChild(el('span', '', count)); sec.appendChild(h3); }
                else { var head = el('div', 'head'), h2 = el('h2', '', 'Where ants died'); h2.id = 'deaths-h'; head.appendChild(h2); head.appendChild(el('span', 'count', count)); sec.appendChild(head); }
                var body = el('div', 'dm-body'), col = el('div', 'dm-mapcol'), side = el('div', 'dm-side');
                // the map with its rulers
                var rt = el('div', 'dm-ruler-top'), mw = el('div', 'dm-mapwrap'), rl = el('div', 'dm-ruler-left'), stage = el('div', 'dm-stage'), world = el('div', 'dm-world');
                stage.style.setProperty('--ar', m.w + ' / ' + m.h); stage.setAttribute('tabindex', '0'); stage.setAttribute('role', 'group');
                stage.setAttribute('aria-label', 'Map of where ants died. ' + count + '. Plus and minus zoom, the arrow keys move.');
                var url = pictureUrl(map, env);
                if (url) { var img = el('img', 'dm-img'); img.alt = ''; img.addEventListener('error', function () { if (img.parentNode) img.parentNode.removeChild(img); }); img.src = url; world.appendChild(img); }
                var cv = el('canvas'); world.appendChild(cv);
                var pts = pointsOf(m, sel);
                if (st.mode === 'heat') {              // the glow is made once for a view of the data (zooming only moves the canvas)
                    var ctx2 = null;
                    try { ctx2 = cv.getContext ? cv.getContext('2d') : null; } catch (e) { ctx2 = null; }
                    if (ctx2 && typeof ctx2.createImageData === 'function') {
                        var per = Math.min(12, Math.max(2, Math.floor(1100 / Math.max(m.w, m.h)))), field = heatField(pts, m.w, m.h, per, Math.max(1.4, m.w * 0.028) * (m.side > 1 ? 1.1 : 1));
                        cv.width = field.cw; cv.height = field.ch;
                        var img2 = ctx2.createImageData(field.cw, field.ch);
                        img2.data.set(heatPixels(field, st.team, rgb(), 2.2)); ctx2.putImageData(img2, 0, 0);
                    }
                }
                var svg = sv('svg', { viewBox: '0 0 ' + m.w + ' ' + m.h, preserveAspectRatio: 'none' }); world.appendChild(svg);
                stage.appendChild(world); mw.appendChild(rl); mw.appendChild(stage); col.appendChild(rt); col.appendChild(mw);
                var zb = el('div', 'dm-zoom');
                [['+', 'Zoom in', function () { zoomBy(1.6); }], ['\u2212', 'Zoom out', function () { zoomBy(1 / 1.6); }], ['Fit', 'Show the whole map', function () { st.zoom = null; st.pick = null; draw(); }]].forEach(function (b) {
                    var bt = el('button', b[0] === 'Fit' ? 'fit' : '', b[0]); bt.type = 'button'; bt.title = b[1]; bt.setAttribute('aria-label', b[1]); bt.addEventListener('click', b[2]); zb.appendChild(bt);
                });
                var zl = el('div', 'zl'); zb.appendChild(zl); stage.appendChild(zb);
                // the side: which matches (when the sum is offered), whose ants, how to show, what to mark, the legend, the facts
                if (!compact && env.sum) {
                    var gs = el('div'), seg0 = el('div', 'seg'); seg0.setAttribute('role', 'group'); seg0.setAttribute('aria-label', 'Which matches');
                    [['match', 'This match'], ['all', 'All matches on this map']].forEach(function (x) {
                        var b = el('button', '', x[1]); b.type = 'button'; b.setAttribute('aria-pressed', String(st.scope === x[0])); b.addEventListener('click', function () { pickScope(x[0]); }); seg0.appendChild(b);
                    });
                    gs.appendChild(el('span', 'lab', 'Show')); gs.appendChild(seg0);
                    if (st.sumState === 'loading') gs.appendChild(el('p', 'dm-hint', 'Adding up the matches on this map\u2026'));
                    if (st.sumState === 'none') gs.appendChild(el('p', 'dm-hint', 'No other matches on this map have a map of deaths yet.'));
                    side.appendChild(gs);
                }
                var g1 = el('div'), chips = el('div', 'dm-chips');
                g1.appendChild(el('span', 'lab', 'Whose ants'));
                function chip(team, label, n) {
                    var b = el('button', 'dm-chip'); b.type = 'button'; b.setAttribute('aria-pressed', String(st.team === team));
                    if (team >= 0) b.appendChild(mark(team));
                    b.appendChild(doc.createTextNode(label + ' ')); b.appendChild(el('b', '', n0(n)));
                    b.addEventListener('click', function () { st.team = team; st.pick = null; render(); }); chips.appendChild(b);
                }
                chip(-1, 'All teams', sel.all);
                for (var t = 0; t < 4; t++) if (isAll ? true : !!ctx.by[t] || sel.by[t] > 0) chip(t, chipName(t), sel.by[t]);
                g1.appendChild(chips); side.appendChild(g1);
                var g2 = el('div'), seg = el('div', 'seg'); seg.setAttribute('role', 'group'); seg.setAttribute('aria-label', 'Show as');
                [['heat', 'Heat'], ['dots', 'Dots']].forEach(function (x) {
                    var b = el('button', '', x[1]); b.type = 'button'; b.setAttribute('aria-pressed', String(st.mode === x[0])); b.addEventListener('click', function () { st.mode = x[0]; keep(x[0]); st.pick = null; render(); }); seg.appendChild(b);
                });
                g2.appendChild(el('span', 'lab', 'Show as')); g2.appendChild(seg); side.appendChild(g2);
                var anyDrowned = m.ants.some(function (a) { return a.drowned; }) || m.cells.some(function (x) { return x.drowned; });
                var showMarks = !compact && !isAll && (m.first || m.last);
                if (!compact && (showMarks || anyDrowned)) {
                    var g3 = el('div'), row = el('div', 'dm-row');
                    if (showMarks) [['first', 'First blood', ''], ['last', 'Last kill', ' last']].forEach(function (x) {
                        var b = el('button', 'dm-tog'); b.type = 'button'; b.setAttribute('aria-pressed', String(!!st[x[0]])); b.appendChild(el('span', 'ring' + x[2])); b.appendChild(doc.createTextNode(x[1]));
                        b.addEventListener('click', function () { st[x[0]] = !st[x[0]]; draw(); syncToggles(); }); b.setAttribute('data-mark', x[0]); row.appendChild(b);
                    });
                    if (anyDrowned) {
                        var d = el('button', 'dm-tog'); d.type = 'button'; d.setAttribute('aria-pressed', String(st.drowned)); d.setAttribute('data-drowned', '1'); d.appendChild(el('span', 'drop')); d.appendChild(doc.createTextNode('Ants that drowned'));
                        d.addEventListener('click', function () { st.drowned = !st.drowned; st.pick = null; render(); }); row.appendChild(d);
                    }
                    g3.appendChild(el('span', 'lab', showMarks ? 'Mark on the map' : 'Show on the map')); g3.appendChild(row); side.appendChild(g3);
                }
                function syncToggles() { var bs = row_buttons(); for (var i = 0; i < bs.length; i++) { var k = bs[i].getAttribute('data-mark'); if (k) bs[i].setAttribute('aria-pressed', String(!!st[k])); } }
                function row_buttons() { return side.querySelectorAll ? side.querySelectorAll('.dm-tog') : []; }
                // the legend
                var lg = el('div', 'dm-legend');
                if (st.mode === 'heat') {
                    lg.appendChild(el('span', 'lab', 'Deaths in a place')); var bar = el('div', 'bar'); bar.style.background = rampCss(st.team, rgb()); lg.appendChild(bar);
                    var ends = el('div', 'ends'); ends.appendChild(el('span', '', 'Few')); ends.appendChild(el('span', '', m.side > 1 ? 'Many in one square' : 'Many')); lg.appendChild(ends);
                } else if (!compact) {
                    lg.appendChild(el('span', 'lab', m.side > 1 || isAll ? 'One square is a place' : 'One dot is one ant')); var key = el('ul', 'key');
                    for (var q = 0; q < 4; q++) { if (st.team >= 0 && st.team !== q) continue; if (!isAll && !ctx.by[q]) continue; var li = el('li'); li.appendChild(mark(q)); li.appendChild(doc.createTextNode(chipName(q) + '\u2019s ants')); key.appendChild(li); }
                    lg.appendChild(key);
                }
                side.appendChild(lg);
                // the facts
                var near = !compact && pts.length ? busiest(pts, radiusOf(m)) : null;
                if (!compact) {
                    var dl = el('dl', 'dm-facts');
                    var fact = function (title, fill) { var dd = el('dd'); fill(dd); dl.appendChild(el('dt', '', title)); dl.appendChild(dd); };
                    if (near) fact('Busiest place', function (dd) { dd.textContent = (isAll ? plural(near.n, 'ant', 'ants') + ' in all those matches' : plural(near.n, 'ant', 'ants')) + ' died within ' + plural(radiusOf(m), 'tile', 'tiles') + ' of tile ' + near.x + ', ' + near.y + '.'; });
                    var line = function (title, a) {
                        if (!a || isAll || a.killer < 0) return;
                        fact(title, function (dd) { dd.appendChild(mark(a.killer)); dd.appendChild(doc.createTextNode(' ' + seatName(ctx, a.killer) + ' on ')); dd.appendChild(mark(a.team)); dd.appendChild(doc.createTextNode(' ' + seatName(ctx, a.team) + ' at ' + clock(a.t) + ', tile ' + a.x + ', ' + a.y)); });
                    };
                    line('First blood', m.first);
                    if (m.last && m.last !== m.first && !(m.first && m.last.t === m.first.t && m.last.x === m.first.x && m.last.y === m.first.y && m.last.team === m.first.team)) line('Last kill', m.last);
                    if (dl.firstChild) side.appendChild(dl);
                    side.appendChild(el('p', 'dm-hint', m.side > 1 ? 'The squares are ' + m.side + ' \u00d7 ' + m.side + ' tiles. Zoom in to see where in a square.' : isAll ? 'Every match on this map, one on top of the other. Switch to Dots to see single places.' : 'Zoom with + and \u2212 (or two fingers) and drag to move. Zoomed in far enough, each dot sits on its own tile. Tap a dot for its tile, time and who killed it.'));
                    if (m.squares && !isAll) { var nt = el('div', 'dm-note'); nt.appendChild(el('b', '', 'Big match. ')); nt.appendChild(doc.createTextNode('This match had ' + n0(m.of) + ' deaths, more than the page keeps one by one, so it keeps how many ants died in each square, and which team.')); side.appendChild(nt); }
                }
                body.appendChild(col); body.appendChild(side); sec.appendChild(body);

                // ---- drawing: needs the real size, so it runs once the card is in the page (redraw) ----
                function geometry() {
                    var sw = stage.clientWidth, sh = stage.clientHeight;
                    if (!(sw > 0) || !(sh > 0)) return null;
                    var s = st.zoom ? st.zoom.s : 1, ppt = sw / m.w, vw = m.w / s, vh = m.h / s;
                    return { sw: sw, sh: sh, s: s, ppt: ppt, vw: vw, vh: vh, vx: st.zoom ? Math.max(0, Math.min(m.w - vw, st.zoom.cx - vw / 2)) : 0, vy: st.zoom ? Math.max(0, Math.min(m.h - vh, st.zoom.cy - vh / 2)) : 0 };
                }
                function maxZoom(g) { return Math.max(8, Math.ceil(26 / g.ppt)); }
                function setZoom(s, cx, cy) {
                    var g = geometry(); if (!g) return;
                    s = Math.min(maxZoom(g), s);
                    st.zoom = s < 1.15 ? null : { s: s, cx: cx, cy: cy };
                    draw();
                }
                function zoomBy(f) { var g = geometry(); if (!g) return; var cx = g.vx + g.vw / 2, cy = g.vy + g.vh / 2; setZoom(g.s * f, cx, cy); }
                function zoomAt(f, clientX, clientY) {
                    var g = geometry(); if (!g) return;
                    var r = stage.getBoundingClientRect(), tx = (clientX - r.left) / (g.ppt * g.s) + g.vx, ty = (clientY - r.top) / (g.ppt * g.s) + g.vy, s2 = Math.min(maxZoom(g), g.s * f);
                    var vw2 = m.w / s2, vh2 = m.h / s2, fx = (clientX - r.left) / g.sw, fy = (clientY - r.top) / g.sh;
                    setZoom(s2, tx - fx * vw2 + vw2 / 2, ty - fy * vh2 + vh2 / 2);
                }
                var tt = null;
                draw = function () {
                    var g = geometry();
                    if (!g) return;
                    var u = 1 / (g.ppt * g.s), vx = g.vx, vy = g.vy, vw = g.vw, vh = g.vh, ppx = g.ppt * g.s, i;
                    world.style.transform = 'translate(' + (-vx * g.ppt * g.s) + 'px,' + (-vy * g.ppt * g.s) + 'px) scale(' + g.s + ')';
                    stage.setAttribute('class', 'dm-stage' + (st.zoom ? ' zoomed' : ''));
                    zl.textContent = st.zoom ? Math.round(g.s * 100) + '%' : '';
                    var step = tickStep(Math.max(vw, vh));
                    empty(rt); empty(rl);
                    for (var tx = Math.ceil(vx / step) * step; tx < vx + vw; tx += step) { var a = el('span', '', String(tx)); a.style.left = ((tx + 0.5 - vx) * ppx) + 'px'; rt.appendChild(a); }
                    for (var ty = Math.ceil(vy / step) * step; ty < vy + vh; ty += step) { var b = el('span', '', String(ty)); b.style.top = ((ty + 0.5 - vy) * ppx) + 'px'; rl.appendChild(b); }
                    rt.style.width = g.sw + 'px';
                    cv.style.display = st.mode === 'heat' ? '' : 'none';
                    empty(svg);
                    var tileMode = ppx >= 20 && m.side === 1 && !isAll, tiles = {};
                    if (ppx >= 20 && m.side === 1) {
                        var grid = sv('g', { stroke: 'rgba(255,255,255,.16)', 'stroke-width': u }, svg);
                        for (var gx = Math.floor(vx); gx <= Math.ceil(vx + vw); gx++) sv('line', { x1: gx, y1: vy, x2: gx, y2: vy + vh }, grid);
                        for (var gy = Math.floor(vy); gy <= Math.ceil(vy + vh); gy++) sv('line', { x1: vx, y1: gy, x2: vx + vw, y2: gy }, grid);
                    }
                    var inView = function (x, y, pad) { return !(x < vx - pad || x > vx + vw + pad || y < vy - pad || y > vy + vh + pad); };
                    if (st.mode === 'dots' && !m.squares && !isAll && m.side === 1) {
                        sel.ants.forEach(function (a) { var k = a.x + ',' + a.y; (tiles[k] = tiles[k] || []).push(a); });
                        var minPx = 4;       // on a very big map (a tile is under two pixels) a mark is a square of the team's color, never smaller than 4 pixels
                        Object.keys(tiles).forEach(function (k) {
                            var list = tiles[k], a = list[list.length - 1], x = a.x + 0.5, y = a.y + 0.5;
                            if (!inView(x, y, 2)) return;
                            var picked = st.pick && st.pick.kind === 'ant' && list.indexOf(st.pick.ant) >= 0;
                            if (ppx < 2.2) { sv('rect', { x: x - minPx * u / 2, y: y - minPx * u / 2, width: minPx * u, height: minPx * u, 'class': 'mkq', 'data-c': SEATS[a.team] }, svg); return; }
                            if (tileMode) sv('rect', { x: a.x + 0.04, y: a.y + 0.04, width: 0.92, height: 0.92, 'class': 'cell', 'data-c': SEATS[a.team], 'stroke-width': 1.6 * u }, svg);
                            var r = tileMode ? 0.3 : (compact ? 4.4 : 5.6) * u;
                            if (picked) sv('circle', { cx: x, cy: y, r: r * 2.1, fill: 'none', stroke: '#fff', 'stroke-width': 2 * u }, svg);
                            for (var j = 0; j < list.length - 1; j++) shape(SHAPES[list[j].team], x + 1.5 * u, y - 1.5 * u, r, list[j].team, 1.3 * u, svg);
                            var top = shape(SHAPES[a.team], x, y, r, a.team, 1.5 * u, svg), tl = sv('title', {}, top); tl.textContent = clean(shortName(seatName(ctx, a.team)), 40) + ' \u00b7 tile ' + a.x + ', ' + a.y + ' \u00b7 ' + clock(a.t);
                            if (list.length > 1) { var tx3 = sv('text', { x: x + r * 1.15, y: y - r * 1.1, 'font-size': 12 * u, 'stroke-width': 3 * u }, svg); tx3.textContent = '\u00d7' + list.length; }
                        });
                    }
                    if (st.mode === 'dots' && (m.squares || isAll || m.side > 1)) {      // squares with their count, the color of the team that lost most there
                        var merged = {}, mx = 0, sd = m.side;
                        sel.cells.forEach(function (c) { var k = c.x + ',' + c.y, e = merged[k] = merged[k] || { x: c.x, y: c.y, n: 0, by: [0, 0, 0, 0] }; e.n += c.count; e.by[c.team] += c.count; });
                        Object.keys(merged).forEach(function (k) { mx = Math.max(mx, merged[k].n); });
                        Object.keys(merged).forEach(function (k) {
                            var e = merged[k], lead = 0, px = e.x * sd, py = e.y * sd, wd = Math.min(sd, m.w - px), ht = Math.min(sd, m.h - py);
                            for (var t2 = 1; t2 < 4; t2++) if (e.by[t2] > e.by[lead]) lead = t2;
                            if (px + wd < vx || px > vx + vw || py + ht < vy || py > vy + vh) return;
                            var picked2 = st.pick && st.pick.kind === 'sq' && st.pick.key === k;
                            var r = sv('rect', { x: px + 0.05 * Math.min(1, sd), y: py + 0.05 * Math.min(1, sd), width: Math.max(0.1, wd - 0.1 * Math.min(1, sd)), height: Math.max(0.1, ht - 0.1 * Math.min(1, sd)), 'class': 'sq', 'data-c': SEATS[lead], 'fill-opacity': 0.2 + 0.6 * e.n / mx, 'stroke-width': (picked2 ? 2.6 : 1.3) * u }, svg);
                            var tl2 = sv('title', {}, r); tl2.textContent = plural(e.n, 'ant', 'ants') + ' \u00b7 tiles ' + px + ', ' + py;
                            if (e.n > 1 && sd * ppx > 17) { var t3 = sv('text', { x: px + wd / 2, y: py + ht / 2 + 4 * u, 'text-anchor': 'middle', 'font-size': 12 * u, 'stroke-width': 3 * u }, svg); t3.textContent = String(e.n); }
                        });
                    }
                    // the rings of the first blood and the last kill, with their tags
                    tags.forEach(function (tg) { if (tg.parentNode) tg.parentNode.removeChild(tg); }); tags = [];
                    function ring(a, kind, label) {
                        var x = a.x + 0.5, y = a.y + 0.5, rad = tileMode ? 0.9 : 11 * u;
                        if (!inView(x, y, 0)) return;
                        sv('circle', { cx: x, cy: y, r: rad, fill: 'none', 'class': 'ring-' + kind, 'stroke-width': 2.4 * u, 'stroke-dasharray': kind === 'first' ? '' : (4 * u) + ' ' + (3 * u) }, svg);
                        var tg = el('div', 'dm-tag' + (kind === 'last' ? ' last' : '')); tg.appendChild(el('b', '', label)); tg.appendChild(doc.createTextNode(clock(a.t)));
                        stage.appendChild(tg); tags.push(tg);
                        var px = (x - vx) * ppx, py = (y - vy) * ppx, wdt = tg.offsetWidth || 90, above = py > 40;
                        tg.style.top = (above ? py - 26 : py + 28) + 'px'; tg.style.left = Math.max(4, Math.min(g.sw - wdt - 4, px - wdt / 2)) + 'px';
                    }
                    if (showMarks) {
                        var visible = function (a) { return a && (st.team < 0 || a.team === st.team) && !(a.drowned && st.drowned === false); };
                        if (st.first && visible(m.first)) ring(m.first, 'first', 'First blood');
                        if (st.last && visible(m.last) && m.last !== m.first) ring(m.last, 'last', 'Last kill');
                    }
                    // the little map when zoomed in
                    var old = stage.querySelector ? stage.querySelector('.dm-mini') : null; if (old && old.parentNode) old.parentNode.removeChild(old);
                    if (st.zoom && url) {
                        var mini = el('div', 'dm-mini'), mi = el('img'), bx = el('i'); mi.alt = ''; mi.src = url; mini.appendChild(mi);
                        bx.style.left = (vx / m.w * 100) + '%'; bx.style.top = (vy / m.h * 100) + '%'; bx.style.width = (vw / m.w * 100) + '%'; bx.style.height = (vh / m.h * 100) + '%'; mini.appendChild(bx);
                        mini.style.setProperty('--ar', m.w + ' / ' + m.h); stage.appendChild(mini);
                    }
                    // the tooltip of the picked mark
                    if (tt && tt.parentNode) tt.parentNode.removeChild(tt);
                    tt = null;
                    if (st.pick) {
                        var t4 = el('div', 'dm-tt'), top2 = el('div', 't'), rw = el('div', 'r'), where, more = '';
                        if (st.pick.kind === 'ant') {
                            var p = st.pick.ant, same = (tiles[p.x + ',' + p.y] || [p]).length - 1;
                            top2.appendChild(mark(p.team)); top2.appendChild(doc.createTextNode(shortName(seatName(ctx, p.team)) + '\u2019s ant died'));
                            var how = p.drowned ? 'Drowned' + (p.killer >= 0 ? '; last hit by ' + shortName(seatName(ctx, p.killer)) : '') : (p.killer >= 0 ? shortName(seatName(ctx, p.killer)) : 'not known');
                            [['Tile', p.x + ', ' + p.y], ['Time', clock(p.t)], ['Ant', TYPES[p.type] + ' ant'], [p.drowned ? 'How' : 'Killed by', how]].forEach(function (r2) { rw.appendChild(el('span', '', r2[0])); rw.appendChild(el('span', '', r2[1])); });
                            if (same > 0) more = plural(same, 'more ant', 'more ants') + ' died on this tile';
                            where = [(p.x + 0.5 - vx) * ppx, (p.y + 0.5 - vy) * ppx];
                        } else {
                            var e2 = st.pick.sq, sd2 = m.side;
                            top2.appendChild(doc.createTextNode(plural(e2.n, 'ant', 'ants') + ' died here'));
                            var by2 = []; for (var t5 = 0; t5 < 4; t5++) if (e2.by[t5] > 0) by2.push(shortName(isAll ? SEATS[t5] : seatName(ctx, t5)) + ' ' + e2.by[t5]);
                            [['Tiles', e2.x * sd2 + ' to ' + Math.min(m.w, e2.x * sd2 + sd2) + ', ' + e2.y * sd2 + ' to ' + Math.min(m.h, e2.y * sd2 + sd2)], ['Whose', by2.join(', ')]].forEach(function (r3) { rw.appendChild(el('span', '', r3[0])); rw.appendChild(el('span', '', r3[1])); });
                            where = [(e2.x * sd2 + sd2 / 2 - vx) * ppx, (e2.y * sd2 + sd2 / 2 - vy) * ppx];
                        }
                        t4.appendChild(top2); t4.appendChild(rw); if (more) t4.appendChild(el('div', 'more', more));
                        tt = t4;
                        var below = (doc.defaultView ? doc.defaultView.innerWidth : 1000) <= 720 && !compact;
                        if (below) { t4.className += ' below'; col.appendChild(t4); } else stage.appendChild(t4);
                        if (!below) {
                            var tw = t4.offsetWidth || 232, th = t4.offsetHeight || 110, lx = where[0] + 18, ly = where[1] - th / 2;
                            if (lx + tw > g.sw - 6) lx = where[0] - 18 - tw;
                            t4.style.left = Math.max(6, lx) + 'px'; t4.style.top = Math.max(6, Math.min(g.sh - th - 6, ly)) + 'px';
                        }
                    }
                    // the stage is read by the pointer: a mark is found from where it was tapped
                    stage._geo = g;
                };
                draw();
                // what a tap finds: the nearest ant within 22 pixels (Dots, one by one), or the square under the finger (squares)
                function findAt(clientX, clientY) {
                    var g = stage._geo || geometry(); if (!g) return null;
                    var r = stage.getBoundingClientRect(), x = (clientX - r.left) / (g.ppt * g.s) + g.vx, y = (clientY - r.top) / (g.ppt * g.s) + g.vy, i;
                    if (st.mode !== 'dots') return null;
                    if (!m.squares && !isAll && m.side === 1) {
                        var best = null, bd = 1e9;
                        for (i = 0; i < sel.ants.length; i++) { var a = sel.ants[i], dd = Math.sqrt((a.x + 0.5 - x) * (a.x + 0.5 - x) + (a.y + 0.5 - y) * (a.y + 0.5 - y)) * g.ppt * g.s; if (dd < bd) { bd = dd; best = a; } }
                        return best && bd < 22 ? { kind: 'ant', ant: best } : null;
                    }
                    var sd = m.side, cx = Math.floor(x / sd), cy = Math.floor(y / sd), e = { x: cx, y: cy, n: 0, by: [0, 0, 0, 0] };
                    sel.cells.forEach(function (c) { if (c.x === cx && c.y === cy) { e.n += c.count; e.by[c.team] += c.count; } });
                    return e.n > 0 ? { kind: 'sq', key: cx + ',' + cy, sq: e } : null;
                }
                // pointers: a drag moves the map when it is zoomed, two fingers zoom, a tap picks a mark; the wheel zooms
                var down = {}, dragged = false, pinch = 0, last = null;
                stage.addEventListener('pointerdown', function (ev) {
                    down[ev.pointerId] = { x: ev.clientX, y: ev.clientY }; dragged = false; last = { x: ev.clientX, y: ev.clientY };
                    var ids = Object.keys(down); if (ids.length === 2) { var p1 = down[ids[0]], p2 = down[ids[1]]; pinch = Math.sqrt((p1.x - p2.x) * (p1.x - p2.x) + (p1.y - p2.y) * (p1.y - p2.y)); }
                    if (stage.setPointerCapture && ids.length === 1) { try { stage.setPointerCapture(ev.pointerId); } catch (e) { /* no capture: the drag still works inside */ } }
                });
                stage.addEventListener('pointermove', function (ev) {
                    if (!down[ev.pointerId]) return;
                    var ids = Object.keys(down), g = geometry(); if (!g) return;
                    down[ev.pointerId] = { x: ev.clientX, y: ev.clientY };
                    if (ids.length >= 2) {
                        var p1 = down[ids[0]], p2 = down[ids[1]], dist = Math.sqrt((p1.x - p2.x) * (p1.x - p2.x) + (p1.y - p2.y) * (p1.y - p2.y));
                        if (pinch > 0 && dist > 0 && Math.abs(dist - pinch) > 2) { zoomAt(dist / pinch, (p1.x + p2.x) / 2, (p1.y + p2.y) / 2); pinch = dist; dragged = true; }
                        return;
                    }
                    if (!last) return;
                    var dx = ev.clientX - last.x, dy = ev.clientY - last.y;
                    if (!dragged && Math.abs(dx) + Math.abs(dy) < 5) return;
                    dragged = true;
                    if (st.zoom) { st.zoom = { s: st.zoom.s, cx: g.vx + g.vw / 2 - dx / (g.ppt * g.s), cy: g.vy + g.vh / 2 - dy / (g.ppt * g.s) }; draw(); }
                    last = { x: ev.clientX, y: ev.clientY };
                });
                function up(ev) {
                    var was = down[ev.pointerId]; delete down[ev.pointerId]; pinch = 0;
                    if (!was || dragged || ev.type === 'pointercancel') { if (!Object.keys(down).length) dragged = false; return; }
                    var found = findAt(ev.clientX, ev.clientY); st.pick = found; draw();
                }
                stage.addEventListener('pointerup', up); stage.addEventListener('pointercancel', up);
                stage.addEventListener('wheel', function (ev) { ev.preventDefault(); zoomAt(ev.deltaY < 0 ? 1.25 : 0.8, ev.clientX, ev.clientY); }, { passive: false });
                stage.addEventListener('keydown', function (ev) {
                    var g = geometry(); if (!g) return;
                    var k = ev.key, mv = g.vw * 0.2;
                    if (k === '+' || k === '=') { zoomBy(1.6); ev.preventDefault(); }
                    else if (k === '-' || k === '_') { zoomBy(1 / 1.6); ev.preventDefault(); }
                    else if (k === '0') { st.zoom = null; draw(); ev.preventDefault(); }
                    else if (st.zoom && (k === 'ArrowLeft' || k === 'ArrowRight' || k === 'ArrowUp' || k === 'ArrowDown')) { st.zoom = { s: st.zoom.s, cx: g.vx + g.vw / 2 + (k === 'ArrowLeft' ? -mv : k === 'ArrowRight' ? mv : 0), cy: g.vy + g.vh / 2 + (k === 'ArrowUp' ? -mv : k === 'ArrowDown' ? mv : 0) }; draw(); ev.preventDefault(); }
                    else if (k === 'Escape' && st.pick) { st.pick = null; draw(); }
                });
            }
            function pickScope(scope) {
                if (scope === st.scope) return;
                st.team = -1; st.pick = null; st.zoom = null;
                if (scope === 'match') { st.scope = 'match'; st.sumState = ''; render(); if (sec.dmDraw) sec.dmDraw(); return; }
                if (st.sum) { st.scope = 'all'; st.sumState = ''; render(); sec.dmDraw(); return; }
                st.sumState = 'loading'; render();
                Promise.resolve(env.sum(map)).then(function (data) {
                    var sum = decode(data);
                    if (!sum || sum.w !== model.w || sum.h !== model.h) { st.sumState = 'none'; render(); sec.dmDraw(); return; }
                    st.sum = sum; st.scope = 'all'; st.sumState = ''; render(); sec.dmDraw();
                }).catch(function () { st.sumState = 'none'; render(); sec.dmDraw(); });
            }
            sec.dmDraw = function () { draw(); };
            sec.dmState = st;
            render();
            return sec;
        }
        return { card: card, quiet: quiet };
    }

    // A card is drawn for the size of its stage: after the page is built, when a hidden panel is shown, and again when the window changes size
    function redraw(rootNode) {
        var boxes = rootNode && rootNode.querySelectorAll ? rootNode.querySelectorAll('.dm') : [];
        for (var i = 0; i < boxes.length; i++) if (boxes[i].dmDraw) boxes[i].dmDraw();
    }

    return { create: create, redraw: redraw, decode: decode, select: select, pointsOf: pointsOf, busiest: busiest, radiusOf: radiusOf, isHuge: isHuge, modeFor: modeFor, heatField: heatField, heatPixels: heatPixels, ramp: ramp, rampAt: rampAt, rampCss: rampCss,
             tickStep: tickStep, pictureUrl: pictureUrl, clock: clock, clean: clean, shortName: shortName, take: take, LETTERS: LETTERS, MODE_KEY: MODE_KEY };
});
