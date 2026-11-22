// Runs the Room's two small overlays in node with a small fake DOM: the seat menu (web/front/seat_menu.js: the choice of a color, docs/PLAY_IN_BROWSER.md "Your room") and the Rules sheet
// (web/front/room_sheet.js: the card that holds the room's rules). Neither knows the room, so each is run on its own:
//   1. the menu: the five choices and their words ("Nobody" of the plan is "Closed"), what open() shows (the chosen one, the ones that are off, the focus, the place under the button), what a choice does
//      (a pick, the same choice again, a choice that is off), every way of closing (Escape gives the focus back, Tab, a press elsewhere, a scroll that moves the button, a resize, a second menu) and the keys;
//   2. the sheet: open and close, the page behind it held while it is open, the focus (into the card, back to the button), Escape, a press on the dark ground that closes it and one that starts inside the card
//      that does not, a button that cannot be pressed (no room yet), and a close by the page that leaves the focus where it is.
// tests/scripts/test_web_room_menus.py runs this with node (the quick tier). usage: node web_room_menus_check.js web/front        (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');
const FRONT = path.resolve(process.argv[2] || path.join(__dirname, '..', '..', 'web', 'front'));
const Menu = require(path.join(FRONT, 'seat_menu.js'));
const Sheet = require(path.join(FRONT, 'room_sheet.js'));

let failed = 0, checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) { console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); failed++; }
}

class El {
    constructor(doc, tag) {
        this.doc = doc; this.tagName = tag; this.children = []; this.attrs = {}; this.listeners = {}; this.hidden = false; this.disabled = false; this.style = {}; this.parent = null;
        this.classes = new Set(); this._text = ''; this.rect = { left: 0, top: 0, right: 0, bottom: 0 }; this.offsetWidth = 0; this.offsetHeight = 0; this.focusArgs = [];
        const self = this;
        this.classList = { add: (c) => self.classes.add(c), remove: (c) => self.classes.delete(c), contains: (c) => self.classes.has(c), toggle: (c, on) => { if (on === undefined ? !self.classes.has(c) : on) self.classes.add(c); else self.classes.delete(c); } };
    }
    set className(v) { this.classes = new Set(String(v).split(/\s+/).filter(Boolean)); }
    get className() { return Array.from(this.classes).join(' '); }
    set textContent(t) { this._text = String(t); this.children = []; }
    get textContent() { return this._text + this.children.map((c) => c.textContent).join(''); }
    appendChild(c) { c.parent = this; this.children.push(c); return c; }
    setAttribute(k, v) { this.attrs[k] = String(v); }
    getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
    removeAttribute(k) { delete this.attrs[k]; }
    addEventListener(t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); }
    contains(n) { for (let x = n; x; x = x.parent) if (x === this) return true; return false; }
    focus(o) { this.focusArgs.push(o || null); this.doc.activeElement = this; }
    getBoundingClientRect() { return this.rect; }
    fire(t, e) { const ev = Object.assign({ target: this, preventDefault() { ev.prevented = true; } }, e || {}); (this.listeners[t] || []).forEach((f) => f(ev)); return ev; }
    all(test, out) { out = out || []; this.children.forEach((c) => { if (test(c)) out.push(c); c.all(test, out); }); return out; }
}
function makeDoc() {
    const doc = { activeElement: null, listeners: {} };
    doc.createElement = (t) => new El(doc, t);
    doc.body = new El(doc, 'body');
    doc.addEventListener = (t, f) => { (doc.listeners[t] = doc.listeners[t] || []).push(f); };
    doc.fire = (t, target) => { (doc.listeners[t] || []).forEach((f) => f({ target })); };
    return doc;
}
function makeWin(w, h) {
    const win = { innerWidth: w, innerHeight: h, listeners: {} };
    win.addEventListener = (t, f) => { (win.listeners[t] = win.listeners[t] || []).push(f); };
    win.fire = (t) => { (win.listeners[t] || []).forEach((f) => f({})); };
    return win;
}
const place = (el, left, top, w, h) => { el.rect = { left, top, right: left + w, bottom: top + h }; };

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The seat menu
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function menuRig(o) {
    o = o || {};
    const doc = makeDoc(), win = makeWin(o.vw || 1000, o.vh || 800);
    const menu = Menu.create({ doc, win });
    const anchor = doc.createElement('button');
    doc.body.appendChild(anchor);
    place(anchor, o.left === undefined ? 100 : o.left, o.top === undefined ? 100 : o.top, 120, 30);
    menu.box.offsetWidth = 150; menu.box.offsetHeight = 200;
    const log = { picked: [], blocked: [], closed: 0 };
    const spec = (extra) => Object.assign({ anchor, value: 'open', off: [], label: 'Blue is', onPick: (w) => log.picked.push(w), onBlocked: (w) => log.blocked.push(w), onClose: () => { log.closed++; } }, extra || {});
    const items = () => menu.box.children;
    const item = (word) => items().filter((i) => i.attrs['data-word'] === word)[0];
    const key = (k) => menu.box.fire('keydown', { key: k });
    return { doc, win, menu, anchor, log, spec, items, item, key };
}

expect('the plan\'s five words are the menu\'s five choices, in this order, and "Nobody" is shown as "Closed"', [Menu.CHOICES.map((c) => c.word), Menu.CHOICES.map((c) => c.name)],
    [['open', 'easy', 'medium', 'hard', 'nobody'], ['Open', 'Easy bot', 'Medium bot', 'Hard bot', 'Closed']]);
expect('a word is named as the button says it; a word that the menu does not know is its own name', [Menu.nameOf('nobody'), Menu.nameOf('hard'), Menu.nameOf('open'), Menu.nameOf('x')], ['Closed', 'Hard bot', 'Open', 'x']);
{
    const t = menuRig();
    expect('the menu is in the page, put away, a menu of five items that each can be a choice', [t.doc.body.children.indexOf(t.menu.box) >= 0, t.menu.box.hidden, t.menu.box.attrs.role, t.items().map((i) => i.attrs.role)],
        [true, true, 'menu', Array(5).fill('menuitemradio')]);
    expect('a closed color says what it is: its item has a line that it stays empty (only that one has a line)', t.items().map((i) => i.all((n) => n.tagName === 'small').map((n) => n.textContent).join('')), ['', '', '', '', 'stays empty']);
    expect('closed, the menu is not open and a close does nothing', [t.menu.isOpen(), (t.menu.close(), t.log.closed)], [false, 0]);
}
{
    const t = menuRig({ left: 100, top: 100 });
    t.menu.open(t.spec({ value: 'medium' }));
    expect('open: the menu shows, the button says it is expanded, the label names the color', [t.menu.isOpen(), t.menu.box.hidden, t.anchor.attrs['aria-expanded'], t.menu.box.attrs['aria-label']], [true, false, 'true', 'Blue is']);
    expect('only the chosen item is checked and marked, and the focus is in the menu on it, without a scroll of the page',
        [t.items().map((i) => i.attrs['aria-checked']), t.items().map((i) => i.classList.contains('sel')), t.doc.activeElement === t.item('medium'), t.item('medium').focusArgs[0]],
        [['false', 'false', 'true', 'false', 'false'], [false, false, true, false, false], true, { preventScroll: true }]);
    expect('the menu is under the button: its left edge at the button\'s, 6 px below its bottom', [t.menu.box.style.left, t.menu.box.style.top], ['100px', '136px']);
    t.menu.close();
    expect('a close puts it away, tells the page once, and the button is not expanded', [t.menu.box.hidden, t.menu.isOpen(), t.log.closed, t.anchor.attrs['aria-expanded']], [true, false, 1, 'false']);
    expect('a close by the page does not take the focus for the button', t.doc.activeElement === t.anchor, false);
}
{
    const t = menuRig();
    t.menu.open(t.spec({ value: 'open' }));
    t.item('hard').fire('click');
    expect('a choice is told to the page once, the menu is put away and the focus is back on the button', [t.log.picked, t.log.blocked, t.menu.isOpen(), t.doc.activeElement === t.anchor, t.anchor.attrs['aria-expanded']], [['hard'], [], false, true, 'false']);
    t.menu.open(t.spec({ value: 'hard' }));
    t.item('hard').fire('click');
    expect('the choice that is already the color\'s sends nothing (the menu only closes)', [t.log.picked, t.menu.isOpen()], [['hard'], false]);
    t.menu.open(t.spec({ value: 'nobody' }));
    t.item('open').fire('click');
    t.menu.open(t.spec({ value: 'open' }));
    t.item('nobody').fire('click');
    expect('every other word is told as the plan says it', t.log.picked, ['hard', 'open', 'nobody']);
}
{
    const t = menuRig();
    t.menu.open(t.spec({ value: 'open', off: ['easy', 'medium', 'hard'] }));
    expect('the choices that are off say so, and the others do not', [t.items().map((i) => i.attrs['aria-disabled'] || null), t.items().map((i) => i.classList.contains('off'))],
        [[null, 'true', 'true', 'true', null], [false, true, true, true, false]]);
    t.item('medium').fire('click');
    expect('a choice that is off is not a pick: the page is told that it was pressed (so it can say why), and the menu is put away', [t.log.picked, t.log.blocked, t.menu.isOpen()], [[], ['medium'], false]);
    t.menu.open(t.spec({ value: 'open', off: [] }));
    expect('the next time that nothing is off, nothing says so', t.items().map((i) => i.attrs['aria-disabled'] || null), [null, null, null, null, null]);
    t.menu.close();
}
{
    const t = menuRig();
    t.menu.open(t.spec({ value: 'open' }));
    const at = () => t.items().findIndex((i) => i === t.doc.activeElement);
    t.key('ArrowDown'); t.key('ArrowDown');
    expect('Down moves the focus along the items', at(), 2);
    t.key('End');
    expect('End goes to the last', at(), 4);
    t.key('ArrowDown');
    expect('Down does not run off the end', at(), 4);
    t.key('Home');
    expect('Home goes to the first', at(), 0);
    t.key('ArrowUp');
    expect('Up does not run off the beginning', at(), 0);
    t.key('ArrowDown');
    const keyEvent = t.key('ArrowDown');
    expect('the arrows are the menu\'s own (the page behind does not scroll)', keyEvent.prevented, true);
    t.key('Enter');
    expect('Enter chooses the focused item', [t.log.picked, t.menu.isOpen()], [['medium'], false]);
    t.menu.open(t.spec({ value: 'open' }));
    t.key('ArrowDown');
    t.key(' ');
    expect('Space chooses it too', t.log.picked, ['medium', 'easy']);
    t.menu.open(t.spec({ value: 'open' }));
    t.key('End');
    t.key('Escape');
    expect('Escape picks nothing, puts the menu away and gives the focus back to the button', [t.log.picked, t.menu.isOpen(), t.doc.activeElement === t.anchor], [['medium', 'easy'], false, true]);
    t.menu.open(t.spec({ value: 'open' }));
    t.doc.activeElement = null;
    t.key('Tab');
    expect('Tab puts the menu away and lets the focus go on from where it is', [t.menu.isOpen(), t.doc.activeElement === t.anchor], [false, false]);
    t.key('Enter');
    expect('a key after the menu is away does nothing', [t.log.picked, t.log.blocked], [['medium', 'easy'], []]);
}
{
    const t = menuRig();
    const other = t.doc.createElement('div');
    t.doc.body.appendChild(other);
    t.menu.open(t.spec());
    t.doc.fire('pointerdown', t.item('easy'));
    expect('a press inside the menu does not close it', t.menu.isOpen(), true);
    t.doc.fire('pointerdown', t.anchor);
    expect('a press on the button does not close it either (the button\'s own press does: it would open it again)', t.menu.isOpen(), true);
    t.doc.fire('pointerdown', other);
    expect('a press anywhere else puts it away, and does not steal the focus', [t.menu.isOpen(), t.log.closed, t.doc.activeElement === t.anchor], [false, 1, false]);
    t.doc.fire('pointerdown', other);
    expect('a press while it is away does nothing', t.log.closed, 1);
}
{
    const t = menuRig();
    t.menu.open(t.spec());
    t.win.fire('scroll');
    expect('a scroll that leaves the button where it was keeps the menu (a phone scrolls a little when a menu takes the focus)', t.menu.isOpen(), true);
    place(t.anchor, 100, 60, 120, 30);
    t.win.fire('scroll');
    expect('a scroll that moves the button puts the menu away', t.menu.isOpen(), false);
    place(t.anchor, 100, 100, 120, 30);
    t.menu.open(t.spec());
    t.win.fire('resize');
    expect('a resize puts it away', t.menu.isOpen(), false);
    t.win.fire('scroll');
    expect('a scroll while it is away does nothing', t.log.closed, 2);
}
{
    const t = menuRig({ left: 900, top: 100 });
    t.menu.open(t.spec());
    expect('a menu that would run off the right of the screen moves in, 8 px from the edge', t.menu.box.style.left, '842px');
    t.menu.close();
    place(t.anchor, 100, 700, 120, 30);
    t.menu.open(t.spec());
    expect('a menu that would run off the bottom opens above its button instead, 6 px over it', t.menu.box.style.top, '494px');
    t.menu.close();
    place(t.anchor, 100, 100, 120, 30);
    t.menu.open(t.spec());
    expect('with room it is under the button again', t.menu.box.style.top, '136px');
    t.menu.close();
}
{
    const t = menuRig();
    const second = t.doc.createElement('button');
    t.doc.body.appendChild(second);
    place(second, 300, 100, 120, 30);
    t.menu.open(t.spec({ value: 'open' }));
    t.menu.open(t.spec({ anchor: second, value: 'easy', label: 'Red is' }));
    expect('a second menu takes the place of the first: one open, the first button is not expanded, the first was told, the second is expanded and named', [t.menu.isOpen(), t.anchor.attrs['aria-expanded'], t.log.closed, second.attrs['aria-expanded'], t.menu.box.attrs['aria-label']], [true, 'false', 1, 'true', 'Red is']);
    expect('the second shows its own value', t.items().map((i) => i.attrs['aria-checked']), ['false', 'true', 'false', 'false', 'false']);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. The Rules sheet
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function sheetRig() {
    const doc = makeDoc();
    const back = doc.createElement('div'), box = doc.createElement('div'), opener = doc.createElement('button'), x = doc.createElement('button'), done = doc.createElement('button'), inside = doc.createElement('input');
    back.hidden = true;
    back.appendChild(box); box.appendChild(x); box.appendChild(done); box.appendChild(inside);
    doc.body.appendChild(opener); doc.body.appendChild(back);
    const held = [];
    const sheet = Sheet.create({ doc, back, box, opener, closers: [x, done], hold: (on) => held.push(on) });
    // a press and release as the browser sends them: pointerdown, pointerup, click
    const tap = (downOn, upOn, clickOn) => { back.fire('pointerdown', { target: downOn }); back.fire('pointerup', { target: upOn }); back.fire('click', { target: clickOn === undefined ? upOn : clickOn }); };
    return { doc, back, box, opener, x, done, inside, held, sheet, tap };
}
{
    const t = sheetRig();
    expect('before it is opened the sheet is put away, the page behind is not held, and the button is not expanded', [t.back.hidden, t.sheet.isOpen(), t.held, t.opener.attrs['aria-expanded'] || null], [true, false, [], null]);
    t.opener.fire('click');
    expect('a press on the button opens it: the card shows, the button says it is expanded, the page behind is held and the focus is in the card', [t.sheet.isOpen(), t.back.hidden, t.opener.attrs['aria-expanded'], t.held, t.doc.activeElement === t.box], [true, false, 'true', [true], true]);
    t.opener.fire('click'); t.sheet.open();
    expect('opened twice it holds the page once', t.held, [true]);
    t.done.fire('click');
    expect('Done closes it: put away, the page behind is let go, the button is not expanded and has the focus again', [t.sheet.isOpen(), t.back.hidden, t.held, t.opener.attrs['aria-expanded'], t.doc.activeElement === t.opener], [false, true, [true, false], 'false', true]);
    t.done.fire('click');
    expect('a close of a card that is away does nothing', t.held, [true, false]);
    t.opener.fire('click');
    t.x.fire('click');
    expect('the cross closes it too', [t.sheet.isOpen(), t.back.hidden, t.held], [false, true, [true, false, true, false]]);
}
{
    const t = sheetRig();
    t.opener.disabled = true;
    t.opener.fire('click'); t.sheet.open();
    expect('a button that cannot be pressed (the room is not there yet) opens nothing, also not from the page', [t.sheet.isOpen(), t.back.hidden, t.held], [false, true, []]);
    t.opener.disabled = false;
    t.sheet.open();
    expect('it opens from the page when the button can be pressed', t.sheet.isOpen(), true);
    t.doc.activeElement = t.inside;
    t.sheet.close();
    expect('a close by the page (the room went away) puts it away and lets the page go, but leaves the focus where it is', [t.sheet.isOpen(), t.back.hidden, t.held, t.doc.activeElement === t.inside], [false, true, [true, false], true]);
    t.opener.fire('click');
    expect('it opens again afterwards', [t.sheet.isOpen(), t.back.hidden], [true, false]);
}
{
    const t = sheetRig();
    t.opener.fire('click');
    const other = t.back.fire('keydown', { key: 'Enter' });
    expect('a key that is not Escape leaves it open', [t.sheet.isOpen(), other.prevented === true], [true, false]);
    const esc = t.back.fire('keydown', { key: 'Escape' });
    expect('Escape closes it, the key is its own, and the focus is back on the button', [t.sheet.isOpen(), esc.prevented, t.doc.activeElement === t.opener], [false, true, true]);
}
{
    const t = sheetRig();
    t.opener.fire('click');
    t.tap(t.back, t.back);
    expect('a press that starts and ends on the dark ground closes it', [t.sheet.isOpen(), t.doc.activeElement === t.opener], [false, true]);
    t.opener.fire('click');
    t.tap(t.inside, t.inside, t.inside);
    expect('a press inside the card does not', t.sheet.isOpen(), true);
    t.tap(t.inside, t.back, t.back);
    expect('a drag that starts inside the card (a text selection) and ends on the dark ground does not close it', t.sheet.isOpen(), true);
    t.tap(t.back, t.inside, t.back);
    expect('nor does one that starts outside and ends inside', t.sheet.isOpen(), true);
    t.back.fire('click', { target: t.back });
    expect('a click on the dark ground that no press led to (a scroll, a synthetic one) does not', t.sheet.isOpen(), true);
    t.back.fire('pointerdown', { target: t.back }); t.back.fire('pointercancel', {}); t.back.fire('pointerup', { target: t.back }); t.back.fire('click', { target: t.back });
    expect('a press that the browser cancelled (it became a scroll) does not', t.sheet.isOpen(), true);
    t.tap(t.back, t.back);
    expect('and a press on the dark ground after all of that does', t.sheet.isOpen(), false);
}

console.log((failed ? 'FAILED ' : 'ok ') + checks + ' checks, ' + failed + ' failed');
process.exit(failed ? 1 : 0);
