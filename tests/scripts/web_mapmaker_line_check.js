#!/usr/bin/env node
// The map maker's straight line (docs/MAP_EDITOR.md, "Painting"): while a stroke of ground, objects or Erase goes on, Shift or Ctrl held down keeps it to ONE line, across or down, through the cell
// where it began (web/front/mapmaker_canvas.js, `aim`, and `straight` of the painter in mapmaker_tools.js). Run with the real canvas, the real tools, the real history and a small fake of the
// DOM, with no browser: pointer events with shiftKey and ctrlKey go in, the bytes of the map come out.
//
//   node tests/scripts/web_mapmaker_line_check.js [--front DIR]
//
//   --front DIR  the folder of the mapmaker_*.js files to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
//   A  the rule, stroke by stroke, for Ground, Objects and Erase: across, down, the longer side wins, a tie is across, the start cell alone, the line is kept, the way back to the start, the
//      edge of the map; against the cells that the rule (written again here) and a hand-made list say
//   B  the modifiers: Shift, Ctrl, both; Command and Alt alone change nothing; Shift let go and pressed again in one stroke; a new stroke starts free
//   C  a zoomed and panned view (the rule is in cells, not in pixels), and a pointer outside the canvas
//   D  the same bytes as a free stroke along the line, with a brush of 3, with Auto edges and for Fill; and, with no modifier, the same bytes as before, for a mouse, a pen and a finger
//   E  the tools that are not strokes of this kind change nothing: tap tools (plants, bigger objects, hills, ants, food), Pan, Pick up and the Ants box (a drag of a box, a drag of a thing)
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const path = require('path');
const crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Canvas = load('mapmaker_canvas.js'), Edit = load('mapmaker_edit.js'), Palette = load('mapmaker_palette.js'), Tools = load('mapmaker_tools.js'),
    Ground = load('mapmaker_ground.js'), Edges = load('mapmaker_edges.js');
const fakeDom = require('./web_mapmaker_fake_dom.js').makeDocument;

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }
function digest(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }

// A made-up map of version 9: every layer-1 cell plain gravel (485), layer 2 empty, the blocks empty
function makeMap(rows, columns) {
    const names = 486, head = 8 + 2 + 4 + 2 + 2, cells = rows * columns;
    const out = new Uint8Array(head + 2 + 11 * names + 8 + 12 * cells + 12), view = new DataView(out.buffer);
    let at = 0;
    const dword = (v) => { view.setUint32(at, v, true); at += 4; }, word = (v) => { view.setUint16(at, v, true); at += 2; };
    dword(9); dword(1); word(10); dword(0); word(1); word(0);
    word(names - 1);
    for (let i = 0; i < names; i++) { out[at] = i === 485 ? 0x67 : 0x2E; if (i === 485) { out[at + 1] = 0x30; out[at + 2] = 0x31; out[at + 3] = 0x61; } at += 11; }
    dword(rows); dword(columns);
    for (let i = 0; i < cells; i++) { word(485); word(0); word(0xCDCD); }
    for (let i = 0; i < cells; i++) { word(0x7FFE); word(0); word(0); }
    word(0); word(0); word(0); word(0x7FFE); word(0); word(6);
    return out;
}
function open(rows, columns) { const f = File.parse(makeMap(rows, columns)); if (!f.ok) throw new Error(f.error); return f; }

// ---- the page: the real canvas and the real tools on fake nodes ----
function rig(rows, columns, opts) {
    opts = opts || {};
    const ctx = new Proxy({}, { get: (t, k) => (k in t ? t[k] : (k === 'createImageData' ? (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }) : () => undefined)), set: (t, k, v) => { t[k] = v; return true; } });
    const listeners = () => { const l = {}; return { l, addEventListener: (t, f) => { (l[t] = l[t] || []).push(f); }, fire: (t, e) => (l[t] || []).forEach((f) => f(e)) }; };
    const size = { w: 600, h: 400 };
    const canvasNode = () => Object.assign(listeners(), { width: 0, height: 0, attrs: {}, getContext: () => ctx, getBoundingClientRect: () => ({ left: 0, top: 0, width: size.w, height: size.h }),
        setAttribute(k, v) { this.attrs[k] = String(v); }, removeAttribute(k) { delete this.attrs[k]; }, getAttribute(k) { return k in this.attrs ? this.attrs[k] : null }, setPointerCapture() {}, focus() {}, classList: { toggle() {} }, hidden: false });
    const cdoc = Object.assign(listeners(), { createElement: canvasNode, visibilityState: 'visible' }), cwin = Object.assign(listeners(), { devicePixelRatio: 1, performance: { now: () => 0 } });
    const node = canvasNode(), plain = () => Object.assign(listeners(), { hidden: true, textContent: '', disabled: false });
    const minimap = Object.assign(plain(), { classList: { toggle() {} } }), where = plain();
    const real = Canvas.create({ doc: cdoc, win: cwin, canvas: node, minimap, mini: canvasNode(), miniCap: plain(), where, zoomIn: plain(), zoomOut: plain() });
    let painter = null;
    const canvas = Object.assign({}, real, { setPainter(p) { painter = p; real.setPainter(p); } });          // (the painter that the tools hand the canvas, to ask it)

    const doc = fakeDom(), el = (tag) => doc.createElement(tag);
    const autoBtn = el('button'), autoMob = el('button');
    autoBtn.appendChild(el('span')); autoBtn.appendChild(el('span')); autoMob.appendChild(el('span')); autoMob.appendChild(el('span'));
    const host = { tools: [], box: el('div'), title: el('h3'), swatches: el('div'), name: el('div'), tile: el('button'), hint: el('p'), brushes: [], fill: el('button'), autoEdges: [autoBtn, autoMob], undo: el('button'), redo: el('button'),
        stepper: el('div'), stepperLabel: el('span'), stepLess: el('button'), stepValue: el('output'), stepMore: el('button') };
    host.stepper.hidden = true; host.tile.appendChild(el('i')); host.tile.appendChild(el('span')); host.box.hidden = true;
    for (const t of Palette.TOOLS) { const b = el('button'); b.setAttribute('data-tool', t.id); b.disabled = true; host.tools.push(b); }
    for (const n of [1, 3, 5, 9]) { const b = el('button'); b.setAttribute('data-brush', String(n)); host.brushes.push(b); }
    let current = open(rows, columns);
    const store = {}, said = [];
    const tools = Tools.create({
        doc, win: { localStorage: { getItem: (k) => store[k] || null, setItem: (k, v) => { store[k] = String(v); } } }, canvas, host, file: () => current, busy: () => false, say: (title, text) => said.push(title + ': ' + text),
        rebase: (bytes) => { current = File.parse(bytes); canvas.swapFile(current); return current; }, onEdit: () => {}, props: () => {}, convert: () => {}, about: () => '',
        loadAnts: opts.ants ? (cb) => cb(opts.ants) : undefined, loadPick: opts.pick ? (cb) => cb(opts.pick) : undefined
    });
    canvas.setFile(current);
    tools.setMap(current);
    const button = (id) => host.tools.filter((b) => b.getAttribute('data-tool') === id)[0];
    const swatch = (tile) => host.swatches.children.filter((b) => b.getAttribute('data-tile') === String(tile))[0];

    const view = () => canvas.state().view;
    // where the middle of a cell is, in the page's pixels, for the view as it is now (a row or a column outside the map is a point outside it); `fr`, `fc`: where in the cell (0 to 1)
    const pt = (r, c, fr, fc) => { const v = view(), w = canvas.state().width, h = canvas.state().height; return { clientX: w / 2 + (c + (fc === undefined ? 0.5 : fc) - v.cx) * v.scale, clientY: h / 2 + (r + (fr === undefined ? 0.5 : fr) - v.cy) * v.scale }; };
    const mods = (m) => ({ shiftKey: /s/.test(m || ''), ctrlKey: /c/.test(m || ''), metaKey: /m/.test(m || ''), altKey: /a/.test(m || '') });
    const fire = (type, extra) => node.fire(type, Object.assign({ pointerType: opts.type || 'mouse', pointerId: 7, button: 0, buttons: 1, shiftKey: false, ctrlKey: false, metaKey: false, altKey: false, clientX: 0, clientY: 0, preventDefault() {} }, extra));
    const api = {
        canvas, tools, host, node, where, doc, painter: () => painter, size, said, file: () => current, view, pt, button, swatch,
        choose(id) { button(id).click(); },
        down(r, c, m, extra) { fire('pointerdown', Object.assign(pt(r, c, extra && extra.fr, extra && extra.fc), mods(m), extra)); },
        move(r, c, m, extra) { fire('pointermove', Object.assign(pt(r, c, extra && extra.fr, extra && extra.fc), mods(m), extra)); },
        moveTo(x, y, m, extra) { fire('pointermove', Object.assign({ clientX: x, clientY: y }, mods(m), extra)); },
        up(r, c, m, extra) { fire('pointerup', Object.assign(pt(r, c), mods(m), { buttons: 0 }, extra)); },
        // the tiles of a layer, to find what changed
        layer(n) { const out = []; for (let i = 0; i < current.cells; i++) out.push(Edit.tile(current, n, i)); return out; },
        digest() { return digest(current.bytes); },
        zoom(times) { for (let i = 0; i < times; i++) node.fire('wheel', { clientX: 300, clientY: 200, deltaY: -600, deltaMode: 0, preventDefault() {} }); },
        pan(dx, dy) { const x = 300, y = 200; fire('pointerdown', { button: 1, buttons: 4, clientX: x, clientY: y }); fire('pointermove', { button: 1, buttons: 4, clientX: x + dx / 2, clientY: y + dy / 2 }); fire('pointermove', { button: 1, buttons: 4, clientX: x + dx, clientY: y + dy }); fire('pointerup', { button: 1, buttons: 0, clientX: x + dx, clientY: y + dy }); }
    };
    return api;
}

// A stroke: down at `start` (no modifier unless start.mod), then steps [r, c, mods, extra], then up where the last step was; the cells of the layer that changed, as 'row,column', in order of index
function stroke(p, layer, start, steps, keep) {
    const before = p.layer(layer);
    p.down(start[0], start[1], start[2]);
    let last = start;
    for (const s of steps) { p.move(s[0], s[1], s[2], s[3]); last = s; }
    const stroking = p.node.attrs['data-stroke'] === 'on';
    if (!keep) p.up(last[0], last[1], last[2]);
    const after = p.layer(layer), out = [];
    for (let i = 0; i < after.length; i++) if (after[i] !== before[i]) out.push(((i / p.file().columns) | 0) + ',' + (i % p.file().columns));
    out.stroking = stroking;
    return out;
}
const sorted = (list) => Array.from(new Set(list)).sort((a, b) => { const [r, c] = a.split(',').map(Number), [s, d] = b.split(',').map(Number); return r - s || c - d; });
const rowOf = (r, c0, c1) => { const o = []; for (let c = Math.min(c0, c1); c <= Math.max(c0, c1); c++) o.push(r + ',' + c); return o; };
const colOf = (c, r0, r1) => { const o = []; for (let r = Math.min(r0, r1); r <= Math.max(r0, r1); r++) o.push(r + ',' + c); return o; };
const freeLine = (a, b) => Edit.lineCells(a[0], a[1], b[0], b[1]).map((x) => x[0] + ',' + x[1]);
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

// The rule, written again from the words of docs/MAP_EDITOR.md: the cells that a stroke paints (brush of 1) for steps [r, c, mods] (a row or a column outside the map is a point outside it)
function model(rows, columns, start, steps) {
    const painted = [start[0] + ',' + start[1]], held = (m) => /[sc]/.test(m || '');
    let last = [start[0], start[1]], lock = null;
    for (const s of steps) {
        let target;
        if (!held(s[2])) {
            lock = null;
            if (s[0] < 0 || s[1] < 0 || s[0] >= rows || s[1] >= columns) continue;
            target = [s[0], s[1]];
        } else {
            if (!lock) lock = { from: last, axis: '' };
            const dr = s[0] - lock.from[0], dc = s[1] - lock.from[1];
            if (!lock.axis) { if (!dr && !dc) continue; lock.axis = Math.abs(dc) >= Math.abs(dr) ? 'across' : 'down'; }
            target = lock.axis === 'across' ? [lock.from[0], clamp(s[1], 0, columns - 1)] : [clamp(s[0], 0, rows - 1), lock.from[1]];
        }
        for (const x of freeLine(last, target)) painted.push(x);
        last = target;
    }
    return sorted(painted);
}

const ROWS = 40, COLUMNS = 60;
// the tools whose stroke is a line: how each is set up (the swatch to choose, the layer that shows the stroke, and a map to paint on)
const STROKES = [
    { tool: 'ground', swatch: 487, layer: 1, name: 'Ground' },
    { tool: 'objects', swatch: 302, layer: 2, name: 'Objects' },
    { tool: 'erase', swatch: null, layer: 2, name: 'Erase' }
];
// a page for a tool: Erase gets a field of rocks to take away (put with free strokes, before the check begins)
function setup(spec, rows, columns, opts) {
    const p = rig(rows || ROWS, columns || COLUMNS, opts);
    if (spec.tool === 'erase') {
        p.choose('objects'); p.swatch(302).click();
        for (let r = 0; r < p.file().rows; r++) { p.down(r, 0); p.move(r, p.file().columns - 1); p.up(r, p.file().columns - 1); }
        p.choose('erase');
    } else {
        p.choose(spec.tool);
        if (spec.swatch) p.swatch(spec.swatch).click();
    }
    return p;
}
// the cells that a stroke changes, in the field of the tool: Erase takes away what was there, Ground and Objects put
function run(spec, start, steps, rows, columns, prepare) {
    const p = setup(spec, rows, columns);
    if (prepare) prepare(p);
    return { p, cells: stroke(p, spec.layer, start, steps) };
}

// ---- A: the rule ----
function caseA() {
    const S = 'sc';
    const scenarios = [
        ['across to the right', [10, 10], [[10, 14, 's'], [10, 20, 's']], rowOf(10, 10, 20)],
        ['across to the left', [10, 30], [[10, 25, 's'], [10, 22, 's']], rowOf(10, 22, 30)],
        ['down', [10, 10], [[14, 10, 's'], [20, 10, 's']], colOf(10, 10, 20)],
        ['up', [20, 10], [[17, 10, 's'], [12, 10, 's']], colOf(10, 12, 20)],
        ['a diagonal move that is more across than down goes across', [10, 10], [[13, 15, 's']], rowOf(10, 10, 15)],
        ['a diagonal move that is more down than across goes down', [10, 10], [[15, 12, 's']], colOf(10, 10, 15)],
        ['a diagonal move to the up and left that is more across goes across', [10, 20], [[8, 12, 's']], rowOf(10, 12, 20)],
        ['a diagonal move to the up and left that is more up goes up', [20, 20], [[12, 18, 's']], colOf(20, 12, 20)],
        ['a tie (as far across as down) goes across', [10, 10], [[14, 14, 's']], rowOf(10, 10, 14)],
        ['a tie to the up and left goes across', [10, 10], [[6, 6, 's']], rowOf(10, 6, 10)],
        ['a tie of one cell goes across', [10, 10], [[11, 11, 's']], rowOf(10, 10, 11)],
        ['inside the start cell nothing but the start cell is painted', [10, 10], [[10, 10, 's', { fr: 0.1, fc: 0.9 }], [10, 10, 's', { fr: 0.95, fc: 0.05 }]], ['10,10']],
        ['a pointer that wanders in the start cell and then goes down is down', [10, 10], [[10, 10, 's', { fr: 0.1, fc: 0.9 }], [10, 10, 's', { fr: 0.9, fc: 0.2 }], [15, 10, 's']], colOf(10, 10, 15)],
        ['the line is kept: a long way down after going across stays across', [10, 10], [[10, 12, 's'], [20, 12, 's'], [20, 30, 's']], rowOf(10, 10, 30)],
        ['and a long way across after going down stays down', [10, 10], [[12, 10, 's'], [12, 30, 's'], [25, 30, 's']], colOf(10, 10, 25)],
        ['a hand that wobbles across a row stays on the row', [10, 10], [[11, 12, 's'], [9, 14, 's'], [10, 16, 's'], [12, 18, 's']], rowOf(10, 10, 18)],
        ['back to the start cell and on down keeps across', [10, 10], [[10, 15, 's'], [10, 10, 's'], [14, 10, 's']], rowOf(10, 10, 15)],
        ['back over the line to the other side of the start', [10, 20], [[10, 25, 's'], [10, 12, 's']], rowOf(10, 12, 25)],
        ['a stroke past the right edge stops at the last column', [10, 50], [[10, 70, 's']], rowOf(10, 50, COLUMNS - 1)],
        ['past the left edge', [10, 8], [[10, -9, 's']], rowOf(10, 0, 8)],
        ['past the bottom edge', [30, 20], [[60, 20, 's']], colOf(20, 30, ROWS - 1)],
        ['past the top edge', [5, 20], [[-6, 20, 's']], colOf(20, 0, 5)],
        ['going out of the map at the top keeps the column of the start', [0, 5], [[-4, 6, 's']], ['0,5']],
        ['the corner', [0, 0], [[-3, -5, 's']], ['0,0']],
        ['out of the map and back along the line', [10, 50], [[10, 80, 's'], [10, 40, 's']], rowOf(10, 40, 59)],
        ['no move at all is the start cell', [10, 10], [], ['10,10']]
    ];
    for (const spec of STROKES) {
        for (const [name, start, steps, want] of scenarios) {
            const got = run(spec, start, steps).cells;
            equal(Array.from(got), sorted(want), spec.name + ': ' + name);
            equal(model(ROWS, COLUMNS, start, steps), sorted(want), 'the rule written again says the same for: ' + name);
        }
    }
    // the status line follows the line, not the pointer, past the edge
    const p = setup(STROKES[0]);
    p.down(10, 50); p.move(10, 70, 's');
    check(p.where.textContent === 'Row 10, column 59', 'the line stops at the edge: the status line says ' + p.where.textContent);
    p.move(10, 55, 's');
    check(p.where.textContent === 'Row 10, column 55', 'and follows it back: ' + p.where.textContent);
    p.up(10, 55);
    p.down(30, 20); p.move(60, 20, 'c');
    check(p.where.textContent === 'Row 39, column 20', 'and past the bottom edge: ' + p.where.textContent);
    p.up(30, 20);
    // free strokes are what they were: the longer way, a diagonal, off the map ignored
    for (const spec of STROKES) {
        const free = [[10, 10], [[13, 15], [14, 25], [60, 25], [20, 20]]], got = run(spec, free[0], free[1]).cells;
        equal(Array.from(got), model(ROWS, COLUMNS, free[0], free[1]), spec.name + ': with no modifier a stroke is free, a diagonal stays a diagonal and a point off the map is ignored');
        const diag = Array.from(run(spec, [10, 10], [[15, 15]]).cells);
        equal(diag, sorted(freeLine([10, 10], [15, 15]).concat(['10,10'])), spec.name + ': a free diagonal');
    }
}

// ---- B: the modifiers ----
function caseB() {
    for (const spec of STROKES) {
        for (const [m, name] of [['s', 'Shift'], ['c', 'Ctrl'], ['sc', 'Shift and Ctrl together']]) {
            equal(Array.from(run(spec, [10, 10], [[13, 15, m]]).cells), sorted(rowOf(10, 10, 15)), spec.name + ': ' + name + ' keeps a stroke to a line');
            equal(Array.from(run(spec, [10, 10], [[15, 12, m]]).cells), sorted(colOf(10, 10, 15)), spec.name + ': ' + name + ' down');
        }
        for (const [m, name] of [['m', 'Command (or the Windows key)'], ['a', 'Alt'], ['', 'no key']]) {
            equal(Array.from(run(spec, [10, 10], [[13, 15, m]]).cells), sorted(freeLine([10, 10], [13, 15]).concat(['10,10'])), spec.name + ': ' + name + ' alone does not lock');
        }
        // held with Command: Shift and Ctrl still lock (Command is not what the rule reads, nor does it cancel it)
        equal(Array.from(run(spec, [10, 10], [[13, 15, 'sm']]).cells), sorted(rowOf(10, 10, 15)), spec.name + ': Shift with Command still locks');
        // the key is read at each move: let go in the middle, the stroke goes on free from where the line ended, to the pointer
        let steps = [[10, 15, 's'], [14, 17, '']], want = sorted(rowOf(10, 10, 15).concat(freeLine([10, 15], [14, 17])));
        equal(Array.from(run(spec, [10, 10], steps).cells), want, spec.name + ': Shift let go in the middle: the line to the pointer, then free');
        equal(model(ROWS, COLUMNS, [10, 10], steps), want, 'the rule says the same');
        // pressed in the middle: the stroke's place now is the new start, and the line goes on from there
        steps = [[10, 15, ''], [12, 17, ''], [15, 18, 's']];
        want = sorted(rowOf(10, 10, 15).concat(freeLine([10, 15], [12, 17]), colOf(17, 12, 15)));
        equal(Array.from(run(spec, [10, 10], steps).cells), want, spec.name + ': Shift pressed in the middle: a line down through the cell the stroke has reached (the first move picks the way)');
        equal(model(ROWS, COLUMNS, [10, 10], steps), want, 'the rule says the same');
        // pressed at the pointer's own cell: nothing yet, then the way chosen by the move
        steps = [[10, 15, ''], [10, 15, 's', { fr: 0.2, fc: 0.2 }], [10, 20, 's']];
        equal(Array.from(run(spec, [10, 10], steps).cells), sorted(rowOf(10, 10, 20)), spec.name + ': Shift pressed and the pointer still in its cell: nothing yet');
        // let go and pressed again: a new start, a new way
        steps = [[10, 15, 's'], [12, 17, ''], [16, 18, 's']];
        want = sorted(rowOf(10, 10, 15).concat(freeLine([10, 15], [12, 17]), colOf(17, 12, 16)));
        equal(Array.from(run(spec, [10, 10], steps).cells), want, spec.name + ': let go and pressed again: the lock starts again where the stroke is');
        equal(model(ROWS, COLUMNS, [10, 10], steps), want, 'the rule says the same');
        // pressed before the stroke: the first press is a cell, the line begins there
        const down = run(spec, [10, 10, 's'], [[10, 12, 's'], [14, 12, 's']]).cells;
        equal(Array.from(down), sorted(rowOf(10, 10, 12)), spec.name + ': Shift held at the press: the line begins at the cell that is pressed');
        // a new stroke is free again, and its own start
        const p = setup(spec);
        const a = stroke(p, spec.layer, [10, 10], [[10, 20, 's']]), b = stroke(p, spec.layer, [20, 30], [[24, 33]]);
        equal(Array.from(a), sorted(rowOf(10, 10, 20)), spec.name + ': the first stroke is a line');
        equal(Array.from(b).filter((x) => spec.tool === 'erase' || true).length > 0 && Array.from(b).sort().join('|'), sorted(freeLine([20, 30], [24, 33]).concat(['20,30'])).sort().join('|'), spec.name + ': the next stroke begins free (the lock does not outlive its stroke)');
        const c = stroke(p, spec.layer, [30, 40], [[34, 40, 's']]), d = stroke(p, spec.layer, [5, 5], [[9, 5, 's']]);
        equal(Array.from(c), sorted(colOf(40, 30, 34)), spec.name + ': a second locked stroke: its own start');
        equal(Array.from(d), sorted(colOf(5, 5, 9)), spec.name + ': and a third: its own start and its own way');
        // the lock is not left behind by a stroke that ended while it held (Shift, up, a new press that begins with Shift, a move the other way)
        const q = setup(spec);
        stroke(q, spec.layer, [10, 10], [[10, 20, 's']]);
        equal(Array.from(stroke(q, spec.layer, [25, 25, 's'], [[30, 25, 's']])), sorted(colOf(25, 25, 30)), spec.name + ': a stroke begun with the key held after a locked one: its own start and way');
    }
}

// ---- C: a zoomed and panned view ----
function caseC() {
    for (const spec of STROKES) {
        const p = setup(spec, 100, 100);
        p.zoom(4); p.pan(-170, -90);
        const v = p.view(), r0 = Math.floor(v.cy), c0 = Math.floor(v.cx);
        check(v.scale > 40 && Math.abs(v.cx - 50) > 0.5 && Math.abs(v.cy - 50) > 0.2, spec.name + ': the view is zoomed in and panned off the middle (' + v.scale.toFixed(1) + ' pixels, at ' + v.cy.toFixed(2) + ',' + v.cx.toFixed(2) + ')');
        const at = (dr, dc) => [r0 + dr, c0 + dc];
        let got = stroke(p, spec.layer, at(0, 0), [[...at(1, 3), 's'], [...at(2, 5), 's']]);
        equal(Array.from(got), sorted(rowOf(r0, c0, c0 + 5)), spec.name + ': across on a zoomed and panned view');
        got = stroke(p, spec.layer, at(-1, -2), [[...at(2, -2), 's'], [...at(3, -1), 's']]);
        equal(Array.from(got), sorted(colOf(c0 - 2, r0 - 1, r0 + 3)), spec.name + ': down');
        got = stroke(p, spec.layer, at(1, 1), [[...at(3, 3), 's']]);
        equal(Array.from(got), sorted(rowOf(r0 + 1, c0 + 1, c0 + 3)), spec.name + ': a tie across');
        // the rule is in cells, not pixels: the pointer moving about inside a cell, far from its middle, makes the same line
        const q = setup(spec, 100, 100); q.zoom(4); q.pan(-170, -90);
        const cornerBefore = q.layer(spec.layer);
        q.down(r0, c0, '', { fr: 0.95, fc: 0.9 }); q.move(r0, c0 + 4, 's', { fr: 0.05, fc: 0.1 }); q.move(r0 + 1, c0 + 4, 's', { fr: 0.4, fc: 0.8 }); q.up(r0, c0 + 4);
        const cornerMade = q.layer(spec.layer).map((t, i) => [t, i]).filter((x) => x[0] !== cornerBefore[x[1]]).map((x) => ((x[1] / 100) | 0) + ',' + (x[1] % 100));
        equal(sorted(cornerMade), sorted(rowOf(r0, c0, c0 + 4)), spec.name + ': a stroke from near the corner of a cell and a pointer near other corners makes the same line');
        // a pointer outside the canvas goes on to the cell it is over, in cells
        const s = setup(spec, 100, 100); s.zoom(4); s.pan(-170, -90);
        const w = s.canvas.state().width, o0 = Math.floor(s.view().cy), o1 = Math.floor(s.view().cx);
        const before = s.layer(spec.layer);
        s.down(o0, o1); s.moveTo(w + 400, 200, 's'); s.moveTo(w + 400, 340, 's'); s.up(o0, o1);
        const after = s.layer(spec.layer), made = [];
        for (let i = 0; i < after.length; i++) if (after[i] !== before[i]) made.push(((i / 100) | 0) + ',' + (i % 100));
        const rows = Array.from(new Set(made.map((x) => x.split(',')[0])));
        check(rows.length === 1 && made.length > 8, spec.name + ': a pointer far outside the canvas (and wandering up and down there) keeps the line, on one row: ' + made.length + ' cells in rows ' + rows.join());
        // up against the map's far edge on a zoomed view
        const e = setup(spec, 100, 100); e.zoom(4); e.pan(-9000, 0);
        const ev = e.view(), er = Math.floor(ev.cy), ec = Math.floor(ev.cx), last = 99;
        const edgeBefore = e.layer(spec.layer);
        e.down(er, ec); e.moveTo(e.canvas.state().width + 1200, e.pt(er, ec).clientY + 30, 's'); e.up(er, ec);
        const edgeAfter = e.layer(spec.layer), edgeMade = [];
        for (let i = 0; i < edgeAfter.length; i++) if (edgeAfter[i] !== edgeBefore[i]) edgeMade.push(((i / 100) | 0) + ',' + (i % 100));
        equal(sorted(edgeMade), sorted(rowOf(er, ec, last)), spec.name + ': zoomed in at the right edge of the map, a pointer far outside stops at the last column');
    }
}

// ---- D: the same bytes ----
function caseD() {
    // a locked stroke makes the same bytes as a free stroke along the same line, with every brush
    for (const spec of STROKES) {
        for (const brush of [1, 3, 5]) {
            const a = setup(spec), b = setup(spec);
            a.host.brushes.filter((x) => x.getAttribute('data-brush') === String(brush))[0].click(); b.host.brushes.filter((x) => x.getAttribute('data-brush') === String(brush))[0].click();
            stroke(a, spec.layer, [10, 10], [[12, 16, 's'], [13, 25, 's'], [20, 30, 's']]);
            stroke(b, spec.layer, [10, 10], [[10, 16], [10, 25], [10, 30]]);
            check(a.digest() === b.digest(), spec.name + ' with a brush of ' + brush + ': a locked stroke makes the bytes of a free stroke along the line');
            const c = setup(spec), d = setup(spec);
            c.host.brushes.filter((x) => x.getAttribute('data-brush') === String(brush))[0].click(); d.host.brushes.filter((x) => x.getAttribute('data-brush') === String(brush))[0].click();
            stroke(c, spec.layer, [10, 10], [[15, 12, 'c'], [25, 13, 'c'], [30, 10, 'c']]);
            stroke(d, spec.layer, [10, 10], [[15, 10], [25, 10], [30, 10]]);
            check(c.digest() === d.digest(), spec.name + ' with a brush of ' + brush + ': and down, with Ctrl');
            // undo: one stroke, one step, byte for byte
            const e = setup(spec), start = JSON.stringify(e.layer(spec.layer));
            const steps = e.tools.state().history.strokes;
            stroke(e, spec.layer, [10, 10], [[10, 20, 's']]);
            check(JSON.stringify(e.layer(spec.layer)) !== start && e.tools.state().history.strokes === steps + 1, spec.name + ': the line is one step of the history');
            e.doc.key('z', { ctrlKey: true });
            check(JSON.stringify(e.layer(spec.layer)) === start && e.tools.state().history.strokes === steps, spec.name + ': one Undo takes the whole line back (the tiles of the layer, as before)');
        }
    }
    // Auto edges
    {
        const make = () => {
            const p = rig(40, 60);
            const f = p.file(); Edit.ensureTile(f, 487, 'd01a');
            p.tools.setMap(p.file());
            p.tools.use(Ground, null); p.tools.useEdges(Edges);
            p.choose('ground'); p.swatch(487).click();
            p.host.autoEdges[0].click();
            return p;
        };
        const a = make(), b = make();
        check(a.tools.state().autoEdges === true, 'Auto edges is on');
        stroke(a, 1, [10, 10], [[11, 18, 's'], [14, 30, 's']]);
        stroke(b, 1, [10, 10], [[10, 18], [10, 30]]);
        check(a.digest() === b.digest(), 'Auto edges: a locked stroke makes the bytes (the edge pieces too) of a free stroke along the line');
        const c = make(), d = make();
        stroke(c, 1, [10, 10], [[18, 11, 'c'], [30, 14, 'c']]);
        stroke(d, 1, [10, 10], [[18, 10], [30, 10]]);
        check(c.digest() === d.digest(), 'Auto edges: and down');
        const free = make(); stroke(free, 1, [10, 10], [[15, 15], [15, 25]]);
        check(free.digest() !== a.digest(), 'a free stroke with Auto edges is another picture (the check can tell)');
    }
    // Fill is one click: a modifier held changes nothing, and a drag afterwards looks around
    for (const m of ['', 's', 'c']) {
        const p = setup(STROKES[0]);
        p.doc.key('b');
        p.down(10, 10, m); p.move(10, 30, m); p.up(10, 30, m);
        const filled = p.layer(1).every((t) => t === 487);
        check(filled && p.node.attrs['data-stroke'] === undefined, 'Fill with "' + m + '" held: the field is filled, and nothing goes on');
    }
    // no modifier: the same bytes as a free stroke for a mouse, a pen and a finger; a finger with Shift held (a keyboard on a tablet) is a line
    for (const spec of STROKES) {
        const results = {};
        for (const type of ['mouse', 'pen', 'touch']) {
            const p = setup(spec, 40, 60, { type });
            p.down(10, 10); p.move(10, 12); p.move(13, 15); p.move(18, 16); p.up(18, 16);
            results[type] = p.digest();
        }
        check(results.mouse === results.pen && results.mouse === results.touch, spec.name + ': with no modifier a mouse, a pen and a finger paint the same bytes');
        const f = setup(spec, 40, 60, { type: 'touch' });
        f.down(10, 10); f.move(10, 12); f.move(13, 15); f.move(18, 16); f.up(18, 16);
        const g = setup(spec, 40, 60);
        g.down(10, 10); g.move(10, 12); g.move(13, 15); g.move(18, 16); g.up(18, 16);
        check(f.digest() === g.digest(), spec.name + ': a finger is a free stroke');
        const t = setup(spec, 40, 60, { type: 'touch' }), tcells = stroke(t, spec.layer, [10, 10], [[10, 12, 's'], [13, 15, 's'], [18, 16, 's']]);
        equal(Array.from(tcells), sorted(rowOf(10, 10, 16)), spec.name + ': a finger with Shift held from a keyboard is a line too');
        // a pen and a mouse the same
        const pn = setup(spec, 40, 60, { type: 'pen' }), pcells = stroke(pn, spec.layer, [10, 10], [[13, 15, 'c']]);
        equal(Array.from(pcells), sorted(rowOf(10, 10, 15)), spec.name + ': a pen with Ctrl held is a line');
    }
    // a second pointer takes the stroke back, a lock or not
    {
        const p = setup(STROKES[0]);
        p.down(10, 10, 's'); p.move(10, 20, 's');
        p.node.fire('pointerdown', { pointerType: 'mouse', pointerId: 8, button: 0, buttons: 1, shiftKey: true, clientX: 300, clientY: 200, preventDefault() {} });
        check(p.node.attrs['data-stroke'] === undefined && p.layer(1).every((t) => t === 485), 'a second pointer takes a locked stroke back, as it takes a free one');
    }
}

// ---- E: what is not a stroke of this kind ----
function caseE() {
    // the painter says which tools are strokes of this kind (the canvas asks it, once a stroke goes on)
    const cases = [['ground', null, true], ['objects', null, true], ['erase', null, true], ['objects', (p) => p.swatch(404).click(), false], ['bigger', null, false], ['hills', null, false], ['food', null, false], ['ants', null, false],
        ['pickup', null, false], ['pan', null, false]];
    const paintFor = (id, prep) => { const p = rig(ROWS, COLUMNS, { ants: fakeAnts([]), pick: fakePick([]) }); p.choose(id); if (prep) prep(p); return p; };
    for (const [id, prep, want] of cases) {
        const p = paintFor(id, prep);
        check(p.painter().straight() === want, id + (prep ? ' (a plant)' : '') + ': straight() is ' + want);
        if (!want) {
            const before = p.digest(), plain = (m) => { const q = paintFor(id, prep); q.down(10, 10); q.move(13, 18, m); q.move(20, 20, m); q.up(20, 20, m); return q; };
            const a = plain(''), b = plain('s'), c = plain('c');
            check(JSON.stringify(a.view()) === JSON.stringify(b.view()) && JSON.stringify(a.view()) === JSON.stringify(c.view()) && a.digest() === b.digest() && a.digest() === c.digest(), id + (prep ? ' (a plant)' : '') + ': Shift or Ctrl held through a drag changes nothing (bytes and view as with no key)');
            check(a.digest() === before, id + ': and a drag puts nothing');
        }
    }
    // the tap tools put with a tap, with or without a key
    for (const m of ['', 's', 'c']) {
        for (const [id, prep] of [['hills', null], ['food', null], ['bigger', null], ['objects', (p) => p.swatch(404).click()], ['ants', null]]) {
            const p = rig(ROWS, COLUMNS); p.choose(id); if (prep) prep(p);
            const q = rig(ROWS, COLUMNS); q.choose(id); if (prep) prep(q);
            p.down(10, 10, m); p.up(10, 10, m);
            q.down(10, 10, ''); q.up(10, 10, '');
            check(p.digest() === q.digest() && p.digest() !== rig(ROWS, COLUMNS).digest(), id + ': a tap with "' + m + '" held puts the thing, as with no key');
        }
    }
    // the Ants box (a drag of a box) and Pick up (a drag of a thing): the tool is handed the same cells, with and without a key
    for (const [id, make] of [['ants', (log) => ({ ants: fakeAnts(log) })], ['pickup', (log) => ({ pick: fakePick(log) })]]) {
        const runs = {};
        for (const m of ['', 's', 'c', 'sc']) {
            const log = [], p = rig(ROWS, COLUMNS, make(log));
            p.choose(id);
            p.down(10, 10, m); p.move(10, 10, m, { fr: 0.2, fc: 0.2 }); p.move(13, 15, m); p.move(14, 14, m); p.move(20, 16, m); p.up(20, 16, m);
            runs[m] = log.join(' ');
        }
        check(runs[''].indexOf('begin') === 0 && runs[''].split('extend').length > 3, id + ': a drag reaches the tool (' + runs[''] + ')');
        check(runs['s'] === runs[''] && runs['c'] === runs[''] && runs['sc'] === runs[''], id + ': Shift or Ctrl changes what the tool is told: ' + runs['s'] + ' / ' + runs['']);
    }
}
// the parts that load on demand, as a stub: a painter that writes down what the canvas tells it
function fakeAnts(log) {
    return { create: () => ({ setMap() {}, show() {}, refresh() {}, mine: () => true, painter: { size: () => 1, tapOnly: () => false, begin: (c) => { log.push('begin:' + c.row + ',' + c.column); return true; }, extend: (a, b) => log.push('extend:' + a.row + ',' + a.column + '>' + b.row + ',' + b.column), end: () => log.push('end'), cancel: () => log.push('cancel') } }) };
}
function fakePick(log) {
    return { create: () => ({ setMap() {}, show() {}, refresh() {}, painter: { size: () => 0, canMove: () => true, tapOnly: () => false, begin: (c) => { log.push('begin:' + c.row + ',' + c.column); return true; }, extend: (a, b) => log.push('extend:' + a.row + ',' + a.column + '>' + b.row + ',' + b.column), end: () => log.push('end'), cancel: () => log.push('cancel'), away() {} } }) };
}

const cases = [['A the rule', caseA], ['B the modifiers', caseB], ['C a zoomed and panned view', caseC], ['D the same bytes', caseD], ['E what is not a stroke of this kind', caseE]];
for (const [name, fn] of cases) {
    const before = failures;
    try { fn(); } catch (e) { failures++; console.log('    FAILED: ' + name + ' stopped: ' + (e && e.stack || e)); }
    console.log('RUNNING: ' + name + ' ... ' + (failures === before ? 'PASS' : 'FAIL'));
}
console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
