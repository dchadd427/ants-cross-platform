// Runs the OWN code of the activity curve (web/replay_activity.js) on a small STRICT fake of the browser's DOM (an element with no innerHTML): the field `action` as the server writes it (a text that the C++ program's
// layout makes, and ones made here), what a damaged field does (no curve, never a throw), the deaths-only curve of a record that has no action, the weights, the buckets, the curve and its label, the curve on the page's
// timeline (a computer: nothing at rest, on while the pointer is over the timeline; a touch screen: faint at rest, solid while a finger is on it), the played part, the page's one call (mount) and the page's
// record handler (AntsHistory.replayStats gives the record to onRecord). tests/scripts/test_web_activity.py runs it with node (the quick tier), and again on broken copies, which it must refuse.
// usage: node web_activity_check.js web/replay_activity.js web/history_page.js
'use strict';
const path = require('path');
const [activityPath, historyPath] = process.argv.slice(2).map((p) => path.resolve(p));
global.AntsHistory = require(historyPath);                                       // (the page loads history_page.js first)
const A = require(activityPath);
let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }
function throws(fn) { try { fn(); return null; } catch (e) { return e; } }

// ---- a strict fake DOM: elements with text, attributes, style, a size and listeners ----
class Node {
    constructor(tag, text) {
        this.tag = tag; this.children = []; this.parentNode = null; this.className = ''; this.attrs = {}; this._text = text; this.listeners = {}; this.offsetWidth = 100; this.offsetHeight = 40;
        this.style = {};
    }
    set innerHTML(v) { throw new Error('innerHTML is never used'); }
    appendChild(c) { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = this; this.children.push(c); return c; }
    removeChild(c) { const i = this.children.indexOf(c); if (i >= 0) this.children.splice(i, 1); c.parentNode = null; return c; }
    setAttribute(k, v) { this.attrs[k] = String(v); if (k === 'class') this.className = String(v); }
    getAttribute(k) { return this.attrs[k]; }
    addEventListener(type, fn) { (this.listeners[type] = this.listeners[type] || []).push(fn); }
    fire(type, ev) { (this.listeners[type] || []).forEach((fn) => fn(Object.assign({ pointerType: 'mouse', clientX: 0 }, ev))); }
    getBoundingClientRect() { return { left: this.left || 0, top: 0, width: this.width === undefined ? 800 : this.width, height: 22 }; }
    set textContent(t) { this.children.forEach((c) => { c.parentNode = null; }); this.children = []; this._text = String(t); }
    get textContent() { return (this._text === undefined ? '' : this._text) + this.children.map((c) => c.textContent).join(''); }
    all(test, out = []) { this.children.forEach((c) => { if (test(c)) out.push(c); c.all(test, out); }); return out; }
    byClass(cls) { return this.all((n) => n.className.split(' ').includes(cls)); }
    byTag(tag) { return this.all((n) => n.tag === tag); }
    get classList() {
        const n = this;
        return {
            add(c) { if (!n.className.split(' ').includes(c)) n.className = (n.className + ' ' + c).trim(); },
            remove(c) { n.className = n.className.split(' ').filter((x) => x !== c).join(' '); },
            contains(c) { return n.className.split(' ').includes(c); }
        };
    }
}
const document = { createElement: (t) => new Node(t), createElementNS: (ns, t) => new Node(t), createTextNode: (t) => new Node('#text', t) };
const has = (n, c) => n.className.split(' ').includes(c);

// ---- the field as a server writes it. `letters` is this file's own encoder of the layout (docs/REPLAYS.md "The activity curve"); the literal below is the text of the C++ program's own test of the same layout ----
const L = A.LETTERS;
const letters = (n, spec) => { const a = new Array(n).fill('A'); Object.keys(spec || {}).forEach((i) => { a[+i] = spec[i]; }); return a.join(''); };       // n letters, all A but those at the given places
const pad = (head, n) => head + 'A'.repeat(n - head.length);
function field(n, s, kinds) { return { v: 1, s, n, death: pad(kinds.death || '', n), bomb: pad(kinds.bomb || '', n), theft: pad(kinds.theft || '', n), fire: pad(kinds.fire || '', n), hit: pad(kinds.hit || '', n) }; }
const SERVER = { v: 1, s: 5, n: 24, death: pad('BADAA_', 24), bomb: pad('AC', 24), theft: pad('AAAa', 24), fire: pad('AAAAb', 24), hit: pad('AAAAAA_', 24) };      // (what make_history_action makes of DD7.2's match)
const SEATS = [0, 1, 2, 3].map((i) => ({ seat: i, color: ['Green', 'Red', 'Blue', 'Black'][i], name: ['Gory', 'Rhea', 'Jaro', 'Anton'][i], bot: i ? 'Hard' : '', score: 0, killed: 0, lost: 0, hatched: 0, bombs_planted: 0, bombs_defused: 0, fires_lit: 0, left: false, winner: false }));
// a match of 8 minutes (96 buckets of 5 seconds): a death and a bomb at 0:50, three deaths and two hits at 5:00 (the biggest), a bomb after it
const ACTION = { v: 1, s: 5, n: 96, death: letters(96, { 10: 'B', 60: 'D' }), bomb: letters(96, { 10: 'B', 61: 'B' }), theft: letters(96), fire: letters(96), hit: letters(96, { 60: 'C' }) };
function record(extra) { return Object.assign({ v: 1, id: 'ants-TREASURE-20261008-143209Z', ended: 1792000000, map: 'TREASURE.LVL', game: 'v0.21.0', turns: 9600, seconds: 480, finished: true, quitter: null, mode: 'highest-score', format: 'ffa4', draw: false, result: 'decided', seats: SEATS, rows: [], recording: 'watch', file: 'x.antsrep', keep_days: 30, action: ACTION }, extra); }
// a deaths field (one by one) with ants dying at the given seconds
function put(v, n) { let s = ''; for (let i = 0; i < n; i++) s += L[Math.floor(v / Math.pow(64, i)) % 64]; return s; }
function deaths(seconds, w, h) { return { v: 1, w: w || 60, h: h || 60, n: seconds.length, k: 'x', d: seconds.map((t) => put(3, 2) + put(4, 2) + put(t, 3) + put(0, 2)).join('') }; }

// ---- reading the data ----
{
    const a = A.actionOf(SERVER);
    same('a field that the server wrote: its bucket, how many there are and the counts of every kind', a && [a.seconds, a.buckets, a.counts.death.slice(0, 7), a.counts.bomb.slice(0, 3), a.counts.theft.slice(2, 5), a.counts.fire.slice(3, 6), a.counts.hit.slice(5, 8)],
        [5, 24, [1, 0, 3, 0, 0, 63, 0], [0, 2, 0], [0, 26, 0], [0, 27, 0], [0, 63, 0]]);
    same('... a letter is its place in A-Z a-z 0-9 - _ (A none, B 1, a 26, 0 52, - 62, _ 63)', [A.actionOf(field(2, 5, { death: 'BA', bomb: 'a0', theft: '-_' })).counts].map((c) => [c.death, c.bomb, c.theft])[0], [[1, 0], [26, 52], [62, 63]]);
    const refused = (label, d) => check('a damaged field makes no curve and throws nothing: ' + label, throws(() => A.actionOf(d)) === null && A.actionOf(d) === null);
    const good = field(4, 5, { death: 'AB' });
    refused('nothing', undefined); refused('a text', 'x'); refused('an array', []); refused('another version', Object.assign({}, good, { v: 2 })); refused('no version', Object.assign({}, good, { v: undefined }));
    refused('a bucket of 4 seconds', Object.assign({}, good, { s: 4 })); refused('a bucket of 0', Object.assign({}, good, { s: 0 })); refused('a bucket that is not a multiple of 5', Object.assign({}, good, { s: 7 }));
    refused('a bucket of a fraction', Object.assign({}, good, { s: 5.5 })); refused('a bucket that is a text', Object.assign({}, good, { s: '5' })); refused('a bucket beyond 5 x 2^20 seconds', Object.assign({}, good, { s: 5 * 1048576 + 5 }));
    refused('no bucket', Object.assign({}, good, { n: 0 })); refused('no bucket and no letters', field(0, 5, {})); refused('241 buckets', field(241, 5, {})); refused('a count of buckets that is a fraction', Object.assign({}, good, { n: 4.5 })); refused('a count that is a text', Object.assign({}, good, { n: '4' }));
    refused('a count that is not finite', Object.assign({}, good, { n: Infinity }));
    refused('a kind that is not as long as n', Object.assign({}, good, { hit: 'AAA' })); refused('... or longer', Object.assign({}, good, { hit: 'AAAAA' })); refused('a kind that is missing', Object.assign({}, good, { fire: undefined }));
    refused('a kind that is a number', Object.assign({}, good, { bomb: 4 })); refused('a kind that is a list of letters', Object.assign({}, good, { hit: ['A', 'A', 'A', 'A'] })); refused('a letter that is not one of the 64 (+)', Object.assign({}, good, { death: 'A+AA' })); refused('... (!)', Object.assign({}, good, { theft: 'AA!A' }));
    refused('... (a space)', Object.assign({}, good, { fire: 'A AA' })); refused('... (a letter of another alphabet)', Object.assign({}, good, { hit: 'AéAA' }));
    check('240 buckets is a field and a bucket of 5 x 2^20 seconds too', A.actionOf(field(240, 5, {})) !== null && A.actionOf(field(2, 5 * 1048576, {})) !== null);
}
{
    same('the weights are the Auto camera\'s: a death 14, a bomb 10, a theft 6, a fire 5, a hit 2', A.KINDS.map((k) => A.WEIGHTS[k]), [14, 10, 6, 5, 2]);
    const s = A.seriesOf(record({ seconds: 50, action: field(10, 5, { death: 'AAB', bomb: 'AAC', theft: 'AAD', fire: 'AAE', hit: 'AAF' }) }));
    same('a bucket weighs what the Auto camera weighs: 1 death, 2 bombs, 3 thefts, 4 fires and 5 hits are 14 + 20 + 18 + 20 + 10', s && s.weight.slice(0, 4), [0, 0, 82, 0]);
    same('... and the series says its buckets, their length and the match\'s seconds', s && [s.seconds, s.buckets, s.total, s.deathsOnly], [5, 10, 50, false]);
    check('a field of nothing but zeros is no curve', A.seriesOf(record({ action: field(10, 5, {}) })) === null);
    same('the match\'s seconds come from the record (seconds, else the turns, else the buckets)', [A.seriesOf(record({ seconds: 600 })).total, A.seriesOf(record({ seconds: undefined, turns: 4000 })).total, A.seriesOf(record({ seconds: undefined, turns: undefined })).total], [600, 200, 480]);
    same('the buckets of a match: 5 seconds up to 20 minutes, then 10, 20, 40 ... until at most 240 hold it', [A.shape(1).buckets, A.shape(480), A.shape(1200), A.shape(1201), A.shape(2400), A.shape(2401), A.shape(4800), A.shape(4801), A.shape(8 * 3600)],
        [1, { seconds: 5, buckets: 96 }, { seconds: 5, buckets: 240 }, { seconds: 10, buckets: 121 }, { seconds: 10, buckets: 240 }, { seconds: 20, buckets: 121 }, { seconds: 20, buckets: 240 }, { seconds: 40, buckets: 121 }, { seconds: 160, buckets: 180 }]);
}
{
    const old = record({ action: undefined, deaths: deaths([5, 6, 12, 479, 480, 481]) });
    const s = A.seriesOf(old);
    same('a record with no action gets the deaths alone: each ant in the bucket of its second (5 and 6 are the second bucket, 12 the third, the last second the last bucket)', s && [s.deathsOnly, s.buckets, s.counts.death[1], s.counts.death[2], s.counts.death[95], s.counts.death.reduce((x, y) => x + y, 0)], [true, 96, 2, 1, 3, 6]);
    same('... and nothing else is counted, so a bucket weighs 14 for each death', s && [s.counts.bomb.every((n) => n === 0), s.counts.hit.every((n) => n === 0), s.weight[1], s.weight[0]], [true, true, 28, 0]);
    check('... the action wins when a record has both', (() => { const both = A.seriesOf(record({ deaths: deaths([5]) })); return both && both.deathsOnly === false && both.weight[10] === 24; })());
    check('... an action that is damaged is not believed: the deaths are drawn', (() => { const d = A.seriesOf(record({ action: Object.assign({}, SERVER, { n: 5 }), deaths: deaths([100]) })); return d && d.deathsOnly === true && d.counts.death[20] === 1; })());
    check('... deaths in squares have no second: no curve', A.seriesOf(record({ action: undefined, deaths: { v: 1, w: 60, h: 60, n: 4, k: 'b', s: 4, d: 'AAAAAAAAA' } })) === null);
    check('... a deaths field that is damaged is no curve and throws nothing', (() => { const bad = (d) => throws(() => A.seriesOf(record({ action: undefined, deaths: d }))) === null && A.seriesOf(record({ action: undefined, deaths: d })) === null; return bad(deaths([5]).d) && bad(Object.assign(deaths([5]), { v: 2 })) && bad(Object.assign(deaths([5]), { d: deaths([5]).d + 'A' })) && bad(Object.assign(deaths([5]), { d: 'AAAA+AAAA' })) && bad(Object.assign(deaths([5]), { d: deaths([5]).d + 'AAAA+AAAA' })) && bad(Object.assign(deaths([5]), { d: '' })) && bad(Object.assign(deaths([5]), { d: 'A'.repeat(9 * 401) })); })());
    check('... a record with neither is no curve', A.seriesOf(record({ action: undefined })) === null && A.seriesOf(null) === null && A.seriesOf('x') === null && A.seriesOf(undefined) === null);
    check('... more than 63 deaths in a bucket is 63 (a deaths field holds at most 400 ants)', (() => { const many = A.seriesOf(record({ action: undefined, deaths: deaths(new Array(70).fill(7)) })); return many.counts.death[1] === 63; })());
    same('a long match has longer buckets: a death at 1,300 seconds of 1,500 is in the bucket 130 of 10 seconds', (() => { const l = A.seriesOf(record({ action: undefined, seconds: 1500, deaths: deaths([1300]) })); return [l.seconds, l.buckets, l.counts.death[130]]; })(), [10, 150, 1]);
}

// ---- the curve ----
{
    const s = A.seriesOf(record());
    const c = A.curveOf(s);
    check('the curve is a path from the left edge to the right edge of a box 1000 wide, and its area is closed along the bottom', /^M0\.00,/.test(c.line) && /1000\.00,[\d.]+$/.test(c.line) && c.area === c.line + ' L1000,100 L0,100 Z', c.line.slice(-40));
    const peak = c.at(60.5 / 96), quiet = c.at(0.9), early = c.at(10.5 / 96);
    check('the highest place is at the top and a quiet place at the bottom (y 100 is no action, 12 the most)', Math.abs(peak - 12) < 0.01 && Math.abs(quiet - 100) < 0.01 && early > 12 && early < 100, peak + ' ' + quiet + ' ' + early);
    check('the line is flat at both ends (it goes on as its first and last bucket)', Math.abs(c.at(0) - 100) < 0.01 && Math.abs(c.at(1) - 100) < 0.01);
    check('the line never goes above the highest point or below the bottom (no overshoot) whatever place is asked', (() => { for (let i = 0; i <= 2000; i++) { const y = c.at(i / 2000); if (!(y >= 12 - 1e-6 && y <= 100 + 1e-6)) return false; } return true; })());
    check('the height grows with the weight but flatter than the weight (a power of 0.8): a bucket of a third of the highest weight is above a third of the way up', (() => { const t = A.seriesOf(record({ seconds: 150, action: field(30, 5, { death: letters(30, { 5: 'B', 20: 'D' }) }) })); const p = A.curveOf(t); const low = 100 - p.at(5.5 / 30), high = 100 - p.at(20.5 / 30); return high > 87.9 && low / high > 0.4 + 1e-3 && low / high < 0.6; })());
    check('the line is the one that the path draws: between two buckets its height is the middle of the path\'s own curve there', (() => {
        const nums = c.line.match(/-?\d+\.\d+/g).map(Number), segs = [];
        for (let i = 2; i + 5 < nums.length; i += 6) segs.push([nums[i - 2], nums[i - 1], nums[i], nums[i + 1], nums[i + 2], nums[i + 3], nums[i + 4], nums[i + 5]]);
        return [20, 59, 60, 61].every((k) => { const g = segs[k], mid = (g[0] + 3 * g[2] + 3 * g[4] + g[6]) / 8, y = (g[1] + 3 * g[3] + 3 * g[5] + g[7]) / 8; return Math.abs(c.at(mid / 1000) - y) < 0.05; });
    })());
    const flat = A.curveOf(A.seriesOf(record({ action: field(1, 5, { death: 'B' }) })));
    check('one bucket is a flat line', flat !== null && Math.abs(flat.at(0.2) - flat.at(0.8)) < 1e-9 && throws(() => flat.at(0.5)) === null);
    check('a blur makes a hill of a needle: the buckets beside a death are not quiet and the far ones are', (() => { const t = A.seriesOf(record({ seconds: 150, action: field(30, 5, { death: pad('', 15) + 'B' + pad('', 14) }) })); const p = A.curveOf(t); return p.at(15.5 / 30) < 13 && p.at(13.5 / 30) < 99 && p.at(2.5 / 30) > 99.9; })());
    check('a match whose last bucket is short has its last middle at the end of the match (no point beyond the box)', (() => { const t = A.seriesOf(record({ seconds: 482, action: field(97, 5, { death: pad('', 96) + 'B' }) })); const p = A.curveOf(t); const nums = p.line.match(/-?\d+\.\d+/g).map(Number); return nums.every((n, i) => i % 2 === 1 || n <= 1000.01) && isFinite(p.at(0.999)); })());
}

// ---- the label ----
{
    const s = A.seriesOf(record({ action: SERVER, seconds: 120, turns: 2400 }));
    same('the label: the time and what the bucket holds (counts, in the order death, bomb, theft, fire; a plural for more than one)', [A.tipOf(s, 2), A.tipOf(s, 14), A.tipOf(s, 5), A.tipOf(s, 17)], [
        { time: '0:02', what: '1 death' }, { time: '0:14', what: '3 deaths' }, { time: '0:05', what: '2 bombs' }, { time: '0:17', what: '26 thefts' }]);
    same('... one of each is singular and the fires come last, 63 is 63', [A.tipOf(A.seriesOf(record({ action: field(10, 5, { death: 'B', bomb: 'B', theft: 'B', fire: 'B', hit: 'B' }) })), 1), A.tipOf(s, 21)], [{ time: '0:01', what: '1 death, 1 bomb, 1 theft, 1 fire' }, { time: '0:21', what: '27 fires' }]);
    same('... a bucket with nothing says quiet, and with fights alone (3 hits or more) says fighting, two hits are quiet', [A.tipOf(s, 100).what, A.tipOf(A.seriesOf(record({ action: field(10, 5, { death: 'AAAAAAAAB', hit: 'DAAAAAAAAA' }) })), 1).what, A.tipOf(A.seriesOf(record({ action: field(10, 5, { death: 'AAAAAAAAB', hit: 'CAAAAAAAAA' }) })), 1).what], ['quiet', 'fighting', 'quiet']);
    same('... the very end is in the last bucket (a match of 50 seconds with a death in its last 5)', A.tipOf(A.seriesOf(record({ seconds: 50, action: field(10, 5, { death: 'AAAAAAAAAB' }) })), 50), { time: '0:50', what: '1 death' });
    same('... a time before the start and one after the end are the first and the last moment', [A.tipOf(s, -50).time, A.tipOf(s, 9999).time, A.tipOf(s, 9999).what], ['0:00', '2:00', 'quiet']);
    same('... a long match (buckets of 20 seconds) names the bucket of the moment', (() => { const l = A.seriesOf(record({ seconds: 4000, action: field(200, 20, { death: pad('', 7) + 'D' }) })); return [A.tipOf(l, 141).what, A.tipOf(l, 139).what, A.tipOf(l, 160).what]; })(), ['3 deaths', 'quiet', 'quiet']);
    same('... a record of deaths alone says no deaths for a bucket with none (fights are not known)', [A.tipOf(A.seriesOf(record({ action: undefined, deaths: deaths([5]) })), 100).what, A.tipOf(A.seriesOf(record({ action: undefined, deaths: deaths([5]) })), 5).what], ['no deaths', '1 death']);
    same('the clock', [A.clock(0), A.clock(59), A.clock(60), A.clock(161), A.clock(3599), A.clock(7200), A.clock(-3), A.clock(NaN), A.clock('x'), A.clock(4.9)], ['0:00', '0:59', '1:00', '2:41', '59:59', '120:00', '0:00', '0:00', '0:00', '0:04']);
}

// ---- the curve on the page's timeline ----
function page(width) {
    const bar = new Node('div'), tl = new Node('div'), rail = new Node('div');
    rail.width = width === undefined ? 800 : width; rail.left = 11;
    tl.appendChild(rail); bar.appendChild(tl);
    return { bar, tl, rail };
}
const series = A.seriesOf(record());
{
    const p = page();
    check('a series with no curve changes nothing and gives no handle', A.attach({ document, rail: p.rail, tl: p.tl, bar: p.bar, series: null, touch: () => false }) === null && p.rail.children.length === 0 && p.bar.className === '');
    const h = A.attach({ document, rail: p.rail, tl: p.tl, bar: p.bar, series, touch: () => false });
    const heat = p.rail.byClass('heat')[0], line = p.rail.byClass('hline')[0], dot = p.rail.byClass('hdot')[0], tip = p.rail.byClass('htip')[0];
    check('the curve is one element in the rail with its svg, its line, its dot and its label, hidden from a screen reader, and the bar says it has a curve', !!heat && heat.parentNode === p.rail && heat.getAttribute('aria-hidden') === 'true' && !!line && !!dot && !!tip && has(p.bar, 'has-activity') && heat.byTag('svg').length === 1);
    check('... three paths (the part to come, the part played and the line) and two clips', heat.byTag('path').length === 3 && heat.byTag('clipPath').length === 2 && heat.byTag('rect').length === 2);
    check('a computer at rest shows nothing: not on, not faint, the tag stays', !has(heat, 'on') && !has(heat, 'faint') && !has(heat, 'hov') && !has(p.bar, 'heathov'));
    p.tl.fire('pointermove', { clientX: 100 });
    check('... the pointer moving over the timeline before it entered is not a hover (the curve waits for the enter)', !has(heat, 'on'));
    p.tl.fire('pointerenter', { clientX: 11 + 400 });
    check('the pointer entering the timeline shows it: on, with the line and the dot, and the tag gives way', has(heat, 'on') && has(heat, 'hov') && !has(heat, 'faint') && has(p.bar, 'heathov'));
    check('... at the middle of the timeline: the line is there, and the label says the moment (4:00 of 8:00) and what happened then', line.style.left === '400px' && dot.style.left === '400px' && tip.textContent === '4:00 \u00b7 quiet', line.style.left + ' ' + tip.textContent);
    p.tl.fire('pointermove', { clientX: 11 + 160 });
    check('... it follows the pointer: 160 of 800 is a fifth of the match, 1:36', line.style.left === '160px' && tip.textContent.startsWith('1:36'), line.style.left + ' ' + tip.textContent);
    p.tl.fire('pointermove', { clientX: 11 + 800 * 52 / 480 });
    check('... in the bucket with the death and the bomb the label says so', tip.textContent === '0:52 \u00b7 1 death, 1 bomb', tip.textContent);
    p.tl.fire('pointermove', { clientX: 11 + 800 * 301 / 480 });
    check('... and in the biggest one, 3 deaths (the hits are not named)', tip.textContent === '5:01 \u00b7 3 deaths', tip.textContent);
    p.tl.fire('pointermove', { clientX: 11 - 300 });
    check('... a pointer left of the rail is the first moment, and one far right the last', line.style.left === '0px' && tip.textContent.startsWith('0:00'));
    p.tl.fire('pointermove', { clientX: 11 + 5000 });
    check('... right of it is the end', line.style.left === '800px' && tip.textContent.startsWith('8:00'));
    check('... the label stays inside the bar, a little past the rail\'s ends: its middle is at least half its width (50) less 11 from the left', (() => { p.tl.fire('pointermove', { clientX: 11 }); return tip.style.left === '39px'; })(), tip.style.left);
    check('... and the same at the right end', (() => { p.tl.fire('pointermove', { clientX: 11 + 800 }); return tip.style.left === '761px'; })(), tip.style.left);
    check('... the dot is on the curve at the pointer, as a part of the height', (() => { p.tl.fire('pointermove', { clientX: 11 + 800 * 10.5 / 96 }); return Math.abs(parseFloat(dot.style.top) - A.curveOf(series).at(10.5 / 96)) < 1e-9 && dot.style.top.endsWith('%'); })(), dot.style.top);
    p.tl.fire('pointerleave', {});
    check('the pointer leaving hides it again', !has(heat, 'on') && !has(heat, 'hov') && !has(heat, 'faint') && !has(p.bar, 'heathov'));
    p.tl.fire('pointerdown', { clientX: 11 + 200 });
    check('a press with the mouse shows it as well, and the move after it (the drag) is followed', has(heat, 'on') && line.style.left === '200px');
    p.tl.fire('pointerup', {});
    check('... and the release with the pointer gone leaves nothing', !has(heat, 'on'));
    p.tl.fire('pointerenter', { clientX: 11 + 100 }); p.tl.fire('pointerdown', { clientX: 11 + 100 }); p.tl.fire('pointerup', {});
    check('... while the pointer is still on the timeline it stays on', has(heat, 'on'));
    p.tl.fire('pointerleave', {}); p.tl.fire('pointerdown', { clientX: 11 + 100 }); p.tl.fire('pointercancel', {});
    check('a press that is cancelled ends the same way', !has(heat, 'on'));
    h.played(25);
    const clips = heat.byTag('rect');
    same('the part played: the colored part is the first quarter, the rest is the other three', [clips[0].attrs.width, clips[1].attrs.x, clips[1].attrs.width], ['250', '250', '750']);
    h.played(0); same('... at the start nothing is played', [clips[0].attrs.width, clips[1].attrs.x, clips[1].attrs.width], ['0', '0', '1000']);
    h.played(100); same('... at the end all of it', [clips[0].attrs.width, clips[1].attrs.x, clips[1].attrs.width], ['1000', '1000', '0']);
    h.played(250); same('... a percent beyond the end is the end', clips[0].attrs.width, '1000');
    h.played(-5); same('... one before the start is the start', clips[0].attrs.width, '0');
    h.played(NaN); same('... a number that is not one is the start', clips[0].attrs.width, '0');
    h.played('50'); same('... a text is not a number', clips[0].attrs.width, '0');
    check('nothing here is markup: the label is text', tip.children.length === 2 && tip.children[0].tag === 'b' && tip.children[1].tag === '#text');
}
{
    const p = page(0);
    const h = A.attach({ document, rail: p.rail, tl: p.tl, bar: p.bar, series, touch: () => false });
    const line = p.rail.byClass('hline')[0];
    p.tl.fire('pointerenter', { clientX: 11 + 40 });
    const dot = p.rail.byClass('hdot')[0];
    check('a rail with no width does not divide by zero: the line, the dot and the label are numbers', throws(() => p.tl.fire('pointermove', { clientX: 50 })) === null && /^[0-9.]+px$/.test(line.style.left) && isFinite(parseFloat(dot.style.top)) && !/NaN/.test(p.rail.byClass('htip')[0].textContent));
    check('... and a handle is still there', !!h);
}
{
    const p = page();
    let finger = true;
    A.attach({ document, rail: p.rail, tl: p.tl, bar: p.bar, series, touch: () => finger });
    const heat = p.rail.byClass('heat')[0], line = p.rail.byClass('hline')[0], tip = p.rail.byClass('htip')[0];
    check('a touch screen at rest shows it faint: not on, no line, the tag stays', has(heat, 'faint') && !has(heat, 'on') && !has(heat, 'hov') && !has(p.bar, 'heathov'));
    p.tl.fire('pointerenter', { pointerType: 'touch', clientX: 11 + 100 });
    check('a finger that is not pressing (the enter that goes with it) does nothing', has(heat, 'faint') && !has(heat, 'on'));
    p.tl.fire('pointermove', { pointerType: 'touch', clientX: 11 + 300 });
    check('... nor does a move without a press', has(heat, 'faint') && !has(heat, 'on') && line.style.left === undefined);
    p.tl.fire('pointerdown', { pointerType: 'touch', clientX: 11 + 160 });
    check('a finger on the timeline makes it solid, with the line and the label, and the tag gives way', has(heat, 'on') && has(heat, 'hov') && !has(heat, 'faint') && line.style.left === '160px' && tip.textContent.startsWith('1:36') && has(p.bar, 'heathov'), tip.textContent);
    p.tl.fire('pointermove', { pointerType: 'touch', clientX: 11 + 480 });
    check('... the label follows the finger', line.style.left === '480px' && tip.textContent.startsWith('4:48'), tip.textContent);
    p.tl.fire('pointerup', { pointerType: 'touch' });
    check('... and when the finger is lifted it is faint again (a touch screen has no hover to stay on)', has(heat, 'faint') && !has(heat, 'on') && !has(heat, 'hov') && !has(p.bar, 'heathov'));
    p.tl.fire('pointerleave', { pointerType: 'touch' });
    check('... a finger that leaves changes nothing', has(heat, 'faint'));
    p.tl.fire('pointerdown', { pointerType: 'touch', clientX: 11 + 20 }); p.tl.fire('pointercancel', { pointerType: 'touch' });
    check('... and a touch that is cancelled is the same as one that is lifted', has(heat, 'faint') && !has(heat, 'on'));
    finger = false;
    p.tl.fire('pointerdown', { pointerType: 'mouse', clientX: 11 + 20 }); p.tl.fire('pointerup', { pointerType: 'mouse' });
    check('a screen that turns out to have a pointer with hover (the page says so) is a computer again: nothing at rest', !has(heat, 'faint') && !has(heat, 'on'));
    finger = true;
    p.tl.fire('pointerdown', { pointerType: 'touch', clientX: 11 + 20 }); p.tl.fire('pointerup', { pointerType: 'touch' });
    check('... and one that the page says is a finger\'s is faint again', has(heat, 'faint'));
}
{
    const p = page();
    A.attach({ document, rail: p.rail, tl: p.tl, bar: p.bar, series, touch: () => { throw new Error('no media queries'); } });
    check('a page that cannot say whether it is a touch screen is a computer (and throws nothing)', !has(p.rail.byClass('heat')[0], 'faint'));
    const q = page();
    A.attach({ document, rail: q.rail, tl: q.tl, series, touch: () => true });
    check('a bar that is not given is fine', has(q.rail.byClass('heat')[0], 'faint') && throws(() => q.tl.fire('pointerdown', { pointerType: 'touch', clientX: 20 })) === null);
    check('... and so is a page with no touch function', throws(() => A.attach({ document, rail: page().rail, tl: page().tl, series })) === null && A.attach({ document, rail: null, tl: null, series }) === null);
}

// ---- the page's one call ----
function windowOf(opts) {
    const p = page();
    const els = { rbar: p.bar, tl: p.tl, rail: p.rail };
    const win = { document: { getElementById: (id) => els[id] || null, createElement: document.createElement, createElementNS: document.createElementNS, createTextNode: document.createTextNode }, antsFinger: opts.finger, resized: 0,
        matchMedia: opts.hoverNone === undefined ? undefined : (q) => ({ matches: q === '(hover: none)' ? opts.hoverNone : false }),
        Event: class { constructor(type) { this.type = type; } }, dispatchEvent(ev) { if (ev.type === 'resize') this.resized++; } };
    Object.defineProperty(p.bar, 'offsetHeight', { get() { return has(p.bar, 'has-activity') && opts.taller ? 130 : 100; } });
    return { win, p };
}
{
    const w = windowOf({ finger: false, hoverNone: false, taller: false });
    const h = A.mount(w.win, record());
    check('the page\'s call puts the curve into the page\'s own timeline and keeps the handle where the page\'s paint finds it', !!h && w.win.antsReplayCurve === h && w.p.rail.byClass('heat').length === 1);
    check('... a computer with hover: nothing at rest, and the bar did not change its height, so the page is not told', !has(w.p.rail.byClass('heat')[0], 'faint') && w.win.resized === 0);
    const t = windowOf({ finger: false, hoverNone: true, taller: true });
    A.mount(t.win, record());
    check('a screen with no hover: faint at rest, and the bar got taller (the phone\'s band), so the page is told once, with a resize', has(t.p.rail.byClass('heat')[0], 'faint') && t.win.resized === 1);
    const f = windowOf({ finger: true, hoverNone: false, taller: false });
    A.mount(f.win, record());
    check('a finger that has touched the page: faint at rest', has(f.p.rail.byClass('heat')[0], 'faint'));
    const n = windowOf({ finger: false, taller: true });
    A.mount(n.win, record());
    check('a window that has no matchMedia is a computer (and nothing throws)', !has(n.p.rail.byClass('heat')[0], 'faint'));
    const none = windowOf({ finger: false, hoverNone: true, taller: true });
    const nothing = A.mount(none.win, record({ action: undefined }));
    check('a record with nothing to draw: no handle, the page keeps no curve, nothing added, nobody told', nothing === null && none.win.antsReplayCurve === null && none.p.rail.children.length === 0 && none.win.resized === 0);
}

// ---- the page's record handler: AntsHistory.replayStats gives the record to onRecord ----
(async function () {
    const ID = 'ants-TREASURE-20261008-143209Z';
    const quietDoc = { getElementById: () => null, createElement: document.createElement };
    const answer = (rec, ok) => () => Promise.resolve({ ok: ok !== false, json: () => Promise.resolve(rec) });
    const settle = () => new Promise((r) => setTimeout(r, 20));
    let given = [];
    AntsHistory.replayStats({ document: quietDoc, fetch: answer(record()), file: ID + '.antsrep', onRecord: (rec) => given.push(rec) });
    await settle();
    check('the record of the match on show is given to the page\'s handler once', given.length === 1 && given[0].id === ID && given[0].action.v === 1);
    given = [];
    AntsHistory.replayStats({ document: quietDoc, fetch: answer(record({ id: 'ants-TINY-20261008-143209Z' })), file: ID + '.antsrep', onRecord: (rec) => given.push(rec) });
    AntsHistory.replayStats({ document: quietDoc, fetch: answer(record(), false), file: ID + '.antsrep', onRecord: (rec) => given.push(rec) });
    AntsHistory.replayStats({ document: quietDoc, fetch: answer(record({ seats: 'x' })), file: ID + '.antsrep', onRecord: (rec) => given.push(rec) });
    AntsHistory.replayStats({ document: quietDoc, fetch: () => Promise.reject(new Error('down')), file: ID + '.antsrep', onRecord: (rec) => given.push(rec) });
    AntsHistory.replayStats({ document: quietDoc, fetch: answer(record()), file: 'not-a-file', onRecord: (rec) => given.push(rec) });
    await settle();
    check('a record of another match, an answer that is not ok, one that is not a record, a server that is down and a name that is no recording give it nothing', given.length === 0);
    function panelPage(env) {
        const els = {};
        ['stats-btn', 'mstats', 'mstats-flow'].forEach((id) => { els[id] = new Node(id === 'stats-btn' ? 'button' : 'aside'); els[id].hidden = true; });
        const header = new Node('header'); header.getBoundingClientRect = () => ({ bottom: 60 });
        const doc = Object.assign(Object.create(document), { getElementById: (id) => els[id] || null, querySelector: (q) => (q === 'header' ? header : null), addEventListener() {}, defaultView: null });
        AntsHistory.replayStats(Object.assign({ document: doc, fetch: answer(record()), file: ID + '.antsrep' }, env));
        return els;
    }
    let threw = null;
    process.on('unhandledRejection', (e) => { threw = e; });
    const broken = panelPage({ onRecord: () => { throw new Error('the curve broke'); } });
    const plain = panelPage({});
    await settle();
    check('a handler that throws costs the panel nothing: the table is there, and nothing is thrown out of the page', broken.mstats.byTag('table').length === 1 && broken['mstats-flow'].byTag('table').length === 1 && threw === null);
    check('a page with no handler gets its panel as before', plain.mstats.byTag('table').length === 1 && threw === null);
    given = [];
    const both = panelPage({ onRecord: (rec) => given.push(rec.id) });
    await settle();
    check('a page with a handler gets the record and the panel both', given.length === 1 && both.mstats.byTag('table').length === 1);
    console.log((failures ? 'FAILED: ' + failures + ' of ' : 'ok: ') + checks + ' checks');
    process.exit(failures ? 1 : 0);
})();
