#!/usr/bin/env node
// The map maker's dialogs (web/front/mapmaker_dialog.js; docs/MAP_EDITOR.md) run on a small fake of the DOM that is written here (no dependency): the box that holds the focus, closes on
// Escape (also when a click on plain text has moved the focus to the body), wraps Tab and leaves the page behind it inert, and the New map dialog with a stand-in for the core.
//
//   node tests/scripts/web_mapmaker_dialog_check.js [--front DIR]
//
//   --front DIR  the folder of the mapmaker_*.js files (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
// What this does not reach, and the opt-in browser check (tests/scripts/test_web_mapmaker.sh) does: layout, real focus, pointer and touch events, the canvas and the page's start
// (web/front/mapmaker_canvas.js and mapmaker_app.js need a real canvas and the core; what the app shows of a map is MapSizes.mapFacts, which web_mapmaker_check.js holds).
'use strict';

const path = require('path');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.resolve(__dirname, '..', '..', 'web', 'front');
const Dialog = require(path.join(FRONT, 'mapmaker_dialog.js')), Sizes = require(path.join(FRONT, 'mapmaker_sizes.js'));

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }

const { makeDocument } = require('./web_mapmaker_fake_dom.js');

function page() {
    const doc = makeDocument(), host = doc.createElement('div'), top = doc.createElement('header'), body = doc.createElement('div'), opener = doc.createElement('button');
    [host, top, body].forEach((n) => doc.body.appendChild(n));
    top.appendChild(opener);
    opener.focus();
    return { doc, host, top, body, opener, dialogs: Dialog.create(doc, host, [top, body]) };
}

function boxChecks() {
    const p = page(), pressed = [];
    const ctl = p.dialogs.open({
        title: 'A box', build: (b) => { const t = p.doc.createElement('input'); t.type = 'text'; b.appendChild(t); },
        buttons: [{ id: 'one', text: 'One', onClick: () => pressed.push('one'), keepOpen: true }, { id: 'two', text: 'Two' }]
    });
    check(p.dialogs.isOpen() && p.host.querySelector('.dlg') !== null, 'a dialog opens');
    equal([p.top.hasAttribute('inert'), p.body.hasAttribute('inert')], [true, true], 'the page behind it is inert');
    check(p.doc.activeElement.tagName === 'input' || p.doc.activeElement.tagName === 'button', 'the focus is inside it: ' + p.doc.activeElement.tagName);
    equal(p.host.querySelector('.dlg').getAttribute('role') + '/' + p.host.querySelector('.dlg').getAttribute('aria-modal'), 'dialog/true', 'it is a modal dialog');
    check(p.host.querySelector('h2').textContent === 'A box' && p.host.querySelector('.dlg').getAttribute('aria-labelledby') === p.host.querySelector('h2').id, 'named by its heading');
    // Tab wraps
    const buttons = p.host.querySelectorAll('button');
    buttons[buttons.length - 1].focus();
    let e = p.doc.key('Tab');
    check(e.prevented && p.doc.activeElement !== buttons[buttons.length - 1] && p.host.contains(p.doc.activeElement), 'Tab from the last control goes to the first');
    p.doc.activeElement.focus();
    const firstControl = p.host.querySelectorAll('input, button')[0];
    firstControl.focus();
    e = p.doc.key('Tab', { shiftKey: true });
    check(e.prevented && p.doc.activeElement === buttons[buttons.length - 1], 'Shift+Tab from the first control goes to the last');
    p.doc.activeElement = p.doc.body;                                       // a click on plain text: the focus is on the page's body
    e = p.doc.key('Tab');
    check(e.prevented && p.host.contains(p.doc.activeElement), 'Tab with the focus outside comes back into the box');
    // a button that keeps the box open
    buttons[0].click();
    equal(pressed, ['one'], 'a button runs its function'); check(p.dialogs.isOpen(), 'and keeps the box open when it says so');
    // Escape with the focus on the body closes it
    p.doc.activeElement = p.doc.body;
    p.doc.key('Escape');
    check(!p.dialogs.isOpen() && p.host.querySelector('.dlg') === null, 'Escape closes it even when the focus is on the page\'s body');
    equal([p.top.hasAttribute('inert'), p.body.hasAttribute('inert')], [false, false], 'the page is live again');
    check(p.doc.activeElement === p.opener, 'the focus is back where it was');
    equal((p.doc.listeners.keydown || []).length, 0, 'no key listener is left on the document');
    // a second dialog replaces the first
    p.dialogs.open({ title: 'First', buttons: [{ text: 'Close' }] });
    p.dialogs.open({ title: 'Second', buttons: [{ text: 'Close' }] });
    equal(p.host.querySelectorAll('.dlg').length, 1, 'one dialog at a time'); equal((p.doc.listeners.keydown || []).length, 1, 'with one key listener');
    p.host.querySelectorAll('button')[0].click();
    check(!p.dialogs.isOpen() && (p.doc.listeners.keydown || []).length === 0, 'a button without keepOpen closes it');
    // the menu and the message
    let chose = 0;
    p.dialogs.menu([{ text: 'New', onClick: () => { chose++; } }, { text: 'Open', onClick() {}, disabled: true }]);
    check(p.host.querySelectorAll('.menulist button').length === 2 && p.host.querySelectorAll('.menulist button')[1].disabled, 'the menu lists its items, and a disabled one stays off');
    p.host.querySelectorAll('.menulist button')[0].click();
    check(chose === 1 && !p.dialogs.isOpen(), 'an item closes the menu and does its work');
    p.dialogs.message('Oops', 'A <b>sentence</b> with markup');
    equal(p.host.querySelector('.fatalmsg').textContent, 'A <b>sentence</b> with markup', 'a message is text');
    // clicking inside .dlg does not close it; clicking the outer .scrim closes it
    p.host.querySelector('.dlg').fire('click', { target: p.host.querySelector('.dlg') });
    check(p.dialogs.isOpen(), 'clicking inside the dialog keeps it open');
    p.host.querySelector('.scrim').fire('click', { target: p.host.querySelector('.scrim') });
    check(!p.dialogs.isOpen(), 'clicking the outer scrim closes the dialog');
}

function newMapChecks() {
    const p = page(), limits = Sizes.limitsOf({ classic: { rows: 90, columns: 130, cells: 10000 } }, { classic: { rows: 256, columns: 256, cells: 65536 }, extended: { rows: 1024, columns: 1024, cells: 262144 } });
    const asked = [], made = [];
    const tall = 'a blank map of 100 rows x 80 columns cannot be written: the grid has 100 rows x 80 columns: a Classic map made now has at most 90 rows (ants cannot enter or cross the 91st row of a taller map, in the original game too); make it an Extended map?';
    const make = (kind, rows, columns) => {
        asked.push([kind, rows, columns]);
        if (kind === 'classic' ? rows > 90 || columns > 130 || rows * columns > 10000 : rows > 1024 || columns > 1024 || rows * columns > 262144) {
            return { status: 1, error: kind === 'classic' ? tall : 'a blank map of ' + rows + ' rows x ' + columns + ' columns cannot be written: the grid has ' + rows + ' rows x ' + columns + ' columns, ' + rows * columns + ' cells, more than the 262144 allowed here' };
        }
        return { status: 0, bytes: new Uint8Array([kind === 'classic' ? 8 : 9]) };
    };
    const ctl = Dialog.newMap({ doc: p.doc, dialogs: p.dialogs, limits, make, onMade: (bytes, kind) => made.push([bytes[0], kind]) });
    const q = (s) => p.host.querySelector(s), live = () => q('.live').textContent, makeButton = () => q('.dlg-foot').querySelectorAll('button')[1];
    const type = (id, text) => { const i = q('#' + id); i.value = text; i.fire('input', {}); };
    equal([q('#size-rows').value, q('#size-columns').value, q('.kind.on .kt').textContent], ['31', '31', 'Classic map'], 'it starts with a 31 x 31 Classic map');
    equal(live(), '961 cells of 10,000 · the file will be about 17 KB', 'the live line');
    check(!makeButton().disabled && q('.note') === null, 'Make map is on and there is no note');
    equal(p.host.querySelectorAll('.kx').map((n) => n.textContent), ['Up to 90 rows and 130 columns, 10,000 cells in all.', 'Up to 512 x 512 cells (262,144 in all). The longest side can be 1,024.'], 'the limits on the cards');
    type('size-rows', '100'); type('size-columns', '80');
    check(makeButton().disabled && q('.note.bad') !== null, 'a size that is too big turns Make map off and gives a note');
    equal(q('.note.bad').textContent, '!A Classic map made now has at most 90 rows (ants cannot enter or cross the 91st row of a taller map, in the original game too).Make it an Extended map', 'with the core\'s sentence and the button');
    equal([q('#size-rows').getAttribute('aria-invalid'), q('#size-columns').getAttribute('aria-invalid')], ['true', null], 'the box that is too big is marked');
    check(asked.length > 0 && asked.every((a) => a[0] === 'classic'), 'the refusal came from the core (the stand-in was asked)');
    q('.note').querySelector('.btn').click();
    equal([q('.kind.on .kt').textContent.slice(0, 12), q('#size-rows').value, q('#size-columns').value, q('.note.bad')], ['Extended map', '100', '80', null], 'the button chooses the Extended map and keeps the sizes that were typed');
    check(!makeButton().disabled, '100 x 80 is fine as an Extended map');
    type('size-rows', '513'); type('size-columns', '513');
    check(makeButton().disabled && /263,169 cells, more than the 262,144 allowed here/.test(q('.note.bad').textContent), 'too many cells: refused with the core\'s sentence, numbers with commas');
    type('size-rows', ''); check(makeButton().disabled && live() === 'Type a whole number of rows and of columns, at least 1.', 'an empty box asks for a number');
    type('size-rows', '1'); type('size-columns', '1'); equal(live(), '1 cell of 262,144 · the file will be about 5 KB', 'one cell is "1 cell"');
    // the kinds keep their own sizes
    q('.kind input').fire('change', {}); q('.kind input').checked = true; q('.kind input').fire('change', {});
    equal([q('.kind.on .kt').textContent, q('#size-rows').value], ['Classic map', '100'], 'back to the Classic map: its sizes are as it had them');
    q('.kind input').checked = false; p.host.querySelectorAll('.kind input')[1].checked = true; p.host.querySelectorAll('.kind input')[1].fire('change', {});
    type('size-rows', '300'); type('size-columns', '400');
    check(q('.note') !== null && /Only the new version can open an Extended map\./.test(q('.note').textContent) && !/Classic/.test(q('.note').textContent), 'an Extended map gets the one note, and says nothing about converting back');
    // Enter makes the map, and the map is made by the core's call
    q('#size-columns').fire('keydown', { key: 'Enter', preventDefault() {} });
    equal(made, [[9, 'extended']], 'Enter makes the Extended map'); check(!p.dialogs.isOpen(), 'and the dialog closes');
    equal(asked[asked.length - 1], ['extended', 300, 400], 'with the sizes of the boxes');
    equal(p.doc.activeElement === p.opener, true, 'the focus is back');
}

function resizeMapChecks() {
    const p = page(), limits = Sizes.limitsOf({ classic: { rows: 90, columns: 130, cells: 10000 } }, { classic: { rows: 256, columns: 256, cells: 65536 }, extended: { rows: 1024, columns: 1024, cells: 262144 } });
    const resized = [];
    Dialog.resizeMap({ doc: p.doc, dialogs: p.dialogs, limits, kind: 'classic', rows: 31, columns: 31, onResize: (r, c) => resized.push([r, c]) });
    const q = (s) => p.host.querySelector(s), btn = () => q('.dlg-foot').querySelectorAll('button')[1];
    const type = (id, text) => { const i = q('#' + id); i.value = text; i.fire('input', {}); };
    equal([q('#resize-rows').value, q('#resize-columns').value, btn().disabled], ['31', '31', true], 'starts with current size and Resize map disabled');
    type('resize-rows', '20');
    check(!btn().disabled && btn().textContent === 'Choose part to keep...' && q('.note') !== null, 'shrinking enables the button and offers Choose part to keep...');
    type('resize-rows', '100');
    check(btn().disabled && q('.note.bad') !== null, 'a size beyond Classic limits is refused');
    type('resize-rows', '40'); type('resize-columns', '50');
    check(!btn().disabled && btn().textContent === 'Resize map' && q('.note') === null, 'growing enables Resize map');
    btn().click();
    equal(resized, [[40, 50]], 'clicking Resize map calls onResize with the new rows and columns');
}

function openMapChecks() {
    const p = page();
    const entries = [
        { file: 'TINY.LVL', name: 'Tiny', original: true, order: 0, width: 31, height: 31, minutes: 5, players: 4, description: 'Small battle', plays: 12, thumb: '/front/preview_tiny.png' },
        { file: 'Mosh Pit.lvl', name: 'Mosh Pit', original: false, kind: 'extended', order: 1, width: 256, height: 256, minutes: 10, players: 4, description: 'Brawl in the center', plays: 50, thumb: '' }
    ];
    let diskClicked = 0, chosen = [];
    const store = {
        loadLibrary() {},
        libraryState: () => 'ready',
        libraryList: () => entries,
        fetchMap: (file, cb) => cb('', new Uint8Array([8, 0, file.length]))
    };
    Dialog.openMap({
        doc: p.doc, dialogs: p.dialogs, store,
        numbered: (n) => n + ' 2',
        onDisk: () => { diskClicked++; },
        onChoose: (bytes, name) => chosen.push([bytes[2], name])
    });
    const q = (s) => p.host.querySelector(s), all = (s) => p.host.querySelectorAll(s);
    equal(q('h2').textContent, 'Open a map', 'Open a map dialog title');
    equal(all('.open-row').length, 2, 'shows the two library maps');
    equal(q('.open-ext').textContent, 'Extended', 'shows the Extended badge on an Extended library map');
    check(/Tiny 2/.test(q('.open-copy-note').textContent), 'shows the free numbered copy name for the selected map');
    // search filters the list
    q('.open-q').value = 'brawl'; q('.open-q').fire('input', {});
    equal(all('.open-row').length, 1, 'search filters by description');
    check(/Mosh Pit 2/.test(q('.open-copy-note').textContent), 'updates the selected map and its copy name');
    // clicking Use as starting point downloads and opens the map
    q('.dlg-foot').querySelectorAll('button')[1].click();
    equal(chosen, [[12, 'Mosh Pit 2']], 'Use as starting point fetches the map and opens it under its numbered copy name');
    check(!p.dialogs.isOpen(), 'and closes the dialog');
    // clicking Open a .lvl file from your device... closes the dialog and triggers onDisk
    Dialog.openMap({ doc: p.doc, dialogs: p.dialogs, store, numbered: (n) => n + ' 2', onDisk: () => { diskClicked++; }, onChoose() {} });
    q('#open-disk-btn').click();
    equal([diskClicked, p.dialogs.isOpen()], [1, false], 'Open from device triggers onDisk and closes the dialog');
    // a failed download is told as plain text: whatever the store says never becomes markup (the words are fixed today; this holds them to textContent)
    const bad = '<img src=x onerror=alert(1)>';
    const badStore = { loadLibrary() {}, libraryState: () => 'ready', libraryList: () => entries, fetchMap: (file, cb) => cb(bad, null) };
    chosen = [];
    Dialog.openMap({ doc: p.doc, dialogs: p.dialogs, store: badStore, numbered: (n) => n + ' 2', onDisk() {}, onChoose: (bytes, name) => chosen.push(name) });
    q('.dlg-foot').querySelectorAll('button')[1].click();
    equal([q('.open-copy-note').textContent, q('.open-copy-note').className, q('.open-copy-note').children.length, chosen.length, p.dialogs.isOpen()], [bad, 'open-copy-note bad', 0, 0, true], 'a failed download shows its words as text, in the bad style, and keeps the dialog open');
}

for (const [name, fn] of [['A the box', boxChecks], ['B New map', newMapChecks], ['C Resize map', resizeMapChecks], ['D Open a map', openMapChecks]]) {
    process.stdout.write('  RUNNING: ' + name + ' ... ');
    const before = failures;
    try { fn(); } catch (e) { failures++; console.log('\n    FAILED: threw ' + (e && e.stack ? e.stack : e)); }
    if (failures === before) console.log('PASS');
}
console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
