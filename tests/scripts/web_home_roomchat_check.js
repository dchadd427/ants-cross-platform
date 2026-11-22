// Runs the room's tab of Home's chat box (web/home/roomchat.js, docs/HOME_CHAT.md "The Room tab") in node, in a small fake page of this file: the two tabs that exist while the page holds a room, the dot on the tab
// that has news, the box that is folded, the keys, the room that goes, and the parts that are missing. usage: node web_home_roomchat_check.js (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');
const R = require(path.resolve(__dirname, '..', '..', 'web', 'home', 'roomchat.js'));
let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }

class Node {
    constructor(tag, cls) { this.tag = tag; this.className = cls || ''; this.hidden = false; this.children = []; this.attrs = {}; this.on = {}; this.parent = null; this.id = ''; this.textContent = ''; this.type = ''; this.focused = false; }
    get firstChild() { return this.children[0] || null; }
    setAttribute(k, v) { this.attrs[k] = String(v); }
    getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
    removeAttribute(k) { delete this.attrs[k]; }
    appendChild(c) { if (c.parent) c.parent.children.splice(c.parent.children.indexOf(c), 1); c.parent = this; this.children.push(c); return c; }
    insertBefore(c, before) { if (c.parent) c.parent.children.splice(c.parent.children.indexOf(c), 1); c.parent = this; const at = before ? this.children.indexOf(before) : -1; if (at < 0) this.children.push(c); else this.children.splice(at, 0, c); return c; }
    addEventListener(t, fn) { (this.on[t] = this.on[t] || []).push(fn); }
    fire(t, ev) { (this.on[t] || []).forEach((f) => f(ev)); }
    focus() { this.focused = true; }
    cloneNode() { const c = new Node(this.tag, this.className); c.id = this.id; c.hidden = this.hidden; return c; }
    querySelector(sel) { const want = sel.slice(1); const walk = (n) => { for (const c of n.children) { if (c.className.split(' ').indexOf(want) !== -1) return c; const d = walk(c); if (d) return d; } return null; }; return walk(this); }
}
function rig(o) {
    o = o || {};
    const box = new Node('section', 'gc'), head = new Node('div', 'gc-head'), title = new Node('h2', 'gc-title'), here = new Node('span', 'gc-online'), pill = new Node('span', 'gc-unread'), globalBody = new Node('div', 'gc-body'), chat = new Node('section', 'chat');
    chat.id = 'chat'; chat.hidden = true; pill.hidden = true; box.setAttribute('data-open', 'true');
    head.appendChild(title); head.appendChild(here); head.appendChild(pill); box.appendChild(head); box.appendChild(globalBody);
    const away = [], counts = [];
    const dock = { root: box, away(on) { away.push(!!on); } };
    const doc = { hidden: false, createElement(t) { return new Node(t); } };
    const observers = [];
    const win = o.noObserver ? {} : { MutationObserver: function (fn) { this.fn = fn; this.observe = function () { }; observers.push(this); } };
    const m = R.mount({ doc, win, dock, chat, news: (n) => counts.push(n) });
    return { box, head, here, pill, globalBody, chat, dock, doc, win, m, away, counts, observers,
        tab: (name) => head.querySelector('.rc-tabs').children.find((t) => t.attrs['data-rc'] === name),
        strip: () => head.querySelector('.rc-tabs'), note: () => head.querySelector('.rc-note'), body: () => box.children.find((c) => c.className === 'rc-body'),
        room(on) { chat.hidden = !on; m.sync(); }, fold(open) { box.setAttribute('data-open', open ? 'true' : 'false'); observers.forEach((w) => w.fn()); }, pillShown(on) { pill.hidden = !on; observers.forEach((w) => w.fn()); } };
}

// ---- 1. no room: no tabs ----
{
    const r = rig();
    check('the strip, the note and the room\'s body are there and hidden while the page holds no room', r.strip().hidden && r.note().hidden && r.body().hidden);
    check('the box says that no tab is there (nothing, or none)', [null, 'none'].indexOf(r.box.getAttribute('data-rc')) !== -1);
    check('the room\'s chat section moved into the box (a move, not a copy)', r.body().children.indexOf(r.chat) !== -1 && r.chat.parent === r.body());
    same('there are two tabs, the front page\'s chat first', r.strip().children.map((t) => t.attrs['data-rc']), ['global', 'room']);
    check('no tab can be chosen while there is no room', (r.m.select('room'), r.m.tab() === 'global') && r.m.exists() === false);
    check('news of the room is not taken while there is no room (the page shows its own)', r.m.news() === false);
    same('the strip is in the box\'s header, before the heading', r.head.children[0] === r.strip(), true);
    check('the dock is not asked to give the box away', r.away.indexOf(true) === -1);
}

// ---- 2. a room: two tabs ----
{
    const r = rig();
    r.room(true);
    check('the strip shows', !r.strip().hidden && r.m.exists());
    same('the tab that shows first is the front page\'s chat', [r.m.tab(), r.tab('global').attrs['aria-selected'], r.tab('room').attrs['aria-selected'], r.box.getAttribute('data-rc')], ['global', 'true', 'false', 'global']);
    same('the tabs are in the tab order by the roving rule (the one that shows)', [r.tab('global').attrs.tabindex, r.tab('room').attrs.tabindex], ['0', '-1']);
    same('the roles', [r.strip().attrs.role, r.tab('global').attrs.role, r.tab('room').attrs.role], ['tablist', 'tab', 'tab']);
    same('the tabs name the chats', [r.tab('global').textContent, r.tab('room').textContent], [R.TEXT.global, R.TEXT.room]);
    same('the words', [R.TEXT.global, R.TEXT.room], ['Ants & Uncles', 'Room']);
    check('the room\'s body and note are hidden while the other tab shows', r.body().hidden && r.note().hidden);
    r.m.select('room');
    same('the Room tab is the one that shows', [r.m.tab(), r.tab('room').attrs['aria-selected'], r.tab('global').attrs['aria-selected'], r.box.getAttribute('data-rc')], ['room', 'true', 'false', 'room']);
    check('the room\'s body and the note show, and the dock gives the box away', !r.body().hidden && !r.note().hidden && r.away.slice(-1)[0] === true);
    r.m.select('global');
    check('back to the front page\'s chat: the dock has the box again', r.body().hidden && r.away.slice(-1)[0] === false && r.m.tab() === 'global');
    r.m.select('nothing');
    same('a name that is not a tab changes nothing', r.m.tab(), 'global');
    r.tab('room').fire('click', {});
    same('a press on a tab chooses it and moves the focus to it', [r.m.tab(), r.tab('room').focused], ['room', true]);
}

// ---- 3. the dots ----
{
    const r = rig();
    r.room(true);
    check('a line of the room while the front page\'s tab shows: a dot on the Room tab (and the page shows no pop-up of its own)', r.m.news() === true && !r.tab('room').children[0].hidden);
    same('the label of the tab says so', r.tab('room').attrs['aria-label'], 'Room, new messages');
    same('the strip is told how many', r.counts.slice(-1), [1]);
    r.m.news(); r.m.news();
    same('every line counts', r.counts.slice(-1), [3]);
    r.m.select('room');
    check('opening the tab clears the dot and the count', r.tab('room').children[0].hidden && r.counts.slice(-1)[0] === 0 && r.tab('room').attrs['aria-label'] === undefined);
    check('a line of the room that is on view is no dot', r.m.seen() === true && r.m.news() === true && r.tab('room').children[0].hidden && r.counts.slice(-1)[0] === 0);
    r.fold(false);
    check('a folded box shows no dot for news that was read', r.tab('room').children[0].hidden && r.counts.slice(-1)[0] === 0);
    r.fold(true);
    check('old news does not come back when the box is opened again', r.tab('room').children[0].hidden && r.counts.slice(-1)[0] === 0);
    r.pillShown(true);
    check('a line of the front page\'s chat while the room\'s tab shows: a dot on the other tab', !r.tab('global').children[0].hidden && r.tab('global').attrs['aria-label'] === 'Ants & Uncles, new messages');
    r.m.select('global');
    check('the dot of the front page\'s chat goes when its tab shows (its own count says the rest)', r.tab('global').children[0].hidden);
    same('the unread count of the front page\'s chat is the dot\'s job while the box has tabs: it is not drawn twice', r.box.getAttribute('data-rc'), 'global');
}

// ---- 4. a box that is folded, a page that is hidden ----
{
    const r = rig();
    r.room(true); r.m.select('room');
    r.fold(false);
    check('the box is folded: the room is not on view', r.m.seen() === false);
    r.m.news();
    check('a line that comes while the box is folded is a dot', !r.tab('room').children[0].hidden && r.counts.slice(-1)[0] === 1);
    r.fold(true);
    check('opening the box with the Room tab on clears the dot', r.tab('room').children[0].hidden && r.counts.slice(-1)[0] === 0);
    r.doc.hidden = true;
    check('a page that is hidden does not see the room', r.m.seen() === false);
    r.m.news();
    check('a line that comes while the page is hidden is a dot', !r.tab('room').children[0].hidden);
    r.doc.hidden = false; r.m.sync();
    check('the page comes back: the dot goes at the next look', r.tab('room').children[0].hidden && r.counts.slice(-1)[0] === 0);
}

// ---- 5. the keys ----
{
    const r = rig();
    r.room(true);
    const key = (name, k) => { let stopped = false; r.tab(name).fire('keydown', { key: k, preventDefault() { stopped = true; } }); return stopped; };
    check('Right goes to the Room tab, Left back, Home and End to the ends', key('global', 'ArrowRight') && r.m.tab() === 'room' && key('room', 'ArrowLeft') && r.m.tab() === 'global' && key('global', 'End') && r.m.tab() === 'room' && key('room', 'Home') && r.m.tab() === 'global');
    check('another key is not ours', key('global', 'a') === false && r.m.tab() === 'global');
    same('the focus follows a key', r.tab('global').focused, true);
}

// ---- 6. the room goes ----
{
    const r = rig();
    r.room(true); r.m.select('room'); r.fold(false); r.m.news();
    r.room(false);
    check('the strip goes, the front page\'s chat has the box again, no count is kept', r.strip().hidden && r.body().hidden && r.note().hidden && r.m.tab() === 'global' && !r.m.exists() && r.box.getAttribute('data-rc') === 'none' && r.away.slice(-1)[0] === false && r.counts.slice(-1)[0] === 0);
    r.room(true);
    same('the next room starts on the front page\'s tab with no news', [r.m.tab(), r.tab('room').children[0].hidden], ['global', true]);
}

// ---- 7. parts that are missing, and a page with no observer ----
{
    const box = new Node('section', 'gc');
    check('no head: no tabs, nothing thrown', R.mount({ doc: { createElement: (t) => new Node(t) }, win: {}, dock: { root: box, away() { } }, chat: new Node('section') }) === null);
    check('no chat: nothing mounted', R.mount({ doc: { createElement: (t) => new Node(t) }, win: {}, dock: { root: box, away() { } }, chat: null }) === null);
    check('no dock: nothing mounted', R.mount({ doc: { createElement: (t) => new Node(t) }, win: {}, dock: null, chat: new Node('section') }) === null);
    const r = rig({ noObserver: true });
    r.room(true);
    check('a browser with no MutationObserver still has the two tabs', !r.strip().hidden && r.m.exists());
}

console.log('the room\'s tab of the chat box: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
