#!/usr/bin/env node
// The browser module of the map core (build_web/lvl_core/lvl_core.js + lvl_core.wasm, wrapped by web/lvl_core_api.js), loaded in node and held to the native core on the
// real maps of the library. docs/MAP_FORMAT.md, "In the browser".
//
//   node tests/scripts/lvl_core_check.js [--module DIR] [--lvl-tool PATH] [--require]
//
//   --module DIR     the folder of lvl_core.js and lvl_core.wasm (default: $ANTS_LVL_CORE_DIR, else build_web/lvl_core of the checkout)
//   --lvl-tool PATH  a built lvl_tool: its output for each file is compared too, live (without it, the stored output of tests/data/lvl_core/*.lvl_tool.txt is used)
//   --require        a missing module is a failure (the default is "SKIP" and exit 0: a checkout that has no web build)
//
// What it holds the module to (the expectation in tests/data/lvl_core/ is written by the native test, test_lvl_core --write-expected, from the same C++ code):
//   A  the module's answers (the report, the original-safe findings, the rewrite, the blank maps, the refusals of hostile files) are the native core's, byte for byte, and the
//      findings are the lines that lvl_tool prints
//   B  the canonical rewrite of each file reads back as the same level (same_level), and rewriting it again changes nothing
//   C  a blank map of each size passes the checker where it should: it fits the place and its only findings are the missing hills and starting ants
//   D  hostile input (empty, one byte, cut, 513 KB, huge counts, random bytes, wrong arguments): an error report, never a throw, never a hang, and the module's memory does
//      not grow when the same calls are made again (every buffer was given back)
// (The match fingerprint is not used: no native tool prints one for a map file. The round-trip law that the writer keeps, same play after a rewrite, is test_lvl_writer's.)
'use strict';

const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');

const ROOT = path.resolve(__dirname, '..', '..');
const EXPECTED = path.join(ROOT, 'tests', 'data', 'lvl_core');

let failures = 0;
let checks = 0;
function check(ok, what) {
    checks++;
    if (!ok) {
        failures++;
        console.log('    FAILED: ' + what);
    }
}

async function runCase(name, fn) {
    process.stdout.write('  RUNNING: ' + name + ' ... ');
    const before = failures;
    try {
        await fn();
    } catch (e) {
        failures++;
        console.log('\n    FAILED: threw ' + (e && e.stack ? e.stack : e));
    }
    if (failures === before) console.log('PASS');
}

function parseArgs(argv) {
    const o = { module: process.env.ANTS_LVL_CORE_DIR || path.join(ROOT, 'build_web', 'lvl_core'), lvlTool: '', require: false };
    for (let i = 0; i < argv.length; i++) {
        if (argv[i] === '--module') o.module = argv[++i];
        else if (argv[i] === '--lvl-tool') o.lvlTool = argv[++i];
        else if (argv[i] === '--require') o.require = true;
        else { console.log('usage: lvl_core_check.js [--module DIR] [--lvl-tool PATH] [--require]'); process.exit(2); }
    }
    return o;
}

function text(file) { return fs.readFileSync(path.join(EXPECTED, file), 'utf8'); }
function bytesOf(relative) { return new Uint8Array(fs.readFileSync(path.join(ROOT, relative))); }

// FNV-1a, 64 bits, 16 hex digits (the native test has the same). Two 32-bit halves, so that a file of 3 MB takes a few milliseconds: h * (2^40 + 0x1b3) modulo 2^64
function fnv(bytes) {
    let hi = 0xcbf29ce4;
    let lo = 0x84222325;
    for (let i = 0; i < bytes.length; i++) {
        lo = (lo ^ bytes[i]) >>> 0;
        const low = lo * 0x1b3;                                        // (below 2^41: exact)
        hi = (hi * 0x1b3 + Math.floor(low / 4294967296) + lo * 256) % 4294967296;
        lo = low % 4294967296;
    }
    return hi.toString(16).padStart(8, '0') + lo.toString(16).padStart(8, '0');
}

function randomBytes(seed, n) {   // xorshift32, as the native test
    const b = new Uint8Array(n);
    let x = seed >>> 0;
    for (let i = 0; i < n; i++) {
        x = (x ^ (x << 13)) >>> 0;
        x = (x ^ (x >>> 17)) >>> 0;
        x = (x ^ (x << 5)) >>> 0;
        b[i] = x & 0xFF;
    }
    return b;
}

function put32(b, at, v) { for (let i = 0; i < 4; i++) b[at + i] = (v >>> (8 * i)) & 0xFF; }

function hostileCases() {
    const tiny = bytesOf('Original-Ants/Maps/TINY.LVL');
    const cases = [];
    const add = (id, b) => cases.push({ id: 'hostile_' + id, bytes: b });
    add('empty', new Uint8Array(0));
    add('one_byte', Uint8Array.of(8));
    add('cut_in_header', tiny.slice(0, 20));
    add('cut_in_dictionary', tiny.slice(0, 3000));
    add('cut_in_layers', tiny.slice(0, 12000));
    add('cut_in_half', tiny.slice(0, tiny.length >> 1));
    add('zeros_513_kb', new Uint8Array(513 * 1024));
    const hugeDictionary = tiny.slice(); hugeDictionary[40] = 0xFF; hugeDictionary[41] = 0xFF;
    add('huge_dictionary', hugeDictionary);
    const hugeGrid = tiny.slice(); put32(hugeGrid, 7412, 0xFFFFFFFF); put32(hugeGrid, 7416, 0xFFFFFFFF);
    add('huge_grid', hugeGrid);
    const hugeBlock = tiny.slice(); hugeBlock[18952] = 0xFF; hugeBlock[18953] = 0xFF;
    add('huge_block_1', hugeBlock);
    add('random_1', randomBytes(1, 4096));
    add('random_2', randomBytes(2, 4096));
    add('random_3', randomBytes(3, 100000));
    const v8 = randomBytes(4, 30000); v8[0] = 8; v8[1] = v8[2] = v8[3] = 0;
    add('version_8_then_random', v8);
    return cases;
}


// A version 9 file with another description (the description's length is the word at 16, its bytes follow) and rules word (at 14): the inputs of the conversions' reasons and notes
function extendedWith(bytes, description, rules) {
    const text = Buffer.from(description, 'latin1');
    const out = new Uint8Array(bytes.length - 2 + 2 + text.length - 0);
    out.set(bytes.subarray(0, 16), 0);
    out[16] = text.length & 0xFF; out[17] = text.length >> 8;
    out.set(text, 18);
    out.set(bytes.subarray(18), 18 + text.length);
    out[14] = rules & 0xFF; out[15] = rules >> 8;
    return out;
}

// The sentences and codes of a conversion's reasons or notes
function codesOf(list) { return list.map((r) => r.code).join(','); }

function hostileExtendedCases(tinyUp) {
    const description = tinyUp[16] | (tinyUp[17] << 8);
    const shift = 18 + description - 40;                // where the offsets of a version 8 file (the dictionary's count at 40) are in the version 9 file
    const cases = [];
    const add = (id, b) => cases.push({ id: 'hostile_ext_' + id, bytes: b });
    const flags = tinyUp.slice(); flags[10] = 1; add('format_flag', flags);
    const rules = tinyUp.slice(); rules[14] = 2; add('rules_2', rules);
    const grid = tinyUp.slice(); put32(grid, 7412 + shift, 0xFFFFFFFF); put32(grid, 7416 + shift, 0xFFFFFFFF); add('huge_grid', grid);
    const grid2 = tinyUp.slice(); put32(grid2, 7412 + shift, 65535); put32(grid2, 7416 + shift, 65535); add('grid_65535', grid2);
    add('cut_in_header', tinyUp.slice(0, 12));
    add('cut_in_layers', tinyUp.slice(0, 12000));
    add('cut_in_half', tinyUp.slice(0, tinyUp.length >> 1));
    const v9 = randomBytes(5, 30000); v9[0] = 9; v9[1] = v9[2] = v9[3] = 0; add('version_9_then_random', v9);
    return cases;
}

function libraryCases() {
    return text('index.txt').split('\n').filter(Boolean).map((line) => {
        const f = line.split('\t');
        return { id: f[0], path: f[1], rewriteStatus: Number(f[2]), rewriteSize: f[3], rewriteFnv: f[4], bytes: bytesOf(f[1]) };
    });
}

function main() {
    const o = parseArgs(process.argv.slice(2));
    o.module = path.resolve(o.module);         // (require() takes a bare 'dir/file.js' for a package name: the folder is made absolute first, relative to the current folder)
    const modulePath = path.join(o.module, 'lvl_core.js');
    if (!fs.existsSync(modulePath) || !fs.existsSync(path.join(o.module, 'lvl_core.wasm'))) {
        console.log((o.require ? 'FAIL' : 'SKIP') + ': no lvl_core.js and lvl_core.wasm in ' + o.module + ' (build them: docs/MAP_FORMAT.md, "In the browser")');
        process.exit(o.require ? 1 : 0);
    }
    const createLvlCore = require(modulePath);
    const api = require(path.join(ROOT, 'web', 'lvl_core_api.js'));
    console.log('=======================================================');
    console.log(' The map core in the browser module (lvl_core.wasm), in node ' + process.version);
    console.log('=======================================================');
    console.log('  module: lvl_core.wasm ' + fs.statSync(path.join(o.module, 'lvl_core.wasm')).size + ' bytes, lvl_core.js ' + fs.statSync(modulePath).size + ' bytes, lvl_api ' + fs.statSync(path.join(ROOT, 'web', 'lvl_core_api.js')).size + ' bytes');
    return createLvlCore().then(async (module) => {
        const core = api.wrap(module);
        const library = libraryCases();
        const hostile = hostileCases();

        await runCase('module: the interface', () => {
            check(core.interfaceVersion === 4, 'interface version 4');
            check(typeof module._lvl_free === 'function' && typeof module._lvl_alloc === 'function', 'the buffer calls are exported');
            check(module.FS === undefined, 'no file system in the module');
        });

        await runCase('A the library files: the reports are the native core\'s', () => {
            for (const c of library) {
                const read = core.readReport(c.bytes);
                const chk = core.checkOriginal(c.bytes);
                check(read.status === 0 && read.text === text(c.id + '.read.json'), c.id + ': the report is the native report, byte for byte');
                check(chk.status === 0 && chk.text === text(c.id + '.check.json'), c.id + ': the original-safe findings are the native ones, byte for byte');
                const rw = core.rewrite(c.bytes);
                check(rw.status === c.rewriteStatus && rw.bytes && String(rw.bytes.length) === c.rewriteSize && fnv(rw.bytes) === c.rewriteFnv, c.id + ': the canonical rewrite is the native one (size and FNV-1a)');
            }
        });

        await runCase('A the findings are the lines that lvl_tool prints', () => {
            for (const c of library) {
                const stored = text(c.id + '.lvl_tool.txt');
                let toolText = stored;
                if (o.lvlTool) {
                    const run = childProcess.spawnSync(o.lvlTool, ['check', path.join(ROOT, c.path), '--original', '--new-map'], { encoding: 'utf8' });
                    toolText = run.stdout;
                    check(toolText === stored, c.id + ': the stored lvl_tool output is what lvl_tool prints now');
                }
                const lines = toolText.split('\n').filter((l) => l.startsWith('  '));
                const findings = core.checkOriginal(c.bytes).json.findings.map((f) => '  ' + (f.place_limit ? 'limit' : f.severity) + ' ' + f.code + ': ' + f.message);
                check(JSON.stringify(findings) === JSON.stringify(lines), c.id + ': ' + findings.length + ' findings, the lines of lvl_tool');
                const works = core.checkOriginal(c.bytes).json.works_in_original_game;
                check(works === toolText.includes(': works in the original game'), c.id + ': the verdict is lvl_tool\'s');
            }
        });

        await runCase('A the report says what the files hold (a 31 x 31, a 60 x 60, the largest, a description of 30 bytes, one that fails the checker)', () => {
            const by = (id) => core.readReport(library.find((c) => c.id === id).bytes).json;
            check(by('tiny').rows === 31 && by('tiny').columns === 31 && by('tiny').version === 8 && by('tiny').loaded, 'tiny is 31 x 31, version 8');
            check(by('medium').rows === 60 && by('medium').columns === 60, 'medium is 60 x 60');
            check(by('party').description_bytes === 30, 'party has a description of 30 bytes');
            check(by('easyjungle').rows === 81 && by('easyjungle').columns === 100, 'easy jungle is 81 x 100');
            check(library.find((c) => c.id === 'bomberworld').bytes.length === 235594, 'bomberworld is the largest file (235,594 bytes)');
            check(by('bomberworld').loaded && by('bomberworld').playable, 'bomberworld loads and plays');
            check(by('tiny').teams.length === 4 && by('tiny').teams.every((t) => t.hill_cells === 16 && t.start_markers === 3), 'every team of tiny has a hill and three start markers');
            check(core.checkOriginal(library.find((c) => c.id === 'tiny').bytes).json.works_in_original_game === true, 'tiny works in the original game');
            const conquer = core.checkOriginal(library.find((c) => c.id === 'conquer').bytes).json;
            check(conquer.works_in_original_game === false && conquer.findings.some((f) => f.code === 'file_cut_in_block_4' && f.severity === 'fatal'), 'conquer fails with a fatal finding');
            check(conquer.findings.some((f) => f.code === 'description_too_long'), 'conquer: a description of 30 bytes is reported');
        });

        await runCase('B the rewrite reads back as the same level', () => {
            for (const c of library) {
                const rw = core.rewrite(c.bytes);
                check(rw.status === 0, c.id + ': rewritten');
                const same = core.sameLevel(c.bytes, rw.bytes);
                check(same.status === 0 && same.json.same === true, c.id + ': the rewrite is the same level');
                const again = core.rewrite(rw.bytes);
                check(again.status === 0 && Buffer.compare(Buffer.from(again.bytes), Buffer.from(rw.bytes)) === 0, c.id + ': rewriting the rewrite changes nothing');
                check(core.readReport(rw.bytes).json.loaded === true, c.id + ': the rewrite loads');
            }
            const a = core.sameLevel(library[0].bytes, library[1].bytes);
            check(a.status === 0 && a.json.same === false && typeof a.json.difference === 'string' && a.json.difference.length > 0, 'two different maps are told apart, with the first difference');
            const swapped = core.sameLevel(library[1].bytes, library[0].bytes);
            check(swapped.status === 0 && swapped.json.same === false, 'in either order');
        });

        await runCase('A, C the blank maps', () => {
            const cases = text('blank_index.txt').split('\n').filter(Boolean).map((line) => {
                const f = line.split('\t');
                const m = /^blank_(\d+)x(\d+)_g(\d+)_e(\d+)_m(\d+)$/.exec(f[0]);
                return { id: f[0], args: m.slice(1).map(Number), status: Number(f[1]), size: f[2], tail: f[3] };
            });
            let made = 0;
            for (const c of cases) {
                const r = core.blank(c.args[0], c.args[1], c.args[2], c.args[3], c.args[4]);
                check(r.status === c.status, c.id + ': status ' + c.status);
                if (c.status === 0) {
                    made++;
                    check(String(r.bytes.length) === c.size && fnv(r.bytes) === c.tail, c.id + ': the bytes are the native blank map\'s');
                    const read = core.readReport(r.bytes);
                    check(read.status === 0 && read.json.loaded && read.json.rows === c.args[0] && read.json.columns === c.args[1] && read.json.eggs === c.args[3] && read.json.minutes === c.args[4], c.id + ': reads back as asked');
                    const chk = core.checkOriginal(r.bytes).json;
                    check(chk.fits_the_place === true && chk.judged === true, c.id + ': fits the place');
                    check(chk.works_in_original_game === false, c.id + ': does not work in the original game yet (no hills)');
                    const codes = new Set(chk.findings.map((f) => f.code));
                    check(codes.size === 2 && codes.has('hill_missing') && codes.has('starting_ants_missing'), c.id + ': only the missing hills and starting ants');
                    check(chk.findings.length === 8, c.id + ': one of each for each of the four teams');
                    check(read.json.teams.every((t) => t.hill_cells === 0 && t.start_markers === 0), c.id + ': no hill cell, no start marker');
                    const expectedRead = (() => { try { return text(c.id + '.read.json'); } catch (e) { return null; } })();
                    if (expectedRead !== null) {
                        check(read.text === expectedRead, c.id + ': the report is the native one');
                        check(core.checkOriginal(r.bytes).text === text(c.id + '.check.json'), c.id + ': the findings are the native ones');
                    }
                    check(core.sameLevel(r.bytes, core.rewrite(r.bytes).bytes).json.same === true, c.id + ': the rewrite of a blank map is the same level');
                } else {
                    check(r.error === c.tail, c.id + ': refused with the native sentence');
                }
            }
            check(made >= 6, 'sizes were made: ' + made);
            // arguments that are no numbers are refused, never thrown
            for (const bad of [[NaN, 1, 485, 6, 10], [1.5, 1, 485, 6, 10], [-1, 1, 485, 6, 10], ['3', 3, 485, 6, 10], [1, 1, 485, 6, Infinity], [undefined, 1, 485, 6, 10]]) {
                const r = core.blank.apply(null, bad);
                check(r.status === 1, 'blank(' + String(bad) + ') is refused');
            }
        });

        await runCase('D hostile files: an error report, no throw, nothing left behind', () => {
            const expected = Object.fromEntries(text('hostile_index.txt').split('\n').filter(Boolean).map((l) => { const f = l.split('\t'); return [f[0], f]; }));
            for (const c of hostile) {
                const started = Date.now();
                const read = core.readReport(c.bytes);
                const chk = core.checkOriginal(c.bytes);
                const rw = core.rewrite(c.bytes);
                const same = core.sameLevel(c.bytes, c.bytes);
                check(Date.now() - started < 2000, c.id + ': answered at once');
                check(read.status === 0 && read.json.loaded === false && read.text === text(c.id + '.read.json'), c.id + ': the report says it does not load, as the native one does');
                check(chk.status === 0 && chk.json.works_in_original_game === false && chk.text === text(c.id + '.check.json'), c.id + ': it does not work in the original game, as the native check says');
                check(rw.status === 1 && rw.bytes === undefined && rw.error === expected[c.id][4], c.id + ': no rewrite, the native sentence');
                check(same.status === 1, c.id + ': no comparison');
            }
            // wrong arguments
            check(core.readReport(null).status === 1 && core.readReport('abc').status === 1 && core.readReport([1, 2]).status === 1, 'something that is no Uint8Array is refused');
            check(core.sameLevel(new Uint8Array(0), 5).status === 1, 'the second file must be bytes too');
            check(core.readReport(new Uint8Array(0), 'bogus').status === 1 && core.readReport(new Uint8Array(0), 'game').status === 0, 'an unknown profile is refused');
            // 513 KB is a new map's limit, not the game's
            const big = hostile.find((c) => c.id === 'hostile_zeros_513_kb').bytes;
            check(core.readReport(big, 'game').json.loaded === false, 'in the game profile the 513 KB file is read, and is still no map');
            // a file of a library map beyond a new map's limits is read in the game profile (the originals are within them: nothing to show), and a 64 MiB buffer is refused or answered, never fatal
            const huge = new Uint8Array(40 * 1024 * 1024);
            const hugeRead = core.readReport(huge);
            check(hugeRead.status === 0 && hugeRead.json.loaded === false && hugeRead.json.findings[0].code === 'file_too_large', 'a 40 MB buffer is refused as too large');
            // and the module still answers as before
            check(core.readReport(library[0].bytes).text === text(library[0].id + '.read.json'), 'the module answers as before');
        });


        await runCase('E the profiles: the limits are the code\'s, the Extended profile reads both kinds, a module older than the page is told apart', () => {
            for (const name of ['new_map', 'game', 'extended']) {
                const l = core.limits(name);
                check(l.status === 0 && l.text === text('limits_' + name + '.json'), name + ': the limits are the native ones, byte for byte');
                check(l.json.profile === name, name + ': named');
            }
            check(core.limits().json.profile === 'new_map', 'the default profile is the new map\'s');
            check(core.limits('extended').json.extended.rows === 1024 && core.limits('extended').json.extended.cells === 262144 && core.limits('extended').json.classic.rows === 256, 'the Extended profile: 1,024 a side, 262,144 cells, a Classic map keeps 256');
            check(core.limits('new_map').json.classic.rows === 90 && core.limits('new_map').json.extended === null, 'a new Classic map: 90 rows, no Extended map');
            check(core.limits('bogus').status === 1 && core.checkMap(new Uint8Array(0), 'bogus').status === 1 && core.convertToClassic(new Uint8Array(0), 'bogus').status === 1 && core.needsExtended(new Uint8Array(0), 'bogus').status === 1, 'an unknown profile is refused');
            check(api.EXPECTED_INTERFACE === 4 && api.moduleVersion(module) === 4 && api.moduleVersion({}) === 0 && api.moduleVersion({ _lvl_interface_version: () => 1 }) === 1 && api.moduleVersion(null) === 0, 'the version a page can ask for before it wraps a module');
            const tiny = library.find((c) => c.id === 'tiny').bytes;
            const up = core.convertToExtended(tiny);
            check(up.status === 0 && up.json.ok === true, 'tiny goes up');
            const read9 = core.readReport(up.bytes, 'extended');
            check(read9.status === 0 && read9.json.loaded === true && read9.json.version === 9 && read9.json.profile === 'extended', 'the Extended profile reads a version 9 file');
            check(core.readReport(up.bytes, 'new_map').json.loaded === false && core.readReport(up.bytes, 'game').json.loaded === false, 'the Classic profiles refuse it, as before');
            check(core.rewrite(up.bytes, 'extended').status === 0 && Buffer.compare(Buffer.from(core.rewrite(up.bytes, 'extended').bytes), Buffer.from(up.bytes)) === 0, 'a version 9 file is its own canonical rewrite');
            check(core.rewrite(up.bytes).status === 1, 'a Classic place cannot rewrite it');
            check(core.sameLevel(up.bytes, up.bytes, 'extended').json.same === true && core.sameLevel(tiny, up.bytes, 'extended').json.same === false, 'two kinds are two levels');
            check(core.blank(200, 200, 485, 6, 10, 'extended').status === 0 && core.blank(257, 10, 485, 6, 10, 'extended').status === 1, 'a Classic blank map under the Extended profile keeps its 256 a side');
        });

        await runCase('E the check by kind: the library as Classic maps and as Extended maps', () => {
            for (const c of library) {
                const m = core.checkMap(c.bytes);
                check(m.status === 0 && m.json.kind === 'classic' && m.text === text(c.id + '.map.json'), c.id + ': the check by kind is the native one, byte for byte');
                check(m.json.works === core.checkOriginal(c.bytes).json.works_in_original_game && m.json.badge === '', c.id + ': a Classic map works as the original-safe profile says, with no badge');
                const up = core.convertToExtended(c.bytes);
                const e = core.checkMap(up.bytes, 'extended');
                check(e.status === 0 && e.json.kind === 'extended' && e.json.badge === 'New version only' && e.text === text(c.id + '.extended.map.json'), c.id + ': the Extended map\'s check is the native one, byte for byte');
                check(e.json.findings.some((f) => f.code === 'not_for_original' && f.severity === 'note'), c.id + ': not for the original, as a note');
                const here = core.checkMap(up.bytes);
                check(here.json.judged === false && here.json.findings.some((f) => f.code === 'extended_not_accepted_here') && here.json.badge === 'New version only', c.id + ': a place for Classic maps does not judge it');
            }
            for (const c of hostile) {
                const m = core.checkMap(c.bytes, 'extended');
                check(m.status === 0 && m.json.works === false, c.id + ': a file that is no map does not work');
            }
        });

        await runCase('E the conversions: up and back is the canonical file, a copy that is another file, the reasons and notes with their codes', () => {
            const index = Object.fromEntries(text('kinds_index.txt').split('\n').filter(Boolean).map((l) => { const f = l.split('\t'); return [f[0], f]; }));
            for (const c of library) {
                const up = core.convertToExtended(c.bytes);
                check(up.status === 0 && up.json.ok === true && up.json.from === 'classic' && up.json.to === 'extended' && up.text === text(c.id + '.to_extended.json'), c.id + ': the conversion is the native one, byte for byte');
                check(up.bytes.length === up.json.bytes && String(up.bytes.length) === index[c.id][1] && fnv(up.bytes) === index[c.id][2], c.id + ': the Extended file is the native one (size and FNV-1a)');
                check(up.bytes[0] === 9 && Buffer.compare(Buffer.from(up.bytes), Buffer.from(c.bytes)) !== 0, c.id + ': version 9, another file');
                const again = core.convertToExtended(up.bytes);
                check(again.json.from === 'extended' && Buffer.compare(Buffer.from(again.bytes), Buffer.from(up.bytes)) === 0, c.id + ': an Extended map converts to itself');
                const back = core.convertToClassic(up.bytes);
                check(back.status === 0 && back.json.ok === true && back.text === text(c.id + '.to_classic.json'), c.id + ': the way back is the native one, byte for byte');
                check(String(back.bytes.length) === index[c.id][3] && fnv(back.bytes) === index[c.id][4], c.id + ': the Classic file is the native one');
                const rw = core.rewrite(c.bytes);
                check(rw.status === 0 && Buffer.compare(Buffer.from(rw.bytes), Buffer.from(back.bytes)) === 0, c.id + ': up and back is the canonical file');
                check(core.needsExtended(up.bytes, 'extended').json.needs_extended === false && core.needsExtended(c.bytes).json.needs_extended === false, c.id + ': it needs nothing');
            }
            const refused = core.convertToExtended(Uint8Array.of(8));
            check(refused.status === 1 && refused.bytes === undefined && /does not load/.test(refused.error), 'a file that does not load is not converted: ' + refused.error);
            check(core.convertToClassic(new Uint8Array(0)).status === 1 && core.convertToExtended('abc').status === 1, 'nor is something that is no bytes');
            // the reasons: 95 rows (the wall at row 90), 300 x 400 with a description of 100 bytes
            const tall = core.blankExtended(95, 20, 485, 6, 10).bytes;
            const down = core.convertToClassic(tall);
            check(down.status === 0 && down.json.ok === false && down.bytes === undefined && codesOf(down.json.reasons) === 'rows_beyond_wall' && down.text === text('to_classic_95x20.json'), 'rows_beyond_wall: ' + down.text);
            const needs = core.needsExtended(tall, 'extended');
            check(needs.status === 0 && needs.json.needs_extended === true && needs.text === text('needs_extended_95x20.json'), 'needs_extended says why, as the native core does');
            const wide = extendedWith(core.blankExtended(300, 400, 485, 6, 10).bytes, 'x'.repeat(100), 1);
            const wideDown = core.convertToClassic(wide);
            check(wideDown.status === 0 && codesOf(wideDown.json.reasons) === 'columns_beyond_classic,rows_beyond_classic,rows_beyond_wall,description_too_long' && wideDown.text === text('to_classic_300x400.json'), 'every reason, in the writer\'s order: ' + codesOf(wideDown.json.reasons));
            // the notes of a conversion that works
            const noted = extendedWith(core.blankExtended(40, 40, 485, 6, 10).bytes, 'd'.repeat(30), 1);
            const notedDown = core.convertToClassic(noted);
            check(notedDown.status === 0 && notedDown.json.ok === true && codesOf(notedDown.json.notes) === 'description_30_bytes,rules_extended_dropped' && notedDown.bytes[0] === 8 && notedDown.text === text('to_classic_40x40_noted.json'), 'the notes: ' + codesOf(notedDown.json.notes));
            // the place: 10,800 cells are a Classic map, not a new one
            const big = core.blankExtended(90, 120, 485, 6, 10).bytes;
            const bigNew = core.convertToClassic(big, 'new_map');
            check(bigNew.json.ok === false && codesOf(bigNew.json.reasons) === 'beyond_place_limits' && core.convertToClassic(big, 'game').json.ok === true, 'beyond_place_limits for a new Classic map only');
            const bigNoted = extendedWith(big, 'd'.repeat(30), 0);
            check(codesOf(core.convertToClassic(bigNoted, 'new_map').json.notes) === '' && codesOf(core.convertToClassic(bigNoted, 'game').json.notes) === 'description_30_bytes', 'a copy that the place refuses tells no note');
            check(core.needsExtended(big, 'new_map').json.needs_extended === true && core.needsExtended(big, 'game').json.needs_extended === false, 'the place decides');
        });

        await runCase('E the blank Extended map, and a map of 512 x 512', () => {
            const cases = text('blank_extended_index.txt').split('\n').filter(Boolean).map((line) => {
                const f = line.split('\t');
                const m = /^blank_extended_(\d+)x(\d+)_g(\d+)_e(\d+)_m(\d+)$/.exec(f[0]);
                return { id: f[0], args: m.slice(1).map(Number), status: Number(f[1]), size: f[2], tail: f[3] };
            });
            for (const c of cases) {
                const started = Date.now();
                const r = core.blankExtended.apply(null, c.args);
                check(r.status === c.status, c.id + ': status ' + c.status);
                if (c.status !== 0) { check(r.error === c.tail, c.id + ': refused with the native sentence'); continue; }
                check(String(r.bytes.length) === c.size && fnv(r.bytes) === c.tail, c.id + ': the bytes are the native blank map\'s');
                const read = core.readReport(r.bytes, 'extended');
                check(read.json.loaded && read.json.version === 9 && read.json.rows === c.args[0] && read.json.columns === c.args[1], c.id + ': reads back as asked');
                const m = core.checkMap(r.bytes, 'extended');
                check(m.json.kind === 'extended' && m.json.fits_the_place === true && m.json.works === false && !m.json.findings.some((f) => f.code === 'rows_beyond_wall'), c.id + ': an Extended map with no hill yet, no wall');
                if (c.args[0] * c.args[1] <= 120000) {
                    check(read.text === text(c.id + '.read.json') && m.text === text(c.id + '.map.json'), c.id + ': the report and the check are the native ones');
                }
                const rw = core.rewrite(r.bytes, 'extended');
                check(rw.status === 0 && Buffer.compare(Buffer.from(rw.bytes), Buffer.from(r.bytes)) === 0, c.id + ': its own canonical file');
                check(Date.now() - started < 20000, c.id + ': in reasonable time');
            }
            for (const bad of [[NaN, 1, 485, 6, 10], [1.5, 1, 485, 6, 10], [-1, 1, 485, 6, 10], ['3', 3, 485, 6, 10], [1, 1, 485, 6, Infinity]]) {
                check(core.blankExtended.apply(null, bad).status === 1, 'blankExtended(' + String(bad) + ') is refused');
            }
            // the largest map goes through every call, and the module keeps nothing of it
            const largest = core.blankExtended(512, 512, 485, 6, 10).bytes;
            let grown = 0;
            for (let i = 0; i < 2; i++) {
                check(core.checkMap(largest, 'extended').json.kind === 'extended', '512 x 512: checked');
                const wall = core.convertToClassic(largest);
                check(wall.json.ok === false && wall.bytes === undefined, '512 x 512: not a Classic map');
                const upAgain = core.convertToExtended(largest);
                check(upAgain.json.ok === true && Buffer.compare(Buffer.from(upAgain.bytes), Buffer.from(largest)) === 0, '512 x 512: converts to itself');
                check(core.needsExtended(largest, 'extended').json.needs_extended === true, '512 x 512: needs to be Extended');
                check(core.readReport(largest, 'extended').json.loaded === true, '512 x 512: read');
                check(core.readReport(largest, 'new_map').json.findings[0].code === 'file_too_large', '512 x 512: a Classic place refuses it by its size');
                if (i === 0) grown = module.HEAPU8.length;
            }
            check(module.HEAPU8.length === grown, 'the second round of the largest map takes no more memory (' + grown + ' bytes) than the first');
            check(module.HEAPU8.length <= 128 * 1024 * 1024, 'the module held the largest map in at most 128 MB (' + module.HEAPU8.length + ' bytes; it may grow to 256 MB)');
        });

        await runCase('E hostile Extended files: a report that says it, no throw', () => {
            const tinyUp = core.convertToExtended(library.find((c) => c.id === 'tiny').bytes).bytes;
            const expected = Object.fromEntries(text('hostile_ext_index.txt').split('\n').filter(Boolean).map((l) => { const f = l.split('\t'); return [f[0], f]; }));
            for (const c of hostileExtendedCases(tinyUp)) {
                const map = core.checkMap(c.bytes, 'extended');
                const read = core.readReport(c.bytes, 'extended');
                const down = core.convertToClassic(c.bytes);
                check(map.status === 0 && map.json.works === false && map.text === text(c.id + '.map.json'), c.id + ': it does not work, as the native check says');
                check(read.status === 0 && read.json.loaded === false && read.text === text(c.id + '.read.json'), c.id + ': it does not load, as the native report says');
                check(down.status === 1 && down.error === expected[c.id][2] && core.convertToExtended(c.bytes).status === 1 && core.needsExtended(c.bytes, 'extended').status === 1, c.id + ': it is not converted, the native sentence');
            }
        });

        await runCase('F the properties: the module sets them as the native core does; the values a file has give the same bytes; the edits are the pieces that changed', () => {
            const profiles = ['new_map', 'game', 'extended'];
            const lines = text('properties_index.txt').split('\n').filter(Boolean).map((l) => l.split('\t'));
            check(lines.length >= 27, 'the stored cases: ' + lines.length);
            for (const f of lines) {
                const id = f[0], ext = f[1].startsWith('ext:');
                let bytes = bytesOf(ext ? f[1].slice(4) : f[1]);
                if (ext) bytes = core.convertToExtended(bytes).bytes;
                const r = core.setProperties(bytes, Uint8Array.from(Buffer.from(f[2], 'hex')), Number(f[3]), Number(f[4]), Number(f[5]), profiles[Number(f[6])]);
                check(r.status === Number(f[7]), id + ': status ' + f[7]);
                if (Number(f[7]) !== 0) { check(r.error === f[9], id + ': the native refusal sentence'); continue; }
                check(r.text === text('properties.' + id + '.json'), id + ': the answer is the native answer, byte for byte');
                const made = r.bytes || new Uint8Array(0);
                check(String(made.length) === f[8] && fnv(made) === f[9], id + ': the file is the native file (size and FNV-1a)');
                check(r.json.ok === (made.length > 0), id + ': bytes come with ok, and only then');
            }
            // the string form of the description is its UTF-8 bytes
            const tiny = library.find((c) => c.id === 'tiny').bytes;
            const asText = core.setProperties(tiny, 'Café, tiny ants', 7, 3, 63, 'extended'), asBytes = core.setProperties(tiny, Uint8Array.from(Buffer.from('Café, tiny ants', 'utf8')), 7, 3, 63, 'extended');
            check(asText.status === 0 && asText.json.ok && asText.json.description_bytes === 16 && Buffer.compare(Buffer.from(asText.bytes), Buffer.from(asBytes.bytes)) === 0, 'a string is stored as UTF-8 (the e acute takes 2 bytes)');
            // a refusal is an answer, with the checker's sentence; the limits are the core's
            const long30 = core.setProperties(tiny, 'x'.repeat(30), 12, 9, 0x7FFE), long31 = core.setProperties(tiny, 'x'.repeat(31), 12, 9, 0x7FFE);
            check(long30.json.ok === true && long30.json.warnings.length === 1 && long30.json.warnings[0].code === 'description_too_long' && long30.json.description_limit === 29, 'a Classic description of 30 bytes is written, with the checker\'s warning');
            check(long31.status === 0 && long31.json.ok === false && long31.bytes === undefined && long31.json.reasons[0].code === 'description_too_long' && /^the description is 31 bytes long: at most 29 fit/.test(long31.json.reasons[0].message), 'a Classic description of 31 bytes is refused, in the checker\'s words');
            const up = core.convertToExtended(tiny).bytes;
            const long256 = core.setProperties(up, 'x'.repeat(256), 12, 9, 0x7FFE, 'extended');
            check(long256.json.ok === false && long256.json.reasons[0].message === 'the description is 256 bytes long: an Extended map holds at most 255' && core.setProperties(up, 'x'.repeat(255), 12, 9, 0x7FFE, 'extended').json.ok === true, 'an Extended description: 255 fit, 256 are refused, in the checker\'s words');
            check(core.setProperties(tiny, 'x', 1, 1, 1, 'bogus').status === 1 && core.setProperties(tiny, 'x', -1, 1, 1).status === 1 && core.setProperties(tiny, 'x', 1.5, 1, 1).status === 1 && core.setProperties(tiny, 5, 1, 1, 1).status === 1 && core.setProperties(tiny, 'x', 1, 1, 1, 'extended').status === 0, 'bad arguments are refused, nothing is thrown');
            check(core.setProperties(up, 'x', 1, 1, 1).status === 1 && /does not load/.test(core.setProperties(up, 'x', 1, 1, 1).error), 'an Extended map needs the Extended profile');
            // the whole library, as Classic maps and as Extended maps: the values the file has (read by the core) set again give the same bytes; other values change only the pieces the edits name
            let count = 0;
            for (const dir of ['Original-Ants/Maps', 'Community-Maps']) {
                for (const name of fs.readdirSync(path.join(ROOT, dir)).filter((n) => /\.lvl$/i.test(n)).sort()) {
                    const classic = bytesOf(dir + '/' + name);
                    const extended = core.convertToExtended(classic).bytes;
                    for (const [bytes, profile] of [[classic, 'extended'], [extended, 'extended']]) {
                        const read = core.readReport(bytes, profile).json;
                        const same = core.setProperties(bytes, Uint8Array.from(Buffer.from(read.description, 'latin1')), read.minutes, read.eggs, read.default_type_tile, profile);
                        check(same.status === 0 && same.json.ok && same.json.changed === false && same.json.edits.length === 0 && Buffer.compare(Buffer.from(same.bytes), Buffer.from(bytes)) === 0, dir + '/' + name + ': the values it has give the same bytes');
                        const set = core.setProperties(bytes, 'Café test', read.minutes === 7 ? 8 : 7, read.eggs === 3 ? 4 : 3, 64, profile);
                        if (set.json.reasons.length) { check(set.json.reasons.length === 1 && set.json.reasons[0].code === 'eggs_not_in_file', name + ': only a file without an egg word refuses the eggs'); continue; }
                        let shift = 0, from = 0;
                        const parts = [];
                        for (const e of set.json.edits) {
                            parts.push(bytes.subarray(from, e.at), set.bytes.subarray(e.at + shift, e.at + shift + e.inserted));
                            from = e.at + e.removed; shift += e.inserted - e.removed;
                        }
                        parts.push(bytes.subarray(from));
                        check(Buffer.compare(Buffer.concat(parts), Buffer.from(set.bytes)) === 0 && set.bytes.length === bytes.length + shift, dir + '/' + name + ': the old file with the edits put in is the new file');
                        const after = core.readReport(set.bytes, profile).json;
                        check(after.loaded && after.description === 'CafÃ© test' && after.minutes === (read.minutes === 7 ? 8 : 7) && after.default_type_tile === 64 && after.rows === read.rows && after.columns === read.columns, name + ': it reads back as asked');
                        count++;
                    }
                }
            }
            check(count >= 1000, 'maps set: ' + count);
        });

        await runCase('G the crop: the module cuts a window as the native core does; the whole map is its canonical file; an object is whole or gone; a refusal is a sentence', () => {
            const profiles = ['new_map', 'game', 'extended'];
            const lines = text('crop_index.txt').split('\n').filter(Boolean).map((l) => l.split('\t'));
            check(lines.length >= 50, 'the stored cases: ' + lines.length);
            let cutCases = 0;
            for (const f of lines) {
                const id = f[0], ext = f[1].startsWith('ext:');
                let bytes = bytesOf(ext ? f[1].slice(4) : f[1]);
                if (ext) bytes = core.convertToExtended(bytes).bytes;
                const r = core.crop(bytes, Number(f[2]), Number(f[3]), Number(f[4]), Number(f[5]), profiles[Number(f[6])] || 'bogus');
                check(r.status === Number(f[7]), id + ': status ' + f[7]);
                if (Number(f[7]) !== 0) { check(r.error === f[9], id + ': the native refusal sentence'); continue; }
                check(r.text === text('crop.' + id + '.json'), id + ': the answer is the native answer, byte for byte');
                const made = r.bytes || new Uint8Array(0);
                check(String(made.length) === f[8] && fnv(made) === f[9], id + ': the file is the native file (size and FNV-1a)');
                check(r.json.ok === (made.length > 0), id + ': bytes come with ok, and only then');
                if (r.json.removed_cells > 0) cutCases++;
            }
            check(cutCases >= 3, 'stored cases that cut objects: ' + cutCases);
            // the whole map is its canonical file, as a Classic and as an Extended map, on the whole library
            let whole = 0, parts = 0;
            for (const dir of ['Original-Ants/Maps', 'Community-Maps']) {
                for (const name of fs.readdirSync(path.join(ROOT, dir)).filter((n) => /\.lvl$/i.test(n)).sort()) {
                    const classic = bytesOf(dir + '/' + name);
                    for (const bytes of [classic, core.convertToExtended(classic).bytes]) {
                        const read = core.readReport(bytes, 'extended').json;
                        const all = core.crop(bytes, 0, 0, read.rows, read.columns, 'extended');
                        const canonical = core.sameLevel(bytes, all.bytes || new Uint8Array(0), 'extended');
                        check(all.status === 0 && all.json.ok && all.json.removed_cells === 0 && all.json.removed_records === 0 && canonical.status === 0 && canonical.json.same === true, dir + '/' + name + ': the whole map is the same level');
                        whole++;
                        // a window of the middle: the part reads back with the size of the window, keeps its kind, and the check can judge it
                        const half = core.crop(bytes, read.rows >> 2, read.columns >> 2, Math.max(1, read.rows >> 1), Math.max(1, read.columns >> 1), 'extended');
                        const back = half.status === 0 && half.json.ok ? core.readReport(half.bytes, 'extended').json : null;
                        check(back && back.loaded && back.rows === Math.max(1, read.rows >> 1) && back.columns === Math.max(1, read.columns >> 1) && back.version === read.version && core.checkMap(half.bytes, 'extended').status === 0, dir + '/' + name + ': the middle half reads back');
                        parts++;
                    }
                }
            }
            check(whole >= 1000 && parts >= 1000, 'maps cropped: ' + whole + ', ' + parts);
            // the call is the wrapper's: whole numbers only, a profile that exists, every refusal a sentence
            const tiny = library.find((c) => c.id === 'tiny').bytes;
            check(core.crop(tiny, 3, 4, 20, 18, 'bogus').status === 1 && core.crop(tiny, -1, 4, 20, 18).status === 1 && core.crop(tiny, 3, 4.5, 20, 18).status === 1 && core.crop(tiny, 3, 4, '20', 18).status === 1 &&
                core.crop(tiny, 3, 4, 20, 4294967296).status === 1 && core.crop(tiny, 3, 4, NaN, 18).status === 1 && core.crop([1, 2], 0, 0, 1, 1).status === 1, 'numbers that are not whole numbers of a dword are refused by the wrapper');
            check(/whole numbers/.test(core.crop(tiny, -1, 4, 20, 18).error) && /whole numbers/.test(core.crop(tiny, 3, 4, 20.5, 18).error) && /whole numbers/.test(core.crop(tiny, 3, 4, 20, 4294967296).error), 'the wrapper says why a number is refused');
            const none = core.crop(tiny, 3, 4, 0, 18), below = core.crop(tiny, 12, 0, 20, 31), wrap = core.crop(tiny, 4294967295, 0, 2, 1);
            check(none.status === 1 && /at least one row and one column/.test(none.error) && below.status === 1 && /is not inside the map, which has 31 rows and 31 columns/.test(below.error) && wrap.status === 1, 'an empty window and one outside the map are refused with a sentence: ' + below.error);
            const up = core.convertToExtended(tiny).bytes;
            check(core.crop(up, 3, 4, 20, 18).status === 1 && /does not load/.test(core.crop(up, 3, 4, 20, 18, 'new_map').error), 'an Extended map needs the Extended profile');
            // a part of a large Extended map is turned into a Classic map by the call that exists for it
            const wide = core.blankExtended(300, 400, 485, 6, 10).bytes;
            const fit = core.crop(wide, 100, 100, 90, 111, 'extended'), tall = core.crop(wide, 100, 100, 100, 100, 'extended');
            check(fit.json.ok && core.convertToClassic(fit.bytes).json.ok && tall.json.ok && core.convertToClassic(tall.bytes).json.reasons.map((r) => r.code).join() === 'rows_beyond_wall', 'a 90 x 111 part becomes a Classic map, a 100 x 100 part is still too tall, as the core says');
            // the source is never touched
            const copy = wide.slice();
            core.crop(wide, 1, 1, 50, 50, 'extended');
            check(Buffer.compare(Buffer.from(copy), Buffer.from(wide)) === 0, 'the bytes that were given are as they were');
        });

        await runCase('D memory: every buffer is given back (a fresh module, a baseline before anything large)', async () => {
            const fresh = await createLvlCore();
            const c2 = api.wrap(fresh);
            const bomber = library.find((c) => c.id === 'bomberworld').bytes;
            const round = () => {
                c2.readReport(bomber); c2.checkOriginal(bomber); const rw = c2.rewrite(bomber); c2.sameLevel(bomber, rw.bytes);
                c2.blank(90, 90, 485, 6, 10);
                const small = c2.convertToExtended(library[0].bytes); c2.checkMap(small.bytes, 'extended'); c2.convertToClassic(small.bytes); c2.needsExtended(small.bytes, 'extended');
                c2.convertToClassic(c2.blankExtended(95, 20, 485, 6, 10).bytes); c2.limits('extended'); c2.checkMap(bomber);
                c2.setProperties(bomber, 'A new text', 7, 3, 63); c2.setProperties(small.bytes, 'x'.repeat(256), 7, 3, 63, 'extended'); c2.setProperties(small.bytes, 'Caf\u00e9', 7, 3, 63, 'extended');
                c2.crop(small.bytes, 3, 4, 20, 18, 'extended'); c2.crop(bomber, 5, 5, 60, 60, 'extended'); c2.crop(small.bytes, 0, 0, 0, 1, 'extended');
                for (const c of hostile.filter((h) => h.bytes.length < 200000)) { c2.readReport(c.bytes); c2.rewrite(c.bytes); }
            };
            round(); round();                                              // warm-up: the allocator settles
            const baseline = fresh.HEAPU8.length;
            const probe = () => { const p = fresh._lvl_alloc(1 << 20); fresh._lvl_free(p); return p; };
            const firstProbe = probe();
            for (let i = 0; i < 300; i++) round();                         // (about 8,000 calls: a leak of 1 KB a call would be 8 MB)
            check(fresh.HEAPU8.length === baseline, 'the heap does not grow over 300 rounds (' + baseline + ' bytes)');
            check(probe() === firstProbe, 'a 1 MB buffer is given the same place before and after (nothing was left in its way)');
            // a freed buffer is reused: the same size comes back at the same place
            const p = fresh._lvl_alloc(4096); fresh._lvl_free(p);
            const q = fresh._lvl_alloc(4096); fresh._lvl_free(q);
            check(p === q, 'a freed buffer is reused');
        });

        await runCase('wrapper: a module of another interface version is refused, a trap kills the core', () => {
            let threw = '';
            try { api.wrap({ _lvl_interface_version: () => 1 }); } catch (e) { threw = e.message; }
            check(/interface version 1/.test(threw) && /needs 4/.test(threw), 'a module of interface version 1 (the first one) is refused with a sentence: ' + threw);
            threw = '';
            try { api.wrap({ _lvl_interface_version: () => 2 }); } catch (e) { threw = e.message; }
            check(/interface version 2/.test(threw) && /needs 4/.test(threw) && /reload the page/.test(threw), 'a cached module of interface version 2 (the one before the properties) is refused with a sentence that says to reload: ' + threw);
            threw = '';
            try { api.wrap({ _lvl_interface_version: () => 3 }); } catch (e) { threw = e.message; }
            check(/interface version 3/.test(threw) && /needs 4/.test(threw) && /reload the page/.test(threw), 'a cached module of interface version 3 (the one before the crop) is refused with a sentence that says to reload: ' + threw);
            threw = '';
            try { api.wrap({}); } catch (e) { threw = e.message; }
            check(/interface version 0/.test(threw), 'a module with no version call is refused');
            // a module whose reader traps (wasm RuntimeError): status 2 once, then the core is dead
            const real = module;
            const broken = Object.create(real);
            broken._lvl_read_report = () => { throw new WebAssembly.RuntimeError('unreachable'); };
            const c3 = api.wrap(broken);
            check(c3.isDead() === false, 'alive at first');
            const first = c3.readReport(new Uint8Array(10));
            check(first.status === 2 && /unreachable/.test(first.error), 'the trap is a status 2 with its sentence: ' + first.error);
            check(c3.isDead() === true, 'the core is dead after a trap');
            for (const r of [c3.readReport(new Uint8Array(1)), c3.checkOriginal(new Uint8Array(1)), c3.rewrite(new Uint8Array(1)), c3.sameLevel(new Uint8Array(1), new Uint8Array(1)), c3.blank(31, 31, 485, 6, 10),
                c3.checkMap(new Uint8Array(1)), c3.convertToExtended(new Uint8Array(1)), c3.convertToClassic(new Uint8Array(1)), c3.needsExtended(new Uint8Array(1)), c3.blankExtended(31, 31, 485, 6, 10), c3.limits(), c3.setProperties(new Uint8Array(1), 'x', 1, 1, 1), c3.crop(new Uint8Array(1), 0, 0, 1, 1)]) {
                check(r.status === 2 && /stopped/.test(r.error) && /create the module again/.test(r.error), 'every later call fails clearly: ' + r.error);
            }
            // a trap in a conversion, in the check by kind, in the limits: the same
            for (const [name, call] of [['_lvl_convert_to_extended', (c) => c.convertToExtended(new Uint8Array(4))], ['_lvl_convert_to_classic', (c) => c.convertToClassic(new Uint8Array(4))], ['_lvl_check_map', (c) => c.checkMap(new Uint8Array(4))],
                ['_lvl_needs_extended', (c) => c.needsExtended(new Uint8Array(4))], ['_lvl_blank_extended', (c) => c.blankExtended(31, 31, 485, 6, 10)], ['_lvl_limits', (c) => c.limits()],
                ['_lvl_set_properties', (c) => c.setProperties(new Uint8Array(4), 'x', 1, 1, 1)], ['_lvl_crop', (c) => c.crop(new Uint8Array(4), 0, 0, 1, 1)]]) {
                const trap = Object.create(real);
                trap[name] = () => { throw new WebAssembly.RuntimeError('unreachable'); };
                const cc = api.wrap(trap);
                const r = call(cc);
                check(r.status === 2 && /unreachable/.test(r.error) && cc.isDead(), name + ': a trap is a status 2 and the core is dead');
            }
            // a module that has no memory left: the null block is "out of memory", not a trap
            const noMemory = Object.create(real);
            noMemory._lvl_read_report = () => 0;
            const c4 = api.wrap(noMemory);
            const none = c4.readReport(new Uint8Array(4));
            check(none.status === 2 && none.error === 'out of memory' && !c4.isDead(), 'a call with no memory left answers out of memory and the core lives');
        });
    });
}

main().then(() => {
    console.log('\n' + checks + ' checks, ' + failures + ' failures');
    process.exit(failures === 0 ? 0 : 1);
}, (e) => {
    console.log('FAILED: ' + (e && e.stack ? e.stack : e));
    process.exit(1);
});
