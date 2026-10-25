#!/usr/bin/env node
// The map maker's Pick up tool (web/front/mapmaker_pickup.js and its hooks in mapmaker_tools.js, mapmaker_canvas.js, mapmaker_palette.js; docs/MAP_EDITOR.md, "Pick up") held to made-up maps and to the
// 540 maps of the library, with the real reader, blocks and history, on a STRICT fake of the DOM (a node list has no filter, map or forEach; an element throws when its innerHTML is set), and, when a
// built map core is there, to the core's own check (the page's code is never trusted with the verdict of a map):
//
//   node tests/scripts/web_mapmaker_pickup_check.js [--front DIR] [--module DIR] [--require] [--quick]
//
//   --front DIR   the folder of the mapmaker_*.js files to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//   --module DIR  the folder of lvl_core.js and lvl_core.wasm (default: $ANTS_LVL_CORE_DIR, else build_web/lvl_core); without one case L says SKIP
//   --require     a missing module is a failure
//   --quick       the library sweep (case K) looks at 40 maps, not all
//
//   A  what stands on a cell, and what a pick is called: a hill (its 16 cells and its door), a food pile with its stage object, a plant with its dropper, ants and a stack of them, what is under what
//   B  an ant's group (the ants of its team that touch), and an area (whole objects only), with the words of the bar
//   C  where a thing cannot go and why, in plain words: the edge, a hill, an object, ants, a pile, a plant, solid ground, a Classic map's one byte; and what a copy cannot be
//   W  water is no refusal: a hill, a starting ant and a food pile may stand on it (a hint in the bar says what to expect, never a warning; blockers still refuse), moves, copies, areas, Done, Undo
//   D  the change of a move, in version 8 and 9: the cells and their anchors (a cell's tie to another place of the map is kept exactly or the move is refused), the solid bits, a bomb's owner word, a pile's bites and points, a plant's dropper, an ant's own type; ONE step of the history
//   E  copy, delete and the move of a whole area: each one Undo, byte for byte, with Redo
//   F  the lists and their limits: 65,535 records, a stack of ants, ants on the move together
//   G  the page: the bar, its buttons and the sentences (this ant or its group for Move, Copy and Delete; the ghost that follows the mouse; the fold of a short screen), on a strict fake DOM; the ghost and the ring drawn on the canvas that lies over the map (and the tool never asks for the map to be painted for them)
//   H  the keys: Enter, the arrows, Shift, Escape, Delete; and the pointer: a tap, a press that drags, a box
//   I  a map that changes under a pick (Undo, another map): the pick is let go; the tool through the page's own Tools (loaded on demand, a part that did not load, Pan has no keys of its own)
//   J  text of a file is only text: names come from the fixed lists; a file whose dictionary holds markup shows none
//   K  the library: every kind of thing of every map picked, moved to a free place and back (undo and redo byte for byte, only what was named changed, the old place empty, the anchors right)
//   L  with a built core: the check of the map says the same after a move, a copy and a delete as it said before (no new finding), on the library
//   M  big maps (512 x 512) stay fast: a pick, the verdict of a ghost and the area of the whole map are measured
//   N  a drag is put down where the ghost is when the mouse or the finger is let go (one change, one Undo; red: the thing goes back to where it was picked up, nothing written, "Put back: ..." says why; Escape, a cancelled or lost pointer, a second lift, Done while held; Done on a red place keeps it in hand; a copy, an area, a group)
//   P  the bar's height holds while a thing is on its way (the map does not strobe under the pointer), and the page follows the bar's own size (a phone turned, a window resized) with an observer that is let go of with the tool
//   O  the bar is a row of its own under the map: it comes after the map and before the line under it, the page is told its height, and the map is given a way to bring a picked thing (or the ghost, when no pointer drives it) above the bar
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const Clock = require('./web_mapmaker_clock.js');       // (CPU time: a budget is held to the work, not to the wait for a core)
const path = require('path');
const crypto = require('crypto');
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const QUICK = argv.includes('--quick');
const REQUIRE = argv.includes('--require');
const MODULE = argv.includes('--module') ? path.resolve(argv[argv.indexOf('--module') + 1]) : (process.env.ANTS_LVL_CORE_DIR || path.join(ROOT, 'build_web', 'lvl_core'));
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Edit = load('mapmaker_edit.js'), Blocks = load('mapmaker_blocks.js'), Palette = load('mapmaker_palette.js'), Objects = load('mapmaker_objects.js'), View = load('mapmaker_view.js');
const Tools = load('mapmaker_tools.js'), Pick = load('mapmaker_pickup.js'), Solid = load('mapmaker_solid.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }
function same(a, b) { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; }
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const EMPTY2 = 0x7FFE;
// the hints for water (what the test matches showed: tools/map_sweep and a probe of the real engine, see the pull request): never a refusal
const HINT_HILL = 'This is water: ants that cannot swim may not be able to leave this hill.', HINT_ANT = 'This is water: a starting ant that cannot swim moves to land beside it, or drowns.';
const HINT_PILE = 'This is water: ants that cannot swim reach it only from the shore. Swimmers can swim to it or build a bridge.';

// ---- made-up maps ----
function makeMap(version, rows, columns, names) {
    names = names || 486;
    const cells = rows * columns, head = version === 8 ? 8 + 2 + 30 : 8 + 2 + 4 + 2 + 2;
    const out = new Uint8Array(head + 2 + 11 * names + 8 + 12 * cells + 12), view = new DataView(out.buffer);
    let at = 0;
    const dword = (v) => { view.setUint32(at, v, true); at += 4; }, word = (v) => { view.setUint16(at, v, true); at += 2; };
    if (version === 8) { dword(8); dword(1); word(10); at += 30; }
    else { dword(9); dword(1); word(10); dword(0); word(1); word(0); }
    word(names - 1);
    for (let i = 0; i < names; i++) { out[at] = i === 485 ? 0x67 : 0x2E; if (i === 485) { out[at + 1] = 0x30; out[at + 2] = 0x31; out[at + 3] = 0x61; } at += 11; }
    dword(rows); dword(columns);
    for (let i = 0; i < cells; i++) { word(485); word(0); word(0xCDCD); }
    for (let i = 0; i < cells; i++) { word(0x7FFE); word(0); word(0); }
    word(0); word(0); word(0); word(0x7FFE); word(0); word(6);
    return out;
}
// the lists: records [[tile, row, column]], piles [{row, column, units, points, stages: [[threshold, tile]]}], waypoints [{row, column, flag, interval}] (block 4)
function mapWith(version, rows, columns, records, piles, waypoints) {
    const base = makeMap(version, rows, columns), f = File.parse(base), bytes = [], word = (v) => bytes.push(v & 255, (v >> 8) & 255), dword = (v) => { word(v); word(v >>> 16); };
    word(records.length); records.forEach((r) => r.forEach(word));
    word(piles.length); piles.forEach((p) => { [p.row, p.column, p.units, p.points, p.stages.length].forEach(word); p.stages.forEach((s) => s.forEach(word)); });
    dword(0x7FFE0000 >>> 0);                                  // (block 3: four bytes)
    word((waypoints || []).length);
    (waypoints || []).forEach((w) => {
        word(w.row); word(w.column); dword(w.flag ? 1 : 0);
        if (w.flag) { dword(w.interval || 60); const b = Buffer.alloc(40); for (let k = 0; k < 5; k++) b.writeDoubleLE(0.2, 8 * k); for (const x of b) bytes.push(x); }
    });
    const out = new Uint8Array(f.blocksAt + bytes.length + 8);
    out.set(base.subarray(0, f.blocksAt), 0); out.set(bytes, f.blocksAt); out.set([0, 0, 0, 0, 6, 0, 0, 0], f.blocksAt + bytes.length);
    return out;
}
function put(f, layer, r, c, t, fl, a) { const p = (layer === 1 ? f.layer1 : f.layer2) + 6 * (r * f.columns + c); [t, fl, a].forEach((v, k) => { f.bytes[p + 2 * k] = v & 255; f.bytes[p + 2 * k + 1] = v >> 8; }); }
function putHill(f, top, left, tile) {
    const er = top + 1, ec = left + 1;
    for (let r = top; r < top + 4; r++) for (let c = left; c < left + 4; c++) put(f, 2, r, c, tile, r === er && c === ec ? 1 : 0, f.version === 8 ? (er | (ec << 8)) : (((er - r) & 255) | (((ec - c) & 255) << 8)));
}
// an object of `cells` ([[dr, dc], ...] from its origin) at (row, column), as the Bigger tool puts it
function putObject(f, tile, row, column, cells) {
    cells.forEach(([dr, dc]) => put(f, 2, row + dr, column + dc, tile, dr === 0 && dc === 0 ? 1 : 0, f.version === 8 ? (row | (column << 8)) : (((-dr) & 255) | (((-dc) & 255) << 8))));
    if (Solid.sets(tile)) cells.forEach(([dr, dc]) => { const p = f.layer1 + 6 * ((row + dr) * f.columns + column + dc) + 2; f.bytes[p] |= 1; });
}
const SQUARE4 = [[-2, -2], [-2, -1], [-2, 0], [-2, 1], [-1, -2], [-1, -1], [-1, 0], [-1, 1], [0, -2], [0, -1], [0, 0], [0, 1], [1, -2], [1, -1], [1, 0], [1, 1]];
const MUSH = 329, GRASS = 1323, ROCK = 302, BOMB = 131, WALL = 134, GREEN_HILL = 248, RED_HILL = 247, BLUE_HILL = 246;
function editor(file) { return Edit.create(file, { rebase: (b) => File.parse(b), erase: Blocks.eraseAt, guard: Blocks.blockedText }); }
function items(f, r, c) { return Pick.itemsAt(f, r, c); }
function lay(f) { return Blocks.layout(f); }

// a pick moved by (dr, dc) through the real history: {ed, change} or {error}
function doIt(ed, picked, dr, dc, mode, kind) {
    const f = ed.file, verdict = Pick.check(f, picked, dr, dc, mode === 'delete' ? 'move' : mode, kind || 'one');
    if (mode !== 'delete' && !verdict.ok) return { error: verdict.text };
    const change = Pick.build(f, picked, dr, dc, mode), dirty = ed.act(change);
    return { change, dirty, file: ed.file };
}

// ---- the strict fake of the DOM ----
function strictDocument() {
    const doc = makeDocument(), make = doc.createElement;
    const list = (nodes) => { const o = { length: nodes.length, item: (i) => nodes[i] || null }; nodes.forEach((n, i) => { o[i] = n; }); return o; };      // (no filter, map or forEach: an old NodeList)
    function harden(n) {
        for (const k of ['innerHTML', 'outerHTML']) Object.defineProperty(n, k, { get() { throw new Error(k + ' was read'); }, set() { throw new Error(k + ' was set: text must be put in with textContent'); } });
        n.insertAdjacentHTML = () => { throw new Error('insertAdjacentHTML was called'); };
        const all = n.querySelectorAll;
        n.querySelectorAll = (s) => list(all(s));
        return n;
    }
    doc.createElement = (tag) => {
        const n = harden(make(tag));
        if (tag === 'canvas') n.getContext = () => n.ctx || (n.ctx = recorder());
        return n;
    };
    const dq = doc.querySelectorAll;
    doc.querySelectorAll = (s) => list(dq(s));
    doc.body = harden(doc.body);
    doc.body.classList = doc.body.classList;
    return doc;
}
// a canvas context that writes down what is drawn
function recorder() {
    const log = { calls: [], fills: [], strokes: [], texts: [], rects: 0 };
    const ctx = new Proxy({}, {
        get(t, k) {
            if (k === 'log') return log;
            if (k === 'measureText') return (s) => ({ width: String(s).length * 6 });
            if (k in t) return t[k];
            return (...a) => { log.calls.push(k); if (k === 'fillRect') { log.fills.push(ctx.fillStyle); log.rects++; } if (k === 'stroke') log.strokes.push(ctx.strokeStyle); if (k === 'fillText') log.texts.push(a[0]); };
        },
        set(t, k, v) { t[k] = v; return true; }
    });
    return ctx;
}

// ---- the page of the tool ----
function page(file, opts) {
    opts = opts || {};
    const doc = strictDocument(), el = (tag) => doc.createElement(tag);
    const wrap = el('div'), cv = el('div'), map = el('canvas'), cvbar = el('div');
    wrap.appendChild(cv); wrap.appendChild(cvbar); cv.appendChild(map); doc.body.appendChild(wrap);
    cv.nextSibling = cvbar;          // (the page's order: the map, the line under it)
    doc.body.style.setProperty = (k, v) => { log.vars[k] = v; };
    map.offsetLeft = 0; map.offsetTop = 0;
    let ed = editor(file), view = View.create(file, opts.width || 600, opts.height || 400), cursor = null, busy = false, stroking = false;          // (`stroking`: the real page takes no change while a stroke goes on, tools.act)
    const log = { acts: 0, refused: 0, refreshes: 0, said: [], focus: 0, frames: [], dialogs: [], settles: [], vars: {} };
    const env = {
        doc, win: Object.assign({ devicePixelRatio: 1, requestAnimationFrame: null }, opts.win || {}), host: { map, cv, hint: el('p') },
        file: () => ed.file, act: (change) => { if (stroking) { log.refused++; return false; } log.acts++; const d = ed.act(change); if (d) pickRef.refresh(); return !!d; },
        say: (t, m) => log.said.push(t + ': ' + m), sprites: () => null, view: () => ({ view, cursor, width: opts.width || 600, height: opts.height || 400 }),
        settle: (keep) => { log.settles.push(keep); }, refresh: () => { log.refreshes++; }, focus: () => { log.focus++; }, busy: () => busy,
        dialogs: { open: (spec) => { const body = el('div'); spec.build(body, { close() {} }); log.dialogs.push({ spec, body }); } }
    };
    if (opts.sprites) env.sprites = opts.sprites;
    const pick = Pick.create(env), pickRef = pick;
    pick.show(true);
    const bar = pick.bar, real = pick.painter;
    // the canvas's stroke: it begins when the tool takes the press, and is over when the tool is told of the lift (canvas.js up)
    const p = Object.assign({}, real, { begin: (c) => { const r = real.begin(c); if (r) stroking = true; return r; }, end: (w) => { stroking = false; return real.end(w); }, cancel: () => { stroking = false; return real.cancel(); }, abandon: () => { stroking = false; return real.abandon(); } });
    const buttons = () => { const o = {}; bar.querySelectorAll('.pk-b').length; for (let i = 0; i < bar.querySelectorAll('.pk-b').length; i++) { const b = bar.querySelectorAll('.pk-b')[i]; o[b.getAttribute('data-k')] = b; } return o; };
    const api = {
        doc, pick, p, bar, env, log, cv, cvbar, wrap, ed: () => ed, file: () => ed.file, buttons, map, over: pick.over,
        text: () => bar.textContent, hidden: () => bar.hidden,
        press: (key) => { const b = buttons()[key]; if (!b) throw new Error('no button ' + key + ' in: ' + Object.keys(buttons()).join(',')); b.click(); },
        has: (key) => !!buttons()[key], off: (key) => !!buttons()[key].disabled,
        warn: () => { const n = bar.querySelector('.pk-warn'); return n ? n.textContent : ''; },
        // a tap on a cell, as the canvas makes it: a press that is a drag (canMove) and a lift, else a tap of the tool
        tap: (r, c) => { const cell = { row: r, column: c }; if (p.canMove(cell)) { if (p.begin(cell)) p.end(); } else if (p.tapOnly(cell)) p.begin(cell); },
        drag: (r0, c0, r1, c1) => { const a = { row: r0, column: c0 }, b = { row: r1, column: c1 }; if (!p.begin(a)) return false; p.extend(a, { row: (r0 + r1) / 2 | 0, column: (c0 + c1) / 2 | 0 }); p.extend(a, b); p.end(); return true; },
        key: (k, extra) => pick.painter.key(Object.assign({ key: k, shiftKey: false }, extra || {})),
        setBusy: (v) => { busy = v; }, setCursor: (c) => { cursor = c; }, state: () => pick.state(), reload: (bytes) => { ed = editor(File.parse(bytes)); pick.setMap(); },
        undo: () => { ed.undo(); pick.refresh(); }, redo: () => { ed.redo(); pick.refresh(); }
    };
    return api;
}

// ================= A =================
function caseA() {
    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')';
        const f = File.parse(mapWith(version, 30, 30, [[153, 5, 5], [153, 5, 5], [152 | (4 << 12), 8, 8], [404, 20, 20]], [{ row: 15, column: 15, units: 30, points: 25, stages: [[30, 264], [20, 265], [10, 266], [0, EMPTY2]] }], [{ row: 20, column: 20, flag: 1, interval: 45 }]));
        putHill(f, 2, 20, RED_HILL);
        putObject(f, MUSH, 25, 10, SQUARE4);
        put(f, 2, 15, 15, 264, 1, f.version === 8 ? (15 | (15 << 8)) : 0);          // a library file shows the first stage on the pile's own cell
        const hill = items(f, 3, 21);
        equal(hill.length, 1, 'one thing on a hill cell' + tag);
        check(hill[0].k === 'obj' && hill[0].fp.length === 16 && hill[0].hill === 1 && hill[0].entrance === 3 * 30 + 21, 'it is a whole red hill with 16 cells and its door' + tag);
        for (let r = 2; r < 6; r++) for (let c = 20; c < 24; c++) check(items(f, r, c).length === 1 && items(f, r, c)[0].fp.length === 16, 'every cell of the hill picks the hill: ' + r + ',' + c + tag);
        equal(items(f, 1, 20).length, 0, 'the cell above the hill is empty' + tag);
        equal(Pick.describe(f, hill, 'one', 0), { name: 'Red hill', rest: ' \u00b7 4 x 4 cells, with its door' }, 'the hill is described in plain words' + tag);
        const mush = items(f, 24, 10);
        check(mush.length === 1 && mush[0].k === 'obj' && mush[0].fp.length === 16 && mush[0].hill === -1, 'a bigger object is picked whole' + tag);
        equal(Pick.describe(f, mush, 'one', 0), { name: 'Mushroom', rest: ' \u00b7 16 cells' }, 'and called a mushroom, from the fixed list' + tag);
        const pile = items(f, 15, 15);
        check(pile.length === 1 && pile[0].k === 'pile' && pile[0].fp.length === 1, 'a pile is picked, and the stage object of its cell with it, not as an object' + tag);
        equal(Pick.describe(f, pile, 'one', 0), { name: 'Food pile', rest: ' \u00b7 Pretzel \u00b7 30 bites of 25 points' }, 'a food pile says what it is, its bites and points' + tag);
        const plant = items(f, 20, 20);
        check(plant.length === 1 && plant[0].k === 'plant' && plant[0].w === 0, 'a plant is picked with its waypoint' + tag);
        equal(Pick.describe(f, plant, 'one', 0), { name: 'Clover', rest: ' \u00b7 drops a power-up every 45 seconds' }, 'a dropper says how often it drops' + tag);
        const stack = items(f, 5, 5);
        check(stack.length === 2 && stack.every((x) => x.k === 'ant' && x.team === 2) && stack[0].i === 1 && stack[1].i === 0, 'a stack of two ants gives both, the one on top (the later record) first' + tag);
        const typed = items(f, 8, 8)[0];
        equal(Pick.describe(f, [typed], 'one', 1), { name: version === 9 ? 'Black combat ant' : 'Black ant', rest: ' \u00b7 one of 1 \u00b7 no other ant of its team touches it' }, 'an ant of an Extended map says its own type' + tag);
        equal(items(f, 0, 0).length, 0, 'an empty cell gives nothing' + tag);
        equal(items(f, -1, 3).length + items(f, 3, 99).length, 0, 'a cell off the map gives nothing' + tag);
        // what is under what: an ant, a plant, a pile and an object on one cell
        const g = File.parse(mapWith(version, 20, 20, [[153, 4, 4], [405, 4, 4]], [{ row: 4, column: 4, units: 10, points: 30, stages: [[10, 280], [0, EMPTY2]] }], []));
        put(g, 2, 4, 4, ROCK, 1, g.version === 8 ? (4 | (4 << 8)) : 0);
        const all = items(g, 4, 4);
        equal(all.map((x) => x.k), ['ant', 'plant', 'pile', 'obj'], 'on one cell the ant is on top, then the plant, the pile, the object' + tag);
    }
    // a pile with a stage object that is bigger than a cell: all its cells are the pile's
    const f = File.parse(mapWith(8, 30, 30, [], [{ row: 10, column: 10, units: 20, points: 25, stages: [[20, 1331], [0, EMPTY2]] }], []));
    putObject(f, 1331, 10, 10, [[0, 0], [0, 1], [1, 0], [1, 1]]);
    const big = items(f, 11, 11);
    check(big.length === 1 && big[0].k === 'pile' && big[0].fp.length === 4, 'a pile whose stage covers four cells is picked from any of them, with its four cells');
    // the word for an object that is not in the lists, and for ones that are
    equal([Pick.nameOf(ROCK), Pick.nameOf(62), Pick.nameOf(131), Pick.nameOf(134), Pick.nameOf(404), Pick.nameOf(410), Pick.nameOf(GRASS), Pick.nameOf(289), Pick.nameOf(349), Pick.nameOf(5), Pick.nameOf(290)],
        ['rock', 'combat ant power-up', 'black bomb', 'fire wall', 'clover', 'flower', 'grass', 'rock', 'rubber duck', 'object', 'rock'], 'the plain words for tiles, from the palette and a short list');
}

// ================= B =================
function caseB() {
    const recs = [[155, 10, 10], [155, 10, 11], [155, 11, 12], [155, 12, 12], [155, 13, 13], [155, 9, 9], [155, 20, 20], [153, 10, 12], [155, 10, 10]];
    const f = File.parse(mapWith(9, 30, 30, recs, [], []));
    const one = items(f, 12, 12)[0], group = Pick.antGroup(f, one);
    equal(group.length, 7, 'the group is the Red ants that touch, a stack counted: 7 of 9 (one is far away, one is Blue)');
    check(group[0] === one || (group[0].i === one.i), 'the picked ant is first');
    check(group.every((x) => x.team === 1), 'only the team of the picked ant');
    equal(Pick.describe(f, [one], 'one', group.length), { name: 'Red ant', rest: ' \u00b7 one of 8 \u00b7 its group: 7 Red ants next to each other' }, 'the bar says the team count and the group');
    equal(Pick.describe(f, group, 'group', group.length), { name: '7 Red ants', rest: ' next to each other' }, 'a group is described as a group');
    const far = items(f, 20, 20)[0];
    equal(Pick.antGroup(f, far).length, 1, 'an ant alone is its own group');
    // an area
    const g = File.parse(mapWith(8, 40, 40, [[155, 12, 30], [155, 12, 31], [155, 14, 30], [155, 5, 5], [155, 23, 30], [155, 9, 30], [155, 15, 23], [155, 15, 36]], [{ row: 20, column: 30, units: 30, points: 25, stages: [[30, 264], [0, EMPTY2]] }], []));
    putObject(g, GRASS, 14, 25, [[-1, -1], [-1, 0], [0, -1], [0, 0]]);
    putObject(g, ROCK, 16, 33, [[0, 0]]);
    putHill(g, 30, 30, GREEN_HILL);
    putObject(g, MUSH, 30, 10, SQUARE4);
    const box = { r0: 10, c0: 24, r1: 22, c1: 35 }, inside = Pick.areaItems(g, box);
    equal(inside.length, 6, 'the area holds the 3 ants, the grass, the rock and the pile (not the far ant, the hill or the mushroom)');
    equal(Pick.describe(g, inside, 'area', 0), { name: 'Area: 6 items', rest: ' \u00b7 3 Red ants, 1 food pile, 1 grass, 1 rock' }, 'with the words of the picture (the ants one cell outside the box on each side are not in it)');
    const cut = Pick.areaItems(g, { r0: 10, c0: 26, r1: 22, c1: 35 });
    check(!cut.some((x) => x.k === 'obj' && x.tile === GRASS), 'an object that the box only reaches into (half of the grass) is not taken');
    const withHill = Pick.areaItems(g, { r0: 28, c0: 28, r1: 36, c1: 36 });
    check(withHill.length === 1 && withHill[0].hill === 0, 'a hill that is whole inside the box is taken, with its team');
    const partHill = Pick.areaItems(g, { r0: 28, c0: 28, r1: 32, c1: 32 });
    equal(partHill.length, 0, 'a hill that the box cuts is not taken');
    equal(Pick.areaItems(g, { r0: 0, c0: 0, r1: 3, c1: 3 }).length, 0, 'an empty box holds nothing');
    equal(Pick.describe(g, Pick.areaItems(g, { r0: 0, c0: 0, r1: 39, c1: 39 }), 'area', 0).name, 'Area: 13 items', 'the whole map holds the 4 ants, the pile, the grass, the rock, the hill and the mushroom');
}

// ================= C =================
function caseC() {
    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')';
        const f = File.parse(mapWith(version, 40, 40, [[155, 20, 20], [153, 30, 30], [405, 33, 33]], [{ row: 25, column: 25, units: 10, points: 30, stages: [[10, 280], [0, EMPTY2]] }], []));
        putHill(f, 2, 2, RED_HILL);
        putHill(f, 2, 30, BLUE_HILL);
        putObject(f, MUSH, 10, 14, SQUARE4);
        putObject(f, ROCK, 36, 4, [[0, 0]]);
        for (let c = 0; c < 6; c++) put(f, 1, 38, 10 + c, 476, 0, 0xCDCD);         // water
        put(f, 1, 22, 22, 485, 1, 0xCDCD);                                         // solid ground
        const red = items(f, 3, 3)[0], cur = (picked, dr, dc, mode, kind) => Pick.check(f, picked, dr, dc, mode || 'move', kind || 'one');
        check(cur([red], 0, 0).ok, 'a hill where it stands is fine (it moves along)' + tag);
        let v = cur([red], 5, 10);
        check(v.ok === false && /^The red hill cannot go here\. It needs a free 4 x 4 square, and the mushroom is in the way \(12 cells\)\. Move it a little, or move the mushroom first\.$/.test(v.text) && v.cells.length === 12, 'a hill on a mushroom: the words of the picture: ' + v.text + tag);
        v = cur([red], -3, 0);
        check(v.ok === false && /^The red hill cannot go here\. It needs a free 4 x 4 square inside the map\. Move it a little\.$/.test(v.text) && v.cells.length === 0, 'a hill past the top edge: ' + v.text + tag);
        v = cur([red], 0, 37);
        check(v.ok === false && /inside the map/.test(v.text), 'a hill past the right edge' + tag);
        v = cur([red], 0, 28);
        check(v.ok === false && /the blue hill is in the way \(16 cells\)/.test(v.text), 'a hill onto the blue hill names it: ' + v.text + tag);
        v = cur([red], 33, 8);
        check(v.ok === true && v.hint === HINT_HILL && v.cells === undefined, 'a hill with water under it may go there, with the hint: ' + JSON.stringify(v) + tag);
        check(cur([red], 32, 8).ok === true && cur([red], 32, 8).hint === undefined, 'a square that is dry has no hint, one row on water has it' + tag);
        v = cur([red], 18, 18);
        check(v.ok === false && /Starting ants are in the way/.test(v.text), 'a hill onto ants' + tag);
        v = cur([red], 23, 22);
        check(v.ok === false && /the food pile is in the way/i.test(v.text), 'a hill onto a food pile' + tag);
        check(cur([red], 5, 5).ok, 'a free place is free' + tag);
        // objects
        const rock = items(f, 36, 4)[0], mush = items(f, 10, 14)[0];
        v = cur([rock], -26, 10);
        check(!v.ok && /^The rock cannot go here\. The mushroom is in the way \(1 cell\)\. Move it a little, or move the mushroom first\.$/.test(v.text), 'a rock onto the mushroom, counted in the singular: ' + v.text + tag);
        v = cur([rock], -33, -1);
        check(!v.ok && /the red hill is in the way/i.test(v.text), 'a rock onto a hill' + tag);
        v = cur([rock], -16, 16);
        check(!v.ok && /Starting ants are in the way\. Move it a little\.$/.test(v.text), 'a rock onto ants (nothing to name and move first)' + tag);
        v = cur([rock], -11, 21);
        check(!v.ok && /the food pile is in the way/i.test(v.text), 'a rock onto a pile' + tag);
        v = cur([rock], -3, 29);
        check(!v.ok && /the plant is in the way/i.test(v.text), 'a rock onto a plant' + tag);
        check(cur([rock], 2, 8).ok, 'a rock may go on water (a toy can float)' + tag);
        v = cur([rock], 4, 0);
        check(!v.ok && /off the edge of the map/.test(v.text) && cur([rock], 3, 0).ok, 'the last row is the last place: one row more is off the map' + tag);
        check(cur([rock], -2, 8).ok && cur([rock], -1, 0).ok, 'a rock on free ground' + tag);
        v = cur([mush], 0, -20);
        check(!v.ok && /would go off the edge of the map/.test(v.text) && /^The mushroom cannot go here\./.test(v.text), 'a mushroom off the left edge: ' + v.text + tag);
        // ants
        const ant = items(f, 20, 20)[0];
        v = cur([ant], 18, -8);
        check(v.ok === true && v.hint === HINT_ANT, 'an ant on water may go there, with the hint: ' + JSON.stringify(v) + tag);
        check(cur([ant], 18, -8, 'copy').ok === true && cur([ant], 18, -8, 'copy').hint === HINT_ANT, 'so may the copy of an ant' + tag);
        v = cur([ant], 10, 10);
        check(!v.ok && /An ant is already there/.test(v.text), 'an ant onto another ant' + tag);
        v = cur([ant], 2, 2);
        check(!v.ok && /solid/.test(v.text), 'an ant onto solid ground' + tag);
        v = cur([ant], -17, -17);
        check(!v.ok && /the red hill is in the way/i.test(v.text), 'an ant onto a hill: ' + v.text + tag);
        v = cur([ant], 5, 5);
        check(!v.ok && /the food pile is in the way/i.test(v.text), 'an ant onto a pile' + tag);
        v = cur([ant], -10, -6);
        check(!v.ok && /the mushroom is in the way/i.test(v.text), 'an ant onto an object' + tag);
        check(cur([ant], -1, 0).ok, 'an ant one cell up' + tag);
        v = cur([ant], 0, -21);
        check(!v.ok && /edge of the map/.test(v.text), 'an ant off the edge' + tag);
        // a pile and a plant
        const pile = items(f, 25, 25)[0], plant = items(f, 33, 33)[0];
        v = cur([pile], 5, 5);
        check(!v.ok && /Starting ants are in the way/.test(v.text), 'a pile onto ants' + tag);
        v = cur([pile], 13, -13);
        check(v.ok === true && v.hint === HINT_PILE, 'a pile on water may go there, with the hint: ' + JSON.stringify(v) + tag);
        v = cur([pile], -15, -11);
        check(!v.ok && /mushroom/.test(v.text), 'a pile onto the mushroom' + tag);
        check(cur([pile], 1, 1).ok, 'a pile on free ground' + tag);
        v = cur([plant], -30, -30);
        check(!v.ok && /red hill/.test(v.text), 'a plant onto a hill' + tag);
        v = cur([plant], -13, -13);
        check(!v.ok && /Starting ants/.test(v.text), 'a plant onto ants' + tag);
        check(cur([plant], 3, 3).ok, 'a plant on free ground' + tag);
        check(cur([plant], 5, -21).ok && cur([plant], 5, -21).hint === undefined && Edit.tile(f, 1, 38 * 40 + 12) === 476, 'a plant may stand on water, like a rock, and says nothing about it' + tag);
        check(cur([rock], 2, 8).hint === undefined, 'nor does a rock' + tag);
        // a copy
        v = cur([rock], 0, 0, 'copy');
        check(!v.ok && v.plain === undefined && /The copy cannot go here\. The rock is in the way \(1 cell\)/.test(v.text), 'a copy cannot land on its own original: ' + v.text + tag);
        v = cur([red], 5, 5, 'copy');
        check(!v.ok && /^A team has one hill, so a hill cannot be copied/.test(v.text), 'a hill cannot be copied' + tag);
        v = cur([items(f, 3, 3)[0], rock], 5, 5, 'copy', 'area');
        check(!v.ok && /one hill/.test(v.text), 'nor can an area that holds one' + tag);
        check(cur([rock], 1, 0, 'copy').ok && cur([ant], -1, 0, 'copy').ok && cur([pile], 2, 0, 'copy').ok && cur([plant], 1, 0, 'copy').ok, 'a rock, an ant, a pile and a plant can be copied to a free cell' + tag);
        // two things that move together may land on each other's old cells
        const two = [rock, items(f, 20, 20)[0]];
        check(cur(two, -1, 0, 'move', 'area').ok, 'an area moves as one body' + tag);
        v = cur(two, -2, -12, 'move', 'area');
        check(!v.ok && /^These 2 items cannot go here\./.test(v.text), 'a blocked area says "These 2 items": ' + v.text + tag);
    }
    // a Classic map keeps the place of an object in one byte of row and column
    const wide = File.parse(mapWith(8, 3, 300, [], [], []));
    putObject(wide, ROCK, 1, 250, [[0, 0]]);
    const r = items(wide, 1, 250)[0];
    check(Pick.check(wide, [r], 0, 5, 'move', 'one').ok, 'a rock to column 255 of a Classic map');
    const v = Pick.check(wide, [r], 0, 6, 'move', 'one');
    check(!v.ok && /one number, so nothing can go past row 255 or column 255/.test(v.text), 'but not to column 256: ' + v.text);
    const ext = File.parse(mapWith(9, 3, 300, [], [], []));
    putObject(ext, ROCK, 1, 250, [[0, 0]]);
    check(Pick.check(ext, [items(ext, 1, 250)[0]], 0, 40, 'move', 'one').ok, 'an Extended map has offsets: column 290 is fine');
    // the corners of a Classic map as big as the checker allows (90 rows, 120 columns)
    const full = File.parse(mapWith(8, 90, 120, [], [], []));
    putObject(full, ROCK, 5, 5, [[0, 0]]);
    const rk = items(full, 5, 5)[0];
    check(Pick.check(full, [rk], 84, 114, 'move', 'one').ok && !Pick.check(full, [rk], 85, 114, 'move', 'one').ok && !Pick.check(full, [rk], 84, 115, 'move', 'one').ok, 'the far corner of a 90 x 120 map is the last place, one more cell is off the map');
}

// ================= W =================
// water is no refusal: a hill, a starting ant and a food pile may stand on it, and the bar says (in words that the test matches showed) what to expect
function caseW() {
    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')';
        const f = File.parse(mapWith(version, 30, 30, [[155, 5, 5], [153, 6, 5], [153, 6, 6], [405, 8, 25]], [{ row: 5, column: 20, units: 10, points: 30, stages: [[10, 280], [0, EMPTY2]] }, { row: 15, column: 15, units: 20, points: 25, stages: [[20, 1331], [0, EMPTY2]] }], []));
        putHill(f, 2, 10, RED_HILL);
        for (let r = 20; r < 30; r++) for (let c = 0; c < 30; c++) put(f, 1, r, c, 476, 0, 0xCDCD);
        putObject(f, ROCK, 27, 12, [[0, 0]]);
        putObject(f, 1331, 15, 15, [[0, 0], [0, 1], [1, 0], [1, 1]]);
        const red = items(f, 3, 11)[0], ant = items(f, 5, 5)[0], ant2 = items(f, 6, 5)[0], pile = items(f, 5, 20)[0], plant = items(f, 8, 25)[0];
        const cur = (picked, dr, dc, mode, kind) => Pick.check(f, picked, dr, dc, mode || 'move', kind || 'one');
        equal(cur([red], 20, 0), { ok: true, hint: HINT_HILL }, 'a hill on water: ok, with the hint' + tag);
        equal(cur([ant], 20, 0), { ok: true, hint: HINT_ANT }, 'an ant on water' + tag);
        equal(cur([pile], 20, 0), { ok: true, hint: HINT_PILE }, 'a pile on water' + tag);
        equal(cur([plant], 20, 0), { ok: true }, 'a plant on water: no hint' + tag);
        // a pile counts by its anchor cell: the cells of its picture that reach water do not give the hint, a dry anchor does not either
        const big = items(f, 15, 15)[0];
        check(big.k === 'pile' && big.fp.length === 4, 'the pile with a stage object of 2 x 2 cells is one thing of 4 cells' + tag);
        equal(cur([big], 4, 0), { ok: true }, 'its anchor is dry, its lower row is on water: no hint' + tag);
        equal(cur([big], 10, 0), { ok: true, hint: HINT_PILE }, 'its anchor on water: the hint' + tag);
        equal(cur([ant], -1, 0), { ok: true }, 'dry ground: no hint' + tag);
        equal(cur([red, ant, pile], 20, 0, 'move', 'area'), { ok: true, hint: [HINT_HILL, HINT_ANT, HINT_PILE].join(' ') }, 'an area with all three: the three hints, once each, in the order of the things' + tag);
        equal(cur([ant, ant2, plant], 20, 0, 'move', 'area').hint, HINT_ANT, 'two ants on water: one hint' + tag);
        equal(cur([red, ant], 20, 0, 'copy', 'area').ok, false, 'a copy of an area that holds the hill is still refused (a team has one hill)' + tag);
        equal(cur([ant, pile], 20, 0, 'copy', 'area'), { ok: true, hint: HINT_ANT + ' ' + HINT_PILE }, 'a copy to water is allowed, with the hints' + tag);
        const rocky = cur([red], 22, 2);
        check(rocky.ok === false && /the rock is in the way/.test(rocky.text) && rocky.hint === undefined, 'what is in the way still refuses on water, and no hint is given: ' + rocky.text + tag);
        // the page: the hint is in the bar, the ghost is green, Done works, the note after it keeps the hint, one Undo
        const q = page(f, {}), was = Uint8Array.from(f.bytes);
        q.tap(3, 11); q.press('move'); q.tap(24, 11);
        check(q.state().mode === 'moving' && q.warn() === '' && !q.off('done') && q.text().indexOf(HINT_HILL) >= 0 && q.state().verdict.ok === true, 'the bar shows the hint for a hill on water, not a warning, and Done is on: ' + q.text() + tag);
        q.press('nudge');
        check(q.text().indexOf(HINT_HILL) >= 0 && q.warn() === '', 'the hint is also there with the nudge arrows' + tag);
        q.press('done');
        check(q.log.acts === 1 && q.ed().history.size().strokes === 1 && q.state().mode === 'picked' && Edit.tile(q.file(), 2, 23 * 30 + 11) === RED_HILL && Edit.tile(q.file(), 2, 3 * 30 + 11) === EMPTY2, 'Done puts the hill on water: one change' + tag);
        check(/One Undo takes it back\./.test(q.text()) && q.text().indexOf(HINT_HILL) >= 0, 'and the note after it keeps the hint: ' + q.text() + tag);
        q.undo(); check(same(q.file().bytes, was), 'one Undo takes it back, byte for byte' + tag);
        // off the map the ghost is gone and the bar says that a lift takes it back, without the hint
        const off = page(File.parse(Uint8Array.from(was)), {});
        off.p.begin({ row: 5, column: 5 }); off.p.extend({ row: 5, column: 5 }, { row: 24, column: 5 });
        check(off.text().indexOf(HINT_ANT) >= 0, 'while the ghost is on water the bar has the hint' + tag);
        off.p.away({ outside: true, x: 900, y: 100 });
        check(off.text().indexOf(HINT_ANT) < 0 && /goes back where it was/.test(off.text()), 'while the pointer is off the map the bar says a lift takes it back, and has no hint: ' + off.text() + tag);
        off.p.end();
        // a lift on water puts it down in one step
        const w = page(File.parse(Uint8Array.from(was)), {});
        w.p.begin({ row: 5, column: 5 }); w.p.extend({ row: 5, column: 5 }, { row: 24, column: 5 }); w.p.end();
        check(w.log.acts === 1 && w.state().mode === 'picked' && /moves to land beside it/.test(w.text()), 'a lift of an ant on water puts it down, and the note says what to expect: ' + w.text() + tag);
    }
}

// ================= D =================
function wordsOf(f, idx) { return [Edit.tile(f, 2, idx), Edit.flags(f, 2, idx), Edit.third(f, 2, idx)]; }
function caseD() {
    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')';
        // a hill
        let f = File.parse(mapWith(version, 30, 30, [], [], []));
        putHill(f, 3, 3, RED_HILL);
        let ed = editor(f), before = Uint8Array.from(f.bytes);
        let res = doIt(ed, items(f, 4, 4), 6, 9, 'move');
        check(!res.error, 'the hill moves' + tag);
        let g = ed.file;
        for (let r = 0; r < 30; r++) for (let c = 0; c < 30; c++) {
            const inNew = r >= 9 && r < 13 && c >= 12 && c < 16, w = wordsOf(g, r * 30 + c);
            if (inNew) {
                const er = 10, ec = 13, want = version === 8 ? (er | (ec << 8)) : (((er - r) & 255) | (((ec - c) & 255) << 8));
                check(w[0] === RED_HILL && w[1] === (r === er && c === ec ? 1 : 0) && w[2] === want, 'the new hill cell ' + r + ',' + c + ' has the tile, the door flag and the anchor at the door' + tag);
            } else check(w[0] === EMPTY2 && w[1] === 0 && w[2] === 0, 'every other cell is empty (' + r + ',' + c + ')' + tag);
        }
        check(ed.history.size().strokes === 1, 'one move is one step of the history' + tag);
        check(res.change.splices.length === 0 && res.change.names.length === 0, 'a hill needs no splice and no name' + tag);
        let after = Uint8Array.from(g.bytes);
        ed.undo(); check(same(ed.file.bytes, before), 'Undo gives the bytes back' + tag);
        ed.redo(); check(same(ed.file.bytes, after), 'Redo gives the same bytes' + tag);
        // back again is the first map
        f = ed.file; res = doIt(ed, items(f, 10, 13), -6, -9, 'move'); check(same(ed.file.bytes, before), 'moved back, the map is byte for byte the first one' + tag);
        // the hill moved one cell (over its own old cells), and the solid bits of layer 1 do not change
        f = File.parse(mapWith(version, 30, 30, [], [], [])); putHill(f, 3, 3, RED_HILL); put(f, 1, 4, 4, 485, 1, 0xCDCD);
        ed = editor(f); before = Uint8Array.from(f.bytes);
        res = doIt(ed, items(f, 4, 4), 0, 1, 'move');
        check(!res.error && Edit.tile(ed.file, 2, 3 * 30 + 3) === EMPTY2 && Edit.tile(ed.file, 2, 3 * 30 + 4) === RED_HILL && Edit.tile(ed.file, 2, 3 * 30 + 7) === RED_HILL, 'one cell to the right: the old column is empty, the new one has the hill' + tag);
        check((Edit.flags(ed.file, 1, 4 * 30 + 4) & 1) === 1, 'a hill does not own the solid bit under it: it stays' + tag);
        ed.undo(); check(same(ed.file.bytes, before), 'and Undo restores it' + tag);

        // a bigger object with a solid tile: the old solid bits go, the new ones come, also when the new cells cover some of the old ones
        f = File.parse(mapWith(version, 30, 30, [], [], [])); putObject(f, MUSH, 10, 10, SQUARE4); ed = editor(f); before = Uint8Array.from(f.bytes);
        check(Solid.sets(MUSH), 'the mushroom is solid (the table)' + tag);
        res = doIt(ed, items(f, 10, 10), 0, 2, 'move'); g = ed.file;
        const solidCells = (x) => { const o = []; for (let i = 0; i < x.cells; i++) if (Edit.flags(x, 1, i) & 1) o.push(i); return o; };
        equal(solidCells(g).join(), solidCells((() => { const h = File.parse(mapWith(version, 30, 30, [], [], [])); putObject(h, MUSH, 10, 12, SQUARE4); return h; })()).join(), 'the solid bits are under the new cells only (the nudge by two overlaps)' + tag);
        for (const [dr, dc] of SQUARE4) { const w = wordsOf(g, (10 + dr) * 30 + 12 + dc), want = version === 8 ? (10 | (12 << 8)) : (((-dr) & 255) | (((-dc) & 255) << 8)); check(w[0] === MUSH && w[1] === (dr === 0 && dc === 0 ? 1 : 0) && w[2] === want, 'the mushroom cell ' + dr + ',' + dc + ' is anchored at its origin' + tag); }
        ed.undo(); check(same(ed.file.bytes, before), 'Undo gives the object and its solid bits back' + tag);
        res = doIt(ed, items(ed.file, 10, 10), 3, -4, 'move'); const moved = Uint8Array.from(ed.file.bytes);
        res = doIt(ed, items(ed.file, 13, 6), -3, 4, 'move'); check(same(ed.file.bytes, before), 'there and back is the same map, solid bits and all' + tag);
        check(ed.history.size().strokes === 2, 'two moves are two steps' + tag);

        // bombs and a wall: the owner word of a bomb stays, a neutral bomb stays neutral
        f = File.parse(mapWith(version, 20, 20, [], [], []));
        put(f, 2, 5, 5, BOMB, 1, 0x0101); put(f, 2, 5, 7, 130, 1, 0x0001); put(f, 2, 5, 9, WALL, 1, version === 8 ? (5 | (9 << 8)) : 0); put(f, 2, 5, 11, WALL, 1, 0x0101);
        ed = editor(f);
        for (const [c, want] of [[5, [BOMB, 1, 0x0101]], [7, [130, 1, 0x0001]], [11, [WALL, 1, 0x0101]]]) {
            const idx = 5 * 20 + c;
            res = doIt(ed, items(ed.file, 5, c), 4, 0, 'move');
            equal(wordsOf(ed.file, 9 * 20 + c), want, 'the bomb or wall at column ' + c + ' keeps its words after a move' + tag);
            check(wordsOf(ed.file, idx)[0] === EMPTY2, 'and its old cell is empty' + tag);
        }
        res = doIt(ed, items(ed.file, 5, 9), 4, 0, 'move');
        equal(wordsOf(ed.file, 9 * 20 + 9), [WALL, 1, version === 8 ? (9 | (9 << 8)) : 0], 'a fire wall that is its own anchor gets the anchor of its new cell' + tag);
        res = doIt(ed, items(ed.file, 9, 5), 2, 3, 'copy'); equal(wordsOf(ed.file, 11 * 20 + 8), [BOMB, 1, 0x0101], 'a copy of a neutral bomb is neutral too' + tag);
        check(items(ed.file, 9, 5).length === 1, 'and the first is still there' + tag);

        // a pile: its bites, points and stages as they were, only its place moves; its stage object goes with it
        f = File.parse(mapWith(version, 30, 30, [], [{ row: 5, column: 5, units: 77, points: 33, stages: [[77, 264], [30, 265], [0, EMPTY2]] }, { row: 9, column: 9, units: 10, points: 30, stages: [[10, 280], [0, EMPTY2]] }], []));
        put(f, 2, 5, 5, 264, 1, version === 8 ? (5 | (5 << 8)) : 0);
        ed = editor(f); before = Uint8Array.from(f.bytes);
        res = doIt(ed, items(f, 5, 5), 12, 14, 'move'); g = ed.file;
        const piles = lay(g).piles;
        check(piles.length === 2 && piles[1].row === 9 && piles[0].row === 17 && piles[0].column === 19 && piles[0].units === 77 && piles[0].points === 33 && JSON.stringify(piles[0].stages) === JSON.stringify([[77, 264], [30, 265], [0, EMPTY2]]), 'the pile has its bites, points and stages in its new place' + tag);
        equal(wordsOf(g, 17 * 30 + 19), [264, 1, version === 8 ? (17 | (19 << 8)) : 0], 'its stage object stands on the new cell, anchored there' + tag);
        check(wordsOf(g, 5 * 30 + 5)[0] === EMPTY2, 'and the old cell is empty' + tag);
        check(res.change.splices.length === 1 && res.change.splices[0].removed.length <= 16, 'the pile list changes in one small splice' + tag);
        ed.undo(); check(same(ed.file.bytes, before), 'Undo' + tag);

        // a plant with its dropper (52 bytes of block 4, with its five odds) moves with it
        f = File.parse(mapWith(version, 30, 30, [[404, 4, 4], [153, 8, 8]], [], [{ row: 4, column: 4, flag: 1, interval: 45 }]));
        ed = editor(f); before = Uint8Array.from(f.bytes);
        res = doIt(ed, items(f, 4, 4), 10, 11, 'move'); g = ed.file;
        let l = lay(g);
        check(l.records[0].row === 14 && l.records[0].column === 15 && l.records[0].tile === 404 && l.waypoints[0].row === 14 && l.waypoints[0].column === 15 && l.waypoints[0].flag === 1 && l.waypoints[0].interval === 45 && l.waypoints[0].length === 52, 'the plant and its dropper are in the new cell, the interval is 45' + tag);
        equal(Array.from(g.bytes.subarray(g.blocksAt + l.waypoints[0].rel + 8, g.blocksAt + l.waypoints[0].rel + 52)), Array.from(before.subarray(f.blocksAt + lay(f).waypoints[0].rel + 8, f.blocksAt + lay(f).waypoints[0].rel + 52)), 'the dropper\'s interval and its five odds are the same bytes' + tag);
        check(res.change.splices.length === 2 && res.change.splices[0].rel > res.change.splices[1].rel, 'two splices, the later one first' + tag);
        ed.undo(); check(same(ed.file.bytes, before), 'Undo' + tag);
        res = doIt(ed, items(ed.file, 4, 4), 3, 0, 'copy'); l = lay(ed.file);
        check(l.records.length === 3 && l.waypoints.length === 2 && l.records[2].row === 7 && l.waypoints[1].row === 7 && l.waypoints[1].flag === 1 && l.waypoints[1].interval === 45, 'a copy of a plant is a plant and a dropper more, the first stays' + tag);
        ed.undo(); check(same(ed.file.bytes, before), 'copy then Undo is the first map' + tag);

        // ants: the team and the type of each one stay (an Extended map has a type for each ant), only the place moves
        const type = version === 9 ? 4 << 12 : 0;
        f = File.parse(mapWith(version, 30, 30, [[153 | type, 3, 3], [153, 3, 4], [155 | (version === 9 ? 2 << 12 : 0), 8, 8], [404, 20, 20]], [], [{ row: 20, column: 20, flag: 0 }]));
        ed = editor(f); before = Uint8Array.from(f.bytes);
        res = doIt(ed, items(f, 3, 3), 10, 10, 'move'); l = lay(ed.file);
        equal(l.records.map((r) => [r.tile, r.row, r.column]), [[153 | type, 13, 13], [153, 3, 4], [155 | (version === 9 ? 2 << 12 : 0), 8, 8], [404, 20, 20]], 'one ant moves with its own type, the others stay' + tag);
        res = doIt(ed, items(ed.file, 13, 13), 2, 2, 'copy'); l = lay(ed.file);
        equal(l.records.map((r) => [r.tile, r.row, r.column]).slice(-1), [[153 | type, 15, 15]], 'a copy of an ant has the same team and type, at the end of the list' + tag);
        check(l.records.length === 5 && ed.history.size().strokes === 2, 'one record more, one more step' + tag);
        ed.undo(); ed.undo(); check(same(ed.file.bytes, before), 'both undone' + tag);
        // delete one ant of a team, a plant, a pile, an object, a hill
        res = doIt(ed, items(ed.file, 3, 3), 0, 0, 'delete'); l = lay(ed.file);
        check(l.records.length === 3 && Blocks.facts(ed.file, false).ants[1] === 1, 'one ant is taken away, the team keeps its other' + tag);
        ed.undo();
        res = doIt(ed, items(ed.file, 20, 20), 0, 0, 'delete'); l = lay(ed.file);
        check(l.records.length === 3 && l.waypoints.length === 0, 'a plant is taken away with its waypoint' + tag);
        ed.undo(); check(same(ed.file.bytes, before), 'and brought back' + tag);
    }
    // a Classic map keeps the anchor as a row byte and a column byte: rows past 127 and columns past 127 keep their eighth bit
    for (const version of [8, 9]) {
        const tall = File.parse(mapWith(version, 200, 200, [], [], []));
        putObject(tall, ROCK, 10, 5, [[0, 0]]); putObject(tall, MUSH, 20, 20, SQUARE4);
        const te = editor(tall);
        doIt(te, items(tall, 10, 5), 140, 150, 'move');
        equal(wordsOf(te.file, 150 * 200 + 155), [ROCK, 1, version === 8 ? (150 | (155 << 8)) : 0], 'a rock moved to row 150, column 155 holds its own anchor' + (version === 8 ? ' as two bytes' : ' as an offset of nothing'));
        doIt(te, items(te.file, 20, 20), 130, 140, 'move');
        equal(wordsOf(te.file, 150 * 200 + 160), [MUSH, 1, version === 8 ? (150 | (160 << 8)) : 0], 'the mushroom moved past row 127 is anchored at its origin');
        check(Edit.anchorIndex(te.file, 149 * 200 + 160 - 1) === 150 * 200 + 160, 'a cell of it names the origin: ' + Edit.anchorIndex(te.file, 149 * 200 + 159));

        // the tie of a cell to its anchor is carried exactly (a cell of an old map that names another place): a tie to a place that stays is kept, a tie to a cell that moves along moves along
        {
        let f, ed, res;
        const tag = ' (version ' + version + ')';
        const off = (r, c, tr, tc) => (version === 8 ? ((tr & 255) | ((tc & 255) << 8)) : (((tr - r) & 255) | (((tc - c) & 255) << 8)));          // (the word of the cell (r, c) that names the place (tr, tc))
        f = File.parse(mapWith(version, 30, 30, [], [], []));
        put(f, 2, 5, 5, 62, 1, off(5, 5, 0, 0)); put(f, 2, 12, 5, 63, 1, off(12, 5, 200, 7)); put(f, 2, 20, 20, 64, 1, off(20, 20, 20, 20));
        ed = editor(f);
        res = doIt(ed, items(ed.file, 5, 5), 4, 3, 'move');
        equal(wordsOf(ed.file, 9 * 30 + 8), [62, 1, off(9, 8, 0, 0)], 'a cell that names a place that stays (the corner) still names it after a move' + tag);
        res = doIt(ed, items(ed.file, 12, 5), 2, 2, 'copy');
        equal(wordsOf(ed.file, 14 * 30 + 7), [63, 1, off(14, 7, 200, 7)], 'a cell that names a place off the map still names it in a copy' + tag);
        equal(wordsOf(ed.file, 12 * 30 + 5), [63, 1, off(12, 5, 200, 7)], 'and the first is as it was' + tag);
        res = doIt(ed, items(ed.file, 20, 20), -3, 1, 'move');
        equal(wordsOf(ed.file, 17 * 30 + 21), [64, 1, off(17, 21, 17, 21)], 'a cell that is its own anchor is its own anchor after a move' + tag);
        // it cannot go where its tie would become a tie to itself, and the sentence says so in plain words; nothing is changed
        let verdict = Pick.check(ed.file, items(ed.file, 9, 8), -9, -8, 'move', 'one');
        check(!verdict.ok && /Part of it is tied to another place on the map/.test(verdict.text) && verdict.cells.length === 1 && verdict.cells[0] === 0, 'a cell cannot go on the place that it names: ' + verdict.text);
        verdict = Pick.check(ed.file, items(ed.file, 9, 8), -9, -8, 'copy', 'one');
        check(!verdict.ok && /Part of it is tied to another place on the map/.test(verdict.text), 'nor can its copy: ' + verdict.text);
        check(Pick.check(ed.file, items(ed.file, 9, 8), -9, -7, 'move', 'one').ok, 'one cell to the side it can');
        let thrown = ''; try { Pick.build(ed.file, items(ed.file, 9, 8), -9, -8, 'move'); } catch (e) { thrown = e.message; }
        check(/tie/.test(thrown), 'and build() refuses it too, so that nothing wrong is ever written: ' + thrown);
        // a wall of three cells (every cell names the origin) moves as a whole: each cell names the new origin, in version 8 as an absolute place
        {
            const wf = File.parse(mapWith(version, 30, 30, [], [], []));
            putObject(wf, WALL, 5, 5, [[0, 0], [0, 1], [0, 2]]);
            const we = editor(wf);
            doIt(we, items(we.file, 5, 6), 4, 3, 'move');
            for (const c of [8, 9, 10]) equal(wordsOf(we.file, 9 * 30 + c), [WALL, c === 8 ? 1 : 0, off(9, c, 9, 8)], 'a cell of a moved wall names the new origin of the wall, column ' + c + tag);
        }
        // a tie to a place that stays must fit the offset of a big map (a version 9 file over 256 rows or columns keeps it as a signed byte each): too far and the move is refused
        if (version === 9) {
            const bf = File.parse(mapWith(9, 300, 300, [], [], []));
            put(bf, 2, 5, 150, 62, 1, off(5, 150, 5, 40));
            const far = Pick.check(bf, items(bf, 5, 150), 0, 100, 'move', 'one'), near = Pick.check(bf, items(bf, 5, 150), 0, 10, 'move', 'one');
            check(!far.ok && /Part of it is tied to another place on the map/.test(far.text), 'on a big map a tie that no offset could hold is refused: ' + far.text);
            check(near.ok, 'and one that fits is not');
            let thrownFar = ''; try { Pick.build(bf, items(bf, 5, 150), 0, 100, 'move'); } catch (e) { thrownFar = e.message; }
            check(/tie/.test(thrownFar), 'and build() will not write it');
        }
        // two cells, one of them naming the other (not an object of the format): picked together by an area, the tie moves along; picked alone, it stays
        f = File.parse(mapWith(version, 30, 30, [], [], []));
        put(f, 2, 10, 10, 62, 1, off(10, 10, 10, 10)); put(f, 2, 10, 12, 63, 0, off(10, 12, 10, 10));
        ed = editor(f);
        const both = Pick.areaItems(f, { r0: 9, c0: 9, r1: 11, c1: 13 });
        check(both.length === 2, 'an area holds both cells (' + both.length + ')');
        res = doIt(ed, both, 5, 4, 'move', 'area');
        equal(wordsOf(ed.file, 15 * 30 + 16), [63, 0, off(15, 16, 15, 14)], 'the cell that names the other one names it at its new place' + tag);
        res = doIt(ed, items(ed.file, 15, 16), 1, 1, 'move');
        equal(wordsOf(ed.file, 16 * 30 + 17), [63, 0, off(16, 17, 15, 14)], 'moved alone it keeps naming the place that stays' + tag);
        res = doIt(ed, Pick.areaItems(ed.file, { r0: 14, c0: 13, r1: 17, c1: 18 }), 0, -2, 'copy', 'area');
        equal(wordsOf(ed.file, 16 * 30 + 15), [63, 0, off(16, 15, 15, 12)], 'the copy of the two cells names the copy of the other one' + tag);
        }
    }
    // delete a hill
    const f = File.parse(mapWith(9, 30, 30, [[155, 20, 20]], [], []));
    putHill(f, 3, 3, RED_HILL);
    const ed = editor(f), before = Uint8Array.from(f.bytes);
    check(Blocks.facts(f, true).hills[1] === 16, 'the red hill has 16 cells');
    const res = doIt(ed, items(f, 4, 4), 0, 0, 'delete');
    check(Blocks.facts(ed.file, true).hills[1] === 0 && ed.history.size().strokes === 1, 'a hill is deleted whole in one step');
    ed.undo(); check(same(ed.file.bytes, before), 'and Undo brings it back');
}

// ================= E =================
function caseE() {
    // two ants stacked on a cell move together, and are found again as two
    const stack = File.parse(mapWith(9, 30, 30, [[153, 5, 5], [153, 5, 5], [155, 7, 7]], [], []));
    const se = editor(stack), two = Pick.areaItems(stack, { r0: 4, c0: 4, r1: 6, c1: 6 });
    check(two.length === 2, 'two stacked ants are two things of an area');
    const sr = doIt(se, two, 10, 10, 'move', 'area'), found = Pick.resolve(se.file, sr.change.landed);
    check(found.length === 2 && found[0].i !== found[1].i && lay(se.file).records.filter((r) => r.row === 15).length === 2, 'both stacked ants moved, and they are found again as two different ones (' + found.map((x) => x.i) + ')');

    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')';
        const f = File.parse(mapWith(version, 60, 60, [[155, 12, 30], [155, 12, 31], [155 | (version === 9 ? 5 << 12 : 0), 14, 30], [153, 5, 5]], [{ row: 20, column: 30, units: 30, points: 25, stages: [[30, 264], [0, EMPTY2]] }], []));
        putObject(f, GRASS, 14, 25, [[-1, -1], [-1, 0], [0, -1], [0, 0]]);
        putObject(f, ROCK, 16, 33, [[0, 0]]);
        putHill(f, 40, 40, GREEN_HILL);
        const ed = editor(f), before = Uint8Array.from(f.bytes), box = { r0: 10, c0: 24, r1: 22, c1: 35 };
        const inside = Pick.areaItems(f, box);
        check(inside.length === 6, 'the area holds 6 things' + tag);
        let res = doIt(ed, inside, 10, 12, 'move', 'area');
        check(!res.error && ed.history.size().strokes === 1, 'the move of a whole area is ONE step' + tag);
        const g = ed.file, l = lay(g), moved = Uint8Array.from(g.bytes);
        equal(l.records.map((r) => [r.row, r.column]), [[22, 42], [22, 43], [24, 42], [5, 5]], 'its ants moved together, the far one did not' + tag);
        check(l.records[2].tile === (155 | (version === 9 ? 5 << 12 : 0)), 'with their types' + tag);
        check(l.piles[0].row === 30 && l.piles[0].column === 42, 'the pile moved' + tag);
        check(Edit.tile(g, 2, 24 * 60 + 37) === GRASS && Edit.tile(g, 2, 26 * 60 + 45) === ROCK && Edit.tile(g, 2, 14 * 60 + 25) === EMPTY2, 'the grass and the rock moved, the old cells are empty' + tag);
        const resolved = Pick.resolve(g, res.change.landed);
        check(resolved.length === 6, 'the six things are found again where they are now' + tag);
        ed.undo(); check(same(ed.file.bytes, before), 'one Undo puts the whole area back, byte for byte' + tag);
        ed.redo(); check(same(ed.file.bytes, moved), 'Redo moves it again' + tag);
        ed.undo();
        // an area onto its own old cells, one step to the side
        res = doIt(ed, Pick.areaItems(ed.file, box), 0, 1, 'move', 'area');
        check(!res.error, 'an area may move one cell over its own old cells' + tag);
        ed.undo(); check(same(ed.file.bytes, before), 'Undo' + tag);
        // an area onto something else is refused as a whole
        const bad = Pick.check(ed.file, Pick.areaItems(ed.file, box), 26, 12, 'move', 'area');
        check(!bad.ok && /^These 6 items cannot go here\./.test(bad.text) && /the green hill is in the way/i.test(bad.text), 'an area that would land on the green hill is refused as a whole, naming the hill: ' + bad.text + tag);
        // copy of the area, then Undo
        res = doIt(ed, Pick.areaItems(ed.file, box), 0, 8, 'copy', 'area'); const l2 = lay(ed.file);
        check(!res.error && l2.records.length === 7 && l2.piles.length === 2 && ed.history.size().strokes === 1, 'a copy of the area adds 3 ants and a pile in one step' + tag);
        check(Edit.tile(ed.file, 2, 14 * 60 + 25) === GRASS && Edit.tile(ed.file, 2, 14 * 60 + 33) === GRASS, 'and the grass stands twice' + tag);
        ed.undo(); check(same(ed.file.bytes, before), 'copy then Undo is the first map' + tag);
        // delete the area
        res = doIt(ed, Pick.areaItems(ed.file, box), 0, 0, 'delete', 'area');
        check(lay(ed.file).records.length === 1 && lay(ed.file).piles.length === 0 && Edit.tile(ed.file, 2, 14 * 60 + 25) === EMPTY2 && Edit.tile(ed.file, 2, 16 * 60 + 33) === EMPTY2, 'Delete takes the 6 away in one step' + tag);
        check((Edit.flags(ed.file, 1, 16 * 60 + 33) & 1) === 0, 'and the rock\'s solid bit' + tag);
        ed.undo(); check(same(ed.file.bytes, before), 'Undo brings all six back' + tag);
    }
}

// ================= F =================
function caseF() {
    // the limit of block 1: 65,535 records, ants and plants together
    const many = [];
    for (let i = 0; i < 65535; i++) many.push([153, (i / 250) | 0, i % 250]);
    const f = File.parse(mapWith(9, 270, 250, many, [], []));
    check(lay(f).records.length === 65535 && lay(f).ok1, 'a file with 65,535 records reads');
    const one = items(f, 0, 0)[0], v = Pick.check(f, [one], 3, 0, 'copy', 'one');
    check(!v.ok && /at most 65,535 starting ants and plants together/.test(v.text), 'a copy would be one more: refused with the number: ' + v.text);
    const mv = Pick.check(f, [items(f, 0, 0)[0]], 0, 0, 'move', 'one');
    check(mv.ok, 'a move needs no room');
    // the limit of block 2: 65,535 piles
    const heaps = [];
    for (let i = 0; i < 65535; i++) heaps.push({ row: (i / 250) | 0, column: i % 250, units: 10, points: 30, stages: [[10, 280], [0, EMPTY2]] });
    const hf = File.parse(mapWith(9, 270, 250, [], heaps, []));
    const pv = Pick.check(hf, [items(hf, 0, 0)[0]], 269, 0, 'copy', 'one');
    check(lay(hf).piles.length === 65535 && !pv.ok && /at most 65,535 food piles/.test(pv.text), 'a copy of a pile in a file that has 65,535 is refused with the number: ' + pv.text);
    check(Pick.check(hf, [items(hf, 0, 0)[0]], 0, 0, 'move', 'one').ok, 'and a move of it is not');
    // ants on the move together: 5,000 of them
    const crowd = [];
    for (let i = 0; i < 5000; i++) crowd.push([i % 2 ? 153 : 155, 5 + ((i / 100) | 0), 5 + (i % 100)]);
    const g = File.parse(mapWith(9, 100, 200, crowd, [], []));
    let t0 = Clock.now();
    const all = Pick.areaItems(g, { r0: 0, c0: 0, r1: 99, c1: 199 });
    const checked = Pick.check(g, all, 40, 80, 'move', 'area');
    check(all.length === 5000 && checked.ok, '5,000 ants are an area and can move 40 down and 80 right');
    const ed = editor(g), before = Uint8Array.from(g.bytes);
    const change = Pick.build(g, all, 40, 80, 'move');
    check(!!ed.act(change) && ed.history.size().strokes === 1, 'in one step');
    check(Clock.now() - t0 < 2000, 'and fast (' + (Clock.now() - t0) + ' ms)');
    equal(lay(ed.file).records.slice(0, 2).map((r) => [r.tile, r.row, r.column]), [[155, 45, 85], [153, 45, 86]], 'each ant keeps its team');
    ed.undo(); check(same(ed.file.bytes, before), 'Undo gives all 5,000 back');
    // a group of ants that touch is found fast too
    check(Pick.antGroup(g, items(g, 5, 5)[0]).length === 50, 'the group of an ant in a column of a team is the column (the other team stands between the columns)');
    const clump = [];
    for (let i = 0; i < 4000; i++) clump.push([154, 5 + ((i / 80) | 0), 5 + (i % 80)]);
    const h2 = File.parse(mapWith(9, 100, 200, clump, [], []));
    t0 = Clock.now();
    check(Pick.antGroup(h2, items(h2, 30, 40)[0]).length === 4000 && Clock.now() - t0 < 1500, 'the group of a clump of 4,000 touching ants is all of them, found fast (' + (Clock.now() - t0) + ' ms)');
    // a map whose lists do not read: nothing is picked from them, nothing breaks
    const bytes = mapWith(8, 10, 10, [[153, 2, 2]], [], []), cut = bytes.slice(0, File.parse(bytes).blocksAt + 3);
    const h = File.parse(cut);
    check(h.ok && items(h, 2, 2).length === 0 && Pick.areaItems(h, { r0: 0, c0: 0, r1: 9, c1: 9 }).length === 0, 'a file whose list is cut short has nothing to pick from it');
    // a file whose block 4 (the plants' droppers) is cut short: its ants can be picked, its plants are left alone
    const half = mapWith(9, 10, 10, [[153, 2, 2], [404, 4, 4]], [], [{ row: 4, column: 4, flag: 1, interval: 45 }]), hf2 = File.parse(half.slice(0, File.parse(half).blocksAt + 2 + 12 + 2 + 4 + 2 + 20));
    check(hf2.ok && !lay(hf2).ok4 && items(hf2, 2, 2).length === 1 && items(hf2, 4, 4).length === 0 && Pick.areaItems(hf2, { r0: 0, c0: 0, r1: 9, c1: 9 }).length === 1, 'a file whose droppers do not read: the ant can be picked, the plant is left alone (moving it would leave its dropper behind)');
}

// ================= G =================
// a map for the page: a red hill, a blue ant group of six touching ants, a lone red ant, a mushroom, a rock, a pile, a plant, and water
function scene(version) {
    const ants = [[153, 20, 10], [153, 20, 11], [153, 21, 10], [153, 21, 12], [153, 22, 11], [153, 22, 12], [155, 30, 30]];
    const f = File.parse(mapWith(version, 40, 40, ants, [{ row: 25, column: 5, units: 30, points: 25, stages: [[30, 264], [0, EMPTY2]] }], [{ row: 35, column: 30, flag: 0 }].concat([])));
    const lr = lay(f);
    f.bytes; putHill(f, 2, 2, RED_HILL); putObject(f, MUSH, 10, 25, SQUARE4); putObject(f, ROCK, 36, 4, [[0, 0]]);
    for (let c = 0; c < 6; c++) put(f, 1, 38, 10 + c, 476, 0, 0xCDCD);
    return File.parse(f.bytes);
}
function sceneBytes(version) {
    const f = File.parse(mapWith(version, 40, 40, [[153, 20, 10], [153, 20, 11], [153, 21, 10], [153, 21, 12], [153, 22, 11], [153, 22, 12], [155, 30, 30], [404, 35, 30]], [{ row: 25, column: 5, units: 30, points: 25, stages: [[30, 264], [0, EMPTY2]] }], [{ row: 35, column: 30, flag: 0 }]));
    putHill(f, 2, 2, RED_HILL); putObject(f, MUSH, 10, 25, SQUARE4); putObject(f, ROCK, 36, 4, [[0, 0]]);
    for (let c = 0; c < 6; c++) put(f, 1, 38, 10 + c, 476, 0, 0xCDCD);
    return f.bytes;
}
const labels = (pg) => { const o = []; const l = pg.bar.querySelectorAll('.pk-b'); for (let i = 0; i < l.length; i++) if (l[i].getAttribute('data-k') !== 'fold') o.push(l[i].textContent); return o; };          // (the Hide button of a short screen is not one of the tool's)

function caseG() {
    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')';
        const pg = page(File.parse(sceneBytes(version)));
        check(pg.hidden() && pg.p.size() === 0, 'with nothing picked the bar is away and the tool paints nothing' + tag);
        check(pg.bar.getAttribute('role') === 'region' && pg.bar.getAttribute('aria-label') === 'Pick up', 'the bar is a labelled region' + tag);
        // a tap on the hill
        check(pg.p.canMove({ row: 3, column: 3 }) && !pg.p.tapOnly({ row: 3, column: 3 }), 'a press on a hill is a drag of the tool (a thing to pick up)' + tag);
        check(!pg.p.canMove({ row: 0, column: 0 }) && pg.p.tapOnly({ row: 0, column: 0 }), 'a press on empty ground looks around, and a tap there is the tool\'s' + tag);
        pg.tap(4, 4);
        check(!pg.hidden() && pg.state().mode === 'picked', 'a tap picks the hill, the bar comes up' + tag);
        equal(labels(pg), ['Move', 'Copy here', 'Delete', 'Select an area...', 'Cancel'], 'its five buttons, as pictured' + tag);
        equal(pg.bar.querySelector('.pk-name').textContent + pg.bar.querySelector('.pk-rest').textContent, 'Red hill \u00b7 4 x 4 cells, with its door', 'the line says what it is' + tag);
        check(pg.off('copy') && /A team has one hill/.test(pg.text()), 'Copy here is off for a hill, and says why' + tag);
        // Move
        pg.press('move');
        check(pg.state().mode === 'moving' && pg.log.focus >= 1, 'Move starts the ghost and gives the focus back to the map (the keys work)' + tag);
        equal(labels(pg), ['Done', 'Nudge arrows', 'Cancel'], 'Done, Nudge arrows, Cancel' + tag);
        const line = () => pg.bar.querySelector('.pk-line').textContent;
        check(pg.off('done') && /^Moving the red hill\. Tap the new place, or drag it\. It lands on row 2, column 2\.$/.test(line()), 'Done is off at the old place, the line says where it lands: ' + line() + tag);
        pg.tap(30, 20);
        check(pg.state().dr === 27 && pg.state().dc === 17 && /It lands on row 29, column 19\./.test(line()) && !pg.off('done'), 'a tap far away puts the middle of the hill there (the top left lands on row 29, column 19), Done on: ' + line() + tag);
        pg.tap(10, 26);
        check(pg.off('done') && /cannot go here/.test(pg.warn()) && /the mushroom is in the way/.test(pg.warn()) && !/It lands/.test(pg.text()), 'a tap on the mushroom turns it red, says why, and turns Done off: ' + pg.warn() + tag);
        check(pg.log.refreshes === 0 && !pg.map.ctx, 'the tool asked for no repaint of the map (the ghost lives on the canvas over it)' + tag);
        pg.tap(30, 20);
        const before = Uint8Array.from(pg.file().bytes);
        pg.press('done');
        check(pg.log.acts === 1 && pg.ed().history.size().strokes === 1 && Edit.tile(pg.file(), 2, 29 * 40 + 19) === RED_HILL && pg.state().mode === 'picked', 'Done is ONE step, and the hill is picked still, at its new place' + tag);
        check(/Moved it: 17 columns right, 27 rows down\. One Undo takes it back\./.test(pg.text()), 'the bar says how far and that one Undo takes it back: ' + pg.text() + tag);
        pg.undo(); check(same(pg.file().bytes, before) && pg.hidden(), 'Undo puts it back, and the pick is let go' + tag);
        pg.redo();
        // Cancel: moving goes back to picked, picked to nothing
        pg.tap(30, 20); pg.press('move'); pg.tap(10, 5); pg.press('cancel');
        check(pg.state().mode === 'picked' && pg.state().dr === 0, 'Cancel while moving goes back to the pick' + tag);
        pg.press('cancel');
        check(pg.state().mode === 'idle' && pg.hidden(), 'Cancel again lets go' + tag);
        // the ghost follows the mouse while a thing is on its way (a desktop), shows the verdict as it goes, and is where it was put when the mouse leaves the map or a key is pressed
        const hv = page(File.parse(sceneBytes(version)));
        hv.tap(3, 3); hv.p.hover({ row: 30, column: 20 });
        check(hv.state().dr === 0 && !hv.state().hover, 'with the hill only picked the mouse changes nothing' + tag);
        hv.press('move');
        hv.p.hover({ row: 30, column: 20 });
        check(hv.state().hover && hv.state().dr === 27 && hv.state().dc === 17 && !hv.off('done') && /It lands on row 29, column 19\./.test(hv.text()), 'the ghost is under the mouse (the middle of the hill on the cell), green, with where it lands: ' + hv.text() + tag);
        hv.p.hover({ row: 10, column: 26 });
        check(hv.state().hover && hv.off('done') && /the mushroom is in the way/.test(hv.warn()), 'over the mushroom it is red, with the reason, and Done is off: ' + hv.warn() + tag);
        hv.p.hover(null);
        check(!hv.state().hover && hv.state().dr === 0 && hv.state().dc === 0 && hv.off('done') && /Tap the new place/.test(hv.text()), 'the mouse leaving the map puts the ghost back where it was put (nowhere yet)' + tag);
        hv.p.hover({ row: 30, column: 20 }); hv.tap(30, 20);
        check(!hv.state().hover && hv.state().dr === 27 && hv.state().dc === 17, 'a click puts it where the mouse is' + tag);
        hv.p.hover({ row: 12, column: 12 });
        check(hv.state().hover && hv.state().dr === 9, 'the mouse moves it again' + tag);
        hv.key('ArrowRight');
        check(!hv.state().hover && hv.state().dr === 27 && hv.state().dc === 18, 'a key acts on where it was put, not on where the mouse is' + tag);
        hv.p.hover({ row: 12, column: 12 }); hv.press('done');
        check(hv.log.acts === 1 && Edit.tile(hv.file(), 2, 29 * 40 + 20) === RED_HILL, 'Done puts it where it was put, not where the mouse is' + tag);
        hv.p.hover({ row: 1, column: 1 });
        check(!hv.state().hover && hv.state().mode === 'picked', 'the mouse changes nothing once it is down' + tag);
        // a click away from the ghost that follows the mouse puts the thing there, and the preview is gone (a key then acts on that place)
        const hw = page(File.parse(sceneBytes(version)));
        hw.tap(3, 3); hw.press('move'); hw.p.hover({ row: 30, column: 20 }); hw.tap(20, 10);
        check(!hw.state().hover && hw.state().dr === 17 && hw.state().dc === 7, 'a click far from the preview puts the thing where it was clicked: ' + hw.state().dr + ',' + hw.state().dc + tag);
        hw.key('ArrowRight');
        check(hw.state().dr === 17 && hw.state().dc === 8, 'and a key goes on from there, not from where the preview was' + tag);
        // on a short screen the bar can be folded to one small button
        hv.press('fold');
        check(hv.state().fold && /folded/.test(hv.bar.className) && hv.has('fold'), 'the bar folds' + tag);
        hv.press('fold');
        check(!hv.state().fold && !/folded/.test(hv.bar.className), 'and opens again' + tag);
        // the nudge arrows
        const g2 = page(File.parse(sceneBytes(version)));
        g2.tap(3, 3); g2.press('move'); g2.press('nudge');
        const arrows = g2.bar.querySelectorAll('.pk-arrow');
        check(arrows.length === 4 && g2.has('up') && g2.has('left') && g2.has('right') && g2.has('down') && g2.has('done') && g2.has('cancel') && !g2.has('nudge'), 'the arrow pad: four arrows, Done and Cancel' + tag);
        for (const k of ['up', 'left', 'right', 'down']) check(/Nudge (up|left|right|down) one cell/.test(g2.buttons()[k].getAttribute('aria-label')) && /pk-arrow/.test(g2.buttons()[k].className), 'the ' + k + ' arrow is a labelled button' + tag);
        g2.press('right'); g2.press('right'); g2.press('down');
        check(g2.state().dr === 1 && g2.state().dc === 2 && /Moved 2 right, 1 down\. Lands on row 3, column 4\./.test(g2.text()), 'arrows move one cell each: "Moved 2 right, 1 down. Lands on row 3, column 4.": ' + g2.text() + tag);
        g2.press('left'); g2.press('left'); g2.press('left');
        check(g2.state().dc === -1 && !g2.off('done'), 'three left: one cell left of where it stood is the edge of the map?' + tag);
        g2.press('left'); g2.press('left');
        check(g2.state().dc === -2 && /edge of the map/.test(g2.text()), 'at the edge of the map an arrow stops and says so: ' + g2.text() + tag);
        g2.press('up'); g2.press('up');
        check(g2.state().dr === -1 || g2.state().dr === -1, 'the top edge stops it too' + tag);
        // the ghost and the ring on the overlay canvas
        const g3 = page(File.parse(sceneBytes(version)), { sprites: () => ({ ghost: (ctx, v, w, h, id, r, c) => { g3.ghosts.push([id, r, c]); } }) });
        g3.ghosts = [];
        g3.tap(3, 3);
        const ctx = g3.over.ctx || g3.over.getContext('2d');
        check(ctx.log.texts.indexOf('door') >= 0 && ctx.log.strokes.indexOf('#f2c230') >= 0, 'the ring is gold, with the door marked' + tag);
        g3.press('move'); g3.tap(30, 20);
        check(g3.over.ctx.log.fills.indexOf('rgba(120,230,170,.30)') >= 0 && g3.over.ctx.log.texts.indexOf('from') >= 0, 'a free place: the ghost is green, and "from" marks the old place' + tag);
        check(g3.ghosts.length === 0 || g3.ghosts[0][0] === RED_HILL, 'the ghost\'s picture is the hill\'s' + tag);
        g3.tap(10, 26);
        check(g3.over.ctx.log.fills.indexOf('rgba(239,83,80,.34)') >= 0, 'in the way: the ghost is red' + tag);
        check(g3.log.refreshes === 0 && !g3.map.ctx, 'and the map is not painted for it' + tag);
        equal(g3.over.className, 'pk-over', 'the canvas over the map has its class' + tag);
        equal(g3.over.getAttribute('aria-hidden'), 'true', 'and is hidden from a screen reader' + tag);
    }
    // what can be picked up: the card of the picture, from the note under the tools
    const hp = page(File.parse(sceneBytes(9))), hint = hp.env.host.hint;
    const link = hint.querySelector('.pk-help');
    check(!!link && link.textContent === 'What can be picked up?', 'the note under the tools has a link: What can be picked up?');
    link.click();
    check(hp.log.dialogs.length === 1 && hp.log.dialogs[0].spec.title === 'What can be picked up', 'it opens a sheet');
    const cards = hp.log.dialogs[0].body.querySelectorAll('.pk-card'), said = [];
    for (let i = 0; i < cards.length; i++) said.push(cards.item(i).textContent);
    equal(said, ['Small objectsRocks, power-ups, bombs: one cell. Move: yes.', 'Bigger objectsThe whole object with all its cells, grass and toys too. Move: yes.',
        'Hills4 x 4 with its door; it keeps its team color and cannot land on another hill. Move: yes.', 'AntsOne ant, or its group (same team, next to each other). Keeps its team. Move: yes.',
        'Food pilesThe whole pile with its stages. Move: yes.', 'GroundNot moved. Paint it, or use Fill and Fix edges. Move: no.',
        'One Undo per moveMove, nudge steps, Copy here, Delete and Move them are each one Undo step. The picked item moves with everything it covers; ants keep their team; nothing changes the ground under it or how it walks.'], 'the seven cards say what the picture says');
    hp.setBusy(true); link.click(); hp.setBusy(false);
    check(hp.log.dialogs.length === 1, 'and not while another dialog is open');
    // an ant: this one or its group
    const pg = page(File.parse(sceneBytes(9)));
    pg.tap(21, 10);
    equal(labels(pg), ['Move this ant', 'Move its group (6)', 'Copy here', 'Delete', 'Cancel'], 'a Blue ant of a group: Move this ant, Move its group (6), Copy here, Delete, Cancel');
    check(/^Blue ant \u00b7 one of 6 \u00b7 its group: 6 Blue ants next to each other$/.test(pg.bar.querySelector('.pk-line').textContent), 'the line counts the team and the group: ' + pg.bar.querySelector('.pk-line').textContent);
    pg.press('group');
    check(pg.state().items.length === 6 && pg.state().kind === 'group' && /^Moving 6 Blue ants\./.test(pg.text()), 'Move its group takes all six: ' + pg.text());
    pg.tap(5, 20);
    const before = Uint8Array.from(pg.file().bytes);
    pg.press('done');
    const l = lay(pg.file()).records;
    check(pg.log.acts === 1 && l.slice(0, 6).every((r) => r.tile === 153 && r.row < 10) && l[6].row === 30 && pg.ed().history.size().strokes === 1, 'the group moved in one step, the Red ant stayed');
    pg.undo(); check(same(pg.file().bytes, before), 'Undo');
    // this ant alone
    const p2 = page(File.parse(sceneBytes(9)));
    p2.tap(21, 10); p2.press('move'); p2.tap(8, 8); p2.press('done');
    check(lay(p2.file()).records.filter((r) => r.row === 8).length === 1 && lay(p2.file()).records.filter((r) => r.row >= 20 && r.row <= 22).length === 5, 'Move this ant moves one ant');
    // Copy here and Delete ask "this ant or its group" the way Move does
    const q1 = page(File.parse(sceneBytes(9)));
    q1.tap(21, 10); q1.press('copy');
    equal(labels(q1), ['Copy this ant', 'Copy its group (6)', 'Back'], 'Copy here on an ant of a group asks: this ant or its group');
    check(q1.state().ask === 'copy' && /Copy only this ant, or its whole group of 6\?/.test(q1.text()), 'with the question in plain words: ' + q1.text());
    q1.press('cancel');
    check(q1.state().ask === '' && q1.state().mode === 'picked' && labels(q1)[2] === 'Copy here', 'Back goes back to the five buttons');
    q1.press('delete'); check(labels(q1).join() === 'Delete this ant,Delete its group (6),Back', 'Delete asks the same way');
    q1.doc.key('Escape', { target: q1.buttons()['ask-one'] });
    check(q1.state().ask === '' && q1.state().mode === 'picked', 'Escape on the question goes back one step, the ant is still picked');
    q1.press('copy'); q1.press('ask-group');
    check(q1.state().mode === 'moving' && q1.state().copy && q1.state().items.length === 6 && q1.state().kind === 'group', 'its group is copied as a group');
    q1.tap(5, 20); q1.press('done');
    check(lay(q1.file()).records.filter((r) => r.tile === 153).length === 12 && q1.ed().history.size().strokes === 1, 'six more Blue ants in one step');
    const q2 = page(File.parse(sceneBytes(9))), n2 = lay(q2.file()).records.length;
    q2.tap(21, 10); q2.press('copy'); q2.press('ask-one'); q2.tap(5, 20); q2.press('done');
    check(lay(q2.file()).records.length === n2 + 1 && q2.ed().history.size().strokes === 1, 'this ant alone is copied alone');
    const q3 = page(File.parse(sceneBytes(9))), b3 = Uint8Array.from(q3.file().bytes);
    q3.tap(21, 10); q3.press('delete'); q3.press('ask-group');
    check(lay(q3.file()).records.length === n2 - 6 && q3.ed().history.size().strokes === 1 && q3.hidden() && /Took away 6 things\./.test(q3.state().note), 'Delete its group takes the six away in one step: ' + q3.state().note);
    q3.undo(); check(same(q3.file().bytes, b3), 'and one Undo brings them back');
    const q4 = page(File.parse(sceneBytes(9)));
    q4.tap(21, 10); q4.press('delete'); q4.press('ask-one');
    check(lay(q4.file()).records.length === n2 - 1, 'Delete this ant takes one');
    // a lone ant has no group question
    const p3 = page(File.parse(sceneBytes(9)));
    p3.tap(30, 30);
    equal(labels(p3), ['Move', 'Copy here', 'Delete', 'Select an area...', 'Cancel'], 'a lone ant gets the plain buttons');
    check(/no other ant of its team touches it/.test(p3.text()), 'and says nothing touches it');
    // copy
    const p4 = page(File.parse(sceneBytes(9)));
    p4.tap(36, 4); p4.press('copy');
    check(/^Copying the rock\. Tap where the copy goes/.test(p4.text()) && p4.off('done'), 'Copy here: the bar says "Copying the rock", Done is off while the copy would sit on the original: ' + p4.text());
    p4.tap(34, 6);
    check(!p4.off('done'), 'a free place turns Done on');
    p4.press('done');
    check(Edit.tile(p4.file(), 2, 36 * 40 + 4) === ROCK && Edit.tile(p4.file(), 2, 34 * 40 + 6) === ROCK && p4.ed().history.size().strokes === 1, 'a rock stands in both places after one step');
    check(/^Copied it\. One Undo takes it back\.$/.test(p4.bar.querySelector('.pk-said').textContent) && p4.state().items[0].fp[0] === 34 * 40 + 6, 'the copy is what is picked now: ' + p4.bar.querySelector('.pk-said').textContent);
    // delete
    const p5 = page(File.parse(sceneBytes(8)));
    p5.tap(3, 3); p5.press('delete');
    check(p5.hidden() && Blocks.facts(p5.file(), true).hills[1] === 0 && p5.ed().history.size().strokes === 1, 'Delete takes the hill away in one step and the bar goes');
    check(/Took away it\. One Undo brings it back\./.test(p5.pick.state().note), 'and says so');
    p5.undo(); check(Blocks.facts(p5.file(), true).hills[1] === 16, 'Undo brings it back');
    // select an area
    const p6 = page(File.parse(sceneBytes(9)));
    p6.tap(36, 4); p6.press('area');
    check(p6.state().mode === 'boxing' && /Select an area/.test(p6.text()) && /drag a box/.test(p6.text()) && labels(p6).join() === 'Cancel', 'Select an area...: a plain line and Cancel');
    check(p6.p.canMove({ row: 0, column: 0 }) && !p6.p.tapOnly({ row: 0, column: 0 }), 'while a box is drawn a press anywhere draws it');
    p6.drag(18, 8, 24, 14);
    equal(p6.state().items.length, 6, 'the box takes the 6 Blue ants in it (the lone Red ant and the rest are outside)');
    check(p6.state().mode === 'picked' && p6.state().kind === 'area', 'and they are picked as an area');
    equal(labels(p6), ['Move them', 'Copy here', 'Delete', 'Cancel'], 'Move them, Copy here, Delete, Cancel');
    check(/^Area: 6 items \u00b7 6 Blue ants$/.test(p6.bar.querySelector('.pk-line').textContent), 'the line counts them: ' + p6.bar.querySelector('.pk-line').textContent);
    p6.press('move'); p6.tap(5, 30);
    const b6 = Uint8Array.from(p6.file().bytes), d6 = p6.state();
    p6.press('done');
    equal(p6.state().box, { r0: 18 + d6.dr, c0: 8 + d6.dc, r1: 24 + d6.dr, c1: 14 + d6.dc }, 'the box of the area goes along with it, all four sides (rows and columns shifted differently)');
    check(d6.dr !== d6.dc, 'and the two shifts differ, so a mix-up of rows and columns shows');
    check(p6.ed().history.size().strokes === 1 && lay(p6.file()).records.filter((r) => r.row < 10).length === 6, 'the area moves in one step');
    p6.undo(); check(same(p6.file().bytes, b6), 'Undo gives the six back');
    const p7 = page(File.parse(sceneBytes(9)));
    p7.tap(36, 4); p7.press('area'); p7.drag(0, 0, 3, 1);
    check(p7.state().mode === 'boxing' && /Nothing is inside that box/.test(p7.text()), 'a box with nothing in it says so and stays in the box mode');
    p7.press('cancel'); check(p7.hidden(), 'Cancel leaves it');
    // tell the screen reader
    check(/Red hill/.test((() => { const q = page(File.parse(sceneBytes(9))); q.tap(3, 3); const n = q.env.host.cv.parentNode.querySelectorAll('.sr'); return n.length ? n.item(0).textContent : ''; })()), 'a screen reader is told what was picked (a status line of its own)');
}

// ================= H =================
function caseH() {
    const pg = page(File.parse(sceneBytes(9)));
    pg.setCursor({ row: 3, column: 3 });
    check(pg.key('ArrowRight') === false && pg.key('x') === false, 'with nothing picked the arrows and other keys are the map\'s own');
    check(pg.key('Enter') === true && pg.state().mode === 'picked', 'Enter picks what is on the marked cell');
    pg.setCursor({ row: 0, column: 0 });
    pg.key('Escape'); check(pg.state().mode === 'idle', 'Escape lets go');
    check(pg.key('Enter') === true && pg.state().mode === 'idle', 'Enter on an empty cell picks nothing (and the key is still the tool\'s, so it does not paint)');
    pg.setCursor({ row: 3, column: 3 }); pg.key('Enter');
    check(pg.key('ArrowRight') === true && pg.state().mode === 'moving' && pg.state().dc === 1 && pg.state().dr === 0, 'an arrow on a pick starts the move with one cell');
    pg.key('ArrowDown', { shiftKey: true });
    check(pg.state().dr === 8, 'Shift moves 8 cells');
    pg.key('ArrowUp', { shiftKey: true }); pg.key('ArrowLeft');
    check(pg.state().dr === 0 && pg.state().dc === 0 && pg.off('done'), 'back at the start Done is off');
    pg.key('ArrowDown'); pg.key('ArrowDown');
    const before = Uint8Array.from(pg.file().bytes);
    check(pg.key('Enter') === true && pg.log.acts === 1 && Edit.tile(pg.file(), 2, 5 * 40 + 2) === RED_HILL, 'Enter puts it down (Done)');
    check(pg.key('Escape') === true && pg.state().mode === 'idle', 'Escape lets go');
    pg.undo(); check(same(pg.file().bytes, before), 'Undo');
    pg.setCursor({ row: 3, column: 3 }); pg.key('Enter');
    pg.key('Enter'); check(pg.state().mode === 'moving' && pg.state().dr === 0, 'Enter on a pick starts the move');
    check(pg.log.focus === 1, 'the keys of the map are heard again after Enter started the move (the focus is given back)');
    const acts = pg.log.acts;
    pg.key('Enter'); check(pg.state().mode === 'picked' && pg.log.acts === acts, 'Enter where the thing already is puts nothing down (no step, back to the pick)');
    pg.key('Enter');
    pg.key('Escape'); check(pg.state().mode === 'picked', 'Escape in a move goes back to the pick');
    pg.setBusy(true);
    check(pg.key('Delete') === false && pg.state().mode === 'picked', 'with a dialog open the keys are not heard');
    pg.setBusy(false);
    check(pg.key('Delete') === true && pg.state().mode === 'idle' && Blocks.facts(pg.file(), true).hills[1] === 0, 'Delete takes the pick away');
    pg.undo();
    pg.setCursor({ row: 3, column: 3 }); pg.key('Enter');
    check(pg.key('Backspace') === true && Blocks.facts(pg.file(), true).hills[1] === 0, 'Backspace does too');
    pg.undo();
    // a box drawn from the bottom right to the top left is the same box
    const bx = page(File.parse(sceneBytes(9)));
    bx.tap(36, 4); bx.press('area'); bx.drag(24, 14, 18, 8);
    check(bx.state().items.length === 6 && bx.state().box.r0 === 18 && bx.state().box.c0 === 8 && bx.state().box.r1 === 24 && bx.state().box.c1 === 14, 'a box drawn upwards and to the left is the same box');
    // Escape heard on the document when the focus is on a button of the bar
    pg.setCursor({ row: 3, column: 3 }); pg.key('Enter');
    pg.doc.activeElement = pg.buttons().move;
    const focused = pg.log.focus, e = pg.doc.key('Escape', { target: pg.buttons().move });
    check(pg.state().mode === 'idle' && e.prevented, 'Escape on a button of the bar lets go');
    check(pg.log.focus === focused + 1, 'and the map gets the keyboard focus back (the button that had it is gone)');
    pg.setCursor({ row: 3, column: 3 }); pg.key('Enter');
    const inp = pg.doc.createElement('input'); pg.doc.key('Escape', { target: inp, tagName: 'input' });
    check(pg.state().mode === 'picked', 'Escape typed in a box is not the tool\'s');
    pg.doc.key('Escape', { target: pg.buttons().move, defaultPrevented: true });
    check(pg.state().mode === 'picked', 'nor one that something else has taken');
    pg.setBusy(true); pg.doc.key('Escape', { target: pg.buttons().move }); pg.setBusy(false);
    check(pg.state().mode === 'picked', 'nor while a dialog is open');
    // the arrows of the keyboard at the edge of the map
    const g = page(File.parse(sceneBytes(8)));
    g.setCursor({ row: 3, column: 3 }); g.key('Enter');
    g.key('ArrowLeft'); g.key('ArrowLeft');
    check(g.state().dc === -2 && g.state().mode === 'moving', 'two cells left is the edge of the map (the hill starts at column 2)');
    g.key('ArrowLeft');
    check(g.state().dc === -2 && /It is at the edge of the map\./.test(g.text()), 'one more is refused in plain words, and nothing moved: ' + g.text());
    g.key('ArrowUp'); g.key('ArrowUp'); g.key('ArrowUp');
    check(g.state().dr === -2 && /edge/.test(g.text()), 'the top edge too');
    // the pointer: a press that is a drag, ghost drags, a tap that picks the next thing on the cell
    const q = page(File.parse(sceneBytes(9)));
    check(q.drag(3, 3, 12, 20) === true && q.state().mode === 'picked' && q.log.acts === 1 && Edit.tile(q.file(), 2, 11 * 40 + 19) === RED_HILL, 'a press on a thing and a drag puts it down where the mouse is let go (the old Move tool dragged too): one change');
    check(q.state().items[0].hill === 1 && q.state().items[0].fp[0] === 11 * 40 + 19, 'the hill is picked still, at its new place');
    q.press('move');
    q.tap(25, 30);
    check(q.p.canMove({ row: 25, column: 30 }) && !q.p.canMove({ row: 12, column: 20 }) && q.p.tapOnly({ row: 12, column: 20 }), 'a press on the ghost drags it; a press where the hill was is a tap that moves the ghost');
    q.p.begin({ row: 25, column: 30 }); q.p.extend({ row: 25, column: 30 }, { row: 14, column: 22 }); q.p.cancel();
    check(q.state().mode === 'picked' && q.log.acts === 1, 'a drag of the ghost that is cancelled (a second finger, the window) takes it back to the pick, and nothing is written');
    q.press('cancel');
    // the thing under the finger is picked and moved even when something else was picked
    const w = page(File.parse(sceneBytes(9)));
    w.tap(3, 3); w.drag(36, 4, 34, 6);
    check(w.state().items.length === 1 && w.state().items[0].k === 'obj' && w.state().items[0].hill === -1 && w.state().mode === 'picked', 'a drag that starts on another thing picks that one and puts it down');
    check(Edit.tile(w.file(), 2, 34 * 40 + 6) === ROCK && Edit.tile(w.file(), 2, 36 * 40 + 4) === EMPTY2, 'at the place where the mouse was let go');
    // a tap on empty ground lets go
    w.tap(0, 39); check(w.state().mode === 'idle', 'a tap on empty ground lets go');
    // a stack: each tap on the cell picks the next thing, and round again
    const s = page(File.parse(mapWith(9, 20, 20, [[153, 4, 4], [405, 4, 4]], [{ row: 4, column: 4, units: 10, points: 30, stages: [[10, 280], [0, EMPTY2]] }], [{ row: 4, column: 4, flag: 0 }])));
    put(s.file(), 2, 4, 4, ROCK, 1, 0);
    const seq = [];
    for (let i = 0; i < 5; i++) { s.tap(4, 4); seq.push(s.state().items[0].k); }
    equal(seq, ['ant', 'plant', 'pile', 'obj', 'ant'], 'tapping the same cell again picks the next thing on it, and goes round');
    check(/Tap again to pick the next thing/.test(s.env.host.cv.parentNode.querySelector('.sr').textContent), 'told');
    s.tap(4, 5); s.tap(4, 4);
    equal(s.state().items[0].k, 'ant', 'a tap elsewhere starts the round again');
}

// ================= I =================
function caseI() {
    // a map that changes under a pick
    const pg = page(File.parse(sceneBytes(9)));
    pg.tap(3, 3);
    pg.ed().act({ names: [], cells: [[40 * 20 + 20, ROCK, 1, 0]], splices: [] }); pg.pick.refresh();
    check(pg.state().mode === 'picked', 'a change somewhere else leaves the pick alone');
    pg.ed().act({ names: [], cells: [[3 * 40 + 3, EMPTY2, 0, 0]], splices: [] }); pg.pick.refresh();
    check(pg.state().mode === 'idle', 'a change under the pick lets it go');
    pg.tap(21, 10);
    pg.ed().act(Blocks.eraseAt(pg.file(), 21 * 40 + 10)); pg.pick.refresh();
    check(pg.state().mode === 'idle', 'the eraser taking the ants away lets the pick of an ant go');
    const pj = page(File.parse(sceneBytes(9)));
    pj.ed().act({ names: [], cells: [[39 * 40 + 39, ROCK, 1, 0]], splices: [] });
    pj.tap(3, 3); pj.press('move'); pj.tap(20, 20);
    pj.undo();
    check(pj.state().mode === 'moving' && !pj.hidden(), 'an Undo of something else leaves the ghost where it is');
    pj.press('done'); pj.undo();
    check(pj.state().mode === 'idle' && pj.hidden(), 'an Undo of the move itself lets the thing go');
    pg.tap(3, 3); pg.reload(sceneBytes(8));
    check(pg.state().mode === 'idle' && pg.hidden(), 'another map lets everything go');
    // hiding the tool
    pg.tap(3, 3); pg.pick.show(false);
    check(pg.state().mode === 'idle' && pg.hidden() && !pg.p.canMove({ row: 3, column: 3 }) && pg.p.begin({ row: 3, column: 3 }) === false && pg.p.key({ key: 'Enter' }) === false, 'another tool lets go, and the painter of Pick up does nothing then');
    pg.pick.show(true);
    check(pg.p.size() === 0, 'the painter paints nothing');

    // through the page's own Tools
    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')';
        const t = toolsPage(File.parse(sceneBytes(version)));
        check(t.loads === 0, 'the part is not asked for before the tool is chosen' + tag);
        t.button('pickup').click();
        check(t.loads === 1 && t.tools.state().tool === 'pickup' && t.painter().size() === 0, 'choosing Pick up asks for its part (once)' + tag);
        t.button('pan').click(); t.button('pickup').click();
        check(t.loads === 1, 'and not again' + tag);
        const p = t.painter();
        check(p.canMove({ row: 3, column: 3 }) === true && p.canMove({ row: 0, column: 0 }) === false && p.tapOnly({ row: 0, column: 0 }) === true && p.tapOnly({ row: 3, column: 3 }) === false, 'the tool\'s painter answers for the cell (a thing is a drag, empty ground a tap)' + tag);
        check(p.begin({ row: 3, column: 3 }) === true, 'a press on a hill starts' + tag);
        p.end();
        check(t.pickPage().state().mode === 'picked' && t.pickBar().hidden === false, 'a tap picks it' + tag);
        check(t.log.settled.length >= 1 && typeof t.log.settled[t.log.settled.length - 1] === 'function', 'the bar tells the page\'s canvas that its size may have changed (and how to bring the pick into view)' + tag);
        check(t.doc.key('ArrowRight', { target: t.canvasNode }) !== null && p.key({ key: 'ArrowRight' }) === true && t.pickPage().state().mode === 'moving', 'a key of the canvas goes to the tool first' + tag);
        p.key({ key: 'Enter' });
        check(t.log.edits === 1 && t.tools.state().history.strokes === 1, 'Done is one step of the page\'s history, and the page is told' + tag);
        check(t.log.dirty.length >= 1 && t.log.minis >= 1, 'the canvas is told which cells changed, and the small map is made again' + tag);
        t.doc.key('z', { ctrlKey: true });
        check(t.pickPage().state().mode === 'idle' && t.tools.state().history.strokes === 0, 'Ctrl+Z undoes it and lets the pick go' + tag);
        p.begin({ row: 3, column: 3 }); p.extend({ row: 3, column: 3 }, { row: 12, column: 20 }); p.end();
        check(t.tools.state().history.strokes === 1 && t.pickPage().state().mode === 'picked', 'a drag through the page\'s tools is put down by the lift: one step of the page\'s history' + tag);
        t.doc.key('z', { ctrlKey: true });
        p.begin({ row: 3, column: 3 }); p.extend({ row: 3, column: 3 }, { row: 12, column: 20 }); p.abandon(); p.end();
        check(t.tools.state().history.strokes === 0 && t.pickPage().state().mode !== 'moving', 'a pointer that was lost without a lift (the window) takes the thing back through the tools too' + tag);
        p.begin({ row: 3, column: 3 }); p.extend({ row: 3, column: 3 }, { row: 12, column: 20 }); p.away({ outside: true, x: 900, y: 50 }); p.end();
        check(t.tools.state().history.strokes === 0 && t.pickPage().state().mode === 'picked', 'a lift off the map, told through the page\'s tools, takes the hill back: nothing is written' + tag);
        p.begin({ row: 3, column: 3 }); p.extend({ row: 3, column: 3 }, { row: 12, column: 20 }); p.away({ outside: false, x: 50, y: 50 }); p.end();
        check(t.tools.state().history.strokes === 1, 'and one on the map puts it down' + tag);
        t.doc.key('z', { ctrlKey: true });
        t.button('pan').click();
        check(t.pickPage().state().mode === 'idle' && p.canMove({ row: 3, column: 3 }) === false && p.key({ key: 'Enter' }) === false && p.begin({ row: 3, column: 3 }) === false, 'Pan is the old Move without the moving: a press on a thing looks around' + tag);
        t.doc.key('p'); check(t.tools.state().tool === 'pickup', 'the letter P chooses Pick up' + tag);
        t.doc.key('m'); check(t.tools.state().tool === 'pan', 'and M chooses Pan, as it chose the old Move' + tag);
        t.button('pickup').click(); p.begin({ row: 3, column: 3 }); p.end();
        t.tools.setMap(File.parse(sceneBytes(version)));
        check(t.pickPage().state().mode === 'idle' && t.pickBar().hidden, 'another map lets the pick go' + tag);
    }
    // the part did not load: the page says so, and the tool is Pan
    const t = toolsPage(File.parse(sceneBytes(9)), { failLoad: true });
    t.button('pickup').click();
    check(t.tools.state().tool === 'pan' && t.said.length === 1 && /did not load/.test(t.said[0]) && /Check your connection/.test(t.said[0]), 'a part that did not load is said in plain words, and the tool is Pan: ' + t.said[0]);
    t.failLoad = false;
    t.button('pickup').click();
    check(t.tools.state().tool === 'pickup' && t.loads === 2, 'and choosing it again tries again');
}

// the page's own Tools with the real Pick up part and a canvas that answers state()
function toolsPage(file, opts) {
    opts = opts || {};
    const doc = strictDocument(), el = (tag) => doc.createElement(tag), log = { dirty: [], edits: 0, minis: 0, refreshes: 0, settled: [] };
    const autoBtn = el('button'), autoMob = el('button');
    autoBtn.appendChild(el('span')); autoBtn.appendChild(el('span')); autoMob.appendChild(el('span')); autoMob.appendChild(el('span'));
    const wrap = el('div'), cv = el('div'), map = el('canvas'), cvbar = el('div'); wrap.appendChild(cv); wrap.appendChild(cvbar); cv.appendChild(map); doc.body.appendChild(wrap);
    const host = { tools: [], box: el('div'), title: el('h3'), swatches: el('div'), name: el('div'), tile: el('button'), hint: el('p'), brushes: [], fill: el('button'), autoEdges: [autoBtn, autoMob], undo: el('button'), redo: el('button'),
        stepper: el('div'), stepperLabel: el('span'), stepLess: el('button'), stepValue: el('output'), stepMore: el('button'), map };
    host.stepper.hidden = true; host.tile.appendChild(el('i')); host.tile.appendChild(el('span')); host.box.hidden = true;
    for (const tl of Palette.TOOLS) { const b = el('button'); b.setAttribute('data-tool', tl.id); b.disabled = true; host.tools.push(b); }
    for (const n of [1, 3, 5, 9]) { const b = el('button'); b.setAttribute('data-brush', String(n)); host.brushes.push(b); }
    let current = file, painter = null, pickPart = null;
    const out = { doc, host, log, loads: 0, said: [], failLoad: !!opts.failLoad, canvasNode: map };
    const view = View.create(file, 600, 400);
    const canvas = { setPainter: (p) => { painter = p; }, refresh: () => { log.refreshes++; }, markDirty: (l) => { log.dirty.push(l.slice()); }, refreshMini: () => { log.minis++; }, isStroking: () => false, state: () => ({ view, cursor: null, width: 600, height: 400 }), settle: (keep) => { log.settled.push(keep); }, focus: () => {} };
    const tools = Tools.create({
        doc, win: { devicePixelRatio: 1 }, canvas, host, file: () => current, busy: () => false, dialogs: { open: () => ({ close() {} }) }, say: (t, m) => out.said.push(t + ': ' + m),
        rebase: (bytes) => { current = File.parse(bytes); return current; }, onEdit: () => { log.edits++; },
        loadPick: (done) => { out.loads++; done(out.failLoad ? null : { create: (env) => { pickPart = Pick.create(env); return pickPart; } }); }
    });
    tools.setMap(current);
    Object.assign(out, { tools, painter: () => painter, file: () => current, button: (id) => host.tools.filter((b) => b.getAttribute('data-tool') === id)[0], pickPage: () => pickPart, pickBar: () => pickPart.bar });
    return out;
}

// ================= J =================
function caseJ() {
    const f = File.parse(sceneBytes(9));
    const evil = '<img src=x>';          // 11 bytes: a whole name of the dictionary
    for (const id of [329, 302, 247, 264, 155, 153, 404, 134]) for (let i = 0; i < 11; i++) f.bytes[f.dictAt + 2 + 11 * id + i] = evil.charCodeAt(i);
    const pg = page(File.parse(f.bytes));
    const seen = [];
    const note = () => seen.push(pg.bar.textContent + '|' + pg.env.host.cv.parentNode.querySelector('.sr').textContent);
    pg.tap(3, 3); note(); pg.press('move'); note(); pg.tap(10, 26); note(); pg.tap(30, 20); note(); pg.press('done'); note(); pg.press('cancel');
    pg.tap(10, 26); note(); pg.press('delete'); pg.undo();
    pg.tap(21, 10); note(); pg.press('group'); pg.tap(10, 26); note(); pg.press('cancel'); pg.press('cancel');
    pg.tap(36, 4); note(); pg.press('area'); pg.drag(0, 0, 39, 39); note(); pg.press('move'); pg.tap(20, 20); note();
    pg.press('cancel'); pg.press('cancel');
    const all = seen.join('\n');
    check(!/[<>]|img|src=/.test(all), 'what the bar and the status line say never holds a name of the file\'s dictionary (the words come from the fixed lists): ' + all.slice(0, 120));
    check(/Red hill/.test(all) && /mushroom/.test(all), 'and the names are the plain ones');
    const code = fs.readFileSync(path.join(FRONT, 'mapmaker_pickup.js'), 'utf8');
    check(!/innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\(|new Function/.test(code), 'the script never builds markup (a strict element throws on innerHTML, and the source has none)');
    check(!/\.dict|file\.names|dictAt/.test(code), 'and never reads the dictionary');
    const strict = strictDocument().createElement('p');
    let threw = false; try { strict.innerHTML = 'x'; } catch (e) { threw = true; }
    check(threw, 'the strict element does throw on innerHTML (the check can see a breach)');
    check(typeof strictDocument().body.querySelectorAll('p').forEach === 'undefined' && typeof strictDocument().body.querySelectorAll('p').map === 'undefined', 'and its node list has no forEach, map or filter');
}

// ================= K =================
function libraryFiles() {
    const found = [];
    for (const dir of ['Original-Ants/Maps', 'Community-Maps']) for (const name of fs.readdirSync(path.join(ROOT, dir)).sort()) if (/\.lvl$/i.test(name)) found.push(dir + '/' + name);
    return found;
}
// the things of a map worth picking: the hills, the first pile, the first plant, two ants, and objects of different kinds
function candidates(f) {
    const out = [], l = lay(f), cols = f.columns, seenTile = {};
    const add = (kind, item) => { if (item) out.push({ kind, item }); };
    const at = (r, c, k) => items(f, r, c).filter((x) => x.k === k)[0];
    for (let i = 0; i < f.cells; i++) {
        const t = Edit.tile(f, 2, i);
        if (t === EMPTY2 || !(Edit.flags(f, 2, i) & 1)) continue;
        const r = (i / cols) | 0, c = i % cols;
        if (t >= 245 && t <= 248) { const h = at(r, c, 'obj'); if (h && h.hill >= 0) add('hill', h); continue; }
        const o = at(r, c, 'obj');
        if (!o || seenTile[t] || Object.keys(seenTile).length >= 5) continue;
        if (o.fp.length > 1 && !seenTile.multi) { seenTile.multi = 1; seenTile[t] = 1; add('multi', o); }
        else if ((t >= 129 && t <= 132 || t === 134) && !seenTile.bomb) { seenTile.bomb = 1; seenTile[t] = 1; add('bomb', o); }
        else if (o.fp.length === 1 && !seenTile.one) { seenTile.one = 1; seenTile[t] = 1; add('small', o); }
    }
    if (l.piles.length) add('pile', at(l.piles[0].row, l.piles[0].column, 'pile'));
    const plant = l.records.findIndex((r) => Blocks.PLANT_NAMES[r.tile] && r.row < f.rows && r.column < cols);
    if (plant >= 0) add('plant', at(l.records[plant].row, l.records[plant].column, 'plant'));
    let ants = 0;
    for (const r of l.records) if (Blocks.teamOfMarker(r.tile) >= 0 && ants < 2 && r.row < f.rows && r.column < cols) { add('ant', at(r.row, r.column, 'ant')); ants++; }
    return out.filter((x) => x.item);
}
// a free place for the items: the nearest shift that the check allows (rings of growing size)
function freeShift(f, picked, mode) {
    for (let R = 1; R <= 30; R++) for (let dr = -R; dr <= R; dr++) for (let dc = -R; dc <= R; dc++) {
        if (Math.max(Math.abs(dr), Math.abs(dc)) !== R) continue;
        if (Pick.check(f, picked, dr, dc, mode, 'one').ok) return [dr, dc];
    }
    return null;
}
// what the bytes must be after a move, from the facts of the file format, written apart from the script's own code: the layers cell by cell, then the three lists by their offsets
function expected(file, it, dr, dc, mode) {
    let refusedLink = false;
    const b = Uint8Array.from(file.bytes), cols = file.columns, L = lay(file);
    const rd = (x, at) => x[at] | (x[at + 1] << 8), wr = (x, at, v) => { x[at] = v & 255; x[at + 1] = (v >> 8) & 255; };
    const cellAt = (layer, idx) => (layer === 1 ? file.layer1 : file.layer2) + 6 * idx;
    const shift = (idx) => { const r = (idx / cols | 0) + dr, c = idx % cols + dc; return r * cols + c; };
    function layer2() {
        const cells = it.k === 'pile' ? it.objs : it.fp;
        if (!cells.length) return;
        const inSet = new Set(cells), small = file.rows <= 256 && file.columns <= 256, s8 = (v) => (v > 127 ? v - 256 : v);
        const words = cells.map((idx) => {
            const p = cellAt(2, idx), t = rd(file.bytes, p), f = rd(file.bytes, p + 2), oldWord = rd(file.bytes, p + 4), np = shift(idx), nr = np / cols | 0, nc = np % cols, r0 = idx / cols | 0, c0 = idx % cols;
            const ownWord = file.version === 8 ? (r0 & 255) | ((c0 & 255) << 8) : 0;
            // where the old word points (a place, maybe off the map), and where the link must point after the shift: along with the cells when it points at one of them, else where it did
            let tr = file.version === 8 ? oldWord & 255 : r0 + s8(oldWord & 255), tc = file.version === 8 ? oldWord >> 8 : c0 + s8(oldWord >> 8);
            if (file.version !== 8 && small) { tr &= 255; tc &= 255; }
            const along = tr >= 0 && tc >= 0 && tr < file.rows && tc < cols && inSet.has(tr * cols + tc), ur = along ? tr + dr : tr, uc = along ? tc + dc : tc;
            let w = file.version === 8 ? ((ur & 255) | ((uc & 255) << 8)) : (((ur - nr) & 255) | (((uc - nc) & 255) << 8));
            // the link is kept only when the word reads back as that place, and a cell is its own anchor after the shift exactly when it was before
            let br = file.version === 8 ? w & 255 : nr + s8(w & 255), bc = file.version === 8 ? w >> 8 : nc + s8(w >> 8);
            if (file.version !== 8 && small) { br &= 255; bc &= 255; }
            const refused = br !== ur || bc !== uc || ((tr === r0 && tc === c0) !== (br === nr && bc === nc));
            if (cells.length === 1 && ((t >= 129 && t <= 132) || (t === 134 && oldWord !== ownWord))) return { idx, np, t, f, w: oldWord, refused: false };        // (a bomb's word is its owner, and a wall that names no origin of its own keeps its word)
            return { idx, np, t, f, w, refused };
        });
        if (words.some((x) => x.refused)) { refusedLink = true; return; }
        if (mode !== 'copy') for (const w of words) { const p = cellAt(2, w.idx); wr(b, p, EMPTY2); wr(b, p + 2, 0); wr(b, p + 4, 0); if (rd(file.bytes, p) !== 0 && Solid.clears(w.t) && w.idx >= cols) { const q = cellAt(1, w.idx) + 2; wr(b, q, rd(b, q) & ~1); } }
        if (mode !== 'delete') for (const w of words) { const p = cellAt(2, w.np); wr(b, p, w.t); wr(b, p + 2, w.f); wr(b, p + 4, w.w); if (Solid.sets(w.t)) { const q = cellAt(1, w.np) + 2; wr(b, q, rd(b, q) | 1); } }
    }
    if (it.k === 'obj' || it.k === 'pile') layer2();
    let out = b;
    const splice = (x, at, removed, inserted) => { const o = new Uint8Array(x.length - removed + inserted.length); o.set(x.subarray(0, at), 0); o.set(inserted, at); o.set(x.subarray(at + removed), at + inserted.length); return o; };
    // block 4 first (the latest in the file), then block 2, then block 1
    if (it.k === 'plant' && it.w >= 0 && mode !== 'move' || (it.k === 'plant' && it.w >= 0)) {
        const w = L.waypoints[it.w], at = file.blocksAt + w.rel;
        if (mode === 'move') { wr(out, at, w.row + dr); wr(out, at + 2, w.column + dc); }
        else if (mode === 'delete') out = splice(out, at, w.length, new Uint8Array(0)), wr(out, file.blocksAt + L.b4.rel, rd(out, file.blocksAt + L.b4.rel) - 1);
        else { const bytes = Uint8Array.from(file.bytes.subarray(at, at + w.length)); wr(bytes, 0, w.row + dr); wr(bytes, 2, w.column + dc); const end = file.blocksAt + L.b4.rel + L.b4.length; out = splice(out, end, 0, bytes); wr(out, file.blocksAt + L.b4.rel, rd(out, file.blocksAt + L.b4.rel) + 1); }
    }
    if (it.k === 'pile') {
        const p = L.piles[it.p], at = file.blocksAt + p.rel;
        if (mode === 'move') { wr(out, at, p.row + dr); wr(out, at + 2, p.column + dc); }
        else if (mode === 'delete') { out = splice(out, at, p.length, new Uint8Array(0)); wr(out, file.blocksAt + L.b2.rel, rd(out, file.blocksAt + L.b2.rel) - 1); }
        else { const bytes = Uint8Array.from(file.bytes.subarray(at, at + p.length)); wr(bytes, 0, p.row + dr); wr(bytes, 2, p.column + dc); const end = file.blocksAt + L.b2.rel + L.b2.length; out = splice(out, end, 0, bytes); wr(out, file.blocksAt + L.b2.rel, rd(out, file.blocksAt + L.b2.rel) + 1); }
    }
    if (it.k === 'ant' || it.k === 'plant') {
        const r = L.records[it.i], at = file.blocksAt + r.rel;
        if (mode === 'move') { wr(out, at + 2, r.row + dr); wr(out, at + 4, r.column + dc); }
        else if (mode === 'delete') { out = splice(out, at, 6, new Uint8Array(0)); wr(out, file.blocksAt, rd(out, file.blocksAt) - 1); }
        else { const bytes = Uint8Array.from(file.bytes.subarray(at, at + 6)); wr(bytes, 2, r.row + dr); wr(bytes, 4, r.column + dc); const end = file.blocksAt + L.b1.length; out = splice(out, end, 0, bytes); wr(out, file.blocksAt, rd(out, file.blocksAt) + 1); }
    }
    return refusedLink ? null : out;
}
function expectedRefuses(f, it, dr, dc, mode) { return expected(f, it, dr, dc, mode) === null; }
// an object whose words are the format's own (one origin that every cell names): the others (a quirk of an old map) are held to the looser check
function canonical(f, it) {
    if (it.k !== 'obj' && it.k !== 'pile') return true;
    const cells = it.k === 'pile' ? it.objs : it.fp;
    if (cells.length <= 1) return Edit.anchorIndex(f, cells[0]) === cells[0] || (cells.length === 1 && (Edit.tile(f, 2, cells[0]) >= 129 && Edit.tile(f, 2, cells[0]) <= 134));
    const flagged = cells.filter((i) => Edit.flags(f, 2, i) & 1);
    return flagged.length === 1 && cells.every((i) => Edit.anchorIndex(f, i) === flagged[0]);
}

// moved and moved back, a thing is where it was only when its own cells are what the editor writes (the solid bit of a solid tile) and the cells it lands on are plain empty cells (an old map has
// a stray word in some empty cells, and the bit of a bomb that is not set): those are not the editor's to bring back
function roundTrips(f, it, dr, dc) {
    const cols = f.columns, cells = it.k === 'pile' ? it.objs : it.fp;
    if (!canonical(f, it)) return false;
    for (const i of cells) { const t = Edit.tile(f, 2, i); if (!!(Edit.flags(f, 1, i) & 1) !== Solid.sets(t) && i >= cols) return false; }
    for (const i of cells) {
        const j = ((i / cols | 0) + dr) * cols + i % cols + dc;
        if (cells.indexOf(j) < 0 && (Edit.tile(f, 2, j) !== EMPTY2 || Edit.flags(f, 2, j) !== 0 || Edit.third(f, 2, j) !== 0 || ((Edit.flags(f, 1, j) & 1) && j >= cols))) return false;
    }
    return true;
}
function sweepOne(rel, bytes0, stats, core) {
    const base = File.parse(Uint8Array.from(bytes0));
    if (!base.ok) return;
    const before0 = core ? core.checkMap(Uint8Array.from(bytes0), 'extended').json : null;
    for (const cand of candidates(base)) {
        const picked = [cand.item];
        if (cand.item.k === 'obj' || cand.item.k === 'pile') for (const [dr, dc] of [[0, 1], [1, 0], [-3, 2], [7, -5], [40, 40], [-25, 3]]) {          // (the tie of a cell to its anchor: the check and the file format agree on every shift)
            const f = File.parse(Uint8Array.from(bytes0)), it = items(f, (cand.item.fp[0] / f.columns) | 0, cand.item.fp[0] % f.columns).filter((x) => x.k === cand.item.k)[0];
            for (const mode of ['move', 'copy']) {
                if (mode === 'copy' && cand.kind === 'hill') continue;
                const v = Pick.check(f, [it], dr, dc, mode, 'one'), want = v.ok ? expected(f, it, dr, dc, mode) : 1;
                stats.ties++;
                if (v.ok) check(want !== null, rel + ': ' + mode + ' of a ' + cand.kind + ' by ' + dr + ',' + dc + ' is allowed, though its tie to an anchor could not be kept');
                if (!v.ok && /tied to another place/.test(v.text)) { stats.tied++; check(expectedRefuses(f, it, dr, dc, mode), rel + ': the check says a tie could not be kept by ' + dr + ',' + dc + ' and the file format says it could'); }
            }
        }
        for (const mode of ['move', 'copy', 'delete']) {
            if (mode === 'copy' && cand.kind === 'hill') continue;
            const f = File.parse(Uint8Array.from(bytes0)), ed = editor(f), it = items(f, (cand.item.fp[0] / f.columns) | 0, cand.item.fp[0] % f.columns).filter((x) => x.k === cand.item.k)[0];
            const shiftBy = mode === 'delete' ? [0, 0] : freeShift(f, [it], mode);
            stats.tried++;
            if (!shiftBy) { stats.nofree++; continue; }
            const want = expected(f, it, shiftBy[0], shiftBy[1], mode), was = Uint8Array.from(f.bytes), ok = want !== null;
            check(want !== null, rel + ': ' + mode + ' of a ' + cand.kind + ' by ' + shiftBy + ' is allowed, though a tie of one of its cells to its anchor could not be kept');
            const res = doIt(ed, [it], shiftBy[0], shiftBy[1], mode);
            if (res.error) { check(false, rel + ': ' + mode + ' ' + cand.kind + ' was refused though the check allowed it: ' + res.error); continue; }
            stats.done++;
            if (ok) { stats.exact++; check(same(ed.file.bytes, want), rel + ': ' + mode + ' of a ' + cand.kind + ' by ' + shiftBy + ' gives exactly the bytes that the file format says'); }
            check(ed.history.size().strokes === 1, rel + ': ' + mode + ' of a ' + cand.kind + ' is ONE step');
            const after = Uint8Array.from(ed.file.bytes);
            if (core) {
                const verdict = core.checkMap(after, 'extended').json, old = new Set(before0.findings.map((x) => x.severity + ':' + x.code));
                const fresh = verdict.findings.filter((x) => !old.has(x.severity + ':' + x.code) && !(mode === 'delete' && ((cand.kind === 'hill' && /^hill_(missing|without_entrance)$/.test(x.code)) || (cand.kind === 'ant' && x.code === 'starting_ants_missing'))));
                check(fresh.length === 0 && (verdict.works || !before0.works || (mode === 'delete' && (cand.kind === 'hill' || cand.kind === 'ant'))), rel + ': the core finds nothing new after a ' + mode + ' of a ' + cand.kind + ': ' + JSON.stringify(fresh.slice(0, 2)));
                stats.cored++;
            }
            ed.undo(); check(same(ed.file.bytes, was), rel + ': Undo of a ' + mode + ' of a ' + cand.kind + ' gives the bytes back');
            ed.redo(); check(same(ed.file.bytes, after), rel + ': Redo gives the same bytes');
            if (mode === 'move' && ok && roundTrips(f, it, shiftBy[0], shiftBy[1])) {
                const g = ed.file, back = items(g, ((it.fp[0] / g.columns) | 0) + shiftBy[0], it.fp[0] % g.columns + shiftBy[1]).filter((x) => x.k === it.k)[0];
                if (back && Pick.check(g, [back], -shiftBy[0], -shiftBy[1], 'move', 'one').ok) { doIt(ed, [back], -shiftBy[0], -shiftBy[1], 'move'); check(same(ed.file.bytes, was), rel + ': there and back is the same map (' + cand.kind + ')'); stats.back++; }
            }
        }
    }
}
function caseK(core) {
    const files = libraryFiles(), stats = { tried: 0, nofree: 0, done: 0, exact: 0, back: 0, cored: 0, ties: 0, tied: 0 };
    const shas = {};
    const list = QUICK ? files.filter((x, i) => i % Math.ceil(files.length / 40) === 0 || /Original-Ants/.test(x)) : files;
    for (const rel of list) { const bytes = new Uint8Array(fs.readFileSync(path.join(ROOT, rel))); shas[rel] = sha(bytes); sweepOne(rel, bytes, stats, core); }
    check(stats.done > list.length * 3 && stats.nofree < stats.tried * 0.15, 'the sweep did real work: ' + JSON.stringify(stats) + ' on ' + list.length + ' maps');
    check(stats.exact > stats.done * 0.7 && stats.back > 0, 'most things are of the format\'s own shape (' + stats.exact + ' of ' + stats.done + '), and moved back they are where they were (' + stats.back + ')');
    for (const rel of list) check(sha(new Uint8Array(fs.readFileSync(path.join(ROOT, rel)))) === shas[rel], rel + ': the library file on the disk is untouched');
    process.stdout.write('(' + list.length + ' maps, ' + JSON.stringify(stats) + ') ');
}

// ================= M =================
function caseM() {
    const f = File.parse(makeMap(9, 512, 512)), cols = 512;
    const recs = [];
    for (let i = 0; i < 20000; i++) recs.push([152 + (((i / 150) | 0) % 4), 20 + ((i / 150) | 0), 20 + (i % 150)]);          // (a row is one team)
    const big = File.parse((() => { const x = mapWith(9, 512, 512, recs, [], []); return x; })());
    putHill(big, 300, 300, RED_HILL); putHill(big, 300, 400, BLUE_HILL);
    for (let k = 0; k < 300; k++) putObject(big, MUSH, 160 + ((k / 15) | 0) * 6, 10 + (k % 15) * 6, SQUARE4);
    let t0 = Clock.now();
    const hill = items(big, 301, 301)[0], tPick = Clock.now() - t0;
    check(hill && hill.hill === 1 && tPick < 100, 'a pick on a 512 x 512 map is found at once (' + tPick + ' ms)');
    t0 = Clock.now();
    for (let i = 0; i < 20; i++) Pick.check(big, [hill], 3 + i, 4, 'move', 'one');
    check(Clock.now() - t0 < 200, 'twenty verdicts of a ghost on it take ' + (Clock.now() - t0) + ' ms');
    t0 = Clock.now();
    const some = items(big, 25, 30)[0], group = Pick.antGroup(big, some);
    check(group.length === 150 && Clock.now() - t0 < 500, 'the group of an ant of the crowd is its row of 150 (found in ' + (Clock.now() - t0) + ' ms)');
    t0 = Clock.now();
    const whole = Pick.areaItems(big, { r0: 0, c0: 0, r1: 511, c1: 511 }), tArea = Clock.now() - t0;
    check(whole.length === 20000 + 300 + 2 && tArea < 3000, 'the area of the whole map holds all of it (' + whole.length + ' things in ' + tArea + ' ms)');
    t0 = Clock.now();
    const v = Pick.check(big, whole, 1, 1, 'move', 'area'), tAll = Clock.now() - t0;
    check(tAll < 3000, 'and the verdict of moving all of it takes ' + tAll + ' ms');
    // the page: the ghost of a hill on a 512 x 512 map at the first view (22 pixels a cell): the work is on the overlay and small
    const sprites = []; const pg = page(big, { sprites: () => ({ ghost: () => sprites.push(1) }) });
    pg.tap(301, 301); pg.press('move');
    t0 = Clock.now(); pg.tap(305, 310); pg.tap(306, 311); pg.tap(307, 312);
    const rects = pg.over.getContext('2d').log.rects;
    check(Clock.now() - t0 < 500 && rects < 5000 && !pg.map.ctx && pg.log.refreshes === 0, 'three taps of the ghost draw ' + rects + ' squares on the overlay in ' + (Clock.now() - t0) + ' ms, and the tool asks for no repaint of the map');
    // a whole 20,000-ant crowd as an area, moved in one step
    const pa = page(big);
    t0 = Clock.now();
    pa.tap(301, 301); pa.press('area'); pa.drag(15, 15, 155, 175);
    const n = pa.state().items.length;
    pa.press('move'); pa.tap(400, 100);
    const before = Uint8Array.from(pa.file().bytes);
    const verdict = pa.state().verdict.ok;
    pa.press('done');
    check(n === 20000 && verdict && pa.log.acts === 1 && pa.ed().history.size().strokes === 1 && Clock.now() - t0 < 6000, 'the whole crowd (' + n + ' things) is picked by a box, moved in one step, in ' + (Clock.now() - t0) + ' ms');
    pa.undo(); check(same(pa.file().bytes, before), 'and Undo puts it all back');
}


// ================= N =================
// letting go of the mouse (or the finger) after a drag puts the thing down where the ghost is
function caseN() {
    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')';
        const bytes = sceneBytes(version);
        // a drag: down, moves, up = ONE change, one Undo
        let q = page(File.parse(Uint8Array.from(bytes))), was = Uint8Array.from(q.file().bytes);
        check(q.p.begin({ row: 3, column: 3 }) === true && q.state().mode === 'idle', 'the press on the hill starts a drag and changes nothing' + tag);
        q.p.extend({ row: 3, column: 3 }, { row: 8, column: 10 });
        check(q.state().mode === 'moving' && q.log.acts === 0 && same(q.file().bytes, was), 'while the button is down the ghost follows and nothing is written' + tag);
        q.p.extend({ row: 8, column: 10 }, { row: 12, column: 20 });
        q.p.end();
        check(q.log.acts === 1 && q.ed().history.size().strokes === 1 && q.state().mode === 'picked' && Edit.tile(q.file(), 2, 11 * 40 + 19) === RED_HILL && Edit.tile(q.file(), 2, 3 * 40 + 3) === EMPTY2, 'the button let go puts the hill down where the ghost was: one change' + tag);
        q.undo(); check(same(q.file().bytes, was), 'and one Undo takes it back' + tag);
        // let go twice (a second up, or one that follows the lost capture): nothing more
        q = page(File.parse(Uint8Array.from(bytes)));
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 12, column: 20 }); q.p.end(); q.p.end(); q.p.cancel(); q.p.abandon();
        check(q.log.acts === 1 && q.ed().history.size().strokes === 1, 'a second lift, a cancel or a lost pointer after the drop does nothing more' + tag);
        // a red place: the lift puts the thing BACK where it was picked up (nothing is written, no Undo step) and the bar says why
        q = page(File.parse(Uint8Array.from(bytes))); was = Uint8Array.from(q.file().bytes);
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 10, column: 26 });
        check(q.state().mode === 'moving' && q.off('done') && /the mushroom is in the way/.test(q.warn()), 'while the button is down on the mushroom the ghost is red and says why: ' + q.warn() + tag);
        q.p.end();
        check(q.log.acts === 0 && q.log.refused === 0 && same(q.file().bytes, was) && q.ed().history.size().strokes === 0 && q.state().mode === 'picked' && q.state().dr === 0 && q.state().dc === 0 && q.state().items[0].fp[0] === 2 * 40 + 2, 'let go on the mushroom: nothing is written, no Undo step, the hill is picked where it was' + tag);
        check(/Put back: it needs a free 4 x 4 square, and the mushroom is in the way \(\d+ cells\)\./.test(q.text()) && q.warn() === '' && q.has('move') && !q.has('done'), 'and the bar says "Put back" with the reason: ' + q.text() + tag);
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 30, column: 20 }); q.p.end();
        check(q.log.acts === 1 && q.state().mode === 'picked' && Edit.tile(q.file(), 2, 29 * 40 + 19) === RED_HILL && q.text().indexOf('Put back') < 0, 'the next drag, to a free place, puts it down in one step, and the old note is gone' + tag);
        // off the edge of the map (a red place on the map: the pointer is still over it): the same
        q = page(File.parse(Uint8Array.from(bytes)));
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 0, column: 0 }); q.p.end();
        check(q.log.acts === 0 && q.state().mode === 'picked' && q.state().dr === 0 && q.text().indexOf('Put back: it needs a free 4 x 4 square inside the map.') >= 0, 'let go past the edge of the map: put back, with the reason: ' + q.text() + tag);
        // the other reasons: ants, a rock, a pile, solid ground (an ant)
        q = page(File.parse(Uint8Array.from(bytes)));
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 21, column: 11 }); q.p.end();
        check(q.log.acts === 0 && q.state().mode === 'picked' && q.text().indexOf('Put back: it needs a free 4 x 4 square, and starting ants are in the way') < 0 && /Put back: starting ants are in the way\./.test(q.text()), 'a lift onto ants: put back, "starting ants are in the way": ' + q.text() + tag);
        // a copy, an area and a group are put back the same way
        q = page(File.parse(Uint8Array.from(bytes))); was = Uint8Array.from(q.file().bytes);
        q.tap(30, 30); q.press('copy'); q.p.begin({ row: 30, column: 30 }); q.p.extend({ row: 30, column: 30 }, { row: 21, column: 10 }); q.p.end();
        check(q.log.acts === 0 && same(q.file().bytes, was) && q.state().mode === 'picked' && q.state().copy === false && /Put back: an ant is already there\./.test(q.text()), 'a dragged copy on a red place is put back: nothing is copied: ' + q.text() + tag);
        q = page(File.parse(Uint8Array.from(bytes)));
        q.tap(30, 30); q.press('copy'); q.p.begin({ row: 30, column: 30 }); q.p.extend({ row: 30, column: 30 }, { row: 33, column: 33 }); q.p.extend({ row: 30, column: 30 }, { row: 30, column: 30 }); q.p.end();
        check(q.log.acts === 0 && q.state().mode === 'moving' && q.state().copy === true && q.text().indexOf('Put back') < 0 && /Tap where the copy goes/.test(q.text()), 'a copy dragged back onto its original is no red place: it waits for a place, nothing put back' + tag);
        // a press that does not move, and a tap, still only place the ghost: red stays red, held, with Done off
        q = page(File.parse(Uint8Array.from(bytes)));
        q.tap(3, 3); q.press('move'); q.tap(10, 26);
        check(q.state().mode === 'moving' && q.off('done') && /the mushroom is in the way/.test(q.warn()), 'a tap on a red place only places the red ghost' + tag);
        q.p.begin({ row: 10, column: 26 }); q.p.end();
        check(q.state().mode === 'moving' && q.log.acts === 0 && q.off('done') && q.text().indexOf('Put back') < 0, 'a click on the red ghost does not put it back either' + tag);
        // Done and Enter on a red place keep the thing in hand with the reason, as before
        q.press('done');
        check(q.state().mode === 'moving' && q.log.acts === 0 && q.off('done') && /the mushroom is in the way/.test(q.warn()), 'Done on a red place keeps the thing in hand, with the reason' + tag);
        check(q.key('Enter') === true && q.state().mode === 'moving' && q.log.acts === 0 && /the mushroom is in the way/.test(q.warn()), 'so does Enter' + tag);
        q.p.begin({ row: 10, column: 26 }); q.p.extend({ row: 10, column: 26 }, { row: 30, column: 20 }); q.p.end();
        check(q.log.acts === 1 && q.state().mode === 'picked' && Edit.tile(q.file(), 2, 29 * 40 + 19) === RED_HILL, 'a drag of the red ghost to a free place puts it down in one step' + tag);
        // Escape while the button is down: the thing goes back, and the lift that follows does nothing
        q = page(File.parse(Uint8Array.from(bytes))); was = Uint8Array.from(q.file().bytes);
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 12, column: 20 });
        check(q.key('Escape') === true && q.state().mode === 'idle' || q.state().mode === 'picked', 'Escape during a drag lets go' + tag);
        q.p.extend({ row: 12, column: 20 }, { row: 14, column: 22 }); q.p.end();
        check(q.log.acts === 0 && same(q.file().bytes, was) && q.state().dr === 0, 'the lift after Escape puts nothing down, the thing is where it was' + tag);
        // Escape before the mouse has moved: the lift that follows does not pick the thing again
        q = page(File.parse(Uint8Array.from(bytes)));
        q.tap(3, 3); q.p.begin({ row: 3, column: 3 }); q.key('Escape'); q.p.end();
        check(q.state().mode === 'idle' && q.log.acts === 0, 'Escape with the button down and the mouse not moved lets go, and the lift does not pick it again' + tag);
        // the pointer is lost (a cancelled touch, the window): the same
        q = page(File.parse(Uint8Array.from(bytes)));
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 12, column: 20 }); q.p.cancel(); q.p.end();
        check(q.log.acts === 0 && q.state().mode !== 'moving', 'a cancelled pointer takes it back like Escape' + tag);
        q = page(File.parse(Uint8Array.from(bytes)));
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 12, column: 20 }); q.p.abandon(); q.p.end();
        check(q.log.acts === 0 && q.state().mode !== 'moving', 'so does a pointer that was lost without a lift (abandon)' + tag);
        // a press that does not move is a tap: it picks (on a thing) or puts the ghost (on the ghost) and drops nothing
        q = page(File.parse(Uint8Array.from(bytes)));
        q.p.begin({ row: 3, column: 3 }); q.p.end();
        check(q.state().mode === 'picked' && q.log.acts === 0, 'a click on a thing only picks it' + tag);
        q.press('move'); q.tap(25, 30);
        const spot = q.state().dr;
        q.p.begin({ row: 25, column: 30 }); q.p.end();
        check(q.state().mode === 'moving' && q.log.acts === 0 && q.state().dr === spot && !q.off('done'), 'a click on the ghost puts nothing down: Done does' + tag);
        // the ghost that follows the mouse starts no drag and puts nothing down by itself
        q.p.hover({ row: 20, column: 20 }); q.p.hover({ row: 21, column: 22 });
        q.p.end();
        check(q.log.acts === 0 && q.state().mode === 'moving', 'the hover ghost does not drag, and a lift with nothing pressed does nothing' + tag);
        q.p.hover(null); q.press('done');
        check(q.log.acts === 1, 'Done still puts it down' + tag);
        // Done or Enter while the button is still held: the page would refuse the change (a stroke goes on) and the pick would be thrown away; they wait for the lift, which puts it down where the ghost is then
        q = page(File.parse(Uint8Array.from(bytes)));
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 12, column: 20 });
        q.press('done');
        check(q.state().mode === 'moving' && q.log.acts === 0 && q.log.refused === 0, 'Done while the button is held waits: the move is still on, nothing refused, nothing written' + tag);
        check(q.key('Enter') === true && q.state().mode === 'moving' && q.log.acts === 0 && q.log.refused === 0, 'so does Enter' + tag);
        q.p.extend({ row: 12, column: 20 }, { row: 15, column: 25 }); q.p.end();
        check(q.log.acts === 1 && q.log.refused === 0 && q.ed().history.size().strokes === 1 && q.state().mode === 'picked' && q.state().items[0].fp[0] === 14 * 40 + 24, 'the lift puts it down where the ghost is then, in one change' + tag);
        // let go off the map (off the canvas, past the map's edge, over the side panel: {outside}) or over the bar: the thing goes back, nothing is written
        const inside = { outside: false, x: 100, y: 100 }, off = { outside: true, x: 900, y: 100 };
        q = page(File.parse(Uint8Array.from(bytes))); was = Uint8Array.from(q.file().bytes);
        q.tap(3, 3); q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 12, column: 20 });
        q.p.away(off);
        check(q.state().mode === 'moving' && /Let go here and it goes back where it was\./.test(q.text()), 'dragged off the map the bar says that a lift takes it back' + tag);
        q.p.end();
        check(q.log.acts === 0 && q.log.refused === 0 && same(q.file().bytes, was) && q.state().mode === 'picked' && q.state().dr === 0 && q.state().items[0].fp[0] === 2 * 40 + 2, 'let go off the map: nothing is written, the hill is picked where it was' + tag);
        // while the pointer is off the map the ghost is not drawn
        q = page(File.parse(Uint8Array.from(bytes)));
        q.tap(3, 3); q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 12, column: 20 });
        const ink = q.over.getContext('2d').log;
        ink.strokes.length = 0; q.p.away(inside);
        ink.strokes.length = 0; q.p.away(off);
        const drawn = ink.strokes.slice();
        ink.strokes.length = 0; q.p.away(inside);
        check(drawn.indexOf('#8be0b0') < 0 && ink.strokes.indexOf('#8be0b0') >= 0, 'the ghost is not drawn while the pointer is off the map, and comes back with it' + tag);
        // back on the map before the lift: it is put down
        q = page(File.parse(Uint8Array.from(bytes)));
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 12, column: 20 }); q.p.away(off); q.p.away(inside); q.p.end();
        check(q.log.acts === 1 && Edit.tile(q.file(), 2, 11 * 40 + 19) === RED_HILL, 'out and back before the lift: put down where the ghost is' + tag);
        // over the bar
        q = page(File.parse(Uint8Array.from(bytes))); was = Uint8Array.from(q.file().bytes);
        q.bar.getBoundingClientRect = () => ({ left: 0, right: 600, top: 300, bottom: 400 });
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 12, column: 20 }); q.p.away({ outside: false, x: 300, y: 350 }); q.p.end();
        check(q.log.acts === 0 && same(q.file().bytes, was) && q.state().mode === 'picked', 'let go over the bar: it goes back' + tag);
        q = page(File.parse(Uint8Array.from(bytes)));
        q.bar.getBoundingClientRect = () => ({ left: 0, right: 600, top: 300, bottom: 400 });
        q.p.begin({ row: 3, column: 3 }); q.p.extend({ row: 3, column: 3 }, { row: 12, column: 20 }); q.p.away({ outside: false, x: 300, y: 250 }); q.p.end();
        check(q.log.acts === 1, 'just above the bar it is put down' + tag);
        // the ghost of a dragged ghost, a copy, an area, a group: the same
        q = page(File.parse(Uint8Array.from(bytes))); was = Uint8Array.from(q.file().bytes);
        q.tap(3, 3); q.press('move'); q.tap(25, 30);
        q.p.begin({ row: 25, column: 30 }); q.p.extend({ row: 25, column: 30 }, { row: 26, column: 31 }); q.p.away(off); q.p.end();
        check(q.log.acts === 0 && same(q.file().bytes, was) && q.state().mode === 'picked', 'a dragged ghost let go off the map: back to the pick' + tag);
        q = page(File.parse(Uint8Array.from(bytes)));
        q.tap(36, 4); q.press('copy'); q.tap(34, 8);
        q.p.begin({ row: 34, column: 8 }); q.p.extend({ row: 34, column: 8 }, { row: 30, column: 12 }); q.p.away(off); q.p.end();
        check(q.log.acts === 0 && q.state().mode === 'picked', 'so does a copy' + tag);
        q = page(File.parse(Uint8Array.from(bytes))); was = Uint8Array.from(q.file().bytes);
        q.tap(36, 4); q.press('area'); q.drag(18, 8, 24, 14);
        q.p.begin({ row: 21, column: 10 }); q.p.extend({ row: 21, column: 10 }, { row: 5, column: 30 }); q.p.away(off); q.p.end();
        check(q.log.acts === 0 && same(q.file().bytes, was) && q.state().mode === 'picked' && q.state().kind === 'area' && q.state().items.length === 6, 'and an area' + tag);
        // away with no drag (a hover, a tap) changes nothing
        q = page(File.parse(Uint8Array.from(bytes)));
        q.tap(3, 3); q.press('move'); q.p.away(off);
        check(q.state().mode === 'moving' && !/goes back/.test(q.text()), 'a pointer off the map with nothing dragged changes nothing' + tag);
        // a press on the ghost that never moved, with the pointer off the map, and its lift: the ghost is back, nothing says "goes back"
        q = page(File.parse(Uint8Array.from(bytes)));
        q.tap(3, 3); q.press('move'); q.tap(25, 30);
        q.p.begin({ row: 25, column: 30 }); q.p.away(off); q.p.end();
        check(q.state().mode === 'moving' && q.log.acts === 0 && !/goes back/.test(q.text()), 'a press that never moved, let go off the map, leaves the ghost where it is and the bar as it was' + tag);
        q.p.begin({ row: 25, column: 30 }); q.p.extend({ row: 25, column: 30 }, { row: 26, column: 31 }); q.p.end();
        check(q.log.acts === 1, 'and the next drag is put down' + tag);
        // the hover, the arrows and Enter still work with a red ghost that was placed by a tap
        q = page(File.parse(Uint8Array.from(bytes)));
        q.tap(3, 3); q.press('move'); q.tap(10, 26);
        q.p.hover({ row: 30, column: 20 }); q.p.hover(null);
        q.key('ArrowLeft');
        q.p.begin({ row: 10, column: 25 }); q.p.extend({ row: 10, column: 25 }, { row: 30, column: 20 }); q.p.end();
        check(q.log.acts === 1 && q.state().mode === 'picked', 'a red ghost that a tap placed can be dragged on to a free place' + tag);
    }
    // a copy: dragging the ghost of a copy and letting go puts the copy down
    let c = page(File.parse(sceneBytes(9)));
    c.tap(36, 4); c.press('copy'); c.tap(34, 8);
    c.p.begin({ row: 34, column: 8 }); c.p.extend({ row: 34, column: 8 }, { row: 30, column: 12 }); c.p.end();
    check(c.log.acts === 1 && Edit.tile(c.file(), 2, 36 * 40 + 4) === ROCK && Edit.tile(c.file(), 2, 30 * 40 + 8 + 4) === ROCK && /^Copied it/.test(c.state().note), 'a dragged copy is put down when the button is let go, the rock is still where it was');
    // an area and a group move as one change
    let a = page(File.parse(sceneBytes(9)));
    a.tap(36, 4); a.press('area'); a.drag(18, 8, 24, 14);
    const box = a.state().items.length, wasA = Uint8Array.from(a.file().bytes);
    a.drag(21, 10, 5, 30);
    check(box === 6 && a.log.acts === 1 && a.ed().history.size().strokes === 1 && a.state().mode === 'picked' && a.state().kind === 'area' && lay(a.file()).records.filter((r) => r.row < 12 && r.column >= 20).length === 6, 'an area dragged by one of its things is put down as a whole, in one step');
    a.undo(); check(same(a.file().bytes, wasA), 'and one Undo gives all of it back');
    let g = page(File.parse(sceneBytes(9)));
    g.tap(21, 10); g.press('group'); g.tap(5, 20); g.press('done');
    const wasG = Uint8Array.from(g.file().bytes), mid = g.state().items[2].fp[0], mr = (mid / 40) | 0, mc = mid % 40;
    g.drag(mr, mc, mr - 2, mc - 5);
    check(g.log.acts === 2 && g.state().kind === 'group' && g.state().items.length === 6 && lay(g.file()).records.filter((r) => r.tile === 153 && r.row <= 4 && r.column <= 16).length === 6, 'a group dragged by one of its ants is put down as a whole');
    g.undo(); check(same(g.file().bytes, wasG), 'in one step');
}

// ================= O =================
// The bar is a row of its own under the map, so the map changes its size when the bar comes, goes or changes its height (mapmaker_canvas.js settle): the tool puts the view where it should be: the top row and
// the scale stay, and a picked thing that the bar would cover is brought into view; never while the pointer drives the ghost, which is under the pointer already.
function caseO() {
    const f = File.parse(mapWith(9, 120, 80, [], [], []));
    putHill(f, 100, 40, RED_HILL);          // (rows 100 to 103, columns 40 to 43)
    const pg = page(f, { width: 400, height: 300 }), last = () => pg.log.settles[pg.log.settles.length - 1];
    const kids = pg.wrap.children;
    check(kids.indexOf(pg.cv) + 1 === kids.indexOf(pg.bar) && kids.indexOf(pg.bar) + 1 === kids.indexOf(pg.cvbar), 'the bar is its own row: after the map, before the line under it');
    check(pg.cv.children.indexOf(pg.bar) < 0, 'and not over the map');
    check(pg.log.vars['--pk-h'] === '0px' && typeof last() === 'function', 'with nothing picked the bar has no height, and the map is still told (it may have been higher a moment ago)');
    pg.bar.offsetHeight = 150; pg.tap(101, 41);
    check(pg.log.vars['--pk-h'] === '150px' && typeof last() === 'function', 'a pick: the page is told how high the bar is (a phone leaves that room below the page), and the map is told how to look');
    const at = (cy, w, h, cx) => { const v = { cx: cx === undefined ? 42 : cx, cy, scale: 20 }; last()(v, w || 400, h || 300); return v; };
    const edge = (v, row, h) => View.toScreen(v, 400, h || 300, row, 0).y, side = (v, col) => View.toScreen(v, 400, 300, 0, col).x;
    check(at(101).cy === 101 && at(101).cx === 42, 'a hill that is in view does not move the map');
    const low = at(96);
    check(Math.abs(edge(low, 104) - (300 - 40)) < 1e-9 && low.cx === 42, 'a hill under the bar is brought up until its foot is one margin above the foot of the map (cy ' + low.cy + ')');
    const high = at(106);
    check(Math.abs(edge(high, 100) - 40) < 1e-9, 'one above the map is brought down to a margin below its top (cy ' + high.cy + ')');
    check(Math.abs(side(at(101, 400, 300, 65), 40) - 40) < 1e-9 && Math.abs(side(at(101, 400, 300, 15), 44) - 360) < 1e-9, 'and sideways the same way: the left side of a hill that is left of the map is a margin inside the left edge, the right side of one that is right of it a margin inside the right edge');
    const lower = at(101, 400, 200);
    check(Math.abs(edge(lower, 104, 200) - (200 - 40)) < 1e-9 && Math.abs(edge(lower, 100, 200) - 80) < 1e-9, 'a map that got lower: the hill is still in view (cy ' + lower.cy + ')');
    // a thing taller than the map has its start in view
    const small = page(f, { width: 400, height: 100 }), lastS = () => small.log.settles[small.log.settles.length - 1];
    small.bar.offsetHeight = 80; small.tap(101, 41);
    const tallAt = (cy) => { const v = { cx: 42, cy, scale: 20 }; lastS()(v, 400, 100); return View.toScreen(v, 400, 100, 100, 0).y; };
    check(Math.abs(tallAt(101) - 40) < 1e-9 && Math.abs(tallAt(99) - 40) < 1e-9, 'a thing taller than the map has its start in view, a margin inside its top, from above and from below');
    // the ghost is looked at too (a tap put it down above the picked place), and not while the pointer drives it
    pg.press('move'); pg.tap(80, 50);
    const ghost = pg.state(), v = { cx: 42, cy: 101, scale: 20 };
    last()(v, 400, 300);
    check(ghost.dr !== 0 && Math.abs(edge(v, 100 + ghost.dr) - 40) < 1e-9, 'a thing on its way is looked at where the ghost is: ' + JSON.stringify([ghost.dr, ghost.dc, v.cy]));
    const idle = () => at(96).cy === 96;          // (the hill would be brought up: it is not looked at)
    pg.p.hover({ row: 90, column: 50 });
    check(idle(), 'while the mouse drives the ghost the map is not moved (it is under the mouse)');
    pg.p.hover(null);
    pg.press('cancel');
    pg.tap(101, 41);
    pg.p.begin({ row: 101, column: 41 }); pg.p.extend({ row: 101, column: 41 }, { row: 90, column: 50 });
    check(idle(), 'nor while a drag goes on');
    pg.p.end();
    pg.press('cancel');
    check(pg.hidden() && idle() && Math.abs(at(30, 400, 200).cy - (30 - 100 / 40)) < 1e-9 && pg.log.vars['--pk-h'] === '0px', 'the bar goes: the map keeps its top row (the middle moves by half of the change), and looks at nothing; the page has no room to keep');
    pg.bar.offsetHeight = undefined;
    pg.tap(101, 41);
    check(pg.log.vars['--pk-h'] === '0px', 'a bar that has no height says 0');
}

// ================= P =================
// The sentence sets the bar's height (a place where it cannot go says more than one where it can), and the bar sets the map's: a model of the page's layout (the bar is 150 high, 200 with the warning, 40 more when it is narrow,
// never lower than its min-height) and a map that is 500 minus the bar.
function barPage(opts) {
    const f = File.parse(mapWith(9, 120, 80, [], [], []));
    putHill(f, 100, 40, RED_HILL);
    const pg = page(f, { width: 400, height: 300, win: opts && opts.win });
    const model = { width: 800 };
    Object.defineProperty(pg.bar, 'offsetWidth', { get: () => model.width });
    Object.defineProperty(pg.bar, 'offsetHeight', { get: () => (pg.bar.hidden ? 0 : Math.max((pg.bar.querySelector('.pk-warn') ? 200 : 150) + (model.width < 500 ? 40 : 0), parseFloat(pg.bar.style.minHeight) || 0)) });
    pg.model = model;
    pg.mapHeight = () => 500 - pg.bar.offsetHeight;
    return pg;
}
function caseP() {
    const pg = barPage(), heights = [];
    pg.tap(101, 41); pg.press('move');
    const lastH = () => pg.mapHeight();
    // the pointer goes over the last rows 2 pixels at a time: a place that is red (off the map) and one that is green (a hill's height away from it) follow each other
    for (let i = 0; i < 12; i++) { pg.p.hover({ row: i % 2 ? 0 : 60, column: 41 }); heights.push(lastH()); }
    check(pg.warn() !== '' || heights.some((h) => h === 300), 'a red place says why in a warning, and the bar is higher for it: ' + heights.join(','));
    check(heights.every((h, i) => i === 0 || h <= heights[i - 1]) && new Set(heights).size <= 2, 'the map is never made higher while the pointer moves: it shrinks once, not at every step (' + heights.join(',') + ')');
    pg.p.hover({ row: 60, column: 41 });
    check(pg.bar.querySelector('.pk-warn') === null && pg.bar.offsetHeight === 200 && pg.bar.style.minHeight === '200px', 'back on a green place the bar stays as high as it was (' + pg.bar.offsetHeight + ', min-height ' + pg.bar.style.minHeight + ')');
    pg.press('fold'); check(pg.bar.style.minHeight === '' && pg.bar.offsetHeight === 150, 'a bar that is folded by its Hide button keeps no height');
    pg.press('fold'); check(pg.bar.style.minHeight === '150px' && pg.bar.offsetHeight === 150, 'and holds the height that it has when it is open again');
    pg.p.hover({ row: 0, column: 41 });
    check(pg.bar.offsetHeight === 200, 'a place where it cannot go makes it high again: ' + pg.bar.offsetHeight);
    pg.p.hover(null); pg.press('cancel'); pg.press('cancel');
    check(pg.hidden() && pg.bar.style.minHeight === '' && pg.log.vars['--pk-h'] === '0px', 'the bar goes: no height is kept');
    pg.tap(101, 41);
    check(pg.bar.style.minHeight === '' && pg.bar.offsetHeight === 150, 'a thing that is only picked up: the bar has the height of its words (' + pg.bar.offsetHeight + ')');
    pg.press('move'); pg.p.hover({ row: 0, column: 41 }); pg.p.hover({ row: 60, column: 41 });
    check(pg.bar.offsetHeight === 200, 'a second move starts again from nothing: it keeps the height of this move (' + pg.bar.offsetHeight + ')');
    pg.press('cancel');
    check(pg.bar.style.minHeight === '' && pg.bar.offsetHeight === 150, 'Cancel: the bar has the height of its words again');
    pg.press('move'); pg.p.hover({ row: 0, column: 41 });
    pg.model.width = 390;          // (the phone is turned upright: the bar is narrower, its words wrap)
    pg.p.hover({ row: 60, column: 41 });
    check(pg.bar.offsetHeight === 190 && pg.bar.style.minHeight === '190px', 'a bar at another width is measured again, not held at the height of the old one: ' + pg.bar.offsetHeight);
    pg.model.width = 800; pg.p.hover({ row: 61, column: 41 });
    check(pg.bar.offsetHeight === 150 && pg.bar.style.minHeight === '150px', 'and the same when it gets wider: ' + pg.bar.offsetHeight);

    // the observer: made with the tool, sees the bar, tells the page when the bar's size changed, lets go when the tool is left
    const made = [];
    class Watcher { constructor(cb) { this.cb = cb; this.seen = []; this.gone = false; made.push(this); } observe(n) { this.seen.push(n); } disconnect() { this.gone = true; this.seen = []; } }
    const rp = barPage({ win: { ResizeObserver: Watcher } });
    check(made.length === 1 && made[0].seen.length === 1 && made[0].seen[0] === rp.bar, 'the tool watches the size of its bar (a ResizeObserver made with the tool)');
    rp.tap(101, 41);
    const settles = () => rp.log.settles.length;
    let n = settles();
    check(rp.log.vars['--pk-h'] === '150px', 'the page has room for the bar: ' + rp.log.vars['--pk-h']);
    made[0].cb([]); made[0].cb([]);
    check(settles() === n, 'the observer is told and the size is the same: nothing is done (it cannot go round in a circle)');
    rp.model.width = 390;          // (a phone turned upright while the bar is open: it is 40 higher, and nothing was drawn again)
    made[0].cb([]);
    check(rp.log.vars['--pk-h'] === '190px' && settles() === n + 1, 'the bar is higher with nothing drawn again: the page gets the room of the bar as it is now (' + rp.log.vars['--pk-h'] + ') and the map is told');
    made[0].cb([]);
    check(settles() === n + 1, 'and the next call with this size does nothing');
    rp.model.width = 800; made[0].cb([]);
    check(rp.log.vars['--pk-h'] === '150px' && settles() === n + 2, 'a wider bar, lower again: ' + rp.log.vars['--pk-h']);
    rp.press('move'); rp.p.hover({ row: 0, column: 41 }); rp.p.hover({ row: 60, column: 41 });
    n = settles(); rp.model.width = 390; made[0].cb([]);
    check(rp.log.vars['--pk-h'] === '190px' && settles() === n + 1, 'while a thing is on its way, the observer sees the bar at the new width too, not at the old height held: ' + rp.log.vars['--pk-h']);
    rp.press('cancel'); rp.press('cancel');
    check(rp.hidden(), 'Cancel twice: the bar goes');
    n = settles(); rp.model.width = 800; made[0].cb([]);
    check(settles() === n, 'a hidden bar is not looked at');
    rp.pick.show(false);
    check(made[0].gone === true && made[0].seen.length === 0, 'the observer is let go of when the tool is left');
    rp.pick.show(false);
    check(made.length === 1, 'and leaving twice does nothing');
    rp.pick.show(true);
    check(made.length === 2 && made[1].seen[0] === rp.bar && made[0].gone === true, 'the tool chosen again watches again, once');
    rp.pick.show(true);
    check(made.length === 2, 'and choosing it twice makes no second observer');

    // a page without the observer: the window's resize does it, and it is taken off again
    const listeners = [], fake = { addEventListener: (t, fn) => listeners.push([t, fn]), removeEventListener: (t, fn) => { const i = listeners.findIndex((l) => l[0] === t && l[1] === fn); if (i >= 0) listeners.splice(i, 1); } };
    const wp = barPage({ win: fake });
    check(listeners.length === 1 && listeners[0][0] === 'resize', 'with no ResizeObserver the tool listens to the window\'s resize');
    wp.tap(101, 41);
    n = wp.log.settles.length; wp.model.width = 390; listeners[0][1]();
    check(wp.log.vars['--pk-h'] === '190px' && wp.log.settles.length === n + 1, 'a resize: the room of the bar is made again (' + wp.log.vars['--pk-h'] + ')');
    listeners[0][1]();
    check(wp.log.settles.length === n + 1, 'and the same size again does nothing');
    wp.pick.show(false);
    check(listeners.length === 0, 'the listener is taken off when the tool is left');
    wp.pick.show(true); wp.pick.show(true);
    check(listeners.length === 1, 'and put on once when it is chosen again');
}

// ================= L (the core) =================
async function caseL() {
    const js = path.join(MODULE, 'lvl_core.js');
    if (!fs.existsSync(js) || !fs.existsSync(path.join(MODULE, 'lvl_core.wasm'))) {
        console.log((REQUIRE ? 'FAIL' : 'SKIP') + ': no lvl_core.js and lvl_core.wasm in ' + MODULE);
        if (REQUIRE) failures++;
        return;
    }
    const core = require(path.join(ROOT, 'web', 'lvl_core_api.js')).wrap(await require(path.resolve(js))());
    caseK(core);
}

module.exports = { page, sceneBytes, mapWith, putHill, putObject, items, lay, File, Edit, Blocks, Pick };
// (the sweep of the library, the long one, is the last: a broken copy that a short case refuses is refused at once, tests/scripts/web_mapmaker_bail.js)
const CASES = [['A what stands on a cell', caseA], ['B a group and an area', caseB], ['C where a thing cannot go', caseC], ['W water is a hint, not a refusal', caseW], ['D the change of a move', caseD], ['E copy, delete and an area', caseE], ['F lists, limits, crowds', caseF],
    ['G the page: the bar, the ghost and the ring', caseG], ['H the keys and the pointer', caseH], ['I a map that changes, the page\'s Tools', caseI], ['J text of a file is only text', caseJ], ['M big maps', caseM], ['O the bar is a row under the map', caseO], ['P the bar holds its height, the page follows its size', caseP], ['N a drag is put down when the button is let go', caseN], ['K the library', () => caseK(null)]];
async function run(cases) {
    for (const [name, fn] of cases) {
        process.stdout.write('  RUNNING: ' + name + ' ... ');
        const before = failures;
        try { await fn(); } catch (e) { failures++; console.log('\n    FAILED: threw ' + (e && e.stack ? e.stack : e)); }
        console.log(failures === before ? 'PASS' : 'FAIL');
    }
}
if (require.main === module) {
    run(CASES.concat([['L the same maps through the real core', caseL]])).then(() => { console.log(checks + ' checks, ' + failures + ' failed'); process.exit(failures ? 1 : 0); });
}
