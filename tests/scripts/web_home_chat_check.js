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
    r.answers = () => r.rows().filter((x) => cls(x).indexOf('gc-private') !== -1).map((x) => x.children.slice(1).map((c) => c.textContent).join('\n'));       // (the private answers of the screen: the lines of each, after its "Only you" tag)
    r.pill = () => { const p = one(dock.root, 'gc-unread'); return p.hidden ? '' : p.textContent; };
    r.on = () => one(dock.root, 'gc-on').textContent;
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
    same('lineOf: a good line is cleaned, the mod flag is a boolean true only', [P.lineOf({ id: 4, who: 'a', text: ' hi  there ', mod: true }), P.lineOf({ id: 4, who: 'a', text: 'x', mod: 'yes' }).mod, P.lineOf({ id: 0, who: 'a', text: 'x' }).id], [{ id: 4, who: 'a', text: 'hi there', mod: true, role: 'mod', sys: false, k: '', u: '' }, false, 0]);
    same('lineOf: not a line when the id is not a whole number, the name or text is not a string, or nothing is left of the text',
        [{ who: 'a', text: 'x' }, { id: '4', who: 'a', text: 'x' }, { id: -1, who: 'a', text: 'x' }, { id: 1.5, who: 'a', text: 'x' }, { id: 4, who: 5, text: 'x' }, { id: 4, who: 'a', text: 5 }, { id: 4, who: 'a', text: ' \u202e \u200b ' }, null, 'x', []].map((m) => P.lineOf(m)),
        [null, null, null, null, null, null, null, null, null, null]);
    same('lineOf: a name of nothing that shows (fillers, variation selectors, tag characters) is "?", a line of marks only is no line, an emoji with its selector is one', [P.lineOf({ id: 1, who: '\u3164\u2800\ufe0f\u180b\udb40\udc41', text: 'x' }).who, P.lineOf({ id: 1, who: 'a', text: '\ufe0f\u0301 ' }), P.lineOf({ id: 1, who: 'a', text: '\u3164\u2800' }), P.lineOf({ id: 1, who: 'a', text: '\u2764\ufe0f' }).text, P.lineOf({ id: 1, who: 'a', text: 'e\u0301' }).text], ['?', null, null, '\u2764\ufe0f', 'e\u0301']);
    same('lineOf: a name over 32 and a line over 200 code points are cut with "…"; a name with nothing left is "?"', [P.lineOf({ id: 1, who: 'n'.repeat(60), text: 't'.repeat(5000) }).who, P.lineOf({ id: 1, who: 'n'.repeat(60), text: 't'.repeat(5000) }).text.length, P.lineOf({ id: 1, who: '\u202e', text: 'x' }).who],
        ['n'.repeat(32) + '…', 201, '?']);
    same('wordOf: the words of the codes; "log in" only where the hub has a login; an unknown code, or a name of Object, is the generic word', [P.wordOf('rate', true), P.wordOf('login', true), P.wordOf('login', false), P.wordOf('muted', false), P.wordOf('banned'), P.wordOf('long'), P.wordOf('empty'), P.wordOf('young'), P.wordOf('bad'), P.wordOf('quota'),
        P.wordOf('what'), P.wordOf(undefined), P.wordOf(5), P.wordOf('constructor'), P.wordOf('__proto__'), P.wordOf('toString'), P.wordOf('hasOwnProperty')],
        ['Slow down a little', 'Log in with Discord to chat', 'Chat is read-only for now', 'You cannot chat for a while', 'You cannot chat here', 'That line is too long', 'Write something first', 'Your account is too new to chat yet', 'That line could not be sent', 'You have chatted a lot, try again later',
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
          && one(box, 'gc-title').textContent === 'Global chat' && r.env.innerHTMLWrites.length === 0);
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
    const words = { rate: 'Slow down a little', muted: 'You cannot chat for a while', banned: 'You cannot chat here', long: 'That line is too long', empty: 'Write something first', young: 'Your account is too new to chat yet', bad: 'That line could not be sent', quota: 'You have chatted a lot, try again later' };
    Object.keys(words).forEach((code) => {
        const r = rig(); r.live(W({ you: YOU, login: true })); r.dock.say('hello');
        r.s().recv({ t: 'err', code });
        check('err ' + code + ': "' + words[code] + '" in the note, which goes after a few seconds', r.note() === words[code] && (r.clock.advance(5900), r.note() === words[code]) && (r.clock.advance(200), r.note() === (code === 'banned' ? 'You are banned from the chat. You can still read it.' : '')));       // (a refusal for a ban is the one that stays: the person is banned, and the field is off)
    });
    const a = rig(); a.live(W({ you: YOU, login: true })); a.dock.say('hello'); a.s().recv({ t: 'err', code: 'login' });
    const b = rig(); b.live(W({ you: YOU, login: false })); b.dock.say('hello'); b.s().recv({ t: 'err', code: 'login' });
    const c = rig(); c.live(W({ you: YOU, login: true })); c.dock.say('hello'); ['what', 5, null, undefined, {}, 'constructor', '__proto__', 'here'].forEach((code) => c.s().recv({ t: 'err', code }));
    const cNote = c.note(); c.s().recv({ t: 'err', code: 'rate' });
    same('err login: "Log in with Discord to chat" where the hub has a login, "Chat is read-only for now" where it has none; a code that is not one of the hub\'s answers to a line (a refused `here` has its own: "here") is no answer to a line: no word, and the line still waits for its own',
        [a.note(), b.note(), cNote, c.note(), c.field().value], ['Log in with Discord to chat', 'Chat is read-only for now', '', 'Slow down a little', 'hello']);
    const noLine = rig(); noLine.live(W({ you: YOU }));
    ['rate', 'bad', 'login', 'muted', 'banned', 'young'].forEach((code) => noLine.s().recv({ t: 'err', code }));
    same('an err when no line of this page is in flight is not for a line of this page: no word, nothing put in the field', [noLine.note(), noLine.field().value], ['', '']);
    const lateErr = rig(); lateErr.live(W({ you: YOU })); lateErr.dock.say('hello'); lateErr.clock.advance(9000); lateErr.s().recv({ t: 'err', code: 'rate' });
    same('(a line that had no answer for 8 s is not waited for: a late err is nobody\'s)', [lateErr.note(), lateErr.field().value], ['', '']);
    const badCommand = rig(); badCommand.live(W({ you: { name: 'maple.leaf', mod: true } })); badCommand.dock.say('/mute nobody 5'); badCommand.s().recv({ t: 'err', code: 'bad' });
    same('a moderator\'s command that the hub refuses is answered privately (a line of this screen that nobody else is sent, and no note), with nothing put in the field, no code of the dock throws, and the dock stays live: it is in flight for a few seconds', [badCommand.answers(), badCommand.note(), badCommand.field().value, badCommand.dock.state(), badCommand.errors.length], [['That command is not right. Type /help to see the commands.'], '', '', 'live', 0]);
    const oldCommand = rig(); oldCommand.live(W({ you: { name: 'maple.leaf', mod: true } })); oldCommand.dock.say('/mute nobody 5'); oldCommand.clock.advance(9000); oldCommand.s().recv({ t: 'err', code: 'bad' });
    same('... and not 8 s later', [oldCommand.note(), oldCommand.answers()], ['', []]);
    const d = rig(); d.live(W({ you: YOU })); d.dock.say('hello'); d.s().recv({ t: 'err', code: 'rate' });
    check('a line that the hub refused comes back into the field (nothing typed is lost)', d.field().value === 'hello');
    d.field().value = 'new words'; d.dock.say('again'); d.s().recv({ t: 'err', code: 'bad' });
    check('... a field that has new words in it is left alone', d.field().value === 'new words');
    const f = rig(); f.live(W({ you: YOU })); f.dock.say('sent and shown'); f.s().recv(L(77, 'maple.leaf', 'sent and shown')); f.s().recv({ t: 'err', code: 'bad' });
    check('a line that came back as a line is not put back into the field by a later err', f.field().value === '');
    const g = rig(); g.live(W({ you: YOU }));
    same('a line that the hub refused (a rate, as when another tab of the account said the same text a moment ago) is not one that was said: the same text goes again 3 s later, and is sent (the page holds back only what the hub accepted)',
        [g.dock.say('hello'), (g.s().recv({ t: 'err', code: 'rate' }), g.clock.advance(3000), g.dock.say('hello')), g.s().sent.length], ['', '', 2]);
    const k = rig(); k.live(W({ you: YOU }));
    k.dock.say('A'); k.s().recv(L(9, 'maple.leaf', 'A')); k.clock.advance(2500); k.dock.say('B'); k.s().recv({ t: 'err', code: 'rate' }); k.clock.advance(2500);
    same('... an err forgets the refused text only: A (accepted, as a line) is still held back for 31 s, B may go again', [k.dock.say('A'), k.dock.say('B')], ['rate', '']);
    const q = rig(); q.live(W({ you: YOU }));
    q.dock.say('first'); q.s().recv({ t: 'err', code: 'rate' }); q.clock.advance(3000); q.dock.say('first');
    same('... and an err that comes for a text that was sent twice forgets it again (every refusal is its own)', [(q.s().recv({ t: 'err', code: 'rate' }), q.clock.advance(3000), q.dock.say('first')), q.s().sent.length], ['', 3]);
    const two = rig(); two.live(W({ you: YOU }));
    two.dock.say('first'); two.clock.advance(2500); two.dock.say('second');
    two.s().recv({ t: 'err', code: 'rate' });
    two.clock.advance(5000);
    same('two lines are in flight and the hub refuses: it refuses the OLDER one (it answers in order): that text goes back into the field and may be sent again, the other is still held back for 31 s', [two.field().value, two.dock.say('first'), two.dock.say('second')], ['first', '', 'rate']);
    const mix = rig(); mix.live(W({ you: YOU }));
    mix.dock.say('A'); mix.clock.advance(2500); mix.dock.say('B');
    mix.s().recv(L(40, 'maple.leaf', 'A'));
    mix.s().recv({ t: 'err', code: 'rate' });
    mix.clock.advance(5000);
    same('... the first came back as a line (answered), the second is refused: the second is the one that is forgotten, the first stays held back', [mix.field().value, mix.dock.say('B'), mix.dock.say('A')], ['B', '', 'rate']);
    const none = rig(); none.live(W({ you: YOU }));
    none.dock.say('X'); none.s().recv(L(41, 'maple.leaf', 'X')); none.dock.say('Y'); none.s().recv(L(42, 'maple.leaf', 'Y'));
    none.s().recv({ t: 'err', code: 'rate' });
    check('an err that comes when every line was answered refuses nothing: no text comes back into the field, nothing is forgotten', none.field().value === '' && none.dock.say('X') === 'rate');
    const errs = rig(); errs.live(W({ you: YOU }));
    errs.dock.say('A'); errs.clock.advance(2500); errs.dock.say('B');
    errs.s().recv({ t: 'err', code: 'rate' });
    const firstBack = errs.field().value;
    errs.field().value = '';
    errs.s().recv({ t: 'err', code: 'rate' });
    same('two lines are in flight and the hub refuses both, one after the other: A comes back, then B (each refusal takes its line off the list of those that wait)', [firstBack, errs.field().value], ['A', 'B']);
    const tabs = rig(); tabs.live(W({ you: YOU }));
    tabs.dock.say('A');
    tabs.s().recv(L(60, 'maple.leaf', 'said in another tab of the account'));
    tabs.s().recv({ t: 'err', code: 'rate' });
    same('a line of the same account that was said in another tab is nobody\'s answer here: the refusal that follows is still for A', tabs.field().value, 'A');
    const nfd = rig(); nfd.live(W({ you: YOU }));
    nfd.dock.say('café');
    nfd.s().recv(L(70, 'maple.leaf', 'café'));
    nfd.clock.advance(2500); nfd.dock.say('beta two');
    nfd.s().recv({ t: 'err', code: 'rate' });
    same('a line that the hub composes (NFC) before it says it back is the line that was sent: its echo answers it, and the next refusal is for the next line, not for this one (the field gets "beta two")', nfd.field().value, 'beta two');
    const decomposed = rig(); decomposed.live(W({ you: YOU }));
    decomposed.dock.say('caf\u00e9');
    decomposed.s().recv(L(74, 'maple.leaf', 'cafe\u0301'));
    decomposed.clock.advance(2500); decomposed.dock.say('beta two');
    decomposed.s().recv({ t: 'err', code: 'rate' });
    same('(the two sides are compared composed: a hub that says a composed line back decomposed answers it too)', decomposed.field().value, 'beta two');
    const mod = rig(); mod.live(W({ you: { name: 'maple.leaf', mod: true } }));
    mod.dock.say('/mute victim 1'); mod.clock.advance(2500); mod.dock.say('beta two');
    mod.s().recv({ t: 'err', code: 'rate' });
    same('a command of a moderator ("/mute victim 1": the hub takes it and says nothing, no echo and no err) is not a line that waits for an answer: the next refusal is for "beta two", and Enter does not issue the command again', mod.field().value, 'beta two');
    const slash = rig(); slash.live(W({ you: YOU }));
    slash.dock.say('/shrug'); slash.s().recv({ t: 'err', code: 'rate' });
    same('(a line that begins with a slash from a visitor who is no moderator is a line like any other: a refusal gives it back)', slash.field().value, '/shrug');
    const modOther = rig(); modOther.live(W({ you: { name: 'maple.leaf', mod: true } }));
    modOther.dock.say('not a command'); modOther.s().recv({ t: 'err', code: 'rate' });
    same('(a moderator\'s line that is no command is a line like any other)', modOther.field().value, 'not a command');
    const cut = rig(); cut.live(W({ you: YOU }));
    cut.dock.say('hi 🫩');
    cut.s().recv(L(71, 'maple.leaf', 'hi'));
    cut.clock.advance(2500); cut.dock.say('beta two');
    cut.s().recv({ t: 'err', code: 'rate' });
    same('a line that the hub says back with characters taken out (it takes out one that no Unicode version it knows has assigned) is answered by that echo: the line that is refused next is "beta two"', cut.field().value, 'beta two');
    const order = rig(); order.live(W({ you: YOU }));
    order.dock.say('hello world'); order.clock.advance(2500); order.dock.say('hello');
    order.s().recv(L(75, 'maple.leaf', 'hello'));
    order.s().recv({ t: 'err', code: 'rate' });
    same('an echo that is the same text as a line that waits answers that line, not an older one that it is only part of ("hello" is part of "hello world": the refusal is for "hello world")', order.field().value, 'hello world');
    const part = rig(); part.live(W({ you: YOU }));
    part.dock.say('hello \ud83e\udee9world'); part.clock.advance(2500); part.dock.say('beta two');
    part.s().recv(L(76, 'maple.leaf', 'hello world'));
    part.s().recv({ t: 'err', code: 'rate' });
    same('an echo that is part of the oldest line that waits (with characters taken out) answers it: "hello world" is the echo of "hello 🫩world", and the refusal is for "beta two"', part.field().value, 'beta two');
    const shortPart = rig(); shortPart.live(W({ you: YOU }));
    shortPart.dock.say('hello world');
    shortPart.s().recv(L(81, 'maple.leaf', 'lo'));
    shortPart.s().recv({ t: 'err', code: 'rate' });
    same('a short echo that is part of the oldest line that waits ("lo" of "hello world": a line of another tab of the account, which the hub says too) answers nothing of this tab\'s: the refusal gives back "hello world"', shortPart.field().value, 'hello world');
    const half = rig(); half.live(W({ you: YOU }));
    half.dock.say('hello wo');
    half.s().recv(L(82, 'maple.leaf', 'hell'));
    half.s().recv({ t: 'err', code: 'rate' });
    same('an echo of half the length of the oldest line that waits (4 of 8 characters) answers it (the rule is "at least half")', half.field().value, '');
    const under = rig(); under.live(W({ you: YOU }));
    under.dock.say('hello wor');
    under.s().recv(L(83, 'maple.leaf', 'hell'));
    under.s().recv({ t: 'err', code: 'rate' });
    same('... and one of less than half (4 of 9) does not', under.field().value, 'hello wor');
    const pair = rig(); pair.live(W({ you: YOU }));
    pair.dock.say('hello world'); pair.clock.advance(2500); pair.dock.say('hi');
    pair.s().recv(L(84, 'maple.leaf', 'hello wrld'));
    pair.s().recv({ t: 'err', code: 'rate' });
    same('with two lines that wait (the oldest "hello world", the younger "hi"), an echo that is part of the OLDEST one and over half of it ("hello wrld", 10 of 11) answers it: the rule is measured on the oldest line, not on the newest, and the refusal that follows is for "hi"', pair.field().value, 'hi');
    const younger = rig(); younger.live(W({ you: YOU }));
    younger.dock.say('hello world'); younger.clock.advance(2500); younger.dock.say('xyz abc');
    younger.s().recv(L(85, 'maple.leaf', 'xz abc'));
    younger.s().recv({ t: 'err', code: 'rate' });
    same('an echo that is part of the NEWER line only ("xz abc" of "xyz abc", 6 of 7) is no answer to the oldest one (a line of another tab of the account): the refusal gives back "hello world"', younger.field().value, 'hello world');
    younger.field().value = '';
    younger.s().recv({ t: 'err', code: 'rate' });
    same('... and the younger line still waits: the next refusal gives it back', younger.field().value, 'xyz abc');
    const shortPair = rig(); shortPair.live(W({ you: YOU }));
    shortPair.dock.say('hello world'); shortPair.clock.advance(2500); shortPair.dock.say('hi');
    shortPair.s().recv(L(86, 'maple.leaf', 'lo'));
    shortPair.s().recv({ t: 'err', code: 'rate' });
    const refusedFirst = shortPair.field().value;
    shortPair.field().value = '';
    shortPair.s().recv({ t: 'err', code: 'rate' });
    same('a short echo ("lo": under half of "hello world", no part of "hi") answers neither of two lines that wait: the refusals give back "hello world", then "hi"', [refusedFirst, shortPair.field().value], ['hello world', 'hi']);
    const idle = rig(); idle.live(W({ you: YOU }));
    idle.s().recv({ t: 'err', code: 'rate' });
    check('an err with nothing waiting for an answer is not for a line of this page: it takes nothing back, says nothing, and no code of the dock throws (the page\'s guard would hide a throw: the dock\'s error hook is looked at)', idle.field().value === '' && idle.note() === '' && idle.errors.length === 0, String(idle.errors[0]));
    const stranger = rig(); stranger.live(W({ you: YOU }));
    stranger.dock.say('hello world'); stranger.s().recv(L(77, 'maple.leaf', 'goodbye'));
    stranger.s().recv({ t: 'err', code: 'rate' });
    same('an echo that is no part of the oldest line (a line of another tab of the account) answers nothing of this tab\'s', stranger.field().value, 'hello world');
    const dup = rig(); dup.live(W({ you: YOU }));
    dup.dock.say('ab'); dup.s().recv(L(79, 'maple.leaf', 'aab'));
    dup.s().recv({ t: 'err', code: 'rate' });
    same('an echo is part of a line only when its characters stand in the line in the same order, each at its own place ("aab" is not part of "ab")', dup.field().value, 'ab');
    const ghost = rig(); ghost.live(W({ you: YOU }));
    ghost.dock.say('ghost'); ghost.clock.advance(2500); ghost.dock.say('beta two');
    ghost.clock.advance(5499);
    ghost.s().recv({ t: 'err', code: 'rate' });
    same('a line that nobody answers (no echo, no refusal) is waited for for 8 s: a refusal 7,999 ms after it was sent is still taken for it', ghost.field().value, 'ghost');
    const late = rig(); late.live(W({ you: YOU }));
    late.dock.say('ghost'); late.clock.advance(2500); late.dock.say('beta two');
    late.clock.advance(5500);
    late.s().recv({ t: 'err', code: 'rate' });
    same('... and 8,000 ms after it was sent nobody waits for it any more: the refusal is for "beta two"', late.field().value, 'beta two');
    const behind = rig(); behind.live(W({ you: YOU }));
    behind.dock.say('ghost'); behind.clock.advance(7000); behind.dock.say('hi 🫩'); behind.clock.advance(2000);
    behind.s().recv(L(78, 'maple.leaf', 'hi'));
    behind.dock.say('beta two');
    behind.s().recv({ t: 'err', code: 'rate' });
    same('a line that nobody answered does not stand in front of the one that comes after it: when the echo "hi" comes, the ghost of 9 s ago is no longer waited for, and "hi" answers "hi ...", not nobody', behind.field().value, 'beta two');
    const later = rig(); later.live(W({ you: YOU }));
    later.dock.say('zzz'); later.dock.say('café'); later.clock.advance(2500); later.dock.say('beta');
    later.s().recv(L(80, 'maple.leaf', 'café'));
    later.s().recv({ t: 'err', code: 'rate' });
    const laterFirst = later.field().value;
    later.field().value = '';
    later.s().recv({ t: 'err', code: 'rate' });
    same('two lines wait, the second one is composed by the hub (its echo is the composed text, the oldest line is still waiting): the echo answers the second, not the oldest; the refusals are for "zzz" and then for "beta"', [laterFirst, later.field().value], ['zzz', 'beta']);
    const gone = rig(); gone.live(W({ you: YOU }));
    gone.dock.say('lost line'); gone.s().fail(); gone.clock.advance(2500); gone.s().open().recv(W({ you: YOU }));
    gone.s().recv({ t: 'err', code: 'rate' });
    check('a line that was in flight when the link went has no answer to wait for: an err of the new link does not bring it back', gone.field().value === '');
    // a moderator's command waits in the same list as the lines, and the hub's answer is told to the send that it is for (`bad` is its word for a wrong command, any other code is a line's)
    const MOD = { name: 'maple.leaf', mod: true };
    const together = rig(); together.live(W({ you: MOD }));
    together.dock.say('/mute nobody 5'); together.dock.say('hello');
    together.s().recv({ t: 'err', code: 'bad' });
    same('a command and a line sent within one round trip, and the hub says `bad` to the command: the private answer is the command\'s, and nothing is put in the field (the line was taken)', [together.answers(), together.field().value], [['That command is not right. Type /help to see the commands.'], '']);
    together.s().recv(L(90, 'maple.leaf', 'hello'));
    same('... the line is shown once, as the line that it is, and the field is still empty', [together.rowTexts().filter((t) => t === 'maple.leaf: hello').length, together.field().value], [1, '']);
    together.clock.advance(2500); together.dock.say('again'); together.s().recv({ t: 'err', code: 'rate' });
    same('... and the next refusal of a line is for that line', together.field().value, 'again');
    const lineFirst = rig(); lineFirst.live(W({ you: MOD }));
    lineFirst.dock.say('hello'); lineFirst.dock.say('/mute nobody 5');
    lineFirst.s().recv({ t: 'err', code: 'rate' });
    const lineBack = lineFirst.field().value;
    lineFirst.s().recv({ t: 'err', code: 'bad' });
    same('a line and then a command, both refused (`rate` for the line, `bad` for the command): the line comes back, and the command does not take its place in the field', [lineBack, lineFirst.field().value, lineFirst.answers()], ['hello', 'hello', ['That command is not right. Type /help to see the commands.']]);
    const muted = rig(); muted.live(W({ you: MOD }));
    muted.dock.say('/mute nobody 5'); muted.dock.say('hello');
    muted.s().recv({ t: 'err', code: 'muted' });
    const mutedBack = muted.field().value;
    muted.field().value = '';
    muted.s().recv({ t: 'err', code: 'muted' });
    same('a command and a line, both refused with a code that is for either (muted): a code that is not `bad` is the line\'s first, then the command\'s, which is not put in the field', [mutedBack, muted.field().value, muted.note()], ['hello', '', 'You cannot chat for a while']);
    const alone = rig(); alone.live(W({ you: MOD }));
    alone.dock.say('/mute nobody 5'); alone.s().recv({ t: 'err', code: 'rate' });
    same('a command that waits alone is the refusal\'s whatever the code (a hub whose database failed says `rate` for it): the answer is told privately, nothing is put in the field', [alone.answers(), alone.note(), alone.field().value], [['You are going too fast, try again in a moment'], '', '']);
    const refusedCommand = rig(); refusedCommand.live(W({ you: MOD }));
    refusedCommand.dock.say('/mute nobody 5'); refusedCommand.s().recv({ t: 'err', code: 'bad' }); refusedCommand.clock.advance(2500);
    const takenCommand = rig(); takenCommand.live(W({ you: MOD }));
    takenCommand.dock.say('/mute victim 5'); takenCommand.clock.advance(2500);
    same('a command that the hub refused may be sent again at once, and one that it took is held back for 31 s like a line that it took', [refusedCommand.dock.say('/mute nobody 5'), takenCommand.dock.say('/mute victim 5')], ['', 'rate']);
    const behindCommand = rig(); behindCommand.live(W({ you: MOD }));
    behindCommand.dock.say('/mute victim 1'); behindCommand.dock.say('hi 🫩'); behindCommand.s().recv(L(91, 'maple.leaf', 'hi'));
    behindCommand.clock.advance(2500); behindCommand.dock.say('beta two'); behindCommand.s().recv({ t: 'err', code: 'rate' });
    same('a command stands before a line whose echo has a character taken out: the echo is still the answer of the oldest LINE ("hi" is "hi 🫩" without it), and the refusal that follows is for "beta two"', behindCommand.field().value, 'beta two');
    const inTime = rig(); inTime.live(W({ you: MOD }));
    inTime.dock.say('/mute nobody 5'); inTime.clock.advance(7999); inTime.s().recv({ t: 'err', code: 'bad' });
    const tooLate = rig(); tooLate.live(W({ you: MOD }));
    tooLate.dock.say('/mute nobody 5'); tooLate.clock.advance(8000); tooLate.s().recv({ t: 'err', code: 'bad' });
    same('a command that nobody answers is waited for for 8 s: a `bad` 7,999 ms after it was sent is its answer, and one 8,000 ms after it is nobody\'s', [inTime.answers(), tooLate.answers()], [['That command is not right. Type /help to see the commands.'], []]);
    const lostCommand = rig(); lostCommand.live(W({ you: MOD }));
    lostCommand.dock.say('/mute nobody 5'); lostCommand.s().fail(); lostCommand.clock.advance(2500); lostCommand.s().open().recv(W({ you: MOD }));
    lostCommand.s().recv({ t: 'err', code: 'bad' });
    same('a command that was in flight when the link went has no answer to wait for either: a `bad` of the new link is nobody\'s', [lostCommand.note(), lostCommand.field().value], ['', '']);
    const noCommand = rig(); noCommand.live(W({ you: YOU }));
    noCommand.dock.say('/shrug'); noCommand.dock.say('hello');
    noCommand.s().recv({ t: 'err', code: 'bad' });
    same('a line that begins with a slash from a visitor who is no moderator is a line, not a command: the `bad` is for the oldest line, "/shrug"', noCommand.field().value, '/shrug');
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
    r.s().recv(L(75, 'a', 'next'));
    check('the summary of a window is gone when the next window starts (it was told; the same words twice in a row must be told twice, and a text that does not change is not announced)', news().textContent === '' && r.log().getAttribute('aria-live') === 'polite');
    r.s().recv(L(76, 'a', 'next 2')); r.s().recv(L(77, 'a', 'next 3'));
    r.clock.advance(3000);
    check('... and the same words come again at the end of that window', news().textContent === 'One more message');
    for (let i = 0; i < 8; i++) { r.s().recv(L(80 + i, 'a', 'slow ' + i)); r.clock.advance(2000); }
    check('a line every 2 s for 16 s is never quiet and never summarized (and the last summary went when the first of them started its window)', r.log().getAttribute('aria-live') === 'polite' && news().textContent === '');
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
    same('lines that come while folded are unread (not the visitor\'s own), counted in the header and in the button\'s name; ', [r.pill(), r.fold().getAttribute('aria-label')], ['2', 'Open chat, 2 unread']);
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
    check('a phone starts with the dock folded to its header, and no unread count for the history', c.root.getAttribute('data-open') === 'false' && c.pill() === '');
    c.s().recv(L(30, 'pebble', 'hi')); c.s().recv(L(31, 'pebble', 'hi again'));
    check('lines make the header say so: "2"', c.pill() === '2');
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
    check('a phone held sideways (844 x 390) starts folded too; a window of 701 x 601 is a desktop; 700 wide or 600 high is not (the folded header, which a press opens)', sw.root.getAttribute('data-open') === 'false' && rig({ width: 701, height: 601 }).root.getAttribute('data-open') === 'true' && rig({ width: 700, height: 900 }).root.getAttribute('data-open') === 'false' && rig({ width: 1280, height: 600 }).root.getAttribute('data-open') === 'false');
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
const fresh = (env) => env.body.children.filter((c) => (c.tagName === 'script' || c.tagName === 'link') && /^home\/chat\.(js|css)$/.test(String(c.src || c.href || '')));       // (the files of the dock: the page asks for those of the Games list too, which the checks of that list own)
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
    check('a plain visit loads nothing of the chat before the first paint, opens no socket, and Home is as before', fresh(env).length === 0 && env.sockets.length === 0 && env.screen() === 'home' && env.$('home').children.length === 4);
    const hub = hubFor(env);
    env.advance(0);
    const parts = fresh(env);
    same('after the first paint the page asks for the style and the script of the dock, once, and nothing else', parts.map((n) => n.tagName + ':' + (n.href || n.src) + ':' + (n.rel || '')), ['link:home/chat.css:stylesheet', 'script:home/chat.js:']);
    check('... the script is async, so it never holds the page', parts[1].async === true && hub.length === 0 && env.$('home').children.length === 4);
    parts[1].onload();
    check('the dock mounts only when BOTH the style and the script are there (no unstyled dock)', env.$('home').children.length === 4 && hub.length === 0);
    env.win.AntsHomeChat = H.loadFile('home/chat.js', env.scope).AntsHomeChat;
    parts[0].onload();
    check('... then it is the last child of Home\'s div, one socket to the hub is opened, and the game\'s own socket is not touched', env.$('home').children.length === 5 && env.$('home').children[4].id === 'gchat' && hub.length === 1 && hub[0].url === 'wss://play.test/hub/ws' && env.sockets.length === 0);
    const dock = env.$('home').children[4];
    check('Home\'s own parts are where they were (the bar, the body, the card, the strip)', env.$('home').children.slice(0, 4).map((c) => c.tagName).join() === 'header,main,div,nav' && dock.getAttribute('data-open') === 'true');
    env.home('mine').click();
    check('My room still makes the room (the page\'s own socket), and the Room shows: the dock is told (it is not on the screen)', env.sockets.length === 1 && env.screen() === 'room' && hub.length === 1);
    hub[0].readyState = 1; hub[0].onmessage({ data: JSON.stringify(W({ you: YOU, history: HISTORY })) });
    hub[0].onmessage({ data: JSON.stringify(L(40, 'a', 'on the Room')) });
    check('a line that comes while the Room shows is unread, and the link stays', one(dock, 'gc-unread').hidden === false && one(dock, 'gc-unread').textContent === '1' && hub.length === 1);
    env.roomBar('rhome').click();
    check('Home again: the dock is read, the link is the same one', one(dock, 'gc-unread').hidden === true && hub.length === 1);
    env.advance(10);
    check('show(home) again does not load the dock a second time', fresh(env).length === 2 && env.$('home').children.length === 5);
}
{   // a dialog over the page, and the hand-off to the game, take the dock's keys and presses too (the focus does not go behind a modal)
    const env = runLobby('', { 'ants.name': 'Maya' }, { picker: true, stayHome: true });
    hubFor(env); env.advance(0); bring(env);
    const root = env.$('home').children[4];
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
    const lateRoot = late.$('home').children[4];
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
    check('a hub that cannot be opened: the dock says "Chat is offline"; Home is whole', down.$('home').children[4].id === 'gchat' && one(down.$('home').children[4], 'gc-note').textContent === 'Chat is offline' && down.screen() === 'home');
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
    check('a style that comes without the script (or the script without the style) mounts nothing, and there is no socket', late.$('home').children.length === 4);
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
          && r.chip.getAttribute('aria-label') === 'Log in with Discord' && r.chip.children.length === 4);       // (the mark, the circle, the name, and the role's word, which is hidden for a person who holds none)
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
    check('the chat panel has no account line: no "Chatting as", no Log out, no More button in it (the chip carries them)', r.root.textContent.indexOf('Chatting as') === -1 && !one(r.root, 'gc-out') && !one(r.root, 'gc-slot') && !one(r.root, 'gc-more') && !one(r.root, 'gc-acct') && buttonsShown(r.root).join() === 'Fold chat,Send');
    check('the chip is the circle with the first letter and the name, and its words say who: "Chatting as maple.leaf"; it is a button that opens a dropdown (aria-haspopup), shut', r.chip.hidden === false && one(r.chip, 'gc-av').hidden === false && one(r.chip, 'gc-av').textContent === 'm' && one(r.chip, 'gc-dc').hidden === true && one(r.chip, 'gc-cn').textContent === 'maple.leaf'
          && r.chip.getAttribute('aria-label') === 'Chatting as maple.leaf' && r.chip.getAttribute('aria-haspopup') === 'true' && r.chip.getAttribute('aria-expanded') === 'false' && r.chip.getAttribute('aria-controls') === 'gchat-login-slot');
    check('the dropdown is its own box (#gchat-login-slot) outside the panel (in the body), shut: "Chatting as maple.leaf" with the name as text in a <bdi>, Log out, and Forget me with its sentence', r.dock.slot.hidden === true && r.dock.slot.id === 'gchat-login-slot' && !r.root.contains(r.dock.slot) && r.dock.slot.textContent.indexOf('Chatting as maple.leaf') === 0
          && one(r.dock.slot, 'gc-an').tagName === 'bdi' && one(r.dock.slot, 'gc-an').textContent === 'maple.leaf' && one(r.dock.slot, 'gc-out').textContent === 'Log out' && one(r.dock.slot, 'gc-forget').textContent === 'Forget me'
          && one(r.dock.slot, 'gc-mnote').textContent === 'Deletes your lines in the chat and logs you out everywhere. A ban or a mute stays.');
    r.chip.click();
    check('a press on the chip opens the dropdown (it is not a dead button) and puts the focus on Log out; the chip says it is open; the panel is as it was (a phone\'s view is not changed)', r.dock.slot.hidden === false && r.chip.getAttribute('aria-expanded') === 'true' && r.env.focused === one(r.dock.slot, 'gc-out') && r.root.getAttribute('data-open') === 'true' && r.went.length === 0);
    const escape = () => (r.doc.listeners.keydown || []).forEach((fn) => fn({ key: 'Escape' }));
    escape();
    check('Escape closes it and the focus goes back to the chip, and the panel stays', r.dock.slot.hidden === true && r.chip.getAttribute('aria-expanded') === 'false' && r.env.focused === r.chip && r.root.getAttribute('data-open') === 'true');
    r.chip.click(); r.chip.focus(); escape();
    check('Escape closes it with the focus on the chip too (not only inside the dropdown), and the panel is not folded by that Escape', r.dock.slot.hidden === true && r.root.getAttribute('data-open') === 'true' && r.env.focused === r.chip);
    r.chip.click(); r.chip.click();
    check('a second press on the chip closes it', r.dock.slot.hidden === true && r.chip.getAttribute('aria-expanded') === 'false');
    const docClick = (target) => (r.doc.listeners.click || []).forEach((fn) => fn({ target }));
    r.chip.click(); docClick(r.field());
    check('a press anywhere else puts it away (a press inside it does not)', r.dock.slot.hidden === true && (r.chip.click(), docClick(one(r.dock.slot, 'gc-an')), r.dock.slot.hidden === false));
    r.dock.open(false);
    check('the dock may be folded: the chip and its dropdown work all the same', r.root.getAttribute('data-open') === 'false' && r.dock.slot.hidden === false && r.went.length === 0);
    r.dock.shown(false);
    check('the Room (Home is not shown) closes the dropdown', r.dock.slot.hidden === true && r.chip.getAttribute('aria-expanded') === 'false');
    r.s().fail();
    check('the link goes: the account line and the chip go until the hub speaks again (a Log out now could not reach it)', r.dock.slot.hidden === true && r.chip.hidden === true);
}
{   // Log out
    const r = rig({ chip: true });
    const first = r.live(W({ login: true, you: YOU, history: HISTORY }));
    r.chip.click();
    one(r.dock.slot, 'gc-out').click();
    same('Log out is ONE POST of the page to /hub/logout, same origin, with its cookie, never cached, no redirect followed, and nothing else (no body, no header of its own)', [r.fetches.length, r.fetches[0].path, r.fetches[0].init], [1, '/hub/logout', { method: 'POST', credentials: 'same-origin', cache: 'no-store', redirect: 'error' }]);
    check('while it is on its way both buttons are off (a second press is nothing)', one(r.dock.slot, 'gc-out').disabled === true && one(r.dock.slot, 'gc-forget').disabled === true && (one(r.dock.slot, 'gc-out').click(), r.fetches.length === 1) && r.sockets.length === 1);
    r.answer({ ok: true, status: 200 });
    check('answered: the socket that was let in with the login is dropped and a new one is made AT ONCE (no wait of a second): the page asks to be told who it is now', first.closes === 1 && r.sockets.length === 2 && r.s() !== first && r.s().url === 'wss://play.test/hub/ws' && r.clock.timers.every((t) => t.fn.toString().length > 0));
    check('after Log out the dropdown is gone from under the focus and the focus is on the chip (the Log in chip now), not nowhere', r.dock.slot.hidden === true && r.env.focused === r.chip);
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
        r.chip.click();
        one(r.dock.slot, 'gc-out').click();
        refusals[label](r);
        check(label + ': "That did not work, try again" for a few seconds, the visitor is still who they were, the socket stays, the buttons are on again', r.note() === 'That did not work, try again' && r.field().disabled === false && first.closes === 0 && r.sockets.length === 1 && one(r.dock.slot, 'gc-out').disabled === false && r.dock.slot.hidden === false);
        r.clock.advance(6001);
        check('... and the words go', r.note() === '');
    });
    const t = rig({ fetchThrows: true });
    t.live(W({ login: true, you: YOU })); one(t.dock.slot, 'gc-out').click();
    check('a fetch that throws is the same: the words, nothing dropped, nothing thrown at the page', t.note() === 'That did not work, try again' && t.sockets.length === 1 && t.errors.length === 0 && one(t.dock.slot, 'gc-out').disabled === false);
    const n = rig({ noFetch: true });
    n.live(W({ login: true, you: YOU })); one(n.dock.slot, 'gc-out').click();
    check('a browser without fetch: the same words (it cannot log out; it can still read and write)', n.note() === 'That did not work, try again' && n.field().disabled === false && n.sockets.length === 1);
}
{   // Forget me
    const r = rig({ chip: true });
    const first = r.live(W({ login: true, you: YOU, history: HISTORY }));
    (r.chip && r.chip.click());
    const forget = one(r.dock.slot, 'gc-forget');
    forget.click();
    check('Forget me asks once more first: the first press sends nothing and the control says "Are you sure? Press again"', r.fetches.length === 0 && forget.textContent === 'Are you sure? Press again' && forget.getAttribute('data-sure') === 'true');
    forget.blur();
    check('... a press elsewhere (the focus leaves it) puts the question back, and the next press asks again', forget.textContent === 'Forget me' && (forget.click(), r.fetches.length === 0 && forget.textContent === 'Are you sure? Press again'));
    r.dock.slot.hidden === false && (r.doc.listeners.keydown || []).forEach((fn) => fn({ key: 'Escape' }));
    check('... Escape (the dropdown goes) puts the question back too, and nothing was sent', forget.textContent === 'Forget me' && r.fetches.length === 0 && r.dock.slot.hidden === true);
    r.chip.click();
    forget.click(); forget.click();
    check('a double press on Forget me counts once: nothing is sent, the question is still there', r.fetches.length === 0 && forget.textContent === 'Are you sure? Press again');
    r.clock.advance(450); forget.click();
    same('Forget me is ONE POST of the page to /hub/forget, the same kind of request as Log out', [r.fetches.length, r.fetches[0].path, r.fetches[0].init], [1, '/hub/forget', { method: 'POST', credentials: 'same-origin', cache: 'no-store', redirect: 'error' }]);
    r.s().recv({ t: 'del', id: 1 }); r.s().recv({ t: 'del', id: 2 });
    r.answer({ ok: true });
    r.s().open().recv(W({ login: true, you: null, history: HISTORY.slice(2) }));
    check('the lines of the account are taken off the screen as the hub says (del), the visitor is a reader after it, the small menu is shut, and the log holds what the hub keeps', first.closes === 1 && r.rowTexts().join('|') === 'juniper: joining!' && r.field().disabled === true && r.dock.slot.hidden === true && r.errors.length === 0);
    const two = rig();
    two.live(W({ login: true, you: YOU }));
    one(two.dock.slot, 'gc-out').click();
    (two.chip && two.chip.click()); one(two.dock.slot, 'gc-forget').click(); two.clock.advance(450); one(two.dock.slot, 'gc-forget').click();
    check('Forget me while Log out is on its way is not a second request (one thing at a time)', two.fetches.length === 1);
}
{   // the name is text, whatever it is
    const hostile = ['<img src=x onerror=alert(1)>', '\u202eevil\u202c', 'x'.repeat(80), '\ud83d\ude00smile', 'a\u200bb'];
    hostile.forEach((name) => {
        const r = rig({ chip: true });
        r.live(W({ login: true, you: { name, mod: false } }));
        const shown = one(r.dock.slot, 'gc-an').textContent, circle = one(r.chip, 'gc-av').textContent, named = one(r.chip, 'gc-cn').textContent;
        check('a name from the hub is shown as the text it is, cleaned and cut like a line\'s: ' + JSON.stringify(name).slice(0, 30), shown === named && shown.length <= 33 && !/[\u202a-\u202e\u200b\u3164\u2800]/.test(shown + circle) && circle.length <= 2
              && r.env.innerHTMLWrites.length === 0 && r.errors.length === 0 && r.chip.all().every((e) => Object.keys(e.attributes).every((k) => ['class', 'aria-label', 'aria-haspopup', 'aria-controls', 'aria-expanded'].indexOf(k) !== -1)));
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

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 9. The players list speaks through this socket (web/home/players.js, docs/HOME_SCREEN.md "The players list"): tap(fn) hears the welcome, the list and the loss of the link, send(obj) writes a frame
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({});
    const heard = [];
    const word = (m) => (m && typeof m === 'object' ? m.t : m);
    r.dock.tap((kind, m) => heard.push([kind, word(m)]));
    same('a piece that taps a dock whose socket is not open yet hears nothing', heard, []);
    r.s().open().recv(W({ presence: true, you: YOU }));
    same('the welcome is told, once, with the welcome itself', heard, [['live', 'welcome']]);
    r.s().recv({ t: 'who', full: true, n: 1, g: 0, more: 0, rows: [] });
    same('a frame of the list is told to it (and is not a line, not a mistake: the log stays as it was)', [heard.slice(1), r.rows().length, r.note()], [[['who', 'who']], 0, '']);
    r.s().recv({ t: 'nonsense' });
    same('a kind that nobody knows is not told to it', heard.length, 2);
    r.dock.shown(false); r.dock.shown(true);
    same('Home showing or not is told to it', heard.slice(2), [['shown', false], ['shown', true]]);
    let late = [];
    r.dock.tap((kind, m) => late.push([kind, word(m)]));
    same('a piece that comes after the welcome is told it at once (the welcome that the socket has)', late, [['live', 'welcome']]);
    r.s().fail();
    same('a lost link is told to every piece', [heard.slice(-1), late.slice(-1)], [[['lost', undefined]], [['lost', undefined]]]);
    late = [];
    r.dock.tap((kind) => late.push(kind));
    same('a piece that comes while the link is down is told that it is (no welcome is kept for a socket that is gone)', late, ['lost']);
    r.clock.advance(60000);
    r.live(W({ presence: true }));
    check('a new socket tells the new welcome to all', heard.slice(-1)[0][0] === 'live' && late.slice(-1)[0] === 'live');
}
{
    const r = rig({});
    check('send says no before the socket has been welcomed and nothing is written', r.dock.send({ t: 'who' }) === false && r.s().sent.length === 0);
    r.s().open();
    check('... also when it is open and the welcome has not come', r.dock.send({ t: 'who' }) === false && r.s().sent.length === 0);
    r.s().recv(W({ presence: true }));
    check('welcomed: it writes the frame as JSON and says yes', r.dock.send({ t: 'here', at: 'look' }) === true && r.s().sent.pop() === '{"t":"here","at":"look"}');
    const s = r.s();
    s.readyState = 3;
    check('a socket that is closing is no place to write', r.dock.send({ t: 'who' }) === false);
    s.readyState = 1; s.send = () => { throw new Error('gone'); };
    check('a socket that throws when written to: no, and the dock says that the link is lost (and tries again later)', r.dock.send({ t: 'who' }) === false && r.dock.state() === 'offline');
    const t = rig({});
    t.dock.tap(() => { throw new Error('a piece that breaks'); });
    let after = 0; t.dock.tap(() => { after++; });
    t.live();
    check('a piece that throws does not stop the others, the dock or the page: the error is reported and the welcome still shows', t.errors.length === 1 && after === 1 && t.dock.state() === 'live' && t.on() === '7 here now');
    t.s().recv({ t: 'who', full: true, n: 1, g: 0, more: 0, rows: [] });
    check('... nor does it when the list comes', t.errors.length === 2 && after === 2 && t.dock.state() === 'live');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 10. The two names (docs/HUB.md "Names"): the chat shows the account's display name or its username, as it picked. The switch is one button in the small menu, only where the hub says that the account has both;
//     a line is the visitor's by the hub's key of the account and not by a name (names are not unique); a moderator's copy of a line has the author's username
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const MINE = 'aaaaaaaaaaaa', OTHERS = 'bbbbbbbbbbbb';
const TWO = { name: 'Maple Leaf', mod: false, k: MINE, pick: 'display', other: 'maple.leaf' };
const NAMED = ['\u041c\u0430\u0440\u0438\u044f', '\u0418\u0432\u0430\u043d \u041f\u0435\u0442\u0440\u043e\u0432', '\u039d\u03af\u03ba\u03bf\u03c2', '\u7530\u4e2d\u592a\u90ce', '\u3055\u304f\u3089', '\uae40\ubbfc\uc218', '\u0645\u0631\u064a\u0645', '\u0645\u064e\u0631\u0652\u064a\u064e\u0645', '\u05d3\u05d5\u05d3',
               '\u05d3\u05b8\u05bc\u05d5\u05b4\u05d3', '\u0930\u093e\u091c\u0947\u0936', '\u0915\u094d\u0937\u0924\u094d\u0930\u093f\u092f', '\u0e2a\u0e21\u0e0a\u0e32\u0e22', '\u0e01\u0e34\u0e49\u0e07', 'Dave \u05d3\u05d5\u05d3', 'Zo\u00eb', 'Nguy\u1ec5n V\u0103n'];
const menuOf = (r) => { const live = r.live; r.live = (...args) => { const sock = live(...args); if (r.chip && r.root.getAttribute('data-you') === 'yes' && r.dock.slot.hidden) r.chip.click(); return sock; }; return r; };       // (the account's dropdown is opened by the chip once the welcome names the visitor)
{   // the pure parts
    const P = Pure;
    same('lineOf: the key and the username of the author, cleaned like a name', [P.lineOf({ id: 4, who: 'Maple Leaf', text: 'x', mod: false, k: MINE, u: 'maple.leaf' }), P.lineOf({ id: 4, who: 'a', text: 'x', k: 5, u: 5 }), P.lineOf({ id: 4, who: 'a', text: 'x', k: null, u: [] }),
        P.lineOf({ id: 4, who: 'a', text: 'x', u: '<b>x</b> \u202eevil\u202c \u200b' }).u, P.lineOf({ id: 4, who: 'a', text: 'x', u: 'u'.repeat(60) }).u, P.lineOf({ id: 4, who: 'a', text: 'x', u: '\u3164\u2800' }).u],
        [{ id: 4, who: 'Maple Leaf', text: 'x', mod: false, role: 'none', sys: false, k: MINE, u: 'maple.leaf' }, { id: 4, who: 'a', text: 'x', mod: false, role: 'none', sys: false, k: '', u: '' }, { id: 4, who: 'a', text: 'x', mod: false, role: 'none', sys: false, k: '', u: '' }, '<b>x</b> evil', 'u'.repeat(32) + '\u2026', '']);
    same('the words of the switch', [P.TEXT.shows, P.TEXT.names, P.TEXT.use], ['Chat shows your ', ['display name', 'username'], 'Show my ']);
    check('the names of other scripts are fine as they are: cleaned for display they come out whole, and are not cut at 32 code points', NAMED.every((n) => P.plain(n, true) === n && P.clip(n, 32) === n && P.lineOf({ id: 1, who: n, text: 'x', u: n }).who === n && P.lineOf({ id: 1, who: n, text: 'x', u: n }).u === n));
}
{   // the switch: where it is, what it says, what it sends
    const r = menuOf(rig({ chip: true }));
    r.live(W({ login: true, you: TWO }));
    const note = one(r.dock.slot, 'gc-pn'), button = one(r.dock.slot, 'gc-pick'), menu = r.dock.slot;
    check('a hub that says that the account has both names: the small menu has a sentence that says which one the chat shows and a button that offers the other, with its name; both are in the menu, before Forget me', shows(note) && shows(button) && note.textContent === 'Chat shows your display name.'
          && button.textContent === 'Show my username: maple.leaf' && button.tagName === 'button' && button.type === 'button' && menu.children.indexOf(note) < menu.children.indexOf(button) && menu.children.indexOf(button) < menu.children.indexOf(one(r.dock.slot, 'gc-forget')));
    check('... the account line says the name that is shown (the display name), the chip too', one(r.dock.slot, 'gc-an').textContent === 'Maple Leaf' && one(r.chip, 'gc-cn').textContent === 'Maple Leaf' && one(r.chip, 'gc-av').textContent === 'M');
    button.click();
    same('a press sends ONE frame, {"t":"pick","pick":"username"} (the other name), on the open socket, and nothing else (no request, no navigation)', [r.s().sent.filter((x) => x.indexOf('pick') !== -1), r.fetches.length, r.went], [['{"t":"pick","pick":"username"}'], 0, []]);
    check('... the page does not change what it shows by itself: the name is the hub\'s to change (it closes the socket and the page connects again)', one(r.dock.slot, 'gc-an').textContent === 'Maple Leaf' && note.textContent === 'Chat shows your display name.');
    const first = r.s();
    button.focus();
    const dropHidden = () => { for (let e = r.env.focused; e; e = e.parent) if (e.hidden) { r.env.focused = null; r.env.doc.activeElement = null; return; } };       // (a browser drops the focus from an element that is hidden, or in one)
    first.fail(); dropHidden();
    check('the dropdown goes while the hub closes the socket, and the chip goes with it (offline): the focus is nowhere for the moment, as in a browser', r.dock.slot.hidden === true && r.chip.hidden === true && r.env.focused === null);
    check('the hub closes the socket: "Chat is offline" for a moment, the account line is away, and the page connects again after its first wait', r.dock.state() === 'offline' && r.dock.slot.hidden === true && (r.clock.advance(1500), r.sockets.length === 2));
    r.s().open().recv(W({ login: true, you: { name: 'maple.leaf', mod: false, k: MINE, pick: 'username', other: 'Maple Leaf' } }));
    dropHidden();
    check('the dropdown is back with the new welcome and the focus is back on the button of the name (not left on the page)', r.dock.slot.hidden === false && r.env.focused === button);
    check('the new welcome says the other name: the account line, the sentence and the button follow, and the small menu is as open as it was', one(r.dock.slot, 'gc-an').textContent === 'maple.leaf' && note.textContent === 'Chat shows your username.' && button.textContent === 'Show my display name: Maple Leaf' && menu.hidden === false);
    button.click();
    same('... and a press now sends the first name', r.s().sent.filter((x) => x.indexOf('pick') !== -1), ['{"t":"pick","pick":"display"}']);
    button.click();
    const sent = r.s().sent.filter((x) => x.indexOf('pick') !== -1);
    check('a second press before the hub has closed the socket sends the same word again (the hub stores the same thing twice and closes the sockets once)', sent.length === 2 && sent[0] === sent[1] && sent[0] === '{"t":"pick","pick":"display"}');
}
{   // where there is no switch
    const cases = {
        'a hub that has no such thing (the welcome of before: name and mod only)': { name: 'maple.leaf', mod: false },
        'the key only': { name: 'maple.leaf', mod: false, k: MINE },
        'a pick and no other name': { name: 'maple.leaf', mod: false, k: MINE, pick: 'username' },
        'an other name and no pick': { name: 'maple.leaf', mod: false, k: MINE, other: 'Maple Leaf' },
        'an empty other name': { name: 'maple.leaf', mod: false, k: MINE, pick: 'username', other: '' },
        'an other name that shows nothing': { name: 'maple.leaf', mod: false, k: MINE, pick: 'username', other: '\u200b\u3164\ufe0f' },
        'an other name that is not text': { name: 'maple.leaf', mod: false, k: MINE, pick: 'username', other: 5 },
        'an other name that is an object': { name: 'maple.leaf', mod: false, k: MINE, pick: 'username', other: { name: 'x' } },
        'a pick that is not one of the two words': { name: 'maple.leaf', mod: false, k: MINE, pick: 'both', other: 'Maple Leaf' },
        'a pick in capitals': { name: 'maple.leaf', mod: false, k: MINE, pick: 'DISPLAY', other: 'Maple Leaf' },
        'a pick that is a number': { name: 'maple.leaf', mod: false, k: MINE, pick: 1, other: 'Maple Leaf' },
        'a pick that is a list': { name: 'maple.leaf', mod: false, k: MINE, pick: ['display'], other: 'Maple Leaf' },
    };
    Object.keys(cases).forEach((label) => {
        const r = menuOf(rig({ chip: true }));
        r.live(W({ login: true, you: cases[label] }));
        check(label + ': no sentence and no button of the switch, the rest of the menu as it was', one(r.dock.slot, 'gc-pn').hidden === true && one(r.dock.slot, 'gc-pick').hidden === true && buttonsShown(r.dock.slot).indexOf('Forget me') !== -1 && one(r.dock.slot, 'gc-an').textContent === 'maple.leaf' && r.errors.length === 0);
        one(r.dock.slot, 'gc-pick').click();
        check(label + ': a press that gets through anyway sends nothing', r.s().sent.filter((x) => x.indexOf('pick') !== -1).length === 0);
    });
    const reader = menuOf(rig({}));
    reader.live(W({ login: true, you: null }));
    check('a reader has no account line and so no switch', one(reader.dock.slot, 'gc-pick').hidden === true && reader.dock.slot.hidden === true);
    const nologin = menuOf(rig({}));
    nologin.live(W({ login: false, you: TWO }));
    check('where the hub has no login there is no account line and no switch', nologin.dock.slot.hidden === true && nologin.dock.slot.hidden === true);
}
{   // a press that cannot be made, and a hub that refuses
    const r = menuOf(rig({}));
    r.live(W({ login: true, you: TWO }));
    r.s().readyState = 2;
    one(r.dock.slot, 'gc-pick').click();
    check('the socket is closing: nothing is written to it, and the visitor is told that it did not work', r.s().sent.filter((x) => x.indexOf('pick') !== -1).length === 0 && r.note() === 'That did not work, try again');
    const q = menuOf(rig({}));
    q.live(W({ login: true, you: TWO }));
    q.dock.say('hello');
    q.s().recv({ t: 'err', code: 'pick' });
    check('a refused switch ({"t":"err","code":"pick"}) says "That did not work, try again" for a few seconds, and the socket stays', q.note() === 'That did not work, try again' && q.dock.state() === 'live' && q.sockets.length === 1 && (q.clock.advance(6001), q.note() === ''));
    q.dock.say('second line');
    q.s().recv({ t: 'err', code: 'pick' });
    q.s().recv({ t: 'err', code: 'rate' });
    check('... and it is nobody\'s line: the line that waits for its answer is still waiting (the next refusal of a line is the oldest one\'s)', q.note() === 'Slow down a little' && q.field().value === 'hello');
    const t = menuOf(rig({}));
    t.live(W({ login: true, you: TWO }));
    t.s().send = () => { throw new Error('gone'); };
    one(t.dock.slot, 'gc-pick').click();
    check('a socket that throws when written to is a lost link, as for any frame, and nothing is thrown at the page', t.dock.state() === 'offline' && t.errors.length === 0);
}
{   // a line is the visitor's by the hub's key
    const history = [{ id: 1, at: 1, who: 'Maple Leaf', text: 'my line', mod: false, k: MINE }, { id: 2, at: 2, who: 'Maple Leaf', text: 'a line of another account with my name', mod: false, k: OTHERS },
                     { id: 3, at: 3, who: 'maple.leaf', text: 'my line from before the switch', mod: false, k: MINE }, { id: 4, at: 4, who: 'Maple Leaf', text: 'a line that has no key', mod: false }];
    const r = rig({});
    r.live(W({ login: true, you: TWO, history }));
    same('in the history only the lines with the visitor\'s key are the visitor\'s: the same name is not enough, a line with no key is not (a hub that sends keys sends them on every line), and a line under the other name still is', r.rows().map((x) => cls(x).indexOf('gc-me') !== -1), [true, false, true, false]);
    r.s().recv(L(5, 'Maple Leaf', 'live, mine')); r.s().recv(Object.assign(L(6, 'Maple Leaf', 'live, not mine'), { k: OTHERS })); r.s().recv(Object.assign(L(7, 'Maple Leaf', 'live, mine'), { k: MINE }));
    same('live lines too', r.rows().slice(4).map((x) => cls(x).indexOf('gc-me') !== -1), [false, false, true]);
    const folded = rig({});
    folded.live(W({ login: true, you: TWO })); folded.dock.open(false);
    folded.s().recv(Object.assign(L(1, 'Maple Leaf', 'mine'), { k: MINE })); folded.s().recv(Object.assign(L(2, 'Maple Leaf', 'not mine'), { k: OTHERS }));
    check('the unread count is the lines of others: the visitor\'s own is read, and another account that shows the same name is not the visitor', folded.pill() === '1');
    const echo = rig({});
    echo.live(W({ login: true, you: TWO }));
    echo.dock.say('hello');
    echo.s().recv(Object.assign(L(1, 'Maple Leaf', 'hello'), { k: OTHERS }));
    echo.s().recv({ t: 'err', code: 'rate' });
    check('a line of another account with the same name and the same words is not the answer to the visitor\'s line: that one still waits (and is refused as the oldest)', echo.note() === 'Slow down a little' && echo.field().value === 'hello');
    const mineEcho = rig({});
    mineEcho.live(W({ login: true, you: TWO }));
    mineEcho.dock.say('hello');
    mineEcho.s().recv(Object.assign(L(1, 'Maple Leaf', 'hello'), { k: MINE }));
    mineEcho.s().recv({ t: 'err', code: 'rate' });
    check('the visitor\'s own echo, by its key, answers the visitor\'s line: a refusal after it is nobody\'s', mineEcho.note() === '' && mineEcho.field().value === '');
    const old = rig({});
    old.live(W({ login: true, you: { name: 'maple.leaf', mod: false }, history: [{ id: 1, at: 1, who: 'maple.leaf', text: 'by the name, as the old hub says', mod: false }, { id: 2, at: 2, who: 'pebble', text: 'x', mod: false }] }));
    same('a hub that sends no keys: a line is the visitor\'s by its name, as it always was', old.rows().map((x) => cls(x).indexOf('gc-me') !== -1), [true, false]);
    const half = rig({});
    half.live(W({ login: true, you: { name: 'maple.leaf', mod: false }, history: [Object.assign({ id: 1, at: 1, who: 'maple.leaf', text: 'x', mod: false }, { k: OTHERS })] }));
    check('... also when only the lines have a key (a hub of the next version and a welcome of this one): the name decides', cls(half.rows()[0]).indexOf('gc-me') !== -1);
    const huge = rig({});
    huge.live(W({ login: true, you: Object.assign({}, TWO, { k: 'k'.repeat(5000) }), history: [{ id: 1, at: 1, who: 'x', text: 'y', mod: false, k: 'k'.repeat(5000) }] }));
    check('a key that is far too long is cut, the same way on both sides, and nothing breaks', huge.errors.length === 0 && cls(huge.rows()[0]).indexOf('gc-me') !== -1 && one(huge.dock.slot, 'gc-an') !== null);
}
{   // a moderator's copy of a line has the author's username
    const history = [{ id: 1, at: 1, who: 'Maple Leaf', text: 'hello', mod: false, k: OTHERS, u: 'aaa.one' }, { id: 2, at: 2, who: 'pebble', text: 'a line that shows the username', mod: false, k: MINE }];
    const r = rig({});
    r.live(W({ login: true, you: Object.assign({}, TWO, { mod: true }), history }));
    r.s().recv(Object.assign(L(3, 'Maple Leaf', 'live'), { k: OTHERS, u: 'bbb.two' }));
    const rows = r.rows();
    check('a line is drawn with the name only: no username, not even in a moderator\'s copy (the hub still sends it; the page keeps it in its model and does not draw it)', rows.length === 3 && rows.every((x) => one(x, 'gc-u') === null)
          && rows[0].textContent === 'Maple Leaf: hello' && rows[1].textContent === 'pebble: a line that shows the username' && rows[2].textContent === 'Maple Leaf: live' && Pure.lineOf({ id: 1, who: 'a', text: 'x', u: 'aaa.one' }).u === 'aaa.one');
    const odd = rig({});
    odd.live(W({ login: true, you: TWO, history: [{ id: 1, at: 1, who: 'a', text: 'x', mod: false, u: '<i>x</i> \u202eevil\u202c \u200b' }, { id: 2, at: 2, who: 'a', text: 'y', mod: false, u: 'u'.repeat(80) }, { id: 3, at: 3, who: 'a', text: 'z', mod: false, u: { x: 1 } }] }));
    check('a username from the hub in any shape is not drawn and breaks nothing (markup, invisible characters, a long one, an object)', odd.rows().every((x) => one(x, 'gc-u') === null) && odd.rows()[0].textContent === 'a: x'
          && odd.env.innerHTMLWrites.length === 0 && odd.log().all().every((e) => ['div', 'p', 'span', 'bdi'].indexOf(e.tagName) !== -1));
}
{   // other scripts: whole, in the line, the account line, the chip and the button
    NAMED.forEach((name) => {
        const r = menuOf(rig({ chip: true }));
        r.live(W({ login: true, you: { name, mod: false, k: MINE, pick: 'display', other: name + '.x' }, history: [{ id: 1, at: 1, who: name, text: 'hello', mod: false, k: MINE, u: name + '.x' }] }));
        check('a name in another script comes through whole (' + name + '): the line (no username drawn), the account line, the chip, the button', r.rows()[0].textContent === name + ': hello' && one(r.dock.slot, 'gc-an').textContent === name && one(r.chip, 'gc-cn').textContent === name
              && one(r.dock.slot, 'gc-pick').textContent === 'Show my username: ' + name + '.x' && cls(r.rows()[0]).indexOf('gc-me') !== -1 && r.errors.length === 0);
    });
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 9. The roles (docs/HUB.md "Roles", "Moderation"): the role of the visitor and its live change, the marks, /help and the private answers, the hub's own line, being removed
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const ME = { name: 'maple.leaf', k: MINE };
const asRole = (role) => Object.assign({}, ME, { role, mod: role !== 'none' });
const sentSays = (r) => r.said().map((m) => m.text);
const tagOf = (row) => one(row, 'gc-tag') ? one(row, 'gc-tag').textContent : null;
{   // the pure parts
    const P = Pure;
    same('roleOf: admin or mod only where the hub says moderator; any other word, a string "true" or nothing is none', [P.roleOf('admin', true), P.roleOf('mod', true), P.roleOf(undefined, true), P.roleOf('root', true), P.roleOf('admin', false), P.roleOf('admin', 'true'), P.roleOf('admin', undefined), P.roleOf('mod', 1)], ['admin', 'mod', 'mod', 'mod', 'none', 'none', 'none', 'none']);
    same('youOf: role where the hub says it, an older hub\'s `mod` only is a moderator, a word that is none of the three is no role, a visitor with no name is nobody',
        [P.youOf({ name: 'a', mod: true, role: 'admin' }).role, P.youOf({ name: 'a', mod: true }).role, P.youOf({ name: 'a', mod: false }).role, P.youOf({ name: 'a', role: 'mod' }).mod, P.youOf({ name: 'a', mod: true, role: 'root' }).role, P.youOf({ name: 'a', mod: true, role: 'none' }).role,
         P.youOf({ name: 'a', role: ['admin'] }).role, P.youOf({ name: '', role: 'admin' }), P.youOf(null), P.youOf('admin'), P.youOf({ name: 'a', mod: 'true' }).role], ['admin', 'mod', 'none', true, 'mod', 'none', 'none', null, null, null, 'none']);
    same('lineOf: the hub\'s own line is a line whose flag `sys` is true and nothing else (not the name, not the text, not "true", not 1); it is no moderator\'s line whatever else it says',
        [P.lineOf({ id: 1, who: 'system', text: 'a was kicked.', sys: true }).sys, P.lineOf({ id: 1, who: 'system', text: 'a was kicked.' }).sys, P.lineOf({ id: 1, who: 'x', text: 'y', sys: 'true' }).sys, P.lineOf({ id: 1, who: 'x', text: 'y', sys: 1 }).sys, P.lineOf({ id: 1, who: 'x', text: 'y', sys: {} }).sys,
         P.lineOf({ id: 1, who: 'system', text: 'a was kicked.', sys: true, mod: true, role: 'admin' }).mod, P.lineOf({ id: 1, who: 'system', text: 'a was kicked.', sys: true, mod: true, role: 'admin' }).role], [true, false, false, false, false, false, 'none']);
    same('lineOf: the role of a line is the hub\'s `role` where `mod` is true; an admin that is not a moderator is none', [P.lineOf({ id: 1, who: 'x', text: 'y', mod: true, role: 'admin' }).role, P.lineOf({ id: 1, who: 'x', text: 'y', mod: true }).role, P.lineOf({ id: 1, who: 'x', text: 'y', mod: false, role: 'admin' }).role, P.lineOf({ id: 1, who: 'x', text: 'y', mod: true, role: 'ADMIN' }).role], ['admin', 'mod', 'none', 'mod']);
    same('wordOfCommand: the slash word in lower case, only where a space or the end follows (a path, a shrug with no word and a line that has a slash inside are no command word)', [P.wordOfCommand('/MUTE bob 5'), P.wordOfCommand('/bans'), P.wordOfCommand('/help me'), P.wordOfCommand('/mute'), P.wordOfCommand('hello /mute'), P.wordOfCommand('/mute\u00e9 x'), P.wordOfCommand('/a/b'), P.wordOfCommand('/'), P.wordOfCommand('')], ['/mute', '/bans', '/help', '/mute', '', '', '', '', '']);
    same('helpOf: a moderator is told four commands, an admin seven (the three more are the admin\'s: ban, unban, bans), anybody else that there are none; each is how it is typed and what it does',
        [P.helpOf('mod').slice(1).map((l) => l[0]), P.helpOf('admin').slice(1).map((l) => l[0]), P.helpOf('none'), P.helpOf('root'), P.helpOf('mod')[0]], [['/mute name minutes', '/unmute name', '/del number', '/kick name'], ['/mute name minutes', '/unmute name', '/del number', '/kick name', '/ban name', '/unban name', '/bans'],
         ['There are no chat commands for you.'], ['There are no chat commands for you.'], 'Commands you can use. Only you can see this.']);
    check('helpOf: every line of it has words for what the command does', P.helpOf('admin').slice(1).every((l) => l.length === 2 && l[1].length >= 10));
    same('bansOf: a heading and a line for each person (the name and the day, UTC), "(no name)" for a row with none; nobody banned; not a list, a list of more than the hub sends, a row that is no object, a time that is no time',
        [P.bansOf({ rows: [{ u: 'bob', at: 1700000000 }, { u: '', at: 1700086400 }, { u: '<b>x</b>', at: -1 }, 5, null] }), P.bansOf({ rows: [] }), P.bansOf({ rows: 'x' }), P.bansOf({ rows: new Array(51).fill({ u: 'a', at: 1 }) }), P.bansOf(null), P.bansOf({ rows: [{ u: 'a', at: 'x' }, { u: 'a', at: 1e12 }, { u: 'a', at: 1.5 }] })],
        [['People who are banned', ['bob', '2023-11-14'], ['(no name)', '2023-11-15'], ['<b>x</b>', '']], ['Nobody is banned.'], null, null, null, ['People who are banned', ['a', ''], ['a', ''], ['a', '']]]);
}
{   // the role of the visitor, from the welcome
    const of = (you) => { const r = rig({ chip: true }); r.live(W({ login: true, you })); return r.root.getAttribute('data-role') + '/' + r.chip.getAttribute('aria-label'); };
    same('the welcome\'s role is the dock\'s: none, mod and admin (the chip says it too, in words); an older hub that says `mod` only is a moderator; a role that is not one of the three is none; a reader has none',
        [of(asRole('none')), of(asRole('mod')), of(asRole('admin')), of({ name: 'maple.leaf', mod: true }), of({ name: 'maple.leaf', mod: false, role: 'root' }), of(null)],
        ['none/Chatting as maple.leaf', 'mod/Chatting as maple.leaf, moderator', 'admin/Chatting as maple.leaf, admin', 'mod/Chatting as maple.leaf, moderator', 'none/Chatting as maple.leaf', 'none/Log in with Discord']);
}
{   // the marks: the chip, the dropdown, the lines
    const r = menuOf(rig({ chip: true }));
    r.live(W({ login: true, you: asRole('admin'), history: [Object.assign(L(1, 'boss', 'hello', true), { role: 'admin' }), L(2, 'clover', 'a room', true), L(3, 'juniper', 'hi'), Object.assign(L(4, 'pebble', 'not a moderator', false), { role: 'admin' })].map((m) => { delete m.t; return m; }) }));
    const word = (e) => e.textContent;
    check('the chip of an admin has the word ADMIN (as text, so the color is not the only sign) and the mark of red; the dropdown says "You are an admin" with the same mark', one(r.chip, 'gc-cr').hidden === false && word(one(r.chip, 'gc-cr')) === 'admin' && cls(one(r.chip, 'gc-cr')).indexOf('gc-cr-admin') !== -1
          && r.dock.slot.hidden === false && one(r.dock.slot, 'gc-yr').hidden === false && word(one(r.dock.slot, 'gc-badge')) === 'admin' && cls(one(r.dock.slot, 'gc-badge')).indexOf('gc-badge-admin') !== -1 && one(r.dock.slot, 'gc-yt').textContent === ' You are an admin');
    same('in the log an admin\'s line has the word admin (the red mark), a moderator\'s the word mod (the gold one), a line of nobody none, and `role: admin` on a line whose `mod` is false marks nobody', r.rows().map((x) => [one(x, 'gc-badge') ? one(x, 'gc-badge').textContent : '', one(x, 'gc-badge') ? cls(one(x, 'gc-badge')).indexOf('gc-badge-admin') !== -1 : false]),
        [['admin ', true], ['mod ', false], ['', false], ['', false]]);
    same('... and the line texts are the names and the lines, with the word before them', r.rowTexts(), ['admin boss: hello', 'mod clover: a room', 'juniper: hi', 'pebble: not a moderator']);
    const m = menuOf(rig({ chip: true }));
    m.live(W({ login: true, you: asRole('mod') }));
    check('a moderator\'s chip says MOD with the gold mark and no red', word(one(m.chip, 'gc-cr')) === 'mod' && cls(one(m.chip, 'gc-cr')).indexOf('gc-cr-admin') === -1 && one(m.dock.slot, 'gc-yt').textContent === ' You are a moderator');
    const n = menuOf(rig({ chip: true }));
    n.live(W({ login: true, you: asRole('none') }));
    check('a person who holds no role has no mark on the chip and no role line in the dropdown', one(n.chip, 'gc-cr').hidden === true && one(n.chip, 'gc-cr').textContent === '' && one(n.dock.slot, 'gc-yr').hidden === true);
}
{   // the role changes while the page is open
    const r = menuOf(rig({ chip: true }));
    const seenBy = [];
    r.live(W({ login: true, you: asRole('none') }));
    r.dock.tap((kind, m) => seenBy.push(kind + ':' + (m && m.you ? m.you.role : '')));
    r.s().recv({ t: 'you', you: asRole('mod') });
    check('a `you` frame that makes a person a moderator: the chip, the data-role and the dropdown follow at once, nobody reloads; the players list hears of it (tap kind `you`)', r.root.getAttribute('data-role') === 'mod' && one(r.chip, 'gc-cr').hidden === false && one(r.chip, 'gc-cr').textContent === 'mod' && r.errors.length === 0 && seenBy.join() === 'live:none,you:mod');
    r.dock.say('/help');
    same('... and /help lists the moderator\'s four commands', r.answers(), ['Commands you can use. Only you can see this.\n/mute name minutes stop a person writing for 1 to 10080 minutes\n/unmute name allow a muted person to write again\n/del number take one line off the screen of everybody (by its number)\n/kick name remove a person from the chat (they can log in again)']);
    r.s().recv({ t: 'you', you: asRole('admin') });
    r.clock.advance(2600); r.dock.say('/help');
    check('... an admin\'s seven', r.answers().length === 2 && r.answers()[1].split('\n').length === 8 && r.root.getAttribute('data-role') === 'admin' && one(r.chip, 'gc-cr').textContent === 'admin');
    r.s().recv({ t: 'you', you: asRole('none') });
    r.clock.advance(2600); r.dock.say('/help'); r.dock.say('/kick bob');
    same('... and a removal: the marks go, and the commands go with them (a command word is refused here, nothing is sent)', [r.root.getAttribute('data-role'), one(r.chip, 'gc-cr').hidden, r.answers().slice(2), r.s().sent.filter((x) => /kick/.test(x)).length], ['none', true, ['There are no chat commands for you.', 'You are not allowed to do that'], 0]);
    const other = rig({}); other.live(W({ login: true, you: asRole('none') }));
    ['x', null, 5, [], {}, { you: null }, { you: 'admin' }, { you: { name: '', role: 'admin' } }, { you: { role: 'admin' } }].forEach((f) => other.s().recv(typeof f === 'object' && f !== null && !Array.isArray(f) && f.you !== undefined ? Object.assign({ t: 'you' }, f) : { t: 'you', you: f }));
    check('a `you` frame that is not a visitor with a name is ignored (nothing changes, nothing throws)', other.root.getAttribute('data-role') === 'none' && other.errors.length === 0);
    const reader = rig({}); reader.live(W({ login: true, you: null }));
    reader.s().recv({ t: 'you', you: asRole('admin') });
    check('a reader is told nothing of a role: a `you` frame does not make a visitor of a reader (the hub names a visitor in the welcome only)', reader.root.getAttribute('data-you') === 'no' && reader.form().hidden === false && reader.field().disabled === true);
    const name = rig({}); name.live(W({ login: true, you: asRole('mod') }));
    name.s().recv({ t: 'you', you: Object.assign({}, asRole('admin'), { name: 'someone.else', k: OTHERS }) });
    name.s().recv(Object.assign(L(5, 'maple.leaf', 'mine'), { k: MINE }));
    check('who the visitor is does not change on a socket: a `you` frame with another name or key changes the role only, and the lines of the visitor are still the visitor\'s', cls(name.rows()[0]).indexOf('gc-me') !== -1 && name.root.getAttribute('data-role') === 'admin');
    const first = rig({}); first.s().open();
    first.s().recv({ t: 'you', you: asRole('admin') });
    check('a `you` frame before the welcome is ignored like every frame before it', first.root.getAttribute('data-role') === 'none' && first.errors.length === 0);
}
{   // /help is the page's own and a command word of a person with no role is not a line
    const r = rig({}); r.live(W({ login: true, you: asRole('mod') }));
    r.dock.say('/help'); r.clock.advance(2600); r.dock.say('/HELP please'); r.clock.advance(2600); r.dock.say('/helpme');
    check('/help is never sent, in any case and with words after it; it is no line and nobody is sent anything; /helpme is not /help (it is sent, as every command of a moderator is, and the hub answers it)', r.said().map((m) => m.text).join() === '/helpme' && r.answers().length === 2 && tagOf(r.rows()[0]) === 'Only you' && r.rows().every((x) => !/^maple/.test(x.textContent)));
    const none = rig({}); none.live(W({ login: true, you: asRole('none') }));
    ['/mute bob 5', '/UNMUTE bob', '/del 3', '/kick bob', '/ban bob', '/unban bob', '/bans'].forEach((c, i) => { none.clock.advance(2600); none.dock.say(c); });
    none.clock.advance(2600); none.dock.say('/shrug'); none.clock.advance(2600); none.dock.say('/helpful tip'); none.clock.advance(2600); none.dock.say('hello');
    same('a person who holds no role: the seven command words are answered "You are not allowed to do that" here and never sent (the hub would refuse them and never post them); any other line that begins with a slash is a line', [none.answers().length, none.answers()[0], sentSays(none)], [7, 'You are not allowed to do that', ['/shrug', '/helpful tip', 'hello']]);
    const field = rig({}); field.live(W({ login: true, you: asRole('none') }));
    field.submit('/ban bob');
    same('the field is emptied by an answer of the page (the text is not kept to be sent again)', [field.field().value, field.answers().length], ['', 1]);
    const reader = rig({}); reader.live(W({ login: true, you: null }));
    same('a reader has no /help: it says why not (the field is off)', [reader.dock.say('/help'), reader.answers().length], ['readonly', 0]);
    const off = rig({}); off.live(W({ login: true, you: asRole('mod') })); off.s().fail();
    same('/help is the page\'s own: it works while the link is down', [off.dock.say('/help'), off.answers().length, off.s().sent.length >= 0], ['', 1, true]);
}
{   // each reply of the hub, in plain words, to the sender only
    const MODE = asRole('mod');
    const reply = (command, frame) => { const r = rig({}); r.live(W({ login: true, you: MODE })); if (command) r.dock.say(command); r.s().recv(frame); return r; };
    const WORDS = { power: 'You are not allowed to do that', role: 'Could not confirm your role, try again in a moment', who: 'Nobody by that name is here', rate: 'You are going too fast, try again in a moment', bad: 'That command is not right. Type /help to see the commands.' };
    Object.keys(WORDS).forEach((code) => {
        const r = reply('/kick bob', { t: 'err', code });
        check('err ' + code + ' to a command: "' + WORDS[code] + '", a private line, nothing in the note, nothing in the field, nothing sent but the command', eq(r.answers(), [WORDS[code]]) && r.note() === '' && r.field().value === '' && sentSays(r).join() === '/kick bob' && tagOf(r.rows()[0]) === 'Only you');
    });
    ['power', 'role', 'who'].forEach((code) => {
        const late = reply('/kick bob', { t: 'err', code }); late.dock.say('hello');
        const alone = rig({}); alone.live(W({ login: true, you: MODE })); alone.clock.advance(1); alone.s().recv({ t: 'err', code });
        const gone = rig({}); gone.live(W({ login: true, you: MODE })); gone.dock.say('/kick bob'); gone.clock.advance(9000); gone.s().recv({ t: 'err', code });
        const line = rig({}); line.live(W({ login: true, you: MODE })); line.dock.say('/kick bob'); line.dock.say('hello'); line.s().recv({ t: 'err', code });
        same('err ' + code + ' is only ever a command\'s: told where the command was forgotten or none was sent, and it never takes a line\'s place (the line still waits for its own answer, and is not put back)', [alone.answers(), gone.answers(), line.answers(), line.field().value, (line.s().recv({ t: 'err', code: 'rate' }), line.field().value)], [[WORDS[code]], [WORDS[code]], [WORDS[code]], '', 'hello']);
    });
    const ok = (c, command) => reply(command, { t: 'ok', c });
    same('ok: one plain line for each of the six commands, to the sender only', ['mute', 'unmute', 'del', 'kick', 'ban', 'unban'].map((c) => ok(c, '/' + c + ' x').answers()[0]),
        ['Done: the person is muted.', 'Done: the person can write again.', 'Done: the line is gone.', 'Done: the person was removed.', 'Done: the person is banned.', 'Done: the ban is lifted.']);
    same('ok answers the command that waits (the next refusal is for the next send), and a word that is no command ("constructor", "__proto__", a number, nothing) says nothing', [(() => { const r = ok('mute', '/mute x 5'); r.clock.advance(2600); r.dock.say('hello'); r.s().recv({ t: 'err', code: 'rate' }); return r.field().value; })(),
        ['constructor', '__proto__', 5, null, undefined, '', 'hasOwnProperty'].map((c) => reply('/mute x 5', { t: 'ok', c }).answers().length)], ['hello', [0, 0, 0, 0, 0, 0, 0]]);
    same('a refusal that is not a command\'s is a line\'s as it was (the note), and a command is never put back in the field', [(() => { const r = rig({}); r.live(W({ login: true, you: MODE })); r.dock.say('hello'); r.s().recv({ t: 'err', code: 'rate' }); return [r.note(), r.field().value, r.answers()]; })()], [['Slow down a little', 'hello', []]]);
    const bans = (frame, command) => { const r = rig({}); r.live(W({ login: true, you: asRole('admin') })); if (command) r.dock.say(command); r.s().recv(frame); return r; };
    const list = bans({ t: 'bans', rows: [{ u: 'bob', at: 1700000000 }, { u: 'carol.x', at: 1700086400 }] }, '/bans');
    same('bans: the heading and a line for each person, a name and a day; private; the command that waited for it is answered', [list.answers(), one(list.rows()[0], 'gc-cmd') ? one(list.rows()[0], 'gc-cmd').tagName : null, (list.clock.advance(2600), list.dock.say('hello'), list.s().recv({ t: 'err', code: 'rate' }), list.field().value)],
        [['People who are banned\nbob 2023-11-14\ncarol.x 2023-11-15'], 'bdi', 'hello']);
    same('bans: nobody; a frame that is not a list says nothing; a name that is markup is only text', [bans({ t: 'bans', rows: [] }).answers(), bans({ t: 'bans', rows: 7 }).answers(), bans({ t: 'bans' }).answers(), (() => { const r = bans({ t: 'bans', rows: [{ u: '<img src=x onerror=alert(1)>', at: 1 }] }); return [r.answers(), r.env.innerHTMLWrites.length, r.log().all().every((e) => ['div', 'p', 'span', 'bdi'].indexOf(e.tagName) !== -1)]; })()],
        [['Nobody is banned.'], [], [], [['People who are banned\n<img src=x onerror=alert(1)> 1970-01-01'], 0, true]]);
    const kept = rig({}); kept.live(W({ login: true, you: MODE })); kept.dock.say('/kick bob'); kept.s().recv({ t: 'err', code: 'who' });
    kept.s().fail(); kept.clock.advance(2600); kept.s().open().recv(W({ login: true, you: MODE }));
    check('a private answer is a line of this screen only: a new link\'s welcome makes the screen again from the hub\'s history, and it is gone', kept.answers().length === 0);
    const room = rig({}); room.live(W({ login: true, you: MODE })); room.dock.shown(false); room.dock.say('/help'); room.dock.shown(true);
    check('an answer that comes while the screen is away is there when it is back (the log is made again from the model)', room.answers().length === 1);
    const cap = rig({}); cap.live(W({ login: true, you: MODE }));
    for (let i = 0; i < 130; i++) { cap.dock.say('/help'); cap.clock.advance(2600); }
    check('the answers are lines like the others: at most 100 lines are kept, the oldest go first', cap.rows().length === 100 && cap.errors.length === 0);
    const unread = rig({}); unread.live(W({ login: true, you: MODE })); unread.dock.open(false); unread.dock.say('/help');
    check('an answer to the visitor is not "unread" (it is what they asked for)', unread.pill() === '');
}
{   // the hub's own line
    const r = rig({});
    const sys = (id, text, extra) => Object.assign(L(id, 'system', text, false), { k: '', sys: true }, extra);
    r.live(W({ login: true, you: ME, history: [Object.assign(sys(1, 'alice was kicked.'), { k: MINE }), L(2, 'system', 'carol was banned.')].map((m) => { delete m.t; return m; }) }));
    r.s().recv(sys(3, 'bob was banned.'));
    r.s().recv(sys(4, 'maple.leaf was kicked.'));
    r.s().recv(sys(5, 'x was kicked.', { mod: true, role: 'admin', k: MINE }));
    const k = r.rows();
    check('a line with the flag is drawn quietly as the hub\'s own: a line that names the person and has no author, no mark, no ": " (class gc-event), never the visitor\'s own (even where its key is the visitor\'s)', k.length === 5 && [0, 2, 3, 4].every((i) => cls(k[i]).indexOf('gc-event') !== -1 && cls(k[i]).indexOf('gc-me') === -1 && one(k[i], 'gc-badge') === null && one(k[i], 'gc-who') === null && one(k[i], 'gc-sep') === null)
          && k[0].textContent === 'alice was kicked.' && k[2].textContent === 'bob was banned.' && k[3].textContent === 'maple.leaf was kicked.' && k[4].textContent === 'x was kicked.' && !cls(k[4]).includes('gc-mod') && !cls(k[4]).includes('gc-admin'));
    check('a line from the name "system" without the flag is a line of a person (the name does not make it the hub\'s): author, ": ", no event class', cls(k[1]).indexOf('gc-event') === -1 && k[1].textContent === 'system: carol was banned.' && one(k[1], 'gc-who') !== null);
    const odd = rig({}); odd.live(W({ login: true, you: ME }));
    odd.s().recv(Object.assign(L(1, 'carol', 'dave was kicked.'), { sys: 'true' })); odd.s().recv(Object.assign(L(2, 'carol', 'dave was banned.'), { sys: 1 })); odd.s().recv(Object.assign(L(3, 'carol', 'a moderator removed dave'), { sys: false }));
    check('a person who writes what the hub\'s line says, even under a name that looks like it, draws a line of a person; only `sys: true` of a frame is the hub\'s', odd.rows().every((x) => cls(x).indexOf('gc-event') === -1 && one(x, 'gc-who') !== null));
    const text = rig({}); text.live(W({ login: true, you: ME }));
    text.s().recv(sys(1, '<img src=x onerror=alert(1)> \u202e was kicked.'));
    check('the text of the hub\'s line is text (no markup, the invisible characters out) in a <bdi>', text.env.innerHTMLWrites.length === 0 && text.rows()[0].textContent === '<img src=x onerror=alert(1)> was kicked.' && one(text.rows()[0], 'gc-text').tagName === 'bdi');
    const unread = rig({}); unread.live(W({ login: true, you: ME })); unread.dock.open(false); unread.s().recv(sys(1, 'bob was kicked.'));
    check('the hub\'s line is news like any line that comes while the dock is folded', unread.pill() === '1');
    const del = rig({}); del.live(W({ login: true, you: ME })); del.s().recv(sys(7, 'bob was kicked.')); del.s().recv({ t: 'del', id: 7 });
    check('and goes with a `del` like any other', del.rows().length === 0);
}
{   // being removed: a plain notice, the field off where the hub's state needs it, no reconnect loop
    const KICK = 'A moderator removed you from the chat. You can log in again.', BAN = 'You are banned from the chat. You can still read it.';
    const k = rig({ chip: true }); k.live(W({ login: true, you: ME }));
    k.s().recv({ t: 'bye', why: 'kicked' });
    check('bye kicked: the notice (plain words) is there at once and stays (not for a few seconds)', k.note() === KICK && (k.clock.advance(20000), k.note() === KICK) && k.errors.length === 0);
    const first = k.sockets.length;
    k.s().fail();
    check('... the hub closes the socket: the page connects again once, after its wait (1 s at the least), as it does after any close, and the notice is there while it does', k.sockets.length === first && k.note() === KICK && (k.clock.advance(1600), k.sockets.length === first + 1));
    k.s().open().recv(W({ login: true, you: null }));
    check('the hub ended the login: the new welcome names nobody, the field is off and the Log in button is there, and the notice stays so that the person knows why', k.field().disabled === true && one(k.root, 'gc-login').hidden === false && k.note() === KICK && k.chip.getAttribute('aria-label') === 'Log in with Discord');
    k.s().fail(); k.clock.advance(5000); k.s().open().recv(W({ login: true, you: ME }));
    check('a welcome that names a visitor again (a login in another tab) ends the notice of a kick', k.note() === '' && k.field().disabled === false);
    const b = rig({ chip: true }); b.live(W({ login: true, you: ME }));
    b.s().recv({ t: 'bye', why: 'banned' });
    check('bye banned: the notice says that the person can still read', b.note() === BAN);
    b.s().fail(); b.clock.advance(1600); b.s().open().recv(W({ login: true, you: ME, history: HISTORY }));
    check('a banned person reads on: the new welcome shows the history, the field is OFF (and says why), Send is gone, and so is the Log in button, and the notice stays', b.rows().length === 3 && b.field().disabled === true && b.field().getAttribute('placeholder') === 'You are banned from the chat' && one(b.root, 'gc-send').hidden === true && one(b.root, 'gc-login').hidden === true && b.note() === BAN);
    same('... a line cannot be sent (say() says banned, nothing goes out), and a /help is still answered', [b.dock.say('hello'), sentSays(b), (b.dock.say('/help'), b.answers().length)], ['banned', [], 1]);
    const again = rig({}); again.live(W({ login: true, you: ME })); again.dock.say('hello'); again.s().recv({ t: 'err', code: 'banned' });
    check('a ban that the page learns from the hub\'s refusal of a line (a page that was loaded after it) is the same: the notice, and the field off', again.note() === 'You cannot chat here' && (again.clock.advance(6100), again.note() === BAN) && again.field().disabled === true);
    const out = rig({}); out.live(W({ login: true, you: ME })); out.s().recv({ t: 'bye', why: 'banned' }); out.s().fail(); out.clock.advance(1600); out.s().open().recv(W({ login: true, you: null }));
    check('a banned person who is logged out is nobody here: the notice goes (it is the account\'s, not the page\'s)', out.note() === 'Chat is read-only for now' || out.note() === '');
    const odd = rig({}); odd.live(W({ login: true, you: ME }));
    ['x', null, 5, '', 'banned ', 'KICKED', ['kicked'], { why: 'kicked' }].forEach((why) => odd.s().recv({ t: 'bye', why }));
    check('a `bye` that is not one of the two words says nothing', odd.note() === '' && odd.field().disabled === false);
    const early = rig({}); early.s().open(); early.s().recv({ t: 'bye', why: 'banned' });
    check('a `bye` before the welcome is ignored', early.note() === '' && early.errors.length === 0);
    // no loop: a hub that welcomes and says bye again and again is met with the growing wait of any failing link, not with a storm
    const storm = rig({ random: () => 0.5 }); let tries = 0;
    for (let t = 0; t < 600; t += 1) {
        if (storm.s() && storm.sockets.length > tries) { tries = storm.sockets.length; storm.s().open().recv(W({ login: true, you: ME })); storm.s().recv({ t: 'bye', why: 'kicked' }); storm.s().fail(); }
        storm.clock.advance(1000);
    }
    check('a hub that says bye to every socket at once: 10 minutes make a few tries (the wait doubles to a minute), not a try a second', storm.sockets.length >= 8 && storm.sockets.length <= 20, storm.sockets.length);
    const logout = rig({}); logout.live(W({ login: true, you: ME })); logout.s().recv({ t: 'bye', why: 'banned' });
    check('(the notice is the page\'s and the hub does not keep it: it is in memory only)', logout.note() === BAN);
}
{   // the source of the dock names no role and no command in a selector or in markup (a name or an argument is text)
    check('the dock never builds markup or a selector from what a person types or what the hub says: no innerHTML, no querySelector, no "new Function"', !/innerHTML|outerHTML|insertAdjacentHTML|querySelector|new Function|\beval\b/.test(chatSource.replace(/\/\/.*/g, '')));
}

console.log('home chat: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
