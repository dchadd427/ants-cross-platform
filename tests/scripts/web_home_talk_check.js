// Runs the emoji picker and the voice button of the chat (web/home/talk.js, docs/HOME_CHAT.md "Emoji and voice") against the REAL chat dock (web/home/chat.js, mounted on a fake hub) and a FAKE speech recognition
// (no microphone: the browser's constructor is a class of this file that the test drives by hand), in the fake browser of tests/scripts/web_page_harness.js. What it checks:
//   1. the pure parts: the emoji (one code point each, ASCII source, eight groups with a name and search words each), the search (names, search words, the rank, plurals), the code point count against the field's UTF-16
//      maxlength, the insert at the caret, the words that are said (spacing, no markup characters, the cut), and when the browser has speech recognition at all (a secure page and a constructor, with or without the prefix);
//   2. the buttons: where they stand in the dock's form, their labels and the privacy words in the tooltip, hidden with the field when nobody may write, and NO microphone button with no support or on a page that is not secure;
//   3. the picker: opens and closes (the button, Escape with the focus back in the field, a press outside, a send), the nine tabs, the search (results, no result, Enter picks the first and never sends), the arrow keys, Home, End and
//      the page keys over rows and the recently used row, the recently used emoji (kept in the browser, read with care, a browser that refuses its storage), the name line, where the picker stands (a box, a row over a low window,
//      a sheet on a phone upright, the keyboard's lift), a pick goes in at the caret and replaces a selection, a full line is said and refused, Escape does not fold the dock;
//   4. the voice: the first note, listening, interim words in the field and the final ones in their place, the words around the caret kept, never sent, a second press stops, Escape stops, typing, a send, a hidden page and a
//      page that goes away end it at once and late events are not heard, every error in plain words, a recognizer that throws, a line that fills, markup that is said stays text;
//   5. the page (web/lobby.html with web/home/home.js): the two files come once, after the dock, and a missing or broken one changes nothing;
//   6. the source of the script: ASCII, no markup, no storage, no network, no recording.
// tests/scripts/test_web_home_talk.py runs this with node (the quick tier). usage: node web_home_talk_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
globalThis.AntsShapes = require(path.resolve(__dirname, '..', '..', 'web', 'shapes.js'));

const lobbyPath = process.argv[2];
if (!lobbyPath) { console.log('usage: web_home_talk_check.js lobby.html'); process.exit(2); }
const lobbyDir = path.dirname(path.resolve(lobbyPath));
const talkPath = path.join(lobbyDir, 'home', 'talk.js');
const talkSource = fs.readFileSync(talkPath, 'utf8');
const Pure = require(talkPath);
const Chat = require(path.join(lobbyDir, 'home', 'chat.js'));
const H = require('./web_page_harness.js')(lobbyPath);
const { El, runLobby } = H;

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
const THROWS = {};       // (a browser that refuses its storage)
const GRIN = String.fromCodePoint(0x1F600), ANT = String.fromCodePoint(0x1F41C), STAR = String.fromCodePoint(0x2728), CAT = String.fromCodePoint(0x1F408);       // (built from code points: the sources are ASCII)

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The pure parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Pure;
    const groups = P.emoji(), all = [].concat(...groups.map((g) => g.list));
    same('the groups, in order', groups.map((g) => g.id), ['smileys', 'people', 'animals', 'food', 'activities', 'travel', 'objects', 'symbols']);
    same('...with their names', groups.map((g) => g.name), ['Smileys', 'People', 'Animals and nature', 'Food and drink', 'Activities and games', 'Travel and places', 'Objects', 'Symbols']);
    check('every single code point emoji of Unicode 9 that the hub keeps and that draws by itself: 879, none twice', all.length === 879 && new Set(all).size === all.length, all.length);
    check('every emoji is ONE code point (no joiner, no variation selector, no tag character: the hub keeps it as it is)', all.every((e) => P.points(e) === 1 && [...e].length === 1));
    check('...and the dock\'s own cleaner (the hub\'s rule) leaves each one as it is', all.every((e) => Chat.plain(e) === e) && Chat.plain(all.join('')) === all.join(''));
    check('...and none is a markup character or a blank', all.every((e) => !/[<>\s]/.test(e)));
    check('every emoji has a name (ASCII, a capital first, no separator of the table) and the search words are lower case ASCII', groups.every((g) => g.names.length === g.list.length && g.keys.length === g.list.length && g.names.every((n) => /^[A-Z0-9][ -~]*$/.test(n) && !/[;~@]/.test(n)) && g.keys.every((ks) => ks.every((k) => /^[a-z0-9][a-z0-9 ]*$/.test(k)))));
    check('the ant is there, and the grin, and the sparkles', all.indexOf(ANT) !== -1 && all.indexOf(GRIN) !== -1 && all.indexOf(STAR) !== -1 && groups[2].list.indexOf(ANT) !== -1 && groups[0].list[0] === GRIN);
    check('talk.js is ASCII only', /^[\x00-\x7f]*$/.test(talkSource));
    same('chr: a code point of the basic plane and one beyond it', [Pure.emoji()[0].list[0], STAR], [GRIN, STAR]);
    same('points counts code points, not UTF-16 units', [P.points(''), P.points('abc'), P.points(GRIN), P.points(GRIN + 'a' + STAR), P.points('\ud83d')], [0, 3, 1, 3, 1]);
    same('head: a pair of surrogates is never cut', [P.head('ab' + GRIN + 'c', 2), P.head('ab' + GRIN + 'c', 3), P.head('ab' + GRIN + 'c', 9), P.head(GRIN, 0)], ['ab', 'ab' + GRIN, 'ab' + GRIN + 'c', '']);
    same('insert: at the caret, after it, and in place of a selection', [P.insert('hello', 2, 2, GRIN), P.insert('hello', 5, 5, GRIN), P.insert('hello', 1, 4, STAR), P.insert('', 0, 0, ANT)],
        [{ value: 'he' + GRIN + 'llo', caret: 4 }, { value: 'hello' + GRIN, caret: 7 }, { value: 'h' + STAR + 'o', caret: 2 }, { value: ANT, caret: 2 }]);
    same('insert: a caret that is not a number is the end', [P.insert('ab', undefined, undefined, GRIN), P.insert('ab', 9, 9, GRIN), P.insert('ab', 1, 0, GRIN)], [{ value: 'ab' + GRIN, caret: 4 }, { value: 'ab' + GRIN, caret: 4 }, { value: 'a' + GRIN + 'b', caret: 3 }]);
    check('insert: 1,000 code points fit, the 1,001st is refused', P.insert('a'.repeat(999), 999, 999, GRIN) !== null && P.insert('a'.repeat(1000), 1000, 1000, GRIN) === null && P.insert('a'.repeat(1000), 0, 1, GRIN) !== null);
    check('insert: 1,000 emoji outside the basic plane are 2,000 UTF-16 units (the field\'s maxlength) and fit; one more does not', (() => { let v = ''; for (let i = 0; i < 1000; i++) { const r = P.insert(v, v.length, v.length, GRIN); if (!r) return false; v = r.value; } return v.length === Chat.LIMIT.field && P.insert(v, v.length, v.length, GRIN) === null; })());
    check('insert: the units never pass the maxlength where the code points fit (the worst is 2 units for 1 point)', 2 * Pure.LIMIT <= Chat.LIMIT.field && Pure.LIMIT === Chat.LIMIT.line);
    same('the line is 1,000 code points and the field 2,000 UTF-16 units (the hub\'s own line, twice for the pairs)', [Pure.LIMIT, Chat.LIMIT.line, Chat.LIMIT.field], [1000, 1000, 2000]);
    same('speak: the words go in at the caret with one space on each side', [P.speak('hello', 'big world', ''), P.speak('hello ', 'big', ''), P.speak('a', 'b', 'c'), P.speak('', 'hi', ''), P.speak('x ', 'y', ' z')],
        [{ value: 'hello big world', caret: 15 }, { value: 'hello big', caret: 9 }, { value: 'a b c', caret: 3 }, { value: 'hi', caret: 2 }, { value: 'x y z', caret: 3 }]);
    same('speak: nothing said leaves the line as it was; blanks collapse; markup characters go', [P.speak('a', '', 'b'), P.speak('a', ' \n ', 'b'), P.speak('', '  one   two ', ''), P.speak('', '<b>hi</b>', '')], [{ value: 'ab', caret: 1 }, { value: 'ab', caret: 1 }, { value: 'one two', caret: 7 }, { value: 'b hi /b', caret: 7 }]);
    check('speak: what is said is cut to what is left of the 1,000 code points, around the words that stay', (() => { const r = P.speak('a'.repeat(950), 'w '.repeat(100), 'z'.repeat(20)); return P.points(r.value) <= 1000 && r.value.indexOf('z'.repeat(20)) !== -1 && r.value.slice(0, 950) === 'a'.repeat(950); })());
    check('speak: a pair of surrogates at the cut is whole', (() => { const r = P.speak('', GRIN.repeat(1500), ''); return P.points(r.value) <= 1000 && !/[\ud800-\udbff]$/.test(r.value) && r.value.length % 2 === 0; })());
    const W = (o) => Object.assign({}, o);
    class Ctor {}
    same('recognition: a secure page and a constructor', [P.recognition(W({ isSecureContext: true, SpeechRecognition: Ctor })) === Ctor, P.recognition(W({ isSecureContext: true, webkitSpeechRecognition: Ctor })) === Ctor], [true, true]);
    same('recognition: none without a constructor, on a page that is not secure (false, missing) or with something that is no constructor', [P.recognition(W({ isSecureContext: true })), P.recognition(W({ isSecureContext: false, SpeechRecognition: Ctor })), P.recognition(W({ SpeechRecognition: Ctor })),
        P.recognition(W({ isSecureContext: true, SpeechRecognition: {} })), P.recognition(null), P.recognition(W({ isSecureContext: 'yes', SpeechRecognition: Ctor }))], [null, null, null, null, null, null]);
    same('the words: the tooltip says who hears you', Pure.TEXT.voice, 'Voice to text: your browser\'s speech service hears you');
    const T = P.table(), names = (q) => P.search(T, q).map((e) => e.name);
    same('search: a name, whole or a start of it, lower or upper case', [names('ant')[0], names('ANT')[0], names('ants')[0], names('thum')[0]], ['Ant', 'Ant', 'Ant', 'Thumbs up']);
    check('search: a search word that the name does not carry (lol, bunny, poop, rofl)', names('lol').indexOf('Face with tears of joy') !== -1 && names('bunny').indexOf('Rabbit') !== -1 && names('poop')[0] === 'Pile of poo' && names('rofl').indexOf('Rolling on the floor laughing') !== -1);
    same('search: a whole word of the name comes before a word that starts with it, and the shorter name first: "cat" is the cat, then the faces of cats', names('cat').slice(0, 3), ['Cat', 'Grinning cat', 'Kissing cat']);
    check('search: a whole search word comes before a name that starts with the word (a trophy for "win" before a winking face)', names('win').indexOf('Trophy') < names('win').indexOf('Winking face'));
    check('search: a word starts a word of the name, it is not found inside one ("art" is the palette, not a heart)', names('art').indexOf('Beating heart') === -1 && names('art').indexOf('Artist palette') !== -1);
    check('search: a start of a search word counts from 4 letters (not "cat" for a catch)', names('cat').indexOf('Fishing pole') === -1 && names('catc').indexOf('Fishing pole') !== -1);
    same('search: every word must answer (two words narrow it), and a word that nothing answers gives nothing', [names('thumbs up'), names('cat qqq'), names('qqqq')], [['Thumbs up'], [], []]);
    same('search: no word, nothing (blanks, punctuation, an empty string)', [names(''), names('   '), names('!?'), names('<>')], [[], [], [], []]);
    check('search: a plural is its singular (hearts, cats)', names('hearts').length === names('heart').length && names('cats').length === names('cat').length);
    check('search: a hundred results at least for a letter (m), and the results are the table\'s own entries (no copy of the characters is made)', P.search(T, 'm').length > 100 && P.search(T, 'm').every((e) => T.by[e.ch] === e));
    check('the table: a name, search words and the words of both for every one, the characters unique', T.all.length === 879 && T.all.every((e) => e.nw.length > 0 && Array.isArray(e.keys)) && Object.keys(T.by).length === 879);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the fakes: the real dock on a fake hub, a fake speech recognition, a fake page
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function loadScript(source, exportName, clock) {
    const root = {};
    new Function('self', 'setTimeout', 'clearTimeout', source)(root, clock.setTimeout, clock.clearTimeout);
    return root[exportName];
}
// o.speech: false (none), 'plain' / 'webkit' (the constructor's name), o.secure: false (a page that is not secure), o.lang, o.startThrows, o.noLogin (the dock's field is off: nobody may write), o.size ([width, height] of the window),
// o.storage (the browser's local storage: an object with getItem and setItem, or THROWS for a browser that refuses it), o.vv (a visualViewport)
function rig(o) {
    o = o || {};
    const env = { elements: {}, innerHTMLWrites: [] };
    const clock = { setTimeout: (fn, ms) => 0, clearTimeout() {} };
    const ChatApi = loadScript(fs.readFileSync(path.join(lobbyDir, 'home', 'chat.js'), 'utf8'), 'AntsHomeChat', clock);
    const html = new El(env, 'html', '');
    html.style.setProperty = (k, v) => { html.style[k] = v; }; html.style.removeProperty = (k) => { delete html.style[k]; };
    const doc = { hidden: false, listeners: {}, documentElement: html, createElement: (t) => new El(env, t), addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); } };
    env.doc = doc;
    const win = { innerWidth: o.size ? o.size[0] : 1280, innerHeight: o.size ? o.size[1] : 800, location: { protocol: 'https:', host: 'play.test' }, listeners: {}, addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); }, isSecureContext: o.secure !== false, navigator: { language: o.lang || 'de-DE' } };
    if (o.storage === THROWS) Object.defineProperty(win, 'localStorage', { get() { throw new Error('storage is refused'); } });
    else if (o.storage) win.localStorage = o.storage;
    if (o.vv) win.visualViewport = o.vv;
    const recs = [];
    class Rec {
        constructor() { this.starts = 0; this.stops = 0; this.aborts = 0; recs.push(this); }
        start() { if (o.startThrows) throw new Error('a recognizer that is busy'); this.starts++; }
        stop() { this.stops++; }
        abort() { this.aborts++; }
    }
    if (o.speech !== false) win[o.speech === 'webkit' ? 'webkitSpeechRecognition' : 'SpeechRecognition'] = Rec;
    const sockets = [];
    class HubSocket {
        constructor(url) { this.url = url; this.sent = []; this.readyState = 0; sockets.push(this); }
        send(text) { if (this.readyState !== 1) throw new Error('send on a socket that is not open'); this.sent.push(text); }
        close() { this.readyState = 3; }
        open() { this.readyState = 1; if (this.onopen) this.onopen({}); return this; }
        recv(x) { if (this.onmessage) this.onmessage({ data: JSON.stringify(x) }); return this; }
    }
    const host = new El(env, 'div', 'home');
    const dock = ChatApi.mount({ doc, win, host, chip: null, screen: () => 'home', fetch: undefined, now: () => 0, random: () => 0, WebSocket: HubSocket, error: (e) => { throw e; } });
    dock.shown(true);
    sockets[0].open().recv({ t: 'welcome', v: 1, you: o.noLogin ? null : { name: 'maple.leaf', mod: false }, login: !!o.noLogin, online: 3, history: [] });
    const timers = [], tclock = { t: 0, setTimeout: (fn, ms) => { timers.push({ fn, at: tclock.t + ms }); return timers.length; }, clearTimeout: (id) => { if (timers[id - 1]) timers[id - 1].fn = null; },
        advance: (ms) => { tclock.t += ms; timers.forEach((x) => { if (x.fn && x.at <= tclock.t) { const f = x.fn; x.fn = null; f(); } }); } };       // (a fake clock for the line of words: the dock's own never moves)
    const Talk = loadScript(talkSource, 'AntsHomeTalk', tclock);
    const taps = [], realTap = dock.tap;
    dock.tap = (fn) => { taps.push(fn); realTap(fn); };      // (the dock's own taps, and a way to tell the buttons what the dock tells them)
    const talk = Talk.mount({ doc, win, dock, emoji: o.emoji, limit: o.limit });
    const r = { env, doc, win, host, dock, talk, recs, sockets, clock: tclock, tell: (kind) => taps.forEach((fn) => fn(kind)) };
    r.field = dock.field;
    r.form = dock.field.parent;
    r.emoji = o.emoji === false ? null : one(r.form, 'gt-emoji');
    r.mic = one(r.form, 'gt-mic');
    r.pick = o.emoji === false ? null : one(r.form, 'gt-pick');
    r.say = one(r.form, 'gt-say');
    r.keys = () => (r.pick ? byClass(r.pick, 'gt-e') : []);
    r.tabs = () => byClass(r.pick, 'gt-tab');
    r.q = r.pick ? one(r.pick, 'gt-q') : null;
    r.clear = r.pick ? one(r.pick, 'gt-x') : null;
    r.cap = () => byClass(r.pick, 'gt-cap').map((e) => e.textContent);
    r.html = html;
    r.body = r.pick ? one(r.pick, 'gt-body') : null;
    r.tabsBox = r.pick ? one(r.pick, 'gt-tabs') : null;
    r.nm = r.pick ? one(r.pick, 'gt-nm') : null;
    r.kw = r.pick ? one(r.pick, 'gt-kw') : null;
    r.big = r.pick ? one(r.pick, 'gt-big') : null;
    r.key = (target, k, extra) => { let stopped = false; target.fire('keydown', Object.assign({ key: k, preventDefault() { stopped = true; } }, extra)); return stopped; };
    r.sayText = () => (r.say.hidden ? '' : r.say.textContent);
    r.rec = () => recs[recs.length - 1];
    r.heard = (...parts) => { const rec = r.rec(); rec.onresult({ results: parts.map((t) => [{ transcript: t, confidence: 0.9 }]) }); };
    r.start = () => { r.mic.click(); const rec = r.rec(); if (rec.onstart) rec.onstart({}); return rec; };
    r.caret = (a, b) => { r.field.selectionStart = a; r.field.selectionEnd = b === undefined ? a : b; };
    r.field.setSelectionRange = (a, b) => { r.field.selectionStart = a; r.field.selectionEnd = b; };
    r.esc = () => { let stopped = false; const ev = { key: 'Escape', stopPropagation() { stopped = true; }, preventDefault() {} }; r.form.fire('keydown', ev); if (!stopped) dock.root.fire('keydown', ev); return stopped; };
    r.said = () => sockets.reduce((all, s) => all.concat(s.sent.map((x) => JSON.parse(x)).filter((m) => m.t === 'say')), []);
    r.hide = (on) => { doc.hidden = on; (doc.listeners.visibilitychange || []).forEach((fn) => fn({})); };
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. The buttons
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig();
    const names = r.form.children.map((c) => cls(c).filter((x) => /^(gc-input|gc-send|gt-)/.test(x))[0]);
    same('in the dock\'s form: the words, the picker, then the field, the two buttons and Send (the login button after it)', names.slice(0, 6), ['gt-say', 'gt-pick', 'gc-input', 'gt-tools', 'gc-send', 'gc-send']);
    check('the form carries the class that gives it a third column', cls(r.form).indexOf('gt-on') !== -1 && cls(r.form).indexOf('gc-form') !== -1);
    same('the emoji button: a button, a label and a title, closed, controls the picker', [r.emoji.tagName, r.emoji.type, r.emoji.getAttribute('aria-label'), r.emoji.getAttribute('title'), r.emoji.getAttribute('aria-expanded'), r.emoji.getAttribute('aria-controls'), r.pick.id], ['button', 'button', 'Emoji', 'Emoji', 'false', 'gt-pick', 'gt-pick']);
    same('the microphone button: a button, not pressed, and its label and tooltip say that the browser\'s speech service hears you', [r.mic.tagName, r.mic.type, r.mic.getAttribute('aria-pressed'), r.mic.getAttribute('aria-label'), r.mic.getAttribute('title')],
        ['button', 'button', 'false', 'Voice to text: your browser\'s speech service hears you', 'Voice to text: your browser\'s speech service hears you']);
    const kinds = (b) => b.children.map((c) => c.tagName + ':' + (c.attributes.viewBox || '') + ':' + c.children.map((x) => x.tagName).join('+'));
    same('the emoji button holds one SVG: a circle, two eyes and a smile', kinds(r.emoji), ['svg:0 0 24 24:circle+circle+circle+path']);
    same('the microphone button holds one SVG: a capsule and the path of the holder, stem and base', kinds(r.mic), ['svg:0 0 24 24:rect+path']);
    check('both are hidden from the screen reader (the button has the name), and the drawing is in no fixed color', [r.emoji, r.mic].every((b) => b.children[0].attributes['aria-hidden'] === 'true' && b.children[0].attributes.focusable === 'false' && !b.findAll((e) => e.attributes && (e.attributes.fill || e.attributes.stroke)).length) && r.mic.children[0].children[0].className === 'gt-cap');
    check('the words line is a polite status and empty; the picker is closed', r.say.getAttribute('role') === 'status' && r.say.getAttribute('aria-live') === 'polite' && r.say.hidden === true && r.pick.hidden === true);
    check('the buttons are shown while a writer may type', r.talk.root.hidden === false);
    check('no id of the page was taken twice', r.pick.id === 'gt-pick');
}
for (const [label, o] of [['no speech recognition (Firefox)', { speech: false }], ['a page that is not secure', { secure: false }]]) {
    const r = rig(o);
    check(label + ': NO microphone button anywhere in the chat, only the emoji button', r.mic === null && byClass(r.dock.root, 'gt-mic').length === 0 && r.talk.mic === null && r.emoji !== null && r.talk.root.children.length === 1);
    check(label + ': and no recognizer was made', r.recs.length === 0);
}
{
    const r = rig({ speech: 'webkit' });
    check('the prefixed constructor (Chrome, Edge, Safari) gives the button', r.mic !== null);
    const q = rig({ noLogin: true });
    check('nobody may write (the field is off): both buttons are hidden with it', q.field.disabled === true && q.talk.root.hidden === true);
    q.field.disabled = false; q.tell('live');
    check('...and shown again when the field is on (the dock tells the buttons through its taps)', q.talk.root.hidden === false);
    q.field.disabled = true; q.tell('lost');
    check('...and hidden again when it goes off', q.talk.root.hidden === true);
    q.field.disabled = false; q.tell('live'); q.talk.emoji.click(); const before = q.field.value; q.field.disabled = true; q.tell('field');
    check('a field that goes off for a ban (the dock\'s tap `field`): the buttons go with it, the picker closes, and an emoji cannot be put into it', q.talk.root.hidden === true && q.talk.pick.hidden === true && q.field.value === before);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. The picker
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function geo(r) {       // the fake page has no layout: the emoji stand in rows of 8 (44 px), the recently used row on top, the group's rows from 100 px
    byClass(r.pick, 'gt-grid').forEach((g) => { const top = cls(g.parent).indexOf('gt-recent') !== -1 ? 0 : 100; byClass(g, 'gt-e').forEach((b, i) => { b.offsetLeft = 44 * (i % 8); b.offsetTop = top + 44 * Math.floor(i / 8); }); });
}
const store = (start) => { const data = { v: start }; return { data, getItem: (k) => (k === 'ants.emoji.recent' && data.v !== undefined ? data.v : null), setItem: (k, v) => { if (k === 'ants.emoji.recent') data.v = v; else throw new Error('another key: ' + k); } }; };
const HEX = (e) => e.codePointAt(0).toString(16).toUpperCase();
{   // the structure
    const r = rig();
    check('the picker is closed at first and holds a tab list, a search field, a panel with the recently used row and the group, and a line with the name', r.pick.hidden === true && !!r.tabsBox && !!r.q && !!r.body && !!r.nm && byClass(r.pick, 'gt-recent').length === 1 && byClass(r.pick, 'gt-page').length === 1);
    r.emoji.click();
    same('nine tabs: a button each with a name, in order (the recently used ones, then the eight groups), one picture each', r.tabs().map((b) => [b.tagName, b.type, b.getAttribute('role'), b.getAttribute('aria-label'), b.getAttribute('title'), b.children.length + ':' + b.children[0].tagName]),
        ['Recently used', 'Smileys', 'People', 'Animals and nature', 'Food and drink', 'Activities and games', 'Travel and places', 'Objects', 'Symbols'].map((n) => ['button', 'button', 'tab', n, n, '1:svg']));
    same('the tab list and the panel have their roles; the smileys are selected and the only tab in the tab order', [r.tabsBox.getAttribute('role'), r.body.getAttribute('role'), r.tabs().map((b) => b.getAttribute('aria-selected')), r.tabs().map((b) => b.tabIndex)], ['tablist', 'tabpanel', ['false', 'true', 'false', 'false', 'false', 'false', 'false', 'false', 'false'], [-1, 0, -1, -1, -1, -1, -1, -1, -1]]);
    same('the search field: a text field with a label, a hint and no spelling help, at most 40 letters', [r.q.tagName, r.q.type, r.q.getAttribute('aria-label'), r.q.getAttribute('placeholder'), r.q.getAttribute('autocomplete'), r.q.getAttribute('autocapitalize'), r.q.getAttribute('autocorrect'), r.q.getAttribute('spellcheck'), r.q.getAttribute('maxlength'), r.q.getAttribute('enterkeyhint')],
        ['input', 'text', 'Search emoji', 'Search emoji (cat, heart, ant...)', 'off', 'none', 'off', 'false', '40', 'search']);
    check('the group shows its name over its emoji; nothing is recent yet, so the recently used row is hidden', r.cap().indexOf('Smileys') !== -1 && r.pick.findAll((e) => cls(e).indexOf('gt-recent') !== -1)[0].hidden === true && r.clear.hidden === true);
    check('every emoji is a button with its name (a screen reader says the name, not the picture) and only text', r.keys().length === 120 && r.keys().every((b) => b.tagName === 'button' && b.type === 'button' && P(b.textContent) === 1 && !!b.getAttribute('aria-label') && b.getAttribute('aria-label') === Pure.emoji()[0].names[r.keys().indexOf(b)]));
    function P(s) { return Pure.points(s); }
    check('the name line is hidden from the screen reader (the emoji have names), shows "Pick an emoji" at first', r.pick.findAll((e) => cls(e).indexOf('gt-foot') !== -1)[0].getAttribute('aria-hidden') === 'true' && r.nm.textContent === 'Pick an emoji' && r.big.textContent === '' && r.kw.textContent === '');
    check('only one emoji is in the tab order (the arrow keys move)', r.keys().filter((b) => b.tabIndex === 0).length === 1 && r.keys()[0].tabIndex === 0);
}
{   // opening, closing, a pick
    const r = rig();
    r.field.value = 'hi '; r.caret(3);
    r.emoji.click();
    check('the emoji button opens the picker: shown, expanded, and the focus is in the search field (the keyboard: type to look)', r.pick.hidden === false && r.emoji.getAttribute('aria-expanded') === 'true' && r.env.focused === r.q);
    r.keys()[0].click();
    check('a pick goes in at the caret, and the caret is after it', r.field.value === 'hi ' + GRIN && r.field.selectionStart === 3 + 2 && r.field.selectionEnd === 5);
    check('the picker stays open and the dock says nothing (it is not sent)', r.pick.hidden === false && r.said().length === 0 && r.sayText() === '');
    r.caret(0); r.keys()[1].click();
    check('the next one goes in where the caret is now', r.field.value === Pure.emoji()[0].list[1] + 'hi ' + GRIN);
    r.caret(0, 2); r.tabs()[3].click();
    r.keys().find((b) => b.textContent === ANT).click();
    check('a selection is replaced (the ant from the animals tab)', r.field.value === ANT + 'hi ' + GRIN);
    check('the field keeps what is not an emoji, the input is plain text (no markup)', r.env.innerHTMLWrites.length === 0);
    check('Escape closes the picker, the focus is back in the field, and the dock stays open (the event does not reach it)', (() => { const stopped = r.esc(); return stopped && r.pick.hidden === true && r.emoji.getAttribute('aria-expanded') === 'false' && r.env.focused === r.field && r.dock.root.getAttribute('data-open') !== 'false'; })());
    check('...(and Escape with nothing open is left to the dock)', r.esc() === false);
    r.emoji.click(); r.emoji.click();
    check('the button again closes it, the focus goes to the field', r.pick.hidden === true && r.env.focused === r.field);
    r.emoji.click();
    r.doc.listeners.pointerdown.forEach((fn) => fn({ target: r.field }));
    check('a press inside the form (the field) keeps it open', r.pick.hidden === false);
    r.doc.listeners.pointerdown.forEach((fn) => fn({ target: r.q }));
    check('...and in the picker (the search field, an emoji)', r.pick.hidden === false);
    r.doc.listeners.pointerdown.forEach((fn) => fn({ target: new El(r.env, 'div', '') }));
    check('a press outside closes it', r.pick.hidden === true && r.emoji.getAttribute('aria-expanded') === 'false');
    r.emoji.click();
    r.form.fire('submit', { preventDefault() {} });
    check('a send closes it', r.pick.hidden === true);
    r.emoji.click();
    r.doc.listeners.pointerdown.forEach((fn) => fn({}));
    check('a press with no target does nothing', r.pick.hidden === false);
    r.hide(true);
    check('a hidden page closes it', r.pick.hidden === true);
    r.hide(false);
    r.emoji.click(); r.q.value = 'cat'; r.q.fire('input', {}); r.esc(); r.emoji.click();
    check('it opens with nothing searched, whatever was there when it closed, on the group that was shown last (the animals, here)', r.q.value === '' && r.clear.hidden === true && r.tabs()[3].getAttribute('aria-selected') === 'true' && byClass(one(r.pick, 'gt-page'), 'gt-e').length === 102);
}
{   // the tabs
    const r = rig();
    r.emoji.click();
    const groups = Pure.emoji();
    r.tabs()[4].click();
    check('a tab shows its group: the name over it, the emoji of that group only, the tab selected and the only one in the tab order', r.cap().indexOf('Food and drink') !== -1 && r.keys().map((b) => b.textContent).join('') === groups[3].list.join('') && r.tabs()[4].getAttribute('aria-selected') === 'true' && r.tabs().filter((b) => b.tabIndex === 0).length === 1 && r.tabs()[4].tabIndex === 0 && cls(r.tabs()[4]).indexOf('gt-sel') !== -1 && cls(r.tabs()[1]).indexOf('gt-sel') === -1);
    check('every group shows all of its emoji (120, 115, 102, 86, 58, 151, 123, 124), and the panel starts at its top', groups.every((g, i) => { r.tabs()[i + 1].click(); return r.keys().length === g.list.length; }) && groups.map((g) => g.list.length).join() === '120,115,102,86,58,151,123,124');
    r.tabs()[0].click();
    check('the first tab is the recently used ones: with none, the line that says where they will be, and no emoji', r.cap().indexOf('Emoji that you pick show up here.') !== -1 && r.keys().length === 0);
    r.tabs()[2].click();
    r.key(r.tabsBox, 'ArrowRight');
    check('the right arrow in the tab list goes to the next tab and shows it, with the focus on it', r.tabs()[3].getAttribute('aria-selected') === 'true' && r.env.focused === r.tabs()[3] && r.cap().indexOf('Animals and nature') !== -1);
    r.key(r.tabsBox, 'ArrowLeft'); r.key(r.tabsBox, 'ArrowLeft');
    check('the left arrow goes back', r.tabs()[1].getAttribute('aria-selected') === 'true' && r.env.focused === r.tabs()[1]);
    r.key(r.tabsBox, 'ArrowLeft'); r.key(r.tabsBox, 'ArrowLeft'); r.key(r.tabsBox, 'ArrowLeft');
    check('...not past the first tab', r.tabs()[0].getAttribute('aria-selected') === 'true');
    r.key(r.tabsBox, 'End');
    check('End goes to the last tab, and no further', r.tabs()[8].getAttribute('aria-selected') === 'true' && (r.key(r.tabsBox, 'ArrowRight'), r.tabs()[8].getAttribute('aria-selected') === 'true'));
    r.key(r.tabsBox, 'Home');
    check('Home goes to the first', r.tabs()[0].getAttribute('aria-selected') === 'true');
    check('another key is left alone (Tab, a letter)', !r.key(r.tabsBox, 'Tab') && !r.key(r.tabsBox, 'a'));
}
{   // the search
    const r = rig();
    r.emoji.click();
    r.q.value = 'cat'; r.q.fire('input', {});
    const found = Pure.search(Pure.table(), 'cat');
    check('a word in the search field: the results replace the group, the count is the line over them, the tabs are dimmed (a class), the clear button shows, the recently used row is gone', r.keys().map((b) => b.textContent).join('') === found.map((e) => e.ch).join('') && r.cap().indexOf(found.length + ' results') !== -1 && cls(r.pick).indexOf('gt-seeking') !== -1 && r.clear.hidden === false && r.tabs().every((b) => cls(b).indexOf('gt-sel') === -1));
    check('the first result is the cat; the line over the results is a polite status', r.keys()[0].textContent === CAT && one(r.pick, 'gt-page').children[0].getAttribute('role') === 'status');
    r.q.value = 'ant qq'; r.q.fire('input', {});
    check('nothing found: the line says so, no emoji', r.keys().length === 0 && r.cap().indexOf('No emoji found. Try another word.') !== -1);
    r.q.value = 'cat'; r.q.fire('input', {});
    const enter = r.key(r.q, 'Enter');
    check('Enter in the search field picks the first result, never sends the chat line (the key is ours), and the picker and the search stay', enter && r.field.value === CAT && r.said().length === 0 && r.pick.hidden === false && r.q.value === 'cat');
    r.q.value = 'qqq'; r.q.fire('input', {});
    r.field.value = ''; const again = r.key(r.q, 'Enter');
    check('Enter with no result puts nothing in, and the key is still ours', again && r.field.value === '');
    r.q.value = ''; r.q.fire('input', {});
    check('Enter with nothing searched puts nothing in, and the key is still ours', r.key(r.q, 'Enter') && r.field.value === '' && r.said().length === 0);
    r.q.value = 'heart'; r.q.fire('input', {});
    r.key(r.q, 'ArrowDown');
    check('the down arrow in the search field goes into the results (the first one)', r.env.focused === r.keys()[0]);
    r.clear.fire('pointerdown', { preventDefault() {} });
    r.clear.click();
    check('the clear button empties the search, shows the group again, hides itself and puts the focus in the field', r.q.value === '' && r.clear.hidden === true && byClass(one(r.pick, 'gt-page'), 'gt-e').length === 120 && cls(r.pick).indexOf('gt-seeking') === -1 && r.env.focused === r.q);
    r.q.value = 'heart'; r.q.fire('input', {});
    r.tabs()[5].click();
    check('a tab pressed while searching ends the search and shows its group', r.q.value === '' && r.cap().indexOf('Activities and games') !== -1 && r.clear.hidden === true);
    r.q.value = 'f'; r.q.fire('input', {});
    check('a long result list (more than 160 for "f") is cut at 160, and the line says so', Pure.search(Pure.table(), 'f').length > 160 && r.keys().length === 160 && r.cap().some((c) => /^Showing 160 of \d+ results\./.test(c)));
    r.q.value = 'ant'; r.q.fire('input', {});
    r.key(r.q, 'ArrowLeft'); r.key(r.q, 'ArrowRight'); r.key(r.q, 'Home');
    check('the other keys of the search field are the field\'s own (the caret moves, nothing is taken)', !r.key(r.q, 'ArrowLeft') && !r.key(r.q, 'a') && !r.key(r.q, 'Backspace'));
    r.q.value = 'cat'; r.q.fire('input', {}); r.field.fire('input', {});
    check('typing in the chat field does not change the search', r.q.value === 'cat' && r.keys().length > 0);
}
{   // the arrow keys over the emoji
    const r = rig({ storage: store('1F600 1F41C 1F44D') });
    r.emoji.click(); geo(r);
    const K = () => r.keys(), at = () => K().indexOf(r.env.focused);
    check('the recently used row is above the group (three here), then the smileys', K().length === 3 + 120 && cls(K()[0].parent.parent).indexOf('gt-recent') !== -1 && K()[3].textContent === GRIN);
    r.key(r.q, 'ArrowDown');
    check('from the search field, down goes to the first emoji (the first recent one)', at() === 0);
    r.key(r.body, 'ArrowRight'); r.key(r.body, 'ArrowRight');
    check('right: one step, over the whole list in order (the recent row, then the group)', at() === 2 && r.key(r.body, 'ArrowRight') && at() === 3);
    r.key(r.body, 'ArrowLeft');
    check('left: one step back', at() === 2);
    r.key(r.body, 'ArrowDown');
    check('down from the third recent one: the emoji under it in the first row of the group (the same place from the left)', at() === 3 + 2);
    r.key(r.body, 'ArrowDown');
    check('down again: one row (8 emoji) lower', at() === 3 + 2 + 8);
    r.key(r.body, 'ArrowUp'); r.key(r.body, 'ArrowUp');
    check('up twice: back in the recently used row, over the same place', at() === 2);
    const left = r.key(r.body, 'ArrowUp');
    check('up from the top row goes back to the search field (the key is ours: the page does not scroll)', left && r.env.focused === r.q);
    r.key(r.q, 'ArrowDown');
    r.key(r.body, 'PageDown');
    check('a page down is three rows: from the recent row, 3 rows down (the 1st recent one: the group\'s row 3)', at() === 3 + 16);
    r.key(r.body, 'PageUp');
    check('a page up goes back as far as the rows go (to the recent row)', at() === 0);
    r.key(r.body, 'End');
    check('End goes to the last, and no further', at() === K().length - 1 && (r.key(r.body, 'ArrowRight'), at() === K().length - 1) && (r.key(r.body, 'ArrowDown'), at() === K().length - 1));
    r.key(r.body, 'Home'); r.key(r.body, 'ArrowLeft');
    check('Home goes to the first, and no further back', at() === 0);
    check('only the emoji that has the focus is in the tab order', K().filter((b) => b.tabIndex === 0).length === 1 && K()[0].tabIndex === 0);
    check('other keys (Tab among them) are left alone', !r.key(r.body, 'a') && !r.key(r.body, 'Tab') && at() === 0);
    K()[5].fire('focus', {});
    r.key(r.body, 'ArrowRight');
    check('an emoji that took the focus by a press or by Tab is where the arrows start', at() === 6 || r.env.focused === K()[6]);
    const w = rig(); w.emoji.click(); w.key(w.q, 'ArrowDown'); w.key(w.body, 'ArrowDown');
    check('a page that tells no places (no layout): the arrows up and down move nothing and break nothing', w.env.focused === w.keys()[0]);
}
{   // the recently used
    const s = store();
    const r = rig({ storage: s });
    r.emoji.click();
    r.keys()[2].click(); r.keys()[0].click(); r.keys()[2].click();
    check('a pick is kept at once, newest first, once: the browser\'s storage holds the code points (hex, separated by blanks), nothing else', s.data.v === HEX(r.keys()[2].textContent) + ' ' + HEX(r.keys()[0].textContent), s.data.v);
    check('the row does not move while the picker is open (an emoji does not slide away under a finger)', r.pick.findAll((e) => cls(e).indexOf('gt-recent') !== -1)[0].hidden === true && r.keys().length === 120);
    r.esc(); r.emoji.click();
    check('opened again: the recently used row stands over the group, newest first, with the group\'s name under it', byClass(byClass(r.pick, 'gt-recent')[0], 'gt-e').map((b) => HEX(b.textContent)).join(' ') === s.data.v && r.pick.findAll((e) => cls(e).indexOf('gt-recent') !== -1)[0].hidden === false && r.cap().indexOf('Recently used') !== -1 && r.cap().indexOf('Smileys') !== -1);
    r.tabs()[0].click();
    check('the first tab shows only them, under the line "Recently used" (and no second row of them over it)', r.keys().map((b) => HEX(b.textContent)).join(' ') === s.data.v && one(r.pick, 'gt-recent').hidden === true && byClass(one(r.pick, 'gt-recent'), 'gt-e').length === 0 && one(r.pick, 'gt-page').children[0].textContent === 'Recently used');
    const t = rig({ storage: store(Array.from({ length: 20 }, (_, i) => (0x1F600 + i).toString(16).toUpperCase()).join(' ')) });
    t.emoji.click(); t.tabs()[0].click();
    check('what the browser kept is read at the first opening: at most 16, in that order', t.keys().length === 16 && HEX(t.keys()[0].textContent) === '1F600' && HEX(t.keys()[15].textContent) === '1F60F');
    const q = store(); const u = rig({ storage: q });
    u.emoji.click(); for (let i = 0; i < 20; i++) u.keys()[i].click();
    check('twenty picks keep the newest 16 (no more is stored)', q.data.v.split(' ').length === 16 && q.data.v.split(' ')[0] === HEX(u.keys()[19].textContent));
    const junk = ['', '<img src=x onerror=1>', 'ZZZZ 1F600', '1F600 1F600 1F600', '1F600  1F41C', '0041 1F600', '1F600 110000 1F41C', '1F600\n1F41C', '1f600', '1F600 1F41C 1F600 1F44D'];
    const got = junk.map((v) => { const j = rig({ storage: store(v) }); j.emoji.click(); j.tabs()[0].click(); return j.keys().map((b) => HEX(b.textContent)).join(' '); });
    same('what is in the storage is read with care: only code points of the table, each once, anything else is ignored (markup, a letter, a number that is no emoji, lower case)', got, ['', '', '1F600', '1F600', '1F600 1F41C', '1F600', '1F600 1F41C', '', '', '1F600 1F41C 1F44D']);
    check('no markup was made from what the storage held', r.env.innerHTMLWrites.length === 0);
    const bad = rig({ storage: THROWS }); bad.emoji.click(); bad.keys()[0].click(); bad.esc(); bad.emoji.click(); bad.tabs()[0].click();
    check('a browser that refuses its storage (the access throws): the picker works, and the picks are kept for the visit', bad.field.value === GRIN && bad.keys().length === 1 && bad.keys()[0].textContent === GRIN);
    const writes = rig({ storage: { getItem() { throw new Error('no'); }, setItem() { throw new Error('full'); } } });
    writes.emoji.click(); writes.keys()[0].click(); writes.esc(); writes.emoji.click(); writes.tabs()[0].click();
    check('a storage that throws on a read and on a write: the same', writes.field.value === GRIN && writes.keys().length === 1);
    const none = rig(); none.emoji.click(); none.keys()[1].click(); none.esc(); none.emoji.click(); none.tabs()[0].click();
    check('no storage at all: the same', none.keys().length === 1);
    const row = rig({ storage: store(Array.from({ length: 16 }, (_, i) => (0x1F600 + i).toString(16).toUpperCase()).join(' ')) });
    row.emoji.click();
    check('over a group the recently used row is one row of the grid (8 here, where the page cannot say): the first 8 of them', byClass(byClass(row.pick, 'gt-recent')[0], 'gt-e').length === 8);
    const wide = rig({ storage: store('1F600 1F41C 1F44D') });
    wide.win.getComputedStyle = () => ({ gridTemplateColumns: '45px 45px 45px 45px 45px 45px' });
    wide.emoji.click();
    check('...and as many as the page says (6 columns: 6 of the 16)', byClass(byClass(wide.pick, 'gt-recent')[0], 'gt-e').length === 3);
    const hidden = rig({ storage: store(Array.from({ length: 16 }, (_, i) => (0x1F600 + i).toString(16).toUpperCase()).join(' ')) });
    hidden.win.getComputedStyle = () => ({ gridTemplateColumns: 'repeat(auto-fill, minmax(44px, 1fr))' });
    hidden.emoji.click();
    check('...a page that tells the rule and not the places (a box that is not drawn) is the page that cannot say: 8', byClass(byClass(hidden.pick, 'gt-recent')[0], 'gt-e').length === 8);
    const long = rig({ storage: store(Array.from({ length: 40 }, (_, i) => (0x1F600 + i).toString(16).toUpperCase()).join(' ')) });
    long.emoji.click(); long.tabs()[0].click();
    check('a stored text that is too long is read as far as 16 picks can go (and no further)', long.keys().length === 16);
    const six = rig({ storage: store(Array.from({ length: 16 }, (_, i) => (0x1F600 + i).toString(16).toUpperCase()).join(' ')) });
    six.win.getComputedStyle = () => ({ gridTemplateColumns: '45px 45px 45px 45px 45px 45px' });
    six.emoji.click();
    check('...(6 columns: six of sixteen)', byClass(byClass(six.pick, 'gt-recent')[0], 'gt-e').length === 6);
}
{   // the name line
    const r = rig();
    r.emoji.click();
    const first = r.keys()[2];
    first.fire('mouseover', {});
    check('pointing at an emoji (or focusing it) shows it large, with its name and its search words', r.big.textContent === first.textContent && r.nm.textContent === 'Face with tears of joy' && r.kw.textContent === 'lol / laugh / funny');
    r.pick.fire('mouseleave', {});
    check('leaving the picker with the pointer brings back "Pick an emoji"', r.nm.textContent === 'Pick an emoji' && r.big.textContent === '' && r.kw.textContent === '');
    r.keys()[0].fire('focus', {});
    check('a focused emoji shows too', r.nm.textContent === 'Grinning face');
    r.env.doc.activeElement = r.keys()[0]; r.pick.fire('mouseleave', {});
    check('the pointer leaving does not take the name of the emoji that has the focus', r.nm.textContent === 'Grinning face');
    r.tabs()[3].click();
    check('a new page starts with "Pick an emoji" again', r.nm.textContent === 'Pick an emoji');
}
{   // where the picker stands: a box, a row over a low window, a sheet at the bottom of a phone upright
    const html = (r) => [cls(r.html).indexOf('gt-sheet-open') !== -1, r.html.style['--gt-sheet'] || ''];
    const box = rig();
    box.form.getBoundingClientRect = () => ({ top: 705, left: 43, width: 1194, height: 56 });
    box.emoji.click();
    same('a wide window: a box (gt-pop), 400 px high where there is room, nothing else set, the page untouched', [cls(box.pick).filter((c) => /^gt-(pop|low|sheet)$/.test(c)), box.pick.style.height, box.pick.style.left, box.pick.style.bottom, html(box)], [['gt-pop'], '400px', '', '', [false, '']]);
    box.esc(); box.form.getBoundingClientRect = () => ({ top: 300, left: 43, width: 1194, height: 56 }); box.emoji.click();
    check('...as high as the room above the field (less 20 px) when there is less', box.pick.style.height === '280px');
    box.esc(); box.form.getBoundingClientRect = () => ({ top: 100, left: 43, width: 1194, height: 56 }); box.emoji.click();
    check('...and not under 120 px', box.pick.style.height === '120px');
    const low = rig({ size: [844, 390] });
    low.form.getBoundingClientRect = () => ({ top: 307, left: 43, width: 758, height: 56 });
    low.emoji.click();
    same('a low window (a phone held sideways): a row as wide as the form (gt-low), from 6 px under the top of the window to 12 px above the field', [cls(low.pick).filter((c) => /^gt-(pop|low|sheet|tight)$/.test(c)), low.pick.style.left, low.pick.style.width, low.pick.style.top, low.pick.style.height, html(low)], [['gt-low'], '43px', '758px', '6px', '295px', [false, '']]);
    low.esc(); low.form.getBoundingClientRect = () => ({ top: 150, left: 12, width: 540, height: 56 }); low.emoji.click();
    check('...in the least room the group names and the name line give way (gt-tight), and it is never lower than 96 px', cls(low.pick).indexOf('gt-tight') !== -1 && low.pick.style.height === '138px');
    low.esc(); low.form.getBoundingClientRect = () => ({ top: 20, left: 12, width: 540, height: 56 }); low.emoji.click();
    check('...(96 px at least)', low.pick.style.height === '96px');
    const caret = rig({ size: [390, 844] });
    caret.field.value = 'hi '; caret.caret(3); caret.field.focus();
    caret.emoji.fire('pointerdown', { preventDefault() {} }); caret.emoji.click();
    caret.field.selectionStart = 0; caret.field.selectionEnd = 0;       // (a press on an emoji takes the caret of a field that has no focus: Chrome puts it at the start)
    caret.keys()[0].click(); caret.field.selectionStart = 0; caret.field.selectionEnd = 0; caret.keys()[1].click();
    check('a field that has lost its focus to the sheet has lost its caret too (the browser puts it at the start): the picks go in where the caret was, one after the other', caret.field.value === 'hi ' + Pure.emoji()[0].list[0] + Pure.emoji()[0].list[1]);
    caret.esc(); caret.field.fire('focus', {}); caret.field.selectionStart = 1; caret.field.selectionEnd = 1; caret.emoji.click(); caret.field.selectionStart = 0; caret.field.selectionEnd = 0;
    check('(a caret that is kept is forgotten when the picker closes and the field is typed in again)', caret.field.value === 'hi ' + Pure.emoji()[0].list[0] + Pure.emoji()[0].list[1] && (caret.field.fire('focus', {}), caret.pick.hidden === true));
    const key = rig(); key.field.value = 'ab'; key.caret(1); key.field.focus(); key.emoji.click(); key.field.fire('blur', {});       // (the browser tells the field that the focus went to the search field)
    key.field.selectionStart = 0; key.field.selectionEnd = 0; key.keys()[0].click();
    check('a box opened by the keyboard moves the focus to the search field: the field\'s caret is kept as well (the press on an emoji may take it)', key.field.value === 'a' + Pure.emoji()[0].list[0] + 'b');
    const phone = rig({ size: [390, 844] });
    phone.field.focus();
    phone.emoji.fire('pointerdown', { preventDefault() {} }); phone.emoji.click();
    same('a phone upright: a sheet (gt-sheet) of 52 percent of the window\'s height, at the bottom; the page keeps that room above it (a class of the page and the height in a variable)', [cls(phone.pick).filter((c) => /^gt-(pop|low|sheet)$/.test(c)), phone.pick.style.height, phone.pick.style.bottom, html(phone)], [['gt-sheet'], '439px', '0px', [true, '439px']]);
    check('a press on the emoji button takes the keyboard away (the sheet is its place): the field has no focus, and keeps what it holds', phone.env.doc.activeElement === null && phone.pick.hidden === false);
    phone.emoji.click();
    check('(a second press closes it)', phone.pick.hidden === true && html(phone)[0] === false);
    phone.emoji.click();
    check('the sheet opens by the keyboard too: the focus goes to the search', phone.env.focused === phone.q && html(phone)[0] === true);
    phone.field.fire('focus', {});
    check('the keyboard coming for the field (a tap on it) closes the sheet and gives the page its room back', phone.pick.hidden === true && html(phone)[0] === false && html(phone)[1] === '');
    phone.emoji.click();
    phone.win.innerHeight = 380;
    (phone.win.listeners.resize || []).forEach((fn) => fn({}));
    check('a keyboard that takes the height of the window (no change of width) leaves the sheet a sheet', cls(phone.pick).indexOf('gt-sheet') !== -1 && cls(phone.pick).indexOf('gt-low') === -1 && phone.pick.style.height === '198px');
    phone.win.innerHeight = 844; phone.win.innerWidth = 844;
    (phone.win.listeners.resize || []).forEach((fn) => fn({}));
    check('...and a change of width (the phone is turned) asks again: now a low window\'s row (the box of a wide one is for a height over 500)', cls(phone.pick).indexOf('gt-pop') !== -1 && cls(phone.pick).indexOf('gt-sheet') === -1 && html(phone)[0] === false && phone.pick.style.height !== '');
    phone.esc();
    const vv = { height: 844, offsetTop: 0, handlers: {}, addEventListener(t, fn) { this.handlers[t] = fn; } };
    const kb = rig({ size: [390, 844], vv });
    kb.emoji.click();
    check('where the browser tells (visualViewport): without a keyboard the sheet is at the bottom', kb.pick.style.bottom === '0px' && kb.pick.style.height === '439px' && html(kb)[1] === '439px');
    vv.height = 500; vv.handlers.resize({});
    check('the keyboard is up: the sheet stands above it (344 px from the bottom) and is lower (275 px: 55 percent of what is left); the page keeps both (619 px)', kb.pick.style.bottom === '344px' && kb.pick.style.height === '275px' && html(kb)[1] === '619px');
    vv.height = 844; vv.handlers.scroll({});
    check('the keyboard goes: the sheet comes back down', kb.pick.style.bottom === '0px' && kb.pick.style.height === '439px' && html(kb)[1] === '439px');
    kb.esc(); vv.height = 500; vv.handlers.resize({});
    check('a closed picker does not follow the keyboard, and has left the page as it was', html(kb)[0] === false && html(kb)[1] === '' && kb.pick.hidden === true);
    const logged = rig({ size: [390, 844] });
    const log = logged.dock.root.querySelector('.gc-log');
    log.scrollHeight = 500; log.clientHeight = 100; log.scrollTop = 400;
    logged.emoji.click();
    check('the sheet takes room from the log: a log that is read at its end stays at its end', log.scrollTop === 500);
    log.scrollTop = 120; logged.esc();
    check('...and a log that is not at its end is left where it is, when the sheet goes', log.scrollTop === 120);
    log.scrollTop = 400; logged.emoji.click(); log.scrollTop = 400; logged.esc();
    check('...and at its end it stays there when the sheet gives the room back', log.scrollTop === 500);
    const mover = rig({ size: [1280, 800] }); mover.emoji.fire('pointerdown', { preventDefault() {} }); mover.field.focus(); mover.emoji.click();
    check('a box leaves the focus where it is (the keyboard of a window of a desktop: the person goes on typing)', mover.env.focused === mover.field);
    mover.win.innerWidth = 600; (mover.win.listeners.resize || []).forEach((fn) => fn({}));
    check('a window made narrow while it is open: the sheet', cls(mover.pick).indexOf('gt-sheet') !== -1 && html(mover)[0] === true);
    mover.field.fire('focus', {});
    check('a field that takes the focus while the box is open does not close a box (only a sheet gives way to the keyboard)', mover.pick.hidden === true);
}
{   // the line fills
    const r = rig();
    r.field.value = 'a'.repeat(999); r.caret(999);
    r.emoji.click();
    r.keys()[0].click();
    check('one more code point fits (999 + 1)', Pure.points(r.field.value) === 1000 && r.sayText() === '');
    r.keys()[1].click();
    check('the 1,001st is refused, the field is as it was, and the line is told: "The message is full."', Pure.points(r.field.value) === 1000 && r.field.value.slice(-1) !== Pure.emoji()[0].list[1] && r.sayText() === 'The message is full.');
    r.field.fire('input', {});
    check('typing clears that line', r.sayText() === '');
    r.keys()[1].click(); r.emoji.click(); r.emoji.click();
    check('opening the picker again clears it too', r.sayText() === '');
    const q = rig();
    q.field.value = ''; q.caret(0);
    for (let i = 0; i < 1000; i++) { if (i === 0) q.emoji.click(); q.keys()[i % 90].click(); }
    check('1,000 emoji beyond the basic plane (or not) fit: no more than 1,000 code points, and the field holds at most its maxlength of UTF-16 units', Pure.points(q.field.value) === 1000 && q.field.value.length <= Chat.LIMIT.field && q.field.maxLength === Chat.LIMIT.field);
    q.keys()[0].click();
    check('the 1,001st is refused', Pure.points(q.field.value) === 1000 && q.sayText() === 'The message is full.');
    q.dock.say(q.field.value);
    check('what the picker built goes out as one line that the dock accepts (not too long)', q.said().length === 1 && Pure.points(q.said()[0].text) === 1000);
}
{   // no selection API (a field that says nothing of its caret): the end
    const r = rig();
    r.field.value = 'x'; r.field.selectionStart = undefined; r.field.selectionEnd = undefined; r.field.setSelectionRange = undefined;
    r.emoji.click(); r.keys()[0].click();
    check('a field with no caret: the pick goes to the end', r.field.value === 'x' + GRIN);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. The voice
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({ lang: 'fr-CA' });
    r.field.value = 'say '; r.caret(4);
    r.mic.click();
    const rec = r.rec();
    check('a press makes one recognizer, in the page\'s language, with interim words, continuing, one alternative; it starts', r.recs.length === 1 && rec.lang === 'fr-CA' && rec.interimResults === true && rec.continuous === true && rec.maxAlternatives === 1 && rec.starts === 1);
    check('the first note says who hears you and asks to allow the microphone; the button is pressed', r.sayText() === 'Your browser\'s speech service hears you. Allow the microphone if it asks.' && r.mic.getAttribute('aria-pressed') === 'true' && cls(r.mic).indexOf('gt-hot') !== -1);
    rec.onstart({});
    check('listening: the words say so and how to stop', r.sayText() === 'Listening. Press the microphone again to stop. Then press Send.');
    r.heard('hel');
    check('interim words go into the field at the caret', r.field.value === 'say hel');
    r.heard('hello wor');
    check('...and are replaced, not added', r.field.value === 'say hello wor');
    r.heard('hello world', 'how are you');
    check('a second result is added after the first', r.field.value === 'say hello world how are you' && r.field.selectionStart === r.field.value.length);
    check('nothing was sent; the button still listens', r.said().length === 0 && r.mic.getAttribute('aria-pressed') === 'true');
    r.mic.click();
    check('a second press stops (the recognizer is told to stop, not to abort: the last words may still come); the button is not pressed any more', rec.stops === 1 && rec.aborts === 0 && r.mic.getAttribute('aria-pressed') === 'false' && cls(r.mic).indexOf('gt-hot') === -1);
    r.heard('hello world', 'how are you today');
    check('the final words still come in after the stop', r.field.value === 'say hello world how are you today');
    rec.onend({});
    check('the end: the words line is empty, the text stays, and nothing was sent', r.sayText() === '' && r.field.value === 'say hello world how are you today' && r.said().length === 0);
    rec.onresult({ results: [[{ transcript: 'late' }]] });
    check('a recognizer that speaks after its end is not heard', r.field.value === 'say hello world how are you today');
    r.mic.click();
    check('a new press makes a new recognizer', r.recs.length === 2 && r.rec().starts === 1);
    r.rec().onstart({}); r.heard('more'); r.mic.click(); r.rec().onend({});
    r.field.value = 'sent'; r.form.fire('submit', { preventDefault() {} });
    check('a send after a finished listening changes nothing', r.sayText() === '' && r.field.value === '' && r.said().length === 1 && r.recs.every((x) => x.aborts === 0));
}
{   // the words around the caret stay
    const r = rig();
    r.field.value = 'one three'; r.caret(3);
    const rec = r.start();
    r.heard('two');
    check('words at the caret of a line that goes on after it: spaces on both sides', r.field.value === 'one two three' && r.field.selectionStart === 7);
    r.heard('two', 'and more');
    check('...and the caret moves with them', r.field.value === 'one two and more three' && r.field.selectionStart === 'one two and more'.length);
    rec.onend({});
    r.field.value = 'abcdef'; r.caret(1, 4);
    const rec2 = r.start();
    r.heard('X');
    check('a selection is replaced by what is said', r.field.value === 'a X ef');
    rec2.onend({});
}
{   // ended by the person or the page
    const mk = () => { const r = rig(); r.field.value = 'a'; r.caret(1); const rec = r.start(); r.heard('b'); return [r, rec]; };
    let [r, rec] = mk();
    check('Escape stops (the event does not fold the dock), the words stay', r.esc() === true && rec.stops === 1 && rec.aborts === 0 && r.mic.getAttribute('aria-pressed') === 'false' && r.field.value === 'a b');
    rec.onend({});
    [r, rec] = mk();
    r.field.fire('input', {});
    check('typing ends it at once (the recognizer is aborted: what the person types is not written over); what was heard stays', rec.aborts === 1 && r.mic.getAttribute('aria-pressed') === 'false' && r.field.value === 'a b' && r.sayText() === '');
    rec.onresult({ results: [[{ transcript: 'ghost' }]] }); rec.onend({}); rec.onerror({ error: 'network' });
    check('...and what it says afterwards is not heard (no words, no error)', r.field.value === 'a b' && r.sayText() === '');
    [r, rec] = mk();
    r.dock.say('x'); r.field.value = ''; r.form.fire('submit', { preventDefault() {} });
    check('a send ends it at once', rec.aborts === 1 && r.mic.getAttribute('aria-pressed') === 'false');
    rec.onresult({ results: [[{ transcript: 'ghost' }]] });
    check('...and the field stays as the send left it', r.field.value === '');
    [r, rec] = mk();
    r.hide(true);
    check('a page that is hidden ends it', rec.aborts === 1 && r.mic.getAttribute('aria-pressed') === 'false');
    [r, rec] = mk();
    r.win.listeners.pagehide.forEach((fn) => fn({}));
    check('a page that goes away ends it', rec.aborts === 1);
    [r, rec] = mk();
    r.field.disabled = true; r.tell('live');
    check('a field that is switched off (the login ended) ends it and hides the buttons', rec.aborts === 1 && r.talk.root.hidden === true && r.mic.getAttribute('aria-pressed') === 'false');
    [r, rec] = mk();
    r.emoji.click();
    check('opening the picker while it listens: the listening goes on (it is a different button), the picker opens', r.pick.hidden === false && rec.aborts === 0 && rec.stops === 0);
    r.mic.click();
    check('...and the microphone press closes the picker and stops the listening', r.pick.hidden === true && rec.stops === 1);
}
{   // errors, in plain words
    const BLOCKED = 'The microphone is blocked for this site. Allow it in your browser\'s site settings, then try again.', NOSPEECH = 'No speech was heard. Press the microphone and speak.', NETWORK = 'The speech service could not be reached. Check your connection and try again.', POLICY = 'This page does not allow the microphone.', SERVICE = 'The browser\'s speech service is not available.';
    const cases = [['not-allowed', BLOCKED], ['service-not-allowed', SERVICE], ['no-speech', NOSPEECH], ['audio-capture', 'No microphone was found.'], ['network', NETWORK], ['language-not-supported', 'Voice to text did not work.'], ['bad-grammar', 'Voice to text did not work.']];
    for (const [code, words] of cases) {
        const r = rig(); const rec = r.start();
        rec.onerror({ error: code }); rec.onend({});
        check('error ' + code + ': "' + words + '", the button is idle', r.sayText() === words && r.mic.getAttribute('aria-pressed') === 'false' && r.said().length === 0, r.sayText());
    }
    const r = rig(); const rec = r.start();
    rec.onerror({}); rec.onend({});
    check('an error with no code is the general one', r.sayText() === 'Voice to text did not work.');
    r.mic.click();
    check('the next press clears the error (the first note shows)', r.sayText() === 'Your browser\'s speech service hears you. Allow the microphone if it asks.');
    const a = rig(); const ra = a.start();
    ra.onerror({ error: 'aborted' }); ra.onend({});
    check('"aborted" (a stop that the page made) is no error: only that nothing was heard', a.sayText() === NOSPEECH);
    const n = rig(); const rn = n.start(); rn.onend({});
    check('it ends with nothing heard and no error: "No speech was heard."', n.sayText() === NOSPEECH && n.field.value === '');
    const h = rig(); const rh = h.start(); h.heard('  '); rh.onend({});
    check('only blanks heard: the same', h.sayText() === NOSPEECH && h.field.value === '');
    const t = rig({ startThrows: true }); t.mic.click();
    check('a recognizer that cannot start: "Voice to text did not work.", idle, and the page is whole', t.sayText() === 'Voice to text did not work.' && t.mic.getAttribute('aria-pressed') === 'false');
    t.mic.click();
    check('...and a second press tries again (no stuck state)', t.recs.length === 2);
    const e1 = rig(); const re1 = e1.start(); re1.onerror({ error: 'not-allowed' });
    check('the refusal is said at once, when the error comes (a phone may end the listening late), as visible text', e1.sayText() === BLOCKED && e1.say.hidden === false && /^[\x20-\x7e]+$/.test(e1.sayText()));
    const pol = (allowed) => ({ allowsFeature: (f) => { pol.asked = f; return allowed; } });
    const p1 = rig(); p1.doc.permissionsPolicy = pol(false); p1.mic.click();
    check('the page\'s own policy says no: "This page does not allow the microphone." and no recognizer is made', p1.sayText() === POLICY && p1.recs.length === 0 && pol.asked === 'microphone' && p1.mic.getAttribute('aria-pressed') === 'false');
    const p2 = rig(); p2.doc.permissionsPolicy = pol(true); const rp2 = p2.start(); rp2.onerror({ error: 'not-allowed' }); rp2.onend({});
    check('the policy allows it: a refusal is the browser\'s ("blocked for this site")', p2.sayText() === BLOCKED && p2.recs.length === 1);
    const p3 = rig(); p3.doc.permissionsPolicy = pol(true); const rp3 = p3.start(); p3.doc.permissionsPolicy = pol(false); rp3.onerror({ error: 'not-allowed' }); rp3.onend({});
    check('a refusal while the policy says no: the page-policy sentence', p3.sayText() === POLICY);
    const p4 = rig(); p4.doc.featurePolicy = pol(false); p4.mic.click();
    check('the older name (featurePolicy) is read too', p4.sayText() === POLICY && p4.recs.length === 0);
    const p5 = rig(); p5.doc.permissionsPolicy = { allowsFeature() { throw new Error('no'); } }; const rp5 = p5.start();
    check('an API that throws, one that is no object, or one without allowsFeature never stops the button', p5.recs.length === 1 && rp5 !== null && (() => { const q = rig(); q.doc.permissionsPolicy = {}; q.mic.click(); const z = rig(); z.doc.permissionsPolicy = 5; z.mic.click(); return q.recs.length === 1 && z.recs.length === 1; })());
    same('policyBlocks: only an explicit false counts', [Pure.policyBlocks(null), Pure.policyBlocks({}), Pure.policyBlocks({ permissionsPolicy: pol(undefined) }), Pure.policyBlocks({ permissionsPolicy: pol(true) }), Pure.policyBlocks({ permissionsPolicy: pol(false) })], [false, false, false, false, true]);
    const w = rig(); const rw = w.start(); rw.onerror({ error: 'network' }); rw.onerror({ error: 'aborted' }); rw.onend({});
    check('a later "aborted" does not hide the real error', w.sayText() === NETWORK);
}
{   // untrusted words, a long speech
    const r = rig(); const rec = r.start();
    r.heard('<img src=x onerror=window.__hit=1> hi <script>');
    check('markup that is said is text in the field: no markup character is left (the hub refuses them), nothing was made from it', r.field.value.indexOf('<') === -1 && r.field.value.indexOf('>') === -1 && /img src=x onerror/.test(r.field.value) && r.env.innerHTMLWrites.length === 0 && !r.win.__hit);
    rec.onend({});
    const q = rig(); q.field.value = 'q'.repeat(990); q.caret(990);
    const rq = q.start();
    q.heard('word '.repeat(40));
    check('a long speech: the field never passes 1,000 code points, the recognizer is told to stop, and the line is full', Pure.points(q.field.value) <= 1000 && q.field.value.slice(0, 990) === 'q'.repeat(990) && rq.stops === 1 && q.sayText() === 'The message is full.' && q.mic.getAttribute('aria-pressed') === 'false');
    rq.onend({});
    check('...and "The message is full." stays after the end (it is the answer)', q.sayText() === 'The message is full.');
    const z = rig(); z.field.value = ''; const rz = z.start();
    z.heard(GRIN.repeat(1500));
    check('emoji that are said are kept whole at the cut', Pure.points(z.field.value) <= 1000 && !/[\ud800-\udbff]$/.test(z.field.value));
    const e = rig(); const re = e.start(); re.onresult({}); re.onresult({ results: [null, [], [{}], [{ transcript: 5 }]] });
    check('a result that is not what the standard says changes nothing', e.field.value === '');
}
{   // the field is no longer there to listen to
    const r = rig(); r.field.value = 'ab'; r.caret(2);
    r.mic.click(); r.rec().onstart({});
    r.field.disabled = true; r.tell('live');
    check('(a hub that welcomes with no name: nothing is left listening)', r.rec().aborts === 1);
}

{   // review round: the line of 1,000 code points, pairs of surrogates, the dock that is inert, a result that carries resultIndex, the focus
    const P = Pure;
    check('speak: nothing that fits adds no spaces and no code point (a line of 999 or 1,000 stays as it was; 500 + 499 around the words stay 999)', [[999, 0], [1000, 0], [500, 499], [0, 1000]].every(([a, b]) => { const r = P.speak('x'.repeat(a), 'hello there', 'y'.repeat(b)); return P.points(r.value) === a + b && r.caret === a; }));
    check('speak: every split of a line near the limit stays within 1,000 code points', (() => { for (let a = 0; a <= 1000; a += 37) for (let b = 0; a + b <= 1000; b += 41) if (P.points(P.speak('x'.repeat(a), 'one two three', 'y'.repeat(b)).value) > 1000) return false; return true; })());
    same('insert: a caret between the two halves of a pair goes before the pair; a selection that ends inside one takes it whole', [P.insert('a' + GRIN + 'b', 2, 2, 'Z'), P.insert('a' + GRIN + 'b', 0, 2, 'Z'), P.insert('a' + GRIN + 'b', 2, 3, 'Z')], [{ value: 'aZ' + GRIN + 'b', caret: 2 }, { value: 'Zb', caret: 1 }, { value: 'aZb', caret: 2 }]);
    const r = rig(); r.field.value = ''; r.caret(0);
    r.mic.click(); const rec = r.rec(); rec.onstart({});
    rec.onresult({ resultIndex: 1, results: [[{ transcript: 'one' }], [{ transcript: 'two' }]] });
    check('a result that carries resultIndex 1 (as a browser that continues does): all the results are used, nothing is dropped', r.field.value === 'one two');
    r.dock.root.setAttribute('inert', '');
    rec.onresult({ results: [[{ transcript: 'one two three' }]] });
    check('a dock that has become inert (the hand-off to the game, a question over the page): the voice stops writing and is off', r.field.value === 'one two' && rec.aborts === 1 && r.mic.getAttribute('aria-pressed') === 'false');
    const q = rig(); q.dock.root.setAttribute('inert', ''); q.mic.click(); q.rec().onstart({});
    check('...and a start under an inert dock says nothing of listening and is stopped', q.sayText() !== 'Listening. Press the microphone again to stop. Then press Send.' && q.rec().aborts === 1);
    const f = rig(); f.field.value = 'hi'; f.caret(2); f.field.focus();
    let stopped = 0;
    f.emoji.fire('pointerdown', { preventDefault() { stopped++; } }); f.emoji.click();
    check('a press on the emoji button (mouse or finger) keeps the focus in the field and opens the picker without moving the focus to a key', stopped === 1 && f.pick.hidden === false && f.env.focused === f.field);
    f.keys()[0].fire('pointerdown', { preventDefault() { stopped++; } }); f.keys()[0].click();
    check('a press on an emoji keeps the focus in the field too, and the pick is in', stopped === 2 && f.env.focused === f.field && f.field.value === 'hi' + Pure.emoji()[0].list[0]);
    f.esc(); f.emoji.click();
    check('the keyboard (a click with no pointer press) puts the focus in the search field', f.env.focused === f.q);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. The page
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const filesOf = (env, name) => env.body.children.filter((c) => (c.tagName === 'script' || c.tagName === 'link') && new RegExp('^home/' + name + '\\.(js|css)$').test(String(c.src || c.href || '')));
function hubFor(env) {
    const sockets = [];
    env.win.WebSocket = function (url) { this.url = url; this.sent = []; this.readyState = 0; this.close = () => { this.readyState = 3; }; this.send = (x) => { if (this.readyState !== 1) throw new Error('not open'); this.sent.push(x); }; sockets.push(this); };
    return sockets;
}
function bring(env, file, name) {
    env.win[name] = H.loadFile('home/' + file + '.js', env.scope)[name];
    filesOf(env, file).forEach((n) => { if (n.onload) n.onload(); });
}
{
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    const hub = hubFor(env);
    env.advance(0);
    check('before the dock is there the page asks for nothing of the buttons', filesOf(env, 'talk').length === 0);
    bring(env, 'chat', 'AntsHomeChat');
    env.advance(0);
    same('when the dock is mounted the page asks for the style and the script of the buttons, once, async', filesOf(env, 'talk').map((n) => n.tagName + ':' + (n.href || n.src) + ':' + (n.rel || '') + ':' + (n.async === true ? 'async' : '')), ['link:home/talk.css:stylesheet:', 'script:home/talk.js::async']);
    filesOf(env, 'talk')[1].onload();
    const form = env.$('home').children.find((c) => c.id === 'gchat').findAll((e) => cls(e).indexOf('gc-form') !== -1)[0];
    check('it mounts only when BOTH files are there (no unstyled button)', cls(form).indexOf('gt-on') === -1);
    env.win.isSecureContext = true; env.win.SpeechRecognition = function () { this.start = () => {}; };
    env.win.AntsHomeTalk = H.loadFile('home/talk.js', env.scope).AntsHomeTalk;
    filesOf(env, 'talk')[0].onload();
    check('...then the buttons stand in the dock\'s form, and the page\'s own parts are where they were', cls(form).indexOf('gt-on') !== -1 && byClass(form, 'gt-emoji').length === 1 && byClass(form, 'gt-mic').length === 1 && env.$('home').children.length === 4);
    env.advance(10);
    env.makeRoom(); env.toggleRoom();
    env.advance(10);
    check('Home shown again asks for nothing more', filesOf(env, 'talk').length === 2 && byClass(form, 'gt-emoji').length === 1);
}
{
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    hubFor(env); env.advance(0);
    check('no dock, no buttons: the page asked for the dock only', filesOf(env, 'talk').length === 0 && filesOf(env, 'chat').length === 2);
    const broken = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    hubFor(broken); broken.advance(0); bring(broken, 'chat', 'AntsHomeChat'); broken.advance(0);
    broken.win.AntsHomeTalk = { mount() { throw new Error('buttons that break'); } };
    filesOf(broken, 'talk').forEach((n) => { if (n.onload) n.onload(); });
    check('buttons that throw when they mount: Home goes on (the page still makes a room, the dock is there)', broken.$('home').children[3].id === 'gchat' && (broken.makeRoom(), broken.sockets.length === 1) && broken.screen() === 'room');
    const none = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    hubFor(none); none.advance(0); bring(none, 'chat', 'AntsHomeChat'); none.advance(0);
    filesOf(none, 'talk').forEach((n) => { if (n.onload) n.onload(); });
    check('a script that did not define them mounts nothing', none.win.AntsHomeTalk === undefined && none.$('home').children.length === 4);
    const late = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    hubFor(late); late.advance(0); bring(late, 'chat', 'AntsHomeChat'); late.advance(0);
    check('(a style that never comes: nothing is mounted)', filesOf(late, 'talk').length === 2 && late.$('home').children.length === 4);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 6. The source of the script
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const code = talkSource.split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n');
    check('no markup is made from text (no innerHTML, outerHTML, insertAdjacentHTML, document.write, eval)', !/innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function/.test(code));
    check('the only thing kept in the browser is the list of picks, in local storage, behind one guarded door (no session storage, cookie, IndexedDB or cache)', !/sessionStorage|indexedDB|document\.cookie|\bcaches\b/.test(code) && (code.match(/localStorage/g) || []).length === 1 && /function store\(\) \{ try \{ return win\.localStorage \|\| null; \} catch/.test(code));
    check('no network of its own (no fetch, XMLHttpRequest, WebSocket, sendBeacon, no address)', !/\bfetch\b|XMLHttpRequest|WebSocket|sendBeacon|https?:|\/\/[a-z]/.test(code));
    check('NO recording of any kind: no MediaRecorder, no getUserMedia, no AudioContext, no Blob', !/MediaRecorder|getUserMedia|mediaDevices|AudioContext|\bBlob\b|FileReader|createObjectURL/.test(code));
    check('the speech constructor is named only where the support is looked up', (code.match(/SpeechRecognition/g) || []).length === 2);
    check('it speaks to the dock through its field and taps only (no socket of its own)', !/dock\.(send|say|state)\b/.test(code) && /dock\.tap\(/.test(code));
    check('words from outside are put in a field\'s value or a node\'s textContent', /field\.value = /.test(code) && /textContent = text/.test(code));
}

{   // the line of words is a bubble: announced, it goes by itself after 6 s, and at once on the next action; the listening line stays while it listens
    const nothing = () => { const r = rig(); const rec = r.start(); rec.onend({}); return r; };
    const r = nothing();
    check('the voice line is shown in the polite status line (a screen reader says it)', r.sayText() === Pure.TEXT.nothing && r.say.getAttribute('role') === 'status' && r.say.getAttribute('aria-live') === 'polite');
    r.clock.advance(5900);
    check('... it is still there before 6 seconds', r.sayText() === Pure.TEXT.nothing);
    r.clock.advance(200);
    check('... and gone after them (hidden, empty)', r.sayText() === '' && r.say.hidden === true);
    const a = rig(); const rec = a.start();
    check('while the microphone listens the line says so', a.sayText() === Pure.TEXT.listening);
    a.clock.advance(120000);
    check('... and it stays as long as it listens', a.sayText() === Pure.TEXT.listening && a.say.hidden === false);
    rec.onerror({ error: 'not-allowed' });
    check('an error line shows at once', a.sayText() !== '' && a.sayText() !== Pure.TEXT.listening);
    a.clock.advance(6001);
    check('... and goes after 6 seconds too', a.sayText() === '');
    const t = nothing(); t.field.fire('input', {});
    check('typing takes it away at once', t.sayText() === '');
    const e = nothing(); e.emoji.click();
    check('opening the picker takes it away at once', e.sayText() === '');
    const m = nothing(); m.start();
    check('the next press of the microphone replaces it at once', m.sayText() === Pure.TEXT.listening);
    const s = nothing(); s.field.value = 'hi'; s.form.fire('submit', { preventDefault() {} });
    check('a send takes it away at once', s.sayText() === '');
    const two = rig(); let rec1 = two.start(); rec1.onend({});
    two.clock.advance(4000);
    rec1 = two.start(); rec1.onend({});
    two.clock.advance(4000);
    check('a second line has its own 6 seconds (the first one\'s timer does not take it away)', two.sayText() === Pure.TEXT.nothing);
    two.clock.advance(2100);
    check('... and then goes', two.sayText() === '');
}
{   // the line before the microphone starts stays until the state changes, like "Listening"; Enter ends the listening and closes the picker where the form cannot requestSubmit
    const r = rig();
    r.mic.click();
    check('the line "asking" (before the browser starts to listen) is shown', r.sayText() === Pure.TEXT.asking);
    r.clock.advance(120000);
    check('... and it stays until the browser says something (it is not timed)', r.sayText() === Pure.TEXT.asking);
    const q = rig();
    q.form.dispatchEvent = (ev) => q.form.fire(ev.type, ev);
    q.win.Event = function (type, o) { this.type = type; this.cancelable = !!(o && o.cancelable); this.preventDefault = () => {}; };
    const rec = q.start(); q.heard('half a line');
    q.emoji.click();
    q.field.fire('keydown', { key: 'Enter', preventDefault() {} });
    check('Enter in a form without requestSubmit (iOS 15) goes through a submit event: the listening ends and the picker closes, as for Send', rec.aborts === 1 && q.pick.hidden === true && q.said().length === 1);
}
{   // the field is a text area that the chat makes as tall as its words: after a pick and after the words that are heard
    const r = rig();
    Object.defineProperty(r.field, 'scrollHeight', { get() { return 22 + 22 * Math.max(1, Math.ceil(this.value.length / 20)); } });
    r.field.value = 'x'.repeat(19); r.caret(19);
    r.emoji.click(); r.keys()[0].click();
    check('an emoji that makes the line longer grows the box', r.field.style.height === '66px');
    r.field.value = ''; r.caret(0);
    const rec = r.start(); r.heard('word word word word word word word word word word');
    check('words that are heard grow the box', r.field.style.height === '88px');
    rec.onend({});
}
{   // the box is one row until the words need a second line; the rule is the one-row (narrow) field's, so a line between the two widths does not flip on each key
    const r = rig();
    const tall = () => cls(r.form).indexOf('gt-tall') !== -1;
    // the fake field wraps at 25 letters in the one-row box and at 40 in the two-row box (its wider field)
    Object.defineProperty(r.field, 'scrollHeight', { get() { const per = tall() ? 40 : 25; return 22 + 22 * Math.max(1, Math.ceil(this.value.length / per)); }, configurable: true });
    const typed = (v) => { r.field.value = v; r.field.fire('input', {}); return { tall: tall(), h: r.field.style.height }; };
    r.dock.grow();
    same('empty: one row, 44 px', [tall(), r.field.style.height], [false, '44px']);
    same('a short line: one row', typed('hello'), { tall: false, h: '44px' });
    same('exactly one line in the one-row field: still one row', typed('x'.repeat(25)), { tall: false, h: '44px' });
    same('a line that needs a second line in the one-row field: two rows (the wider field makes it one line high)', typed('x'.repeat(26)), { tall: true, h: '44px' });
    const seen = []; for (let i = 0; i < 4; i++) { r.dock.grow(); seen.push(tall()); }
    same('a line that fits only in the wide field stays two rows on every measure and key (no flip-flop)', seen, [true, true, true, true]);
    same('...and on each letter around the boundary', [typed('x'.repeat(30)).tall, typed('x'.repeat(31)).tall, typed('x'.repeat(30)).tall, typed('x'.repeat(40)).tall], [true, true, true, true]);
    same('a long line: two rows, as high as its lines in the wide field', typed('x'.repeat(100)), { tall: true, h: '88px' });
    same('the words are deleted back to one narrow line: one row again', typed('x'.repeat(25)), { tall: false, h: '44px' });
    typed('x'.repeat(100)); r.field.value = 'x'.repeat(100);
    r.form.fire('submit', { preventDefault() {} }); r.dock.grow();
    same('Send clears the field: one row again', [r.field.value, tall(), r.field.style.height], ['', false, '44px']);
    Object.defineProperty(r.field, 'scrollHeight', { get() { return this.value === '' ? 66 : 88; }, configurable: true });       // (a hint that wraps in the narrow field: an empty box is one row all the same)
    r.field.value = ''; r.dock.grow();
    check('an empty field whose hint wraps is still one row', !tall());
    r.field.disabled = true; r.field.value = 'x'.repeat(100); r.dock.grow();
    check('a field that is off stays one row', !tall());
}

{   // the room's chat box (web/front/lobby_chat.js) mounts the same script with no emoji button and a limit of 100: its line is printable ASCII, and at most 100 characters
    same('insert keeps to the limit that it is given (3 here), and to 1,000 where none is', [Pure.insert('ab', 2, 2, 'c', 3).value, Pure.insert('abc', 3, 3, 'd', 3), Pure.insert('a'.repeat(1000), 1000, 1000, 'b'), Pure.insert('a'.repeat(100), 100, 100, 'b').value.length], ['abc', null, null, 101]);
    same('speak keeps to the limit that it is given: what is heard is cut to what is left of it (less two)', [Pure.speak('', 'one two three four', '', 10).value, Pure.speak('', 'x'.repeat(50), '', 100).value.length], ['one two', 50]);
    const room = rig({ emoji: false, limit: 100 });
    check('no emoji button and no picker: the microphone and nothing else stand beside Send', room.emoji === null && room.pick === null && !!room.mic && room.form.children.filter((c) => (c.className || '').indexOf('gt-pick') !== -1).length === 0);
    room.field.value = '';
    room.start();
    room.heard('word '.repeat(40));
    check('what is heard never makes the line longer than 100 characters', room.field.value.length <= 100 && room.field.value.length > 50, String(room.field.value.length));
    let off = true;
    room.dock.off = () => off;
    room.tell('x');
    const tools = room.form.children.filter((c) => (c.className || '').indexOf('gt-tools') !== -1)[0];
    check('a dock that says it is off (the room, while the link is down) hides the buttons, and shows them again when it is not', tools.hidden === true && (off = false, room.tell('x'), tools.hidden === false));
    const full = rig({});
    full.start();
    full.heard('word '.repeat(300));
    check('(the front page\'s own box keeps its 1,000)', full.field.value.length > 950 && full.field.value.length <= 1000, String(full.field.value.length));
}

console.log(failures === 0 ? 'OK: ' + checks + ' checks' : failures + ' of ' + checks + ' checks FAILED');
process.exit(failures === 0 ? 0 : 1);
