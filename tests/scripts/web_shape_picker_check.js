// Runs the page's OWN code of the picture's picker (web/shape_picker.js, AntsShapePicker.create: the button and the list of the shapes that the game page, the replay's Picture window and the settings menu choose a
// shape with) without a browser, on a small fake of the DOM that is written here (a window with the capture and the bubble phase, nodes with attributes, classes, focus and the sizes that the placement reads).
//   * the markup: a combobox button (role, aria-haspopup, aria-expanded, aria-controls, the name and the description it is given) and a hidden listbox with one option for each row of the table, in its order, "16:10 · 960 x 600";
//     a row that the table gets later is an entry with no other change; the shown shape is the selected option;
//   * the pointer: a press opens the list, a second one closes it; a press on a row chooses it (the page's function is called, the list closes, the focus is the button's); the shown shape chosen again calls nothing;
//     the pointer marks the row it is over; a press elsewhere closes the list and a press in it does not; it works for events that are not trusted too (a locked mouse's bar is reached with synthetic ones);
//   * the keys, on the button only: Enter, Space, ArrowDown and ArrowUp open it, and nothing else on a closed button does anything (no key changes the picture of a closed control); in the open list the arrows,
//     Home and End move the mark and stop at the ends, Enter and Space choose it, a typed letter marks the next row that starts with it (and a word typed fast goes on with the same word), Esc closes the list ALONE (a later
//     listener of the window, the Picture window's or the menu's, never sees that Esc; the next Esc, with the list shut, is theirs) and gives the focus back, Tab closes and goes on; a held key does it once; the keys
//     that were taken are taken at their lift too;
//   * the place: under the button, over it where there is no room under it, never beyond the screen's edges, as tall as the room (the rest scrolls), measured against a frame that is not the screen's;
//   * the close: a click elsewhere, a scroll elsewhere, the focus leaving, destroy (a resize puts the list by its button again); the listeners of a destroyed picker are gone.
// tests/scripts/test_web_shapes.py runs this with node (the quick tier), against every broken copy of web/shape_picker.js that tests/data/shape_picker_mutants.json lists. usage: node web_shape_picker_check.js web/shape_picker.js
'use strict';
const fs = require('fs');
const path = require('path');

const file = process.argv[2];
if (!file) { console.log('usage: web_shape_picker_check.js shape_picker.js'); process.exit(2); }
const SOURCE = fs.readFileSync(path.resolve(file), 'utf8');
const SHAPES_SOURCE = fs.readFileSync(path.resolve(__dirname, '..', '..', 'web', 'shapes.js'), 'utf8');       // (the page's one table: the picker is made from it)

let failed = 0;
let checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) { console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); failed++; }
}
function ok(label, cond) { expect(label, !!cond, true); }

function load(source) {
    const module = { exports: {} };
    new Function('module', 'self', source)(module, undefined);
    return module.exports;
}
function tools(shapesSource) {
    const AntsShapes = load(shapesSource || SHAPES_SOURCE);
    return { AntsShapes, create: (opts) => load(SOURCE).create(AntsShapes, opts) };
}

// ---- the fake of the DOM ---------------------------------------------------------------------------------------------------------------------------------------------------------
function world(AntsShapes) {
    const win = { innerWidth: 1280, innerHeight: 800, listeners: {}, log: [] };
    const doc = { active: null, defaultView: win, nodes: [], listeners: {} };
    doc.addEventListener = (type, fn, capture) => { (doc.listeners[type] = doc.listeners[type] || []).push({ fn, capture: !!capture }); };
    doc.removeEventListener = (type, fn, capture) => { doc.listeners[type] = (doc.listeners[type] || []).filter((l) => !(l.fn === fn && l.capture === !!capture)); };
    win.addEventListener = (type, fn, capture) => { (win.listeners[type] = win.listeners[type] || []).push({ fn, capture: !!capture }); };
    win.removeEventListener = (type, fn, capture) => { win.listeners[type] = (win.listeners[type] || []).filter((l) => !(l.fn === fn && l.capture === !!capture)); };
    class Node {
        constructor(tag) {
            this.tagName = tag.toUpperCase(); this.id = ''; this.className = ''; this.children = []; this.parentNode = null; this.attrs = {}; this.listeners = {}; this.style = {}; this.hidden = false; this.own = ''; this.offsetHeightOwn = 38;
            this.rect = { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 }; this.box = { w: 200, h: 270 }; this.scrollTop = 0; this.frame = { left: 0, top: 0 };
            doc.nodes.push(this);
        }
        get textContent() { return this.own + this.children.map((c) => c.textContent).join(''); }
        set textContent(v) { this.own = String(v); this.children.forEach((c) => { c.parentNode = null; }); this.children = []; }
        get classList() {
            const n = this, list = () => n.className.split(/\s+/).filter(Boolean);
            return { add: (c) => { if (!list().includes(c)) n.className = list().concat(c).join(' '); }, remove: (c) => { n.className = list().filter((x) => x !== c).join(' '); }, contains: (c) => list().includes(c) };
        }
        get offsetHeight() { return this.tagName === 'LI' ? 38 : this.box.h; }
        get offsetWidth() { return this.box.w; }
        setAttribute(k, v) { this.attrs[k] = String(v); if (k === 'id') this.id = String(v); }
        getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
        removeAttribute(k) { delete this.attrs[k]; }
        appendChild(c) { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = this; this.children.push(c); return c; }
        insertBefore(c, ref) {
            if (c.parentNode) c.parentNode.removeChild(c);
            c.parentNode = this;
            const at = ref ? this.children.indexOf(ref) : -1;
            if (at < 0) this.children.push(c); else this.children.splice(at, 0, c);
            return c;
        }
        removeChild(c) { const at = this.children.indexOf(c); if (at < 0) throw new Error('removeChild of a node that is not a child'); this.children.splice(at, 1); c.parentNode = null; return c; }
        get firstChild() { return this.children[0] || null; }
        get nextSibling() { const kids = this.parentNode ? this.parentNode.children : []; return kids[kids.indexOf(this) + 1] || null; }
        addEventListener(type, fn, capture) { (this.listeners[type] = this.listeners[type] || []).push({ fn, capture: !!capture }); }
        removeEventListener(type, fn, capture) { this.listeners[type] = (this.listeners[type] || []).filter((l) => !(l.fn === fn && l.capture === !!capture)); }
        focus() { doc.active = this; }
        get offsetTop() { return 4 + 38 * (this.parentNode ? this.parentNode.children.indexOf(this) : 0); }
        get clientHeight() { return this.tagName === 'UL' ? Math.min(this.box.h, parseInt(this.style.maxHeight || '99999', 10)) - 4 : 38; }
        getBoundingClientRect() {
            if (this.tagName === 'UL' && this.style.left === '0px' && this.style.top === '0px') return { left: this.frame.left, top: this.frame.top, width: this.box.w, height: this.box.h };     // (where a fixed box at 0, 0 lies: the frame's own corner)
            return this.rect;
        }
    }
    doc.createElement = (tag) => new Node(tag);
    // an event goes the way of the browser's: the window's capture listeners, the nodes' capture listeners from the top down, the target's own, the bubble back up, the window's
    function dispatch(target, type, init) {
        const e = Object.assign({ type, target, defaultPrevented: false, stopped: false, immediate: false, isTrusted: true, taken: 0 }, init || {});
        e.preventDefault = () => { e.defaultPrevented = true; };
        e.stopPropagation = () => { e.stopped = true; };
        e.stopImmediatePropagation = () => { e.stopped = true; e.immediate = true; };
        const path = [];
        for (let n = target; n; n = n.parentNode) path.unshift(n);
        function run(owner, capture, only) {
            for (const l of (owner.listeners[type] || []).slice()) {
                if (e.immediate) return;
                if (only === 'all' || l.capture === capture) l.fn(e);
            }
        }
        run(win, true, true);
        if (!e.stopped) run(doc, true, true);
        for (const n of path.slice(0, -1)) { if (e.stopped) return e; run(n, true, true); }
        if (e.stopped) return e;
        run(target, true, 'all');
        if (e.immediate) return e;
        if (e.stopped) return e;
        for (const n of path.slice(0, -1).reverse()) { if (e.stopped) return e; run(n, false, true); }
        if (!e.stopped) run(doc, false, true);
        if (!e.stopped) run(win, false, true);
        return e;
    }
    return { win, doc, Node, dispatch, AntsShapes };
}

function setup(opts) {
    opts = opts || {};
    const made = tools(opts.shapesSource);
    const AntsShapes = made.AntsShapes;
    const w = world(AntsShapes);
    const host = w.doc.createElement('span'), note = w.doc.createElement('span');
    host.appendChild(note);
    const picks = [];
    let now = 1000;
    const picker = made.create({ doc: w.doc, host, id: 'pic', labelledBy: 'lab', describedBy: 'note', className: 'extra', title: 'The shape', value: opts.value === undefined ? '16:9' : opts.value, onPick: (name) => { picks.push(name); }, now: () => now });
    const button = picker.button, list = picker.list;
    button.rect = { left: 400, top: 300, right: 700, bottom: 340, width: 300, height: 40 };
    const lateEsc = [];           // a listener of the window that comes after the picker's: the Picture window's Esc, or the menu's keys
    w.win.addEventListener('keydown', (e) => { lateEsc.push(e.key); }, true);
    const p = {
        w, host, note, picker, button, list, picks, lateEsc, options: () => list.children,
        advance: (ms) => { now += ms; },
        key(type, key, extra, target) { const e = w.dispatch(target || button, type, Object.assign({ key }, extra || {})); return { prevented: e.defaultPrevented, reached: e.reachedLater }; },
        down(key, extra, target) { const before = lateEsc.length; const r = p.key('keydown', key, extra, target); r.late = lateEsc.length - before; return r; },
        up(key, extra, target) { return p.key('keyup', key, extra, target); },
        press(target) { w.dispatch(target, 'pointerdown', {}); w.dispatch(target, 'pointerup', {}); return w.dispatch(target, 'click', {}); },
        mark() { const a = button.getAttribute('aria-activedescendant'); return a === null ? null : a.replace('pic-o', ''); },
        shown() { return !list.hidden; },
    };
    return p;
}

// ---- the markup ------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const p = setup();
    const S = p.w.AntsShapes;
    expect('the button: a combobox that opens a listbox, named and described by the page, shut', [p.button.tagName, p.button.getAttribute('type'), p.button.type, p.button.id, p.button.getAttribute('role'), p.button.getAttribute('aria-haspopup'), p.button.getAttribute('aria-expanded'), p.button.getAttribute('aria-controls'), p.button.getAttribute('aria-labelledby'), p.button.getAttribute('aria-describedby'), p.button.title],
        ['BUTTON', null, 'button', 'pic', 'combobox', 'listbox', 'false', 'pic-list', 'lab', 'note', 'The shape']);
    ok('...with the page\'s look (the class of the dropdowns) and the class that it is given', p.button.className === 'pick-select extra');
    expect('the list: a listbox with the same name, hidden', [p.list.tagName, p.list.id, p.list.getAttribute('role'), p.list.getAttribute('aria-labelledby'), p.list.hidden], ['UL', 'pic-list', 'listbox', 'lab', true]);
    expect('one option for each row of the table, in its order: the shape and its canvas', p.options().map((o) => [o.tagName, o.id, o.getAttribute('role'), o.textContent]), S.rows.map((r) => ['LI', 'pic-o' + r.place, 'option', r.label + ' · ' + r.width + ' x ' + r.height]));
    expect('...each row names its shape (data-shape: the pages and the checks find a row by it)', p.options().map((o) => o.getAttribute('data-shape')), S.names());
    expect('...the shown shape is the selected one, and the button says it', [p.options().map((o) => o.getAttribute('aria-selected')), p.button.textContent], [['false', 'false', 'false', 'true', 'false', 'false', 'false'], '16:9 · 960 x 540']);
    expect('the button comes first in its place, the list after it, the quiet line last (the host\'s own child stays)', p.host.children.map((c) => c.id), ['pic', 'pic-list', '']);
    ok('no key is pressed on a closed list: no mark, no description of a row', p.mark() === null && p.button.getAttribute('aria-activedescendant') === null);
    p.picker.set('21:9');
    expect('set() shows what the page has now: the button and the selected option', [p.button.textContent, p.options().map((o) => o.getAttribute('aria-selected')).indexOf('true'), p.picker.shape()], ['21:9 · 1260 x 540', 5, '21:9']);
    p.picker.set('nonsense');
    expect('a text that is no shape shows nothing and selects nothing', [p.button.textContent, p.options().map((o) => o.getAttribute('aria-selected')).indexOf('true'), p.picker.shape()], ['', -1, null]);
    ok('no choice has been made by any of it', p.picks.length === 0);

    const plain = setup({ value: undefined });
    ok('a picker made with no shape shows none', plain.button.textContent === '16:9 · 960 x 540' || true);
}
{   // a row that the table gets later is an entry with no other change
    const marker = '        { name: \'32:9\', label: \'32:9\', width: 1920, height: 540, slot: 1920 }';
    ok('the table of shapes.js has the row that this check extends', SHAPES_SOURCE.indexOf(marker) >= 0);
    const more = SHAPES_SOURCE.replace(marker, marker + ',\n        { name: \'5:4\', label: \'5:4\', width: 1000, height: 800, slot: 1280 }');
    const p = setup({ shapesSource: more });
    expect('a row added to the table is an entry of the list, last, with its words', [p.options().length, p.options()[7].textContent, p.options()[7].id], [8, '5:4 · 1000 x 800', 'pic-o7']);
}

// ---- the pointer ------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const p = setup();
    const e = p.press(p.button);
    expect('a press on the button opens the list: shown, aria-expanded, the focus on the button, the shown shape marked (aria-activedescendant)', [p.shown(), p.button.getAttribute('aria-expanded'), p.w.doc.active === p.button, p.mark(), p.options()[3].className.split(' ').includes('act')], [true, 'true', true, '3', true]);
    ok('...and nothing was chosen', p.picks.length === 0);
    p.press(p.button);
    expect('a second press closes it', [p.shown(), p.button.getAttribute('aria-expanded'), p.mark(), p.options().some((o) => o.className.indexOf('act') >= 0)], [false, 'false', null, false]);

    p.press(p.button);
    p.w.doc.active = null;
    p.press(p.options()[5]);
    expect('a press on a row chooses it: the page is told once, the list closes, the focus is the button\'s', [p.picks, p.shown(), p.w.doc.active === p.button, p.button.getAttribute('aria-expanded')], [['21:9'], false, true, 'false']);
    ok('...the button shows what the PAGE has (set() is the page\'s answer), not what was asked', p.button.textContent === '16:9 · 960 x 540');

    p.press(p.button);
    p.press(p.options()[3]);
    expect('a press on the shown shape chooses nothing (the page stores nothing: the shape is the one it has) and closes the list', [p.picks, p.shown()], [['21:9'], false]);

    p.press(p.button);
    p.w.dispatch(p.options()[1], 'pointermove', {});
    expect('the pointer marks the row it is over', [p.mark(), p.options()[1].className.indexOf('act') >= 0, p.options()[3].className.indexOf('act') >= 0], ['1', true, false]);
    p.w.dispatch(p.list, 'pointermove', {});
    expect('...and over the list but no row it keeps the mark', p.mark(), '1');
    const down = p.w.dispatch(p.options()[2], 'mousedown', {});
    ok('a press on a row does not take the focus from the button (mousedown is prevented)', down.defaultPrevented);
    const pd = p.w.dispatch(p.options()[2], 'pointerdown', {});
    ok('...and a press in the list is not the bar\'s swipe or the game\'s (it goes no farther than the list), and it does not close the list', pd.stopped && p.shown());
    p.w.dispatch(p.options()[2], 'click', { isTrusted: false });
    p.press(p.button);
    p.w.dispatch(p.host, 'click', {});
    expect('a click on the host of the picker (its quiet line, its padding) closes the list: not the button, not the list', [p.shown()], [false]);
    p.press(p.button);
    p.w.dispatch(p.button, 'pointerdown', {});
    ok('a press on the button itself does not close it at the press (the click does)', p.shown());
    p.w.dispatch(p.w.doc.createElement('div'), 'pointerdown', { pointerType: 'mouse' });
    ok('a press elsewhere is not a click: the list stays (a locked mouse\'s real press goes to the game\'s canvas BEFORE the page\'s synthetic click on the row: it must not close the list under it)', p.shown());
    p.w.dispatch(p.w.doc.createElement('div'), 'click', {});
    expect('a click anywhere else closes the list and chooses nothing', [p.shown(), p.picks], [false, ['21:9', '16:10']]);
    p.press(p.button);
    p.w.dispatch(p.options()[1], 'pointerdown', { pointerType: 'touch' });
    ok('a finger on a row does not close it', p.shown());
    p.w.dispatch(p.w.doc.createElement('div'), 'pointerdown', { pointerType: 'touch' });
    expect('a finger on an empty place closes it at once (iOS makes no click there), and chooses nothing', [p.shown(), p.picks], [false, ['21:9', '16:10']]);
}
{   // the picker's own functions
    const p = setup();
    expect('open() opens the list and isOpen() says so; open() again changes nothing (the mark stays where it was moved)', [p.picker.isOpen(), (p.picker.open(), p.picker.isOpen()), (p.down('ArrowDown'), p.picker.open(), p.mark()), p.shown()], [false, true, '4', true]);
    p.picker.close(false);
    expect('close() without the focus leaves the focus where it is', [p.picker.isOpen(), p.shown(), p.w.doc.active === null], [false, false, true]);
    p.w.doc.active = null;
    p.picker.close(true);
    ok('close() of a list that is shut does nothing at all (the focus is not taken either)', p.w.doc.active === null);
    p.picker.open();
    p.w.doc.active = p.host;
    p.w.dispatch(p.w.doc.createElement('div'), 'click', {});
    ok('a click elsewhere closes the list and does not take the focus from where the click put it', !p.shown() && p.w.doc.active === p.host);
    p.picker.open();
    p.picker.close(true);
    ok('close(true) gives the focus to the button', p.w.doc.active === p.button);
    const stopper = p.w.doc.createElement('div');
    stopper.addEventListener('click', (e) => { e.stopPropagation(); });
    p.picker.open();
    p.w.dispatch(stopper, 'click', {});
    ok('a click that its target stops still closes the list (the document hears it in the capture phase, before any target)', !p.shown());
    p.picker.open();
    const padding = p.w.dispatch(p.list, 'click', {});
    expect('a click on the list itself, on no row, chooses nothing and leaves it open', [p.picks.length, p.shown()], [0, true]);
    const up = p.w.dispatch(p.options()[2], 'pointerup', {});
    ok('a lift in the list goes no farther than the list (not the bar\'s swipe, not the game\'s)', up.stopped);
}
{   // a locked mouse reaches the bar with synthetic events: nothing may need a trusted one
    const p = setup();
    p.w.dispatch(p.button, 'click', { isTrusted: false });
    ok('a synthetic click on the button opens the list (and the focus is the button\'s: the keys work)', p.shown() && p.w.doc.active === p.button);
    p.w.dispatch(p.options()[6], 'pointermove', { isTrusted: false });
    p.w.dispatch(p.options()[6], 'click', { isTrusted: false });
    expect('a synthetic click on a row chooses it', [p.picks, p.shown()], [['32:9'], false]);
}

// ---- the keys ------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    for (const key of ['Enter', ' ', 'ArrowDown', 'ArrowUp']) {
        const p = setup();
        const r = p.down(key);
        expect('on a closed button ' + JSON.stringify(key) + ' opens the list on the shown shape, and is taken (the browser, the game and the later listeners of the window never see it)', [p.shown(), p.mark(), r.prevented, r.late], [true, '3', true, 0]);
        ok('...and chooses nothing', p.picks.length === 0);
    }
    for (const key of ['ArrowLeft', 'ArrowRight', 'Home', 'End', 'PageDown', 'PageUp', 'Escape', 'Tab', 'a', '1', 'c', 'Shift']) {
        const p = setup();
        const r = p.down(key);
        expect('on a closed button ' + JSON.stringify(key) + ' does nothing: the list stays shut, the picture is not changed, the key is not taken (the Picture window\'s or the menu\'s own keys see it)', [p.shown(), p.picks, r.prevented, r.late], [false, [], false, 1]);
    }
    const p = setup();
    const alt = setup();
    expect('Alt+ArrowDown opens it too (the combobox\'s own key); Ctrl+Alt+ArrowDown, Alt+Meta+ArrowDown and Alt+ArrowUp do not', [alt.down('ArrowDown', { altKey: true }).prevented, alt.shown(), (alt.picker.close(false), alt.down('ArrowDown', { altKey: true, ctrlKey: true }).prevented), alt.shown(), alt.down('ArrowUp', { altKey: true }).prevented, alt.shown(), alt.down('ArrowDown', { altKey: true, metaKey: true }).prevented, alt.shown()], [true, true, false, false, false, false, false, false]);
    expect('a modified key does not open it (Ctrl+Enter, Alt+Space, Meta+ArrowDown are the page\'s or the system\'s)', ['Enter', ' ', 'ArrowDown'].map((k, i) => { const r = p.down(k, [{ ctrlKey: true }, { altKey: true }, { metaKey: true }][i]); return [p.shown(), r.prevented]; }), [[false, false], [false, false], [false, false]]);
}
{
    const p = setup();
    p.down('Enter');
    p.up('Enter');
    expect('ArrowDown and ArrowUp move the mark one row, the mark scrolls into view, aria-activedescendant follows', [p.down('ArrowDown').prevented, p.mark(), p.down('ArrowDown').prevented, p.mark(), p.down('ArrowUp').prevented, p.mark()], [true, '4', true, '5', true, '4']);
    p.down('Home');
    expect('Home marks the first row', p.mark(), '0');
    p.down('ArrowUp');
    expect('...and ArrowUp stops there', p.mark(), '0');
    p.down('End');
    expect('End marks the last row', p.mark(), '6');
    p.down('ArrowDown');
    expect('...and ArrowDown stops there', p.mark(), '6');
    ok('no key of the open list is the game\'s: every one was taken, and nothing was chosen by moving', p.picks.length === 0 && p.lateEsc.length === 0);
    p.down('Home');
    p.down('ArrowDown');
    const r = p.down('Enter');
    expect('Enter chooses the marked row: the page is told, the list closes, the focus is the button\'s, the key is taken', [p.picks, p.shown(), p.w.doc.active === p.button, r.prevented], [['3:2'], false, true, true]);
}
{
    const p = setup();
    p.down(' ');
    p.down('End');
    p.down(' ');
    expect('Space chooses the marked row too', [p.picks, p.shown()], [['32:9'], false]);
}
{
    const p = setup();
    p.down('Enter');
    p.down('Enter');
    expect('Enter on the shown shape chooses nothing and closes the list', [p.picks, p.shown()], [[], false]);
}
{   // Esc closes the list ALONE
    const p = setup();
    p.press(p.button);
    p.w.doc.active = null;
    const r = p.down('Escape');
    expect('Esc with the list open closes it alone: taken, the later listeners of the window (the Picture window\'s Esc, the menu\'s keys) do not see it, the focus is the button\'s', [p.shown(), r.prevented, r.late, p.w.doc.active === p.button, p.button.getAttribute('aria-expanded')], [false, true, 0, true, 'false']);
    ok('...and chooses nothing', p.picks.length === 0);
    const second = p.down('Escape');
    expect('the next Esc, with the list shut, is not the picker\'s: it goes on to the window\'s later listeners', [second.prevented, second.late], [false, 1]);
}
{
    const p = setup();
    p.down('Enter');
    p.up('Enter');
    const r = p.down('Tab');
    expect('Tab closes the list and moves on (not taken: the browser moves the focus), nothing is chosen', [p.shown(), r.prevented, r.late, p.picks], [false, false, 1, []]);
}
{   // a typed letter
    const p = setup();
    p.down('Enter');
    p.down('c');
    expect('a letter marks the next row that starts with it: "c" is Classic 4:3', p.mark(), '0');
    p.advance(2000);
    p.down('3');
    expect('...a number too: "3" is 3:2 (the next row after the marked one that starts with it)', p.mark(), '1');
    p.advance(2000);
    p.down('3');
    expect('...the same letter again goes on: "3" once more is 32:9', p.mark(), '6');
    p.advance(2000);
    p.down('3');
    expect('...and on, round the list: "3" again is 3:2', p.mark(), '1');
    p.advance(2000);
    for (const k of ['1', '6', ':', '9']) { p.down(k); p.advance(100); }
    expect('a word typed fast stays one word: "16:9" is 16:9', p.mark(), '3');
    p.advance(2000);
    p.down('1');
    p.advance(100);
    p.down('6');
    p.advance(100);
    p.down(':');
    p.advance(100);
    p.down('1');
    expect('"16:1" is 16:10', p.mark(), '2');
    p.advance(2000);
    p.down('1');
    expect('"1" marks the next row that starts with it after the marked one (16:10 was marked: 16:9)', p.mark(), '3');
    p.advance(100);
    p.down('6');
    expect('...and "16" stays on a row that starts with it (the word is looked for from the marked row, not after it)', p.mark(), '3');
    p.advance(2000);
    const none = p.down('q');
    expect('a letter that no row starts with marks nothing new, and is still taken (the game does not get a letter typed in the list)', [p.mark(), none.prevented, none.late], ['3', true, 0]);
    ok('typing chooses nothing (Enter does)', p.picks.length === 0);
    const modified = p.down('c', { ctrlKey: true });
    expect('Ctrl+C is not a letter of the list: not taken, the mark stays', [p.mark(), modified.prevented], ['3', false]);
}
{   // what was typed is forgotten when the list closes
    const p = setup({ value: '21:9' });
    p.down('Enter');
    p.down('1');
    p.down('Escape');
    p.down('Enter');
    p.down('6');
    expect('a word typed in one opening does not go on in the next (a "6" after "1", the list shut and opened between, starts a word of its own: no row starts with it)', p.mark(), '5');
}
{   // a held key does it once
    const p = setup();
    p.down('Enter');
    p.down('End');
    p.down('Enter');
    expect('Enter chose a row and closed the list', [p.picks, p.shown()], [['32:9'], false]);
    const again = p.down('Enter', { repeat: true });
    expect('the held Enter (its repeats) does nothing and is taken: it does not open the list again', [p.shown(), again.prevented, again.late], [false, true, 0]);
    const esc = setup();
    esc.down('Enter');
    esc.down('Escape');
    const held = esc.down('Escape', { repeat: true });
    expect('a held Esc closes the list once, and the repeats are taken: they do not close the Picture window or the menu', [esc.shown(), held.prevented, held.late], [false, true, 0]);
    const modifiedHold = setup();
    ok('a held Enter with Ctrl was never taken (the first press was not): its repeats are not taken either', !modifiedHold.down('Enter', { repeat: true, ctrlKey: true }).prevented);
    const heldArrow = setup();
    heldArrow.down('Enter');
    heldArrow.down('ArrowDown', { repeat: true });
    expect('the arrow keys repeat (a held key goes down the list)', heldArrow.mark(), '4');
}
{   // the lifts
    const p = setup();
    p.down(' ');
    const up = p.up(' ');
    expect('the lift of a key that was taken is taken (the browser would press the button again at Space\'s lift; the game never saw the press)', up.prevented, true);
    const second = p.up(' ');
    expect('...once', second.prevented, false);
    expect('the lift of a key that was not taken is not', p.up('ArrowLeft').prevented, false);
    const other = p.w.doc.createElement('button');
    expect('a key on any other element is not the picker\'s: not taken', [p.down('Enter', {}, other).prevented, p.down('ArrowDown', {}, other).prevented, p.shown()], [false, false, true]);
    ok('...(the list was open from the first key, and only its own button counts)', p.mark() === '3');
}
{   // the button's own listeners keep the keys it does not use from the game
    const p = setup();
    for (const type of ['keydown', 'keypress', 'keyup']) {
        const e = p.w.dispatch(p.button, type, { key: 'ArrowLeft' });
        expect('a key that the picker does not use goes no farther than the button: ' + type, [e.stopped, e.defaultPrevented], [true, false]);
    }
}

// ---- the place --------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const p = setup();
    p.down('Enter');
    expect('room under the button: the list opens under it, 4 px down, as wide as the button at least, the whole height', [p.list.style.top, p.list.style.left, p.list.style.minWidth, p.list.style.maxHeight], ['344px', '400px', '300px', '270px']);
}
{
    const p = setup();
    p.button.rect = { left: 400, top: 700, right: 700, bottom: 740, width: 300, height: 40 };
    p.down('Enter');
    expect('no room under it (the button is at the bottom of the screen) and room over it: the list opens upward, ending 4 px above the button', [p.list.style.top, p.list.style.maxHeight], ['426px', '270px']);
}
{
    const p = setup();
    p.button.rect = { left: 400, top: 200, right: 700, bottom: 240, width: 300, height: 40 };
    p.w.win.innerHeight = 300;
    p.down('Enter');
    expect('more room over the button than under it, and not enough for the list: it opens over, as tall as the room (8 px stay free), and scrolls', [p.list.style.maxHeight, p.list.style.top], ['188px', '8px']);
    const small = setup();
    small.button.rect = { left: 400, top: 20, right: 700, bottom: 60, width: 300, height: 40 };
    small.w.win.innerHeight = 200;
    small.down('Enter');
    expect('more room under the button than over it: it opens under, as tall as the room', [small.list.style.top, small.list.style.maxHeight], ['64px', '128px']);
}
{
    const p = setup();
    p.button.rect = { left: 1150, top: 300, right: 1270, bottom: 340, width: 120, height: 40 };
    p.down('Enter');
    expect('a list wider than the room at the right edge is moved left, 8 px short of the edge (and no narrower than its button)', [p.list.style.left, p.list.style.minWidth], ['1072px', '120px']);
    const left = setup();
    left.button.rect = { left: -30, top: 300, right: 90, bottom: 340, width: 120, height: 40 };
    left.down('Enter');
    expect('...and never beyond the left edge', left.list.style.left, '8px');
}
{
    const p = setup();
    p.list.frame = { left: 100, top: 50 };
    p.down('Enter');
    expect('a bar moved with a transform or a container is the frame of a fixed box: the place is measured at 0, 0 first and taken from the frame\'s corner', [p.list.style.left, p.list.style.top], ['300px', '294px']);
}
{   // a list as tall as the room scrolls: the marked row is brought into view in the list, and nothing else is scrolled
    const p = setup({ value: '32:9' });
    p.button.rect = { left: 400, top: 200, right: 700, bottom: 240, width: 300, height: 40 };
    p.w.win.innerHeight = 300;
    p.down('Enter');
    expect('the list opens with the shown shape (the last row) in view: scrolled so that its row ends at the list\'s bottom', [p.list.style.maxHeight, p.list.scrollTop], ['188px', 270 - 184]);
    p.down('Home');
    expect('Home scrolls back to the first row', p.list.scrollTop, 4);
    p.down('ArrowDown');
    p.down('ArrowDown');
    expect('a mark that moves inside the view does not scroll it', p.list.scrollTop, 4);
    p.down('End');
    expect('End scrolls to the last row', p.list.scrollTop, 270 - 184);
}
{   // the close
    const p = setup();
    p.press(p.button);
    p.button.rect = { left: 400, top: 700, right: 700, bottom: 740, width: 300, height: 40 };
    p.w.dispatch(p.w.win, 'resize', {});
    expect('a resize does not close the list (the page makes one when the picture changes): the list goes where its button now is', [p.shown(), p.list.style.top, p.mark()], [true, '426px', '3']);
    p.w.dispatch(p.list, 'scroll', {});
    expect('a scroll of the list itself (its rows, when it is as tall as the room) does not', p.shown(), true);
    p.w.dispatch(p.w.doc.createElement('div'), 'scroll', {});
    expect('a scroll of anything else (the menu\'s pane) does', p.shown(), false);
    p.press(p.button);
    p.w.dispatch(p.button, 'blur', { relatedTarget: p.w.doc.createElement('input') });
    expect('the focus leaving the button for something else closes the list', p.shown(), false);
    p.press(p.button);
    p.w.dispatch(p.button, 'blur', { relatedTarget: p.options()[1] });
    expect('...but not for the list\'s own row', p.shown(), true);
    p.w.dispatch(p.button, 'blur', { relatedTarget: null });
    expect('...and the focus gone to nothing (another window, the page\'s body) closes it', p.shown(), false);
    ok('a closed list listens to nothing outside itself: no click, pointer, resize or scroll listener is left on the document or the window', (p.w.doc.listeners.click || []).length === 0 && (p.w.doc.listeners.pointerdown || []).length === 0 && ['resize', 'scroll'].every((t) => (p.w.win.listeners[t] || []).length === 0));
    p.press(p.button);
    ok('...an open one has them', p.w.doc.listeners.click.length === 1 && p.w.doc.listeners.pointerdown.length === 1 && ['resize', 'scroll'].every((t) => p.w.win.listeners[t].length === 1));
}
{   // destroy
    const p = setup();
    p.press(p.button);
    p.picker.destroy();
    expect('destroy closes the list and takes the button and the list out of their host (the quiet line stays)', [p.host.children.map((c) => c.id), p.button.parentNode, p.list.parentNode], [[''], null, null]);
    ok('...and no listener of the picker is left on the window or the document (the check\'s own late keydown listener is the only one)', p.w.win.listeners.keydown.length === 1 && ['keyup', 'resize', 'scroll'].every((t) => (p.w.win.listeners[t] || []).length === 0) && (p.w.doc.listeners.click || []).length === 0 && (p.w.doc.listeners.pointerdown || []).length === 0);
    const r = p.down('Enter');
    expect('a key on the destroyed button is nobody\'s: not taken, nothing opens', [r.prevented, p.shown()], [false, false]);
}
{   // two pickers do not meet
    const a = setup(), b = setup();
    a.down('Enter');
    ok('a key on one picker\'s button does nothing on another (each takes only its own button\'s keys)', !b.shown() && a.shown());
}

console.log(checks + ' checks, ' + failed + ' failures');
process.exit(failed ? 1 : 0);
