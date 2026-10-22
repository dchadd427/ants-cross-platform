// Runs the OWN code of the map "Where ants died" (web/death_map.js) with the real AntsHistory on a small STRICT fake of the browser's DOM (a node list that has a length and an index and nothing else: no
// filter, no map, no forEach; an element with no innerHTML): the field as a server writes it (a packed text that a C++ program made, and ones made here), what a damaged field does (nothing, never a throw), the glow
// on a canvas, the dots, the squares of a big map, the chips, the switch of the ants that drowned, the marks of the first blood and the last kill, the zoom, the tooltip, the sum of all the matches of a map, a name
// that is text and never markup, and a card that has no data. tests/scripts/test_web_death_map.py runs it with node (the quick tier), and again on broken copies, which it must refuse.
// usage: node web_death_map_check.js web/death_map.js web/history_page.js
'use strict';
const path = require('path');
const [mapPath, historyPath] = process.argv.slice(2).map((p) => path.resolve(p));
const H = require(historyPath);
global.AntsHistory = H;                                                         // (the page loads history_page.js first)
const DM = require(mapPath);
let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }
function throws(fn) { try { fn(); return null; } catch (e) { return e; } }

// ---- a strict fake DOM: elements with text, attributes, style, a size and listeners; a list that is only a list ----
class List {
    constructor(items) { for (let i = 0; i < items.length; i++) this[i] = items[i]; this.length = items.length; }
}
const made = [];
let STAGE = 0;                                                                  // (the width that an attached stage has: 0 until the card is in the page)
class Node {
    constructor(tag, text) {
        this.tag = tag; this.children = []; this.parentNode = null; this.className = ''; this.attrs = {}; this._text = text; this.listeners = {}; this.offsetWidth = 90; this.offsetHeight = 90;
        this.style = { setProperty: (k, v) => { this.style[k] = v; } };
        made.push(this);
    }
    get clientWidth() { return this.className.split(' ').includes('dm-stage') ? STAGE : 0; }
    get clientHeight() { return this.className.split(' ').includes('dm-stage') ? STAGE : 0; }
    set innerHTML(v) { throw new Error('innerHTML is never used'); }
    get firstChild() { return this.children[0] || null; }
    appendChild(c) { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = this; this.children.push(c); return c; }
    removeChild(c) { const i = this.children.indexOf(c); if (i >= 0) this.children.splice(i, 1); c.parentNode = null; return c; }
    setAttribute(k, v) { this.attrs[k] = String(v); if (k === 'class') this.className = String(v); }
    getAttribute(k) { return this.attrs[k]; }
    addEventListener(type, fn) { (this.listeners[type] = this.listeners[type] || []).push(fn); }
    fire(type, ev) { (this.listeners[type] || []).forEach((fn) => fn(Object.assign({ preventDefault() {} }, ev))); }
    getBoundingClientRect() { return { left: 0, top: 0, width: this.clientWidth, height: this.clientHeight }; }
    set textContent(t) { this.children.forEach((c) => { c.parentNode = null; }); this.children = []; this._text = String(t); }
    get textContent() { return (this._text === undefined ? '' : this._text) + this.children.map((c) => c.textContent).join(''); }
    all(test, out = []) { this.children.forEach((c) => { if (test(c)) out.push(c); c.all(test, out); }); return out; }
    byClass(cls) { return this.all((n) => n.className.split(' ').includes(cls)); }
    byTag(tag) { return this.all((n) => n.tag === tag); }
    querySelectorAll(sel) { return new List(sel.startsWith('.') ? this.byClass(sel.slice(1)) : []); }
    querySelector(sel) { return sel.startsWith('.') ? (this.byClass(sel.slice(1))[0] || null) : null; }
    getContext() { return this.context || null; }
    get classList() { const n = this; return { add(c) { if (!n.className.split(' ').includes(c)) n.className = (n.className + ' ' + c).trim(); }, remove(c) { n.className = n.className.split(' ').filter((x) => x !== c).join(' '); }, contains(c) { return n.className.split(' ').includes(c); }, toggle(c) { if (this.contains(c)) { this.remove(c); return false; } this.add(c); return true; } }; }
    focus() {}
}
const document = { createElement: (t) => new Node(t), createElementNS: (ns, t) => new Node(t), createTextNode: (t) => new Node('#text', t), defaultView: { innerWidth: 1280 } };
const STYLE = { '--c-green': '#79bf93', '--c-red': '#d4707f', '--c-blue': '#8f8fd6', '--c-black': '#b3afb9' };
function memory() { const m = {}; return { getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { m[k] = String(v); }, data: m }; }
const text = (n) => (n ? n.textContent : '');

// ---- the field as a server writes it. `pack` is this file's own encoder of the layout (docs/REPLAYS.md "The map of deaths"); the C++ program's text below is the other side of the same layout ----
const L = DM.LETTERS;
function put(v, n) { let s = ''; for (let i = 0; i < n; i++) s += L[Math.floor(v / Math.pow(64, i)) % 64]; return s; }
function packAnt(a) { return put(a.x, 2) + put(a.y, 2) + put(a.t, 3) + put((a.team & 3) | ((a.type || 0) << 2) | (a.drowned ? 32 : 0) | (((a.killer === undefined || a.killer < 0) ? 7 : a.killer) << 6), 2); }
function field(w, h, ants) { return { v: 1, w, h, n: ants.length, k: 'x', d: ants.map(packAnt).join('') }; }
function squares(w, h, side, cells, extra) { return Object.assign({ v: 1, w, h, n: cells.reduce((n, c) => n + c.count, 0), k: 'b', s: side, d: cells.map((c) => put(c.x, 2) + put(c.y, 2) + put((c.team & 3) | (c.drowned ? 4 : 0), 1) + put(c.count, 3)).join('') }, extra || {}); }
const A = (x, y, t, team, killer, type, drowned) => ({ x, y, t, team, killer, type: type || 0, drowned: !!drowned });
// four seats' worth of deaths on a 60 x 60 map: the first blood is the second ant (the first has nobody to blame), the last kill the last but one
const ANTS = [A(10, 12, 2, 0, -1), A(15, 21, 32, 3, 1), A(30, 31, 146, 2, 3, 4), A(30, 31, 150, 1, 0, 1, true), A(29, 37, 200, 3, 1, 0, true), A(28, 37, 210, 3, 1), A(29, 37, 220, 2, 1, 0, true), A(49, 23, 557, 3, 0), A(58, 58, 600, 1, -1, 5, true)];
const REC_SEATS = [0, 1, 2, 3].map((i) => ({ seat: i, color: ['Green', 'Red', 'Blue', 'Black'][i], name: ['Gory', 'Rhea', 'Jaro', 'Anton'][i], bot: i ? 'Hard' : '', score: 0, killed: 0, lost: 0, hatched: 0, bombs_planted: 0, bombs_defused: 0, fires_lit: 0, left: false, winner: false }));
function record(deaths, extra) { return Object.assign({ v: 1, id: 'ants-MEDIUM-20261020-055605Z', ended: 1792000000, map: 'MEDIUM.LVL', game: 'v0.12.106', turns: 12000, seconds: 600, finished: true, quitter: null, mode: 'highest-score', format: 'ffa4', draw: false, result: 'decided', seats: REC_SEATS, rows: [], recording: 'watch', file: 'x.antsrep', keep_days: 30, deaths }, extra || {}); }

// ---- reading the field ----
{
    const cpp = { v: 1, w: 60, h: 40, n: 4, k: 'x', d: 'HAJAjBAyDAAAACAAAH7AnAMRAXBeAfApJAFA' };      // (made by ants_server make_history_deaths from four kills: (7,9) 1999 turns team 2 by 3 type 4 drowned; (0,0) 40 turns team 0 nobody; (59,39) 22000 turns team 3 by 1 type 5; (30,31) 12345 turns team 1 by 0 type 1)
    const m = DM.decode(cpp);
    same('a field that the server wrote: every ant, in order, with all six facts', m && m.ants.map((a) => [a.x, a.y, a.t, a.team, a.type, a.drowned, a.killer]), [[7, 9, 99, 2, 4, true, 3], [0, 0, 2, 0, 0, false, -1], [59, 39, 1100, 3, 5, false, 1], [30, 31, 617, 1, 1, false, 0]]);
    same('... its size, its count and the first and last kill (the ones that a team is to blame for)', [m.w, m.h, m.total, m.squares, m.first.x, m.last.x], [60, 40, 4, false, 7, 30]);
    const sum = DM.decode({ v: 1, w: 60, h: 40, n: 8, k: 'b', map: 'TEST.LVL', matches: 2, s: 1, d: 'AAAAACAAHAJAGCAAeAfABCAA7AnADCAA' });
    same('squares that the server wrote (the sum of two matches of the four kills above): the side, the cells with team, cause and count, the number of matches and the map name', sum && [sum.squares, sum.side, sum.total, sum.matches, sum.map, sum.cells.map((c) => [c.x, c.y, c.team, c.drowned, c.count])],
        [true, 1, 8, 2, 'TEST.LVL', [[0, 0, 0, false, 2], [7, 9, 2, true, 2], [30, 31, 1, false, 2], [59, 39, 3, false, 2]]]);
    const big = DM.decode({ v: 1, w: 512, h: 512, n: 400, k: 'b', s: 32, d: 'AAAAABAAAAAAFBAAAAAAGBAABAAABBAA', f: 'VBPBBAAgB', l: 'TAaFPGAKD' });
    same('squares of a big match, with its first blood and last kill as single ants: the first is (85, 79) at second 1, team 0, hit by team 1', big && [big.squares, big.side, big.cells.length, [big.first.x, big.first.y, big.first.t, big.first.team, big.first.drowned, big.first.killer]], [true, 32, 4, [85, 79, 1, 0, true, 1]]);
    check('... in 512 x 512 the first and the last are on the map', big.first.x < 512 && big.first.y < 512 && big.last.x < 512 && big.last.y < 512 && big.first.killer >= 0 && big.last.killer >= 0);
    same('the pure helpers agree: huge is a match kept in squares or a map over 256, a mode is Heat unless the choice was Dots, and a huge one starts as Dots',
        [DM.isHuge(m), DM.isHuge(big), DM.isHuge(DM.decode(field(300, 20, [A(1, 1, 1, 0, 1)]))), DM.modeFor(m, null), DM.modeFor(m, 'dots'), DM.modeFor(m, 'heat'), DM.modeFor(m, 'x'), DM.modeFor(big, 'heat'), DM.modeFor(big, null)],
        [false, true, true, 'heat', 'dots', 'heat', 'heat', 'dots', 'dots']);
}
{
    const good = field(60, 60, ANTS);
    const refused = (label, d) => check('a damaged field makes no map and throws nothing: ' + label, throws(() => DM.decode(d)) === null && DM.decode(d) === null);
    refused('nothing', undefined); refused('a text', 'x'); refused('an array', []); refused('another version', Object.assign({}, good, { v: 2 })); refused('no version', Object.assign({}, good, { v: undefined }));
    refused('a size of 0', Object.assign({}, good, { w: 0 })); refused('a fraction', Object.assign({}, good, { h: 60.5 })); refused('a size that is a text', Object.assign({}, good, { w: '60' })); refused('a size above 4,096', Object.assign({}, good, { w: 4097 }));
    refused('no count', Object.assign({}, good, { n: 0 })); refused('a count that is not finite', Object.assign({}, good, { n: Infinity })); refused('an unknown kind', Object.assign({}, good, { k: 'q' })); refused('no letters', Object.assign({}, good, { d: 5 }));
    refused('a length that is not nine letters each', Object.assign({}, good, { d: good.d + 'A' })); refused('a letter that is not one of the 64', Object.assign({}, good, { d: good.d.slice(0, 4) + '+' + good.d.slice(5) }));
    refused('a bad letter that a bigger one after it would make up for (+B is -1 + 64)', Object.assign({}, good, { d: '+B' + good.d.slice(2) })); refused('... in squares', Object.assign(squares(60, 60, 4, [{ x: 0, y: 0, team: 0, count: 1 }]), { d: '+BAAAAAB' }));
    refused('a square of count 0 beside one that has ants', squares(60, 60, 4, [{ x: 0, y: 0, team: 0, count: 0 }, { x: 1, y: 1, team: 1, count: 3 }]));
    refused('a type of 6', Object.assign({}, good, { d: put(1, 2) + put(1, 2) + put(1, 3) + put(6 << 2, 2) })); refused('a team to blame of 5', Object.assign({}, good, { d: put(1, 2) + put(1, 2) + put(1, 3) + put(5 << 6, 2) }));
    refused('too many ants', Object.assign({}, good, { d: good.d.repeat(50) })); refused('squares with no side', squares(60, 60, 0, [{ x: 0, y: 0, team: 0, count: 1 }])); refused('a square of count 0', squares(60, 60, 4, [{ x: 0, y: 0, team: 0, count: 0 }]));
    refused('squares of an odd length', Object.assign(squares(60, 60, 4, [{ x: 0, y: 0, team: 0, count: 1 }]), { d: 'AAAAAAAAA' })); refused('a flag of 8 (above 7)', Object.assign(squares(60, 60, 4, [{ x: 0, y: 0, team: 0, count: 1 }]), { d: 'AAAAIBAA' }));
    refused('no ant at all', Object.assign({}, good, { d: '', n: 1 }));
    const far = DM.decode(field(30, 20, [A(500, 400, 5, 1, 2), A(29, 19, 6, 0, 1)]));
    same('a position beyond the map is put on its edge, in x and in y', far.ants.map((a) => [a.x, a.y]), [[29, 19], [29, 19]]);
    const farSq = DM.decode(squares(60, 60, 8, [{ x: 4000, y: 4000, team: 2, count: 3 }]));
    same('a square beyond the map is put on the last square (a map of 60 and squares of 8 has squares 0 to 7)', farSq.cells.map((c) => [c.x, c.y]), [[7, 7]]);
    check('the first blood of a field is the first ant that a team is to blame for; one that nobody killed is no first blood', (() => { const d = DM.decode(good); return d.first.t === 32 && d.last.t === 557; })());
    check('the text may hold more than the header says: what it holds is drawn, the header is the "of"', (() => { const d = DM.decode(Object.assign({}, good, { n: 40 })); return d.total === ANTS.length && d.of === 40; })());
    check('... and a header that says less than the text holds is not believed', DM.decode(Object.assign({}, good, { n: 3 })).of === ANTS.length);
    check('a first blood and a last kill that nobody was to blame for are none (squares)', (() => { const d = DM.decode(squares(512, 512, 32, [{ x: 1, y: 1, team: 0, count: 2 }], { f: packAnt(A(10, 10, 5, 0, -1)), l: packAnt(A(20, 20, 9, 1, -1)) })); return d.first === null && d.last === null; })());
    check('the text of a map name in a sum is cleaned (no control, zero-width or bidi characters) and short', DM.decode(Object.assign({}, squares(60, 60, 1, [{ x: 1, y: 1, team: 0, count: 2 }]), { map: 'A‮B​C\u0007' + 'x'.repeat(100), matches: 3 })).map === ('ABC' + 'x'.repeat(61)).slice(0, 64));
    same('clean() takes out controls, zero-width, bidi and the soft hyphen, and cuts', [DM.clean('A\u0000B\u007fC‮D​E­F﻿G⁦H'), DM.clean('x'.repeat(100), 10), DM.clean(5), DM.clean(null)], ['ABCDEFGH', 'xxxxxxxxxx', '', '']);
}

// ---- what is shown ----
{
    const m = DM.decode(field(60, 60, ANTS));
    const all = DM.select(m, { team: -1, drowned: true });
    same('all teams: every ant, and the count of each team', [all.total, all.by, all.all], [9, [1, 2, 2, 4], 9]);
    const t3 = DM.select(m, { team: 3, drowned: true });
    same('one team: only its ants, the chips still count all teams', [t3.ants.length, t3.total, t3.by], [4, 4, [1, 2, 2, 4]]);
    const dry = DM.select(m, { team: -1, drowned: false });
    same('without the ants that drowned: they are neither drawn nor counted, in any chip', [dry.total, dry.by, dry.ants.some((a) => a.drowned)], [5, [1, 0, 1, 3], false]);
    const sq = DM.decode(squares(60, 60, 4, [{ x: 1, y: 1, team: 0, count: 5 }, { x: 2, y: 2, team: 1, count: 3, drowned: true }, { x: 2, y: 3, team: 1, count: 1 }]));
    same('squares are counted by their counts and filtered the same way', [DM.select(sq, { team: -1, drowned: true }).total, DM.select(sq, { team: -1, drowned: false }).total, DM.select(sq, { team: 1, drowned: false }).total, DM.select(sq, { team: 1, drowned: true }).by], [9, 6, 1, [5, 4, 0, 0]]);
    same('the points of the glow: a tile\'s center for an ant, the middle of a square for squares, weighted by the count', [DM.pointsOf(m, all).length, DM.pointsOf(sq, DM.select(sq, { team: -1, drowned: true })).map((p) => [p[0], p[1], p[2]])], [9, [[5.5, 5.5, 5], [9.5, 9.5, 3], [9.5, 13.5, 1]]]);
    const b = DM.busiest(DM.pointsOf(m, all), 2);
    same('the busiest place: the most ants within 2 tiles, found at an ant; the first of equals', [b.n, b.x, b.y], [3, 29, 37]);
    same('... and no points is no place', DM.busiest([], 2), null);
    same('the radius is 2 tiles on a small map, a thirtieth of a big one, and never less than a square', [DM.radiusOf({ w: 60, squares: false, side: 1 }), DM.radiusOf({ w: 512, squares: false, side: 1 }), DM.radiusOf({ w: 60, squares: true, side: 8 })], [2, 17, 8]);
    const near = [[10, 10, 1]]; for (let i = 0; i < 2000; i++) near.push([200 + (i % 40), 100 + Math.floor(i / 40), 1]);
    check('a busy place of two thousand points is found without looking at every pair', DM.busiest(near, 5).n > 100);
}

// ---- the glow ----
{
    const f = DM.heatField([[10, 10, 1]], 60, 60, 4, 1.5), at = (x, y) => f.data[y * f.cw + x];
    same('the glow: a canvas of 4 pixels a tile, the same on both sides', [f.cw, f.ch], [240, 240]);
    check('... is highest at the middle of the tile and falls away to nothing', at(42, 42) > at(46, 42) && at(46, 42) > at(54, 42) && at(120, 120) === 0 && at(42, 42) > 0.99);
    check('... the blob is a soft round one: at one sigma (6 pixels) it is e^-0.5 of the middle, at two e^-2', Math.abs(at(48, 42) / at(42, 42) - Math.exp(-0.5)) < 1e-3 && Math.abs(at(54, 42) / at(42, 42) - Math.exp(-2)) < 1e-3 && Math.abs(at(42, 48) / at(42, 42) - Math.exp(-0.5)) < 1e-3);
    const two = DM.heatField([[10, 10, 1], [10, 10, 1]], 60, 60, 4, 1.5);
    check('... two ants on one tile are twice as bright', Math.abs(two.data[42 * two.cw + 42] - 2 * at(42, 42)) < 1e-4);
    check('... a count weighs like that many ants', Math.abs(DM.heatField([[10, 10, 3]], 60, 60, 4, 1.5).data[42 * 240 + 42] - 3 * at(42, 42)) < 1e-4);
    const edge = DM.heatField([[0, 0, 1], [59, 59, 1]], 60, 60, 4, 1.5);
    check('... a point at the corner of the map is drawn on the map and does not throw', edge.data[2 * 240 + 2] > 0.9 && edge.data[237 * 240 + 237] > 0.9);
    check('... a canvas is never more than 2,400 pixels a side', DM.heatField([[1, 1, 1]], 4000, 3000, 12, 3).cw === 2400 && DM.heatField([[1, 1, 1]], 4000, 3000, 12, 3).ch === 2400);
    const px = DM.heatPixels(f, -1, DM.ramp(-1, null) && [[121, 191, 147], [212, 112, 127], [143, 143, 214], [179, 175, 185]], 2.2);
    check('the pixels: nothing where nothing died, a color with alpha where it did, the cream of the legend\'s end for a place with many', px[3] === 0 && px[(42 * 240 + 42) * 4 + 3] > 150 && px[(42 * 240 + 42) * 4] > 200);
    same('the colors of the glow for all teams start dark red and end cream; one team is that team\'s color', [DM.rampAt(DM.ramp(-1), 0), DM.rampAt(DM.ramp(-1), 1)], [[157, 13, 23], [255, 244, 207]]);
    check('... and the legend\'s bar is a gradient of the same colors', /^linear-gradient\(90deg, #0d171f 0%, rgb\(157,13,23\) 0%, .*rgb\(255,244,207\) 100%\)$/.test(DM.rampCss(-1, null)), DM.rampCss(-1, null));
    const rgbTeam = [[100, 200, 100], [200, 100, 100], [100, 100, 200], [180, 180, 180]];
    check('... one team\'s bar ends lighter than its color and starts darker', DM.rampAt(DM.ramp(2, rgbTeam), 1)[0] > 200 && DM.rampAt(DM.ramp(2, rgbTeam), 0)[2] < 100);
    same('... and is made of that team\'s own color (the page\'s, not a built-in one): its middle is the color itself', [DM.rampAt(DM.ramp(2, rgbTeam), 0.7), DM.rampAt(DM.ramp(0, rgbTeam), 0.7)], [[100, 100, 200], [100, 200, 100]]);
    same('the rulers step by 2, 5, 10, 25, 50 and 250 tiles as the view grows', [DM.tickStep(10), DM.tickStep(30), DM.tickStep(60), DM.tickStep(200), DM.tickStep(512), DM.tickStep(2000)], [2, 5, 10, 25, 50, 250]);
    same('the picture of the map: the original\'s own, else the picker\'s, else none', [DM.pictureUrl('TREASURE.LVL', { replay: { mapKey: () => 'treasure' } }), DM.pictureUrl('Pond.lvl', { replay: { mapKey: () => '' }, picker: { thumbFor: (m, l) => '/maps/previews/Pond.webp' + (l ? '?' + l : '') }, maps: () => 'list' }), DM.pictureUrl('Pond.lvl', {}), DM.pictureUrl('x', { replay: { mapKey: () => '' }, picker: { thumbFor: () => '' } })], ['front/preview_treasure.png', '/maps/previews/Pond.webp?list', '', '']);
}

// ---- a card ----
function make(env) { return DM.create(document, (n) => STYLE[n], Object.assign({ replay: { mapKey: (m) => (/^medium/i.test(m) ? 'medium' : '') }, storage: memory() }, env || {})); }
function sized(card, w) { STAGE = w || 560; DM.redraw(card.parentNode || card); return card.byClass('dm-stage')[0]; }
function host(card) { const root = new Node('main'); root.appendChild(card); return root; }
function button(card, label) { return card.byTag('button').filter((b) => text(b).startsWith(label))[0]; }
const press = (b) => b.fire('click', {});
const svgOf = (card) => card.byClass('dm-stage')[0].byTag('svg')[0];
function marks(card) { return svgOf(card).byClass('mkp'); }
{
    const D = make(), rec = record(field(60, 60, ANTS)), card = D.card(rec, {}), root = host(card);
    same('the card: a section with its id, labelled by its title, Heat first', [card.tag, card.id, card.attrs['aria-labelledby'], text(card.byTag('h2')[0]), card.className.split(' ').includes('heat')], ['section', 'deaths', 'deaths-h', 'Where ants died', true]);
    same('its title line says how many ants died', text(card.byClass('count')[0]), '9 ants died');
    same('the chips: all teams and every seat with its number (the names without "(bot, Hard)")', card.byClass('dm-chip').map(text), ['All teams 9', 'Gory 1', 'Rhea 2', 'Jaro 2', 'Anton 4']);
    same('... All teams is pressed; the chips of the seats carry their own shape and color', [card.byClass('dm-chip').map((c) => c.attrs['aria-pressed']).join(), card.byClass('dm-chip')[3].byClass('mk')[0].className, card.byClass('dm-chip')[3].byClass('mk')[0].attrs['data-c']], ['true,false,false,false,false', 'mk triangle', 'Blue']);
    same('Show as: Heat is pressed, Dots is not', card.byClass('seg')[0].byTag('button').map((b) => [text(b), b.attrs['aria-pressed']]), [['Heat', 'true'], ['Dots', 'false']]);
    same('the marks: first blood and last kill (on), and the one switch of the ants that drowned (on)', card.byClass('dm-tog').map((b) => [text(b), b.attrs['aria-pressed']]), [['First blood', 'true'], ['Last kill', 'true'], ['Ants that drowned', 'true']]);
    check('the legend of the glow: a bar with its two ends', card.byClass('dm-legend')[0].byClass('bar').length === 1 && card.byClass('ends')[0].byTag('span').map(text).join() === 'Few,Many');
    same('the facts: the busiest place, first blood and last kill with their tiles and times', [card.byClass('dm-facts')[0].byTag('dt').map(text), card.byClass('dm-facts')[0].byTag('dd').map(text)],
        [['Busiest place', 'First blood', 'Last kill'], ['3 ants died within 2 tiles of tile 29, 37.', ' Rhea (bot, Hard) on  Anton (bot, Hard) at 0:32, tile 15, 21', ' Gory on  Anton (bot, Hard) at 9:17, tile 49, 23']]);
    check('nothing is drawn before the stage has a size', svgOf(card).children.length === 0);
    const stage = sized(card, 560);
    check('with a size the stage has its picture, a canvas, the svg and the rulers', stage.byTag('img').length === 1 && stage.byTag('img')[0].src === 'front/preview_medium.png' && stage.byTag('canvas').length === 1 && card.byClass('dm-ruler-top')[0].children.length > 3, [stage.byTag('img').length, stage.byTag('img')[0] && stage.byTag('img')[0].src, stage.byTag('canvas').length, card.byClass('dm-ruler-top')[0].children.length].join());
    check('the stage is a group with a name that says how to zoom, and can be focused', stage.attrs.role === 'group' && /Plus and minus zoom/.test(stage.attrs['aria-label']) && stage.attrs.tabindex === '0');
    check('Heat draws the glow and no marks', marks(card).length === 0 && stage.byTag('canvas')[0].style.display === '');
    // the glow is painted on a real canvas context when there is one
    const painted = [];
    const card2 = make().card(rec, {}); const cv = card2.byTag('canvas')[0];
    check('(the canvas is painted when the page has one: not with this fake, which has none, and not before the card exists)', painted.length === 0 && cv.getContext() === null);
    const ctxCard = (() => { const c = make(); const orig = document.createElement; document.createElement = (t) => { const n = orig(t); if (t === 'canvas') n.context = { createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4), w, h }), putImageData: (d) => painted.push(d) }; return n; }; const k = c.card(rec, {}); document.createElement = orig; return k; })();
    check('a canvas with a context gets the glow: an image of the field\'s size with some alpha', painted.length === 1 && painted[0].data.length === painted[0].w * painted[0].h * 4 && painted[0].data.some((v, i) => i % 4 === 3 && v > 0) && ctxCard.byTag('canvas')[0].width === painted[0].w);

    // Dots
    press(button(card, 'Dots'));
    check('Dots: the mode is remembered, Dots is pressed and the glow is hidden', card.byClass('seg')[0].byTag('button')[1].attrs['aria-pressed'] === 'true' && card.className.split(' ').includes('dots') && card.byTag('canvas')[0].style.display === 'none');
    sized(card, 560);
    same('every ant has a mark (the 9 ants are on 7 tiles: the top one of a tile is the last that died there), each with its seat\'s shape and color', [marks(card).length, marks(card).map((m) => m.attrs['data-c']).sort().join()], [9, 'Black,Black,Black,Black,Blue,Blue,Green,Red,Red']);
    check('... the circle, square, triangle and diamond are the seats\'', (() => { const t = {}; marks(card).forEach((m) => { t[m.attrs['data-c']] = m.tag; }); return t.Green === 'circle' && t.Red === 'rect' && t.Blue === 'polygon' && t.Black === 'polygon'; })());
    same('... two ants on one tile show a count (two tiles hold two)', svgOf(card).byTag('text').map(text), ['×2', '×2']);
    same('the legend of the dots: one dot is one ant, a key for each seat', [text(card.byClass('dm-legend')[0].byClass('lab')[0]), card.byClass('key')[0].byTag('li').map(text)], ['One dot is one ant', ['Gory’s ants', 'Rhea’s ants', 'Jaro’s ants', 'Anton’s ants']]);
    same('the rings: the first blood and the last kill, with their tags', [svgOf(card).byClass('ring-first').length, svgOf(card).byClass('ring-last').length, card.byClass('dm-tag').map(text)], [1, 1, ['First blood0:32', 'Last kill9:17']]);
    check('... the last kill\'s ring is dashed, the first blood\'s is whole', svgOf(card).byClass('ring-last')[0].attrs['stroke-dasharray'] !== '' && svgOf(card).byClass('ring-first')[0].attrs['stroke-dasharray'] === '');
    press(button(card, 'First blood'));
    check('the switch of First blood takes its ring and its tag away and leaves the other', svgOf(card).byClass('ring-first').length === 0 && card.byClass('dm-tag').length === 1 && button(card, 'First blood').attrs['aria-pressed'] === 'false' && svgOf(card).byClass('ring-last').length === 1);
    press(button(card, 'First blood'));
    // a team
    press(card.byClass('dm-chip')[3]);
    sized(card, 560);
    same('a team\'s chip: only its ants, its count in the title, its chip pressed', [text(card.byClass('count')[0]), marks(card).map((m) => m.attrs['data-c']).join(), card.byClass('dm-chip').map((c) => c.attrs['aria-pressed']).join(), card.byClass('key')[0].byTag('li').map(text)],
        ['2 of Jaro’s ants died (of 9)', 'Blue,Blue', 'false,false,false,true,false', ['Jaro’s ants']]);
    check('... and only the marks of that team\'s kills stay (the ring of the first blood is of Rhea\'s victim, Anton: it goes with Anton)', svgOf(card).byClass('ring-first').length === 0);
    press(card.byClass('dm-chip')[0]);
    // drowned
    press(button(card, 'Ants that drowned'));
    sized(card, 560);
    same('the one switch that hides the ants that drowned: they are neither drawn nor counted, the title says so, the chips follow', [marks(card).length, text(card.byClass('count')[0]), card.byClass('dm-chip').map(text)],
        [5, '5 ants died, not counting the ants that drowned', ['All teams 5', 'Gory 1', 'Rhea 0', 'Jaro 1', 'Anton 3']]);
    check('... and the switch is off', button(card, 'Ants that drowned').attrs['aria-pressed'] === 'false');
    press(button(card, 'Ants that drowned'));
    sized(card, 560);
    check('... on again: they all come back', marks(card).length === 9 && text(card.byClass('count')[0]) === '9 ants died');
    // the tooltip
    const st = card.byClass('dm-stage')[0];
    const tapAt = (tx, ty) => { const g = st._geo, cx = (tx + 0.5 - (g.vx)) * g.ppt * g.s, cy = (ty + 0.5 - g.vy) * g.ppt * g.s; st.fire('pointerdown', { pointerId: 1, clientX: cx, clientY: cy }); st.fire('pointerup', { pointerId: 1, clientX: cx, clientY: cy, type: 'pointerup' }); };
    tapAt(30, 31);
    const tt = card.byClass('dm-tt')[0];
    same('a tap on a mark names the tile, the time, the kind of ant and who killed it', [text(tt.byClass('t')[0]), tt.byClass('r')[0].byTag('span').map(text)], ['Jaro’s ant died', ['Tile', '30, 31', 'Time', '2:26', 'Ant', 'Combat ant', 'Killed by', 'Anton']].map((x, i) => (i ? x : x)));
    same('... an ant that was on the same tile is told of', text(tt.byClass('more')[0]), '1 more ant died on this tile');
    tapAt(29, 37);
    same('a drowned ant says how, and who hit it last', [card.byClass('dm-tt')[0].byClass('r')[0].byTag('span').map(text).slice(6)], [['How', 'Drowned; last hit by Rhea']]);
    check('a tap on nothing takes the tooltip away', (() => { st.fire('pointerdown', { pointerId: 1, clientX: 2, clientY: 2 }); st.fire('pointerup', { pointerId: 1, clientX: 2, clientY: 2, type: 'pointerup' }); return card.byClass('dm-tt').length === 0; })());
    tapAt(10, 12);
    same('an ant with nobody to blame says "not known"', card.byClass('dm-tt')[0].byClass('r')[0].byTag('span').map(text).slice(6), ['Killed by', 'not known']);
    // zoom
    press(button(card, '+'));
    check('+ zooms in: the stage is marked zoomed, the scale shows, the little map appears, the rulers follow', card.byClass('dm-stage')[0].className.includes('zoomed') && text(card.byClass('zl')[0]) === '160%' && card.byClass('dm-mini').length === 1);
    for (let i = 0; i < 12; i++) press(button(card, '+'));
    check('the zoom stops (at 8 times on a map of 60: a tile is 75 pixels), the grid of tiles shows', text(card.byClass('zl')[0]) === '800%' && svgOf(card).byTag('g').length === 1 && svgOf(card).byTag('line').length > 10);
    press(button(card, 'Fit'));
    check('Fit shows the whole map again', !card.byClass('dm-stage')[0].className.includes('zoomed') && text(card.byClass('zl')[0]) === '' && card.byClass('dm-mini').length === 0);
    const wheel = (dy, cx, cy) => st.fire('wheel', { deltaY: dy, clientX: cx, clientY: cy });
    wheel(-100, 280, 280); wheel(-100, 280, 280);
    check('the wheel zooms, around the pointer, and a wheel outwards goes back', text(card.byClass('zl')[0]) === '156%' && (wheel(100, 280, 280), wheel(100, 280, 280), text(card.byClass('zl')[0]) === ''));
    wheel(-100, 280, 280); wheel(-100, 280, 280); wheel(-100, 280, 280);
    const g0 = st._geo, vx0 = g0.vx;
    st.fire('pointerdown', { pointerId: 7, clientX: 300, clientY: 300 }); st.fire('pointermove', { pointerId: 7, clientX: 340, clientY: 300 }); st.fire('pointerup', { pointerId: 7, clientX: 340, clientY: 300, type: 'pointerup' });
    check('a drag moves the map (the view goes the other way) and is no tap', st._geo.vx < vx0 && card.byClass('dm-tt').length === 0);
    const vx1 = st._geo.vx;
    st.fire('keydown', { key: 'ArrowRight' }); st.fire('keydown', { key: '-' }); st.fire('keydown', { key: '0' });
    check('the keys move, zoom out and fit', st._geo.s === 1 && vx1 !== undefined);
    // two fingers
    st.fire('pointerdown', { pointerId: 1, clientX: 200, clientY: 280 }); st.fire('pointerdown', { pointerId: 2, clientX: 360, clientY: 280 });
    st.fire('pointermove', { pointerId: 2, clientX: 440, clientY: 280 });
    check('two fingers moving apart zoom in', st._geo.s > 1.2);
    st.fire('pointerup', { pointerId: 1, clientX: 200, clientY: 280, type: 'pointerup' }); st.fire('pointerup', { pointerId: 2, clientX: 440, clientY: 280, type: 'pointerup' });
    check('... and lifting them is no tap', card.byClass('dm-tt').length === 0);
    // the choice is remembered, and a store that throws changes nothing
    const store = memory(), c3 = make({ storage: store }).card(rec, {}); press(button(c3, 'Dots'));
    same('the choice of Dots is kept on the device, under one key', store.data, { [DM.MODE_KEY]: 'dots' });
    check('a card made afterwards starts with it', make({ storage: store }).card(rec, {}).className.split(' ').includes('dots'));
    const boom = { getItem() { throw new Error('no storage'); }, setItem() { throw new Error('no storage'); } };
    check('a storage that throws changes nothing: Heat first, and a press still works', (() => { const c4 = make({ storage: boom }).card(rec, {}); press(button(c4, 'Dots')); return c4.className.split(' ').includes('dots'); })());
    check('no storage at all is no storage', make({ storage: null }).card(rec, {}).className.split(' ').includes('heat'));
    check('the nodes of the cards are made by createElement, never by innerHTML (the fake throws on it), and no element was made from a name', made.every((n) => ['img', 'canvas', 'svg', 'circle', 'rect', 'polygon', 'path', 'g', 'line', 'text', 'title', 'section', 'div', 'span', 'h2', 'h3', 'button', 'dl', 'dt', 'dd', 'p', 'ul', 'li', 'b', 'i', 'main', '#text'].includes(n.tag)), made.map((n) => n.tag).filter((t, i, a) => a.indexOf(t) === i).join());
}

// ---- the marks follow what is shown: a first blood that drowned is not shown when the drowned are hidden; the small card and the sum have no marks even when the field names a first blood ----
{
    const wet = field(60, 60, [A(5, 5, 10, 0, 1, 0, true), A(20, 20, 50, 2, 0)]);
    const card = make().card(record(wet), {}); host(card); press(button(card, 'Dots')); sized(card, 560);
    same('a first blood that drowned is shown while the drowned are shown, and so is the last kill', [svgOf(card).byClass('ring-first').length, svgOf(card).byClass('ring-last').length], [1, 1]);
    press(button(card, 'Ants that drowned')); sized(card, 560);
    same('hiding the drowned takes the ring of that ant away (and the facts of it: the first blood is then the other ant)', [svgOf(card).byClass('ring-first').length, svgOf(card).byClass('ring-last').length, marks(card).length], [0, 1, 1]);
    const small = make().card(record(wet), { compact: true }); host(small); press(button(small, 'Dots')); sized(small, 236);
    same('the small card has no rings and no tags, though the field names a first blood', [svgOf(small).byClass('ring-first').length, svgOf(small).byClass('ring-last').length, small.byClass('dm-tag').length, marks(small).length], [0, 0, 0, 2]);
    const sumWithMarks = squares(60, 60, 1, [{ x: 5, y: 5, team: 0, count: 2 }], { map: 'MEDIUM.LVL', matches: 4, f: packAnt(A(5, 5, 10, 0, 1)), l: packAnt(A(5, 5, 20, 0, 1)) });
    const withSum = make({ sum: () => Promise.resolve(sumWithMarks) }).card(record(wet), {}); host(withSum); press(button(withSum, 'All matches'));
    return new Promise((r) => setTimeout(r, 20)).then(() => {
        sized(withSum, 560); press(button(withSum, 'Dots')); sized(withSum, 560);
        same('the sum of a map has no rings, no tags and no first blood, though a field of it names one', [svgOf(withSum).byClass('ring-first').length, svgOf(withSum).byClass('ring-last').length, withSum.byClass('dm-tag').length, text(withSum.byClass('count')[0]), button(withSum, 'First blood')], [0, 0, 0, '2 ants died in 4 matches', undefined]);
    }).then(() => phoneChecks());
}
function phoneChecks() {
// ---- a phone: the tooltip goes under the map; a compact card ----
{
    const D = make(), rec = record(field(60, 60, ANTS)), card = D.card(rec, {});
    document.defaultView.innerWidth = 390;
    host(card); press(button(card, 'Dots')); const st = sized(card, 346);
    const g = st._geo; st.fire('pointerdown', { pointerId: 1, clientX: (30.5 - g.vx) * g.ppt, clientY: (31.5 - g.vy) * g.ppt }); st.fire('pointerup', { pointerId: 1, clientX: (30.5 - g.vx) * g.ppt, clientY: (31.5 - g.vy) * g.ppt, type: 'pointerup' });
    check('on a phone the tooltip opens under the map, not over it', card.byClass('dm-tt')[0].className.includes('below') && card.byClass('dm-tt')[0].parentNode === card.byClass('dm-mapcol')[0]);
    document.defaultView.innerWidth = 1280;
    const cmp = D.card(rec, { compact: true });
    same('the compact card: a small title with the count, the chips, Heat and Dots, a legend, the zoom buttons; no marks, no facts, no hint, no switch of the drowned', [cmp.tag, cmp.className.includes('compact'), text(cmp.byTag('h3')[0]), cmp.byClass('dm-chip').length, cmp.byClass('dm-tog').length, cmp.byClass('dm-facts').length, cmp.byClass('dm-hint').length, cmp.byClass('dm-zoom').length], ['div', true, 'Where ants died9 ants died', 5, 0, 0, 0, 1]);
    check('... it has no id (the page may hold two of them)', cmp.id === undefined || cmp.id === '');
    check('... a card with no data is no card there (nothing is added to the panel)', D.card(record(undefined), { compact: true }) === null && D.card(record(5), { compact: true }) === null && D.card(null, { compact: true }) === null);
}

// ---- a match of very many deaths: squares ----
{
    const cells = []; for (let i = 0; i < 40; i++) cells.push({ x: 3 * (i % 8) + 28, y: 3 * Math.floor(i / 8) + 28, team: i % 4, count: 1 + (i % 7), drowned: i % 5 === 0 });
    const huge = squares(512, 512, 8, cells, { f: packAnt(A(100, 100, 40, 1, 2)), l: packAnt(A(300, 200, 9000, 0, 3)) });
    const D = make(), rec = record(huge, { map: 'Pond.lvl' }), card = D.card(rec, {});
    host(card);
    same('a match kept in squares starts as Dots (colored squares, not the glow), says why in one box, and counts all of its deaths', [card.byClass('seg')[0].byTag('button').map((b) => b.attrs['aria-pressed']), text(card.byClass('count')[0]), text(card.byClass('dm-note')[0])],
        [['false', 'true'], huge.n + ' ants died', 'Big match. This match had ' + huge.n + ' deaths, more than the page keeps one by one, so it keeps how many ants died in each square, and which team.']);
    const st = sized(card, 560);
    const sq = svgOf(card).byClass('sq');
    same('one square for each cell, a team\'s color on it, and no number while the squares are too small to hold one', [sq.length, svgOf(card).byTag('text').length], [40, 0]);
    for (let i = 0; i < 3; i++) press(button(card, '+'));
    check('zoomed in the squares are big enough: the count is in each that holds more than one ant', svgOf(card).byTag('text').length > 5 && svgOf(card).byTag('text').every((t) => +text(t) > 1), svgOf(card).byTag('text').length);
    press(button(card, 'Fit'));
    check('... every square is on the map', sq.every((r) => +r.attrs.x >= 0 && +r.attrs.y >= 0 && +r.attrs.x + +r.attrs.width <= 512.01 && +r.attrs.y + +r.attrs.height <= 512.01));
    same('the hint names the size of a square; the legend of the squares says a square is a place', [text(card.byClass('dm-hint')[0]), text(card.byClass('dm-legend')[0].byClass('lab')[0])], ['The squares are 8 × 8 tiles. Zoom in to see where in a square.', 'One square is a place']);
    check('the first blood and the last kill are still marked, as single ants', svgOf(card).byClass('ring-first').length === 1 && svgOf(card).byClass('ring-last').length === 1);
    const g = st._geo;
    st.fire('pointerdown', { pointerId: 1, clientX: (28 * 8 + 4 - g.vx) * g.ppt, clientY: (28 * 8 + 4 - g.vy) * g.ppt }); st.fire('pointerup', { pointerId: 1, clientX: (28 * 8 + 4 - g.vx) * g.ppt, clientY: (28 * 8 + 4 - g.vy) * g.ppt, type: 'pointerup' });
    same('a tap on a square tells how many died there, on which tiles, whose', card.byClass('dm-tt')[0].byClass('r')[0].byTag('span').map(text).slice(0, 2), ['Tiles', '224 to 232, 224 to 232']);
    press(button(card, 'Heat'));
    check('Heat is still there for a big match, with a legend that says "Many in one square"', card.byClass('ends')[0].byTag('span').map(text).join() === 'Few,Many in one square' && card.byTag('canvas')[0].style.display === '');
    const exactHuge = DM.decode(field(512, 512, [A(500, 500, 3, 0, 1), A(1, 1, 4, 1, 2)]));
    check('a map over 256 with few deaths keeps every ant and also starts with squares of the team\'s color: a mark is never smaller than 4 pixels', (() => { const c = make().card(record(field(512, 512, [A(500, 500, 3, 0, 1), A(1, 1, 4, 1, 2)]), { map: 'Pond.lvl' }), {}); host(c); sized(c, 560); return c.className.includes('dots') && svgOf(c).byClass('mkq').length === 2 && exactHuge.ants.length === 2; })());
}

// ---- all the matches of a map ----
{
    const sum = squares(60, 60, 1, [{ x: 29, y: 37, team: 0, count: 12 }, { x: 30, y: 37, team: 2, count: 4 }, { x: 10, y: 10, team: 3, count: 1, drowned: true }], { map: 'MEDIUM.LVL', matches: 43 });
    const asked = [];
    const D = make({ sum: (m) => { asked.push(m); return Promise.resolve(sum); } }), rec = record(field(60, 60, ANTS)), card = D.card(rec, {});
    host(card);
    same('the card offers the switch: this match, or all matches on this map (when the page can ask)', card.byClass('seg')[0].byTag('button').map((b) => [text(b), b.attrs['aria-pressed']]), [['This match', 'true'], ['All matches on this map', 'false']]);
    check('a page that cannot ask has no switch', make().card(rec, {}).byClass('seg').length === 1);
    press(button(card, 'All matches'));
    check('asking says so while it waits, and asks for the map of this match, once', text(card.byClass('dm-hint')[0]) === 'Adding up the matches on this map…' && asked.join() === 'MEDIUM.LVL');
    return Promise.resolve().then(() => Promise.resolve()).then(() => {
        same('the sum: the title counts the matches, the chips are the seat colors, no marks and no first blood', [text(card.byClass('count')[0]), card.byClass('dm-chip').map(text), card.byClass('dm-tog').map(text), card.byClass('dm-facts')[0].byTag('dt').map(text)],
            ['17 ants died in 43 matches', ['All teams 17', 'Green 12', 'Red 0', 'Blue 4', 'Black 1'], ['Ants that drowned'], ['Busiest place']]);
        same('... the busiest place says "in all those matches"', text(card.byClass('dm-facts')[0].byTag('dd')[0]), '16 ants in all those matches died within 2 tiles of tile 29, 37.');
        same('... and the hint says that every match is on top of the other', text(card.byClass('dm-hint')[0]), 'Every match on this map, one on top of the other. Switch to Dots to see single places.');
        press(card.byClass('dm-chip')[1]);
        same('one team in the sum: its ants of all the matches, in the title', [text(card.byClass('count')[0]), card.byClass('dm-chip').map((c) => c.attrs['aria-pressed']).join()], ['12 of Green’s ants died in 43 matches', 'false,true,false,false,false']);
        press(card.byClass('dm-chip')[0]);
        sized(card, 560); press(button(card, 'Dots')); sized(card, 560);
        check('Dots in the sum are squares of one tile, a count in each that holds more than one ant once a tile is big enough', svgOf(card).byClass('sq').length === 3 && svgOf(card).byTag('text').length === 0 && (press(button(card, '+')), press(button(card, '+')), svgOf(card).byTag('text').map(text).join() === '12,4'), svgOf(card).byTag('text').map(text).join());
        press(button(card, 'Fit'));
        press(button(card, 'This match'));
        check('back to this match: its own numbers and its marks', text(card.byClass('count')[0]) === '9 ants died' && card.byClass('dm-tog').length === 3);
        press(button(card, 'All matches'));
        check('the sum is kept: no second question', asked.length === 1 && text(card.byClass('count')[0]) === '17 ants died in 43 matches');
        // a sum that fails, is not there, or is of another map size
        const bad = (answer) => { const c = make({ sum: () => answer }).card(rec, {}); host(c); press(button(c, 'All matches')); return new Promise((r) => setTimeout(r, 20)).then(() => c); };
        return Promise.all([bad(null), bad(Promise.reject(new Error('no'))), bad(squares(30, 30, 1, [{ x: 1, y: 1, team: 0, count: 1 }]))]).then((cs) => {
            cs.forEach((c, i) => check('a sum that is not there (' + ['none', 'a failure', 'another size'][i] + ') leaves this match as it was and says so in one line', text(c.byClass('count')[0]) === '9 ants died' && /No other matches on this map have a map of deaths yet\./.test(text(c)) && c.byClass('seg')[0].byTag('button')[0].attrs['aria-pressed'] === 'true'));
        });
    }).then(() => nameChecks());
}
}

// ---- names are text; a card without data says so ----
function nameChecks() {
    const evil = '<img src=x onerror=window.__pwn=1>‮​' + 'N'.repeat(80), rec = record(field(60, 60, ANTS), { seats: REC_SEATS.map((s, i) => Object.assign({}, s, { name: i === 2 ? evil : s.name, bot: i === 2 ? '' : s.bot })) });
    const card = make().card(rec, {}); host(card); press(button(card, 'Dots')); const st = sized(card, 560);
    const everything = card.textContent;
    check('a name is put in as text: it is in the page as characters, with no bidi or zero-width character and cut to 40, and no element was made from it', everything.includes('<img src=x onerror=window.__pwn=1>') && !/[‮​]/.test(everything) && !everything.includes('N'.repeat(41)) && card.byTag('img').length === 1);
    const g = st._geo; st.fire('pointerdown', { pointerId: 1, clientX: (30.5 - g.vx) * g.ppt, clientY: (31.5 - g.vy) * g.ppt }); st.fire('pointerup', { pointerId: 1, clientX: (30.5 - g.vx) * g.ppt, clientY: (31.5 - g.vy) * g.ppt, type: 'pointerup' });
    check('... also in the tooltip and in the titles of the marks', !/[‮​]/.test(card.textContent) && text(card.byClass('dm-tt')[0].byClass('t')[0]).startsWith('<img src=x onerror=window.__pwn=1>'));
    const none = make().card(record(undefined), {});
    same('a match with no map of deaths: one quiet line, in the card of the same id, and nothing else', [none.id, text(none.byTag('h2')[0]), text(none.byClass('note')[0]), none.byTag('button').length, none.byClass('dm-stage').length], ['deaths', 'Where ants died', 'No map of deaths for this match.', 0, 0]);
    check('a record that is damaged, a field that is damaged and no record at all give the same line and throw nothing', [record({ v: 1 }), record('x'), record(field(60, 60, ANTS).d), null, undefined, 5, { deaths: 3 }].every((r) => { let c; return throws(() => { c = make().card(r, {}); }) === null && text(c.byClass('note')[0]) === 'No map of deaths for this match.'; }));
    check('the map name is cleaned before it is asked for', (() => { let asked = ''; const c = make({ sum: (m) => { asked = m; return Promise.resolve(null); } }).card(record(field(60, 60, ANTS), { map: 'Po‮nd\u0001.lvl' }), {}); host(c); press(button(c, 'All matches')); return asked === 'Pond.lvl'; })());
    // redraw on a node that holds no card, and on a card whose stage has no size, throws nothing
    check('drawing again a page that has no card, or a card that is hidden (no size), does nothing', throwsNothing());
    return replayPanelChecks().then(() => {
        console.log(failures ? failures + ' of ' + checks + ' checks FAILED' : 'ok: ' + checks + ' checks');
        process.exit(failures ? 1 : 0);
    });
}
// ---- the Stats panel of the replay page (AntsHistory.replayStats) gets the small card, and draws it when the closed panel is opened ----
function replayPanelChecks() {
    const file = 'ants-MEDIUM-20261020-055605Z.antsrep';
    function page(rec, env) {
        const els = {};
        ['stats-btn', 'mstats', 'mstats-flow'].forEach((id) => { els[id] = new Node(id === 'stats-btn' ? 'button' : 'aside'); els[id].hidden = true; });
        els['mstats-flow'].className = '';
        const header = new Node('header'); header.getBoundingClientRect = () => ({ bottom: 60 });
        const listeners = {}, win = { listeners: {}, addEventListener(t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); } };
        const doc = Object.assign(Object.create(document), { getElementById: (id) => els[id] || null, querySelector: (q) => (q === 'header' ? header : null), addEventListener: (t, f) => { (listeners[t] = listeners[t] || []).push(f); }, defaultView: win });
        replayEnv = Object.assign({ document: doc, fetch: () => Promise.resolve({ ok: true, json: () => Promise.resolve(rec) }), file }, env);
        H.replayStats(replayEnv);
        return { els, win, listeners, ready: new Promise((r) => setTimeout(r, 20)) };
    }
    let replayEnv;
    const rec = record(field(60, 60, ANTS));
    const drawn = [];
    const D = make();
    const withCard = page(rec, { deathMap: D, redraw: (node) => drawn.push(node) });
    return withCard.ready.then(() => {
        const pop = withCard.els.mstats, flow = withCard.els['mstats-flow'];
        same('the Stats panel holds the small card under its table, above the link to the match page, in the panel and in the phone\'s row', [pop.byClass('dm').length, flow.byClass('dm').length, pop.byClass('dm')[0].className.includes('compact'), pop.byClass('mbody')[0].children.map((c) => c.className).slice(-2)], [1, 1, true, ['dm compact heat', 'mf']]);
        check('the two cards are two (each has its own state)', pop.byClass('dm')[0] !== flow.byClass('dm')[0]);
        check('nothing is drawn until the panel is opened: the redraw is not asked for', drawn.length === 0);
        withCard.els['stats-btn'].fire('click', {});
        check('opening the panel asks for the card to be drawn, for the panel', drawn.length === 1 && drawn[0] === pop && !pop.hidden);
        withCard.els['stats-btn'].fire('click', {});
        check('closing it does not', drawn.length === 1 && pop.hidden);
        flow.byClass('mt')[0].fire('click', {});
        check('opening the phone\'s row asks for the card of the row to be drawn', drawn.length === 2 && drawn[1] === flow && !flow.classList.contains('closed'));
        flow.byClass('mt')[0].fire('click', {});
        check('closing the row does not', drawn.length === 2);
        const resize = withCard.win.listeners.resize || [];
        check('a window that changes size draws what is open again, and only that', resize.length === 1 && (resize[0](), drawn.length === 2) && (withCard.els['stats-btn'].fire('click', {}), resize[0](), drawn.length === 4 && drawn[3] === pop));
        const plain = page(rec, {});
        return plain.ready.then(() => {
            check('a page with no card maker has the panel as it was: no card, the numbers, the link', plain.els.mstats.byClass('dm').length === 0 && plain.els.mstats.byClass('mf').length === 1 && plain.els.mstats.byTag('table').length === 1);
            const none = page(record(undefined), { deathMap: D, redraw: () => {} });
            return none.ready.then(() => {
                check('a match with no map of deaths gets nothing added', none.els.mstats.byClass('dm').length === 0 && none.els.mstats.byClass('mf').length === 1);
                const broken = page(rec, { deathMap: { card() { throw new Error('the card broke'); } }, redraw: () => { throw new Error('the redraw broke'); } });
                return broken.ready.then(() => {
                    check('a card that throws costs the panel nothing: the numbers are there, the button works, a redraw that throws is not heard', broken.els.mstats.byClass('dm').length === 0 && broken.els.mstats.byTag('table').length === 1 && throws(() => broken.els['stats-btn'].fire('click', {})) === null && !broken.els.mstats.hidden);
                });
            });
        });
    });
}
function throwsNothing() {
    STAGE = 0;
    const c = make().card(record(field(60, 60, ANTS)), {}), root = host(c);
    return throws(() => { DM.redraw(root); DM.redraw(new Node('div')); DM.redraw(null); DM.redraw({}); DM.redraw(root); }) === null && svgOf(c).children.length === 0;
}
