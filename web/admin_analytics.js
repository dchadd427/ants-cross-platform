// The section "Analytics" of the admin page (docs/ANALYTICS.md, "The Analytics tab"): the numbers of the counter of the site's visits (hub/analytics.py) as three views, Overview, Visitors and Health, for a range
// of days (7, 30 or 90, or two dates) with the same range's earlier days for the changes. It is the page's third file (admin_page.js would pass its size otherwise), loaded before it, and uses the page's own helpers
// (the one object `h` of make()): a word is made, a call is sent and a text is marked as data in one way in all of them. The numbers come from ONE call, POST /hub/admin/api/analytics {from, to}, with the hub's token;
// everything else (the totals, the changes, the charts, the tables, the CSV files) is made here from that answer. Every text that came from a visitor's browser (a site that sent a visitor, the message of an error) is
// put in as text with the page's mark (`typed`: data-untrusted), never as markup; the charts are SVG made from numbers. Days are Pacific days, as the page's other dates. ES5 and no module system, as the page:
// a browser gets the global AntsAdminAnalytics, node gets module.exports (tests/scripts/web_admin_analytics_check.js runs it with a fake DOM).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsAdminAnalytics = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var API = '/hub/admin/api/analytics';
    var SVG_NS = 'http://www.w3.org/2000/svg';
    var TIME_ZONE = 'America/Los_Angeles';
    var MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    var WEEKDAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
    var PRESETS = [['7', '7 days', 7], ['30', '30 days', 30], ['90', '90 days', 90]];
    var VIEWS = [['overview', 'Overview'], ['visitors', 'Visitors'], ['health', 'Health']];
    var MAX_DAYS = 400;                                       // the longest range (the hub says the same)
    var PERF_MIN = 20;                                        // a day with fewer timings than this has no line (the hub keeps the numbers: a few timings are jumpy)
    var ERRORS_SHOWN = 50;
    var DUR_EDGES = [5, 10, 20, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1800, 3600];       // the hub's bins of a visit's time (hub/analytics_sums.py)
    var DUR_GROUPS = [['Under 10 s', 0, 2], ['10 to 30 s', 2, 4], ['30 s to 1 min', 4, 6], ['1 to 3 min', 6, 9], ['3 to 10 min', 9, 11], ['Over 10 min', 11, 15]];
    var WIDTHS = ['Under 400 px', '400 to 767 px', '768 to 1199 px', '1200 to 1599 px', '1600 px and up'];
    var SHOWN = 10;                                           // rows of a list before "Other"
    var FOOT = 'Days run from midnight to midnight, Pacific time';
    var NOTE_TRACKED = ' Visitors are counted for each day on its own: a person who comes on 3 days is 3 visitors, because nothing links the days.';
    var ZIP_NOTE = 'Numbers only: a file has no names, addresses or visitor ids, because none are kept.';

    // ---- days (Pacific) ----------------------------------------------------------------------------------------------------------------
    function pacificDay(ms) {
        var f = new Intl.DateTimeFormat('en-US', { timeZone: TIME_ZONE, year: 'numeric', month: '2-digit', day: '2-digit' }), p = {};
        f.formatToParts(new Date(ms)).forEach(function (x) { p[x.type] = x.value; });
        return p.year + '-' + p.month + '-' + p.day;
    }
    function parts(day) {                                    // the year, month and day of a text that is a real day, else null
        var m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(String(day));
        if (!m) return null;
        var y = +m[1], mo = +m[2], d = +m[3], t = new Date(Date.UTC(y, mo - 1, d));
        return t.getUTCFullYear() === y && t.getUTCMonth() === mo - 1 && t.getUTCDate() === d ? { y: y, m: mo, d: d } : null;
    }
    function ordinal(day) { var p = parts(day); return p ? Math.round(Date.UTC(p.y, p.m - 1, p.d) / 86400000) : NaN; }
    function addDays(day, n) {
        var d = new Date((ordinal(day) + n) * 86400000);
        return d.getUTCFullYear() + '-' + ('0' + (d.getUTCMonth() + 1)).slice(-2) + '-' + ('0' + d.getUTCDate()).slice(-2);
    }
    function weekday(day) { return WEEKDAYS[new Date(ordinal(day) * 86400000).getUTCDay()]; }
    function isWeekend(day) { var w = weekday(day); return w === 'Sat' || w === 'Sun'; }
    function shortDay(day) { var p = parts(day); return p ? p.d + ' ' + MONTHS[p.m - 1] : ''; }
    function longDay(day) { var p = parts(day); return p ? weekday(day) + ' ' + p.d + ' ' + MONTHS[p.m - 1] : ''; }
    function fullDay(day) { var p = parts(day); return p ? p.d + ' ' + MONTHS[p.m - 1] + ' ' + p.y : ''; }
    function rangeWords(from, to) {
        var a = parts(from), b = parts(to);
        if (!a || !b) return '';
        return a.y === b.y ? shortDay(from) + ' – ' + fullDay(to) : fullDay(from) + ' – ' + fullDay(to);
    }
    function rangeOf(preset, today, custom) {
        for (var i = 0; i < PRESETS.length; i++) if (PRESETS[i][0] === preset) return { from: addDays(today, 1 - PRESETS[i][2]), to: today };
        return custom ? { from: custom.from, to: custom.to } : { from: addDays(today, -29), to: today };
    }
    function daysIn(from, to) { return ordinal(to) - ordinal(from) + 1; }
    // a custom range as the page may ask for it: two real days, the first not after the second, the last not after today, no more than 400 days; or the words that say what is wrong
    function customProblem(from, to, today) {
        if (!parts(from) || !parts(to)) return 'Pick two days.';
        if (ordinal(from) > ordinal(to)) return 'The first day must not be after the last one.';
        if (ordinal(to) > ordinal(today)) return 'The last day cannot be after today.';
        if (daysIn(from, to) > MAX_DAYS) return 'The range can be up to ' + MAX_DAYS + ' days.';
        return '';
    }

    // ---- words and numbers -------------------------------------------------------------------------------------------------------------
    function thousands(n) { return String(Math.round(n)).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }
    function percent(n, total) {
        if (!total) return '0%';
        var p = n * 100 / total;
        return (p < 10 && p > 0 ? p.toFixed(1) : String(Math.round(p))) + '%';
    }
    function plural(n, one, many) { return thousands(n) + ' ' + (n === 1 ? one : many); }
    // "17% more than the 30 days before" for a total and the total of the same number of days before it; null when there was nothing before to compare with
    function change(now, before, days) {
        if (!(before > 0)) return null;
        var p = Math.round(Math.abs(now - before) * 100 / before);
        if (!p) return { dir: 'same', text: 'About the same as the ' + days + ' days before' };
        return { dir: now > before ? 'up' : 'down', text: (now > before ? '▲ ' : '▼ ') + p + '% ' + (now > before ? 'more' : 'less') + ' than the ' + days + ' days before' };
    }
    function seconds(ms) { return ms < 10000 ? (ms / 1000).toFixed(1) + ' s' : Math.round(ms / 1000) + ' s'; }
    function duration(s) {
        s = Math.round(s);
        if (s < 60) return s + ' s';
        var m = Math.floor(s / 60);
        return m < 60 ? m + ' min ' + (s % 60) + ' s' : Math.floor(m / 60) + ' h ' + (m % 60) + ' min';
    }
    var names = { region: null, language: null };
    function displayNames(type) {
        if (names[type] === null) { try { names[type] = new Intl.DisplayNames(['en'], { type: type }); } catch (e) { names[type] = false; } }
        return names[type];
    }
    function countryName(code) { if (!code) return 'Unknown'; try { return (displayNames('region') && displayNames('region').of(code)) || code; } catch (e) { return code; } }
    function languageName(code) { try { return (displayNames('language') && displayNames('language').of(code)) || code; } catch (e) { return code; } }
    function pageName(path) { return path === 'other' ? 'Other pages' : path === '/map' ? '/map/…' : path; }
    function niceStep(max, ticks) {
        var raw = max / ticks, mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10)), f = raw / mag;
        return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * mag;
    }
    function sumOf(list, key) { var t = 0; for (var i = 0; i < list.length; i++) t += list[i][key] || 0; return t; }
    function entries(map) { return Object.keys(map || {}).map(function (k) { return [k, map[k]]; }); }
    function weightedMiddle(days, name, index) {   // the middle of the days' numbers (index 1 is p50, 3 is p95), each day counted as often as it has timings (the hub keeps no single timing)
        var rows = [], total = 0;
        days.forEach(function (d) { var p = d.perf && d.perf[name]; if (p && p[0] >= PERF_MIN) { rows.push([p[index], p[0]]); total += p[0]; } });
        if (!total) return null;
        rows.sort(function (a, b) { return a[0] - b[0]; });
        var seen = 0;
        for (var i = 0; i < rows.length; i++) { seen += rows[i][1]; if (seen * 2 >= total) return rows[i][0]; }
        return rows[rows.length - 1][0];
    }

    // ---- the files of the export: CSV, and a zip of them ----------------------------------------------------------------------------------
    function cell(v) {
        var t = v === null || v === undefined ? '' : String(v);
        if (/^[=+\-@\t\r]/.test(t) && !/^-?\d+(?:\.\d+)?$/.test(t)) t = "'" + t;          // (a program that opens the file must not take a text for a formula)
        return /[",\r\n]/.test(t) ? '"' + t.replace(/"/g, '""') + '"' : t;
    }
    function csv(header, rows) { return [header].concat(rows).map(function (r) { return r.map(cell).join(','); }).join('\r\n') + '\r\n'; }
    var CRC_TABLE = null;
    function crc32(bytes) {
        if (!CRC_TABLE) { CRC_TABLE = []; for (var n = 0; n < 256; n++) { var c = n; for (var k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; CRC_TABLE[n] = c >>> 0; } }
        var crc = 0xffffffff;
        for (var i = 0; i < bytes.length; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 255] ^ (crc >>> 8);
        return (crc ^ 0xffffffff) >>> 0;
    }
    function utf8(text) {
        var out = [];
        for (var i = 0; i < text.length; i++) {
            var c = text.charCodeAt(i);
            if (c >= 0xd800 && c < 0xdc00 && i + 1 < text.length) { c = 0x10000 + ((c - 0xd800) << 10) + (text.charCodeAt(++i) - 0xdc00); }
            if (c < 0x80) out.push(c);
            else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 63));
            else if (c < 0x10000) out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
            else out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
        }
        return out;
    }
    function le(n, size) { var out = []; for (var i = 0; i < size; i++) out.push(Math.floor(n / Math.pow(2, 8 * i)) & 255); return out; }
    // a zip with no compression (the files are small): [{name, text}] -> array of bytes
    function zip(files) {
        var out = [], central = [];
        files.forEach(function (f) {
            var name = utf8(f.name), data = utf8(f.text), crc = crc32(data), at = out.length;
            var head = [0x50, 0x4b, 3, 4].concat(le(20, 2), le(0x0800, 2), le(0, 2), le(0, 2), le(0x21, 2), le(crc, 4), le(data.length, 4), le(data.length, 4), le(name.length, 2), le(0, 2));
            out = out.concat(head, name, data);
            central = central.concat([0x50, 0x4b, 1, 2], le(20, 2), le(20, 2), le(0x0800, 2), le(0, 2), le(0, 2), le(0x21, 2), le(crc, 4), le(data.length, 4), le(data.length, 4), le(name.length, 2), le(0, 2), le(0, 2), le(0, 2), le(0, 2), le(0, 4), le(at, 4), name);
        });
        return out.concat(central, [0x50, 0x4b, 5, 6], le(0, 2), le(0, 2), le(files.length, 2), le(files.length, 2), le(central.length, 4), le(out.length, 4), le(0, 2));
    }

    // ---- the files of an export, from the answer ----------------------------------------------------------------------------------------
    function files(data) {
        var L = data.lists, out = {};
        out.daily = ['daily_totals.csv', csv(['day', 'visitors', 'visits', 'not_tracked_visits', 'sign_ups', 'errors'],
            data.days.map(function (d) { return [d.day, d.visitors, d.visits + d.nt, d.nt, d.signups, d.errors]; }))];
        var visitors = [];
        entries(L.countries).sort(function (a, b) { return b[1][0] + b[1][1] - a[1][0] - a[1][1]; }).forEach(function (e) { visitors.push(['country', e[0] || 'unknown', e[1][0], e[1][1]]); });
        [['browser', 'browsers'], ['system', 'systems'], ['device', 'devices'], ['language', 'langs']].forEach(function (p) { entries(L[p[1]]).forEach(function (e) { visitors.push([p[0], e[0], e[1], 0]); }); });
        L.widths.forEach(function (n, i) { visitors.push(['screen_width', WIDTHS[i], n, 0]); });
        visitors.push(['new_or_returning', 'new', L['new'], 0], ['new_or_returning', 'returning', L.returning, 0]);
        out.visitors = ['countries_browsers_systems_devices.csv', csv(['list', 'name', 'tracked', 'not_tracked'], visitors)];
        var pages = [];
        [['entry_page', 'entry'], ['exit_page', 'exit'], ['referrer_site', 'refs']].forEach(function (p) { entries(L[p[1]]).forEach(function (e) { pages.push([p[0], e[0] || (p[0] === 'referrer_site' ? 'direct' : ''), e[1]]); }); });
        DUR_GROUPS.forEach(function (g) { var n = 0; for (var i = g[1]; i < g[2]; i++) n += L.dur[i] || 0; pages.push(['time_on_site', g[0], n]); });
        out.pages = ['pages_and_referrers.csv', csv(['list', 'name', 'visits'], pages)];
        var timings = [];
        data.days.forEach(function (d) { ['load', 'game'].forEach(function (k) { if (d.perf && d.perf[k]) timings.push([d.day, k, d.perf[k][0], d.perf[k][1], d.perf[k][2], d.perf[k][3]]); }); });
        out.timings = ['timings.csv', csv(['day', 'what', 'count', 'p50_ms', 'p75_ms', 'p95_ms'], timings)];
        out.errors = ['errors.csv', csv(['where', 'message', 'file_and_line', 'times', 'visitors', 'first_seen', 'last_seen_day'],
            data.errors.map(function (e) { return [e.kind, e.msg, e.src, e.n, e.visitors, e.first, e.last || '']; }))];
        return out;
    }

    // ---- the section ---------------------------------------------------------------------------------------------------------------------
    function make(h) {
        var doc = h.doc, el = h.el, add = h.add;

        function svg(tag, attrs, text) {
            var n = doc.createElementNS ? doc.createElementNS(SVG_NS, tag) : doc.createElement(tag);
            Object.keys(attrs || {}).forEach(function (k) { n.setAttribute(k, String(attrs[k])); });
            if (text !== undefined) n.textContent = text;
            return n;
        }
        function chip(label, on, fn, cls) {
            var b = el('button', 'chip' + (on ? ' on' : '') + (cls ? ' ' + cls : ''), label);
            b.type = 'button';
            b.setAttribute('aria-pressed', on ? 'true' : 'false');
            b.addEventListener('click', fn);
            return b;
        }
        function swatch(cls) { return el('i', 'sw ' + cls); }

        // ---- a row of a list: the name, a bar (a hatched part for the visits of browsers that asked not to be tracked), the count and its share ----
        function barRow(label, n, max, total, hatched, cls, mark) {
            var row = el('div', 'brow' + (cls ? ' ' + cls : '')), track = el('div', 'btrack'), fill = el('i', 'bfill');
            var share = max ? Math.max(n > 0 ? 1 : 0, Math.round(n * 1000 / max) / 10) : 0;
            fill.style.width = share + '%';
            add(track, fill);
            if (hatched) {
                var hat = el('i', 'bhat');
                hat.style.width = (max ? Math.round(hatched * 1000 / max) / 10 : 0) + '%';
                add(track, hat);
            }
            add(row, mark ? mark(label) : el('span', 'bl', label), track, el('span', 'bn', thousands(n)), el('span', 'bp', percent(n, total)));
            return row;
        }
        // a list: the biggest SHOWN rows, then Other with the rest of the entries (and how many they are) when the list is longer
        function barList(map, o) {
            o = o || {};
            var items = entries(map).filter(function (e) { return e[0] !== 'other' && e[0] !== 'Other'; }), other = ((map && map.other) || 0) + ((map && map.Other) || 0), total = o.total, wrap = el('div', 'blist'), unknown = null;
            if (o.unknownKey !== undefined) items = items.filter(function (e) { if (e[0] === o.unknownKey) { unknown = e; return false; } return true; });
            if (!o.keepOrder) items.sort(function (a, b) { return o.valueOf(b) - o.valueOf(a) || (a[0] < b[0] ? -1 : 1); });
            var shown = items.slice(0, o.shown || SHOWN), rest = items.slice(o.shown || SHOWN), restSum = other, restHat = 0, max = 0;
            rest.forEach(function (e) { restSum += o.valueOf(e); if (o.hatOf) restHat += o.hatOf(e); });
            shown.concat(unknown ? [unknown] : []).forEach(function (e) { max = Math.max(max, o.valueOf(e)); });
            max = Math.max(max, restSum);
            if (!shown.length && !restSum && !unknown) return add(wrap, el('p', 'none', 'Nothing yet.'));
            shown.forEach(function (e) { add(wrap, barRow(o.label(e[0]), o.valueOf(e), max, total, o.hatOf ? o.hatOf(e) : 0, '', o.mark)); });
            if (restSum > 0) add(wrap, barRow(o.otherLabel && rest.length ? o.otherLabel(rest.length) : o.otherName || 'Other', restSum, max, total, restHat, 'dim'));
            if (unknown) add(wrap, barRow(o.label(unknown[0]), o.valueOf(unknown), max, total, o.hatOf ? o.hatOf(unknown) : 0, 'dim'));
            return wrap;
        }
        function plain(map, label, total, mark) { return barList(map, { total: total, valueOf: function (e) { return e[1]; }, label: label, mark: mark }); }

        function codeMark(text) { var s = h.typed('span', 'bl pg', text); return s; }

        // ---- the charts ----
        var W = 900, HT = 300, PAD = { l: 44, r: 12, t: 10, b: 28 };       // (the size of the picture in its own units; `size` below sets W and HT for the chart that is being made: one unit is about one pixel of the page)
        function size(width) {
            var w = Math.max(280, Math.min(1000, Math.round(width || 900)));
            W = w;
            HT = w < 500 ? 240 : 300;
            return w;
        }
        function frame(title, nodes) {
            var s = svg('svg', { viewBox: '0 0 ' + W + ' ' + HT, 'class': 'chart', role: 'img', 'aria-label': title, preserveAspectRatio: 'xMidYMid meet' });
            nodes.forEach(function (n) { add(s, n); });
            return s;
        }
        function axes(max, step, days) {
            var out = [], inner = HT - PAD.t - PAD.b;
            for (var v = 0; v <= max; v += step) {
                var y = PAD.t + inner - (inner * v / max);
                out.push(svg('line', { x1: PAD.l, x2: W - PAD.r, y1: y, y2: y, 'class': 'grid' }));
                out.push(svg('text', { x: PAD.l - 6, y: y + 4, 'class': 'tick', 'text-anchor': 'end' }, v >= 1000 && v % 1000 === 0 ? v / 1000 + 'k' : String(v)));
            }
            return out;
        }
        function xLabels(days, x) {
            var out = [], n = days.length, want = n <= 8 ? n : 4, last = -100;
            for (var k = 0; k < want; k++) {
                var i = n <= 8 ? k : Math.round(k * (n - 1) / (want - 1));
                if (i - last < 2 && k) continue;
                last = i;
                out.push(svg('text', { x: x(i), y: HT - 8, 'class': 'tick', 'text-anchor': i === 0 ? 'start' : i === n - 1 ? 'end' : 'middle' }, shortDay(days[i].day)));
            }
            return out;
        }
        // Visitors per day: a mint bar and, above it, a hatched one for the visits that sent only a country; the weekends are shaded
        function barChart(days, width) {
            size(width);
            var max = 1, i;
            days.forEach(function (d) { max = Math.max(max, d.visitors + d.nt); });
            var step = niceStep(max, 3), top = Math.ceil(max / step) * step, inner = HT - PAD.t - PAD.b, cw = (W - PAD.l - PAD.r) / days.length;
            var x = function (j) { return PAD.l + cw * (j + 0.5); }, nodes = [], hits = [];
            days.forEach(function (d, j) { if (isWeekend(d.day)) nodes.push(svg('rect', { x: PAD.l + cw * j, y: PAD.t, width: cw, height: inner, 'class': 'weekend' })); });
            nodes = nodes.concat(axes(top, step, days));
            var bw = Math.max(1, cw * 0.74);
            days.forEach(function (d, j) {
                var hv = inner * d.visitors / top, hn = inner * d.nt / top;
                if (d.visitors) nodes.push(svg('rect', { x: x(j) - bw / 2, y: PAD.t + inner - hv, width: bw, height: hv, 'class': 'bar', 'data-day': d.day }));
                if (d.nt) nodes.push(svg('rect', { x: x(j) - bw / 2, y: PAD.t + inner - hv - hn, width: bw, height: hn, 'class': 'barnt', 'data-day': d.day, fill: 'url(#hatch)' }));
                hits.push(svg('rect', { x: PAD.l + cw * j, y: PAD.t, width: cw, height: inner, 'class': 'hit', 'data-index': j }));
            });
            var defs = svg('defs'), pattern = svg('pattern', { id: 'hatch', width: 6, height: 6, patternUnits: 'userSpaceOnUse', patternTransform: 'rotate(45)' });
            add(pattern, svg('rect', { width: 6, height: 6, 'class': 'hatchbg' }), svg('line', { x1: 0, y1: 0, x2: 0, y2: 6, 'class': 'hatchline' }));
            add(defs, pattern);
            return { svg: frame('Visitors per day', [defs].concat(nodes, xLabels(days, x), hits)), hits: hits, x: x, width: W };
        }
        // two or three lines of milliseconds (p50 solid, p75 dashed, p95 dotted), a gap for a day with too few timings, a dotted vertical line on the days a new version went live
        function lineChart(days, name, versions, width) {
            size(width);
            var max = 0, series = [[1, 'p50', 'l50'], [2, 'p75', 'l75'], [3, 'p95', 'l95']];
            days.forEach(function (d) { var p = d.perf && d.perf[name]; if (p && p[0] >= PERF_MIN) max = Math.max(max, p[3]); });
            if (!max) return null;
            var maxS = max / 1000, step = niceStep(maxS, 4), top = Math.ceil(maxS / step) * step, inner = HT - PAD.t - PAD.b, n = days.length;
            var x = function (j) { return PAD.l + (W - PAD.l - PAD.r) * (n === 1 ? 0.5 : j / (n - 1)); }, nodes = [];
            for (var v = 0; v <= top + 1e-9; v += step) {
                var y = PAD.t + inner - inner * v / top;
                nodes.push(svg('line', { x1: PAD.l, x2: W - PAD.r, y1: y, y2: y, 'class': 'grid' }), svg('text', { x: PAD.l - 6, y: y + 4, 'class': 'tick', 'text-anchor': 'end' }, (Math.round(v * 10) / 10) + ' s'));
            }
            versions.forEach(function (j) { nodes.push(svg('line', { x1: x(j), x2: x(j), y1: PAD.t, y2: PAD.t + inner, 'class': 'newver', 'data-day': days[j].day })); });
            series.forEach(function (s) {
                var d = '', pen = false, lastY = null;
                days.forEach(function (day, j) {
                    var p = day.perf && day.perf[name];
                    if (!p || p[0] < PERF_MIN) { pen = false; return; }
                    var y = PAD.t + inner - inner * (p[s[0]] / 1000) / top;
                    d += (pen ? 'L' : 'M') + Math.round(x(j) * 10) / 10 + ' ' + Math.round(y * 10) / 10;
                    pen = true;
                    lastY = y;
                });
                if (d) nodes.push(svg('path', { d: d, 'class': 'line ' + s[2], fill: 'none' }), svg('text', { x: W - PAD.r - 2, y: lastY - 6, 'class': 'tick end ' + s[2], 'text-anchor': 'end' }, s[1]));
            });
            return frame(name === 'load' ? 'Page load per day' : 'Game download and start per day', nodes.concat(xLabels(days, x)));
        }

        // ---- the section ----
        function render(host) {
            var today = pacificDay(h.env.now());
            var st = { view: 'overview', preset: '30', custom: false, from: null, to: null, data: null, error: '', table: false, menu: null, loading: false, seq: 0, all: false, dead: false, customError: '' };
            var nav = el('div', 'anav chips'), bar = el('section', 'panel arange'), body = el('div', 'abody'), foot = el('p', 'afoot');
            add(host, nav, bar, body, foot);
            h.setCleanup(function () { st.dead = true; closeMenu(); });

            function range() { return st.preset === 'custom' ? { from: st.from, to: st.to } : rangeOf(st.preset, today); }
            function load() {
                var r = range(), seq = ++st.seq;
                st.loading = true;
                st.error = '';
                drawBar();
                return Promise.resolve(h.hubToken()).then(function (hub) {
                    if (!hub) { if (seq === st.seq && !st.dead) { st.loading = false; st.error = 'The numbers could not be loaded.'; draw(); } return null; }
                    return h.call('POST', API, { from: r.from, to: r.to }).then(function (res) {
                        if (st.dead || seq !== st.seq) return null;
                        st.loading = false;
                        if (h.refused(res)) return null;
                        if (res.status === 200 && res.json && res.json.days) { st.data = res.json; st.error = ''; }
                        else st.error = res.status === 503 && res.json && res.json.error === 'analytics_off' ? 'The counter is switched off on this hub (ANTS_HUB_ANALYTICS is off).' :
                            res.status === 400 && res.json && res.json.error === 'range' ? 'That range is not one the counter can show.' : 'The numbers could not be loaded.';
                        draw();
                        return null;
                    });
                }).then(null, function () { if (!st.dead && seq === st.seq) { st.loading = false; st.error = 'The server did not answer.'; draw(); } });
            }
            function setPreset(p) { st.preset = p; st.custom = false; st.customError = ''; st.menu = null; load(); }

            // ---- the views' chips and the range bar ----
            function drawNav() {
                nav.textContent = '';
                nav.setAttribute('role', 'group');
                nav.setAttribute('aria-label', 'Views');
                VIEWS.forEach(function (v) { add(nav, chip(v[1], st.view === v[0], function () { st.view = v[0]; st.menu = null; drawNav(); draw(); })); });
            }
            function drawBar() {
                bar.textContent = '';
                var r = range(), top = el('div', 'rtop'), left = el('div', 'rleft'), right = el('div', 'rright'), n = parts(r.from) && parts(r.to) ? daysIn(r.from, r.to) : 0;
                add(left, el('span', 'rlabel', 'Range'));
                PRESETS.forEach(function (p) { add(left, chip(p[1], st.preset === p[0] && !st.custom, function () { setPreset(p[0]); })); });
                add(left, chip('Custom', st.preset === 'custom' || st.custom, function () { st.custom = !st.custom; if (st.preset !== 'custom' && st.custom) { st.from = st.from || r.from; st.to = st.to || r.to; } drawBar(); }));
                var d1 = el('div', 'rdates', rangeWords(r.from, r.to)), d2 = el('div', 'rsub', n ? n + ' days, last day is ' + (r.to === today ? 'today so far' : fullDay(r.to)) : '');
                add(right, add(el('div', 'rwhen'), d1, d2));
                if (st.data) add(right, exportMenu());
                add(top, left, right);
                add(bar, top);
                if (st.custom) add(bar, customForm());
            }
            function customForm() {
                var form = el('div', 'rcustom'), a = el('div', 'f'), b = el('div', 'f');
                function field(label, id, value) {
                    var box = el('div', 'f'), l = el('label', null, label), i = el('input', 'date');
                    i.type = 'date'; i.id = id; i.value = value || '';
                    i.setAttribute('min', addDays(today, 1 - MAX_DAYS)); i.setAttribute('max', today);
                    l.setAttribute('for', id);
                    return { box: add(box, l, i), input: i };
                }
                var from = field('From', 'an-from', st.from), to = field('To', 'an-to', st.to);
                var go = h.button('Show these days', 'sm', function () {
                    var problem = customProblem(from.input.value, to.input.value, today);
                    st.customError = problem;
                    if (problem) { drawBar(); return; }
                    st.preset = 'custom'; st.from = from.input.value; st.to = to.input.value;
                    st.custom = true;
                    load();
                });
                add(form, from.box, to.box, go, el('p', 'rnote', 'Up to ' + MAX_DAYS + ' days back. Days run from midnight to midnight, Pacific time.'));
                if (st.customError) add(form, h.message(st.customError, 'bad'));
                return form;
            }

            // ---- the export ----
            function closeMenu() { if (st.menu) { st.menu.off(); st.menu = null; } }
            function download(name, mime, bytes) {
                if (h.env.download) { h.env.download(name, mime, bytes); return; }
                try {
                    var blob = new Blob([typeof bytes === 'string' ? bytes : new Uint8Array(bytes)], { type: mime }), url = URL.createObjectURL(blob), a = doc.createElement('a');
                    a.href = url; a.download = name;
                    add(doc.body, a);
                    a.click();
                    doc.body.removeChild(a);
                    setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
                } catch (e) { h.toast('This browser could not save the file.'); }
            }
            function exportMenu() {
                var wrap = el('div', 'amw'), btn = h.button('Export CSV ▾', 'sm', null);
                btn.setAttribute('aria-haspopup', 'menu');
                btn.setAttribute('aria-expanded', st.menu ? 'true' : 'false');
                btn.addEventListener('click', function () { if (st.menu) { closeMenu(); drawBar(); } else openMenu(wrap, btn); });
                add(wrap, btn);
                if (st.menu) add(wrap, st.menu.node);
                return wrap;
            }
            function openMenu(wrap, btn) {
                var r = range(), made = files(st.data), node = el('div', 'amenu2'), stamp = r.from + '_' + r.to;
                node.setAttribute('role', 'menu');
                add(node, add(el('div', 'mhead'), el('b', null, 'Export CSV'), el('small', null, rangeWords(r.from, r.to) + ', one row for each day or each line, Pacific days.')));
                function item(title, sub, run) {
                    var b = el('button', 'mi'); b.type = 'button'; b.setAttribute('role', 'menuitem');
                    add(b, el('b', null, title), el('small', null, sub));
                    b.addEventListener('click', function () { run(); closeMenu(); drawBar(); });
                    return add(node, b);
                }
                function one(key, title, sub) { item(title, sub, function () { download(made[key][0].replace('.csv', '_' + stamp + '.csv'), 'text/csv;charset=utf-8', made[key][1]); }); }
                one('daily', 'Daily totals', 'visitors, visits, not-tracked visits, sign-ups, errors for each day');
                one('visitors', 'Countries, browsers, systems, devices', 'visits and visitors, with the not-tracked share');
                one('pages', 'Pages and referrers', 'entry pages, exit pages, referrer sites, time on site');
                one('timings', 'Timings', 'page load and game download, p50 p75 p95 for each day');
                one('errors', 'Errors', 'grouped by message and file:line, with first and last seen');
                item('Everything, as one .zip', 'one CSV file for each of the lists above', function () {
                    download('analytics_' + stamp + '.zip', 'application/zip', zip(Object.keys(made).map(function (k) { return { name: made[k][0], text: made[k][1] }; })));
                });
                add(node, el('p', 'mfoot', ZIP_NOTE));
                function onKey(e) { if (e.key === 'Escape') { closeMenu(); drawBar(); } }
                function onPress(e) { if (!wrap.contains(e.target)) { closeMenu(); drawBar(); } }
                doc.addEventListener('keydown', onKey, true);
                doc.addEventListener('click', onPress, true);
                st.menu = { node: node, off: function () { doc.removeEventListener('keydown', onKey, true); doc.removeEventListener('click', onPress, true); } };
                drawBar();
            }

            // ---- the body ----
            function draw() {
                drawBar();
                body.textContent = '';
                foot.textContent = '';
                if (st.loading && !st.data) { add(body, el('p', 'aload', 'Loading the numbers…')); return; }
                if (st.error && !st.data) {
                    add(body, h.message(st.error, 'bad'), h.button('Try again', 'sm', load));
                    return;
                }
                if (!st.data) return;
                if (st.error) add(body, h.message(st.error, 'bad'));
                var d = st.data, c = d.counter;
                if (st.view === 'overview') overview(d);
                else if (st.view === 'visitors') visitors(d);
                else health(d);
                var legal = el('a', null, 'Legal page');
                legal.setAttribute('href', '/legal');
                add(foot, doc.createTextNode((c.first_day ? 'Counted since ' + fullDay(c.first_day) + ' · ' : '') + FOOT + ' · Updated ' + updated(c.now) + ' · No cookies; no address or visitor id is kept (see the '), legal, doc.createTextNode(').'));
            }
            function updated(seconds) { var p = pacificDay(seconds * 1000); return (p === today ? 'today' : shortDay(p)) + ', ' + clock(seconds); }
            function clock(seconds) {
                var f = new Intl.DateTimeFormat('en-US', { timeZone: TIME_ZONE, hour: '2-digit', minute: '2-digit', hourCycle: 'h23' }), p = {};
                f.formatToParts(new Date(seconds * 1000)).forEach(function (x) { p[x.type] = x.value; });
                return p.hour + ':' + p.minute;
            }
            function whenWords(seconds) {                                                // "today at 09:12", "yesterday at 23:40", "14 Dec at 09:12" (Pacific time)
                var day = pacificDay(seconds * 1000);
                return (day === today ? 'today' : day === addDays(today, -1) ? 'yesterday' : shortDay(day)) + ' at ' + clock(seconds);
            }
            function nothingYet(d) { return !d.counter.first_day && !d.counter.last_at; }

            function tile(label, big, deltaNode, small, cls) {
                var t = el('div', 'tile' + (cls ? ' ' + cls : ''));
                return add(t, el('div', 'tl', label), el('div', 'tb', big), deltaNode || el('div', 'td mute'), small);
            }
            function deltaNode(c, badWhenUp) {
                if (!c) return el('div', 'td mute', 'No earlier days to compare with');
                var n = el('div', 'td ' + (c.dir === 'same' ? 'mute' : (c.dir === 'up') !== !!badWhenUp ? 'good' : 'warn'));
                n.textContent = c.text;
                return n;
            }
            function overview(d) {
                var days = d.days, n = days.length, vis = sumOf(days, 'visitors'), nt = sumOf(days, 'nt'), visits = sumOf(days, 'visits') + nt, sign = sumOf(days, 'signups'), errs = sumOf(days, 'errors');
                var empty = nothingYet(d), b = d.before;
                var kinds = d.errors.length, fresh = d.errors.filter(function (e) { return ordinal(e.first) >= ordinal(today) - 1 && ordinal(e.first) > ordinal(d.from); }).length;
                var tiles = el('div', 'tiles');
                var line = function (t) { return el('div', 'tt', t); };
                add(tiles,
                    tile('Visitors', thousands(vis), empty ? null : deltaNode(change(vis, b.visitors, n)), line(empty ? 'nothing counted yet' : 'about ' + thousands(vis / n) + ' a day'), empty ? 'blank' : ''),
                    tile('Visits', thousands(visits), empty ? null : deltaNode(change(visits, b.visits + b.nt, n)), empty ? line('nothing counted yet') : add(el('div', 'tt'), swatch('hat'), doc.createTextNode(thousands(nt) + ' not tracked (' + percent(nt, visits) + ')')), empty ? 'blank' : ''),
                    tile('Sign-ups', thousands(sign), empty ? null : deltaNode(change(sign, b.signups, n)), line(empty ? 'nothing counted yet' : percent(sign, vis) + ' of visitors'), empty ? 'blank' : ''),
                    tile('Errors', thousands(errs), empty ? null : deltaNode(change(errs, b.errors, n), true), line(empty ? 'nothing counted yet' : plural(kinds, 'kind', 'kinds') + ', ' + fresh + ' new in 2 days'), empty ? 'blank' : ''));
                add(body, tiles);
                if (empty) {
                    var card = el('section', 'aempty'), list = el('ul', 'achecks');
                    var began = whenWords(d.counter.since);
                    add(card, el('h2', null, 'Nothing counted yet'),
                        el('p', null, 'The counter was switched on ' + began + '. The first visit shows up here a minute or two after it happens. A day’s numbers are final at midnight, Pacific time.'));
                    add(list, el('li', 'ok', 'The counter is on, since ' + began), el('li', 'wait', 'No event received yet'));
                    add(card, list);
                    add(body, card);
                    return;
                }
                add(body, el('p', 'anote', NOTE_TRACKED.replace(/^ /, '') + (nt ? ' Visits include the ' + thousands(nt) + ' from browsers that asked not to be tracked; they have no visitor count.' : '')));
                var chart = el('section', 'panel'), legend = el('div', 'alegend');
                add(chart, el('h2', null, 'Visitors per day'));
                add(legend, add(el('span'), swatch('mint'), doc.createTextNode('Visitors')), add(el('span'), swatch('hat'), doc.createTextNode('Not tracked: visits that sent only a country')), add(el('span'), swatch('wk'), doc.createTextNode('Weekend')));
                add(chart, legend);
                var toggle = h.button(st.table ? 'Show the chart' : 'Show as a table', 'sm ghost', function () { st.table = !st.table; draw(); });
                if (st.table) add(chart, dayTable(days));
                else add(chart, dayChart(days));
                add(chart, add(el('div', 'ahelp'), toggle, el('span', null, st.table ? 'Newest day first.' : 'Move over a bar for its numbers. The last bar is ' + (d.to === today ? 'today so far.' : 'the last day.'))));
                add(body, chart);
            }
            function dayChart(days) {
                var holder = el('div', 'chartbox'), card = el('div', 'tip'), c = barChart(days, bodyWidth() - 44);
                card.hidden = true;
                function show(j) {
                    var day = days[j];
                    card.textContent = '';
                    add(card, el('b', null, longDay(day.day) + (day.day === today ? ' (so far)' : '')), kvRow('Visitors', thousands(day.visitors)), kvRow('Not tracked', thousands(day.nt) + ' visits'), kvRow('Visits in all', thousands(day.visits + day.nt)));
                    var at = (c.x(j) / c.width) * 100;
                    card.style.left = (at < 55 ? at + 2 : Math.max(2, at - 30)) + '%';          // (to the right of the bar, or to its left near the right edge)
                    card.hidden = false;
                }
                c.hits.forEach(function (hit, j) {
                    hit.addEventListener('mouseenter', function () { show(j); });
                    hit.addEventListener('click', function () { show(j); });
                });
                holder.addEventListener('mouseleave', function () { card.hidden = true; });
                return add(holder, c.svg, card);
            }
            function kvRow(k, v) { return add(el('div', 'tk'), el('span', null, k), el('span', null, v)); }
            function dayTable(days) {
                var wrap = el('div', 'atable'), t = el('table');
                add(t, add(el('thead'), add(el('tr'), el('th', null, 'Day'), el('th', 'nc', 'Visitors'), el('th', 'nc', 'Not tracked'), el('th', 'nc', 'Visits'))));
                var tb = el('tbody');
                days.slice().reverse().forEach(function (day) {
                    add(tb, add(el('tr'), el('td', null, longDay(day.day) + (day.day === today ? ' (so far)' : '')), el('td', 'nc', thousands(day.visitors)), el('td', 'nc', thousands(day.nt)), el('td', 'nc', thousands(day.visits + day.nt))));
                });
                return add(wrap, add(t, tb));
            }

            // ---- Visitors ----
            function panelOf(title, hint, body2) {
                var p = h.panel(title, hint);
                return add(p, body2);
            }
            function visitors(d) {
                var L = d.lists, nt = L.nt, all = L.visits + nt, trackedVisitors = L.visitors;
                if (nothingYet(d)) { add(body, el('p', 'aload', 'Nothing counted yet.')); return; }
                add(body, el('p', 'anote', 'Not-tracked visits (' + percent(nt, all) + ') send a country and nothing else. They are in the countries below (hatched) and in the totals, and in no other list on this page.'));
                var left = el('div', 'acol'), right = el('div', 'acol'), grid = el('div', 'agrid');
                var countryTotal = all;
                var legend = el('div', 'alegend');
                add(legend, add(el('span'), swatch('mint'), doc.createTextNode('Visits that are tracked')), add(el('span'), swatch('hat'), doc.createTextNode('Not tracked')));
                var countries = barList(L.countries, { total: countryTotal, valueOf: function (e) { return e[1][0] + e[1][1]; }, hatOf: function (e) { return e[1][1]; }, label: countryName, unknownKey: '',
                                                       otherLabel: function (n) { return 'Other (' + n + (n === 1 ? ' country)' : ' countries)'); } });
                add(left, add(panelOf('Countries', 'Visits from each country, by the address at the moment of the visit (the address is not kept).', legend), countries));
                var nr = el('div', 'nr'), bar = el('div', 'nrbar'), a = el('i', 'nrnew'), b2 = el('i', 'nrret'), tot = L['new'] + L.returning;
                a.style.width = (tot ? Math.round(L['new'] * 1000 / tot) / 10 : 0) + '%';
                b2.style.width = (tot ? Math.round(L.returning * 1000 / tot) / 10 : 0) + '%';
                add(bar, a, b2);
                var cols = el('div', 'nrcols');
                add(cols, add(el('div'), add(el('span', 'nrk'), swatch('mint'), doc.createTextNode('New')), add(el('div', 'nrv'), doc.createTextNode(thousands(L['new']) + ' '), el('small', null, percent(L['new'], tot)))),
                    add(el('div'), add(el('span', 'nrk'), swatch('blue'), doc.createTextNode('Returning')), add(el('div', 'nrv'), doc.createTextNode(thousands(L.returning) + ' '), el('small', null, percent(L.returning, tot)))));
                add(nr, bar, cols, el('p', 'ph', 'Returning means the browser was seen on an earlier day of the last 7. Nothing else links one day to another, and the link is kept in memory only: a restart of the hub forgets it.'));
                add(right, panelOf('New and returning', null, nr));
                add(right, panelOf('Where visitors came from', 'The site that sent them (only the name of the site, never the page). “(direct)” is no referrer: a typed address, a bookmark or an app.',
                    barList(L.refs, { total: L.visits, valueOf: function (e) { return e[1]; }, label: function (k) { return k === '' ? '(direct)' : k; }, mark: function (t) { return h.typed('span', 'bl', t); },
                                      otherLabel: function (n) { return 'Other (' + n + ' sites)'; }, otherName: 'Other sites', shown: SHOWN })));
                var pages = function (key, total) { return barList(L[key], { total: total, valueOf: function (e) { return e[1]; }, label: pageName, mark: codeMark, otherName: 'Other pages' }); };
                add(right, panelOf('Entry pages', 'The first page of a visit.', pages('entry', L.visits)));
                add(right, panelOf('Exit pages', 'The last page of a visit.', pages('exit', L.visits)));
                add(right, panelOf('Time on the site', 'A visit, from the first page to the last. The gold column holds the middle.', timeOnSite(L)));
                add(left, panelOf('Browsers', 'Counted visitors only.', plain(L.browsers, function (k) { return k; }, trackedVisitors)));
                add(left, panelOf('Systems', 'The operating system.', plain(L.systems, function (k) { return k; }, trackedVisitors)));
                var dv = el('div'), wd = {};
                L.widths.forEach(function (n, i) { wd[WIDTHS[i]] = n; });
                add(dv, plain(L.devices, function (k) { return k; }, trackedVisitors), el('h3', 'sub', 'Width of the screen'), barList(wd, { total: trackedVisitors, valueOf: function (e) { return e[1]; }, label: function (k) { return k; }, shown: 5, keepOrder: true }));
                add(left, panelOf('Devices and screen size', 'A phone, a tablet or a desktop, and the width of the window.', dv));
                add(left, panelOf('Languages', 'The language the browser asks for.', plain(L.langs, languageName, trackedVisitors)));
                add(grid, left, right);
                add(body, grid);
            }
            function timeOnSite(L) {
                var wrap = el('div'), cols = el('div', 'hist'), total = sumOf([{ n: 0 }], 'n'), groups = DUR_GROUPS.map(function (g) { var n = 0; for (var i = g[1]; i < g[2]; i++) n += L.dur[i] || 0; return n; });
                total = groups.reduce(function (a, b) { return a + b; }, 0);
                var mid = L.dur_mid, midGroup = -1, edges = [0].concat(DUR_EDGES);
                DUR_GROUPS.forEach(function (g, i) { if (mid >= edges[g[1]] && (g[2] >= edges.length || mid < edges[g[2]])) midGroup = i; });
                var max = Math.max.apply(null, groups.concat([1]));
                groups.forEach(function (n, i) {
                    var col = el('div', 'hcol' + (i === midGroup ? ' mid' : '')), barBox = el('div', 'hbar'), fill = el('i');
                    fill.style.height = Math.round(n * 100 / max) + '%';
                    add(barBox, fill);
                    add(col, el('span', 'hp', percent(n, total)), barBox, el('span', 'hl', DUR_GROUPS[i][0]));
                    add(cols, col);
                });
                add(wrap, el('p', 'hsum', total ? 'Middle: ' + duration(L.dur_mid) + '   Average: ' + duration(L.dur_sum / total) : 'No visit yet.'), cols);
                return wrap;
            }

            // ---- Health ----
            function bodyWidth() { return body.clientWidth || 944; }                    // (what the page is as wide as: a chart is drawn that wide; node has no layout and says 944)
            function health(d) {
                var days = d.days, versions = [], seen = {}, columns = bodyWidth() < 700 ? 1 : 2;
                days.forEach(function (day, j) {                          // the days that a version appears which no earlier day of the range had (the first day's are not new: nothing is known before it)
                    var known = Object.keys(seen).length, fresh = false;
                    Object.keys(day.vers || {}).forEach(function (v) { if (!seen[v] && known) fresh = true; seen[v] = true; });
                    if (fresh) versions.push(j);
                });
                var grid = el('div', 'hgrid');
                function timing(name, title, hint) {
                    var p = el('section', 'panel'), chart = lineChart(days, name, versions, columns === 2 ? (bodyWidth() - 18) / 2 - 44 : bodyWidth() - 44), mid = weightedMiddle(days, name, 1), slow = weightedMiddle(days, name, 3);
                    add(p, el('h2', null, title));
                    var key = el('div', 'alegend');
                    add(key, add(el('span'), el('i', 'ln l50'), doc.createTextNode('p50: half of the visits')), add(el('span'), el('i', 'ln l75'), doc.createTextNode('p75')), add(el('span'), el('i', 'ln l95'), doc.createTextNode('p95: the slowest 1 in 20')));
                    add(p, key);
                    if (!chart) { add(p, el('p', 'none', 'Not enough timings yet (a day needs ' + PERF_MIN + ').'), el('p', 'ph', hint)); return p; }
                    add(p, add(el('p', 'hsum'), doc.createTextNode('Half under ' + seconds(mid) + '  '), el('small', null, 'slowest 1 in 20: ' + seconds(slow))), add(el('div', 'chartbox'), chart), el('p', 'ph', hint));
                    return p;
                }
                add(grid, timing('load', 'Page load', 'From pressing a link to a page that works.'), timing('game', 'Game download and start', 'From pressing Play to the first picture of the game. The first visit after a new version loads it all again.'));
                add(body, grid);
                var marks = versions.length;
                if (marks) add(body, el('p', 'anote', 'The dotted vertical lines are days a new version went live (' + marks + ' in this range): the first visit after one loads everything again, so the numbers go up that day.'));
                add(body, errorList(d), counterPanel(d));
            }
            function errorList(d) {
                var p = h.panel('Errors, grouped', null), total = d.errors.reduce(function (a, e) { return a + e.n; }, 0);
                var h2 = p.querySelector ? p.querySelector('h2') : null;
                if (h2) add(h2, doc.createTextNode(' '), el('span', 'n', plural(total, 'error', 'errors') + ', ' + plural(d.errors.length, 'kind', 'kinds')));
                add(p, el('p', 'ph', 'Errors that the pages and the game caught in a visitor’s browser. Same message and same place count as one kind.'));
                if (!d.errors.length) return add(p, el('p', 'none', 'No error in this range.'));
                var list = el('div', 'elist'), shown = st.all ? d.errors : d.errors.slice(0, ERRORS_SHOWN);
                add(list, add(el('div', 'erow ehead'), el('span', null, 'Where'), el('span', null, 'Message'), el('span', 'nc', 'Times'), el('span', 'nc', 'Visitors'), el('span', null, 'First seen'), el('span', null, 'Last seen')));
                shown.forEach(function (e) {
                    var row = el('div', 'erow'), msg = el('div', 'emsg'), fresh = ordinal(e.first) >= ordinal(today) - 1 && ordinal(e.first) > ordinal(d.from);
                    add(msg, add(el('div'), h.typed('span', 'etext', e.msg), fresh ? el('span', 'tg warn', 'New') : null), e.src ? h.typed('code', 'esrc', e.src) : el('code', 'esrc dim', 'file not named'));
                    add(row, el('span', 'tg ' + (e.kind === 'Game' ? 'game' : 'page'), e.kind), msg, el('span', 'nc big', thousands(e.n)), el('span', 'nc', thousands(e.visitors)), el('span', null, shortDay(e.first)), el('span', null, e.last ? lastDay(e.last) : ''));
                    add(list, row);
                });
                add(p, list);
                var foot = el('div', 'efoot');
                add(foot, el('span', null, 'Showing ' + (st.all || d.errors.length <= ERRORS_SHOWN ? 'the ' + plural(d.errors.length, 'kind', 'kinds') + ' of this range' : 'the first ' + ERRORS_SHOWN + ' of ' + d.errors.length + ' kinds') + '. Only a message that is cleaned of addresses, names and quoted text and the site’s own file and line are kept, with no name, address or page address.'));
                if (!st.all && d.errors.length > ERRORS_SHOWN) add(foot, h.button('Show all', 'sm ghost', function () { st.all = true; draw(); }));
                return add(p, foot);
            }
            function lastDay(day) { return day === today ? 'today' : day === addDays(today, -1) ? 'yesterday' : shortDay(day); }    // (an error keeps the day it was last seen, never the time)
            function lastSeen(seconds) { var day = pacificDay(seconds * 1000); return day === today ? 'today, ' + clock(seconds) : day === addDays(today, -1) ? 'yesterday' : shortDay(day); }
            function counterPanel(d) {
                var c = d.counter, p = h.panel('The counter itself', 'Is the counting working?');
                function kv(k, v, small) {
                    var r = el('div', 'kv'), vv = el('span', 'v', v);
                    if (small) add(vv, doc.createTextNode(' '), el('small', null, small));
                    return add(r, el('span', 'k', k), vv);
                }
                add(p, kv('Last event received', c.last_at ? lastSeen(c.last_at) : 'none yet'),
                    kv('Events today', thousands(c.accepted_today), 'since midnight'),
                    kv('Events refused', thousands(c.refused_today) + ' today', 'too big or the wrong shape'),
                    kv('Crawlers left out', thousands(c.crawlers_today) + ' today', 'not counted'),
                    c.dropped_today ? kv('Over the day\u2019s limit', thousands(c.dropped_today) + ' today', 'dropped: a flood?') : null,
                    kv('Raw events kept', c.raw_days + ' days', c.oldest_raw ? 'back to ' + shortDay(c.oldest_raw) : null),
                    kv('Daily totals kept', c.first_day ? 'since ' + fullDay(c.first_day) : 'nothing yet'),
                    kv('Country list', c.geo ? c.geo.name : 'none', c.geo ? 'updated ' + c.geo.month : 'every country is unknown'));
                return p;
            }

            drawNav();
            draw();
            return load();
        }
        return { render: render };
    }

    return { make: make, pacificDay: pacificDay, addDays: addDays, ordinal: ordinal, rangeOf: rangeOf, rangeWords: rangeWords, customProblem: customProblem, change: change, percent: percent, thousands: thousands, duration: duration, seconds: seconds,
             csv: csv, cell: cell, crc32: crc32, zip: zip, files: files, niceStep: niceStep, weightedMiddle: weightedMiddle, countryName: countryName, languageName: languageName, isWeekend: isWeekend, longDay: longDay, shortDay: shortDay, DUR_GROUPS: DUR_GROUPS, WIDTHS: WIDTHS };
});
