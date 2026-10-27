#!/usr/bin/env node
// The map maker's small map (docs/MAP_EDITOR.md, "The small map"): the hills and starting ants drawn over it at the size it is shown, and its box: Hide and Show, Lock, and a place of its own
// (web/front/mapmaker_minimap.js). Run with the real module on a small fake of the DOM (a recording canvas, boxes with a size, buttons, pointer events, a storage), with no browser:
//
//   node tests/scripts/web_mapmaker_minimap_check.js [--front DIR]
//
//   --front DIR  the folder of the mapmaker_*.js files to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
//   A  the hills: where they are, at least 10 pixels at every map size (31 to 480 cells a side, 1 and 2 pixels to a CSS pixel), centered, an edge (white for the black team, black for the others)
//      and the team's color; the starting ants: small, and left out beside a hill of their team
//   B  Hide and Show: the box and the button, the focus, what is kept (and a storage that throws or holds rubbish)
//   C  Lock: the label and the state, kept; what it stops (a drag, the arrow keys, Home, two taps) and what it does not (Hide, Show, staying inside the room)
//   D  the place: a drag inside the room, the arrow keys by a step, Home, two taps, a tap that is not a drag, a drag that does not reach the canvas, the room that shrinks, a stored place that does not fit
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const load = (name) => require(path.join(FRONT, name));
const Mini = load('mapmaker_minimap.js'), File = load('mapmaker_file.js'), Canvas = load('mapmaker_canvas.js');
const fs = require('fs');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }

// A recording canvas: the rectangles that were filled, with the color at that moment
function recorder() { const ctx = { rects: [], fillRect(x, y, w, h) { this.rects.push({ x, y, w, h, c: this.fillStyle }); } }; return ctx; }
// A made-up map: layer 2 holds the hills' tiles at the cells given ({tile, cells}), the starting ants are {team, row, column}
function fakeFile(rows, columns, hillCells, markers) {
    const bytes = new Uint8Array(6 * rows * columns);
    for (const h of hillCells) for (const [r, c] of h.cells) { bytes[6 * (r * columns + c)] = h.tile & 255; bytes[6 * (r * columns + c) + 1] = h.tile >> 8; }
    return { bytes, layer2: 0, rows, columns, markers: markers || [] };
}
function hillBlock(tile, row, column) { const cells = []; for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) cells.push([row + r, column + c]); return { tile, cells }; }
const TEAM_OF_TILE = { 245: 'black', 246: 'blue', 247: 'red', 248: 'green' };
const COLORS = { green: '#5dff2e', red: '#ff2d86', blue: '#3d9bff', black: '#161616' };

function caseA() {
    // where the hills are: the middle of their cells, one for each team that has one
    const f = fakeFile(60, 60, [hillBlock(245, 10, 20), hillBlock(248, 40, 30), hillBlock(247, 0, 0)]);
    const list = Mini.hills(f).sort((a, b) => a.team < b.team ? -1 : 1);
    equal(list.map((h) => h.team), ['black', 'green', 'red'], 'the teams that have a hill');
    equal(list.map((h) => [h.x, h.y]), [[22, 12], [32, 42], [2, 2]], 'the middle of each hill, in cells');
    equal(Mini.hills(fakeFile(30, 30, [])), [], 'no hill, no mark');
    // several hills of one team (a legal map may have them: the checker only warns): one square for each group of touching cells
    const two = Mini.hills(fakeFile(60, 60, [hillBlock(246, 5, 5), hillBlock(246, 40, 40)]));
    equal(two.map((h) => [h.team, h.x, h.y]), [['blue', 7, 7], ['blue', 42, 42]], 'two hills of a team far apart: two marks');
    equal(Mini.hills(fakeFile(60, 60, [hillBlock(246, 10, 10), hillBlock(246, 14, 14)])).length, 1, 'cells that touch at a corner are one hill');
    equal(Mini.hills(fakeFile(60, 60, [hillBlock(246, 10, 10), hillBlock(246, 16, 16)])).length, 2, 'two hills two cells apart are two hills');
    equal(Mini.hills(fakeFile(60, 60, [hillBlock(246, 10, 56), hillBlock(246, 14, 0)])).length, 2, 'the end of a row and the start of the next are not touching');
    equal(Mini.hills(fakeFile(60, 60, [hillBlock(246, 10, 10), hillBlock(247, 14, 14)])).map((h) => h.team).sort(), ['blue', 'red'], 'touching hills of two teams stay two');
    equal(Mini.hills(fakeFile(60, 60, [{ tile: 248, cells: [[1, 1], [1, 2], [2, 1]] }])).map((h) => [h.x, h.y]), [[1.8333333333333333, 1.8333333333333333]], 'a hill that is part of one: the middle of the cells it has');
    equal(Mini.hills(fakeFile(60, 60, [{ tile: 248, cells: [[0, 0], [59, 59], [0, 59], [59, 0]] }])).length, 4, 'cells at the four corners of the map are four hills');
    const ctx2 = recorder(); Mini.marks(ctx2, fakeFile(60, 60, [hillBlock(246, 5, 5), hillBlock(246, 40, 40)]), Mini.hills(fakeFile(60, 60, [hillBlock(246, 5, 5), hillBlock(246, 40, 40)])), 156, 1);
    check(ctx2.rects.length === 4, 'a mark for each of two hills of a team (' + ctx2.rects.length + ' rectangles)');
    equal(Mini.hills(fakeFile(30, 30, [{ tile: 244, cells: [[1, 1]] }, { tile: 249, cells: [[2, 2]] }])), [], 'the tiles next to the hills are not hills');
    const real = File.parse(new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', 'GAUNTLET.LVL'))));
    check(real.ok && Mini.hills(real).length === 4 && ['black', 'blue', 'green', 'red'].every((t) => Mini.hills(real).some((h) => h.team === t)), 'a library map has four hills, one for each team (read only)');

    // the size, at every map size and pixel ratio: at least 10 CSS pixels; a bigger hill keeps its own size; centered; the edge; the color
    for (const [side, w, px] of [[31, 156, 1], [60, 156, 1], [120, 156, 1], [240, 156, 1], [480, 156, 1], [480, 312, 2], [31, 468, 3], [60, 100, 1]]) {
        const file = fakeFile(side, side, [hillBlock(245, 4, 4), hillBlock(246, 4, side - 9), hillBlock(247, side - 9, 4), hillBlock(248, side - 9, side - 9)]);
        const ctx = recorder(), k = w / side, want = Math.max(10 * px, Math.round(4 * k));
        Mini.marks(ctx, file, Mini.hills(file), w, px);
        check(ctx.rects.length === 8, side + ' x ' + side + ' at ' + px + ': two rectangles for each of four hills (' + ctx.rects.length + ')');
        for (let i = 0; i < 4 && ctx.rects.length === 8; i++) {
            const e = ctx.rects[2 * i], inner = ctx.rects[2 * i + 1], team = Mini.hills(file)[i].team, h = Mini.hills(file)[i];
            check(e.w === want && e.h === want && e.w >= 10 * px, side + ' x ' + side + ' at ' + px + ' ' + team + ': ' + e.w + ' pixels wide, wanted ' + want);
            check(Math.abs(e.x + e.w / 2 - h.x * k) <= 0.5 && Math.abs(e.y + e.h / 2 - h.y * k) <= 0.5, side + ' x ' + side + ' ' + team + ': centered on its hill');
            check(e.c === (team === 'black' ? '#fff' : '#000'), side + ' x ' + side + ' ' + team + ': the edge is ' + (team === 'black' ? 'white' : 'black') + ' (' + e.c + ')');
            check(inner.c === COLORS[team] && inner.x === e.x + px && inner.y === e.y + px && inner.w === e.w - 2 * px && inner.h === e.h - 2 * px, side + ' x ' + side + ' ' + team + ': the team color inside an edge of one CSS pixel (' + inner.c + ')');
        }
    }
    // the starting ants: 5 CSS pixels, drawn where no hill of their team is near, left out beside one
    const f2 = fakeFile(60, 60, [hillBlock(245, 10, 10)], [{ team: 'black', row: 11, column: 11 }, { team: 'black', row: 50, column: 50 }, { team: 'red', row: 11, column: 11 }]);
    const ctx = recorder(); Mini.marks(ctx, f2, Mini.hills(f2), 156, 1);
    check(ctx.rects.length === 6, 'one hill (2 rectangles) and two ants that are not beside a hill of their team (' + ctx.rects.length + ')');
    const ants = ctx.rects.slice(2);
    check(ants.length === 4 && ants[0].w === 5 && ants[0].c === '#fff' && ants[1].c === '#161616' && ants[2].c === '#000' && ants[3].c === COLORS.red, 'an ant is a 5 pixel square with the edge and the color of its team');
    check(Math.abs(ants[0].x + 2.5 - 50.5 * 156 / 60) <= 1, 'an ant is centered on its cell');
}

// ---- a fake page for the box ----
function listeners() { const l = {}; return { l, addEventListener(t, f) { (l[t] = l[t] || []).push(f); }, fire(t, e) { (l[t] || []).forEach((f) => f(Object.assign({ preventDefault() {} }, e))); } }; }
function button() { return Object.assign(listeners(), { hidden: false, attrs: {}, textContent: '', focused: 0, captured: 0, setPointerCapture() { this.captured++; }, classes: {}, setAttribute(k, v) { this.attrs[k] = String(v); }, focus() { this.focused++; }, classList: { toggle: (k, on) => {} } }); }
function page(opts) {
    opts = opts || {};
    const store = opts.store || {}, room = { clientWidth: opts.w || 600, clientHeight: opts.h || 400 };
    const win = { devicePixelRatio: 1, localStorage: opts.noStorage ? { getItem() { throw new Error('no'); }, setItem() { throw new Error('no'); } } : { getItem: (k) => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); } } };
    const box = Object.assign(listeners(), { hidden: true, offsetWidth: 168, offsetHeight: 190, offsetLeft: 420, offsetTop: 12, parentNode: room, style: {} });
    const grip = button(), lock = button(), hide = button(), show = Object.assign(button(), { hidden: true, offsetWidth: 90, offsetHeight: 44, style: {} });
    grip.classes = {}; grip.classList = { toggle: (k, on) => { grip.classes[k] = !!on; } };
    const asked = [];
    const mini = Mini.create({ doc: null, win, minimap: box, mini: null, grip, lock, hide, show }, () => asked.push(1));
    // (the box sits where its style says, as the browser would put it: offsetLeft and offsetTop follow left and top once they are set)
    const sync = () => { if (box.style.left) { box.offsetLeft = parseFloat(box.style.left); box.offsetTop = parseFloat(box.style.top); } };
    const api = { store, room, win, box, grip, lock, hide, show, mini, asked, sync };
    api.gap = 1000;          // (the time between two presses: a second, unless a test is about two taps in a row)
    api.prevented = 0;          // (the presses and keys that the page was kept from)
    api.down = (x, y, extra) => { clock += api.gap; sync(); grip.fire('pointerdown', Object.assign({ pointerId: 1, button: 0, clientX: x, clientY: y, preventDefault() { api.prevented++; } }, extra)); };
    api.move = (x, y) => { grip.fire('pointermove', { pointerId: 1, clientX: x, clientY: y }); sync(); };
    api.up = (x, y) => grip.fire('pointerup', { pointerId: 1, clientX: x, clientY: y });
    api.key = (k) => { grip.fire('keydown', { key: k, preventDefault() { api.prevented++; } }); sync(); };
    api.drag = (x0, y0, x1, y1) => { api.down(x0, y0); api.move(x1, y1); api.up(x1, y1); };
    return api;
}
const KEEP = 'ants_mapmaker_minimap';
let clock = 1e6;
Date.now = () => clock;

function caseB() {
    const p = page();
    check(p.mini.update(false) === false && p.box.hidden && p.show.hidden, 'the whole map on the screen: no small map and no Show button');
    check(p.mini.update(true) === true && !p.box.hidden && p.show.hidden, 'a part of the map on the screen: the small map, not the button');
    p.hide.fire('click', {});
    check(p.box.hidden && !p.show.hidden, 'Hide: the box goes and the Show map button comes');
    check(p.show.focused === 1 && p.asked.length === 1, 'the focus goes to the Show map button (the Hide button is gone) and a new frame is asked for');
    check(p.mini.update(true) === false && p.box.hidden && !p.show.hidden, 'while hidden a part of the map on the screen keeps the box away and the button there');
    check(p.mini.update(false) === false && p.box.hidden && p.show.hidden, 'the whole map on the screen: neither');
    p.mini.update(true);
    p.show.fire('click', {});
    check(!p.box.hidden && p.show.hidden && p.hide.focused === 1, 'Show map: the box is back and the focus is on Hide');
    check(JSON.parse(p.store[KEEP])[2] === 0, 'what is kept says shown');
    p.hide.fire('click', {});
    check(JSON.parse(p.store[KEEP])[2] === 1, 'what is kept says hidden');
    const q = page({ store: p.store });
    check(q.mini.state().off === true && q.mini.update(true) === false && !q.show.hidden, 'a new visit finds it hidden, with the button');
    check(page({ store: { [KEEP]: 'rubbish' } }).mini.state().off === false, 'rubbish in the storage is ignored');
    check(page({ store: { [KEEP]: '[1,2,3]' } }).mini.state().off === false, 'a list of the wrong length is ignored');
    check(page({ store: { [KEEP]: '"abcd"' } }).mini.state().off === false, 'a text is ignored');
    check(page({ store: { [KEEP]: '{"2":1,"3":1,"length":4}' } }).mini.state().off === false, 'an object that looks like a list is ignored');
    check(page({ store: { [KEEP]: '[0,0,1]' } }).mini.state().off === false, 'a list of three is ignored');
    const odd = page({ store: { [KEEP]: '[true,false,1,1]' } });
    check(odd.mini.state().x === null && odd.mini.state().off === true && odd.mini.state().lock === true, 'booleans are not a place (the flags are read)');
    check(page({ store: { [KEEP]: '["12","34",0,0]' } }).mini.state().x === null, 'numbers in quotes are not a place');
    const n = page({ noStorage: true });
    n.mini.update(true); n.hide.fire('click', {}); n.lock.fire('click', {});
    check(n.mini.state().off === true && n.mini.state().lock === true && n.box.hidden, 'a storage that throws changes nothing for this visit');
}

function caseC() {
    const p = page();
    check(p.lock.attrs['aria-pressed'] === 'false' && p.lock.attrs['aria-label'] === 'Lock minimap position' && p.mini.state().lock === false, 'it starts unlocked');
    p.mini.update(true);
    p.lock.fire('click', {});
    check(p.mini.state().lock && p.lock.attrs['aria-pressed'] === 'true' && p.lock.attrs['aria-label'] === 'Unlock minimap position' && p.grip.attrs['aria-disabled'] === 'true' && p.grip.classes.locked === true, 'Lock: pressed, named Unlock, the grip looks locked');
    check(JSON.parse(p.store[KEEP])[3] === 1, 'the lock is kept');
    check(page({ store: p.store }).mini.state().lock === true, 'a new visit finds it locked');
    p.api = null;
    p.drag(500, 20, 300, 200);
    check(p.box.style.left === undefined || p.box.style.left === '', 'locked: a drag does not move it');
    p.key('ArrowLeft'); p.key('ArrowDown');
    check(p.box.style.left === undefined || p.box.style.left === '', 'locked: the arrow keys do not move it');
    p.hide.fire('click', {});
    check(p.box.hidden && !p.show.hidden, 'locked: Hide still works');
    p.show.fire('click', {});
    check(!p.box.hidden, 'locked: Show still works');
    p.lock.fire('click', {});
    check(!p.mini.state().lock && p.lock.attrs['aria-label'] === 'Lock minimap position' && p.grip.classes.locked === false && p.grip.attrs['aria-disabled'] === 'false', 'Unlock: back to the first look');
    p.drag(500, 20, 400, 100);
    check(p.box.style.left === '320px' && p.box.style.top === '92px', 'unlocked: a drag moves it again');
    // two taps and Home do nothing while locked; the room that shrinks still brings it inside
    p.lock.fire('click', {});
    p.gap = 100; p.down(10, 10); p.up(10, 10); p.down(10, 10); p.up(10, 10); p.key('Home'); p.gap = 1000;
    check(p.box.style.left === '320px' && p.box.style.top === '92px', 'locked: two taps and Home do not bring it back to the corner');
    p.room.clientWidth = 400; p.room.clientHeight = 200;
    p.mini.update(true);
    check(p.box.style.left === '232px' && p.box.style.top === '10px', 'locked in a room that is smaller: still brought inside (' + p.box.style.left + ',' + p.box.style.top + ')');
    check(JSON.parse(p.store[KEEP])[0] === 320, 'what the person chose is kept, not the squeezed place');
}

function caseD() {
    const p = page();
    p.mini.update(true);
    equal([p.box.style.left, p.box.style.top], ['', ''], 'no place of its own: the corner of the style');
    p.drag(500, 20, 400, 80);
    equal([p.box.style.left, p.box.style.top, p.box.style.right], ['320px', '72px', 'auto'], 'a drag moves it by what the pointer moved');
    check(p.grip.captured === 1 && p.prevented === 1, 'the grip captures the pointer and keeps the page from the press (no scroll, no text selection)');
    equal(JSON.parse(p.store[KEEP]).slice(0, 2), [320, 72], 'the place is kept');
    p.drag(400, 80, -900, -900);
    equal([p.box.style.left, p.box.style.top], ['0px', '0px'], 'a drag past the top and the left stops at the room');
    p.drag(400, 80, 9000, 9000);
    equal([p.box.style.left, p.box.style.top], ['432px', '210px'], 'a drag past the bottom and the right stops at the room (600 x 400, a box of 168 x 190)');
    p.hide.fire('click', {});
    equal([p.show.style.left, p.show.style.top], ['432px', '210px'], 'the Show map button stands where the box was');
    p.show.fire('click', {});
    // the arrow keys: a step of 16, kept inside; Home
    const kept = p.prevented; p.key('Home');
    check(p.prevented === kept + 1, 'Home is kept from scrolling the page');
    equal([p.box.style.left, p.box.style.top, p.box.style.right, p.mini.state().x], ['', '', '', null], 'Home: back to the corner');
    p.box.offsetLeft = 420; p.box.offsetTop = 12;
    p.key('ArrowLeft');
    equal([p.box.style.left, p.box.style.top], ['404px', '12px'], 'an arrow key moves it 16 pixels');
    p.key('ArrowDown'); p.key('ArrowDown'); p.key('ArrowRight');
    equal([p.box.style.left, p.box.style.top], ['420px', '44px'], 'down, down, right');
    const before = p.prevented; p.key('ArrowUp');
    check(p.prevented === before + 1, 'an arrow key is kept from scrolling the page');
    p.key('x');
    check(p.prevented === before + 1, 'a letter is not');
    equal([p.box.style.left, p.box.style.top], ['420px', '28px'], 'up (and a letter does nothing)');
    for (let i = 0; i < 40; i++) p.key('ArrowRight');
    check(p.box.style.left === '432px', 'the arrow keys stop at the room');
    // a tap is not a drag; two taps in a row bring it back; two taps far apart in time do not
    p.drag(100, 100, 100, 100);
    equal(p.box.style.left, '432px', 'a tap moves nothing');
    p.gap = 100;
    p.down(100, 100); p.move(103, 104); p.up(103, 104);
    p.down(100, 100); p.move(102, 99); p.up(102, 99);
    p.gap = 1000;
    equal([p.box.style.left, p.mini.state().x], ['', null], 'two taps that wobble a little (under 6 pixels): back to the corner');
    p.box.offsetLeft = 420; p.box.offsetTop = 12;
    p.drag(500, 20, 450, 60);
    p.down(1, 1); p.up(1, 1);
    p.down(1, 1); p.up(1, 1);
    check(p.box.style.left === '370px', 'two taps seconds apart are two single taps');
    p.drag(500, 20, 450, 60);
    p.gap = 100; p.down(1, 1); p.up(1, 1); p.drag(500, 20, 400, 60); p.down(1, 1); p.up(1, 1); p.gap = 1000;
    check(p.mini.state().x !== null, 'a tap, a drag and a tap are not two taps in a row');
    p.drag(500, 20, 450, 60);
    // a right button and another pointer do not drag
    const x0 = p.box.style.left;
    p.down(500, 20, { button: 2 }); p.move(300, 300); p.up(300, 300);
    equal(p.box.style.left, x0, 'a right button does not drag');
    p.down(500, 20); p.grip.fire('pointermove', { pointerId: 9, clientX: 300, clientY: 300 }); p.up(300, 300);
    equal(p.box.style.left, x0, 'another pointer does not move it');
    p.down(500, 20); p.grip.fire('pointercancel', {}); p.move(300, 300);
    equal(p.box.style.left, x0, 'a cancelled pointer ends the drag');

    // the room that shrinks: brought inside, what was chosen is kept; the room that grows: the place is the chosen one again
    const q = page();
    q.mini.update(true); q.drag(500, 20, 400, 150);
    const chosen = [q.box.style.left, q.box.style.top];
    q.room.clientWidth = 300; q.room.clientHeight = 250; q.mini.update(true);
    equal([q.box.style.left, q.box.style.top], ['132px', '60px'], 'a room of 300 x 250: brought inside');
    equal(JSON.parse(q.store[KEEP]).slice(0, 2), [320, 142], 'and the chosen place is still what is kept');
    q.room.clientWidth = 600; q.room.clientHeight = 400; q.mini.update(true);
    check(q.box.style.left === '132px', 'a room that grows again does not move it back by itself');
    // the Show map button is the size that is kept inside the room while the box is hidden
    const h = page(); h.mini.update(true); h.drag(500, 20, 9000, 9000); h.hide.fire('click', {});
    h.room.clientWidth = 400; h.room.clientHeight = 300; h.mini.update(true);
    equal([h.show.style.left, h.show.style.top], ['310px', '210px'], 'hidden, in a smaller room: the Show map button is kept inside by its own size');
    // a stored place: kept when it fits, dropped (the corner) when it does not
    const fits = page({ store: { [KEEP]: '[100,50,0,0]' } }); fits.mini.update(true);
    equal([fits.box.style.left, fits.box.style.top], ['100px', '50px'], 'a stored place that fits is used');
    const off = page({ store: { [KEEP]: '[900,500,0,0]' } }); off.mini.update(true);
    equal([off.box.style.left, off.box.style.top, off.mini.state().x], ['', '', null], 'a stored place that does not fit is dropped: the corner');
    off.drag(500, 20, 400, 80);
    check(off.box.style.left === '320px', 'and a drag after that works');
    const neg = page({ store: { [KEEP]: '[-5,10,0,0]' } }); neg.mini.update(true);
    check(neg.mini.state().x === null, 'a stored place outside the room on the left is dropped');
    const nul = page({ store: { [KEEP]: '[null,null,1,1]' } });
    check(nul.mini.state().x === null && nul.mini.state().off && nul.mini.state().lock, 'no place, hidden and locked, is read');
    const txt = page({ store: { [KEEP]: '["a","b",0,0]' } });
    check(txt.mini.state().x === null, 'a place that is not a number is not taken');
    txt.mini.update(true);
    check(txt.mini.state().x === null, 'and stays dropped');

    // a drag of the box never reaches the canvas: the real canvas on a fake page keeps its painter out of it
    const log = [], ctx = new Proxy({}, { get: (t, k) => (k in t ? t[k] : () => undefined), set: (t, k, v) => { t[k] = v; return true; } });
    const node = () => Object.assign(listeners(), { width: 0, height: 0, attrs: {}, getContext: () => ctx, getBoundingClientRect: () => ({ left: 0, top: 0, width: 600, height: 400 }), setAttribute(k, v) { this.attrs[k] = v; }, removeAttribute() {}, getAttribute() { return null; }, setPointerCapture() {}, focus() {}, classList: { toggle() {} }, hidden: false, style: {} });
    const g = page(), cv = node();
    const api = Canvas.create({ doc: { createElement: node, addEventListener() {}, visibilityState: 'visible' }, win: Object.assign(listeners(), { devicePixelRatio: 1, performance: { now: () => 0 }, localStorage: g.win.localStorage }), canvas: cv, minimap: g.box, mini: node(), miniCap: node(), grip: g.grip, lock: g.lock, hide: g.hide, show: g.show, where: node(), zoomIn: node(), zoomOut: node() });
    api.setPainter({ size: () => 1, begin: () => { log.push('begin'); return true; }, extend: () => log.push('extend'), end: () => log.push('end'), cancel: () => log.push('cancel') });
    g.drag(500, 20, 300, 100);
    equal(log, [], 'a drag of the box starts no stroke');
    check(!!g.grip.l.pointerdown && !!cv.l.pointerdown, 'the canvas has its own handlers, and the grip has its own');
}

const cases = [['A the hills', caseA], ['B Hide and Show', caseB], ['C Lock', caseC], ['D the place', caseD]];
for (const [name, fn] of cases) {
    const before = failures;
    try { fn(); } catch (e) { failures++; console.log('    FAILED: ' + name + ' stopped: ' + (e && e.stack || e)); }
    console.log('RUNNING: ' + name + ' ... ' + (failures === before ? 'PASS' : 'FAIL'));
}
console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
