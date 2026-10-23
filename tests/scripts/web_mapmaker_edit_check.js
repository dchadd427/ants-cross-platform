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
//   L  the Bigger tool on a fake page: the letter K, the one button and its sheet of 29 named objects, a tap that puts one whole, a hill in the way, Erase and undo, and the solid bit of a rock
//   M  the properties of the map (mapmaker_props.js) as ONE step of the history: splices of the header and the eggs, the layers that move when the description grows, undo and redo byte for byte
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Canvas = load('mapmaker_canvas.js'), Tiles = load('mapmaker_tiles.js'), Edit = load('mapmaker_edit.js'), Palette = load('mapmaker_palette.js'), Tools = load('mapmaker_tools.js'), Blocks = load('mapmaker_blocks.js'), Ground = load('mapmaker_ground.js'), Edges = load('mapmaker_edges.js');

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
    equal(Edit.BRUSHES, [1, 2, 3, 4, 5, 6, 7, 8, 9], 'the brush sizes');
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
        for (const bombId of [129, 130, 131, 132]) {
            const sb = ed.begin('objects', bombId); sb.stamp(0, 0, 1); sb.end();
            equal(cellOf(f, 2, 0, 0), [bombId, 1, 0x0101], 'bomb ' + bombId + ' has neutral owner 0x0101' + tag);
            check((cellOf(f, 1, 0, 0)[1] & 1) === 0 && Edit.anchorIndex(f, 0) === -1, 'bomb ' + bombId + ' is walkable and has no anchor' + tag);
            const se = ed.begin('erase', null); se.stamp(0, 0, 1); se.end();
        }
        const sf = ed.begin('objects', 134); sf.stamp(4, 4, 1); sf.end();
        check(cellOf(f, 2, 4, 4)[0] === 134 && (cellOf(f, 1, 4, 4)[1] & 1) === 1, 'fire wall 134 is solid' + tag);
        const sfe = ed.begin('erase', null); sfe.stamp(4, 4, 1); sfe.end();
        check((cellOf(f, 1, 4, 4)[1] & 1) === 0, 'erasing fire wall clears solid bit' + tag);

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
    for (const row of Palette.GROUND.concat(Palette.OBJECTS, Palette.PLANTS)) check(names[row[0]] === row[1], 'tile ' + row[0] + ' is ' + names[row[0]] + ' in the game\'s list, and ' + row[1] + ' in the palette');
    for (const row of Palette.GROUND) check(row[0] >= 431 && row[0] <= 669, 'ground ' + row[1] + ' is a ground tile (ids 431 to 669)');
    for (const row of Palette.OBJECTS.concat(Palette.PLANTS)) check(row[0] < 431, 'object ' + row[1] + ' is an object tile');
    const ground = Palette.items('ground'), objects = Palette.items('objects');
    check(ground.length === 5 && objects.length === 30 && Palette.items('plants').length === 14 && Palette.items('erase').length === 0 && Palette.items('move').length === 0, 'the palettes: 5 kinds of ground, 30 objects (with 14 plants, 4 bombs and fire wall), none for the eraser and Move');
    equal(ground.map((i) => [i.id, i.name, i.label, i.kind]), [[485, 'g01a', 'Gravel', 'gravel'], [487, 'd01a', 'Dirt', 'dirt'], [571, 's01a', 'Slate', 'slate'], [439, 'M01a', 'Mud', 'mud'], [476, 'w01a', 'Water', 'water']], 'the five kinds of ground and the plain tile of each: no grass, no sand');
    check(ground.every((i) => i.color === Tiles.css(i.id, Tiles.EMPTY2)) && objects.every((i) => i.color === Tiles.css(0, i.id)), 'every swatch is the color the canvas draws that tile in');
    const keys = Palette.TOOLS.map((t) => t.key);
    check(new Set(keys).size === keys.length && keys.every((k) => k.length === 1), 'every tool has its own letter');
    equal(Palette.TOOLS.filter((t) => t.draws).map((t) => t.id), ['ground', 'objects', 'erase'], 'the tools that paint with a brush');
    equal(Palette.TOOLS.filter((t) => t.places).map((t) => t.id), ['bigger', 'hills', 'ants', 'food'], 'the tools that put a whole thing with a tap');
    check(Palette.TOOLS.every((t) => Palette.usable(t.id)) && !Palette.usable('nothing'), 'every tool can be chosen');
    equal(Palette.TOOLS.map((t) => t.id), ['ground', 'objects', 'bigger', 'hills', 'ants', 'food', 'erase', 'pickup', 'pan'], 'the nine tools: the seven of the picture, Bigger after Objects, and Pick up and Pan (the old Move) last');
    check(Palette.toolOfKey('G').id === 'ground' && Palette.toolOfKey('x') === null, 'a letter finds its tool, in either case');
    check(Palette.paints('ground') && Palette.paints('erase') && !Palette.paints('pan') && !Palette.paints('pickup') && !Palette.paints('hills') && !Palette.places('pickup') && Palette.usable('pickup') && Palette.usable('pan') && !Palette.usable('move'), 'which tools paint');
    check(Palette.places('hills') && Palette.places('ants') && Palette.places('food') && !Palette.places('ground') && !Palette.places('erase'), 'which tools put a whole thing');
    equal(Palette.TOOLS.map((t) => t.key), ['g', 'o', 'k', 'h', 'a', 'f', 'e', 'p', 'm'], 'the letters of the tools (B, the two brackets and the keys of the canvas are not among them)');
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
    check(new Set(Palette.GROUND.concat(Palette.OBJECTS, Palette.PLANTS).map((r) => r[2])).size === 35, 'every label is different');
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
    const autoBtn = el('button'), autoMob = el('button');
    autoBtn.appendChild(el('span')); autoBtn.appendChild(el('span'));
    autoMob.appendChild(el('span')); autoMob.appendChild(el('span'));
    const host = { tools: [], box: el('div'), title: el('h3'), swatches: el('div'), name: el('div'), tile: el('button'), hint: el('p'), brushes: [], fill: el('button'), autoEdges: [autoBtn, autoMob], undo: el('button'), redo: el('button'),
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
        doc, win: opts.storage ? { localStorage: opts.storage } : {}, canvas, host, file: () => current, busy: () => busy,
        dialogs: { open: (spec) => { const body = el('div'), ctl = { closed: false, close() { ctl.closed = true; } }; spec.build(body, ctl); sheets.push({ spec, body, ctl }); return ctl; } }, say: (title, text) => said.push(title + ': ' + text),
        rebase: (bytes) => { log.rebased++; current = File.parse(bytes); return current; }, onEdit: () => { log.edits++; }
    });
    tools.setMap(current);
    return { doc, host, tools, sheets, said, painter: () => painter, log, file: () => current, busy: (v) => { busy = v; }, stroking: (v) => { stroking = v; }, button: (id) => host.tools.filter((b) => b.getAttribute('data-tool') === id)[0], brush: (n) => host.brushes.filter((b) => b.getAttribute('data-brush') === String(n))[0] };
}

function caseG() {
    const p = toolsPage(open(9, 30, 30));
    equal(p.host.tools.map((b) => [b.getAttribute('data-tool'), b.disabled]), [['ground', false], ['objects', false], ['bigger', false], ['hills', false], ['ants', false], ['food', false], ['erase', false], ['pickup', false], ['pan', false]], 'all nine tools are on');
    check(p.button('ground').getAttribute('aria-pressed') === 'true' && p.button('pan').getAttribute('aria-pressed') === 'false' && p.button('pickup').getAttribute('aria-pressed') === 'false', 'Ground is the tool at the start');
    check(!p.host.box.hidden && !p.host.tile.hidden && p.host.swatches.children.length === 5 && p.host.title.textContent === 'Ground', 'with its palette of 5 kinds');
    check(p.host.tile.querySelector('span').textContent === 'Gravel' && p.host.tile.querySelector('i').style.background === Tiles.css(485, Tiles.EMPTY2), 'the phone\'s palette button shows the tile that is chosen');
    p.host.tile.click();
    check(p.sheets.length === 1 && p.sheets[0].spec.title === 'Ground' && p.sheets[0].body.children[0].children.length === 5, 'and opens the palette as a sheet of 5 kinds with their names');
    p.sheets[0].body.children[0].children[2].click();
    check(p.tools.state().ground === 571 && p.sheets[0].ctl.closed && p.host.tile.querySelector('span').textContent === 'Slate', 'a tile of the sheet is chosen and the sheet closes');
    p.host.swatches.children[0].click();
    check(p.host.swatches.children.filter((b) => b.getAttribute('aria-checked') === 'true').length === 1 && p.host.name.textContent === 'Gravel', 'one swatch chosen, and its name said');
    check(p.host.undo.disabled && p.host.redo.disabled && !p.host.fill.disabled && !p.brush(3).disabled, 'undo and redo are off with nothing drawn; Fill and the brushes are on');
    p.host.swatches.children[1].click();
    check(p.tools.state().ground === 487 && p.host.name.textContent === 'Dirt', 'a swatch chooses a tile and its name shows');
    p.button('objects').click();
    check(p.host.swatches.children.length === 30 && p.host.title.textContent === 'Objects' && p.tools.state().tool === 'objects', 'Objects has its own palette of 30');
    check(p.tools.state().ground === 487, 'and the ground that was chosen is kept');
    p.button('erase').click();
    check(p.host.box.hidden && p.host.tile.hidden && p.host.fill.disabled && p.brush(5).disabled === false, 'Erase has no palette (nor the phone\'s palette button) and no Fill, and keeps the brushes');
    p.button('pan').click();
    check(p.host.box.hidden && p.painter().size() === 0 && p.brush(5).disabled && p.host.fill.disabled, 'Pan paints nothing: no brush, no fill');
    check(!p.painter().canMove({ row: 1, column: 1 }) && !p.painter().tapOnly({ row: 1, column: 1 }) && p.painter().begin({ row: 1, column: 1 }) === false && p.painter().key({ key: 'ArrowUp' }) === false, 'Pan moves nothing: a drag only looks around, whatever stands there, and it has no keys');
    p.button('pickup').click();
    check(p.host.box.hidden && p.painter().size() === 0 && p.brush(5).disabled && p.host.fill.disabled && p.tools.state().tool === 'pickup', 'Pick up paints nothing either (its own part loads on demand: the page has none in this check)');
    p.stroking(true); p.button('hills').click(); p.stroking(false);
    check(p.tools.state().tool === 'pickup', 'a tool cannot be chosen while a stroke is going on');

    // keys
    const q = toolsPage(open(9, 30, 30));
    q.doc.key('o'); check(q.tools.state().tool === 'objects', 'the letter O chooses Objects');
    q.doc.key('E'); check(q.tools.state().tool === 'erase', 'E chooses Erase (either case)');
    q.doc.key('m'); check(q.tools.state().tool === 'pan', 'M chooses Pan (the letter of the old Move)');
    q.doc.key('p'); check(q.tools.state().tool === 'pickup', 'P chooses Pick up');
    q.doc.key('h'); check(q.tools.state().tool === 'hills', 'H chooses Hills');
    q.doc.key('a'); check(q.tools.state().tool === 'ants', 'A chooses Ants');
    q.doc.key('F'); check(q.tools.state().tool === 'food', 'F chooses Food (either case)');
    q.doc.key('m');
    q.doc.key('g'); check(q.tools.state().tool === 'ground', 'G chooses Ground');
    q.doc.key(']'); check(q.tools.state().brush === 2, 'the right bracket makes the brush bigger');
    for (let i = 0; i < 8; i++) q.doc.key(']');
    check(q.tools.state().brush === 9, 'up to 9');
    q.doc.key('['); check(q.tools.state().brush === 8, 'the left bracket makes it smaller');
    q.doc.key('b'); check(q.tools.state().fill === true && q.host.fill.getAttribute('aria-pressed') === 'true', 'B turns Fill on');
    q.doc.key('b'); check(q.tools.state().fill === false, 'and off');
    const typing = q.doc.createElement('input');
    q.doc.key('o', { target: typing });
    check(q.tools.state().tool === 'ground', 'a letter typed into a box is not a tool');
    q.busy(true); q.doc.key('o'); q.busy(false);
    check(q.tools.state().tool === 'ground', 'nor while a dialog is open');
    const ctrl = q.doc.key('g', { ctrlKey: true });
    check(!ctrl.defaultPrevented && q.tools.state().tool === 'ground', 'Control with a letter is not a tool');

    // a painting tool keeps a stroke when the pointer is lost (the window loses the focus): `abandon` ends it like a lift (Pick up takes its thing back instead), and `away` changes nothing for it
    {
        const a = toolsPage(open(9, 30, 30)), ap = a.painter();
        a.host.swatches.children[1].click();
        ap.begin({ row: 5, column: 5 }); ap.extend({ row: 5, column: 5 }, { row: 5, column: 8 }); ap.away({ outside: true, x: 900, y: 0 }); ap.abandon();
        check([5, 6, 7, 8].every((c) => Edit.tile(a.file(), 1, 5 * 30 + c) === 487) && a.tools.state().history.strokes === 1, 'a painting stroke whose pointer was lost is kept, as a lift keeps it');
    }
    // a stroke through the painter, undo and redo
    const r = toolsPage(open(9, 30, 30)), pt = r.painter(), start = digest(r.file().bytes);
    r.host.swatches.children[1].click();                                    // dirt (487), which the dictionary does not have yet
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
    // Erase through the painter; Pan does not paint
    r.button('erase').click();
    check(pt.size() === 1 && pt.begin({ row: 2, column: 2 }) === true, 'Erase has a stroke');
    pt.end();
    r.button('pan').click();
    check(pt.size() === 0 && pt.begin({ row: 2, column: 2 }) === false, 'Pan has none');
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
    // a tool that puts a thing down when the pointer lifts (Pick up) is told a lost pointer is not a lift
    const dropTool = () => { const q = fakeCanvasPage(); q.api.setPainter({ size: () => 1, begin: () => { q.log.push('begin'); return true; }, extend: () => q.log.push('extend'), end: () => { q.log.push('end'); q.stroking = q.api.isStroking(); }, cancel: () => { q.log.push('cancel'); q.stroking = q.api.isStroking(); }, abandon: () => { q.log.push('abandon'); q.stroking = q.api.isStroking(); }, away: (w) => q.aways.push((w.outside ? 'off' : 'on') + ':' + w.x + ',' + w.y) }); q.aways = []; return q; };
    p = dropTool(); p.mouse('pointerdown'); p.mouse('pointermove', { clientX: 330 }); p.mouse('pointerup', { clientX: 900, clientY: 700 });
    check(!p.api.isStroking() && p.log.join() === 'begin,extend,end', 'the button let go far outside the map (the pointer is captured) is a lift: end');
    check(p.stroking === false, 'the stroke is over when the tool is told of the lift (the page takes no change while a stroke goes on, and the tool writes its change then)');
    p = dropTool(); p.mouse('pointerdown'); p.mouse('pointermove', { clientX: 330 }); p.win.fire('blur', {});
    check(p.stroking === false, 'and when a lost pointer is told');
    p = dropTool(); p.mouse('pointerdown'); p.mouse('pointermove', { clientX: 330 }); p.mouse('pointercancel', { type: 'pointercancel' });
    check(!p.api.isStroking() && p.log.join() === 'begin,extend,cancel', 'a cancelled pointer is a cancel');
    p = dropTool(); p.mouse('pointerdown'); p.mouse('pointermove', { clientX: 330 }); p.mouse('lostpointercapture');
    check(!p.api.isStroking() && p.log.join() === 'begin,extend,abandon', 'a pointer that is lost without a lift is abandoned, not dropped');
    p = dropTool(); p.mouse('pointerdown'); p.mouse('pointermove', { clientX: 330 }); p.win.fire('blur', {});
    check(!p.api.isStroking() && p.log.join() === 'begin,extend,abandon', 'a window that loses the focus abandons it');
    p = dropTool(); p.mouse('pointerdown'); p.mouse('pointermove', { clientX: 330 }); p.doc.visibilityState = 'hidden'; p.doc.fire('visibilitychange', {});
    check(!p.api.isStroking() && p.log.join() === 'begin,extend,abandon', 'a page that is hidden abandons it');
    p = dropTool(); p.mouse('pointerdown'); p.mouse('pointermove', { clientX: 330 }); p.mouse('pointermove', { buttons: 0, clientX: 340 });
    check(!p.api.isStroking() && p.log.join() === 'begin,extend,end', 'a move with no button down (the lift was not heard) is a lift');
    p = dropTool(); p.mouse('pointerdown', { pointerType: 'touch', pointerId: 3 }); p.mouse('pointermove', { pointerType: 'touch', pointerId: 3, clientX: 340, clientY: 230 }); p.mouse('pointermove', { pointerType: 'touch', pointerId: 3, clientX: 380, clientY: 260 });
    check(p.api.isStroking() && p.log.join() === 'begin,extend,extend', 'a finger that has moved begins the drag and follows it: ' + p.log.join());
    p.mouse('pointerup', { pointerType: 'touch', pointerId: 3, clientX: 900, clientY: 700 });
    check(!p.api.isStroking() && p.log.join() === 'begin,extend,extend,end', 'the finger lifted (even outside the map) is a lift');
    // the tool is told where the pointer is, in the page's own coordinates, before it is told of the lift: on the map, off the canvas, past the map's edge (a mouse, a finger and a pen alike)
    for (const type of ['mouse', 'touch', 'pen']) {
        const id = { mouse: 7, touch: 3, pen: 4 }[type], at = (x, y) => ({ pointerType: type, pointerId: id, clientX: x, clientY: y });
        p = dropTool(); p.mouse('pointerdown', at(300, 200)); p.mouse('pointermove', at(330, 210)); p.mouse('pointermove', at(340, 220)); p.mouse('pointerup', at(340, 220));
        check(p.aways[p.aways.length - 1] === 'on:340,220' && p.log.slice(-1)[0] === 'end', type + ': a lift on the map is told as on the map, then the lift: ' + p.aways.join(' '));
        p = dropTool(); p.mouse('pointerdown', at(300, 200)); p.mouse('pointermove', at(330, 210)); p.mouse('pointermove', at(340, 220)); p.mouse('pointermove', at(900, 220)); p.mouse('pointerup', at(900, 220));
        check(p.aways.indexOf('off:900,220') >= 0 && p.aways[p.aways.length - 1] === 'off:900,220' && p.log.slice(-1)[0] === 'end', type + ': a lift off the canvas is told as off the map: ' + p.aways.join(' '));
        p = dropTool(); p.mouse('pointerdown', at(300, 200)); p.mouse('pointermove', at(330, 210)); p.mouse('pointermove', at(340, 220)); p.mouse('pointermove', at(595, 220)); p.mouse('pointerup', at(595, 220));
        check(p.aways[p.aways.length - 1] === 'off:595,220', type + ': a lift on the canvas but past the edge of the map is off the map too: ' + p.aways.join(' '));
        p = dropTool(); p.mouse('pointerdown', at(300, 200)); p.mouse('pointermove', at(330, 210)); p.mouse('pointermove', at(900, 220)); p.mouse('pointermove', at(340, 220)); p.mouse('pointerup', at(340, 220));
        check(p.aways[p.aways.length - 1] === 'on:340,220', type + ': out and back is on the map again: ' + p.aways.join(' '));
        p = dropTool(); p.mouse('pointerdown', at(300, 200)); p.mouse('pointermove', at(900, 220)); p.mouse('pointercancel', Object.assign(at(900, 220), { type: 'pointercancel' }));
        check(p.log.slice(-1)[0] === 'cancel' && p.aways.length <= 1 && p.aways.indexOf('on:900,220') < 0, type + ': a cancelled pointer is a cancel, and is not told as a lift');
    }
    p = dropTool(); p.mouse('pointerdown', { pointerType: 'pen', pointerId: 4 }); p.mouse('pointermove', { pointerType: 'pen', pointerId: 4, clientX: 330 }); p.mouse('pointerup', { pointerType: 'pen', pointerId: 4 });
    check(p.log.join().indexOf('end') > 0 && !p.api.isStroking(), 'a pen is a lift too: ' + p.log.join());
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
    // the tool is told which cell a press is on (Pick up: a press on a thing is a drag, one on empty ground a tap)
    p = tapPage();
    p.api.setPainter({ size: () => 0, tapOnly: (cell) => !!cell && typeof cell.row === 'number' && typeof cell.column === 'number', begin: (c) => { p.calls.push('begin'); return false; }, extend: () => {}, end: () => {}, cancel: () => {} });
    p.mouse('pointerdown'); p.mouse('pointerup', { buttons: 0 });
    check(p.calls.join() === 'begin' && !p.api.isStroking(), 'the canvas gives the tool the cell that was pressed, and a tool that takes a tap there gets it');
    // a tool with keys of its own is asked first: its keys are not the cursor's
    p = tapPage();
    const keyed = [], where = () => p.canvas.attrs['data-center'];
    p.api.setPainter({ size: () => 0, tapOnly: () => true, key: (e) => { keyed.push(e.key); return e.key === 'ArrowRight'; }, begin: () => false, extend: () => {}, end: () => {}, cancel: () => {} });
    const prevented = [], event = (key) => ({ key, ctrlKey: false, metaKey: false, altKey: false, shiftKey: false, preventDefault() { prevented.push(key); } });
    p.canvas.fire('keydown', event('ArrowRight'));
    p.canvas.fire('keydown', event('+'));
    check(keyed.join() === 'ArrowRight,+' && prevented.join() === 'ArrowRight,+', 'a key of the tool is the tool\'s first: the tool is asked, and what it takes is not the map\'s (the arrow does not move the marked square: ' + keyed.join() + ')');
    // a tool that shows what a click would do is told where the mouse is, and when it has left the map (Pick up: the ghost follows the mouse)
    p = tapPage();
    const hovered = [];
    p.api.setPainter({ size: () => 0, tapOnly: () => true, hover: (c) => hovered.push(c ? c.row + ',' + c.column : 'none'), begin: () => false, extend: () => {}, end: () => {}, cancel: () => {} });
    p.mouse('pointermove', { buttons: 0 }); p.canvas.fire('pointerleave');
    check(hovered.length === 2 && /^\d+,\d+$/.test(hovered[0]) && hovered[1] === 'none', 'the mouse over a cell, and leaving the map, are told to the tool: ' + hovered.join(' | '));
    check(p.canvas.attrs['data-view'] !== undefined && /^[0-9.]+,[0-9.]+,[0-9.]+$/.test(p.canvas.attrs['data-view']), 'the canvas says its view exactly (what a test reads): ' + p.canvas.attrs['data-view']);
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
        // put over (even one cell of) another team's hill: refused in words with the team's name, and nothing changes
        const before = Array.from(ed.file.bytes), stepsHere = ed.history.size().strokes;
        for (const [r, c] of [[12, 14], [12, 15], [9, 12], [11, 10]]) {
            const over = Blocks.placeHill(ed.file, 0, r, c);
            check(typeof over.error === 'string' && /^Blue\u2019s hill is here\. Move it first/.test(over.error) && !over.cells && !over.splices, 'a hill with its entrance at (' + r + ', ' + c + ') would touch the Blue hill: refused, the team is named: ' + over.error + tag);
        }
        check(Array.from(ed.file.bytes).join() === before.join() && ed.history.size().strokes === stepsHere && hillFootprint(ed.file, 246).length === 16, 'and nothing was taken away' + tag);
        ed.act(Blocks.placeHill(ed.file, 0, 4, 19));
        expectHill(ed.file, 248, 4, 19, ' of Green, moved beside it' + tag);
        check(hillFootprint(ed.file, 246).length === 16, 'the Blue hill is still there' + tag);
        // undo all the way: byte for byte the first states
        let n = 0;
        while (ed.history.canUndo()) { ed.undo(); n++; }
        const opened = File.parse(bytes);
        check(n === 6, 'six steps are undone: four hills, the move of Blue and the move of Green' + tag);
        equal(cells2(ed.file), Array.from(opened.bytes.subarray(opened.layer2, opened.layer2 + 6 * 480)), 'undo puts back every cell of layer 2' + tag);
        while (ed.history.canRedo()) ed.redo();
        expectHill(ed.file, 248, 4, 19, ' redone' + tag);
        // refusals at the edge: the hill needs the cell before its entrance and two after it
        for (const [r, c, why] of [[0, 5, 'the top row'], [5, 0, 'the first column'], [18, 5, 'the second to last row'], [5, 22, 'the second to last column'], [19, 5, 'the last row']]) {
            const bad = Blocks.placeHill(ed.file, 1, r, c);
            check(typeof bad.error === 'string' && /no room/.test(bad.error) && !bad.cells, 'no hill with its entrance in ' + why + ': ' + (bad.error || '').slice(0, 40) + tag);
        }
        check(!Blocks.placeHill(ed.file, 1, 1, 1).error && !Blocks.placeHill(ed.file, 1, 17, 21).error, 'the nearest places to the edge that have room are accepted' + tag);
        // an object under the hill goes away (whole); another team's hill near it stays
        const g = editorOn(makeMap(version, 20, 24, 486));
        g.act(Blocks.placeHill(g.file, 1, 6, 6));
        put(g.file, 2, 12, 13, 302, 1, version === 8 ? (12 | (13 << 8)) : 0);
        put(g.file, 2, 11, 12, 303, 1, version === 8 ? (11 | (12 << 8)) : 0); put(g.file, 2, 10, 12, 303, 0, version === 8 ? (11 | (12 << 8)) : 1);       // (a bigger object: one cell under the hill, one beside it)
        g.act(Blocks.placeHill(g.file, 3, 12, 12));
        check(hillFootprint(g.file, 247).length === 16 && Edit.tile(g.file, 2, 12 * 24 + 13) === 245 && hillFootprint(g.file, 245).length === 16, 'a hill put over an object takes the object away; the other team\'s hill beside it stays' + tag);
        check(Edit.tile(g.file, 2, 10 * 24 + 12) === 0x7FFE && Edit.tile(g.file, 2, 11 * 24 + 12) === 245, 'a bigger object that is partly under the hill goes whole: its cell beside the hill goes too' + tag);
        check(/Red\u2019s hill is here/.test(Blocks.placeHill(g.file, 3, 8, 8).error), 'and one that would touch the Red hill is refused' + tag);
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

// ---- J3: what the first checks of the lists left unsaid ----
function caseJ3() {
    const nameAt = (f, id) => String.fromCharCode.apply(null, Array.from(f.bytes.slice(f.dictAt + 2 + 11 * id, f.dictAt + 2 + 11 * id + 8))).replace(/\0.*$/, '');
    for (const version of [8, 9]) {
        const tag = ' (version ' + version + ')';
        // the start marker of each team is named in the dictionary, in place (the checker's rule of a new map: every tile that is used has its name)
        const ed = editorOn(mapWith(version, 20, 24, 486, [], []));
        for (let t = 0; t < 4; t++) { ed.act(Blocks.placeStart(ed.file, t, 5 + 3 * t, 5, 2)); check(nameAt(ed.file, Blocks.TEAMS[t].marker) === Blocks.TEAMS[t].markerName, Blocks.TEAMS[t].label + ': its start marker is named ' + Blocks.TEAMS[t].markerName + ' in the dictionary' + tag); }
        // the stages of a pile are kept between 2 and 6, whatever is asked
        for (const [asked, got] of [[1, 2], [0, 2], [-3, 2], [2, 2], [6, 6], [99, 6]]) {
            const x = editorOn(mapWith(version, 20, 24, 486, [], []));
            x.act(Blocks.placePile(x.file, 5, 5, asked));
            check(Blocks.layout(x.file).piles[0].stages.length === got, 'a pile of ' + asked + ' stages asked for has ' + got + tag);
        }
        // a step that has only a splice is counted: end() says how many cells and splices it changed
        const y = editorOn(mapWith(version, 20, 24, 486, [], []));
        let st = y.begin('act');
        st.change(Blocks.placeStart(y.file, 1, 8, 8, 3));
        check(st.end() === 1 && y.history.size().strokes === 1, 'a change of one splice is one step that reports 1' + tag);
        st = y.begin('act');
        st.change(Blocks.placeHill(y.file, 1, 10, 10));
        check(st.end() === 16, 'and a hill of 16 cells reports 16' + tag);
        // a stroke taken back (a second finger) gives back the lists too, byte for byte, and keeps no step
        const z = editorOn(mapWith(version, 20, 24, 486, [[152, 3, 3], [152, 3, 4]], [{ row: 9, column: 9, units: 10, points: 30, stages: [[10, 280], [0, 0x7FFE]] }])), start = Array.from(z.file.bytes);
        const e = z.begin('erase', null);
        e.stamp(3, 3, 1); e.stamp(9, 9, 1);
        check(Blocks.layout(z.file).records.length === 0 && Blocks.layout(z.file).piles.length === 0, 'the eraser took the ants and the pile' + tag);
        e.cancel();
        check(Array.from(z.file.bytes).join() === start.join() && z.history.size().strokes === 0, 'a stroke that is cancelled puts the ants and the pile back, byte for byte, and keeps no step' + tag);
        // a patch whose bytes are not where it says (it cannot be, but) is not forced onto the file
        const w = editorOn(mapWith(version, 20, 24, 486, [], []));
        w.act(Blocks.placeStart(w.file, 0, 5, 5, 3));
        const lay = Blocks.layout(w.file), at = w.file.blocksAt + lay.records[1].rel + 2;
        w.file.bytes[at] ^= 0x55;
        const length = w.file.bytes.length, tampered = w.file.bytes[at];
        w.undo();
        check(w.file.bytes.length === length && w.file.bytes[at] === tampered, 'an undo whose splice does not find its bytes changes nothing (' + w.file.bytes.length + ' bytes)' + tag);
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

    // the arrow keys in the team swatches choose a team, not a tile
    const groundBefore = p.tools.state().ground;
    p.button('hills').click();
    p.host.swatches.children[0].focus();
    p.host.swatches.listeners.keydown[0]({ key: 'ArrowDown', preventDefault() {} });
    check(p.tools.state().team === 1 && p.tools.state().ground === groundBefore && p.host.swatches.children[1].getAttribute('aria-checked') === 'true', 'the arrow keys in the team swatches go from team to team (Red), and leave the ground that was chosen');
    p.host.swatches.listeners.keydown[0]({ key: 'ArrowUp', preventDefault() {} });
    check(p.tools.state().team === 0, 'and back');
    p.host.swatches.children[3].click();
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

    // on a map with fewer cells than ants, the line says how many fit
    const tiny = toolsPage(File.parse(mapWith(8, 3, 3, 486, [], [])));
    tiny.button('ants').click();
    for (let i = 0; i < 60; i++) tiny.host.stepMore.click();
    tiny.painter().begin({ row: 1, column: 1 });
    check(Blocks.layout(tiny.file()).records.length === 9 && tiny.host.name.textContent === 'Green: 9 ants (only 9 fit)', 'asked for 64 ants on a map of 9 cells, the line says what is on the map: ' + tiny.host.name.textContent);
    tiny.host.stepLess.click();
    check(tiny.host.name.textContent === 'Green: 9 ants', 'and the note goes when the number is changed: ' + tiny.host.name.textContent);

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
    check(fsheet.spec.title === 'Food' && fsheet.body.children[0].className === 'stepper sheet' && fsheet.body.children.length === 5 && fsheet.body.children[4].children.length === 32, 'the sheet of Food has Stages, Bites, Points steppers, 32 food items and its line');
    fsheet.body.children[2].querySelectorAll('button')[1].click();
    fsheet.body.children[3].querySelectorAll('button')[1].click();
    check(p.tools.state().units === 25 && p.tools.state().points === 30, 'stepping Bites and Points updates the Food state');
    fsheet.body.children[4].children[1].click();
    check(p.tools.state().food === 256 && p.tools.state().units === 40 && p.tools.state().points === 25, 'picking Lollipop sets its item id, default bites and points');
    p.host.stepLess.click(); p.host.stepMore.click();
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

// ---- L: the Bigger tool on a fake page ----
function caseL() {
    const EMPTY2 = 0x7FFE;
    const p = toolsPage(File.parse(mapWith(8, 24, 24, 486, [], []))), pt = p.painter();
    const list = Palette.items('bigger');
    p.doc.key('k');
    check(p.tools.state().tool === 'bigger' && p.button('bigger').getAttribute('aria-pressed') === 'true', 'the letter K chooses Bigger');
    check(p.host.title.textContent === 'Bigger objects' && !p.host.box.hidden && !p.host.tile.hidden && p.host.swatches.hidden && p.host.stepper.hidden, 'its palette is the one button (no swatches, no stepper, on a desktop too)');
    check(p.host.tile.querySelector('.pkn').textContent === '4 cells, 2 x 2' && p.host.tile.querySelector('.pkc').textContent === 'grass4' && p.host.tile.querySelector('i').className === 'fp' && p.host.tile.querySelector('i').children.length === 4 && p.host.tile.querySelector('i').children.filter((c) => c.className === 'org').length === 1 && p.host.tile.querySelector('i').children.filter((c) => c.style.background === list[0].color).length === 4 && p.host.name.textContent === 'grass4 · 4 cells', 'the button says the game\'s name and, under it, the number of cells: ' + p.host.tile.querySelector('span').textContent);
    check(pt.tapOnly() && pt.door() && pt.size() === 1 && p.host.fill.disabled && p.brush(3).disabled, 'a tap puts it whole (the cell that is tapped has the dot), no brush and no Fill');
    equal(pt.shape(), list[0].box, 'the canvas marks the box of the object');
    check(/Erase takes it away whole/.test(p.host.hint.children[0].textContent) && p.host.hint.children[1].textContent.length > 0, 'the notes say so');
    // the sheet
    p.host.tile.click();
    const sheet = p.sheets[p.sheets.length - 1], picks = sheet.body.children[0].children;
    check(sheet.spec.title === 'Bigger objects' && picks.length === 29 && picks.every((b) => b.className === 'pick') && sheet.body.children[1].className === 'pickinfo', 'the sheet has the 29 objects and a line');
    equal(picks.map((b) => b.querySelector('.pkn').textContent + ' ' + b.querySelector('.pkc').textContent).slice(0, 3), ['4 cells, 2 x 2 grass4', '10 cells, 5 x 2 grassmed1', '6 cells, 2 x 3 grass2'], 'named as the game names them, with their cells');
    equal(picks.map((b) => b.querySelector('i').children.filter((c) => c.style.background).length), list.map((i) => i.cells.length), 'every chip draws as many filled squares as the object has cells');
    equal(picks.slice(0, 2).map((b) => b.getAttribute('aria-label')), ['grass4 · 4 cells', 'grassmed1 · 10 cells'], 'and a screen reader hears both in one');
    check(picks[0].getAttribute('aria-checked') === 'true' && picks.filter((b) => b.getAttribute('aria-checked') === 'true').length === 1, 'the one that is chosen is marked');
    picks[1].click();
    check(p.tools.state().big === 1324 && sheet.ctl.closed && p.host.tile.querySelector('.pkn').textContent === '10 cells, 5 x 2' && p.host.tile.querySelector('.pkc').textContent === 'grassmed1', 'a pick is chosen, the sheet closes and the button follows');
    equal(pt.shape(), list[1].box, 'and the marked box follows');
    check(p.tools.state().ground === 485 && p.tools.state().objects === 302, 'the ground and the objects that were chosen are as they were');
    // a tap puts it whole
    check(pt.begin({ row: 10, column: 10 }) === false && p.log.edits === 1, 'a tap puts it (the page is told)');
    check(Edit.objectCells(p.file(), 10 * 24 + 10).length === 10 && Edit.tile(p.file(), 2, 10 * 24 + 10) === 1324 && !p.host.undo.disabled && !p.tools.isClean(), 'whole: 10 cells, and Undo is on');
    check(p.log.dirty.length === 1 && p.log.dirty[0].length === 10, 'the canvas was told the 10 cells: ' + JSON.stringify(p.log.dirty.map((d) => d.length)));
    const solid = Array.from({ length: p.file().cells }, (_, i) => Edit.flags(p.file(), 1, i) & 1).reduce((a, b) => a + b, 0);
    check(solid === 10, 'with a solid bit under each');
    // the same place again: nothing changes
    const steps = p.tools.state().history.strokes;
    pt.begin({ row: 10, column: 10 });
    check(p.tools.state().history.strokes === steps && p.said.length === 0, 'the same object on the same place is no new step');
    // a hill is in the way: a plain sentence, nothing changes
    p.button('hills').click(); p.host.swatches.children[0].click(); pt.begin({ row: 5, column: 15 });
    p.button('bigger').click();
    const was = Array.from(p.file().bytes), edits = p.log.edits;
    pt.begin({ row: 5, column: 15 });
    check(p.said.length === 1 && /^It cannot be put there: A hill is in the way/.test(p.said[0]) && p.log.edits === edits && JSON.stringify(was) === JSON.stringify(Array.from(p.file().bytes)), 'over a hill: ' + p.said[0] + ', and nothing changed');
    // the eraser takes it whole
    p.button('erase').click();
    pt.begin({ row: 10, column: 12 }); pt.end();
    check(Edit.tile(p.file(), 2, 10 * 24 + 10) === EMPTY2 && Edit.objectCells(p.file(), 10 * 24 + 11).length === 0, 'Erase takes it whole, from the cell that was touched');
    p.doc.key('z', { ctrlKey: true });
    check(Edit.objectCells(p.file(), 10 * 24 + 10).length === 10, 'undo gives it back whole');
    p.doc.key('z', { ctrlKey: true });
    p.doc.key('z', { ctrlKey: true });
    check(Edit.tile(p.file(), 2, 10 * 24 + 10) === EMPTY2 && Edit.flags(p.file(), 1, 10 * 24 + 10) === 0, 'and two undos more (the hill, then the object) leave nothing (the solid bit goes with it)');
    // a phone: the same button and sheet; the label of the sheet is a radio group
    check(sheet.body.children[0].getAttribute('role') === 'radiogroup' && sheet.body.children[0].getAttribute('aria-label') === 'Bigger objects', 'the sheet is a radio group named Bigger objects');
    // the tools that have a palette, and Objects with the solid bit
    const q = toolsPage(File.parse(mapWith(9, 24, 24, 486, [], []))), qt = q.painter();
    q.button('objects').click();
    q.doc.key('o');
    qt.begin({ row: 4, column: 4 }); qt.end();
    check(Edit.tile(q.file(), 2, 4 * 24 + 4) === 302 && (Edit.flags(q.file(), 1, 4 * 24 + 4) & 1) === 1, 'a rock put by the Objects tool is solid, as the library\'s are');
    q.button('erase').click();
    qt.begin({ row: 4, column: 4 }); qt.end();
    check(Edit.tile(q.file(), 2, 4 * 24 + 4) === EMPTY2 && (Edit.flags(q.file(), 1, 4 * 24 + 4) & 1) === 0, 'and it is not when the rock is taken away');
    // the Objects tool never takes a hill, a team's ants or a food pile away: the brush is refused as a whole, in plain words, and nothing changes
    const layers = (f) => JSON.stringify(Array.from(f.bytes.subarray(f.layer1, f.blocksAt)));
    const r = toolsPage(File.parse(mapWith(8, 24, 24, 486, [], []))), rt = r.painter();
    r.button('hills').click(); r.host.swatches.children[2].click(); rt.begin({ row: 8, column: 8 });
    r.button('ants').click(); r.host.swatches.children[0].click(); rt.begin({ row: 18, column: 3 });
    r.button('food').click(); rt.begin({ row: 3, column: 18 });
    r.button('objects').click();
    const before = layers(r.file()), editsBefore = r.log.edits;
    r.said.length = 0;
    check(rt.begin({ row: 9, column: 9 }) === false && /^It cannot be put there: Blue\u2019s hill is here\. Move it first, or take it away with Erase\./.test(r.said[0] || '') && layers(r.file()) === before, 'a rock put where a hill is: ' + r.said[0] + ', and nothing changed');
    r.brush(9).click(); r.said.length = 0;
    check(rt.begin({ row: 12, column: 12 }) === false && /Blue\u2019s hill is here/.test(r.said[0] || '') && layers(r.file()) === before, 'a brush of 9 that touches the hill is refused as a whole, not only on the hill\'s cells');
    r.said.length = 0;
    check(rt.begin({ row: 18, column: 3 }) === false && /Starting ants are in the way/.test(r.said[0] || '') && layers(r.file()) === before, 'over a team\'s starting ants: ' + r.said[0]);
    r.said.length = 0;
    check(rt.begin({ row: 3, column: 18 }) === false && /food pile is in the way/.test(r.said[0] || '') && layers(r.file()) === before, 'over a food pile: ' + r.said[0]);
    r.brush(1).click(); r.said.length = 0;
    check(rt.begin({ row: 20, column: 20 }) === true && r.said.length === 0, 'a free place is as it was');
    rt.end();
    check(r.log.edits > editsBefore && Edit.tile(r.file(), 2, 20 * 24 + 20) === 302, 'and the rock is put');
    // a drag that runs into the hill says so once and goes on where it can
    r.said.length = 0;
    r.button('objects').click();
    check(rt.begin({ row: 8, column: 2 }) === true, 'a drag starts beside the hill');
    rt.extend({ row: 8, column: 2 }, { row: 8, column: 12 });
    rt.end();
    check(r.said.length === 1 && Edit.tile(r.file(), 2, 8 * 24 + 2) === 302 && Edit.tile(r.file(), 2, 8 * 24 + 8) === 246 && hillFootprint(r.file(), 246).length === 16, 'a drag over the hill says it once and leaves the hill whole');
}

// ---- M: the properties of the map as one step of the history ----
// A CHANGE of mapmaker_props.js: splices with absolute places (the header, the eggs at the end), put on from the last to the first; made here by hand, as the core's answer would make it
function propertiesChange(f) {
    const n = f.bytes.length, version = f.version, desc = version === 8 ? { at: 10, removed: [0, 0, 0], inserted: [97, 98, 99] } : { at: 16, removed: [0, 0], inserted: [3, 0, 97, 98, 99] };
    return { names: [], cells: [], splices: [
        { abs: true, at: n - 2, removed: Uint8Array.from([6, 0]), inserted: Uint8Array.from([9, 0]) },
        { abs: true, at: desc.at, removed: Uint8Array.from(desc.removed), inserted: Uint8Array.from(desc.inserted) },
        { abs: true, at: 8, removed: Uint8Array.from([10, 0]), inserted: Uint8Array.from([7, 0]) }] };
}
function caseM() {
    for (const version of [8, 9]) {
        const tag = 'version ' + version;
        const ed = editorOn(makeMap(version, 12, 12)), before = Array.from(ed.file.bytes), f0 = ed.file, grow = version === 9 ? 3 : 0, digestBefore = digest(f0.bytes.subarray(f0.layer1, f0.layer2 + 6 * f0.cells));
        const dirty = ed.act(propertiesChange(f0));
        check(dirty !== null && ed.history.size().strokes === 1, tag + ': the properties are ONE step of the history');
        const f1 = ed.file, now = Array.from(f1.bytes);
        check(now.length === before.length + grow && now[8] === 7 && now[now.length - 2] === 9 && now[now.length - 1] === 0, tag + ': the bytes say what the splices say (' + now.length + ' bytes)');
        check(f1.ok && f1.rows === 12 && f1.columns === 12 && f1.layer1 === f0.layer1 + grow && f1.blocksAt === f0.blocksAt + grow, tag + ': the file is read again: the layers moved by what the description grew');
        equal(digest(f1.bytes.subarray(f1.layer1, f1.layer2 + 6 * f1.cells)), digestBefore, tag + ': the cells are the cells they were');
        const weight = Edit.weight({ keys: [], splices: propertiesChange(f0).splices });
        check(weight <= 12, tag + ': a step that holds only the changed bytes weighs ' + weight);
        // a stroke after it, on the moved layers, then undo of both and redo of both: byte for byte
        const stroke = ed.begin('ground', 487); stroke.stamp(2, 3, 1); stroke.end();
        const drawn = Array.from(ed.file.bytes);
        check(Edit.tile(ed.file, 1, 2 * 12 + 3) === 487 && ed.history.size().strokes === 2, tag + ': a stroke after the properties paints on the layers where they now are');
        check(ed.undo() !== null && Array.from(ed.file.bytes).join() === now.join(), tag + ': undo takes the stroke back');
        check(ed.undo() !== null && Array.from(ed.file.bytes).join() === before.join(), tag + ': undo of the properties gives the old file back, byte for byte');
        check(ed.undo() === null && !ed.history.canUndo(), tag + ': nothing more to undo');
        check(ed.redo() !== null && Array.from(ed.file.bytes).join() === now.join() && ed.redo() !== null && Array.from(ed.file.bytes).join() === drawn.join(), tag + ': redo brings both back, byte for byte');
        check(ed.history.isClean() === false && ed.file.version === version, tag + ': a map with properties changed is not clean');
        // a list of the blocks after the layers and the header in ONE patch: the places are counted from the start of the file and from the end of layer 2, in the order they were made
        const g = editorOn(makeMap(version, 12, 12));
        const change = propertiesChange(g.file);
        change.splices.push({ rel: 0, removed: new Uint8Array(0), inserted: Uint8Array.from([0, 0]) });          // (a list that grows by a word at the start of block 1)
        check(g.act(change) !== null, tag + ': a change with both kinds of places is one step');
        const both = Array.from(g.file.bytes);
        check(g.undo() !== null && Array.from(g.file.bytes).join() === Array.from(makeMap(version, 12, 12)).join(), tag + ': and undo gives the old file back');
        check(g.redo() !== null && Array.from(g.file.bytes).join() === both.join(), tag + ': and redo the new one');
    }
    // the tools: one step, the buttons, the page told, the canvas given the new file
    const p = toolsPage(open(9, 12, 12));
    const start = p.log.edits;
    check(p.tools.act(propertiesChange(p.file())) === true && p.log.edits === start + 1 && !p.host.undo.disabled && p.log.rebased >= 1, 'the tools take the change as one step: the page is told once, undo is on, the canvas got the new bytes');
    check(p.file().bytes[8] === 7 && p.file().bytes.length > 0, 'and the page\'s file has them');
    p.tools.undo();
    check(p.file().bytes[8] === 10 && p.host.undo.disabled && !p.host.redo.disabled, 'undo through the tools gives the old file back');
    p.tools.redo();
    check(p.file().bytes[8] === 7, 'redo through the tools');
    p.stroking(true);
    check(p.tools.act(propertiesChange(p.file())) === false, 'while a stroke is going on nothing is put into the history');
    p.stroking(false);
    check(p.tools.act({ names: [], cells: [], splices: [] }) === false, 'a change of nothing is no step');
}

// ---- N: the kinds of ground on a fake page (the chips, the sheet of a kind, any single tile) ----
function caseN() {
    const p = toolsPage(open(9, 30, 30)), kinds = () => p.host.swatches.children, on = () => kinds().filter((b) => b.getAttribute('aria-checked') === 'true').map((b) => b.getAttribute('data-tile'));
    equal(kinds().map((b) => b.getAttribute('data-tile')), ['485', '487', '571', '439', '476'], 'five kind chips');
    check(p.host.swatches.className === 'swatches grounds' && kinds().every((b) => b.className === 'swatch gk' && b.children.length === 2 && b.children[1].textContent === b.getAttribute('aria-label')), 'each with its picture and its name under it');
    kinds()[0].click();
    check(p.sheets.length === 0, 'before the sheets are loaded a second tap on the chosen kind does nothing more');
    p.tools.use(Ground, null);
    check(p.host.name.textContent === 'Gravel: tap it again for all its tiles' && on().join() === '485', 'with the sheets loaded the line says to tap again: ' + p.host.name.textContent);
    kinds()[1].click();
    check(p.tools.state().ground === 487 && p.sheets.length === 0 && on().join() === '487' && p.host.name.textContent === 'Dirt: tap it again for all its tiles', 'the first tap on another kind chooses its plain tile and opens nothing');
    kinds()[1].click();
    const sheet = p.sheets[0];
    check(p.sheets.length === 1 && sheet.spec.title === 'Dirt: all its tiles' && sheet.spec.cls === 'ksheetbox', 'a tap on the chosen kind opens its sheet: ' + (sheet && sheet.spec.title));
    const chip = (id) => sheet.body.querySelectorAll('.gt').filter((b) => b.getAttribute('data-tile') === String(id))[0];
    const dg03a = Ground.edges('dirt', 'gravel')[1].id;
    check(Ground.name(dg03a) === 'dg03a' && sheet.body.querySelectorAll('.gt').filter((b) => b.getAttribute('aria-checked') === 'true').length === 1, 'the sheet has the gold ring on the plain tile');
    chip(dg03a).click();
    check(p.tools.state().ground === dg03a && on().join() === '487' && p.host.name.textContent === 'Dirt next to gravel: north edge (dg03a)', 'any single tile of the sheet can be chosen: dg03a, and the kind chip of Dirt stays on: ' + p.host.name.textContent);
    check(p.host.tile.querySelector('span').textContent === 'Dirt \u00b7 dg03a' && !sheet.ctl.closed, 'the phone\'s button names it, and the sheet stays open for the brush');
    const kr = sheet.body.querySelectorAll('.brange')[0];
    kr.value = '5';
    for (const fn of kr.listeners.input || []) fn({ type: 'input' });
    check(p.tools.state().brush === 5 && p.brush(5).getAttribute('aria-checked') === 'true', 'the sheet\'s brush row is the page\'s brush');
    p.brush(1).click();
    const pt = p.painter();
    check(pt.begin({ row: 8, column: 8 }) === true, 'a stroke with the tile begins');
    pt.end();
    const f = p.file(), at = f.dictAt + 2 + 11 * dg03a;
    check(Edit.tile(f, 1, 8 * 30 + 8) === dg03a && f.names === dg03a + 1 && String.fromCharCode.apply(null, Array.from(f.bytes.slice(at, at + 5))) === 'dg03a', 'it paints the cell with that tile and writes its game name in the dictionary');
    kinds()[1].click();
    check(p.sheets.length === 2 && p.sheets[1].spec.title === 'Dirt: all its tiles', 'a tap on the chosen kind opens the sheet again, with the custom tile chosen');
    p.sheets[1].body.querySelectorAll('.gl').filter((b) => b.textContent === 'd01b')[0].click();
    check(p.tools.state().ground === 488 && p.host.name.textContent === 'Dirt (d01b)', 'a look-alike tile can be chosen too: ' + p.host.name.textContent);
    // the phone's sheet: five kinds, the chosen one opens the sheet, and a button for it in the footer
    p.host.tile.click();
    const picker = p.sheets[p.sheets.length - 1];
    check(picker.spec.title === 'Ground' && picker.body.querySelectorAll('.pick').length === 5 && picker.body.querySelectorAll('.pick').filter((b) => b.getAttribute('aria-checked') === 'true').length === 1, 'the phone\'s sheet has the five kinds');
    equal(picker.spec.buttons.map((b) => b.text), ['All dirt tiles', 'Close'], 'with a button for all the tiles of the chosen kind');
    picker.body.querySelectorAll('.pick')[0].click();
    check(p.tools.state().ground === 485 && picker.ctl.closed, 'a tap on another kind chooses its plain tile and closes the sheet');
    p.host.tile.click();
    const again = p.sheets[p.sheets.length - 1];
    again.body.querySelectorAll('.pick')[0].click();
    check(p.sheets[p.sheets.length - 1].spec.title === 'Gravel: all its tiles', 'a tap on the chosen kind there opens its sheet');
    // the ground tool without the sheets (the lazy part did not load) still paints and names the plain tiles
    const q = toolsPage(open(9, 30, 30));
    q.tools.use(null, null);
    check(q.host.name.textContent === 'Gravel' && q.host.swatches.children.length === 5, 'without the sheets the page keeps its five kinds');

    // Plants (walk-under canopy and power-up droppers)
    const pm = toolsPage(File.parse(mapWith(8, 24, 24, 486, [], []))), pmt = pm.painter();
    pm.button('objects').click();
    const clover = pm.host.swatches.children.filter((b) => b.getAttribute('data-tile') === '404')[0];
    clover.click();
    check(pm.tools.state().objects === 404 && !pm.host.stepper.hidden && pmt.tapOnly() && pm.host.stepValue.textContent === 'Off', 'choosing a plant shows the power-up drop interval stepper (Off = walk-under canopy)');
    pmt.begin({ row: 6, column: 6 });
    let play = Blocks.layout(pm.file());
    check(play.records.length === 1 && play.records[0].tile === 404 && play.waypoints.length === 1 && play.waypoints[0].flag === 0 && pm.file().plants.length === 1, 'a canopy plant writes Block 1 and an 8-byte Block 4 waypoint (flag 0)');
    pm.host.stepMore.click(); pm.host.stepMore.click();
    check(pm.tools.state().dropper === 30 && pm.host.stepValue.textContent === '30s', 'stepping up sets a 30s power-up dropper interval');
    pmt.begin({ row: 8, column: 8 });
    play = Blocks.layout(pm.file());
    check(play.records.length === 2 && play.waypoints.length === 2 && play.waypoints[1].flag === 1 && play.waypoints[1].interval === 30, 'a power-up plant writes a 52-byte Block 4 dropper (flag 1) and names the 5 power-ups');
    // Pan does not drag a placed item any more (Pick up does: tests/scripts/web_mapmaker_pickup_check.js)
    pm.button('pan').click();
    check(pmt.canMove({ row: 6, column: 6 }) === false && pmt.begin({ row: 6, column: 6 }) === false, 'Pan ignores a placed plant: a drag looks around');
    play = Blocks.layout(pm.file());
    check(play.records[0].row === 6 && play.records[0].column === 6, 'and the plant stays where it is');

    // Erasing a leftover solid ground cell clears the Layer-1 solid footprint so starting ants do not avoid the area
    const hb = toolsPage(File.parse(mapWith(8, 24, 24, 486, [], []))), hbt = hb.painter();
    hb.button('bigger').click(); hbt.begin({ row: 10, column: 10 });
    check((Edit.flags(hb.file(), 1, 10 * 24 + 10) & 1) === 1, 'placing a Bigger object marks its footprint solid on Layer 1');
    put(hb.file(), 1, 12, 12, 486, 1, 0);
    hb.button('erase').click(); hbt.begin({ row: 12, column: 12 }); hbt.end();
    check((Edit.flags(hb.file(), 1, 12 * 24 + 12) & 1) === 0, 'erasing an empty cell with a leftover solid flag clears the solid bit');
    hb.button('erase').click(); hbt.begin({ row: 10, column: 10 }); hbt.end();
    hb.button('ants').click(); hbt.begin({ row: 10, column: 10 });
    check(Blocks.layout(hb.file()).records.some((r) => r.row === 10 && r.column === 10), 'starting ants can be placed directly on the cell where the erased object stood');

    // Resizing a map (growing and cropping) updates dimensions, cells, anchors and Blocks 1..4 in one undoable step, and drops partially clipped multi-cell objects whole
    const rs = toolsPage(File.parse(mapWith(8, 20, 20, 486, [], []))), rst = rs.painter();
    rs.button('bigger').click(); rst.begin({ row: 4, column: 6 });
    const grow = Blocks.resizeMap(rs.file(), 26, 30, 0, 0);
    check(rs.tools.act(grow) === true && rs.file().rows === 26 && rs.file().columns === 30, 'growing a 20x20 map to 26x30 updates rows and columns');
    const shrink = Blocks.resizeMap(rs.file(), 16, 18, 7, 6);
    check(rs.tools.act(shrink) === true && rs.file().rows === 16 && rs.file().columns === 18 && Edit.tile(rs.file(), 2, 0 * 18 + 5) === File.EMPTY2 && (Edit.flags(rs.file(), 1, 1 * 18 + 5) & 1) === 0, 'shrinking the map to 16x18 drops a partially clipped multi-cell object whole and clears its solid bit');
    rs.doc.key('z', { ctrlKey: true });
    check(rs.file().rows === 26 && rs.file().columns === 30, 'Ctrl+Z undoes the shrink back to 26x30');
    rs.doc.key('z', { ctrlKey: true });
    check(rs.file().rows === 20 && rs.file().columns === 20, 'Ctrl+Z undoes the grow back to 20x20');
    const raw9 = mapWith(9, 20, 20, 486, [], []); raw9[14] = 0;
    const rs9 = toolsPage(File.parse(raw9));
    check(rs9.file().bytes[14] === 0 && rs9.tools.act(Blocks.resizeMap(rs9.file(), 95, 20, 0, 0)) === true && rs9.file().rows === 95 && rs9.file().bytes[14] === 1, 'resizing an Extended map past 90 rows upgrades rules to 1');
    rs9.doc.key('z', { ctrlKey: true });
    check(rs9.file().rows === 20 && rs9.file().bytes[14] === 0, 'Ctrl+Z undoes the Extended map resize and restores rules to 0');

    // Auto edges: Off by default, remembered in localStorage, sets edge pieces in one Undo step, and preserves hand-placed custom tiles
    const storeMem = {};
    const st = { getItem: (k) => storeMem[k] || null, setItem: (k, v) => { storeMem[k] = String(v); } };
    const ae = toolsPage(open(8, 12, 12), { storage: st }), aet = ae.painter();
    check(ae.tools.state().autoEdges === false && ae.host.autoEdges[0].disabled === true && ae.host.autoEdges[0].textContent === 'Auto edges: Off', 'Auto edges starts Off and disabled until ground and edges load');
    ae.tools.use(Ground, null);
    ae.tools.useEdges(Edges);
    check(ae.host.autoEdges[0].disabled === false && ae.host.autoEdges[1].hidden === false, 'loading Ground and Edges enables the Auto edges switch, visible on phone while Ground is chosen');
    ae.button('objects').click();
    check(ae.host.autoEdges[1].hidden === true, 'the phone Auto edges button hides when another tool is chosen');
    ae.button('ground').click();
    ae.host.autoEdges[0].click();
    check(ae.tools.state().autoEdges === true && ae.host.autoEdges[0].textContent === 'Auto edges: On' && storeMem.ants_mapmaker_auto_edges === '1', 'clicking Auto edges turns it On and saves 1 in localStorage');
    const ae2 = toolsPage(open(8, 12, 12), { storage: st });
    check(ae2.tools.state().autoEdges === true, 'a new page instance reads Auto edges: On from localStorage');
    // Paint a 3x3 dirt patch into gravel with Auto edges: On -> surrounding gravel cells get gd edge/corner pieces, and one Undo restores the exact bytes
    const aeMap = open(8, 12, 12, 488);
    Edit.ensureTile(aeMap, 487, 'd01a');
    const ae3 = toolsPage(aeMap, { storage: st }), aet3 = ae3.painter();
    ae3.tools.use(Ground, null);
    ae3.tools.useEdges(Edges);
    ae3.host.swatches.children[1].click(); // Dirt (487)
    ae3.brush(3).click();
    const beforeAutoBytes = digest(ae3.file().bytes);
    aet3.begin({ row: 5, column: 5 }); aet3.end();
    const gdNorth = Ground.edges('gravel', 'dirt')[6].id; // S of gravel = dirt -> shape 08 (index 6)
    check(Edit.tile(ae3.file(), 1, 5 * 12 + 5) === 487 && Edit.tile(ae3.file(), 1, 3 * 12 + 5) === gdNorth, 'painting 3x3 dirt in gravel with Auto edges: On sets gravel-next-to-dirt edge pieces around the stroke');
    const entryAt = (f, k) => Buffer.from(f.bytes.subarray(f.dictAt + 2 + 11 * k, f.dictAt + 13 + 11 * k)).toString('latin1').split('\0')[0];
    check(entryAt(ae3.file(), gdNorth) === Ground.name(gdNorth), 'and names the auto-placed edge pieces in the dictionary');
    ae3.doc.key('z', { ctrlKey: true });
    check(digest(ae3.file().bytes) === beforeAutoBytes && ae3.host.undo.disabled === true, 'one Undo restores the stroke, the auto-placed edge pieces and the dictionary byte for byte');
}

const cases = [['A the brush and the line', caseA], ['A2 anchors', anchors], ['B strokes', caseB], ['C fill', caseC], ['D undo and redo', caseD], ['E the dictionary', caseE], ['F the palette', caseF], ['G the tools on a fake page', caseG], ['H the canvas on a fake page', caseH], ['H2 a tap tool on the canvas', caseH2], ['I hills', caseI], ['J starting ants and food piles', caseJ], ['J2 the history of the lists is bounded', caseJ2], ['J3 what the first checks left unsaid', caseJ3], ['K Hills, Ants and Food on a fake page', caseK], ['L the Bigger tool on a fake page', caseL], ['M the properties of the map as one step', caseM], ['N the kinds of ground on a fake page', caseN]];
for (const [name, fn] of cases) {
    const before = failures;
    try { fn(); } catch (e) { failures++; console.log('    FAILED: ' + name + ' stopped: ' + (e && e.stack || e)); }
    console.log('RUNNING: ' + name + ' ... ' + (failures === before ? 'PASS' : 'FAIL'));
}
console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
