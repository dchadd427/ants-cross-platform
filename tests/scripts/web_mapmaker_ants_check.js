#!/usr/bin/env node
// The map maker's Ants tool box (web/front/mapmaker_ants.js; docs/MAP_EDITOR.md, "Ants: one at a time, group, mix") held to the library's maps, with the real reader, blocks and history and a small
// fake of the DOM (no browser, no map core: the real core's verdict is the browser part `ants`):
//
//   node tests/scripts/web_mapmaker_ants_check.js [--front DIR] [--write-maps DIR]
//
//   A  one ant put and taken away, on real maps: the count, the record, every other byte as it was, Undo and Redo give the same bytes, a refusal says why (solid ground, an object, a hill, a
//      cell that has an ant) and changes nothing
//   B  the mix: the same plan every time, exactly `per` ants for each team, no two ants side by side with every other cell, a box that is too small is refused with the numbers, one Undo
//   C  the clump: the nearest free cells, appended to the list (a pinned digest and the bytes made by hand), two clumps both stay and never share a cell, up to 250 ants, one Undo for each, and
//      Pick up moves one clump and leaves the other, and Clear <team> ants is one step
//   D  the limit of the file (65,535 records) and the warnings (a team with none, too many, packed too tight)
//   E  the box in the page on a fake DOM: the switch, a tap, Take one away, a mix and its Put button, Clear with its question, one change for each
//   G  the clump in the page: a tap puts a new clump, a tap on a clump puts the next one round it, the number box up to 250, a refusal says why, one Undo for each clump
//   H  maps with many big clumps, made with the real editor code (3 clumps of 250 or 150 for every team on two library maps): the file reads again, counts the ants and is as the writer makes it. With --write-maps DIR
//      the maps are written there too, for the game to load and play (tests/scripts/test_web_mapmaker.py, TheGameLoadsTheClumps)
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Edit = load('mapmaker_edit.js'), Blocks = load('mapmaker_blocks.js'), Ants = load('mapmaker_ants.js'), Pick = load('mapmaker_pickup.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }
function same(a, b) { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; }
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
function open(rel) { const f = File.parse(new Uint8Array(fs.readFileSync(path.join(ROOT, rel)))); if (!f.ok) throw new Error(rel); return f; }
function editor(file) { return Edit.create(file, { rebase: (bytes) => File.parse(bytes), erase: Blocks.eraseAt, guard: Blocks.blockedText }); }
function count(file) { return Blocks.layout(file).records.length; }
function ants(file) { return Blocks.facts(file, false).ants; }
function free(file, n, from) {         // n cells where an ant may start
    const out = [];
    for (let i = from || 0; i < file.cells && out.length < n; i++) { const r = Math.floor(i / file.columns), c = i % file.columns; if (!Ants.whyNot(file, i) && Ants.teamAt(file, r, c) < 0) out.push([r, c]); }
    return out;
}

const MAPS = ['Original-Ants/Maps/TINY.LVL', 'Original-Ants/Maps/MEDIUM.LVL', 'Original-Ants/Maps/TREASURE.LVL'];

// A
for (const rel of MAPS) {
    const f0 = open(rel), ed = editor(f0), before = Uint8Array.from(f0.bytes), n0 = count(f0), a0 = ants(f0).slice();
    const [cell] = free(f0, 1, f0.columns * 3 + 3);
    const ch = Ants.putOne(f0, 2, cell[0], cell[1]);
    check(!ch.error, rel + ': one ant can be put on a free cell');
    check(!!ed.act(ch), rel + ': the change is one step');
    let f = ed.file;
    check(count(f) === n0 + 1 && ants(f)[2] === a0[2] + 1 && Ants.teamAt(f, cell[0], cell[1]) === 2, rel + ': one more record, of the right team, on the cell');
    const after = Uint8Array.from(f.bytes);
    ed.undo();
    check(same(Blocks.block1Bytes(Blocks.layout(ed.file).records), Blocks.block1Bytes(Blocks.layout(f0).records)), rel + ': undo gives the list back, record for record');
    check(count(ed.file) === n0 && Ants.teamAt(ed.file, cell[0], cell[1]) === -1, rel + ': Undo takes it away');
    ed.redo(); check(same(ed.file.bytes, after), rel + ': Redo gives the same bytes');
    // every byte outside block 1's count and the new record is as before (the dictionary may hold a marker name more)
    check(Blocks.layout(ed.file).records.slice(0, n0).every((r, i) => { const o = Blocks.layout(f0).records[i]; return r.tile === o.tile && r.row === o.row && r.column === o.column; }), rel + ': the records that were there are as they were');
    const dup = Ants.putOne(ed.file, 1, cell[0], cell[1]);
    check(dup.error && /already/.test(dup.error), rel + ': a cell that has an ant is refused');
    const take = Ants.takeOne(ed.file, cell[0], cell[1]);
    check(take && !!ed.act(take) && count(ed.file) === n0 && Ants.teamAt(ed.file, cell[0], cell[1]) === -1, rel + ': a tap on the ant takes it away (any team)');
    check(Ants.takeOne(ed.file, cell[0], cell[1]) === null, rel + ': nothing to take on an empty cell');
    // refusals
    const lay = Blocks.layout(f0);
    let solid = -1, object = -1, hill = -1;
    for (let i = 0; i < f0.cells; i++) {
        if (solid < 0 && (Edit.flags(f0, 1, i) & 1) && Edit.tile(f0, 2, i) === Blocks.EMPTY2) solid = i;
        if (object < 0 && Edit.tile(f0, 2, i) !== Blocks.EMPTY2 && Blocks.teamOfHill(Edit.tile(f0, 2, i)) < 0) object = i;
        if (hill < 0 && Blocks.teamOfHill(Edit.tile(f0, 2, i)) >= 0) hill = i;
    }
    for (const [name, i] of [['solid ground', solid], ['an object', object], ['a hill', hill]]) {
        if (i < 0) continue;
        const bad = Ants.putOne(f0, 0, Math.floor(i / f0.columns), i % f0.columns);
        check(bad.error && bad.error.length > 20 && !bad.splices && (name !== 'a hill' || /hill/.test(bad.error)), rel + ': ' + name + ' is refused with a sentence');
    }
    check(Ants.putOne(f0, 0, f0.rows, 0).error && Ants.putOne(f0, 0, 0, -1).error, rel + ': a cell outside the map is refused');
    check(same(f0.bytes, before), rel + ': refusals change nothing');
}

// B
{
    const f0 = open('Original-Ants/Maps/TREASURE.LVL'), ed = editor(f0), before = Uint8Array.from(f0.bytes);
    const box = { r0: 4, c0: 4, r1: 40, c1: 50 }, all = [0, 1, 2, 3];
    const p1 = Ants.planMix(f0, box, all, 25, 2), p2 = Ants.planMix(f0, box, all, 25, 2);
    check(p1.ok && JSON.stringify(p1.cells) === JSON.stringify(p2.cells), 'mix: the same plan every time');
    const per = [0, 0, 0, 0]; p1.cells.forEach((c) => per[c.team]++);
    check(per.every((n) => n === 25) && p1.cells.length === 100, 'mix: exactly 25 ants for each team');
    check(p1.cells.every((c) => ((c.r - box.r0) + (c.c - box.c0)) % 2 === 0), 'mix: every other cell is a chessboard from the corner of the box');
    const set = new Set(p1.cells.map((c) => c.r * 1000 + c.c));
    check(set.size === 100 && p1.cells.every((c) => !set.has((c.r + 1) * 1000 + c.c) && !set.has(c.r * 1000 + c.c + 1)), 'mix: every other cell, no two ants side by side, no cell twice');
    check(p1.cells.every((c) => c.r >= box.r0 && c.r <= box.r1 && c.c >= box.c0 && c.c <= box.c1 && !Ants.whyNot(f0, c.r * f0.columns + c.c)), 'mix: every ant is inside the box on a free cell');
    const p3 = Ants.planMix(f0, box, [3, 2], 7, 1);
    check(p3.ok && p3.cells.length === 14 && p3.cells.filter((c) => c.team === 3).length === 7 && p3.cells.filter((c) => c.team === 2).length === 7, 'mix: two teams, seven ants each');
    // measured mixing: no row and no column is one team, few neighbors share a team, the ants are spread over the box, for widths 16 to 24 and both spacings
    for (let width = 16; width <= 24; width++) for (const gap of [1, 2]) {
        const bx = { r0: 3, c0: 3, r1: 3 + 21, c1: 3 + width - 1 }, q = Ants.planMix(f0, bx, all, 25, gap);
        if (!q.ok) { check(false, 'mix ' + width + ' wide, gap ' + gap + ' is planned'); continue; }
        const rowsOf = {}, colsOf = {}, at = {}, cnt = {};
        q.cells.forEach((c) => { (rowsOf[c.r] = rowsOf[c.r] || new Set()).add(c.team); (colsOf[c.c] = colsOf[c.c] || new Set()).add(c.team); cnt['r' + c.r] = (cnt['r' + c.r] || 0) + 1; cnt['c' + c.c] = (cnt['c' + c.c] || 0) + 1; at[c.r * 1000 + c.c] = c.team; });
        const single = Object.keys(rowsOf).filter((k) => cnt['r' + k] >= 2 && rowsOf[k].size < 2).length + Object.keys(colsOf).filter((k) => cnt['c' + k] >= 2 && colsOf[k].size < 2).length;
        let pairs = 0, same = 0;
        q.cells.forEach((c) => { for (const d of [1, 1000]) { const o = at[c.r * 1000 + c.c + d]; if (o !== undefined) { pairs++; if (o === c.team) same++; } } });
        const top = Math.min(...q.cells.map((c) => c.r)), bottom = Math.max(...q.cells.map((c) => c.r));
        check(single === 0, 'mix ' + width + ' wide, gap ' + gap + ': no row or column is one team (' + single + ')');
        check(pairs === 0 || same / pairs < 0.4, 'mix ' + width + ' wide, gap ' + gap + ': few neighbors share a team (' + same + ' of ' + pairs + ')');
        check(bottom - top >= 15, 'mix ' + width + ' wide, gap ' + gap + ': the ants are spread over the box (' + top + ' to ' + bottom + ')');
        check(JSON.stringify(Ants.planMix(f0, bx, all, 25, gap).cells) === JSON.stringify(q.cells), 'mix ' + width + ' wide, gap ' + gap + ': the same plan again');
    }
    const lineBox = Ants.planMix(f0, { r0: 4, c0: 0, r1: 4, c1: 198 }, [0], 100, 2);
    check(lineBox.ok === (lineBox.free >= 100) && /fit|need/.test(lineBox.text), 'mix: a one-row box is judged by its spaced cells');
    const edm = editor(open('Original-Ants/Maps/TREASURE.LVL')), seen = Blocks.layout(edm.file).records.filter((r) => Blocks.teamOfMarker(r.tile) >= 0).slice(0, 3);
    if (seen.length) { const bx = { r0: Math.max(0, seen[0].row - 2), c0: Math.max(0, seen[0].column - 2), r1: seen[0].row + 4, c1: seen[0].column + 4 }, room = Ants.planMix(edm.file, bx, [0, 1], 1, 1).free, q = Ants.planMix(edm.file, bx, [0, 1], Math.floor(room / 2), 1); check(q.ok && q.cells.every((c) => !(c.r === seen[0].row && c.c === seen[0].column)), 'mix: a cell that has an ant is passed over'); }
    const small = Ants.planMix(f0, { r0: 5, c0: 5, r1: 8, c1: 8 }, all, 25, 2);
    check(!small.ok && /100 ants need 200 cells; your box has 16/.test(small.text) && /bigger box/.test(small.text), 'mix: a box that is too small is refused politely, with the numbers');
    check(Ants.putMix(f0, small).error, 'mix: the change of a refused plan is an error');
    const n0 = count(f0), a0 = ants(f0).slice();
    check(!!ed.act(Ants.putMix(f0, p1)), 'mix: one change');
    const f = ed.file, a1 = ants(f);
    check(count(f) === n0 + 100 && [0, 1, 2, 3].every((t) => a1[t] === a0[t] + 25), 'mix: 25 more of each team');
    ed.undo(); check(count(ed.file) === n0 && same(Blocks.block1Bytes(Blocks.layout(ed.file).records), Blocks.block1Bytes(Blocks.layout(f0).records)), 'mix: one Undo takes all 100 away, record for record');
    check(Blocks.layout(ed.file).records.length === n0, 'mix: the list is as long as before');
}

// C
{
    // the cells of a clump, made again here: the free cells (the Ants box's own rule) by distance from the tapped cell, then row, then column
    const nearest = (file, row, column, n) => {
        const list = [];
        for (let i = 0; i < file.cells; i++) { const r = Math.floor(i / file.columns), c = i % file.columns; if (!Ants.whyNot(file, i) && Ants.teamAt(file, r, c) < 0) list.push({ r, c, d: (r - row) * (r - row) + (c - column) * (c - column) }); }
        list.sort((a, b) => a.d - b.d || a.r - b.r || a.c - b.c);
        return list.slice(0, n).map((x) => [x.r, x.c]);
    };
    const cellsOf = (file, from, to) => Blocks.layout(file).records.slice(from, to).map((r) => [r.row, r.column]);
    const f0 = open('Original-Ants/Maps/TINY.LVL'), ed = editor(f0), lay0 = Blocks.layout(f0), n0 = lay0.records.length, want = nearest(f0, 10, 10, 5);
    const change = Blocks.placeStart(f0, 0, 10, 10, 5);
    check(!change.error && change.splices.length === 2 && change.cells.length === 0, 'clump: a change of block 1 only (the new records at the end, and the count word)');
    ed.act(change);
    const lay1 = Blocks.layout(ed.file);
    equal(cellsOf(ed.file, n0, n0 + 5), want, 'clump: the five nearest free cells, nearest first');
    check(lay1.records.length === n0 + 5 && lay1.records.slice(n0).every((r) => r.tile === Blocks.TEAMS[0].marker) && lay1.records.slice(0, n0).every((r, i) => r.tile === lay0.records[i].tile && r.row === lay0.records[i].row && r.column === lay0.records[i].column), 'clump: 5 records of Green after the ones that were there, which are as they were');
    // the bytes, made by hand: the count word, the old records, the new ones, and everything after
    const at = f0.blocksAt, hand = new Uint8Array(f0.bytes.length + 30);
    hand.set(f0.bytes.subarray(0, at), 0);
    hand.set([(n0 + 5) & 255, (n0 + 5) >> 8], at);
    hand.set(f0.bytes.subarray(at + 2, at + 2 + 6 * n0), at + 2);
    want.forEach(([r, c], i) => hand.set([Blocks.TEAMS[0].marker & 255, Blocks.TEAMS[0].marker >> 8, r & 255, r >> 8, c & 255, c >> 8], at + 2 + 6 * n0 + 6 * i));
    hand.set(f0.bytes.subarray(at + 2 + 6 * n0), at + 2 + 6 * n0 + 30);
    check(same(ed.file.bytes, hand), 'clump: the file is byte for byte the old one with 5 records and the count word put in');
    check(sha(ed.file.bytes) === 'eaa6bb74214bb3855e769e7cc78937093c3aa45296bd079f3ca77a74d7d3a332', 'clump: a clump of 5 on TINY is byte for byte what it should be (digest ' + sha(ed.file.bytes).slice(0, 12) + ')');
    const one = Uint8Array.from(ed.file.bytes);
    // a second clump of the same team, a third of another: both stay, the first does not move, no two ants share a cell, one Undo for each
    ed.act(Blocks.placeStart(ed.file, 0, 12, 12, 9));
    ed.act(Blocks.placeStart(ed.file, 2, 11, 11, 7));
    const lay2 = Blocks.layout(ed.file), cells = lay2.records.map((r) => r.row + ',' + r.column);
    check(lay2.records.length === n0 + 21 && ants(ed.file)[0] === ants(f0)[0] + 14 && ants(ed.file)[2] === ants(f0)[2] + 7, 'two clumps of one team stay (5 and 9), and the clump of another team (7): 21 more ants');
    equal(cellsOf(ed.file, n0, n0 + 5), want, 'the first clump has not moved');
    check(new Set(cells).size === cells.length, 'no two ants of the map stand on one cell');
    check(lay2.records.slice(n0 + 5, n0 + 14).every((r) => !Ants.whyNot(f0, r.row * f0.columns + r.column)) && lay2.records.slice(n0 + 14).every((r) => !Ants.whyNot(f0, r.row * f0.columns + r.column)), 'and every one of them stands where an ant can walk');
    const three = Uint8Array.from(ed.file.bytes);
    ed.undo(); check(count(ed.file) === n0 + 14, 'one Undo takes the clump of 7 away and no other ant');
    ed.undo(); check(same(ed.file.bytes, one), 'the next Undo takes the clump of 9 away: the bytes of the first clump, exactly');
    ed.undo(); check(same(ed.file.bytes, f0.bytes), 'the third gives the map as it was opened, byte for byte');
    ed.redo(); ed.redo(); ed.redo();
    check(same(ed.file.bytes, three), 'and three Redos give the same bytes as before');
    // up to 250 ants, nearest first, in one change
    for (const rel of MAPS.slice(1)) {
        const g = open(rel), r = Math.floor(g.rows / 2), c = Math.floor(g.columns / 2), eg = editor(g), g0 = count(g);
        const big = Blocks.placeStart(g, 1, r, c, 250);
        check(!big.error && eg.act(big) && count(eg.file) === g0 + 250, rel + ': a clump of 250 is 250 records');
        equal(cellsOf(eg.file, g0, g0 + 250), nearest(g, r, c, 250), rel + ': the 250 nearest free cells');
        check(Blocks.placeStart(g, 1, r, c, 251).splices[0].inserted.length === 6 * 250 && Blocks.placeStart(g, 1, r, c, 5000).splices[0].inserted.length === 6 * 250, rel + ': more than 250 is 250');
        check(Blocks.placeStart(g, 1, r, c, 0).splices[0].inserted.length === 6, rel + ': a clump has at least one ant');
        eg.undo(); check(same(eg.file.bytes, g.bytes), rel + ': one Undo takes the 250 away, byte for byte');
        eg.act(Blocks.placeStart(eg.file, 1, r, c, 250)); eg.act(Blocks.placeStart(eg.file, 1, r + 1, c + 1, 250));
        check(count(eg.file) === g0 + 500 && new Set(Blocks.layout(eg.file).records.slice(g0).map((q) => q.row + ',' + q.column)).size === 500, rel + ': two clumps of 250 on nearly the same cell are 500 ants, none on another\'s cell');
    }
    // a clump never starts on a cell that cannot hold an ant: solid ground with nothing on it, a food pile whose cell shows nothing
    let tried = 0;
    for (const rel of MAPS) {
        const g = open(rel);
        let solid = -1;
        for (let i = 0; i < g.cells && solid < 0; i++) if ((Edit.flags(g, 1, i) & 1) && Edit.tile(g, 2, i) === Blocks.EMPTY2) solid = i;
        if (solid < 0) continue;
        const r = Math.floor(solid / g.columns), c = solid % g.columns, got = Blocks.placeStart(g, 0, r, c, 3).splices[0].inserted;
        const first = [got[2] + 256 * got[3], got[4] + 256 * got[5]];
        check(!(first[0] === r && first[1] === c), rel + ': a clump tapped on solid ground does not start on it');
        tried++;
    }
    check(tried > 0, 'the library has solid ground with nothing on it to try');
    const pile = editor(open('Original-Ants/Maps/TINY.LVL'));
    pile.act(Blocks.placePile(pile.file, 10, 10, 3));
    pile.act({ names: [], cells: [[10 * pile.file.columns + 10, Blocks.EMPTY2, 0, 0]], splices: [] });
    const onPile = Blocks.placeStart(pile.file, 0, 10, 10, 3).splices[0].inserted;
    check(Ants.whyNot(pile.file, 10 * pile.file.columns + 10) !== '' && !(onPile[2] === 10 && onPile[4] === 10), 'a clump tapped on a food pile (its cell shows nothing) does not start on it');
    // Pick up moves one clump of a team and leaves the other where it is (an ant and the ants of its team that touch it)
    {
        const g = open('Original-Ants/Maps/MEDIUM.LVL'), eg = editor(g), n1 = count(g);
        eg.act(Blocks.placeStart(eg.file, 1, 10, 10, 30)); eg.act(Blocks.placeStart(eg.file, 1, 45, 45, 40));
        const fresh = Blocks.layout(eg.file).records.slice(n1), a = fresh[0], b = fresh[30];
        const ia = Pick.itemsAt(eg.file, a.row, a.column)[0], ib = Pick.itemsAt(eg.file, b.row, b.column)[0];
        const ga = Pick.antGroup(eg.file, ia), gb = Pick.antGroup(eg.file, ib);
        check(ga.length === 30 && gb.length === 40 && ga.every((it) => it.i >= n1 && it.i < n1 + 30) && gb.every((it) => it.i >= n1 + 30), 'pick up: an ant of a clump picks up that clump and no other (30 and 40 ants of one team)');
        const [dr, dc] = [[0, 5], [5, 0], [-5, 0], [0, -5], [2, 3]].find(([r, c]) => Pick.check(eg.file, ga, r, c, 'move', 'group').ok);
        check(dr !== undefined && !!eg.act(Pick.build(eg.file, ga, dr, dc, 'move')), 'pick up: the first clump can be moved');
        const after = Blocks.layout(eg.file).records.slice(n1);
        check(after.length === 70 && after.slice(0, 30).every((r, i) => r.row === fresh[i].row + dr && r.column === fresh[i].column + dc) && after.slice(30).every((r, i) => r.row === fresh[30 + i].row && r.column === fresh[30 + i].column), 'pick up: it moved by ' + dr + ' rows and ' + dc + ' columns, and the second clump did not move');
        eg.undo(); check(Blocks.layout(eg.file).records.slice(n1).every((r, i) => r.row === fresh[i].row && r.column === fresh[i].column), 'pick up: one Undo puts it back');
    }
    // Clear <team> ants
    const ed2 = editor(open('Original-Ants/Maps/TREASURE.LVL')), t = Blocks.teamOfMarker(Blocks.layout(ed2.file).records.find((r) => Blocks.teamOfMarker(r.tile) >= 0).tile);
    const had = ants(ed2.file)[t], total = count(ed2.file);
    check(had > 0 && !!ed2.act(Ants.clearTeam(ed2.file, t)) && ants(ed2.file)[t] === 0 && count(ed2.file) === total - had, 'clear: all ants of one team go, in one step, the others stay');
    ed2.undo(); check(ants(ed2.file)[t] === had, 'clear: one Undo brings them back');
    const none = editor(open('Original-Ants/Maps/TINY.LVL')); none.act(Ants.clearTeam(none.file, 0));
    check(Ants.clearTeam(none.file, 0).splices.length === 0, 'clear: a team with no ants gives an empty change');
}

// D
{
    const f0 = open('Original-Ants/Maps/TINY.LVL'), ed = editor(f0), lay = Blocks.layout(f0);
    const plants = new Uint8Array(6 * (65530 - lay.records.length)), at = lay.b1.length;
    const full = { names: [], cells: [], splices: [{ rel: lay.b1.length, removed: new Uint8Array(0), inserted: plants }, { rel: 0, removed: f0.bytes.slice(f0.blocksAt, f0.blocksAt + 2), inserted: new Uint8Array([0xFA, 0xFF]) }] };
    ed.act(full);
    check(count(ed.file) === 65530, 'limit: a file of 65,530 records');
    const [c1] = free(ed.file, 1, 700);
    const over5 = Blocks.placeStart(ed.file, 1, c1[0], c1[1], 6);
    check(over5.error && /does not fit/.test(over5.error) && /65,535/.test(over5.error), 'limit: a clump of 6 on 65,530 records is refused in words (' + over5.error + ')');
    check(!Blocks.placeStart(ed.file, 1, c1[0], c1[1], 5).error, 'limit: a clump of 5 still fits');
    check(!Ants.putOne(ed.file, 1, c1[0], c1[1]).error, 'limit: one more fits');
    const plan = { ok: true, cells: [0, 1, 2, 3, 4, 5].map((i) => ({ r: i, c: 0, team: 0 })) };
    const err = Ants.putMix(ed.file, plan).error;
    check(err && /65,535 at most/.test(err) && /Take 1 ant away/.test(err), 'limit: a mix over 65,535 is refused with the numbers (' + err + ')');
    const s = Ants.summary(ed.file, plan.cells);
    check(s.over && s.lines[0].kind === 'bad', 'limit: the warning is red');
    const w = Ants.summary(f0, null);
    check(w.missing.length === 0 ? w.lines[0].kind === 'ok' : w.lines[0].kind === 'warn', 'warnings: a team with none is amber, all four is the good line');
    const none = Ants.summary(open('Original-Ants/Maps/TINY.LVL'), null);
    check(typeof none.packed === 'number', 'warnings: the packed count is a number');
    check(!Ants.summary(ed.file, null).over, 'limit: 65,530 records is not over');
    ed.act({ names: [], cells: [], splices: [{ rel: ed.file.bytes.length - ed.file.blocksAt >= 0 ? Blocks.layout(ed.file).b1.length : 0, removed: new Uint8Array(0), inserted: new Uint8Array(30) }, { rel: 0, removed: ed.file.bytes.slice(ed.file.blocksAt, ed.file.blocksAt + 2), inserted: new Uint8Array([0xFF, 0xFF]) }] });
    check(count(ed.file) === 65535 && Ants.putOne(ed.file, 1, c1[0], c1[1]).error && /65,535/.test(Ants.putOne(ed.file, 1, c1[0], c1[1]).error), 'limit: with 65,535 records not one more ant is put');
    check(!Ants.summary(ed.file, null).over && Ants.summary(ed.file, [{ r: 0, c: 0 }]).over, 'limit: 65,535 is not over, 65,536 is');
    const gone = open('Original-Ants/Maps/TREASURE.LVL'), edg = editor(gone), t0 = Blocks.teamOfMarker(Blocks.layout(gone).records.find((r) => Blocks.teamOfMarker(r.tile) >= 0).tile);
    edg.act(Ants.clearTeam(gone, t0));
    const one = Ants.summary(edg.file, null);
    check(one.missing.length === 1 && one.missing[0] === Blocks.TEAMS[t0].label && one.lines[0].kind === 'warn' && new RegExp(Blocks.TEAMS[t0].label + ' has no ants. The Check says').test(one.lines[0].text), 'warnings: one team with none is named in an amber line');
    const pileFile = open('Original-Ants/Maps/TREASURE.LVL'), pl = Blocks.layout(pileFile).piles[0];
    if (pl && pl.row < pileFile.rows && pl.column < pileFile.columns) check(Ants.putOne(pileFile, 0, pl.row, pl.column).error, 'refusal: the cell of a food pile is refused');
    const f1 = open('Original-Ants/Maps/MEDIUM.LVL');
    const tight = Ants.planMix(f1, { r0: 3, c0: 3, r1: 16, c1: 18 }, [0, 1, 2, 3], 50, 1);
    check(tight.ok && Ants.packedCount(f1, tight.cells) > 0, 'warnings: ants shoulder to shoulder give the soft hint');
    // a free 5 x 5 square: the middle ant is packed in when the other eight cells are taken, and not when one is free
    let sq = null;
    for (let i = 0; i < f1.cells && !sq; i++) { const r = Math.floor(i / f1.columns), c = i % f1.columns; let ok = r > 1 && c > 1 && r < f1.rows - 3 && c < f1.columns - 3; for (let dr = -2; dr <= 2 && ok; dr++) for (let dc = -2; dc <= 2; dc++) if (Ants.whyNot(f1, (r + dr) * f1.columns + c + dc) || Ants.teamAt(f1, r + dr, c + dc) >= 0) { ok = false; break; } if (ok) sq = [r, c]; }
    const ring = []; for (let dr = -1; dr <= 1; dr++) for (let dc = -1; dc <= 1; dc++) ring.push({ r: sq[0] + dr, c: sq[1] + dc });
    check(Ants.packedCount(f1, ring) === Ants.packedCount(f1, null) + 1, 'warnings: an ant with all eight cells round it taken is packed in');
    check(Ants.packedCount(f1, ring.filter((c) => !(c.r === sq[0] && c.c === sq[1] - 1))) === Ants.packedCount(f1, null), 'warnings: one free cell beside it and it is not');
    check(Ants.summary(Object.assign({}, f1, { rows: 120 }), null).lines.some((l) => /more than 90 rows/.test(l.text)), 'warnings: a map of more than 90 rows says that the game cannot play it yet');
    const loose = Ants.planMix(f1, { r0: 3, c0: 3, r1: 40, c1: 40 }, [0, 1, 2, 3], 10, 2);
    check(loose.ok && Ants.packedCount(f1, loose.cells) === Ants.packedCount(f1, null), 'warnings: every other cell gives none');
}

// E
{
    const doc = makeDocument(), f0 = open('Original-Ants/Maps/TREASURE.LVL'), ed = editor(f0), said = [], dialogs = [];
    const tools = doc.createElement('div'), side = doc.createElement('div'), hint = doc.createElement('p'), line = doc.createElement('div');
    line.className = 'checkline'; side.appendChild(line); tools.appendChild(hint); doc.body.appendChild(tools); doc.body.appendChild(side);
    let team = 0, clump = 4, acts = 0;
    const part = Ants.create({
        doc, win: {}, host: { tools, hint, side }, file: () => ed.file, act: (c) => { const d = ed.act(c); if (d) acts++; part.refresh(); return !!d; }, say: (t, m) => said.push(m),
        dialogs: { open: (spec) => { dialogs.push(spec); } }, team: () => team, setTeam: (t) => { team = t; }, ants: () => clump, setAnts: (n) => { clump = n; }, view: () => null, about: () => ({ ant: 'Combat ants by default', eggs: 2 }), props: () => {}, refresh: () => {}
    });
    part.show(true);
    const btn = (k) => part.panel.querySelector('[data-k="' + k + '"]');
    check(!part.panel.hidden && part.painter.tapOnly(), 'page: the box shows, One at a time takes the tap and a drag looks around');
    check(part.panel.textContent.indexOf('The game picks a random facing at the start of each match; the editor shows an example.') >= 0, 'page: the one line that says the facing is an example (the game picks it at random for every ant at the start of a match)');
    check(btn('t2') && btn('t3') && /Black/.test(part.panel.textContent) && /Same for every team: Combat ants by default, 2 eggs each/.test(part.panel.textContent), 'page: four team buttons with counts, and what is the same for every team');
    const [c] = free(ed.file, 1, ed.file.columns * 5 + 5), n0 = count(ed.file);
    btn('t3').click(); team = 3;
    part.painter.begin({ row: c[0], column: c[1] });
    check(count(ed.file) === n0 + 1 && Ants.teamAt(ed.file, c[0], c[1]) === 3 && acts === 1, 'page: a tap puts one ant of the picked team, one change');
    part.painter.begin({ row: c[0], column: c[1] });
    check(count(ed.file) === n0 && acts === 2, 'page: a tap on that ant takes it away');
    btn('take').click(); part.painter.begin({ row: c[0], column: c[1] });
    check(count(ed.file) === n0 && acts === 2 && /no ant/.test(part.panel.textContent), 'page: Take one away on an empty cell says so and changes nothing');
    btn('put').click();
    team = 0; part.painter.begin({ row: c[0], column: c[1] }); check(Ants.teamAt(ed.file, c[0], c[1]) === 0, 'page: a Green ant is put');
    part.painter.begin({ row: c[0], column: c[1] }); check(Ants.teamAt(ed.file, c[0], c[1]) === -1 && acts === 4, 'page: a tap on a Green ant takes it away too');
    acts = 2; team = 3;
    check(part.panel.querySelectorAll('.am-team').map((b) => b.getAttribute('data-k')).join(',') === 't3,t2,t0,t1', 'page: the buttons are Black, Blue, Green, Red');
    part.painter.begin({ row: 0, column: 0 });
    btn('group').click(); btn('clump').click();
    check(part.painter.tapOnly(), 'page: Clump takes the tap too, and a drag looks around');
    check(/A tap puts a new clump\. Pick up moves one\./.test(part.panel.textContent) && !/move it/.test(part.panel.textContent), 'page: the note under the clump\'s number says what a tap does now');
    // typing in the number box of the clump: the keys change nothing on the page, and what the box says is taken when it is left or Enter is pressed, which makes the panel again; the box that the person
    // is in is then the new one, with the focus and the number (the panel puts the focus back on the control that had it)
    const numberBox = (k) => () => part.panel.querySelector('input[data-k="' + k + '"]');
    numberBox('clump')().value = '';
    equal(doc.typeCheck(numberBox('clump'), '12'), '', 'page: the clump\'s number box: every key stays in the same box with the focus');
    equal(doc.caretCheck(numberBox('clump'), 1, '3'), '', 'page: a key in the middle of the number keeps the caret after it');
    equal(doc.composeCheck(numberBox('clump'), ['9']), '', 'page: an input method in the number box is not cut off');
    numberBox('clump')().value = '30'; numberBox('clump')().fire('change', {});
    check(clump === 30 && numberBox('clump')().value === '30' && doc.activeElement === numberBox('clump')() && numberBox('clump')().isConnected, 'page: when the number is taken, the focus is on the number box (' + clump + ')');
    numberBox('clump')().value = '4'; numberBox('clump')().fire('change', {});
    check(clump === 4 && doc.activeElement === numberBox('clump')(), 'page: and again');
    btn('mix').click();
    check(!part.painter.tapOnly(), 'page: Mix takes the drag');
    numberBox('per')().value = '';
    equal(doc.typeCheck(numberBox('per'), '40'), '', 'page: the mix\'s number box (ants for each team): every key stays in the same box with the focus');
    numberBox('per')().value = '25'; numberBox('per')().fire('change', {});
    check(part.state().per === 25 && doc.activeElement === numberBox('per')(), 'page: when the number is taken, the focus is on the number box');
    part.painter.begin({ row: 4, column: 4 }); part.painter.extend({ row: 4, column: 4 }, { row: 40, column: 50 }); part.painter.end();
    check(part.state().plan && part.state().plan.ok && !btn('put-mix').disabled && /Put 100 ants/.test(btn('put-mix').textContent), 'page: a box gives a plan and the button "Put 100 ants"');
    btn('put-mix').click();
    check(count(ed.file) === n0 + 100 && acts === 3, 'page: Put makes one change of 100 ants');
    part.painter.begin({ row: 5, column: 5 }); part.painter.extend({ row: 5, column: 5 }, { row: 7, column: 7 }); part.painter.end();
    check(btn('put-mix').disabled && /too small/.test(part.panel.textContent), 'page: a box that is too small turns the button off and says why');
    btn('cancel').click(); check(!part.state().box, 'page: Cancel drops the box');
    btn('one').click();
    const total = ants(ed.file)[team];
    btn('clear').click();
    check(dialogs.length === 1 && /Clear the Black ants\?/.test(dialogs[0].title) && dialogs[0].buttons[1].text === 'Take away ' + total + ' Black ants', 'page: Clear asks first and says how many');
    dialogs[0].buttons[1].onClick();
    check(ants(ed.file)[3] === 0 && acts === 4, 'page: the question\'s button clears one team in one step');
    check(side.className.indexOf('am-on') >= 0, 'page: on a wide screen the box takes the right-hand column');
    part.show(false); check(part.panel.hidden && side.className.indexOf('am-on') < 0, 'page: it goes away with the tool');

    // F: Extended map per-spawn ant types (One at a time, Clump, Mix, and UI selector)
    const tiny = open('Original-Ants/Maps/TINY.LVL');
    const v9 = new Uint8Array(tiny.bytes.length - 40 + 18);
    v9[0] = 9;
    v9.set(tiny.bytes.subarray(4, 10), 4);
    v9.set(tiny.bytes.subarray(40), 18);
    const f9 = File.parse(v9), ed9 = editor(f9);
    check(f9.ok && f9.version === 9 && f9.kind === 'extended', 'v9: parsed as Extended map');
    const [c1, c2] = free(ed9.file, 2, ed9.file.columns * 5 + 5), n9 = count(ed9.file);
    check(!!ed9.act(Ants.putOne(ed9.file, 1, c1[0], c1[1], 4)), 'v9: putOne Combat ant');
    check(!!ed9.act(Ants.putOne(ed9.file, 2, c2[0], c2[1], 1)), 'v9: putOne Bomber ant');
    const recs = Blocks.layout(ed9.file).records;
    check(Blocks.teamOfMarker(recs[n9].tile) === 1 && Blocks.typeOfMarker(recs[n9].tile) === 4, 'v9: record 1 has team Red and antType 4 (Combat)');
    check(Blocks.teamOfMarker(recs[n9 + 1].tile) === 2 && Blocks.typeOfMarker(recs[n9 + 1].tile) === 1, 'v9: record 2 has team Blue and antType 1 (Bomber)');
    const lastTwo = ed9.file.markers.slice(-2);
    check(lastTwo[0].team === 'red' && lastTwo[0].antType === 4 && lastTwo[1].team === 'blue' && lastTwo[1].antType === 1, 'v9: MapFile.parse reads antType on markers');
    check(!!ed9.act(Blocks.placeStart(ed9.file, 1, 10, 10, 3, 5)), 'v9: placeStart clump of 3 Swimmer ants');
    const recs2 = Blocks.layout(ed9.file).records, swimmers = recs2.slice(n9 + 2);
    check(recs2.length === n9 + 5 && Blocks.typeOfMarker(recs2[n9].tile) === 4 && Blocks.typeOfMarker(recs2[n9 + 1].tile) === 1 && swimmers.length === 3 && swimmers.every((r) => Blocks.teamOfMarker(r.tile) === 1 && Blocks.typeOfMarker(r.tile) === 5), 'v9: the clump adds 3 Swimmer ants of Red after the ants that were there');
    check(Blocks.markerTile(f9, 1, 5) === Blocks.TEAMS[1].marker + 5 * 4096 && Blocks.markerTile(tiny, 1, 5) === Blocks.TEAMS[1].marker, 'v9: only a version 9 file carries the type in the tile');
    check(!!ed9.act(Ants.clearTeam(ed9.file, 1)) && !!ed9.act(Ants.clearTeam(ed9.file, 2)) && ants(ed9.file)[1] === 0, 'v9: clearTeam removes typed markers');
    check(!!ed9.act(Ants.putOne(ed9.file, 1, c1[0], c1[1], 4)) && !!ed9.act(Ants.putOne(ed9.file, 1, c2[0], c2[1], 2)), 'v9: put mixed Red Combat + Fire ants');
    check(!!ed9.act(Blocks.eraseAt(ed9.file, c1[0] * ed9.file.columns + c1[1])) && ants(ed9.file)[1] === 0, 'v9: eraseAt on one ant removes all starting ants of that team even with mixed types');
    let pickedType = 0;
    const part9 = Ants.create({
        doc, win: {}, host: { tools, hint, side }, file: () => ed9.file, act: (c) => { const d = ed9.act(c); part9.refresh(); return !!d; }, say: () => {},
        dialogs: { open: () => {} }, team: () => 0, setTeam: () => {}, ants: () => 4, setAnts: () => {}, antType: () => pickedType, setAntType: (at) => { pickedType = at; },
        convert: () => {}, view: () => null, about: () => ({ ant: 'Worker ants', eggs: 2 }), props: () => {}, refresh: () => {}
    });
    part9.show(true);
    const btn9 = (k) => part9.panel.querySelector('[data-k="' + k + '"]');
    check(btn9('at4') && btn9('at1') && btn9('at5'), 'v9 UI: Ant type buttons rendered on Extended map');
    btn9('at4').click();
    check(pickedType === 4 && part9.state().antType === 4, 'v9 UI: clicking Combat selects antType 4');
    btn9('group').click(); btn9('clump').click();
    const before9 = count(ed9.file), [c9] = free(ed9.file, 1, ed9.file.columns * 20 + 20);
    part9.painter.begin({ row: c9[0], column: c9[1] });
    check(count(ed9.file) === before9 + 4 && Blocks.layout(ed9.file).records.slice(before9).every((r) => Blocks.teamOfMarker(r.tile) === 0 && Blocks.typeOfMarker(r.tile) === 4), 'v9 UI: a tap in Clump puts 4 Combat ants of the picked team');
    part9.show(false);
}

// G
{
    const doc = makeDocument(), f0 = open('Original-Ants/Maps/TREASURE.LVL'), ed = editor(f0), said = [], dialogs = [];
    const tools = doc.createElement('div'), side = doc.createElement('div'), hint = doc.createElement('p'), line = doc.createElement('div');
    line.className = 'checkline'; side.appendChild(line); tools.appendChild(hint); doc.body.appendChild(tools); doc.body.appendChild(side);
    let team = 1, clump = 6, acts = 0;
    const part = Ants.create({
        doc, win: {}, host: { tools, hint, side }, file: () => ed.file, act: (c) => { const d = ed.act(c); if (d) acts++; part.refresh(); return !!d; }, say: (t, m) => said.push(m),
        dialogs: { open: (spec) => { dialogs.push(spec); } }, team: () => team, setTeam: (t) => { team = t; }, ants: () => clump, setAnts: (n) => { clump = n; }, view: () => null, about: () => ({ ant: 'Combat ants by default', eggs: 2 }), props: () => {}, refresh: () => {}
    });
    part.show(true);
    const btn = (k) => part.panel.querySelector('[data-k="' + k + '"]');
    btn('group').click(); btn('clump').click();
    const n0 = count(f0), red0 = ants(f0)[1], start = Uint8Array.from(ed.file.bytes);
    const at = free(ed.file, 1, ed.file.columns * 10 + 10)[0], far = free(ed.file, 1, ed.file.columns * 40 + 40)[0];
    check(part.painter.tapOnly() && part.painter.size() === 1, 'clump page: a tap puts, a drag looks around, one cell is marked');
    check(part.painter.begin({ row: at[0], column: at[1] }) === false && count(ed.file) === n0 + 6 && ants(ed.file)[1] === red0 + 6 && acts === 1, 'clump page: a tap puts a clump of 6 of the picked team, one change');
    check(/Put 6 Red ants at row \d+, column \d+\. One Undo takes them away\./.test(part.panel.textContent) && btn('t1').textContent.indexOf(String(red0 + 6)) >= 0, 'clump page: the box says what was put, and the team\'s count has grown');
    const first = Blocks.layout(ed.file).records.slice(n0).map((r) => r.row + ',' + r.column);
    part.painter.begin({ row: far[0], column: far[1] });
    check(count(ed.file) === n0 + 12 && acts === 2 && ants(ed.file)[1] === red0 + 12, 'clump page: a tap somewhere else puts a new clump: 12 in all, the first is not moved');
    equal(Blocks.layout(ed.file).records.slice(n0, n0 + 6).map((r) => r.row + ',' + r.column), first, 'clump page: the cells of the first clump are the same');
    part.painter.begin({ row: at[0], column: at[1] });
    const all = Blocks.layout(ed.file).records.map((r) => r.row + ',' + r.column);
    check(count(ed.file) === n0 + 18 && acts === 3 && new Set(all).size === all.length, 'clump page: a tap on a clump puts the next one round it: 18 in all, none on another\'s cell, nothing taken away');
    // the number box: up to 250, and no more
    const box = () => part.panel.querySelector('input[data-k="clump"]');
    box().value = '250'; box().fire('change', {});
    check(clump === 250 && box().value === '250' && btn('clump+').disabled && !btn('clump-').disabled, 'clump page: 250 can be typed, and More is off there');
    box().value = '900'; box().fire('change', {});
    check(clump === 250 && box().value === '250', 'clump page: a bigger number is 250');
    btn('clump-').click();
    check(clump === 249 && !btn('clump+').disabled, 'clump page: Fewer goes to 249');
    btn('clump+').click();
    part.painter.begin({ row: Math.floor(ed.file.rows / 2), column: Math.floor(ed.file.columns / 2) });
    check(count(ed.file) === n0 + 268 && acts === 4 && /Put 250 Red ants/.test(part.panel.textContent), 'clump page: a clump of 250 is one change');
    // one Undo for each clump, and the bytes of the start come back
    ed.undo(); check(count(ed.file) === n0 + 18, 'clump page: Undo takes the clump of 250 away and no other ant');
    ed.undo(); ed.undo(); ed.undo();
    check(same(ed.file.bytes, start), 'clump page: four Undos for four clumps give the map as it was, byte for byte');
    // a refusal says why and changes nothing
    const cut = open('Original-Ants/Maps/TINY.LVL'), edc = editor(cut), lay = Blocks.layout(cut), filler = new Uint8Array(6 * (65530 - lay.records.length));
    edc.act({ names: [], cells: [], splices: [{ rel: lay.b1.length, removed: new Uint8Array(0), inserted: filler }, { rel: 0, removed: cut.bytes.slice(cut.blocksAt, cut.blocksAt + 2), inserted: new Uint8Array([0xFA, 0xFF]) }] });
    const partFull = Ants.create({
        doc, win: {}, host: { tools, hint, side }, file: () => edc.file, act: (c) => !!edc.act(c), say: (t, m) => said.push(m),
        dialogs: { open: () => {} }, team: () => 0, setTeam: () => {}, ants: () => 6, setAnts: () => {}, view: () => null, about: () => null, props: () => {}, refresh: () => {}
    });
    partFull.show(true);
    partFull.panel.querySelector('[data-k="group"]').click(); partFull.panel.querySelector('[data-k="clump"]').click();
    const [cf] = free(edc.file, 1, 700), bytesFull = Uint8Array.from(edc.file.bytes), told = said.length;
    partFull.painter.begin({ row: cf[0], column: cf[1] });
    check(said.length === told + 1 && /does not fit/.test(said[told]) && same(edc.file.bytes, bytesFull) && !/Put 6/.test(partFull.panel.textContent), 'clump page: a clump that does not fit says why in a message, changes nothing and puts no "Put" note (' + said[told] + ')');
    part.show(false);
    // a map that fills up: the last clump is as big as the room, and the box says so
    const full = editor(open('Original-Ants/Maps/TINY.LVL')), notes = [];
    const partF = Ants.create({
        doc, win: {}, host: { tools, hint, side }, file: () => full.file, act: (c) => !!full.act(c), say: (t, m) => said.push(m),
        dialogs: { open: () => {} }, team: () => 0, setTeam: () => {}, ants: () => 250, setAnts: () => {}, view: () => null, about: () => null, props: () => {}, refresh: () => {}
    });
    partF.show(true);
    partF.panel.querySelector('[data-k="group"]').click(); partF.panel.querySelector('[data-k="clump"]').click();
    let last = count(full.file);
    for (let k = 0; k < 6; k++) {
        partF.painter.begin({ row: 15, column: 15 });
        const now = count(full.file), text = partF.panel.textContent, m = /Put (\d+) Green ants at row 15, column 15 \(only (\d+) fit\)\./.exec(text);
        if (m) notes.push([+m[1], +m[2], now - last]);
        last = now;
    }
    check(notes.length >= 1 && notes.every(([said1, only, put]) => said1 === only && only === put && only < 250), 'clump page: when the map fills up the box says how many fit, and that is how many were put (' + JSON.stringify(notes) + ')');
    check(count(full.file) === full.file.cells, 'clump page: the clumps fill every cell of the map and no more (' + count(full.file) + ' of ' + full.file.cells + ')');
}

// H
{
    const WRITE = argv.includes('--write-maps') ? path.resolve(argv[argv.indexOf('--write-maps') + 1]) : '';
    for (const [rel, name, size] of [['Original-Ants/Maps/MEDIUM.LVL', 'ClumpsMedium.LVL', 250], ['Original-Ants/Maps/TREASURE.LVL', 'ClumpsTreasure.LVL', 150]]) {      // (TREASURE has room for 2,214 ants)
        const f0 = open(rel), ed = editor(f0), had = ants(f0).slice(), n0 = count(f0);
        const spots = [];
        for (const fr of [0.2, 0.5, 0.8]) for (const fc of [0.2, 0.4, 0.6, 0.8]) spots.push([Math.floor(f0.rows * fr), Math.floor(f0.columns * fc)]);
        let acts = 0;
        for (let t = 0; t < 4; t++) for (let k = 0; k < 3; k++) { const at = spots[(t + 4 * k) % spots.length], ch = Blocks.placeStart(ed.file, t, at[0], at[1], size); if (!ch.error && ed.act(ch)) acts++; }
        check(acts === 12, name + ': twelve clumps of ' + size + ' are twelve changes (' + acts + ')');
        const after = ants(ed.file), back = File.parse(Uint8Array.from(ed.file.bytes));
        check(back.ok && count(back) === n0 + 12 * size && [0, 1, 2, 3].every((t) => after[t] === had[t] + 3 * size && ants(back)[t] === after[t]), name + ': the file reads again, with ' + 3 * size + ' more ants for every team');
        const fresh = Blocks.layout(ed.file).records.slice(n0), cells = fresh.map((r) => r.row + ',' + r.column);
        check(new Set(cells).size === cells.length && fresh.every((r) => r.row < ed.file.rows && r.column < ed.file.columns && !Ants.whyNot(f0, r.row * f0.columns + r.column)), name + ': every new ant on a cell of its own, inside the map, where an ant can walk');
        if (WRITE) { fs.mkdirSync(WRITE, { recursive: true }); fs.writeFileSync(path.join(WRITE, name), Buffer.from(ed.file.bytes)); }
    }
}

console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
