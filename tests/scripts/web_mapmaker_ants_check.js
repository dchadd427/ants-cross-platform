#!/usr/bin/env node
// The map maker's Ants tool box (web/front/mapmaker_ants.js; docs/MAP_EDITOR.md, "Ants: one at a time, group, mix") held to the library's maps, with the real reader, blocks and history and a small
// fake of the DOM (no browser, no map core: the real core's verdict is the browser part `ants`):
//
//   node tests/scripts/web_mapmaker_ants_check.js [--front DIR]
//
//   A  one ant put and taken away, on real maps: the count, the record, every other byte as it was, Undo and Redo give the same bytes, a refusal says why (solid ground, an object, a hill, a
//      cell that has an ant) and changes nothing
//   B  the mix: the same plan every time, exactly `per` ants for each team, no two ants side by side with every other cell, a box that is too small is refused with the numbers, one Undo
//   C  the group (clump) of before is byte for byte what it was (a pinned digest), and Clear <team> ants is one step
//   D  the limit of the file (65,535 records) and the warnings (a team with none, too many, packed too tight)
//   E  the box in the page on a fake DOM: the switch, a tap, Take one away, a mix and its Put button, Clear with its question, one change for each
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Edit = load('mapmaker_edit.js'), Blocks = load('mapmaker_blocks.js'), Ants = load('mapmaker_ants.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function same(a, b) { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; }
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
function open(rel) { const f = File.parse(new Uint8Array(fs.readFileSync(path.join(ROOT, rel)))); if (!f.ok) throw new Error(rel); return f; }
function editor(file) { return Edit.create(file, { rebase: (bytes) => File.parse(bytes), erase: Blocks.eraseAt, guard: Blocks.blockedText }); }
function count(file) { return Blocks.layout(file).records.length; }
function ants(file) { return Blocks.facts(file, false).ants; }
function free(file, n, from) {         // n cells where an ant may start
    const out = [];
    for (let i = from || 0; i < file.cells && out.length < n; i++) { const r = Math.floor(i / file.columns), c = i % file.columns; if (!Ants.whyNot(file, i) && Ants.teamAt(file, r, c) < 0) out.push([r, c]); }
    return out;
}

const MAPS = ['Original-Ants/Maps/TINY.LVL', 'Original-Ants/Maps/MEDIUM.LVL', 'Original-Ants/Maps/TREASURE.LVL'];

// A
for (const rel of MAPS) {
    const f0 = open(rel), ed = editor(f0), before = Uint8Array.from(f0.bytes), n0 = count(f0), a0 = ants(f0).slice();
    const [cell] = free(f0, 1, f0.columns * 3 + 3);
    const ch = Ants.putOne(f0, 2, cell[0], cell[1]);
    check(!ch.error, rel + ': one ant can be put on a free cell');
    check(!!ed.act(ch), rel + ': the change is one step');
    let f = ed.file;
    check(count(f) === n0 + 1 && ants(f)[2] === a0[2] + 1 && Ants.teamAt(f, cell[0], cell[1]) === 2, rel + ': one more record, of the right team, on the cell');
    const after = Uint8Array.from(f.bytes);
    ed.undo();
    check(same(Blocks.block1Bytes(Blocks.layout(ed.file).records), Blocks.block1Bytes(Blocks.layout(f0).records)), rel + ': undo gives the list back, record for record');
    check(count(ed.file) === n0 && Ants.teamAt(ed.file, cell[0], cell[1]) === -1, rel + ': Undo takes it away');
    ed.redo(); check(same(ed.file.bytes, after), rel + ': Redo gives the same bytes');
    // every byte outside block 1's count and the new record is as before (the dictionary may hold a marker name more)
    check(Blocks.layout(ed.file).records.slice(0, n0).every((r, i) => { const o = Blocks.layout(f0).records[i]; return r.tile === o.tile && r.row === o.row && r.column === o.column; }), rel + ': the records that were there are as they were');
    const dup = Ants.putOne(ed.file, 1, cell[0], cell[1]);
    check(dup.error && /already/.test(dup.error), rel + ': a cell that has an ant is refused');
    const take = Ants.takeOne(ed.file, cell[0], cell[1]);
    check(take && !!ed.act(take) && count(ed.file) === n0 && Ants.teamAt(ed.file, cell[0], cell[1]) === -1, rel + ': a tap on the ant takes it away (any team)');
    check(Ants.takeOne(ed.file, cell[0], cell[1]) === null, rel + ': nothing to take on an empty cell');
    // refusals
    const lay = Blocks.layout(f0);
    let solid = -1, object = -1, hill = -1;
    for (let i = 0; i < f0.cells; i++) {
        if (solid < 0 && (Edit.flags(f0, 1, i) & 1) && Edit.tile(f0, 2, i) === Blocks.EMPTY2) solid = i;
        if (object < 0 && Edit.tile(f0, 2, i) !== Blocks.EMPTY2 && Blocks.teamOfHill(Edit.tile(f0, 2, i)) < 0) object = i;
        if (hill < 0 && Blocks.teamOfHill(Edit.tile(f0, 2, i)) >= 0) hill = i;
    }
    for (const [name, i] of [['solid ground', solid], ['an object', object], ['a hill', hill]]) {
        if (i < 0) continue;
        const bad = Ants.putOne(f0, 0, Math.floor(i / f0.columns), i % f0.columns);
        check(bad.error && bad.error.length > 20 && !bad.splices && (name !== 'a hill' || /hill/.test(bad.error)), rel + ': ' + name + ' is refused with a sentence');
    }
    check(Ants.putOne(f0, 0, f0.rows, 0).error && Ants.putOne(f0, 0, 0, -1).error, rel + ': a cell outside the map is refused');
    check(same(f0.bytes, before), rel + ': refusals change nothing');
}

// B
{
    const f0 = open('Original-Ants/Maps/TREASURE.LVL'), ed = editor(f0), before = Uint8Array.from(f0.bytes);
    const box = { r0: 4, c0: 4, r1: 40, c1: 50 }, all = [0, 1, 2, 3];
    const p1 = Ants.planMix(f0, box, all, 25, 2), p2 = Ants.planMix(f0, box, all, 25, 2);
    check(p1.ok && JSON.stringify(p1.cells) === JSON.stringify(p2.cells), 'mix: the same plan every time');
    const per = [0, 0, 0, 0]; p1.cells.forEach((c) => per[c.team]++);
    check(per.every((n) => n === 25) && p1.cells.length === 100, 'mix: exactly 25 ants for each team');
    check(p1.cells.every((c) => ((c.r - box.r0) + (c.c - box.c0)) % 2 === 0), 'mix: every other cell is a chessboard from the corner of the box');
    const set = new Set(p1.cells.map((c) => c.r * 1000 + c.c));
    check(set.size === 100 && p1.cells.every((c) => !set.has((c.r + 1) * 1000 + c.c) && !set.has(c.r * 1000 + c.c + 1)), 'mix: every other cell, no two ants side by side, no cell twice');
    check(p1.cells.every((c) => c.r >= box.r0 && c.r <= box.r1 && c.c >= box.c0 && c.c <= box.c1 && !Ants.whyNot(f0, c.r * f0.columns + c.c)), 'mix: every ant is inside the box on a free cell');
    const p3 = Ants.planMix(f0, box, [3, 2], 7, 1);
    check(p3.ok && p3.cells.length === 14 && p3.cells.filter((c) => c.team === 3).length === 7 && p3.cells.filter((c) => c.team === 2).length === 7, 'mix: two teams, seven ants each');
    // measured mixing: no row and no column is one team, few neighbors share a team, the ants are spread over the box, for widths 16 to 24 and both spacings
    for (let width = 16; width <= 24; width++) for (const gap of [1, 2]) {
        const bx = { r0: 3, c0: 3, r1: 3 + 21, c1: 3 + width - 1 }, q = Ants.planMix(f0, bx, all, 25, gap);
        if (!q.ok) { check(false, 'mix ' + width + ' wide, gap ' + gap + ' is planned'); continue; }
        const rowsOf = {}, colsOf = {}, at = {}, cnt = {};
        q.cells.forEach((c) => { (rowsOf[c.r] = rowsOf[c.r] || new Set()).add(c.team); (colsOf[c.c] = colsOf[c.c] || new Set()).add(c.team); cnt['r' + c.r] = (cnt['r' + c.r] || 0) + 1; cnt['c' + c.c] = (cnt['c' + c.c] || 0) + 1; at[c.r * 1000 + c.c] = c.team; });
        const single = Object.keys(rowsOf).filter((k) => cnt['r' + k] >= 2 && rowsOf[k].size < 2).length + Object.keys(colsOf).filter((k) => cnt['c' + k] >= 2 && colsOf[k].size < 2).length;
        let pairs = 0, same = 0;
        q.cells.forEach((c) => { for (const d of [1, 1000]) { const o = at[c.r * 1000 + c.c + d]; if (o !== undefined) { pairs++; if (o === c.team) same++; } } });
        const top = Math.min(...q.cells.map((c) => c.r)), bottom = Math.max(...q.cells.map((c) => c.r));
        check(single === 0, 'mix ' + width + ' wide, gap ' + gap + ': no row or column is one team (' + single + ')');
        check(pairs === 0 || same / pairs < 0.4, 'mix ' + width + ' wide, gap ' + gap + ': few neighbors share a team (' + same + ' of ' + pairs + ')');
        check(bottom - top >= 15, 'mix ' + width + ' wide, gap ' + gap + ': the ants are spread over the box (' + top + ' to ' + bottom + ')');
        check(JSON.stringify(Ants.planMix(f0, bx, all, 25, gap).cells) === JSON.stringify(q.cells), 'mix ' + width + ' wide, gap ' + gap + ': the same plan again');
    }
    const lineBox = Ants.planMix(f0, { r0: 4, c0: 0, r1: 4, c1: 198 }, [0], 100, 2);
    check(lineBox.ok === (lineBox.free >= 100) && /fit|need/.test(lineBox.text), 'mix: a one-row box is judged by its spaced cells');
    const edm = editor(open('Original-Ants/Maps/TREASURE.LVL')), seen = Blocks.layout(edm.file).records.filter((r) => Blocks.teamOfMarker(r.tile) >= 0).slice(0, 3);
    if (seen.length) { const bx = { r0: Math.max(0, seen[0].row - 2), c0: Math.max(0, seen[0].column - 2), r1: seen[0].row + 4, c1: seen[0].column + 4 }, room = Ants.planMix(edm.file, bx, [0, 1], 1, 1).free, q = Ants.planMix(edm.file, bx, [0, 1], Math.floor(room / 2), 1); check(q.ok && q.cells.every((c) => !(c.r === seen[0].row && c.c === seen[0].column)), 'mix: a cell that has an ant is passed over'); }
    const small = Ants.planMix(f0, { r0: 5, c0: 5, r1: 8, c1: 8 }, all, 25, 2);
    check(!small.ok && /100 ants need 200 cells; your box has 16/.test(small.text) && /bigger box/.test(small.text), 'mix: a box that is too small is refused politely, with the numbers');
    check(Ants.putMix(f0, small).error, 'mix: the change of a refused plan is an error');
    const n0 = count(f0), a0 = ants(f0).slice();
    check(!!ed.act(Ants.putMix(f0, p1)), 'mix: one change');
    const f = ed.file, a1 = ants(f);
    check(count(f) === n0 + 100 && [0, 1, 2, 3].every((t) => a1[t] === a0[t] + 25), 'mix: 25 more of each team');
    ed.undo(); check(count(ed.file) === n0 && same(Blocks.block1Bytes(Blocks.layout(ed.file).records), Blocks.block1Bytes(Blocks.layout(f0).records)), 'mix: one Undo takes all 100 away, record for record');
    check(Blocks.layout(ed.file).records.length === n0, 'mix: the list is as long as before');
}

// C
{
    const f0 = open('Original-Ants/Maps/TINY.LVL'), ed = editor(f0);
    const change = Blocks.placeStart(f0, 0, 10, 10, 5);
    ed.act(change);
    check(sha(ed.file.bytes) === '939244e62fc816d2d7332cf9ddaf1c9ebe861d32eb598aa11afb2b80bb980f91', 'group: a clump of 5 on TINY is byte for byte what it was (digest ' + sha(ed.file.bytes).slice(0, 12) + ')');
    const ed2 = editor(open('Original-Ants/Maps/TREASURE.LVL')), t = Blocks.teamOfMarker(Blocks.layout(ed2.file).records.find((r) => Blocks.teamOfMarker(r.tile) >= 0).tile);
    const had = ants(ed2.file)[t], total = count(ed2.file);
    check(had > 0 && !!ed2.act(Ants.clearTeam(ed2.file, t)) && ants(ed2.file)[t] === 0 && count(ed2.file) === total - had, 'clear: all ants of one team go, in one step, the others stay');
    ed2.undo(); check(ants(ed2.file)[t] === had, 'clear: one Undo brings them back');
    const none = editor(open('Original-Ants/Maps/TINY.LVL')); none.act(Ants.clearTeam(none.file, 0));
    check(Ants.clearTeam(none.file, 0).splices.length === 0, 'clear: a team with no ants gives an empty change');
}

// D
{
    const f0 = open('Original-Ants/Maps/TINY.LVL'), ed = editor(f0), lay = Blocks.layout(f0);
    const plants = new Uint8Array(6 * (65530 - lay.records.length)), at = lay.b1.length;
    const full = { names: [], cells: [], splices: [{ rel: lay.b1.length, removed: new Uint8Array(0), inserted: plants }, { rel: 0, removed: f0.bytes.slice(f0.blocksAt, f0.blocksAt + 2), inserted: new Uint8Array([0xFA, 0xFF]) }] };
    ed.act(full);
    check(count(ed.file) === 65530, 'limit: a file of 65,530 records');
    const [c1] = free(ed.file, 1, 700);
    check(!Ants.putOne(ed.file, 1, c1[0], c1[1]).error, 'limit: one more fits');
    const plan = { ok: true, cells: [0, 1, 2, 3, 4, 5].map((i) => ({ r: i, c: 0, team: 0 })) };
    const err = Ants.putMix(ed.file, plan).error;
    check(err && /65,535 at most/.test(err) && /Take 1 ant away/.test(err), 'limit: a mix over 65,535 is refused with the numbers (' + err + ')');
    const s = Ants.summary(ed.file, plan.cells);
    check(s.over && s.lines[0].kind === 'bad', 'limit: the warning is red');
    const w = Ants.summary(f0, null);
    check(w.missing.length === 0 ? w.lines[0].kind === 'ok' : w.lines[0].kind === 'warn', 'warnings: a team with none is amber, all four is the good line');
    const none = Ants.summary(open('Original-Ants/Maps/TINY.LVL'), null);
    check(typeof none.packed === 'number', 'warnings: the packed count is a number');
    check(!Ants.summary(ed.file, null).over, 'limit: 65,530 records is not over');
    ed.act({ names: [], cells: [], splices: [{ rel: ed.file.bytes.length - ed.file.blocksAt >= 0 ? Blocks.layout(ed.file).b1.length : 0, removed: new Uint8Array(0), inserted: new Uint8Array(30) }, { rel: 0, removed: ed.file.bytes.slice(ed.file.blocksAt, ed.file.blocksAt + 2), inserted: new Uint8Array([0xFF, 0xFF]) }] });
    check(count(ed.file) === 65535 && Ants.putOne(ed.file, 1, c1[0], c1[1]).error && /65,535/.test(Ants.putOne(ed.file, 1, c1[0], c1[1]).error), 'limit: with 65,535 records not one more ant is put');
    check(!Ants.summary(ed.file, null).over && Ants.summary(ed.file, [{ r: 0, c: 0 }]).over, 'limit: 65,535 is not over, 65,536 is');
    const gone = open('Original-Ants/Maps/TREASURE.LVL'), edg = editor(gone), t0 = Blocks.teamOfMarker(Blocks.layout(gone).records.find((r) => Blocks.teamOfMarker(r.tile) >= 0).tile);
    edg.act(Ants.clearTeam(gone, t0));
    const one = Ants.summary(edg.file, null);
    check(one.missing.length === 1 && one.missing[0] === Blocks.TEAMS[t0].label && one.lines[0].kind === 'warn' && new RegExp(Blocks.TEAMS[t0].label + ' has no ants. The Check says').test(one.lines[0].text), 'warnings: one team with none is named in an amber line');
    const pileFile = open('Original-Ants/Maps/TREASURE.LVL'), pl = Blocks.layout(pileFile).piles[0];
    if (pl && pl.row < pileFile.rows && pl.column < pileFile.columns) check(Ants.putOne(pileFile, 0, pl.row, pl.column).error, 'refusal: the cell of a food pile is refused');
    const f1 = open('Original-Ants/Maps/MEDIUM.LVL');
    const tight = Ants.planMix(f1, { r0: 3, c0: 3, r1: 16, c1: 18 }, [0, 1, 2, 3], 50, 1);
    check(tight.ok && Ants.packedCount(f1, tight.cells) > 0, 'warnings: ants shoulder to shoulder give the soft hint');
    // a free 5 x 5 square: the middle ant is packed in when the other eight cells are taken, and not when one is free
    let sq = null;
    for (let i = 0; i < f1.cells && !sq; i++) { const r = Math.floor(i / f1.columns), c = i % f1.columns; let ok = r > 1 && c > 1 && r < f1.rows - 3 && c < f1.columns - 3; for (let dr = -2; dr <= 2 && ok; dr++) for (let dc = -2; dc <= 2; dc++) if (Ants.whyNot(f1, (r + dr) * f1.columns + c + dc) || Ants.teamAt(f1, r + dr, c + dc) >= 0) { ok = false; break; } if (ok) sq = [r, c]; }
    const ring = []; for (let dr = -1; dr <= 1; dr++) for (let dc = -1; dc <= 1; dc++) ring.push({ r: sq[0] + dr, c: sq[1] + dc });
    check(Ants.packedCount(f1, ring) === Ants.packedCount(f1, null) + 1, 'warnings: an ant with all eight cells round it taken is packed in');
    check(Ants.packedCount(f1, ring.filter((c) => !(c.r === sq[0] && c.c === sq[1] - 1))) === Ants.packedCount(f1, null), 'warnings: one free cell beside it and it is not');
    check(Ants.summary(Object.assign({}, f1, { rows: 120 }), null).lines.some((l) => /more than 90 rows/.test(l.text)), 'warnings: a map of more than 90 rows says that the game cannot play it yet');
    const loose = Ants.planMix(f1, { r0: 3, c0: 3, r1: 40, c1: 40 }, [0, 1, 2, 3], 10, 2);
    check(loose.ok && Ants.packedCount(f1, loose.cells) === Ants.packedCount(f1, null), 'warnings: every other cell gives none');
}

// E
{
    const doc = makeDocument(), f0 = open('Original-Ants/Maps/TREASURE.LVL'), ed = editor(f0), said = [], dialogs = [];
    const tools = doc.createElement('div'), side = doc.createElement('div'), hint = doc.createElement('p'), line = doc.createElement('div');
    line.className = 'checkline'; side.appendChild(line); tools.appendChild(hint); doc.body.appendChild(tools); doc.body.appendChild(side);
    let team = 0, clump = 4, acts = 0;
    const part = Ants.create({
        doc, win: {}, host: { tools, hint, side }, file: () => ed.file, act: (c) => { const d = ed.act(c); if (d) acts++; part.refresh(); return !!d; }, say: (t, m) => said.push(m),
        dialogs: { open: (spec) => { dialogs.push(spec); } }, team: () => team, setTeam: (t) => { team = t; }, ants: () => clump, setAnts: (n) => { clump = n; }, view: () => null, about: () => ({ ant: 'Combat ants by default', eggs: 2 }), props: () => {}, refresh: () => {}
    });
    part.show(true);
    const btn = (k) => part.panel.querySelector('[data-k="' + k + '"]');
    check(!part.panel.hidden && part.mine() && part.painter.tapOnly(), 'page: the box shows, One at a time takes the tap and a drag looks around');
    check(part.panel.textContent.indexOf('The game picks a random facing at the start of each match; the editor shows an example.') >= 0, 'page: the one line that says the facing is an example (the game picks it at random for every ant at the start of a match)');
    check(btn('t2') && btn('t3') && /Black/.test(part.panel.textContent) && /Same for every team: Combat ants by default, 2 eggs each/.test(part.panel.textContent), 'page: four team buttons with counts, and what is the same for every team');
    const [c] = free(ed.file, 1, ed.file.columns * 5 + 5), n0 = count(ed.file);
    btn('t3').click(); team = 3;
    part.painter.begin({ row: c[0], column: c[1] });
    check(count(ed.file) === n0 + 1 && Ants.teamAt(ed.file, c[0], c[1]) === 3 && acts === 1, 'page: a tap puts one ant of the picked team, one change');
    part.painter.begin({ row: c[0], column: c[1] });
    check(count(ed.file) === n0 && acts === 2, 'page: a tap on that ant takes it away');
    btn('take').click(); part.painter.begin({ row: c[0], column: c[1] });
    check(count(ed.file) === n0 && acts === 2 && /no ant/.test(part.panel.textContent), 'page: Take one away on an empty cell says so and changes nothing');
    btn('put').click();
    team = 0; part.painter.begin({ row: c[0], column: c[1] }); check(Ants.teamAt(ed.file, c[0], c[1]) === 0, 'page: a Green ant is put');
    part.painter.begin({ row: c[0], column: c[1] }); check(Ants.teamAt(ed.file, c[0], c[1]) === -1 && acts === 4, 'page: a tap on a Green ant takes it away too');
    acts = 2; team = 3;
    check(part.panel.querySelectorAll('.am-team').map((b) => b.getAttribute('data-k')).join(',') === 't3,t2,t0,t1', 'page: the buttons are Black, Blue, Green, Red');
    part.painter.begin({ row: 0, column: 0 });
    btn('group').click(); btn('clump').click();
    check(!part.mine(), 'page: Clump is the tool\'s own (the box does not take the tap)');
    btn('mix').click();
    check(part.mine() && !part.painter.tapOnly(), 'page: Mix takes the drag');
    part.painter.begin({ row: 4, column: 4 }); part.painter.extend({ row: 4, column: 4 }, { row: 40, column: 50 }); part.painter.end();
    check(part.state().plan && part.state().plan.ok && !btn('put-mix').disabled && /Put 100 ants/.test(btn('put-mix').textContent), 'page: a box gives a plan and the button "Put 100 ants"');
    btn('put-mix').click();
    check(count(ed.file) === n0 + 100 && acts === 3, 'page: Put makes one change of 100 ants');
    part.painter.begin({ row: 5, column: 5 }); part.painter.extend({ row: 5, column: 5 }, { row: 7, column: 7 }); part.painter.end();
    check(btn('put-mix').disabled && /too small/.test(part.panel.textContent), 'page: a box that is too small turns the button off and says why');
    btn('cancel').click(); check(!part.state().box, 'page: Cancel drops the box');
    btn('one').click();
    const total = ants(ed.file)[team];
    btn('clear').click();
    check(dialogs.length === 1 && /Clear the Black ants\?/.test(dialogs[0].title) && dialogs[0].buttons[1].text === 'Take away ' + total + ' Black ants', 'page: Clear asks first and says how many');
    dialogs[0].buttons[1].onClick();
    check(ants(ed.file)[3] === 0 && acts === 4, 'page: the question\'s button clears one team in one step');
    check(side.className.indexOf('am-on') >= 0, 'page: on a wide screen the box takes the right-hand column');
    part.show(false); check(part.panel.hidden && side.className.indexOf('am-on') < 0, 'page: it goes away with the tool');

    // F: Extended map per-spawn ant types (One at a time, Clump, Mix, and UI selector)
    const tiny = open('Original-Ants/Maps/TINY.LVL');
    const v9 = new Uint8Array(tiny.bytes.length - 40 + 18);
    v9[0] = 9;
    v9.set(tiny.bytes.subarray(4, 10), 4);
    v9.set(tiny.bytes.subarray(40), 18);
    const f9 = File.parse(v9), ed9 = editor(f9);
    check(f9.ok && f9.version === 9 && f9.kind === 'extended', 'v9: parsed as Extended map');
    const [c1, c2] = free(ed9.file, 2, ed9.file.columns * 5 + 5), n9 = count(ed9.file);
    check(!!ed9.act(Ants.putOne(ed9.file, 1, c1[0], c1[1], 4)), 'v9: putOne Combat ant');
    check(!!ed9.act(Ants.putOne(ed9.file, 2, c2[0], c2[1], 1)), 'v9: putOne Bomber ant');
    const recs = Blocks.layout(ed9.file).records;
    check(Blocks.teamOfMarker(recs[n9].tile) === 1 && Blocks.typeOfMarker(recs[n9].tile) === 4, 'v9: record 1 has team Red and antType 4 (Combat)');
    check(Blocks.teamOfMarker(recs[n9 + 1].tile) === 2 && Blocks.typeOfMarker(recs[n9 + 1].tile) === 1, 'v9: record 2 has team Blue and antType 1 (Bomber)');
    const lastTwo = ed9.file.markers.slice(-2);
    check(lastTwo[0].team === 'red' && lastTwo[0].antType === 4 && lastTwo[1].team === 'blue' && lastTwo[1].antType === 1, 'v9: MapFile.parse reads antType on markers');
    check(!!ed9.act(Blocks.placeStart(ed9.file, 1, 10, 10, 3, 5)), 'v9: placeStart clump of 3 Swimmer ants');
    const redRecs = Blocks.layout(ed9.file).records.filter((r) => Blocks.teamOfMarker(r.tile) === 1);
    check(redRecs.length === 3 && redRecs.every((r) => Blocks.typeOfMarker(r.tile) === 5), 'v9: clump replaced Red markers with 3 Swimmer ants');
    check(!!ed9.act(Ants.clearTeam(ed9.file, 1)) && !!ed9.act(Ants.clearTeam(ed9.file, 2)) && ants(ed9.file)[1] === 0, 'v9: clearTeam removes typed markers');
    check(!!ed9.act(Ants.putOne(ed9.file, 1, c1[0], c1[1], 4)) && !!ed9.act(Ants.putOne(ed9.file, 1, c2[0], c2[1], 2)), 'v9: put mixed Red Combat + Fire ants');
    check(!!ed9.act(Blocks.eraseAt(ed9.file, c1[0] * ed9.file.columns + c1[1])) && ants(ed9.file)[1] === 0, 'v9: eraseAt on one ant removes all starting ants of that team even with mixed types');
    let pickedType = 0;
    const part9 = Ants.create({
        doc, win: {}, host: { tools, hint, side }, file: () => ed9.file, act: (c) => { const d = ed9.act(c); part9.refresh(); return !!d; }, say: () => {},
        dialogs: { open: () => {} }, team: () => 0, setTeam: () => {}, ants: () => 4, setAnts: () => {}, antType: () => pickedType, setAntType: (at) => { pickedType = at; },
        convert: () => {}, view: () => null, about: () => ({ ant: 'Worker ants', eggs: 2 }), props: () => {}, refresh: () => {}
    });
    part9.show(true);
    const btn9 = (k) => part9.panel.querySelector('[data-k="' + k + '"]');
    check(btn9('at4') && btn9('at1') && btn9('at5'), 'v9 UI: Ant type buttons rendered on Extended map');
    btn9('at4').click();
    check(pickedType === 4 && part9.state().antType === 4, 'v9 UI: clicking Combat selects antType 4');
    part9.show(false);
}

console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
