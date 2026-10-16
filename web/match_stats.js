/* The stats sections of the match page (docs/REPLAYS.md "The match pages"): a band of highlights, "Over time" and "Activity". They read what the server's replay pass keeps under a record's `details`
   (`AntsHistory.detailsOf`: a plain object or nothing) and build a panel from the keys they know, or nothing when the keys are not there, so a match without them looks as it did before. `details` is data
   that came from a server: every number is read as a number (anything else is 0), every list as a list, a seat as 0 to 3, and a name is put in as text, never as markup. The charts are inline SVG, with
   no library: each seat has its colour and its own shape (circle, square, triangle, diamond), so that no chart depends on the colour alone.
   Plain functions with no page in them where they can be (tests/scripts/web_match_stats_check.js runs them, and the sections on a small fake of the DOM, with node). */
(function (root, factory) {
    'use strict';
    var api = factory(root);
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsMatchStats = api;
})(typeof self !== 'undefined' ? self : this, function (root) {
    'use strict';
    var H = root.AntsHistory || (typeof require === 'function' ? require('./history_page.js') : null);
    var SEATS = ['Green', 'Red', 'Blue', 'Black'], SHAPES = ['circle', 'square', 'triangle', 'diamond'], NS = 'http://www.w3.org/2000/svg';
    var MAX_POINTS = 200, MAX_MINUTES = 120, MAX_TURNS = 2200000, MAX_LEAD = 200, MAX_ROWS = 8, MAX_GRID = 60, BASE = [13, 23, 31], FALLBACK = [[121, 191, 147], [212, 112, 127], [143, 143, 214], [179, 175, 185]];

    // ---- reading `details` ----
    function num(x) { return typeof x === 'number' && isFinite(x) ? x : 0; }
    function list(x) { return Array.isArray(x) ? x : []; }
    function obj(x) { return x && typeof x === 'object' && !Array.isArray(x) ? x : {}; }
    function at(a, i) { return num(list(a)[i]); }
    function sum(a) { return list(a).reduce(function (n, v) { return n + num(v); }, 0); }
    function seatOf(x) { return typeof x === 'number' && x >= 0 && x <= 3 && x === Math.floor(x) ? x : -1; }
    function clock(turns) { var s = Math.floor(Math.max(0, num(turns)) / 20); return Math.floor(s / 60) + ':' + (s % 60 < 10 ? '0' : '') + (s % 60); }
    function seconds(turns) { return Math.round(Math.max(0, num(turns)) / 20) + ' s'; }
    function n0(n) { return H.numberText(n); }
    function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }

    // Details come from a server and are trusted for nothing: a length, a step or a list that is not what a match can have is cut here, so that a damaged record can make a wrong chart but never a hang
    function turnsOf(x, fallback) { var n = num(x); return Math.min(MAX_TURNS, Math.max(1, n > 0 ? n : fallback)); }
    function px(v) { return v < -100000 ? -100000 : v > 100000 ? 100000 : v; }      // a coordinate of a chart: finite and short, so that a path never has an exponent in it

    // The context of one record: the seats that played (by seat number), its details, and the helpers that read a seat's name
    function contextOf(rec) {
        var by = {}, played = [];
        H.seatsOf(rec).forEach(function (s) { by[s.seat] = s; played.push(s.seat); });
        played.sort();
        return { rec: rec, by: by, played: played, d: H.detailsOf(rec), mode187: !!rec && rec.mode === '187' };
    }
    function nameText(ctx, seat) { return ctx.by[seat] ? H.seatLabel(ctx.by[seat]) : SEATS[seat] || ''; }
    function sideText(ctx, side) { return nameText(ctx, side.seat) + (side.ally >= 0 ? ' & ' + nameText(ctx, side.ally) : ''); }
    // A seat, or a side of two (a seat and its ally), in a sentence: {seat, ally} (ally -1 for none)
    function side(seat, ally) { return { seat: seatOf(seat), ally: seatOf(ally) }; }

    // ---- what the page says: the highlights, as parts of a sentence (a string, or a side) ----
    // the leaders: [{t, seat, ally}] from details.highlights.lead
    function leadersOf(d) {
        var out = [];
        list(obj(d.highlights).lead).slice(0, MAX_LEAD).forEach(function (e) {
            var s = seatOf(list(e)[1]);
            if (s >= 0) out.push({ t: num(list(e)[0]), seat: s, ally: seatOf(list(e)[2]) });
        });
        return out;
    }
    function leadSentence(lead) {
        if (!lead.length) return null;
        var parts = [lead.length > 1 ? 'Changed hands ' + plural(lead.length - 1, 'time', 'times') + ': ' : 'Never changed hands: '];
        lead.forEach(function (e, i) {
            if (i) parts.push(', then ');
            parts.push(side(e.seat, e.ally));
            parts.push(i + 1 < lead.length ? ' until ' + clock(lead[i + 1].t) : ' to the end');
        });
        parts.push('.');
        return parts;
    }
    function peakOf(ctx, pm) {      // the busiest seat, and its busiest minute
        var best = null, peak = null;
        ctx.played.forEach(function (p) {
            var n = sum(pm[p]);
            if (!best || n > best.n) best = { seat: p, n: n };
            list(pm[p]).forEach(function (v, k) { if (!peak || num(v) > peak.n) peak = { seat: p, minute: k + 1, n: num(v) }; });
        });
        return { busiest: best, peak: peak };
    }
    // No ant was lost: nothing in the kills and the damage (they are what the pass attributed to a team), and none in the scoreboard's own count of lost ants (an ant that died with no team to blame is in no kill)
    function noAntLost(ctx, combat) {
        var c = obj(combat);
        if (!combat || sum(c.dealt) !== 0 || sum(list(c.kills).map(sum)) !== 0 || list(c.ev).length > 0) return false;
        return ctx.played.length > 0 && ctx.played.every(function (p) { var l = ctx.by[p] && ctx.by[p].lost; return typeof l === 'number' && l === 0; });
    }
    // Up to four chips for the band: [{href, label, parts}]. A chip links to the section that has the rest, so none is made for a section that is not on the page.
    function chipsOf(ctx) {
        var d = ctx.d, chips = [];
        if (!d) return chips;
        var hl = obj(d.highlights), fb = obj(hl.first_blood), act = obj(d.activity), lead = leadersOf(d), cb = obj(hl.comeback);
        var by = seatOf(fb.by), of = seatOf(fb.of);
        if (fb.t !== undefined && by >= 0 && of >= 0) chips.push({ href: '#overtime', label: 'First blood', parts: [side(by, -1), ' on ', side(of, -1), ', ' + clock(fb.t)] });
        else if (noAntLost(ctx, d.combat)) chips.push({ href: '#overtime', label: 'Fights', parts: ['No ant was lost in this match'] });
        if (lead.length > 1) chips.push({ href: '#overtime', label: 'Lead', parts: ['Changed hands ' + plural(lead.length - 1, 'time', 'times') + '; ', side(lead[lead.length - 1].seat, lead[lead.length - 1].ally), ' took it at ' + clock(lead[lead.length - 1].t)] });
        else if (lead.length === 1) chips.push({ href: '#overtime', label: 'Lead', parts: [side(lead[0].seat, lead[0].ally), ' led from ' + clock(lead[0].t) + ' to the end'] });
        if (num(cb.behind) >= 100 && seatOf(cb.seat) >= 0) chips.push({ href: '#overtime', label: 'Comeback', parts: [side(cb.seat, cb.ally), ' won from ' + n0(cb.behind) + ' points behind (at ' + clock(cb.t) + ')'] });
        var busiest = peakOf(ctx, list(act.per_minute)).busiest;
        if (busiest && busiest.n > 0) chips.push({ href: '#activity', label: 'Busiest', parts: [side(busiest.seat, -1), ': ' + n0(busiest.n) + ' orders'] });
        return chips.slice(0, 4);
    }
    // The facts under the charts of "Over time": [[label, parts]]
    function overTimeFacts(ctx) {
        var d = ctx.d, hl = obj(d.highlights), rows = [], fb = obj(hl.first_blood), lk = obj(hl.last_kill), cb = obj(hl.comeback), bu = obj(hl.burst), lead = leadSentence(leadersOf(d));
        if (fb.t !== undefined && seatOf(fb.by) >= 0 && seatOf(fb.of) >= 0) rows.push(['First blood', [side(fb.by, -1), ' killed an ant of ', side(fb.of, -1), ' at ' + clock(fb.t)]]);
        if (lk.t !== undefined && lk.t !== fb.t && seatOf(lk.by) >= 0 && seatOf(lk.of) >= 0) rows.push(['Last kill', [side(lk.by, -1), ' killed an ant of ', side(lk.of, -1), ' at ' + clock(lk.t)]]);
        if (lead) rows.push(['Lead', lead]);
        if (num(cb.behind) > 0 && seatOf(cb.seat) >= 0) rows.push(['Comeback', [side(cb.seat, cb.ally), ' was ' + n0(cb.behind) + ' points behind at ' + clock(cb.t) + ' and won.']]);
        if (num(bu.kills) >= 3 && seatOf(bu.seat) >= 0) rows.push(['Best run', [side(bu.seat, -1), ' killed ' + num(bu.kills) + ' ants in ' + seconds(num(bu.t1) - num(bu.t0)) + ' (from ' + clock(bu.t0) + ').']]);
        var out = list(hl.out).slice(0, MAX_ROWS);
        list(hl.wiped_out).slice(0, MAX_ROWS).forEach(function (w) {
            var s = seatOf(list(w)[0]), gone = out.some(function (o) { return seatOf(list(o)[0]) === s; }), times = num(list(w)[2]);
            if (s < 0) return;
            rows.push([gone ? 'Out' : 'Wiped out', [side(s, -1), ' had no ant left at ' + clock(list(w)[1]) + (gone ? ' and was out of the match.' : (times > 1 ? ' (' + times + ' times)' : '') + '; an egg hatched a new one.')]]);
        });
        return rows;
    }
    // The facts under "Activity": the busiest seat and minute
    function activityFacts(ctx, minutes) {
        var pm = list(obj(ctx.d.activity).per_minute), p = peakOf(ctx, pm), turns = Math.max(1, num(ctx.d.turns)), rows = [];
        if (p.busiest && p.busiest.n > 0) rows.push(['Busiest seat', [side(p.busiest.seat, -1), ': ' + n0(p.busiest.n) + ' orders, ' + Math.round(p.busiest.n / Math.max(1, turns / 1200)) + ' a minute']]);
        var all = [], best = 0;
        for (var m = 0; m < minutes; m++) { all.push(ctx.played.reduce(function (n, s) { return n + at(pm[s], m); }, 0)); if (all[m] > all[best]) best = m; }
        if (all.length && all[best] > 0) rows.push(['Busiest minute', ['Minute ' + (best + 1) + ' (' + best + ':00 to ' + (best + 1) + ':00): ' + n0(all[best]) + ' orders from all seats']]);
        if (p.peak && p.peak.n > 0) rows.push(['Busiest single minute', [side(p.peak.seat, -1), ' in minute ' + p.peak.minute + ': ' + n0(p.peak.n) + ' orders']]);
        return rows;
    }
    // a step of 1, 2 or 5 times a power of ten that gives at most `rows` rows for the largest value
    function niceStep(v, rows) {
        if (!(v > 0)) return 1;
        var raw = v / rows, p = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10)), m = raw / p;
        return (m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10) * p;
    }
    // the cell colour: the seat's colour mixed into the dark box by `a` (0 to 1), and the ink that reads on it
    function lum(c) { var f = c.map(function (v) { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }); return 0.2126 * f[0] + 0.7152 * f[1] + 0.0722 * f[2]; }
    function tintOf(rgb, a) {
        a = Math.max(0, Math.min(1, a));
        var m = rgb.map(function (v, i) { return Math.round(BASE[i] + (v - BASE[i]) * a); });
        return { background: 'rgb(' + m.join(',') + ')', color: lum(m) > 0.3 ? '#06120d' : '#f4ece2' };
    }

    // ---- the DOM (a document is given to the sections: the page's own, or a fake) ----
    function builder(doc, getStyle) {
        var seatRgb = null;
        function el(tag, className, text) { var n = doc.createElement(tag); if (className) n.className = className; if (text !== undefined) n.textContent = text; return n; }
        function sv(tag, attrs, parent) { var n = doc.createElementNS(NS, tag); Object.keys(attrs || {}).forEach(function (k) { n.setAttribute(k, attrs[k]); }); if (parent) parent.appendChild(n); return n; }
        function rgbOf(seat) {
            if (!seatRgb) {
                seatRgb = SEATS.map(function (c, i) {
                    var v = (getStyle ? getStyle('--c-' + c.toLowerCase()) : '') || '', m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(String(v).trim());
                    return m ? [parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16)] : FALLBACK[i];
                });
            }
            return seatRgb[seat];
        }
        function tint(node, seat, a) { var t = tintOf(rgbOf(seat), a); node.style.background = t.background; node.style.color = t.color; }
        function panel(id, title, countText) {
            var s = el('section', 'panel stat'); s.id = id; s.setAttribute('aria-labelledby', id + '-h');
            var head = el('div', 'head'), h2 = el('h2', '', title); h2.id = id + '-h'; head.appendChild(h2);
            if (countText) head.appendChild(el('span', 'count', countText));
            s.appendChild(head); return s;
        }
        function sq(seat) { var i = el('i', 'sq'); i.setAttribute('data-c', SEATS[seat]); return i; }
        function mark(seat) { var m = el('i', 'mk ' + SHAPES[seat]); m.setAttribute('data-c', SEATS[seat]); return m; }
        function nameNode(ctx, seat, withMark) {
            var w = el('span', 'nm'); w.appendChild(withMark ? mark(seat) : sq(seat)); w.appendChild(doc.createTextNode(nameText(ctx, seat))); return w;
        }
        // parts of a sentence: strings, and sides (a seat, or a seat and its ally) with their colour squares
        function say(ctx, parts) {
            var p = el('span', 'say');
            parts.forEach(function (x) {
                if (typeof x === 'string') { p.appendChild(doc.createTextNode(x)); return; }
                if (x.seat < 0) return;
                p.appendChild(nameNode(ctx, x.seat));
                if (x.ally >= 0) { p.appendChild(doc.createTextNode(' & ')); p.appendChild(nameNode(ctx, x.ally)); }
            });
            return p;
        }
        function legend(ctx) {
            var ul = el('ul', 'key');
            ctx.played.forEach(function (s) { var li = el('li'); li.appendChild(mark(s)); li.appendChild(doc.createTextNode(nameText(ctx, s))); ul.appendChild(li); });
            return ul;
        }
        function facts(ctx, rows, extra) {
            var f = el('dl', 'facts hl' + (extra ? ' ' + extra : ''));
            rows.forEach(function (r) { var d = el('div'); d.appendChild(el('dt', '', r[0])); var dd = el('dd'); dd.appendChild(say(ctx, r[1])); d.appendChild(dd); f.appendChild(d); });
            return f;
        }
        // the same table for a wide screen and for a phone: a row for every seat with a meter for each number (the scoreboard's look), and a card for every seat (the scoreboard's phone look)
        function seatTable(ctx, cols, caption) {
            var box = el('div', 'tblbox'), t = el('table', 'tbl'), max = cols.map(function (c) { return Math.max.apply(null, ctx.played.map(function (s) { return num(c.val(s)); }).concat([0])); });
            t.appendChild(el('caption', 'sr', caption));
            var hr = el('tr'); hr.appendChild(el('th', '', 'Player')); cols.forEach(function (c) { hr.appendChild(el('th', c.first ? 'sp' : '', c.label)); });
            var th = el('thead'); th.appendChild(hr); t.appendChild(th);
            var tb = el('tbody');
            function meter(seat, v, m) { var bar = el('span', 'mb'), f = el('i'); f.style.width = m ? Math.round(v / m * 100) + '%' : '0%'; f.setAttribute('data-c', SEATS[seat]); bar.appendChild(f); return bar; }
            ctx.played.forEach(function (s) {
                var tr = el('tr'), td = el('td'), w = el('span', 'who'); w.appendChild(sq(s)); w.appendChild(el('span', '', nameText(ctx, s))); td.appendChild(w); tr.appendChild(td);
                cols.forEach(function (c, i) {
                    var v = num(c.val(s)), cell = el('td', c.first ? 'sp' : ''), b = el('b', v ? '' : 'zero', n0(v)); b.style.fontWeight = '500'; cell.appendChild(b); cell.appendChild(meter(s, v, max[i])); tr.appendChild(cell);
                });
                tb.appendChild(tr);
            });
            t.appendChild(tb); box.appendChild(t);
            var cards = el('div', 'seatcards');
            ctx.played.forEach(function (s) {
                var c = el('div', 'sc-card'), top = el('div', 'top'), w = el('span', 'who'); w.appendChild(sq(s)); w.appendChild(el('span', '', nameText(ctx, s))); top.appendChild(w); c.appendChild(top);
                var g = el('div', 'grid cols' + Math.min(4, cols.length));
                cols.forEach(function (col, i) { var v = num(col.val(s)), cell = el('div', 'cell'); cell.appendChild(el('span', '', col.short || col.label)); cell.appendChild(el('b', v ? '' : 'zero', n0(v))); cell.appendChild(meter(s, v, max[i])); g.appendChild(cell); });
                c.appendChild(g); cards.appendChild(c);
            });
            box.appendChild(cards); return box;
        }
        function markPath(shape, x, y, r) {
            if (shape === 'square') return 'M' + (x - r) + ' ' + (y - r) + 'h' + 2 * r + 'v' + 2 * r + 'h' + (-2 * r) + 'z';
            if (shape === 'triangle') return 'M' + x + ' ' + (y - r - 1) + 'L' + (x + r + 1) + ' ' + (y + r) + 'L' + (x - r - 1) + ' ' + (y + r) + 'z';
            if (shape === 'diamond') return 'M' + x + ' ' + (y - r - 1) + 'L' + (x + r + 1) + ' ' + y + 'L' + x + ' ' + (y + r + 1) + 'L' + (x - r - 1) + ' ' + y + 'z';
            return null;
        }
        function dot(parent, seat, x, y, r) {
            var d = markPath(SHAPES[seat], x, y, r), n = d ? sv('path', { d: d }, parent) : sv('circle', { cx: x, cy: y, r: r }, parent);
            n.setAttribute('class', 'mkp'); n.setAttribute('data-c', SEATS[seat]); return n;
        }
        // A line chart of series that grow: o.series[seat] = the values at step, 2 * step ... of a seat; o.flags = vertical marks [{t, label}]; o.stepped draws a step line (kills).
        function lineChart(ctx, o) {
            var W = o.width, Ht = o.height, L = 46, R = 14, T = 12, B = 30, w = W - L - R, h = Ht - T - B, step = o.step, total = Math.max(1, o.turns);
            var maxV = 0; ctx.played.forEach(function (s) { list(o.series[s]).forEach(function (v) { maxV = Math.max(maxV, num(v)); }); });
            var tstep = niceStep(Math.max(maxV, o.ymax || 0), 5), top = Math.max(tstep, Math.ceil(Math.max(maxV, o.ymax || 0) / tstep) * tstep);
            var svg = sv('svg', { viewBox: '0 0 ' + W + ' ' + Ht, width: W, height: Ht, role: 'img', 'aria-label': o.label, 'class': 'chart' });
            var X = function (t) { return px(L + w * (t / total)); }, Y = function (v) { return px(T + h - h * (v / top)); };
            var jit = function (s) { return o.stepped ? (s - 1.5) * 2.6 : 0; };       // seats that tie on a step lie side by side, not on top of each other
            var ticks = Math.round(top / tstep);
            for (var i = 0; i <= ticks; i++) {
                var y = Y(tstep * i);
                sv('line', { x1: L, x2: W - R, y1: y, y2: y, 'class': i ? 'grid' : 'axis' }, svg);
                sv('text', { x: L - 8, y: y + 4, 'class': 'tick', 'text-anchor': 'end' }, svg).textContent = n0(Math.round(tstep * i));
            }
            var every = Math.max(total / 1200 > 18 ? 5 : total / 1200 > 9 ? 2 : 1, Math.ceil(total / 1200 / MAX_GRID));
            for (var m = 0; m * 1200 <= total; m += every) {
                var x = X(m * 1200);
                sv('line', { x1: x, x2: x, y1: T, y2: T + h, 'class': m ? 'vgrid' : 'axis' }, svg);
                sv('text', { x: x, y: Ht - 10, 'class': 'tick', 'text-anchor': 'middle' }, svg).textContent = m + ':00';
            }
            list(o.flags).forEach(function (f) {
                var fx = X(f.t), late = fx > W * 0.7;
                sv('line', { x1: fx, x2: fx, y1: T, y2: T + h, 'class': 'flag' }, svg);
                sv('text', { x: late ? fx - 6 : fx + 6, y: T + 11, 'class': 'flagt', 'text-anchor': late ? 'end' : 'start' }, svg).textContent = f.label;
            });
            ctx.played.forEach(function (s) {
                var v = list(o.series[s]).slice(0, MAX_POINTS); if (!v.length) return;
                var d = 'M' + X(0).toFixed(1) + ' ' + (Y(0) + jit(s)).toFixed(1);
                v.forEach(function (val, k) { var px = X((k + 1) * step).toFixed(1), py = (Y(num(val)) + jit(s)).toFixed(1); d += o.stepped ? 'H' + px + 'V' + py : 'L' + px + ' ' + py; });
                sv('path', { d: d, 'class': 'ln', 'data-c': SEATS[s], fill: 'none' }, svg);
            });
            ctx.played.forEach(function (s) {
                var v = list(o.series[s]).slice(0, MAX_POINTS); if (!v.length) return;
                if (o.stepped) v.forEach(function (val, k) { if (num(val) > (k ? num(v[k - 1]) : 0)) dot(svg, s, X((k + 1) * step), Y(num(val)) + jit(s), 3.6); });
                else dot(svg, s, X(v.length * step), Y(num(v[v.length - 1])), 5);
            });
            return svg;
        }
        // a box that draws its chart for its own width, and again when the width changes (redraw)
        function chartBox(draw) {
            var box = el('div', 'chartbox'), last = 0;
            box.drawIfNeeded = function () {
                var w = Math.max(280, Math.floor(box.clientWidth || 600));
                if (w === last) return;
                last = w; box.textContent = ''; box.appendChild(draw(w));
            };
            return box;
        }

        // ---- the band of highlights ----
        function summaryBand(rec) {
            var ctx = contextOf(rec), chips = chipsOf(ctx);
            if (!chips.length) return null;
            var s = el('section', 'panel band'); s.id = 'summary'; s.setAttribute('aria-label', 'Highlights of this match');
            var ul = el('ul', 'chips');
            chips.forEach(function (c) { var li = el('li'), a = el('a'); a.href = c.href; a.appendChild(el('span', 'lab', c.label)); a.appendChild(say(ctx, c.parts)); li.appendChild(a); ul.appendChild(li); });
            s.appendChild(ul); return s;
        }

        // ---- over time: the score, who led, the kills, and the highlights ----
        function overTime(rec) {
            var ctx = contextOf(rec), d = ctx.d, ot = d && obj(d.over_time);
            var series = ot && (ctx.mode187 ? list(ot.kills) : list(ot.score));
            if (!series || !series.some(function (v) { return list(v).length > 0; })) return null;
            var step = turnsOf(d.step, 300), turns = turnsOf(d.turns, 1), hl = obj(d.highlights), first = obj(hl.first_blood);
            var s = panel('overtime', 'Over time', 'Every ' + seconds(step) + ' · ' + clock(turns) + ' long');
            s.appendChild(legend(ctx));
            var flags = first.t !== undefined && seatOf(first.by) >= 0 ? [{ t: num(first.t), label: 'First blood ' + clock(first.t) }] : [];
            function endText(a) { return ctx.played.map(function (p) { var v = list(list(a)[p]); return nameText(ctx, p) + ' ' + n0(v[v.length - 1]); }).join(', '); }
            s.appendChild(el('h3', 'lab ct', ctx.mode187 ? 'Kills so far' : 'Score'));
            var sb = chartBox(function (w) {
                return lineChart(ctx, { width: w, height: w < 520 ? 210 : 250, series: series, step: step, turns: turns, flags: flags, label: (ctx.mode187 ? 'Kills' : 'Score') + ' of every seat over time. At the end: ' + endText(series) });
            });
            s.appendChild(sb);
            var lead = leadersOf(d);
            if (lead.length) {      // who led: a strip under the chart (a side of two is two-toned)
                var row = el('div', 'leadrow'); row.appendChild(el('span', 'lab', 'Lead'));
                var bar = el('div', 'leadbar'); bar.setAttribute('role', 'img');
                lead.forEach(function (e, i) {
                    var t0 = i === 0 ? 0 : e.t, t1 = i + 1 < lead.length ? lead[i + 1].t : turns, seg = el('i', e.ally >= 0 ? 'two' : '');
                    seg.style.flex = Math.max(1, t1 - t0) + ' 1 0'; seg.setAttribute('data-c', SEATS[e.seat]); seg.title = sideText(ctx, e) + ' ' + clock(t0) + ' - ' + clock(t1);
                    if (e.ally >= 0) seg.setAttribute('data-c2', SEATS[e.ally]);
                    bar.appendChild(seg);
                });
                bar.setAttribute('aria-label', 'Who led: ' + lead.map(function (e) { return sideText(ctx, e) + ' from ' + clock(e.t); }).join(', then '));
                row.appendChild(bar); s.appendChild(row);
            }
            var kills = list(ot.kills), kb = null;
            if (!ctx.mode187) {
                s.appendChild(el('h3', 'lab ct', 'Kills so far'));
                if (!ctx.played.some(function (p) { return sum(kills[p]) > 0; })) s.appendChild(el('p', 'quiet', 'No ant was killed in this match.'));
                else {
                    kb = chartBox(function (w) { return lineChart(ctx, { width: w, height: w < 520 ? 170 : 190, series: kills, step: step, turns: turns, flags: flags, stepped: true, ymax: 4, label: 'Kills of every seat over time. At the end: ' + endText(kills) }); });
                    s.appendChild(kb);
                }
            }
            var facts_ = overTimeFacts(ctx);
            if (facts_.length) s.appendChild(facts(ctx, facts_));
            s.appendChild(el('p', 'lgd', 'Each line is the seat\'s own ' + (ctx.mode187 ? 'kills' : 'score, as the scoreboard counts it') + ', read every ' + seconds(step) + '. The strip shows who had the most on the game\'s score box (a seat and its ally add their points up); a change counts when the new leader holds the lead for 10 seconds.'));
            return s;
        }

        // ---- activity: the orders of every seat, by minute and by kind ----
        function activity(rec) {
            var ctx = contextOf(rec), a = ctx.d && obj(ctx.d.activity), pm = a && list(a.per_minute);
            if (!pm || !ctx.played.some(function (p) { return list(pm[p]).length > 0; })) return null;
            var kinds = obj(a.by_kind), minutes = 0;
            ctx.played.forEach(function (p) { minutes = Math.max(minutes, Math.min(MAX_MINUTES, list(pm[p]).length)); });
            var total = ctx.played.reduce(function (n, p) { return n + sum(pm[p]); }, 0);
            var s = panel('activity', 'Activity', n0(total) + ' orders in all');
            var gmax = 0; ctx.played.forEach(function (p) { list(pm[p]).slice(0, minutes).forEach(function (v) { gmax = Math.max(gmax, num(v)); }); });
            s.appendChild(el('h3', 'lab ct', 'Orders in each minute'));
            var heat = el('div', 'heat'); heat.style.setProperty('--n', minutes); heat.setAttribute('role', 'table'); heat.setAttribute('aria-label', 'Orders of each seat in each minute of the match');
            var hh = el('div', 'hrow hdr'); hh.setAttribute('role', 'row'); hh.appendChild(el('span', 'hl', 'minute'));
            for (var m = 0; m < minutes; m++) hh.appendChild(el('span', 'hm', String(m + 1)));
            heat.appendChild(hh);
            ctx.played.forEach(function (p) {
                var r = el('div', 'hrow'); r.setAttribute('role', 'row');
                var lab = el('span', 'hl'); lab.appendChild(sq(p)); lab.appendChild(el('span', 'hn', nameText(ctx, p))); r.appendChild(lab);
                var rowMax = Math.max.apply(null, list(pm[p]).slice(0, minutes).map(num).concat([0]));
                for (var k = 0; k < minutes; k++) {
                    var v = at(pm[p], k), c = el('span', 'hc' + (v === 0 ? ' z' : '') + (v === rowMax && v > 0 ? ' top' : ''), v ? String(v) : '·');
                    if (v) tint(c, p, 0.16 + 0.84 * v / gmax);
                    c.setAttribute('role', 'cell'); c.title = nameText(ctx, p) + ', minute ' + (k + 1) + ': ' + v + ' orders'; r.appendChild(c);
                }
                heat.appendChild(r);
            });
            s.appendChild(heat);
            s.appendChild(el('p', 'lgd heatnote', 'The darker the square, the more orders in that minute (the same scale for every seat). A ring marks each seat\'s busiest minute.'));
            s.appendChild(el('h3', 'lab ct', 'Orders by kind'));
            var K = [['move', 'Moves'], ['attack', 'Attacks'], ['bomb', 'Bombs'], ['fire', 'Fires'], ['bridge', 'Bridges'], ['raid', 'Raids']];
            var cols = [{ label: 'Orders', val: function (p) { return sum(pm[p]); } }];
            K.forEach(function (k, i) { cols.push({ label: k[1], val: function (p) { return at(kinds[k[0]], p); }, first: i === 0 }); });
            s.appendChild(seatTable(ctx, cols, 'Orders of every seat by kind'));
            s.appendChild(facts(ctx, activityFacts(ctx, minutes)));
            s.appendChild(el('p', 'lgd', 'Every order that a player (or a computer player) gave: a move, an attack, a special order (a bomb, a fire, a bridge or a raid, by the type of ant that got it), a stop, a hatch or a team offer. Orders that name no ant of the seat are counted too.'));
            return s;
        }

        return { summaryBand: summaryBand, overTime: overTime, activity: activity };
    }

    // The sections of the page, for a document (and a way to read a colour of the page's style): each makes a panel from a record, or null. The page's SECTIONS list holds them.
    function sections(doc, getStyle) {
        var b = builder(doc, getStyle);
        return { summary: b.summaryBand, overtime: b.overTime, activity: b.activity };
    }
    // A chart is drawn for the width of its box: after the page is built, and again when the window changes size
    function redraw(rootNode) {
        var boxes = rootNode.querySelectorAll ? rootNode.querySelectorAll('.chartbox') : [];
        for (var i = 0; i < boxes.length; i++) if (boxes[i].drawIfNeeded) boxes[i].drawIfNeeded();
    }
    // Whether a record has stats to show (details that hold a key of a section), and why not when it has none: the note under the scoreboard
    function hasStats(rec) {
        var d = H.detailsOf(rec);
        return !!d && (obj(d.over_time).kills !== undefined || obj(d.activity).per_minute !== undefined);
    }
    function noStatsNote(rec) {
        if (hasStats(rec)) return '';
        var st = H.recState(rec), what = 'Over time and activity are counted from ';
        if (H.detailsOf(rec)) return what + 'the recording, and this one could not be counted. Only the totals above were kept.';
        if (st.kind === 'old') return what + 'a recording that this version can play. This match was recorded by an older version (' + (rec && typeof rec.game === 'string' && rec.game ? rec.game : 'unknown') + '), so only the totals above were kept.';
        if (st.kind === 'removed') return what + 'the recording. This one was removed before they were, so only the totals above were kept.';
        return what + 'the recording a few minutes after a match ends. They are not ready for this match yet.';
    }

    return { sections: sections, redraw: redraw, hasStats: hasStats, noStatsNote: noStatsNote, contextOf: contextOf, chipsOf: chipsOf, overTimeFacts: overTimeFacts, activityFacts: activityFacts, leadersOf: leadersOf,
             leadSentence: leadSentence, niceStep: niceStep, tintOf: tintOf, clock: clock };
});
