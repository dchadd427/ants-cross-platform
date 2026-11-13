// Runs the emoji picker and the voice button of the chat (web/home/talk.js, docs/HOME_SCREEN.md "Emoji and voice") against the REAL chat dock (web/home/chat.js, mounted on a fake hub) and a FAKE speech recognition
// (no microphone: the browser's constructor is a class of this file that the test drives by hand), in the fake browser of tests/scripts/web_page_harness.js. What it checks:
//   1. the pure parts: the emoji (one code point each, ASCII source, groups), the code point count against the field's UTF-16 maxlength, the insert at the caret, the words that are said (spacing, no markup characters, the cut),
//      and when the browser has speech recognition at all (a secure page and a constructor, with or without the prefix);
//   2. the buttons: where they stand in the dock's form, their labels and the privacy words in the tooltip, hidden with the field when nobody may write, and NO microphone button with no support or on a page that is not secure;
//   3. the picker: opens and closes (the button, Escape with the focus back in the field, a press outside, a send), a pick goes in at the caret and replaces a selection, the arrow keys, a full line is said and refused,
//      Escape does not fold the dock;
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
const GRIN = String.fromCodePoint(0x1F600), ANT = String.fromCodePoint(0x1F41C), STAR = String.fromCodePoint(0x2728);       // (built from code points: the sources are ASCII)

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The pure parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Pure;
    const groups = P.emoji(), all = [].concat(...groups.map((g) => g.list));
    same('the groups, in order', groups.map((g) => g.name), ['Faces', 'Hands', 'Hearts and signs', 'Ants and nature', 'Games']);
    check('about a hundred emoji, none twice', all.length >= 60 && all.length <= 150 && new Set(all).size === all.length, all.length);
    check('every emoji is ONE code point (no joiner, no variation selector, no tag character: the hub keeps it as it is)', all.every((e) => P.points(e) === 1 && [...e].length === 1));
    check('...and the dock\'s own cleaner (the hub\'s rule) leaves each one as it is', all.every((e) => Chat.plain(e) === e) && Chat.plain(all.join('')) === all.join(''));
    check('...and none is a markup character or a blank', all.every((e) => !/[<>\s]/.test(e)));
    check('the ant is there', all.indexOf(ANT) !== -1 && all.indexOf(GRIN) !== -1 && all.indexOf(STAR) !== -1);
    check('talk.js is ASCII only', /^[\x00-\x7f]*$/.test(talkSource));
    same('chr: a code point of the basic plane and one beyond it', [Pure.emoji()[0].list[0], STAR], [GRIN, STAR]);
    same('points counts code points, not UTF-16 units', [P.points(''), P.points('abc'), P.points(GRIN), P.points(GRIN + 'a' + STAR), P.points('\ud83d')], [0, 3, 1, 3, 1]);
    same('head: a pair of surrogates is never cut', [P.head('ab' + GRIN + 'c', 2), P.head('ab' + GRIN + 'c', 3), P.head('ab' + GRIN + 'c', 9), P.head(GRIN, 0)], ['ab', 'ab' + GRIN, 'ab' + GRIN + 'c', '']);
    same('insert: at the caret, after it, and in place of a selection', [P.insert('hello', 2, 2, GRIN), P.insert('hello', 5, 5, GRIN), P.insert('hello', 1, 4, STAR), P.insert('', 0, 0, ANT)],
        [{ value: 'he' + GRIN + 'llo', caret: 4 }, { value: 'hello' + GRIN, caret: 7 }, { value: 'h' + STAR + 'o', caret: 2 }, { value: ANT, caret: 2 }]);
    same('insert: a caret that is not a number is the end', [P.insert('ab', undefined, undefined, GRIN), P.insert('ab', 9, 9, GRIN), P.insert('ab', 1, 0, GRIN)], [{ value: 'ab' + GRIN, caret: 4 }, { value: 'ab' + GRIN, caret: 4 }, { value: 'a' + GRIN + 'b', caret: 3 }]);
    check('insert: 200 code points fit, the 201st is refused', P.insert('a'.repeat(199), 199, 199, GRIN) !== null && P.insert('a'.repeat(200), 200, 200, GRIN) === null && P.insert('a'.repeat(200), 0, 1, GRIN) !== null);
    check('insert: 200 emoji outside the basic plane are 400 UTF-16 units (the field\'s maxlength) and fit; one more does not', (() => { let v = ''; for (let i = 0; i < 200; i++) { const r = P.insert(v, v.length, v.length, GRIN); if (!r) return false; v = r.value; } return v.length === Chat.LIMIT.field && P.insert(v, v.length, v.length, GRIN) === null; })());
    check('insert: the units never pass the maxlength where the code points fit (the worst is 2 units for 1 point)', 2 * Pure.LIMIT <= Chat.LIMIT.field && Pure.LIMIT === Chat.LIMIT.line);
    same('speak: the words go in at the caret with one space on each side', [P.speak('hello', 'big world', ''), P.speak('hello ', 'big', ''), P.speak('a', 'b', 'c'), P.speak('', 'hi', ''), P.speak('x ', 'y', ' z')],
        [{ value: 'hello big world', caret: 15 }, { value: 'hello big', caret: 9 }, { value: 'a b c', caret: 3 }, { value: 'hi', caret: 2 }, { value: 'x y z', caret: 3 }]);
    same('speak: nothing said leaves the line as it was; blanks collapse; markup characters go', [P.speak('a', '', 'b'), P.speak('a', ' \n ', 'b'), P.speak('', '  one   two ', ''), P.speak('', '<b>hi</b>', '')], [{ value: 'ab', caret: 1 }, { value: 'ab', caret: 1 }, { value: 'one two', caret: 7 }, { value: 'b hi /b', caret: 7 }]);
    check('speak: what is said is cut to what is left of the 200 code points, around the words that stay', (() => { const r = P.speak('a'.repeat(150), 'w '.repeat(100), 'z'.repeat(20)); return P.points(r.value) <= 200 && r.value.indexOf('z'.repeat(20)) !== -1 && r.value.slice(0, 150) === 'a'.repeat(150); })());
    check('speak: a pair of surrogates at the cut is whole', (() => { const r = P.speak('', GRIN.repeat(300), ''); return P.points(r.value) <= 200 && !/[\ud800-\udbff]$/.test(r.value) && r.value.length % 2 === 0; })());
    const W = (o) => Object.assign({}, o);
    class Ctor {}
    same('recognition: a secure page and a constructor', [P.recognition(W({ isSecureContext: true, SpeechRecognition: Ctor })) === Ctor, P.recognition(W({ isSecureContext: true, webkitSpeechRecognition: Ctor })) === Ctor], [true, true]);
    same('recognition: none without a constructor, on a page that is not secure (false, missing) or with something that is no constructor', [P.recognition(W({ isSecureContext: true })), P.recognition(W({ isSecureContext: false, SpeechRecognition: Ctor })), P.recognition(W({ SpeechRecognition: Ctor })),
        P.recognition(W({ isSecureContext: true, SpeechRecognition: {} })), P.recognition(null), P.recognition(W({ isSecureContext: 'yes', SpeechRecognition: Ctor }))], [null, null, null, null, null, null]);
    same('the words: the tooltip says who hears you', Pure.TEXT.voice, 'Voice to text: your browser\'s speech service hears you');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the fakes: the real dock on a fake hub, a fake speech recognition, a fake page
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function loadScript(source, exportName, clock) {
    const root = {};
    new Function('self', 'setTimeout', 'clearTimeout', source)(root, clock.setTimeout, clock.clearTimeout);
    return root[exportName];
}
// o.speech: false (none), 'plain' / 'webkit' (the constructor's name), o.secure: false (a page that is not secure), o.lang, o.startThrows, o.noLogin (the dock's field is off: nobody may write)
function rig(o) {
    o = o || {};
    const env = { elements: {}, innerHTMLWrites: [] };
    const clock = { setTimeout: (fn, ms) => 0, clearTimeout() {} };
    const ChatApi = loadScript(fs.readFileSync(path.join(lobbyDir, 'home', 'chat.js'), 'utf8'), 'AntsHomeChat', clock);
    const doc = { hidden: false, listeners: {}, createElement: (t) => new El(env, t), addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); } };
    env.doc = doc;
    const win = { innerWidth: 1280, innerHeight: 800, location: { protocol: 'https:', host: 'play.test' }, listeners: {}, addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); }, isSecureContext: o.secure !== false, navigator: { language: o.lang || 'de-DE' } };
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
    const talk = Talk.mount({ doc, win, dock });
    const r = { env, doc, win, host, dock, talk, recs, sockets, clock: tclock, tell: (kind) => taps.forEach((fn) => fn(kind)) };
    r.field = dock.field;
    r.form = dock.field.parent;
    r.emoji = one(r.form, 'gt-emoji');
    r.mic = one(r.form, 'gt-mic');
    r.pick = one(r.form, 'gt-pick');
    r.say = one(r.form, 'gt-say');
    r.keys = () => byClass(r.pick, 'gt-e');
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
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. The picker
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig();
    check('the picker holds buttons with one emoji each, in groups that are named, and every button is text only', r.keys().length === Pure.emoji().reduce((n, g) => n + g.list.length, 0) && r.keys().every((b) => b.tagName === 'button' && b.type === 'button' && P(b.textContent) === 1) && byClass(r.pick, 'gt-row').length === 5 && byClass(r.pick, 'gt-row').every((g) => g.getAttribute('role') === 'group' && !!g.getAttribute('aria-label')));
    function P(s) { return Pure.points(s); }
    check('only one button is in the tab order (the arrow keys move)', r.keys().filter((b) => b.tabIndex === 0).length === 0 || r.keys().filter((b) => b.tabIndex === 0).length === 1);
    r.field.value = 'hi '; r.caret(3);
    r.emoji.click();
    check('the emoji button opens the picker: shown, expanded, and the focus is on an emoji', r.pick.hidden === false && r.emoji.getAttribute('aria-expanded') === 'true' && r.env.focused === r.keys()[0] && r.keys()[0].tabIndex === 0 && r.keys().filter((b) => b.tabIndex === 0).length === 1);
    r.pick.fire('keydown', { key: 'ArrowRight', preventDefault() {} });
    check('the right arrow moves to the next one (and only that one is in the tab order)', r.env.focused === r.keys()[1] && r.keys()[1].tabIndex === 0 && r.keys()[0].tabIndex === -1);
    r.pick.fire('keydown', { key: 'ArrowRight', preventDefault() {} });
    r.pick.fire('keydown', { key: 'ArrowLeft', preventDefault() {} });
    r.pick.fire('keydown', { key: 'ArrowDown', preventDefault() {} });
    r.pick.fire('keydown', { key: 'ArrowUp', preventDefault() {} });
    check('left, right, down and up: one step each', r.env.focused === r.keys()[1]);
    r.pick.fire('keydown', { key: 'End', preventDefault() {} });
    check('End goes to the last, and no further', r.env.focused === r.keys()[r.keys().length - 1]);
    r.pick.fire('keydown', { key: 'ArrowRight', preventDefault() {} });
    check('...not past it', r.env.focused === r.keys()[r.keys().length - 1]);
    r.pick.fire('keydown', { key: 'Home', preventDefault() {} });
    r.pick.fire('keydown', { key: 'ArrowLeft', preventDefault() {} });
    check('Home goes to the first, and no further back', r.env.focused === r.keys()[0]);
    let prevented = 0;
    r.pick.fire('keydown', { key: 'a', preventDefault() { prevented++; } });
    r.pick.fire('keydown', { key: 'Tab', preventDefault() { prevented++; } });
    r.pick.fire('keydown', {});
    check('other keys (Tab among them) are left alone', prevented === 0 && r.env.focused === r.keys()[0]);
    r.keys()[0].click();
    check('a pick goes in at the caret, and the caret is after it', r.field.value === 'hi ' + GRIN && r.field.selectionStart === 3 + 2 && r.field.selectionEnd === 5);
    check('the picker stays open and the dock says nothing (it is not sent)', r.pick.hidden === false && r.said().length === 0 && r.sayText() === '');
    r.caret(0); r.keys()[1].click();
    check('the next one goes in where the caret is now', r.field.value === Pure.emoji()[0].list[1] + 'hi ' + GRIN);
    r.caret(0, 2); r.keys().find((b) => b.textContent === ANT).click();
    check('a selection is replaced', r.field.value === ANT + 'hi ' + GRIN);
    check('the field keeps what is not an emoji, the input is plain text (no markup)', r.env.innerHTMLWrites.length === 0);
    check('Escape closes the picker, the focus is back in the field, and the dock stays open (the event does not reach it)', (() => { const stopped = r.esc(); return stopped && r.pick.hidden === true && r.emoji.getAttribute('aria-expanded') === 'false' && r.env.focused === r.field && r.dock.root.getAttribute('data-open') !== 'false'; })());
    check('...(and Escape with nothing open is left to the dock)', r.esc() === false);
    r.emoji.click(); r.emoji.click();
    check('the button again closes it, the focus goes to the field', r.pick.hidden === true && r.env.focused === r.field);
    r.emoji.click();
    r.doc.listeners.pointerdown.forEach((fn) => fn({ target: r.field }));
    check('a press inside the form (the field) keeps it open', r.pick.hidden === false);
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
}
{   // the line fills
    const r = rig();
    r.field.value = 'a'.repeat(199); r.caret(199);
    r.emoji.click();
    r.keys()[0].click();
    check('one more code point fits (199 + 1)', Pure.points(r.field.value) === 200 && r.sayText() === '');
    r.keys()[1].click();
    check('the 201st is refused, the field is as it was, and the line is told: "The message is full."', Pure.points(r.field.value) === 200 && r.field.value.slice(-1) !== Pure.emoji()[0].list[1] && r.sayText() === 'The message is full.');
    r.field.fire('input', {});
    check('typing clears that line', r.sayText() === '');
    r.keys()[1].click(); r.emoji.click(); r.emoji.click();
    check('opening the picker again clears it too', r.sayText() === '');
    const q = rig();
    q.field.value = ''; q.caret(0);
    for (let i = 0; i < 200; i++) { if (i === 0) q.emoji.click(); q.keys()[i % 90].click(); }
    check('200 emoji beyond the basic plane (or not) fit: no more than 200 code points, and the field holds at most its maxlength of UTF-16 units', Pure.points(q.field.value) === 200 && q.field.value.length <= Chat.LIMIT.field && q.field.maxLength === Chat.LIMIT.field);
    q.keys()[0].click();
    check('the 201st is refused', Pure.points(q.field.value) === 200 && q.sayText() === 'The message is full.');
    q.dock.say(q.field.value);
    check('what the picker built goes out as one line that the dock accepts (not too long)', q.said().length === 1 && Pure.points(q.said()[0].text) === 200);
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
    const q = rig(); q.field.value = 'q'.repeat(190); q.caret(190);
    const rq = q.start();
    q.heard('word '.repeat(40));
    check('a long speech: the field never passes 200 code points, the recognizer is told to stop, and the line is full', Pure.points(q.field.value) <= 200 && q.field.value.slice(0, 190) === 'q'.repeat(190) && rq.stops === 1 && q.sayText() === 'The message is full.' && q.mic.getAttribute('aria-pressed') === 'false');
    rq.onend({});
    check('...and "The message is full." stays after the end (it is the answer)', q.sayText() === 'The message is full.');
    const z = rig(); z.field.value = ''; const rz = z.start();
    z.heard(GRIN.repeat(300));
    check('emoji that are said are kept whole at the cut', Pure.points(z.field.value) <= 200 && !/[\ud800-\udbff]$/.test(z.field.value));
    const e = rig(); const re = e.start(); re.onresult({}); re.onresult({ results: [null, [], [{}], [{ transcript: 5 }]] });
    check('a result that is not what the standard says changes nothing', e.field.value === '');
}
{   // the field is no longer there to listen to
    const r = rig(); r.field.value = 'ab'; r.caret(2);
    r.mic.click(); r.rec().onstart({});
    r.field.disabled = true; r.tell('live');
    check('(a hub that welcomes with no name: nothing is left listening)', r.rec().aborts === 1);
}

{   // review round: the line of 200 code points, pairs of surrogates, the dock that is inert, a result that carries resultIndex, the focus
    const P = Pure;
    check('speak: nothing that fits adds no spaces and no code point (a line of 199 or 200 stays as it was; 100 + 99 around the words stay 199)', [[199, 0], [200, 0], [100, 99], [0, 200]].every(([a, b]) => { const r = P.speak('x'.repeat(a), 'hello there', 'y'.repeat(b)); return P.points(r.value) === a + b && r.caret === a; }));
    check('speak: every split of a line near the limit stays within 200 code points', (() => { for (let a = 0; a <= 200; a += 7) for (let b = 0; a + b <= 200; b += 11) if (P.points(P.speak('x'.repeat(a), 'one two three', 'y'.repeat(b)).value) > 200) return false; return true; })());
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
    check('the keyboard (a click with no pointer press) still puts the focus on an emoji', f.env.focused === f.keys()[0]);
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
    check('...then the buttons stand in the dock\'s form, and the page\'s own parts are where they were', cls(form).indexOf('gt-on') !== -1 && byClass(form, 'gt-emoji').length === 1 && byClass(form, 'gt-mic').length === 1 && env.$('home').children.length === 5);
    env.advance(10);
    env.home('mine').click(); env.roomBar('rhome').click();
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
    check('buttons that throw when they mount: Home goes on (My room still makes the room, the dock is there)', broken.$('home').children[4].id === 'gchat' && (broken.home('mine').click(), broken.sockets.length === 1) && broken.screen() === 'room');
    const none = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    hubFor(none); none.advance(0); bring(none, 'chat', 'AntsHomeChat'); none.advance(0);
    filesOf(none, 'talk').forEach((n) => { if (n.onload) n.onload(); });
    check('a script that did not define them mounts nothing', none.win.AntsHomeTalk === undefined && none.$('home').children.length === 5);
    const late = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    hubFor(late); late.advance(0); bring(late, 'chat', 'AntsHomeChat'); late.advance(0);
    check('(a style that never comes: nothing is mounted)', filesOf(late, 'talk').length === 2 && late.$('home').children.length === 5);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 6. The source of the script
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const code = talkSource.split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n');
    check('no markup is made from text (no innerHTML, outerHTML, insertAdjacentHTML, document.write, eval)', !/innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function/.test(code));
    check('nothing is kept in the browser (no storage, cookie or IndexedDB)', !/localStorage|sessionStorage|indexedDB|document\.cookie|\bcaches\b/.test(code));
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
{   // the field is a text area that the chat makes as tall as its words: after a pick and after the words that are heard
    const r = rig();
    r.win.matchMedia = () => ({ matches: true });
    Object.defineProperty(r.field, 'scrollHeight', { get() { return 22 + 22 * Math.max(1, Math.ceil(this.value.length / 20)); } });
    r.field.value = 'x'.repeat(19); r.caret(19);
    r.emoji.click(); r.keys()[0].click();
    check('an emoji that makes the line longer grows the box', r.field.style.height === '66px');
    r.field.value = ''; r.caret(0);
    const rec = r.start(); r.heard('word word word word word word word word word word');
    check('words that are heard grow the box', r.field.style.height === '88px');
    rec.onend({});
}

console.log(failures === 0 ? 'OK: ' + checks + ' checks' : failures + ' of ' + checks + ' checks FAILED');
process.exit(failures === 0 ? 0 : 1);
