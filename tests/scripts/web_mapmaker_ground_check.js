#!/usr/bin/env node
// The map maker's kinds of ground (web/front/mapmaker_ground.js, the flat colors of mapmaker_tiles.js, the five chips of mapmaker_palette.js; docs/MAP_EDITOR.md, "Ground") held to the game's list of
// names, to the facts of the 239 ground tiles and to the 540 maps of the library, with no browser:
//
//   node tests/scripts/web_mapmaker_ground_check.js [--front DIR]
//
//   --front DIR   the folder of the mapmaker_*.js files to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
//   A  the names are the game's (src/ants_assets/lvl_tile_names.inc, ids 431 to 669) and every one of the 239 is classified: five kinds only (Gravel, Dirt, Slate, Mud, Water), plain or an edge with
//      the other kind and a shape from 2 to 9; the canvas's own table of kinds (mapmaker_tiles.js) says the same for every id; no word of grass or sand is left
//   B  the facts: 31 plain tiles and 208 edge tiles; the 12 pairs of land kinds and mud-water, water-mud; 8 shapes for every pair, each an "a" tile (and, but for water, a "b" look-alike);
//      water has edges against mud only; the three dirt-slate tiles that no map uses
//   C  the library: every ground cell of the 540 maps is one of the 239 (but the few stray ids), all five kinds are used, and a cell's edge tile has the other kind on the side that its shape names
//      (a side piece: that neighbor; a corner piece: both neighbors of that corner), in at least 80% of the cells for every shape: the shapes of the table are the maps' own
//   D  the palette: five chips, the plain tile of each, the names of the game; the chips' flat colors are those of their kinds (and never green or sand-colored)
//   E  the sheet of a kind on a small fake of the DOM: the plain tile, then for each other kind a compass of the 8 pieces in the order NW N NE W E SW S SE with the plain tile in the middle, the
//      look-alikes in one line, the gold ring (aria-checked) on the chosen tile, any tile can be chosen, the brush row; water has the one group (mud)
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const Ground = load('mapmaker_ground.js'), Tiles = load('mapmaker_tiles.js'), Palette = load('mapmaker_palette.js'), File = load('mapmaker_file.js');
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }

const names = fs.readFileSync(path.join(ROOT, 'src', 'ants_assets', 'lvl_tile_names.inc'), 'utf8').split('\n').filter((l) => l.startsWith('"')).map((l) => l.replace(/^"/, '').replace(/",?\s*$/, ''));
const KINDS = ['gravel', 'dirt', 'slate', 'mud', 'water'];
const ids = [];
for (let id = 431; id <= 669; id++) ids.push(id);

function caseA() {
    check(Ground.FIRST === 431 && Ground.LAST === 669 && Ground.NAMES.length === 239, 'the table has 239 names, ids 431 to 669');
    for (const id of ids) {
        check(Ground.name(id) === names[id], 'tile ' + id + ' is ' + names[id] + ' in the game\'s list, and ' + Ground.name(id) + ' here');
        const t = Ground.info(id);
        check(!!t && KINDS.includes(t.kind) && t.shape >= 1 && t.shape <= 9 && (t.shape === 1) === (t.other === null) && (t.other === null || (KINDS.includes(t.other) && t.other !== t.kind)), 'tile ' + id + ' (' + names[id] + ') is classified: ' + JSON.stringify(t));
        check(Tiles.kindOf(id) === Ground.kindOf(id) && Tiles.kindOf(id) === t.kind, 'the canvas\'s table of kinds agrees about ' + names[id] + ': ' + Tiles.kindOf(id) + ' and ' + t.kind);
        const letter = names[id].charAt(0).toLowerCase();
        check({ g: 'gravel', d: 'dirt', s: 'slate', m: 'mud', w: 'water' }[letter] === t.kind, names[id] + ': the first letter is the kind');
    }
    check(Ground.info(430) === null && Ground.info(670) === null && Ground.kindOf(485) === 'gravel' && Tiles.kindOf(100) === null, 'ids outside the ground are not ground');
    equal(Ground.KINDS.map((k) => k.key), KINDS, 'the five kinds, in the order of the palette');
    equal(Ground.KINDS.map((k) => k.label), ['Gravel', 'Dirt', 'Slate', 'Mud', 'Water'], 'with their names');
    equal(Ground.KINDS.map((k) => k.plain), [485, 487, 571, 439, 476], 'and their plain tiles: g01a, d01a, s01a, M01a, w01a');
    const words = (Ground.KINDS.map((k) => k.label).join(' ') + ' ' + Palette.GROUND.map((r) => r[2]).join(' ')).toLowerCase();
    check(!/grass|sand|dirt 2|gravel 2/.test(words), 'no grass, no sand, no second Dirt in the labels');
    for (const f of ['mapmaker_tiles.js', 'mapmaker_palette.js', 'mapmaker_ground.js']) {
        const text = fs.readFileSync(path.join(FRONT, f), 'utf8');
        check(!/\bgrass: \[|\bsand: \[|'grass'|'sand'|\bSand\b|\bGrass\b/.test(text.replace(/grass\d|grassmed|grassbig|'Grass 2'/g, '')), f + ' has no ground called grass or sand');
    }
}

function caseB() {
    const plain = ids.filter((id) => !Ground.info(id).other), edge = ids.filter((id) => Ground.info(id).other);
    check(plain.length === 31 && edge.length === 208, '31 plain tiles and 208 edge tiles (' + plain.length + ', ' + edge.length + ')');
    const pairs = {};
    for (const id of edge) { const t = Ground.info(id), k = t.kind + '>' + t.other; pairs[k] = (pairs[k] || 0) + 1; }
    const sixteen = ['gravel>dirt', 'gravel>slate', 'gravel>mud', 'dirt>gravel', 'dirt>slate', 'dirt>mud', 'slate>gravel', 'slate>dirt', 'slate>mud', 'mud>gravel', 'mud>dirt', 'mud>slate'];
    const want = {};
    for (const k of sixteen) want[k] = 16;
    want['mud>water'] = 8; want['water>mud'] = 8;
    equal(Object.entries(pairs).sort(), Object.entries(want).sort(), 'the pairs that have edge tiles, and how many tiles each');
    equal(Ground.others('water'), ['mud'], 'water has edges against mud only: no water-gravel, water-dirt or water-slate tile');
    equal(Ground.others('mud'), ['gravel', 'dirt', 'slate', 'water'], 'mud has four neighbors');
    equal(Ground.others('gravel'), ['dirt', 'slate', 'mud'], 'gravel has three');
    for (const key of KINDS) for (const other of Ground.others(key)) {
        const list = Ground.edges(key, other);
        equal(list.map((e) => e.shape), [2, 3, 4, 5, 6, 7, 8, 9], key + ' next to ' + other + ': the eight shapes in the compass order');
        check(list.every((e) => e.id > 0 && Ground.info(e.id).variant === 'a' && Ground.info(e.id).other === other && Ground.info(e.id).kind === key), key + ' next to ' + other + ': an "a" tile for each shape');
        check(list.every((e) => e.variants.length === (key === 'water' || other === 'water' ? 0 : 1)), key + ' next to ' + other + ': a "b" look-alike for each, but for water');
    }
    equal(Object.keys(Ground.UNUSED).map(Number).sort(), [527, 533, 537], 'the three dirt-slate tiles that no map uses');
    equal([527, 533, 537].map((id) => Ground.name(id)), ['ds04a', 'ds07a', 'ds09a'], 'ds04a, ds07a, ds09a');
    equal(Ground.plainIds('gravel'), [485, 486], 'gravel: g01a and its look-alike g01b');
    equal(Ground.plainIds('dirt'), [487, 488, 489, 490], 'dirt: d01a and three look-alikes');
    equal(Ground.plainIds('slate').length, 9, 'slate: s01a to s01i');
    equal(Ground.plainIds('mud').length, 15, 'mud: M01a first, then 14 that look the same');
    equal(Ground.plainIds('water'), [476], 'water: one plain tile');
    equal(Ground.label(485), 'Gravel', 'a plain tile is called by its kind');
    equal(Ground.label(Ground.edges('gravel', 'dirt')[1].id), 'Gravel next to dirt: north edge', 'an edge piece says what it is');
    equal(Ground.label(Ground.edges('mud', 'water')[0].id), 'Mud next to water: north-west corner', 'and a corner piece');
}

function caseC() {
    // the neighbors that must be the other kind: a side piece, one side; a corner piece, the two sides of that corner (north and west for the north-west piece)
    const NEEDS = { 2: [[-1, 0], [0, -1]], 3: [[-1, 0]], 4: [[-1, 0], [0, 1]], 5: [[0, -1]], 6: [[0, 1]], 7: [[1, 0], [0, -1]], 8: [[1, 0]], 9: [[1, 0], [0, 1]] };
    const stat = {}, used = new Set();
    let stray = 0, cells = 0, maps = 0;
    for (const dir of ['Original-Ants/Maps', 'Community-Maps']) {
        for (const name of fs.readdirSync(path.join(ROOT, dir)).sort()) {
            if (!/\.lvl$/i.test(name)) continue;
            const f = File.parse(new Uint8Array(fs.readFileSync(path.join(ROOT, dir, name))));
            maps++;
            for (let r = 0; r < f.rows; r++) for (let c = 0; c < f.columns; c++) {
                const id = f.tile1(r, c);
                cells++;
                if (id < 431 || id > 669) { stray++; continue; }
                const t = Ground.info(id);
                used.add(t.kind);
                if (!t.other || t.variant !== 'a' || r < 1 || c < 1 || r >= f.rows - 1 || c >= f.columns - 1) continue;
                const ok = NEEDS[t.shape].every((d) => Ground.kindOf(f.tile1(r + d[0], c + d[1])) === t.other), s = stat[t.shape] = stat[t.shape] || [0, 0];
                s[1]++; if (ok) s[0]++;
            }
        }
    }
    check(maps === 540, 'the library has 540 maps (' + maps + ')');
    check(stray < 0.002 * cells, 'a few stray ids only: ' + stray + ' of ' + cells + ' cells are not ground');
    equal(Array.from(used).sort(), KINDS.slice().sort(), 'the maps use all five kinds, and no other');
    for (const shape of Object.keys(NEEDS)) check(stat[shape] && stat[shape][1] > 5000 && stat[shape][0] / stat[shape][1] >= 0.8, 'shape ' + shape + ': the other kind is where the name says in ' + (stat[shape] ? Math.round(100 * stat[shape][0] / stat[shape][1]) : 0) + '% of ' + (stat[shape] || [0, 0])[1] + ' cells');
}

function caseD() {
    const items = Palette.items('ground');
    equal(items.map((i) => [i.id, i.name, i.label, i.kind]), [[485, 'g01a', 'Gravel', 'gravel'], [487, 'd01a', 'Dirt', 'dirt'], [571, 's01a', 'Slate', 'slate'], [439, 'M01a', 'Mud', 'mud'], [476, 'w01a', 'Water', 'water']], 'the five chips');
    for (const i of items) check(names[i.id] === i.name && Ground.KINDS.some((k) => k.plain === i.id && k.label === i.label), i.label + ' paints the plain tile of its kind, named as the game names it');
    const rgb = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
    for (const i of items) check(Tiles.css(i.id, Tiles.EMPTY2) === i.color, i.label + ': the chip is the color the canvas draws it in');
    const [gr, gg, gb] = rgb(Tiles.css(485, Tiles.EMPTY2)), [dr, dg, db] = rgb(Tiles.css(487, Tiles.EMPTY2)), [sr, sg, sb] = rgb(Tiles.css(571, Tiles.EMPTY2)), [mr, mg, mb] = rgb(Tiles.css(439, Tiles.EMPTY2)), [wr, wg, wb] = rgb(Tiles.css(476, Tiles.EMPTY2));
    check(gr > gg && gg >= gb - 12 && gr < 190, 'gravel is brown-gray, not green (' + [gr, gg, gb] + ')');
    check(dr > 190 && dg > 180 && db > 160 && dr - db < 40, 'dirt is cream (' + [dr, dg, db] + ')');
    check(sb > sr && sb > sg && sb < 200, 'slate is blue-gray stone (' + [sr, sg, sb] + ')');
    check(Math.max(mr, mg, mb) < 100 && Math.max(mr, mg, mb) - Math.min(mr, mg, mb) < 12, 'mud is dark gray (' + [mr, mg, mb] + ')');
    check(wb > wr + 40 && wb > wg + 20, 'water is blue (' + [wr, wg, wb] + ')');
    equal(new Set(items.map((i) => i.color)).size, 5, 'five different colors');
    // the edge and corner tiles of a kind are drawn in that kind's colors (flat), and every id has one
    for (const id of ids) check(/^#[0-9a-f]{6}$/.test(Tiles.css(id, Tiles.EMPTY2)), 'tile ' + id + ' has a flat color');
    // the pictures' averages replace the flat colors, and nothing else: hex digits for each ground id, and the objects' own
    const keep = Tiles.css(487, Tiles.EMPTY2), keepObject = Tiles.css(0, 302);
    Tiles.packedTables();                  // (made before the averages come, as the canvas has made them by then)
    Tiles.setAverages('010203' + '0a0b0c', { 302: 0x102030 });
    check(Tiles.css(431, Tiles.EMPTY2) === '#010203' && Tiles.css(432, Tiles.EMPTY2) === '#0a0b0c' && Tiles.css(487, Tiles.EMPTY2) === keep && Tiles.css(0, 302) === '#102030', 'average colors replace the flat ones for the ids that they name');
    const p = Tiles.packedTables();
    check(p.ground[431] === Tiles.hexToPacked('#010203') && p.objects[302] === Tiles.hexToPacked('#102030'), 'and the packed tables of the pixel loop follow');
    check(keepObject !== '#102030', 'the object had another color before');
}

function caseE() {
    const doc = makeDocument();
    function run(kind, chosen) {
        const body = doc.createElement('div'), asked = { chosen, picked: [], pics: [], brush: 1 };
        Ground.sheet({
            doc, kind, chosen: () => asked.chosen, choose: (id) => { asked.chosen = id; asked.picked.push(id); }, pic: (node, id, px) => { asked.pics.push([id, px]); },
            brush: () => asked.brush, setBrush: (n) => { asked.brush = n; }
        }, body);
        return { body, asked };
    }
    const heads = (b) => b.querySelectorAll('.kgroup').map((x) => x.querySelector('h3').textContent);
    const ringed = (b) => b.querySelectorAll('.gt, .gl').filter((x) => x.getAttribute('aria-checked') === 'true').map((x) => +x.getAttribute('data-tile'));
    const g = run('gravel', 485);
    equal(heads(g.body), ['Plain gravel', 'Gravel next to Dirt', 'Gravel next to Slate', 'Gravel next to Mud'], 'gravel: the plain tile, then each other kind');
    const compasses = g.body.querySelectorAll('.compass');
    check(compasses.length === 3 && compasses.every((c) => c.children.length === 9 && c.children[4].className.split(' ').includes('mid')), 'each other kind is a compass of 9 cells with the plain tile in the middle');
    equal(compasses[0].children.filter((c, i) => i !== 4).map((c) => Ground.name(+c.getAttribute('data-tile'))), ['gd02a', 'gd03a', 'gd04a', 'gd05a', 'gd06a', 'gd07a', 'gd08a', 'gd09a'], 'the compass reads NW N NE W E SW S SE');
    equal(compasses[2].children.filter((c, i) => i !== 4).map((c) => Ground.name(+c.getAttribute('data-tile'))), ['gm02a', 'gm03a', 'gm04a', 'gm05a', 'gm06a', 'gm07a', 'gm08a', 'gm09a'], 'the same for mud');
    check(compasses[0].children[0].getAttribute('aria-label') === 'gd02a: Gravel next to dirt: north-west corner', 'a chip says its name and what it is: ' + compasses[0].children[0].getAttribute('aria-label'));
    check(compasses[0].children[4].getAttribute('aria-hidden') === 'true' && compasses[0].children[4].getAttribute('data-tile') === null, 'the middle cell is only a picture of the plain tile');
    const lines = g.body.querySelectorAll('.kmore');
    equal(lines.map((l) => l.textContent), ['1 more looks the same:g01b', 'The same look:gd02bgd03bgd04bgd05bgd06bgd07bgd08bgd09b', 'The same look:gs02bgs03bgs04bgs05bgs06bgs07bgs08bgs09b', 'The same look:gm02bgm03bgm04bgm05bgm06bgm07bgm08bgm09b'], 'the look-alikes are listed in one line each, not as chips');
    equal(ringed(g.body), [485], 'the gold ring is on the chosen tile');
    check(g.asked.pics.length === 1 + 3 * 9 && g.asked.pics.every((p) => p[1] === 52), 'every tile has its picture, 52 pixels');
    compasses[0].children[1].click();
    equal([g.asked.picked, ringed(g.body)], [[503], [503]], 'any tile can be chosen: gd03a, and the ring moves to it');
    g.body.querySelectorAll('.gl')[0].click();
    equal([g.asked.picked, ringed(g.body)], [[503, 486], [486]], 'a look-alike too (g01b)');
    const brange = g.body.querySelectorAll('.kbrush input')[0], bval = g.body.querySelectorAll('.kbrush output')[0];
    equal([brange.min, brange.max, brange.value, bval.textContent], ['1', '9', '1', '1'], 'the brush slider: 1 to 9');
    brange.value = '5';
    brange.fire('input', {});
    check(g.asked.brush === 5 && brange.getAttribute('data-brush') === '5' && bval.textContent === '5', 'moving the brush slider sets the size and updates its value');
    const m = run('mud', 439);
    equal(heads(m.body), ['Plain mud', 'Mud next to Gravel', 'Mud next to Dirt', 'Mud next to Slate', 'Mud next to Water'], 'mud has the most neighbors, water among them');
    check(m.body.querySelectorAll('.kmore')[0].textContent.indexOf('14 more look the same:') === 0 && m.body.querySelectorAll('.kmore')[0].querySelectorAll('.gl').length === 14, 'and 14 plain tiles that look alike, in one line');
    check(m.body.querySelectorAll('.kmore').length === 4 && m.body.querySelectorAll('.compass')[3].children.every((c, i) => i === 4 || Ground.info(+c.getAttribute('data-tile')).other === 'water'), 'water pieces have no look-alike line (mud-water has one tile each)');
    const w = run('water', 476);
    equal(heads(w.body), ['Plain water', 'Water next to Mud'], 'water: edges against mud only');
    check(w.body.querySelectorAll('.kmore').length === 0, 'and no look-alikes at all');
    const d = run('dirt', 487), ds = d.body.querySelectorAll('.compass')[1].children;
    equal(ds.filter((c, i) => i !== 4 && c.className.split(' ').includes('dim')).map((c) => Ground.name(+c.getAttribute('data-tile'))), ['ds04a', 'ds07a', 'ds09a'], 'dirt next to slate: the three tiles that no map uses are dimmed');
    check(ds[2].getAttribute('aria-label').indexOf('(no map uses it)') > 0, 'and say so to a screen reader');
}

const cases = [['A the names and the kinds', caseA], ['B the facts', caseB], ['C the library', caseC], ['D the palette and the colors', caseD], ['E the sheet of a kind', caseE]];
for (const [name, fn] of cases) {
    const before = failures;
    console.log('RUNNING: ' + name + ' ...');
    try { fn(); } catch (e) { failures++; console.log('    CRASHED: ' + (e && e.stack || e)); }
    console.log(failures === before ? 'RUNNING: ' + name + ' ... PASS' : 'RUNNING: ' + name + ' ... FAIL');
}
console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
