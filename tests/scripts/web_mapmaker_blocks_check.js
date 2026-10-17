#!/usr/bin/env node
// The map maker's hills, starting ants and food piles (web/front/mapmaker_blocks.js; docs/MAP_EDITOR.md) held to the 540 maps of the library, with no browser and no map core (the real core's
// answers are tests/scripts/web_mapmaker_core_check.js):
//
//   node tests/scripts/web_mapmaker_blocks_check.js [--front DIR] [--list-blocks]
//
//   --front DIR     the folder of the mapmaker_*.js files to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//   --list-blocks   print, for each map, what the page reads of its two lists (the Python test reads the same files again with struct and compares)
//
//   A  the lists of every map are read where they are, and written back as the same bytes: block 1 from its records, block 2 from its piles (a pile that is kept, and a pile made again from
//      what was read), and a splice that changes nothing is no splice
//   B  a change of the lists and an undo of it: the bytes of the whole file come back, byte for byte (the dictionary may have a name more), on every map
//   C  a hill taken away by the eraser and put in the same place again: the same cells of layer 2, for every whole hill of the library
//   D  everything that a change does not name is as it was: a team's ants put or taken, a pile put or taken, a hill put (the layers, the other lists, the dictionary, the blocks after)
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Edit = load('mapmaker_edit.js'), Blocks = load('mapmaker_blocks.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function same(a, b) { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; }

function libraryFiles() {
    const found = [];
    for (const dir of ['Original-Ants/Maps', 'Community-Maps']) {
        for (const name of fs.readdirSync(path.join(ROOT, dir)).sort()) if (/\.lvl$/i.test(name)) found.push(dir + '/' + name);
    }
    return found;
}

function readAll() {
    return libraryFiles().map((rel) => {
        const bytes = new Uint8Array(fs.readFileSync(path.join(ROOT, rel))), file = File.parse(bytes);
        return { rel, bytes, file };
    });
}

function listBlocks(maps) {
    for (const m of maps) {
        const lay = Blocks.layout(m.file), marks = [152, 153, 154, 155].map((t) => lay.records.filter((r) => r.tile === t).length);
        const piles = lay.piles.map((p) => [p.row, p.column, p.units, p.points].concat(p.stages.reduce((a, s) => a.concat(s), [])).join(',')).join(';');
        console.log([m.rel, lay.ok1 ? 1 : 0, lay.ok2 ? 1 : 0, lay.records.length, marks.join(','), lay.piles.length, piles].join('\t'));
    }
}

// A made-up edit stroke with the page's hooks
function editorOn(file) { return Edit.create(file, { rebase: (b) => File.parse(b), erase: Blocks.eraseAt }); }
function withoutNames(f) { return Array.from(f.bytes.subarray(0, f.dictAt)).join() + '|' + Array.from(f.bytes.subarray(f.dictAt + 2 + 11 * f.names)).join(); }

// ---- A ----
function caseA(maps) {
    let ok1 = 0, ok2 = 0, notOk = [], fromRecords = 0, fromPiles = 0, noSplice = 0;
    for (const m of maps) {
        const f = m.file, lay = Blocks.layout(f), b = f.bytes;
        if (!f.ok) { notOk.push(m.rel); continue; }
        if (lay.ok1) {
            ok1++;
            const again = Blocks.block1Bytes(lay.records), was = b.subarray(f.blocksAt + lay.b1.rel, f.blocksAt + lay.b1.rel + lay.b1.length);
            if (same(again, was)) fromRecords++; else check(false, m.rel + ': block 1 made again from its records is not the same bytes');
            if (Blocks.diffSplice(f, lay.b1.rel, lay.b1.length, again) === null && same(Edit.spliceAt(b, f.blocksAt + lay.b1.rel, lay.b1.length, again), b)) noSplice++;
            else check(false, m.rel + ': a splice of block 1 that changes nothing is not nothing');
        } else notOk.push(m.rel + ' (block 1)');
        if (lay.ok2) {
            ok2++;
            const kept = Blocks.block2Bytes(f, lay.piles.map((p) => ({ keep: true, rel: p.rel, length: p.length }))), made = Blocks.block2Bytes(f, lay.piles.map((p) => ({ row: p.row, column: p.column, units: p.units, points: p.points, stages: p.stages })));
            const was = b.subarray(f.blocksAt + lay.b2.rel, f.blocksAt + lay.b2.rel + lay.b2.length);
            if (same(kept, was) && same(made, was)) fromPiles++; else check(false, m.rel + ': block 2 made again (kept, and from what was read) is not the same bytes');
            if (Blocks.diffSplice(f, lay.b2.rel, lay.b2.length, made) !== null) check(false, m.rel + ': a splice of block 2 that changes nothing is not nothing');
        } else notOk.push(m.rel + ' (block 2)');
    }
    check(maps.length === 540, 'the library has 540 maps (' + maps.length + ')');
    check(ok1 === 540, 'block 1 reads in all 540 maps (' + ok1 + '; not: ' + notOk.join(', ') + ')');
    check(ok2 === 540, 'block 2 reads in all 540 maps (' + ok2 + '; not: ' + notOk.join(', ') + ')');
    check(fromRecords === ok1 && fromPiles === ok2 && noSplice === ok1, 'every list that reads is written back as the same bytes (' + fromRecords + ' of block 1, ' + fromPiles + ' of block 2)');
}

// ---- B ----
function caseB(maps) {
    let changes = 0;
    for (const m of maps) {
        const f0 = File.parse(m.bytes.slice()), ed = editorOn(f0), start = withoutNames(f0), lay = Blocks.layout(f0);
        if (!lay.ok1) continue;
        const rowsOk = f0.rows >= 2 && f0.columns >= 2;
        if (!rowsOk) continue;
        const r = Math.min(f0.rows - 1, 1), c = Math.min(f0.columns - 1, 1);
        ed.act(Blocks.placeStart(ed.file, 1, r, c, 3));
        if (Blocks.layout(ed.file).ok2) ed.act(Blocks.placePile(ed.file, r, c, 4));
        if (f0.rows > 3 && f0.columns > 3) { const h = Blocks.placeHill(ed.file, 2, 1, 1); if (!h.error) ed.act(h); }           // (a hill that would touch another team's is refused)
        const e = ed.begin('erase', null);
        e.stamp(r, c, 3);
        e.end();
        changes++;
        let n = 0;
        while (ed.history.canUndo()) { ed.undo(); n++; }
        if (withoutNames(ed.file) !== start) check(false, m.rel + ': undo of ' + n + ' steps does not give the bytes back');
        while (ed.history.canRedo()) ed.redo();
        while (ed.history.canUndo()) ed.undo();
        if (withoutNames(ed.file) !== start) check(false, m.rel + ': redo and undo again does not give the bytes back');
    }
    check(changes >= 530, 'ants, a pile, a hill and an erase were made and undone on ' + changes + ' maps, and every one came back byte for byte');
}

// ---- C ----
// a hill as the checker wants it (4 x 4 of its tile round the entrance, each cell anchored at it, only the entrance flagged), found by its entrance
function wholeHill(f, team) {
    const tile = Blocks.TEAMS[team].hill, cells = [];
    for (let i = 0; i < f.cells; i++) if (Edit.tile(f, 2, i) === tile) cells.push(i);
    if (cells.length !== 16) return null;
    const entrances = cells.filter((i) => (Edit.flags(f, 2, i) & 1) !== 0);
    if (entrances.length !== 1) return null;
    const er = Math.floor(entrances[0] / f.columns), ec = entrances[0] % f.columns;
    if (er < 1 || ec < 1 || er + 2 >= f.rows || ec + 2 >= f.columns) return null;
    for (let r = er - 1; r <= er + 2; r++) for (let c = ec - 1; c <= ec + 2; c++) {
        const i = r * f.columns + c, want = f.version === 8 ? (er | (ec << 8)) : (((er - r) & 255) | (((ec - c) & 255) << 8));
        if (Edit.tile(f, 2, i) !== Blocks.TEAMS[team].hill || ((Edit.flags(f, 2, i) & 1) !== 0) !== (r === er && c === ec) || (!(r === er && c === ec) && Edit.third(f, 2, i) !== want)) return null;
    }
    return { er, ec };
}

function caseC(maps) {
    let hills = 0, different = [];
    for (const m of maps) {
        const f0 = File.parse(m.bytes.slice());
        for (let team = 0; team < 4; team++) {
            const h = wholeHill(f0, team);
            if (!h) continue;
            const ed = editorOn(File.parse(m.bytes.slice())), was = Array.from(ed.file.bytes.subarray(ed.file.layer2, ed.file.layer2 + 6 * ed.file.cells));
            const st = ed.begin('erase', null);
            st.stamp(h.er, h.ec, 1);
            st.end();
            const gone = Array.from(ed.file.bytes.subarray(ed.file.layer2, ed.file.layer2 + 6 * ed.file.cells));
            if (same(gone, was)) { check(false, m.rel + ': the eraser did not take the hill of team ' + team); continue; }
            ed.act(Blocks.placeHill(ed.file, team, h.er, h.ec));
            const back = Array.from(ed.file.bytes.subarray(ed.file.layer2, ed.file.layer2 + 6 * ed.file.cells));
            hills++;
            if (!same(back, was)) different.push(m.rel + ' team ' + team);
        }
    }
    check(hills > 1800, 'the library has ' + hills + ' whole hills, and every one taken away and put back in the same place is the same cells (' + different.length + ' are not' + (different.length ? ': ' + different.slice(0, 5).join(', ') : '') + ')');
    check(different.length === 0, 'no hill comes back different');
    console.log('    (' + hills + ' hills)');
}

// ---- D ----
function caseD(maps) {
    let done = 0;
    for (const m of maps) {
        const f0 = File.parse(m.bytes.slice()), lay0 = Blocks.layout(f0);
        if (!lay0.ok1 || !lay0.ok2 || f0.rows < 6 || f0.columns < 6) continue;
        const ed = editorOn(f0), layers = Array.from(f0.bytes.subarray(f0.layer1, f0.blocksAt)), tail = Array.from(f0.bytes.subarray(f0.blocksAt + lay0.b2.rel + lay0.b2.length));
        ed.act(Blocks.placeStart(ed.file, 0, 3, 3, 5));
        let lay = Blocks.layout(ed.file), f = ed.file;
        const mineAfter = lay.records.filter((r) => r.tile === 154).length, others = lay.records.filter((r) => r.tile !== 154).map((r) => [r.tile, r.row, r.column].join());
        const othersBefore = lay0.records.filter((r) => r.tile !== 154).map((r) => [r.tile, r.row, r.column].join());
        if (mineAfter !== 5 || others.join('|') !== othersBefore.join('|')) check(false, m.rel + ': the ants of Green are 5 and the other records are as they were');
        if (!same(Array.from(f.bytes.subarray(f.blocksAt + lay.b2.rel + lay.b2.length)), tail) || !same(Array.from(f.bytes.subarray(f.layer1, f.blocksAt)), layers)) check(false, m.rel + ': the layers and the blocks after block 2 are as they were after the ants');
        ed.act(Blocks.placePile(ed.file, 3, 3, 5));
        lay = Blocks.layout(ed.file); f = ed.file;
        const before = lay0.piles.filter((p) => !(p.row === 3 && p.column === 3)).map((p) => [p.row, p.column, p.units, p.points, p.stages.join(';')].join());
        const now = lay.piles.filter((p) => !(p.row === 3 && p.column === 3)).map((p) => [p.row, p.column, p.units, p.points, p.stages.join(';')].join());
        if (before.join('|') !== now.join('|') || lay.piles.filter((p) => p.row === 3 && p.column === 3).length !== 1) check(false, m.rel + ': a pile is put and the others are as they were');
        if (!same(Array.from(f.bytes.subarray(f.blocksAt + lay.b2.rel + lay.b2.length)), tail)) check(false, m.rel + ': the blocks after block 2 are as they were after the pile');
        done++;
    }
    check(done >= 520, 'ants and a pile were put on ' + done + ' maps and nothing else of the file changed');
}

const maps = readAll();
// (a list is printed and the script ends by itself: process.exit would cut a long output to a pipe short, as it did on macOS)
if (argv.includes('--list-blocks')) listBlocks(maps);
else {
    const cases = [['A the lists of the library are written back as they are', caseA], ['B changes and undo, byte for byte', caseB], ['C a hill taken away and put back', caseC], ['D only what is named changes', caseD]];
    for (const [name, fn] of cases) {
        const before = failures;
        try { fn(maps); } catch (e) { failures++; console.log('    FAILED: ' + name + ' stopped: ' + (e && e.stack || e)); }
        console.log('RUNNING: ' + name + ' ... ' + (failures === before ? 'PASS' : 'FAIL'));
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exitCode = failures ? 1 : 0;
}
