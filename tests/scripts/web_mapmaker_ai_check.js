#!/usr/bin/env node
// Connect an AI, the page's side (web/front/mapmaker_ai_tools.js and mapmaker_ai.js; docs/MAP_AI.md) held to real maps, with the real reader, editor, blocks, objects and dialogs, a made-up map core
// and a small fake of the DOM, of the socket and of fetch (no browser: the real core's verdicts are the browser part of tests/scripts/test_web_mapmaker.sh):
//
//   node tests/scripts/web_mapmaker_ai_check.js [--front DIR] [--tools NAME,NAME,...]
//
//   A  the table of tools: a handler for each name that the hub has (--tools: the names of hub/aitools.py) and for no other, a name that is not a tool (submit, __proto__, constructor) is refused
//   B  looking: the facts of a map, a window of it as text (hills, ants, food in it, cut by the edge, outside is refused), the tiles, the guides, the library, a picture of the right size that
//      shrinks until it fits and is refused when it never does
//   C  changing: each change is ONE step of the history and Undo takes all of it back; a refusal changes nothing; an old `base` is refused with the new version; the person's unsaved work is kept
//      first, once for a session, and not changing anything when it cannot be kept; a stroke that is going on is waited for; more than 5,000 cells is refused and undone; the AI undoes its own
//      steps and never the person's
//   D  the sheet, the strip and the socket on a fake page: the keys and what a name that holds markup does, a key shown once and gone with the sheet, the calls that the page makes (the token, the
//      body, nothing else), the switch that opens the socket and Stop that closes it, a call that is run and answered, a frame that is not a call that is ignored, nothing is stored
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
// the names of the hub's tools: given, or read from hub/aitools.py (the table that the hub and the page agree on)
const HUB_TOOLS = argv.includes('--tools') ? argv[argv.indexOf('--tools') + 1].split(',').sort() : (() => {
    const file = path.join(ROOT, 'hub', 'aitools.py');
    return fs.existsSync(file) ? Array.from(fs.readFileSync(file, 'utf8').matchAll(/^\s*Tool\("([a-z_]+)"/gm), (m) => m[1]).sort() : null;
})();
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Edit = load('mapmaker_edit.js'), Blocks = load('mapmaker_blocks.js'), Palette = load('mapmaker_palette.js'), Dialog = load('mapmaker_dialog.js');
const Tools = load('mapmaker_ai_tools.js'), Part = load('mapmaker_ai.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }
function map(rel) { const f = File.parse(new Uint8Array(fs.readFileSync(path.join(ROOT, rel)))); if (!f.ok) throw new Error(rel); return f; }

// ---- the page's side of the tools, made up ----
function rig(rel, over) {
    over = over || {};
    const sim = { saved: false, copies: [], kept: [], notes: [], acts: [], changed: 0, stroking: 0, fail: '', fetched: [] };
    const file0 = map(rel || 'Original-Ants/Maps/TINY.LVL');
    const ed = Edit.create(file0, { rebase: (b) => File.parse(b), erase: Blocks.eraseAt, guard: Blocks.blockedText });
    const core = {
        checkMap: () => ({ status: 0, json: { works: true, headline: 'Works in the original game', kind: 'classic', findings: [{ severity: 'note', message: 'A note that says <b>bold</b>', row: 1, column: 2 }] } }),
        readReport: () => ({ status: 0, json: { loaded: true, description: 'A made-up map', minutes: 12, eggs: 9, default_type_tile: 0x7FFE, version: 8 } }),
        setProperties: () => ({ status: 0, json: { ok: false, reasons: [{ message: 'the description does not fit' }] } })
    };
    let part;
    const env = {
        core: core, limits: { classic: { rows: 90, columns: 90, cells: 8100, openCells: 16000 }, extended: { side: 2048, cells: 4194304, longest: 8192 } }, pictureMax: 4000,
        file: () => ed.file, name: () => 'My test map', editor: () => ed, changed: () => { sim.changed++; part.touch(); }, rebase: (b) => File.parse(b),
        stroking: () => sim.stroking > 0 && sim.stroking--, later: (f) => f(), isSaved: () => sim.saved, numbered: (n) => n + ' 2',
        keep: (name, done) => { sim.kept.push(name); done(sim.fail); }, keepBefore: (done) => { sim.copies.push(1); done(sim.fail); },
        newMap: () => 'A made-up refusal',
        mini: (f, w, h) => ({ w: w, h: h }), makeCanvas: (w, h) => { const c = { w: w, h: h, draws: [], getContext: () => ({ drawImage: (...a) => c.draws.push(a), imageSmoothingEnabled: true }), toDataURL: () => 'data:image/png;base64,' + 'A'.repeat(over.k ? Math.round(c.w * c.h * over.k) : 200) }; return c; },
        library: () => [{ name: 'Treasure', file: 'TREASURE.LVL', kind: 'classic', width: 60, height: 60, minutes: 12, players: 4 }, { name: 'Odd <b>one</b>', file: 'ODD.LVL', kind: 'classic' }],
        fetchMap: (file, done) => { sim.fetched.push(file); done(file === 'TREASURE.LVL' ? '' : 'failed', file === 'TREASURE.LVL' ? new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants/Maps/TREASURE.LVL'))) : null); },
        fetchText: (url, done) => done(url.endsWith('/ants-map-guide/SKILL.md') ? '' : 'failed', 'the guide text'),
        edges: () => null, note: (t) => sim.notes.push(t), onAct: (t) => sim.acts.push(t)
    };
    part = Tools.create(env);
    const run = (name, args) => { let out = null; part.run(name, args || {}, (frame) => { out = frame; }); return out; };
    return { part, run, ed, sim, env, core };
}
const HILLS = (f) => Blocks.facts(f, true).hills;
// the map as it is after its dictionary (a name that a stroke needed stays in the dictionary after an Undo, as it does for the person: it is harmless, and not part of what is compared)
function bytesOf(f) { return Buffer.from(f.bytes.subarray(f.dictAt + 2 + 11 * f.names)).toString('hex'); }
function freeCell(f) { for (let i = 0; i < f.cells; i++) { const r = Math.floor(i / f.columns), c = i % f.columns; if (r > 3 && c > 3 && r < f.rows - 4 && c < f.columns - 4 && f.tile2(r, c) === 0x7FFE && !Blocks.inTheWay(f, i) && f.tile1(r, c) === 485) return [r, c]; } return null; }

// ---- A ----
console.log('RUNNING: A the table of tools');
{
    const r = rig();
    const names = r.part.names();
    if (HUB_TOOLS) equal(names, HUB_TOOLS, 'a handler for each tool of the hub, and for no other');
    check(names.length >= 20, 'the table has its tools');
    for (const bad of ['submit', 'publish', 'download', 'chat', '__proto__', 'constructor', 'hasOwnProperty', 'toString', 'valueOf', '', 'MAP_INFO', 'map_info ']) {
        const out = r.run(bad);
        check(out && out.ok === false && out.error === 'bad_args', bad + ' is not a tool: ' + JSON.stringify(out));
    }
    check(!/submit|publish|download|\/submissions|innerHTML|insertAdjacentHTML|document\.write|\beval\(|new Function|localStorage|sessionStorage|indexedDB|XMLHttpRequest|sendBeacon/.test(fs.readFileSync(path.join(FRONT, 'mapmaker_ai_tools.js'), 'utf8').replace(/\/\/.*$/gm, '')), 'the tools never submit, publish, download, store or write markup');
}

// ---- B ----
console.log('RUNNING: B looking');
{
    const r = rig();
    const info = r.run('map_info');
    check(info.ok && info.result.kind === 'classic' && info.result.rows === 31 && info.result.columns === 31 && info.result.cells === 961, 'map_info: the size');
    equal([info.result.name, info.result.description, info.result.minutes, info.result.eggs, info.result.default_ant], ['My test map', 'A made-up map', 12, 9, 'none'], 'map_info: the facts of the properties');
    check(info.result.check.works && info.result.check.findings[0].message === 'A note that says <b>bold</b>', 'map_info: the checker\u2019s sentences as they are (the hub marks them as typed text)');
    equal(info.result.teams.map((t) => t.team), ['green', 'red', 'blue', 'black'], 'map_info: the four teams');
    check(info.result.teams.every((t) => t.hill === 'whole' && t.ants > 0), 'map_info: every team of TINY has its whole hill and ants');
    equal(info.result.food_piles, 14, 'map_info: the food piles');
    check(typeof info.result.version === 'number' && info.result.version >= 1, 'map_info: a version');

    const f = r.ed.file, region = r.run('get_region', { row: 0, column: 0, rows: 31, columns: 31 });
    check(region.ok && region.result.ground.length === 31 && region.result.ground.every((l) => l.length === 31) && region.result.objects.every((l) => l.length === 31), 'get_region: the whole map is rows x columns letters');
    check(region.result.ground.join('').split('').every((c) => 'gdsmw?'.includes(c)) && region.result.objects.join('').split('').every((c) => '.GRBKPbf=pFOo'.includes(c)), 'get_region: the letters are the legend\u2019s');
    equal(region.result.hills.length, 4, 'get_region: the four hills');
    check(region.result.hills.every((h) => h.whole && h.cells === 16 && f.tile2(h.row, h.column) === Blocks.TEAMS[['green', 'red', 'blue', 'black'].indexOf(h.team)].hill), 'get_region: a hill is where its entrance cell holds its tile');
    equal(region.result.ants.length + region.result.ants_more, f.markers.length, 'get_region: every starting ant');
    equal(region.result.food.length, 14, 'get_region: the food piles with their numbers');
    const cut = r.run('get_region', { row: 25, column: 25, rows: 40, columns: 40 });
    check(cut.ok && cut.result.rows === 6 && cut.result.columns === 6, 'get_region: a window is cut by the map\u2019s edge');
    check(r.run('get_region', { row: 31, column: 0, rows: 3, columns: 3 }).error === 'refused', 'get_region: a window outside the map is refused');
    const one = r.run('get_region', { row: f.markers[0].row, column: f.markers[0].column, rows: 1, columns: 1 });
    check(one.ok && one.result.ants.length >= 1 && one.result.ants[0].team === f.markers[0].team, 'get_region: an ant is listed in the window that holds it');

    const tiles = r.run('list_tiles');
    equal(tiles.result.ground.map((t) => t.id).sort(), [439, 476, 485, 487, 571], 'list_tiles: the five plain grounds');
    check(tiles.result.bigger.length === 29 && tiles.result.food.length > 20 && tiles.result.teams.length === 4 && tiles.result.ants.length === 6 && tiles.result.plants.length === 14, 'list_tiles: the groups');
    equal(Object.keys(r.run('list_tiles', { group: 'plants' }).result).sort(), ['plants', 'version'], 'list_tiles: a group limits the answer');
    equal(r.run('read_guide', { topic: 'guide' }).result, { topic: 'guide', text: 'the guide text' }, 'read_guide: the text');
    equal(r.run('read_guide', { topic: 'tools' }).error, 'refused', 'read_guide: a guide that does not load is refused with a sentence');
    check(r.run('read_guide', { topic: 'nothing' }).error === 'bad_args', 'read_guide: no such topic');
    const lib = r.run('list_library_maps');
    equal(lib.result.maps.map((m) => m.name), ['Treasure', 'Odd <b>one</b>'], 'list_library_maps: the names as they are (the hub marks them as typed text)');
    const viewed = r.run('view_library_map', { name: 'treasure', view: 'region', row: 0, column: 0, rows: 4, columns: 4 });
    check(viewed.ok && viewed.result.rows === 4 && viewed.result.ground.length === 4, 'view_library_map: a window of a library map, the name found without regard to case');
    check(r.run('view_library_map', { name: 'Treasure' }).result.rows === 60, 'view_library_map: its facts');
    const asked = r.sim.fetched.length;
    check(r.run('view_library_map', { name: 'nothing here' }).error === 'refused' && r.sim.fetched.length === asked, 'view_library_map: a name that is not in the library is refused and nothing is fetched for it');
    check(r.run('view_library_map', { name: 'Odd <b>one</b>' }).error === 'refused' && r.sim.fetched[r.sim.fetched.length - 1] === 'ODD.LVL', 'view_library_map: a map that does not load is refused, and only a file of the library\u2019s own list was asked for');
    check(bytesOf(r.ed.file) === bytesOf(f), 'looking changes nothing');

    const pic = r.run('get_picture', {});
    check(pic.ok && pic.result.width === 800 && pic.result.height === 800 && /^A+$/.test(pic.result.png), 'get_picture: the whole map at 800 pixels');
    const win = r.run('get_picture', { row: 2, column: 4, rows: 10, columns: 20, max_px: 400 });
    check(win.ok && win.result.width === 400 && win.result.height === 200 && win.result.rows === 10 && win.result.columns === 20, 'get_picture: a window keeps its shape: ' + JSON.stringify(win.result && [win.result.width, win.result.height]));
    const big = rig(null, { k: 0.006 }), shrunk = big.run('get_picture', { max_px: 1000 });
    check(shrunk.ok && shrunk.result.width < 1000 && shrunk.result.png.length <= 4000, 'get_picture: a picture that is too big for the hub is made smaller until it fits');
    const never = rig(null, { k: 1 }).run('get_picture', {});
    check(never.error === 'too_big', 'get_picture: a picture that never fits is refused');
    check(r.run('get_picture', { row: 99, column: 0, rows: 5, columns: 5 }).error === 'refused', 'get_picture: a window outside the map is refused');
    equal(Tools.pictureSize(60, 30, 1000), { w: 1000, h: 500, k: 1000 / 60 }, 'the size of a picture');
}

// ---- C ----
console.log('RUNNING: C changing');
{
    const steps = (r) => r.ed.history.size().strokes;
    let r = rig();
    const before = bytesOf(r.ed.file);
    const water = r.run('paint', { layer: 'ground', tile: 476, rect: { row: 2, column: 3, rows: 3, columns: 4 } });
    check(water.ok && water.cells === 12 && water.result.changed === 12 && JSON.stringify(water.result.rect) === '{"row":2,"column":3,"rows":3,"columns":4}', 'paint: a rectangle of water: ' + JSON.stringify(water));
    let all = true;
    for (let rr = 2; rr < 5; rr++) for (let c = 3; c < 7; c++) all = all && r.ed.file.tile1(rr, c) === 476;
    check(all && steps(r) === 1, 'paint: every cell, and one step of the history');
    check(r.sim.copies.length === 1, 'the first change of a session keeps the person\u2019s unsaved work first, once');
    r.run('paint', { layer: 'ground', tile: 439, cells: [[10, 10], [11, 11]] });
    check(r.sim.copies.length === 1 && steps(r) === 2 && r.ed.file.tile1(11, 11) === 439, 'paint: listed cells, and no second copy in the session');
    const line = r.run('paint', { layer: 'ground', tile: 571, line: { from: [20, 2], to: [20, 12], brush: 2 } });
    check(line.ok && r.ed.file.tile1(20, 7) === 571 && r.ed.file.tile1(21, 7) === 571, 'paint: a line with a brush');
    const fill = r.run('paint', { layer: 'ground', tile: 439, fill: { row: 25, column: 25 } });
    check(fill.ok && fill.result.changed > 0, 'paint: a fill');
    check(r.run('paint', { layer: 'ground', tile: 476, cells: [[31, 0]] }).error === 'refused' && r.run('paint', { layer: 'ground', tile: 476, line: { from: [0, 0], to: [40, 0] } }).error === 'refused', 'paint: a cell outside the map is refused');
    check(r.run('paint', { layer: 'ground', tile: 1000, cells: [[0, 0]] }).error === 'bad_args' && r.run('paint', { layer: 'objects', tile: 404, cells: [[0, 0]] }).error === 'bad_args' && r.run('paint', { layer: 'objects', tile: 248, cells: [[0, 0]] }).error === 'bad_args', 'paint: a tile that is not for that layer is refused (a plant, a hill)');
    const undone = r.run('undo', { steps: 4 });
    check(undone.ok && undone.result.undone === 4 && bytesOf(r.ed.file) === before, 'undo: the AI\u2019s own steps, all four, give the map back byte for byte');
    check(r.run('undo').error === 'changed_by_person', 'undo: nothing of the AI\u2019s left');
    const again = r.run('redo', { steps: 2 });
    check(again.ok && again.result.redone === 2 && r.ed.file.tile1(2, 3) === 476, 'redo: the AI\u2019s own steps again');

    // a hill is in the way of a brush of objects: the whole call is refused and nothing changes
    r = rig();
    const green = HILLS(r.ed.file);
    const hill = Blocks.hillCells(r.ed.file, 0)[0], hr = Math.floor(hill / 31), hc = hill % 31, snapshot = bytesOf(r.ed.file);
    const blocked = r.run('paint', { layer: 'objects', tile: 302, rect: { row: hr, column: hc, rows: 2, columns: 2 } });
    check(blocked.ok === false && blocked.error === 'refused' && /hill/i.test(blocked.message) && bytesOf(r.ed.file) === snapshot && steps(r) === 0, 'paint: a hill in the way is refused with its sentence and nothing changes: ' + JSON.stringify(blocked));
    const erased = r.run('erase', { rect: { row: hr, column: hc, rows: 4, columns: 4 } });
    check(erased.ok && Blocks.hillCells(r.ed.file, 0).length === 0 && steps(r) === 1, 'erase: a hill goes whole, as one step');
    r.run('undo');
    check(Blocks.hillCells(r.ed.file, 0).length === 16, 'erase: and comes back with Undo');

    // the things that have a tool of their own
    r = rig();
    const moved = r.run('put_hill', { team: 'red', row: 15, column: 15 });
    check(moved.ok && Blocks.hillCells(r.ed.file, 1).length === 16 && r.ed.file.tile2(15, 15) === Blocks.TEAMS[1].hill && steps(r) === 1, 'put_hill: a hill moves to the entrance cell');
    check(r.run('put_hill', { team: 'red', row: 0, column: 0 }).error === 'refused', 'put_hill: no room at the corner is refused with a sentence');
    const ants = r.run('put_ants', { team: 'blue', row: 10, column: 20, count: 5 });
    check(ants.ok && ants.result.ants_now === ants.result.ants_before + 5, 'put_ants: five more ants');
    check(r.run('put_ants', { team: 'blue', row: 10, column: 24, count: 5, type: 'fire' }).error === 'refused', 'put_ants: an ant type is for an Extended map');
    const food = r.run('put_food', { row: 7, column: 8, stages: 4, bites: 30, points: 25 });
    check(food.ok && r.ed.file.food.length === 15, 'put_food: a pile');
    check(r.run('put_food', { row: 7, column: 8, food: 5 }).error === 'bad_args', 'put_food: a tile that is not a food item is refused');
    const spot = freeCell(r.ed.file), plant = r.run('put_plant', { tile: 421, row: spot[0], column: spot[1], dropper: 30 });
    check(plant.ok && r.ed.file.plants.some((p) => p.row === spot[0] && p.column === spot[1]), 'put_plant: a flower: ' + JSON.stringify(plant));
    check(r.run('put_plant', { tile: 302, row: 12, column: 14 }).error === 'bad_args', 'put_plant: a tile that is not a plant is refused');
    const rock = r.run('put_object', { tile: 302, row: 3, column: 3 });
    check(rock.ok && r.ed.file.tile2(3, 3) === 302, 'put_object: a small rock');
    const big = Palette.items('bigger')[0], placed = r.run('put_object', { tile: big.id, row: 20, column: 20 });
    check(placed.ok && r.ed.file.tile2(20, 20) === big.id, 'put_object: a bigger object');
    check(r.run('put_object', { tile: 999, row: 3, column: 3 }).error === 'bad_args' && r.run('put_object', { tile: 302, row: 99, column: 3 }).error === 'refused', 'put_object: a tile that is no object, and a cell outside the map');
    const refused = r.run('set_properties', { description: 'x'.repeat(100) });
    check(refused.error === 'refused' && /does not fit/.test(refused.message), 'set_properties: the core\u2019s reason is the answer');
    check(r.run('resize_map', { rows: 500, columns: 500 }).error === 'refused', 'resize_map: more than a Classic map may have is refused');
    check(r.run('new_map', { kind: 'classic', rows: 10, columns: 10 }).error === 'refused', 'new_map: the page\u2019s refusal is the answer');
    check(r.run('fix_edges').error === 'refused', 'fix_edges: refused while the edge pieces are not loaded');
    check(r.run('paint', { layer: 'ground', tile: 476, cells: [[1, 1]], auto_edges: true }).error === 'refused', 'paint: auto edges are refused while the edge pieces are not loaded');
    r.run('note', { text: 'Now the lake <b>x</b>' });
    equal(r.sim.notes, ['Now the lake <b>x</b>'], 'note: the line goes to the strip as it is (the strip puts it in with textContent)');
    const saved = r.run('save_draft', { name: 'Lake map' });
    check(saved.ok && saved.result.saved_as === 'Lake map 2' && r.sim.kept[0] === 'Lake map 2', 'save_draft: a free name');
    r.sim.fail = 'no';
    check(r.run('save_draft', {}).error === 'refused', 'save_draft: a browser that will not keep it is said so');
    check(bytesOf(r.ed.file).length > 0, 'the map is still there');

    // taking turns with the person
    r = rig();
    const v = r.run('map_info').result.version;
    r.run('paint', { layer: 'ground', tile: 476, cells: [[5, 5]], base: v });
    const stale = r.run('paint', { layer: 'ground', tile: 476, cells: [[6, 6]], base: v });
    check(stale.error === 'changed_by_person' && /version/.test(stale.message) && r.ed.file.tile1(6, 6) !== 476, 'an old base is refused with the new version and changes nothing: ' + JSON.stringify(stale));
    const now = r.part.version(), fresh = r.run('paint', { layer: 'ground', tile: 476, cells: [[6, 6]], base: now });
    check(fresh.ok && r.ed.file.tile1(6, 6) === 476, 'the new version is the right base');
    // the person draws a stroke: the AI's step is no longer at the top, so its undo is refused
    const stroke = r.ed.begin('ground', 439); stroke.stamp(9, 9, 1); stroke.end(); r.part.touch();
    const mine = r.run('undo');
    check(mine.error === 'changed_by_person' && r.ed.file.tile1(9, 9) === 439 && r.ed.file.tile1(6, 6) === 476, 'undo: the person\u2019s step is never undone by the AI: ' + JSON.stringify(mine));
    check(r.part.undoAll() === 0, 'Undo AI changes: stops at the person\u2019s step');
    r.ed.undo();
    check(r.part.undoAll() === 2 && r.ed.file.tile1(5, 5) !== 476, 'Undo AI changes: the AI\u2019s steps at the top');

    // the person's unsaved work is kept first, and when it cannot be kept nothing is changed
    r = rig();
    r.sim.fail = 'no';
    const snap = bytesOf(r.ed.file), noCopy = r.run('paint', { layer: 'ground', tile: 476, cells: [[2, 2]] });
    check(noCopy.error === 'refused' && /safety copy/.test(noCopy.message) && bytesOf(r.ed.file) === snap, 'a map that cannot be kept first is not changed');
    r.sim.fail = ''; r.sim.saved = true; r.sim.copies.length = 0;
    r.run('paint', { layer: 'ground', tile: 476, cells: [[2, 2]] });
    check(r.sim.copies.length === 0, 'a map that is saved needs no copy');
    r = rig(); r.part.arm(); r.run('paint', { layer: 'ground', tile: 476, cells: [[2, 2]] }); r.part.arm(); r.run('paint', { layer: 'ground', tile: 476, cells: [[3, 3]] });
    check(r.sim.copies.length === 2, 'a session that is armed again keeps a copy again');

    // a stroke that is going on is waited for
    r = rig(); r.sim.stroking = 3;
    check(r.run('paint', { layer: 'ground', tile: 476, cells: [[2, 2]] }).ok, 'a call waits for the person\u2019s stroke and then goes on');
    r = rig(); r.sim.stroking = 100000;
    const busy = r.run('paint', { layer: 'ground', tile: 476, cells: [[2, 2]] });
    check(busy.error === 'busy' && r.ed.file.tile1(2, 2) !== 476, 'a stroke that does not end is a busy answer, and nothing changed');

    // more than 5,000 cells: refused and undone
    r = rig();
    const flood = rig(); flood.ed.act(Blocks.resizeMap(flood.ed.file, 90, 90, 0, 0)); flood.part.touch(); flood.sim.saved = true;
    check(flood.ed.file.rows === 90, 'the test map is 90 x 90');
    const grass = flood.ed.begin('ground', 485); for (let rr = 0; rr < 90; rr++) for (let cc = 0; cc < 90; cc++) grass.stamp(rr, cc, 1); grass.end(); flood.part.touch();
    const snap2 = bytesOf(flood.ed.file), huge = flood.run('paint', { layer: 'ground', tile: 476, fill: { row: 0, column: 0 } });
    check(huge.error === 'too_big' && bytesOf(flood.ed.file) === snap2, 'a fill of more than 5,000 cells is refused and undone: ' + JSON.stringify(huge));
}

// ---- D ----
console.log('RUNNING: D the sheet, the strip and the socket');
{
    const doc = makeDocument(), body = doc.body;
    const top = doc.createElement('header'), host = doc.createElement('div'); host.id = 'dialogs';
    body.appendChild(top); body.appendChild(host);
    const entry = doc.createElement('button'), lbl = doc.createElement('span'); lbl.className = 'lbl'; lbl.textContent = 'Connect an AI...'; entry.className = 'btn aik-btn'; entry.appendChild(lbl); body.appendChild(entry);
    const dialogs = Dialog.create(doc, host, []);
    const calls = [], sockets = [], answers = {};
    const storage = { touched: 0 };
    const win = {
        location: { origin: 'https://site.example', protocol: 'https:', host: 'site.example' }, setInterval: () => 1, clearInterval: () => {}, setTimeout: () => 1, clearTimeout: () => {},
        navigator: { clipboard: { writeText: (t) => { win.copied = t; return Promise.resolve(); } } }, copied: '',
        WebSocket: class { constructor(url) { this.url = url; this.sent = []; this.readyState = 1; this.closed = null; sockets.push(this); } send(t) { this.sent.push(JSON.parse(t)); } close(code, why) { this.closed = [code, why]; this.readyState = 3; if (this.onclose) this.onclose(); } }
    };
    for (const k of ['localStorage', 'sessionStorage', 'indexedDB']) Object.defineProperty(win, k, { get: () => { storage.touched++; return null; } });
    const KEY = 'antsai_0a1b2c3d_' + 'S'.repeat(43);
    function fetch(url, opts) {
        calls.push({ url: url, opts: opts || {} });
        const a = answers[(opts && opts.method || 'GET') + ' ' + url] || answers[url] || { status: 404, json: null };
        return Promise.resolve({ status: a.status, ok: a.status === 200, json: () => Promise.resolve(a.json), text: () => Promise.resolve(a.text || '') });
    }
    answers['GET /hub/ai/keys'] = { status: 200, json: { ok: true, keys: [{ id: '0a1b2c3d', name: '<img src=x onerror=alert(1)>', scope: 'edit', made: 1900000000, ends: 1902592000, used: 0, calls: 0 }, { id: '11223344', name: 'Look key', scope: 'read', made: 1900000000, ends: 1902592000, used: 1900000100, calls: 3 }], max: 3, days: [7, 30, 90], csrf: 'c'.repeat(64), armed: false } };
    answers['POST /hub/ai/keys'] = { status: 200, json: { ok: true, key: KEY, id: '99887766', name: 'Home laptop', scope: 'edit', made: 1900000000, ends: 1902592000, used: 0, calls: 0 } };
    answers['POST /hub/ai/keys/revoke'] = { status: 200, json: { ok: true } };
    const r = rig();
    let ai = null;
    ai = Part.create({ doc: doc, win: win, dialogs: dialogs, fetch: fetch, host: top, entry: entry, announce: () => {}, tools: r.env });
    r.env.changed = () => { sim.changed++; ai.touch(); };
    const sim = r.sim;
    const text = () => body.textContent;
    const flush = () => new Promise((resolve) => setImmediate(resolve));
    const buttonNamed = (name) => doc.querySelectorAll('button').filter((b) => b.textContent === name)[0];

    (async () => {
        ai.open(); await flush(); await flush();
        check(calls[0].url === '/hub/ai/keys' && calls[0].opts.method === 'GET' && calls[0].opts.credentials === 'same-origin' && !calls[0].opts.headers['X-Ants-Csrf'], 'the sheet asks for the keys with the login and no token');
        check(/Connect an AI/.test(text()) && /2 of 3/.test(text()) && /Look only/.test(text()) && /Look and edit/.test(text()), 'the sheet: the keys and what they may do');
        check(doc.querySelectorAll('img').length === 0 && text().includes('<img src=x onerror=alert(1)>'), 'a name with markup is text');
        check(!!buttonNamed('Make a key...') && !!buttonNamed('Close'), 'the sheet: Make a key and Close');
        check(/Let an AI work in this tab: Off/.test(text()), 'the switch says Off');

        // revoke
        doc.querySelectorAll('button').filter((b) => b.getAttribute('aria-label') === 'Revoke Look key')[0].click();
        check(/Revoke \u201cLook key\u201d\? An AI that uses it is cut off at once/.test(text()), 'revoke: the question');
        buttonNamed('Keep it').click();
        check(!/An AI that uses it is cut off at once/.test(text()), 'revoke: Keep it asks no more');
        doc.querySelectorAll('button').filter((b) => b.getAttribute('aria-label') === 'Revoke Look key')[0].click();
        const before = calls.length;
        buttonNamed('Revoke this key').click(); await flush(); await flush();
        const post = calls.slice(before).filter((c) => c.opts.method === 'POST')[0];
        check(post && post.url === '/hub/ai/keys/revoke' && post.opts.body === '{"id":"11223344"}' && post.opts.headers['X-Ants-Csrf'] === 'c'.repeat(64) && post.opts.headers['Content-Type'] === 'application/json', 'revoke: one POST with the key\u2019s id, the token and the type: ' + JSON.stringify(post));

        // make a key
        buttonNamed('Make a key...').click();
        check(/Make a key/.test(text()) && /Stops working after/.test(text()) && /There is no key that never ends/.test(text()) && /3 keys at a time/.test(text()), 'make: the screen');
        const name = doc.querySelector('#aik-name');
        name.value = 'Home laptop';
        const radios = doc.querySelectorAll('input').filter((i) => i.type === 'radio');
        const read = radios.filter((i) => i.name === 'aik-can')[1], ninety = radios.filter((i) => i.name === 'aik-days')[2];
        read.checked = true; read.fire('change', {}); ninety.checked = true; ninety.fire('change', {});
        const mark = calls.length;
        buttonNamed('Make the key').click(); await flush(); await flush();
        const made = calls.slice(mark).filter((c) => c.opts.method === 'POST')[0];
        equal(JSON.parse(made.opts.body), { name: 'Home laptop', scope: 'read', days: 90 }, 'make: the body is the name, the scope and the days and nothing else');
        check(made.url === '/hub/ai/keys' && made.opts.headers['X-Ants-Csrf'] === 'c'.repeat(64), 'make: the token of the sheet');
        check(doc.querySelectorAll('code').some((c) => c.textContent === KEY) && /only time you will see the whole key/.test(text()), 'shown: the key once');
        buttonNamed('Copy key').click(); await flush();
        check(win.copied === KEY, 'shown: Copy key copies it');
        buttonNamed('Next: connect your AI').click();
        check(doc.querySelectorAll('pre').some((p) => p.textContent.includes('claude mcp add --transport http ants-map-maker') && p.textContent.includes('https://site.example/hub/ai/mcp') && p.textContent.includes('Bearer ' + KEY)), 'connect: the command holds the key');
        check(/Teach it to make good maps/.test(text()) && doc.querySelectorAll('a').some((a) => a.getAttribute('href') === '/ai/skills/all.zip'), 'connect: the skills link');
        const tabs = doc.querySelectorAll('button').filter((b) => b.getAttribute('role') === 'tab');
        tabs[1].click();
        check(doc.querySelectorAll('pre').some((p) => p.textContent.includes('"mcpServers"') && p.textContent.includes('Bearer ' + KEY)), 'connect: the small file');
        tabs[2].click();
        check(doc.querySelectorAll('pre').some((p) => p.textContent.includes('curl -X POST https://site.example/hub/ai/v1/call')), 'connect: the plain web call');
        buttonNamed('Done').click();
        check(!text().includes(KEY), 'the key is not on the page any more');
        check(storage.touched === 0, 'no storage was touched: ' + storage.touched);
        equal(Part.commandFor('code', 'https://x.example', ''), 'claude mcp add --transport http ants-map-maker \\\n  https://x.example/hub/ai/mcp \\\n  --header "Authorization: Bearer <your key>"', 'the command without a key says where the key goes');

        // the switch and the socket
        ai.open(); await flush(); await flush();
        const sw = doc.querySelectorAll('button').filter((b) => b.getAttribute('role') === 'switch')[0];
        check(!!sw && sw.getAttribute('aria-checked') === 'false', 'the switch is off');
        sw.click();
        check(sockets.length === 1 && sockets[0].url === 'wss://site.example/hub/ai/ws' && ai.state() === 'connecting', 'the switch opens the socket of the armed tab');
        sockets[0].onmessage({ data: JSON.stringify({ t: 'ready', v: 1 }) });
        check(ai.state() === 'armed' && /This tab is open to an AI/.test(text()) && /Waiting for your AI to connect/.test(text()) && /AI is on/.test(lbl.textContent), 'ready: the strip says that the tab is open');
        sockets[0].onmessage({ data: 'not json' }); sockets[0].onmessage({ data: JSON.stringify([1]) }); sockets[0].onmessage({ data: JSON.stringify({ t: 'call', id: 'x', tool: 'map_info', args: {} }) }); sockets[0].onmessage({ data: JSON.stringify({ t: 'call', id: 1, tool: 'map_info' }) });
        equal(sockets[0].sent, [], 'frames that are not calls are ignored');
        sockets[0].onmessage({ data: JSON.stringify({ t: 'call', id: 7, tool: 'map_info', args: {}, key: 'Home <b>laptop</b>', client: 'Claude Code' }) });
        const first = sockets[0].sent[0];
        check(first && first.t === 'result' && first.id === 7 && first.ok === true && first.result.kind === 'classic' && first.cells === 0, 'a call is run and answered: ' + JSON.stringify(first).slice(0, 120));
        check(ai.state() === 'working' && /An AI is working on this map/.test(text()) && /Claude Code/.test(text()) && /Home <b>laptop<\/b>/.test(text()) && !doc.querySelectorAll('b').some((b) => /laptop/.test(b.textContent)), 'the strip names the app and the key as text');
        sockets[0].onmessage({ data: JSON.stringify({ t: 'call', id: 8, tool: 'submit', args: {} }) });
        check(sockets[0].sent[1].ok === false && sockets[0].sent[1].error === 'bad_args', 'a tool that is not one is refused to the hub');
        sockets[0].onmessage({ data: JSON.stringify({ t: 'call', id: 9, tool: 'paint', args: { layer: 'ground', tile: 476, rect: { row: 1, column: 1, rows: 2, columns: 2 } } }) });
        const painted = sockets[0].sent[2];
        check(painted.ok && painted.cells === 4 && r.ed.file.tile1(1, 1) === 476 && /1 change so far/.test(text()) && /Painted 4 cells/.test(text()), 'an edit is made, counted and said: ' + JSON.stringify(painted).slice(0, 120));
        sockets[0].onmessage({ data: JSON.stringify({ t: 'call', id: 10, tool: 'note', args: { text: 'A lake <b>next</b>' } }) });
        check(/The AI says: \u201cA lake <b>next<\/b>\u201d/.test(text()) && !doc.querySelectorAll('b').some((b) => /next/.test(b.textContent)) && text().split('The AI says').length === 2, 'a note is shown as text, once');
        // Undo AI changes and Stop
        buttonNamed('Undo AI changes').click();
        check(r.ed.file.tile1(1, 1) !== 476, 'Undo AI changes takes the AI\u2019s steps back');
        buttonNamed('Stop').click();
        check(sockets[0].closed && sockets[0].closed[0] === 1000 && ai.state() === 'off' && /Connect an AI/.test(lbl.textContent), 'Stop closes the socket at once');
        const n = sockets[0].sent.length;
        sockets[0].onmessage({ data: JSON.stringify({ t: 'call', id: 11, tool: 'map_info', args: {} }) });
        check(sockets[0].sent.length === n, 'a call after Stop is not run');
        // a second arming, and what the hub says when it ends it
        ai.open(); await flush(); await flush();
        doc.querySelectorAll('button').filter((b) => b.getAttribute('role') === 'switch')[0].click();
        check(sockets.length === 2, 'armed again: a new socket');
        sockets[1].onmessage({ data: JSON.stringify({ t: 'ready', v: 1 }) });
        sockets[1].onmessage({ data: JSON.stringify({ t: 'bye', why: 'replaced' }) });
        sockets[1].onclose();
        check(ai.state() === 'off' && /Another tab took the AI\u2019s place/.test(text()), 'bye: the strip says why the AI is off');
        buttonNamed('OK').click();
        check(!/took the AI/.test(text()), 'the message goes with OK');

        // no keys, no login, no switch
        answers['GET /hub/ai/keys'] = { status: 503, json: { error: 'ai_off' } };
        ai.open(); await flush(); await flush();
        check(/not switched on here yet/.test(text()) && !buttonNamed('Make a key...') && !doc.querySelectorAll('button').some((b) => b.getAttribute('role') === 'switch'), 'ai_off: the sheet says it is not available and offers nothing');
        buttonNamed('Close').click();
        answers['GET /hub/ai/keys'] = { status: 401, json: { error: 'login' } };
        ai.open(); await flush(); await flush();
        check(doc.querySelectorAll('a').some((a) => a.getAttribute('href') === '/hub/login?next=mapmaker'), 'no login: a link to log in that comes back to the map maker');
        buttonNamed('Close').click();

        // the skills screen
        answers['GET /hub/ai/keys'] = { status: 200, json: { ok: true, keys: [], max: 3, days: [7, 30, 90], csrf: 'c'.repeat(64), armed: false } };
        answers['GET /ai/skills/index.json'] = { status: 200, json: { skills: [{ name: 'ants-map-guide', topic: 'guide', description: 'Start here', bytes: 3000, files: 1 }], all: { zip: 'all.zip', bytes: 26000 } } };
        ai.open(); await flush(); await flush();
        buttonNamed('How to connect an AI app').click();
        doc.querySelectorAll('button').filter((b) => b.textContent === 'What is in them, and how to install them')[0].click(); await flush(); await flush();
        check(/ants-map-guide/.test(text()) && doc.querySelectorAll('a').some((a) => a.getAttribute('href') === '/ai/skills/ants-map-guide.zip') && /Settings, Capabilities, Skills/.test(text()) && /~\/\.claude\/skills\//.test(text()), 'the skills screen lists the skills and says how to install them');
        const bad = calls.filter((c) => !/^\/hub\/ai\/|^\/ai\/skills\//.test(c.url));
        check(bad.length === 0, 'the page calls nothing but the hub\u2019s key pages and the skills: ' + JSON.stringify(bad.map((c) => c.url)));
    })().then(() => {
        console.log('ai check: ' + checks + ' checks' + (failures ? ', ' + failures + ' FAILED' : ' passed'));
        process.exit(failures ? 1 : 0);
    }, (e) => { console.log('    CRASHED: ' + (e && e.stack || e)); process.exit(1); });
}
