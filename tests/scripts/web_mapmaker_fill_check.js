#!/usr/bin/env node
// The map maker's "Fill a box" (web/front/mapmaker_fill.js, the switch in web/front/mapmaker_tools.js; docs/MAP_EDITOR.md, "Fill a box") held to real library maps, with the real reader, blocks, editor and
// history and a small fake of the DOM (no browser, no map core: the pictures of the box are the browser part of tests/scripts/test_web_mapmaker.sh). The AI's `fill_area` is held in
// tests/scripts/web_mapmaker_ai_check.js; it uses the plan and the change that are checked here.
//
//   node tests/scripts/web_mapmaker_fill_check.js [--front DIR]
//
//   A  the patterns: every cell, every other cell, every Nth row and column are exactly what a list made again here says, counted from the box's corner; random is the same for a seed whatever the
//      box is, differs for another seed, follows its density and a lower density is a part of a higher one; every number is put in its range
//   B  which cells are skipped, and why: water, solid ground, a rock, an object, a hill, ants, a plant, a pile; water is allowed for a bridge piece (which wants it) and for a swimmer ant of an
//      Extended map and for nothing else; a bridge piece on dry ground is skipped
//   C  the cap of one fill: 250 ants, 5,000 of anything else, and what the file's lists have room for; the cells over it are told, in the order of the map
//   D  one step: what a fill puts is exactly the bytes of putting the same cells one by one with the page's own functions (objects, rocks that are solid, plants and flowers that drop power-ups,
//      piles and foods, ants of a team and a type, on a Classic and an Extended map), one Undo takes it all away and one Redo gives the same bytes back
//   E  the box in the page on a fake DOM: the status and the count while a box is dragged, the fill when it is let go, the line after an Undo and a Redo, a lost drag that puts nothing, the
//      cap's words, New pattern and the team and ant type of Ants
//   F  the switch in the tools: off at the start and remembered, only for Objects, Food and Ants, the painter's hand-over to the part, a drag that paints a line while it is off, a part that
//      did not load
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Edit = load('mapmaker_edit.js'), Blocks = load('mapmaker_blocks.js'), Palette = load('mapmaker_palette.js'), Tiles = load('mapmaker_tiles.js');
const Fill = load('mapmaker_fill.js'), Ants = load('mapmaker_ants.js'), Tools = load('mapmaker_tools.js'), Solid = load('mapmaker_solid.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }
function same(a, b) { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; }
function open(rel) { const f = File.parse(new Uint8Array(fs.readFileSync(path.join(ROOT, rel)))); if (!f.ok) throw new Error(rel); return f; }
const EMPTY2 = Edit.EMPTY2;
const MEDIUM = 'Original-Ants/Maps/MEDIUM.LVL';

const toV9 = (bytes) => require('./web_mapmaker_extended.js').toExtended(FRONT, bytes);          // (the map as an Extended map, made here)
function editor(file) { return Edit.create(file, { rebase: (b) => File.parse(b), erase: Blocks.eraseAt, guard: Blocks.blockedText }); }
function copy(file) { return File.parse(Uint8Array.from(file.bytes)); }          // (an editor changes the bytes of the file that it is given)
const key = (cells) => cells.map((c) => c.r + ',' + c.c).sort().join(' ');
const idx = (f, r, c) => r * f.columns + c;
const water = (f, i) => Tiles.kindOf(Edit.tile(f, 1, i)) === 'water';
// the map's cells that are free for anything but water (nothing on them, not solid)
function plain(f, i) { return !(Edit.flags(f, 1, i) & 1) && Edit.tile(f, 2, i) === EMPTY2 && !Fill.occupied(f)[i]; }
function find(f, test, from) { for (let i = from || 0; i < f.cells; i++) if (test(i)) return i; return -1; }
const rc = (f, i) => [Math.floor(i / f.columns), i % f.columns];
// the part of a file that a fill changes and an Undo gives back: the layers and the lists (the dictionary may hold a name more after an Undo, as it does for the person)
function body(f) { return Buffer.from(f.bytes.subarray(f.layer1)).toString('hex'); }

// ---- A ----
console.log('RUNNING: A the patterns');
{
    const f = open(MEDIUM), boxes = [{ r0: 3, c0: 4, r1: 3, c1: 4 }, { r0: 5, c0: 5, r1: 5, c1: 17 }, { r0: 7, c0: 9, r1: 19, c1: 9 }, { r0: 11, c0: 13, r1: 26, c1: 30 }, { r0: 20, c0: 7, r1: 41, c1: 20 }];
    const thing = Fill.objectThing(129);
    const listed = (p) => key(p.cells.concat(p.extra, p.skipped));          // (the cells on the pattern, whatever became of them)
    const want = (box, test) => { const out = []; for (let r = box.r0; r <= box.r1; r++) for (let c = box.c0; c <= box.c1; c++) if (test(r - box.r0, c - box.c0, r, c)) out.push({ r, c }); return key(out); };
    for (const box of boxes) {
        const label = 'box ' + [box.r0, box.c0, box.r1, box.c1].join(',') + ': ';
        equal(listed(Fill.plan(f, thing, box, { pattern: 'all' })), want(box, () => true), label + 'every cell');
        equal(listed(Fill.plan(f, thing, box, { pattern: 'checker' })), want(box, (rr, cc) => (rr + cc) % 2 === 0), label + 'every other cell, counted from the corner of the box');
        for (let n = 2; n <= 9; n++) {
            equal(listed(Fill.plan(f, thing, box, { pattern: 'rows', n })), want(box, (rr) => rr % n === 0), label + 'every ' + n + 'th row from the top of the box');
            equal(listed(Fill.plan(f, thing, box, { pattern: 'cols', n })), want(box, (rr, cc) => cc % n === 0), label + 'every ' + n + 'th column from the left side of the box');
        }
    }
    // random: the same for a seed, and a cell is decided by its place, not by the box that holds it
    const big = { r0: 2, c0: 2, r1: 41, c1: 41 }, a = Fill.plan(f, thing, big, { pattern: 'random', density: 30, seed: 7 }), b = Fill.plan(f, thing, big, { pattern: 'random', density: 30, seed: 7 });
    equal(listed(a), listed(b), 'random: the same seed gives the same cells');
    equal(a.cells.map((c) => c.r + ',' + c.c), b.cells.map((c) => c.r + ',' + c.c), 'random: the same cells in the same order');
    const other = Fill.plan(f, thing, big, { pattern: 'random', density: 30, seed: 8 });
    check(listed(a) !== listed(other), 'random: another seed gives other cells');
    const part = Fill.plan(f, thing, { r0: 10, c0: 12, r1: 30, c1: 33 }, { pattern: 'random', density: 30, seed: 7 }), inA = new Set(listed(a).split(' '));
    check(listed(part).split(' ').every((k) => inA.has(k)) && part.pattern > 0, 'random: a smaller box holds the cells of the bigger one that lie in it, and no other');
    const total = 40 * 40;
    for (const d of [5, 30, 50, 95]) {
        const p = Fill.plan(f, thing, big, { pattern: 'random', density: d, seed: 3 });
        check(Math.abs(p.pattern - total * d / 100) < total * 0.06, 'random ' + d + '%: about ' + Math.round(total * d / 100) + ' of ' + total + ' cells (' + p.pattern + ')');
    }
    const low = new Set(listed(Fill.plan(f, thing, big, { pattern: 'random', density: 20, seed: 3 })).split(' ')), high = new Set(listed(Fill.plan(f, thing, big, { pattern: 'random', density: 60, seed: 3 })).split(' '));
    check(low.size < high.size && Array.from(low).every((k) => high.has(k)), 'random: a lower density is a part of a higher one');
    // the numbers are put in their range
    equal([Fill.norm({ pattern: 'rows', n: 1 }).n, Fill.norm({ pattern: 'rows', n: 12 }).n, Fill.norm({ n: 'x' }).n], [2, 9, 2], 'n is 2 to 9');
    equal([Fill.norm({ density: 1 }).density, Fill.norm({ density: 200 }).density, Fill.norm({ seed: 0 }).seed, Fill.norm({ seed: 99999 }).seed], [5, 95, 1, 9999], 'density is 5 to 95 and the seed 1 to 9999');
    equal(Fill.norm({ pattern: 'spiral' }).pattern, 'all', 'a pattern that is not one is every cell');
    equal(Fill.PATTERNS.map((p) => p[0]), ['all', 'checker', 'rows', 'cols', 'random'], 'the five patterns');
}

// ---- B ----
console.log('RUNNING: B which cells are skipped');
{
    const f0 = open(MEDIUM), ed = editor(f0);
    const wet = find(f0, (i) => water(f0, i) && plain(f0, i)), dry = find(f0, (i) => !water(f0, i) && plain(f0, i) && i > 3 * f0.columns);
    check(wet >= 0 && dry >= 0, 'MEDIUM has water and plain ground to try');
    // things put on the map by the page's own tools: a rock (solid), a bomb, a plant, a pile and ants
    const spots = [];
    for (let i = dry; spots.length < 4 && i < f0.cells; i++) if (!water(f0, i) && plain(f0, i) && (i % f0.columns) % 3 === 0 && !spots.some((s) => Math.abs(s - i) < 3)) spots.push(i);
    const [rockAt, bombAt, plantAt, pileAt] = spots.map((i) => rc(f0, i));
    { const s = ed.begin('objects', 302); s.stamp(rockAt[0], rockAt[1], 1); s.end(); }
    { const s = ed.begin('objects', 129); s.stamp(bombAt[0], bombAt[1], 1); s.end(); }
    ed.act(Blocks.placePlant(ed.file, plantAt[0], plantAt[1], 404, 0));
    ed.act(Blocks.placePile(ed.file, pileAt[0], pileAt[1], 4, null));
    const antAt = find(ed.file, (i) => plain(ed.file, i) && !water(ed.file, i) && !spots.includes(i) && i > spots[3] + 5), [ar, ac] = rc(ed.file, antAt);
    ed.act(Blocks.placeStart(ed.file, 0, ar, ac, 1, 0));
    const f = ed.file, hillAt = rc(f, find(f, (i) => Blocks.teamOfHill(Edit.tile(f, 2, i)) >= 0));
    const why = (thing, r, c, file) => { const g = file || f, p = Fill.plan(g, thing, { r0: r, c0: c, r1: r, c1: c }, { pattern: 'all' }); return p.skipped.length ? p.skipped[0].why : (p.cells.length ? '' : '?'); };
    const wr = rc(f, wet), dr = rc(f, dry);
    for (const [name, thing] of [['a bomb', Fill.objectThing(129)], ['a power-up', Fill.objectThing(62)], ['a rock', Fill.objectThing(302)], ['a fire wall piece', Fill.objectThing(134)], ['a plant', Fill.plantThing(404, 0)], ['a flower that drops', Fill.plantThing(421, 30)], ['a food pile', Fill.pileThing(4, null)], ['an ant', Fill.antThing(1, 0)]]) {
        equal(why(thing, dr[0], dr[1]), '', name + ' goes on plain ground');
        equal(why(thing, wr[0], wr[1]), 'water', name + ' is skipped on water');
        equal(why(thing, rockAt[0], rockAt[1]), 'rock', name + ' is skipped on a rock (solid)');
        for (const [what, at] of [['an object', bombAt], ['a plant', plantAt], ['a food pile', pileAt], ['ants', [ar, ac]], ['a hill', hillAt]]) equal(why(thing, at[0], at[1]), 'taken', name + ' is skipped where ' + what + ' is');
    }
    // water: only a bridge piece (which needs it) and a swimmer ant of an Extended map are put on it
    const bridge = Fill.objectThing(Fill.BRIDGE);
    equal(why(bridge, wr[0], wr[1]), '', 'a bridge piece goes on water');
    equal(why(bridge, dr[0], dr[1]), 'dry', 'a bridge piece is skipped on dry ground');
    equal(why(bridge, bombAt[0], bombAt[1]), 'taken', 'a bridge piece is skipped where something is, water or not');
    const ext = editor(File.parse(toV9(open(MEDIUM).bytes))).file, ewet = rc(ext, find(ext, (i) => water(ext, i) && plain(ext, i)));
    equal(why(Fill.antThing(1, Fill.SWIMMER), ewet[0], ewet[1], ext), '', 'a swimmer ant of an Extended map stands on water');
    for (const type of [0, 1, 2, 3, 4]) equal(why(Fill.antThing(1, type), ewet[0], ewet[1], ext), 'water', 'an ant of type ' + type + ' does not');
    equal(why(Fill.antThing(1, Fill.SWIMMER), wr[0], wr[1]), 'water', 'a Classic map has no swimmers: its ant is skipped on water');
    equal(Fill.plan(f, Fill.antThing(1, Fill.SWIMMER), { r0: dr[0], c0: dr[1], r1: dr[0], c1: dr[1] }, { pattern: 'all' }).thing.type, 0, 'a Classic map’s ants have the default type');
    equal(Fill.whyNot(f, wet, Fill.antThing(1, Fill.SWIMMER), Fill.occupied(f)), 'water', 'and a swimmer is no swimmer there, whatever it is asked');
    // a box of cells that are all skipped puts nothing, and says so in words
    const none = Fill.plan(f, Fill.objectThing(129), { r0: wr[0], c0: wr[1], r1: wr[0], c1: wr[1] }, { pattern: 'all' });
    check(none.cells.length === 0 && none.skipped.length === 1 && none.skips.water === 1 && /Nothing to put/.test(Fill.nothingText(none)) && /1 water/.test(Fill.nothingText(none)), 'nothing to put: the words say why');
    const empty = Fill.plan(f, Fill.objectThing(129), { r0: 3, c0: 3, r1: 3, c1: 3 }, { pattern: 'random', density: 5, seed: 9 });
    check(empty.pattern === 0 && /no cell of the box is on the pattern/.test(Fill.nothingText(empty)), 'a pattern that picks no cell of the box says that');
    // the counts add up: every cell of the pattern is put, skipped or left out
    const whole = Fill.plan(f, Fill.objectThing(129), { r0: 0, c0: 0, r1: f.rows - 1, c1: f.columns - 1 }, { pattern: 'all' });
    equal([whole.cells.length + whole.extra.length + whole.skipped.length, whole.skips.water + whole.skips.rock + whole.skips.taken + whole.skips.dry], [f.cells, whole.skipped.length], 'every cell is put or skipped, and the counts of the reasons add up');
    check(whole.skips.water > 100 && whole.skips.taken > 0 && whole.skips.rock > 0, 'the whole of MEDIUM has water, things and rock in the way');
    // a thing and a cell of the lists that cannot be read
    check(Fill.nothingText(Fill.plan(f, Fill.pileThing(3, null), { r0: wr[0], c0: wr[1], r1: wr[0], c1: wr[1] }, { pattern: 'all' })).length > 20, 'the same words for a food pile');
    check(Fill.objectThing(404) === null && Fill.objectThing(1323) === null && Fill.objectThing(129).kind === 'obj' && Fill.plantThing(129, 0) === null, 'a plant, a bigger object or a tile that is not a one-cell object is no object of a fill');
    equal([Fill.objectThing(129).many, Fill.objectThing(62).many, Fill.objectThing(302).many, Fill.plantThing(404, 0).many, Fill.plantThing(421, 0).many, Fill.pileThing(3, null).many, Fill.antThing(2, 0).many, Fill.objectThing(37).many], ['bombs', 'power-ups', 'rocks', 'clovers', 'plants', 'food piles', 'ants', 'bridge pieces'], 'a thing in the plural');
}

// ---- C ----
console.log('RUNNING: C the cap of one fill');
{
    let f = null, name = '';
    for (const dir of ['Original-Ants/Maps', 'Community-Maps']) for (const n of fs.readdirSync(path.join(ROOT, dir)).sort()) {
        if (f || !/\.lvl$/i.test(n)) continue;
        const g = File.parse(new Uint8Array(fs.readFileSync(path.join(ROOT, dir, n))));
        if (g.ok && g.cells >= 7500) { const t = Fill.occupied(g); let free = 0; for (let i = 0; i < g.cells; i++) if (!Fill.whyNot(g, i, Fill.objectThing(129), t)) free++; if (free > 5400) { f = g; name = n; } }
    }
    check(!!f, 'the library has a map of 7,500 cells or more with more than 5,400 free cells (' + name + ')');
    const whole = { r0: 0, c0: 0, r1: f.rows - 1, c1: f.columns - 1 }, freeCount = (th) => { const t = Fill.occupied(f); let n = 0; for (let i = 0; i < f.cells; i++) if (!Fill.whyNot(f, i, th, t)) n++; return n; };
    const ant = Fill.plan(f, Fill.antThing(0, 0), whole, { pattern: 'all' }), freeAnts = freeCount(Fill.antThing(0, 0));
    equal([ant.cells.length, ant.extra.length, ant.cap], [250, freeAnts - 250, 250], 'ants: one fill puts 250 and leaves the rest out');
    const first = ant.cells.concat(ant.extra);
    check(first.every((c, i) => i === 0 || first[i - 1].r < c.r || (first[i - 1].r === c.r && first[i - 1].c < c.c)), 'the cells are in the order of the map: rows from the top, each row from the left, the last ones are left out');
    const bombs = Fill.plan(f, Fill.objectThing(129), whole, { pattern: 'all' }), freeBombs = freeCount(Fill.objectThing(129));
    equal([bombs.cells.length, bombs.extra.length, bombs.cap], [5000, freeBombs - 5000, 5000], 'anything else: 5,000 and the rest is left out');
    const few = Fill.plan(f, Fill.antThing(0, 0), { r0: 0, c0: 0, r1: 5, c1: 5 }, { pattern: 'all' });
    check(few.extra.length === 0 && few.cells.length + few.skipped.length === 36, 'a box under the cap leaves nothing out');
    // the file's lists have room for 65,535 starting ants and plants together
    const ed = editor(f), lay = Blocks.layout(ed.file), spots = [];
    for (let r = 0; spots.length < 65535 - lay.records.length - 30; r++) spots.push({ tile: Blocks.TEAMS[1].marker, row: 0, column: 0 });          // (stacked on one cell, so that the others stay free)
    ed.act(Blocks.appendStarts(ed.file, lay, spots));
    check(Blocks.layout(ed.file).records.length === 65505, 'the list holds 65,505 records');
    const near = Fill.plan(ed.file, Fill.antThing(0, 0), whole, { pattern: 'all' }), nearPlants = Fill.plan(ed.file, Fill.plantThing(404, 0), whole, { pattern: 'all' });
    equal([near.cells.length, nearPlants.cells.length, near.cap], [30, 30, 30], 'ants and plants share the list: only the room that is left is put');
    check(near.extra.length > 0 && near.cells.length + near.extra.length > 30, 'the rest is left out, not lost');
    check(Fill.changeOf(ed.file, Fill.antThing(0, 0), near.cells.concat(near.extra.slice(0, 1))).error, 'a change that would not fit is refused with a sentence');
    check(!Fill.changeOf(ed.file, Fill.antThing(0, 0), near.cells).error, 'and one that fits is made');
    check(Fill.plan(ed.file, Fill.objectThing(129), whole, { pattern: 'all' }).cap === 5000, 'objects are not in the list: the room of the list does not limit them');
    // the piles of block 2 have room for 65,535 too
    const pe = editor(f), stack = [];
    for (let i = 0; i < 65535 - Blocks.layout(pe.file).piles.length - 20; i++) stack.push({ r: 0, c: 0 });
    pe.act(Fill.placePiles(pe.file, stack, 3, null));
    const piles = Fill.plan(pe.file, Fill.pileThing(3, null), whole, { pattern: 'all' });
    equal([piles.cells.length, piles.cap, piles.extra.length > 0], [20, 20, true], 'food piles: only the room that is left in block 2 is put');
}

// ---- D ----
console.log('RUNNING: D one step');
{
    const maps = [['Classic', open(MEDIUM)], ['Extended', File.parse(toV9(open(MEDIUM).bytes))]], food = Blocks.FOOD_ITEMS[2];
    const box = { r0: 14, c0: 12, r1: 33, c1: 40 };
    for (const [kind, f0] of maps) {
        const things = [
            ['bombs', Fill.objectThing(129), 'checker'], ['rocks (solid)', Fill.objectThing(302), 'rows'], ['fire walls (solid)', Fill.objectThing(134), 'checker'], ['bridge pieces', Fill.objectThing(37), 'all'], ['power-ups', Fill.objectThing(63), 'cols'],
            ['clovers', Fill.plantThing(404, 0), 'checker'], ['flowers that drop power-ups', Fill.plantThing(421, 30), 'random'], ['food piles of 4 stages', Fill.pileThing(4, null), 'rows'],
            ['a food item', Fill.pileThing(food[4].length + 1, { id: food[0], units: food[2], points: food[3] }), 'checker'], ['ants', Fill.antThing(2, 0), 'checker']
        ].concat(kind === 'Extended' ? [['swimmer ants', Fill.antThing(1, Fill.SWIMMER), 'all'], ['fire ants', Fill.antThing(3, 2), 'cols']] : []);
        for (const [name, thing, pattern] of things) {
            const label = kind + ' map, ' + name + ': ', ed = editor(copy(f0)), before = body(ed.file), p = Fill.plan(ed.file, thing, box, { pattern, n: 3, density: 25, seed: 4 });
            check(p.cells.length > 2, label + 'the box has cells for it (' + p.cells.length + ')');
            const ch = Fill.changeOf(ed.file, p.thing, p.cells);
            check(!ch.error, label + 'the change is made');
            const strokes = ed.history.size().strokes, dirty = ed.act(ch);
            check(!!dirty && ed.history.size().strokes === strokes + 1, label + 'the whole fill is one step of the history');
            const after = Buffer.from(ed.file.bytes).toString('hex');
            // the same cells put one by one with the page's own functions
            const one = editor(copy(f0));
            for (const c of p.cells) {
                if (thing.kind === 'obj') { const made = Edit.ensureTile(one.file, thing.id, thing.name); if (made.grew) one.rebase(File.parse(made.bytes)); const s = one.begin('objects', thing.id); s.stamp(c.r, c.c, 1); s.end(); }
                else if (thing.kind === 'plant') one.act(Blocks.placePlant(one.file, c.r, c.c, thing.id, thing.dropper));
                else if (thing.kind === 'pile') one.act(Blocks.placePile(one.file, c.r, c.c, thing.stages, thing.custom));
                else one.act(Ants.putOne(one.file, thing.team, c.r, c.c, f0.version === 9 ? thing.type : 0));
            }
            check(Buffer.from(one.file.bytes).toString('hex') === after, label + 'the same bytes as putting ' + p.cells.length + ' one by one');
            if (thing.kind === 'obj' && Palette.itemOf('objects', thing.id)) check(p.cells.every((c) => Edit.tile(ed.file, 2, idx(ed.file, c.r, c.c)) === thing.id && ((Edit.flags(ed.file, 1, idx(ed.file, c.r, c.c)) & 1) === 1) === Solid.sets(thing.id)), label + 'every cell holds the tile, and is solid where the tile is');
            if (thing.kind === 'plant') check(ed.file.plants.length === f0.plants.length + p.cells.length, label + 'every plant is in the lists');
            if (thing.kind === 'ant') check(ed.file.markers.length === f0.markers.length + p.cells.length && ed.file.markers.slice(-p.cells.length).every((m) => (m.antType || 0) === (f0.version === 9 ? thing.type : 0)), label + 'every ant is in the list, of the type');
            ed.undo();
            check(body(ed.file) === before, label + 'one Undo takes all of it away, byte for byte');
            check(!ed.history.canUndo() && ed.history.canRedo(), label + 'and that was the only step');
            ed.redo();
            check(Buffer.from(ed.file.bytes).toString('hex') === after, label + 'one Redo gives the same bytes back');
        }
    }
    // a thing that is put again on the same box: the cells that now hold it are skipped
    const ed = editor(open(MEDIUM)), p1 = Fill.plan(ed.file, Fill.objectThing(129), box, { pattern: 'all' });
    ed.act(Fill.changeOf(ed.file, p1.thing, p1.cells));
    const p2 = Fill.plan(ed.file, Fill.objectThing(129), box, { pattern: 'all' });
    check(p2.cells.length === 0 && p2.skips.taken >= p1.cells.length, 'the same box again puts nothing: its cells are taken');
    // the seed makes a fill repeat: a fill and its Undo and Redo give one set of cells, and another seed another set
    const g = editor(open(MEDIUM)), plan1 = Fill.plan(g.file, Fill.objectThing(129), box, { pattern: 'random', density: 30, seed: 5 });
    g.act(Fill.changeOf(g.file, plan1.thing, plan1.cells)); g.undo(); g.redo();
    const placed = [];
    for (let i = 0; i < g.file.cells; i++) if (Edit.tile(g.file, 2, i) === 129 && !Edit.tile(open(MEDIUM), 2, i) !== undefined && Edit.tile(open(MEDIUM), 2, i) === EMPTY2) placed.push({ r: Math.floor(i / g.file.columns), c: i % g.file.columns });
    equal(key(placed), key(plan1.cells), 'random: the cells that are on the map after an Undo and a Redo are the cells of the plan');
    const plan2 = Fill.plan(open(MEDIUM), Fill.objectThing(129), box, { pattern: 'random', density: 30, seed: 5 }), plan3 = Fill.plan(open(MEDIUM), Fill.objectThing(129), box, { pattern: 'random', density: 30, seed: 6 });
    check(key(plan2.cells) === key(plan1.cells) && key(plan3.cells) !== key(plan1.cells), 'random: the plan of a seed is the same again, and another seed has another');
}

// ---- E ----
console.log('RUNNING: E the box in the page');
function partPage(rel, tool, over) {
    over = over || {};
    const doc = makeDocument(), el = (t) => doc.createElement(t), file0 = typeof rel === 'string' ? open(rel) : rel, ed = editor(file0);
    const side = el('aside'), checkline = el('div'), row = el('div'), tools = el('div'), hint = el('p'), map = el('canvas'), cv = el('div'), said = [], log = { refreshes: 0, cancels: 0, teams: [], types: [] };
    checkline.className = 'checkline'; side.appendChild(el('h3')); side.appendChild(checkline); tools.appendChild(row); tools.appendChild(hint); cv.appendChild(map); doc.body.appendChild(side); doc.body.appendChild(tools); doc.body.appendChild(cv);
    const state = { tool: tool || 'objects', objects: 129, food: 0, stages: 4, units: 30, points: 25, dropper: 0, team: 1, antType: 0 };
    let stroking = false;
    const later = [];
    const part = Fill.create({
        doc, win: { setTimeout: (fn) => { if (over.later) later.push(fn); else fn(); return 1; } }, host: { tools, hint, side, cv, map, row }, file: () => ed.file, act: (ch) => !!ed.act(ch), say: (t, m) => said.push(t + ': ' + m), dialogs: null, busy: () => false,
        state: () => Object.assign({}, state), setTeam: (t) => { state.team = t; log.teams.push(t); }, setAntType: (t) => { state.antType = t; log.types.push(t); }, antTypes: () => Ants.ANT_TYPES,
        sprites: () => null, pic: () => {}, view: () => ({}), refresh: () => { log.refreshes++; }, stroking: () => stroking, cancel: () => { log.cancels++; },
        step: () => ({ top: ed.history.top(), redo: ed.history.canRedo() })
    });
    part.show(true);
    // a drag as the canvas makes it: the box is begun on a cell and goes on from the last cell to the next, through the middle
    const p = part.painter, drag = (a, b, hold) => {
        const mid = [Math.round((a[0] + b[0]) / 2), Math.round((a[1] + b[1]) / 2)];
        p.begin({ row: a[0], column: a[1] }); p.extend({ row: a[0], column: a[1] }, { row: mid[0], column: mid[1] }); p.extend({ row: mid[0], column: mid[1] }, { row: b[0], column: b[1] });
        if (!hold) p.end();
    };
    return { part, ed, state, said, log, doc, side, row, tools, drag, later, stroke: (v) => { stroking = v; }, line: () => (part.pane.querySelector('.fb-st') || { textContent: '' }).textContent, phone: () => tools.querySelector('.fb-ps'),
             press: (k) => { const b = part.pane.querySelector('[data-k="' + k + '"]'); if (!b) throw new Error('no control ' + k); b.click(); return b; } };
}
{
    const t = partPage(MEDIUM, 'objects');
    check(!t.part.pane.hidden && t.side.className.indexOf('fb-on') >= 0, 'on a wide screen the box takes the right-hand column');
    equal(t.line(), 'Drag a box on the map. Let go, and it fills.', 'idle: it says what to do');
    check(t.part.pane.querySelectorAll('.fb-pb').length === 5, 'five patterns to choose from');
    check(t.part.painter.size() === 1 && t.part.painter.tapOnly() === false, 'the painter marks one cell and a drag is a box');
    t.drag([40, 25], [47, 36], true);
    const plan = t.part.state().plan;
    check(plan && plan.rows === 8 && plan.columns === 12 && plan.cells.length > 0 && /^8 rows x 12 columns: \d+ bombs?\./.test(t.line()), 'while the box is dragged the status counts rows, columns and bombs: ' + t.line());
    check(/cells? skipped \(/.test(t.line()) && plan.skipped.length > 0, 'and the cells that are skipped, with the reasons: ' + t.line());
    t.part.painter.cancel(); t.drag([47, 36], [40, 25], true);
    equal([t.part.state().box.r0, t.part.state().box.c0, t.part.state().box.r1, t.part.state().box.c1], [40, 25, 47, 36], 'a box dragged up and to the left is the same box');
    t.part.painter.cancel(); t.drag([40, 25], [47, 36], true);
    const before = t.ed.history.size().strokes;
    t.part.painter.end();
    check(t.ed.history.size().strokes === before + 1 && t.ed.file.cells > 0, 'let go: one step of the history');
    check(/^Put \d+ bombs/.test(t.line()) && /One Undo takes them all away\.$/.test(t.line()), 'the line says what was put: ' + t.line());
    const n = plan.cells.length;
    t.ed.undo(); t.part.refresh();
    equal(t.line(), 'Undid the fill of ' + n + ' bombs. One Redo puts them all back.', 'after an Undo the line says so');
    t.ed.redo(); t.part.refresh();
    check(/^Put \d+ bombs back\. One Undo takes them all away\.$/.test(t.line()), 'after a Redo too: ' + t.line());
    t.ed.undo(); t.part.refresh(); t.ed.act(Blocks.placePile(t.ed.file, 3, 3, 4, null)); t.part.refresh();
    equal(t.line(), 'Drag a box on the map. Let go, and it fills.', 'another change of the map takes the line away');
    // a pattern chosen changes the box that is there, and the numbers have their steppers
    t.drag([40, 25], [47, 36], true);
    const all = t.part.state().plan.cells.length + t.part.state().plan.skipped.length;
    t.press('p-all');
    equal(t.part.state().plan.cells.length + t.part.state().plan.skipped.length, 96, 'Every cell: all 8 x 12 cells are on the pattern (' + all + ' for the other cell)');
    t.press('p-rows'); t.press('n+'); t.press('n+');
    equal([t.part.state().n, t.part.state().plan.pattern], [5, 2 * 12], 'Every Nth row: 5 is the 5th row, rows 0 and 5 of 8: 2 x 12 cells');
    check(/Every 5th row, counted from the top of the box\./.test(t.part.pane.textContent), 'and the note says where the count starts');
    t.press('p-random');
    const seed0 = t.part.state().seed, cells0 = key(t.part.state().plan.cells);
    check(/Pattern 1\. Undo and Redo repeat it\./.test(t.part.pane.textContent), 'Random shows the number of its pattern');
    t.press('seed');
    check(t.part.state().seed === seed0 % 9999 + 1 && key(t.part.state().plan.cells) !== cells0, 'New pattern is the next pattern and other cells');
    const kept = t.ed.history.size().strokes;
    t.part.painter.cancel();
    check(t.part.state().box === null && t.ed.history.size().strokes === kept, 'a drag that is taken away (a second finger) puts nothing and the box is gone');
    t.drag([40, 25], [47, 36], true); const steps = t.ed.history.size().strokes; t.part.painter.abandon();
    check(t.ed.history.size().strokes === steps && t.part.state().box === null, 'a pointer that was lost without a lift puts nothing');
    t.drag([40, 25], [47, 36], true);
    const esc = t.part.painter.key({ key: 'Escape' });
    check(esc === true && t.log.cancels === 1 && t.part.state().box === null, 'Escape takes a box away');
    // a slider that is dragged stays in the page (a control that is made again takes the finger's drag from it), and the box is made again for the new density
    t.drag([40, 25], [47, 36], true); t.press('p-random');
    const range = t.part.pane.querySelector('.fb-range');
    range.value = '60'; range.fire('input', {});
    check(t.part.pane.querySelector('.fb-range') === range && t.part.state().density === 60 && t.part.state().plan.pattern > 0 && /Random, 60%/.test(t.part.pane.textContent), 'the density slider keeps its node while it is moved, and the box follows it');
    t.part.painter.cancel();
    // a box of cells that are all skipped puts nothing and says why
    const lake = partPage(MEDIUM, 'objects'), wetAt = rc(lake.ed.file, find(lake.ed.file, (i) => water(lake.ed.file, i) && plain(lake.ed.file, i)));
    lake.drag(wetAt, wetAt);
    check(lake.ed.history.size().strokes === 0 && /^Nothing to put here: 1 cell of the pattern are skipped \(1 water\)\.$/.test(lake.line()), 'a box of skipped cells puts nothing and says why: ' + lake.line());
    // the stroke of a finger's tap is still going when the tool is told: the fill is made a moment later
    const fin = partPage(MEDIUM, 'objects', { later: true });
    fin.stroke(true); fin.drag([20, 20], [20, 20]);
    check(fin.ed.history.size().strokes === 0 && fin.later.length === 1, 'a tap that ends while the canvas still counts it as a stroke waits a moment: nothing yet');
    fin.stroke(false); fin.later.shift()();
    check(fin.ed.history.size().strokes === 1 && fin.ed.history.canUndo() && Edit.tile(fin.ed.file, 2, idx(fin.ed.file, 20, 20)) === 129, 'and puts its one bomb a moment later, as one step');
    // the cap
    const big = partPage('Original-Ants/Maps/TREASURE.LVL', 'ants');
    big.state.team = 2;
    big.drag([0, 0], [big.ed.file.rows - 1, big.ed.file.columns - 1], true);
    const cap = big.part.state().plan;
    check(cap.cells.length === 250 && cap.extra.length > 0 && /would fit this box at this spacing\. One fill puts at most 250, so the last \d+ \(the dashed ones\) are left out\./.test(big.line()), 'the cap: the words say how many fit and how many are left out: ' + big.line());
    big.part.painter.end();
    check(/^Put 250 ants/.test(big.line()) && /\d+ more did not fit in one fill: drag the same box again for them\./.test(big.line()) && big.ed.file.markers.length === open('Original-Ants/Maps/TREASURE.LVL').markers.length + 250, 'let go: 250 ants are put and the line says how many did not fit: ' + big.line());
    // the team and the ant type of the Ants tool
    big.press('t3'); big.press('t0');
    equal(big.log.teams, [3, 0], 'the team buttons choose the team');
    check(big.part.pane.querySelectorAll('.fb-type').length === 0, 'a Classic map has no ant types');
    const ex = partPage(File.parse(toV9(open(MEDIUM).bytes)), 'ants');
    equal(ex.part.pane.querySelectorAll('.fb-type').length, 6, 'an Extended map has the six ant types');
    ex.press('at5');
    equal(ex.log.types, [5], 'and a button chooses one');
    ex.drag([20, 20], [24, 24]);
    check(ex.ed.file.markers.slice(-5).every((m) => m.antType === 5), 'the ants are put with the chosen type');
}

// ---- F ----
console.log('RUNNING: F the switch in the tools');
function toolsPage(opts) {
    opts = opts || {};
    const doc = makeDocument(), el = (t) => doc.createElement(t), file0 = open(MEDIUM), host = { tools: [], brushes: [] }, said = [], store = opts.store || {}, loads = [];
    const panel = el('div'), cv = el('div'), map = el('canvas'), side = el('aside');
    for (const k of ['box', 'title', 'swatches', 'name', 'tile', 'hint', 'fill', 'undo', 'redo', 'stepper', 'stepperLabel', 'stepLess', 'stepValue', 'stepMore', 'fillRow', 'fillBtn']) host[k] = el(k === 'swatches' || k === 'box' || k === 'stepper' || k === 'fillRow' ? 'div' : (k === 'title' ? 'h3' : (k === 'hint' || k === 'name' ? 'p' : (k === 'stepValue' ? 'output' : 'button'))));
    host.tile.appendChild(el('i')); host.tile.appendChild(el('span'));
    for (const k of ['box', 'tile', 'hint', 'fillRow']) panel.appendChild(host[k]);
    cv.appendChild(map); host.map = map; host.side = side; host.autoEdges = [];
    for (const t of Palette.TOOLS) { const b = el('button'); b.setAttribute('data-tool', t.id); host.tools.push(b); }
    for (const n of [1, 3, 5, 9]) { const b = el('button'); b.setAttribute('data-brush', String(n)); host.brushes.push(b); }
    host.fillRow.hidden = true; host.fillBtn.setAttribute('aria-checked', 'false');
    doc.body.appendChild(panel); doc.body.appendChild(cv); doc.body.appendChild(side);
    let current = file0, painter = null, stroking = false;
    const canvas = { setPainter: (p) => { painter = p; }, refresh() {}, markDirty() {}, refreshMini() {}, isStroking: () => stroking, cancelStroke() {}, state: () => ({}) };
    const win = { localStorage: { getItem: (k) => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = v; } } };
    const tools = Tools.create({
        doc, win, canvas, host, file: () => current, busy: () => false, dialogs: null, say: (t, m) => said.push(t + ': ' + m), rebase: (b) => { current = File.parse(b); return current; }, onEdit() {},
        loadFill: (done) => { loads.push(done); if (!opts.hold) done(opts.fail ? null : Fill); }
    });
    tools.setMap(current);
    const button = (id) => host.tools.filter((b) => b.getAttribute('data-tool') === id)[0];
    return { doc, host, tools, painter: () => painter, said, store, loads, button, win, file: () => current, side, stroke: (v) => { stroking = v; } };
}
{
    const p = toolsPage();
    check(p.tools.state().fillBox === false && p.host.fillRow.hidden, 'the switch is off at the start, and Ground has no line for it');
    for (const tool of ['bigger', 'hills', 'erase', 'pickup', 'pan', 'ground']) { p.button(tool).click(); check(p.host.fillRow.hidden, 'Fill a box has no line for ' + tool); }
    for (const tool of ['objects', 'food', 'ants']) { p.button(tool).click(); check(!p.host.fillRow.hidden && !p.host.fillBtn.disabled, 'Fill a box has its line for ' + tool); }
    p.button('objects').click();
    // off: a drag paints a line of rocks as before
    p.host.swatches.children[0] && p.host.swatches.children[0].click();
    const pa = p.painter();
    check(pa.size() === 1 && pa.tapOnly({ row: 5, column: 5 }) === false, 'objects, switch off: the brush paints (a drag is a line)');
    p.host.swatches.children.filter((b) => b.getAttribute('data-tile') === '404')[0].click();
    check(pa.ghost() === 404, 'a plant is shown under the pointer while the switch is off');
    p.host.fillBtn.click();
    check(pa.ghost() === 0, 'and is not while the switch is on: the box has its own pictures');
    p.host.swatches.children.filter((b) => b.getAttribute('data-tile') === '129')[0].click();
    check(p.tools.state().fillBox === true && p.host.fillBtn.getAttribute('aria-checked') === 'true' && p.store.ants_mapmaker_fill_box === '1' && p.loads.length === 1, 'the switch turns on, is remembered and asks for the part');
    check(!!Fill.create && p.side.className.indexOf('fb-on') >= 0, 'the part shows its box in the right-hand column');
    const before = p.tools.state().history.strokes;
    pa.begin({ row: 40, column: 25 }); pa.extend({ row: 40, column: 25 }, { row: 47, column: 36 }); pa.end();
    const after = p.tools.state().history.strokes;
    check(after === before + 1, 'on: a drag is a box, and it fills as one step of the history (' + before + ' to ' + after + ')');
    const bombs = []; for (let i = 0; i < p.file().cells; i++) if (Edit.tile(p.file(), 2, i) === 129 && Edit.tile(open(MEDIUM), 2, i) === EMPTY2) bombs.push({ r: Math.floor(i / p.file().columns), c: i % p.file().columns });
    equal(key(bombs), key(Fill.plan(open(MEDIUM), Fill.objectThing(129), { r0: 40, c0: 25, r1: 47, c1: 36 }, { pattern: 'checker' }).cells), 'the bombs are the cells of the box on the chosen pattern, not a line');
    p.tools.undo();
    check(p.tools.state().history.strokes === before, 'and one Undo takes the whole fill away');
    p.button('bigger').click();
    check(p.host.fillRow.hidden && p.side.className.indexOf('fb-on') < 0, 'Bigger has no Fill a box: its line and the box go');
    p.button('objects').click();
    check(p.side.className.indexOf('fb-on') >= 0 && p.host.hint.textContent === 'Drag a box on the map. Let go, and it fills.', 'back on Objects the box and its hint are there again');
    p.host.fillBtn.click();
    check(p.tools.state().fillBox === false && p.store.ants_mapmaker_fill_box === '0' && p.side.className.indexOf('fb-on') < 0, 'turned off again');
    // remembered: a page that opens again with the switch on
    const q = toolsPage({ store: { ants_mapmaker_fill_box: '1' } });
    check(q.tools.state().fillBox === true && q.loads.length === 0, 'a switch that was on is on when the page opens again, and the part is asked for when a tool needs it');
    q.button('objects').click();
    check(q.loads.length === 1 && q.side.className.indexOf('fb-on') >= 0, 'the Objects tool asks for it');
    q.button('ants').click();
    check(q.loads.length === 1, 'and it is asked for once');
    // a part that does not load: said in plain words, and the switch is off
    const bad = toolsPage({ fail: true });
    bad.button('objects').click(); bad.host.fillBtn.click();
    check(bad.said.length === 1 && /did not load/.test(bad.said[0]) && bad.tools.state().fillBox === false, 'a part that did not load: said, and the switch is off');
    check(bad.painter().size() === 1 && bad.painter().tapOnly({ row: 3, column: 3 }) === false, 'and the tool paints as before');
    // a drag that is going on: the tools take no other tool
    const held = toolsPage({ hold: true });
    held.button('objects').click(); held.host.fillBtn.click();
    check(held.loads.length === 1 && held.painter().size() === 1, 'while the part is on its way the tool works as before');
    held.loads[0](Fill);
    check(held.side.className.indexOf('fb-on') >= 0, 'and takes the box when it arrives');
}

console.log('fill check: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
