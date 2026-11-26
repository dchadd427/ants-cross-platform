// Runs the private messages of Home (web/home/messages.js, docs/HUB_MESSAGES.md) against a FAKE chat dock (the hub's frames come from the test; what the page sends goes into a list; Block is a command that the test answers)
// and a fake clock, in the fake browser of tests/scripts/web_page_harness.js (its DOM). What it checks:
//   1. the pure parts: a conversation, a message, the list and a thread cleaned (hostile text stays text, names and texts are cut, a handle has the hub's shape, a number is a whole number, too many rows is no frame);
//   2. the source: ASCII, no markup, no storage, no network, no address to follow;
//   3. who has it: a guest, a hub that is not there or a welcome that has not come: no panel, and the ways in refuse (the part has no button: the list of conversations is the Messages tab of the bell, tests/scripts/web_home_alerts_check.js);
//   4. the conversations and their count: what the part keeps for the bell (convs, count, the callback that says they changed), what a message that comes in or is read does to them;
//   5. the panel: opens on a conversation, Escape, a press outside, the X, where it stands, a phone's sheet, the focus back on what opened it;
//   6. a conversation: dmopen, the bubbles (mine and theirs, a Report only on theirs), the notices, the day words, the field, sending (exact frames, nothing drawn until the hub's echo, the field kept
//      every word of a refusal, the page's own refusals (empty, markup, too long, offline); the arrow goes back to the bell's Messages tab;
//   7. what is on the screen is read: dmread for a message that comes in, none for a panel that is closed or a hidden page, the read when the page is shown again;
//   8. the menu: Block and Unblock are the hub's commands with the handle, their answers; Delete asks twice and goes back to the Messages tab;
//   9. the report: the card, the three reasons, the exact frame, the answers, Escape, Cancel, the focus; report(h) with and without a message of theirs;
//  10. what the hub may send: a frame that is wrong, too many rows, hostile names, unknown kinds: nothing breaks and nothing becomes markup; a new welcome clears and asks again for the open conversation; the keys (Tab stays inside).
// tests/scripts/test_web_home_messages.py runs this with node. usage: node web_home_messages_check.js [web/lobby.html]     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
globalThis.AntsShapes = require(path.resolve(__dirname, '..', '..', 'web', 'shapes.js'));

const lobbyPath = process.argv[2] || path.resolve(__dirname, '..', '..', 'web', 'lobby.html');
const lobbyDir = path.dirname(path.resolve(lobbyPath));
const modPath = path.join(lobbyDir, 'home', 'messages.js');
const modSource = fs.readFileSync(modPath, 'utf8');
const Pure = require(modPath);
const Chat = require(path.join(lobbyDir, 'home', 'chat.js'));
const H = require('./web_page_harness.js')(lobbyPath);
const { El } = H;

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function same(label, got, want) {
    const a = JSON.stringify(got);
    const b = JSON.stringify(want);
    check(label, a === b, 'got ' + a + ', wanted ' + b);
}
const cls = (e) => String(e.className).split(/\s+/);
const byClass = (root, c) => root.findAll((e) => cls(e).indexOf(c) !== -1);
const one = (root, c) => byClass(root, c)[0] || null;
const texts = (root, c) => byClass(root, c).map((e) => e.textContent);

const NOW = 1800000000;                                  // (seconds since 1970: the page's clock stands here)
const H1 = 'a1b2c3d4e5f6', H2 = '0123456789ab', H3 = 'ffffffffffff';
const conv = (h, name, o) => Object.assign({ h, name, at: NOW - 60, text: 'hello', me: 0, u: 0, f: 0 }, o);
const thread = (h, name, rows, o) => Object.assign({ t: 'thread', h, name, f: 0, rows }, o);
const mine = (id, text, at) => ({ id, at: at || NOW - 30, text, me: 1 });
const theirs = (id, text, at) => ({ id, at: at || NOW - 40, text });
const dayOf = (at) => (at >= NOW - 36000 ? 'Today' : 'Friday');

function makeClock() {
    const c = { t: 0, timers: [], next: 1 };
    c.setTimeout = (fn, ms) => { const id = c.next++; c.timers.push({ id, at: c.t + (ms || 0), fn }); return id; };
    c.clearTimeout = (id) => { c.timers = c.timers.filter((x) => x.id !== id); };
    c.advance = (ms) => {
        const until = c.t + ms;
        for (let turns = 0; ; turns++) {
            if (turns > 2000) throw new Error('a storm of timers');
            const due = c.timers.filter((x) => x.at <= until).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            c.timers = c.timers.filter((x) => x !== due);
            c.t = Math.max(c.t, due.at);
            due.fn();
        }
        c.t = until;
    };
    return c;
}
function load(clock) {
    const root = {};
    new Function('self', 'setTimeout', 'clearTimeout', modSource)(root, clock.setTimeout, clock.clearTimeout);
    return root.AntsHomeMessages;
}

// one part on a fake page: o.phone (the window is a phone), o.noHead (the dock has no head), o.noSay, o.noWhen
function rig(o) {
    o = o || {};
    const env = { elements: {}, innerHTMLWrites: [] };
    const clock = makeClock();
    const Mod = load(clock);
    const doc = {
        hidden: false, listeners: {}, capturing: {}, activeElement: null, body: new El(env, 'body', ''),
        createElement: (t) => new El(env, t), createElementNS: (ns, t) => new El(env, t),
        querySelector: (sel) => doc.body.querySelector(sel),
        addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); },
        fire(t, ev) { (doc.listeners[t] || []).forEach((fn) => fn(ev || {})); },
    };
    env.doc = doc;
    env.root = new El(env, 'html', '');
    const state = { phone: !!o.phone };
    const win = { AntsHomeChat: Chat, innerWidth: o.phone ? 390 : 1280, innerHeight: o.phone ? 844 : 800, matchMedia: () => ({ matches: state.phone }), listeners: {}, addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); } };
    // the history, as a browser keeps it: an entry for the page, one more for each pushState; back() takes the newest entry away and says popstate (the page's own entry is never taken)
    const entries = [{ ants: 'home' }];
    win.history = { get state() { return entries[entries.length - 1]; }, pushState(st) { entries.push(JSON.parse(JSON.stringify(st))); }, back() { if (entries.length > 1) { entries.pop(); (win.listeners.popstate || []).forEach((fn) => fn({})); } } };
    if (o.vv) {                                          // the visual viewport: what the person sees (the keyboard is out of it); resize(h, top) is what a keyboard or a pan does
        const vl = {};
        win.visualViewport = { height: win.innerHeight, offsetTop: 0, addEventListener(t, fn) { (vl[t] = vl[t] || []).push(fn); }, removeEventListener(t, fn) { vl[t] = (vl[t] || []).filter((f) => f !== fn); }, listeners: vl, scale: 1 };
        win.resizeVisual = (h, top) => { win.visualViewport.height = h; win.visualViewport.offsetTop = top || 0; (vl.resize || []).slice().forEach((fn) => fn({})); };
        win.panVisual = (top) => { win.visualViewport.offsetTop = top; (vl.scroll || []).slice().forEach((fn) => fn({})); };
    }
    const bar = new El(env, 'div'), box = new El(env, 'div', 'gchat'), head = new El(env, 'div'), here = new El(env, 'span');
    bar.className = 'hbar'; bar.getBoundingClientRect = () => ({ top: 0, bottom: 70, left: 0, right: 100, width: 100, height: 70 });
    box.getBoundingClientRect = () => ({ top: 550, bottom: 790, left: 0, right: 700, width: 700, height: 240 });
    head.className = 'gc-head'; here.className = 'gc-online';
    const title = new El(env, 'h2'), fold = new El(env, 'button');
    title.className = 'gc-title'; fold.className = 'gc-fold';
    if (!o.noHead) { head.appendChild(title); head.appendChild(here); head.appendChild(fold); box.appendChild(head); }
    doc.body.appendChild(bar); doc.body.appendChild(box);
    const sent = [], calls = [], taps = [], said = [], noted = [], errors = [], link = { open: true }, tally = { changed: 0, toList: 0 }, from = new El(env, 'button', '');
    from.className = 'al-bell'; doc.body.appendChild(from);
    const dock = {
        root: box, tap(fn) { taps.push(fn); }, send(obj) { if (!link.open) return false; sent.push(JSON.parse(JSON.stringify(obj))); return true; }, call(text, done) { calls.push({ text, done }); }, note(t) { noted.push(t); },
        when: o.noWhen ? undefined : (at) => ({ time: '10:' + String(at % 60).padStart(2, '0'), full: 'full', day: dayOf(at), key: dayOf(at) })
    };
    const part = Mod.mount({ doc, win, dock, say: o.noSay ? undefined : (w, t) => said.push([w, t]), now: () => NOW * 1000, error: (e) => errors.push(e), changed: () => { tally.changed++; }, toList: () => { tally.toList++; } });
    const r = { env, clock, entries, Mod, doc, win, box, head, here, bar, part, dock, sent, calls, taps, said, noted, errors, link, state, tally, from };
    r.emit = (kind, m) => taps.forEach((fn) => fn(kind, m));
    r.live = (you) => { r.link.open = true; r.emit('live', { t: 'welcome', v: 1, you: you === undefined ? { name: 'hazel', mod: false, role: 'none', k: 'k1' } : you, login: true, presence: true, history: [] }); return r; };
    r.list = (rows) => { r.emit('msgs', { t: 'msgs', n: rows.reduce((n, c) => n + c.u, 0), rows }); return r; };
    r.shade = () => one(doc.body, 'pm-shade');
    r.panel = () => one(doc.body, 'pm-panel');
    r.card = () => one(doc.body, 'pm-card');
    r.open = (h, name) => { r.part.open(h || H1, name || 'maple', r.from); return r.panel(); };            // (the conversation, opened as the bell does: what has the focus again when it closes is `from`)
    r.names = () => texts(r.panel(), 'pm-nm');
    r.frames = (t) => sent.filter((m) => m.t === t);
    r.key = (key, extra) => { const ev = Object.assign({ key, preventDefault() { ev.stopped = true; } }, extra); doc.fire('keydown', ev); return ev; };
    r.openThread = (h, name, rows, o2) => { r.live().list([conv(h, name)]); r.open(h, name); r.emit('thread', thread(h, name, rows || [], o2)); return r; };
    r.bubbles = () => byClass(r.panel(), 'pm-msg');
    r.field = () => one(r.panel(), 'pm-input');
    r.type = (text) => { r.field().value = text; return r; };
    r.status = () => one(r.panel(), 'pm-status').textContent;
    r.sendBtn = () => one(r.panel(), 'pm-send');
    r.menu = () => { one(r.panel(), 'pm-more').click(); return one(r.panel(), 'pm-menu'); };
    r.count = () => { const n = r.part.count(); return n ? String(n) : ''; };
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. the pure parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Pure;
    same('a conversation is cleaned: name, preview, the flags as exactly 1', P.convOf({ h: H1, name: '  maple  ', at: 5, text: 'hi   there', me: 1, u: 2, f: 1, b: 1 }, Chat), { h: H1, name: 'maple', at: 5, text: 'hi there', me: true, u: 2, f: true, b: true });
    same('...a flag that is not 1 is no flag', P.convOf({ h: H1, name: 'maple', at: 5, text: 'x', me: true, u: 0, f: '1', b: 2 }, Chat), { h: H1, name: 'maple', at: 5, text: 'x', me: false, u: 0, f: false, b: false });
    check('a handle must be 12 lower case hex characters', [H1, 'A1B2C3D4E5F6', 'a1b2c3d4e5f', 'a1b2c3d4e5f67', 'g1b2c3d4e5f6', 7, null, ''].map((h) => P.convOf({ h, name: 'x', at: 1, text: 't', u: 0 }, Chat)).filter(Boolean).length === 1);
    check('the numbers are whole numbers in their range', [{ at: -1, u: 0 }, { at: 1.5, u: 0 }, { at: 5e9, u: 0 }, { at: 1, u: -1 }, { at: 1, u: 1.5 }, { at: 1, u: '1' }, { at: 1 }].every((n) => P.convOf(Object.assign({ h: H1, name: 'x', text: 't' }, n), Chat) === null));
    check('no name, no conversation', P.convOf({ h: H1, name: '', at: 1, text: 't', u: 0 }, Chat) === null && P.convOf({ h: H1, at: 1, text: 't', u: 0 }, Chat) === null && P.convOf({ h: H1, name: '​‮', at: 1, text: 't', u: 0 }, Chat) === null);
    check('a name is cut to 32 code points and a text to 500, markup and the hub-removed controls are plain text', (() => { const c = P.convOf({ h: H1, name: 'n'.repeat(80), at: 1, text: '<b>x</b>‮' + 'y'.repeat(900), u: 0 }, Chat); return c.name.length <= 33 && Chat.points(c.name) <= 33 && c.text.indexOf('‮') === -1 && c.text.indexOf('<b>x</b>') === 0 && Chat.points(c.text) <= 501; })());
    same('a message is cleaned: id, time, text, mine as exactly 1', [P.msgOf({ id: 3, at: 9, text: ' a   b ', me: 1 }, Chat), P.msgOf({ id: 4, at: 9, text: 'c', me: 'yes' }, Chat)], [{ id: 3, at: 9, text: 'a b', me: true }, { id: 4, at: 9, text: 'c', me: false }]);
    check('a message with no text, no id or a wrong time is none', [{ id: 1, at: 1, text: '' }, { id: 1, at: 1 }, { id: 'x', at: 1, text: 't' }, { id: 1, at: -3, text: 't' }, { id: 1.5, at: 1, text: 't' }, null, 'x'].every((x) => P.msgOf(x, Chat) === null));
    same('a list keeps the rows that are right, in their order', P.listOf({ rows: [conv(H1, 'one'), { h: 'bad' }, conv(H2, 'two')] }, Chat).map((c) => c.name), ['one', 'two']);
    check('a list of more than 50 rows, or with no rows, is no frame', P.listOf({ rows: Array.from({ length: 51 }, () => conv(H1, 'x')) }, Chat) === null && P.listOf({}, Chat) === null && P.listOf(null, Chat) === null && P.listOf({ rows: 'x' }, Chat) === null && P.listOf({ rows: Array.from({ length: 50 }, () => conv(H1, 'x')) }, Chat).length === 50);
    same('a thread: the name, the flags and the messages that are right', P.threadOf(thread(H1, 'maple', [mine(1, 'a'), { id: 'x' }, theirs(2, 'b')], { f: 1, b: 1 }), Chat), { h: H1, name: 'maple', f: true, b: true, rows: [{ id: 1, at: NOW - 30, text: 'a', me: true }, { id: 2, at: NOW - 40, text: 'b', me: false }], loaded: true });
    check('a thread of more than 100 messages, with a wrong handle or with no name is no frame', P.threadOf(thread(H1, 'x', Array.from({ length: 101 }, (_, i) => theirs(i + 1, 't'))), Chat) === null && P.threadOf(thread('bad', 'x', []), Chat) === null && P.threadOf(thread(H1, '', []), Chat) === null && P.threadOf(thread(H1, 'x', Array.from({ length: 100 }, (_, i) => theirs(i + 1, 't'))), Chat).rows.length === 100);
    same('the circle holds the first code point', [P.initial('maple', Chat), P.initial(String.fromCodePoint(0x1F41C) + 'x', Chat)], ['m', String.fromCodePoint(0x1F41C)]);
    same('the limits are the hub\'s own', [P.LIMIT.convs, P.LIMIT.rows, P.LIMIT.text, P.LIMIT.name], [50, 100, 500, 32]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. the source
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const code = modSource.replace(/\/\/.*/g, '');
    check('messages.js is ASCII only', /^[\x00-\x7f]*$/.test(modSource));
    check('no markup is written: no innerHTML, no outerHTML, no insertAdjacentHTML, no document.write', !/innerHTML|outerHTML|insertAdjacentHTML|document\.write/.test(code));
    check('nothing is kept in the browser and nothing is fetched: no storage, no cookie, no fetch, no XMLHttpRequest, no WebSocket, no sendBeacon, no eval', !/localStorage|sessionStorage|indexedDB|document\.cookie|fetch\(|XMLHttpRequest|WebSocket|sendBeacon|eval\(|new Function/.test(code));
    check('no link is made: no href, no anchor, no location', !/href|createElement\('a'\)|location\./.test(code));
    check('every handle that is sent goes through the hub\'s own shape', /HANDLE\.test/.test(code));
    check('the hub is spoken to with these frames only', (() => { const found = [...code.matchAll(/\{ t: '(\w+)'/g)].map((m) => m[1]); return found.length > 0 && found.every((t) => ['dmopen', 'dm', 'dmread', 'dmdel', 'dmreport'].indexOf(t) !== -1); })());
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. who has it
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig();
    check('before the welcome there is no panel, and the part says it is off and not ready', !r.panel() && r.part.state() === 'off' && r.part.ready() === false && r.part.convs().length === 0);
    check('...and the ways in refuse', r.part.open(H1, 'maple', r.from) === false && !r.panel() && r.part.report(H1, 'maple') === false && r.frames('dmopen').length === 0);
    r.live(null);
    check('a guest (the welcome names nobody) has no panel, and the ways in refuse', r.part.open(H1, 'maple', r.from) === false && r.part.report(H1, 'maple') === false && !r.panel() && r.part.ready() === false);
    r.live();
    check('a person who is logged in is ready and has nothing open', r.part.ready() === true && r.part.state() === 'closed');
    r.list([conv(H1, 'maple', { u: 2 })]);
    r.emit('lost');
    check('a hub that is gone takes the conversations away from the bell, and the bell is told', r.part.state() === 'off' && r.part.ready() === false && r.part.convs().length === 0 && r.part.count() === 0 && r.tally.changed > 0);
    r.live();
    check('and it is ready again with the next welcome (it knows none of the conversations until the hub says them)', r.part.ready() === true && r.part.count() === 0);
    check('a hub that has not said the list yet gives no count', r.count() === '' && r.part.count() === 0);
    check('mount is null where there is no dock, no tap or no chat script', (() => { const { Mod, doc, win } = r; return Mod.mount({ doc, win, dock: null }) === null && Mod.mount({ doc, win, dock: {} }) === null && Mod.mount({ doc, win: Object.assign({}, win, { AntsHomeChat: null }), dock: r.dock }) === null; })());
    check('the part makes no button and puts nothing in the chat\'s head (the list is the bell\'s)', !byClass(r.doc.body, 'pm-btn').length && r.head.children.length === 3 && cls(r.head).indexOf('pm-has') === -1);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. the conversations and their count (what the bell draws)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig().live();
    r.list([conv(H1, 'maple', { u: 2 }), conv(H2, 'fern', { u: 1 })]);
    check('the count is what is not read, in all conversations', r.part.count() === 3 && r.part.convs().length === 2);
    same('the conversations are given as the hub said them, cleaned: handle, name, the flags, what is not read', r.part.convs().map((c) => [c.h, c.name, c.u, c.f, c.b]), [[H1, 'maple', 2, false, false], [H2, 'fern', 1, false, false]]);
    check('the bell is told at every list', r.tally.changed >= 1);
    const before = r.tally.changed;
    r.list([conv(H1, 'maple'), conv(H2, 'fern')]);
    check('the hub\'s list that has none unread takes the count away, and the bell is told', r.part.count() === 0 && r.tally.changed > before);
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 1, at: NOW - 5, text: 'a new one', me: 0, u: 1, f: 0 });
    check('a message that comes in and is not read raises it', r.part.count() === 1);
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 2, at: NOW - 4, text: 'and another', me: 0, u: 2, f: 0 });
    check('...to what the hub counts', r.part.count() === 2);
    r.emit('msg', { t: 'msg', h: H3, name: 'wren', id: 3, at: NOW - 3, text: 'hi', me: 0, u: 1, f: 0 });
    check('a message from a new person makes a conversation and counts', r.part.count() === 3 && r.part.convs()[0].h === H3);
    same('...and the conversation with the newest message is the first', r.part.convs().map((c) => c.h), [H3, H1, H2]);
    r.emit('msg', { t: 'msg', h: H3, name: 'wren', id: 4, at: NOW - 2, text: 'my own', me: 1, u: 0, f: 0 });
    check('a message that this person sent (the echo) does not count and clears what was not read of that conversation', r.part.count() === 2 && r.part.convs()[0].me === true);
    r.list(Array.from({ length: 50 }, (_, i) => conv(('00000000000' + i.toString(16)).slice(-12), 'p' + i, { u: 3 })));
    check('fifty conversations at the most are kept, with their counts', r.part.convs().length === 50 && r.part.count() === 150);
    const c0 = r.part.convs(); c0.pop();
    check('what convs() gives is a copy: changing it changes nothing', r.part.convs().length === 50);
    check('a guest has none: no conversations and no count whatever the hub said before', (() => { r.live(null); return r.part.convs().length === 0 && r.part.count() === 0; })());
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. the panel
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig().live().list([conv(H1, 'maple', { u: 2 })]);
    const panel = r.open();
    check('a conversation opens a dialog named by it, with the arrow, the person and the X', !!panel && panel.getAttribute('role') === 'dialog' && panel.getAttribute('aria-label') === 'Private messages: maple' && !!one(panel, 'pm-back') && !!one(panel, 'pm-x') && texts(panel, 'pm-nm')[0] === 'maple' && r.part.state() === 'thread');
    check('the panel is not a list: it has no title of the list, no row and no rule of the list', !byClass(panel, 'pm-title').length && !byClass(panel, 'pm-row').length && !byClass(panel, 'pm-keep').length);
    check('what is not read of it is read at once, and the bell is told', r.part.count() === 0 && r.frames('dmopen').length === 1);
    r.key('Escape');
    check('Escape closes it and the focus is back on what opened it (the bell)', !r.shade() && r.doc.activeElement === r.from && r.part.state() === 'closed');
    r.open();
    r.shade().click();
    check('a press on the page outside closes it', !r.shade() && r.doc.activeElement === r.from);
    r.open();
    one(r.panel(), 'pm-x').click();
    check('the X closes it, the whole of it, and the focus is back on what opened it', !r.shade() && r.doc.activeElement === r.from && r.tally.toList === 0);
    r.open();
    r.panel().click();
    check('a press inside does not close it', !!r.shade());
    r.part.close();
    check('close() closes it', !r.shade() && r.part.state() === 'closed');
    const free = rig().live().list([conv(H1, 'maple')]);
    free.part.open(H1, 'maple');
    one(free.panel(), 'pm-x').click();
    check('a conversation that was opened with nothing to give the focus back to closes all the same', !free.shade() && free.part.state() === 'closed');
    const w = rig().live();
    w.open();
    check('in a wide window the panel stands under the bar, down to the chat (the top of the chat box less 8)', w.panel().style.top === '76px' && w.panel().style.height === '466px' && cls(w.shade()).indexOf('phone') === -1);
    const p = rig({ phone: true }).live();
    p.open();
    check('on a phone it is a sheet: the class that the style reads, and no place of its own', cls(p.shade()).indexOf('phone') !== -1 && p.panel().style.top === '' && p.panel().style.height === '');
    p.state.phone = false;
    p.win.listeners.resize.forEach((fn) => fn({}));
    check('a window that becomes wide puts the panel in its place', cls(p.shade()).indexOf('phone') === -1 && p.panel().style.top === '76px');
    check('nothing went wrong in any of it', !r.errors.length && !w.errors.length && !p.errors.length && !free.errors.length, [].concat(r.errors, w.errors, p.errors).map(String).join());
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 6. a conversation, and sending
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig().live().list([conv(H1, 'maple', { u: 2 })]);
    r.open(H1, 'maple');
    same('a conversation of the bell opens and asks the hub for it, with the handle, exactly', r.frames('dmopen'), [{ t: 'dmopen', h: H1 }]);
    check('...at once the name is there, and the words say that it is loading; the count is read', r.part.state() === 'thread' && r.names().indexOf('maple') !== -1 && /Loading/.test(one(r.panel(), 'pm-empty').textContent) && r.count() === '' && r.part.isOpen(H1) === true);
    r.emit('thread', thread(H1, 'maple', [theirs(1, 'hey hazel, want a 2v2?', NOW - 90000), theirs(2, 'no rush', NOW - 50), mine(3, 'yes! give me five minutes', NOW - 30)]));
    same('the messages, in order, mine and theirs', r.bubbles().map((b) => [cls(b)[1], one(b, 'pm-mt').textContent]), [['them', 'hey hazel, want a 2v2?'], ['them', 'no rush'], ['me', 'yes! give me five minutes']]);
    same('the day words: a divider where the day changes, and Today for today', texts(r.panel(), 'pm-day'), ['Friday', 'Today']);
    same('the time under each message', texts(r.panel(), 'pm-tm'), ['10:00', '10:10', '10:30']);
    same('only their messages have a Report', r.bubbles().map((b) => byClass(b, 'pm-rep').length), [1, 1, 0]);
    same('the head: the name, Private with its lock, the way back, the menu and the close', [r.names()[0], texts(r.panel(), 'pm-tag')[0], !!one(r.panel(), 'pm-back'), !!one(r.panel(), 'pm-more'), !!one(r.panel(), 'pm-x')], ['maple', 'Private', true, true, true]);
    same('the notice says who can read it, what the admins see and how long it is kept', texts(r.panel(), 'pm-note'), ['Only you and maple can read this. Admins see a message only if one of you reports it. Kept for 7 days.']);
    same('...and the line over the field says it is not the public chat', texts(r.panel(), 'pm-to'), ['Private message to maple. This is not the public chat.']);
    check('the field says Private message and has a name', r.field().placeholder === 'Private message…' && r.field().getAttribute('aria-label') === 'Private message to maple');
    check('the Send button has the lock and the word', !!one(r.sendBtn(), 'pm-ico') && r.sendBtn().textContent === 'Send');
    check('the dialog is named by the conversation', r.panel().getAttribute('aria-label') === 'Private messages: maple' && r.panel().getAttribute('data-view') === 'thread');
    // sending
    r.type('  see you   there  ').sendBtn().click();
    same('Send sends the cleaned text, exactly', r.frames('dm'), [{ t: 'dm', h: H1, text: 'see you there' }]);
    check('nothing is drawn as sent until the hub\'s echo; the field keeps the words and the button waits', r.bubbles().length === 3 && r.field().value === '  see you   there  ' && r.sendBtn().getAttribute('aria-busy') === 'true');
    r.sendBtn().click(); r.field().key('Enter');
    check('and a second press, or Enter, sends nothing while it waits', r.frames('dm').length === 1);
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 4, at: NOW - 1, text: 'see you there', me: 1, u: 0, f: 0 });
    check('the echo draws it, empties the field and frees the button', r.bubbles().length === 4 && one(r.bubbles()[3], 'pm-mt').textContent === 'see you there' && r.field().value === '' && !r.sendBtn().getAttribute('aria-busy'));
    r.type('with Enter'); r.field().key('Enter');
    same('Enter in the field sends, and the page is not left', [r.frames('dm').length, r.frames('dm')[1]], [2, { t: 'dm', h: H1, text: 'with Enter' }]);
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 5, at: NOW, text: 'with Enter', me: 1, u: 0, f: 0 });
    const form = one(r.panel(), 'pm-form'); r.type('by the form'); let stopped = false; form.fire('submit', { preventDefault() { stopped = true; } });
    check('a form that a keyboard submits is a send and not a new page', stopped && r.frames('dm').length === 3);
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 6, at: NOW, text: 'by the form', me: 1, u: 0, f: 0 });
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 7, at: NOW, text: 'from my other tab', me: 1, u: 0, f: 0 });
    r.type('half typed');
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 8, at: NOW, text: 'from my other tab again', me: 1, u: 0, f: 0 });
    check('a message that another tab of the account sent is drawn and does not empty a field that is being typed in', r.bubbles().length === 8 && r.field().value === 'half typed');
    r.type('');
    // the page's own refusals
    r.sendBtn().click();
    check('an empty field sends nothing and says so', r.frames('dm').length === 3 && r.status() === 'Write something first');
    r.type('a < b'); r.sendBtn().click();
    check('markup characters are refused here, in words', r.frames('dm').length === 3 && /cannot hold/.test(r.status()));
    r.type('x'.repeat(501)); r.sendBtn().click();
    check('501 code points are refused here, 500 are not', r.frames('dm').length === 3 && r.status() === 'That message is too long');
    r.type('y'.repeat(500)); r.sendBtn().click();
    check('...500 go', r.frames('dm').length === 4 && r.status() === '');
    r.emit('dmx', { t: 'dmx', h: H1, why: 'rate' });
    check('a refusal of the hub frees the button, keeps the words and says the words under the field', r.field().value === 'y'.repeat(500) && !r.sendBtn().getAttribute('aria-busy') && r.status() === 'Slow down a little');
    r.field().fire('input', {});
    check('typing takes the words away', r.status() === '');
    const words = { empty: 'Write something first', long: 'That message is too long', bad: 'That message could not be sent', banned: 'You cannot send messages now', muted: 'You cannot send messages now', young: 'Your account is too new to send messages yet', who: 'That person is not available', dup: 'You just sent that', rate: 'Slow down a little', quota: 'You have sent a lot of messages today', new: 'You have started a lot of conversations: try again later', full: 'Messages are full for now: try again later', busy: 'That did not go through, try again', gone: 'That message is gone' };
    same('every code of the hub has its words', Object.keys(words).map((why) => { r.emit('dmx', { t: 'dmx', h: H1, why }); return r.status(); }), Object.keys(words).map((k) => words[k]));
    r.emit('dmx', { t: 'dmx', h: H1, why: 'something new' });
    same('a code that the page does not know is the plain failure', r.status(), 'That did not go through, try again');
    r.link.open = false; r.sendBtn().click();
    check('a hub that is not there is said, and nothing is waiting', r.status() === 'Chat is offline' && !r.sendBtn().getAttribute('aria-busy'));
    r.link.open = true; r.sendBtn().click();
    r.clock.advance(8100);
    check('a hub that never answers frees the button after 8 seconds', !r.sendBtn().getAttribute('aria-busy') && r.status() === 'That did not go through, try again');
    r.emit('dmx', { t: 'dmx', h: H2, why: 'who' });
    check('a refusal about another conversation is not said under this field: it is a toast of the page', r.status() === 'That did not go through, try again' && r.said.length === 1 && r.said[0][1] === 'warn');
    one(r.panel(), 'pm-back').click();
    check('the arrow closes the panel and asks the bell for its Messages tab (the hub is not asked again)', r.part.state() === 'closed' && !r.shade() && r.tally.toList === 1 && r.frames('dmopen').length === 1);
    r.open(H1, 'maple');
    check('and a second opening asks the hub again (what is read is the hub\'s to know)', r.frames('dmopen').length === 2 && r.bubbles().length > 0);
    check('nothing went wrong in any of it', !r.errors.length, r.errors.map(String).join());
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 7. what is on the screen is read
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig().openThread(H1, 'maple', [theirs(1, 'one')]);
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 2, at: NOW, text: 'two', me: 0, u: 1, f: 0 });
    same('a message that comes in while the conversation is on the screen is drawn and read', [r.bubbles().length, r.frames('dmread'), r.count()], [2, [{ t: 'dmread', h: H1 }], '']);
    r.emit('msg', { t: 'msg', h: H3, name: 'wren', id: 3, at: NOW, text: 'elsewhere', me: 0, u: 1, f: 0 });
    same('one for another person is counted and not read', [r.frames('dmread').length, r.count()], [1, '1']);
    r.emit('msgs', { t: 'msgs', n: 1, rows: [conv(H1, 'maple', { u: 1 }), conv(H3, 'wren', { u: 1 })] });
    same('the hub\'s list that says the open conversation has unread is read at once', [r.frames('dmread').length, r.count()], [2, '1']);
    check('isOpen is true for the conversation on the screen and for no other', r.part.isOpen(H1) === true && r.part.isOpen(H3) === false && r.part.isOpen('0'.repeat(12)) === false);
    r.doc.hidden = true;
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 4, at: NOW, text: 'while hidden', me: 0, u: 1, f: 0 });
    same('a page that is hidden does not have it on the screen: the message counts, nothing is read, isOpen is false', [r.frames('dmread').length, r.count(), r.part.isOpen(H1)], [2, '2', false]);
    r.doc.hidden = false; r.doc.fire('visibilitychange', {});
    same('shown again, it is read', [r.frames('dmread').length, r.count()], [3, '1']);
    one(r.panel(), 'pm-x').click();
    check('a closed panel has nothing on the screen', r.part.isOpen(H1) === false);
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 5, at: NOW, text: 'closed', me: 0, u: 1, f: 0 });
    same('...a message counts, and nothing is read', [r.frames('dmread').length, r.count()], [3, '2']);
    r.part.open(H1, 'maple');
    check('opening it again asks the hub (which reads) and the count of that conversation goes', r.frames('dmopen').length === 2 && r.count() === '1');
    r.emit('thread', thread(H1, 'maple', [theirs(1, 'one'), theirs(2, 'two')]));
    check('the thread frame is the conversation (drawn once, in order)', r.bubbles().length === 2);
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 2, at: NOW, text: 'two', me: 0, u: 1, f: 0 });
    check('a message that is in the thread already is not drawn twice', r.bubbles().length === 2);
    check('nothing went wrong in any of it', !r.errors.length, r.errors.map(String).join());
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 8. the menu: Block, Unblock, Delete
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig().openThread(H1, 'maple', [theirs(1, 'one')]);
    check('the menu is closed until it is pressed, then it has Block and Delete', !one(r.panel(), 'pm-menu') && one(r.panel(), 'pm-more').getAttribute('aria-expanded') === 'false');
    const m = r.menu();
    same('...in words', byClass(m, 'pm-mi').map((b) => b.textContent), ['Block maple', 'Delete this conversation']);
    check('the menu is a menu: roles for a screen reader, and it knows it is open', m.getAttribute('role') === 'menu' && byClass(m, 'pm-mi').every((b) => b.getAttribute('role') === 'menuitem') && one(r.panel(), 'pm-more').getAttribute('aria-expanded') === 'true');
    byClass(m, 'pm-mi')[0].click();
    same('Block is the hub\'s own command with the handle', r.calls.map((c) => c.text), ['/ignore h:' + H1]);
    check('the menu is closed, and nothing is said until the hub answers', !one(r.panel(), 'pm-menu') && !r.said.length);
    r.calls[0].done('reply', { t: 'reply', c: 'ignore', r: 'ok', n: 1 });
    same('the answer is said', r.said, [['maple is blocked: you get no more messages from them.', undefined]]);
    same('...and the menu now offers Unblock', byClass(r.menu(), 'pm-mi').map((b) => b.textContent)[0], 'Unblock maple');
    byClass(one(r.panel(), 'pm-menu'), 'pm-mi')[0].click();
    check('Unblock is /unignore with the handle', r.calls[1].text === '/unignore h:' + H1);
    r.calls[1].done('reply', { t: 'reply', c: 'unignore', r: 'ok', n: 0 });
    same('...and says so, and the menu offers Block again', [r.said[1][0], byClass(r.menu(), 'pm-mi')[0].textContent], ['maple is not blocked any more.', 'Block maple']);
    byClass(one(r.panel(), 'pm-menu'), 'pm-mi')[0].click();
    r.calls[2].done('reply', { t: 'reply', c: 'ignore', r: 'staff' });
    same('a moderator cannot be blocked: the words, as a warning', r.said[2], ['You cannot block a moderator.', 'warn']);
    check('...and the menu still says Block', byClass(r.menu(), 'pm-mi')[0].textContent === 'Block maple');
    byClass(one(r.panel(), 'pm-menu'), 'pm-mi')[0].click();
    r.calls[3].done('rate');
    same('a command that failed is the plain failure', r.said[3], ['That did not go through, try again', 'warn']);
    byClass(r.menu(), 'pm-mi')[0].click();
    r.calls[4].done('offline');
    same('...and a hub that is not there is said', r.said[4], ['Chat is offline', 'warn']);
    r.emit('thread', thread(H1, 'maple', [theirs(1, 'one')], { b: 1 }));
    check('a thread that says blocked offers Unblock, and so does the hub\'s list', byClass(r.menu(), 'pm-mi')[0].textContent === 'Unblock maple');
    r.emit('msgs', { t: 'msgs', n: 0, rows: [conv(H1, 'maple')] });
    check('a list that says not blocked is believed', byClass(one(r.panel(), 'pm-menu') || r.menu(), 'pm-mi')[0].textContent === 'Block maple');
    r.key('Escape');
    check('Escape closes the menu first and the focus goes to its button, the panel stays', !one(r.panel(), 'pm-menu') && !!r.shade() && r.doc.activeElement === one(r.panel(), 'pm-more'));
    // delete
    let d = byClass(r.menu(), 'pm-mi')[1];
    d.click();
    check('Delete asks once more: the first press changes the words and sends nothing', !r.frames('dmdel').length && byClass(r.panel(), 'pm-mi')[1].textContent === 'Press again to delete it');
    r.clock.advance(4100);
    check('...and the question goes away after 4 seconds', byClass(r.panel(), 'pm-mi')[1].textContent === 'Delete this conversation');
    byClass(r.panel(), 'pm-mi')[1].click();
    byClass(r.panel(), 'pm-mi')[1].click();
    same('the second press sends dmdel with the handle, exactly', r.frames('dmdel'), [{ t: 'dmdel', h: H1 }]);
    check('...the panel is closed, the conversation is not in the list and the bell is asked for its Messages tab', r.part.state() === 'closed' && !r.shade() && r.part.convs().length === 0 && r.part.count() === 0 && r.tally.toList === 1);
    r.link.open = false;
    check('nothing went wrong in any of it', !r.errors.length, r.errors.map(String).join());
    const o = rig().openThread(H1, 'maple', [theirs(1, 'one')]);
    o.link.open = false;
    byClass(o.menu(), 'pm-mi')[1].click(); byClass(o.panel(), 'pm-mi')[1].click();
    check('a delete that cannot be sent keeps the conversation and says so', o.part.state() === 'thread' && o.said.length === 1 && o.said[0][0] === 'Chat is offline');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 9. the report
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig().openThread(H1, 'maple', [theirs(1, 'first'), theirs(2, 'come on, answer me'), mine(3, 'no')]);
    const rep = byClass(r.bubbles()[1], 'pm-rep')[0];
    rep.focus(); rep.click();
    const c = r.card();
    check('Report opens the card over the panel: a dialog with the title, the message and the three reasons', !!c && c.getAttribute('role') === 'dialog' && texts(c, 'pm-ct')[0] === 'Report this message' && texts(c, 'pm-quote')[0] === 'come on, answer me' && !!r.panel());
    same('the reasons, in words, and the first is chosen', [texts(c, 'pm-opt'), byClass(c, 'pm-opt').map((l) => l.children[0].checked)], [['Harassment or threats', 'Spam or links', 'Something else'], [true, false, false]]);
    same('the card says what the admins see, and that the person is not told', texts(c, 'pm-fine'), ['The admins will see this message and the 5 before it in this conversation, with both names. Nothing else of your messages. maple is not told who reported.']);
    check('the card is named by its title, and the focus is inside it', c.getAttribute('aria-labelledby') === 'pm-card-title' && one(c, 'pm-ct').id === 'pm-card-title' && c.contains(r.doc.activeElement));
    byClass(c, 'pm-opt')[1].children[0].checked = true;
    one(c, 'pm-go').click();
    same('Send report sends the exact frame with the number of that message and the reason that was chosen', r.frames('dmreport'), [{ t: 'dmreport', h: H1, id: 2, why: 'spam' }]);
    one(c, 'pm-go').click();
    check('...once while it waits, and the card stays until the hub answers', r.frames('dmreport').length === 1 && !!r.card() && one(c, 'pm-go').getAttribute('aria-busy') === 'true');
    r.emit('dmreported', { t: 'dmreported', h: H1, id: 99 });
    check('an answer about another message is not this one', !!r.card());
    r.emit('dmreported', { t: 'dmreported', h: H1, id: 2 });
    check('the answer closes the card, says thanks and puts the focus back on the Report button that opened it', !r.card() && r.said.length === 1 && r.said[0][0] === 'Thanks: the admins will look at it.' && r.doc.activeElement === rep);
    // a refusal, Cancel, Escape
    byClass(r.bubbles()[0], 'pm-rep')[0].click();
    one(r.card(), 'pm-go').click();
    r.emit('dmx', { t: 'dmx', h: H1, why: 'rate' });
    same('a refusal closes the card and says the words as a warning', [!!r.card(), r.said[1]], [false, ['Slow down a little', 'warn']]);
    byClass(r.bubbles()[0], 'pm-rep')[0].click();
    one(r.card(), 'pm-no').click();
    check('Cancel closes it, sends nothing more', !r.card() && r.frames('dmreport').length === 2 && !!r.shade());
    byClass(r.bubbles()[0], 'pm-rep')[0].click();
    const esc = r.key('Escape');
    check('Escape closes the card first and the panel stays', esc.stopped === true && !r.card() && !!r.shade());
    r.key('Escape');
    check('...and the next Escape closes the panel', !r.shade());
    r.open(H1, 'maple');
    r.emit('thread', thread(H1, 'maple', [theirs(1, 'first')]));
    byClass(r.bubbles()[0], 'pm-rep')[0].click();
    one(r.card(), 'pm-go').click();
    r.clock.advance(8100);
    check('a report that gets no answer in 8 seconds closes the card and says so', !r.card() && r.said[r.said.length - 1][0] === 'That did not go through, try again');
    r.part.close();
    check('close() leaves no card and no panel', !r.card() && !r.shade());
    check('a report from a hub that is not there is said, and the card stays for another try', (() => {
        const q = rig().openThread(H1, 'maple', [theirs(1, 'first')]);
        byClass(q.bubbles()[0], 'pm-rep')[0].click();
        q.link.open = false;
        one(q.card(), 'pm-go').click();
        return !!q.card() && q.said[0][0] === 'Chat is offline' && !one(q.card(), 'pm-go').getAttribute('aria-busy');
    })());
    // report(h): from the person's sheet
    const s = rig().live().list([conv(H1, 'maple')]);
    check('report(h) asks the hub for the conversation and shows no card yet', s.part.report(H1, 'maple') === true && s.frames('dmopen').length === 1 && !s.card() && !s.shade());
    s.emit('thread', thread(H1, 'maple', [theirs(1, 'old'), theirs(2, 'newest of theirs'), mine(3, 'my reply')]));
    check('...and when it comes the card is of the newest message of THEIRS (not of the visitor\'s own), with no panel', !!s.card() && texts(s.card(), 'pm-quote')[0] === 'newest of theirs' && !s.shade());
    one(s.card(), 'pm-go').click();
    same('...and sends that message\'s number', s.frames('dmreport'), [{ t: 'dmreport', h: H1, id: 2, why: 'abuse' }]);
    s.emit('dmreported', { t: 'dmreported', h: H1, id: 2 });
    check('...and the answer closes it', !s.card() && s.said[0][0] === 'Thanks: the admins will look at it.');
    const n = rig().live();
    n.part.report(H2, 'fern');
    n.emit('thread', thread(H2, 'fern', [mine(1, 'hello?')]));
    same('a person who wrote nothing to the visitor: no card, the words as a warning', [!!n.card(), n.said], [false, [['fern has not sent you a private message, so there is nothing to report.', 'warn']]]);
    n.emit('thread', thread(H2, 'fern', [theirs(1, 'late')]));
    check('a thread that comes later (not asked for) opens no card', !n.card());
    const e = rig().live();
    e.part.report(H2, 'fern');
    e.emit('dmx', { t: 'dmx', h: H2, why: 'who' });
    e.emit('thread', thread(H2, 'fern', [theirs(1, 'late')]));
    same('a refusal ends the wait: said as a warning, and no card for a thread that comes after', [!!e.card(), e.said], [false, [['That person is not available', 'warn']]]);
    const t = rig().live();
    t.part.report(H2, 'fern');
    t.clock.advance(8100);
    t.emit('thread', thread(H2, 'fern', [theirs(1, 'late')]));
    same('a thread that never comes is said after 8 seconds and opens no card later', [!!t.card(), t.said], [false, [['That did not go through, try again', 'warn']]]);
    const bad = rig().live();
    check('report with a handle that is not one does nothing', bad.part.report('nope', 'x') === false && !bad.sent.length);
    const off = rig().live();
    off.link.open = false;
    check('report with a hub that is not there says so', off.part.report(H1, 'maple') === false && off.said.length === 1 && off.said[0][0] === 'Chat is offline');
    check('a page whose toast is missing writes to the chat\'s own line instead', (() => { const q = rig({ noSay: true }).live(); q.part.report(H1, 'maple'); q.emit('thread', thread(H1, 'maple', [mine(1, 'x')])); return q.noted.length === 1 && /nothing to report/.test(q.noted[0]); })());
    check('nothing went wrong in any of it', ![r, s, n, e, t].some((x) => x.errors.length), [r, s, n, e, t].map((x) => x.errors.map(String).join()).join());
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 10. what the hub may send, the keys, a new welcome
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig().live().list([conv(H1, 'maple', { u: 1 })]);
    const before = JSON.stringify([r.part.count(), r.part.state()]);
    r.emit('msgs', { t: 'msgs', n: 1, rows: Array.from({ length: 51 }, () => conv(H2, 'x')) });
    r.emit('msgs', null); r.emit('msgs', { rows: 'x' }); r.emit('msgs');
    r.emit('msg', { t: 'msg', h: 'bad', name: 'x', id: 1, at: 1, text: 't', me: 0, u: 1, f: 0 });
    r.emit('msg', { t: 'msg', h: H2, name: 'x', id: 'one', at: 1, text: 't', me: 0, u: 1, f: 0 });
    r.emit('msg', { t: 'msg', h: H2, name: 'x', id: 1, at: 1, text: '', me: 0, u: 1, f: 0 });
    r.emit('msg', { t: 'msg', h: H2, name: 'x', id: 1, at: 1, text: 't', me: 0, f: 0 });
    r.emit('msg', null); r.emit('thread', null); r.emit('thread', { h: H1 }); r.emit('dmx', null); r.emit('dmx', { h: 7, why: 'x' }); r.emit('dmreported', null);
    r.emit('nonsense', { t: 'nonsense' }); r.emit('who', { t: 'who' }); r.emit('alerts', { t: 'alerts', rows: [] });
    check('frames that are wrong in any part, and kinds that are not its own, change nothing and throw nothing', JSON.stringify([r.part.count(), r.part.state()]) === before && !r.errors.length && r.said.length === 0, r.errors.map(String).join());
    r.list([conv(H3, '<img src=x onerror=alert(1)>' + 'z'.repeat(60), { text: '<b>bold</b>' + 'w'.repeat(900) })]);
    const hostile = r.part.convs()[0];
    check('a hostile name and text are cut in the list that the bell draws, and are only text', r.part.convs().length === 1 && Chat.points(hostile.name) <= 33 && hostile.name.indexOf('<img src=x onerror=alert(1)>') === 0 && Chat.points(hostile.text) === 501 && r.env.innerHTMLWrites.length === 0);      // (500 code points and the dots)
    r.open(H3, hostile.name);
    check('...and in the panel (the name that the bell gave)', Chat.points(r.names()[0]) <= 33 && r.names()[0].indexOf('<img src=x onerror=alert(1)>') === 0 && !r.panel().findAll((e) => ['img', 'b', 'script'].indexOf(e.tagName) !== -1).length && r.env.innerHTMLWrites.length === 0);
    r.emit('thread', thread(H3, 'x', Array.from({ length: 100 }, (_, i) => theirs(i + 1, '<i>' + i + '</i>'))));
    check('100 messages are drawn, as text', r.bubbles().length === 100 && one(r.bubbles()[0], 'pm-mt').textContent === '<i>0</i>' && !r.panel().findAll((e) => e.tagName === 'i').length);
    r.emit('msg', { t: 'msg', h: H3, name: 'x', id: 101, at: NOW, text: 'the 101st', me: 0, u: 1, f: 0 });
    check('a 101st message keeps 100 on the screen: the oldest goes', r.bubbles().length === 100 && one(r.bubbles()[99], 'pm-mt').textContent === 'the 101st' && one(r.bubbles()[0], 'pm-mt').textContent === '<i>1</i>');
    // a new welcome
    r.live();
    same('a new welcome (a new socket) clears what was known and asks for the conversation on the screen again', [r.part.count(), r.part.state(), r.frames('dmopen').length], [0, 'thread', 2]);
    check('...with the name that was shown, until the thread comes', r.names().indexOf('x') !== -1 && /Loading/.test(one(r.panel(), 'pm-empty').textContent));
    r.live(null);
    check('a welcome that names nobody (a log out) closes the panel and takes the conversations away', !r.shade() && r.part.ready() === false && r.part.convs().length === 0 && r.part.state() === 'off');
    r.live();
    r.emit('you', { t: 'you', you: { name: 'hazel', mod: true, role: 'mod' } });
    r.emit('you', { t: 'you', you: null });
    check('a change of role changes nothing here, and a you that names nobody is ignored', r.part.ready() === true);
    // the keys
    const k = rig().openThread(H1, 'maple', [theirs(1, 'first')]);
    byClass(k.bubbles()[0], 'pm-rep')[0].click();
    const inCard = k.card().findAll((e) => e.tagName === 'button' || e.tagName === 'input');
    inCard[inCard.length - 1].focus(); const t1 = k.key('Tab');
    check('Tab on the last control of the card goes to the first, and stays in the card', k.doc.activeElement === inCard[0] && t1.stopped === true);
    const t2 = k.key('Tab', { shiftKey: true });
    check('Shift+Tab on the first goes to the last', k.doc.activeElement === inCard[inCard.length - 1] && t2.stopped === true);
    k.key('Escape');
    const all = k.panel().findAll((e) => (e.tagName === 'button' || e.tagName === 'input') && !e.hidden);
    all[all.length - 1].focus(); k.key('Tab');
    check('Tab on the last control of the panel goes to the first, and stays in the panel', k.doc.activeElement === all[0]);
    k.key('Tab', { shiftKey: true });
    check('Shift+Tab on the first goes to the last', k.doc.activeElement === all[all.length - 1]);
    const other = k.key('a');
    check('another key is nobody\'s here', other.stopped !== true);
    k.part.close();
    const q = rig().live();
    const idle = q.key('Escape');
    check('with nothing open Escape and Tab are left alone', idle.stopped !== true && q.key('Tab').stopped !== true);
    const ph = rig({ phone: true }).live().list([conv(H1, 'maple')]);
    ph.open(H1, 'maple');
    ph.emit('thread', thread(H1, 'maple', [theirs(1, 'first')]));
    byClass(ph.menu(), 'pm-mi')[0].click(); ph.calls[0].done('reply', { t: 'reply', c: 'ignore', r: 'ok' });
    check('on a phone the answer of Block is said under the field (the toast is under the sheet), not as a toast', ph.status() === 'maple is blocked: you get no more messages from them.' && ph.said.length === 0);
    byClass(ph.bubbles()[0], 'pm-rep')[0].click(); one(ph.card(), 'pm-go').click(); ph.emit('dmreported', { t: 'dmreported', h: H1, id: 1 });
    check('...and so is the thanks of a report, once the card is gone', ph.status() === 'Thanks: the admins will look at it.' && ph.said.length === 0 && !ph.card());
    check('a conversation on a phone puts the focus on the way back (the keyboard stays down), on a wide window in the field', ph.doc.activeElement === one(ph.panel(), 'pm-back') && (() => { const w = rig().openThread(H1, 'maple', []); return w.doc.activeElement === w.field(); })());
    const nw = rig({ noWhen: true }).live().list([conv(H1, 'maple', { at: NOW - 120 })]);
    nw.open(H1, 'maple'); nw.emit('thread', thread(H1, 'maple', [theirs(1, 'a while ago', NOW - 120)]));
    check('a dock without its own words for a time gives "N min ago" under a message', texts(nw.panel(), 'pm-tm')[0] === '' && texts(nw.panel(), 'pm-day')[0] === '2 min ago' && !nw.errors.length, JSON.stringify([texts(nw.panel(), 'pm-tm'), texts(nw.panel(), 'pm-day')]));
    check('nothing went wrong in any of it', ![r, k, q, ph, nw].some((x) => x.errors.length), [r, k, q, ph, nw].map((x) => x.errors.map(String).join()).join());
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 11. the rules that the review of the mutants asked for: an own message is not read, a message of theirs does not end a send that waits, a typed /msg opens the conversation
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig().openThread(H1, 'maple', [theirs(1, 'first')]);
    r.sent.length = 0;
    r.type('hello'); one(r.panel(), 'pm-send').click();
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 7, at: NOW, text: 'hello', me: 1, u: 0, f: 0 });
    same('the echo of an own message in the conversation that is on the screen is drawn and is not read to the hub (nothing was unread)', [r.bubbles().length, r.frames('dmread')], [2, []]);
    r.type('second'); one(r.panel(), 'pm-send').click();
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 8, at: NOW, text: 'a word from them', me: 0, u: 1, f: 0 });
    check('a message of theirs that comes in while a send waits does not end it: the field keeps the text, the button waits, and the message is read', r.field().value === 'second' && one(r.panel(), 'pm-send').getAttribute('aria-busy') === 'true' && r.frames('dmread').length === 1);
    r.emit('msg', { t: 'msg', h: H1, name: 'maple', id: 9, at: NOW, text: 'second', me: 1, u: 0, f: 0 });
    check('...and the echo of the sent message does: the field is empty and the button is free', r.field().value === '' && one(r.panel(), 'pm-send').getAttribute('aria-busy') === null);
}
{
    const r = rig().live().list([]);
    r.emit('open', { t: 'reply', c: 'msg', r: 'open', h: H2, to: 'tess' });
    same('a typed /msg that the hub opened: the panel is on that conversation (named until the hub sends it), and the hub is asked for it', [!!r.panel(), r.part.state(), r.frames('dmopen'), texts(r.panel(), 'pm-nm')], [true, 'thread', [{ t: 'dmopen', h: H2 }], ['tess']]);
    r.part.close(); r.sent.length = 0;
    r.emit('open', { t: 'reply', c: 'msg', r: 'open', h: 'nope', to: 'tess' });
    r.emit('open', { t: 'reply', c: 'msg', r: 'open', to: 'tess' });
    check('...an answer with no handle that the hub could have made opens nothing', !r.panel() && r.frames('dmopen').length === 0);
    const g = rig().live(null);
    g.emit('open', { t: 'reply', c: 'msg', r: 'open', h: H2, to: 'tess' });
    check('...and a guest is never shown it', !g.panel());
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 12. a phone: the sheet is the visible part of the window (a keyboard takes the bottom of it), the field stays in view, and X, Back, a pull down, Escape and a press outside all close the whole sheet
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({ phone: true, vv: true }).openThread(H1, 'maple', [theirs(1, 'first'), mine(2, 'second')]);
    const sh = () => r.shade();
    same('on a phone the sheet is exactly the visible part of the window: from its top, as high as it', [sh().style.top, sh().style.height, cls(sh()).indexOf('kb') !== -1], ['0px', '844px', false]);
    r.win.resizeVisual(430, 0);
    same('a keyboard (the visible part is 430 px high) makes the sheet 430 px high, with the room for the messages: the class that the style reads', [sh().style.height, sh().style.top, cls(sh()).indexOf('kb') !== -1], ['430px', '0px', true]);
    r.win.panVisual(120);
    same('...the page that is panned under the keyboard moves the sheet with the visible part', [sh().style.top, sh().style.height], ['120px', '430px']);
    r.win.resizeVisual(844, 0);
    same('...and a keyboard that goes gives the sheet its height back', [sh().style.top, sh().style.height, cls(sh()).indexOf('kb') !== -1], ['0px', '844px', false]);
    const seenInView = [];
    r.field().scrollIntoView = (o) => seenInView.push(o);
    r.field().focus(); r.field().fire('focus', {});
    check('the field with the focus makes room at once (the keyboard comes a moment later)', cls(sh()).indexOf('kb') !== -1);
    r.clock.advance(300);
    same('...and when the keyboard is there the field is brought into view', seenInView, [{ block: 'nearest' }]);
    r.win.resizeVisual(430, 0);
    r.field().blur(); r.clock.advance(400);
    check('...a field that lost the focus with the keyboard still up keeps the sheet at the visible part', sh().style.height === '430px' && cls(sh()).indexOf('kb') !== -1);
    r.win.resizeVisual(844, 0);
    r.part.close();
    check('the sheet that is closed stops listening to the viewport: nothing is left on it', (r.win.visualViewport.listeners.resize || []).length === 0 && (r.win.visualViewport.listeners.scroll || []).length === 0);
    const w = rig({ vv: true }).openThread(H1, 'maple', []);
    check('in a wide window the sheet is not sized by the script (the panel has its own place)', w.shade().style.top === '' && w.shade().style.height === '' && cls(w.shade()).indexOf('kb') === -1);
    const n = rig({ phone: true }).openThread(H1, 'maple', []);
    check('a browser with no visual viewport: the sheet is the window\'s height', n.shade().style.height === '844px' && n.shade().style.top === '0px');
}
{
    const open = () => rig({ phone: true, vv: true }).openThread(H1, 'maple', [theirs(1, 'first')]);
    const closed = (r, what) => { check(what + ': the sheet is gone (not the list, not another panel), the page is as it was', r.part.state() === 'closed' && !r.shade() && byClass(r.doc.body, 'pm-shade').length === 0 && r.entries.length === 1 && r.doc.activeElement === r.from); };
    let r = open();
    check('opening a conversation makes one entry of its own in the history (the page\'s own state is kept in it)', r.entries.length === 2 && r.entries[1].pm === 1 && r.entries[1].ants === 'home');
    one(r.panel(), 'pm-x').click();
    closed(r, 'X');
    r = open();
    one(r.panel(), 'pm-back').click();
    check('the arrow closes the sheet, leaves no entry of its own in the history and asks the bell for its Messages tab', r.part.state() === 'closed' && !r.shade() && r.entries.length === 1 && r.tally.toList === 1);
    r = open(); r.key('Escape');
    closed(r, 'Escape');
    r = open(); r.shade().click();
    closed(r, 'a press outside');
    r = open(); r.win.history.back();
    check('the phone\'s Back button closes the sheet and leaves the page where it was', r.part.state() === 'closed' && !r.shade() && r.entries.length === 1 && r.doc.activeElement === r.from);
    r = open();
    r.menu();
    r.win.history.back();
    check('Back with the menu open closes the menu first: the sheet stays, with an entry again', r.part.state() === 'thread' && !!r.panel() && !one(r.panel(), 'pm-menu') && r.entries.length === 2);
    r.win.history.back();
    check('...and the next Back closes the sheet', r.part.state() === 'closed' && r.entries.length === 1);
    r = open();
    r.emit('thread', thread(H1, 'maple', [theirs(1, 'first')]));
    one(r.panel(), 'pm-rep').click();
    check('the report card is up', !!r.card());
    r.win.history.back();
    check('Back with the report card up closes the card first: the sheet stays, with an entry again', !r.card() && r.part.state() === 'thread' && r.entries.length === 2);
    r.win.history.back();
    r.win.history.back();
    check('...and a Back that comes when nothing is open does nothing (the page is not touched)', r.part.state() === 'closed' && r.entries.length === 1);
    r = open(); one(r.panel(), 'pm-x').click(); r.part.open(H1, 'maple');
    check('a sheet that opens again after a close has one entry again, not two', r.entries.length === 2);
}
{
    const open = () => rig({ phone: true, vv: true }).openThread(H1, 'maple', [theirs(1, 'first')]);
    const down = (z, y, extra) => z.fire('pointerdown', Object.assign({ pointerType: 'touch', pointerId: 7, clientY: y, target: z }, extra));
    const move = (z, y) => z.fire('pointermove', { pointerType: 'touch', pointerId: 7, clientY: y });
    const up = (z) => z.fire('pointerup', { pointerType: 'touch', pointerId: 7 });
    let r = open(), grab = one(r.panel(), 'pm-grab');
    down(grab, 100); move(grab, 150);
    check('a finger on the grip pulls the sheet down with it', r.panel().style.transform === 'translateY(50px)');
    up(grab);
    check('...a short pull lets it go back to its place and does not close it', r.panel().style.transform === '' && r.part.state() === 'thread');
    r = open(); grab = one(r.panel(), 'pm-grab');
    down(grab, 100); move(grab, 260); up(grab);
    check('a long pull (80 px or more) closes the sheet, and the page is as it was', r.part.state() === 'closed' && !r.shade() && r.entries.length === 1 && r.doc.activeElement === r.from);
    r = open(); const top = one(r.panel(), 'pm-top');
    down(top, 100, { target: one(top, 'pm-who') }); move(top, 300); up(top);
    check('the head of the sheet pulls it down too', r.part.state() === 'closed');
    r = open(); const x = one(r.panel(), 'pm-x'), head = one(r.panel(), 'pm-top');
    down(head, 100, { target: x }); move(head, 400); up(head);
    check('a press that starts on a button of the head is a press, not a pull', r.part.state() === 'thread' && r.panel().style.transform === '');
    down(head, 100, { pointerType: 'mouse' }); move(head, 400); up(head);
    check('a mouse does not pull the sheet (it has the X)', r.part.state() === 'thread');
    down(head, 100); move(head, 400); head.fire('pointercancel', {});
    check('a pull that is cancelled (the browser took the touch) closes nothing and the sheet is back in its place', r.part.state() === 'thread' && r.panel().style.transform === '');
    move(head, 500); up(head);
    check('a move or a lift with no press before it does nothing', r.part.state() === 'thread');
}

console.log(failures ? failures + ' of ' + checks + ' checks FAILED' : 'all ' + checks + ' checks hold');
process.exit(failures ? 1 : 0);
