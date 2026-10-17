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

// FNV-1a, 64 bits, 16 hex digits (the native test has the same)
function fnv(bytes) {
    let h = 0xcbf29ce484222325n;
    for (let i = 0; i < bytes.length; i++) {
        h ^= BigInt(bytes[i]);
        h = (h * 0x100000001b3n) & 0xFFFFFFFFFFFFFFFFn;
    }
    return h.toString(16).padStart(16, '0');
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

function libraryCases() {
    return text('index.txt').split('\n').filter(Boolean).map((line) => {
        const f = line.split('\t');
        return { id: f[0], path: f[1], rewriteStatus: Number(f[2]), rewriteSize: f[3], rewriteFnv: f[4], bytes: bytesOf(f[1]) };
    });
}

function main() {
    const o = parseArgs(process.argv.slice(2));
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
            check(core.interfaceVersion === 1, 'interface version 1');
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
            // memory: the same calls again and again must not make the module's memory grow
            const bomber = library.find((c) => c.id === 'bomberworld').bytes;
            const round = () => {
                core.readReport(bomber); core.checkOriginal(bomber); const rw = core.rewrite(bomber); core.sameLevel(bomber, rw.bytes);
                core.blank(100, 100, 485, 6, 10);
                for (const c of hostile) { core.readReport(c.bytes); core.rewrite(c.bytes); }
            };
            round(); round();
            const settled = module.HEAPU8.length;
            for (let i = 0; i < 40; i++) round();
            check(module.HEAPU8.length === settled, 'the module\'s memory does not grow over 40 more rounds (' + settled + ' bytes): every buffer is given back');
            // and the module still answers as before
            check(core.readReport(library[0].bytes).text === text(library[0].id + '.read.json'), 'the module answers as before');
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
