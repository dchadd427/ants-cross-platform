// Runs the chat dock of Home (web/home/chat.js, docs/HOME_SCREEN.md "The chat dock") against a FAKE hub: a fake WebSocket that plays the hub's protocol (docs/HUB.md) and a fake clock, in the fake browser of
// tests/scripts/web_page_harness.js (its DOM). The dock is made by the page's real code on the real page in section 5; the rest runs it alone, so that every case is one line of the script:
//   1. the pure parts: the text cleaned for display, the cut, the frame, the line, the words of the err codes, the backoff, the address;
//   2. what the dock shows: read-only (login:false), logged out (login:true, you null), a writer (you), the history, the lines, the mod mark, the online count, who may type;
//   3. what the hub may send: unknown kinds, garbage, huge frames, a hostile line (markup, bidi, long), a burst of 1000 lines, del, err in every code, a second welcome, another version;
//   4. the link: offline, "reconnecting…", the backoff (1 s to 60 s plus jitter), a hub that opens and closes at once, a socket that never opens or never speaks, nothing sent while offline, the ping, the visible tab,
//      the page that goes away and comes back, no WebSocket at all or a constructor that throws;
//   5. what the visitor does and sees: Send and its pace, the fold and the strip, unread, the Room, the keys and the focus;
//   6. the page (web/lobby.html with web/home/home.js): the dock is loaded after the first paint, only when Home is shown, a broken or missing dock changes nothing, START still hands off;
//   7. the source of the script: no markup, no storage, no link, no other site.
// tests/scripts/test_web_home_chat.py runs this with node (the quick tier). usage: node web_home_chat_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
globalThis.AntsShapes = require(path.resolve(__dirname, '..', '..', 'web', 'shapes.js'));

const lobbyPath = process.argv[2];
if (!lobbyPath) { console.log('usage: web_home_chat_check.js lobby.html'); process.exit(2); }
const lobbyDir = path.dirname(path.resolve(lobbyPath));
const chatPath = path.join(lobbyDir, 'home', 'chat.js');
const chatSource = fs.readFileSync(chatPath, 'utf8');
const Pure = require(chatPath);
const H = require('./web_page_harness.js')(lobbyPath);
const { El, runLobby, KEY, E, C, roomMessage } = H;

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
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const shows = (e) => { for (let x = e; x; x = x.parent) if (x.hidden) return false; return true; };                  // (an element is on the screen when it and every box around it is not hidden)
const buttonsShown = (root) => root.findAll((e) => e.tagName === 'button' && shows(e)).map((e) => e.textContent || e.getAttribute('aria-label'));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the fakes: a clock, a hub's socket, the page that the dock is mounted into
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function makeClock() {
    const c = { t: 0, timers: [], next: 1 };
    c.setTimeout = (fn, ms) => { const id = c.next++; c.timers.push({ id, at: c.t + (ms || 0), fn }); return id; };
    c.clearTimeout = (id) => { c.timers = c.timers.filter((x) => x.id !== id); };
    c.advance = (ms) => {
        const until = c.t + ms;
        for (let turns = 0; ; turns++) {
            if (turns > 20000) throw new Error('a storm of timers: the code asks for the next one at once, for ever');
            const due = c.timers.filter((x) => x.at <= until).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            c.timers = c.timers.filter((x) => x !== due);
            c.t = Math.max(c.t, due.at);
            due.fn();
        }
        c.t = until;
    };
    c.now = () => c.t;
    return c;
}
function loadChat(clock) {
    const root = {};
    new Function('self', 'setTimeout', 'clearTimeout', chatSource)(root, clock.setTimeout, clock.clearTimeout);
    return root.AntsHomeChat;
}
const cls = (e) => String(e.className).split(/\s+/);
const byClass = (root, c) => root.findAll((e) => cls(e).indexOf(c) !== -1);
const one = (root, c) => byClass(root, c)[0] || null;
const L = (id, who, text, mod) => ({ t: 'line', id, at: 1700000000 + id, who, text, mod: !!mod });
const W = (o) => Object.assign({ t: 'welcome', v: 1, you: null, login: false, online: 7, history: [] }, o);
const HISTORY = [L(1, 'pebble', 'anyone up for Islands?'), L(2, 'clover', 'I have a room open', true), L(3, 'juniper', 'joining!')].map((m) => { const x = Object.assign({}, m); delete x.t; return x; });
const YOU = { name: 'maple.leaf', mod: false };

// one dock on a fake page: o.width and o.height (the window), o.random, o.noWebSocket, o.throws (a constructor that throws), o.hidden
function rig(o) {
    o = o || {};
    const env = { elements: {}, innerHTMLWrites: [] };
    const clock = makeClock();
    const Chat = loadChat(clock);
    const doc = { hidden: false, listeners: {}, createElement: (t) => new El(env, t), addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); } };
    env.doc = doc;                                        // (the fake DOM's focus() looks for it)
    const win = { innerWidth: o.width || 1280, innerHeight: o.height || 800, location: { protocol: 'https:', host: 'play.test' }, listeners: {}, addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); } };
    const sockets = [];
    class HubSocket {
        constructor(url) { if (o.throws) throw new Error('a page that may not open it'); this.url = url; this.sent = []; this.calls = 0; this.readyState = 0; this.closes = 0; sockets.push(this); }
        send(text) { this.calls++; if (this.readyState !== 1) throw new Error('send on a socket that is not open'); this.sent.push(text); if (o.pong !== false && text === '{"t":"ping"}') this.recv({ t: 'online', n: 7 }); }       // (a hub answers a ping with the count, unless o.pong is false)
        close() { this.closes++; this.readyState = 3; }
        open() { this.readyState = 1; if (this.onopen) this.onopen({}); return this; }
        recv(x) { if (this.onmessage) this.onmessage({ data: typeof x === 'string' || typeof x !== 'object' || x instanceof ArrayBuffer ? x : JSON.stringify(x) }); return this; }
        fail() { this.readyState = 3; if (this.onerror) this.onerror({}); if (this.onclose) this.onclose({ code: 1006 }); return this; }
    }
    const host = new El(env, 'div', 'home');
    const errors = [];
    const hidden = (on) => { doc.hidden = on; (doc.listeners.visibilitychange || []).forEach((fn) => fn({})); };
    doc.hidden = !!o.hidden;
    // the login: the bar's chip (a hidden button, as home.js makes it), where a press on Log in goes, and the page's fetch (a thenable that the test answers by hand: r.answer(response), r.refuse())
    const went = [], fetches = [];
    let chip = null, pending = null;
    if (o.chip) { chip = new El(env, 'button', ''); chip.className = 'btn hlogin'; chip.hidden = true; chip.textContent = 'Log in'; }
    win.assign = (url) => went.push(url);
    win.location.assign = (url) => went.push(url);
    const fetcher = o.noFetch ? undefined : (path, init) => { fetches.push({ path, init }); if (o.fetchThrows) throw new Error('no network'); return { then(ok, bad) { pending = { ok, bad }; } }; };
    const dock = Chat.mount({ doc, win, host, chip, screen: o.screen, fetch: fetcher, now: clock.now, random: o.random || (() => 0), WebSocket: o.noWebSocket ? undefined : HubSocket, error: (e) => errors.push(e) });
    if (o.home !== false) dock.shown(true);                // (the page tells the dock which screen shows, right after it is mounted)
    const r = { env, clock, Chat, doc, win, host, sockets, dock, errors, hidden, HubSocket, root: dock.root, chip, went, fetches };
    r.answer = (response) => { const p = pending; pending = null; p.ok(response); };
    r.refuse = () => { const p = pending; pending = null; p.bad(new Error('the network')); };
    r.pending = () => pending !== null;
    r.s = () => sockets[sockets.length - 1];
    r.log = () => one(dock.root, 'gc-log');
    r.rows = () => r.log().children.slice(1);
    r.rowTexts = () => r.rows().map((x) => x.textContent);
    r.note = () => one(dock.root, 'gc-note').textContent;
    r.pill = () => { const p = one(dock.root, 'gc-unread'); return p.hidden ? '' : p.textContent; };
    r.on = () => one(dock.root, 'gc-on').textContent;
    r.peek = () => one(dock.root, 'gc-peek').textContent;
    r.fold = () => one(dock.root, 'gc-fold');
    r.form = () => one(dock.root, 'gc-form');
    r.field = () => one(dock.root, 'gc-input');
    r.live = (welcome) => { r.s().open().recv(welcome || W({})); return r.s(); };      // (the newest socket opens and the hub welcomes it)
    r.submit = (text) => { r.field().value = text; r.form().fire('submit', { preventDefault() {} }); };
    r.said = () => r.sockets.reduce((all, s) => all.concat(s.sent.map((x) => JSON.parse(x)).filter((m) => m.t === 'say')), []);
    r.pings = () => r.sockets.reduce((n, s) => n + s.sent.filter((x) => x === '{"t":"ping"}').length, 0);
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The pure parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Pure;
    same('plain: controls, bidirectional and zero-width characters out, blanks into one space, the ends trimmed', P.plain('  a\tb\n\u202e c\u200b d\u2066e\u2069 \u0007f \ufeff '), 'a b c de f');
    same('plain: markup stays text (it is shown with textContent), a pair of surrogates stays, a lone one goes', [P.plain('<b>x</b> <img src=x onerror=alert(1)>'), P.plain('a\ud83d\ude00b'), P.plain('a\ud83db'), P.plain('a\ude00b')], ['<b>x</b> <img src=x onerror=alert(1)>', 'a\ud83d\ude00b', 'ab', 'ab']);
    same('plain: the invisible characters of the hub\'s own list go from a line (soft hyphen, fillers, the braille blank, tag characters, the joiner of the grapheme, the Khmer vowels), and the variation selector of an emoji stays', [P.plain('a\u00adb\u3164c\u2800d\u115fe\u1160f\uffa0g\u034fh\u17b4i\u17b5j\ud834\udd59k\udb40\udc41l'), P.plain('\u2764\ufe0f x'), P.plain('\u180ea\u180bb')], ['abcdefghijkl', '\u2764\ufe0f x', 'a\u180bb']);
    same('plain, for a name (strict): the variation selectors go too, the Mongolian ones and those of the supplement', [P.plain('a\ufe0fb\u180bc\u180dd\u180fe\udb40\udd00f\udb40\uddefg', true), P.plain('\ufe00\ufe0f\u180b', true)], ['abcdefg', '']);
    same('plain: a run of lone surrogates leaves nothing behind (low ones, high ones, one after a pair)', [P.plain('x\udc00\udc00y'), P.plain('x\ud800\ud800z'), P.plain('\udc00\udc00\udc00'), P.plain('a\ud83d\ude00\udc00\udc00b'), P.plain('\ud83d\ud83d\ude00')], ['xy', 'xz', '', 'a\ud83d\ude00b', '\ud83d\ude00']);
    same('points and clip count code points, and never cut a pair', [P.points('a\ud83d\ude00b'), P.clip('abcdef', 3), P.clip('abc', 3), P.clip('\ud83d\ude00\ud83d\ude00\ud83d\ude00', 2), P.clip('', 5)], [3, 'abc…', 'abc', '\ud83d\ude00\ud83d\ude00…', '']);
    check('parse: an object with a string "t" only; not a string, too big, not JSON, an array, a number, null, no "t", a "t" that is not a string are all null',
        P.parse('{"t":"x"}') !== null && P.parse(5) === null && P.parse(new ArrayBuffer(4)) === null && P.parse('x'.repeat(P.LIMIT.frame + 1)) === null && P.parse('{') === null && P.parse('[1]') === null && P.parse('3') === null
        && P.parse('null') === null && P.parse('{}') === null && P.parse('{"t":5}') === null && P.parse('{"t":"a","__proto__":{"x":1}}') !== null && P.parse('{"t":"ok"}'.padEnd(P.LIMIT.frame, ' ')) !== null);
    same('count: whole numbers from 0 only', [P.count(0), P.count(7), P.count(-1), P.count(1.5), P.count('3'), P.count(null), P.count(NaN), P.count(Infinity), P.count(1e12)], [0, 7, null, null, null, null, null, null, null]);
    same('lineOf: a good line is cleaned, the mod flag is a boolean true only', [P.lineOf({ id: 4, who: 'a', text: ' hi  there ', mod: true }), P.lineOf({ id: 4, who: 'a', text: 'x', mod: 'yes' }).mod, P.lineOf({ id: 0, who: 'a', text: 'x' }).id], [{ id: 4, who: 'a', text: 'hi there', mod: true }, false, 0]);
    same('lineOf: not a line when the id is not a whole number, the name or text is not a string, or nothing is left of the text',
        [{ who: 'a', text: 'x' }, { id: '4', who: 'a', text: 'x' }, { id: -1, who: 'a', text: 'x' }, { id: 1.5, who: 'a', text: 'x' }, { id: 4, who: 5, text: 'x' }, { id: 4, who: 'a', text: 5 }, { id: 4, who: 'a', text: ' \u202e \u200b ' }, null, 'x', []].map((m) => P.lineOf(m)),
        [null, null, null, null, null, null, null, null, null, null]);
    same('lineOf: a name of nothing that shows (fillers, variation selectors, tag characters) is "?", a line of marks only is no line, an emoji with its selector is one', [P.lineOf({ id: 1, who: '\u3164\u2800\ufe0f\u180b\udb40\udc41', text: 'x' }).who, P.lineOf({ id: 1, who: 'a', text: '\ufe0f\u0301 ' }), P.lineOf({ id: 1, who: 'a', text: '\u3164\u2800' }), P.lineOf({ id: 1, who: 'a', text: '\u2764\ufe0f' }).text, P.lineOf({ id: 1, who: 'a', text: 'e\u0301' }).text], ['?', null, null, '\u2764\ufe0f', 'e\u0301']);
    same('lineOf: a name over 32 and a line over 200 code points are cut with "…"; a name with nothing left is "?"', [P.lineOf({ id: 1, who: 'n'.repeat(60), text: 't'.repeat(5000) }).who, P.lineOf({ id: 1, who: 'n'.repeat(60), text: 't'.repeat(5000) }).text.length, P.lineOf({ id: 1, who: '\u202e', text: 'x' }).who],
        ['n'.repeat(32) + '…', 201, '?']);
    same('wordOf: the words of the codes; "log in" only where the hub has a login; an unknown code, or a name of Object, is the generic word', [P.wordOf('rate', true), P.wordOf('login', true), P.wordOf('login', false), P.wordOf('muted', false), P.wordOf('banned'), P.wordOf('long'), P.wordOf('empty'), P.wordOf('young'), P.wordOf('bad'),
        P.wordOf('what'), P.wordOf(undefined), P.wordOf(5), P.wordOf('constructor'), P.wordOf('__proto__'), P.wordOf('toString'), P.wordOf('hasOwnProperty')],
        ['Slow down a little', 'Log in with Discord to chat', 'Chat is read-only for now', 'You cannot chat for a while', 'You cannot chat here', 'That line is too long', 'Write something first', 'Your account is too new to chat yet', 'That line could not be sent',
         'That did not go through', 'That did not go through', 'That did not go through', 'That did not go through', 'That did not go through', 'That did not go through', 'That did not go through']);
    same('delayOf at the low end of the spread: half a step, never under 1 s (steps of 1 s doubling to 60 s)', [0, 1, 2, 3, 4, 5, 6, 7, 20, 1000].map((n) => P.delayOf(n, 0)), [1000, 1000, 2000, 4000, 8000, 16000, 30000, 30000, 30000, 30000]);
    same('... at the high end: one and a half steps, never over 60 s', [0, 1, 2, 3, 4, 5, 6, 7, 20, 1000].map((n) => P.delayOf(n, 1)), [1500, 3000, 6000, 12000, 24000, 48000, 60000, 60000, 60000, 60000]);
    same('... in between it is in between, and a random that is no number in 0..1 is the middle of the spread', [P.delayOf(3, 0.5), P.delayOf(0, NaN), P.delayOf(3, 7), P.delayOf(3, undefined), P.delayOf(3, -1)], [8000, 1250, 8000, 8000, 8000]);
    check('... for any step and any random the wait is from 1 s to 60 s, and a new random each time gives waits that differ (no wave)', (() => { const seen = new Set(); for (let n = 0; n < 16; n++) for (let i = 0; i <= 100; i++) { const d = P.delayOf(n, i / 100); if (d < 1000 || d > 60000) return false; if (n === 5) seen.add(d); } return seen.size > 50; })());
    same('addressOf: the page\'s own host, wss on https and ws on http, at /hub/ws', [P.addressOf({ protocol: 'https:', host: 'beta.example' }), P.addressOf({ protocol: 'http:', host: '127.0.0.1:8080' }), P.PATH], ['wss://beta.example/hub/ws', 'ws://127.0.0.1:8080/hub/ws', '/hub/ws']);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. What the dock shows
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{   // read only: the welcome says login:false and nobody is named
    const r = rig();
    const box = r.root;
    check('mounting opens ONE socket, to wss://<the page\'s host>/hub/ws, and sends nothing before the hub has spoken', r.sockets.length === 1 && r.s().url === 'wss://play.test/hub/ws' && r.s().sent.length === 0);
    check('the dock is a section "gchat" named by its heading (an h2, "Global chat"), made of text and properties only', box.tagName === 'section' && box.id === 'gchat' && box.getAttribute('aria-labelledby') === 'gchat-title' && one(box, 'gc-title').tagName === 'h2' && one(box, 'gc-title').id === 'gchat-title'
          && one(box, 'gc-long').textContent === 'Global chat' && one(box, 'gc-short').getAttribute('aria-hidden') === 'true' && r.env.innerHTMLWrites.length === 0);
    check('the log is made again quietly (aria-live off for 300 ms, so that a screen reader is not read the whole history at once) and is role=log, aria-live=polite after; it is labelled and can be focused to scroll it by key; the note is a polite status; the field and Send are labelled',
        r.log().getAttribute('aria-live') === 'off' && (r.clock.advance(300), true) && r.log().getAttribute('role') === 'log' && r.log().getAttribute('aria-live') === 'polite' && r.log().getAttribute('aria-label') === 'Chat messages' && r.log().getAttribute('tabindex') === '0'
        && one(box, 'gc-note').getAttribute('role') === 'status' && one(box, 'gc-note').getAttribute('aria-live') === 'polite' && r.field().getAttribute('aria-label') === 'Say something to everyone' && one(box, 'gc-send').textContent === 'Send' && one(box, 'gc-send').type === 'submit'
        && r.fold().type === 'button' && r.fold().getAttribute('aria-controls') === 'gchat-body' && r.fold().getAttribute('aria-label') === 'Fold chat' && r.fold().getAttribute('aria-expanded') === 'true' && r.field().getAttribute('autocomplete') === 'off');
    check('before the welcome: "connecting…", no form, no online count, no note, the dock is open on a desktop', box.getAttribute('data-state') === 'connecting' && r.on() === 'connecting…' && r.form().hidden === true && r.note() === '' && box.getAttribute('data-open') === 'true' && r.rows().length === 0);
    r.live(W({ login: false, you: null, online: 7, history: HISTORY }));
    check('the welcome shows the history oldest first as "name: text", the mod\'s line marked, and how many are here', eq(r.rowTexts(), ['pebble: anyone up for Islands?', 'mod clover: I have a room open', 'juniper: joining!']) && one(r.rows()[1], 'gc-badge').textContent === 'mod '
          && cls(r.rows()[1]).indexOf('gc-mod') !== -1 && cls(r.rows()[0]).indexOf('gc-mod') === -1 && r.on() === '7 here now' && box.getAttribute('data-state') === 'live');
    check('a name and a line each sit in a <bdi> (the right-to-left letters of one do not reorder the other)', eq(r.rows()[0].children.map((c) => c.tagName), ['bdi', 'span', 'bdi']));
    check('login:false and nobody named: "Chat is read-only for now" and the guest line; NO field, NO Send, NO login button (the only button on the screen is the fold button; the login\'s place is hidden)',
        r.note() === 'Chat is read-only for now' && r.form().hidden === true && one(box, 'gc-guest').hidden === false && one(box, 'gc-guest').textContent === 'You can read as a guest. Playing and watching stay anonymous.'
        && eq(buttonsShown(r.root), ['Fold chat']) && r.dock.slot.hidden === true && r.dock.slot.id === 'gchat-login-slot' && box.getAttribute('data-you') === 'no');
    same('a press on Send that is not there sends nothing: say() says why', [r.dock.say('hi'), r.s().sent.length, r.note()], ['readonly', 0, 'Chat is read-only for now']);
    check('the welcome\'s lines are the past: no unread count', r.pill() === '' && r.errors.length === 0);
}
{   // a hub with a login, a reader who is not logged in: the words ask for the login, and there is still no button
    const r = rig();
    r.live(W({ login: true, you: null }));
    check('login:true and nobody named: the field is off and says "Log in with Discord to chat", Send is gone and a Log in button is in its place (as in the approved picture); the account line is not shown',
          r.form().hidden === false && r.field().disabled === true && r.field().getAttribute('placeholder') === 'Log in with Discord to chat' && r.field().getAttribute('aria-label') === 'Log in with Discord to chat'
          && one(r.root, 'gc-send').hidden === true && one(r.root, 'gc-login').hidden === false && one(r.root, 'gc-login').textContent === 'Log in' && one(r.root, 'gc-login').getAttribute('aria-label') === 'Log in with Discord'
          && r.note() === '' && one(r.root, 'gc-guest').hidden === false && r.dock.slot.hidden === true && buttonsShown(r.root).length === 2);
}
{   // a writer: the welcome names the visitor
    const r = rig();
    r.live(W({ login: true, you: YOU, online: 1, history: HISTORY }));
    check('a named visitor gets the field and Send (and no read-only words); the count says "1 here now"', r.form().hidden === false && r.note() === '' && one(r.root, 'gc-guest').hidden === true && r.on() === '1 here now' && r.root.getAttribute('data-you') === 'yes');
    check('a "you" that is not an object with a name is nobody: no field', [null, 5, 'x', {}, { name: 5 }, { name: '' }, { name: ' \u202e ' }, { name: '\ufe0f\u3164\u180b' }, { name: '\udb40\udc41\u2800' }, []].every((you) => { const q = rig(); q.live(W({ you })); return q.form().hidden === true && q.errors.length === 0; }));
    r.s().recv(L(9, 'maple.leaf', 'my own line'));
    check('the visitor\'s own line is marked (the page\'s gold), and the mod flag of "you" changes nothing else', cls(r.rows()[3]).indexOf('gc-me') !== -1 && r.rowTexts()[3] === 'maple.leaf: my own line');
    r.s().recv({ t: 'online', n: 12 });
    same('online: the count follows the hub; what is no whole number hides it, and the next good one shows it again', [r.on(), (r.s().recv({ t: 'online', n: -4 }), r.on()), (r.s().recv({ t: 'online', n: 1.5 }), r.on()), (r.s().recv({ t: 'online', n: '9' }), r.on()), (r.s().recv({ t: 'online' }), r.on()), (r.s().recv({ t: 'online', n: 3 }), r.on())],
        ['12 here now', '', '', '', '', '3 here now']);
    check('... and the header shows no count of a hub that gave none (the element is hidden)', (r.s().recv({ t: 'online', n: 'many' }), one(r.root, 'gc-online').hidden === true) && (r.s().recv({ t: 'online', n: 2 }), one(r.root, 'gc-online').hidden === false));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. What the hub may send
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{   // kinds that are not known, and noise
    const r = rig();
    r.live(W({ you: YOU, history: HISTORY }));
    const before = r.rowTexts().join('|') + r.note() + r.on();
    ['{"t":"hello"}', '{"t":"WELCOME2"}', '{"t":"say","text":"x"}', '{"t":"ping"}', '{"t":"__proto__"}', '{"t":"constructor","id":3}', '{"t":""}'].forEach((f) => r.s().recv(f));
    check('a message of a kind that is not known is ignored (nothing changes, nothing is thrown, the link stays)', r.rowTexts().join('|') + r.note() + r.on() === before && r.dock.state() === 'live' && r.sockets.length === 1);
    for (let i = 0; i < 29; i++) r.s().recv(['not json', '{', '[1,2]', '"x"', 'null', '{"x":1}', '{"t":5}', '', '\u0000'][i % 9]);
    check('29 frames of noise are ignored, the link stays up and a good frame after them is read', r.dock.state() === 'live' && (r.s().recv(L(20, 'pebble', 'still here')), r.rowTexts().pop() === 'pebble: still here'));
    const big = rig();
    big.live(W({ you: YOU, history: HISTORY }));
    big.s().recv(new ArrayBuffer(8)); big.s().recv(42); big.s().recv(undefined); big.s().recv(null);
    big.s().recv('{"t":"line","id":21,"who":"a","text":"' + 'x'.repeat(130000) + '"}');
    check('a frame that is not text, and a frame of more than 120,000 characters, are noise as well: ignored, and the link stays', big.rowTexts().length === 3 && big.dock.state() === 'live' && big.errors.length === 0);
    big.s().recv('{"t":"line","id":22,"who":"a","text":"' + 'x'.repeat(100000) + '"}');
    check('... a frame of 100,000 characters is still read, and cut to a line', big.rowTexts().length === 4 && big.rowTexts()[3] === 'a: ' + 'x'.repeat(200) + '…');
    for (let i = 0; i < 40; i++) r.s().recv('garbage');
    check('a hub that sends nothing but noise is dropped after 30 frames of it: offline, and a retry is waiting', r.dock.state() === 'offline' && r.s().closes === 1 && r.clock.timers.some((t) => t.at > r.clock.t));
    same('malformed lines are not lines: the page draws none of them', (() => {
        const q = rig(); q.live(W({}));
        [{ id: 1 }, { who: 'a', text: 'b' }, { id: 'x', who: 'a', text: 'b' }, { id: 2, who: 7, text: 'b' }, { id: 3, who: 'a', text: {} }, { id: 4, who: 'a', text: '' }, { id: -5, who: 'a', text: 'b' }, { id: 1.5, who: 'a', text: 'b' }, { id: 6, who: ['a'], text: 'b' }].forEach((m) => q.s().recv(Object.assign({ t: 'line' }, m)));
        return q.rowTexts();
    })(), []);
    check('the same line twice (the same id) is drawn once', (() => { const q = rig(); q.live(W({})); q.s().recv(L(5, 'a', 'x')); q.s().recv(L(5, 'a', 'x again')); return q.rowTexts().length === 1 && q.rowTexts()[0] === 'a: x'; })());
    check('a line, a count or an err before the welcome is ignored (the dock is still connecting, nothing is drawn, no word is shown)', (() => { const q = rig(); q.s().open().recv(L(5, 'a', 'x')); q.s().recv({ t: 'online', n: 2 }); q.s().recv({ t: 'err', code: 'rate' }); return q.rowTexts().length === 0 && q.dock.state() === 'connecting' && q.on() === 'connecting…' && q.note() === ''; })());
}
{   // hostile text
    const r = rig();
    r.live(W({ you: YOU }));
    const hostile = [
        L(1, '<img src=x onerror=alert(1)>', '<script>alert(1)</script> <b>bold</b> javascript:alert(1) http://evil.example/x [link](http://a.b) &lt;i&gt;'),
        L(2, 'ev\u202eil', 'a\u202e\u2066b\u2069c\u200bd\u200ee\u061cf\ufeffg\u0000h\u0007i'),
        L(3, 'n'.repeat(90), 't'.repeat(4000)),
        L(4, 'a', ' \n\t  many   blanks \r\n here '),
        L(5, '\ud83d', 'lone \ud83d surrogate \ude00 and a pair \ud83d\ude00 stay'),
    ];
    hostile.forEach((m) => r.s().recv(m));
    const texts = r.rowTexts();
    same('markup and links are shown as the plain text that they are (nothing is built from them), names and lines are cut, the bidirectional and zero-width characters and controls are gone, blanks are one space', texts,
        ['<img src=x onerror=alert(1)>: <script>alert(1)</script> <b>bold</b> javascript:alert(1) http://evil.example/x [link](http://a.b) &lt;i&gt;', 'evil: abcdefghi', 'n'.repeat(32) + '…: ' + 't'.repeat(200) + '…', 'a: many blanks here', '?: lone surrogate and a pair \ud83d\ude00 stay']);
    const tags = {};
    r.log().all().forEach((e) => { tags[e.tagName] = (tags[e.tagName] || 0) + 1; });
    same('the only elements in the log are the log itself, its welcome line, and lines of div, span and bdi: no a, img, script, b, i, iframe or style, ever', Object.keys(tags).sort(), ['bdi', 'div', 'p', 'span']);
    check('no element of the log has an attribute that came from the hub (no href, src, style, on...)', r.log().all().every((e) => Object.keys(e.attributes).every((k) => ['class', 'role', 'aria-live', 'aria-label', 'tabindex'].indexOf(k) !== -1)));
    check('nothing was written as markup anywhere (innerHTML), and nothing threw', r.env.innerHTMLWrites.length === 0 && r.errors.length === 0);
    check('a name that is a word of the page ("mod", "__proto__", "constructor") is only a name', (() => { const q = rig(); q.live(W({})); q.s().recv(L(1, '__proto__', 'constructor')); q.s().recv(L(2, 'constructor', 'toString', false)); q.s().recv(Object.assign(L(3, 'a', 'b'), { mod: 'true' })); return q.rowTexts().join('|') === '__proto__: constructor|constructor: toString|a: b' && q.errors.length === 0 && one(q.rows()[2], 'gc-badge') === null; })());
}
{   // a burst of 1000 lines
    const r = rig();
    r.live(W({ you: YOU, history: HISTORY }));
    const started = Date.now();
    for (let i = 0; i < 1000; i++) r.s().recv(L(1000 + i, 'u' + (i % 7), 'line number ' + i));
    r.clock.advance(1);
    const rows = r.rows();
    check('a burst of 1000 lines leaves 100 on the screen (the oldest go first) and the welcome line stays at the top', rows.length === 100 && rows[0].textContent === 'u4: line number 900' && rows[99].textContent === 'u5: line number 999' && r.log().children[0].textContent === 'Welcome to the front page. Everyone here can read what you write.');
    check('... and it takes no time worth speaking of (under 2 s), the model holds 100, not 1000, and nothing threw', Date.now() - started < 2000 && r.errors.length === 0 && r.log().children.length === 101);
    r.s().recv({ t: 'del', id: 1950 });
    check('del takes the line off the screen at once (and only that one)', r.rows().length === 99 && r.rowTexts().indexOf('u5: line number 950') === -1 && r.rowTexts().indexOf('u6: line number 951') !== -1 && r.rowTexts().indexOf('u4: line number 949') !== -1);
    ['del-no-id', undefined, null, 'x', -1, 1.5, 99999999].forEach((id) => r.s().recv({ t: 'del', id }));
    r.s().recv({ t: 'del' });
    check('a del of nothing, of a line that is not here or of no number changes nothing', r.rows().length === 99 && r.errors.length === 0);
    r.s().recv({ t: 'del', id: 1999 }); r.s().recv({ t: 'del', id: 1999 });
    check('a line that is gone is gone for good (a second del is nothing, the line cannot come back by itself)', r.rows().length === 98 && r.rowTexts().indexOf('u5: line number 999') === -1);
}
{   // the err codes, in words
    const words = { rate: 'Slow down a little', muted: 'You cannot chat for a while', banned: 'You cannot chat here', long: 'That line is too long', empty: 'Write something first', young: 'Your account is too new to chat yet', bad: 'That line could not be sent' };
    Object.keys(words).forEach((code) => {
        const r = rig(); r.live(W({ you: YOU, login: true }));
        r.s().recv({ t: 'err', code });
        check('err ' + code + ': "' + words[code] + '" in the note, which goes after a few seconds', r.note() === words[code] && (r.clock.advance(5900), r.note() === words[code]) && (r.clock.advance(200), r.note() === ''));
    });
    const a = rig(); a.live(W({ you: null, login: true })); a.s().recv({ t: 'err', code: 'login' });
    const b = rig(); b.live(W({ you: null, login: false })); b.s().recv({ t: 'err', code: 'login' });
    const c = rig(); c.live(W({ you: YOU, login: true })); ['what', 5, null, undefined, {}, 'constructor'].forEach((code) => c.s().recv({ t: 'err', code }));
    same('err login: "Log in with Discord to chat" where the hub has a login, "Chat is read-only for now" where it has none; a code that is not known is the generic words', [a.note(), b.note(), c.note()], ['Log in with Discord to chat', 'Chat is read-only for now', 'That did not go through']);
    const d = rig(); d.live(W({ you: YOU })); d.dock.say('hello'); d.s().recv({ t: 'err', code: 'rate' });
    check('a line that the hub refused comes back into the field (nothing typed is lost)', d.field().value === 'hello');
    d.field().value = 'new words'; d.dock.say('again'); d.s().recv({ t: 'err', code: 'bad' });
    check('... a field that has new words in it is left alone', d.field().value === 'new words');
    const f = rig(); f.live(W({ you: YOU })); f.dock.say('sent and shown'); f.s().recv(L(77, 'maple.leaf', 'sent and shown')); f.s().recv({ t: 'err', code: 'bad' });
    check('a line that came back as a line is not put back into the field by a later err', f.field().value === '');
}
{   // the counts of the welcome, the history's own ids, the noise of one socket, the order of the ids
    const o = (online) => { const q = rig(); q.live(W({ online })); return [q.on(), one(q.root, 'gc-online').hidden]; };
    same('the count of the welcome is a whole number or nothing (a hub that says "many", -3 or 1.5 gives no count); more than the hub\'s 500 is "500+"', [o(7), o('many'), o(-3), o(1.5), o(null), o(500), o(501), o(1e9)],
        [['7 here now', false], ['', true], ['', true], ['', true], ['', true], ['500 here now', false], ['500+ here now', false], ['500+ here now', false]]);
    const online = rig(); online.live(W({})); online.s().recv({ t: 'online', n: 900 });
    check('... and the same for the count that follows', online.on() === '500+ here now');
    const dup = rig(); dup.live(W({ history: [{ id: 1, at: 1, who: 'a', text: 'one', mod: false }, { id: 2, at: 1, who: 'a', text: 'two', mod: false }, { id: 2, at: 1, who: 'a', text: 'two again', mod: false }, { id: 3, at: 1, who: 'a', text: 'three', mod: false }] }));
    check('a history that has the same id twice shows that line once (the first)', eq(dup.rowTexts(), ['a: one', 'a: two', 'a: three']));
    const spread = rig();
    spread.live(W({ you: YOU }));
    for (let i = 0; i < 20; i++) spread.s().recv('noise ' + i);
    const first = spread.s();
    first.fail(); spread.clock.advance(1000);
    spread.live(W({ you: YOU }));
    for (let i = 0; i < 20; i++) spread.s().recv('noise ' + i);
    check('the frames of noise are counted for each socket (20 and 20 are not 40): the second socket is still up', spread.sockets.length === 2 && spread.dock.state() === 'live' && spread.s() !== first);
    const ids = rig({ width: 390, height: 844 });
    ids.live(W({ you: YOU, history: HISTORY }));
    ids.s().recv(L(10, 'a', 'ten')); ids.s().recv(L(5, 'a', 'five, late'));
    ids.s().fail(); ids.clock.advance(2000);
    ids.live(W({ you: YOU, history: HISTORY.concat([{ id: 6, at: 1, who: 'a', text: 'six', mod: false }, { id: 9, at: 1, who: 'a', text: 'nine', mod: false }, { id: 10, at: 1, who: 'a', text: 'ten', mod: false }, { id: 11, at: 1, who: 'a', text: 'eleven', mod: false }]) }));
    check('the newest id that was seen never goes down: after a late line (5 after 10) only the line above 10 is news (1, not 4)', ids.pill() === '1', ids.pill());
    const del = rig();
    del.live(W({ history: HISTORY }));
    del.s().recv({ t: 'del', id: '3' }); del.s().recv({ t: 'del', id: [3] }); del.s().recv({ t: 'del', id: true });
    check('a del whose id is a string, a list or a boolean is not a del of line 3 (the ids are compared as numbers, strictly)', del.rows().length === 3);
}
{   // a second welcome, another version
    const r = rig();
    r.live(W({ you: YOU, history: HISTORY, online: 4 }));
    r.s().recv(W({ you: null, login: false, online: 99, history: [L(50, 'x', 'y')] }));
    check('a second welcome on a socket is ignored (who you are, the lines and the count are the first one\'s)', r.on() === '4 here now' && r.form().hidden === false && r.rowTexts().length === 3);
    const v = rig(); v.s().open().recv(W({ v: 2 }));
    check('a hub that says another version is no hub for this page: the socket is dropped, the dock is offline and tries again later', v.dock.state() === 'offline' && v.s().closes === 1 && v.rowTexts().length === 0);
    const h = rig(); h.s().open().recv(W({ history: 'lines' }));
    check('a history that is no list is no welcome', h.dock.state() === 'offline');
    const g = rig(); g.live(W({ history: Array.from({ length: 80 }, (_, i) => ({ id: i + 1, at: 1, who: 'a', text: 'n' + i, mod: false })) }));
    check('a history of 80 lines is cut to the last 50', g.rows().length === 50 && g.rows()[0].textContent === 'a: n30');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. The link
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{   // the hub is not there: offline, reconnecting, the backoff, nothing sent
    const r = rig();
    r.s().fail();
    check('a socket that fails: "Chat is offline" in the note, "reconnecting…" in the header, no field, no count; the first retry is in 1 s', r.dock.state() === 'offline' && r.note() === 'Chat is offline' && r.on() === 'reconnecting…' && r.form().hidden === true && r.sockets.length === 1);
    const waitsOf = (rand) => {
        const q = rig({ random: () => rand }), waits = [];
        q.s().fail();
        let t0 = q.clock.t;
        for (let i = 0; i < 10; i++) { const n = q.sockets.length; for (let spent = 0; q.sockets.length === n && spent < 1000; spent++) q.clock.advance(100); waits.push(Math.round((q.clock.t - t0) / 100) * 100); q.s().fail(); t0 = q.clock.t; }
        return waits;
    };
    same('the retries come after 1, 1, 2, 4, 8, 16 and then 30 s at the low end of the spread, never sooner', waitsOf(0), [1000, 1000, 2000, 4000, 8000, 16000, 30000, 30000, 30000, 30000]);
    same('... and after 1.5, 3, 6, 12, 24, 48 and then 60 s at the high end, never later', waitsOf(1), [1500, 3000, 6000, 12000, 24000, 48000, 60000, 60000, 60000, 60000]);
    check('... and the note keeps saying it', r.note() === 'Chat is offline' && r.errors.length === 0);
}
{   // jitter, and a storm of clients
    const lo = rig({ random: () => 0 }), hi = rig({ random: () => 0.999 });
    [lo, hi].forEach((q) => q.s().fail());
    const first = (q) => { const n = q.sockets.length; let t = 0; while (q.sockets.length === n && t < 100000) { q.clock.advance(10); t += 10; } return t; };
    const a = first(lo), b = first(hi);
    check('jitter: the first retry is 1 to 1.5 s, by chance (so that many pages that lost one hub do not come back on the same second)', a === 1000 && b >= 1490 && b <= 1500, a + ' ' + b);
    const hour = (rand) => { const once = rig({ random: () => rand }), times = [0]; for (let t = 0; t < 3600000; t += 100) { once.clock.advance(100); if (once.sockets.length > times.length) { times.push(once.clock.t); once.s().fail(); } } return times; };
    const slow = hour(1), quick = hour(0), gaps = slow.slice(1).map((x, i) => x - slow[i]);
    check('a hub that is down for an hour costs about one try a minute at the slowest (65 sockets), never two within a second, and the last waits are 60 s', slow.length >= 60 && slow.length <= 70 && Math.min.apply(null, gaps) >= 1500 && gaps.slice(-10).every((g) => g === 60000), slow.length + ' ' + gaps.slice(-3));
    check('... and one in half a minute at the quickest (125 sockets)', quick.length >= 110 && quick.length <= 130, quick.length);
    const q = rig({ random: () => 0 });
    q.clock.advance(1);
    let count = 0;
    for (let i = 0; i < 40; i++) { q.s().open().recv(W({})); q.s().fail(); q.clock.advance(1000); count = q.sockets.length; }
    check('a hub that welcomes and then closes at once does not bring the wait back to 1 s (a link must hold for 30 s for that): 40 such tries in 40 s are 8 sockets at most', count <= 8, count);
    const u = rig({ random: () => 1 });
    u.s().fail(); u.clock.advance(1500); u.s().open().recv(W({})); u.clock.advance(29900); u.s().fail();
    const held = u.sockets.length; u.clock.advance(2999);
    check('a link that held for 29.9 s does not start the backoff again: the next retry is in 3 s, not in 1.5', u.sockets.length === held && (u.clock.advance(1), u.sockets.length === held + 1));
    const s = rig({ random: () => 1 });
    s.s().fail(); s.clock.advance(1500); s.s().fail(); s.clock.advance(3000); s.s().open().recv(W({})); s.clock.advance(31000); s.s().fail();
    const n = s.sockets.length; s.clock.advance(1499);
    check('a link that held for 30 s starts the backoff again: the next retry is in 1.5 s, not in 6', s.sockets.length === n && (s.clock.advance(1), s.sockets.length === n + 1));
}
{   // nothing is sent while offline
    const r = rig();
    r.live(W({ you: YOU, login: true }));
    r.s().readyState = 2;
    check('a socket that is closing (the hub has not said so yet) is not written to', r.dock.say('hello') === 'offline' && r.s().calls === 0);
    r.s().readyState = 1;
    r.s().fail();
    const old = r.s();
    same('say() while the link is down: refused, "Chat is offline", and no socket was asked to send', [r.dock.say('hello'), old.calls, old.sent.length, r.sockets.length, r.note()], ['offline', 0, 0, 1, 'Chat is offline']);
    r.submit('typed while offline');
    check('the form is hidden while offline, and a submit that gets through anyway sends nothing and keeps the words in the field', r.form().hidden === true && old.sent.length === 0 && r.said().length === 0 && r.field().value === 'typed while offline');
    r.clock.advance(1000);
    const fresh = r.s();
    check('a socket that is only connecting (not open, no welcome) is not written to either', fresh !== old && r.dock.say('hello') === 'offline' && fresh.calls === 0 && fresh.sent.length === 0);
    fresh.open();
    check('... open but not welcomed: still nothing sent', r.dock.say('hello') === 'offline' && fresh.calls === 0);
    fresh.recv(W({ you: YOU, login: true }));
    check('welcomed again: the form is back, with the words that were typed, and a line goes out', r.form().hidden === false && r.field().value === 'typed while offline' && r.dock.say('hello') === '' && fresh.sent.length === 1);
}
{   // a socket that never opens, or never speaks; a constructor that throws; no WebSocket at all
    const r = rig();
    r.clock.advance(14999);
    check('a socket that has not been welcomed in 15 s is given up: offline, and a retry is waiting', r.dock.state() === 'connecting' && (r.clock.advance(1), r.dock.state() === 'offline') && r.s().closes === 1);
    const q = rig(); q.s().open(); q.clock.advance(15000);
    check('... also when it opened and then said nothing', q.dock.state() === 'offline');
    const t = rig({ throws: true });
    check('a page that may not open a socket (the constructor throws): offline, no throw, and it keeps trying on the backoff', t.dock.state() === 'offline' && t.note() === 'Chat is offline' && t.errors.length === 0 && (t.clock.advance(1000), true) && t.errors.length === 0);
    const n = rig({ noWebSocket: true });
    check('a browser without WebSocket: offline for good, no throw, no timer left running, nothing about "reconnecting…"', n.dock.state() === 'offline' && n.note() === 'Chat is offline' && (n.clock.advance(600000), n.clock.timers.length === 0) && n.sockets.length === 0 && one(n.root, 'gc-online').hidden === true && n.errors.length === 0);
}
{   // the ping
    const r = rig({ pong: false });
    r.live(W({ you: YOU }));
    r.clock.advance(24999);
    check('no ping before 25 s', r.pings() === 0);
    r.clock.advance(1);
    check('a ping {"t":"ping"} 25 s after the welcome', r.pings() === 1 && r.s().sent[0] === '{"t":"ping"}');
    r.s().recv({ t: 'online', n: 8 });
    r.clock.advance(25000); r.s().recv({ t: 'online', n: 8 }); r.clock.advance(25000); r.s().recv({ t: 'online', n: 8 }); r.clock.advance(25000);
    check('... and one every 25 s as long as the hub answers (4 in 100 s)', r.pings() === 4 && r.dock.state() === 'live');
    r.clock.advance(29999);
    check('a hub that does not answer a ping for 30 s is given up: no answer, no link', r.dock.state() === 'live' && (r.clock.advance(1), r.dock.state() === 'offline') && r.note() === 'Chat is offline');
    const before = r.pings();
    r.clock.advance(300000);
    check('no ping goes out while there is no welcomed socket (the retries open sockets that the hub never welcomes here: none is pinged)', r.pings() === before && r.sockets.length > 2);
    const q = rig(); q.live(W({}));
    for (let i = 0; i < 20; i++) { q.clock.advance(25000); q.s().recv({ t: 'online', n: 2 }); }
    check('twenty pings in 500 s, each answered: one socket, still live', q.sockets.length === 1 && q.pings() === 20 && q.dock.state() === 'live');
}
{   // a lost link takes its ping with it: after three lost links there is still one ping chain
    const r = rig({ random: () => 0 });
    r.live(W({ you: YOU }));
    for (let i = 0; i < 3; i++) { r.s().fail(); r.clock.advance(2500); r.live(W({ you: YOU })); }
    const before = r.pings();
    r.clock.advance(25000);
    check('three lost links later the page still pings once every 25 s (one chain, not four)', r.dock.state() === 'live' && r.pings() - before === 1, r.pings() - before);
    r.clock.advance(50000);
    check('... and once more for each 25 s after that', r.pings() - before === 3);
}
{   // the tab: hidden is not painted, visible is made again
    const r = rig();
    r.live(W({ you: YOU, history: HISTORY }));
    r.hidden(true);
    const painted = r.log().children.length;
    for (let i = 0; i < 50; i++) r.s().recv(L(100 + i, 'u', 'while hidden ' + i));
    r.s().recv({ t: 'del', id: 105 }); r.s().recv({ t: 'online', n: 5 });
    check('a hidden tab is not painted: the log, the count and the strip stay as they were while the model goes on (the link stays open)', r.log().children.length === painted && r.on() === '7 here now' && r.s().closes === 0 && r.dock.state() === 'live');
    r.hidden(false);
    check('the tab comes back: the log is made again from the model (the lines that came, without the one that was deleted), the count is right', r.rows().length === 52 && r.rowTexts().indexOf('u: while hidden 5') === -1 && r.rowTexts().pop() === 'u: while hidden 49' && r.on() === '5 here now');
    check('... and what came while the tab was hidden is not unread when the log is open on Home (it is on the screen now), but is when the dock is folded', r.pill() === '' && (() => { const q = rig(); q.live(W({ you: YOU })); q.dock.open(false); q.hidden(true); q.s().recv(L(5, 'a', 'x')); q.hidden(false); return q.pill() === '1'; })());
}
{   // visible again after the link was lost: do not wait for the timer (but not twice in the same second)
    const r = rig({ random: () => 1 });
    r.live(W({})); r.hidden(true); r.s().fail();
    const n = r.sockets.length;
    r.clock.advance(300);
    r.hidden(false);
    check('a tab that comes back within a second of a failed try does not open another at once', r.sockets.length === n);
    r.clock.advance(5000);
    const m = r.sockets.length;
    r.hidden(true); r.s().fail(); r.clock.advance(1100); r.hidden(false);
    check('a tab that comes back while the link is down does not wait for the timer (the wait is 3 s, a second and a tenth have passed)', r.sockets.length === m + 1 && r.dock.state() !== 'live');
    const q = rig({ random: () => 1 });
    q.s().fail(); q.clock.advance(1500); q.s().fail(); q.clock.advance(3000); q.s().fail();
    const k = q.sockets.length;
    q.clock.advance(100);
    (q.win.listeners.online || []).forEach((fn) => fn({}));
    check('the network coming back (the page\'s online event) within a second of the last try waits for the timer', q.sockets.length === k);
    q.clock.advance(1000);
    (q.win.listeners.online || []).forEach((fn) => fn({}));
    check('... later it is a reason to try now (the wait is 6 s, a second and a tenth have passed)', q.sockets.length === k + 1);
}
{   // a flood of lines is not read out line by line
    const r = rig();
    r.live(W({ you: YOU, history: HISTORY }));
    r.clock.advance(400);
    const news = () => one(r.root, 'gc-news');
    check('(at rest the log is a polite live region, and the summary place is a polite status that says nothing)', r.log().getAttribute('aria-live') === 'polite' && news().getAttribute('role') === 'status' && news().getAttribute('aria-live') === 'polite' && news().textContent === '');
    r.s().recv(L(50, 'a', '1')); r.s().recv(L(51, 'a', '2'));
    check('the first two lines of a moment are read out as they come (the log stays live)', r.log().getAttribute('aria-live') === 'polite');
    for (let i = 3; i <= 10; i++) r.s().recv(L(50 + i, 'a', String(i)));
    check('the third line of the same 3 s turns the log quiet: the lines are in it, a screen reader is not told each one', r.log().getAttribute('aria-live') === 'off' && r.rows().length === 13 && news().textContent === '');
    r.clock.advance(2999);
    check('... for the 3 s of the window', r.log().getAttribute('aria-live') === 'off' && news().textContent === '');
    r.clock.advance(1);
    check('... then the log is live again and one line says how many were not read: "8 more messages"', r.log().getAttribute('aria-live') === 'polite' && news().textContent === '8 more messages');
    r.s().recv(L(70, 'a', 'x')); r.s().recv(L(71, 'a', 'y')); r.s().recv(L(72, 'a', 'z'));
    r.clock.advance(3000);
    check('a window of three lines has one that is not read: "One more message"', news().textContent === 'One more message' && r.log().getAttribute('aria-live') === 'polite');
    for (let i = 0; i < 8; i++) { r.s().recv(L(80 + i, 'a', 'slow ' + i)); r.clock.advance(2000); }
    check('a line every 2 s for 16 s is never quiet and never summarized', r.log().getAttribute('aria-live') === 'polite' && news().textContent === 'One more message');
    const q = rig();
    q.live(W({ you: YOU }));
    q.clock.advance(400);
    q.dock.open(false);
    for (let i = 0; i < 20; i++) q.s().recv(L(200 + i, 'a', 'folded ' + i));
    q.clock.advance(3000);
    check('a log that is folded gets no line painted, so nothing is read and nothing is summarized, however many come', q.log().getAttribute('aria-live') === 'polite' && one(q.root, 'gc-news').textContent === '' && q.pill() === '20');
    const rep2 = rig();
    rep2.live(W({ you: YOU, history: HISTORY }));
    for (let i = 0; i < 6; i++) rep2.s().recv(L(90 + i, 'a', String(i)));
    rep2.clock.advance(250);
    check('the quiet of a log that is made again and the quiet of a flood do not cut each other short (the log stays quiet until both are over)', rep2.log().getAttribute('aria-live') === 'off');
    rep2.clock.advance(3000);
    check('... then it is live', rep2.log().getAttribute('aria-live') === 'polite');
}
{   // a notice does not outlive the link, and the log is polite from the first moment
    const r = rig();
    r.live(W({ you: YOU }));
    r.dock.say('');
    check('(a word is up: "Write something first")', r.note() === 'Write something first');
    r.s().fail();
    check('the link goes: the note says that the chat is offline, not the old word', r.note() === 'Chat is offline');
    const early = rig({ home: false });
    check('the log is a polite live region before anything has happened (and before Home has told the dock that it shows)', early.log().getAttribute('aria-live') === 'polite' && early.log().getAttribute('role') === 'log');
}
{   // an error inside the dock stays inside it
    const g = rig();
    g.live(W({ you: YOU }));
    g.doc.createElement = () => { throw new Error('out of memory'); };
    let escaped = false;
    try { g.s().recv(L(5, 'a', 'x')); g.s().recv({ t: 'online', n: 3 }); g.clock.advance(60000); g.submit('hello'); } catch (e) { escaped = true; }
    check('a handler of the dock that goes wrong keeps it to itself (nothing reaches the page; the error goes to the one who asked for it)', !escaped && g.errors.length >= 1);
}
{   // a page that goes away, and comes back from the cache
    const r = rig();
    r.live(W({ you: YOU }));
    (r.win.listeners.pagehide || []).forEach((fn) => fn({ persisted: true }));
    check('pagehide lets the hub go: the socket is closed and the dock says offline (a page in the back-forward cache holds no socket)', r.s().closes === 1 && r.dock.state() === 'offline');
    r.clock.advance(600000);
    check('... and nothing comes back by itself', r.sockets.length === 1);
    (r.win.listeners.pageshow || []).forEach((fn) => fn({ persisted: false }));
    check('a pageshow of a page that was loaded fresh does nothing', r.sockets.length === 1);
    (r.win.listeners.pageshow || []).forEach((fn) => fn({ persisted: true }));
    check('a pageshow of a page from the cache opens the link again', r.sockets.length === 2 && (r.s().open().recv(W({ you: YOU })), r.dock.state() === 'live'));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. What the visitor does and sees
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{   // Send, and its pace
    const r = rig();
    r.live(W({ you: YOU, login: true }));
    r.submit('hello there');
    same('Send puts {"t":"say","text":...} on the socket, exactly, and empties the field', [r.s().sent, r.field().value], [['{"t":"say","text":"hello there"}'], '']);
    r.submit('  two   spaces  \u202e  ');
    same('the text is cleaned as the hub would clean it (blanks, controls and bidirectional characters) before it goes', r.said().map((m) => m.text), ['hello there', 'two spaces']);
    r.submit('third'); r.submit('third');
    same('the page paces itself below the hub (a burst of 2, then one every 2.5 s): the third line in a row is not sent, and says "Slow down a little"; the words stay in the field', [r.said().length, r.note(), r.field().value], [2, 'Slow down a little', 'third']);
    r.clock.advance(2499);
    r.dock.say('third');
    check('... not before 2.5 s have passed', r.said().length === 2);
    r.clock.advance(1);
    check('... and one line after that', r.dock.say('third') === '' && r.said().length === 3);
    r.clock.advance(5000);
    same('the same text twice within 31 s is not sent (the hub drops it too); after that it is', [r.dock.say('third'), (r.clock.advance(26000), r.dock.say('third'))], ['rate', '']);
    r.clock.advance(60000);
    check('a long rest gives back the burst of 2 and no more', r.dock.say('a1') === '' && r.dock.say('a2') === '' && r.dock.say('a3') === 'rate');
    same('an empty line, a line of blanks and controls only, and one of 201 code points are refused here, with their words', [r.dock.say(''), r.dock.say(' \t \u202e '), r.dock.say('x'.repeat(201)), r.note()], ['empty', 'empty', 'long', 'That line is too long']);
    check('200 code points go (so do 200 emoji: 400 UTF-16 units), 201 do not', (r.clock.advance(60000), r.dock.say('y'.repeat(200)) === '') && (r.clock.advance(60000), r.dock.say('\ud83d\ude00'.repeat(200)) === '') && (r.clock.advance(60000), r.dock.say('\ud83d\ude00'.repeat(201)) === 'long'));
    const same3 = rig();
    same3.live(W({ you: YOU }));
    const sent3 = [same3.dock.say('A'), (same3.clock.advance(2500), same3.dock.say('B')), (same3.clock.advance(2500), same3.dock.say('A')), (same3.clock.advance(2500), same3.dock.say('B'))];
    same('a text that was sent 5 s ago is not sent again, even when another was sent in between (A, B, A and B within 31 s: the hub remembers them all and answers "rate", which is a mistake against the 20 a minute)', [sent3, same3.said().map((m) => m.text)], [['', '', 'rate', 'rate'], ['A', 'B']]);
    same3.clock.advance(24000);
    check('... after 31 s from the first, the first may go again; the second still may not (it went at 2.5 s)', same3.dock.say('A') === '' && same3.dock.say('B') === 'rate' && (same3.clock.advance(2600), same3.dock.say('B') === ''), same3.said().map((m) => m.text).join());
    const proto = rig();
    proto.live(W({ you: YOU }));
    check('texts that are names of Object (__proto__, constructor, toString) are only texts to the pace', proto.dock.say('__proto__') === '' && (proto.clock.advance(2500), proto.dock.say('constructor') === '') && (proto.clock.advance(2500), proto.dock.say('__proto__') === 'rate') && proto.errors.length === 0);
    const lt = rig();
    lt.live(W({ you: YOU }));
    same('a line with < or > is not sent (the hub refuses it, and a refusal is a mistake against the 20 a minute): "Lines cannot hold < or >"', [lt.dock.say('a < b'), lt.note(), lt.dock.say('x > y'), lt.sockets[0].sent.length, lt.dock.say('fine') === ''], ['markup', 'Lines cannot hold < or >', 'markup', 0, true]);
    check('the field takes at most 400 characters (a paste cannot be a megabyte), and nothing threw', r.field().getAttribute('maxlength') === '400' && r.errors.length === 0);
}
{   // fold, strip, unread, the Room
    const r = rig();
    r.live(W({ you: YOU, history: HISTORY }));
    r.dock.shown(true);
    check('a desktop opens with the panel out, the minus button says "Fold chat", and lines that come are read', r.root.getAttribute('data-open') === 'true' && r.fold().getAttribute('aria-expanded') === 'true' && (r.s().recv(L(10, 'a', 'one')), r.pill() === '') && r.rows().length === 4);
    r.fold().click();
    check('the minus button folds it to the header: the body is hidden, the button says "Open chat", focus stays on the button', r.root.getAttribute('data-open') === 'false' && one(r.root, 'gc-body').hidden === true && r.fold().getAttribute('aria-label') === 'Open chat' && r.fold().getAttribute('aria-expanded') === 'false' && r.env.focused === r.fold());
    r.s().recv(L(11, 'a', 'two')); r.s().recv(L(12, 'maple.leaf', 'mine')); r.s().recv(L(13, 'b', 'three'));
    same('lines that come while folded are unread (not the visitor\'s own), counted in the header and in the button\'s name; the strip shows the last line', [r.pill(), r.fold().getAttribute('aria-label'), r.peek()], ['2', 'Open chat, 2 unread', 'b: three']);
    r.fold().click();
    check('opening it again reads them: the count is gone, the log has every line', r.pill() === '' && r.root.getAttribute('data-open') === 'true' && r.rowTexts().pop() === 'b: three' && r.fold().getAttribute('aria-label') === 'Fold chat');
    r.dock.shown(false);
    r.s().recv(L(14, 'c', 'on the Room')); r.s().recv(L(15, 'c', 'on the Room 2'));
    check('on the Room the dock is not on the screen: its lines are unread, and the socket stays open', r.pill() === '2' && r.s().closes === 0 && r.dock.state() === 'live');
    r.dock.shown(true);
    check('back on Home with the dock out: read', r.pill() === '' && r.rowTexts().pop() === 'c: on the Room 2');
    r.dock.open(false); r.dock.shown(false);
    for (let i = 0; i < 120; i++) r.s().recv(L(200 + i, 'z', 'n' + i));
    same('more than 99 unread says "99+"', r.pill(), '99+');
    r.s().recv({ t: 'del', id: 250 });
    check('a line that is deleted before it was read is not unread any more (and a line that the cap pushed out is not counted either: 100 at the most)', r.pill() === '99' && (r.s().recv({ t: 'del', id: 251 }), r.s().recv({ t: 'del', id: 252 }), r.pill() === '97') && (() => { const q = rig(); q.live(W({ you: YOU })); q.dock.open(false); q.s().recv(L(1, 'a', 'x')); q.s().recv(L(2, 'a', 'y')); q.s().recv({ t: 'del', id: 1 }); return q.pill() === '1'; })());
    r.dock.shown(true); r.dock.open(true);
    check('read again', r.pill() === '');
    const c = rig({ width: 390, height: 844 });
    c.live(W({ you: YOU, history: HISTORY }));
    c.dock.shown(true);
    check('a phone starts with the strip (folded), the last line in it, and no unread count for the history', c.root.getAttribute('data-open') === 'false' && c.pill() === '' && c.peek() === 'juniper: joining!' && one(c.root, 'gc-short').textContent === 'Chat');
    c.s().recv(L(30, 'pebble', 'hi')); c.s().recv(L(31, 'pebble', 'hi again'));
    check('lines make the strip say so: "2" and the last line', c.pill() === '2' && c.peek() === 'pebble: hi again');
    one(c.root, 'gc-head').click();
    check('a press anywhere on the strip opens it (the whole strip is the target) and reads it; the page moves no focus for a press', c.root.getAttribute('data-open') === 'true' && c.pill() === '' && !c.env.focused);
    one(c.root, 'gc-head').click();
    check('... and a press on the header of the open panel does not fold it (only the minus button does)', c.root.getAttribute('data-open') === 'true');
    c.root.fire('keydown', { key: 'a' });
    check('a key that is not Escape does nothing', c.root.getAttribute('data-open') === 'true');
    c.root.fire('keydown', { key: 'Escape' });
    check('Escape inside the dock folds it and puts the focus on its button', c.root.getAttribute('data-open') === 'false' && c.env.focused === c.fold());
    c.root.fire('keydown', { key: 'Escape' });
    check('Escape on a folded dock does nothing', c.root.getAttribute('data-open') === 'false' && c.errors.length === 0);
    const sw = rig({ width: 844, height: 390 });
    check('a phone held sideways (844 x 390) starts folded too; a window of 721 x 501 is a desktop; 720 wide or 500 high is not', sw.root.getAttribute('data-open') === 'false' && rig({ width: 721, height: 501 }).root.getAttribute('data-open') === 'true' && rig({ width: 720, height: 900 }).root.getAttribute('data-open') === 'false' && rig({ width: 1280, height: 500 }).root.getAttribute('data-open') === 'false');
}
{   // the log is read from the bottom: a log that is made again, and a line that comes to a reader at the bottom, scroll to the newest line; a reader who has scrolled up is not moved
    const r = rig();
    r.live(W({ you: YOU, history: HISTORY }));
    r.clock.advance(1);
    const log = r.log();
    log.scrollHeight = 5000; log.clientHeight = 300; log.scrollTop = 0;
    r.dock.open(false); r.dock.open(true); r.clock.advance(1);
    check('a log that is made again (the dock opens) shows its newest line: scrolled to the bottom, wherever it was', log.scrollTop === 5000);
    log.scrollTop = 4700;
    r.s().recv(L(30, 'a', 'x')); r.clock.advance(1);
    check('a line that comes to a reader who is at the bottom keeps the log at the bottom', log.scrollTop === 5000);
    let writes = 0, top = 4700;
    Object.defineProperty(log, 'scrollTop', { configurable: true, get: () => top, set: (v) => { writes++; top = v; } });
    for (let i = 0; i < 5; i++) r.s().recv(L(40 + i, 'a', 'burst ' + i));
    r.clock.advance(1);
    check('a burst of lines scrolls once, at its end (not once for each line)', writes === 1 && top === 5000, writes + ' ' + top);
    top = 100;
    r.s().recv(L(31, 'a', 'y')); r.s().recv(L(32, 'a', 'z')); r.clock.advance(1);
    check('... a reader who has scrolled up to read is not moved', top === 100);
}
{   // after a lost link the lines that came meanwhile are news
    const r = rig({ random: () => 0 });
    r.live(W({ you: YOU, history: HISTORY }));
    r.dock.shown(true); r.dock.open(false);
    r.s().fail(); r.clock.advance(1000);
    r.live(W({ you: YOU, history: HISTORY.concat([{ id: 7, at: 1, who: 'new', text: 'while you were away', mod: false }, { id: 8, at: 1, who: 'maple.leaf', text: 'mine from another tab', mod: false }]) }));
    check('the welcome after a lost link replaces the log (no line twice), and the lines newer than any seen before are unread, the visitor\'s own not', r.pill() === '1' && r.dock.state() === 'live' && r.on() === '7 here now');
    r.dock.open(true);
    check('... opening it shows them once', r.rowTexts().length === 5 && r.pill() === '');
}
{   // the Escape key and the form
    const r = rig();
    r.live(W({ you: YOU }));
    let prevented = 0;
    r.field().value = 'x';
    r.form().fire('submit', { preventDefault() { prevented++; } });
    check('the form never submits the page (its submit is prevented)', prevented === 1 && r.said().length === 1);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 6. The page: the dock comes after the first paint, only when Home is shown; a missing or broken one changes nothing
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const fresh = (env) => env.body.children.filter((c) => c.tagName === 'script' || c.tagName === 'link');
function hubFor(env, o) {                                // the page's window gets a hub socket class (the page's own game socket is the harness's); returns the sockets it opened
    const sockets = [];
    env.win.WebSocket = function (url) { if (o && o.throws) throw new Error('refused'); this.url = url; this.sent = []; this.readyState = 0; this.close = () => { this.readyState = 3; }; this.send = (x) => { if (this.readyState !== 1) throw new Error('not open'); this.sent.push(x); }; sockets.push(this); };
    return sockets;
}
function bring(env) {                                     // the two files arrive: the style and the script load (the script is run as the page would run it)
    env.win.AntsHomeChat = H.loadFile('home/chat.js', env.scope).AntsHomeChat;
    fresh(env).forEach((n) => { if (n.onload) n.onload(); });
}
{
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    check('a plain visit loads nothing of the chat before the first paint, opens no socket, and Home is as before', fresh(env).length === 0 && env.sockets.length === 0 && env.screen() === 'home' && env.$('home').children.length === 3);
    const hub = hubFor(env);
    env.advance(0);
    const parts = fresh(env);
    same('after the first paint the page asks for the style and the script of the dock, once, and nothing else', parts.map((n) => n.tagName + ':' + (n.href || n.src) + ':' + (n.rel || '')), ['link:home/chat.css:stylesheet', 'script:home/chat.js:']);
    check('... the script is async, so it never holds the page', parts[1].async === true && hub.length === 0 && env.$('home').children.length === 3);
    parts[1].onload();
    check('the dock mounts only when BOTH the style and the script are there (no unstyled dock)', env.$('home').children.length === 3 && hub.length === 0);
    env.win.AntsHomeChat = H.loadFile('home/chat.js', env.scope).AntsHomeChat;
    parts[0].onload();
    check('... then it is the last child of Home\'s div, one socket to the hub is opened, and the game\'s own socket is not touched', env.$('home').children.length === 4 && env.$('home').children[3].id === 'gchat' && hub.length === 1 && hub[0].url === 'wss://play.test/hub/ws' && env.sockets.length === 0);
    const dock = env.$('home').children[3];
    check('Home\'s own parts are where they were (the bar, the Yard, the card)', env.$('home').children.slice(0, 3).map((c) => c.tagName).join() === 'header,main,div' && dock.getAttribute('data-open') === 'true');
    env.home('mine').click();
    check('My room still makes the room (the page\'s own socket), and the Room shows: the dock is told (it is not on the screen)', env.sockets.length === 1 && env.screen() === 'room' && hub.length === 1);
    hub[0].readyState = 1; hub[0].onmessage({ data: JSON.stringify(W({ you: YOU, history: HISTORY })) });
    hub[0].onmessage({ data: JSON.stringify(L(40, 'a', 'on the Room')) });
    check('a line that comes while the Room shows is unread, and the link stays', one(dock, 'gc-unread').hidden === false && one(dock, 'gc-unread').textContent === '1' && hub.length === 1);
    env.roomBar('rhome').click();
    check('Home again: the dock is read, the link is the same one', one(dock, 'gc-unread').hidden === true && hub.length === 1);
    env.advance(10);
    check('show(home) again does not load the dock a second time', fresh(env).length === 2 && env.$('home').children.length === 4);
}
{   // a dialog over the page, and the hand-off to the game, take the dock's keys and presses too (the focus does not go behind a modal)
    const env = runLobby('', { 'ants.name': 'Maya' }, { picker: true, stayHome: true });
    hubFor(env); env.advance(0); bring(env);
    const root = env.$('home').children[3];
    check('(the dock is there and takes presses)', root.id === 'gchat' && !root.hasAttribute('inert'));
    env.home('mine').click();
    env.arrive({ key: KEY, seats: [[C, 'Maya'], [E, ''], [E, ''], [E, '']] });
    env.$('browse').click();
    check('the map list is a dialog: the dock is inert with the bars, the Room and the strips behind it', root.hasAttribute('inert') && env.home('hbar').hasAttribute('inert') && env.$('lobby').hasAttribute('inert'));
    const late = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    hubFor(late); late.advance(0);
    late.win.AntsHomeChat = H.loadFile('home/chat.js', late.scope).AntsHomeChat;
    const hold = late.home('mine'); hold.click();
    const sock = late.arrive({ key: KEY, seats: [[C, 'Sam'], [E, ''], [E, ''], [E, '']] });
    sock.receive(roomMessage([[C, 'Sam'], [C, 'Maya'], [E, ''], [E, '']], { you: 0, leader: 0, flags: 7 }));
    fresh(late).forEach((n) => n.onload());
    const lateRoot = late.$('home').children[3];
    check('a dock that is mounted while the page is on its way to the game is inert from the start', late.assigned.length === 1 && lateRoot.id === 'gchat' && lateRoot.hasAttribute('inert') && late.home('hbar').hasAttribute('inert'));
    late.advance(15000);
    check('... and 15 s later, when Home is free, so is the dock', !lateRoot.hasAttribute('inert') && !late.home('hbar').hasAttribute('inert'));
}
{   // a link to a room opens the Room: no Home, no dock until Home is shown
    const env = runLobby('?room=k7m2xq', { 'ants.name': 'Sam' });
    env.advance(10);
    check('a page that opens on the Room asks for no chat (Home was never shown)', fresh(env).length === 0);
    env.$('name-step-go').click();
    env.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    env.roomBar('rhome').click();
    env.advance(10);
    check('... pressing Home shows it, and then the dock is asked for, once', fresh(env).length === 2);
}
{   // the hub is down, slow or garbage; the dock is missing or breaks: nothing else changes, START still hands off
    const down = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    hubFor(down, { throws: true });
    down.advance(0); bring(down);
    check('a hub that cannot be opened: the dock says "Chat is offline"; Home is whole', down.$('home').children[3].id === 'gchat' && one(down.$('home').children[3], 'gc-note').textContent === 'Chat is offline' && down.screen() === 'home');
    down.home('mine').click();
    const s = down.arrive({ key: KEY, seats: [[C, 'Sam'], [E, ''], [E, ''], [E, '']] });
    down.roomBar('rhome').click();
    s.receive(roomMessage([[C, 'Sam'], [C, 'Maya'], [E, ''], [E, '']], { you: 0, leader: 0, flags: 7 }));
    check('... My room, the room, Home and START that hands off to the game are as without the chat', down.assigned.length === 1 && down.$('home').children[2].hidden === false && !!down.storage.data['ants.rejoin.' + H.helloRoom(s.sent[0]) + '.0']);
    const garbage = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    const hub = hubFor(garbage);
    garbage.advance(0); bring(garbage);
    hub[0].readyState = 1;
    const give = (f) => { if (hub[hub.length - 1].onmessage) hub[hub.length - 1].onmessage({ data: f }); };       // (a socket that the dock let go hears nothing more)
    ['x', '{', '{"t":"line"}'].concat(Array.from({ length: 16 }, () => '\u0000\u0001')).forEach(give);
    give('x'.repeat(5e6));
    check('a hub that sends garbage and a frame of 5 MB: no throw, the page works (My room makes its room)', (garbage.home('mine').click(), garbage.sockets.length === 1 && garbage.screen() === 'room'));
    const broken = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    broken.advance(0);
    broken.win.AntsHomeChat = { mount() { throw new Error('a dock that breaks'); } };
    let threw = false;
    try { fresh(broken).forEach((n) => n.onload()); } catch (e) { threw = true; }
    broken.home('mine').click(); broken.roomBar('rhome').click();
    check('a dock whose mount throws is nothing: no throw into the page, Home and the Room go on', !threw && broken.screen() === 'home' && broken.sockets.length === 1);
    const odd = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    odd.advance(0);
    let told = 0;
    odd.win.AntsHomeChat = { mount() { return { shown() { if (++told > 1) throw new Error('a dock that breaks later'); } }; } };
    fresh(odd).forEach((n) => n.onload());
    let oddThrew = false;
    try { odd.home('mine').click(); odd.roomBar('rhome').click(); } catch (e) { oddThrew = true; }
    check('a dock that breaks when it is told which screen shows (after it was mounted well) is nothing: Home and the Room go on, and each screen is told to it', !oddThrew && odd.screen() === 'home' && odd.sockets.length === 1 && told >= 3, told);
    const lone = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    lone.advance(0); fresh(lone).forEach((n) => n.onload());
    lone.home('mine').click();
    check('a script that came without the dock\'s global (a page whose chat.js was cut) is nothing as well', lone.screen() === 'room');
    const late = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    hubFor(late);
    late.advance(0);
    late.win.AntsHomeChat = H.loadFile('home/chat.js', late.scope).AntsHomeChat;
    fresh(late).slice(0, 1).forEach((n) => n.onload());
    check('a style that comes without the script (or the script without the style) mounts nothing, and there is no socket', late.$('home').children.length === 3);
    const nolink = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    nolink.env = nolink; nolink.body.appendChild = () => { throw new Error('no place for it'); };
    check('a page that cannot take the dock\'s two files does not throw', (() => { try { nolink.advance(0); return true; } catch (e) { return false; } })());
}
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 7. The source of the script
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const code = chatSource.replace(/\/\/.*/g, '');
    check('chat.js builds nothing from markup, runs nothing from text, keeps nothing in the browser and makes no link', !/innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function|srcdoc|localStorage|sessionStorage|indexedDB|document\.cookie|createElement\(['"]a['"]\)|setAttribute\(['"](href|src|style|on)/.test(code));
    check('chat.js names no other site (no http:// or https:// address at all) and loads nothing', !/https?:\/\//.test(code) && !/XMLHttpRequest|importScripts|\bimport\b/.test(code));
    check('the one thing that it asks for is a POST of the page itself to the two pages of the login (/hub/logout, /hub/forget): `fetch` is in one line, and it is that one', (code.match(/\bfetch\b/g) || []).length === 2 && code.indexOf('var f = env.fetch || win.fetch;') !== -1
        && /f\.call\(win, path, \{ method: 'POST', credentials: 'same-origin', cache: 'no-store', redirect: 'error' \}\)/.test(code) && /leave\(LOGOUT\)/.test(code) && /leave\(FORGET\)/.test(code) && (code.match(/\bleave\(/g) || []).length === 3);
}


// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 8. The login: the Log in button and the bar's chip, the account line with Log out and Forget me, and what the page may do about it (docs/HUB.md "Login", docs/HOME_SCREEN.md)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{   // a hub with a login, a visitor who is not logged in
    const r = rig({ chip: true });
    check('before the hub has spoken the chip is hidden (a page that cannot reach the hub offers no login), and so is the account line', r.chip.hidden === true && r.dock.slot.hidden === true);
    r.live(W({ login: true, you: null }));
    check('login:true and nobody named: the chip is shown, with the mark of Discord and "Log in" (not the circle), and says what it does', r.chip.hidden === false && one(r.chip, 'gc-dc').hidden === false && one(r.chip, 'gc-av').hidden === true && one(r.chip, 'gc-cn').textContent === 'Log in'
          && r.chip.getAttribute('aria-label') === 'Log in with Discord' && r.chip.children.length === 3);
    r.chip.click();
    same('a press on the chip is a navigation of the page to /hub/login?next=home (a full page, not a request): the one address, once; no socket message, nothing else', [r.went, r.sockets.length, r.s().sent.length, r.fetches.length], [['/hub/login?next=home'], 1, 0, 0]);
    one(r.root, 'gc-login').click();
    same('... and the Log in button of the dock goes to the same place', r.went, ['/hub/login?next=home', '/hub/login?next=home']);
    const room = rig({ chip: true, screen: () => 'room' });
    room.live(W({ login: true })); room.chip.click();
    const odd = rig({ chip: true, screen: () => '/evil?x=1' });
    odd.live(W({ login: true })); odd.chip.click(); one(odd.root, 'gc-login').click();
    same('from the Room the way back is the Room (next=room); a screen that is neither is Home: the address is made from one of two words, never from what the page says', [room.went, odd.went], [['/hub/login?next=room'], ['/hub/login?next=home', '/hub/login?next=home']]);
    check('a login that the page cannot start (a navigation that throws) throws nothing at the page', (() => { const q = rig({ chip: true }); q.win.location.assign = () => { throw new Error('blocked'); }; q.live(W({ login: true })); q.chip.click(); return q.errors.length === 1 && q.dock.state() === 'live'; })());
    r.s().fail();
    check('the hub goes: the chip goes with it (no login is offered to a page that cannot reach the hub), and comes back with the welcome', r.chip.hidden === true && (r.clock.advance(1500), r.s().open().recv(W({ login: true })), r.chip.hidden === false));
    const none = rig({ chip: true });
    none.live(W({ login: false, you: null }));
    check('login:false: no chip, no button on the screen but the fold button, nothing to press', none.chip.hidden === true && eq(buttonsShown(none.root), ['Fold chat']));
    const bare = rig();
    bare.live(W({ login: true }));
    check('a page that has no chip (home.js without the bar) still has the dock\'s button, and nothing throws', one(bare.root, 'gc-login').hidden === false && bare.errors.length === 0);
}
{   // a logged-in visitor
    const r = rig({ chip: true });
    r.live(W({ login: true, you: YOU, history: HISTORY }));
    check('named: the field and Send, no Log in button, the guest line gone, no word of "read-only" or "log in"', r.form().hidden === false && r.field().disabled === false && r.field().getAttribute('placeholder') === 'Say something to everyone' && one(r.root, 'gc-send').hidden === false
          && one(r.root, 'gc-login').hidden === true && one(r.root, 'gc-guest').hidden === true && r.note() === '');
    check('the place of the login says "Chatting as maple.leaf" with Log out and a small menu (More), in its own box (#gchat-login-slot), the name as text in a <bdi>', r.dock.slot.hidden === false && r.dock.slot.id === 'gchat-login-slot' && r.dock.slot.textContent.indexOf('Chatting as maple.leaf') === 0
          && one(r.dock.slot, 'gc-an').tagName === 'bdi' && one(r.dock.slot, 'gc-an').textContent === 'maple.leaf' && one(r.dock.slot, 'gc-link').textContent === 'Log out' && one(r.dock.slot, 'gc-more').getAttribute('aria-label') === 'More');
    check('the chip is the circle with the first letter and the name, and its words say who: "Chatting as maple.leaf"', r.chip.hidden === false && one(r.chip, 'gc-av').hidden === false && one(r.chip, 'gc-av').textContent === 'm' && one(r.chip, 'gc-dc').hidden === true && one(r.chip, 'gc-cn').textContent === 'maple.leaf'
          && r.chip.getAttribute('aria-label') === 'Chatting as maple.leaf');
    check('Forget me is not on the screen until the small menu is opened (and the menu is not either)', one(r.root, 'gc-menu').hidden === true && buttonsShown(r.root).indexOf('Forget me') === -1 && one(r.root, 'gc-more').getAttribute('aria-expanded') === 'false');
    one(r.root, 'gc-more').click();
    check('More opens it: a sentence that says what Forget me does (the lines go, every login goes, a ban or a mute stays) and the button; the toggle says it is open', one(r.root, 'gc-menu').hidden === false && one(r.root, 'gc-more').getAttribute('aria-expanded') === 'true'
          && one(r.root, 'gc-mnote').textContent === 'Deletes your lines in the chat and logs you out everywhere. A ban or a mute stays.' && buttonsShown(r.root).indexOf('Forget me') !== -1);
    r.root.fire('keydown', { key: 'Escape' });
    check('Escape puts the small menu away first (and the focus goes back to More), and the panel stays; the next Escape folds the panel', one(r.root, 'gc-menu').hidden === true && r.root.getAttribute('data-open') === 'true' && r.env.focused === one(r.root, 'gc-more') && (r.root.fire('keydown', { key: 'Escape' }), r.root.getAttribute('data-open') === 'false'));
    r.dock.open(true);
    r.chip.click();
    check('a press on the name chip opens the dock if it is folded and puts the focus on Log out (a chip that does nothing is a dead button)', r.root.getAttribute('data-open') === 'true' && r.env.focused === one(r.root, 'gc-link'));
    r.dock.open(false); r.chip.click();
    check('... also from a folded dock', r.root.getAttribute('data-open') === 'true' && r.went.length === 0);
    r.s().fail();
    check('the link goes: the account line and the chip go until the hub speaks again (a Log out now could not reach it)', r.dock.slot.hidden === true && r.chip.hidden === true);
}
{   // Log out
    const r = rig({ chip: true });
    const first = r.live(W({ login: true, you: YOU, history: HISTORY }));
    one(r.root, 'gc-link').click();
    same('Log out is ONE POST of the page to /hub/logout, same origin, with its cookie, never cached, no redirect followed, and nothing else (no body, no header of its own)', [r.fetches.length, r.fetches[0].path, r.fetches[0].init], [1, '/hub/logout', { method: 'POST', credentials: 'same-origin', cache: 'no-store', redirect: 'error' }]);
    check('while it is on its way both buttons are off (a second press is nothing)', one(r.root, 'gc-link').disabled === true && one(r.root, 'gc-forget').disabled === true && (one(r.root, 'gc-link').click(), r.fetches.length === 1) && r.sockets.length === 1);
    r.answer({ ok: true, status: 200 });
    check('answered: the socket that was let in with the login is dropped and a new one is made AT ONCE (no wait of a second): the page asks to be told who it is now', first.closes === 1 && r.sockets.length === 2 && r.s() !== first && r.s().url === 'wss://play.test/hub/ws' && r.clock.timers.every((t) => t.fn.toString().length > 0));
    check('... the person is nobody on the screen at once (no field to write in, the Log in button, the chip says Log in), whatever the hub says next', r.field().disabled === true && one(r.root, 'gc-login').hidden === false && r.dock.slot.hidden === true && r.chip.hidden === false && one(r.chip, 'gc-cn').textContent === 'Log in' && r.root.getAttribute('data-you') === 'no');
    check('... and a say() in that moment sends nothing (the new socket is not open): "Chat is offline"', r.dock.say('hello') === 'offline' && first.sent.filter((x) => x.indexOf('say') !== -1).length === 0);
    r.s().open().recv(W({ login: true, you: null, history: HISTORY }));
    check('the new welcome has nobody named: read-only with the Log in button; the buttons of the account are not on the screen; the old socket was never written to again', r.field().disabled === true && buttonsShown(r.root).join() === 'Fold chat,Log in' && first.sent.length === 0 && r.dock.state() === 'live' && r.errors.length === 0);
}
{   // Log out that does not work
    const refusals = {
        'a hub that says no (the answer is not ok)': (r) => r.answer({ ok: false, status: 403 }),
        'no answer at all (the network)': (r) => r.refuse(),
        'an answer that is nothing': (r) => r.answer(undefined),
        'an answer that says ok in words': (r) => r.answer({ ok: 'yes' }),
    };
    Object.keys(refusals).forEach((label) => {
        const r = rig({ chip: true });
        const first = r.live(W({ login: true, you: YOU }));
        one(r.root, 'gc-link').click();
        refusals[label](r);
        check(label + ': "That did not work, try again" for a few seconds, the visitor is still who they were, the socket stays, the buttons are on again', r.note() === 'That did not work, try again' && r.field().disabled === false && first.closes === 0 && r.sockets.length === 1 && one(r.root, 'gc-link').disabled === false && r.dock.slot.hidden === false);
        r.clock.advance(6001);
        check('... and the words go', r.note() === '');
    });
    const t = rig({ fetchThrows: true });
    t.live(W({ login: true, you: YOU })); one(t.root, 'gc-link').click();
    check('a fetch that throws is the same: the words, nothing dropped, nothing thrown at the page', t.note() === 'That did not work, try again' && t.sockets.length === 1 && t.errors.length === 0 && one(t.root, 'gc-link').disabled === false);
    const n = rig({ noFetch: true });
    n.live(W({ login: true, you: YOU })); one(n.root, 'gc-link').click();
    check('a browser without fetch: the same words (it cannot log out; it can still read and write)', n.note() === 'That did not work, try again' && n.field().disabled === false && n.sockets.length === 1);
}
{   // Forget me
    const r = rig({ chip: true });
    const first = r.live(W({ login: true, you: YOU, history: HISTORY }));
    one(r.root, 'gc-more').click();
    one(r.root, 'gc-forget').click();
    same('Forget me is ONE POST of the page to /hub/forget, the same kind of request as Log out', [r.fetches.length, r.fetches[0].path, r.fetches[0].init], [1, '/hub/forget', { method: 'POST', credentials: 'same-origin', cache: 'no-store', redirect: 'error' }]);
    r.s().recv({ t: 'del', id: 1 }); r.s().recv({ t: 'del', id: 2 });
    r.answer({ ok: true });
    r.s().open().recv(W({ login: true, you: null, history: HISTORY.slice(2) }));
    check('the lines of the account are taken off the screen as the hub says (del), the visitor is a reader after it, the small menu is shut, and the log holds what the hub keeps', first.closes === 1 && r.rowTexts().join('|') === 'juniper: joining!' && r.field().disabled === true && one(r.root, 'gc-menu').hidden === true && r.errors.length === 0);
    const two = rig();
    two.live(W({ login: true, you: YOU }));
    one(two.root, 'gc-link').click();
    one(two.root, 'gc-more').click(); one(two.root, 'gc-forget').click();
    check('Forget me while Log out is on its way is not a second request (one thing at a time)', two.fetches.length === 1);
}
{   // the name is text, whatever it is
    const hostile = ['<img src=x onerror=alert(1)>', '\u202eevil\u202c', 'x'.repeat(80), '\ud83d\ude00smile', 'a\u200bb'];
    hostile.forEach((name) => {
        const r = rig({ chip: true });
        r.live(W({ login: true, you: { name, mod: false } }));
        const shown = one(r.dock.slot, 'gc-an').textContent, circle = one(r.chip, 'gc-av').textContent, named = one(r.chip, 'gc-cn').textContent;
        check('a name from the hub is shown as the text it is, cleaned and cut like a line\'s: ' + JSON.stringify(name).slice(0, 30), shown === named && shown.length <= 33 && !/[\u202a-\u202e\u200b\u3164\u2800]/.test(shown + circle) && circle.length <= 2
              && r.env.innerHTMLWrites.length === 0 && r.errors.length === 0 && r.chip.all().every((e) => Object.keys(e.attributes).every((k) => ['class', 'aria-label'].indexOf(k) !== -1)));
    });
    const r = rig({ chip: true });
    r.live(W({ login: true, you: { name: '\ud83d\ude00smile', mod: false } }));
    check('the circle holds the first CODE POINT of the name (a pair of surrogates is not cut in two)', one(r.chip, 'gc-av').textContent === '\ud83d\ude00');
    const q = rig({ chip: true });
    q.live(W({ login: true, you: { name: '\u3164\u2800\ufe0f', mod: false } }));
    check('a name of nothing that shows is nobody: the visitor is a reader, the account line is not shown', q.dock.slot.hidden === true && q.field().disabled === true);
}
{   // the words are the agreed ones, and the page reads no cookie
    check('the words of the login: "Log in", "Log in with Discord", "Chatting as ", "Log out", "Forget me"', [Pure.TEXT.login, Pure.TEXT.loginWith, Pure.TEXT.chatting, Pure.TEXT.logout, Pure.TEXT.forget].join('|') === 'Log in|Log in with Discord|Chatting as |Log out|Forget me');
    check('chat.js never touches the cookie, a token or the address of Discord', !/document\.cookie|discord\.com|Authorization|access_token|\.href\b|location\.search|location\.hash/.test(chatSource.replace(/\/\/.*/g, '')));
}

console.log('home chat: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
