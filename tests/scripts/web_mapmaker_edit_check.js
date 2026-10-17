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
//   H  the canvas on a fake page: a stroke ends when its pointer is lost; a tap tool (Hills, Ants, Food) puts with a tap and a drag looks around
//   I  hills (mapmaker_blocks.js): a whole hill for a team, moved, over other things, in version 8 and 9, the dictionary, undo and redo, byte for byte
//   J  starting ants and food piles: the records of blocks 1 and 2, spliced exactly, everything else as it was; the eraser takes them away; one history with the strokes
//   K  Hills, Ants and Food on a fake page: the team swatches, the steppers, the sheets of a phone, the letters H, A and F, a tap that puts, undo
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Canvas = load('mapmaker_canvas.js'), Tiles = load('mapmaker_tiles.js'), Edit = load('mapmaker_edit.js'), Palette = load('mapmaker_palette.js'), Tools = load('mapmaker_tools.js'), Blocks = load('mapmaker_blocks.js');

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
    equal(Palette.TOOLS.filter((t) => t.draws).map((t) => t.id), ['ground', 'objects', 'erase'], 'the tools that paint with a brush');
    equal(Palette.TOOLS.filter((t) => t.places).map((t) => t.id), ['hills', 'ants', 'food'], 'the tools that put a whole thing with a tap');
    check(Palette.TOOLS.every((t) => Palette.usable(t.id)) && !Palette.usable('nothing'), 'every tool can be chosen');
    equal(Palette.TOOLS.map((t) => t.id), ['ground', 'objects', 'hills', 'ants', 'food', 'erase', 'move'], 'the seven tools in the order of the picture');
    check(Palette.toolOfKey('G').id === 'ground' && Palette.toolOfKey('x') === null, 'a letter finds its tool, in either case');
    check(Palette.paints('ground') && Palette.paints('erase') && !Palette.paints('move') && !Palette.paints('hills'), 'which tools paint');
    check(Palette.places('hills') && Palette.places('ants') && Palette.places('food') && !Palette.places('ground') && !Palette.places('erase'), 'which tools put a whole thing');
    equal(Palette.TOOLS.map((t) => t.key), ['g', 'o', 'h', 'a', 'f', 'e', 'm'], 'the letters of the tools (B, the two brackets and the keys of the canvas are not among them)');
    const teams = Palette.teams();
    equal(teams.map((t) => t.label), ['Green', 'Red', 'Blue', 'Black'], 'the four teams in the order of the game\'s seats, by the game\'s names');
    check(teams.every((t) => t.color === Tiles.TEAMS[t.key] && t.color === Tiles.css(0, Blocks.TEAMS[t.id].hill)), 'each with the color that its hill has on the canvas');
    check(new Set(teams.map((t) => t.color)).size === 4, 'four different colors');
    // the names of the tiles that hills, ants and food put are the game's
    for (const t of Blocks.TEAMS) {
        check(names[t.hill] === t.hillName && names[t.marker] === t.markerName, t.label + ': the names of its hill (' + t.hill + ') and its start marker (' + t.marker + ') are the game\'s: ' + names[t.hill] + ', ' + names[t.marker]);
    }
    for (const id of Object.keys(Blocks.FOOD_NAMES)) check(names[+id] === Blocks.FOOD_NAMES[id], 'food tile ' + id + ' is ' + names[+id] + ' in the game\'s list, and ' + Blocks.FOOD_NAMES[id] + ' in the page');
    for (const n of Object.keys(Blocks.FOOD)) {
        const set = Blocks.FOOD[n], st = set.stages;
        check(st.length === +n && st[0][0] === set.units && st[st.length - 1][0] === 0 && st[st.length - 1][1] === 0x7FFE, n + ' stages: as many stages as asked, the first at all the units, the last empty at 0');
        check(st.every((s, i) => i === 0 || s[0] < st[i - 1][0]) && st.slice(0, -1).every((s) => Blocks.FOOD_NAMES[s[1]] !== undefined), n + ' stages: thresholds go down, and every tile has its name');
        check(set.points >= 10 && set.points <= 50 && set.units > 0, n + ' stages: a pile of ' + set.units + ' bites of ' + set.points + ' points');
    }
    check(Blocks.MIN_STAGES === 2 && Blocks.MAX_STAGES === 6 && Blocks.MIN_ANTS === 1 && Blocks.MAX_ANTS === 64, 'the ranges of the steppers');
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
        n.querySelectorAll = (sel) => n.all().filter((x) => sel.split(',').some((s) => (s[0] === '.' ? x.className.split(' ').indexOf(s.slice(1)) >= 0 : s === x.tagName)));
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
    const host = { tools: [], box: el('div'), title: el('h3'), swatches: el('div'), name: el('div'), tile: el('button'), hint: el('p'), brushes: [], fill: el('button'), undo: el('button'), redo: el('button'),
        stepper: el('div'), stepperLabel: el('span'), stepLess: el('button'), stepValue: el('output'), stepMore: el('button') };
    host.stepper.hidden = true;
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
    equal(p.host.tools.map((b) => [b.getAttribute('data-tool'), b.disabled]), [['ground', false], ['objects', false], ['hills', false], ['ants', false], ['food', false], ['erase', false], ['move', false]], 'all seven tools are on');
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
    p.stroking(true); p.button('hills').click(); p.stroking(false);
    check(p.tools.state().tool === 'move', 'a tool cannot be chosen while a stroke is going on');

    // keys
    const q = toolsPage(open(9, 30, 30));
    q.doc.key('o'); check(q.tools.state().tool === 'objects', 'the letter O chooses Objects');
    q.doc.key('E'); check(q.tools.state().tool === 'erase', 'E chooses Erase (either case)');
    q.doc.key('m'); check(q.tools.state().tool === 'move', 'M chooses Move');
    q.doc.key('h'); check(q.tools.state().tool === 'hills', 'H chooses Hills');
    q.doc.key('a'); check(q.tools.state().tool === 'ants', 'A chooses Ants');
    q.doc.key('F'); check(q.tools.state().tool === 'food', 'F chooses Food (either case)');
    q.doc.key('m');
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


// ---- H2: a tap tool on the canvas ----
function caseH2() {
    function tapPage() {
        const p = fakeCanvasPage();
        const calls = [];
        p.api.setPainter({ size: () => 4, tapOnly: () => true, door: () => true, begin: (c) => { calls.push('begin:' + c.row + ',' + c.column); return false; }, extend: () => calls.push('extend'), end: () => calls.push('end'), cancel: () => calls.push('cancel') });
        p.calls = calls;
        return p;
    }
    let p = tapPage();
    p.mouse('pointerdown'); p.mouse('pointerup', { buttons: 0 });
    check(p.calls.length === 1 && /^begin:\d+,\d+$/.test(p.calls[0]) && !p.api.isStroking(), 'a click of the mouse puts the thing where it was: one call, no stroke (' + p.calls.join() + ')');
    p = tapPage();
    p.mouse('pointerdown'); p.mouse('pointermove', { clientX: 340 }); p.mouse('pointerup', { clientX: 340, buttons: 0 });
    check(p.calls.length === 0, 'a mouse that is dragged looks around: nothing is put');
    p = tapPage();
    p.mouse('pointerdown', { pointerType: 'touch' }); p.mouse('pointerup', { pointerType: 'touch', buttons: 0 });
    check(p.calls.length === 1, 'a tap of a finger puts it');
    p = tapPage();
    p.mouse('pointerdown', { pointerType: 'touch' }); p.mouse('pointermove', { pointerType: 'touch', clientX: 360 }); p.mouse('pointerup', { pointerType: 'touch', clientX: 360, buttons: 0 });
    check(p.calls.length === 0, 'a finger that is dragged looks around: nothing is put');
    p = tapPage();
    p.mouse('pointerdown', { pointerType: 'touch', pointerId: 1 }); p.mouse('pointerdown', { pointerType: 'touch', pointerId: 2, clientX: 360 });
    p.mouse('pointerup', { pointerType: 'touch', pointerId: 2, buttons: 0, clientX: 360 }); p.mouse('pointerup', { pointerType: 'touch', pointerId: 1, buttons: 0 });
    check(p.calls.length === 0, 'two fingers (a pinch) put nothing');
    p = tapPage();
    p.mouse('pointerdown', { button: 1 }); p.mouse('pointerup', { button: 1, buttons: 0 });
    check(p.calls.length === 0, 'the middle button looks around and puts nothing');
    p = tapPage();
    p.canvas.fire('keydown', { key: ' ', ctrlKey: false, metaKey: false, altKey: false, preventDefault() {} });
    p.mouse('pointerdown'); p.mouse('pointerup', { buttons: 0 });
    check(p.calls.length === 0, 'the space key held down turns a click into looking around');
    p = tapPage();
    p.canvas.fire('keydown', { key: 'ArrowRight', ctrlKey: false, metaKey: false, altKey: false, shiftKey: false, preventDefault() {} });
    p.canvas.fire('keydown', { key: 'Enter', ctrlKey: false, metaKey: false, altKey: false, shiftKey: false, preventDefault() {} });
    check(p.calls.length === 1, 'Enter puts it at the marked square');
}

// ---- I: hills ----
// A made-up map with the lists of block 1 (records: [tile, row, column]) and block 2 (piles: {row, column, units, points, stages: [[threshold, tile]]}), written here without the page's code
function mapWith(version, rows, columns, names, records, piles) {
    const base = makeMap(version, rows, columns, names), f = File.parse(base);
    const parts = [], word = (v) => [v & 255, (v >> 8) & 255];
    parts.push(word(records.length)); records.forEach((r) => r.forEach((v) => parts.push(word(v))));
    parts.push(word(piles.length));
    piles.forEach((p) => { [p.row, p.column, p.units, p.points, p.stages.length].forEach((v) => parts.push(word(v))); p.stages.forEach((s) => s.forEach((v) => parts.push(word(v)))); });
    const lists = parts.reduce((a, w) => { a.push(w[0], w[1]); return a; }, []), out = new Uint8Array(f.blocksAt + lists.length + 8);
    out.set(base.subarray(0, f.blocksAt), 0); out.set(lists, f.blocksAt); out.set(base.subarray(f.blocksAt + 4), f.blocksAt + lists.length);
    return out;
}
function editorOn(bytes) {
    const file = File.parse(bytes);
    if (!file.ok) throw new Error(file.error);
    const ed = Edit.create(file, { rebase: (b) => File.parse(b), erase: Blocks.eraseAt });
    return ed;
}
// the cells of layer 2 as words, and everything else of a file: compared apart when a dictionary may have grown
function withoutNames(f) { return Array.from(f.bytes.subarray(0, f.dictAt)).join() + '|' + Array.from(f.bytes.subarray(f.dictAt + 2 + 11 * f.names)).join(); }
function cells2(f) { return Array.from(f.bytes.subarray(f.layer2, f.layer2 + 6 * f.cells)); }
function after(f) { return Array.from(f.bytes.subarray(f.blocksAt)); }
function hillFootprint(f, tile) {
    const out = [];
    for (let i = 0; i < f.cells; i++) if (Edit.tile(f, 2, i) === tile) out.push(i);
    return out;
}
function expectHill(f, tile, er, ec, tag) {
    const cells = hillFootprint(f, tile);
    check(cells.length === 16, 'the hill has 16 cells' + tag + ' (' + cells.length + ')');
    let whole = true;
    for (let r = er - 1; r <= er + 2; r++) for (let c = ec - 1; c <= ec + 2; c++) {
        const want = f.version === 8 ? (er | (ec << 8)) : (((er - r) & 255) | (((ec - c) & 255) << 8));
        const got = cellOf(f, 2, r, c);
        if (got[0] !== tile || got[1] !== (r === er && c === ec ? 1 : 0) || got[2] !== want) whole = false;
    }
    check(whole, 'the 4 x 4 cells round the entrance, each anchored at it, the entrance alone flagged' + tag);
}

function caseI() {
    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')';
        const bytes = makeMap(version, 20, 24, 486), ed = editorOn(bytes);
        for (let t = 0; t < 4; t++) {
            const team = Blocks.TEAMS[t], ch = Blocks.placeHill(ed.file, t, 4 + 8 * (t % 2), 4 + 10 * Math.floor(t / 2) + 2);
            check(!ch.error && ch.cells.length === 16 && ch.splices.length === 0, team.label + ': a hill is 16 cells and no list' + tag);
            check(ed.act(ch) !== null, team.label + ': it is put' + tag);
            expectHill(ed.file, team.hill, 4 + 8 * (t % 2), 4 + 10 * Math.floor(t / 2) + 2, ' of ' + team.label + tag);
        }
        check(ed.history.size().strokes === 4 && ed.file.names === 486, 'four hills are four steps of the history' + tag);
        check(ed.file.bytes[ed.file.dictAt + 2 + 11 * 245] === 66 && String.fromCharCode.apply(null, Array.from(ed.file.bytes.slice(ed.file.dictAt + 2 + 11 * 245, ed.file.dictAt + 2 + 11 * 245 + 9))) === 'BLACKHILL', 'the hill\'s tile is named in the dictionary (it was ".")' + tag);
        // moving: put again somewhere else, the first one goes
        const first = cells2(ed.file), mv = Blocks.placeHill(ed.file, 2, 12, 12);
        ed.act(mv);
        expectHill(ed.file, 246, 12, 12, ' moved' + tag);
        check(Edit.tile(ed.file, 2, 4 * 24 + 4 + 2) !== 246 && hillFootprint(ed.file, 246).indexOf(12 * 24 + 12) >= 0, 'the old place of the Blue hill is empty again' + tag);
        // the entrance cell is where the tap was; the others are around it
        equal(cellOf(ed.file, 2, 12, 12)[1], 1, 'the cell that was tapped is the entrance' + tag);
        equal(cellOf(ed.file, 2, 11, 11)[1], 0, 'and the cell before it is not' + tag);
        // the same place again changes nothing and is no step
        const steps = ed.history.size().strokes, same = Blocks.placeHill(ed.file, 2, 12, 12);
        check(ed.act(same) === null && ed.history.size().strokes === steps, 'a hill put where it is changes nothing and keeps no step' + tag);
        // put over another team's hill: it goes whole, over an object: the object goes whole
        const over = Blocks.placeHill(ed.file, 0, 12, 14);
        ed.act(over);
        check(hillFootprint(ed.file, 246).length === 0 && hillFootprint(ed.file, 248).length === 16, 'a hill put over another team\'s hill takes that one away whole (not half of it)' + tag);
        expectHill(ed.file, 248, 12, 14, ' of Green over it' + tag);
        // undo all the way: byte for byte the first states
        let n = 0;
        while (ed.history.canUndo()) { ed.undo(); n++; }
        const opened = File.parse(bytes);
        check(n === 6, 'six steps are undone: four hills, the move, and the one put over a hill' + tag);
        equal(cells2(ed.file), Array.from(opened.bytes.subarray(opened.layer2, opened.layer2 + 6 * 480)), 'undo puts back every cell of layer 2' + tag);
        while (ed.history.canRedo()) ed.redo();
        expectHill(ed.file, 248, 12, 14, ' redone' + tag);
        // refusals at the edge: the hill needs the cell before its entrance and two after it
        for (const [r, c, why] of [[0, 5, 'the top row'], [5, 0, 'the first column'], [18, 5, 'the second to last row'], [5, 22, 'the second to last column'], [19, 5, 'the last row']]) {
            const bad = Blocks.placeHill(ed.file, 1, r, c);
            check(typeof bad.error === 'string' && /no room/.test(bad.error) && !bad.cells, 'no hill with its entrance in ' + why + ': ' + (bad.error || '').slice(0, 40) + tag);
        }
        check(!Blocks.placeHill(ed.file, 1, 1, 1).error && !Blocks.placeHill(ed.file, 1, 17, 21).error, 'the nearest places to the edge that have room are accepted' + tag);
        // an object under the hill goes whole: a hill of Black on top of a hill of Red that sticks out of the footprint
        const g = editorOn(makeMap(version, 20, 24, 486));
        g.act(Blocks.placeHill(g.file, 1, 6, 6));
        put(g.file, 2, 6, 9, 302, 1, version === 8 ? (6 | (9 << 8)) : 0);
        const cover = Blocks.placeHill(g.file, 3, 8, 8);
        g.act(cover);
        check(hillFootprint(g.file, 247).length === 0 && Edit.tile(g.file, 2, 6 * 24 + 9) === 302 && hillFootprint(g.file, 245).length === 16, 'a hill that is put half over another goes over it whole, the object beside it stays' + tag);
        // a short dictionary grows by itself, and the page is told through the rebase hook
        let rebased = 0;
        const small = Edit.create(File.parse(makeMap(version, 20, 24, 100)), { rebase: (b) => { rebased++; return File.parse(b); }, erase: Blocks.eraseAt });
        small.act(Blocks.placeHill(small.file, 0, 5, 5));
        check(rebased >= 1 && small.file.names === 249 && hillFootprint(small.file, 248).length === 16, 'a dictionary that is too short grows to the hill\'s tile (' + small.file.names + ' names)' + tag);
        const named = String.fromCharCode.apply(null, Array.from(small.file.bytes.slice(small.file.dictAt + 2 + 11 * 248, small.file.dictAt + 2 + 11 * 248 + 9)));
        check(named === 'GREENHILL', 'with the game\'s name for it: ' + named + tag);
        small.undo();
        check(hillFootprint(small.file, 248).length === 0, 'and undo takes the hill away (the longer dictionary stays: it is harmless)' + tag);
    }
    // erase takes a hill whole through the eraser stroke (E2) and it can be put back where it was: the same cells
    const e = editorOn(makeMap(8, 14, 14, 486));
    e.act(Blocks.placeHill(e.file, 3, 6, 6));
    const once = cells2(e.file), st = e.begin('erase', null);
    check(st.stamp(7, 8, 1) === 16, 'the eraser on one cell of a hill takes its 16 cells');
    st.end();
    check(hillFootprint(e.file, 245).length === 0, 'and no cell is left');
    e.act(Blocks.placeHill(e.file, 3, 6, 6));
    equal(cells2(e.file), once, 'a hill taken away and put in the same place is the same cells again');
}

// ---- J: starting ants and food piles ----
function caseJ() {
    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')';
        // starting ants
        const rec = [[421, 3, 4], [152, 2, 2], [155, 9, 9], [152, 2, 3], [154, 18, 20]];
        const piles = [{ row: 5, column: 5, units: 10, points: 30, stages: [[10, 280], [0, 0x7FFE]] }];
        const base = mapWith(version, 20, 24, 486, rec, piles), ed = editorOn(base), f0 = ed.file;
        const lay = Blocks.layout(f0);
        check(lay.ok1 && lay.ok2 && lay.records.length === 5 && lay.piles.length === 1 && lay.piles[0].stages.length === 2, 'the lists of a map are read' + tag);
        const tail = Array.from(f0.bytes.subarray(f0.blocksAt + lay.b2.rel));
        let ch = Blocks.placeStart(f0, 3, 12, 12, 4);
        check(!ch.error && ch.cells.length === 0 && ch.splices.length === 1, 'ants: a change of block 1 only' + tag);
        ed.act(ch);
        let l2 = Blocks.layout(ed.file);
        const black = l2.records.filter((r) => r.tile === 152), others = l2.records.filter((r) => r.tile !== 152);
        check(black.length === 4 && black[0].row === 12 && black[0].column === 12, 'four ants of Black, the first on the cell that was tapped' + tag);
        check(new Set(black.map((r) => r.row + ',' + r.column)).size === 4 && black.every((r) => Math.abs(r.row - 12) <= 1 && Math.abs(r.column - 12) <= 1), 'all on different cells, next to each other' + tag);
        equal(black.slice(1).map((r) => [r.row, r.column]), [[11, 12], [12, 11], [12, 13]], 'the nearest cells first, by row and column (the same way every time)' + tag);
        equal(others.map((r) => [r.tile, r.row, r.column]), [[421, 3, 4], [155, 9, 9], [154, 18, 20]], 'the plant and the other teams\' ants are as they were, in their order' + tag);
        check(l2.b1.length === 2 + 6 * 7 && Array.from(ed.file.bytes.subarray(ed.file.blocksAt + l2.b2.rel)).join() === tail.join(), 'block 1 has its 7 records and the lists after it are byte for byte as they were' + tag);
        const placed = Array.from(ed.file.bytes);
        // moving: the team's ants go from where they were
        ed.act(Blocks.placeStart(ed.file, 3, 15, 3, 2));
        l2 = Blocks.layout(ed.file);
        equal(l2.records.filter((r) => r.tile === 152).map((r) => [r.row, r.column]), [[15, 3], [14, 3]], 'put again: two ants of Black in the new place and no other' + tag);
        check(l2.records.length === 5, 'and the others are still there: 5 records' + tag);
        // count is kept inside its range
        check(Blocks.layout((() => { const x = editorOn(base); x.act(Blocks.placeStart(x.file, 0, 10, 10, 500)); return x.file; })()).records.filter((r) => r.tile === 154).length === 64, 'at most 64 ants for a team, whatever number is asked' + tag);
        check(Blocks.layout((() => { const x = editorOn(base); x.act(Blocks.placeStart(x.file, 0, 10, 10, 0)); return x.file; })()).records.filter((r) => r.tile === 154).length === 1, 'and at least one (a team that is put has an ant)' + tag);
        // undo and redo
        ed.undo();
        check(Array.from(ed.file.bytes).join() === placed.join(), 'undo of the move gives the bytes of before, byte for byte' + tag);
        ed.undo();
        check(withoutNames(ed.file) === withoutNames(File.parse(base)), 'and of the first put: the map as it was opened (the name that the dictionary got stays: it is harmless)' + tag);
        ed.redo(); ed.redo();
        check(Blocks.layout(ed.file).records.filter((r) => r.tile === 152).length === 2, 'redo puts them back' + tag);
        // ants avoid objects, solid ground and the ants of another team
        const crowd = mapWith(version, 12, 12, 486, [[153, 5, 5]], []), c2 = editorOn(crowd);
        put(c2.file, 2, 5, 6, 302, 1, version === 8 ? (5 | (6 << 8)) : 0); put(c2.file, 1, 4, 5, 485, 1, 0xCDCD);
        c2.act(Blocks.placeStart(c2.file, 1, 5, 5, 5));
        const mine = Blocks.layout(c2.file).records.filter((r) => r.tile === 155).map((r) => r.row + ',' + r.column);
        check(mine.length === 5 && mine.indexOf('5,5') < 0 && mine.indexOf('5,6') < 0 && mine.indexOf('4,5') < 0, 'ants are not put on another team\'s ant, on an object or on solid ground: ' + mine.join(' ') + tag);
        const edge = editorOn(mapWith(version, 3, 3, 486, [], []));
        edge.act(Blocks.placeStart(edge.file, 0, 0, 0, 9));
        check(Blocks.layout(edge.file).records.length === 9, 'a corner of a small map: nine ants on its nine cells' + tag);
        edge.act(Blocks.placeStart(edge.file, 1, 1, 1, 30));
        check(Blocks.layout(edge.file).records.filter((r) => r.tile === 155).length === 0 || Blocks.layout(edge.file).records.filter((r) => r.tile === 155).length <= 9, 'a map with fewer cells than ants has fewer ants (' + tag + ')');
        // a dictionary that is too short grows for the start marker, and the lists keep their place (they are counted from the end of layer 2, which does not move)
        let grew = 0;
        const sm = Edit.create(File.parse(mapWith(version, 20, 24, 100, [[421, 3, 4]], [{ row: 5, column: 5, units: 10, points: 30, stages: [[10, 280], [0, 0x7FFE]] }])), { rebase: (b) => { grew++; return File.parse(b); }, erase: Blocks.eraseAt });
        sm.act(Blocks.placeStart(sm.file, 3, 6, 6, 4));
        sm.act(Blocks.placePile(sm.file, 9, 9, 3));
        check(grew >= 3 && sm.file.names === 266 && Blocks.layout(sm.file).records.length === 5 && Blocks.layout(sm.file).piles.length === 2, 'ants and a pile in a map with a dictionary of 100 names: it grows to the names that they need (' + sm.file.names + ')' + tag);
        sm.undo(); sm.undo();
        check(Blocks.layout(sm.file).records.length === 1 && Blocks.layout(sm.file).piles.length === 1 && Blocks.layout(sm.file).records[0].tile === 421, 'and undo takes them away though the dictionary stays longer' + tag);
        sm.redo(); sm.redo();
        check(Blocks.layout(sm.file).records.length === 5 && Blocks.layout(sm.file).piles.length === 2, 'and redo puts them back' + tag);
        // a list that cannot be read is not changed, and the page is told why
        const cut = File.parse(base.subarray(0, File.parse(base).blocksAt + 5));
        check(!Blocks.layout(cut).ok1 && /cannot be changed/.test(Blocks.placeStart(cut, 0, 3, 3, 4).error), 'a file that ends inside block 1: the ants are refused in words' + tag);

        // food
        for (let n = 2; n <= 6; n++) {
            const x = editorOn(mapWith(version, 20, 24, 486, [], []));
            const fc = Blocks.placePile(x.file, 7, 8, n);
            check(!fc.error && fc.splices.length === 1, n + ' stages: a change of block 2 only' + tag);
            x.act(fc);
            const lp = Blocks.layout(x.file), set = Blocks.FOOD[n];
            check(lp.piles.length === 1 && lp.piles[0].row === 7 && lp.piles[0].column === 8 && lp.piles[0].units === set.units && lp.piles[0].points === set.points && JSON.stringify(lp.piles[0].stages) === JSON.stringify(set.stages), n + ' stages: the pile in the list as the table says' + tag);
            check(set.stages.slice(0, -1).every((st) => String.fromCharCode.apply(null, Array.from(x.file.bytes.slice(x.file.dictAt + 2 + 11 * st[1], x.file.dictAt + 2 + 11 * st[1] + 3))) === Blocks.FOOD_NAMES[st[1]].slice(0, 3)), n + ' stages: every tile of it is named in the dictionary' + tag);
        }
        const y = editorOn(base), flist = Blocks.layout(y.file);
        y.act(Blocks.placePile(y.file, 9, 9, 4));
        y.act(Blocks.placePile(y.file, 5, 5, 6));                              // on the pile that is there: it is replaced
        let lp = Blocks.layout(y.file);
        check(lp.piles.length === 2 && lp.piles.filter((p) => p.row === 5 && p.column === 5).length === 1 && lp.piles.filter((p) => p.row === 5 && p.column === 5)[0].stages.length === 6, 'a pile put on a pile replaces it' + tag);
        check(Blocks.layout(y.file).records.length === 5 && flist.records.length === 5, 'block 1 is not touched by a pile' + tag);
        y.undo(); y.undo();
        check(Array.from(y.file.bytes).join() === Array.from(base).join() || Blocks.layout(y.file).piles.length === 1, 'two undos: back to the one pile of the file' + tag);
        // a pile that is in the file with the cell of its first stage in layer 2: erasing it takes both
        const z = editorOn(mapWith(version, 20, 24, 486, [], [{ row: 8, column: 8, units: 10, points: 30, stages: [[10, 280], [0, 0x7FFE]] }]));
        put(z.file, 2, 8, 8, 280, 1, version === 8 ? (8 | (8 << 8)) : 0);
        const gone = Blocks.eraseAt(z.file, 8 * 24 + 8);
        check(gone && gone.splices.length === 1 && gone.cells.length === 1, 'the eraser on a pile: the pile and its cell of layer 2' + tag);
        const es = z.begin('erase', null);
        check(es.stamp(8, 8, 1) >= 2, 'a stroke of the eraser takes the pile' + tag);
        es.end();
        check(Blocks.layout(z.file).piles.length === 0 && Edit.tile(z.file, 2, 8 * 24 + 8) === 0x7FFE, 'no pile and no cell' + tag);
        z.undo();
        check(Blocks.layout(z.file).piles.length === 1 && Edit.tile(z.file, 2, 8 * 24 + 8) === 280, 'undo gives both back' + tag);
        check(Blocks.eraseAt(z.file, 3 * 24 + 3) === null, 'the eraser on an empty cell has nothing more to take' + tag);
        // a pile with a bigger body (cells round its anchor): the eraser on another cell of the body takes the pile too
        put(z.file, 2, 7, 7, 280, 0, version === 8 ? (8 | (8 << 8)) : (((8 - 7) & 255) | (((8 - 7) & 255) << 8)));
        const body = z.begin('erase', null);
        body.stamp(7, 7, 1);
        body.end();
        check(Blocks.layout(z.file).piles.length === 0 && Edit.tile(z.file, 2, 7 * 24 + 7) === 0x7FFE && Edit.tile(z.file, 2, 8 * 24 + 8) === 0x7FFE, 'the eraser on a cell of the pile\'s body takes the whole pile' + tag);
        // a food list that stops where the original stops (a pile without stages) is not changed
        const bad = mapWith(version, 10, 10, 486, [], [{ row: 2, column: 2, units: 10, points: 30, stages: [] }]);
        const bf = File.parse(bad);
        check(!Blocks.layout(bf).ok2 && Blocks.layout(bf).ok1 && /cannot be changed/.test(Blocks.placePile(bf, 4, 4, 3).error), 'a food list with a pile that has no stages is refused in words, and its ants can still be changed' + tag);
        check(Blocks.placePile(File.parse(mapWith(version, 10, 10, 486, [], [])), 10, 4, 3).error !== undefined, 'a pile outside the map is refused' + tag);
        // one stroke of the eraser over a hill, a team's ants and a pile is one step
        const m = editorOn(mapWith(version, 20, 24, 486, [[152, 10, 10], [152, 10, 11]], [{ row: 12, column: 12, units: 10, points: 30, stages: [[10, 280], [0, 0x7FFE]] }]));
        m.act(Blocks.placeHill(m.file, 2, 5, 5));
        const stepsBefore = m.history.size().strokes, ref = Array.from(m.file.bytes), em = m.begin('erase', null);
        em.stamp(10, 10, 1); em.segment(10, 10, 12, 12, 1); em.stamp(6, 6, 1);
        em.end();
        check(m.history.size().strokes === stepsBefore + 1, 'one drag of the eraser over ants, a pile and a hill is one step of the history' + tag);
        check(Blocks.layout(m.file).records.length === 0 && Blocks.layout(m.file).piles.length === 0 && hillFootprint(m.file, 246).length === 0, 'all three are gone' + tag);
        m.undo();
        check(Array.from(m.file.bytes).join() === ref.join(), 'one undo gives all three back, byte for byte' + tag);
        m.redo();
        check(Blocks.layout(m.file).records.length === 0 && m.history.size().redo === 0, 'and redo takes them away again' + tag);
    }
}

// ---- the history is bounded for the lists too ----
function caseJ2() {
    const big = [];
    for (let i = 0; i < 65535; i++) big.push([152, i % 90, (i * 7) % 100]);
    const ed = editorOn(mapWith(8, 100, 100, 486, big, []));
    ed.act(Blocks.placeStart(ed.file, 3, 50, 50, 8));                       // takes out all 65,535 records of Black: 393 KB of bytes, in one patch
    const first = ed.history.size();
    check(first.strokes === 1 && first.records > 20000 && first.records <= Edit.MAX_RECORDS, 'a change that takes 65,535 records out is one step that weighs ' + first.records + ' records');
    for (let n = 0; n < 40; n++) { ed.act(Blocks.placeStart(ed.file, 3, 20 + n, 30, 8)); ed.undo(); ed.redo(); }
    const h = Edit.createHistory(), heavy = () => ({ keys: new Uint32Array(0), before: new Uint16Array(0), after: new Uint16Array(0), splices: [{ rel: 0, removed: new Uint8Array(393000), inserted: new Uint8Array(40) }] });
    for (let n = 0; n < 40; n++) h.push(heavy());
    const size = h.size();
    check(size.records <= Edit.MAX_RECORDS && size.strokes >= 1 && size.strokes < 40 && size.dropped > 0, 'the history of 40 such changes is bounded (' + size.strokes + ' steps, ' + size.records + ' records, ' + size.dropped + ' dropped): the bytes it keeps are at most about ' + Math.round(Edit.MAX_RECORDS * 16 / 1048576) + ' MB');
    const w = Edit.weight({ keys: new Uint32Array(3), splices: [{ rel: 0, removed: new Uint8Array(100), inserted: new Uint8Array(60) }] });
    check(w === 3 + 1 + 10, 'a splice weighs a record and one more for each 16 bytes it took out and put in (' + w + ')');
    check(Edit.weight({ keys: new Uint32Array(5) }) === 5, 'a patch of cells weighs a record for each');
    const one = editorOn(mapWith(8, 20, 20, 486, [], []));
    one.act(Blocks.placeStart(one.file, 0, 10, 10, 5));
    const p = one.begin('erase', null); p.stamp(10, 10, 1); const patch = p.finish();
    check(patch.splices && patch.splices.length === 1 && patch.splices[0].removed.length === 31 && patch.splices[0].inserted.length === 1, 'the patch of a splice holds the bytes it took out and put in, and nothing else of the file (' + patch.splices[0].removed.length + ' and ' + patch.splices[0].inserted.length + ' bytes)');
    p.cancel();
    // a change that does not move the count of a list is a splice that begins after what stays: the plant that is first, and the count word
    const two = editorOn(mapWith(8, 20, 20, 486, [[421, 1, 1]], []));
    two.act(Blocks.placeStart(two.file, 0, 10, 10, 4));
    const moved = Blocks.placeStart(two.file, 0, 15, 15, 4);
    check(moved.splices.length === 1 && moved.splices[0].rel >= 10 && moved.splices[0].removed.length <= 24 && moved.splices[0].inserted.length <= 24, 'moving 4 ants is a splice of the bytes that differ, after the plant and the count (' + moved.splices[0].rel + ', ' + moved.splices[0].removed.length + ' bytes)');
}

// ---- K: Hills, Ants and Food on a fake page ----
function caseK() {
    const bytes = mapWith(8, 24, 24, 486, [], []);
    const p = toolsPage(File.parse(bytes)), pt = p.painter();
    p.button('hills').click();
    check(p.tools.state().tool === 'hills' && p.host.title.textContent === 'Hills' && !p.host.box.hidden && !p.host.tile.hidden, 'Hills has a palette');
    check(p.host.swatches.children.length === 4 && p.host.swatches.className === 'swatches teams' && !p.host.swatches.hidden && p.host.stepper.hidden, 'of four teams, and no stepper');
    equal(p.host.swatches.children.map((b) => b.textContent), ['Green', 'Red', 'Blue', 'Black'], 'with their names');
    equal(p.host.swatches.children.map((b) => b.children[0].style.background), Palette.teams().map((t) => t.color), 'and their colors');
    check(p.host.swatches.children[0].getAttribute('aria-checked') === 'true' && p.host.swatches.children.filter((b) => b.getAttribute('aria-checked') === 'true').length === 1, 'one is chosen (Green)');
    check(p.host.tile.querySelector('span').textContent === 'Green' && p.host.name.textContent === 'Green: no hill yet', 'the phone\'s button says the team, and the line under the palette what the map has: ' + p.host.name.textContent);
    check(pt.size() === 4 && pt.tapOnly() && pt.door() && p.host.fill.disabled && p.brush(3).disabled, 'the square is 4 x 4 with the door marked; a tap puts it; no Fill and no brush');
    p.host.swatches.children[2].click();
    check(p.tools.state().team === 2 && p.host.tile.querySelector('span').textContent === 'Blue', 'a swatch chooses the team');
    check(pt.begin({ row: 5, column: 5 }) === false && p.log.edits === 1 && Edit.tile(p.file(), 2, 5 * 24 + 5) === 246 && hillFootprint(p.file(), 246).length === 16, 'a tap puts the Blue hill (the page is told)');
    check(p.host.name.textContent === 'Blue: has a hill' && !p.host.undo.disabled && !p.tools.isClean() && !p.tools.isSaved(), 'the line says so, Undo is on, and the map is not clean and not saved');
    check(p.log.dirty.length === 1 && p.log.dirty[0].length === 16, 'the canvas was told the 16 cells: ' + JSON.stringify(p.log.dirty.map((d) => d.length)));
    pt.begin({ row: 0, column: 0 });
    check(p.said.length === 1 && /^It cannot be put there: .*no room/.test(p.said[0]) && p.log.edits === 1, 'a place with no room is said in plain words and nothing changes: ' + p.said[0]);
    p.doc.key('z', { ctrlKey: true });
    check(hillFootprint(p.file(), 246).length === 0 && p.tools.isClean() && p.host.name.textContent === 'Blue: no hill yet', 'Control+Z takes the hill away; the map is clean again');
    p.doc.key('z', { ctrlKey: true, shiftKey: true });
    check(hillFootprint(p.file(), 246).length === 16, 'Control+Shift+Z puts it back');
    // the phone's sheet for the teams
    p.host.tile.click();
    const sheet = p.sheets[p.sheets.length - 1];
    check(sheet.spec.title === 'Hills' && sheet.body.children[0].children.length === 4 && sheet.body.children[0].children.every((b) => b.className === 'pick') && sheet.body.children.length === 1, 'the sheet of Hills has the four teams and no stepper');
    sheet.body.children[0].children[3].click();
    check(p.tools.state().team === 3 && sheet.ctl.closed && p.host.tile.querySelector('span').textContent === 'Black', 'a team of the sheet is chosen and the sheet closes');

    // Ants
    p.doc.key('a');
    check(p.tools.state().tool === 'ants' && p.host.title.textContent === 'Ants' && !p.host.stepper.hidden && p.host.swatches.children.length === 4, 'Ants: four teams and a stepper');
    check(p.host.stepperLabel.textContent === 'Ants at the start' && p.host.stepValue.textContent === '4' && p.host.tile.querySelector('span').textContent === 'Black · 4 ants', 'it says 4, and the phone\'s button says the team and the ants');
    check(pt.size() === 1 && pt.tapOnly() && !pt.door(), 'one cell marked');
    p.host.stepMore.click(); p.host.stepMore.click();
    check(p.tools.state().ants === 6 && p.host.stepValue.textContent === '6', 'the stepper makes it more');
    for (let i = 0; i < 80; i++) p.host.stepMore.click();
    check(p.tools.state().ants === 64 && p.host.stepMore.disabled && !p.host.stepLess.disabled, 'up to 64, where More is off');
    for (let i = 0; i < 80; i++) p.host.stepLess.click();
    check(p.tools.state().ants === 1 && p.host.stepLess.disabled && p.host.tile.querySelector('span').textContent === 'Black · 1 ant', 'down to 1, where Less is off ("1 ant")');
    p.host.stepMore.click(); p.host.stepMore.click();
    check(p.tools.state().ants === 3, 'back to 3');
    pt.begin({ row: 10, column: 10 });
    check(Blocks.layout(p.file()).records.length === 3 && p.host.name.textContent === 'Black: 3 ants' && p.log.rebased >= 1, 'a tap puts 3 ants of Black; the list is longer, and the page read the file again');
    p.doc.key('z', { ctrlKey: true });
    check(Blocks.layout(p.file()).records.length === 0 && p.host.name.textContent === 'Black: no ants yet', 'undo takes them away');
    p.host.tile.click();
    const asheet = p.sheets[p.sheets.length - 1];
    check(asheet.spec.title === 'Ants' && asheet.body.children.length === 3 && asheet.body.children[1].className === 'stepper sheet', 'the sheet of Ants has the teams, a stepper and a line');
    const stepper = asheet.body.children[1], more = stepper.children[1].children[2], less = stepper.children[1].children[0], val = stepper.children[1].children[1];
    more.click();
    check(p.tools.state().ants === 4 && val.textContent === '4' && p.host.stepValue.textContent === '4' && p.host.tile.querySelector('span').textContent === 'Black · 4 ants', 'the stepper of the sheet changes the same number, and the page\'s button follows');
    less.click(); less.click(); less.click(); less.click();
    check(p.tools.state().ants === 1 && less.disabled, 'down to 1 in the sheet');

    // Food
    p.doc.key('f');
    check(p.tools.state().tool === 'food' && p.host.title.textContent === 'Food' && p.host.swatches.hidden && !p.host.stepper.hidden, 'Food: no teams, a stepper');
    check(p.host.stepperLabel.textContent === 'Stages of the pile' && p.host.stepValue.textContent === '4' && p.host.tile.querySelector('span').textContent === '4 stages' && /^30 bites of 25 points\./.test(p.host.name.textContent), 'at 4 stages: ' + p.host.name.textContent);
    for (let i = 0; i < 9; i++) p.host.stepMore.click();
    check(p.tools.state().stages === 6 && p.host.stepMore.disabled, 'at most 6 stages');
    for (let i = 0; i < 9; i++) p.host.stepLess.click();
    check(p.tools.state().stages === 2 && p.host.stepLess.disabled, 'at least 2 stages (a pile that can be eaten)');
    p.host.stepMore.click();
    pt.begin({ row: 3, column: 3 });
    const piles = Blocks.layout(p.file()).piles;
    check(piles.length === 1 && piles[0].stages.length === 3 && piles[0].row === 3 && piles[0].column === 3 && /1 pile on the map/.test(p.host.name.textContent), 'a tap puts a pile of 3 stages: ' + p.host.name.textContent);
    p.host.tile.click();
    const fsheet = p.sheets[p.sheets.length - 1];
    check(fsheet.spec.title === 'Food' && fsheet.body.children[0].className === 'stepper sheet' && fsheet.body.children.length === 2, 'the sheet of Food has a stepper and its line');
    // the history is one: hill, ants, pile, in the order they were made
    p.button('hills').click(); p.host.swatches.children[0].click(); pt.begin({ row: 15, column: 15 });
    p.doc.key('z', { ctrlKey: true });
    check(hillFootprint(p.file(), 248).length === 0 && Blocks.layout(p.file()).piles.length === 1, 'undo takes the last thing made, whatever tool made it');
    p.doc.key('z', { ctrlKey: true });
    check(Blocks.layout(p.file()).piles.length === 0, 'the next undo the pile before it');
    // Erase takes ants and piles in a stroke through the painter
    p.button('ants').click(); pt.begin({ row: 20, column: 20 });
    p.button('erase').click();
    check(pt.size() === 1 && pt.begin({ row: 20, column: 20 }) === true, 'Erase begins a stroke on ants');
    pt.end();
    check(Blocks.layout(p.file()).records.length === 0, 'the ants of the team are gone');
    // a hill that stands there already, though the dictionary of the map does not know its tile: put again, the cells do not change, the dictionary gets the name, and the page is told
    const known = File.parse(mapWith(8, 24, 24, 100, [], []));
    for (let r = 4; r <= 7; r++) for (let c = 4; c <= 7; c++) put(known, 2, r, c, 248, r === 5 && c === 5 ? 1 : 0, 5 | (5 << 8));
    const q = toolsPage(known), edits = q.log.edits;
    q.button('hills').click();
    q.painter().begin({ row: 5, column: 5 });
    check(q.file().names === 249 && q.log.edits === edits + 1 && q.tools.state().history.strokes === 0 && q.tools.isClean(), 'a hill put where it is, in a dictionary that does not know its tile: no step, but the name is written and the page is told');
    p.button('erase').click();
    check(p.host.hint.children[0].textContent.indexOf('team’s ants') > 0 && p.host.hint.children[1].textContent.length > 0, 'Erase says that it takes ants, hills and piles too');
}

const cases = [['A the brush and the line', caseA], ['A2 anchors', anchors], ['B strokes', caseB], ['C fill', caseC], ['D undo and redo', caseD], ['E the dictionary', caseE], ['F the palette', caseF], ['G the tools on a fake page', caseG], ['H the canvas on a fake page', caseH], ['H2 a tap tool on the canvas', caseH2], ['I hills', caseI], ['J starting ants and food piles', caseJ], ['J2 the history of the lists is bounded', caseJ2], ['K Hills, Ants and Food on a fake page', caseK]];
for (const [name, fn] of cases) {
    const before = failures;
    try { fn(); } catch (e) { failures++; console.log('    FAILED: ' + name + ' stopped: ' + (e && e.stack || e)); }
    console.log('RUNNING: ' + name + ' ... ' + (failures === before ? 'PASS' : 'FAIL'));
}
console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
