#!/usr/bin/env node
// The map maker's bigger objects (web/front/mapmaker_objects.js, mapmaker_solid.js, the Bigger list of mapmaker_palette.js; docs/MAP_EDITOR.md, "Bigger objects") held to the 540 maps of the
// library and to made-up maps of both kinds, with no browser and no map core (the real core's answers are tests/scripts/web_mapmaker_core_check.js):
//
//   node tests/scripts/web_mapmaker_objects_check.js [--front DIR] [--list-objects]
//
//   --front DIR      the folder of the mapmaker_*.js files to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//   --list-objects   print, for each tile that stands on several cells in the library (but the hills, the bombs, the power-ups, food and plants), the number of maps, of objects and its commonest footprint (the Python test reads the same files again)
//
//   A  the table of solid tiles is the game's (src/ants_sim/movement_tables_data.inc, the tile flag 0x01 of every one of the 1,344 ids)
//   B  the list of bigger objects is what the library says: the game's names, the tiles that stand on several cells in at least 30 of the 540 maps (and none that is missing), and the footprint
//      that the maps have most often; nothing in it is a hill, a bomb, a power-up, food or a plant of the flower droppers
//   C  an object put on every map of the library (all 29, each in a free place) and taken away again: the cells, the anchors, the flags and the solid bits are those of the library's own objects,
//      the eraser takes it whole, and the layers are the same bytes as before; undo and redo are byte for byte; the same on the same maps as Extended maps (version 9)
//   D  what is in the way: a hill, starting ants or a food pile are never taken away by a tap (a sentence says so); an object under the new one goes whole; the same place twice is nothing
//   F  the solid bit for the objects of the Objects tool too (a rock, a power-up), set where they stand and cleared when they go, other flags kept, a hill's bits left alone
//   G  every object of the 540 maps taken away cell by cell: no cell keeps the solid bit without an object (row 0 and hills apart)
//   E  the edge of the map, a version 9 map wider than 256 (relative anchors), a dictionary that is too short, the box that the canvas marks
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Edit = load('mapmaker_edit.js'), Blocks = load('mapmaker_blocks.js'), Palette = load('mapmaker_palette.js'), Objects = load('mapmaker_objects.js'), Solid = load('mapmaker_solid.js'), View = load('mapmaker_view.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }
function same(a, b) { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; }

const EMPTY2 = 0x7FFE;
const names = fs.readFileSync(path.join(ROOT, 'src', 'ants_assets', 'lvl_tile_names.inc'), 'utf8').split('\n').filter((l) => l.startsWith('"')).map((l) => l.replace(/^"/, '').replace(/",?\s*$/, ''));

// the tile flags of the game (kTileFlags of src/ants_sim/movement_tables_data.inc): 0x01 solid, 0x02 food, 0x04 power-up, 0x10 plant
function tileFlags() {
    const text = fs.readFileSync(path.join(ROOT, 'src', 'ants_sim', 'movement_tables_data.inc'), 'utf8'), at = text.indexOf('kTileFlags[1344] = {');
    const body = text.slice(text.indexOf('{', at) + 1, text.indexOf('};', at)).replace(/\/\*[\s\S]*?\*\//g, '');
    return (body.match(/0x[0-9a-fA-F]+|\d+/g) || []).map((v) => Number(v));
}
const FLAGS = tileFlags();

function libraryFiles() {
    const found = [];
    for (const dir of ['Original-Ants/Maps', 'Community-Maps']) {
        for (const name of fs.readdirSync(path.join(ROOT, dir)).sort()) if (/\.lvl$/i.test(name)) found.push(dir + '/' + name);
    }
    return found;
}
function readAll() {
    return libraryFiles().map((rel) => { const bytes = new Uint8Array(fs.readFileSync(path.join(ROOT, rel))), file = File.parse(bytes); return { rel, bytes, file }; });
}

// The objects of a version 8 map that stand on more than one cell: groups of layer-2 cells of one tile and one anchor, where the anchor cell is in the group and flagged. [{tile, cells: [[dr, dc]]}]
function multiCellObjects(f) {
    const groups = new Map();
    for (let i = 0; i < f.cells; i++) {
        const t = Edit.tile(f, 2, i);
        if (t === EMPTY2) continue;
        const w = Edit.third(f, 2, i), key = t + ',' + (w & 255) + ',' + (w >> 8), r = Math.floor(i / f.columns), c = i % f.columns;
        if (!groups.has(key)) groups.set(key, { tile: t, ar: w & 255, ac: w >> 8, cells: [], origin: false });
        const g = groups.get(key);
        g.cells.push([r - g.ar, c - g.ac]);
        if (r === g.ar && c === g.ac && (Edit.flags(f, 2, i) & 1)) g.origin = true;
    }
    return Array.from(groups.values()).filter((g) => g.origin && g.cells.length > 1);
}
// the tiles that the Bigger tool does not offer, whatever their footprint: the hills, the bombs, the power-ups, food and the plants of the flower droppers
function excluded(t) { return (t >= 245 && t <= 248) || (t >= 129 && t <= 132) || (FLAGS[t] & (0x02 | 0x04 | 0x10)) !== 0; }
const sig = (cells) => cells.map((rc) => rc[0] + ',' + rc[1]).sort().join(' ');

// what the library has of each tile that stands on several cells: {maps, objects, shapes: Map(sig -> count)}
function measure(maps) {
    const out = new Map();
    for (const m of maps) {
        const seen = new Set();
        for (const g of multiCellObjects(m.file)) {
            if (!out.has(g.tile)) out.set(g.tile, { maps: 0, objects: 0, shapes: new Map() });
            const o = out.get(g.tile), s = sig(g.cells);
            o.objects++; o.shapes.set(s, (o.shapes.get(s) || 0) + 1);
            if (!seen.has(g.tile)) { seen.add(g.tile); o.maps++; }
        }
    }
    return out;
}
// the commonest footprint of a tile (a tie goes to the bigger one)
function commonest(o) {
    let best = null;
    for (const [s, n] of o.shapes) if (!best || n > best[1] || (n === best[1] && s.split(' ').length > best[0].split(' ').length)) best = [s, n];
    return best;
}

function listObjects(maps) {
    const seen = measure(maps);
    for (const t of Array.from(seen.keys()).filter((id) => !excluded(id)).sort((a, b) => a - b)) {
        const o = seen.get(t), best = commonest(o);
        console.log([t, o.maps, o.objects, best[0].split(' ').length, best[1]].join('\t'));
    }
}

// ---- A ----
function caseA() {
    check(FLAGS.length === 1344, 'the game\'s tile flags are 1,344 (' + FLAGS.length + ')');
    let bad = [], solid = 0;
    for (let id = 0; id < 1344; id++) { const want = (FLAGS[id] & 1) !== 0; if (want) solid++; if (Solid.is(id) !== want) bad.push(id); }
    check(bad.length === 0, 'the table of solid tiles is the game\'s for all 1,344 ids (' + bad.slice(0, 8).join(', ') + ' are not)');
    check(solid === 192, 'the game has 192 solid tiles (' + solid + ')');
    check(!Solid.is(EMPTY2) && !Solid.is(-1) && !Solid.is(1344) && !Solid.is(65535), 'an empty cell and an id that is not a tile are not solid');
    // (a bomb and a bridge are walkable: the game's table has no solid flag for them, and the palette's Bridge chip must not put one)
    for (const row of Palette.OBJECTS) if (names[row[0]]) { const walk = (row[0] >= 129 && row[0] <= 132) || row[0] === 37; check(Solid.is(row[0]) === !walk && Solid.sets(row[0]) === !walk, 'the object ' + row[1] + (walk ? ' is walkable (a bomb or a bridge: not solid)' : ' is solid, and sets the solid bit where it stands')); }
    check(!Solid.is(37) && !Solid.sets(37) && !Solid.clears(0x7FFE) && Solid.clears(37), 'a bridge piece (37) is not solid and puts no solid bit, and taking one away clears the bit of its cell like any object');
    check([245, 246, 247, 248].every((t) => Solid.is(t) && !Solid.sets(t)) && !Solid.sets(129) && Solid.sets(302), 'a hill is solid but owns no solid bits of its cells (a library hill has them on a few cells only); a rock does');
}

// ---- B ----
function caseB(maps) {
    const list = Palette.BIGGER, items = Palette.items('bigger');
    check(list.length === 29 && items.length === 29, 'the list has 29 bigger objects (' + list.length + ')');
    check(new Set(list.map((r) => r[0])).size === list.length, 'no tile twice');
    for (const row of list) check(names[row[0]] === row[1], 'tile ' + row[0] + ' is ' + names[row[0]] + ' in the game\'s list, and ' + row[1] + ' in the palette');
    for (const it of items) {
        check(it.label === it.name && it.solid === Solid.sets(it.id) && it.color && it.cells.length >= 2, it.name + ': a label that is the game\'s name, its solid flag, a color and at least two cells');
        check(it.cells.some((rc) => rc[0] === 0 && rc[1] === 0), it.name + ': the anchor is one of its cells');
        check(new Set(it.cells.map((rc) => rc.join())).size === it.cells.length, it.name + ': no cell twice');
        check(!excluded(it.id) && Solid.is(it.id), it.name + ' is not a hill, a bomb, a power-up, food or a plant, and it is solid');
        const b = it.box, rs = it.cells.map((rc) => rc[0]), cs = it.cells.map((rc) => rc[1]);
        equal([b.r0, b.r1, b.c0, b.c1], [Math.min(0, ...rs), Math.max(0, ...rs), Math.min(0, ...cs), Math.max(0, ...cs)], it.name + ': its box');
    }
    const seen = measure(maps), listed = new Set(list.map((r) => r[0]));
    const wanted = Array.from(seen.keys()).filter((t) => !excluded(t) && seen.get(t).maps >= 30 && commonest(seen.get(t))[0].split(' ').length > 1).sort((a, b) => a - b);
    equal(wanted, Array.from(listed).sort((a, b) => a - b), 'the tiles that stand on several cells in at least 30 maps (a list made again from the library)');
    for (const row of list) {
        const o = seen.get(row[0]), best = o && commonest(o);
        check(!!o && best[0] === sig(Palette.items('bigger').filter((i) => i.id === row[0])[0].cells), row[1] + ': the footprint is the one that the maps have most often');
        check(!!o && best[1] * 2 >= o.objects, row[1] + ': it is the shape of at least half of its ' + (o ? o.objects : 0) + ' objects (' + (best ? best[1] : 0) + ')');
    }
    const order = list.map((r) => seen.get(r[0]).maps);
    check(order.every((n, i) => i === 0 || order[i - 1] >= n), 'the list is the most used first');
    console.log('    (' + list.length + ' objects; the commonest is used in ' + order[0] + ' maps, the least in ' + order[order.length - 1] + ')');
}

// ---- C ----
function toV9(bytes) {
    const f = File.parse(bytes), dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.length);
    let n = 10;
    while (n < 40 && bytes[n] !== 0) n++;
    const desc = bytes.subarray(10, n), head = new Uint8Array(18 + desc.length), hv = new DataView(head.buffer);
    hv.setUint32(0, 9, true); hv.setUint32(4, dv.getUint32(4, true), true); hv.setUint16(8, dv.getUint16(8, true), true); hv.setUint32(10, 0, true); hv.setUint16(14, 0, true); hv.setUint16(16, desc.length, true);
    head.set(desc, 18);
    const rest = bytes.slice(40), out = new Uint8Array(head.length + rest.length);
    out.set(head, 0); out.set(rest, head.length);
    const g = File.parse(out);
    if (!g.ok || g.kind !== 'extended') throw new Error('the conversion for the test does not read');
    for (let i = 0; i < g.cells; i++) {
        const t = Edit.tile(g, 2, i);
        if (t === EMPTY2 || (t >= 129 && t <= 132)) continue;
        const r = Math.floor(i / g.columns), c = i % g.columns, w = Edit.third(f, 2, i), ar = w & 255, ac = w >> 8;
        const p = g.layer2 + 6 * i + 4, rel = ((ar - r) & 255) | (((ac - c) & 255) << 8);
        out[p] = rel & 255; out[p + 1] = rel >> 8;
    }
    return out;
}

function editorOn(file) { return Edit.create(file, { rebase: (b) => File.parse(b), erase: Blocks.eraseAt }); }
function layersOf(f) { return f.bytes.slice(f.layer1, f.blocksAt); }
// the same level: layer 1 byte for byte, layer 2 byte for byte except the flags and the third word of a cell that is empty on both sides (an empty cell of a library map can hold leftovers that
// no reader looks at; the eraser writes a clean empty cell, so a spot that held leftovers comes back clean)
function sameLevel(f, a, b) {
    if (a.length !== b.length) return false;
    const half = 6 * f.cells;
    for (let i = 0; i < half; i++) if (a[i] !== b[i]) return false;
    for (let i = 0; i < f.cells; i++) {
        const p = half + 6 * i, ta = a[p] | (a[p + 1] << 8), tb = b[p] | (b[p + 1] << 8);
        if (ta !== tb) return false;
        if (ta === EMPTY2) continue;
        for (let k = 2; k < 6; k++) if (a[p + k] !== b[p + k]) return false;
    }
    return true;
}
// the name of a tile in the dictionary, read up to its first zero byte as the original reads it
function nameOf(f, id) {
    let out = '';
    for (let i = 0; i < 11 && f.bytes[f.dictAt + 2 + 11 * id + i] !== 0; i++) out += String.fromCharCode(f.bytes[f.dictAt + 2 + 11 * id + i]);
    return out;
}
function firstDifference(a, b, f) {
    for (let i = 0; i < Math.min(a.length, b.length); i++) if (a[i] !== b[i]) return ' [first difference: cell ' + (Math.floor(i / 6) % f.cells) + ', layer ' + (i >= 6 * f.cells ? 2 : 1) + ', word ' + Math.floor((i % 6) / 2) + ': ' + b[i] + ' became ' + a[i] + ']';
    return a.length === b.length ? '' : ' [the layers are ' + a.length + ' and ' + b.length + ' bytes]';
}
// a place where the whole object stands: every cell of its footprint is empty on layer 2, on the map, on ground that is not solid, with nothing of another tool on it
function freeSpot(file, item, lay) {
    const box = item.box;
    for (let r = 1 - box.r0; r < file.rows - box.r1; r += 1) {              // (not on row 0: its solid bit is never cleared, as in the original, so an object there does not come back clean)
        for (let c = -box.c0; c < file.columns - box.c1; c += 1) {
            let ok = true;
            for (const rc of item.cells) {
                const idx = (r + rc[0]) * file.columns + c + rc[1];
                if (Edit.tile(file, 2, idx) !== EMPTY2 || (Edit.flags(file, 1, idx) & 1) || Blocks.inTheWay(file, idx)) { ok = false; break; }
            }
            if (ok) return { r, c };
        }
    }
    return null;
}

function putAndTakeAway(m, bytes, item, tag, stats) {
    const f0 = File.parse(bytes.slice()), spot = freeSpot(f0, item);
    if (!spot) { stats.nospot++; return; }
    const ed = editorOn(f0), before = layersOf(f0), r = spot.r, c = spot.c;
    const change = Objects.place(ed.file, item, r, c);
    if (change.error) { check(false, tag + ' ' + item.name + ': refused in a free place: ' + change.error); return; }
    const dirty = ed.act(change), f = ed.file;
    if (!dirty) { check(false, tag + ' ' + item.name + ': nothing was changed'); return; }
    stats.placed++;
    const dict = f.dictAt, grew = f.names > f0.names;
    check(f.names > item.id && nameOf(f, item.id) !== '.', tag + ' ' + item.name + ': the dictionary has a name for the tile that is not "." (the original does not draw a tile named "."): ' + nameOf(f, item.id));
    let bad = '';
    for (const rc of item.cells) {
        const idx = (r + rc[0]) * f.columns + c + rc[1], origin = rc[0] === 0 && rc[1] === 0;
        const want = f.version === 8 ? ((r & 255) | ((c & 255) << 8)) : (((-rc[0]) & 255) | (((-rc[1]) & 255) << 8));
        if (Edit.tile(f, 2, idx) !== item.id) bad = 'a cell does not hold the tile';
        else if (Edit.flags(f, 2, idx) !== (origin ? 1 : 0)) bad = 'the origin is the only cell that is flagged';
        else if (Edit.third(f, 2, idx) !== want) bad = 'the anchor of a cell is not ' + want;
        else if (!(Edit.flags(f, 1, idx) & 1)) bad = 'the solid bit is not set';
        else if (Edit.anchorIndex(f, idx) !== r * f.columns + c) bad = 'the anchor does not lead to the origin';
        else if (Edit.objectCells(f, idx).length !== item.cells.length) bad = 'the cells of the object round its anchor are ' + Edit.objectCells(f, idx).length;
    }
    check(bad === '', tag + ' ' + item.name + ': ' + (bad || 'every cell is as the library\'s objects are'));
    // nothing else changed in the layers (the dictionary may have grown: the layers are read from where they now are)
    const after = layersOf(f), moved = [];
    for (let i = 0; i < after.length; i++) if (after[i] !== before[i]) moved.push(Math.floor(i / 6));
    const cellsMoved = new Set(moved.map((k) => k % f.cells + 0));
    const inside = new Set(item.cells.map((rc) => (r + rc[0]) * f.columns + c + rc[1]));
    let outside = 0;
    for (const k of moved) if (!inside.has(k % f.cells)) outside++;
    check(outside === 0 && cellsMoved.size <= inside.size, tag + ' ' + item.name + ': only the cells of its footprint changed (' + outside + ' cells outside)');
    // the eraser takes it whole, one tap on any cell of it
    const pick = item.cells[(item.id + r) % item.cells.length], er = ed.begin('erase', null);
    er.stamp(r + pick[0], c + pick[1], 1);
    er.end();
    check(sameLevel(ed.file, layersOf(ed.file), before), tag + ' ' + item.name + ': taken away by one tap, the layers are the level they were (the solid bits too)' + firstDifference(layersOf(ed.file), before, ed.file));
    if (same(layersOf(ed.file), before)) stats.exact++;
    ed.undo();
    check(same(layersOf(ed.file), after), tag + ' ' + item.name + ': undo of the eraser gives the object back');
    ed.undo();
    check(same(layersOf(ed.file), before), tag + ' ' + item.name + ': undo of the object gives the layers back');
    ed.redo();
    check(same(layersOf(ed.file), after), tag + ' ' + item.name + ': redo puts the object back');
    stats.erased++;
    if (grew) stats.grew++;
}

function caseC(maps) {
    const items = Palette.items('bigger'), stats = { placed: 0, erased: 0, nospot: 0, grew: 0, exact: 0 }, v9 = { placed: 0, erased: 0, nospot: 0, grew: 0, exact: 0 };
    let n = 0;
    for (const m of maps) {
        n++;
        for (const item of items) putAndTakeAway(m, m.bytes, item, m.rel, stats);
        // the same map as an Extended map: every ninth map, to keep the time down (the arithmetic is the same for every map)
        if (n % 9 === 0) { const x = toV9(m.bytes); for (const item of items) putAndTakeAway(m, x, item, m.rel + ' (version 9)', v9); }
    }
    check(stats.placed >= 540 * 29 * 0.85, stats.placed + ' objects were put on the 540 maps and taken away again (' + stats.nospot + ' had no free place for one)');
    check(stats.erased === stats.placed, 'every one was taken away');
    check(v9.placed >= 60 * 29 * 0.85 && v9.erased === v9.placed, v9.placed + ' more on the maps as Extended maps, and taken away');
    check(stats.exact >= stats.placed * 0.95, stats.exact + ' of the ' + stats.placed + ' came back as the very same bytes (the others had leftovers in an empty cell)');
    console.log('    (' + stats.placed + ' + ' + v9.placed + ' objects; ' + stats.grew + ' needed the dictionary to grow)');
}

// ---- D ----
function makeMap(version, rows, columns, namesCount, records, piles) {
    namesCount = namesCount || 486;
    const cells = rows * columns, head = version === 8 ? 8 + 2 + 30 : 8 + 2 + 4 + 2 + 2;
    const tailLength = 2 + 6 * records.length + 2 + piles.reduce((a, p) => a + 10 + 4 * p.stages.length, 0) + 12;
    const out = new Uint8Array(head + 2 + 11 * namesCount + 8 + 12 * cells + tailLength), view = new DataView(out.buffer);
    let at = 0;
    const dword = (v) => { view.setUint32(at, v, true); at += 4; }, word = (v) => { view.setUint16(at, v, true); at += 2; };
    if (version === 8) { dword(8); dword(1); word(10); at += 30; } else { dword(9); dword(1); word(10); dword(0); word(1); word(0); }
    word(namesCount - 1);
    for (let i = 0; i < namesCount; i++) { out[at] = i === 485 ? 0x67 : 0x2E; if (i === 485) { out[at + 1] = 0x30; out[at + 2] = 0x31; out[at + 3] = 0x61; } at += 11; }
    dword(rows); dword(columns);
    for (let i = 0; i < cells; i++) { word(485); word(0); word(0xCDCD); }
    for (let i = 0; i < cells; i++) { word(EMPTY2); word(0); word(0); }
    word(records.length);
    for (const r of records) { word(r[0]); word(r[1]); word(r[2]); }
    word(piles.length);
    for (const p of piles) { word(p.row); word(p.column); word(p.units); word(p.points); word(p.stages.length); for (const s of p.stages) { word(s[0]); word(s[1]); } }
    word(0); word(EMPTY2); word(0); word(6);
    return out;
}
function open(version, rows, columns, namesCount, records, piles) { const f = File.parse(makeMap(version, rows, columns, namesCount, records || [], piles || [])); if (!f.ok) throw new Error(f.error); return f; }
function putCell(f, layer, r, c, t, fl, a) { const p = (layer === 1 ? f.layer1 : f.layer2) + 6 * (r * f.columns + c); [t, fl, a].forEach((v, k) => { f.bytes[p + 2 * k] = v & 255; f.bytes[p + 2 * k + 1] = v >> 8; }); }
function item(name) { const i = Palette.items('bigger').filter((x) => x.name === name)[0]; if (!i) throw new Error('no ' + name); return i; }
function tileAt(f, r, c) { return Edit.tile(f, 2, r * f.columns + c); }

function caseD() {
    for (const version of [8, 9]) {
        const tag = 'version ' + version + ': ';
        const g1 = item('grass4'), big = item('bearbod');
        // a hill in the way
        const f = open(version, 30, 30);
        const ed0 = editorOn(f);
        ed0.act(Blocks.placeHill(ed0.file, 0, 8, 8));
        const hillBytes = layersOf(ed0.file), refused = Objects.place(ed0.file, g1, 8, 8);
        check(typeof refused.error === 'string' && /hill is in the way/.test(refused.error), tag + 'an object over a hill is refused in plain words: ' + refused.error);
        check(same(layersOf(ed0.file), hillBytes), tag + 'and nothing changed');
        const near = Objects.place(ed0.file, g1, 8, 11);          // (touching the hill's last column: its footprint is the cells before the anchor)
        check(near.error === undefined || /hill/.test(near.error), tag + 'next to a hill is allowed or refused only because of the hill');
        const clear = Objects.place(ed0.file, g1, 20, 20);
        check(!clear.error && clear.cells.length === 4, tag + 'a place away from it is fine');
        // starting ants in the way
        const fa = open(version, 30, 30, 486, [[154, 10, 10]]);
        const ea = editorOn(fa), why = Objects.place(ea.file, g1, 10, 10);
        check(/Starting ants are in the way/.test(why.error || ''), tag + 'an object over starting ants is refused: ' + why.error);
        check(!Objects.place(ea.file, g1, 10, 12).error, tag + 'beside the ants it is fine');
        // a food pile in the way (its cell, and the object that its cell anchors)
        const fp = open(version, 30, 30, 486, [], [{ row: 12, column: 12, units: 10, points: 30, stages: [[10, 280], [0, EMPTY2]] }]);
        const ep = editorOn(fp), way = Objects.place(ep.file, g1, 12, 12);
        check(/food pile is in the way/.test(way.error || ''), tag + 'an object over a food pile is refused: ' + way.error);
        for (let r = 11; r <= 12; r++) for (let c = 11; c <= 12; c++) putCell(ep.file, 2, r, c, 280, r === 12 && c === 12 ? 1 : 0, version === 8 ? (12 | (12 << 8)) : (((12 - r) & 255) | (((12 - c) & 255) << 8)));
        const body = Objects.place(ep.file, g1, 11, 11);          // (touches the cell at 11, 11 of the pile's object, not the pile's own cell)
        check(/food pile is in the way/.test(body.error || ''), tag + 'and one over the object that shows the pile, away from the pile\'s own cell: ' + body.error);
        // an object under the new one goes whole, and so does a small one
        const fo = open(version, 30, 30);
        const eo = editorOn(fo);
        eo.act(Objects.place(eo.file, big, 15, 15));
        const placed = layersOf(eo.file);
        const over = Objects.place(eo.file, g1, 15, 17);           // (touches the right edge of bearbod: one cell of it is under the new object)
        check(!over.error, tag + 'an object put over a bigger one is allowed');
        eo.act(over);
        let left = 0;
        for (let r = 0; r < 30; r++) for (let c = 0; c < 30; c++) if (tileAt(eo.file, r, c) === big.id) left++;
        check(left === 0, tag + 'the bigger object under it went whole (' + left + ' cells of it are left)');
        let mine = 0;
        for (let r = 0; r < 30; r++) for (let c = 0; c < 30; c++) if (tileAt(eo.file, r, c) === g1.id) mine++;
        check(mine === 4, tag + 'and the new one is whole (' + mine + ' cells)');
        let solid = 0;
        for (let i = 0; i < eo.file.cells; i++) if (Edit.flags(eo.file, 1, i) & 1) solid++;
        check(solid === 4, tag + 'the solid bits of the object that went are gone, the new one\'s are set (' + solid + ')');
        eo.undo();
        check(same(layersOf(eo.file), placed), tag + 'one undo gives the bigger object back, whole');
        // the same object on the same place is nothing; on another place it is a new object
        const again = Objects.place(eo.file, big, 15, 15);
        check(!again.error && again.cells.length === 0 && again.solid.length === 0, tag + 'the same object on the same place changes nothing');
        const steps = eo.history.size().strokes;
        check(eo.act(again) === null && eo.history.size().strokes === steps, tag + 'and it is no step of the history');
        // an object whose tile is not in the table of solid tiles (a plant) that goes whole with the cell under the new object: the solid bit of its other cell goes too
        const fq = open(version, 30, 30), eq = editorOn(fq);
        for (const c of [10, 11]) { putCell(eq.file, 2, 10, c, 410, c === 10 ? 1 : 0, version === 8 ? (10 | (10 << 8)) : (((10 - 10) & 255) | (((10 - c) & 255) << 8))); putCell(eq.file, 1, 10, c, 485, 1, 0xCDCD); }
        eq.act(Objects.place(eq.file, g1, 11, 10));
        check(tileAt(eq.file, 10, 11) === EMPTY2 && (Edit.flags(eq.file, 1, 10 * 30 + 11) & 1) === 0, tag + 'a plant (not in the table of solid tiles) that goes under a new object loses the solid bit of its other cell too');
        // a small object put on a bigger one takes it away whole (the Objects tool, as before) and its solid bit stays
        const st = eo.begin('objects', 302);
        st.stamp(15, 15, 1);
        st.end();
        let rest = 0;
        for (let r = 0; r < 30; r++) for (let c = 0; c < 30; c++) if (tileAt(eo.file, r, c) === big.id) rest++;
        check(rest === 0 && tileAt(eo.file, 15, 15) === 302, tag + 'a small object put on a bigger one takes it away whole');
        let sol2 = 0;
        for (let i = 0; i < eo.file.cells; i++) if (Edit.flags(eo.file, 1, i) & 1) sol2++;
        check(sol2 === 1, tag + 'and only its own solid bit is left (' + sol2 + ')');
    }
    // a bad anchor, an item that is no item
    const f = open(8, 10, 10);
    check(/no room/.test(Objects.place(f, item('grass4'), -1, 3).error) && /no room/.test(Objects.place(f, item('grass4'), 3, 10).error) && /no room/.test(Objects.place(f, null, 3, 3).error), 'an anchor off the map, and nothing to put, are refused');
}

// ---- E ----
function caseE() {
    // the edge: the cells that fall off the map are dropped, the anchor stays
    for (const version of [8, 9]) {
        const tag = 'version ' + version + ': ';
        const f = open(version, 12, 12), ed = editorOn(f), it = item('grassmed1');            // 2 rows, 5 columns: -1..0 rows, -2..2 columns
        const change = Objects.place(ed.file, it, 0, 0);
        check(!change.error && change.cells.length === 3, tag + 'at the top left corner only the 3 cells on the map are put (' + (change.cells || []).length + ')');
        ed.act(change);
        check(tileAt(ed.file, 0, 0) === it.id && Edit.flags(ed.file, 2, 0) === 1 && tileAt(ed.file, 0, 2) === it.id, tag + 'the anchor and the cells that are on the map are there');
        check(Edit.objectCells(ed.file, 0).length === 3, tag + 'and the eraser finds the three');
        const solid = Array.from({ length: 144 }, (_, i) => Edit.flags(ed.file, 1, i) & 1).reduce((a, b) => a + b, 0);
        check(solid === 3, tag + 'with 3 solid bits');
        const br = Objects.place(ed.file, it, 11, 11);
        check(br.cells.length === 6, tag + 'at the bottom right corner the 3 columns and 2 rows that are on the map (' + br.cells.length + ')');
    }
    // row 0 never loses its solid bit (the original's rule), whatever takes the object away
    const f0 = open(8, 12, 12), e0 = editorOn(f0), it0 = item('grass4');
    putCell(e0.file, 1, 0, 3, 485, 1, 0xCDCD);
    e0.act(Objects.place(e0.file, it0, 1, 3));
    const er = e0.begin('erase', null);
    er.stamp(1, 3, 1);
    er.end();
    check((Edit.flags(e0.file, 1, 3) & 1) === 1 && (Edit.flags(e0.file, 1, 1 * 12 + 3) & 1) === 0 && tileAt(e0.file, 0, 3) === EMPTY2, 'the solid bit of row 0 stays when an object that stood over it is taken away; the other cells lose theirs');
    // a version 9 map wider than 256: the anchors are offsets, and the eraser follows them
    const big = open(9, 20, 300);
    const eb = editorOn(big), it = item('bearbod');
    eb.act(Objects.place(eb.file, it, 10, 290));
    const w = Edit.third(eb.file, 2, 10 * 300 + 292);
    check(w === (((-0) & 255) | (((290 - 292) & 255) << 8)), 'a cell two columns right of the anchor holds the offset 0, -2 (' + w.toString(16) + ')');
    check(Edit.objectCells(eb.file, 10 * 300 + 292).length === it.cells.length, 'the cells round the anchor are all found');
    const er2 = eb.begin('erase', null);
    er2.stamp(9, 288, 1);
    er2.end();
    let left = 0;
    for (let i = 0; i < big.cells; i++) if (Edit.tile(eb.file, 2, i) !== EMPTY2) left++;
    check(left === 0, 'one tap takes it whole (' + left + ' cells left)');
    // a dictionary that is too short: it grows, and the name is the game's
    const short = open(8, 12, 12, 100), es = editorOn(short), cg = item('grass4');
    const made = Objects.place(es.file, cg, 5, 5);
    check(made.names.length === 1 && made.names[0][0] === cg.id && made.names[0][1] === 'grass4', 'the change names the tile for the dictionary');
    es.act(made);
    check(es.file.names === cg.id + 1 && es.file.bytes[es.file.dictAt + 2 + 11 * cg.id] === 0x67, 'a dictionary of 100 names grows to hold it, with its name');
    es.undo();
    check(tileAt(es.file, 5, 5) === EMPTY2, 'undo takes the object away (the longer dictionary stays: a name that is not used is harmless)');
    // the box that the canvas marks
    const f = open(8, 12, 12);
    equal(View.cursorRect(f, { row: 5, column: 5 }, 1, item('grassmed1').box), { r0: 4, r1: 5, c0: 3, c1: 7 }, 'the marked box of a 2 x 5 object is its box round the cell');
    equal(View.cursorRect(f, { row: 0, column: 0 }, 1, item('grassmed1').box), { r0: 0, r1: 0, c0: 0, c1: 2 }, 'cut by the map\'s edge');
    equal(View.cursorRect(f, { row: 5, column: 5 }, 4), { r0: 4, r1: 7, c0: 4, c1: 7 }, 'and a hill\'s square (a brush of 4) is as it was');
    equal(View.cursorRect(f, { row: 5, column: 5 }, 1), { r0: 5, r1: 5, c0: 5, c1: 5 }, 'and one cell');
}

// ---- G ----
// every object of every map taken away cell by cell: no cell is left that has the solid bit and no object (the original clears the bit whenever it clears a cell: tiles that are not in the table of
// solid tiles, such as bombs and plants, stand on cells that carry it); row 0 keeps its bit; a hill's cells are not touched
function caseG(maps) {
    let cells = 0, left = 0, row0 = 0, hills = 0, n = 0;
    for (const m of maps) {
        for (const bytes of (++n % 9 === 0 ? [m.bytes, toV9(m.bytes)] : [m.bytes])) {
            const f0 = File.parse(bytes.slice()), ed = editorOn(f0), list = [];
            for (let i = 0; i < f0.cells; i++) {
                const t = Edit.tile(f0, 2, i);
                if (t === EMPTY2) continue;
                if (t >= 245 && t <= 248) { hills++; continue; }
                list.push(i);
            }
            const er = ed.begin('erase', null);
            for (const i of list) er.stamp(Math.floor(i / f0.columns), i % f0.columns, 1);
            er.end();
            const f = ed.file;
            for (const i of list) {
                cells++;
                const t = Edit.tile(f, 2, i), bit = Edit.flags(f, 1, i) & 1, row = Math.floor(i / f.columns);
                if (t !== EMPTY2) continue;
                if (row === 0) { row0++; continue; }
                if (bit) left++;
            }
        }
    }
    check(cells > 200000, cells + ' object cells of the library were taken away (' + hills + ' hill cells left alone, ' + row0 + ' in row 0)');
    check(left === 0, left + ' cells are left with the solid bit and no object');
}

// ---- F ----
function caseF() {
    for (const version of [8, 9]) {
        const tag = 'version ' + version + ': ';
        const f = open(version, 20, 20), ed = editorOn(f), solid = (r, c) => Edit.flags(ed.file, 1, r * 20 + c) & 1;
        let st = ed.begin('objects', 302);
        st.stamp(5, 5, 3); st.end();
        let n = 0;
        for (let r = 4; r <= 6; r++) for (let c = 4; c <= 6; c++) n += solid(r, c);
        check(n === 9, tag + 'rocks put with a brush of 3 are solid on all 9 cells (' + n + ')');
        st = ed.begin('objects', 63);
        st.stamp(5, 5, 1); st.end();
        check(solid(5, 5) === 1 && Edit.tile(ed.file, 2, 5 * 20 + 5) === 63, tag + 'a power-up put on a rock stays solid (a power-up is solid in the game)');
        st = ed.begin('objects', 129);
        st.stamp(5, 5, 1); st.end();
        check(solid(5, 5) === 0 && solid(5, 4) === 1, tag + 'a tile that is not solid put on a rock clears that cell\'s bit and no other');
        st = ed.begin('erase', null);
        st.stamp(5, 5, 3); st.end();
        n = 0;
        for (let i = 0; i < ed.file.cells; i++) n += Edit.flags(ed.file, 1, i) & 1;
        check(n === 0 && Edit.flags(ed.file, 1, 5 * 20 + 5) === 0, tag + 'the eraser takes the bits with the rocks, and leaves the other flags of the cells (' + n + ' left)');
        while (ed.history.canUndo()) ed.undo();
        check(same(layersOf(ed.file), layersOf(open(version, 20, 20))), tag + 'undo of all of it gives the layers back, byte for byte');
        // a flag that is not the solid bit is kept
        const g = open(version, 20, 20);
        putCell(g, 1, 7, 7, 485, 0x0100, 0xCDCD);
        const e2 = editorOn(File.parse(g.bytes.slice())), s2 = e2.begin('objects', 302);
        s2.stamp(7, 7, 1); s2.end();
        check(Edit.flags(e2.file, 1, 7 * 20 + 7) === 0x0101, tag + 'the other flags of a cell are kept when the bit is set (' + Edit.flags(e2.file, 1, 7 * 20 + 7).toString(16) + ')');
        // a hill that is taken away leaves the bits of layer 1 as they were
        const h = open(version, 20, 20), eh = editorOn(h);
        eh.act(Blocks.placeHill(eh.file, 1, 8, 8));
        putCell(eh.file, 1, 8, 8, 485, 1, 0xCDCD);
        const before = layersOf(eh.file).slice(0, 6 * 400), er = eh.begin('erase', null);
        er.stamp(8, 8, 1); er.end();
        check(same(layersOf(eh.file).slice(0, 6 * 400), before), tag + 'the eraser leaves the solid bits of layer 1 under a hill alone');
    }
}

const maps = readAll();
// (a list is printed and the script ends by itself: process.exit would cut a long output to a pipe short)
if (argv.includes('--list-objects')) listObjects(maps);
else {
    const cases = [['A solid tiles are the game\'s', caseA], ['B the list is what the library says', caseB], ['D what is in the way', caseD], ['E edges, a wide map, the dictionary', caseE], ['F small objects are solid too', caseF], ['G the solid bit goes with every object', caseG], ['C put and taken away on the library', caseC]];       // (the sweep of the library, the long one, is the last: a broken copy that a short case refuses is refused at once, tests/scripts/web_mapmaker_bail.js)
    for (const [name, fn] of cases) {
        const before = failures;
        try { fn(maps); } catch (e) { failures++; console.log('    FAILED: ' + name + ' stopped: ' + (e && e.stack || e)); }
        console.log('RUNNING: ' + name + ' ... ' + (failures === before ? 'PASS' : 'FAIL'));
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exitCode = failures ? 1 : 0;
}
