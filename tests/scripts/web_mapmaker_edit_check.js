#!/usr/bin/env node
// The map maker's drawing (web/front/mapmaker_edit.js, mapmaker_palette.js, mapmaker_tools.js; docs/MAP_EDITOR.md) run on small made-up maps and on a small fake of the DOM, with no
// browser and no map core:
//
//   node tests/scripts/web_mapmaker_edit_check.js [--front DIR]
//
//   --front DIR  the folder of the mapmaker_*.js files to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
//   A  the brush and the line between two cells (the arithmetic of a stroke)
//   B  a stroke on the ground, on objects and with the eraser: what each word of a cell becomes, in version 8 and version 9; a bigger object (a hill) goes whole
//   C  fill: the region is the one a plain search finds, it stops at another tile, a field that has objects in it is not filled over
//   D  undo and redo: every state comes back byte for byte; a patch holds only the cells that changed; the history is bounded
//   E  the dictionary: the name a tile needs, in place or by growing it, and a map that is read again after it grew
//   F  the palette: the names are those of the game's list (src/ants_assets/lvl_tile_names.inc), the colors those of the canvas
//   G  the tools on a fake page: a tool chosen, a palette, the keys, a stroke that goes through the painter, undo and redo, Fill, the brush
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Canvas = load('mapmaker_canvas.js'), Tiles = load('mapmaker_tiles.js'), Edit = load('mapmaker_edit.js'), Palette = load('mapmaker_palette.js'), Tools = load('mapmaker_tools.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }
function digest(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }

// A made-up map: version 8 or 9, `names` names in the dictionary ('.' but the ground 485), every layer-1 cell plain ground, layer 2 empty, the blocks empty
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
function open(version, rows, columns, names) { const f = File.parse(makeMap(version, rows, columns, names)); if (!f.ok) throw new Error(f.error); return f; }
function cellOf(f, layer, r, c) { const i = r * f.columns + c; return [Edit.tile(f, layer, i), Edit.flags(f, layer, i), Edit.third(f, layer, i)]; }
function put(f, layer, r, c, t, fl, a) { const p = (layer === 1 ? f.layer1 : f.layer2) + 6 * (r * f.columns + c); [t, fl, a].forEach((v, k) => { f.bytes[p + 2 * k] = v & 255; f.bytes[p + 2 * k + 1] = v >> 8; }); }

// a hill as the checker wants it: 4 x 4 cells of the tile, each anchored at the entrance (the second cell of the second row), the entrance flagged
function putHill(f, top, left, tile) {
    const er = top + 1, ec = left + 1;
    for (let r = top; r < top + 4; r++) for (let c = left; c < left + 4; c++) {
        const word = f.version === 8 ? (er | (ec << 8)) : (((er - r) & 255) | (((ec - c) & 255) << 8));
        put(f, 2, r, c, tile, r === er && c === ec ? 1 : 0, word);
    }
}

// ---- A ----
function caseA() {
    const f = open(8, 20, 30);
    equal(Edit.brushRect(f, 5, 5, 1), { r0: 5, r1: 5, c0: 5, c1: 5 }, 'a brush of 1 is the cell');
    equal(Edit.brushRect(f, 5, 5, 3), { r0: 4, r1: 6, c0: 4, c1: 6 }, 'a brush of 3 is a square round the cell');
    equal(Edit.brushRect(f, 5, 5, 9), { r0: 1, r1: 9, c0: 1, c1: 9 }, 'a brush of 9 reaches 4 cells each way');
    equal(Edit.brushRect(f, 0, 0, 5), { r0: 0, r1: 2, c0: 0, c1: 2 }, 'a brush at the corner is cut by the edge');
    equal(Edit.brushRect(f, 19, 29, 3), { r0: 18, r1: 19, c0: 28, c1: 29 }, 'and at the far corner');
    equal(Edit.brushRect(f, 5, 5, 2), { r0: 5, r1: 6, c0: 5, c1: 6 }, 'an even brush leans down and right');
    equal(Edit.BRUSHES, [1, 3, 5, 9], 'the brush sizes');
    equal(Edit.lineCells(2, 2, 2, 2), [], 'no line from a cell to itself');
    equal(Edit.lineCells(0, 0, 0, 4), [[0, 1], [0, 2], [0, 3], [0, 4]], 'a horizontal line, the first cell not counted');
    equal(Edit.lineCells(3, 3, 0, 0), [[2, 2], [1, 1], [0, 0]], 'a diagonal line backwards');
    let ok = true, joined = true;
    for (let n = 0; n < 400; n++) {
        const r0 = (n * 7) % 20, c0 = (n * 13) % 30, r1 = (n * 11 + 3) % 20, c1 = (n * 17 + 5) % 30, line = Edit.lineCells(r0, c0, r1, c1);
        if (line.length !== Math.max(Math.abs(r1 - r0), Math.abs(c1 - c0))) ok = false;
        let pr = r0, pc = c0;
        for (const [r, c] of line) { if (Math.abs(r - pr) > 1 || Math.abs(c - pc) > 1) joined = false; pr = r; pc = c; }
        if (line.length && (pr !== r1 || pc !== c1)) ok = false;
    }
    check(ok, 'a line has as many cells as its longer side and ends at the target');
    check(joined, 'and has no gap: every cell touches the one before it');
}

// ---- B ----
function caseB() {
    for (const version of [8, 9]) {
        const f = open(version, 12, 16), ed = Edit.create(f), tag = ' (version ' + version + ')';
        let s = ed.begin('ground', 487);
        check(s.stamp(3, 4, 1) === 1, 'a brush of 1 changes one cell' + tag);
        equal(cellOf(f, 1, 3, 4), [487, 0, 0xCDCD], 'the ground changes its tile and keeps its other words' + tag);
        check(s.stamp(3, 4, 1) === 0, 'the same tile again changes nothing' + tag);
        check(s.stamp(6, 6, 3) === 9, 'a brush of 3 changes nine cells' + tag);
        check(s.end() === 10, 'the stroke ends with its ten cells' + tag);
        equal(ed.history.size().strokes, 1, 'one stroke in the history' + tag);

        s = ed.begin('objects', 302);
        s.stamp(2, 2, 1);
        equal(cellOf(f, 2, 2, 2), [302, 1, version === 8 ? (2 | (2 << 8)) : 0], 'an object is its own anchor, flagged as the origin' + tag);
        s.stamp(0, 5, 3);
        s.end();
        check(Edit.objectCells(f, 2 * 16 + 2).length === 1, 'and it is one cell' + tag);

        s = ed.begin('erase', null);
        check(s.stamp(2, 2, 1) === 1, 'the eraser takes an object away' + tag);
        equal(cellOf(f, 2, 2, 2), [0x7FFE, 0, 0], 'to the empty cell of a blank map' + tag);
        check(s.stamp(2, 2, 1) === 0 && s.stamp(10, 10, 1) === 0, 'and an empty cell stays as it is' + tag);
        equal(cellOf(f, 1, 3, 4), [487, 0, 0xCDCD], 'the ground is not touched by the eraser' + tag);
        s.end();

        // a hill goes whole
        putHill(f, 5, 8, 246);
        check(Edit.objectCells(f, 6 * 16 + 9).length === 16 && Edit.objectCells(f, 5 * 16 + 8).length === 16, 'the 16 cells of a hill are one object, from any of its cells' + tag);
        s = ed.begin('erase', null);
        check(s.stamp(8 - 2, 11, 1) === 16, 'the eraser on one cell of a hill takes all 16' + tag);
        let left = 0;
        for (let r = 0; r < 12; r++) for (let c = 0; c < 16; c++) if (Edit.tile(f, 2, r * 16 + c) === 246) left++;
        check(left === 0, 'no cell of the hill is left' + tag);
        s.end();
        putHill(f, 5, 8, 246);
        s = ed.begin('objects', 303);
        s.stamp(7, 10, 1);
        let hill = 0;
        for (let r = 0; r < 12; r++) for (let c = 0; c < 16; c++) if (Edit.tile(f, 2, r * 16 + c) === 246) hill++;
        check(hill === 0 && Edit.tile(f, 2, 7 * 16 + 10) === 303, 'an object put on a hill takes the hill away whole' + tag);
        s.end();
    }
    // an object with an anchor that is nowhere near stands alone (some old maps are like that), and so does one whose anchor cell is another object
    const f = open(8, 10, 10), ed = Edit.create(f);
    put(f, 2, 0, 0, 62, 1, 0x0000); put(f, 2, 1, 1, 62, 1, 0x0000);        // two power-ups that both name the cell (0, 0) as their anchor
    check(Edit.objectCells(f, 0).length === 2, 'the quirk: a power-up that names itself as the anchor, with another that names it too, are two cells of one object');
    put(f, 2, 3, 3, 62, 1, 0xFFFF); put(f, 2, 4, 4, 62, 0, 0x0505);
    equal(Edit.objectCells(f, 3 * 10 + 3), [33], 'an anchor outside the map: the cell stands alone');
    equal(Edit.objectCells(f, 4 * 10 + 4), [44], 'a cell that names itself without the origin flag stands alone');
    equal(Edit.objectCells(f, 5), [], 'an empty cell has no object');
    // an object that is not as a stroke of the page would write it is made so (the same tile alone is not enough: its flag and its anchor count)
    put(f, 2, 6, 6, 302, 0, 0x0000);
    const fixer = ed.begin('objects', 302);
    check(fixer.stamp(6, 6, 1) === 1 && cellOf(f, 2, 6, 6)[1] === 1 && cellOf(f, 2, 6, 6)[2] === (6 | (6 << 8)), 'a cell of the same object tile with no flag and another anchor is written again');
    fixer.end();
    // a cell that goes and comes back inside one stroke is not in the patch (and the stroke is not kept)
    const bounce = ed.begin('ground', 476);
    bounce.stamp(8, 8, 1);
    const wasHere = Edit.tile(f, 1, 8 * 10 + 8);
    Edit.applyPatch(f, { keys: Uint32Array.of((8 * 10 + 8) * 2), before: Uint16Array.of(wasHere, 0, 0xCDCD), after: Uint16Array.of(485, 0, 0xCDCD) }, true);
    check(bounce.finish().keys.length === 0 && bounce.end() === 0, 'a cell that is put back to its first words inside one stroke is not a record, and a stroke of no records is not kept');
    // a stroke taken back leaves the map as it was
    const before = digest(f.bytes), strokesBefore = ed.history.size().strokes, c = ed.begin('ground', 476);
    c.stamp(1, 1, 9); c.segment(1, 1, 8, 8, 5);
    const back = c.cancel();
    check(back.length > 0 && digest(f.bytes) === before && ed.history.size().strokes === strokesBefore, 'a cancelled stroke puts every cell back and keeps nothing');
    const e2 = ed.begin('ground', 485);
    const keptStrokes = ed.history.size().strokes;
    check(e2.stamp(2, 2, 1) === 0 && e2.end() === 0 && ed.history.size().strokes === keptStrokes, 'painting the tile that is there changes nothing and keeps no stroke');
    const g = ed.begin('ground', 476);
    g.stamp(5, 5, 1);
    Edit.applyPatch(f, { keys: Uint32Array.of((5 * 10 + 5) * 2), before: Uint16Array.of(485, 0, 0xCDCD), after: Uint16Array.of(476, 0, 0xCDCD) }, false);
    g.stamp(5, 5, 1);
    equal(g.end(), 1, 'a patch lists the cells once (a cell touched twice in a stroke is one record)');
}

// ---- anchors of a version 9 map that wraps ----
function anchors() {
    const small = open(9, 256, 4), t = (r, c) => r * 4 + c;
    put(small, 2, 250, 1, 62, 1, 10);                              // offset +10 rows, 0 columns: 260, which wraps to row 4 in a map of 256 x 256 or less
    equal(Edit.anchorIndex(small, t(250, 1)), t(4, 1), 'in a version 9 map of 256 x 256 or less the anchor wraps modulo 256');
    const wide = open(9, 10, 300);
    put(wide, 2, 3, 295, 62, 1, 10 << 8);                          // offset +10 columns: 305, outside a map of 300 columns, and no wrap in a map that is wider than 256
    equal(Edit.anchorIndex(wide, 3 * 300 + 295), -1, 'in a wider map it does not wrap');
    const v8 = open(8, 20, 20);
    put(v8, 2, 3, 4, 62, 1, 7 | (9 << 8));
    equal(Edit.anchorIndex(v8, 3 * 20 + 4), 7 * 20 + 9, 'in a version 8 map the anchor is the row in the low byte and the column in the high byte');
}

// ---- C ----
function naiveRegion(f, layer, start) {
    const want = Edit.tile(f, layer, start), seen = new Set([start]), queue = [start];
    while (queue.length) {
        const i = queue.shift(), r = Math.floor(i / f.columns), c = i % f.columns;
        for (const [dr, dc] of [[0, 1], [0, -1], [1, 0], [-1, 0]]) {
            const nr = r + dr, nc = c + dc;
            if (nr < 0 || nc < 0 || nr >= f.rows || nc >= f.columns) continue;
            const j = nr * f.columns + nc;
            if (!seen.has(j) && Edit.tile(f, layer, j) === want) { seen.add(j); queue.push(j); }
        }
    }
    return seen;
}
function caseC() {
    const f = open(8, 14, 18), ed = Edit.create(f);
    // a wall of water across the ground, with a gap
    for (let c = 0; c < 18; c++) if (c !== 12) put(f, 1, 7, c, 476, 0, 0xCDCD);
    const upper = naiveRegion(f, 1, 0);
    let s = ed.begin('ground', 487);
    check(s.fill(0, 0) === upper.size, 'fill changes the cells that a plain search finds (' + upper.size + ')');
    let same = true;
    for (let i = 0; i < f.cells; i++) if ((Edit.tile(f, 1, i) === 487) !== upper.has(i)) same = false;
    check(same, 'and only those: the cells of the wall are not changed');
    s.end();
    // the gap joins the two sides
    s = ed.begin('ground', 439);
    const all = naiveRegion(f, 1, 13 * 18 + 17);
    s.fill(13, 17);
    let n = 0;
    for (let i = 0; i < f.cells; i++) if (Edit.tile(f, 1, i) === 439) n++;
    check(n === all.size, 'a region joined through a gap is filled through it');
    s.end();
    // objects: an empty field
    s = ed.begin('objects', 304);
    const empties = naiveRegion(f, 2, 0);
    check(s.fill(0, 0) === empties.size && empties.size === f.cells, 'filling empty cells puts the object on all of them (' + f.cells + ')');
    s.end();
    s = ed.begin('objects', 305);
    check(s.fill(0, 0) === 0, 'a field that has an object in it is not filled over');
    check(s.end() === 0 && Edit.tile(f, 2, 0) === 304, 'the objects stay as they were');
    s = ed.begin('erase', null);
    check(s.fill(0, 0) === 0, 'the eraser has no fill');
    // a bigger object is not a field
    const g = open(8, 12, 12), eg = Edit.create(g);
    putHill(g, 2, 2, 247);
    const t = eg.begin('objects', 302);
    check(t.fill(3, 3) === 0, 'fill on a hill does nothing');
    equal(cellOf(g, 2, 3, 3), [247, 1, 3 | (3 << 8)], 'and leaves the hill as it is');
    const big = open(9, 512, 512), eb = Edit.create(big), t0 = Date.now(), sb = eb.begin('ground', 476);
    check(sb.fill(0, 0) === 512 * 512, 'a fill of all of a 512 x 512 map');
    const took = Date.now() - t0;
    check(took < 1500, 'takes ' + took + ' ms');
    const patch = sb.finish();
    check(patch.keys.length === 512 * 512 && patch.before.length === 3 * 512 * 512, 'its patch holds one record for each cell, three words before and three after');
}

// ---- D ----
function caseD() {
    const f = open(9, 40, 50), ed = Edit.create(f);
    const states = [digest(f.bytes)];
    const kinds = [['ground', 487], ['objects', 302], ['ground', 571], ['erase', null], ['objects', 63], ['ground', 476]];
    for (let n = 0; n < 30; n++) {
        const [kind, tile] = kinds[n % kinds.length], s = ed.begin(kind, tile), r = (n * 7) % 40, c = (n * 11) % 50;
        s.stamp(r, c, [1, 3, 5, 9][n % 4]);
        s.segment(r, c, (r + 9) % 40, (c + 17) % 50, [1, 3][n % 2]);
        if (s.end()) states.push(digest(f.bytes));
    }
    equal(ed.history.size().strokes, states.length - 1, 'every stroke that changed a cell is in the history');
    let back = true;
    for (let i = states.length - 2; i >= 0; i--) { ed.undo(); if (digest(f.bytes) !== states[i]) back = false; }
    check(back, 'undo brings back every earlier state, byte for byte');
    check(!ed.history.canUndo() && ed.history.canRedo() && ed.history.isClean(), 'at the start there is nothing to undo, and the map is clean');
    let forward = true;
    for (let i = 1; i < states.length; i++) { ed.redo(); if (digest(f.bytes) !== states[i]) forward = false; }
    check(forward, 'redo brings every later state back');
    check(!ed.history.isClean(), 'a map with strokes in its history is not clean');
    ed.history.markSaved();
    check(ed.history.isSaved() && !ed.history.isClean(), 'a map that was handed over is saved, though it is not clean');
    ed.undo();
    check(!ed.history.isSaved(), 'an undo after that makes it unsaved again');
    ed.redo();
    check(ed.history.isSaved(), 'and the redo makes it the saved map again');
    ed.undo();
    const s = ed.begin('ground', 523);
    s.stamp(0, 0, 1);
    s.end();
    check(!ed.history.canRedo(), 'a new stroke ends the redo line');
    check(!ed.history.isSaved(), 'and it makes a saved map unsaved: even when it brings the number of strokes back to what it was');
    check(ed.undo() !== null && ed.undo() !== null, 'undo goes on after it');
    // more strokes than the history keeps: undo goes back as far as it can, and the map is still not the one that was opened
    const long = Edit.create(open(9, 30, 30)), longStart = digest(long.file.bytes);
    for (let n = 0; n < Edit.MAX_STROKES + 5; n++) { const t = long.begin('ground', n % 2 ? 476 : 487); t.stamp(Math.floor(n / 30), n % 30, 1); t.end(); }
    while (long.undo()) { /* all that are kept */ }
    check(long.history.size().dropped === 5 && !long.history.canUndo(), 'after ' + (Edit.MAX_STROKES + 5) + ' strokes the history holds ' + Edit.MAX_STROKES + ' and says it dropped 5');
    check(!long.history.isClean() && digest(long.file.bytes) !== longStart, 'and a map undone as far as it goes is not called clean: Download would skip the core, and nothing would ask before it is left');
    const kept = Edit.create(open(9, 30, 30));
    for (let n = 0; n < Edit.MAX_STROKES + 2; n++) { const t = kept.begin('ground', n % 2 ? 476 : 487); t.stamp(Math.floor(n / 30), n % 30, 1); t.end(); }
    kept.history.markSaved();
    const extra = kept.begin('ground', 523); extra.stamp(2, 2, 1); extra.end();
    kept.undo();
    check(!kept.history.isSaved(), 'a stroke that pushed an old one out of the history makes a saved map unsaved, even when it is undone again');
    // bounded
    const h = Edit.createHistory(), mk = (n) => ({ keys: new Uint32Array(n), before: new Uint16Array(3 * n), after: new Uint16Array(3 * n) });
    for (let i = 0; i < Edit.MAX_STROKES + 50; i++) h.push(mk(1));
    equal(h.size().strokes, Edit.MAX_STROKES, 'the history keeps at most ' + Edit.MAX_STROKES + ' strokes');
    check(h.size().dropped === 50 && !h.isClean(), 'and says that it dropped the oldest');
    const g = Edit.createHistory();
    for (let i = 0; i < 10; i++) g.push(mk(Math.floor(Edit.MAX_RECORDS / 4)));
    check(g.size().records <= Edit.MAX_RECORDS && g.size().strokes >= 3, 'it keeps at most ' + Edit.MAX_RECORDS + ' changed cells in all (' + g.size().records + ' in ' + g.size().strokes + ' strokes)');
    const one = Edit.createHistory();
    one.push(mk(Edit.MAX_RECORDS + 10));
    equal(one.size().strokes, 1, 'a single stroke bigger than that is still kept: it can be undone');
    // a patch is small
    const big = open(9, 512, 512), eb = Edit.create(big), s1 = eb.begin('ground', 487);
    s1.stamp(100, 100, 3);
    const p = s1.finish();
    check(p.keys.length === 9 && p.keys.byteLength + p.before.byteLength + p.after.byteLength === 9 * (4 + 6 + 6), 'a stroke of 9 cells on a 512 x 512 map is a patch of ' + (p.keys.byteLength + p.before.byteLength + p.after.byteLength) + ' bytes, not a copy of the map');
}

// ---- E ----
function caseE() {
    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')', f = open(version, 7, 9);
        let r = Edit.ensureTile(f, 485, 'g01a');
        check(!r.grew && !r.named && r.bytes === f.bytes, 'a tile that has its name is left as it is' + tag);
        r = Edit.ensureTile(f, 302, 'smlrock1');
        check(!r.grew && r.named && r.bytes === f.bytes, 'a tile inside the dictionary whose name is "." gets its name in place' + tag);
        const g = File.parse(r.bytes);
        check(g.ok && g.names === 486 && String.fromCharCode.apply(null, Array.from(r.bytes.slice(g.dictAt + 2 + 11 * 302, g.dictAt + 2 + 11 * 302 + 8))) === 'smlrock1' && r.bytes[g.dictAt + 2 + 11 * 302 + 8] === 0, 'with its name and the zero that ends it' + tag);
        put(g, 1, 3, 3, 485, 0, 0xCDCD);
        const before = digest(g.bytes.slice(g.layer1, g.layer2 + 6 * g.cells));
        const grown = Edit.ensureTile(g, 571, 's01a');
        check(grown.grew && grown.bytes.length === g.bytes.length + 11 * (572 - 486), 'a tile beyond the dictionary grows it by the names between and its own' + tag);
        const h = File.parse(grown.bytes);
        check(h.ok && h.names === 572 && h.rows === 7 && h.columns === 9 && h.kind === (version === 8 ? 'classic' : 'extended'), 'the longer file is read again with the same size' + tag);
        check(digest(h.bytes.slice(h.layer1, h.layer2 + 6 * h.cells)) === before, 'and the cells of both layers are as they were' + tag);
        const slot = (id) => Array.from(h.bytes.slice(h.dictAt + 2 + 11 * id, h.dictAt + 2 + 11 * id + 11));
        equal(slot(500), [46, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], 'a name in between is "."' + tag);
        equal(slot(571).slice(0, 5), [115, 48, 49, 97, 0], 'the new name is last' + tag);
        equal(slot(485).slice(0, 5), [103, 48, 49, 97, 0], 'the old names stay' + tag);
        check(h.bytes.length - (h.layer2 + 6 * h.cells) === g.bytes.length - (g.layer2 + 6 * g.cells), 'the blocks after the layers are as long as they were' + tag);
        check(Edit.ensureTile(h, Edit.MAX_NAMES, 'x').error !== undefined && Edit.ensureTile(h, Edit.MAX_NAMES - 1, 'x').grew, 'the dictionary has at most ' + Edit.MAX_NAMES + ' names' + tag);
    }
}

// ---- F ----
function caseF() {
    const names = fs.readFileSync(path.join(ROOT, 'src', 'ants_assets', 'lvl_tile_names.inc'), 'utf8').split('\n').filter((l) => l.startsWith('"')).map((l) => l.replace(/^"/, '').replace(/",?\s*$/, ''));
    check(names.length === 1344, 'the game\'s list has 1,344 names (' + names.length + ')');
    for (const row of Palette.GROUND.concat(Palette.OBJECTS)) check(names[row[0]] === row[1], 'tile ' + row[0] + ' is ' + names[row[0]] + ' in the game\'s list, and ' + row[1] + ' in the palette');
    for (const row of Palette.GROUND) check(row[0] >= 431 && row[0] <= 669, 'ground ' + row[1] + ' is a ground tile (ids 431 to 669)');
    for (const row of Palette.OBJECTS) check(row[0] < 431, 'object ' + row[1] + ' is an object tile');
    const ground = Palette.items('ground'), objects = Palette.items('objects');
    check(ground.length === 8 && objects.length === 11 && Palette.items('erase').length === 0 && Palette.items('move').length === 0, 'the palettes: 8 kinds of ground, 11 objects, none for the eraser and Move');
    check(ground.every((i) => i.color === Tiles.css(i.id, Tiles.EMPTY2)) && objects.every((i) => i.color === Tiles.css(0, i.id)), 'every swatch is the color the canvas draws that tile in');
    const keys = Palette.TOOLS.map((t) => t.key);
    check(new Set(keys).size === keys.length && keys.every((k) => k.length === 1), 'every tool has its own letter');
    equal(Palette.TOOLS.filter((t) => t.draws).map((t) => t.id), ['ground', 'objects', 'erase'], 'the tools that draw in this version');
    equal(Palette.TOOLS.map((t) => t.id), ['ground', 'objects', 'hills', 'ants', 'food', 'erase', 'move'], 'the seven tools in the order of the picture');
    check(Palette.toolOfKey('G').id === 'ground' && Palette.toolOfKey('x') === null, 'a letter finds its tool, in either case');
    check(Palette.paints('ground') && Palette.paints('erase') && !Palette.paints('move') && !Palette.paints('hills'), 'which tools paint');
    check(new Set(Palette.GROUND.concat(Palette.OBJECTS).map((r) => r[2])).size === 19, 'every label is different');
}

// ---- G: the tools on a fake page ----
function fakeDom() {
    const doc = { listeners: {}, activeElement: null };
    function node(tag) {
        const n = { tagName: tag, className: '', children: [], attrs: {}, listeners: {}, own: '', style: {}, disabled: false, hidden: false, tabIndex: 0, title: '', type: '' };
        n.appendChild = (c) => { n.children.push(c); return c; };
        n.setAttribute = (k, v) => { n.attrs[k] = String(v); };
        n.getAttribute = (k) => (k in n.attrs ? n.attrs[k] : null);
        n.addEventListener = (t, f) => { (n.listeners[t] = n.listeners[t] || []).push(f); };
        n.focus = () => { doc.activeElement = n; };
        n.click = () => { if (!n.disabled) for (const f of n.listeners.click || []) f({ type: 'click' }); };
        n.all = () => n.children.reduce((a, c) => a.concat([c], c.all()), []);
        n.querySelectorAll = (sel) => n.all().filter((x) => sel.split(',').some((s) => (s === '.swatch' ? x.className === 'swatch' : (s === '.pick' ? x.className === 'pick' : s === x.tagName))));
        n.querySelector = (sel) => {
            if (sel === '[aria-checked="true"]') return n.all().filter((x) => x.attrs['aria-checked'] === 'true')[0] || null;
            return n.querySelectorAll(sel)[0] || null;
        };
        Object.defineProperty(n, 'textContent', { get: () => n.own + n.children.map((c) => c.textContent).join(''), set: (v) => { n.own = String(v); n.children = []; } });
        return n;
    }
    doc.createElement = node;
    doc.addEventListener = (t, f) => { (doc.listeners[t] = doc.listeners[t] || []).push(f); };
    doc.key = (key, extra) => {
        const e = Object.assign({ type: 'keydown', key, ctrlKey: false, metaKey: false, altKey: false, shiftKey: false, target: doc.activeElement || node('body'), defaultPrevented: false, preventDefault() { this.defaultPrevented = true; } }, extra || {});
        for (const f of (doc.listeners.keydown || []).slice()) f(e);
        return e;
    };
    doc.body = node('body');
    return doc;
}

function toolsPage(file, opts) {
    opts = opts || {};
    const doc = fakeDom(), el = (tag) => doc.createElement(tag), log = { dirty: [], edits: 0, minis: 0, refreshes: 0, rebased: 0 };
    const host = { tools: [], box: el('div'), title: el('h3'), swatches: el('div'), name: el('div'), tile: el('button'), hint: el('p'), brushes: [], fill: el('button'), undo: el('button'), redo: el('button') };
    host.tile.appendChild(el('i')); host.tile.appendChild(el('span'));
    const sheets = [], said = [];
    for (const t of Palette.TOOLS) { const b = el('button'); b.setAttribute('data-tool', t.id); b.disabled = true; host.tools.push(b); }
    for (const n of [1, 3, 5, 9]) { const b = el('button'); b.setAttribute('data-brush', String(n)); host.brushes.push(b); }
    host.box.hidden = true;
    let current = file, stroking = false, painter = null, busy = false;
    const canvas = {
        setPainter: (p) => { painter = p; }, refresh: () => { log.refreshes++; }, markDirty: (list) => { log.dirty.push(list.slice()); }, refreshMini: () => { log.minis++; },
        isStroking: () => stroking
    };
    const tools = Tools.create({
        doc, win: {}, canvas, host, file: () => current, busy: () => busy,
        dialogs: { open: (spec) => { const body = el('div'), ctl = { closed: false, close() { ctl.closed = true; } }; spec.build(body, ctl); sheets.push({ spec, body, ctl }); return ctl; } }, say: (title, text) => said.push(title + ': ' + text),
        rebase: (bytes) => { log.rebased++; current = File.parse(bytes); return current; }, onEdit: () => { log.edits++; }
    });
    tools.setMap(current);
    return { doc, host, tools, sheets, said, painter: () => painter, log, file: () => current, busy: (v) => { busy = v; }, stroking: (v) => { stroking = v; }, button: (id) => host.tools.filter((b) => b.getAttribute('data-tool') === id)[0], brush: (n) => host.brushes.filter((b) => b.getAttribute('data-brush') === String(n))[0] };
}

function caseG() {
    const p = toolsPage(open(9, 30, 30));
    equal(p.host.tools.map((b) => [b.getAttribute('data-tool'), b.disabled]), [['ground', false], ['objects', false], ['hills', true], ['ants', true], ['food', true], ['erase', false], ['move', false]], 'Ground, Objects, Erase and Move are on; Hills, Ants and Food wait');
    check(p.button('ground').getAttribute('aria-pressed') === 'true' && p.button('move').getAttribute('aria-pressed') === 'false', 'Ground is the tool at the start');
    check(!p.host.box.hidden && !p.host.tile.hidden && p.host.swatches.children.length === 8 && p.host.title.textContent === 'Ground', 'with its palette of 8');
    check(p.host.tile.querySelector('span').textContent === 'Grass' && p.host.tile.querySelector('i').style.background === Tiles.css(485, Tiles.EMPTY2), 'the phone\'s palette button shows the tile that is chosen');
    p.host.tile.click();
    check(p.sheets.length === 1 && p.sheets[0].spec.title === 'Ground' && p.sheets[0].body.children[0].children.length === 8, 'and opens the palette as a sheet of 8 tiles with their names');
    p.sheets[0].body.children[0].children[4].click();
    check(p.tools.state().ground === 571 && p.sheets[0].ctl.closed && p.host.tile.querySelector('span').textContent === 'Sand', 'a tile of the sheet is chosen and the sheet closes');
    p.host.swatches.children[0].click();
    check(p.host.swatches.children.filter((b) => b.getAttribute('aria-checked') === 'true').length === 1 && p.host.name.textContent === 'Grass', 'one swatch chosen, and its name said');
    check(p.host.undo.disabled && p.host.redo.disabled && !p.host.fill.disabled && !p.brush(3).disabled, 'undo and redo are off with nothing drawn; Fill and the brushes are on');
    p.host.swatches.children[2].click();
    check(p.tools.state().ground === 487 && p.host.name.textContent === 'Dirt', 'a swatch chooses a tile and its name shows');
    p.button('objects').click();
    check(p.host.swatches.children.length === 11 && p.host.title.textContent === 'Objects' && p.tools.state().tool === 'objects', 'Objects has its own palette of 11');
    check(p.tools.state().ground === 487, 'and the ground that was chosen is kept');
    p.button('erase').click();
    check(p.host.box.hidden && p.host.tile.hidden && p.host.fill.disabled && p.brush(5).disabled === false, 'Erase has no palette (nor the phone\'s palette button) and no Fill, and keeps the brushes');
    p.button('move').click();
    check(p.host.box.hidden && p.painter().size() === 0 && p.brush(5).disabled && p.host.fill.disabled, 'Move paints nothing: no brush, no fill');
    p.button('hills').click();
    check(p.tools.state().tool === 'move', 'a tool that is not there cannot be chosen');

    // keys
    const q = toolsPage(open(9, 30, 30));
    q.doc.key('o'); check(q.tools.state().tool === 'objects', 'the letter O chooses Objects');
    q.doc.key('E'); check(q.tools.state().tool === 'erase', 'E chooses Erase (either case)');
    q.doc.key('m'); check(q.tools.state().tool === 'move', 'M chooses Move');
    q.doc.key('h'); check(q.tools.state().tool === 'move', 'H, which is not there yet, does nothing');
    q.doc.key('g'); check(q.tools.state().tool === 'ground', 'G chooses Ground');
    q.doc.key(']'); check(q.tools.state().brush === 3, 'the right bracket makes the brush bigger');
    q.doc.key(']'); q.doc.key(']'); q.doc.key(']'); check(q.tools.state().brush === 9, 'up to 9');
    q.doc.key('['); check(q.tools.state().brush === 5, 'the left bracket makes it smaller');
    q.doc.key('b'); check(q.tools.state().fill === true && q.host.fill.getAttribute('aria-pressed') === 'true', 'B turns Fill on');
    q.doc.key('b'); check(q.tools.state().fill === false, 'and off');
    const typing = q.doc.createElement('input');
    q.doc.key('o', { target: typing });
    check(q.tools.state().tool === 'ground', 'a letter typed into a box is not a tool');
    q.busy(true); q.doc.key('o'); q.busy(false);
    check(q.tools.state().tool === 'ground', 'nor while a dialog is open');
    const ctrl = q.doc.key('g', { ctrlKey: true });
    check(!ctrl.defaultPrevented && q.tools.state().tool === 'ground', 'Control with a letter is not a tool');

    // a stroke through the painter, undo and redo
    const r = toolsPage(open(9, 30, 30)), pt = r.painter(), start = digest(r.file().bytes);
    r.host.swatches.children[2].click();                                    // dirt (487), which the dictionary does not have yet
    check(pt.size() === 1, 'the painter says the brush is 1');
    check(pt.begin({ row: 5, column: 5 }) === true, 'a press begins a stroke that goes on');
    check(r.log.rebased === 1 && r.file().names === 488, 'a tile beyond the dictionary grew it first (' + r.file().names + ' names)');
    pt.extend({ row: 5, column: 5 }, { row: 5, column: 10 });
    pt.end();
    check([5, 6, 7, 8, 9, 10].every((c) => Edit.tile(r.file(), 1, 5 * 30 + c) === 487), 'the line is painted');
    check(r.log.dirty.length === 2 && r.log.dirty[1].length === 5, 'the canvas was told which cells changed: ' + JSON.stringify(r.log.dirty.map((d) => d.length)));
    check(r.log.minis === 1 && !r.host.undo.disabled && r.host.redo.disabled && !r.tools.isClean(), 'Undo is on, the map is not clean');
    const after = digest(r.file().bytes);
    r.doc.key('z', { ctrlKey: true });
    check(Edit.tile(r.file(), 1, 5 * 30 + 7) === 485 && r.host.undo.disabled && !r.host.redo.disabled, 'Control+Z takes the stroke back');
    r.doc.key('z', { ctrlKey: true, shiftKey: true });
    check(digest(r.file().bytes) === after, 'Control+Shift+Z puts it back (byte for byte)');
    r.doc.key('z', { metaKey: true });
    r.doc.key('y', { ctrlKey: true });
    check(digest(r.file().bytes) === after, 'Command+Z and Control+Y work too');
    r.host.undo.click();
    check(r.host.redo.disabled === false && Edit.tile(r.file(), 1, 5 * 30 + 7) === 485, 'the Undo button works');
    r.host.redo.click();
    check(digest(r.file().bytes) === after, 'the Redo button works');
    r.stroking(true); r.doc.key('z', { ctrlKey: true }); r.stroking(false);
    check(digest(r.file().bytes) === after, 'undo waits while a stroke is going on');
    // fill: one stroke, and begin answers false (nothing goes on)
    const dirtyBefore = r.log.dirty.length;
    r.doc.key('z', { ctrlKey: true }); r.doc.key('z', { ctrlKey: true, shiftKey: true });
    check(r.log.dirty.length === dirtyBefore + 2 && r.log.dirty[dirtyBefore].length > 0, 'undo and redo tell the canvas which cells to paint again');
    r.doc.key('b');
    check(pt.begin({ row: 20, column: 20 }) === false, 'with Fill on a press fills and nothing goes on');
    check(r.tools.state().fill === false && r.host.fill.getAttribute('aria-pressed') === 'false', 'Fill is for one click: it is off again');
    r.doc.key('b');
    r.doc.key('b');
    check(Edit.tile(r.file(), 1, 25 * 30 + 25) === 487 && r.tools.state().history.strokes === 2, 'the whole field is dirt, in one more stroke');
    // a cancelled stroke
    const c0 = digest(r.file().bytes);
    pt.begin({ row: 1, column: 1 }); pt.extend({ row: 1, column: 1 }, { row: 3, column: 3 }); pt.cancel();
    check(digest(r.file().bytes) === c0 && r.tools.state().history.strokes === 2, 'a stroke that is cancelled leaves no trace');
    // a tile that only needs its name in the dictionary changes the map (the check must run again), though the stroke is taken back
    const w = toolsPage(open(9, 20, 20)), editsBefore = w.log.edits;
    w.button('objects').click();
    check(w.painter().begin({ row: 3, column: 3 }) === true, 'an object stroke begins');
    w.painter().cancel();
    check(w.log.edits === editsBefore + 1 && w.file().names === 486 && Edit.tile(w.file(), 2, 63) === 0x7FFE, 'a name written in the dictionary alone is a change that the page is told of (' + (w.log.edits - editsBefore) + ')');
    // a tile that the dictionary cannot hold: a plain message, no stroke
    Palette.GROUND.push([1400, 'zz', 'Test tile']);
    const z = toolsPage(open(9, 20, 20));
    z.host.swatches.children[z.host.swatches.children.length - 1].click();
    check(z.painter().begin({ row: 1, column: 1 }) === false && z.said.length === 1 && /cannot be used/.test(z.said[0]), 'a tile beyond the dictionary is said in plain words, and nothing is painted: ' + z.said[0]);
    Palette.GROUND.pop();
    // Erase through the painter; Move does not paint
    r.button('erase').click();
    check(pt.size() === 1 && pt.begin({ row: 2, column: 2 }) === true, 'Erase has a stroke');
    pt.end();
    r.button('move').click();
    check(pt.size() === 0 && pt.begin({ row: 2, column: 2 }) === false, 'Move has none');
    // the brush buttons
    r.button('ground').click();
    r.brush(5).click();
    check(r.tools.state().brush === 5 && r.brush(5).getAttribute('aria-checked') === 'true' && r.brush(1).getAttribute('aria-checked') === 'false' && pt.size() === 5, 'a brush button sets the size');
    check(start !== after, 'the map did change');
}

// ---- H: a stroke ends when its pointer is lost, not only when it lifts (the canvas on a fake page) ----
function fakeCanvasPage() {
    const ctx = new Proxy({}, { get: (t, k) => (k in t ? t[k] : (k === 'createImageData' ? (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }) : () => undefined)), set: (t, k, v) => { t[k] = v; return true; } });
    const listeners = () => { const l = {}; return { l, addEventListener: (t, f) => { (l[t] = l[t] || []).push(f); }, fire: (t, e) => (l[t] || []).forEach((f) => f(e)) }; };
    const canvasNode = () => Object.assign(listeners(), { width: 0, height: 0, attrs: {}, classes: {}, getContext: () => ctx, getBoundingClientRect: () => ({ left: 0, top: 0, width: 600, height: 400 }),
        setAttribute(k, v) { this.attrs[k] = String(v); }, removeAttribute(k) { delete this.attrs[k]; }, getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }, setPointerCapture() {}, focus() {}, classList: { toggle() {} }, hidden: false });
    const doc = Object.assign(listeners(), { createElement: canvasNode, visibilityState: 'visible' }), win = Object.assign(listeners(), { devicePixelRatio: 1, performance: { now: () => 0 } });
    const canvas = canvasNode(), log = [], plain = () => Object.assign(listeners(), { hidden: true, textContent: '', disabled: false });
    const api = Canvas.create({ doc, win, canvas, minimap: plain(), mini: canvasNode(), miniCap: plain(), where: plain(), zoomIn: plain(), zoomOut: plain() });
    const painter = { size: () => 1, begin: () => { log.push('begin'); return true; }, extend: () => log.push('extend'), end: () => log.push('end'), cancel: () => log.push('cancel') };
    api.setPainter(painter);
    api.setFile(open(9, 31, 31));
    const mouse = (type, extra) => canvas.fire(type, Object.assign({ pointerType: 'mouse', pointerId: 7, button: 0, buttons: 1, clientX: 300, clientY: 200, preventDefault() {} }, extra));
    return { api, canvas, doc, win, log, mouse };
}

function caseH() {
    let p = fakeCanvasPage();
    p.mouse('pointerdown');
    check(p.api.isStroking() && p.log.join() === 'begin', 'a press of the mouse begins a stroke');
    p.mouse('pointermove', { clientX: 330 });
    check(p.log.join() === 'begin,extend', 'a move with the button down goes on');
    p.mouse('pointerup');
    check(!p.api.isStroking() && p.log.join() === 'begin,extend,end', 'the button lifted ends it');
    p = fakeCanvasPage(); p.mouse('pointerdown'); p.mouse('pointermove', { buttons: 0, clientX: 340 });
    check(!p.api.isStroking() && p.log.join() === 'begin,end', 'a move with no button down (it was let go where the page could not hear) ends the stroke');
    p = fakeCanvasPage(); p.mouse('pointerdown'); p.mouse('lostpointercapture');
    check(!p.api.isStroking() && p.log.join() === 'begin,end', 'a pointer that is lost ends the stroke');
    p = fakeCanvasPage(); p.mouse('pointerdown'); p.win.fire('blur', {});
    check(!p.api.isStroking() && p.log.join() === 'begin,end', 'a window that loses the focus ends the stroke');
    p = fakeCanvasPage(); p.mouse('pointerdown'); p.doc.visibilityState = 'hidden'; p.doc.fire('visibilitychange', {});
    check(!p.api.isStroking() && p.log.join() === 'begin,end', 'a page that is hidden ends the stroke');
    p = fakeCanvasPage(); p.mouse('pointerdown'); p.mouse('pointerdown', { pointerId: 8 });
    check(!p.api.isStroking() && p.log.join() === 'begin,cancel', 'a second pointer takes the stroke back');
    p = fakeCanvasPage(); p.mouse('pointerdown', { button: 1 });
    check(!p.api.isStroking() && p.log.length === 0, 'the middle button looks around and does not paint');
    p = fakeCanvasPage(); p.mouse('pointerdown', { pointerType: 'touch', button: 0 });
    check(!p.api.isStroking() && p.log.length === 0, 'a finger that has only come down has not begun a stroke yet (it may be the first of two)');
    p.mouse('pointerup', { pointerType: 'touch' });
    check(p.log.join() === 'begin,end', 'a tap is a one-cell stroke');
}

const cases = [['A the brush and the line', caseA], ['A2 anchors', anchors], ['B strokes', caseB], ['C fill', caseC], ['D undo and redo', caseD], ['E the dictionary', caseE], ['F the palette', caseF], ['G the tools on a fake page', caseG], ['H the canvas on a fake page', caseH]];
for (const [name, fn] of cases) {
    const before = failures;
    try { fn(); } catch (e) { failures++; console.log('    FAILED: ' + name + ' stopped: ' + (e && e.stack || e)); }
    console.log('RUNNING: ' + name + ' ... ' + (failures === before ? 'PASS' : 'FAIL'));
}
console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
