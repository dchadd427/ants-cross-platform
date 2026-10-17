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
//   C  the limits as the cards say them, and the core's refusals of a size as the sentences of the approved pictures (1c: too many rows)
//   D  the check of a new map and of an opened one by the profile the page uses ("extended": both kinds), in the words of the checks card
//   E  an older module is refused by its interface version, before anything is asked of it
// Exit status 0 when everything passes (or there is no module and --require is not given), 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const FRONT = path.join(ROOT, 'web', 'front');
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
    equal(refused('classic', 100, 80), { text: 'A Classic map made now has at most 90 rows (ants cannot enter or cross the 91st row of a taller map, in the original game too).', offersExtended: true }, 'picture 1c');
    equal(refused('classic', 91, 10).offersExtended, true, 'one row too many offers the Extended map');
    equal(refused('classic', 90, 131), { text: 'The grid has 90 rows x 131 columns, more than the 90 x 130 allowed here.', offersExtended: false }, 'one column too many');
    equal(refused('classic', 90, 120).text, 'The grid has 90 rows x 120 columns, 10800 cells, more than the 10000 allowed here.', 'too many cells');
    equal(refused('extended', 513, 513).text, 'The grid has 513 rows x 513 columns, 263169 cells, more than the 262144 allowed here.', 'too many cells in an Extended map');
    equal(refused('extended', 1025, 10).text, 'The grid has 1025 rows x 10 columns, more than the 1024 x 1024 allowed here.', 'a side of more than 1,024');
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
    check(core.checkMap(new Uint8Array(5), 'extended').status === 0, 'a file that is no map still gets an answer to show');
}

function caseE() {
    check(LvlCore.moduleVersion({}) === 0 && LvlCore.moduleVersion({ _lvl_interface_version: () => 1 }) === 1, 'the version of a module is asked before anything else');
    let thrown = null;
    try { LvlCore.wrap({ _lvl_interface_version: () => 1 }); } catch (e) { thrown = e; }
    check(thrown && /interface version 1/.test(thrown.message), 'an older module is refused by wrap(): ' + (thrown && thrown.message));
    check(LvlCore.EXPECTED_INTERFACE === 2, 'the page needs interface version 2');
}

async function main() {
    const o = parseArgs(process.argv.slice(2));
    const js = path.join(o.module, 'lvl_core.js');
    if (!fs.existsSync(js) || !fs.existsSync(path.join(o.module, 'lvl_core.wasm'))) {
        console.log((o.require ? 'FAIL' : 'SKIP') + ': no lvl_core.js and lvl_core.wasm in ' + o.module + ' (build the web build, or give --module)');
        process.exit(o.require ? 1 : 0);
    }
    const core = LvlCore.wrap(await require(js)());
    for (const [name, fn] of [['A blank maps', caseA], ['B the library, as version 8 and as version 9', caseB], ['C limits and refusals', caseC], ['D the checks', caseD], ['E an older module', caseE]]) {
        process.stdout.write('  RUNNING: ' + name + ' ... ');
        const before = failures;
        try { fn(core); } catch (e) { failures++; console.log('\n    FAILED: threw ' + (e && e.stack ? e.stack : e)); }
        if (failures === before) console.log('PASS');
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}
main();
