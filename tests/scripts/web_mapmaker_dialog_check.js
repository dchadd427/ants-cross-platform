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

// ---- a fake DOM: just what the dialogs use ----
function makeDocument() {
    const doc = { listeners: {}, activeElement: null };
    function matches(node, text) {                                          // (a list with commas; a part may hold descendants: "a b")
        return text.split(',').some((part) => {
            const chain = part.trim().split(/\s+/);
            if (!simple(node, chain[chain.length - 1])) return false;
            let up = node.parentNode;
            for (let i = chain.length - 2; i >= 0; i--) {
                while (up && !simple(up, chain[i])) up = up.parentNode;
                if (!up) return false;
                up = up.parentNode;
            }
            return true;
        });
    }
    function simple(node, part) {
        return [part].some((part) => {
            const m = /^\s*([a-z0-9]*)((?:\[[^\]]+\]|:not\((?:\[[^\]]+\])\)|:checked|[.#][\w-]+)*)\s*$/.exec(part);
            if (!m) throw new Error('the fake DOM does not know the selector ' + part);
            if (m[1] && node.tagName !== m[1]) return false;
            const rest = m[2].match(/:not\(\[[^\]]+\]\)|\[[^\]]+\]|:checked|[.#][\w-]+/g) || [];
            return rest.every((q) => {
                if (q === ':checked') return !!node.checked;
                if (q[0] === '#') return node.id === q.slice(1);
                if (q[0] === '.') return node.className.split(/\s+/).includes(q.slice(1));
                const negate = q.startsWith(':not('), inner = negate ? q.slice(5, -1) : q;
                const am = /^\[([\w-]+)(?:=("?)([^"\]]*)\2)?\]$/.exec(inner);
                const has = am[1] === 'disabled' ? !!node.disabled : node.hasAttribute(am[1]);
                const ok = has && (am[3] === undefined || node.getAttribute(am[1]) === am[3]);
                return negate ? !ok : ok;
            });
        });
    }
    function node(tagName) {
        const n = { tagName, className: '', children: [], parentNode: null, attrs: {}, listeners: {}, own: '', style: {}, disabled: false, checked: false, value: '', id: '', type: '' };
        n.appendChild = (c) => { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = n; n.children.push(c); return c; };
        n.removeChild = (c) => { n.children.splice(n.children.indexOf(c), 1); c.parentNode = null; return c; };
        n.setAttribute = (k, v) => { n.attrs[k] = String(v); };
        n.getAttribute = (k) => (k in n.attrs ? n.attrs[k] : null);
        n.removeAttribute = (k) => { delete n.attrs[k]; };
        n.hasAttribute = (k) => k in n.attrs;
        n.addEventListener = (t, f) => { (n.listeners[t] = n.listeners[t] || []).push(f); };
        n.classList = { toggle: (c, on) => { const l = n.className.split(/\s+/).filter(Boolean).filter((x) => x !== c); if (on) l.push(c); n.className = l.join(' '); } };
        n.all = () => n.children.reduce((a, c) => a.concat([c], c.all ? c.all() : []), []);
        n.querySelectorAll = (s) => n.all().filter((x) => matches(x, s));
        n.querySelector = (s) => n.querySelectorAll(s)[0] || null;
        n.contains = (o) => o === n || n.all().includes(o);
        n.focus = () => { doc.activeElement = n; };
        n.click = () => { if (!n.disabled) n.fire('click', {}); };
        n.fire = (type, e) => { e.type = type; e.target = e.target || n; for (const f of n.listeners[type] || []) f(e); };
        Object.defineProperty(n, 'offsetParent', { get: () => (doc.body.contains(n) ? doc.body : null) });
        Object.defineProperty(n, 'textContent', { get: () => n.own + n.children.map((c) => c.textContent).join(''), set: (v) => { n.own = String(v); n.children = []; } });
        return n;
    }
    doc.createElement = node;
    doc.createTextNode = (t) => { const n = node('#text'); n.own = String(t); return n; };
    doc.body = node('body');
    doc.activeElement = doc.body;
    doc.addEventListener = (t, f) => { (doc.listeners[t] = doc.listeners[t] || []).push(f); };
    doc.removeEventListener = (t, f) => { doc.listeners[t] = (doc.listeners[t] || []).filter((x) => x !== f); };
    // a key goes to the document's listeners (the dialog listens there) after the target's own
    doc.key = (key, extra) => {
        const e = Object.assign({ type: 'keydown', key, shiftKey: false, target: doc.activeElement, prevented: false, preventDefault() { this.prevented = true; }, stopPropagation() {} }, extra || {});
        for (const f of (doc.listeners.keydown || []).slice()) f(e);
        return e;
    };
    return doc;
}

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
    p.doc.key('Escape');
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

for (const [name, fn] of [['A the box', boxChecks], ['B New map', newMapChecks]]) {
    process.stdout.write('  RUNNING: ' + name + ' ... ');
    const before = failures;
    try { fn(); } catch (e) { failures++; console.log('\n    FAILED: threw ' + (e && e.stack ? e.stack : e)); }
    if (failures === before) console.log('PASS');
}
console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
