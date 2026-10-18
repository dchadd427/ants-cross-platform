#!/usr/bin/env node
// Copy, convert and crop in the map maker (web/front/mapmaker_crop.js, mapmaker_convert.js; docs/MAP_EDITOR.md): the arithmetic of the frame, the Crop to fit screen and the dialogs on a small fake
// of the DOM (tests/scripts/web_mapmaker_fake_dom.js) with a stand-in for the map core. No browser and no real core: the real core is held to the same calls by
// tests/scripts/web_mapmaker_core_check.js (case J) and tests/scripts/lvl_core_check.js (case G), the browser by the opt-in check.
//
//   node tests/scripts/web_mapmaker_convert_check.js [--front DIR]
//
//   --front DIR  the folder of the mapmaker_*.js files (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
//   A  the frame: where it starts, how it moves, what a pull at a handle does and when it gets shorter by itself, the keys, the readout, the three shapes; and on thousands of random pulls
//      the frame stays on the map and within the limits
//   B  the names of a new map (the next number, the Extended and Classic names, never over 40 characters, never a taken one) and the short form of the core's sentences
//   C  the Crop to fit screen: its words, the frame moved and pulled with a pointer, the arrow keys, Use this part, Cancel and Escape, nothing left listening
//   D  Copy this map: a copy of the same kind, a name that is taken, the other kind (the dimmed choice and its reason, Next), and that the open map's bytes are never handed over as the new map
//   E  Start an Extended map, Make a Classic map (when it fits, with what the core says to tell), and the reasons when it does not, with the ways out that the reasons call for
//   F  Crop it to fit and Shorten the description as steps on a copy: the core is asked, the part goes through the conversion again, Cancel comes back to the reasons
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const path = require('path');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.resolve(__dirname, '..', '..', 'web', 'front');
const Crop = require(path.join(FRONT, 'mapmaker_crop.js')), Convert = require(path.join(FRONT, 'mapmaker_convert.js')), Dialog = require(path.join(FRONT, 'mapmaker_dialog.js'));
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }

const L = { rows: 90, columns: 130, cells: 10000 };       // the limits of a new Classic map, as the core states them

// ---- A: the frame ----
function caseA() {
    equal(Crop.start(L, 120, 400), { row: 15, column: 144, rows: 90, columns: 111 }, 'the frame starts in the center, as tall as it may be, then as wide as the cells allow');
    equal(Crop.start(L, 50, 400), { row: 0, column: 135, rows: 50, columns: 130 }, 'a map less tall than the limit: the frame is as wide as it may be');
    equal(Crop.start(L, 95, 60), { row: 2, column: 0, rows: 90, columns: 60 }, 'a narrow map: the frame is as wide as the map');
    equal(Crop.start(L, 1, 1), { row: 0, column: 0, rows: 1, columns: 1 }, 'the smallest map');
    equal(Crop.shapes(L, 120, 400).map((s) => [s.label, s.rows, s.columns]), [['Tallest', 90, 111], ['Widest', 76, 130], ['Smaller', 60, 90]], 'the three shapes of the picture');
    equal(Crop.shapes(L, 100, 80).map((s) => [s.rows, s.columns]), [[90, 80], [90, 80], [60, 80]], 'the shapes are held to the map');
    equal(Crop.readout({ row: 15, column: 144, rows: 90, columns: 111 }, L), { big: '90 rows x 111 columns', cells: '9,990 of 10,000 cells', percent: 100, range: 'Rows 15 to 104 · Columns 144 to 254' }, 'the readout counts from 0');
    equal(Crop.readout({ row: 0, column: 0, rows: 1, columns: 1 }, L).percent, 2, 'the bar is never empty');
    equal(Crop.move({ row: 15, column: 144, rows: 90, columns: 111 }, 500, 500, 120, 400), { row: 30, column: 289, rows: 90, columns: 111 }, 'a move stops at the edge of the map');
    equal(Crop.move({ row: 15, column: 144, rows: 90, columns: 111 }, -500, -500, 120, 400), { row: 0, column: 0, rows: 90, columns: 111 }, 'and at the other edge');
    const start = Crop.start(L, 120, 400);
    const wider = Crop.resize(L, start, 'e', 0, 20, 120, 400);
    equal(wider, { frame: { row: 22, column: 144, rows: 76, columns: 130 }, note: 'rows' }, 'pulled wider than 130 columns: 130, and 76 rows around the middle (the picture)');
    equal(Crop.noteText(wider.frame, L), 'You made the frame 130 columns wide, so it can be 76 rows tall at most (10,000 cells in all). It shortened by itself.', 'the sentence of the picture');
    const r5 = Crop.resize(L, start, 'e', 0, 5, 120, 400);
    check(r5.frame.columns === 116 && r5.frame.rows === 86 && r5.note === 'rows', 'wider by 5: 116 columns and 86 rows (10,000 / 116)');
    equal(Crop.resize(L, start, 'w', 0, -300, 120, 400).frame, { row: 22, column: 144 - 19, rows: 76, columns: 130 }, 'pulled at the left edge: it stops at 130 columns and the frame is shortened around the middle');
    equal(Crop.resize(L, start, 'se', 10, 10, 120, 400).frame.rows * Crop.resize(L, start, 'se', 10, 10, 120, 400).frame.columns <= 10000, true, 'a corner never has more than the cells');
    equal(Crop.resize(L, start, 'se', 10, 10, 120, 400), { frame: { row: 15, column: 144, rows: 82, columns: 121 }, note: 'rows' }, 'a corner pulled out by 10: 121 columns, and 82 rows (the bottom edge gives way)');
    equal(Crop.resize(L, start, 'e', 0, -100, 120, 400).frame, { row: 15, column: 144, rows: 90, columns: 11 }, 'pulled in at the right edge: smaller');
    equal(Crop.resize(L, start, 'e', 0, -500, 120, 400).frame.columns, 1, 'never smaller than one column');
    equal(Crop.resize(L, start, 'n', -100, 0, 120, 400).frame, { row: 15, column: 144, rows: 90, columns: 111 }, 'a frame at its tallest cannot be pulled taller');
    equal(Crop.resize(L, { row: 22, column: 144, rows: 76, columns: 130 }, 's', 20, 0, 120, 400), { frame: { row: 22, column: 144, rows: 76, columns: 130 }, note: '' }, 'a frame at its widest cannot be pulled taller, and says nothing: it stops');
    equal(Crop.resize(L, { row: 22, column: 144, rows: 76, columns: 130 }, 's', -50, 0, 120, 400).frame.rows, 26, 'pulled up from the bottom edge: shorter');
    equal(Crop.resize(L, { row: 0, column: 0, rows: 10, columns: 10 }, 'ne', -5, 500, 20, 400).frame, { row: 0, column: 0, rows: 10, columns: 130 }, 'a corner pulled past the map and the limit');
    equal(Crop.key(start, 'ArrowLeft', 120, 400), { row: 15, column: 143, rows: 90, columns: 111 }, 'an arrow moves the frame by one cell');
    equal(Crop.key(start, 'ArrowDown', 120, 400).row, 16, 'down');
    equal(Crop.key(start, 'a', 120, 400), null, 'another key is nothing');
    // thousands of pulls: the frame stays on the map and within the limits, a pull never moves the opposite edge (when nothing gave way)
    let x = 12345;
    const rnd = (n) => { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; return (x >>> 0) % n; };
    let bad = 0, shortened = 0;
    for (let i = 0; i < 20000; i++) {
        const R = 1 + rnd(300), C = 1 + rnd(500), start2 = Crop.start(L, R, C);
        let f = start2;
        for (let k = 0; k < 4; k++) {
            const h = ['n', 's', 'e', 'w', 'ne', 'nw', 'se', 'sw'][rnd(8)];
            const out = Crop.resize(L, f, h, rnd(401) - 200, rnd(601) - 300, R, C);
            f = out.frame;
            shortened += out.note === 'rows';
            if (f.row < 0 || f.column < 0 || f.rows < 1 || f.columns < 1 || f.row + f.rows > R || f.column + f.columns > C || f.rows > L.rows || f.columns > L.columns || f.rows * f.columns > L.cells) bad++;
            const m = Crop.move(f, rnd(401) - 200, rnd(601) - 300, R, C);
            if (m.rows !== f.rows || m.columns !== f.columns || m.row < 0 || m.column < 0 || m.row + m.rows > R || m.column + m.columns > C) bad++;
            f = m;
        }
    }
    check(bad === 0, 'the frame stays on the map and within the limits, ' + bad + ' times it did not');
    check(shortened > 1000, 'the pulls met the limit of the cells: ' + shortened);
}

// ---- B: the names ----
function caseB() {
    const none = () => false;
    equal([Convert.numbered('Treasure', none), Convert.numbered('Treasure 2', none), Convert.numbered('Treasure 9', none), Convert.numbered('  Treasure   ', none)], ['Treasure 2', 'Treasure 3', 'Treasure 10', 'Treasure 2'], 'the next number');
    equal(Convert.numbered('A', (n) => n === 'A 2' || n === 'A 3'), 'A 4', 'a taken name gets the next number after it');
    const long = 'x'.repeat(40), n40 = Convert.numbered(long, none);
    check(n40.length <= 40 && /^x+ 2$/.test(n40), 'a name of 40 characters stays within 40: ' + n40.length);
    check(Convert.numbered(long + ' 99', none).length <= 40, 'a name that ends with a number stays within 40');
    equal([Convert.suffixed('Treasure', 'Extended'), Convert.suffixed('Treasure', 'Classic')], ['Treasure Extended', 'Treasure Classic'], 'the name of the other kind');
    const sl = Convert.suffixed(long, 'Extended');
    check(sl.length <= 40 && / Extended$/.test(sl), 'a name of 40 characters keeps the word and stays within 40: ' + sl);
    check(Convert.suffixed('é'.repeat(45), 'Classic').length <= 40, 'in letters, not in bytes');
    equal(Convert.shortReason('this map has 120 rows; a Classic map made now has at most 90 (ants cannot enter or cross the 91st row of a taller map, in the original game too): crop it to 90 rows first, or keep it an Extended map'), 'this map has 120 rows and a Classic map made now has at most 90', 'the short form of the rows reason');
    equal(Convert.shortReason('this map is 400 columns wide; a Classic map has at most 256 columns'), 'this map is 400 columns wide and a Classic map has at most 256 columns', 'and of the columns reason');
    equal(Convert.shortReason('the description is 31 bytes; a Classic map holds 30 (29 characters end with the zero byte that the original needs)'), 'the description is 31 bytes and a Classic map holds 30', 'and of the description reason');
    equal(Convert.ordered([{ code: 'columns_beyond_classic' }, { code: 'rows_beyond_wall' }, { code: 'description_too_long' }, { code: 'format_flags_set' }, { code: 'rows_beyond_classic' }]).map((r) => r.code),
        ['rows_beyond_wall', 'rows_beyond_classic', 'columns_beyond_classic', 'description_too_long', 'format_flags_set'], 'the reasons in the order of the picture: rows, columns, the rest as the core gave them');
}

// ---- C: the Crop to fit screen ----
function screen(R, C, onUse, onCancel, limits) {
    const doc = makeDocument(), nodes = {}, log = { used: [], cancelled: 0 };
    const ids = ['crop-stage', 'crop-frame', 'crop-fill', 'crop-note', 'crop-use', 'crop-cancel', 'crop-name', 'crop-tags', 'crop-whole', 'crop-shapes', 'crop-nothing', 'crop-chips', 'crop-big', 'crop-cells', 'crop-range'];
    ids.forEach((id) => { nodes[id] = doc.createElement(id === 'crop-use' || id === 'crop-cancel' ? 'button' : 'div'); nodes[id].id = id; doc.body.appendChild(nodes[id]); });
    nodes['crop-stage'].getBoundingClientRect = () => ({ width: C * 2, left: 0, top: 0 });         // two pixels for each cell
    nodes['crop-stage'].style.setProperty = function (k, v) { this[k] = v; };
    nodes['crop-stage'].appendChild(doc.createElement('canvas'));
    ['nw', 'n', 'ne', 'w', 'e', 'sw', 's', 'se'].forEach((h) => { const i = doc.createElement('i'); i.className = 'h ' + h; i.setAttribute('data-h', h); nodes['crop-frame'].appendChild(i); });
    const host = doc.createElement('div'), editor = doc.createElement('div'); host.hidden = true;
    doc.body.appendChild(host); doc.body.appendChild(editor);
    const ctl = Crop.open({ doc, $: (id) => nodes[id], host, editor, limits: limits || L, rows: R, columns: C, name: 'Tall Valley', tags: (el) => { el.textContent = 'Extended map'; }, picture: () => doc.createElement('canvas'), onUse: (f) => { log.used.push(f); onUse && onUse(f); }, onCancel: () => { log.cancelled++; onCancel && onCancel(); } });
    const pointer = (target, type, x, y) => nodes['crop-frame'].fire(type, { clientX: x, clientY: y, target, pointerId: 1, preventDefault() {}, stopPropagation() {} });
    return { doc, nodes, host, editor, ctl, log, pointer, handle: (h) => nodes['crop-frame'].children.filter((c) => c.getAttribute('data-h') === h)[0], text: (id) => nodes[id].textContent };
}
function caseC() {
    const s = screen(120, 400);
    check(!s.host.hidden && s.editor.hidden, 'the screen is shown and the editor is hidden meanwhile');
    equal([s.text('crop-name'), s.text('crop-tags'), s.text('crop-big'), s.text('crop-cells'), s.text('crop-range')], ['Tall Valley', 'Extended map', '90 rows x 111 columns', '9,990 of 10,000 cells', 'Rows 15 to 104 · Columns 144 to 254'], 'the words of the start');
    equal(s.text('crop-whole'), 'Whole map: 120 rows x 400 columns. The dim part is left out.', 'the whole map');
    equal(s.text('crop-shapes'), 'The frame can be any of these shapes, or smaller. It never has more than 90 rows, 130 columns or 10,000 cells:', 'the limits come from the core');
    equal(s.text('crop-nothing'), 'Nothing changes until you save a copy. “Tall Valley” stays as it is.', 'nothing changes');
    check(/^iThe frame starts in the center, as big as a Classic map may be: 90 rows, so it can be up to 111 columns wide\.$/.test(s.text('crop-note')), 'the note of the start: ' + s.text('crop-note'));
    equal(s.nodes['crop-chips'].children.map((c) => c.className), ['chip on', 'chip', 'chip'], 'the tallest shape is marked');
    equal(s.nodes['crop-chips'].children.map((c) => c.textContent), ['Tallest90 x 111 · 9,990', 'Widest76 x 130 · 9,880', 'Smaller60 x 90 · 5,400'], 'the shapes with their cells');
    equal([s.nodes['crop-frame'].style.left, s.nodes['crop-frame'].style.top, s.nodes['crop-frame'].style.width, s.nodes['crop-frame'].style.height], [(144 / 400 * 100) + '%', (15 / 120 * 100) + '%', (111 / 400 * 100) + '%', '75%'], 'the frame is placed in percent of the map');
    equal([s.nodes['crop-stage'].style['--ar'], s.nodes['crop-stage'].style['--k']], ['400/120', String(400 / 120)], 'the picture has the proportions of the map');
    check(s.doc.activeElement === s.nodes['crop-frame'], 'the frame has the focus (the arrow keys work at once)');
    // a pointer: the frame is dragged by 40 pixels, which are 20 cells
    const frame = s.nodes['crop-frame'];
    s.pointer(frame, 'pointerdown', 100, 100); s.pointer(frame, 'pointermove', 140, 100);
    equal(s.text('crop-range'), 'Rows 15 to 104 · Columns 164 to 274', 'dragged by 40 pixels: 20 cells');
    check(s.text('crop-note') === '', 'the note of the start goes when the person moves the frame');
    s.pointer(frame, 'pointerup', 140, 100); s.pointer(frame, 'pointermove', 400, 400);
    equal(s.text('crop-range'), 'Rows 15 to 104 · Columns 164 to 274', 'a pointer that was let go moves nothing');
    // a pull at the right edge by 60 pixels (30 cells): 130 columns wide, 76 rows, the sentence
    s.pointer(s.handle('e'), 'pointerdown', 200, 100); s.pointer(frame, 'pointermove', 260, 100); s.pointer(frame, 'pointerup', 260, 100);
    equal([s.text('crop-big'), s.text('crop-cells'), s.text('crop-range')], ['76 rows x 130 columns', '9,880 of 10,000 cells', 'Rows 22 to 97 · Columns 164 to 293'], 'pulled wider: shorter by itself');
    check(/^!You made the frame 130 columns wide, so it can be 76 rows tall at most \(10,000 cells in all\)\. It shortened by itself\.$/.test(s.text('crop-note')), 'the sentence of the picture: ' + s.text('crop-note'));
    check(s.nodes['crop-chips'].children[1].className === 'chip on' && s.nodes['crop-chips'].children[0].className === 'chip', 'the widest shape is marked now');
    s.pointer(s.handle('nw'), 'pointerdown', 0, 0); s.pointer(frame, 'pointermove', 20, 10); s.pointer(frame, 'pointerup', 20, 10);
    equal(s.text('crop-range'), 'Rows 27 to 97 · Columns 174 to 293', 'a pull at a corner moves two edges');
    check(s.text('crop-note') === '', 'a pull that needed no shortening has no note');
    // the keys
    const press = (key) => frame.fire('keydown', { key, shiftKey: false, preventDefault() { this.prevented = true; } });
    press('ArrowRight'); press('ArrowDown');
    equal(s.text('crop-range'), 'Rows 28 to 98 · Columns 175 to 294', 'the arrow keys move the frame by one cell');
    const other = { key: 'x', preventDefault() { this.prevented = true; } }; frame.fire('keydown', other);
    check(!other.prevented, 'another key is left alone');
    const keydowns = (s.doc.listeners.keydown || []).length;
    s.ctl.use();
    equal(s.log.used, [{ row: 28, column: 175, rows: 71, columns: 120 }], 'Use this part hands the frame over');
    check(s.host.hidden && !s.editor.hidden, 'and the editor is back');
    equal(s.nodes['crop-stage'].children.length, 1, 'the picture goes with the screen: only the frame is left in the stage');
    check((s.doc.listeners.keydown || []).length === keydowns - 1, 'the screen stops listening to the keys when it goes');
    equal([s.nodes['crop-use'].listeners.click.length, s.nodes['crop-cancel'].listeners.click.length], [0, 0], 'the buttons are given back as they were');
    // Cancel and Escape
    const t = screen(120, 400);
    t.nodes['crop-cancel'].click();
    check(t.log.cancelled === 1 && t.log.used.length === 0 && t.host.hidden && !t.editor.hidden, 'Cancel leaves with nothing');
    const u = screen(120, 400);
    u.doc.key('Escape');
    check(u.log.cancelled === 1 && u.log.used.length === 0 && u.host.hidden, 'Escape is Cancel');
    u.doc.key('Escape');
    check(u.log.cancelled === 1, 'and a screen that is gone does not hear it again');
    // limits that are not the Classic ones: everything on the screen comes from the limits it is given
    const odd = screen(100, 200, null, null, { rows: 50, columns: 60, cells: 2000 });
    equal([odd.text('crop-big'), odd.text('crop-cells'), odd.text('crop-shapes')], ['50 rows x 40 columns', '2,000 of 2,000 cells', 'The frame can be any of these shapes, or smaller. It never has more than 50 rows, 60 columns or 2,000 cells:'], 'the limits are the ones the core gave');
    check(/as big as a Classic map may be: 50 rows, so it can be up to 40 columns wide/.test(odd.text('crop-note')), 'and so is the note of the start');
    // a tall map and a small one
    const small = screen(40, 30);
    equal([small.text('crop-big'), small.text('crop-range')], ['40 rows x 30 columns', 'Rows 0 to 39 · Columns 0 to 29'], 'a map that is smaller than the limits: the frame is the whole map');
}

// ---- a stand-in for the core: files that are a few numbers ----
let serial = 0;
function file(kind, rows, columns, desc) { const b = new Uint8Array(4); b[0] = ++serial; b.kind = kind; b.rows = rows; b.columns = columns; b.desc = desc || 'ok'; return b; }
function standIn(log) {
    return {
        convertToClassic(b) {
            log.push('toClassic');
            const reasons = [];
            if (b.columns > 256) reasons.push({ code: 'columns_beyond_classic', message: 'this map is ' + b.columns + ' columns wide; a Classic map has at most 256 columns' });
            if (b.rows > 90) reasons.push({ code: 'rows_beyond_wall', message: 'this map has ' + b.rows + ' rows; a Classic map made now has at most 90 (ants cannot enter or cross the 91st row of a taller map, in the original game too): crop it to 90 rows first, or keep it an Extended map' });
            if (b.desc.length > 30) reasons.push({ code: 'description_too_long', message: 'the description is ' + b.desc.length + ' bytes; a Classic map holds 30 (29 characters end with the zero byte that the original needs)' });
            if (b.flags) reasons.push({ code: 'format_flags_set', message: 'the map has format flags (0x1); a Classic map has none' });
            const notes = !reasons.length && b.desc.length === 30 ? [{ code: 'description_30_bytes', message: 'the description has 30 bytes: the original reads no zero byte that ends it (29 characters fit), and the original-safe check warns' }] : [];
            return { status: 0, json: { ok: !reasons.length, reasons, notes }, bytes: reasons.length ? undefined : file('classic', b.rows, b.columns, b.desc) };
        },
        convertToExtended(b) { log.push('toExtended'); return { status: 0, json: { ok: !b.refuse, reasons: b.refuse ? [{ code: 'not_writable_as_extended', message: 'the map cannot be written as an Extended map: no' }] : [], notes: [] }, bytes: b.refuse ? undefined : file('extended', b.rows, b.columns, b.desc) }; },
        readReport(b) { return { status: 0, json: { loaded: true, rows: b.rows, columns: b.columns, version: b.kind === 'classic' ? 8 : 9, description: b.desc, minutes: 10, eggs: 6, default_type_tile: 0x7FFE } }; },
        crop(b, row, column, rows, columns, profile) {
            log.push(['crop', row, column, rows, columns, profile]);
            if (this.cropFails) return { status: 1, error: 'the part to keep is not inside the map', text: '' };
            const out = file(b.kind, rows, columns, b.desc); out.cropped = { row, column };
            return { status: 0, json: { ok: true, removed_cells: 3, removed_records: 2, reasons: [], notes: [{ code: 'objects_cut', message: '3 cells of objects were taken out because the frame cuts the object (an object is kept whole or not at all)' }] }, bytes: out };
        }
    };
}
const LIMITS = { classic: { rows: 90, columns: 130, cells: 10000, openCells: 65536 }, extended: { side: 512, cells: 262144, longest: 1024 } };

function page(cur) {
    const doc = makeDocument(), host = doc.createElement('div'), top = doc.createElement('header'), body = doc.createElement('div'), opener = doc.createElement('button');
    [host, top, body].forEach((n) => doc.body.appendChild(n));
    top.appendChild(opener); opener.focus();
    const calls = [], made = [], crops = [], core = standIn(calls), dialogs = Dialog.create(doc, host, [top, body]);
    const env = { doc, dialogs, core, limits: LIMITS, current: () => cur, thumb: () => null, tags: (el, kind) => { el.textContent = kind === 'extended' ? 'Extended map' : 'Classic map'; }, onMade: (bytes, name) => made.push({ bytes, name }), crop: (spec) => crops.push(spec) };
    const convert = Convert.create(env);
    const q = (s) => host.querySelector(s), all = (s) => host.querySelectorAll(s);
    return {
        doc, host, dialogs, core, calls, made, crops, convert, q, all,
        title: () => (q('h2') ? q('h2').textContent : ''), button: (label) => all('button').filter((b) => b.textContent === label)[0],
        type(value) { const box = q('input[type=text]') || all('input').filter((i) => i.getAttribute('type') === 'text')[0]; box.value = value; box.fire('input', {}); },
        name() { return all('input').filter((i) => i.getAttribute('type') === 'text')[0].value; },
        reasons: () => all('.creasons .note').map((n) => n.textContent)
    };
}

// ---- D: Copy this map ----
function caseD() {
    const treasure = file('classic', 60, 60, 'Treasure for ants'), before = Array.from(treasure);
    const cur = { name: 'Treasure', bytes: treasure, file: { rows: 60, columns: 60 }, kind: 'classic', report: { headline: 'Works in the original game' } };
    let p = page(cur);
    p.convert.copy();
    equal(p.title(), 'Copy this map', 'the dialog');
    check(/Treasure/.test(p.q('.srccard').textContent) && /Classic map/.test(p.q('.srccard').textContent) && /60 x 60 · works in the original game/.test(p.q('.srccard').textContent), 'the card of the map that is copied');
    equal(p.all('.kind').map((l) => l.textContent), ['Classic map (same kind)Works in the original game and in the new version.', 'Extended map (start an Extended map from this one)New version onlyBigger limits. Works in the new version only. Any Classic map can be the start.'], 'the two choices, as drawn');
    equal(p.name(), 'Treasure 2', 'the name of the copy is the next free one');
    check(/The name “Treasure 2” is free\./.test(p.q('.cfree').textContent) && /This makes a new map\. “Treasure” is never changed or replaced\./.test(p.host.textContent), 'the name is free, and the copy says the map stays');
    equal(p.all('.dlg-foot button').map((b) => b.textContent), ['Cancel', 'Make new map'], 'the buttons of a copy');
    check(p.all('input').filter((i) => i.type === 'radio' || i.getAttribute('type') === 'radio').length === 2, 'two radio buttons');
    check(!p.button('Make new map').disabled, 'a copy under a free name can be made');
    // a name that is taken: the open map's, in any case and with spaces round it
    p.type('  treasure ');
    check(p.button('Make new map').disabled && /A map called “treasure” already exists\. A map is never replaced, so please pick another name\./.test(p.host.textContent), 'a name that is the open map\'s is refused');
    p.type('');
    check(p.button('Make new map').disabled && /Give the new map a name\./.test(p.host.textContent), 'an empty name is refused');
    p.type('Picnic');
    check(!p.button('Make new map').disabled && !/already exists/.test(p.host.textContent), 'another name is free again');
    p.button('Make new map').click();
    check(p.made.length === 1 && p.made[0].name === 'Picnic' && p.made[0].bytes !== treasure && Buffer.compare(Buffer.from(p.made[0].bytes), Buffer.from(treasure)) === 0 && !p.dialogs.isOpen(), 'the copy is the same bytes in another array under the new name');
    equal(p.calls, [], 'a copy of the same kind asks the core nothing');
    // the other kind: Next, then the dialog of the Extended map
    p = page(cur);
    p.convert.copy();
    p.all('input').filter((i) => i.getAttribute('type') === 'radio')[1].checked = true; p.all('input').filter((i) => i.getAttribute('type') === 'radio')[1].fire('change', {});
    equal(p.all('.dlg-foot button').map((b) => b.textContent), ['Cancel', 'Next: what changes'], 'the button of the other kind');
    check(p.all('.kind').map((l) => l.className).join() === 'kind,kind on', 'the chosen card is marked');
    p.type('My own name');
    p.type('treasure');
    check(p.button('Next: what changes').disabled, 'the name of the open map stops the other kind too');
    p.type('My own name');
    p.button('Next: what changes').click();
    equal(p.title(), 'Start an Extended map from this one?', 'Next opens the dialog of the Extended map');
    equal(p.name(), 'My own name', 'a name that was typed goes along');
    equal(p.made.length, 0, 'nothing is made yet');
    p = page(cur);
    p.convert.copy();
    p.all('input').filter((i) => i.getAttribute('type') === 'radio')[1].fire('change', {}); p.all('input').filter((i) => i.getAttribute('type') === 'radio')[1].checked = true; p.all('input').filter((i) => i.getAttribute('type') === 'radio')[1].fire('change', {});
    p.button('Next: what changes').click();
    equal(p.name(), 'Treasure Extended', 'a name that was not typed is the Extended name');
    // Escape and Cancel leave nothing
    p = page(cur); p.convert.copy(); p.doc.key('Escape');
    check(!p.dialogs.isOpen() && p.made.length === 0, 'Escape closes the dialog and makes nothing');
    p = page(cur); p.convert.copy(); p.button('Cancel').click();
    check(!p.dialogs.isOpen() && p.made.length === 0, 'Cancel too');
    // an Extended map that fits: the other choice is open
    const sunny = file('extended', 80, 100, 'A sunny picnic for the ants!!!'), sc = { name: 'Sunny Picnic', bytes: sunny, file: { rows: 80, columns: 100 }, kind: 'extended', report: { headline: 'Works in the new version' } };
    p = page(sc); p.convert.copy();
    equal(p.all('.kind').map((l) => l.textContent), ['Extended map (same kind)New version onlyWorks in the new version only.', 'Classic map (convert)Works in the original game and in the new version.'], 'an Extended map that fits: the Classic choice is open');
    check(p.all('input').filter((i) => i.getAttribute('type') === 'radio').every((i) => !i.disabled), 'both can be chosen');
    // an Extended map that does not fit: dimmed, with the reason and a way in
    const tall = file('extended', 120, 400, 'The tall valley has many ants!!'), tc = { name: 'Tall Valley', bytes: tall, file: { rows: 120, columns: 400 }, kind: 'extended', report: { headline: 'Works in the new version' } };
    p = page(tc); p.convert.copy();
    equal(p.all('.kind')[1].textContent, 'Classic map (convert)Not yet: this map has 120 rows and a Classic map made now has at most 90. See why and what to do', 'the dimmed choice says why, and offers the reasons');
    check(p.all('.kind')[1].className === 'kind off' && p.all('input').filter((i) => i.getAttribute('type') === 'radio')[1].disabled, 'the Classic choice is dimmed and cannot be chosen');
    p.all('button').filter((b) => b.textContent === 'See why and what to do')[0].click();
    equal(p.title(), 'This map cannot be a Classic map yet', 'See why and what to do opens the reasons');
    equal(tall.rows, 120, 'the map is as it was');
    equal(Array.from(treasure), before, 'and so are the bytes that were copied');
}

// ---- E: Start an Extended map, Make a Classic map, the reasons ----
function caseE() {
    const treasure = file('classic', 60, 60, 'Treasure for ants');
    const cur = { name: 'Treasure', bytes: treasure, file: { rows: 60, columns: 60 }, kind: 'classic', report: { headline: 'Works in the original game' } };
    let p = page(cur);
    p.convert.convert();
    equal(p.title(), 'Start an Extended map from this one?', 'the link of the side panel on a Classic map');
    equal(p.q('.lead').textContent, 'Treasure (60 x 60) will be the start of a new Extended map.', 'the lead');
    equal(p.all('.cbox').map((b) => b.textContent), ['What stays the same✓How the map plays. Every tile, hill, food pile and ant stays where it is.✓Your Classic map “Treasure”. It is never touched.', 'What changes→The new map is an Extended map: the original game cannot open it.→The limits get bigger: up to 512 x 512 cells.'], 'what stays and what changes, with the core\'s limit');
    equal([p.name(), p.all('label')[0].textContent], ['Treasure Extended', 'Name of the new Extended map'], 'the name');
    equal(p.all('.dlg-foot button').map((b) => b.textContent), ['Cancel', 'Make the new Extended map'], 'the buttons');
    p.type('Treasure');
    check(p.button('Make the new Extended map').disabled && /already exists/.test(p.host.textContent), 'the name of the open map is taken');
    p.type('Treasure Big');
    p.button('Make the new Extended map').click();
    check(p.made.length === 1 && p.made[0].name === 'Treasure Big' && p.made[0].bytes.kind === 'extended' && p.made[0].bytes !== treasure && treasure.kind === 'classic' && !p.dialogs.isOpen(), 'the Extended map is a new map from the core\'s conversion');
    equal(p.calls, ['toExtended'], 'the core was asked once');
    // the core refuses: a sentence, no map
    const odd = file('classic', 60, 60); odd.refuse = true;
    p = page({ name: 'Odd', bytes: odd, file: { rows: 60, columns: 60 }, kind: 'classic', report: null });
    p.convert.convert(); p.button('Make the new Extended map').click();
    check(p.made.length === 0 && /could not be made/.test(p.title()) && /The map cannot be written as an Extended map: no\./.test(p.host.textContent), 'a refusal of the core is said, and nothing is made');
    // an Extended map that fits
    const sunny = file('extended', 80, 100, 'A sunny picnic for the ants!!!'), sc = { name: 'Sunny Picnic', bytes: sunny, file: { rows: 80, columns: 100 }, kind: 'extended', report: { headline: 'Works in the new version' } };
    p = page(sc);
    p.convert.convert();
    equal(p.title(), 'Make a Classic map from this one?', 'the link of the side panel on an Extended map');
    equal(p.q('.lead').textContent, 'Sunny Picnic (80 x 100, Extended map) fits a Classic map. The Classic map will be a new map.', 'it fits');
    equal(p.all('.cbox').map((b) => b.textContent), ['What stays the same✓How the map plays.✓Your Extended map. It is never touched.', 'What changes→The copy keeps the limits of a Classic map: at most 90 rows and 130 columns.→The “New version only” badge goes away.'], 'what stays and what changes');
    equal(p.all('.note').map((n) => n.textContent), ['iThe description has 30 bytes: the original reads no zero byte that ends it (29 characters fit), and the original-safe check warns.'], 'what the core says to tell, with a capital');
    equal(p.all('.dlg-foot button').map((b) => b.textContent), ['Cancel', 'Make the new Classic map'], 'the buttons');
    p.button('Make the new Classic map').click();
    check(p.made.length === 1 && p.made[0].name === 'Sunny Picnic Classic' && p.made[0].bytes.kind === 'classic' && sunny.kind === 'extended' && !p.dialogs.isOpen(), 'the Classic map is a new map under the name of the other kind');
    // the reasons
    const tall = file('extended', 120, 400, 'The tall valley has many ants!!'), tc = { name: 'Tall Valley', bytes: tall, file: { rows: 120, columns: 400 }, kind: 'extended', report: { headline: 'Works in the new version' } };
    p = page(tc); p.convert.convert();
    equal(p.reasons(), ['✕This map has 120 rows; a Classic map made now has at most 90 (ants cannot enter or cross the 91st row of a taller map, in the original game too): crop it to 90 rows first, or keep it an Extended map', '✕This map is 400 columns wide; a Classic map has at most 256 columns', '✕The description is 31 bytes; a Classic map holds 30 (29 characters end with the zero byte that the original needs)'], 'the reasons in the order of the picture, in the converter\'s words');
    equal(p.q('.lead').textContent, 'A Classic map has to fit the original game. These things stop it:', 'the lead');
    equal(p.all('.cacts button').map((b) => b.textContent), ['Crop it to fit...', 'Shorten the description...'], 'the ways out');
    equal(p.q('.chelp').textContent, 'Crop opens a movable frame of the biggest size a Classic map may have (90 rows, up to 130 columns, 10,000 cells in all). Nothing changes until you save a copy.', 'the help of the crop, with the core\'s limits');
    equal(p.all('.dlg-foot button').map((b) => b.textContent), ['Close', 'Keep it Extended'], 'the buttons');
    p.button('Keep it Extended').click();
    check(!p.dialogs.isOpen() && p.made.length === 0 && p.crops.length === 0, 'Keep it Extended leaves it as it is');
    p = page(tc); p.convert.convert(); p.button('Close').click();
    check(!p.dialogs.isOpen() && p.made.length === 0, 'Close too');
    // only what the reasons call for
    const onlyText = file('extended', 40, 40, 'x'.repeat(31));
    p = page({ name: 'Wordy', bytes: onlyText, file: { rows: 40, columns: 40 }, kind: 'extended', report: null }); p.convert.convert();
    equal(p.all('.cacts button').map((b) => b.textContent), ['Shorten the description...'], 'a description that is too long: only the way to shorten it');
    check(!p.q('.chelp'), 'and no help about the crop');
    const onlySize = file('extended', 120, 60, 'ok');
    p = page({ name: 'Tall', bytes: onlySize, file: { rows: 120, columns: 60 }, kind: 'extended', report: null }); p.convert.convert();
    equal(p.all('.cacts button').map((b) => b.textContent), ['Crop it to fit...'], 'a map that is too tall: only the crop');
    const flagged = file('extended', 40, 40, 'ok'); flagged.flags = 1;
    p = page({ name: 'Flagged', bytes: flagged, file: { rows: 40, columns: 40 }, kind: 'extended', report: null }); p.convert.convert();
    check(p.all('.cacts button').length === 0 && !p.q('.cdo') && p.reasons().length === 1, 'a reason that neither helps with: no ways out are offered');
}

// ---- F: Crop it to fit and Shorten the description ----
function caseF() {
    const tall = file('extended', 120, 400, 'The tall valley has many ants!!'), tc = { name: 'Tall Valley', bytes: tall, file: { rows: 120, columns: 400 }, kind: 'extended', report: { headline: 'Works in the new version' } };
    let p = page(tc); p.convert.convert();
    p.button('Crop it to fit...').click();
    check(!p.dialogs.isOpen() && p.crops.length === 1, 'the dialog goes and the crop screen opens');
    const spec = p.crops[0];
    equal([spec.name, spec.rows, spec.columns, spec.bytes === tall], ['Tall Valley', 120, 400, true], 'the screen is given the map\'s name, size and bytes');
    // the part goes through the core, then through the conversion again
    spec.onUse({ row: 15, column: 144, rows: 90, columns: 111 });
    equal(p.calls.slice(-2), [['crop', 15, 144, 90, 111, 'extended'], 'toClassic'], 'the core cuts the part and the conversion is asked of the part');
    check(/cannot be a Classic map yet/.test(p.title()) && p.reasons().length === 1 && /description is 31 bytes/.test(p.reasons()[0]), 'the part still has a description that is too long: the reasons again, without the size');
    equal(p.all('.cacts button').map((b) => b.textContent), ['Shorten the description...'], 'and the way out that is left');
    check(p.made.length === 0, 'nothing is made yet');
    // Cancel of the crop screen comes back to the reasons of the copy that was being worked on
    p = page(tc); p.convert.convert(); p.button('Crop it to fit...').click();
    const calls = p.calls.length;
    p.crops[0].onCancel();
    check(/cannot be a Classic map yet/.test(p.title()) && p.reasons().length === 3 && p.calls.length === calls + 1, 'Cancel comes back to the reasons');
    // a part that fits: the dialog says what was kept, and the new map is the part
    const fine = file('extended', 120, 400, 'short'), fc = { name: 'Fine', bytes: fine, file: { rows: 120, columns: 400 }, kind: 'extended', report: null };
    p = page(fc); p.convert.convert(); p.button('Crop it to fit...').click();
    p.crops[0].onUse({ row: 15, column: 144, rows: 90, columns: 111 });
    equal(p.title(), 'Make a Classic map from this one?', 'a part that fits');
    equal(p.q('.lead').textContent, 'Fine, cropped to 90 x 111, fits a Classic map. The Classic map will be a new map.', 'the lead says it was cropped');
    equal(p.all('.cbox').map((b) => b.textContent), ['What stays the same✓How the map plays inside the part you chose.✓Your Extended map. It is never touched.', 'What changes→Only the part you chose is kept: rows 15 to 104, columns 144 to 254.→The copy keeps the limits of a Classic map: at most 90 rows and 130 columns.→The “New version only” badge goes away.'], 'what changed is the part, in rows and columns from 0');
    equal(p.all('.note').map((n) => n.textContent), ['i3 cells of objects were taken out because the frame cuts the object (an object is kept whole or not at all).'], 'what the core says of the cut');
    p.button('Make the new Classic map').click();
    check(p.made.length === 1 && p.made[0].name === 'Fine Classic' && p.made[0].bytes.kind === 'classic' && p.made[0].bytes.rows === 90 && p.made[0].bytes.columns === 111, 'the new map is the part, as a Classic map');
    check(fine.rows === 120 && fine.kind === 'extended', 'the map that was open is as it was');
    // the core refuses the part: a sentence
    p = page(fc); p.core.cropFails = true; p.convert.convert(); p.button('Crop it to fit...').click();
    p.crops[0].onUse({ row: 15, column: 144, rows: 90, columns: 111 });
    check(/could not be made/.test(p.title()) && /The part to keep is not inside the map\./.test(p.host.textContent) && p.made.length === 0, 'a refusal of the core is said');
    // a second crop works on the part and adds its place
    p = page(fc); p.convert.convert(); p.button('Crop it to fit...').click();
    p.crops[0].onUse({ row: 10, column: 100, rows: 100, columns: 200 });
    equal(p.title(), 'This map cannot be a Classic map yet', 'a part that is still too big');
    p.button('Crop it to fit...').click();
    equal([p.crops[1].rows, p.crops[1].columns], [100, 200], 'the next crop is of the part');
    p.crops[1].onUse({ row: 5, column: 20, rows: 90, columns: 111 });
    equal(p.all('.cbox')[1].textContent.split('.')[0], 'What changes\u2192Only the part you chose is kept: rows 15 to 104, columns 120 to 230', 'and the place is counted from the whole map');
}

function main() {
    for (const [name, fn] of [['A the frame', caseA], ['B the names', caseB], ['C the Crop to fit screen', caseC], ['D Copy this map', caseD], ['E Start an Extended map, Make a Classic map, the reasons', caseE], ['F Crop it to fit', caseF]]) {
        process.stdout.write('RUNNING: ' + name + ' ... ');
        const before = failures;
        try { fn(); } catch (e) { failures++; console.log('\n    FAILED: threw ' + (e && e.stack ? e.stack : e)); }
        if (failures === before) console.log('PASS');
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}
main();
