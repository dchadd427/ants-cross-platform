#!/usr/bin/env node
// The map maker's code that needs no browser and no map core (web/front/mapmaker_*.js; docs/MAP_EDITOR.md), run on the real files of the library and on small made-up ones:
//
//   node tests/scripts/web_mapmaker_check.js [--front DIR] [--list-maps]
//
//   --front DIR  the folder of the mapmaker_*.js files to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
//   A  the file reader: every map of Original-Ants/Maps and Community-Maps is read (rows, columns, the tile ids of a few cells and a digest of all of them, which
//      tests/scripts/test_web_mapmaker.py computes again in Python), a version 9 file made here is read, and a file that is cut, wrong or empty is refused
//   B  the view: a zoom keeps the point under the pointer, a view never leaves the map, a cell and its place on the screen give each other, the first view of a small and of a
//      big map, and that a frame of a 512 x 512 map draws only the tiles that are on the screen (one rectangle each), or a picture of the canvas when the tiles are small
//   C  the sizes: the limits as the cards say them, the live line of three sizes, the refusals of the core turned into their sentences
//   D  the checks card: the headline, marks and links, from made-up reports; a sentence that holds markup stays text; no link where a finding has no place
//   E  the colors of the tiles: the kinds of ground and object that the page shows, and the packed form that the pixel loop uses
// Exit status 0 when everything passes, 1 otherwise. (The page and the real module together: web_mapmaker_core_check.js and the opt-in browser check.)
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Tiles = load('mapmaker_tiles.js'), View = load('mapmaker_view.js'), Sizes = load('mapmaker_sizes.js'), Checks = load('mapmaker_checks.js');

let failures = 0, checks = 0;
function check(ok, what) {
    checks++;
    if (!ok) { failures++; console.log('    FAILED: ' + what); }
}
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }
function near(a, b, eps, what) { check(Math.abs(a - b) <= eps, what + ': ' + a + ' is not ' + b); }

function libraryFiles() {
    const found = [];
    for (const dir of ['Original-Ants/Maps', 'Community-Maps']) {
        for (const name of fs.readdirSync(path.join(ROOT, dir)).sort()) if (/\.lvl$/i.test(name)) found.push(path.join(ROOT, dir, name));
    }
    return found;
}

// The digest of what the reader sees of a map: every tile of both layers in order, as little-endian words (the same in Python)
function digestOf(map) {
    const words = new Uint16Array(map.rows * map.columns * 2);
    let i = 0;
    for (let r = 0; r < map.rows; r++) for (let c = 0; c < map.columns; c++) { words[i++] = map.tile1(r, c); words[i++] = map.tile2(r, c); }
    return crypto.createHash('sha256').update(Buffer.from(words.buffer)).digest('hex');
}

// A small version 9 file: 5 rows x 7 columns, a 3 byte description, 2 names, tile (r, c) of layer 1 is 431 + r * 7 + c, layer 2 holds 0x7FFE except cell (2, 3) that holds 246
function makeVersion9() {
    const rows = 5, columns = 7, description = 'abc', names = 2;
    const parts = [];
    const dword = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v); parts.push(b); };
    const word = (v) => { const b = Buffer.alloc(2); b.writeUInt16LE(v); parts.push(b); };
    dword(9); dword(1); word(10); dword(0); word(0); word(description.length); parts.push(Buffer.from(description));
    word(names - 1); for (let i = 0; i < names; i++) parts.push(Buffer.from('name' + i + '\0\0\0\0\0\0\0'.slice(0, 11 - 5)));
    dword(rows); dword(columns);
    for (let i = 0; i < rows * columns; i++) { word(431 + i); word(0); word(0xCDCD); }
    for (let i = 0; i < rows * columns; i++) { word(i === 2 * columns + 3 ? 246 : 0x7FFE); word(0); word(0); }
    word(1); word(153); word(1); word(2);                      // block 1: one blue start marker at row 1, column 2
    word(1); word(4); word(5); word(3); word(2); word(1); word(0x1234); word(1);    // block 2: one pile at row 4, column 5 with 1 stage
    word(0); word(0); word(0); word(6);
    return new Uint8Array(Buffer.concat(parts));
}

function caseA() {
    const files = libraryFiles();
    check(files.length === 540, 'the library has 540 maps (' + files.length + ')');
    let classic = 0;
    for (const f of files) {
        const bytes = new Uint8Array(fs.readFileSync(f));
        const m = File.parse(bytes);
        check(m.ok && m.kind === 'classic' && m.version === 8 && m.rows >= 1 && m.columns >= 1, path.basename(f) + ' is read as a Classic map');
        if (m.ok) {
            classic++;
            check(m.layer2 + 6 * m.cells <= bytes.length, path.basename(f) + ': both layers lie inside the file');
        }
    }
    check(classic === 540, 'all 540 are read');
    const v9 = File.parse(makeVersion9());
    check(v9.ok && v9.kind === 'extended' && v9.version === 9 && v9.rows === 5 && v9.columns === 7, 'a version 9 file is read: ' + JSON.stringify([v9.ok, v9.kind, v9.rows, v9.columns, v9.error]));
    if (v9.ok) {
        equal([v9.tile1(0, 0), v9.tile1(4, 6), v9.tile1(2, 3)], [431, 431 + 34, 431 + 17], 'version 9 layer 1');
        equal([v9.tile2(2, 3), v9.tile2(0, 0)], [246, 0x7FFE], 'version 9 layer 2');
        equal([v9.tile1(-1, 0), v9.tile1(0, 7), v9.tile2(5, 0)], [485, 485, 0x7FFE], 'a cell outside the grid is plain ground, no object');
        equal(v9.markers, [{ team: 'blue', row: 1, column: 2 }], 'version 9 block 1');
        equal(v9.food, [{ row: 4, column: 5 }], 'version 9 block 2');
    }
    const good = makeVersion9();
    for (const [what, bytes] of [['an empty file', new Uint8Array(0)], ['one byte', new Uint8Array(1)], ['a cut file', good.subarray(0, good.length - 120)], ['a file cut in the header', good.subarray(0, 30)],
        ['another version', Uint8Array.from(good, (b, i) => (i === 0 ? 7 : b))], ['a text', new Uint8Array(Buffer.from('this is not a map file at all, only some words that fill a hundred bytes or so, to be long enough'))],
        ['not bytes', 'text']]) {
        check(File.parse(bytes).ok === false, what + ' is refused');
    }
    // the original keeps the lower 16 bits of each grid number: a Classic file whose upper bits hold something is read as the original reads it
    const tiny = new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', 'TINY.LVL')));
    const at = 42 + 11 * ((tiny[40] | (tiny[41] << 8)) + 1), noisy = Uint8Array.from(tiny);
    new DataView(noisy.buffer).setUint32(at, 31 | 0xABCD0000, true); new DataView(noisy.buffer).setUint32(at + 4, 31 | 0x12340000, true);
    const seen = File.parse(noisy);
    check(seen.ok && seen.rows === 31 && seen.columns === 31, 'the upper 16 bits of the grid numbers of a Classic file are not read');
    // a grid that is far too large is refused before anything is read from it
    const huge = Uint8Array.from(good);
    new DataView(huge.buffer).setUint32(18 + 3 + 2 + 22, 100000, true);
    check(File.parse(huge).ok === false, 'a header that announces 100,000 rows is refused');
    equal([File.downloadName('CON'), File.downloadName('nul.txt'), File.downloadName('COM3'), File.downloadName('Console')], ['_CON.lvl', '_nul.txt.lvl', '_COM3.lvl', 'Console.lvl'], 'names that Windows keeps for devices');
    const cut = File.downloadName('\u{1F41C}'.repeat(60));
    check(cut === '\u{1F41C}'.repeat(40) + '.lvl', 'a long name is cut between characters, not inside one');
    equal([File.downloadName('Sunny Picnic'), File.downloadName('a/b\\c:d*e?f"g<h>i|j'), File.downloadName('  ..  '), File.downloadName(''), File.downloadName('x'.repeat(100)).length], ['Sunny Picnic.lvl', 'a_b_c_d_e_f_g_h_i_j.lvl', 'map.lvl', 'map.lvl', 44], 'the name of a download');
}

function caseB() {
    const f = { rows: 300, columns: 400, cells: 120000 };
    const w = 760, h = 640;
    let v = View.create(f, w, h);
    check(v.scale >= 22 && View.fitScale(f, w, h) < 22, 'a big map starts as a stretch of 22 pixels a tile');
    equal([v.cx, v.cy], [200, 150], 'and starts at its middle');
    const small = { rows: 31, columns: 31, cells: 961 };
    v = View.create(small, w, h);
    check(View.isWhole(v, small, w, h), 'a small map starts whole');
    near(v.scale, (h - 8) / 31, 0.001, 'at the size that fits');
    near(View.create({ rows: 10, columns: 10, cells: 100 }, w, h).scale, 48, 0.001, 'and at most 48 pixels a tile');
    v = View.create(small, 360, 500);
    near(v.scale, (360 - 8) / 31, 0.001, 'a small map on a phone is fitted to the width');
    // zoom keeps the point under the pointer
    v = View.create(f, w, h);
    for (const [x, y, factor] of [[100, 100, 1.5], [700, 600, 0.6], [380, 320, 2], [5, 5, 3]]) {
        const before = View.cellAt(v, f, w, h, x, y);
        const pt = { c: (x - w / 2) / v.scale + v.cx, r: (y - h / 2) / v.scale + v.cy };
        View.zoomAt(v, f, w, h, factor, x, y);
        const after = { c: (x - w / 2) / v.scale + v.cx, r: (y - h / 2) / v.scale + v.cy };
        check(before !== null, 'a point of the canvas is on the map');
        const atEdge = v.cx - w / (2 * v.scale) <= 1e-9 || v.cy - h / (2 * v.scale) <= 1e-9 || v.cx + w / (2 * v.scale) >= f.columns - 1e-9 || v.cy + h / (2 * v.scale) >= f.rows - 1e-9;
        check(atEdge || (Math.abs(pt.c - after.c) < 1e-6 && Math.abs(pt.r - after.r) < 1e-6), 'a zoom by ' + factor + ' at (' + x + ', ' + y + ') keeps the map point under it');
    }
    // never further out than the whole map, never closer than 48 pixels a tile, never off the map
    for (let i = 0; i < 40; i++) View.zoomAt(v, f, w, h, 0.5, 300, 300);
    near(v.scale, View.minScale(f, w, h), 1e-9, 'zooming out stops at the whole map');
    check(View.isWhole(v, f, w, h), 'the whole map is on the screen then');
    for (let i = 0; i < 40; i++) View.zoomAt(v, f, w, h, 2, 300, 300);
    near(v.scale, 48, 1e-9, 'zooming in stops at 48 pixels a tile');
    for (const [dx, dy] of [[100000, 100000], [-100000, -100000], [-3, 500]]) {
        View.panBy(v, f, w, h, dx, dy);
        const hw = w / (2 * v.scale), hh = h / (2 * v.scale);
        check(v.cx - hw >= -1e-9 && v.cx + hw <= f.columns + 1e-9 && v.cy - hh >= -1e-9 && v.cy + hh <= f.rows + 1e-9, 'a pan by (' + dx + ', ' + dy + ') stays on the map');
    }
    // a cell and its place
    v = View.create(f, w, h);
    for (const [r, c] of [[150, 200], [140, 190], [160, 205]]) {
        const p = View.toScreen(v, w, h, r, c);
        equal(View.cellAt(v, f, w, h, p.x + 1, p.y + 1), { row: r, column: c }, 'cell (' + r + ', ' + c + ') is found where it is drawn');
    }
    equal(View.cellAt(View.create(small, 360, 500), small, 360, 500, 2, 2), null, 'a point outside the map is no cell');
    // the visible tiles
    v = View.create(f, w, h);
    const vis = View.visible(v, f, w, h);
    check(vis && vis.c1 - vis.c0 + 1 <= Math.ceil(w / v.scale) + 1 && vis.r1 - vis.r0 + 1 <= Math.ceil(h / v.scale) + 1, 'only about ' + Math.ceil(w / v.scale) + ' x ' + Math.ceil(h / v.scale) + ' tiles are visible: ' + JSON.stringify(vis));
    // reveal and centerOn
    View.centerOn(v, f, w, h, 10, 20, 30);
    check(v.scale >= 30 && Math.abs(v.cx - Math.max(20.5, w / (2 * v.scale))) < 1e-6, 'a cell can be brought to the middle at a size');
    // drawing: a 512 x 512 map at 24 pixels a tile draws the tiles on the screen and nothing else
    const big = bigMap(512, 512);
    v = View.create(big, w, h);
    check(v.scale >= 22, 'the 512 x 512 map starts zoomed in');
    const log = { rects: 0, fills: 0 };
    const ctx = fakeContext(log);
    big.reads.n = 0;
    const done = View.draw(ctx, v, big, w, h, { cursor: { row: 256, column: 256 }, scratch: fakeScratch() });
    const maxTiles = (Math.ceil(w / v.scale) + 1) * (Math.ceil(h / v.scale) + 1);
    check(done.rects > 0 && done.rects <= maxTiles && done.pixels === 0, 'a frame of the 512 x 512 map draws ' + done.rects + ' tiles (at most ' + maxTiles + ' are on the screen)');
    check(big.reads.n <= 2 * maxTiles + 8, 'and reads only those tiles (' + big.reads.n + ' reads)');
    // and when the whole map is on the screen, the tiles are under 2 pixels: the canvas is filled as a picture
    const fit = View.create(big, w, h);
    fit.scale = 0; View.clamp(fit, big, w, h);
    const log2 = { rects: 0, fills: 0 }, scratch = fakeScratch();
    const done2 = View.draw(fakeContext(log2), fit, big, w, h, { scratch: scratch });
    check(done2.rects === 0 && done2.pixels >= w * h * 0.9 && log2.fills <= 1, 'the whole 512 x 512 map is drawn as a picture of the canvas (' + done2.pixels + ' pixels, ' + log2.fills + ' fills)');
    const pixels = scratch.image.data32, at = (x, y) => pixels[y * scratch.image.width + x];
    const expect = Tiles.hexToPacked(Tiles.css(big.tile1(256, 256), big.tile2(256, 256)));
    const mid = View.toScreen(fit, w, h, 256, 256);
    check(at(Math.floor(mid.x + fit.scale / 2), Math.floor(mid.y + fit.scale / 2)) === expect, 'a pixel of the picture has the color of the tile under it');
    check(at(0, 0) === Tiles.hexToPacked('#0b1612'), 'the margin round the map is the background');
    // the small map
    const size = View.miniSize(big, 168, 168);
    equal(size, { w: 168, h: 168 }, 'the small map of a square map');
    equal(View.miniSize({ rows: 300, columns: 400 }, 168, 168), { w: 168, h: 126 }, 'the small map of a 300 x 400 map');
    equal(View.miniSize({ rows: 31, columns: 31 }, 168, 168), { w: 31, h: 31 }, 'the small map is never bigger than a pixel a tile');
    v = View.create(f, w, h);
    const frame = View.miniFrame(v, f, w, h, 168, 126);
    near(frame.w, (w / v.scale) * 168 / 400, 1e-9, 'the frame of the visible part on the small map is as wide as the visible part');
    equal(View.miniPoint(f, 168, 126, 84, 63), { row: 150, column: 200 }, 'a click in the middle of the small map is the middle of the map');
    equal(View.miniPoint(f, 168, 126, -50, 9000), { row: 299, column: 0 }, 'a click outside the small map is the nearest cell');
}

// a map with the reader's interface and no file: 512 x 512, tile ids from the cell's place
function bigMap(rows, columns) {
    const reads = { n: 0 };
    const bytes = new Uint8Array(rows * columns * 12);
    for (let i = 0; i < rows * columns; i++) {
        const o = i * 12;
        const t1 = [485, 486, 476, 487, 571][i % 5], t2 = i % 11 === 0 ? 289 : 0x7FFE;
        bytes[o] = t1 & 255; bytes[o + 1] = t1 >> 8;
        bytes[rows * columns * 6 + o / 2] = t2 & 255; bytes[rows * columns * 6 + o / 2 + 1] = t2 >> 8;
    }
    const layer2 = rows * columns * 6;
    return {
        rows, columns, cells: rows * columns, bytes, layer1: 0, layer2, markers: [], food: [],
        tile1: (r, c) => { reads.n++; const o = 6 * (r * columns + c); return bytes[o] | (bytes[o + 1] << 8); },
        tile2: (r, c) => { reads.n++; const o = layer2 + 6 * (r * columns + c); return bytes[o] | (bytes[o + 1] << 8); }, reads
    };
}
function fakeContext(log) {
    const noop = () => {};
    return { fillRect: () => { log.fills++; log.rects++; }, strokeRect: noop, beginPath: noop, moveTo: noop, lineTo: noop, stroke: noop, arc: noop, fill: noop, drawImage: noop, setTransform: noop, fillStyle: '', strokeStyle: '', lineWidth: 1 };
}
function fakeScratch() {
    const s = { width: 0, height: 0, image: null };
    s.getContext = () => ({
        createImageData: (w, h) => { const data = new Uint8ClampedArray(w * h * 4); const image = { width: w, height: h, data, data32: new Uint32Array(data.buffer) }; s.image = image; return image; },
        putImageData: () => {}
    });
    return s;
}

function caseC() {
    const limits = Sizes.limitsOf({ classic: { rows: 90, columns: 130, cells: 10000 } }, { classic: { rows: 256, columns: 256, cells: 65536 }, extended: { rows: 1024, columns: 1024, cells: 262144 } });
    equal(limits, { classic: { rows: 90, columns: 130, cells: 10000, openCells: 65536 }, extended: { side: 512, cells: 262144, longest: 1024 } }, 'the limits as the cards need them');
    const words = (kind) => Sizes.limitLine(kind, limits).map((p) => p[0]).join('');
    const other = Sizes.limitsOf({ classic: { rows: 80, columns: 120, cells: 9000 } }, { classic: { rows: 256, columns: 256, cells: 65536 }, extended: { rows: 2048, columns: 1500, cells: 1000000 } });
    equal(other, { classic: { rows: 80, columns: 120, cells: 9000, openCells: 65536 }, extended: { side: 1000, cells: 1000000, longest: 2048 } }, 'other limits are taken as they are (nothing is kept by hand)');
    equal(Sizes.limitLine('classic', other).map((p) => p[0]).join(''), 'Up to 80 rows and 120 columns, 9,000 cells in all.', 'and the Classic card says them');
    equal(Sizes.limitLine('extended', other).map((p) => p[0]).join(''), 'Up to 1000 x 1000 cells (1,000,000 in all). The longest side can be 2,048.', 'and the Extended card');
    equal(words('classic'), 'Up to 90 rows and 130 columns, 10,000 cells in all.', 'the Classic card');
    equal(words('extended'), 'Up to 512 x 512 cells (262,144 in all). The longest side can be 1,024.', 'the Extended card');
    equal(Sizes.limitLine('classic', limits).filter((p) => p[1]).map((p) => p[0]), ['90 rows', '130 columns'], 'bold in the Classic card');
    equal(Sizes.limitLine('extended', limits).filter((p) => p[1]).map((p) => p[0]), ['512 x 512'], 'bold in the Extended card');
    const line = (kind, r, c) => { const l = Sizes.liveLine(kind, r, c, limits); return l && [Sizes.number(l.cells), Sizes.number(l.max), l.text]; };
    equal(line('classic', '31', '31'), ['961', '10,000', 'about 17 KB'], '31 x 31 Classic');
    equal(line('extended', '300', '400'), ['120,000', '262,144', 'about 1.4 MB'], '300 x 400 Extended');
    equal(line('classic', '100', '80'), ['8,000', '10,000', 'about 99 KB'], '100 x 80 Classic');
    equal(line('classic', '', '80'), null, 'an empty box');
    equal(line('classic', '0', '80'), null, 'zero');
    equal(line('classic', '1e3', '80'), null, 'not a whole number');
    equal(line('classic', ' 12 ', '80')[0], '960', 'spaces round a number');
    equal(line('classic', '-5', '5'), null, 'a minus sign');
    equal(Sizes.number(1234567), '1,234,567', 'numbers get commas');
    equal(Sizes.cellsWord(1), '1 cell', 'one cell'); equal(Sizes.cellsWord(961), '961 cells', 'many cells');
    equal(Sizes.sizeText(5e12), 'over 1 GB', 'a huge file size does not become a huge number of megabytes');
    // the side panel: a legal opened Classic map of 16,000 cells is not "16,000 of 10,000"
    equal(Sizes.mapFacts('classic', 961, 16940, limits), { max: 10000, percent: 10, caption: '961 of 10,000 cells', file: 'about 17 KB' }, 'facts of a small Classic map');
    equal(Sizes.mapFacts('classic', 16000, 200000, limits), { max: 65536, percent: 24, caption: '16,000 of 65,536 cells', file: 'about 195 KB' }, 'facts of an opened Classic map of 16,000 cells (80 x 200)');
    equal(Sizes.mapFacts('classic', 65536, 900000, limits).percent, 100, 'a full Classic map');
    equal(Sizes.mapFacts('extended', 120000, 1445386, limits), { max: 262144, percent: 46, caption: '120,000 of 262,144 cells', file: 'about 1.4 MB' }, 'facts of an Extended map');
    equal([Sizes.sizeText(1048575), Sizes.sizeText(1048576), Sizes.sizeText(3151114), Sizes.sizeText(0)], ['about 1024 KB', 'about 1.0 MB', 'about 3.0 MB', 'about 0 KB'], 'file sizes');
    equal(Sizes.blankBytes(961), 16940, 'the size of a blank 31 x 31 map is the writer\'s');
    const tall = 'a blank map of 100 rows x 80 columns cannot be written: the grid has 100 rows x 80 columns: a Classic map made now has at most 90 rows (ants cannot enter or cross the 91st row of a taller map, in the original game too); make it an Extended map?';
    equal(Sizes.refusal(tall), { text: 'A Classic map made now has at most 90 rows (ants cannot enter or cross the 91st row of a taller map, in the original game too).', offersExtended: true }, 'the sentence for too many rows');
    equal(Sizes.refusal('a blank map of 90 rows x 131 columns cannot be written: the grid has 90 rows x 131 columns, more than the 90 x 130 allowed here'),
        { text: 'The grid has 90 rows x 131 columns, more than the 90 x 130 allowed here.', offersExtended: false }, 'too many columns');
    equal(Sizes.refusal('a blank map of 90 rows x 120 columns cannot be written: the grid has 90 rows x 120 columns, 10800 cells, more than the 10000 allowed here').text, 'The grid has 90 rows x 120 columns, 10,800 cells, more than the 10,000 allowed here.', 'numbers of a refusal get commas as the live line has them');
    equal(Sizes.refusal('a blank map of 513 rows x 513 columns cannot be written: the grid has 513 rows x 513 columns, 263169 cells, more than the 262144 allowed here').offersExtended, false, 'too many cells for an Extended map offers nothing');
    equal(Sizes.refusal('a blank map needs 1 to 65535 rows and columns').text, 'A blank map needs 1 to 65,535 rows and columns.', 'a sentence with no prefix');
}

// A document that is just enough for the checks card
function fakeDocument() {
    function node(tag) {
        const n = { tag, className: '', children: [], attrs: {}, listeners: {}, own: '', type: '' };
        n.appendChild = (c) => { n.children.push(c); return c; };
        n.setAttribute = (k, v) => { n.attrs[k] = v; };
        n.addEventListener = (t, f) => { n.listeners[t] = f; };
        Object.defineProperty(n, 'textContent', { get: () => n.own + n.children.map((c) => c.textContent).join(''), set: (v) => { n.own = String(v); n.children = []; } });
        return n;
    }
    return { createElement: node, createTextNode: (t) => { const n = node('#text'); n.own = String(t); return n; } };
}
function walk(n, fn) { fn(n); n.children.forEach((c) => walk(c, fn)); }
function texts(n, cls) { const out = []; walk(n, (x) => { if ((' ' + x.className + ' ').includes(' ' + cls + ' ')) out.push(x.textContent); }); return out; }

function caseD() {
    const doc = fakeDocument();
    const works = { kind: 'classic', judged: true, works: true, headline: 'Works in the original game', badge: '', findings: [{ severity: 'note', code: 'object_outside_map', message: 'one plant stands outside the map (row 31, column 12): the original ignores it', row: 31, column: 12 }] };
    const shown = [];
    let card = Checks.render(doc, works, { onShow: (r, c) => shown.push([r, c]) });
    equal(texts(card, 'hl'), ['✓Works in the original game'], 'Classic works: the headline');
    equal(texts(card, 'clean'), [Checks.ALL_GOOD], 'and the line that says nothing is to fix');
    equal(texts(card, 'sev'), ['Good to know'], 'a note is "Good to know"');
    equal(texts(card, 'msg'), ['One plant stands outside the map (row 31, column 12): the original ignores it'], 'the sentence of the checker with a capital');
    equal(texts(card, 'go'), ['Show me: row 31, column 12'], 'the link that shows where');
    let link = null; walk(card, (x) => { if (x.className === 'go') link = x; });
    link.listeners.click(); equal(shown, [[31, 12]], 'and it says where');
    equal(texts(card, 'badge-nv'), [], 'a Classic map has no badge');
    equal(texts(card, 'tag'), ['Classic map'], 'the kind is named');
    const no = {
        kind: 'classic', judged: true, works: false, headline: 'Does not work in the original game', badge: '', findings: [
            { severity: 'fatal', code: 'food_pile_outside_map', message: 'one food pile stands outside the map (row 12, column 40)', row: 12, column: 40 },
            { severity: 'warning', code: 'hill_missing', message: 'Blue has no hill', row: -1, column: -1 },
            { severity: 'warning', code: 'starting_ants_missing', message: 'Red has no starting ants (no start marker, tile 155)', row: -1, column: -1 }]
    };
    card = Checks.render(doc, no, { onShow() {} });
    equal(texts(card, 'sev'), ['Stops the map', 'Warning', 'Warning'], 'Classic does not: the marks, in the checker\'s order');
    equal(texts(card, 'go'), ['Show me: row 12, column 40'], 'a link for the finding that has a place and none for the others');
    equal(texts(Checks.render(doc, no, null), 'go'), [], 'and no link at all without a handler');
    equal(texts(card, 'clean'), [], 'no "nothing to fix" while something stops the map');
    equal(texts(card, 'hl'), ['!Does not work in the original game'], 'a map that does not work has the other mark');
    const tall = { kind: 'classic', judged: true, works: false, headline: 'Does not work in the original game', badge: '', findings: [{ severity: 'warning', code: 'rows_beyond_wall', message: 'the map has 100 rows: ants cannot enter or cross the 91st row', row: 90, column: -1 }] };
    equal(texts(Checks.render(doc, tall, { onShow() {} }), 'go'), ['Show me: row 90'], 'a finding of a row only says the row');
    const ext = { kind: 'extended', judged: true, works: true, headline: 'Works in the new version', badge: 'New version only', findings: [{ severity: 'note', code: 'not_for_original', message: 'not for the original game (needs a version 8 file): this is an Extended map, version 9, which only this remake loads', row: -1, column: -1 }] };
    card = Checks.render(doc, ext, null);
    equal(texts(card, 'badge-nv'), ['New version only'], 'Extended works: the neutral badge');
    equal(texts(card, 'foot'), [Checks.NOT_A_FAULT], 'and the line that says it is not a fault');
    equal(texts(card, 'tag'), ['Extended map'], 'the kind is Extended');
    equal(texts(Checks.render(doc, Object.assign({}, ext, { works: false, headline: 'Does not work in the new version' }), null), 'foot'), [], 'the line is for a map that works only');
    const hostile = { kind: 'classic', judged: true, works: false, headline: 'Does not work in the original game', badge: '', findings: [{ severity: 'fatal', code: 'x', message: '<img src=x onerror=alert(1)>', row: -1, column: -1 }] };
    card = Checks.render(doc, hostile, null);
    equal(texts(card, 'msg'), ['<img src=x onerror=alert(1)>'], 'markup in a sentence stays text');
    equal(Checks.render(doc, { kind: 'extended', judged: false, works: false, headline: 'Not checked', badge: 'New version only', findings: [] }, null).textContent.includes(Checks.ALL_GOOD), false, 'a map that was not judged does not say nothing is to fix');
    equal(Checks.summary(works), { works: true, text: 'Works in the original game' }, 'the summary of a good map');
    equal(Checks.summary(null), { works: false, text: 'Not checked' }, 'the summary of nothing');
}

function caseE() {
    const rgb = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
    const [gr, gg, gb] = rgb(Tiles.css(485, 0x7FFE)), [wr, wg, wb] = rgb(Tiles.css(476, 0x7FFE));
    check(gr > gb && gr >= gg, 'plain gravel (485) is brown-gray');
    check(rgb(Tiles.css(487, 0x7FFE)).every((v) => v > 170) && rgb(Tiles.css(571, 0x7FFE))[2] > rgb(Tiles.css(571, 0x7FFE))[0] && rgb(Tiles.css(439, 0x7FFE)).every((v) => v < 100), 'dirt is light, slate bluish and mud dark gray');
    check(wb > wr && wb > wg, 'water (476) is blue');
    equal([Tiles.css(485, 245), Tiles.css(485, 246), Tiles.css(485, 247), Tiles.css(485, 248)], ['#262626', '#4a5fc8', '#c0392b', '#2f9a52'], 'the four hills: black, blue, red, green');
    equal(Tiles.css(485, 134), '#4a4a4a', 'a wall');
    equal(Tiles.css(485, 99999 & 0xFFFF), Tiles.css(485, 99999 & 0xFFFF), 'an id that is no tile has a color');
    check(['#8d7871', '#85706a', '#957f78'].includes(Tiles.css(60000, 0x7FFE)), 'an id of layer 1 that is no ground is drawn as gravel');
    const t = Tiles.packedTables();
    equal(t.objects[0x7FFE], 0, 'no object: the packed table is 0');
    equal(t.ground[485], Tiles.hexToPacked(Tiles.css(485, 0x7FFE)), 'packed ground');
    equal(Tiles.hexToPacked('#112233').toString(16), 'ff332211', 'a packed color is 0xAABBGGRR');
    equal(Tiles.TEAMS, { black: '#262626', blue: '#4a5fc8', green: '#2f9a52', red: '#c0392b' }, 'the team colors');
}

if (argv.includes('--list-maps')) {          // for the Python test, which reads the same files with struct: name, rows, columns, digest
    for (const f of libraryFiles()) {
        const m = File.parse(new Uint8Array(fs.readFileSync(f)));
        console.log([path.relative(ROOT, f), m.rows, m.columns, digestOf(m)].join('\t'));
    }
}
else {
    for (const [name, fn] of [['A the file reader', caseA], ['B the view', caseB], ['C the sizes', caseC], ['D the checks card', caseD], ['E the colors of the tiles', caseE]]) {
        process.stdout.write('  RUNNING: ' + name + ' ... ');
        const before = failures;
        try { fn(); } catch (e) { failures++; console.log('\n    FAILED: threw ' + (e && e.stack ? e.stack : e)); }
        if (failures === before) console.log('PASS');
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exitCode = failures ? 1 : 0;
}
