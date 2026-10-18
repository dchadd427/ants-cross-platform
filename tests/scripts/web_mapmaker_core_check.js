#!/usr/bin/env node
// The map maker's code (web/front/mapmaker_*.js; docs/MAP_EDITOR.md) held to the real map core: the browser module (build_web/lvl_core/lvl_core.js + lvl_core.wasm, wrapped by
// web/lvl_core_api.js) is loaded in node, and what the page reads out of a file is compared with what the core says of it.
//
//   node tests/scripts/web_mapmaker_core_check.js [--module DIR] [--require]
//
//   --module DIR  the folder of lvl_core.js and lvl_core.wasm (default: $ANTS_LVL_CORE_DIR, else build_web/lvl_core of the checkout)
//   --require     a missing module is a failure (the default is "SKIP" and exit 0: a checkout that has no web build)
//
//   A  a blank map of each kind and size is read by the page as the core reports it (rows, columns, kind), and a Classic one has the bytes that the live line says
//   B  every map of the library (540) is read, goes up to an Extended map through the core, and is read again with the same rows, columns and every tile of both layers,
//      the same start markers and food piles as the core counts: the page reads version 8 and version 9 where the core does
//   C  the limits as the cards say them, and the core's refusals of a size as short sentences (too many rows offers an Extended map)
//   D  the check of a new map and of an opened one by the profile the page uses ("extended": both kinds), in the words of the checks card
//   E  an older module is refused by its interface version, before anything is asked of it
//   F  a map that has been drawn on (web/front/mapmaker_edit.js): the core reads it, checks it, writes it as the same level, and the objects keep the anchors of their kind (version 8 and 9)
//   H  bigger objects (web/front/mapmaker_objects.js): all of them on a blank map of each kind and three on each of the 540 maps; the core finds nothing new, writes the same bytes, and the anchors survive a conversion
//   G  hills, starting ants and food piles (web/front/mapmaker_blocks.js): the core's checker says "Blue has no hill" until the hill is there and then says nothing of it, counts the ants and the piles
//      as they were put, writes the edited file as the same bytes, and every hill of the 540 maps taken away and put back is the same level (version 8 and 9)
// Exit status 0 when everything passes (or there is no module and --require is not given), 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const FRONT = path.join(ROOT, 'web', 'front');
const Edit = require(path.join(FRONT, 'mapmaker_edit.js')), Blocks = require(path.join(FRONT, 'mapmaker_blocks.js')), Palette = require(path.join(FRONT, 'mapmaker_palette.js'));
const File = require(path.join(FRONT, 'mapmaker_file.js')), Sizes = require(path.join(FRONT, 'mapmaker_sizes.js')), Checks = require(path.join(FRONT, 'mapmaker_checks.js'));
const LvlCore = require(path.join(ROOT, 'web', 'lvl_core_api.js'));

let failures = 0, checks = 0;
function check(ok, what) {
    checks++;
    if (!ok) { failures++; console.log('    FAILED: ' + what); }
}
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }

function parseArgs(argv) {
    const o = { module: process.env.ANTS_LVL_CORE_DIR || path.join(ROOT, 'build_web', 'lvl_core'), require: false };
    for (let i = 0; i < argv.length; i++) {
        if (argv[i] === '--module') o.module = argv[++i];
        else if (argv[i] === '--require') o.require = true;
        else { console.log('usage: web_mapmaker_core_check.js [--module DIR] [--require]'); process.exit(2); }
    }
    return o;
}

function libraryFiles() {
    const found = [];
    for (const dir of ['Original-Ants/Maps', 'Community-Maps']) {
        for (const name of fs.readdirSync(path.join(ROOT, dir)).sort()) if (/\.lvl$/i.test(name)) found.push(path.join(ROOT, dir, name));
    }
    return found;
}

function caseA(core) {
    for (const [rows, columns] of [[1, 1], [31, 31], [90, 111], [10, 130], [90, 1]]) {
        const made = core.blank(rows, columns, 485, 6, 10, 'new_map');
        check(made.status === 0, 'a blank ' + rows + ' x ' + columns + ' Classic map is made');
        if (made.status !== 0) continue;
        const f = File.parse(made.bytes), r = core.readReport(made.bytes, 'extended').json;
        check(f.ok && f.kind === 'classic' && f.rows === rows && f.columns === columns && r.rows === rows && r.columns === columns, 'the page reads the blank ' + rows + ' x ' + columns + ' map as the core does');
        check(made.bytes.length === Sizes.blankBytes(rows * columns), 'the live line is exact for ' + rows + ' x ' + columns + ' (' + made.bytes.length + ' bytes, line says ' + Sizes.blankBytes(rows * columns) + ')');
        check(f.tile1(rows - 1, columns - 1) === 485 && f.tile2(0, 0) === File.EMPTY2, 'a blank map is plain ground with no object');
    }
    for (const [rows, columns] of [[2, 2], [300, 400], [512, 512], [1024, 256], [1, 1024]]) {
        const made = core.blankExtended(rows, columns, 485, 6, 10);
        check(made.status === 0, 'a blank ' + rows + ' x ' + columns + ' Extended map is made');
        if (made.status !== 0) continue;
        const f = File.parse(made.bytes), r = core.readReport(made.bytes, 'extended').json;
        check(f.ok && f.kind === 'extended' && f.rows === rows && f.columns === columns && r.rows === rows && r.columns === columns && r.version === 9, 'the page reads the blank ' + rows + ' x ' + columns + ' Extended map as the core does');
        check(Math.abs(made.bytes.length - Sizes.blankBytes(rows * columns)) <= 64, 'the live line is within a few bytes for ' + rows + ' x ' + columns + ' (' + made.bytes.length + ' against ' + Sizes.blankBytes(rows * columns) + ')');
        check(f.tile1(rows - 1, columns - 1) === 485 && f.tile2(rows - 1, columns - 1) === File.EMPTY2, 'a blank Extended map is plain ground with no object');
    }
}

function sameCells(a, b) {
    for (let r = 0; r < a.rows; r++) for (let c = 0; c < a.columns; c++) if (a.tile1(r, c) !== b.tile1(r, c) || a.tile2(r, c) !== b.tile2(r, c)) return [r, c];
    return null;
}

function caseB(core) {
    let read = 0, up = 0;
    for (const file of libraryFiles()) {
        const name = path.basename(file), bytes = new Uint8Array(fs.readFileSync(file));
        const f = File.parse(bytes), r = core.readReport(bytes, 'extended');
        if (!f.ok || r.status !== 0 || !r.json.loaded) { check(false, name + ' is read by the page and by the core'); continue; }
        if (f.rows !== r.json.rows || f.columns !== r.json.columns) { check(false, name + ': the page has ' + f.rows + ' x ' + f.columns + ', the core ' + r.json.rows + ' x ' + r.json.columns); continue; }
        const markers = r.json.teams.reduce((n, t) => n + t.start_markers, 0);
        if (f.markers.length !== markers || f.food.length !== r.json.food_piles) check(false, name + ': markers ' + f.markers.length + '/' + markers + ', food ' + f.food.length + '/' + r.json.food_piles);
        read++;
        const e = core.convertToExtended(bytes);
        if (e.status !== 0 || !e.json.ok) { check(false, name + ' goes up to an Extended map'); continue; }
        const g = File.parse(e.bytes);
        if (!g.ok || g.kind !== 'extended' || g.rows !== f.rows || g.columns !== f.columns) { check(false, name + ' as an Extended map is read: ' + JSON.stringify([g.ok, g.kind, g.rows, g.columns, g.error])); continue; }
        const differs = sameCells(f, g);
        if (differs || g.markers.length !== f.markers.length || g.food.length !== f.food.length) { check(false, name + ': the cells of the Extended map differ at ' + JSON.stringify(differs)); continue; }
        up++;
    }
    equal([read, up], [540, 540], 'maps read, and read again as Extended maps with the same cells');
}

function caseC(core) {
    const limits = Sizes.limitsOf(core.limits('new_map').json, core.limits('extended').json);
    const words = (kind) => Sizes.limitLine(kind, limits).map((p) => p[0]).join('');
    equal(words('classic'), 'Up to 90 rows and 130 columns, 10,000 cells in all.', 'the Classic card, from the core\'s limits');
    equal(words('extended'), 'Up to 512 x 512 cells (262,144 in all). The longest side can be 1,024.', 'the Extended card, from the core\'s limits');
    const refused = (kind, rows, columns) => {
        const answer = kind === 'classic' ? core.blank(rows, columns, 485, 6, 10, 'new_map') : core.blankExtended(rows, columns, 485, 6, 10);
        return answer.status === 1 ? Sizes.refusal(answer.error) : answer.status;
    };
    equal(refused('classic', 100, 80), { text: 'A Classic map made now has at most 90 rows (ants cannot enter or cross the 91st row of a taller map, in the original game too).', offersExtended: true }, 'too many rows');
    equal(refused('classic', 91, 10).offersExtended, true, 'one row too many offers the Extended map');
    equal(refused('classic', 90, 131), { text: 'The grid has 90 rows x 131 columns, more than the 90 x 130 allowed here.', offersExtended: false }, 'one column too many');
    equal(refused('classic', 90, 120).text, 'The grid has 90 rows x 120 columns, 10,800 cells, more than the 10,000 allowed here.', 'too many cells');
    equal(refused('extended', 513, 513).text, 'The grid has 513 rows x 513 columns, 263,169 cells, more than the 262,144 allowed here.', 'too many cells in an Extended map');
    equal(refused('extended', 1025, 10).text, 'The grid has 1,025 rows x 10 columns, more than the 1,024 x 1,024 allowed here.', 'a side of more than 1,024');
    // the page's own test of a size agrees with the core on both sides of every limit
    const fits = (kind, rows, columns) => (kind === 'classic' ? rows <= limits.classic.rows && columns <= limits.classic.columns && rows * columns <= limits.classic.cells
        : rows <= limits.extended.longest && columns <= limits.extended.longest && rows * columns <= limits.extended.cells);
    for (const kind of ['classic', 'extended']) {
        for (const [rows, columns] of [[90, 130], [90, 111], [90, 112], [91, 1], [1, 130], [1, 131], [512, 512], [513, 512], [1024, 256], [1024, 257], [1025, 1], [1, 1024], [1, 1025], [100, 100]]) {
            const answer = kind === 'classic' ? core.blank(rows, columns, 485, 6, 10, 'new_map') : core.blankExtended(rows, columns, 485, 6, 10);
            check((answer.status === 0) === fits(kind, rows, columns), kind + ' ' + rows + ' x ' + columns + ': the core says ' + (answer.status === 0 ? 'yes' : 'no') + ' and the page ' + (fits(kind, rows, columns) ? 'yes' : 'no'));
        }
    }
}

function caseD(core) {
    const tiny = new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', 'TINY.LVL')));
    let report = core.checkMap(tiny, 'extended').json;
    equal([report.kind, report.works, report.badge, Checks.summary(report).text], ['classic', true, '', 'Works in the original game'], 'TINY by the page\'s profile');
    const up = core.convertToExtended(tiny).bytes;
    report = core.checkMap(up, 'extended').json;
    equal([report.kind, report.works, report.badge, Checks.summary(report).text], ['extended', true, 'New version only', 'Works in the new version'], 'TINY as an Extended map');
    const blank = core.blank(31, 31, 485, 6, 10, 'new_map').bytes;
    report = core.checkMap(blank, 'extended').json;
    equal([report.kind, report.works, Checks.summary(report).text], ['classic', false, 'Does not work in the original game'], 'a new map has no hills yet');
    check(report.findings.length === 8 && report.findings.every((f) => f.severity === 'warning'), 'with eight warnings: a hill and starting ants for each team');
    const tall = core.checkMap(core.blank(100, 80, 485, 6, 10, 'game').bytes, 'extended').json;
    check(tall.findings.some((f) => f.code === 'rows_beyond_wall' && f.row === 90), 'a Classic map of 100 rows has the warning about the 91st row, at row 90');
    check(!tall.findings.some((f) => f.place_limit), 'and no limit of the place: the page judges by the profile that takes both kinds');
    const wide = core.blank(80, 200, 485, 6, 10, 'game').bytes, wideFile = File.parse(wide);
    check(wideFile.ok && core.checkMap(wide, 'extended').json.judged, 'a legal Classic map of 80 x 200 cells is opened');
    equal(Sizes.mapFacts('classic', wideFile.cells, wide.length, Sizes.limitsOf(core.limits('new_map').json, core.limits('extended').json)).caption, '16,000 of 65,536 cells', 'and its meter counts against the limit of an opened Classic map');
    check(core.checkMap(new Uint8Array(5), 'extended').status === 0, 'a file that is no map still gets an answer to show');
}

function caseE() {
    check(LvlCore.moduleVersion({}) === 0 && LvlCore.moduleVersion({ _lvl_interface_version: () => 1 }) === 1, 'the version of a module is asked before anything else');
    let thrown = null;
    try { LvlCore.wrap({ _lvl_interface_version: () => 1 }); } catch (e) { thrown = e; }
    check(thrown && /interface version 1/.test(thrown.message), 'an older module is refused by wrap(): ' + (thrown && thrown.message));
    check(LvlCore.EXPECTED_INTERFACE === 2, 'the page needs interface version 2');
}

// The page's drawing, then the core: what the page writes into the bytes is a map that the core reads, checks and writes again as the same level
function paint(file, ed, kind, tile, stamps, size) {
    const name = (Palette.itemOf(kind, tile) || { name: '' }).name;
    if (tile !== null) { const made = Edit.ensureTile(file, tile, name); if (made.grew) { file = File.parse(made.bytes); ed.rebase(file); } }
    const s = ed.begin(kind, tile);
    for (const [r, c] of stamps) s.stamp(r, c, size || 1);
    s.end();
    return file;
}
function digestOf(bytes) { return require('crypto').createHash('sha256').update(bytes).digest('hex'); }
function cellsDigest(f) { return digestOf(f.bytes.slice(f.layer1, f.layer2 + 6 * f.cells)); }       // (both layers: the dictionary, which only grows, is not in it)

function caseF(core) {
    const codes = (bytes) => core.checkMap(bytes, 'extended').json.findings.map((f) => f.severity + ':' + f.code).sort();
    for (const kind of ['classic', 'extended']) {
        const made = kind === 'classic' ? core.blank(40, 50, 485, 6, 10, 'new_map') : core.blankExtended(300, 400, 485, 6, 10);
        let file = File.parse(made.bytes);
        const ed = Edit.create(file), blankCodes = codes(file.bytes), start = digestOf(file.bytes);
        file = paint(file, ed, 'ground', 487, [[3, 3], [3, 4], [10, 10]], 3);
        file = paint(file, ed, 'ground', 571, [[20, 20]], 9);
        file = paint(file, ed, 'ground', 476, [[0, 0]], 5);
        file = paint(file, ed, 'objects', 302, [[5, 5], [5, 6], [30, 30]], 3);
        file = paint(file, ed, 'objects', 63, [[12, 12]], 1);
        file = paint(file, ed, 'erase', null, [[5, 5]], 1);
        const tag = ' (' + kind + ')';
        const read = core.readReport(file.bytes, 'extended').json;
        check(read.loaded && read.rows === file.rows && read.columns === file.columns, 'the core reads the map that was drawn on' + tag);
        equal(codes(file.bytes), blankCodes, 'and finds nothing new in it: a tile outside the dictionary, a name of "." and an anchor in the wrong place are all findings' + tag);
        check(read.tile_ids_used >= 6, 'it counts the tiles used (' + read.tile_ids_used + ')' + tag);
        const written = core.rewrite(file.bytes, 'extended');
        check(written.status === 0, 'the core writes it (' + (written.error || '') + ')' + tag);
        if (written.status !== 0) continue;
        const again = File.parse(written.bytes);
        check(again.ok && again.kind === kind && sameCells(file, again) === null, 'the file that it writes has the same cells' + tag);
        equal(core.sameLevel(file.bytes, written.bytes, 'extended').json, { same: true }, 'and is the same level' + tag);
        check(core.checkMap(written.bytes, 'extended').json.works === core.checkMap(file.bytes, 'extended').json.works, 'with the same answer to the check' + tag);
        let n = 0;
        while (ed.undo()) n++;
        check(n === 6 && cellsDigest(ed.file) === cellsDigest(File.parse(made.bytes)), 'six undos put every cell back' + tag);
        check(start !== digestOf(file.bytes) && digestOf(written.bytes) !== digestOf(made.bytes), 'and the drawn map is not the blank one' + tag);
    }
    // a map of the library: a hill goes whole, and the check says so; undo brings it back
    const tinyBytes = new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', 'TINY.LVL')));
    for (const [label, bytes] of [['TINY', tinyBytes], ['TINY as an Extended map', core.convertToExtended(tinyBytes).bytes]]) {
        const file = File.parse(bytes), ed = Edit.create(file), before = codes(bytes), start = digestOf(bytes);
        let hill = -1;
        for (let i = 0; i < file.cells && hill < 0; i++) if (Edit.tile(file, 2, i) >= 245 && Edit.tile(file, 2, i) <= 248 && Edit.objectCells(file, i).length === 16) hill = i;
        check(hill >= 0, label + ' has a hill that the page finds whole');
        const s = ed.begin('erase', null);
        s.stamp(Math.floor(hill / file.columns), hill % file.columns, 1);
        s.end();
        const after = codes(file.bytes);
        check(after.filter((c) => c === 'warning:hill_missing').length === 1 && !after.some((c) => /hill_not_whole|hill_without_entrance/.test(c)), label + ': the hill is gone whole, and the core says one team has no hill: ' + JSON.stringify(after));
        ed.undo();
        check(digestOf(file.bytes) === start && JSON.stringify(codes(file.bytes)) === JSON.stringify(before), label + ': undo gives the same bytes and the same check');
    }
    // the anchors of an object painted in an Extended map are the ones that a Classic copy reads (the same cell), and the other way
    const up = core.convertToExtended(tinyBytes).bytes, upFile = File.parse(up), upEd = Edit.create(upFile);
    const stamps = [[2, 2], [5, 9], [upFile.rows - 1, upFile.columns - 1]];
    paint(upFile, upEd, 'objects', 303, stamps, 1);
    const down = core.convertToClassic(upFile.bytes);
    check(down.status === 0 && down.json.ok, 'an Extended map with objects drawn on it goes down to a Classic map (' + JSON.stringify(down.json && down.json.reasons) + ')');
    if (down.json && down.json.ok) {
        const classic = File.parse(down.bytes);
        check(stamps.every(([r, c]) => classic.tile2(r, c) === 303 && Edit.third(classic, 2, r * classic.columns + c) === (r | (c << 8))), 'each object is its own anchor there: row in the low byte, column in the high byte');
        check(stamps.every(([r, c]) => Edit.anchorIndex(upFile, r * upFile.columns + c) === r * upFile.columns + c), 'and in the Extended map the page reads the anchor as the cell itself');
    }
    // the library, all of it: draw, undo, and the bytes are the file again; what the core writes of the original it writes of the drawn map
    let worked = 0, undone = 0, written = 0;
    for (const file0 of libraryFiles()) {
        const bytes = new Uint8Array(fs.readFileSync(file0)), f = File.parse(bytes);
        if (!f.ok) continue;
        worked++;
        const ed = Edit.create(f), start = digestOf(bytes);
        let g = f;
        g = paint(g, ed, 'ground', 487, [[0, 0], [Math.floor(g.rows / 2), Math.floor(g.columns / 2)]], 5);
        g = paint(g, ed, 'objects', 304, [[1, 1], [g.rows - 1, g.columns - 1]], 3);
        g = paint(g, ed, 'erase', null, [[Math.floor(g.rows / 2), Math.floor(g.columns / 2)]], 9);
        const edited = g.bytes.slice();
        const here = core.readReport(edited, 'extended').json;
        if (here.loaded && core.rewrite(edited, 'extended').status === 0 && core.rewrite(bytes, 'extended').status === 0) written++;
        else if (core.rewrite(bytes, 'extended').status === 0) check(false, path.basename(file0) + ': the original is written by the core and the drawn map is not');
        while (ed.undo()) { /* all of them */ }
        if (cellsDigest(ed.file) === cellsDigest(File.parse(bytes))) undone++;
    }
    check(worked === 540 && undone === 540, 'all 540 maps of the library: drawn on and taken back to the cells they had (' + undone + ')');
    check(written > 400, 'and the core writes the drawn map wherever it writes the original (' + written + ')');
}


// ---- G: hills, ants and food with the core ----
function caseG(core) {
    const editorOn = (file) => Edit.create(file, { rebase: (b) => File.parse(b), erase: Blocks.eraseAt });
    const messages = (bytes) => core.checkMap(bytes, 'extended').json.findings.map((f) => f.message);
    const codes = (bytes) => core.checkMap(bytes, 'extended').json.findings.map((f) => f.code);
    const teamsOf = (bytes) => core.readReport(bytes, 'extended').json.teams;
    for (const kind of ['classic', 'extended']) {
        const tag = ' (' + kind + ')', made = kind === 'classic' ? core.blank(40, 50, 485, 6, 10, 'new_map') : core.blankExtended(300, 400, 485, 6, 10);
        const ed = editorOn(File.parse(made.bytes));
        const first = messages(ed.file.bytes).filter((m) => !/not for the original/.test(m));
        check(first.length === 8 && first.indexOf('Blue has no hill') >= 0 && first.indexOf('Black has no hill') >= 0 && first.some((m) => /^Blue has no starting ants/.test(m)), 'a blank map: every team has no hill and no starting ants' + tag + ': ' + first.join('; '));
        const spots = [[8, 8], [8, 40], [30, 8], [30, 40]];
        for (let t = 0; t < 4; t++) {
            const label = Blocks.TEAMS[t].label, ch = Blocks.placeHill(ed.file, t, spots[t][0], spots[t][1]);
            check(!ch.error && ed.act(ch) !== null, label + ': the hill is put' + tag);
            const now = messages(ed.file.bytes);
            check(now.indexOf(label + ' has no hill') < 0 && now.filter((m) => /no hill/.test(m)).length === 3 - t, label + ': "' + label + ' has no hill" is gone and the others are still said' + tag);
            check(!codes(ed.file.bytes).some((c) => /hill_not_whole|hill_without_entrance|hills_several|tile_outside_dictionary|tile_not_drawn/.test(c)), label + ': the core finds the hill whole, with its entrance, and its tile in the dictionary' + tag);
            equal(teamsOf(ed.file.bytes)[t].hill_cells, 16, label + ': the core counts 16 cells of its hill' + tag);
        }
        for (let t = 0; t < 4; t++) {
            const label = Blocks.TEAMS[t].label, ch = Blocks.placeStart(ed.file, t, spots[t][0] + 5, spots[t][1], 5);
            check(!ch.error && ed.act(ch) !== null, label + ': five ants are put' + tag);
            const now = messages(ed.file.bytes);
            check(now.filter((m) => /no starting ants/.test(m)).length === 3 - t && !now.some((m) => m.indexOf(label + ' has no starting ants') === 0), label + ': "' + label + ' has no starting ants" is gone' + tag);
            equal(teamsOf(ed.file.bytes)[t].start_markers, 5, label + ': the core counts 5 start markers' + tag);
        }
        let report = core.checkMap(ed.file.bytes, 'extended').json;
        check(report.works && report.findings.every((f) => f.severity === 'note'), 'every team has its hill and its ants: the map works, nothing to fix (' + report.headline + ')' + tag);
        for (let n = 2; n <= 6; n++) {
            const ch = Blocks.placePile(ed.file, 15 + n, 20, n);
            check(!ch.error && ed.act(ch) !== null, n + ' stages: the pile is put' + tag);
            const found = core.readReport(ed.file.bytes, 'extended').json;
            check(found.food_piles === n - 1 && !codes(ed.file.bytes).some((c) => /food_pile|tile_outside_dictionary|tile_not_drawn/.test(c)), n + ' stages: the core counts ' + (n - 1) + ' piles, none outside the map, none without stages, none that it cannot show' + tag);
        }
        const finished = ed.file.bytes.slice(), written = core.rewrite(finished, 'extended');
        check(written.status === 0 && written.bytes.length === finished.length && digestOf(written.bytes) === digestOf(finished), 'the core writes the edited file as the very same bytes (the lists are as the writer makes them)' + tag);
        equal(core.sameLevel(finished, written.bytes, 'extended').json, { same: true }, 'the same level' + tag);
        check(core.readReport(finished, 'extended').json.playable === true, 'and all four teams play' + tag);
        // the eraser through the page's hooks, then the check says so again
        const gone = ed.begin('erase', null);
        gone.stamp(8, 8, 1); gone.stamp(30 + 5, 8, 1); gone.stamp(15 + 4, 20, 1);
        gone.end();
        const after = messages(ed.file.bytes);
        check(after.indexOf('Green has no hill') >= 0 && after.some((m) => /^Black has no starting ants/.test(m)) === false && core.readReport(ed.file.bytes, 'extended').json.food_piles === 4, 'the eraser takes a hill, a team\'s ants and a pile, and the core says so (' + after.filter((m) => !/not for the original/.test(m)).join('; ') + ')' + tag);
        ed.undo();
        check(digestOf(ed.file.bytes) === digestOf(finished), 'undo of the whole drag gives the same bytes' + tag);
        let steps = 0;
        while (ed.history.canUndo()) { ed.undo(); steps++; }
        check(steps === 4 + 4 + 5 && messages(ed.file.bytes).filter((m) => !/not for the original/.test(m)).join() === first.join(), 'all ' + steps + ' steps undone: the blank map\'s eight sentences are back' + tag);
        equal(core.sameLevel(made.bytes, ed.file.bytes, 'extended').json.same === true || /dictionary name/.test(core.sameLevel(made.bytes, ed.file.bytes, 'extended').json.difference), true, 'and it is the blank map\'s level (only the names that the dictionary got may differ)' + tag);
    }
    // the library: every whole hill taken away and put back is the same level, in both kinds; a team's ants moved and put back by another path are the level that they were
    const each = [], differences = {};
    let hills = 0, same = 0, sameUp = 0, upHills = 0;
    for (const file0 of libraryFiles()) {
        const bytes = new Uint8Array(fs.readFileSync(file0)), f0 = File.parse(bytes);
        if (!f0.ok) continue;
        const up = core.convertToExtended(bytes);
        for (const [source, isUp] of [[bytes, false], [up.status === 0 && up.json.ok ? up.bytes : null, true]]) {
            if (!source) continue;
            const f = File.parse(source);
            for (let t = 0; t < 4; t++) {
                const tile = Blocks.TEAMS[t].hill, cells = [];
                for (let i = 0; i < f.cells; i++) if (Edit.tile(f, 2, i) === tile) cells.push(i);
                const entrances = cells.filter((i) => (Edit.flags(f, 2, i) & 1) !== 0);
                if (cells.length !== 16 || entrances.length !== 1 || Edit.objectCells(f, entrances[0]).length !== 16) continue;
                const er = Math.floor(entrances[0] / f.columns), ec = entrances[0] % f.columns;
                if (er < 1 || ec < 1 || er + 2 >= f.rows || ec + 2 >= f.columns) continue;
                if (cells.some((i) => { const r = Math.floor(i / f.columns), c = i % f.columns; return r < er - 1 || r > er + 2 || c < ec - 1 || c > ec + 2; })) continue;       // (a hill that is not the 4 x 4 square round its entrance is not whole)
                const ed = editorOn(File.parse(source.slice())), s = ed.begin('erase', null);
                const bare = cells.filter((i) => Blocks.eraseAt(f, i) === null)[0];           // (a cell of the hill with no ants and no pile on it: the eraser takes those too)
                if (bare === undefined) continue;
                s.stamp(Math.floor(bare / f.columns), bare % f.columns, 1); s.end();
                const gone = core.checkMap(ed.file.bytes, 'extended').json.findings.map((x) => x.code);
                if (gone.filter((c) => c === 'hill_missing').length !== 1 + (core.checkMap(source, 'extended').json.findings.filter((x) => x.code === 'hill_missing').length)) check(false, file0 + ': the core does not say that team ' + t + ' has no hill after the eraser');
                ed.act(Blocks.placeHill(ed.file, t, er, ec));
                const verdict = core.sameLevel(source, ed.file.bytes, 'extended').json;
                if (isUp) upHills++; else hills++;
                if (verdict.same) { if (isUp) sameUp++; else same++; } else { const key = verdict.difference.replace(/#\d+.*$/, '#'); differences[key] = (differences[key] || 0) + 1; each.push(file0 + ' team ' + t + ': ' + verdict.difference); }
            }
        }
    }
    check(hills > 2000 && upHills > 2000, 'the library has ' + hills + ' whole hills (and ' + upHills + ' as Extended maps)');
    const names = each.filter((d) => /dictionary name #24[5-8]/.test(d)).length;
    check(each.length === names, 'every hill taken away and put back is the same level; the only difference is the name that the dictionary got, when the map had "." for the hill\'s tile (' + names + ' of ' + (hills + upHills) + '), never a cell: ' + each.filter((d) => !/dictionary name #24[5-8]/.test(d)).slice(0, 3).join(' | '));
    console.log('    (' + same + ' of ' + hills + ' and ' + sameUp + ' of ' + upHills + ' are the same level to the name; ' + names + ' differ only in a dictionary name)');
}

// ---- H: bigger objects with the core ----
function caseH(core) {
    const Objects = require(path.join(FRONT, 'mapmaker_objects.js'));
    const editorOn = (file) => Edit.create(file, { rebase: (b) => File.parse(b), erase: Blocks.eraseAt });
    const codes = (bytes) => core.checkMap(bytes, 'extended').json.findings.map((f) => f.severity + ':' + f.code).sort();
    const items = Palette.items('bigger');
    const spotFor = (f, item, from) => {
        for (let r = (from || 0) + 1 - item.box.r0; r < f.rows - item.box.r1; r++) for (let c = -item.box.c0; c < f.columns - item.box.c1; c++) {
            if (item.cells.every((rc) => { const i = (r + rc[0]) * f.columns + c + rc[1]; return Edit.tile(f, 2, i) === 0x7FFE && !(Edit.flags(f, 1, i) & 1) && !Blocks.inTheWay(f, i); })) return [r, c];
        }
        return null;
    };
    // a blank map of each kind: all the objects, side by side; the core finds nothing new, writes the very same bytes, and the same level
    for (const kind of ['classic', 'extended']) {
        const tag = ' (' + kind + ')', made = kind === 'classic' ? core.blank(60, 100, 485, 6, 10, 'new_map') : core.blankExtended(300, 400, 485, 6, 10);
        const ed = editorOn(File.parse(made.bytes)), blank = codes(made.bytes);
        let put = 0;
        for (const item of items) {
            const at = spotFor(ed.file, item, put % 40);
            check(!!at, item.name + ': there is room on the blank map' + tag);
            if (!at) continue;
            const change = Objects.place(ed.file, item, at[0], at[1]);
            check(!change.error && ed.act(change) !== null, item.name + ': put' + tag);
            put++;
        }
        const bytes = ed.file.bytes.slice(), read = core.readReport(bytes, 'extended').json;
        check(read.loaded && read.rows === ed.file.rows, 'the core reads the map with ' + put + ' bigger objects on it' + tag);
        equal(codes(bytes), blank, 'and finds nothing new: no tile outside the dictionary, none named "." (the original does not draw it), no hill that is not whole' + tag);
        const written = core.rewrite(bytes, 'extended');
        check(written.status === 0 && digestOf(written.bytes) === digestOf(bytes), 'the core writes the edited file as the very same bytes' + tag);
        equal(core.sameLevel(bytes, written.bytes, 'extended').json, { same: true }, 'the same level' + tag);
        // a control: the same cells with the tile named "." (what a page that forgot the name would write) are a finding
        const f = File.parse(bytes.slice()), spot = items[0].id, slot = f.dictAt + 2 + 11 * spot;
        f.bytes[slot] = 0x2E; for (let i = 1; i < 11; i++) f.bytes[slot + i] = 0;
        check(codes(f.bytes).some((c) => c === 'info:tile_not_drawn' || /tile_not_drawn/.test(c)) === (kind === 'classic'), 'a tile that is named "." is found by the checker (the control of this check)' + tag);
        let steps = 0;
        while (ed.history.canUndo()) { ed.undo(); steps++; }
        check(steps === put && cellsDigest(ed.file) === cellsDigest(File.parse(made.bytes)), 'all ' + steps + ' undone: the cells of the blank map' + tag);
    }
    // the library: three objects on each map, taken away again; the core reads and checks every step as it did the map
    let worked = 0, same = 0, kept = 0;
    for (const file0 of libraryFiles()) {
        const bytes = new Uint8Array(fs.readFileSync(file0)), f0 = File.parse(bytes);
        if (!f0.ok) continue;
        worked++;
        const before = codes(bytes), ed = editorOn(File.parse(bytes.slice()));
        let n = 0;
        for (const item of [items[0], items[Math.floor(items.length / 2)], items[items.length - 1]]) {
            const at = spotFor(ed.file, item);
            if (!at) continue;
            const change = Objects.place(ed.file, item, at[0], at[1]);
            if (change.error || ed.act(change) === null) { check(false, file0 + ': ' + item.name + ' is not put'); continue; }
            n++;
        }
        const edited = ed.file.bytes.slice(), read = core.readReport(edited, 'extended').json;
        check(read.loaded, file0 + ': the core reads it with ' + n + ' objects more');
        if (JSON.stringify(codes(edited)) === JSON.stringify(before)) kept++;
        else check(false, file0 + ': the check says something else with ' + n + ' objects more: ' + JSON.stringify(codes(edited).filter((c) => before.indexOf(c) < 0)));
        if (core.rewrite(bytes, 'extended').status === 0) check(core.rewrite(edited, 'extended').status === 0, file0 + ': the core writes the map with the objects wherever it writes the original');
        while (ed.history.canUndo()) ed.undo();
        const verdict = core.sameLevel(bytes, ed.file.bytes, 'extended').json;
        if (verdict.same || /^(dictionary name|tile_type_count)/.test(verdict.difference || '')) same++; else check(false, file0 + ': undo does not give the level back: ' + verdict.difference);
    }
    check(worked === 540 && kept === 540 && same === 540, 'all 540 maps: the verdicts of the checker do not change when bigger objects are put (' + kept + '), and undo gives the level back (' + same + ')');
    // the anchors through a conversion, in both directions
    const tinyBytes = new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', 'TINY.LVL')));
    for (const [label, source] of [['TINY', tinyBytes], ['TINY as an Extended map', core.convertToExtended(tinyBytes).bytes]]) {
        const ed = editorOn(File.parse(source.slice())), item = items.filter((i) => i.name === 'bearbod')[0], at = spotFor(ed.file, item, 2);
        check(!!at && ed.act(Objects.place(ed.file, item, at[0], at[1])) !== null, label + ': an object is put');
        const f = ed.file, origin = at[0] * f.columns + at[1];
        const other = source === tinyBytes ? core.convertToExtended(f.bytes) : core.convertToClassic(f.bytes);
        check(other.status === 0 && other.json.ok, label + ': the map with the object goes to the other kind (' + JSON.stringify(other.json && other.json.reasons) + ')');
        if (!other.json || !other.json.ok) continue;
        const g = File.parse(other.bytes);
        check(item.cells.every((rc) => { const i = (at[0] + rc[0]) * g.columns + at[1] + rc[1]; return Edit.tile(g, 2, i) === item.id && Edit.anchorIndex(g, i) === origin && Edit.objectCells(g, i).length === item.cells.length; }), label + ': every cell of the object is anchored at the same cell in the other kind, and the page finds all ' + item.cells.length + ' round it');
        const wantWord = (f2, rc) => f2.version === 8 ? (at[0] | (at[1] << 8)) : (((-rc[0]) & 255) | (((-rc[1]) & 255) << 8));
        check(item.cells.every((rc) => Edit.third(g, 2, (at[0] + rc[0]) * g.columns + at[1] + rc[1]) === wantWord(g, rc)), label + ': in the words of version ' + g.version + ' that the page writes itself (absolute in 8, relative in 9)');
    }
}

async function main() {
    const o = parseArgs(process.argv.slice(2));
    const js = path.join(o.module, 'lvl_core.js');
    if (!fs.existsSync(js) || !fs.existsSync(path.join(o.module, 'lvl_core.wasm'))) {
        console.log((o.require ? 'FAIL' : 'SKIP') + ': no lvl_core.js and lvl_core.wasm in ' + o.module + ' (build the web build, or give --module)');
        process.exit(o.require ? 1 : 0);
    }
    const core = LvlCore.wrap(await require(path.resolve(js))());
    for (const [name, fn] of [['A blank maps', caseA], ['B the library, as version 8 and as version 9', caseB], ['C limits and refusals', caseC], ['D the checks', caseD], ['E an older module', caseE], ['F drawing and the core', caseF], ['G hills, ants and food with the core', caseG], ['H bigger objects with the core', caseH]]) {
        process.stdout.write('  RUNNING: ' + name + ' ... ');
        const before = failures;
        try { fn(core); } catch (e) { failures++; console.log('\n    FAILED: threw ' + (e && e.stack ? e.stack : e)); }
        if (failures === before) console.log('PASS');
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}
main();
