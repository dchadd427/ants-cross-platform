#!/usr/bin/env node
// The map maker draws each tile the way the GAME draws it (web/front/mapmaker_tiles.js: tableOf, ground, object, stage, place; web/front/mapmaker_sprites.js: groundAnim, objectAnim; docs/MAP_EDITOR.md,
// "Sprites"): through the open map's own dictionary, not through the cell's number. The game's renderer (src/ants_app/renderer.cpp, Renderer::set_level) resolves the picture of a dictionary entry:
// an entry that is the name of the animation of its own number (any case) is that number; "." or an empty entry draws nothing; any other name is looked up in the animation table. Run with no browser:
//
//   node tests/scripts/web_mapmaker_names_check.js [--front DIR] [--atlas DIR]
//
//   --front DIR   the folder of the mapmaker_*.js files to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//   --atlas DIR   a folder made by tools/map_atlas.py (default: made here in a temporary folder)
//
//   A  the rule on made-up dictionaries and a made-up animation table: the number when the entry is the animation's own name (case apart), the name's animation when it is not, nothing for "." and
//      for an empty entry and for a name that no animation has, the last of two equal names and the first of two that differ in case, the start markers never, the ids that the game draws from the
//      id itself (power-ups, bombs, the fire wall, hills, bridges), an entry that begins with "pu", an entry outside the dictionary, a pile's stage that falls back to its number, a tile about to be put,
//      the stamp that changes with the dictionary and not with the file that holds it, the colors
//   B  the atlas names are the docs' animation table, and every picture that the game's rule can ask for is in the atlas
//   C  a frame kept from before (the flat colors painted cell by cell after a stroke) is painted whole when a name of the dictionary changes
//   D  the library: for EVERY cell of the 540 maps (layer 1, every anchored object of layer 2, every pile's stage) the animation that the editor draws is the one that an independent oracle gives, which
//      is written again from renderer.cpp with its own reader of the files (a linear search of the docs' table, not the editor's tables); and how many cells and maps the pictures by number got wrong
//      (printed: they must be some, or the check sees nothing)
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
let ATLAS = argv.includes('--atlas') ? path.resolve(argv[argv.indexOf('--atlas') + 1]) : '';
if (!ATLAS) {
    ATLAS = fs.mkdtempSync(path.join(os.tmpdir(), 'ants_atlas_'));
    const made = spawnSync(process.env.PYTHON || 'python3', [path.join(ROOT, 'tools', 'map_atlas.py'), ATLAS], { encoding: 'utf8' });
    if (made.status !== 0) { console.log('the atlas could not be made: ' + made.stdout + made.stderr); process.exit(1); }
}
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Tiles = load('mapmaker_tiles.js'), Sprites = load('mapmaker_sprites.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }

const EMPTY2 = 0x7FFE;
const INDEX = JSON.parse(fs.readFileSync(path.join(ATLAS, 'atlas.json'), 'utf8'));
const DOCS = JSON.parse(fs.readFileSync(path.join(ROOT, 'docs', 'chd_table4_animations.json'), 'utf8')).map((e) => e.name);

// ---- a made-up map: only what the reader needs of the file (the dictionary and the layers) ----
function fake(dictionary, rows, columns) {
    const names = dictionary.length, dictAt = 40, layer1 = dictAt + 2 + 11 * names + 8, cells = rows * columns, layer2 = layer1 + 6 * cells, bytes = new Uint8Array(layer2 + 6 * cells + 16);
    bytes[dictAt] = (names - 1) & 255; bytes[dictAt + 1] = (names - 1) >> 8;
    dictionary.forEach((n, i) => { for (let k = 0; k < Math.min(11, n.length); k++) bytes[dictAt + 2 + 11 * i + k] = n.charCodeAt(k); });
    return { bytes, names, dictAt, rows, columns, layer1, layer2, blocksAt: layer2 + 6 * cells };
}
function setName(file, id, name) { const at = file.dictAt + 2 + 11 * id; for (let k = 0; k < 11; k++) file.bytes[at + k] = k < name.length ? name.charCodeAt(k) : 0; }

function caseA() {
    const table = ['jacks3', 'jacks4', 'FdPezP1', 'pu_bomb', 'x', 'X', 'Ab', 'aB', 'dup', 'dup', 'pumpkin', 'lunchbox'];
    Tiles.setAnimations(table);
    const dict = ['.', 'jacks4', 'jacks4', 'JACKS3', 'zzz', 'fdpezp1', 'x', 'AB', 'dup', '', 'PU_BOMB', '', 'AB', 'dup', 'DUP'];
    const f = fake(dict, 1, 4), tab = Tiles.tableOf(f);
    check(!!tab && tab.n === dict.length, 'a table for the dictionary');
    // entry 1 is "jacks4": the animation of number 1 is jacks4 -> the number
    equal(Tiles.ground(tab, 1), 1, 'an entry that is the name of its own number is the number');
    equal(Tiles.ground(tab, 2), 1, 'an entry that is another animation\'s name is that animation (entry 2 "jacks4" is animation 1)');
    equal(Tiles.ground(tab, 3), 0, 'a name of another case is the same name (JACKS3 is jacks3)');
    equal(Tiles.ground(tab, 5), 2, 'fdpezp1 in lower case finds FdPezP1');
    equal(Tiles.ground(tab, 4), -1, 'a name that no animation has draws nothing');
    equal(Tiles.ground(tab, 0), -1, '"." draws nothing');
    equal(Tiles.ground(tab, 9), -1, 'an empty entry draws nothing');
    equal(Tiles.ground(tab, 99), -1, 'a number beyond the dictionary draws nothing');
    equal(Tiles.ground(tab, 6), 4, 'x is the exact name x (animation 4), not X (5)');
    equal(Tiles.ground(tab, 12), 6, 'AB: no exact name, the first of the two that differ in case (Ab, 6)');
    equal(Tiles.ground(tab, 7), 7, 'but AB at 7 is the own name of animation 7 (aB, case apart): the number');
    equal(Tiles.ground(tab, 13), 9, 'the last of two equal names wins the exact lookup (dup: 9; entry 13 is not number 8, so it is looked up)');
    equal(Tiles.ground(tab, 14), 8, 'DUP in capitals: the lower-case table has the first (8)');
    equal(Tiles.ground(tab, 8), 8, 'an entry that is the own name of its number (dup at 8) is the number, though 9 has the same name');
    equal(Tiles.ground(null, 123), 123, 'with no table a tile is its number');
    // the start markers (152 .. 155): never drawn, whatever their entries
    const big = [], real = DOCS.slice();
    for (let i = 0; i < 160; i++) big.push(i === 152 || i === 153 ? real[i] : '.');
    Tiles.setAnimations(real);
    const bf = fake(big, 1, 1), bt = Tiles.tableOf(bf);
    equal([Tiles.ground(bt, 152), Tiles.ground(bt, 153), Tiles.object(bt, 152), Tiles.object(bt, 155)], [-1, -1, -1, -1], 'a start marker is never drawn (BSTART, USTART named in the dictionary)');
    // the ids that the game draws from the id itself
    for (const id of [34, 35, 36, 37, 38, 62, 63, 64, 65, 66, 129, 130, 131, 132, 134, 245, 246, 247, 248]) {
        equal(Tiles.object(bt, id), id, 'tile ' + id + ' is drawn from its number although its entry is "."');
        equal(Tiles.object(Tiles.tableOf(fake(['.'], 1, 1)), id), id, 'tile ' + id + ' is drawn from its number although the dictionary does not reach it');
    }
    equal([Tiles.object(bt, 100), Tiles.object(bt, 2000), Tiles.object(bt, 133)], [-1, -1, -1], 'an object whose entry is "." or outside the dictionary draws nothing');
    // a name that begins with "pu"
    Tiles.setAnimations(table);
    const pf = fake(['.', '.', '.', '.', '.', '.', '.', '.', '.', '.', 'pumpkin', 'puff', 'pu_bomb'], 1, 1), pt = Tiles.tableOf(pf);
    equal([Tiles.object(pt, 10), Tiles.object(pt, 11), Tiles.object(pt, 12)], [-1, -1, -1], 'an entry that begins with "pu" is not a static object (the game keeps the power-ups for the cell\'s own flag)');
    equal(Tiles.ground(pt, 10), 10, 'but a ground cell takes it (pumpkin is animation 10)');
    // a pile's stage: the name's animation, else its own number when it has an animation
    equal([Tiles.stage(tab, 2), Tiles.stage(tab, 4), Tiles.stage(tab, 0), Tiles.stage(tab, 99), Tiles.stage(null, 7)], [1, 4, 0, -1, 7], 'a stage is the name\'s animation; "." or no animation: its own number while the table has it; beyond the table nothing');
    // a tile about to be put: "." and outside are named by the tool, the number is the picture
    equal([Tiles.place(tab, 0), Tiles.place(tab, 99), Tiles.place(tab, 2), Tiles.place(tab, 4), Tiles.place(null, 5)], [0, 99, 1, -1, 5], 'a tile that is put: the number for "." and outside; the entry that stays for another');
    // the table follows the dictionary, not the file object
    const g = fake(dict, 1, 4), t1 = Tiles.tableOf(f), t2 = Tiles.tableOf(g);
    check(t1 === t2 && t1.stamp === t2.stamp, 'the same dictionary in another file object is the same table (the stamp does not move)');
    setName(g, 4, 'jacks3');
    const t3 = Tiles.tableOf(g);
    check(t3.stamp !== t2.stamp && Tiles.ground(t3, 4) === 0, 'a name written in place makes a new table with a new stamp, and the picture follows');
    setName(g, 4, 'jacks3');
    check(Tiles.tableOf(g) === t3, 'the same name again changes nothing');
    check(Tiles.tableOf(Object.assign({}, g, { bytes: new Uint8Array(3) })) === null, 'a file whose dictionary the bytes do not hold has no table');
    Tiles.setAnimations(null);
    check(Tiles.tableOf(g) === null && Tiles.ground(null, 5) === 5, 'without animation names there is no table: tiles are their numbers');
    // the colors follow the names
    Tiles.setAnimations(real);
    Tiles.setAverages('000000'.repeat(239), { 339: 0x112233, 340: 0x445566, 281: 0x778899, 282: 0xaabbcc });          // (colors of the pictures that the next lines look at; a made-up atlas)
    const own = real.slice(0, 700), shifted = own.slice(), blanks = own.slice();
    shifted[340] = 'jacks4'; shifted[282] = 'fdfrl2'; blanks[485] = '.';
    const ct = Tiles.tableOf(fake(shifted, 1, 2)), kt = Tiles.tableOf(fake(own, 1, 2)), bt2 = Tiles.tableOf(fake(blanks, 1, 2));
    equal([Tiles.css(500, 340, null), Tiles.css(500, 340, kt)], ['#445566', '#445566'], 'the flat color of 340 named for itself is its own');
    equal(Tiles.css(500, 340, ct), '#112233', 'the flat color of 340 named jacks4 is the color of jacks4 (339)');
    equal(Tiles.css(500, 282, ct), '#778899', 'and 282 named fdfrl2 shows the color of 281');
    equal(Tiles.css(485, EMPTY2, kt), Tiles.css(485, EMPTY2, null), 'a ground cell named for itself has its own color');
    equal(Tiles.css(485, EMPTY2, bt2), Tiles.BACK, 'a ground cell whose entry is "." is the black that the game leaves');
    equal(Tiles.css(485, 340, bt2), '#445566', 'an object over it keeps its color');
    equal(Tiles.css(485, 3000, kt), Tiles.css(485, EMPTY2, kt), 'an object beyond the dictionary is not drawn: the ground shows');
    const pk = Tiles.packedTables(ct), pn = Tiles.packedTables(null), pb = Tiles.packedTables(bt2);
    check(pk.objects[340] === Tiles.hexToPacked('#112233') && pn.objects[340] === Tiles.hexToPacked('#445566'), 'the packed colors follow the names too');
    const kept = Tiles.tableOf(fake(shifted, 1, 2));
    Tiles.packedTables(kept);
    Tiles.setAverages('000000'.repeat(239), { 339: 0x010203, 340: 0x445566, 281: 0x778899, 282: 0xaabbcc });          // (the pictures' colors change: a table made before is made again)
    check(Tiles.packedTables(Tiles.tableOf(fake(shifted, 1, 2))).objects[340] === Tiles.hexToPacked('#010203'), 'new average colors reach a table that was made before');
    check(pb.ground[485] === Tiles.hexToPacked(Tiles.BACK) && pb.ground[486] === pn.ground[486] && pk.objects[3000] === 0, 'and the black, the ground and the empty places');
}

// the canvas on a fake page: a frame kept from before is out of date when a name of the dictionary changes (the flat colors are painted cell by cell after a stroke)
function caseD() {
    Tiles.setAnimations(INDEX.names);
    const ctx = new Proxy({}, { get: (t, k) => (k in t ? t[k] : (k === 'createImageData' ? (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }) : () => undefined)), set: (t, k, v) => { t[k] = v; return true; } });
    const node = () => ({ width: 0, height: 0, attrs: {}, hidden: true, textContent: '', disabled: false, addEventListener() {}, getContext: () => ctx, getBoundingClientRect: () => ({ left: 0, top: 0, width: 600, height: 400 }),
        setAttribute(k, v) { this.attrs[k] = String(v); }, removeAttribute(k) { delete this.attrs[k]; }, getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }, setPointerCapture() {}, focus() {}, classList: { toggle() {} } });
    const doc = { createElement: node, addEventListener() {}, visibilityState: 'visible' }, win = { devicePixelRatio: 1, performance: { now: () => 0 }, addEventListener() {} }, canvas = node();
    const api = load('mapmaker_canvas.js').create({ doc, win, canvas, minimap: node(), mini: node(), miniCap: node(), where: node(), zoomIn: node(), zoomOut: node() });
    const f = File.parse(new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', 'TREASURE.LVL'))));
    api.setFile(f);
    const kind = () => canvas.attrs['data-paint'].split(':')[0], scale = api.state().view.scale;
    check(scale >= 5 && scale < 10, 'the whole of Treasure is drawn in flat colors, a rectangle each (' + scale.toFixed(2) + ' pixels a cell)');
    api.markDirty([30 * f.columns + 30]);
    check(kind() === 'cells', 'a stroke paints the cells that changed (' + canvas.attrs['data-paint'] + ')');
    setName(f, 485, '.');
    api.markDirty([30 * f.columns + 30]);
    check(kind() === 'full', 'a name written in the dictionary at the same time makes it a whole frame: other cells have that tile (' + canvas.attrs['data-paint'] + ')');
    api.markDirty([30 * f.columns + 30]);
    check(kind() === 'cells', 'and the next stroke is by cells again');
    // a dictionary of 65,536 names (the reader takes that many; the game, 1,344): the table of the dictionary is asked for once for all the cells of a frame, not once for each (3,000 cells at most)
    const raw = new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', 'TREASURE.LVL'))), names0 = (raw[40] | (raw[41] << 8)) + 1, old = 2 + 11 * names0;
    const dict = new Uint8Array(2 + 11 * 65536);
    dict.set(raw.subarray(40 + 2, 40 + old), 2); dict[0] = 255; dict[1] = 255;
    for (let i = names0; i < 65536; i++) dict[2 + 11 * i] = 0x2E;
    const bytes = new Uint8Array(40 + dict.length + raw.length - 40 - old);
    bytes.set(raw.subarray(0, 40), 0); bytes.set(dict, 40); bytes.set(raw.subarray(40 + old), 40 + dict.length);
    const big = File.parse(bytes);
    check(big.ok && big.names === 65536 && big.rows === f.rows, 'a map with 65,536 names is read');
    api.setFile(big);
    const real = Tiles.tableOf, many = []; let asked = 0;
    for (let i = 0; i < 2000; i++) many.push(i);
    Tiles.tableOf = function (file) { asked++; return real(file); };
    try {
        api.markDirty(many);
        check(kind() === 'cells' && asked <= 4, 'a stroke of 2,000 cells in rectangles asks for the table ' + asked + ' times (at most 4)');
        api.state().view.scale = 3; api.markDirty([0]);
        asked = 0; api.markDirty(many);
        check(kind() === 'cells' && asked <= 4, 'and in pixels ' + asked + ' times (at most 4)');
    } finally { Tiles.tableOf = real; }
}

function caseB() {
    check(Array.isArray(INDEX.names) && INDEX.names.length === 1344, 'the atlas has the 1,344 animation names');
    equal(INDEX.names, DOCS, 'the atlas\'s names are the docs\' animation table, in the order of the numbers');
    // every animation of one part has a picture that the page can find (the sheet, the ground sheet or fetched alone); the ones of several parts that are not in the sheet are menu screens and pieces of ants
    let missing = 0, parts = {};
    const PARTS = JSON.parse(fs.readFileSync(path.join(ROOT, 'docs', 'chd_table4_animations.json'), 'utf8'));
    for (let id = 0; id < 1344; id++) {
        const frames = PARTS[id].subitems.length ? PARTS[id].subitems[0].frames : [];
        const there = INDEX.objects.items[id] || INDEX.far[id] || (id >= INDEX.ground.first && id < INDEX.ground.first + INDEX.ground.count);
        if (frames.length === 1 && !there) missing++;
        if (frames.length > 1 && !there) parts[id] = frames.length;
    }
    equal(missing, 0, 'every animation of one part has a picture in the atlas (the sheet, the ground or fetched alone)');
    check(Object.keys(parts).length < 300, 'only animations of several parts are left out (menu screens, pieces of ants): ' + Object.keys(parts).length);
    for (const id of [339, 340, 281, 282, 628, 644, 335, 346]) check(!!(INDEX.objects.items[id] || INDEX.far[id] || (id >= 431 && id <= 669)), 'tile ' + id + ' (' + DOCS[id] + ') has a picture: a map can name it');
}

// ---- the oracle: the renderer's rule, read again from src/ants_app/renderer.cpp and written here with its own reader of the files ----
function lower(s) { return s.replace(/[A-Z]/g, (c) => String.fromCharCode(c.charCodeAt(0) + 32)); }
function readMap(buf) {
    const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength), version = dv.getUint32(0, true);
    let at = version === 8 ? 40 : 18 + dv.getUint16(16, true);
    const count = dv.getUint16(at, true) + 1, dictionary = [];
    at += 2;
    for (let i = 0; i < count; i++, at += 11) { let n = ''; for (let k = 0; k < 11 && buf[at + k] !== 0; k++) n += String.fromCharCode(buf[at + k]); dictionary.push(n); }
    let rows = dv.getUint32(at, true), columns = dv.getUint32(at + 4, true);
    if (version === 8) { rows &= 0xFFFF; columns &= 0xFFFF; }
    at += 8;
    const cells = rows * columns, one = [], two = [], flag = [];
    for (let i = 0; i < cells; i++) one.push(dv.getUint16(at + 6 * i, true));
    for (let i = 0; i < cells; i++) { two.push(dv.getUint16(at + 6 * cells + 6 * i, true)); flag.push(dv.getUint16(at + 6 * cells + 6 * i + 2, true)); }
    // block 1 (a count and 6 bytes a record), then block 2 (a count, then per pile 10 bytes and 4 a stage)
    let p = at + 12 * cells, piles = [];
    const n1 = p + 2 <= buf.length ? dv.getUint16(p, true) : 0;
    p += 2 + 6 * n1;
    if (p + 2 <= buf.length) {
        const n2 = dv.getUint16(p, true);
        p += 2;
        for (let i = 0; i < n2; i++) {
            if (p + 10 > buf.length) break;
            const stages = dv.getUint16(p + 8, true);
            if (stages === 0 || p + 10 + 4 * stages > buf.length) break;
            const list = [];
            for (let s = 0; s < stages; s++) list.push([dv.getUint16(p + 10 + 4 * s, true), dv.getUint16(p + 12 + 4 * s, true)]);
            piles.push({ row: dv.getUint16(p, true), column: dv.getUint16(p + 2, true), units: dv.getUint16(p + 4, true), stages: list });
            p += 10 + 4 * stages;
        }
    }
    return { rows, columns, dictionary, one, two, flag, piles };
}

// Renderer::set_level: the animation of dictionary entry i (-1: none)
function tileAnim(dictionary, i) {
    const name = dictionary[i];
    if (name === '' || name === '.') return -1;
    if (i >= 152 && i <= 155) return -1;                                           // (the editor's start markers are never drawn)
    if (i < DOCS.length && lower(DOCS[i]) === lower(name)) return i;
    let found = -1;
    for (let j = 0; j < DOCS.length; j++) if (DOCS[j] === name) found = j;         // (the game's table: the last of equal names)
    if (found < 0) for (let j = 0; j < DOCS.length; j++) if (lower(DOCS[j]) === lower(name)) { found = j; break; }      // (the lower-case table: the first)
    return found;
}
const GAME_DRAWS_BY_ID = (id) => (id >= 62 && id <= 66) || (id >= 129 && id <= 132) || id === 134 || (id >= 245 && id <= 248) || (id >= 34 && id <= 38);

// the animation of an anchored object of layer 2 (Renderer::set_level's loop over layer 2), a pile's stage (Renderer::food_stage_anim) and a ground cell (Renderer::draw_terrain)
function oracleObject(m, id) {
    if (GAME_DRAWS_BY_ID(id)) return id;
    if (id >= 152 && id <= 155) return -1;
    if (id >= m.dictionary.length) return -1;
    const name = m.dictionary[id];
    if (name === '' || name === '.' || lower(name).indexOf('pu') === 0) return -1;
    return tileAnim(m.dictionary, id);
}
function oracleStage(m, id) { const a = id < m.dictionary.length ? tileAnim(m.dictionary, id) : -1; return a >= 0 ? a : (id < DOCS.length ? id : -1); }
function oracleGround(m, id) { return id < m.dictionary.length ? tileAnim(m.dictionary, id) : -1; }
// what the editor shows of a pile: the last stage that holds the pile's units, on an anchor whose layer-2 cell is empty
function oraclePileTiles(m) {
    const at = {};
    for (const pile of m.piles) for (const [threshold, tile] of pile.stages) if (pile.units <= threshold) at[pile.row * m.columns + pile.column] = tile;
    return at;
}

function libraryFiles() {
    const found = [];
    for (const dir of ['Original-Ants/Maps', 'Community-Maps']) for (const name of fs.readdirSync(path.join(ROOT, dir)).sort()) if (/\.lvl$/i.test(name)) found.push(path.join(ROOT, dir, name));
    return found;
}

function caseC() {
    Tiles.setAnimations(INDEX.names);
    const files = libraryFiles(), tally = { cells: 0, ground: 0, objects: 0, piles: 0 }, bad = { ground: 0, objects: 0, piles: 0 }, was = { ground: 0, objects: 0, piles: 0 }, nothing = { ground: 0, objects: 0, piles: 0 }, other = { ground: 0, objects: 0, piles: 0 }, badMaps = new Set(), wasMaps = new Set(), otherMaps = new Set(), nothingMaps = new Set();
    let eat = null;
    check(files.length === 540, 'the library has 540 maps (' + files.length + ')');
    for (const file of files) {
        const buf = new Uint8Array(fs.readFileSync(file)), m = readMap(buf), f = File.parse(buf), name = path.basename(file);
        if (!f.ok) { check(false, name + ' is read'); continue; }
        const tab = Tiles.tableOf(f), food = Sprites.foodTiles(f), pileAt = oraclePileTiles(m);
        check(!!tab && tab.n === m.dictionary.length, name + ': a table of its ' + m.dictionary.length + ' names');
        if (!tab) continue;
        let wrong = 0, before = 0;
        for (let r = 0; r < m.rows; r++) {
            for (let c = 0; c < m.columns; c++) {
                const i = r * m.columns + c;
                // layer 1
                const og = oracleGround(m, m.one[i]), eg = Sprites.groundAnim(f, tab, r, c), bg = Sprites.groundAnim(f, null, r, c);
                tally.cells++; tally.ground++;
                if (eg !== og) { bad.ground++; wrong++; }
                if (bg !== og) { was.ground++; before++; (og < 0 ? nothing : other).ground++; (og < 0 ? nothingMaps : otherMaps).add(name); }
                // layer 2: an anchored object, or a pile's stage on an empty anchor
                let oo = -1, isPile = false;
                if (m.two[i] === EMPTY2) { if (pileAt[i]) { oo = oracleStage(m, pileAt[i]); isPile = true; } }
                else if (m.flag[i] & 1) oo = oracleObject(m, m.two[i]);
                const eo = Sprites.objectAnim(f, tab, food, r, c), bo = Sprites.objectAnim(f, null, food, r, c);
                if (m.two[i] !== EMPTY2 && (m.flag[i] & 1) || isPile) tally[isPile ? 'piles' : 'objects']++;
                if (eo !== oo) { bad[isPile ? 'piles' : 'objects']++; wrong++; }
                if (bo !== oo) { was[isPile ? 'piles' : 'objects']++; before++; (oo < 0 ? nothing : other)[isPile ? 'piles' : 'objects']++; (oo < 0 ? nothingMaps : otherMaps).add(name); }
            }
        }
        if (wrong) badMaps.add(name);
        if (before) wasMaps.add(name);
        if (/^EatClanMap/i.test(name)) eat = { f, tab, m };
    }
    equal(bad, { ground: 0, objects: 0, piles: 0 }, 'cells where the editor\'s picture is not the game\'s, over all 540 maps (' + badMaps.size + ' maps)');
    console.log('    names: ' + tally.cells + ' cells in 540 maps; ' + tally.objects + ' anchored objects and ' + tally.piles + ' pile stages. Where the editor and the game differ: by number before ' + (was.ground + was.objects + was.piles) + ' cells in ' + wasMaps.size + ' maps (' + was.ground + ' ground, ' + was.objects + ' objects, ' + was.piles + ' pile stages); by the dictionary now ' + (bad.ground + bad.objects + bad.piles) + ' cells in ' + badMaps.size + ' maps');
    console.log('    names: of the cells that differed by number, the game draws ANOTHER picture in ' + (other.ground + other.objects + other.piles) + ' cells of ' + otherMaps.size + ' maps (' + other.ground + ' ground, ' + other.objects + ' objects, ' + other.piles + ' pile stages) and NOTHING in ' + (nothing.ground + nothing.objects + nothing.piles) + ' cells of ' + nothingMaps.size + ' maps (' + nothing.ground + ' ground, ' + nothing.objects + ' objects, ' + nothing.piles + ' pile stages: the entry is ".")');
    check(was.ground > 1000 && was.objects > 500 && was.piles > 0 && wasMaps.size > 100, 'the pictures by number were wrong in many cells: the check can see the difference (' + (was.ground + was.objects + was.piles) + ' cells in ' + wasMaps.size + ' maps)');
    // EatClanMap: the owner's example: jacks4 and the purple ring
    check(!!eat, 'EatClanMap is in the library');
    if (eat) {
        const { f, tab } = eat;
        equal(DOCS[339], 'jacks4', 'animation 339 is jacks4');
        equal(DOCS[340], 'fdpezp1', 'animation 340 is the pink eraser');
        equal([Tiles.stage(tab, 340), Tiles.object(tab, 340)], [339, 339], 'EatClanMap: stage tile 340 is named jacks4 in its dictionary, so the game (and now the editor) draws animation 339');
        equal(Tiles.stage(tab, 282), 281, 'EatClanMap: tile 282 is named fdfrl2: animation 281, not the table\'s own 282');
        // the cell of the owner's picture: row 19, column 15
        const food = Sprites.foodTiles(f);
        // the window of the owner's two pictures (rows 13 to 20, columns 11 to 19): a pile of jacks at (13, 11) and (13, 13), a ring at (13, 14)
        for (const [row, column, now, was] of [[13, 11, 339, 340], [13, 13, 339, 340], [13, 14, 281, 282]]) {
            equal([Sprites.objectAnim(f, tab, food, row, column), Sprites.objectAnim(f, null, food, row, column)], [now, was], 'EatClanMap row ' + row + ', column ' + column + ': drawn as ' + DOCS[now] + ' (' + now + '), not ' + DOCS[was] + ' (' + was + ')');
        }
    }
}

(function main() {
    for (const [name, fn] of [['A the rule on made-up dictionaries', caseA], ['B the atlas names and pictures', caseB], ['C a frame that is kept', caseD], ['D the library against an oracle of the renderer', caseC]]) {
        const before = failures;
        console.log('RUNNING: ' + name + ' ...');
        try { fn(); } catch (e) { failures++; console.log('    CRASHED: ' + (e && e.stack || e)); }
        console.log('RUNNING: ' + name + ' ... ' + (failures === before ? 'PASS' : 'FAIL'));
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
})();
