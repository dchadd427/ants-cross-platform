#!/usr/bin/env node
// The map maker's Fix edges (web/front/mapmaker_edges.js, the `ground` list of a CHANGE in mapmaker_edit.js; docs/MAP_EDITOR.md, "Fix edges") run on made-up maps, on the 540 maps of the
// library and on a small fake of the DOM, with no browser:
//
//   node tests/scripts/web_mapmaker_edges_check.js [--front DIR]
//
//   --front DIR  the folder of the mapmaker_*.js files to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
//   A  the shapes: which sides of a cell touch the other kind, and the piece that follows (one side: an edge piece, two sides that meet: a corner piece, anything else: none)
//   B  the rule on made-up maps: the kind of the higher rank wears the piece, the other side stays plain; water only against mud; opposite sides, three sides and a diagonal get none of their own (B2: the cells beside them wear); a piece
//      placed by hand is kept when right (and in its look-alike) and chosen again when not; the border of the map has no neighbor; a region changes nothing outside it
//   B2 the lower kind wears where the higher cannot (a strip of water one cell wide in mud, an inlet, a lone cell): the owner's scene painted stroke by stroke in both orders (Auto edges, the real
//      stroke of mapmaker_edit.js), every pair that has pieces, the pairs that have none (water beside gravel, dirt or slate: nothing is put), no doubled edge, a notch of the lower kind stays plain,
//      and random strokes: the map Auto edges leaves is the one that Fix edges makes from the plain ground, and a second Fix edges finds nothing
//   C  the library (Community-Maps and the original maps): the rule never changes a kind, only tiles of the table change, it is idempotent (a fixed map is left as it is), and how much of the
//      library it changes is held under a cap (the numbers are printed)
//   C2 the game's own maps as the gold standard: Fix edges on TREASURE.LVL changes nothing and on ISLANDS.LVL only its two known gaps, never rewrites a hand-made piece there, and on no map of the
//      library makes two facing cells both wear a piece (the game's maps never have them: a dark seam line)
//   D  the change on real bytes: one step of the history, only the tile words of layer 1 differ (flags, solid bits, layer 2, everything else the same), undo brings back the exact bytes, redo again
//   E  the dialog on a fake page: the part you see or the whole map, the count, before and after drawn from the same cells, Cancel does nothing, Fix edges makes one change, nothing to fix
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Tiles = load('mapmaker_tiles.js'), Edit = load('mapmaker_edit.js'), Ground = load('mapmaker_ground.js'), Edges = load('mapmaker_edges.js'), Dialog = load('mapmaker_dialog.js');
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }
function digest(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }

const PLAIN = {}; Ground.KINDS.forEach((k) => { PLAIN[k.key] = k.plain; });
const LETTER = { g: 'gravel', d: 'dirt', s: 'slate', m: 'mud', w: 'water' };
// a made-up reader of layer 1 from rows of letters (g d s m w); `put` changes one cell
function grid(rows, over, named) {
    const cells = rows.map((r) => r.split('').map((ch) => PLAIN[LETTER[ch]]));
    const count = named || 700, bytes = new Uint8Array(4 + 2 + 11 * count);           // (4 bytes of header, then the dictionary: a word and 11 bytes a name, all of them "." but the plain tiles)
    bytes[4] = (count - 1) & 255; bytes[5] = (count - 1) >> 8;
    for (let i = 0; i < count; i++) bytes[6 + 11 * i] = 0x2E;
    Object.keys(PLAIN).forEach((k) => { const id = PLAIN[k], nm = Ground.name(id); if (id < count) for (let c = 0; c < nm.length; c++) bytes[6 + 11 * id + c] = nm.charCodeAt(c); });
    const f = { rows: rows.length, columns: rows[0].length, tile1: (r, c) => (r < 0 || c < 0 || r >= rows.length || c >= rows[0].length ? 485 : cells[r][c]), cells, names: count, dictAt: 4, bytes };      // (a read beyond the map is gravel, as in mapmaker_file.js)
    Object.keys(over || {}).forEach((k) => { const [r, c] = k.split(',').map(Number); cells[r][c] = over[k]; });
    return f;
}
function apply(f, made) { made.cells.forEach(([i, t]) => { f.cells[(i / f.columns) | 0][i % f.columns] = t; }); }
function nameAt(f, r, c) { return Ground.name(f.tile1(r, c)); }
function names(f, r0, r1) { const out = []; for (let r = r0; r <= r1; r++) { const row = []; for (let c = 0; c < f.columns; c++) row.push(nameAt(f, r, c)); out.push(row.join(' ')); } return out; }

function shapeChecks() {
    const want = { '0000': 0, '1000': 3, '0100': 8, '0010': 5, '0001': 6, '1010': 2, '1001': 4, '0110': 7, '0101': 9, '1100': 0, '0011': 0, '1110': 0, '1101': 0, '1011': 0, '0111': 0, '1111': 0 };
    Object.keys(want).forEach((k) => equal(Edges.shapeOf(+k[0], +k[1], +k[2], +k[3]), want[k], 'north, south, west, east = ' + k));
    equal(Object.keys(Edges.RANK).sort(), Object.keys(PLAIN).sort(), 'a rank for each of the five kinds');
}

function ruleChecks() {
    // gravel in mud: the gravel wears the pieces toward mud (gm), the mud stays plain
    let f = grid(['mmmmm', 'mgggm', 'mgggm', 'mgggm', 'mmmmm']), made = Edges.plan(f, null, Ground);
    equal(made.count, 8, 'gravel in mud: the ring of eight cells changes');
    apply(f, made);
    equal(names(f, 1, 3), ['M01a gm02a gm03a gm04a M01a', 'M01a gm05a g01a gm06a M01a', 'M01a gm07a gm08a gm09a M01a'], 'gravel in mud: the eight pieces round the plain tile, the mud stays plain');
    equal(Edges.plan(f, null, Ground).count, 0, 'a fixed map is left as it is');
    // water in mud: the water wears (wm); water beside gravel, dirt or slate has no piece
    f = grid(['mmmmm', 'mwwwm', 'mwwwm', 'mwwwm', 'mmmmm']); apply(f, Edges.plan(f, null, Ground));
    equal(names(f, 1, 1), ['M01a wm02a wm03a wm04a M01a'], 'water in mud: wm pieces');
    ['g', 'd', 's'].forEach((k) => { f = grid(['www', 'www', k + k + k]); equal(Edges.plan(f, null, Ground).count, 0, 'water beside ' + LETTER[k] + ': no piece exists, nothing changes'); });
    // the other pairs: the higher rank wears (gravel over dirt, slate over dirt, gravel over slate, dirt over mud, slate over mud)
    [['g', 'd', 'gd03a', 'd01a'], ['s', 'd', 'sd03a', 'd01a'], ['g', 's', 'gs03a', 's01a'], ['d', 'm', 'dm03a', 'M01a'], ['s', 'm', 'sm03a', 'M01a']].forEach(([top, under, piece]) => {
        f = grid([under + under + under, top + top + top]); apply(f, Edges.plan(f, null, Ground));
        equal([nameAt(f, 1, 1), Ground.kindOf(f.tile1(0, 1)), Ground.info(f.tile1(0, 1)).other], [piece, LETTER[under], null], LETTER[top] + ' under ' + LETTER[under] + ': the ' + LETTER[top] + ' side wears ' + piece + ', the other stays plain');
    });
    // the shapes by the sides: a channel (opposite sides), three sides and a diagonal get no piece
    f = grid(['mmm', 'ggg', 'mmm']); apply(f, Edges.plan(f, null, Ground)); equal(names(f, 0, 2), ['mg08a mg08a mg08a', 'g01a g01a g01a', 'mg03a mg03a mg03a'], 'a channel of gravel one cell wide: the gravel has no piece for opposite sides, the mud on both sides wears');
    f = grid(['mmm', 'mgm', 'mgm']); made = Edges.plan(f, null, Ground); apply(f, made); equal(nameAt(f, 1, 1), 'g01a', 'a cell with mud on three sides gets none (the mud round it wears)');
    f = grid(['mgg', 'ggg', 'ggg']); equal(Edges.plan(f, null, Ground).cells.map((c) => c[0]), [1, 3], 'mud only at a diagonal: no piece for the cell at its corner (the two beside it have a side)');
    // the border of the map has no neighbor; a tile that is not ground has none
    f = grid(['gg', 'mm']); apply(f, Edges.plan(f, null, Ground)); equal(names(f, 0, 0), ['gm08a gm08a'], 'the border: only the side that has a neighbor counts');
    f = grid(['gg', 'gg'], { '1,1': 100 }); equal(Edges.plan(f, null, Ground).count, 0, 'a tile that is not ground is no kind and stays');
    // pieces placed by hand: right ones stay (in their look-alike too), wrong ones are chosen again
    f = grid(['ggg', 'mmm'], { '0,1': Ground.NAMES.indexOf('gm08b') + Ground.FIRST });
    equal(Edges.plan(f, null, Ground).cells.map((c) => c[0]), [0, 2], 'a piece in its look-alike (b) is right and stays; the cells beside it change');
    f = grid(['ggg', 'mmm'], { '0,1': Ground.NAMES.indexOf('gm03a') + Ground.FIRST }); apply(f, Edges.plan(f, null, Ground)); equal(nameAt(f, 0, 1), 'gm08a', 'a wrong piece is chosen again');
    f = grid(['mg', 'mm'], { '0,0': Ground.NAMES.indexOf('mg06a') + Ground.FIRST });
    equal(Edges.plan(f, null, Ground).cells.map((c) => c[0]), [1], 'a piece toward a higher kind that is the right one (mud with gravel on its east side) stays');
    f = grid(['mmm', 'mmm'], { '0,0': Ground.NAMES.indexOf('mg06a') + Ground.FIRST }); apply(f, Edges.plan(f, null, Ground)); equal(nameAt(f, 0, 0), 'mg06a', 'a piece that the rule cannot place (nothing beside it) stays: it is the map maker\'s choice');
    f = grid(['mmm', 'mmm'], { '0,0': Ground.NAMES.indexOf('M01b') + Ground.FIRST }); equal(Edges.plan(f, null, Ground).count, 0, 'a plain look-alike stays');
    f = grid(['www', 'ggg'], { '0,1': Ground.FIRST + Ground.NAMES.indexOf('wm03a') }); equal(Edges.plan(f, null, Ground).count, 0, 'a wm piece on water beside gravel (no piece exists there) stays');
    // one boundary, one wearer: gravel beside mud that already wears the piece toward it gets none (mud with gravel on its west side wears mg05a)
    f = grid(['gm'], { '0,1': Ground.FIRST + Ground.NAMES.indexOf('mg05a') }); equal(Edges.plan(f, null, Ground).count, 0, 'a neighbor that already wears the facing piece means no second piece (east side)');
    f = grid(['mg'], { '0,0': Ground.FIRST + Ground.NAMES.indexOf('mg06a') }); equal(Edges.plan(f, null, Ground).count, 0, 'the same with the facing cell on the west');
    f = grid(['g', 'm'], { '1,0': Ground.FIRST + Ground.NAMES.indexOf('mg03a') }); equal(Edges.plan(f, null, Ground).count, 0, 'and with the facing cell to the south');
    f = grid(['m', 'g'], { '0,0': Ground.FIRST + Ground.NAMES.indexOf('mg08a') }); equal(Edges.plan(f, null, Ground).count, 0, 'and to the north');
    f = grid(['gm'], { '0,1': Ground.FIRST + Ground.NAMES.indexOf('mg06a') }); equal(Edges.plan(f, null, Ground).count, 1, 'a piece that does not face this cell (mg06a points east) is no reason to leave the boundary bare');
    // a corner piece that faces this cell counts like an edge piece (the mud at north-east wears mg07a toward the gravel at north-west; the gravel keeps only its south side)
    f = grid(['gm', 'mg'], { '0,1': Ground.FIRST + Ground.NAMES.indexOf('mg07a') }); apply(f, Edges.plan(f, null, Ground)); equal(names(f, 0, 0), ['gm08a mg07a'], 'a corner piece that faces a cell is already drawn: only the other side of that cell counts');
    f = grid(['g', 's'], { '1,0': Ground.FIRST + Ground.NAMES.indexOf('sd03a') }); equal(Edges.plan(f, null, Ground).cells.map((c) => c[0]), [0], 'a piece of the neighbor that points here but toward ANOTHER kind (slate toward dirt) does not draw this boundary');
    // the pass is one pass: a neighbor that swaps its piece in the same pass still wears one (the case of two passes: gravel above slate with a piece, mud beside it)
    f = grid(['gg', 'sm'], { '1,0': Ground.FIRST + Ground.NAMES.indexOf('sg03a'), '1,1': Ground.FIRST + Ground.NAMES.indexOf('md06b') });
    apply(f, Edges.plan(f, null, Ground)); equal(Edges.plan(f, null, Ground).count, 0, 'two cells whose pieces depend on each other need no second pass');
    // a cell outside the area keeps its piece, so it still counts as drawn (the same four cells, but only the top row is the area)
    f = grid(['gg', 'sm'], { '1,0': Ground.FIRST + Ground.NAMES.indexOf('sg03a'), '1,1': Ground.FIRST + Ground.NAMES.indexOf('md06b') });
    equal(Edges.plan(f, { r0: 0, r1: 0, c0: 0, c1: 1 }, Ground).cells.map((c) => c[0]), [1], 'a piece outside the area that the rule would change if it were inside is still the piece that counts');
    // the walking class: a cell is never given a tile that walks differently (s01f is a plain look-alike of another class than the slate piece)
    const odd = Ground.FIRST + Ground.NAMES.indexOf('s01f');
    check(Ground.terrain(odd) !== Ground.terrain(Ground.FIRST + Ground.NAMES.indexOf('sm03a')), 'the made-up case has two classes');
    f = grid(['mmm', 'sss'], { '1,1': odd }); equal(Edges.plan(f, null, Ground).cells.map((c) => c[0]), [3, 5], 'a cell whose new tile would walk differently stays');
    // two other kinds: the one with more sides wins
    f = grid(['mmm', 'mgd', 'ggg']); apply(f, Edges.plan(f, null, Ground)); equal(nameAt(f, 1, 1), 'gm02a', 'gravel with mud on two sides and dirt on one: the kind with more sides wins (mud)');
    f = grid(['ddd', 'dgm', 'ggg']); apply(f, Edges.plan(f, null, Ground)); equal(nameAt(f, 1, 1), 'gd02a', 'gravel with dirt on two sides and mud on one: dirt');
    f = grid(['dmd', 'mgd', 'ddd']); apply(f, Edges.plan(f, null, Ground)); equal(nameAt(f, 1, 1), 'gm02a', 'gravel with mud on two sides and dirt on two: both count two, the lower rank (mud) wins');
    // a region: nothing outside it changes, its neighbors still count
    f = grid(['gggg', 'gggg', 'mmmm']); made = Edges.plan(f, { r0: 1, r1: 1, c0: 1, c1: 2 }, Ground);
    equal(made.cells.map((c) => c[0]), [5, 6], 'a region: only its cells change');
    equal(made.region, { r0: 1, r1: 1, c0: 1, c1: 2 }, 'and it says which cells it looked at');
    equal(Edges.plan(f, { r0: -5, r1: 99, c0: -5, c1: 99 }, Ground).count, 4, 'a region beyond the map is cut to the map');
    equal(Edges.plan(grid(['mmmm', 'gggg', 'gggg']), { r0: -5, r1: 99, c0: -5, c1: 99 }, Ground).count, 4, 'and above it too');
    f = grid(['gg', 'mm']); equal(Edges.plan(f, null, Ground).cells, [[0, Ground.FIRST + Ground.NAMES.indexOf('gm08a')], [1, Ground.FIRST + Ground.NAMES.indexOf('gm08a')]], 'cells are [index, tile] in row order');
    f = grid(['gg', 'mm']); made = Edges.plan(f, null, Ground); let ch = Edges.change(f, made, Ground);
    equal([ch.names, ch.cells, ch.solid, ch.ground], [[], [], [], made.cells], 'a change: the tiles of layer 1 and the dictionary, nothing else');
    // the dictionary: the name of a piece that is "." is put in by one splice (and taken out again by Undo); a named piece needs none; a dictionary that ends before the piece grows
    const piece = Ground.FIRST + Ground.NAMES.indexOf('gm08a'), slot = 4 + 2 + 11 * piece;
    equal(ch.splices.length, 1, 'a piece that the dictionary names "." brings one splice');
    const sp = ch.splices[0];
    equal([sp.abs, sp.at, sp.removed.length, sp.inserted.length, Array.from(sp.inserted.subarray(2 + 11 * piece, 2 + 11 * piece + 6))], [true, 4, 2 + 11 * 700, 2 + 11 * 700, [103, 109, 48, 56, 97, 0]], 'the splice is the whole dictionary with the name gm08a in the slot of its id');
    equal(Array.from(sp.removed), Array.from(f.bytes.subarray(4, 4 + 2 + 11 * 700)), 'and it holds the bytes that it takes out');
    check(sp.inserted.every((v, i) => (i >= 2 + 11 * piece && i < 2 + 11 * piece + 11) || v === f.bytes[4 + i]), 'nothing else of the dictionary changes');
    f.bytes.set([103, 109, 48, 56, 97, 0], slot); equal(Edges.change(f, made, Ground).splices.length, 0, 'a piece that the dictionary names already needs no splice');
    const short = grid(['gg', 'mm'], {}, 500), grown = Edges.change(short, Edges.plan(short, null, Ground), Ground).splices[0];
    equal([grown.removed.length, grown.inserted.length, grown.inserted[0] | (grown.inserted[1] << 8)], [2 + 11 * 500, 2 + 11 * (piece + 1), piece], 'a dictionary that ends before the piece grows to it (the word is the number of names less one)');
    equal(Array.from(grown.inserted.subarray(2 + 11 * 500, 2 + 11 * 500 + 2)), [0x2E, 0], 'the new slots before the piece are "."');
}

// ---- the lower kind wears where the higher cannot ----

// A made-up map for the real stroke of mapmaker_edit.js: `rows` x `columns` of plain `fill` ground, version 8 (a dictionary of 700 names, '.' all of them: the pieces are not named here)
function strokeMap(rows, columns, fill) {
    const names = 700, cells = rows * columns, head = 8 + 2 + 30, out = new Uint8Array(head + 2 + 11 * names + 8 + 12 * cells + 12), view = new DataView(out.buffer);
    let at = 0;
    const dword = (v) => { view.setUint32(at, v, true); at += 4; }, word = (v) => { view.setUint16(at, v, true); at += 2; };
    dword(8); dword(1); word(10); at += 30; word(names - 1);
    for (let i = 0; i < names; i++) { out[at] = 0x2E; at += 11; }
    dword(rows); dword(columns);
    for (let i = 0; i < cells; i++) { word(fill); word(0); word(0xCDCD); }
    for (let i = 0; i < cells; i++) { word(0x7FFE); word(0); word(0); }
    word(0); word(0); word(0); word(0x7FFE); word(0); word(6);
    return Edit.create(File.parse(out));
}
// one stroke with Auto edges On, as mapmaker_tools.js does it (`applyAuto`: the brush's square and two cells more all round): `path` is the cells that the pointer comes to, the first is where it goes down
function paintStroke(ed, tile, path, brush) {
    const stroke = ed.begin('ground', tile);
    const auto = (rect) => {
        const made = Edges.strokePlan(ed.file, { r0: rect.r0 - 2, r1: rect.r1 + 2, c0: rect.c0 - 2, c1: rect.c1 + 2 }, Ground, tile, stroke.painted, stroke.beforeTile);
        if (made.count) stroke.change({ names: [], cells: [], solid: [], splices: [], ground: made.cells });
    };
    stroke.stamp(path[0][0], path[0][1], brush); auto(Edit.brushRect(ed.file, path[0][0], path[0][1], brush));
    for (let i = 1; i < path.length; i++) {
        const [r0, c0] = path[i - 1], [r1, c1] = path[i];
        stroke.segment(r0, c0, r1, c1, brush);
        const a = Edit.brushRect(ed.file, r0, c0, brush), b = Edit.brushRect(ed.file, r1, c1, brush);
        auto({ r0: Math.min(a.r0, b.r0), r1: Math.max(a.r1, b.r1), c0: Math.min(a.c0, b.c0), c1: Math.max(a.c1, b.c1) });
    }
    stroke.end();
}
// paint the cells of `picture` (rows of letters) that hold `letter`, row by row with a brush of 1 (a run of cells is one stroke)
function paintLetter(ed, picture, letter) {
    picture.forEach((row, r) => { for (let c = 0; c < row.length; c++) if (row[c] === letter) { let e = c; while (e + 1 < row.length && row[e + 1] === letter) e++; paintStroke(ed, PLAIN[LETTER[letter]], [[r, c], [r, e]], 1); c = e; } });
}
function editNames(ed, r0, r1, c0, c1) {
    const out = [];
    for (let r = r0; r <= r1; r++) { const row = []; for (let c = c0; c <= c1; c++) row.push(Ground.name(ed.file.tile1(r, c))); out.push(row.join(' ')); }
    return out;
}

// the owner's picture: a blob of mud on gravel with two strips of water one cell wide inside it, the lower one bending (g gravel, m mud, w water)
const SCENE = ['gggggggggggg', 'gmmmmmmmmmmg', 'gmmmwmmmmmmg', 'gmmmwmmmmmmg', 'gmmmwmmmmmmg', 'gmmmwmmmmmmg', 'gmmmmmmmmmmg', 'gmmmwwmmmmmg', 'gmmmmwmmmmmg', 'gmmmmwmmmmmg', 'gmmmmmmmmmmg', 'gggggggggggg'];
const SCENE_WEARS = ['gm08a gm08a gm08a gm08a gm08a gm08a gm08a gm08a gm08a gm08a', 'M01a M01a M01a MW08a M01a M01a M01a M01a M01a M01a', 'M01a M01a MW06a w01a MW05a M01a M01a M01a M01a M01a',
    'M01a M01a MW06a w01a MW05a M01a M01a M01a M01a M01a', 'M01a M01a MW06a w01a MW05a M01a M01a M01a M01a M01a', 'M01a M01a MW06a w01a MW05a M01a M01a M01a M01a M01a', 'M01a M01a M01a M01a M01a M01a M01a M01a M01a M01a',
    'M01a M01a MW06a w01a wm04a M01a M01a M01a M01a M01a', 'M01a M01a M01a MW04a w01a MW05a M01a M01a M01a M01a', 'M01a M01a M01a MW06a w01a MW05a M01a M01a M01a M01a', 'M01a M01a M01a M01a MW03a M01a M01a M01a M01a M01a', 'gm03a gm03a gm03a gm03a gm03a gm03a gm03a gm03a gm03a gm03a'];

function lowerChecks() {
    const plain = (k) => Ground.name(PLAIN[k]);
    // every pair that has pieces: a strip of the higher kind one cell wide in the lower kind has no piece of its own, the lower cells on both sides wear the pieces toward it; the higher kind wears
    // them where two or more cells wide
    const pairs = [['g', 'd'], ['g', 'm'], ['g', 's'], ['s', 'd'], ['d', 'm'], ['s', 'm'], ['w', 'm']];
    pairs.forEach(([hi, lo]) => {
        const H = LETTER[hi], L = LETTER[lo], toward = (shape) => Ground.name(Ground.edges(L, H).filter((e) => e.shape === shape)[0].id);
        let f = grid([lo.repeat(3), hi.repeat(3), lo.repeat(3)]);
        apply(f, Edges.plan(f, null, Ground));
        equal(names(f, 0, 2), [[toward(8), toward(8), toward(8)].join(' '), plain(H) + ' ' + plain(H) + ' ' + plain(H), [toward(3), toward(3), toward(3)].join(' ')], H + ' in ' + L + ' one cell wide: no piece on the ' + H + ', the ' + L + ' on both sides wears ' + toward(8) + ' and ' + toward(3));
        equal(Edges.plan(f, null, Ground).count, 0, H + ' strip: a second pass changes nothing');
        f = grid([lo.repeat(4), hi.repeat(4), hi.repeat(4), lo.repeat(4)]);
        apply(f, Edges.plan(f, null, Ground));
        const wears = (shape) => Ground.name(Ground.edges(H, L).filter((e) => e.shape === shape)[0].id);
        equal(names(f, 0, 3).map((row) => row.split(' ')[1]), [plain(L), wears(3), wears(8), plain(L)], H + ' two cells wide in ' + L + ': the ' + H + ' wears (' + wears(3) + ', ' + wears(8) + '), the ' + L + ' stays plain');
    });
    // the pairs that have no piece: water beside gravel, dirt or slate stays as it is, and the editor puts nothing
    ['g', 'd', 's'].forEach((k) => { const f = grid(['www', k.repeat(3), 'www']); equal(Edges.plan(f, null, Ground).count, 0, 'a strip of ' + LETTER[k] + ' in water: no piece exists, nothing changes'); equal(Edges.plan(grid([k.repeat(3), 'www', k.repeat(3)]), null, Ground).count, 0, 'a strip of water in ' + LETTER[k] + ': no piece exists, nothing changes'); });
    // an inlet (the water has mud on three sides) and a lone cell: the mud round it wears, the water stays plain
    let f = grid(['mmmm', 'mwwm', 'mmmm']); apply(f, Edges.plan(f, null, Ground));
    equal(names(f, 0, 2), ['M01a MW08a MW08a M01a', 'MW06a w01a w01a MW05a', 'M01a MW03a MW03a M01a'], 'a stub of water in mud: the mud round it wears');
    f = grid(['mmm', 'mwm', 'mmm']); apply(f, Edges.plan(f, null, Ground));
    equal(names(f, 0, 2), ['M01a MW08a M01a', 'MW06a w01a MW05a', 'M01a MW03a M01a'], 'a lone cell of water in mud: the four cells beside it wear');
    equal(Edges.plan(f, null, Ground).count, 0, 'and a second pass changes nothing');
    f = grid(['gmg', 'ggg']); equal(Edges.plan(f, null, Ground).cells.map((c) => c[0]), [0, 2, 4], 'a lone cell of mud in gravel: the gravel beside and below it wears, as before');
    // where two strips of water are one cell apart the mud between has water on two opposite sides: no piece fits it
    f = grid(['mmm', 'www', 'mmm', 'www', 'mmm']); apply(f, Edges.plan(f, null, Ground));
    equal(names(f, 0, 4).map((row) => row.split(' ')[1]), ['MW08a', 'w01a', 'M01a', 'w01a', 'MW03a'], 'one row of mud between two strips of water has water on opposite sides: it stays plain');
    // a notch of the lower kind (an inner corner of a staircase) stays plain: the higher cells beside it draw it
    f = grid(['ggg', 'gdd', 'gdd']); apply(f, Edges.plan(f, null, Ground)); equal(nameAt(f, 1, 1), 'd01a', 'a notch: the dirt cell with gravel on two sides that meet stays plain');
    // a hand-placed piece of the higher cell that faces a lower cell is a boundary that is drawn: that cell does not wear it a second time (water with mud on three sides keeps its wm03a)
    f = grid(['mmm', 'mwm', 'mmm'], { '1,1': Ground.FIRST + Ground.NAMES.indexOf('wm03a') }); apply(f, Edges.plan(f, null, Ground));
    equal(names(f, 0, 2), ['M01a M01a M01a', 'MW06a wm03a MW05a', 'M01a MW03a M01a'], 'a hand-placed wm03a on the water: the mud to the north is drawn already; the others wear');
    check(doubled(f.rows, f.columns, (r, c) => f.tile1(r, c)) === 0, 'and no boundary has two facing pieces');
    // a hand-placed piece of the mud (mg08a: gravel on the south) is kept, and the water beside it still is drawn by its neighbors
    f = grid(['mmm', 'mwm', 'mgm'], { '0,1': Ground.FIRST + Ground.NAMES.indexOf('md08a') }); apply(f, Edges.plan(f, null, Ground));
    equal(nameAt(f, 0, 1), 'md08a', 'a hand-placed piece beside the water stays');
    // the owner's scene on the whole map: Fix edges
    f = grid(SCENE); apply(f, Edges.plan(f, null, Ground));
    equal(names(f, 0, 11).map((row) => row.split(' ').slice(1, 11).join(' ')), SCENE_WEARS, 'the owner\'s scene (mud on gravel with two strips of water one cell wide, one bending) after Fix edges');
    equal(Edges.plan(f, null, Ground).count, 0, 'and a second Fix edges finds nothing');
}

// Auto edges: painting stroke by stroke leaves the same map in either order, and the same as Fix edges makes of the plain ground
function strokeChecks() {
    const canonical = (ed) => { const rows = ed.file.rows, columns = ed.file.columns, plainOf = (r, c) => Ground.kind(Ground.kindOf(ed.file.tile1(r, c))).plain, f = grid(Array.from({ length: rows }, (_, r) => Array.from({ length: columns }, (_, c) => Ground.kind(Ground.kindOf(plainOf(r, c))).letter).join('')));
        apply(f, Edges.plan(f, null, Ground)); return f; };
    const same = (ed) => { const f = canonical(ed); for (let r = 0; r < ed.file.rows; r++) for (let c = 0; c < ed.file.columns; c++) if (ed.file.tile1(r, c) !== f.tile1(r, c)) return [r, c, Ground.name(ed.file.tile1(r, c)), Ground.name(f.tile1(r, c))]; return true; };
    const gravel = SCENE.map((row) => row.replace(/[mw]/g, 'g')), rows = SCENE.length, columns = SCENE[0].length;
    [['mud first, then the water', ['m', 'w']], ['water first, then the mud round it', ['w', 'm']]].forEach(([what, order]) => {
        const ed = strokeMap(rows, columns, 485);
        order.forEach((letter) => paintLetter(ed, SCENE, letter));
        equal(editNames(ed, 0, 11, 1, 10), SCENE_WEARS, 'Auto edges, the owner\'s scene, ' + what + ': every water and mud boundary has its piece');
        equal(same(ed), true, 'Auto edges, ' + what + ': the map is the one that Fix edges makes of the plain ground');
        equal(Edges.plan(ed.file, null, Ground).count, 0, 'Auto edges, ' + what + ': Fix edges finds nothing left');
    });
    // a brush of 3 and one long stroke, the strips painted over the finished mud, then some of the mud painted over a strip again
    const ed = strokeMap(rows, columns, 485);
    paintStroke(ed, 439, [[2, 2], [2, 9], [8, 9], [8, 2], [3, 2]], 3);
    paintStroke(ed, 476, [[2, 5], [9, 5]], 1);
    paintStroke(ed, 439, [[4, 5], [4, 5]], 1);
    equal(same(ed), true, 'Auto edges: a brush of 3, a long stroke, a strip and a gap painted over it: the map is the one that Fix edges makes of the plain ground');
    // random strokes of any kind: the same, and a second Fix edges finds nothing (the stroke's own plan and Fix edges agree)
    const rnd = seeded(20261023), pick = (n) => Math.floor(rnd() * n), kinds = [485, 487, 571, 439, 476];
    let differ = 0, left = 0, first = null;
    for (let k = 0; k < 120; k++) {
        const rs = 8 + pick(10), cs = 8 + pick(10), m = strokeMap(rs, cs, 485);
        for (let s = 1 + pick(7); s > 0; s--) {
            const path = [[pick(rs), pick(cs)]];
            for (let i = pick(8); i > 0; i--) path.push([Math.max(0, Math.min(rs - 1, path[path.length - 1][0] + pick(5) - 2)), Math.max(0, Math.min(cs - 1, path[path.length - 1][1] + pick(5) - 2))]);
            paintStroke(m, kinds[pick(5)], path, [1, 1, 1, 2, 3][pick(5)]);
        }
        const got = same(m);
        if (got !== true) { differ++; first = first || got; }
        if (Edges.plan(m.file, null, Ground).count) left++;
    }
    equal([differ, left, first], [0, 0, null], 'random strokes (120 maps): Auto edges leaves what Fix edges makes of the plain ground, and Fix edges finds nothing left');
    // a hand-placed piece beside a stroke is kept (a mud piece next to the water that is painted)
    const hand = strokeMap(6, 6, 439); hand.file.bytes.set([Ground.FIRST + Ground.NAMES.indexOf('md08a') & 255, Ground.FIRST + Ground.NAMES.indexOf('md08a') >> 8], hand.file.layer1 + 6 * (2 * 6 + 2));
    paintStroke(hand, 476, [[2, 3], [4, 3]], 1);
    equal(hand.file.tile1(2, 2), Ground.FIRST + Ground.NAMES.indexOf('md08a'), 'Auto edges keeps a piece that was put by hand beside the water');
}

// ---- the library ----
function libraryFiles() {
    const out = [];
    [path.join(ROOT, 'Community-Maps'), path.join(ROOT, 'Original-Ants', 'Maps')].forEach((dir) => fs.readdirSync(dir).filter((n) => /\.lvl$/i.test(n)).sort().forEach((n) => out.push(path.join(dir, n))));
    return out;
}

const SHARE_CAP = 0.03;          // the share of the library's ground cells that Fix edges changes (2.2 in 100 when this was written, 2.6 with the lower kind wearing where the higher cannot; no piece is ever taken away)

function inc() {
    const text = fs.readFileSync(path.join(ROOT, 'src', 'ants_sim', 'movement_tables_data.inc'), 'utf8'), at = text.indexOf('kTileTerrain[1344] = {');
    return text.slice(text.indexOf('{', at) + 1, text.indexOf('};', at)).replace(/\/\*.*?\*\//g, '').split(',').map((x) => x.trim()).filter(Boolean).map(Number);
}

// a small seeded generator (the same maps on every run)
function seeded(seed) { let x = seed >>> 0; return () => { x = (Math.imul(x, 1664525) + 1013904223) >>> 0; return x / 4294967296; }; }

// idempotence: after one pass nothing is left for a second one, on random hand-made grids of any tile of the ground table (pieces everywhere, wrong ones too) and on library maps after random edits
function randomChecks() {
    const rnd = seeded(20261019), pick = (n) => Math.floor(rnd() * n), table = Ground.LAST - Ground.FIRST + 1;
    let again = 0, grids = 0;
    for (let k = 0; k < 400; k++) {
        const rows = 3 + pick(8), columns = 3 + pick(8), cells = [];
        for (let r = 0; r < rows; r++) { const row = []; for (let c = 0; c < columns; c++) row.push(rnd() < 0.5 ? Ground.FIRST + pick(table) : PLAIN[Object.keys(PLAIN)[pick(5)]]); cells.push(row); }
        const f = { rows, columns, tile1: (r, c) => (r < 0 || c < 0 || r >= rows || c >= columns ? 485 : cells[r][c]), cells };
        const made = Edges.plan(f, null, Ground); apply(f, made); grids++;
        if (Edges.plan(f, null, Ground).count) again++;
    }
    equal([grids, again], [400, 0], 'random grids of any tiles (pieces too): a second pass changes nothing');
    const list = libraryFiles().filter((_, i) => i % 9 === 0);
    let edited = 0, left = 0;
    list.forEach((file) => {
        const m = File.parse(new Uint8Array(fs.readFileSync(file)));
        if (!m.ok) return;
        const cells = []; for (let r = 0; r < m.rows; r++) { const row = []; for (let c = 0; c < m.columns; c++) row.push(m.tile1(r, c)); cells.push(row); }
        for (let e = 0; e < 6; e++) {                                          // (six rectangles of the map put to a plain kind or to any tile of the table)
            const r0 = pick(m.rows), c0 = pick(m.columns), r1 = Math.min(m.rows - 1, r0 + pick(6)), c1 = Math.min(m.columns - 1, c0 + pick(6)), any = rnd() < 0.4, plain = PLAIN[Object.keys(PLAIN)[pick(5)]];
            for (let r = r0; r <= r1; r++) for (let c = c0; c <= c1; c++) cells[r][c] = any ? Ground.FIRST + pick(table) : plain;
        }
        const f = { rows: m.rows, columns: m.columns, tile1: (r, c) => (r < 0 || c < 0 || r >= m.rows || c >= m.columns ? 485 : cells[r][c]), cells };
        apply(f, Edges.plan(f, null, Ground)); edited++;
        if (Edges.plan(f, null, Ground).count) left++;
    });
    equal([edited >= 55, left], [true, 0], 'library maps after six random edits: a second pass changes nothing (' + edited + ' maps)');
    // a region: the cells outside it are never given a tile, and the rest of the map is not looked at for a second pass of the same region
    const f = grid(['gggg', 'gggg', 'mmmm', 'mmmm']), region = { r0: 0, r1: 1, c0: 1, c1: 2 };
    apply(f, Edges.plan(f, region, Ground)); equal(Edges.plan(f, region, Ground).count, 0, 'the same area a second time changes nothing');
}

function speedChecks() {
    const n = 512, cells = [];
    for (let r = 0; r < n; r++) { const row = []; for (let c = 0; c < n; c++) row.push(((r >> 3) + (c >> 3)) % 3 === 0 ? 485 : 439); cells.push(row); }
    const f = { rows: n, columns: n, tile1: (r, c) => (r < 0 || c < 0 || r >= n || c >= n ? 485 : cells[r][c]) }, t0 = Date.now(), made = Edges.plan(f, null, Ground), took = Date.now() - t0;
    check(made.count > 30000 && took < 3000, 'a 512 x 512 map is planned in under 3 seconds (' + took + ' ms, ' + made.count + ' cells)');
}

const FACES = { S: [7, 8, 9], N: [2, 3, 4], E: [4, 6, 9], W: [2, 5, 7] };
// how many pairs of neighbors (one boundary) have BOTH cells wearing a piece that faces the other
function doubled(rows, columns, at) {
    let n = 0;
    for (let r = 0; r < rows; r++) for (let c = 0; c < columns; c++) [['S', 1, 0, 'N'], ['E', 0, 1, 'W']].forEach(([d, dr, dc, back]) => {
        if (r + dr >= rows || c + dc >= columns) return;
        const a = Ground.info(at(r, c)), b = Ground.info(at(r + dr, c + dc));
        if (a && b && a.kind !== b.kind && a.other === b.kind && FACES[d].includes(a.shape) && b.other === a.kind && FACES[back].includes(b.shape)) n++;
    });
    return n;
}

function goldChecks() {
    const limits = { 'TREASURE.LVL': 0, 'ISLANDS.LVL': 2, 'MEDIUM.LVL': 0 };         // (MEDIUM draws its lakes the other way, the mud wearing toward the water: nothing to add there either)
    Object.keys(limits).forEach((name) => {
        const f = File.parse(new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', name))));
        const made = Edges.plan(f, null, Ground), by = new Map(made.cells);
        check(made.count <= limits[name], name + ': Fix edges changes at most ' + limits[name] + ' cells (' + made.count + ')');
        check(made.cells.every(([i]) => { const t = Ground.info(f.tile1((i / f.columns) | 0, i % f.columns)); return !(t && t.other); }), name + ': no piece that the author placed is rewritten');
        check(doubled(f.rows, f.columns, (r, c) => (by.has(r * f.columns + c) ? by.get(r * f.columns + c) : f.tile1(r, c))) === 0, name + ': no boundary has a piece on both facing cells');
        if (name === 'ISLANDS.LVL') equal(made.cells.map(([i, t]) => [(i / f.columns) | 0, i % f.columns, Ground.name(t)]), [[34, 53, 'wm06a'], [46, 17, 'gm03a']], 'ISLANDS: the two cells are real gaps of the original (a plain edge cell)');
    });
}

function libraryChecks() {
    const table = inc();
    equal(Ground.TERRAIN, table.slice(Ground.FIRST, Ground.LAST + 1).join(''), 'the walking classes of the ground table are those of the engine (kTileTerrain)');
    let wrong = 0; for (let id = Ground.FIRST; id <= Ground.LAST; id++) if (Ground.terrain(id) !== table[id]) wrong++;
    equal([wrong, Ground.terrain(1), Ground.terrain(Ground.LAST + 1)], [0, -1, -1], 'terrain(id) answers with the engine\'s class for every ground tile and -1 for a tile outside the table');
    const list = libraryFiles(), sum = { maps: 0, cells: 0, changed: 0, same: 0, toPiece: 0, toPlain: 0, other: 0, worst: 0, doubleBefore: 0, doubleAfter: 0 }, bad = { kind: 0, table: 0, again: 0, order: 0, walk: 0, doubled: 0 };
    list.forEach((file) => {
        const f = File.parse(new Uint8Array(fs.readFileSync(file)));
        if (!f.ok) return;
        const reader = { rows: f.rows, columns: f.columns, tile1: (r, c) => f.tile1(r, c) }, was = [], total = f.rows * f.columns;
        for (let i = 0; i < total; i++) was.push(f.tile1((i / f.columns) | 0, i % f.columns));
        const made = Edges.plan(reader, null, Ground), cells = was.slice();
        let last = -1;
        made.cells.forEach(([i, t]) => {
            const a = Ground.info(was[i]), b = Ground.info(t);
            if (!a || !b) { bad.table++; return; }
            if (a.kind !== b.kind) bad.kind++;
            if (i <= last) bad.order++;
            if (Ground.terrain(was[i]) !== Ground.terrain(t)) bad.walk++;
            last = i; cells[i] = t;
            if (b.other) sum.toPiece++; else sum.toPlain++;
        });
        const again = Edges.plan({ rows: f.rows, columns: f.columns, tile1: (r, c) => cells[r * f.columns + c] }, null, Ground);
        if (again.count) bad.again++;
        const was2 = doubled(f.rows, f.columns, (r, c) => was[r * f.columns + c]), now2 = doubled(f.rows, f.columns, (r, c) => cells[r * f.columns + c]);
        sum.doubleBefore += was2; sum.doubleAfter += now2; if (now2 > was2) bad.doubled++;
        sum.maps++; sum.cells += total; sum.changed += made.count; if (!made.count) sum.same++;
        sum.worst = Math.max(sum.worst, made.count / total);
    });
    console.log('    library: ' + sum.maps + ' maps, ' + sum.cells + ' ground cells; Fix edges changes ' + sum.changed + ' (' + (100 * sum.changed / sum.cells).toFixed(1) + ' in 100: ' + sum.toPiece + ' get a piece, ' + sum.toPlain + ' go back to plain); ' + sum.same + ' maps stay as they are; the most changed map: ' + (100 * sum.worst).toFixed(1) + ' in 100');
    check(sum.maps >= 540, 'the library has its 540 maps (' + sum.maps + ')');
    equal(bad, { kind: 0, table: 0, again: 0, order: 0, walk: 0, doubled: 0 }, 'the library: no kind or walking class changes, the rule creates no boundary with two facing pieces (57 hand-made ones remain in 6 community maps), only tiles of the table, a fixed map is left as it is, cells in row order');
    check(sum.changed / sum.cells < SHARE_CAP, 'the share of the library changed is under ' + SHARE_CAP + ': ' + (sum.changed / sum.cells));
    check(sum.doubleAfter === sum.doubleBefore, 'boundaries with two facing pieces: ' + sum.doubleBefore + ' placed by hand in the library, ' + sum.doubleAfter + ' after Fix edges');
    check(sum.toPlain === 0, 'no piece goes back to plain: the rule only puts pieces where it has an answer');
    check(sum.same >= 100, 'at least 100 maps are already right (' + sum.same + ')');
}

// ---- the change on real bytes ----
// A made-up map of version 8 or 9: `rows` x `columns` of gravel with mud in the lower half, and a dictionary of 486 names (a new map's: it ends at gravel)
function makeMap(version, rows, columns) {
    const names = 486, cells = rows * columns, head = version === 8 ? 8 + 2 + 30 : 8 + 2 + 4 + 2 + 2;
    const out = new Uint8Array(head + 2 + 11 * names + 8 + 12 * cells + 12), view = new DataView(out.buffer);
    let at = 0;
    const dword = (v) => { view.setUint32(at, v, true); at += 4; }, word = (v) => { view.setUint16(at, v, true); at += 2; };
    if (version === 8) { dword(8); dword(1); word(10); at += 30; }
    else { dword(9); dword(1); word(10); dword(0); word(1); word(0); }
    word(names - 1);
    for (let i = 0; i < names; i++) { out[at] = 0x2E; if (i === 485 || i === 439) { const nm = Ground.name(i); for (let c = 0; c < nm.length; c++) out[at + c] = nm.charCodeAt(c); } at += 11; }
    dword(rows); dword(columns);
    for (let r = 0; r < rows; r++) for (let c = 0; c < columns; c++) { word(r < rows / 2 ? 485 : 439); word(r === 1 ? 1 : 0); word(0xCDCD); }
    for (let i = 0; i < cells; i++) { word(0x7FFE); word(0); word(0); }
    word(0); word(0); word(0); word(0x7FFE); word(0); word(6);
    return out;
}

// the change of the whole map on a file's bytes: what changes, what stays, one Undo, Redo
function verify(name, f, hooksRebase) {
    const made = Edges.plan(f, null, Ground), copy = f.bytes.slice(), was = digest(copy), first = { layer1: f.layer1, names: f.names, dictAt: f.dictAt, rows: f.rows, columns: f.columns };
    const ed = Edit.create(f, { rebase: hooksRebase });
    const flagsBefore = made.cells.map(([i]) => [Edit.flags(f, 1, i), Edit.third(f, 1, i)]), change = Edges.change(f, made, Ground);
    const dirty = ed.act(change), now = ed.file;
    equal(dirty && dirty.length, made.count, name + ': the cells to paint again are the cells changed');
    check(now.rows === first.rows && now.columns === first.columns, name + ': the grid is the same');
    // everything before the dictionary and everything from the grid's size on is the same bytes but the tile words of the cells; the dictionary only gets names
    let differ = 0, outside = 0, i;
    for (i = 0; i < first.dictAt; i++) if (copy[i] !== now.bytes[i]) outside++;
    const tailA = copy.subarray(first.layer1 - 8), tailB = now.bytes.subarray(now.layer1 - 8);
    check(tailA.length === tailB.length, name + ': the part from the grid on has the same length');
    for (i = 0; i < tailA.length; i++) if (tailA[i] !== tailB[i]) { differ++; const off = i - 8; if (off < 0 || off >= 6 * first.rows * first.columns || off % 6 > 1) outside++; }
    check(differ > 0 && outside === 0, name + ': only the tile words of layer 1 changed outside the dictionary (' + differ + ' bytes, ' + outside + ' outside)');
    const entry = (bytes, dictAt, k) => Buffer.from(bytes.subarray(dictAt + 2 + 11 * k, dictAt + 13 + 11 * k)).toString('latin1').split('\0')[0];
    const oldNames = [], newNames = [];
    for (i = 0; i < first.names; i++) oldNames.push(entry(copy, first.dictAt, i));
    for (i = 0; i < now.names; i++) newNames.push(entry(now.bytes, now.dictAt, i));
    check(oldNames.every((n, k) => n === newNames[k] || n === '.'), name + ': the dictionary keeps its names; a "." gets a name');
    check(newNames.every((n, k) => k < oldNames.length || n === '.' || n === Ground.name(k)), name + ': a new slot is "." or the piece\'s name');
    check(made.cells.every(([idx, t]) => newNames[t] === Ground.name(t)), name + ': every piece that was put has its name in the dictionary (the original does not draw a tile named ".")');
    made.cells.forEach(([idx, t], k) => {
        if (Edit.tile(now, 1, idx) !== t || Edit.flags(now, 1, idx) !== flagsBefore[k][0] || Edit.third(now, 1, idx) !== flagsBefore[k][1]) check(false, name + ': cell ' + idx + ' has its tile, its flags and its third word kept');
    });
    const fixed = digest(now.bytes);
    check(ed.history.canUndo() && ed.undo() !== null, name + ': one Undo');
    check(digest(ed.file.bytes) === was, name + ': undo brings back the exact bytes (the dictionary too)');
    check(!ed.history.canUndo() && ed.history.isClean(), name + ': the whole fix was one step');
    check(ed.redo() !== null && digest(ed.file.bytes) === fixed, name + ': redo makes it again');
    equal(Edges.plan(ed.file, null, Ground).count, 0, name + ': nothing is left to fix');
    return { named: change.splices.length > 0, grew: now.names > first.names };
}

function bytesChecks() {
    const rebase = (next) => { const r = File.parse(next); if (!r.ok) throw new Error(r.error); return r; };
    const taken = { plain: 0, named: 0 }, want = { plain: 3, named: 3 };          // (library maps that need no name, library maps that need names)
    libraryFiles().forEach((file) => {
        const f = File.parse(new Uint8Array(fs.readFileSync(file)));
        if (!f.ok) return;
        const made = Edges.plan(f, null, Ground);
        if (!made.count) return;
        const sort = Edges.change(f, made, Ground).splices.length ? 'named' : 'plain';
        if (taken[sort] >= want[sort]) return;
        taken[sort]++;
        const got = verify(path.basename(file), f, rebase);
        check((sort === 'named') === got.named, path.basename(file) + ': a dictionary that has "." for a piece gets its name');
    });
    equal(taken, want, 'maps of the library tried');
    [8, 9].forEach((version) => {
        const f = File.parse(makeMap(version, 12, 14));
        if (!f.ok) throw new Error(f.error);
        const got = verify('a made-up map of version ' + version, f, rebase);
        check(got.named && got.grew, 'version ' + version + ': the dictionary of a new map ends at gravel, so it grows to hold the pieces');
    });
}

// ---- the dialog ----
function page(sprites, visible, big) {
    const doc = makeDocument(), host = doc.createElement('div'), body = doc.createElement('div'), opener = doc.createElement('button');
    [host, body].forEach((n) => doc.body.appendChild(n)); body.appendChild(opener); opener.focus();
    const make = doc.createElement;
    doc.createElement = (tag) => {
        const n = make(tag);
        if (tag === 'canvas') { n.log = []; n.width = n.height = 0; n.getContext = () => ({ canvas: n, fillRect: (x, y, w, h) => n.log.push(['rect', this_style, x, y, w, h]), drawImage() {}, set fillStyle(v) { this_style = v; }, set imageSmoothingEnabled(v) {}, set imageSmoothingQuality(v) {} }); }
        return n;
    };
    let this_style = '';
    const f = big ? grid(Array.from({ length: 70 }, (_, r) => (r < 35 ? 'g'.repeat(90) : 'm'.repeat(90)))) : grid(['mmmmmm', 'mgggmm', 'mgggmm', 'mgggmm', 'mmmmmm', 'dddddd', 'gggggg']);
    const applied = [], dialogs = Dialog.create(doc, host, [body]);
    const env = { doc, dialogs, ground: Ground, file: () => f, visible: () => visible, apply: (ch) => { applied.push(ch); return true; }, sprites: () => sprites };
    return { doc, host, f, applied, dialogs, env };
}
function dialogChecks() {
    const shown = [], sprites = { ground(ctx, id, x, y, s) { shown.push([ctx.canvas, id, x, y, s]); return true; } };
    let p = page(sprites, { r0: 1, r1: 3, c0: 0, c1: 5 });
    Edges.open(p.env);
    check(p.dialogs.isOpen() && p.host.querySelector('h2').textContent === 'Fix edges', 'the dialog opens with its heading');
    const radios = p.host.querySelectorAll('input'), buttons = p.host.querySelectorAll('button'), caps = p.host.querySelectorAll('figcaption');
    equal(radios.map((r) => r.value + ':' + !!r.checked), ['part:true', 'all:false'], 'the part you see is chosen first');
    equal(buttons.map((b) => b.textContent + ':' + !!b.disabled), ['Cancel:false', 'Fix edges:false'], 'Cancel and Fix edges');
    equal(caps.map((c) => c.textContent), ['Before', 'After: 8 cells change'], 'the captions count the cells of the part you see');
    const pictures = (cv) => shown.filter((s) => s[0] === cv).map((s) => Ground.name(s[1]));
    const cvs = p.host.querySelectorAll('canvas');
    check(pictures(cvs[0]).join(' ').indexOf('gm') < 0 && pictures(cvs[1]).filter((n) => /^gm/.test(n)).length === 8, 'before has plain tiles only and after has the eight pieces');
    check(cvs[0].width > 0 && cvs[0].width === cvs[1].width && cvs[0].height === cvs[1].height, 'both pictures are of the same size');
    radios[1].checked = true; radios[1].fire('change', {});
    equal(p.host.querySelectorAll('figcaption')[1].textContent, 'After: 20 cells change', 'the whole map: every cell that needs a piece');
    p.dialogs.close();
    p = page(sprites, { r0: 0, r1: 0, c0: 0, c1: 5 }); Edges.open(p.env);
    equal(p.host.querySelectorAll('figcaption')[1].textContent, 'After: nothing changes', 'a part with nothing to fix says so');
    check(p.host.querySelectorAll('button')[1].disabled === true, 'and Fix edges is off');
    p.host.querySelectorAll('input')[1].checked = true; p.host.querySelectorAll('input')[1].fire('change', {});
    check(p.host.querySelectorAll('button')[1].disabled === false && p.host.querySelectorAll('figcaption')[1].textContent === 'After: 20 cells change', 'the whole map has them, and Fix edges is on again');
    p.host.querySelectorAll('button')[0].click();
    check(!p.dialogs.isOpen() && p.applied.length === 0, 'Cancel closes it and changes nothing');
    // Fix edges: one change, the cells of the plan, then it closes
    p = page(sprites, { r0: 1, r1: 3, c0: 0, c1: 5 }); Edges.open(p.env);
    p.host.querySelectorAll('button')[1].click();
    equal(p.applied.length, 1, 'Fix edges makes one change');
    equal(p.applied[0], Edges.change(p.f, Edges.plan(p.f, { r0: 1, r1: 3, c0: 0, c1: 5 }, Ground), Ground), 'it is the change of the plan of the part you see');
    equal(p.applied[0].ground.length, 8, 'with its eight cells');
    check(!p.dialogs.isOpen(), 'and the dialog closes');
    p = page(sprites, { r0: 1, r1: 3, c0: 0, c1: 5 }); Edges.open(p.env);
    p.host.querySelectorAll('input')[1].checked = true; p.host.querySelectorAll('input')[1].fire('change', {});
    p.host.querySelectorAll('button')[1].click();
    equal(p.applied.length === 1 && p.applied[0], Edges.change(p.f, Edges.plan(p.f, null, Ground), Ground), 'the whole map: the change of the plan of every cell');
    // the screen is nowhere (no cell visible): the whole map is the only choice
    p = page(sprites, null); Edges.open(p.env);
    const r = p.host.querySelectorAll('input');
    check(r[0].disabled === true && r[1].checked === true, 'with no visible part the whole map is the only choice');
    // flat colors (no pictures yet): both drawn with rectangles
    p = page(null, { r0: 0, r1: 6, c0: 0, c1: 5 }); Edges.open(p.env);
    const flat = p.host.querySelectorAll('canvas');
    check(flat[0].log.length === 42 && flat[1].log.length === 42, 'without the pictures every cell is a flat rectangle (' + flat[0].log.length + ')');
    // a big area: the pictures show the window of cells round the first change, and say so
    p = page(sprites, null, true); Edges.open(p.env);
    const cv = p.host.querySelectorAll('canvas')[0];
    check(p.host.querySelectorAll('p').some((q) => /48 by 32 cells/.test(q.textContent)), 'a big area says that the pictures show 48 by 32 cells');
    check(p.host.querySelectorAll('figcaption')[1].textContent === 'After: 90 cells change', 'the count is of the whole area: ' + p.host.querySelectorAll('figcaption')[1].textContent);
    check(shown.filter((q) => q[0] === p.host.querySelectorAll('canvas')[1] && /^gm/.test(Ground.name(q[1]))).length === 48, 'the window holds the cells that change: the 48 pieces of its width');
    check(cv.width <= 48 * 16 && cv.height <= 32 * 16 && cv.width >= 48 * 2, 'and the picture is bounded');
}

function strokePlanChecks() {
    // N2d: painting a 3x3 gravel patch into dirt gives the same 8 edge and corner pieces as Fix edges
    const before = grid(['ddddddd', 'ddddddd', 'ddddddd', 'ddddddd', 'ddddddd', 'ddddddd', 'ddddddd']);
    const after = grid(['ddddddd', 'ddddddd', 'ddgggdd', 'ddgggdd', 'ddgggdd', 'ddddddd', 'ddddddd']);
    const painted = {};
    for (let r = 2; r <= 4; r++) for (let c = 2; c <= 4; c++) painted[r * 7 + c] = 1;
    const sp = Edges.strokePlan(after, { r0: 0, r1: 6, c0: 0, c1: 6 }, Ground, 485, painted, (k) => before.tile1((k / 7) | 0, k % 7));
    const fp = Edges.plan(after, null, Ground);
    equal(sp.cells, fp.cells, 'strokePlan on a painted 3x3 gravel patch in dirt matches Fix edges');
    // N2e: a hand-placed piece outside painted (at row 2, col 1: ds03a on dirt) is left alone while Auto edges paints gravel beside it
    const ds03a = Ground.edges('dirt', 'slate')[1].id;
    const bHand = grid(['ddddddd', 'ddddddd', 'ddddddd', 'ddddddd', 'ddddddd', 'ddddddd', 'ddddddd']);
    bHand.cells[2][1] = ds03a;
    const aHand = grid(['ddddddd', 'ddddddd', 'ddgggdd', 'ddgggdd', 'ddgggdd', 'ddddddd', 'ddddddd']);
    aHand.cells[2][1] = ds03a;
    const spHand = Edges.strokePlan(aHand, { r0: 0, r1: 6, c0: 0, c1: 6 }, Ground, 485, painted, (k) => bHand.tile1((k / 7) | 0, k % 7));
    check(spHand.cells.every(([idx]) => idx !== 2 * 7 + 1), 'a hand-placed tile next to the stroke is not overwritten by strokePlan');
    // Painting over the hand-placed cell and the column below it (3x4 patch) replaces it and sets its NW corner piece
    aHand.cells[2][1] = 485; aHand.cells[3][1] = 485; aHand.cells[4][1] = 485;
    painted[2 * 7 + 1] = 1; painted[3 * 7 + 1] = 1; painted[4 * 7 + 1] = 1;
    const spOver = Edges.strokePlan(aHand, { r0: 0, r1: 6, c0: 0, c1: 6 }, Ground, 485, painted, (k) => bHand.tile1((k / 7) | 0, k % 7));
    check(spOver.cells.some(([idx, t]) => idx === 2 * 7 + 1 && Ground.info(t).kind === 'gravel' && Ground.info(t).other === 'dirt'), 'painting over the hand-placed cell replaces it and sets its edge piece');
}

shapeChecks();
ruleChecks();
goldChecks();
libraryChecks();
randomChecks();
lowerChecks();
strokeChecks();
speedChecks();
bytesChecks();
dialogChecks();
strokePlanChecks();
console.log(failures ? failures + ' of ' + checks + ' checks FAILED' : 'edges check: ' + checks + ' checks passed');
process.exit(failures ? 1 : 0);
