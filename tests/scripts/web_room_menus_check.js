// Runs the Room's two small overlays in node with a small fake DOM: the seat menu (web/front/seat_menu.js: the choice of a color, docs/PLAY_IN_BROWSER.md "Your room") and the Rules sheet
// (web/front/room_sheet.js: the card that holds the room's rules). Neither knows the room, so each is run on its own:
//   1. the menu: the five choices and their words ("Nobody" of the plan is "Closed"), what open() shows (the chosen one, the ones that are off, the focus, the place under the button), what a choice does
//      (a pick, the same choice again, a choice that is off), every way of closing (Escape gives the focus back, Tab, a press elsewhere, a scroll that moves the button, a resize, a second menu) and the keys;
//   2. the sheet: open and close, the page behind it held while it is open, the focus (into the card, back to the button), Escape, a press on the dark ground that closes it and one that starts inside the card
//      that does not, a button that cannot be pressed (no room yet), and a close by the page that leaves the focus where it is.
//   3. a phone held upright: the tile of a color (what it shows of a person, a computer player, an open and a closed color; what is a target and what is only a picture) and the leader's sheet for a color (the rows of a person, a
//      computer player and an open color; what each press says to the page and what it does not; the question before Remove; the focus; every way of closing; a card that is drawn again while it is open).
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

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. A phone held upright: the tile of a color
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const WORDS = { on: 'Ready', off: 'Not ready' };
// a card as front/lobby_rules.js viewOf() gives it (the page's own function is run against the same tables by web_lobby_rules_check.js and web_room_tiles_check.js)
const card = (o) => Object.assign({ seat: 1, color: { id: 'red', name: 'Red' }, kind: 'person', who: [{ text: 'Wren', cls: 'nm' }], mode: '', stat: '', statCls: 'pstat', remove: false, ask: false, grip: true, tab: '', teams: null, label: 'Red: Wren', drop: '', ready: null }, o || {});
function tileOf(c) {
    const doc = makeDoc(), li = doc.createElement('li');
    Menu.tile(doc, li, c, WORDS);
    const cls = (name) => li.all((n) => n.classes.has(name));
    const text = (name) => cls(name).map((n) => n.textContent);
    return { doc, li, cls, text, kids: li.children.map((n) => n.className) };
}
{
    const t = tileOf(card({ who: [{ text: '[ANT] Zoe', cls: 'nm' }, { text: 'You', cls: 'you' }, { text: 'Host', cls: 'htag' }], ready: { kind: 'chip', on: true }, label: 'Red: [ANT] Zoe, you, host' }));
    expect('a tile holds the ant, the words and the status, and a button over all of it (and nothing else that can be pressed)', [t.kids, t.li.all((n) => n.tagName === 'button').length], [['antbox', 'txt', 'tail', 'tap'], 1]);
    expect('the ant is the color\'s, the color\'s name and the tags are on one line and the name is on a line of its own', [t.cls('ant').map((n) => n.className), t.text('cname'), t.text('you'), t.text('htag'), t.text('nm'), t.cls('tline')[0].children.map((n) => n.className), t.cls('pname')[0].children.map((n) => n.className)],
        [['ant ant-red'], ['Red'], ['You'], ['Host'], ['[ANT] Zoe'], ['cname', 'you', 'htag'], ['nm']]);
    const tap = t.cls('tap')[0];
    expect('the button names the seat, is a dialog\'s opener and says what it is for', [tap.attrs['data-tap'], tap.attrs['data-key'], tap.attrs['aria-haspopup'], tap.attrs['aria-label'], tap.type], ['1', 'tap:1', 'dialog', 'Change Red: [ANT] Zoe, you, host', 'button']);
    expect('the leader\'s tile has an arrow, and a mark that says Ready to a screen reader and in the picture', [t.cls('tchev').length, t.cls('tmark').map((n) => n.className), t.text('sr')], [1, ['tmark on'], ['Ready']]);
}
{
    const t = tileOf(card({ grip: false, ready: { kind: 'chip', on: false } }));
    expect('a guest\'s tile is a picture: no button, no arrow; the mark that says Not ready is there', [t.cls('tap').length, t.cls('tchev').length, t.li.all((n) => n.tagName === 'button').length, t.cls('tmark').map((n) => n.className), t.text('sr')], [0, 0, 0, ['tmark no'], ['Not ready']]);
    expect('a person with no mark (a game from the desktop, a rematch that has not begun) has none', tileOf(card({ ready: null })).cls('tmark').length, 0);
    expect('a computer player\'s mark is the dim one', tileOf(card({ kind: 'bot', who: [], mode: 'hard', ready: { kind: 'chip', on: true, bot: true } })).cls('tmark').map((n) => n.className), ['tmark on bot']);
}
{
    const names = (c) => tileOf(c).text('nm');
    expect('an open color says Open, and a computer player with no name of its own says its level', [names(card({ kind: 'open', who: [], mode: 'open' })), names(card({ kind: 'bot', who: [], mode: 'medium' }))], [['Open'], ['Medium bot']]);
    expect('a computer player is shown by its own name, and a closed color says Closed', [names(card({ kind: 'bot', who: [{ text: 'Pip (bot, Hard)', cls: 'nm' }], mode: 'hard' })), names(card({ kind: 'nobody', who: [], mode: 'nobody' }))], [['Pip (bot, Hard)'], ['Closed']]);
    expect('a guest\'s tile says what the card says (Open, Closed, the bot\'s name)', [names(card({ kind: 'open', who: [{ text: 'Open', cls: '' }], grip: false })), names(card({ kind: 'nobody', who: [{ text: 'Closed', cls: '' }], grip: false }))], [['Open'], ['Closed']]);
    expect('a name that is not text-safe stays text: it is set with textContent and holds no node', tileOf(card({ who: [{ text: '<b>x</b>', cls: 'nm' }] })).cls('nm')[0].children.length, 0);
}
{
    const teams = (side) => [1, 2].map((k) => ({ side: k, pressed: k === side, disabled: false }));
    const tag = (c) => { const t = tileOf(c); return [t.text('tteam'), t.cls('tteam').map((n) => n.className)]; };
    expect('the leader\'s tile shows the team that is lit, as a tag with its number', [tag(card({ teams: teams(2) })), tag(card({ teams: teams(1) }))], [[['Team 2'], ['tteam t2']], [['Team 1'], ['tteam t1']]]);
    expect('a guest\'s tile shows the team that the card says', tag(card({ grip: false, tab: 'Team 2' })), [['Team 2'], ['tteam t2']]);
    expect('a color with no team shows no tag, and neither does one that has no buttons (fewer than three play)', [tag(card({ teams: teams(0) }))[0], tag(card())[0]], [[], []]);
    const t = tileOf(card({ teams: teams(1) }));
    expect('the tag is two parts so that a narrow phone can say T1 and a screen reader still says Team 1', t.cls('tteam')[0].children.map((n) => n.textContent), ['Team ', '1']);
}
{
    const t = tileOf(card({ stat: 'Opening the game…', statCls: 'pstat wait' }));
    expect('a line of status (START is on its way, a person is back from a match) is kept under the name', [t.text('pstat'), t.cls('pstat')[0].className], [['Opening the game…'], 'pstat wait']);
    expect('a tile with none has no such line', tileOf(card()).cls('pstat').length, 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. A phone held upright: the leader's sheet for a color
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function sheetRig2() {
    const doc = makeDoc(), held = [], log = { team: [], color: [], mode: [], blocked: [], remove: 0, yes: 0, no: 0, closed: [] };
    const sheet = Menu.createSheet({ doc, hold: (on) => held.push(on) });
    const colors = [['green', 'Green', 'person'], ['red', 'Red', 'person'], ['blue', 'Blue', 'open'], ['black', 'Black', 'open']];
    const spec = (o) => Object.assign({ seat: 1, id: 'red', name: 'Red', kind: 'person', title: 'Wren', parts: ['Ready'], teams: [{ side: 1, pressed: false, disabled: false }, { side: 2, pressed: true, disabled: false }],
        colors: colors.map((c, i) => ({ seat: i, id: c[0], name: c[1], kind: c[2], here: i === 1, label: i === 1 ? c[1] + ', where Wren is' : 'Move Wren to ' + c[1] })), mode: '', remove: 1, off: [], note: '',
        onTeam: (s) => log.team.push(s), onColor: (s) => log.color.push(s), onMode: (w) => log.mode.push(w), onBlocked: (w) => log.blocked.push(w), onRemove: () => { log.remove++; }, onRemoveYes: () => { log.yes++; }, onRemoveNo: () => { log.no++; },
        onClose: (keep) => log.closed.push(keep) }, o || {});
    const keyed = (k) => sheet.box.all((n) => n.attrs['data-key'] === k)[0];
    const all = (name) => sheet.box.all((n) => n.classes.has(name));
    const open = (o) => sheet.open(spec(o));
    const openSpec = (o) => spec(Object.assign({ seat: 2, id: 'blue', name: 'Blue', kind: 'open', title: 'Open', parts: [], teams: null, colors: null, mode: 'open', remove: 0 }, o || {}));
    const tap = (downOn, upOn, clickOn) => { sheet.back.fire('pointerdown', { target: downOn }); sheet.back.fire('pointerup', { target: upOn }); sheet.back.fire('click', { target: clickOn === undefined ? upOn : clickOn }); };
    return { doc, held, log, sheet, spec, keyed, all, open, openSpec, tap };
}
{
    const t = sheetRig2();
    expect('the sheet is in the page, put away, a dialog; nothing is held and nothing is open', [t.doc.body.children.indexOf(t.sheet.back) >= 0, t.sheet.back.hidden, t.sheet.box.attrs.role, t.sheet.box.attrs['aria-modal'], t.sheet.isOpen(), t.sheet.seat(), t.held], [true, true, 'dialog', 'true', false, -1, []]);
    t.sheet.paint(t.spec());
    expect('drawing a card that is away does nothing', [t.sheet.box.children.length, t.sheet.isOpen()], [0, false]);
    t.open();
    expect('opened: shown, the page behind is held once, the focus is in the card, and it knows the seat', [t.sheet.isOpen(), t.sheet.back.hidden, t.held, t.doc.activeElement === t.sheet.box, t.sheet.seat()], [true, false, [true], true, 1]);
    t.open();
    expect('opened again it holds the page once and draws again', t.held, [true]);
    expect('the title is the person\'s name and its words are the color, then what the page says', [t.all('ss-who')[0].all((n) => n.tagName === 'h3')[0].textContent, t.all('ss-sub')[0].textContent, t.all('ss-who')[0].all((n) => n.tagName === 'b')[0].attrs['data-c']], ['Wren', 'Red · Ready', 'red']);
    expect('a person\'s card has the team row, the color row and Remove, and no list of what the color is', [t.all('ss-team').length, t.all('ss-colors').length, t.all('ss-remove').length, t.all('ss-list').length], [1, 1, 1, 0]);
    expect('the card is labelled by its title', [t.sheet.box.attrs['aria-labelledby'], t.all('ss-who')[0].all((n) => n.id === 'ss-title').length], ['ss-title', 1]);
}
{
    const t = sheetRig2();
    t.open();
    expect('the team buttons say which is lit', [1, 2].map((k) => t.keyed('team:' + k).attrs['aria-pressed']), ['false', 'true']);
    t.keyed('team:1').fire('click'); t.keyed('team:2').fire('click');
    expect('a press on a team is told to the page with its number, and the card does not change by itself', [t.log.team, [1, 2].map((k) => t.keyed('team:' + k).attrs['aria-pressed'])], [[1, 2], ['false', 'true']]);
    t.open({ teams: [{ side: 1, pressed: false, disabled: false }, { side: 2, pressed: false, disabled: true }] });
    expect('a team that a press would refuse says so (and can still be pressed: the page says why)', [t.keyed('team:1').attrs['aria-disabled'] || null, t.keyed('team:2').attrs['aria-disabled']], [null, 'true']);
    t.keyed('team:2').fire('click');
    expect('... and the page is told', t.log.team, [1, 2, 2]);
    t.open({ teams: null });
    expect('a card with no team buttons (fewer than three play) has no row', t.all('ss-team').length, 0);
}
{
    const t = sheetRig2();
    t.open();
    expect('the color row has the four colors in order, the own color pressed, each with the words of its press', [[0, 1, 2, 3].map((k) => t.keyed('color:' + k).attrs['data-c']), [0, 1, 2, 3].map((k) => t.keyed('color:' + k).attrs['aria-pressed']), t.keyed('color:2').attrs['aria-label'], t.keyed('color:1').attrs['aria-label']],
        [['green', 'red', 'blue', 'black'], ['false', 'true', 'false', 'false'], 'Move Wren to Blue', 'Red, where Wren is']);
    expect('a color that no person holds is a dim ant, and one with a player is not', [2, 0].map((k) => t.keyed('color:' + k).all((n) => n.classes.has('ant'))[0].classes.has('ghost')), [true, false]);
    t.keyed('color:2').fire('click'); t.keyed('color:0').fire('click');
    expect('a press on another color is told to the page with the seat', t.log.color, [2, 0]);
    t.keyed('color:1').fire('click');
    expect('a press on the own color sends nothing', t.log.color, [2, 0]);
}
{
    const t = sheetRig2();
    t.open(t.openSpec());
    expect('a color that no person holds has the five choices and neither team buttons nor Color row nor Remove', [t.all('ss-item').map((n) => n.attrs['data-word']), t.all('ss-team').length, t.all('ss-colors').length, t.all('ss-remove').length], [['open', 'easy', 'medium', 'hard', 'nobody'], 0, 0, 0]);
    expect('the plan\'s words are shown as the menu\'s: Closed for nobody; Open and Closed say what they do; the ant of the title is dim', [t.all('ss-item').map((n) => n.all((x) => x.classes.has('sm-name'))[0].textContent), t.all('ss-item').map((n) => n.all((x) => x.tagName === 'small').map((x) => x.textContent).join('')), t.all('ant')[0].classes.has('ghost')],
        [['Open', 'Easy bot', 'Medium bot', 'Hard bot', 'Closed'], ['takes the next player who joins', '', '', '', 'stays empty'], true]);
    expect('the chosen one is checked and marked, a radio of a group of five', [t.all('ss-item').map((n) => n.attrs['aria-checked']), t.all('ss-item')[0].classes.has('sel'), t.all('ss-item').map((n) => n.attrs.role), t.all('ss-list')[0].attrs.role], [['true', 'false', 'false', 'false', 'false'], true, Array(5).fill('radio'), 'radiogroup']);
    expect('a color that the page gives no name (an open color, a computer player with none of its own, a closed color) is called by what the choice is, in the words of the menu', ['open', 'medium', 'nobody'].map((word) => { t.open(t.openSpec({ title: '', mode: word })); return t.all('ss-who')[0].all((n) => n.tagName === 'h3')[0].textContent; }), ['Open', 'Medium bot', 'Closed']);
    expect('... and a computer player with a name of its own is called by it', (t.open(t.openSpec({ title: 'Pip (bot, Hard)', mode: 'hard' })), t.all('ss-who')[0].all((n) => n.tagName === 'h3')[0].textContent), 'Pip (bot, Hard)');
    t.open(t.openSpec());
    t.keyed('mode:hard').fire('click'); t.keyed('mode:open').fire('click'); t.keyed('mode:nobody').fire('click');
    expect('a press on another choice is told with the plan\'s word; the one that is chosen already sends nothing', [t.log.mode, t.log.blocked], [['hard', 'nobody'], []]);
    t.open(t.openSpec({ mode: 'medium', off: ['easy', 'medium', 'hard'] }));
    expect('the choices that are off say so', t.all('ss-item').map((n) => n.attrs['aria-disabled'] || null), [null, 'true', 'true', 'true', null]);
    t.keyed('mode:easy').fire('click'); t.keyed('mode:medium').fire('click');
    expect('a press on one that is off is no pick: the page is told so that it can say why (also when it is the chosen one)', [t.log.mode, t.log.blocked], [['hard', 'nobody'], ['easy', 'medium']]);
    t.open(t.openSpec({ kind: 'bot', title: 'Pip (bot, Hard)', mode: 'hard', teams: [{ side: 1, pressed: true, disabled: false }, { side: 2, pressed: false, disabled: false }] }));
    expect('a computer player has the team row and the choices, and its ant is not dim', [t.all('ss-team').length, t.all('ss-item').length, t.all('ant')[0].classes.has('ghost')], [1, 5, false]);
}
{
    const t = sheetRig2();
    t.open();
    t.keyed('rm').fire('click');
    expect('Remove is told to the page once and nothing is removed here', [t.log.remove, t.log.yes], [1, 0]);
    t.open({ remove: 2 });
    expect('the question says who, has Remove and Keep (and no Remove button of the card), and the focus is on Keep', [t.all('ss-ask')[0].attrs['aria-label'], t.all('ss-ask')[0].all((n) => n.tagName === 'p')[0].textContent, t.all('ss-remove').length, t.all('ss-yes')[0].textContent, t.all('ss-keep')[0].textContent, t.doc.activeElement === t.keyed('no')],
        ['Remove Wren from the room?', 'Remove Wren from the room?', 0, 'Remove', 'Keep', true]);
    t.keyed('yes').fire('click'); t.keyed('no').fire('click');
    expect('each answer is told to the page', [t.log.yes, t.log.no], [1, 1]);
    t.open({ remove: 1 });
    expect('the question that is answered with Keep puts the focus back on Remove', [t.all('ss-ask').length, t.doc.activeElement === t.keyed('rm')], [0, true]);
    t.open({ remove: 2 });
    t.doc.activeElement = t.keyed('yes');
    t.open({ remove: 2 });
    expect('the question drawn again keeps the focus where it is (it only goes to Keep when it opens)', t.doc.activeElement === t.keyed('yes'), true);
    t.open({ remove: 0 });
    expect('a card with no Remove (the leader\'s own color) has none and no question', [t.all('ss-remove').length, t.all('ss-ask').length], [0, 0]);
}
{
    const t = sheetRig2();
    t.open();
    t.doc.activeElement = t.keyed('team:1');
    t.open({ teams: [{ side: 1, pressed: true, disabled: false }, { side: 2, pressed: false, disabled: false }] });
    expect('a card that is drawn again while it is open keeps the focus on the same button (a new one: the page made it from a new Room)', [t.doc.activeElement === t.keyed('team:1'), t.keyed('team:1').attrs['aria-pressed']], [true, 'true']);
    const before = t.keyed('team:1'), later = [];
    t.open({ teams: [{ side: 1, pressed: true, disabled: false }, { side: 2, pressed: false, disabled: false }], onTeam: (side) => later.push(side) });
    expect('a card that the room did not change is not made again (the same buttons stay: a press that is under a finger is not lost), and a press on it goes to the page\'s newest answer', [t.keyed('team:1') === before, (before.fire('click'), later), t.log.team], [true, [1], []]);
    t.open({ teams: [{ side: 1, pressed: false, disabled: false }, { side: 2, pressed: true, disabled: false }] });
    expect('a card that the room changed is made again', t.keyed('team:1') !== before, true);
    t.open({ note: 'Team 1 has two colors already.' });
    expect('a line that the page says (a refused press) is in the card as a status; with none there is no line', [t.all('ss-note').map((n) => n.textContent), t.all('ss-note').map((n) => n.attrs.role)], [['Team 1 has two colors already.'], ['status']]);
    t.open({ note: '' });
    expect('... and gone when the page says none', t.all('ss-note').length, 0);
    t.open({ seat: 3, id: 'black', name: 'Black', title: 'Priya' });
    expect('a card drawn for another color is that color\'s: the seat, the title and the color', [t.sheet.seat(), t.all('ss-who')[0].all((n) => n.tagName === 'h3')[0].textContent, t.all('ss-who')[0].all((n) => n.tagName === 'b')[0].attrs['data-c']], [3, 'Priya', 'black']);
    expect('drawing it again does not hold the page twice', t.held, [true]);
}
{
    const t = sheetRig2();
    t.open();
    t.keyed('done').fire('click');
    expect('Done closes it: put away, the page behind is let go, the page is told that a person closed it (the focus may go back)', [t.sheet.isOpen(), t.sheet.back.hidden, t.held, t.log.closed, t.sheet.seat()], [false, true, [true, false], [true], -1]);
    t.keyed('done').fire('click');
    expect('a close of a card that is away does nothing', [t.held, t.log.closed], [[true, false], [true]]);
    t.open(); t.keyed('x').fire('click');
    expect('the cross closes it too, with a label', [t.sheet.isOpen(), t.log.closed, t.keyed('x').attrs['aria-label']], [false, [true, true], 'Close']);
    t.open();
    const other = t.sheet.back.fire('keydown', { key: 'Enter' });
    expect('a key that is not Escape leaves it open', [t.sheet.isOpen(), other.prevented === true], [true, false]);
    const esc = t.sheet.back.fire('keydown', { key: 'Escape' });
    expect('Escape closes it and the key is its own', [t.sheet.isOpen(), esc.prevented, t.log.closed], [false, true, [true, true, true]]);
    t.open();
    t.sheet.close();
    expect('a close by the page (the player left, the window turned) is told with no focus to give back, and lets the page go', [t.sheet.isOpen(), t.log.closed, t.held.slice(-2)], [false, [true, true, true, false], [true, false]]);
}
{
    const t = sheetRig2();
    t.open();
    t.tap(t.sheet.back, t.sheet.back);
    expect('a press that starts and ends on the dark ground closes it', [t.sheet.isOpen(), t.log.closed], [false, [true]]);
    t.open();
    t.tap(t.sheet.box, t.sheet.box, t.sheet.box);
    expect('a press inside the card does not', t.sheet.isOpen(), true);
    t.tap(t.sheet.box, t.sheet.back, t.sheet.back);
    expect('a drag from inside the card that ends on the dark ground does not', t.sheet.isOpen(), true);
    t.sheet.back.fire('click', { target: t.sheet.back });
    expect('a click that no press led to does not', t.sheet.isOpen(), true);
    t.sheet.back.fire('pointerdown', { target: t.sheet.back }); t.sheet.back.fire('pointercancel', {}); t.sheet.back.fire('pointerup', { target: t.sheet.back }); t.sheet.back.fire('click', { target: t.sheet.back });
    expect('a press that the browser cancelled (a scroll) does not', t.sheet.isOpen(), true);
}
{
    const t = sheetRig2();
    t.open({ title: '<img src=x>', parts: ['<b>'] });
    expect('a name is text and nothing more: the title and the words hold no node but their own', [t.all('ss-who')[0].all((n) => n.tagName === 'h3')[0].children.length, t.all('ss-sub')[0].children.map((n) => n.tagName)], [0, ['b', 'span']]);
}

console.log((failed ? 'FAILED ' : 'ok ') + checks + ' checks, ' + failed + ' failed');
process.exit(failed ? 1 : 0);
