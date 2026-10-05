// Runs the pages' OWN code for the player's name (the owner: "the ability for somebody to type in their name ... so their name goes into the game instead of random", and "when joining a
// link from somebody else, it should ask you first what you want your name to be"):
//   - the rules of a name (the block NAME_BEGIN .. NAME_END, the same text in web/lobby.html and web/shell.html) on a table of names: the rules of the desktop start menu
//     (printable ASCII, trimmed, at most 32, nothing that starts with "Bot (" with blanks and case ignored; an empty name means "none chosen");
//   - the name step (runNameStep): filled in from what the browser remembered, a bad name explained and not accepted, the button or Enter accepts once, the name is remembered;
//   - web/shell.html: which addresses ask for a name (a shared link: ANTS_PAGE.asksForName), the address without its name (withoutName), and the gate that holds the game back until the name
//     is chosen (makeNameGate): the game is not started, so it does not connect, before the button; the chosen name goes into the game's arguments as --name;
//   - web/lobby.html (the front page) as a whole, with a small fake of the browser's DOM: a first visit (Treasure, Medium opponents in all three bases, 2 players on the Host card), the card that plays against the computer (a group of four buttons for each base and the Teams; START takes this tab to the game page of this computer),
//     the Host card (players 2 - 4, the empty seats; an old stored 1 player is 2), the host's own seat in this tab, and the field that every button shares is remembered and filled in, its name goes to the seat that this person plays
//     (the first seat of the page) and the other seats of the page keep random names, a link made for somebody else carries none, bad names start nothing, a name with < > & is only
//     ever text (the page writes no markup at all), an empty field falls back to a random name (and to Player for a join), a shared link of the page asks first and starts nothing before.
//     the Rejoin button at the top of the front page as a whole (only on the plain page, with a fresh entry of this site: its words, its address, no key anywhere; old entries removed, other servers' left alone;
//     the page's return from memory and the storage events), the block's own rules being tests/scripts/web_rejoin_block_check.js.
// tests/scripts/test_web_name.py runs this with node (the quick tier). usage: node web_name_check.js web/shell.html web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const nodeCrypto = require('crypto');

const shellPath = process.argv[2];
const lobbyPath = process.argv[3];
if (!shellPath || !lobbyPath) { console.log('usage: web_name_check.js shell.html lobby.html'); process.exit(2); }

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

// The lines between the line of `begin` and the line of `end`
function between(text, begin, end, path) {
    const at = text.indexOf(begin);
    if (at < 0) throw new Error(path + ': the marker ' + begin + ' is missing');
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) throw new Error(path + ': the marker ' + end + ' is missing');
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
}
const shellText = fs.readFileSync(shellPath, 'utf8');
const lobbyText = fs.readFileSync(lobbyPath, 'utf8');

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The rules of a name: the same text in both pages, on a table
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const nameBlockShell = between(shellText, 'NAME_BEGIN', 'NAME_END', shellPath);
const nameBlockLobby = between(lobbyText, 'NAME_BEGIN', 'NAME_END', lobbyPath);
check('the rules and the step of a name are the same text in both pages', nameBlockShell === nameBlockLobby);
const names = new Function(nameBlockLobby + '\nreturn { nameCheck: nameCheck, runNameStep: runNameStep, KEY: NAME_KEY, MAX: NAME_MAX };')();
const nameCheck = names.nameCheck;
check('the name is remembered under ants.name', names.KEY === 'ants.name');
check('a name has at most 32 characters', names.MAX === 32);

const NAME_TABLE = [
    // [what is typed, ok, the name that comes back]
    ['', true, ''], ['   ', true, ''], ['Alice', true, 'Alice'], ['  Alice  ', true, 'Alice'], ['Mary Ann', true, 'Mary Ann'], ['a', true, 'a'],
    ['a'.repeat(32), true, 'a'.repeat(32)], [' ' + 'a'.repeat(32) + ' ', true, 'a'.repeat(32)], ['a'.repeat(33), false],
    ['x'.repeat(100), false],
    ['Bot (Medium)', false], ['bot (x)', false], ['BOT(HARD)', false], ['  Bot (', false], ['B o t (', false], ['b  o  t  (', false], ['bot(x', false],
    ['Bot', true, 'Bot'], ['Botany', true, 'Botany'], ['Bot Bob', true, 'Bot Bob'], ['Robot (x)', true, 'Robot (x)'], ['bot )', true, 'bot )'], ['The Bot (x)', true, 'The Bot (x)'],
    ['Zoë', false], ['名前', false], ['café', false], ['🐜', false], [' Bob', false], ['Bob​', false],
    ['a\tb', true, 'ab'], ['a\nb', true, 'ab'], ['a\r\nb', true, 'ab'], ['\u0001x', false], ['x\u007f', false], ['x\u0000', false],
    ['<script>alert(1)</script>', true, '<script>alert(1)</script>'], ['a&b', true, 'a&b'], ['"q" \'q\' <b>', true, '"q" \'q\' <b>'], ['100%', true, '100%'], ['..\\/', true, '..\\/'],
    [null, true, ''], [undefined, true, ''], [42, true, ''], [{}, true, ''],
];
for (const [text, ok, name] of NAME_TABLE) {
    const got = nameCheck(text);
    const shown = JSON.stringify(text);
    check('nameCheck(' + (shown === undefined ? 'undefined' : shown.slice(0, 40)) + ') is ' + (ok ? 'accepted' : 'refused'), got.ok === ok, JSON.stringify(got));
    if (ok) check('nameCheck(' + String(shown).slice(0, 40) + ') gives ' + JSON.stringify(name), got.name === name, JSON.stringify(got));
    else check('nameCheck(' + String(shown).slice(0, 40) + ') says why, in a short line', typeof got.why === 'string' && got.why.length > 8 && got.why.length < 120, JSON.stringify(got));
}
check('the refusal of a Bot ( name says what it is for', /computer players/.test(nameCheck('Bot (x)').why));
check('the refusal of a non-ASCII name says ASCII', /ASCII/.test(nameCheck('Zoë').why));
check('the refusal of a long name says 32', /32/.test(nameCheck('a'.repeat(33)).why));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. The name step
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function fakeUi() {
    const ui = {
        shown: 0, hidden: 0,
        input: { value: '', listeners: {}, addEventListener(t, fn) { this.listeners[t] = fn; } },
        button: { listeners: {}, addEventListener(t, fn) { this.listeners[t] = fn; } },
        message: { textContent: '' },
        show() { this.shown++; },
        hide() { this.hidden++; },
    };
    return ui;
}
function fakeStore(initial) {
    const data = Object.assign({}, initial);
    return { data, writes: [], recall(k) { return Object.prototype.hasOwnProperty.call(data, k) ? data[k] : null; }, remember(k, v) { this.writes.push([k, v]); data[k] = v; } };
}
function typeIn(ui, text) { ui.input.value = text; if (ui.input.listeners.input) ui.input.listeners.input({}); }
{
    const ui = fakeUi();
    const store = fakeStore({ 'ants.name': 'Maya' });
    const done = [];
    names.runNameStep(ui, store, function (n) { done.push(n); });
    check('the step is shown once, with the remembered name in the field', ui.shown === 1 && ui.input.value === 'Maya');
    check('nothing is done before the button', done.length === 0 && ui.hidden === 0 && store.writes.length === 0);
    ui.button.listeners.click();
    same('the button accepts the remembered name', done, ['Maya']);
    check('... it is remembered again and the step goes away', store.writes.length === 1 && store.writes[0][1] === 'Maya' && ui.hidden === 1);
    ui.button.listeners.click();
    ui.input.listeners.keydown({ key: 'Enter', preventDefault() {} });
    check('a second click or Enter does nothing more (once)', done.length === 1 && store.writes.length === 1);
}
for (const remembered of ['Bot (x)', 'Zoë', 'a'.repeat(40), '', '   ']) {
    const ui = fakeUi();
    names.runNameStep(ui, fakeStore({ 'ants.name': remembered }), function () {});
    check('a remembered name that the rules refuse (' + JSON.stringify(remembered).slice(0, 20) + ') is not put into the field', ui.input.value === '' || (remembered === '   ' && ui.input.value === ''));
}
{
    const ui = fakeUi();
    names.runNameStep(ui, fakeStore({}), function () {});
    check('nothing remembered: an empty field', ui.input.value === '');
}
{
    const ui = fakeUi();
    const store = fakeStore({});
    const done = [];
    names.runNameStep(ui, store, function (n) { done.push(n); });
    typeIn(ui, 'Bot (Hard)');
    ui.button.listeners.click();
    check('a bad name is explained, not accepted, not remembered, and the step stays', done.length === 0 && /computer players/.test(ui.message.textContent) && store.writes.length === 0 && ui.hidden === 0);
    typeIn(ui, 'Zoë');
    ui.input.listeners.keydown({ key: 'Enter', preventDefault() {} });
    check('Enter with a bad name is explained too', done.length === 0 && /ASCII/.test(ui.message.textContent));
    ui.input.listeners.keydown({ key: 'a', preventDefault() {} });
    check('another key does not accept', done.length === 0);
    typeIn(ui, '  Zed  ');
    check('typing again clears the message', ui.message.textContent === '');
    ui.input.listeners.keydown({ key: 'Enter', preventDefault() {} });
    same('Enter with a good name accepts it, trimmed', done, ['Zed']);
    same('... and remembers it', store.writes, [['ants.name', 'Zed']]);
}
{
    const ui = fakeUi();
    const store = fakeStore({ 'ants.name': 'Maya' });
    const done = [];
    names.runNameStep(ui, store, function (n) { done.push(n); });
    typeIn(ui, '');
    ui.button.listeners.click();
    same('an empty name is accepted: none chosen', done, ['']);
    same('... and forgets the remembered one', store.writes, [['ants.name', '']]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. web/shell.html: which addresses ask, the address without its name, the gate that holds the game back
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const shellPage = between(shellText, 'ANTS_PAGE_BEGIN', 'ANTS_PAGE_END', shellPath);
const gateBlock = between(shellText, 'NAMEGATE_BEGIN', 'NAMEGATE_END', shellPath);
const keysBlockShell = between(shellText, 'REJOINKEY_BEGIN', 'REJOINKEY_END', shellPath);
const shellCode = shellPage + '\n' + nameBlockShell + '\n' + keysBlockShell + '\n' + gateBlock + '\nreturn { P: ANTS_PAGE, makeNameGate: makeNameGate, holdsThisSeat: holdsThisSeat };';
const shell = new Function('window', 'document', shellCode)({ location: { search: '' }, localStorage: null }, {
    getElementById() { return { setAttribute() {}, classList: { add() {} } }; },
    querySelectorAll() { return []; },
});
const P = shell.P;
const ASK = [
    ['a shared link: a door and a room', '?join=/ws&room=ABC', true],
    ['... with a fill level', '?join=/ws&room=ABC&fill=hard', true],
    ['... with a seat', '?join=/ws&room=ABC&seat=2', true],
    ['... with the picture', '?join=/ws&room=ABC&aspect=4:3', true],
    ['... with a longer door', '?join=/ws/room-1&room=demo-small-2p-x7k2', true],
    ['the room code of 32 characters', '?join=/ws&room=' + 'x'.repeat(32), true],
    ['a name is chosen (the page that made the address put it there)', '?join=/ws&room=ABC&name=Bob', false],
    ['a name that is empty is a name that was chosen: none', '?join=/ws&room=ABC&name=', false],
    ['a name among the others', '?join=/ws&room=ABC&seat=1&name=Bob&fill=easy', false],
    ['a frame of another page, with a name', '?join=/ws&room=ABC&seat=1&name=Bob&embed=1', false],
    ['a frame of another page, even without a name', '?join=/ws&room=ABC&embed=1', false],
    ['embed=0 is no frame', '?join=/ws&room=ABC&embed=0', true],
    ['no room', '?join=/ws', false],
    ['a room that is no room code', '?join=/ws&room=a%20b', false],
    ['a room code of 33 characters', '?join=/ws&room=' + 'x'.repeat(33), false],
    ['a door that is not this site\'s', '?join=//evil.example/ws&room=ABC', false],
    ['a door that goes up', '?join=/ws/../x&room=ABC', false],
    ['no door', '?room=ABC', false],
    ['nothing', '', false],
    ['the single game', '?aspect=4:3', false],
];
for (const [label, search, want] of ASK) check('asksForName: ' + label + ' (' + search.slice(0, 50) + ')', P.asksForName(search) === want, String(P.asksForName(search)));
same('joinArguments is as it was for a shared link (the name step puts --name in later)', P.joinArguments('?join=/ws&room=ABC', false, 'h').args, ['--join-url', 'ws://h/ws', '--room', 'ABC']);
const STRIP = [
    ['?join=/ws&room=ABC&name=Bob', '?join=/ws&room=ABC'], ['?name=Bob&join=/ws', '?join=/ws'], ['?join=/ws&name=Bob&room=ABC', '?join=/ws&room=ABC'],
    ['?name=Bob', ''], ['?join=/ws&name=', '?join=/ws'], ['?join=/ws&room=ABC', '?join=/ws&room=ABC'], ['', ''], ['?username=Bob&join=/ws', '?username=Bob&join=/ws'],
    ['?join=/ws&name=a&name=b&room=ABC', '?join=/ws&room=ABC'], ['?join=/ws&name=Zo%C3%AB%20X&seat=1', '?join=/ws&seat=1'],
];
for (const [search, want] of STRIP) same('withoutName(' + search + ')', P.withoutName(search), want);
check('a copy of the address of a game that was given a name is a shared link', P.asksForName(P.withoutName('?join=/ws&room=ABC&seat=1&name=Bob')) === true);

// the gate: a shared link holds the game back until the button; every other address does not
{
    const ui = fakeUi();
    const args = ['--join-url', 'ws://h/ws', '--room', 'ABC', '--aspect', '16:9'];
    const gate = shell.makeNameGate(true, args, ui, fakeStore({ 'ants.name': 'Maya' }));
    let started = 0;
    check('a shared link: the gate is closed and the step is shown (with the remembered name)', gate.pending() && ui.shown === 1 && ui.input.value === 'Maya');
    gate.when(function () { started++; });
    check('... the game is NOT started while the step is up (it cannot connect)', started === 0 && args.indexOf('--name') === -1);
    typeIn(ui, 'Zed');
    check('... nor while a name is only typed', started === 0);
    ui.button.listeners.click();
    check('... the button starts it, once, with the name as --name', started === 1 && args.join(' ') === '--join-url ws://h/ws --room ABC --aspect 16:9 --name Zed', args.join(' '));
    gate.when(function () { started++; });
    check('... and what asks after that is started at once', started === 2 && !gate.pending());
}
{
    const ui = fakeUi();
    const args = ['--join-url', 'ws://h/ws'];
    const gate = shell.makeNameGate(true, args, ui, fakeStore({}));
    let started = 0;
    gate.when(function () { started++; });
    ui.button.listeners.click();                       // the field is empty: none chosen
    check('an empty name starts the game with no --name (the game\'s Player)', started === 1 && args.indexOf('--name') === -1);
}
{
    const ui = fakeUi();
    const args = ['--join-url', 'ws://h/ws'];
    const gate = shell.makeNameGate(true, args, ui, fakeStore({}));
    let started = 0;
    gate.when(function () { started++; });
    typeIn(ui, 'Bot (Easy)');
    ui.button.listeners.click();
    check('a bad name does not start the game', started === 0 && args.length === 2 && gate.pending());
}
{
    const ui = fakeUi();
    const args = ['--join-url', 'ws://h/ws', '--name', 'Bob'];
    const gate = shell.makeNameGate(false, args, ui, fakeStore({ 'ants.name': 'Maya' }));
    let started = 0;
    gate.when(function () { started++; });
    check('any other address: the game starts at once, no step is shown, the arguments are untouched', started === 1 && ui.shown === 0 && args.join(' ') === '--join-url ws://h/ws --name Bob' && !gate.pending());
}
// a reload of a game that this browser is playing: the address has lost its name (the page drops it), so it looks like a shared link, but the browser holds the key of that very seat of that room
// on that server, and the name step must not stand between the player and the way back (the browser check found it: a reloaded page waited for a name and never rejoined)
{
    const NOW = Date.now();
    const SERVER = 'ws://h/ws';
    const entry = (age, server) => '{"k":"0f1e2d3c4b5a69788796a5b4c3d2e1f0","s":"' + (server || SERVER) + '","t":' + (NOW - age) + '}';
    const storageOf = (items) => {
        const data = Object.assign({}, items);
        return { data, get length() { return Object.keys(data).length; }, key(i) { const k = Object.keys(data); return i < k.length ? k[i] : null; }, getItem(k) { return k in data ? data[k] : null; }, removeItem(k) { delete data[k]; } };
    };
    const args = (search) => P.joinArguments(search, false, 'h').args;
    const holds = (search, items) => shell.holdsThisSeat(search, args(search), storageOf(items), NOW);
    const asks = (search, items) => P.asksForName(search) && !holds(search, items);
    const KEEP = { 'ants.rejoin.ABC.1': entry(5000) };
    check('a reload of a seat that this browser holds does not ask: the address of a game that was given a name, minus its name', asks(P.withoutName('?join=/ws&room=ABC&seat=1&name=Bob&aspect=16:9'), KEEP) === false);
    check('... the same without a seat in the address (Play in this tab, Join: the room only): any seat of the room counts', asks('?join=/ws&room=ABC&aspect=16:9', KEEP) === false);
    check('... with the fill level of the room\'s links too', asks('?join=/ws&room=ABC&fill=easy', KEEP) === false);
    check('a shared link of a room that this browser has no key for asks, as ever', asks('?join=/ws&room=ABC&seat=1', {}) === true);
    check('... and so does one for another room', asks('?join=/ws&room=XYZ&seat=1', KEEP) === true);
    check('... and one for another seat of the room (a link for the friend\'s seat: the key is mine, the seat is not)', asks('?join=/ws&room=ABC&seat=2', KEEP) === true);
    check('a key of another server does not count', asks('?join=/ws&room=ABC&seat=1', { 'ants.rejoin.ABC.1': entry(5000, 'wss://other.example/ws') }) === true);
    check('a key of a day and a second does not count (the game would not use it either)', asks('?join=/ws&room=ABC&seat=1', { 'ants.rejoin.ABC.1': entry(24 * 3600 * 1000 + 1000) }) === true);
    check('a key of a day to the millisecond does', asks('?join=/ws&room=ABC&seat=1', { 'ants.rejoin.ABC.1': entry(24 * 3600 * 1000) }) === false);
    check('a broken entry does not count', asks('?join=/ws&room=ABC&seat=1', { 'ants.rejoin.ABC.1': 'junk' }) === true);
    check('the door of the address is the server that the key must be of: /ws/room-1 is another server than /ws', asks('?join=/ws/room-1&room=ABC&seat=1', KEEP) === true && holds('?join=/ws/room-1&room=ABC&seat=1', { 'ants.rejoin.ABC.1': entry(5000, 'ws://h/ws/room-1') }) === true);
    check('with a storage that cannot be read the step asks, as for any shared link', shell.holdsThisSeat('?join=/ws&room=ABC&seat=1', args('?join=/ws&room=ABC&seat=1'), null, NOW) === false);
    check('with no door in the arguments there is no key to hold', shell.holdsThisSeat('?room=ABC&seat=1', [], storageOf(KEEP), NOW) === false);
    check('... and no other word of the arguments is taken for the door (the program\'s own name, say, which an entry may name as its server)', shell.holdsThisSeat('?room=ABC&seat=1', ['./this.program', '--room', 'ABC'], storageOf({ 'ants.rejoin.ABC.1': entry(5000, './this.program') }), NOW) === false);
    check('a frame (embed) is never asked, whatever the keys: the page does not even look', /var asks = !ANTS_EMBED && ANTS_PAGE\.asksForName\(window\.location\.search\);\s*if \(asks\) \{[^}]*holdsThisSeat\(window\.location\.search, ANTS_ARGS, window\.localStorage, Date\.now\(\)\)/.test(shellText));
    check('the keys block is the same text in the front page and the game page', keysBlockShell === between(lobbyText, 'REJOINKEY_BEGIN', 'REJOINKEY_END', lobbyPath));
}
// what the page does with the gate (read from the file): the game starts through it, and only through it
{
    const startCalls = [...shellText.matchAll(/\bstartGame\(\)/g)].length;
    const defs = [...shellText.matchAll(/function startGame\(\)/g)].length;
    const gated = [...shellText.matchAll(/ANTS_NAME_GATE\.when\(startGame\)/g)].length;
    check('the game of the page starts only through the gate (no direct startGame() call, two gated starts)', startCalls === defs && gated === 2, startCalls + ' calls, ' + defs + ' definitions, ' + gated + ' gated');
    check('the page makes the gate from the address (embed: never asked) and puts the name in the game\'s own arguments', /makeNameGate\(asks, ANTS_ARGS,/.test(shellText) && /var asks = !ANTS_EMBED && ANTS_PAGE\.asksForName\(window\.location\.search\)/.test(shellText));
    for (const id of ['name-step', 'name-step-title', 'name-step-input', 'name-step-go', 'name-step-msg']) check('the card of the step has #' + id, new RegExp('id="' + id + '"').test(shellText));
    check('the name field of the card holds 32 characters at the most and is no autofill target', /id="name-step-input"[^>]*maxlength="32"[^>]*autocomplete="off"/.test(shellText));
    check('the card is hidden until the step shows it', /id="name-step"[^>]*hidden/.test(shellText));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. web/lobby.html as a whole, with a small fake of the browser
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const NAMES_OF_THE_PAGE = (function () { const m = /var NAMES = \[([^\]]*)\]/.exec(lobbyText); return m ? m[1].split(',').map((s) => s.trim().replace(/^'|'$/g, '')) : []; })();
check('the page has its list of random names', NAMES_OF_THE_PAGE.length >= 4);

class El {
    constructor(env, tag, id) {
        this.env = env; this.tagName = tag; this.id = id || ''; this.children = []; this.parent = null; this.attributes = {}; this.listeners = {};
        this._text = ''; this.style = {}; this.hidden = false; this.value = ''; this.className = ''; this.src = ''; this.classes = new Set(); this.readOnly = false; this._checked = false;
    }
    get checked() { return this._checked; }
    set checked(on) {                                    // a radio button: checking it unchecks the others of its group (the same name), as a browser does
        const radio = (e) => e.tagName === 'input' && e.attributes.type === 'radio' && e.attributes.name;
        if (on && radio(this)) for (const other of Object.values(this.env.elements)) if (other !== this && radio(other) && other.attributes.name === this.attributes.name) other._checked = false;
        this._checked = !!on;
    }
    get textContent() { return this._text + this.children.map((c) => c.textContent).join(''); }
    set textContent(v) { this.children = []; this._text = String(v); }
    set innerHTML(v) { this.env.innerHTMLWrites.push(String(v)); this.children = []; this._text = String(v); }
    get innerHTML() { return this._text; }
    get firstChild() { return this.children[0] || null; }
    appendChild(c) { c.parent = this; this.children.push(c); return c; }
    removeChild(c) { this.children = this.children.filter((x) => x !== c); c.parent = null; return c; }
    setAttribute(k, v) { this.attributes[k] = String(v); if (k === 'class') this.className = String(v); }
    getAttribute(k) { return Object.prototype.hasOwnProperty.call(this.attributes, k) ? this.attributes[k] : null; }
    removeAttribute(k) { delete this.attributes[k]; }
    addEventListener(t, fn) { (this.listeners[t] = this.listeners[t] || []).push(fn); }
    fire(t, ev) { (this.listeners[t] || []).forEach((fn) => fn(ev || {})); }
    click() { this.fire('click', {}); }
    key(k) { this.fire('keydown', { key: k, preventDefault() {} }); }
    focus() { this.env.focused = this; }
    select() {}
    contains(o) { for (let n = o; n; n = n.parent) if (n === this) return true; return false; }
    get classList() {
        const el = this;
        const has = () => new Set(String(el.className).split(/\s+/).filter(Boolean));
        return {
            add(c) { const s = has(); s.add(c); el.className = [...s].join(' '); },
            remove(c) { const s = has(); s.delete(c); el.className = [...s].join(' '); },
            toggle(c, force) { const s = has(); const on = force === undefined ? !s.has(c) : !!force; if (on) s.add(c); else s.delete(c); el.className = [...s].join(' '); return on; },
            contains(c) { return has().has(c); },
        };
    }
    all() { return [this].concat(...this.children.map((c) => (c.all ? c.all() : []))); }
    findAll(pred) { return this.all().filter((e) => e.tagName && pred(e)); }
    querySelector(sel) { const cls = sel.replace(/^\./, ''); return this.all().find((e) => e !== this && e.tagName && String(e.className).split(/\s+/).indexOf(cls) !== -1) || null; }
}

const THROWS = 'THROWS';
function runLobby(search, stored, options) {
    options = options || {};
    const env = { innerHTMLWrites: [], assigned: [], replaced: [], opened: [], copied: [], shared: [], elements: {}, focused: null, confirms: [] };
    const html = lobbyText;
    const body = html.slice(html.indexOf('<body>'));
    for (const m of body.matchAll(/<(\w+)([^>]*)>/g)) {
        const idm = /\bid="([^"]+)"/.exec(m[2]);
        if (!idm) continue;
        const el = new El(env, m[1], idm[1]);
        el.hidden = /\bhidden\b/.test(m[2].replace(/aria-hidden/g, '').replace(/="[^"]*"/g, ''));       // (the boolean attribute, not a word inside a value)
        el._checked = /\bchecked\b/.test(m[2].replace(/="[^"]*"/g, ''));                                      // (the same for a radio button that is checked in the markup)
        for (const a of m[2].matchAll(/([\w-]+)="([^"]*)"/g)) { el.attributes[a[1]] = a[2]; if (a[1] === 'class') el.className = a[2]; }
        if (m[1] === 'input' && el.attributes.value !== undefined) el.value = el.attributes.value;               // (an input's value as the markup gives it: the radio buttons of the levels, the players)
        env.elements[idm[1]] = el;
    }
    const doc = {
        body: new El(env, 'body', ''),
        getElementById(id) { if (!env.elements[id]) throw new Error('the page asked for #' + id + ', which its markup does not have'); return env.elements[id]; },
        createElement(tag) { return new El(env, tag, ''); },
        createTextNode(text) { return { isText: true, textContent: String(text), parent: null }; },
        activeElement: null,
        execCommand() { return true; },
        hidden: false, listeners: {},
        addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); },
    };
    const data = stored === THROWS ? {} : Object.assign({}, stored);
    const storage = {
        data,
        getItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); return Object.prototype.hasOwnProperty.call(data, k) ? data[k] : null; },
        setItem(k, v) { if (stored === THROWS) throw new Error('storage is blocked'); data[k] = String(v); },
        get length() { if (stored === THROWS) throw new Error('storage is blocked'); return Object.keys(data).length; },
        key(i) { if (stored === THROWS) throw new Error('storage is blocked'); const names = Object.keys(data); return i < names.length ? names[i] : null; },
        removeItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); delete data[k]; },
    };
    // the tab's own storage (the card keeps its room there until START): options.session is what it holds, options.noSession is a browser that has none; where the other storage throws this one does too
    const sessionData = stored === THROWS ? {} : Object.assign({}, options.session);
    const session = {
        data: sessionData,
        getItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); return Object.prototype.hasOwnProperty.call(sessionData, k) ? sessionData[k] : null; },
        setItem(k, v) { if (stored === THROWS) throw new Error('storage is blocked'); sessionData[k] = String(v); },
        removeItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); delete sessionData[k]; },
    };
    const win = {
        location: { search, href: 'https://play.test/' + search, pathname: '/', origin: 'https://play.test', protocol: 'https:', host: 'play.test', assign(u) { env.assigned.push(u); } },
        localStorage: storage,
        crypto: nodeCrypto.webcrypto,
        listeners: {},
        addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); },
        open(url, name, features) { env.opened.push({ url, name, features }); return { closed: false, focus() {} }; },
        confirm(q) { env.confirms.push(q); return true; },
    };
    if (!options.noSession) win.sessionStorage = session;
    const nav = { clipboard: { writeText(t) { env.copied.push(t); return { then() {} }; } } };
    // options.share: a browser that has navigator.share (true: it takes the data; 'throws': it throws; 'rejects': the person closes the sheet, a promise that fails)
    if (options.share === true) nav.share = (data) => { env.shared.push(data); return Promise.resolve(); };
    if (options.share === 'throws') nav.share = () => { throw new Error('not allowed'); };
    if (options.share === 'rejects') nav.share = (data) => { env.shared.push(data); return Promise.reject(new Error('AbortError')); };
    const history = { replaceState(...a) { env.replaced.push(a); } };
    const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)];
    const code = scripts[scripts.length - 1][1];
    // the site's answers (options.fetch(url, init) -> a promise of a response; the default is no network at all; options.noFetch is a browser that has no fetch)
    env.fetches = [];
    const network = options.fetch || (() => Promise.reject(new Error('the test has no network')));
    const fetchSpy = options.noFetch ? undefined : (url, init) => { env.fetches.push({ url, init }); return network(url, init); };
    new Function('window', 'document', 'history', 'navigator', 'setInterval', 'setTimeout', 'fetch', code)(win, doc, history, nav, function () { return 0; }, function () { return 0; }, fetchSpy);
    env.doc = doc;
    env.storage = storage;
    env.session = session;
    env.win = win;
    env.body = doc.body;
    env.$ = (id) => env.elements[id];
    env.type = (id, text) => { const el = env.elements[id]; el.value = text; el.fire('input', {}); };
    env.rows = () => env.$('seat-rows').children;
    env.rowButton = (seat, label) => env.rows()[seat].children.find((c) => c.textContent === label);
    env.frames = () => env.$('grid').children.map((cell) => cell.findAll((e) => e.tagName === 'iframe')[0]).filter(Boolean);
    env.frameOf = (seat) => { const cell = env.$('grid').children.find((c) => c.getAttribute('data-seat') === String(seat)); return cell ? cell.findAll((e) => e.tagName === 'iframe')[0] : null; };
    env.param = (url, key) => new URL(url).searchParams.get(key);
    env.roomStarted = () => !env.$('room-panel').hidden;
    // The person picks a button: it is checked and its change event comes (the others of its group are unchecked)
    env.pick = (id) => { env.$(id).checked = true; env.$(id).fire('change', {}); };
    return env;
}
function randomName(n) { return NAMES_OF_THE_PAGE.indexOf(n) !== -1; }

// ---- the plain page (nothing in the address)
{
    const env = runLobby('', {});
    check('the page has ONE name field, near the top, in the same box for START and Join', !!env.$('player-name') && !env.$('join-name') && env.$('who') && !env.$('who').hidden);
    check('nothing is asked first: the card and "How it works" are there, the step\'s button and its way out are not', !env.$('cards').hidden && !env.$('how').hidden && env.$('who-back').hidden && env.$('who-go').hidden && env.$('who-title').hidden);
    check('... and the page is not in its room mode', !env.body.classList.contains('in-room'));
    check('nothing remembered: the field is empty and its placeholder is Player', env.$('player-name').value === '' && env.$('player-name').getAttribute('placeholder') === 'Player' && env.$('player-name').getAttribute('maxlength') === '32');
}
{
    const env = runLobby('', { 'ants.name': 'Maya' });
    check('a remembered name is filled in the next time', env.$('player-name').value === 'Maya');
}
for (const bad of ['Bot (x)', 'Zoë', 'z'.repeat(40)]) {
    const env = runLobby('', { 'ants.name': bad });
    check('a remembered name that the rules refuse is not filled in (' + JSON.stringify(bad).slice(0, 14) + ')', env.$('player-name').value === '');
}
{
    let env = null;
    try { env = runLobby('', THROWS); } catch (e) { check('a browser that refuses its storage still runs the page', false, e.message); }
    if (env) {
        env.type('player-name', 'Alice');
        env.$('play').click();
        check('... and still starts (nothing is remembered, nothing breaks)', env.assigned.length === 1);
    }
}

// A room's panel without the card: START goes to the game page, but an address that names a map or a room opens the panel (after the name step, as for every address of somebody else's): the old
// addresses work as they did. openRoom types the name (when one is given) and takes the step.
const openRoom = (search, stored, name) => {
    const env = runLobby(search, stored || {});
    if (name !== undefined) env.type('player-name', name);
    env.$('who-go').click();
    return env;
};

// ---- a room's panel: the typed name goes to the first seat of the page; the others keep random names
{
    const env = runLobby('?map=small&players=4', {});
    env.type('player-name', '  Alice  ');
    env.$('who-go').click();
    check('the name step with a good name makes the room', env.roomStarted() && env.$('player-name').value === '  Alice  ');
    check('... and the room takes the place of the card and of "How it works"', env.$('cards').hidden && env.$('how').hidden && !env.$('room-panel').hidden);
    check('... and the page is in its room mode (the header is a smaller one: the class in-room of the body)', env.body.classList.contains('in-room'));
    check('... and remembers the name (trimmed) under ants.name', env.storage.data['ants.name'] === 'Alice');
    check('before a seat starts every row shows a random name', env.rows().length >= 2 && env.rows().every((r, i) => randomName(r.children[0].textContent.split(' · ')[1])));
    env.rowButton(2, 'Play here').click();
    const f2 = env.frameOf(2);
    check('"Play here" on a seat: its game gets the typed name, its seat and the page\'s own flags', !!f2 && env.param(f2.src, 'name') === 'Alice' && env.param(f2.src, 'seat') === '2' && /&embed=1$/.test(f2.src) && env.param(f2.src, 'aspect') === '16:9', f2 && f2.src);
    const cell2 = env.$('grid').children.find((c) => c.getAttribute('data-seat') === '2');
    check('... the frame\'s label and the seat\'s row show the name as text', cell2.textContent.indexOf('Alice') !== -1 && env.rows()[2].children[0].textContent.indexOf('Alice') !== -1);
    env.rowButton(1, 'Play here').click();
    const f1 = env.frameOf(1);
    check('a second seat on the same page keeps a random name', !!f1 && randomName(env.param(f1.src, 'name')) && env.param(f1.src, 'name') !== 'Alice', f1 && f1.src);
    env.rowButton(3, 'Open a window').click();
    check('a window of its own for a third seat: a random name too, in the window\'s address', env.opened.length === 1 && randomName(env.param(env.opened[0].url, 'name')) && env.param(env.opened[0].url, 'seat') === '3' && env.param(env.opened[0].url, 'embed') === null);
    const anyLink = env.$('any-link').value;
    check('the link for anybody carries no name (whoever opens it is asked) and no seat', env.param(anyLink, 'name') === null && env.param(anyLink, 'seat') === null && env.param(anyLink, 'join') === '/ws' && !!env.param(anyLink, 'room'), anyLink);
    env.rowButton(0, 'Copy link').click();
    check('the link of a seat for somebody else carries its seat and no name', env.copied.length === 1 && env.param(env.copied[0], 'seat') === '0' && env.param(env.copied[0], 'name') === null && !/name=/.test(env.copied[0]), env.copied[0]);
    env.rowButton(2, 'Play here').click();
    check('"Play here" again (a reconnect) keeps the seat\'s name', env.param(env.frameOf(2).src, 'name') === 'Alice');
}
{   // the first seat that starts takes the name, whatever its number
    const env = openRoom('?map=treasure&players=4', {}, 'Zed');
    env.rowButton(3, 'Open a window').click();
    check('a window can be the first seat: it takes the name', env.param(env.opened[0].url, 'name') === 'Zed' && env.rows()[3].children[0].textContent.indexOf('Zed') !== -1);
    env.rowButton(0, 'Play here').click();
    check('... and then a frame is random', randomName(env.param(env.frameOf(0).src, 'name')));
}
{   // "All seats on this page": only the first seat takes the name
    const env = openRoom('?map=treasure&players=4', {}, 'Alice');
    env.$('all-here').click();
    const urls = [0, 1, 2, 3].map((s) => env.frameOf(s).src);
    check('all seats on this page: seat 0 plays as Alice, the others have random names, all different', env.param(urls[0], 'name') === 'Alice' && urls.slice(1).every((u) => randomName(env.param(u, 'name'))) && new Set(urls.slice(1).map((u) => env.param(u, 'name'))).size === 3);
}
{   // the random fallback
    const env = openRoom('?map=treasure&players=4', {});
    env.$('all-here').click();
    const got = [0, 1, 2, 3].map((s) => env.param(env.frameOf(s).src, 'name'));
    check('an empty field: every seat has a random name, as before', got.every(randomName) && new Set(got).size === 4, got.join(','));
    check('... and an empty field is remembered as none', env.storage.data['ants.name'] === '');
}

// ---- bad names start nothing
for (const bad of ['Bot (Medium)', ' bOt(x', 'Zoë', '名前', 'x'.repeat(33), 'a\u0001b']) {
    const env = runLobby('', {});
    env.type('player-name', bad);
    env.$('play').click();
    check('START with a bad name (' + JSON.stringify(bad).slice(0, 16) + ') goes nowhere and says why under the field', env.assigned.length === 0 && env.$('name-msg').textContent.length > 8 && env.replaced.length === 0 && env.$('player-name').getAttribute('aria-invalid') === 'true');
    env.type('player-name', 'Fine');
    check('... typing again clears the message', env.$('name-msg').textContent === '' && env.$('player-name').getAttribute('aria-invalid') === null);
    const step = runLobby('?map=small&players=4', {});
    step.type('player-name', bad);
    step.$('who-go').click();
    check('the name step of an address that hosts a match with a bad name (' + JSON.stringify(bad).slice(0, 16) + ') makes no room and says why', !step.roomStarted() && step.$('name-msg').textContent.length > 8 && step.replaced.length === 0);
    const env2 = runLobby('', {});
    env2.type('player-name', bad);
    env2.type('join-code', 'ABC-1');
    env2.$('join-go').click();
    check('Join with a bad name (' + JSON.stringify(bad).slice(0, 16) + ') goes nowhere and says why', env2.assigned.length === 0 && env2.$('name-msg').textContent.length > 8);
}
{   // a bad name typed after the room is made starts no seat
    const env = openRoom('?map=small&players=4', {});
    env.type('player-name', 'Bot (x)');
    env.rowButton(1, 'Play here').click();
    env.rowButton(2, 'Open a window').click();
    env.$('all-here').click();
    check('a seat does not start with a bad name in the field', env.frames().length === 0 && env.opened.length === 0 && env.$('name-msg').textContent.length > 8);
    env.type('player-name', 'Good');
    env.rowButton(1, 'Play here').click();
    check('... and starts when the name is fine', env.frames().length === 1 && env.param(env.frameOf(1).src, 'name') === 'Good');
}

// ---- joining with a code
{
    const env = runLobby('', {});
    env.type('player-name', 'Alice');
    env.type('join-code', 'demo-small-2p-x7k2');
    env.$('join-go').click();
    check('Join: the game page is opened with the room and the typed name', env.assigned.length === 1 && env.param(env.assigned[0], 'room') === 'demo-small-2p-x7k2' && env.param(env.assigned[0], 'name') === 'Alice' && env.param(env.assigned[0], 'join') === '/ws', env.assigned[0]);
    check('... the name is remembered', env.storage.data['ants.name'] === 'Alice');
    const env2 = runLobby('', { 'ants.name': 'Maya' });
    env2.type('join-code', 'ABC');
    env2.$('join-code').key('Enter');
    check('Enter in the code field joins, with the remembered name that the field shows', env2.assigned.length === 1 && env2.param(env2.assigned[0], 'name') === 'Maya');
    const env3 = runLobby('', {});
    env3.type('join-code', 'ABC');
    env3.$('join-go').click();
    check('an empty name joins too, with the name\'s parameter empty (the game says Player; the game page does not ask again)', env3.assigned.length === 1 && env3.param(env3.assigned[0], 'name') === '' && /[?&]name=(&|$)/.test(env3.assigned[0]), env3.assigned[0]);
    const env4 = runLobby('', {});
    env4.type('player-name', 'Alice');
    env4.type('join-code', 'not a code!');
    env4.$('join-go').click();
    check('a bad room code joins nowhere (the form says why)', env4.assigned.length === 0 && /room code/.test(env4.$('join-hint').textContent));
    check('... in a notice (the class bad), and typing the code again takes the notice away', env4.$('join-hint').classList.contains('bad') && (env4.type('join-code', 'ABC'), !env4.$('join-hint').classList.contains('bad') && env4.$('join-hint').textContent === ''));
    const env5 = runLobby('', {});
    env5.type('player-name', 'Ann & <b>Bob</b>');
    env5.type('join-code', 'ABC');
    env5.$('join-go').click();
    check('a name with < > & goes into the address URL-encoded', env5.assigned.length === 1 && env5.assigned[0].indexOf('name=Ann%20%26%20%3Cb%3EBob%3C%2Fb%3E') !== -1 && env5.param(env5.assigned[0], 'name') === 'Ann & <b>Bob</b>', env5.assigned[0]);
}

// ---- a name with < > & is text, never markup
{
    const evil = '<img src=x onerror=alert(1)>&amp;"\'';
    const typed = evil.slice(0, 32);
    const env = openRoom('?map=treasure&players=4', {}, typed);
    env.rowButton(0, 'Play here').click();
    env.rowButton(1, 'Open a window').click();
    env.$('all-here').click();
    check('the page writes no markup at all in a whole session (no innerHTML)', env.innerHTMLWrites.length === 0, env.innerHTMLWrites.slice(0, 2).join(' | '));
    const cell0 = env.$('grid').children.find((c) => c.getAttribute('data-seat') === '0');
    check('the name shows as text in the frame\'s label and in the row', cell0.textContent.indexOf(typed) !== -1 && env.rows()[0].textContent.indexOf(typed) !== -1);
    check('no element of the page was made from the name\'s characters (a < in a name makes no tag)', cell0.findAll((e) => e.tagName === 'img').length === 0 && env.rows()[0].findAll((e) => e.tagName === 'img').length === 0);
    check('the frame\'s address carries the name encoded: no raw < or > or quote in it', env.frameOf(0).src.indexOf('<') === -1 && env.frameOf(0).src.indexOf('>') === -1 && env.param(env.frameOf(0).src, 'name') === typed, env.frameOf(0).src);
}
check('the step\'s way out is a plain anchor to "/" with a readable text, hidden in the markup (the script shows it with the step)', /<a id="who-back" class="btn sm who-back" href="\/" hidden>&larr; Back to the front page<\/a>/.test(lobbyText));
check('the page assigns no innerHTML anywhere', !/\.innerHTML\s*[+]?=/.test(lobbyText) && !/document\.write/.test(lobbyText) && !/insertAdjacentHTML/.test(lobbyText));

// ---- a link of the page that asks for a match: the name is asked first, every time, and nothing starts before
{
    const env = runLobby('?room=demo-small-2p-abc12', { 'ants.name': 'Maya' });
    check('a shared room link: the step is up, with the remembered name in the field and a Join button', !env.$('who-go').hidden && env.$('who-go').textContent === 'Join' && !env.$('who-title').hidden && /demo-small-2p-abc12/.test(env.$('who-title').textContent) && env.$('player-name').value === 'Maya');
    check('... nothing starts before the button: no room panel, no frame, the address is not rewritten', !env.roomStarted() && env.frames().length === 0 && env.replaced.length === 0 && env.opened.length === 0 && env.$('cards').hidden && env.$('how').hidden);
    check('... the step has a way out: "Back to the front page" is shown, a plain link to the site\'s front page (no query: nothing of the match goes with it)', !env.$('who-back').hidden && env.$('who-back').getAttribute('href') === '/');
    check('... the name step is the full page, not a room (the room class comes with the room)', !env.body.classList.contains('in-room'));
    env.type('player-name', 'Bot (x)');
    env.$('who-go').click();
    check('... a bad name does not start it and says why', !env.roomStarted() && env.$('name-msg').textContent.length > 8);
    env.type('player-name', 'Zed');
    env.$('player-name').key('Enter');
    check('... Enter with a good name does: the room panel opens (the address is rewritten to the room), the step goes away', env.roomStarted() && env.replaced.length === 1 && env.$('who-go').hidden && env.$('who-title').hidden && env.$('who-back').hidden && env.storage.data['ants.name'] === 'Zed');
    env.rowButton(1, 'Play here').click();
    check('... and the seat that this person plays takes the name', env.param(env.frameOf(1).src, 'name') === 'Zed');
}
{
    const env = runLobby('?room=demo-small-2p-abc12', {});
    check('a shared room link with nothing remembered: an empty field, the placeholder', env.$('player-name').value === '' && !env.roomStarted());
    env.$('who-go').click();
    check('... an empty name is fine: the room opens, random names for the seats', env.roomStarted());
    env.rowButton(0, 'Play here').click();
    check('... (a random name for the seat)', randomName(env.param(env.frameOf(0).src, 'name')));
}
{
    const env = runLobby('?map=treasure&players=2', {});
    check('a link that hosts a match on a map asks for the name first too (button: Host), and has its way out too', !env.$('who-go').hidden && !env.$('who-back').hidden && env.$('who-go').textContent === 'Host' && !env.roomStarted() && env.replaced.length === 0);
    env.type('player-name', 'Alice');
    env.$('who-go').click();
    check('... then the room is made on that map', env.roomStarted() && env.replaced.length === 1 && /room=demo-treasure-2p-/.test(String(env.replaced[0][2])));
}
{
    const env = runLobby('?room=demo-small-2p-abc12&play=here', { 'ants.name': 'Maya' });
    check('the test mode ?play=here asks nobody: every seat starts at once', !env.$('who-go').hidden === false && env.roomStarted() && env.frames().length === 2);
    const names2 = [0, 1].map((s) => env.param(env.frameOf(s).src, 'name'));
    check('... the first seat takes the remembered name, the other seat is random', names2[0] === 'Maya' && randomName(names2[1]), names2.join(','));
    const env2 = runLobby('?map=small&play=here', {});
    check('?map= with ?play=here asks nobody either', env2.roomStarted() && env2.frames().length === 4 && env2.$('who-go').hidden);
}
{
    const env = runLobby('?room=a%20b', {});
    check('an address that is no room code is the plain page (nothing is asked)', !env.$('cards').hidden && !env.$('how').hidden && env.$('who-go').hidden && !env.roomStarted());
}
{
    const env = runLobby('?aspect=4:3', {});
    check('an address with only a picture is the plain page', !env.$('cards').hidden && env.$('who-go').hidden);
}

// ---- the front page: ONE card, "New match" (the owner, on a phone: "I don't see a way to change colors or send an invite to another person should all be right there. We don't need separate AI and
// online. Only do online."): a map with its preview, four seats (exactly one of them You, a choice for each of the others), the Teams, an invitation for each Friend seat and START; under it "Have a code?" and Join
const lobbyScript = (() => { const all = [...lobbyText.matchAll(/<script>([\s\S]*?)<\/script>/g)]; return all[all.length - 1][1]; })();
const WORDS5 = ['friend', 'easy', 'medium', 'hard', 'nobody'];
const seatId = (seat, word) => 'seat-' + seat + '-' + word;
// what the four rows show: the seats whose You marker is up, and the one button that is checked in each row's group ('?n' when it is not exactly one)
const youSeats = (env) => [0, 1, 2, 3].filter((s) => !env.$('seat-you-' + s).hidden);
const seatWords = (env) => [0, 1, 2, 3].map((s) => { const on = WORDS5.filter((w) => env.$(seatId(s, w)).checked); return on.length === 1 ? on[0] : '?' + on.length; });
const choose = (env, seat, word) => env.pick(seatId(seat, word));
const sit = (env, seat) => env.$('sit-' + seat).click();
const teamChoices = (env) => env.$('teams').children.map((o) => [o.value, o.textContent]);
const invites = (env) => env.$('invite-list').children;
const inviteLinks = (env) => invites(env).map((row) => row.children[1].value);
const copyOf = (env, i) => invites(env)[i].children[2];
const pickTeam = (env, value) => { env.$('teams').value = value; env.$('teams').fire('change', {}); };
const pickMap = (env, key) => { env.$('map-pick').value = key; env.$('map-pick').fire('change', {}); };
const FFA_ONLY = [['ffa', 'Free for all']];
const FOUR_TEAMS = [['ffa', 'Free for all'], ['0+1', 'Green + Red against Blue + Black'], ['0+2', 'Green + Blue against Red + Black'], ['0+3', 'Green + Black against Red + Blue']];
const MAP_KEYS = ['tiny', 'small', 'medium', 'gauntlet', 'treasure', 'islands'];
const CODE = /^demo-([a-z]+)-4p-(t[0-3][0-3]-)?[a-z2-9]{6}$/;
{
    const env = runLobby('', {});
    same('a first visit: Treasure, You at Green, a Medium bot in every other seat, free for all', [env.$('map-pick').value, youSeats(env), seatWords(env), env.$('teams').value], ['treasure', [0], ['medium', 'medium', 'medium', 'medium'], 'ffa']);
    check('... the name under You is one of the page\'s random names (the field is empty: "Leave it empty for a random name when you host"), and the marker says You', randomName(env.$('seat-name-0').textContent) && !env.$('seat-name-0').hidden && [0, 1, 2, 3].every((s) => new RegExp('<span class="you" id="seat-you-' + s + '" hidden> &middot; You</span>').test(lobbyText)));
    same('... the rows of the other seats have a Sit here and a group of five buttons; the row of You has neither', [0, 1, 2, 3].map((s) => [env.$('sit-' + s).hidden, env.$('seat-set-' + s).hidden]), [[true, true], [false, false], [false, false], [false, false]]);
    check('... the seat of You is marked (a class), the others are not', env.$('seat-row-0').classList.contains('is-you') && [1, 2, 3].every((s) => !env.$('seat-row-' + s).classList.contains('is-you')));
    same('... four seats play: the Teams select offers free for all and Green with each of the others', [env.$('teams-line').hidden, teamChoices(env)], [false, FOUR_TEAMS]);
    same('... no invitation (no Friend), START is on and says that it starts at once', [env.$('invites').hidden, env.$('play').disabled, env.$('start-note').textContent], [true, false, 'Starts at once, in this tab. Bots gather food, raid and fight back.']);
    same('... the preview and the Map Info line are those of Treasure', [env.$('map-preview').src, env.$('map-info').textContent], ['front/preview_treasure.png', "One person's trash... (12 min)"]);
    check('... the card is there with the name field, "Have a code?" and the picture\'s buttons; "How it works" too', !env.$('cards').hidden && !env.$('how').hidden && !!env.$('player-name') && !!env.$('join-code') && !!env.$('aspect-16-9'));
    check('... START is named for a screen reader (the button is the original\'s own picture)', env.$('play').getAttribute('aria-label') === 'Start the match' && env.$('play').getAttribute('aria-describedby') === 'start-note');
    check('nothing was written by merely loading (no choice, no room code)', Object.keys(env.storage.data).length === 0 && Object.keys(env.session.data).length === 0, JSON.stringify([env.storage.data, env.session.data]));
    const ids = [...lobbyText.matchAll(/<input type="radio" name="(seat-\d)" id="(seat-\d-\w+)" value="(\w*)"/g)].map((m) => m.slice(1, 4).join(' '));
    same('the choices are radio buttons of four groups (names seat-0 to seat-3), Friend, Easy, Medium, Hard, Nobody in this order (the arrow keys and Tab are the browser\'s own)', ids,
         [0, 1, 2, 3].flatMap((s) => WORDS5.map((w) => 'seat-' + s + ' seat-' + s + '-' + w + ' ' + w)));
    same('every seat\'s group is a fieldset named by its colour for a screen reader (the legend), and every Sit here says which seat it is for',
         [0, 1, 2, 3].map((s) => [new RegExp('<fieldset class="seatset" id="seat-set-' + s + '"><legend class="sr">' + ['Green', 'Red', 'Blue', 'Black'][s] + '</legend>').test(lobbyText), env.$('sit-' + s).getAttribute('aria-label')]),
         [[true, 'Sit here as Green'], [true, 'Sit here as Red'], [true, 'Sit here as Blue'], [true, 'Sit here as Black']]);
}
{   // Sit here
    const env = runLobby('', {});
    choose(env, 1, 'hard');
    choose(env, 2, 'friend');
    choose(env, 3, 'nobody');
    sit(env, 2);
    same('Sit here at Blue: You are at Blue (and only there); Green shows the choice that it kept (Medium), Blue\'s own choice (Friend) is kept out of sight, Red and Black keep theirs', [youSeats(env), seatWords(env)], [[2], ['medium', 'hard', 'friend', 'nobody']]);
    same('... Blue has the marker, the name and neither buttons nor Sit here; the others have Sit here and buttons', [[0, 1, 2, 3].map((s) => env.$('sit-' + s).hidden), [0, 1, 2, 3].map((s) => env.$('seat-set-' + s).hidden), [0, 1, 2, 3].map((s) => env.$('seat-name-' + s).hidden)], [[false, false, true, false], [false, false, true, false], [true, true, false, true]]);
    check('... and the focus goes to the Sit here of the seat that You left (the button that was pressed is gone)', env.focused === env.$('sit-0'));
    same('... what is remembered: You at Blue and the four choices', JSON.parse(env.storage.data['ants-match']), { map: 'treasure', you: 2, seats: ['medium', 'hard', 'friend', 'nobody'], teams: 'ffa' });
    sit(env, 0);
    same('... and back at Green: Blue shows its Friend again', [youSeats(env), seatWords(env)], [[0], ['medium', 'hard', 'friend', 'nobody']]);
    const again = runLobby('', env.storage.data);
    same('the next visit comes back with them: You, the choices', [youSeats(again), seatWords(again)], [[0], ['medium', 'hard', 'friend', 'nobody']]);
}
{   // the name under You: the typed name, as text, and a random one while the field is empty
    const env = runLobby('', {});
    env.type('player-name', 'Ann & <b>Bob</b>');
    check('the name that is typed is under You as text (no markup is made of it)', env.$('seat-name-0').textContent === 'Ann & <b>Bob</b>' && env.innerHTMLWrites.length === 0);
    env.type('player-name', '  Zed  ');
    check('... trimmed, as the name is', env.$('seat-name-0').textContent === 'Zed');
    env.type('player-name', 'Bot (x)');
    check('... a name that the rules refuse is not shown (a random one is: nothing starts with it)', randomName(env.$('seat-name-0').textContent));
    env.type('player-name', '');
    check('... an empty field is a random name again', randomName(env.$('seat-name-0').textContent));
    sit(env, 3);
    env.type('player-name', 'Maya');
    check('the name follows You to another seat', env.$('seat-name-3').textContent === 'Maya' && env.$('seat-name-3').hidden === false && env.$('seat-name-0').hidden === true);
    const remembered = runLobby('', { 'ants.name': 'Zoe' });
    check('a remembered name is under You from the start', remembered.$('seat-name-0').textContent === 'Zoe');
}
{   // invitations: a link for each Friend seat
    const env = runLobby('', {});
    choose(env, 1, 'friend');
    check('a Friend seat shows the invitations, one row for it', !env.$('invites').hidden && invites(env).length === 1);
    const link = inviteLinks(env)[0];
    const code = env.param(link, 'room');
    check('its room is one of four seats on the map, with no team word (free for all), and is in the tab\'s session', CODE.test(code) && /^demo-treasure-4p-[a-z2-9]{6}$/.test(code) && JSON.parse(env.session.data['ants-match-room']).code === code, code);
    same('the link: this site\'s game page, the door, the room, the friend\'s seat, the plan (Green is You, Red the friend, Medium bots at Blue and Black), the picture and the people to wait for (You and the friend)',
         [new URL(link).origin + new URL(link).pathname, env.param(link, 'join'), env.param(link, 'seat'), env.param(link, 'fill'), env.param(link, 'aspect'), env.param(link, 'start')], ['https://play.test/', '/ws', '1', 'none,none,medium,medium', '16:9', '2']);
    check('... no name (whoever opens it is asked what they want to be called), no teams (the code has them), no key', ['name', 'teams', 'key', 'token', 'embed'].every((k) => env.param(link, k) === null) && !/key|token/i.test(link), link);
    same('the row: the friend\'s colour, the link in a field that cannot be typed in, Copy link; no Share where the browser has none', [invites(env)[0].children[0].textContent, invites(env)[0].children[1].value === link && invites(env)[0].children[1].attributes.readonly !== undefined && invites(env)[0].children[1].readOnly === true, copyOf(env, 0).textContent, invites(env)[0].children.length], ['Red', true, 'Copy link', 3]);
    check('... the field has a name for a screen reader', invites(env)[0].children[1].getAttribute('aria-label') === 'Invitation link for Red');
    copyOf(env, 0).click();
    same('Copy link copies that link (and nothing else)', env.copied, [link]);
    choose(env, 3, 'friend');
    same('a second Friend seat: two rows, in the order of the seats, the same room, the same plan (none for both friends) and the people to wait for (three)', [invites(env).map((r) => r.children[0].textContent), inviteLinks(env).map((l) => env.param(l, 'room')).every((r) => r === code), inviteLinks(env).map((l) => env.param(l, 'seat')), inviteLinks(env).map((l) => env.param(l, 'fill')), inviteLinks(env).map((l) => env.param(l, 'start'))],
         [['Red', 'Black'], true, ['1', '3'], ['none,none,medium,none', 'none,none,medium,none'], ['3', '3']]);
    choose(env, 1, 'easy');
    same('a seat that is no Friend any more loses its row, and the plan has its bot', [invites(env).map((r) => r.children[0].textContent), env.param(inviteLinks(env)[0], 'fill'), env.param(inviteLinks(env)[0], 'start')], [['Black'], 'none,easy,medium,none', '2']);
    choose(env, 3, 'hard');
    check('no Friend seat: no invitations', env.$('invites').hidden && invites(env).length === 0);
    check('the page made no markup of any of it', env.innerHTMLWrites.length === 0);
}
{   // Share, where the browser has it
    const env = runLobby('', {}, { share: true });
    choose(env, 2, 'friend');
    same('the row has Share too where the browser has it', [invites(env)[0].children.length, invites(env)[0].children[3].textContent], [4, 'Share']);
    invites(env)[0].children[3].click();
    same('Share gives the browser the link of that seat (a title and a line of text as well, nothing private)', [env.shared.length, env.shared[0].url, env.shared[0].title, /Blue/.test(env.shared[0].text), Object.keys(env.shared[0]).sort()], [1, inviteLinks(env)[0], 'Ants', true, ['text', 'title', 'url']]);
    check('a link that was shared is in use like a copied one: no note yet', env.$('links-note').hidden);
    choose(env, 3, 'hard');
    check('... and a choice that changes the links after a Share brings the note', !env.$('links-note').hidden);
    const refused = runLobby('', {}, { share: 'throws' });
    choose(refused, 2, 'friend');
    let ok = true;
    try { invites(refused)[0].children[3].click(); } catch (e) { ok = false; }
    check('a Share that fails (or is refused by the person: a promise that fails) breaks nothing', ok);
    const closed = runLobby('', {}, { share: 'rejects' });
    choose(closed, 2, 'friend');
    invites(closed)[0].children[3].click();
    check('... a rejected promise is caught too', closed.shared.length === 1);
}
{   // the room's code: made when a Friend seat first needs it, kept while the map and the teams stay, new when they change, with a note once a link was in use
    const env = runLobby('', {});
    choose(env, 1, 'friend');
    const first = env.param(inviteLinks(env)[0], 'room');
    choose(env, 2, 'friend');
    choose(env, 3, 'hard');
    choose(env, 2, 'easy');
    sit(env, 3);
    same('the code stays while the map and the teams stay (other seats, other choices, You at another seat)', inviteLinks(env).map((l) => env.param(l, 'room')), [first]);
    check('... and no note is up (no link was copied)', env.$('links-note').hidden);
    pickMap(env, 'small');
    const second = env.param(inviteLinks(env)[0], 'room');
    check('another map: another room (demo-small-4p-...), and the links are made again; no note yet (nothing was copied)', /^demo-small-4p-[a-z2-9]{6}$/.test(second) && second !== first && env.$('links-note').hidden, second);
    copyOf(env, 0).click();
    check('a link is copied: the note is not up', env.$('links-note').hidden);
    choose(env, 0, 'easy');
    check('a choice that changes the links (the plan has a bot at Green now) after a copy: the note is up (it says "copy them again"), the room is the same', !env.$('links-note').hidden && /<p class="hint" id="links-note" role="status" hidden>[^<]*copy them again\.<\/p>/.test(lobbyText) && env.param(inviteLinks(env)[0], 'room') === second);
    copyOf(env, 0).click();
    check('... copying again takes the note away', env.$('links-note').hidden);
    choose(env, 0, 'medium');
    check('... and the next change brings it back', !env.$('links-note').hidden);
    choose(env, 0, 'easy');
    check('... and a choice that is put back makes the links the ones that were copied: no note', env.$('links-note').hidden);
    pickMap(env, 'tiny');
    check('another map after a copy: another room and the note', /^demo-tiny-4p-/.test(env.param(inviteLinks(env)[0], 'room')) && !env.$('links-note').hidden);
    copyOf(env, 0).click();
    pickTeam(env, '0+1');
    check('the teams are a word of the code: Green + Red (all four seats play) makes demo-tiny-4p-t01-<six characters>, and the note is up again (the links changed after a copy)', /^demo-tiny-4p-t01-[a-z2-9]{6}$/.test(env.param(inviteLinks(env)[0], 'room')) && !env.$('links-note').hidden);
}
{   // a reload keeps the room of the tab (the links that were sent stay good); START lets go of it; the page that comes back from memory has a room of its own
    const env = runLobby('', {});
    choose(env, 1, 'friend');
    const link = inviteLinks(env)[0];
    copyOf(env, 0).click();
    const reloaded = runLobby('', env.storage.data, { session: env.session.data });
    same('a reload (the picture\'s selector reloads the page): the same links', inviteLinks(reloaded), [link]);
    check('... and no note (they are the links that were copied)', reloaded.$('links-note').hidden);
    choose(reloaded, 2, 'friend');
    check('... and a change after the reload that changes the links brings the note (what was copied is remembered by the tab)', !reloaded.$('links-note').hidden && reloaded.param(inviteLinks(reloaded)[0], 'room') === env.param(link, 'room'));
    pickMap(reloaded, 'islands');
    const third = runLobby('', reloaded.storage.data, { session: reloaded.session.data });
    same('a reload after the map changed keeps the room of that map (the tab saved it when it was made)', [/^demo-islands-4p-/.test(third.param(inviteLinks(third)[0], 'room')), third.param(inviteLinks(third)[0], 'room') === reloaded.param(inviteLinks(reloaded)[0], 'room')], [true, true]);
    const other = runLobby('', reloaded.storage.data, { session: { 'ants-match-room': JSON.stringify({ code: 'demo-tiny-4p-abcdef', used: '' }) } });
    check('a code that the tab holds for another map is not used (the choices changed meanwhile): the page makes a room of its own', /^demo-islands-4p-/.test(other.param(inviteLinks(other)[0], 'room')) && other.param(inviteLinks(other)[0], 'room') !== 'demo-tiny-4p-abcdef');
    check('... and with no link copied there is no note', other.$('links-note').hidden);
    const kept = runLobby('', reloaded.storage.data, { session: { 'ants-match-room': JSON.stringify({ code: 'demo-tiny-4p-abcdef', used: 'the links that were copied' }) } });
    check('... but what was copied stays known (the map changed in another tab): the links are not those any more, so the note is up', !kept.$('links-note').hidden && /^demo-islands-4p-/.test(kept.param(inviteLinks(kept)[0], 'room')));
    const longUsed = runLobby('', reloaded.storage.data, { session: { 'ants-match-room': JSON.stringify({ code: 'demo-tiny-4p-abcdef', used: 'x'.repeat(4001) }) } });
    const numberUsed = runLobby('', reloaded.storage.data, { session: { 'ants-match-room': JSON.stringify({ code: 'demo-tiny-4p-abcdef', used: 5 }) } });
    check('... a record that is too long or is no text is not believed', longUsed.$('links-note').hidden && numberUsed.$('links-note').hidden);
    for (const junk of ['not json', '{}', '{"code":5}', '{"code":"demo-islands-4p-ABCDEF"}', '{"code":"demo-islands-4p-t01-abcdef"}', '{"code":"x"}', 'null', '[]', '"demo-islands-4p-abcdef"']) {
        const j = runLobby('', reloaded.storage.data, { session: { 'ants-match-room': junk } });
        check('a room entry that is junk (' + junk.slice(0, 40) + ') is not used: the page makes its own', /^demo-islands-4p-[a-z2-9]{6}$/.test(j.param(inviteLinks(j)[0], 'room')) && j.param(inviteLinks(j)[0], 'room') !== 'abcdef');
    }
    // START takes this tab into the match with the same room, and the tab lets go of it
    const start = runLobby('', {});
    start.type('player-name', 'Ann');
    choose(start, 1, 'friend');
    const startRoom = start.param(inviteLinks(start)[0], 'room');
    start.$('play').click();
    check('START: one assignment, this tab (no window), the same room as the invitations', start.assigned.length === 1 && start.opened.length === 0 && start.param(start.assigned[0], 'room') === startRoom, JSON.stringify(start.assigned));
    check('... and the tab let go of the room (the next match is another room)', start.session.data['ants-match-room'] === undefined);
    start.win.listeners.pageshow.forEach((fn) => fn({ persisted: true }));
    check('a page that comes back from the browser\'s memory (Back) has a room of its own', start.param(inviteLinks(start)[0], 'room') !== startRoom && CODE.test(start.param(inviteLinks(start)[0], 'room')));
    let ok = true;
    try { const t = runLobby('', {}, { noSession: true }); choose(t, 1, 'friend'); ok = inviteLinks(t).length === 1; } catch (e) { ok = false; }
    check('a browser with no session storage (or one that refuses it) still has its links', ok);
}
{   // Back from the browser's memory without START (from a Join, say): the card's room and what was copied stay
    const env = runLobby('', {});
    choose(env, 1, 'friend');
    const link = inviteLinks(env)[0];
    copyOf(env, 0).click();
    env.win.listeners.pageshow.forEach((fn) => fn({ persisted: true }));
    same('a page that comes back from the browser\'s memory without START shows the same links, still in use: no note', [inviteLinks(env), env.$('links-note').hidden], [[link], true]);
    choose(env, 3, 'hard');
    check('... and a change after that brings the note (what was copied is not forgotten)', !env.$('links-note').hidden);
}
{   // START
    const env = runLobby('', {});
    env.type('player-name', 'Ann');
    choose(env, 1, 'friend');
    choose(env, 2, 'hard');
    sit(env, 3);
    env.$('play').click();
    const u = env.assigned[0] || '';
    same('START: this tab goes to the game page with the room, your seat (Black), the plan (Green a Medium bot, Red the friend, Blue a Hard bot, You at Black), your name, the picture and the people to wait for (You and the friend)',
         [env.assigned.length, new URL(u).origin + new URL(u).pathname, env.param(u, 'join'), env.param(u, 'seat'), env.param(u, 'fill'), env.param(u, 'name'), env.param(u, 'aspect'), env.param(u, 'start')], [1, 'https://play.test/', '/ws', '3', 'medium,none,hard,none', 'Ann', '16:9', '2']);
    check('... a room of four seats on the map, no teams parameter, no key, no embed, nothing opened in another window', CODE.test(env.param(u, 'room')) && env.param(u, 'teams') === null && env.param(u, 'key') === null && env.param(u, 'embed') === null && env.opened.length === 0 && env.replaced.length === 0, u);
    same('... the choices are remembered (ants-match) and the name (ants.name), and nothing else of the page\'s storage was written', [Object.keys(env.storage.data).sort(), JSON.parse(env.storage.data['ants-match']).you, env.storage.data['ants.name']], [['ants-match', 'ants.name'], 3, 'Ann']);
    const bots = runLobby('', {});
    bots.$('play').click();
    const b = bots.assigned[0] || '';
    same('START with bots only: no Friend row, so one person to wait for (the leader\'s game presses START at once), a Medium bot in every other seat, an empty name is a random one of the page\'s', [bots.param(b, 'start'), bots.param(b, 'fill'), bots.param(b, 'seat'), randomName(bots.param(b, 'name'))], ['1', 'none,medium,medium,medium', '0', true]);
    check('... and nothing was remembered by it (no choice was made)', Object.keys(bots.storage.data).join() === 'ants.name', JSON.stringify(bots.storage.data));
    const four = runLobby('', {});
    for (const s of [1, 2, 3]) choose(four, s, 'friend');
    four.$('play').click();
    same('three friends: four people to wait for and no bot: no plan at all (no fill parameter)', [four.param(four.assigned[0], 'start'), four.param(four.assigned[0], 'fill'), inviteLinks(four).map((l) => four.param(l, 'fill'))], ['4', null, [null, null, null]]);
    const shape = runLobby('?aspect=4:3', {});
    shape.$('play').click();
    check('the picture of the page goes with START (and the invitations)', shape.param(shape.assigned[0], 'aspect') === '4:3' && (choose(shape, 1, 'friend'), shape.param(inviteLinks(shape)[0], 'aspect')) === '4:3');
    const stored = runLobby('', { 'ants.aspect.v2': '4:3' });
    stored.$('play').click();
    check('... also the one that its buttons remembered', stored.param(stored.assigned[0], 'aspect') === '4:3');
}
{   // fewer than two players: START is off and says why
    const env = runLobby('', {});
    choose(env, 1, 'nobody');
    choose(env, 2, 'nobody');
    check('two seats play: START is on', env.$('play').disabled === false && env.$('start-note').textContent === 'Starts at once, in this tab. Bots gather food, raid and fight back.');
    choose(env, 3, 'nobody');
    same('one seat plays (You): START is off, and the line under it says what to do', [env.$('play').disabled, env.$('start-note').textContent], [true, 'Pick at least one more seat: a friend or a bot.']);
    same('... the Teams select is gone (nothing to choose)', [env.$('teams-line').hidden, teamChoices(env)], [true, FFA_ONLY]);
    env.$('play').click();
    check('... and a click on it (a script, a keyboard that reaches it) goes nowhere', env.assigned.length === 0);
    choose(env, 2, 'friend');
    same('a Friend seat is a player: START is on again, and says that it waits for the friend', [env.$('play').disabled, env.$('start-note').textContent], [false, 'Starts when your friend is in, or when you press START in the waiting room.']);
    choose(env, 3, 'friend');
    check('... two friends: "your friends are in"', env.$('start-note').textContent === 'Starts when your friends are in, or when you press START in the waiting room.');
    sit(env, 1);
    choose(env, 2, 'nobody');
    choose(env, 3, 'nobody');
    same('Sit here moves what counts: You at Red and Green a Medium bot (it kept its choice): two players, START is on', [youSeats(env), env.$('play').disabled], [[1], false]);
    choose(env, 0, 'nobody');
    check('... Green set to Nobody: one player, START is off', env.$('play').disabled === true);
}
{   // the Teams: the pairs that the playing seats allow; the chosen team is a word of the room's code
    const env = runLobby('', {});
    choose(env, 1, 'nobody');
    same('Red is Nobody: three seats play (Green, Blue, Black), and the pairs of them are the choices', [env.$('teams-line').hidden, teamChoices(env)], [false, [['ffa', 'Free for all'], ['0+2', 'Green + Blue against Black'], ['0+3', 'Green + Black against Blue'], ['2+3', 'Blue + Black against Green']]]);
    pickTeam(env, '2+3');
    choose(env, 2, 'friend');
    same('a team stays while it is still one of the choices; a Friend seat gives the room a code with its word (t23)', [env.$('teams').value, /^demo-treasure-4p-t23-[a-z2-9]{6}$/.test(env.param(inviteLinks(env)[0], 'room'))], ['2+3', true]);
    choose(env, 3, 'nobody');
    same('a seat that leaves the match takes the team with it: two seats play, free for all, a code with no word', [env.$('teams-line').hidden, env.$('teams').value, /^demo-treasure-4p-[a-z2-9]{6}$/.test(env.param(inviteLinks(env)[0], 'room'))], [true, 'ffa', true]);
    choose(env, 3, 'hard');
    same('... and it stays free for all when the seat comes back (a team is chosen again by a person)', env.$('teams').value, 'ffa');
    sit(env, 1);
    check('Sit here changes the seats that play, and the choices follow (Red is You, Green a bot, Blue a Friend, Black Hard: four play)', teamChoices(env).length === 4 && teamChoices(env)[1][1] === 'Green + Red against Blue + Black');
    pickTeam(env, '0+1');
    env.$('play').click();
    const u = env.assigned[0] || '';
    check('START with a team: the room\'s code names it (t01), and there is no teams parameter', /^demo-treasure-4p-t01-[a-z2-9]{6}$/.test(env.param(u, 'room')) && env.param(u, 'teams') === null && !/teams=/.test(u), u);
    same('... remembered: the team', JSON.parse(env.storage.data['ants-match']).teams, '0+1');
}
{   // the map: its preview and Map Info line, the setup screen's own; it is remembered
    const info = (() => { const m = /var MAP_INFO = \{([\s\S]*?)\};/.exec(lobbyText); return new Function('return {' + m[1] + '};')(); })();
    same('the page has the Map Info line of each of the six maps', Object.keys(info).sort(), MAP_KEYS.slice().sort());
    const env = runLobby('', {});
    let wrong = 0;
    let sample = '';
    for (const key of MAP_KEYS) {
        pickMap(env, key);
        const alt = env.$('map-preview').getAttribute('alt');
        if (env.$('map-preview').src !== 'front/preview_' + key + '.png' || env.$('map-info').textContent !== info[key] || alt.toLowerCase().indexOf(key) === -1 || env.$('map-pick').value !== key) { wrong++; if (!sample) sample = key + ': ' + env.$('map-preview').src + ' / ' + env.$('map-info').textContent + ' / ' + alt; }
    }
    check('choosing a map shows its own preview, Map Info line and a text for it (' + sample + ')', wrong === 0);
    check('... the select has the six maps in the page\'s order', JSON.stringify(env.$('map-pick').children.map((o) => o.value)) === JSON.stringify(MAP_KEYS));
    pickMap(env, 'islands');
    env.$('play').click();
    same('the map is in START\'s room (demo-islands-4p-...) and remembered with the rest', [/^demo-islands-4p-/.test(env.param(env.assigned[0], 'room')), JSON.parse(env.storage.data['ants-match']).map], [true, 'islands']);
    const again = runLobby('', env.storage.data);
    same('the next visit comes back on that map, with its preview', [again.$('map-pick').value, again.$('map-preview').src], ['islands', 'front/preview_islands.png']);
}
{   // what the earlier pages left in the browser is read for a first visit, and never written
    const old = { 'ants-solo-seats': 'easy,none,hard', 'ants-solo-teams': '0+3', 'ants-four-map': 'small', 'ants-four-players': '4', 'ants-four-fill': 'none,medium,medium,medium', 'ants-four-teams': '0+1' };
    const env = runLobby('', old);
    same('the opponents of a game on this computer, its team and the last map: Red Easy, Blue Nobody, Black Hard, Green and You with Black as a team, Small', [seatWords(env), youSeats(env), env.$('teams').value, env.$('map-pick').value, env.$('map-preview').src], [['medium', 'easy', 'nobody', 'hard'], [0], '0+3', 'small', 'front/preview_small.png']);
    choose(env, 2, 'friend');
    same('... what the card remembers is its own key; the old keys are left as they were and nothing was added', [Object.keys(env.storage.data).sort(), env.storage.data['ants-solo-seats'], env.storage.data['ants-four-fill']], [Object.keys(old).concat(['ants-match']).sort(), 'easy,none,hard', 'none,medium,medium,medium']);
    const hosted = runLobby('', { 'ants-four-players': '3', 'ants-four-fill': 'none,easy,none,hard', 'ants-four-teams': '0+2', 'ants-four-map': 'tiny' });
    same('a browser that only hosted rooms: its room is the card (3 players: Red Easy, Blue a Friend, Black Nobody) with its team', [seatWords(hosted), hosted.$('teams').value, hosted.$('map-pick').value, hosted.$('invites').hidden === false && invites(hosted).length === 1 && invites(hosted)[0].children[0].textContent], [['medium', 'easy', 'friend', 'nobody'], '0+2', 'tiny', 'Blue']);
    const legacy = runLobby('', { 'ants-solo-bots': 'hard' });
    same('the opponents that the first versions remembered (Hard) are in all three seats', seatWords(legacy), ['medium', 'hard', 'hard', 'hard']);
    const legacyNone = runLobby('', { 'ants-solo-bots': 'none' });
    same('... and None is Nobody in all three (the original\'s single player: START is off until a seat is picked)', [seatWords(legacyNone), legacyNone.$('play').disabled], [['medium', 'nobody', 'nobody', 'nobody'], true]);
    const own = runLobby('', Object.assign({ 'ants-match': JSON.stringify({ map: 'gauntlet', you: 1, seats: ['hard', 'medium', 'friend', 'easy'], teams: 'ffa' }) }, old));
    same('a state of the card\'s own beats all of them', [youSeats(own), seatWords(own), own.$('map-pick').value], [[1], ['hard', 'medium', 'friend', 'easy'], 'gauntlet']);
    for (const junk of ['not json', '{}', '{"map":"treasure","you":9,"seats":["medium","medium","medium","medium"],"teams":"ffa"}', '{"map":"treasure","you":0,"seats":["medium","medium","medium","HARD"],"teams":"ffa"}', 'null', '[]']) {
        const j = runLobby('', Object.assign({ 'ants-match': junk }, old));
        same('a remembered state that is junk (' + junk.slice(0, 40) + ') is not used: the first visit\'s', [seatWords(j), j.$('map-pick').value], [['medium', 'easy', 'nobody', 'hard'], 'small']);
    }
    let ok = true;
    try { const t = runLobby('', THROWS); choose(t, 1, 'friend'); sit(t, 2); t.$('play').click(); ok = t.assigned.length === 1; } catch (e) { ok = false; }
    check('a browser that refuses its storage still runs the card, invites and starts (nothing is remembered, nothing breaks)', ok);
}
{   // random clicks on the page itself: every address that START and the invitations make is read by the game page as what the card shows; no text of the address reaches the game's arguments
    let seed = 99;
    const rnd = (n) => { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return (seed >>> 0) % n; };
    let wrong = '';
    let started = 0;
    let invited = 0;
    let teamed = 0;
    const env = runLobby('', {});
    const names = ['Ann', 'Bob & <b>x</b>', '', 'x'.repeat(40), 'a=b&seat=2&start=4&room=z&fill=h', 'Zoë', 'Bot (x)', '  Max  ', '--name y', '%00'];
    for (let step = 0; step < 4000; step++) {
        const what = rnd(10);
        if (what < 4) choose(env, rnd(4), WORDS5[rnd(5)]);
        else if (what < 6) sit(env, rnd(4));
        else if (what < 7) pickMap(env, MAP_KEYS[rnd(6)]);
        else if (what < 8) { const choices = teamChoices(env); pickTeam(env, choices[rnd(choices.length)][0]); }
        else if (what < 9) env.type('player-name', names[rnd(names.length)]);
        else if (invites(env).length) copyOf(env, rnd(invites(env).length)).click();
        // what the card shows now
        const words = seatWords(env);
        const you = youSeats(env);
        if (you.length !== 1 || words.some((w) => w.charAt(0) === '?')) { wrong += ' rows ' + step; break; }
        const friends = [0, 1, 2, 3].filter((s) => s !== you[0] && words[s] === 'friend');
        const bots = [0, 1, 2, 3].filter((s) => s !== you[0] && ['easy', 'medium', 'hard'].indexOf(words[s]) !== -1);
        const playing = 1 + friends.length + bots.length;
        if (env.$('play').disabled !== (playing < 2)) wrong += ' start ' + step;
        if (invites(env).length !== friends.length || env.$('invites').hidden !== (friends.length === 0)) wrong += ' invites ' + step;
        const plan = [0, 1, 2, 3].map((s) => (bots.indexOf(s) !== -1 ? words[s] : 'none')).join(',');
        for (const [k, link] of inviteLinks(env).entries()) {
            const args = P.joinArguments(new URL(link).search, true, 'play.test').args;
            invited++;
            const want = ['--join-url', 'wss://play.test/ws', '--room', env.param(link, 'room'), '--seat', String(friends[k]), ...(bots.length ? ['--fill-bots', plan] : []), '--start-when', String(1 + friends.length)];
            if (JSON.stringify(args) !== JSON.stringify(want) || !CODE.test(env.param(link, 'room'))) wrong += ' invite ' + step + ' ' + JSON.stringify(args);
        }
        if (playing >= 2 && step % 7 === 0) {
            const before = env.assigned.length;
            const typed = nameCheck(env.$('player-name').value);
            env.$('play').click();
            if (!typed.ok) {                                   // (a bad name starts nothing)
                if (env.assigned.length !== before) wrong += ' badname ' + step;
                continue;
            }
            const u = env.assigned[before];
            started++;
            const args = P.joinArguments(new URL(u).search, true, 'play.test').args;
            const nameAt = args.indexOf('--name');
            const rest = args.filter((a, i) => i !== nameAt && i !== nameAt + 1);
            const want = ['--join-url', 'wss://play.test/ws', '--room', env.param(u, 'room'), '--seat', String(you[0]), ...(bots.length ? ['--fill-bots', plan] : []), '--start-when', String(1 + friends.length)];
            if (JSON.stringify(rest) !== JSON.stringify(want) || nameAt === -1 || args[nameAt + 1] !== (typed.name || env.$('seat-name-' + you[0]).textContent)) wrong += ' start ' + step + ' ' + JSON.stringify(args);
            if (nameAt !== -1 && !/^[\x20-\x7e]{1,32}$/.test(args[nameAt + 1])) wrong += ' name ' + step;
            if (env.param(u, 'room').indexOf('-t') !== -1) teamed++;
            env.win.listeners.pageshow.forEach((fn) => fn({ persisted: true }));         // (Back after START: the card is there again, with a room of its own)
        }
        if (wrong.length > 300) break;
    }
    check('4000 random clicks (seats, Sit here, map, teams, names, copies): every START (' + started + ') and invitation (' + invited + ', ' + teamed + ' of the STARTs with a team in the code) is read by the game page as exactly what the card shows: your seat, the plan, the people to wait for, no teams, no key; a bad name starts nothing:' + wrong, wrong === '' && started > 200 && invited > 500 && teamed > 10);
}
check('"How it works" is a details element that no script opens or closes (the script only hides it with the card, and shows it with the card)', /<details class="how" id="how" hidden>\s*<summary>/.test(lobbyText) && (lobbyScript.match(/\$\('how'\)[.\w]*/g) || []).length === 2 && (lobbyScript.match(/\$\('how'\)[.\w]*/g) || []).every((u) => u === "$('how').hidden") && !/getElementById\('how'\)/.test(lobbyScript));
for (const bad of ['Bot (Medium)', 'Zoë', 'x'.repeat(33)]) {
    const env = runLobby('', {});
    env.type('player-name', bad);
    env.$('play').click();
    check('START with a bad name (' + JSON.stringify(bad).slice(0, 16) + ') goes nowhere and says why under the field', env.assigned.length === 0 && env.$('name-msg').textContent.length > 8 && env.$('player-name').getAttribute('aria-invalid') === 'true');
}
{
    const env = runLobby('', THROWS);
    env.$('play').click();
    check('a browser that refuses its storage still plays (nothing is remembered, nothing breaks)', env.assigned.length === 1 && env.param(env.assigned[0], 'fill') === 'none,medium,medium,medium');
}
// ... the addresses: ?map=...&players=1 plays on this computer after the name step; an address that names no players still hosts 4 (the card does not offer a game on this computer, the old address does)
{
    const env = runLobby('?map=small&players=1&fill=hard', { 'ants.name': 'Maya' });
    check('?map=small&players=1&fill=hard asks for the name first (button Play), and nothing starts before', !env.$('who-go').hidden && env.$('who-go').textContent === 'Play' && /computer/.test(env.$('who-title').textContent) && env.assigned.length === 0 && env.$('cards').hidden && env.$('how').hidden);
    check('... the step explains itself in its own line, and the general line is not shown', !env.$('who-step-hint').hidden && /game on this computer/.test(env.$('who-step-hint').textContent) && env.$('who-general').hidden);
    env.$('who-go').click();
    const u = env.assigned[0] || '';
    check('... then the game of this computer on that map with the Hard bots and the name', env.assigned.length === 1 && env.param(u, 'map') === 'small' && env.param(u, 'bots') === 'hard' && env.param(u, 'name') === 'Maya', u);
    check('... and the step is gone, the general line is back', env.$('who-step-hint').hidden && !env.$('who-general').hidden && env.$('who-go').hidden && env.$('who-title').hidden && env.$('who-back').hidden);
    same('... such an address writes nothing of the card\'s or of the earlier pages\' keys (the card remembers its own choices only)', Object.keys(env.storage.data).sort(), ['ants.name']);
}
{
    const env = runLobby('?map=small&players=1&fill=hard', { 'ants.name': 'Maya', 'ants-solo-seats': 'easy,medium,hard', 'ants-solo-teams': '0+1' });
    env.$('who-go').click();
    const u = env.assigned[0] || '';
    check('an address with &fill=hard: Hard in all three bases, no teams', env.param(u, 'bots') === 'hard' && env.param(u, 'teams') === null, u);
    same('... and the earlier pages\' keys are left as they were', [env.storage.data['ants-solo-seats'], env.storage.data['ants-solo-teams'], 'ants-four-map' in env.storage.data, 'ants-four-players' in env.storage.data], ['easy,medium,hard', '0+1', false, false]);
}
{
    const env = runLobby('?map=treasure&players=1&play=here', {});
    check('the test mode ?play=here asks nobody: ?map=...&players=1 plays at once', env.assigned.length === 1 && env.$('who-go').hidden);
    const room = runLobby('?map=small', {});
    check('an address that names a map and no players hosts 4, as it always did (after the name step)', !room.$('who-go').hidden && room.$('who-go').textContent === 'Host');
    room.$('who-go').click();
    check('... the room of 4 on that map', room.roomStarted() && /demo-small-4p-/.test(room.$('room-code').textContent) && room.assigned.length === 0);
    check('... and the page is in its room mode', room.body.classList.contains('in-room'));
    check('... the card and "How it works" give way to the room', room.$('cards').hidden && room.$('how').hidden && !room.$('room-panel').hidden);
    same('... nothing is remembered by it (the card\'s choices are the card\'s own)', Object.keys(room.storage.data).sort(), ['ants.name']);
}
// ... the room's own seat in this tab: the address that Join makes, with the bots of the leader's START (the room panel of an old address)
{
    const env = openRoom('?map=treasure&players=3&fill=medium', {}, 'Ann');
    check('?map=treasure&players=3&fill=medium makes the room panel, and the play-here note of the frames is not shown yet', env.roomStarted() && env.$('frames-note').hidden === true);
    env.$('play-tab').click();
    const u = env.assigned[0] || '';
    check('"Play in this tab": this tab goes to the game page with the room, the bots of the leader\'s START and the name (no seat, no embed, no new window)',
          env.assigned.length === 1 && env.opened.length === 0 && env.param(u, 'join') === '/ws' && env.param(u, 'room') === env.$('room-code').textContent && env.param(u, 'fill') === 'medium' && env.param(u, 'name') === 'Ann' && env.param(u, 'seat') === null && env.param(u, 'embed') === null, u);
    const bad = openRoom('?map=treasure&players=3', {});
    bad.type('player-name', 'Bot (x)');
    bad.$('play-tab').click();
    check('... a bad name starts nothing', bad.assigned.length === 0 && bad.$('name-msg').textContent.length > 8);
    const shown = openRoom('?map=treasure&players=3', {});
    shown.rowButton(1, 'Play here').click();
    check('a game that plays on the page shows the note about the frames', shown.$('frames-note').hidden === false);
}
check('the front page says no more that the page opens a window by default (its only window.open is the seat\'s explicit one)', (lobbyText.match(/window\.open\(/g) || []).length === 1);


// ---- the line of numbers in the header (the block STATS has its own check, web_stats_check.js; here is the page as a whole: what it asks, what it shows, when it looks)
const settle = async () => { for (let i = 0; i < 60; i++) await Promise.resolve(); };
const answers = (table) => (url) => {
    const a = table[url];
    if (a === undefined) return Promise.reject(new TypeError('Failed to fetch'));
    if (a === 'page') return Promise.resolve({ status: 200, json: () => Promise.reject(new SyntaxError('Unexpected token < in JSON')) });      // a site with no /stats answers with the game page
    if (typeof a === 'number') return Promise.resolve({ status: a, json: () => Promise.resolve({}) });
    return Promise.resolve({ status: 200, json: () => Promise.resolve(a) });
};
const GOOD_STATS = { now: { matches: 3, players: 7 }, online: { day: 5, total: 900 }, local: { day: 16, total: 384 }, since: '2026-10-04' };
const statsView = (env) => ({ hidden: env.$('stats').hidden, dot: env.$('stats-dot').className, live: env.$('stats-live').textContent, played: env.$('stats-played').textContent, title: env.$('stats').getAttribute('title') });
(async () => {
    {
        const env = runLobby('', {});
        await settle();
        same('no network: the line is not there and says nothing (no text of an error), the page itself works', [statsView(env).hidden, statsView(env).live, statsView(env).played, env.fetches.map((f) => f.url)], [true, '', '', ['/stats', '/busy']]);
        env.$('play').click();
        check('... and START plays', env.assigned.length === 1);
    }
    {
        const env = runLobby('', {}, { fetch: answers({ '/stats': GOOD_STATS }) });
        check('before the answer the line is hidden (the markup says so)', /<p class="stats" id="stats" hidden>/.test(lobbyText) && env.$('stats').hidden === true);
        await settle();
        same('/stats answers: the line is shown, the dot is green, the numbers are the owner\'s example', statsView(env), { hidden: false, dot: 'live on', live: '3 matches being played · 7 players online', played: '1,284 games played (21 today)', title: 'Counted since 2026-10-04. Today means the last 24 hours.' });
        same('... one request, to the site\'s own /stats, with no cache and no cookies', env.fetches.map((f) => [f.url, f.init.cache, f.init.credentials]), [['/stats', 'no-store', 'omit']]);
        check('... and the page made no markup from the numbers', env.innerHTMLWrites.length === 0);
    }
    {
        const env = runLobby('', {}, { fetch: answers({ '/stats': 'page', '/busy': { matches: 0, players: 2 } }) });
        await settle();
        same('/stats is the game page (the site has no such route) and /busy answers: the live part, a grey dot, no totals', [env.fetches.map((f) => f.url), statsView(env)],
             [['/stats', '/busy'], { hidden: false, dot: 'live', live: '0 matches being played · 2 players online', played: '', title: null }]);
    }
    for (const [label, table] of [['/stats answers 404 and /busy 200', { '/stats': 404, '/busy': { matches: 1, players: 1 } }], ['/stats fails and /busy answers', { '/busy': { matches: 1, players: 1 } }]]) {
        const env = runLobby('', {}, { fetch: answers(table) });
        await settle();
        same(label + ': 1 match, 1 player, singular', [statsView(env).hidden, statsView(env).live], [false, '1 match being played · 1 player online']);
    }
    {
        const env = runLobby('', {}, { fetch: answers({ '/stats': { now: { matches: '<img src=x onerror=alert(1)>', players: 1 }, online: { day: 0, total: 0 }, local: { day: 0, total: 0 } }, '/busy': 500 }) });
        await settle();
        check('an answer with text where numbers belong shows nothing, and nothing of it is in the page', statsView(env).hidden === true && !/[<>]/.test(env.$('stats').textContent + env.$('stats-live').textContent + env.$('stats-played').textContent) && env.innerHTMLWrites.length === 0);
    }
    {   // the page looks again when it is shown after being hidden, and not while it is hidden
        let now = GOOD_STATS;
        const env = runLobby('', {}, { fetch: (url) => answers({ '/stats': now })(url) });
        await settle();
        check('the page listens for its visibility', (env.doc.listeners.visibilitychange || []).length === 1);
        const before = env.fetches.length;
        env.doc.hidden = true;
        env.doc.listeners.visibilitychange.forEach((fn) => fn());
        await settle();
        check('hidden: it asks nothing', env.fetches.length === before);
        now = { now: { matches: 0, players: 0 }, online: { day: 0, total: 1000 }, local: { day: 0, total: 234 }, since: '2026-10-04' };
        env.doc.hidden = false;
        env.doc.listeners.visibilitychange.forEach((fn) => fn());
        await settle();
        same('shown again: it looks at once and shows the new numbers (a grey dot, no match, 1,234 games, 0 today)', [env.fetches.length - before, statsView(env).dot, statsView(env).live, statsView(env).played], [1, 'live', '0 matches being played · 0 players online', '1,234 games played (0 today)']);
        now = undefined;                                                                  // the site stops answering: the next look finds neither /stats nor /busy
        env.doc.hidden = true;
        env.doc.listeners.visibilitychange.forEach((fn) => fn());
        env.doc.hidden = false;
        env.doc.listeners.visibilitychange.forEach((fn) => fn());
        await settle();
        same('... and when the site stops answering the line goes away again (it was shown before)', [statsView(env).hidden, env.fetches.length - before], [true, 3]);
    }
    {
        const env = runLobby('', {}, { noFetch: true });
        await settle();
        check('a browser with no fetch has no line and the page works', statsView(env).hidden === true && env.fetches.length === 0 && (env.$('play').click(), env.assigned.length === 1));
    }
    {   // a page that is a room, or asks for a name, has the line too (it is the header's)
        const room = runLobby('?room=demo-small-2p-abc12&play=here', {}, { fetch: answers({ '/stats': GOOD_STATS }) });
        await settle();
        check('a room has the line too', room.roomStarted() && statsView(room).hidden === false);
    }

// ---- the Rejoin button at the top of the front page (the block REJOIN has its own check, web_rejoin_block_check.js; here is the page as a whole: when it is there, what it says, where it goes)
{
    const SERVER = 'wss://play.test/ws';                                    // (the fake page is https://play.test/)
    const KEY = '0f1e2d3c4b5a69788796a5b4c3d2e1f0';
    const entry = (age, server) => '{"k":"' + KEY + '","s":"' + (server || SERVER) + '","t":' + (Date.now() - age) + '}';
    const rejoinView = (env) => ({ hidden: env.$('rejoin').hidden, button: env.$('rejoin-go').textContent, note: env.$('rejoin-note').textContent });
    const wholeText = (env) => Object.values(env.elements).map((e) => e.textContent + '|' + JSON.stringify(e.attributes)).join('\n') + env.innerHTMLWrites.join('');
    {
        const env = runLobby('', {});
        same('no entry: nothing of the Rejoin is shown, and the cards are there as ever', [rejoinView(env).hidden, env.$('cards').hidden, env.$('how').hidden], [true, false, false]);
        check('... the markup has it hidden, with no text of its own (the script fills it in)', /<section id="rejoin" class="rejoin" aria-label="Your running match" hidden>\s*<button id="rejoin-go" type="button" class="btn big"><\/button>\s*<p class="rejoin-note" id="rejoin-note"><\/p>\s*<\/section>/.test(lobbyText));
    }
    {
        const stored = { 'ants.rejoin.demo-tiny-2p-abc.1': entry(5000) };
        const env = runLobby('', stored);
        same('a fresh entry of this site: ONE button with the room, and the note under it', rejoinView(env), { hidden: false, button: 'Rejoin your match (demo-tiny-2p-abc)', note: 'Your match in room demo-tiny-2p-abc is still running: go back to your seat.' });
        check('... the cards stay where they were (the button is above them, in its own block, not in their grid)', !env.$('cards').hidden && lobbyText.indexOf('<section id="rejoin"') > lobbyText.indexOf('</header>') && lobbyText.indexOf('<section id="rejoin"') < lobbyText.indexOf('<main class="page">'));
        check('... and the key is in no text or attribute of the page', wholeText(env).indexOf(KEY) === -1 && wholeText(env).indexOf(KEY.slice(0, 8)) === -1);
        env.$('rejoin-go').click();
        same('pressing it takes THIS tab to the game page: the room, the seat, the (empty) name and the shape', env.assigned, ['https://play.test/?join=/ws&room=demo-tiny-2p-abc&seat=1&name=&aspect=16:9']);
        check('... with no key in the address, and nothing opened in another window', env.assigned[0].indexOf(KEY) === -1 && env.opened.length === 0 && env.storage.data['ants.rejoin.demo-tiny-2p-abc.1'] === stored['ants.rejoin.demo-tiny-2p-abc.1']);
    }
    {
        const env = runLobby('', { 'ants.rejoin.r-1.3': entry(1000), 'ants.name': 'Maya', 'ants.aspect.v2': '4:3' });
        env.$('rejoin-go').click();
        same('the remembered name and the remembered shape go with it', env.assigned, ['https://play.test/?join=/ws&room=r-1&seat=3&name=Maya&aspect=4:3']);
    }
    {
        const env = runLobby('?aspect=4:3', { 'ants.rejoin.r-1.0': entry(1000), 'ants.name': 'Maya' });
        env.$('rejoin-go').click();
        same('the shape of the address is the page\'s shape, so it is the one that goes with the button', env.assigned, ['https://play.test/?join=/ws&room=r-1&seat=0&name=Maya&aspect=4:3']);
    }
    {
        const env = runLobby('', { 'ants.rejoin.r-1.0': entry(1000), 'ants.name': 'Zo\u00eb' });
        env.$('rejoin-go').click();
        check('a remembered name that the rules refuse is no name (the field is empty too)', env.assigned.length === 1 && env.param(env.assigned[0], 'name') === '' && /&name=&aspect=/.test(env.assigned[0]), env.assigned[0]);
    }
    {
        const env = runLobby('', { 'ants.rejoin.old-room.0': entry(25 * 3600 * 1000), 'ants.rejoin.mid-room.1': entry(23 * 3600 * 1000) });
        same('an entry older than a day is not offered and is removed (as the game does); one a little younger is the offer', [rejoinView(env).button, Object.keys(env.storage.data)], ['Rejoin your match (mid-room)', ['ants.rejoin.mid-room.1']]);
    }
    {
        const env = runLobby('', { 'ants.rejoin.other.0': entry(1000, 'wss://other.test/ws'), 'ants.rejoin.mine.1': entry(9000) });
        same('an entry of another server is not offered and stays, even when it is newer', [rejoinView(env).button, Object.keys(env.storage.data).sort()], ['Rejoin your match (mine)', ['ants.rejoin.mine.1', 'ants.rejoin.other.0']]);
    }
    {
        const env = runLobby('', { 'ants.rejoin.other.0': entry(1000, 'ws://play.test/ws'), 'ants.rejoin.bad.7': entry(1000), 'ants.rejoin.junk.1': 'not json' });
        check('only entries of the game count: another scheme, a seat that is none and a value that is no JSON show nothing', rejoinView(env).hidden === true && Object.keys(env.storage.data).length === 3);
    }
    {
        const env = runLobby('', { 'ants.rejoin.a.0': entry(60000), 'ants.rejoin.b.2': entry(1000), 'ants.rejoin.c.1': entry(30000) });
        same('of several entries the newest is offered, and the seat goes with it', [rejoinView(env).button, (env.$('rejoin-go').click(), env.assigned.map((u) => env.param(u, 'seat')))], ['Rejoin your match (b)', ['2']]);
    }
    for (const [label, search] of [['a room (?room=)', '?room=demo-tiny-2p-abc'], ['a room that starts at once (?play=here)', '?room=demo-tiny-2p-abc&play=here'], ['a match to host (?map=)', '?map=tiny&players=2&play=here']]) {
        const env = runLobby(search, { 'ants.rejoin.demo-tiny-2p-abc.1': entry(1000) });
        check(label + ': the page is about that, not about the match of the key: no button', rejoinView(env).hidden === true && env.$('cards').hidden === true);
        env.$('who-go').click();
        check('... not even after the name step', rejoinView(env).hidden === true);
        (env.win.listeners.pageshow || []).forEach((fn) => fn({ persisted: true }));
        (env.win.listeners.storage || []).forEach((fn) => fn({}));
        check('... nor when the page comes back from the browser\'s memory or another tab writes to the storage (the cards are not up)', rejoinView(env).hidden === true && env.$('cards').hidden === true);
    }
    {   // the page that comes back from the browser's memory (Back), and another tab that changes the storage: the match may have ended meanwhile
        const env = runLobby('', { 'ants.rejoin.r-1.1': entry(1000) });
        check('the page listens for its return (the Rejoin button\'s and the card\'s) and for the storage', (env.win.listeners.pageshow || []).length === 2 && (env.win.listeners.storage || []).length === 1);
        delete env.storage.data['ants.rejoin.r-1.1'];
        env.win.listeners.pageshow.forEach((fn) => fn({ persisted: false }));
        check('a page that is loaded anew is not looked at again by pageshow (it has just looked)', rejoinView(env).hidden === false);
        env.win.listeners.pageshow.forEach((fn) => fn({ persisted: true }));
        check('the match ended in another tab, and the page comes back from memory: the button is gone', rejoinView(env).hidden === true);
        env.storage.data['ants.rejoin.r-2.0'] = entry(500);
        env.win.listeners.storage.forEach((fn) => fn({}));
        same('a new match in another tab: the button is there, for that match', [rejoinView(env).hidden, rejoinView(env).button], [false, 'Rejoin your match (r-2)']);
        delete env.storage.data['ants.rejoin.r-2.0'];
        env.win.listeners.storage.forEach((fn) => fn({}));
        check('... and gone again when the key is let go of', rejoinView(env).hidden === true);
        env.$('rejoin-go').click();
        check('a press that comes after the button is gone goes nowhere', env.assigned.length === 0);
    }
    {
        let env = null;
        try { env = runLobby('', THROWS); } catch (e) { check('a browser that refuses its storage still runs the page, with no button', false, e.message); }
        if (env) check('a storage that throws offers nothing, and the cards are there', rejoinView(env).hidden === true && !env.$('cards').hidden);
    }
    {
        const env = runLobby('', { 'ants.rejoin.r-1.1': entry(1000) });
        const before = JSON.stringify(env.storage.data);
        env.$('rejoin-go').click();
        check('the page writes nothing to the storage for it (the game page owns the entries)', JSON.stringify(env.storage.data) === before);
    }
}

})().then(() => {
    console.log('web name check: ' + checks + ' checks, ' + failures + ' failures');
    process.exit(failures === 0 ? 0 : 1);
}, (e) => {
    console.log('FAIL ' + e.message + '\n' + e.stack);
    process.exit(1);
});
