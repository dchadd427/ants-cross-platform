// Runs the pages' OWN code for the player's name (the owner: "the ability for somebody to type in their name ... so their name goes into the game instead of random", and "when joining a
// link from somebody else, it should ask you first what you want your name to be"):
//   - the rules of a name (the block NAME_BEGIN .. NAME_END, the same text in web/lobby.html and web/shell.html) on a table of names: the rules of the desktop start menu
//     (printable ASCII, trimmed, at most 32, nothing that starts with "Bot (" with blanks and case ignored; an empty name means "none chosen");
//   - the name step (runNameStep): filled in from what the browser remembered, a bad name explained and not accepted, the button or Enter accepts once, the name is remembered;
//   - web/shell.html: which addresses ask for a name (a shared link: ANTS_PAGE.asksForName), the address without its name (withoutName), and the gate that holds the game back until the name
//     is chosen (makeNameGate): the game is not started, so it does not connect, before the button; the chosen name goes into the game's arguments as --name;
//   - web/lobby.html (the front page) as a whole, with a small fake of the browser's DOM: a first visit (1 player, Treasure, Medium opponents in all three bases), single player (one level for each base and the Teams; Play takes this tab to the game page of this computer),
//     the host's own seat in this tab, and the field that Host and Join share is remembered and filled in, its name goes to the seat that this person plays
//     (the first seat of the page) and the other seats of the page keep random names, a link made for somebody else carries none, bad names start nothing, a name with < > & is only
//     ever text (the page writes no markup at all), an empty field falls back to a random name (and to Player for a join), a shared link of the page asks first and starts nothing before.
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
const shellCode = shellPage + '\n' + nameBlockShell + '\n' + gateBlock + '\nreturn { P: ANTS_PAGE, makeNameGate: makeNameGate };';
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
        this._text = ''; this.style = {}; this.hidden = false; this.value = ''; this.className = ''; this.src = ''; this.classes = new Set(); this.readOnly = false;
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
    const env = { innerHTMLWrites: [], assigned: [], replaced: [], opened: [], copied: [], elements: {}, focused: null, confirms: [] };
    const html = lobbyText;
    const body = html.slice(html.indexOf('<body>'));
    for (const m of body.matchAll(/<(\w+)([^>]*)>/g)) {
        const idm = /\bid="([^"]+)"/.exec(m[2]);
        if (!idm) continue;
        const el = new El(env, m[1], idm[1]);
        el.hidden = /\bhidden\b/.test(m[2].replace(/aria-hidden/g, '').replace(/="[^"]*"/g, ''));       // (the boolean attribute, not a word inside a value)
        for (const a of m[2].matchAll(/([\w-]+)="([^"]*)"/g)) { el.attributes[a[1]] = a[2]; if (a[1] === 'class') el.className = a[2]; }
        env.elements[idm[1]] = el;
    }
    const doc = {
        body: new El(env, 'body', ''),
        getElementById(id) { if (!env.elements[id]) throw new Error('the page asked for #' + id + ', which its markup does not have'); return env.elements[id]; },
        createElement(tag) { return new El(env, tag, ''); },
        createTextNode(text) { return { isText: true, textContent: String(text), parent: null }; },
        activeElement: null,
        execCommand() { return true; },
    };
    const data = stored === THROWS ? {} : Object.assign({}, stored);
    const storage = {
        data,
        getItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); return Object.prototype.hasOwnProperty.call(data, k) ? data[k] : null; },
        setItem(k, v) { if (stored === THROWS) throw new Error('storage is blocked'); data[k] = String(v); },
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
    const nav = { clipboard: { writeText(t) { env.copied.push(t); return { then() {} }; } } };
    const history = { replaceState(...a) { env.replaced.push(a); } };
    const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)];
    const code = scripts[scripts.length - 1][1];
    new Function('window', 'document', 'history', 'navigator', 'setInterval', 'setTimeout', code)(win, doc, history, nav, function () { return 0; }, function () { return 0; });
    env.storage = storage;
    env.win = win;
    env.$ = (id) => env.elements[id];
    env.type = (id, text) => { const el = env.elements[id]; el.value = text; el.fire('input', {}); };
    env.rows = () => env.$('seat-rows').children;
    env.rowButton = (seat, label) => env.rows()[seat].children.find((c) => c.textContent === label);
    env.frames = () => env.$('grid').children.map((cell) => cell.findAll((e) => e.tagName === 'iframe')[0]).filter(Boolean);
    env.frameOf = (seat) => { const cell = env.$('grid').children.find((c) => c.getAttribute('data-seat') === String(seat)); return cell ? cell.findAll((e) => e.tagName === 'iframe')[0] : null; };
    env.param = (url, key) => new URL(url).searchParams.get(key);
    env.roomStarted = () => !env.$('room-panel').hidden;
    // The first visit has 1 player (single player is on the front page): the tests below that host a match choose 4 players first, as a person does; options.firstVisit keeps the page as it comes
    if (!options.firstVisit) { env.$('players').value = '4'; env.$('players').fire('change', {}); }
    return env;
}
function randomName(n) { return NAMES_OF_THE_PAGE.indexOf(n) !== -1; }

// ---- the plain page (nothing in the address)
{
    const env = runLobby('', {});
    check('the page has ONE name field, near the top, in the same box for Host and Join', !!env.$('player-name') && !env.$('join-name') && env.$('who') && !env.$('who').hidden);
    check('nothing is asked first: the setup and the join form are there, the step\'s button is not', !env.$('setup').hidden && !env.$('join').hidden && env.$('who-go').hidden && env.$('who-title').hidden);
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
        env.$('create').click();
        check('... and still hosts (nothing is remembered, nothing breaks)', env.roomStarted());
    }
}

// ---- hosting: the typed name goes to the first seat of the page; the others keep random names
{
    const env = runLobby('', { 'ants-four-map': 'small' });
    env.type('player-name', '  Alice  ');
    env.$('create').click();
    check('Create with a good name makes the room', env.roomStarted() && env.$('player-name').value === '  Alice  ');
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
    const env = runLobby('', {});
    env.type('player-name', 'Zed');
    env.$('create').click();
    env.rowButton(3, 'Open a window').click();
    check('a window can be the first seat: it takes the name', env.param(env.opened[0].url, 'name') === 'Zed' && env.rows()[3].children[0].textContent.indexOf('Zed') !== -1);
    env.rowButton(0, 'Play here').click();
    check('... and then a frame is random', randomName(env.param(env.frameOf(0).src, 'name')));
}
{   // "All seats on this page": only the first seat takes the name
    const env = runLobby('', {});
    env.type('player-name', 'Alice');
    env.$('create').click();
    env.$('all-here').click();
    const urls = [0, 1, 2, 3].map((s) => env.frameOf(s).src);
    check('all seats on this page: seat 0 plays as Alice, the others have random names, all different', env.param(urls[0], 'name') === 'Alice' && urls.slice(1).every((u) => randomName(env.param(u, 'name'))) && new Set(urls.slice(1).map((u) => env.param(u, 'name'))).size === 3);
}
{   // the random fallback
    const env = runLobby('', {});
    env.$('create').click();
    env.$('all-here').click();
    const got = [0, 1, 2, 3].map((s) => env.param(env.frameOf(s).src, 'name'));
    check('an empty field: every seat has a random name, as before', got.every(randomName) && new Set(got).size === 4, got.join(','));
    check('... and an empty field is remembered as none', env.storage.data['ants.name'] === '');
}

// ---- bad names start nothing
for (const bad of ['Bot (Medium)', ' bOt(x', 'Zoë', '名前', 'x'.repeat(33), 'a\u0001b']) {
    const env = runLobby('', {});
    env.type('player-name', bad);
    env.$('create').click();
    check('Create with a bad name (' + JSON.stringify(bad).slice(0, 16) + ') makes no room and says why under the field', !env.roomStarted() && env.$('name-msg').textContent.length > 8 && env.replaced.length === 0 && env.$('player-name').getAttribute('aria-invalid') === 'true');
    env.type('player-name', 'Fine');
    check('... typing again clears the message', env.$('name-msg').textContent === '' && env.$('player-name').getAttribute('aria-invalid') === null);
    const env2 = runLobby('', {});
    env2.type('player-name', bad);
    env2.type('join-code', 'ABC-1');
    env2.$('join-go').click();
    check('Join with a bad name (' + JSON.stringify(bad).slice(0, 16) + ') goes nowhere and says why', env2.assigned.length === 0 && env2.$('name-msg').textContent.length > 8);
}
{   // a bad name typed after the room is made starts no seat
    const env = runLobby('', {});
    env.$('create').click();
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
    const env5 = runLobby('', {});
    env5.type('player-name', 'Ann & <b>Bob</b>');
    env5.type('join-code', 'ABC');
    env5.$('join-go').click();
    check('a name with < > & goes into the address URL-encoded', env5.assigned.length === 1 && env5.assigned[0].indexOf('name=Ann%20%26%20%3Cb%3EBob%3C%2Fb%3E') !== -1 && env5.param(env5.assigned[0], 'name') === 'Ann & <b>Bob</b>', env5.assigned[0]);
}

// ---- a name with < > & is text, never markup
{
    const evil = '<img src=x onerror=alert(1)>&amp;"\'';
    const env = runLobby('', {});
    env.type('player-name', evil.slice(0, 32));
    const typed = evil.slice(0, 32);
    env.$('create').click();
    env.rowButton(0, 'Play here').click();
    env.rowButton(1, 'Open a window').click();
    env.$('all-here').click();
    check('the page writes no markup at all in a whole session (no innerHTML)', env.innerHTMLWrites.length === 0, env.innerHTMLWrites.slice(0, 2).join(' | '));
    const cell0 = env.$('grid').children.find((c) => c.getAttribute('data-seat') === '0');
    check('the name shows as text in the frame\'s label and in the row', cell0.textContent.indexOf(typed) !== -1 && env.rows()[0].textContent.indexOf(typed) !== -1);
    check('no element of the page was made from the name\'s characters (a < in a name makes no tag)', cell0.findAll((e) => e.tagName === 'img').length === 0 && env.rows()[0].findAll((e) => e.tagName === 'img').length === 0);
    check('the frame\'s address carries the name encoded: no raw < or > or quote in it', env.frameOf(0).src.indexOf('<') === -1 && env.frameOf(0).src.indexOf('>') === -1 && env.param(env.frameOf(0).src, 'name') === typed, env.frameOf(0).src);
}
check('the page assigns no innerHTML anywhere', !/\.innerHTML\s*[+]?=/.test(lobbyText) && !/document\.write/.test(lobbyText) && !/insertAdjacentHTML/.test(lobbyText));

// ---- a link of the page that asks for a match: the name is asked first, every time, and nothing starts before
{
    const env = runLobby('?room=demo-small-2p-abc12', { 'ants.name': 'Maya' });
    check('a shared room link: the step is up, with the remembered name in the field and a Join button', !env.$('who-go').hidden && env.$('who-go').textContent === 'Join' && !env.$('who-title').hidden && /demo-small-2p-abc12/.test(env.$('who-title').textContent) && env.$('player-name').value === 'Maya');
    check('... nothing starts before the button: no room panel, no frame, the address is not rewritten', !env.roomStarted() && env.frames().length === 0 && env.replaced.length === 0 && env.opened.length === 0 && env.$('setup').hidden && env.$('join').hidden);
    env.type('player-name', 'Bot (x)');
    env.$('who-go').click();
    check('... a bad name does not start it and says why', !env.roomStarted() && env.$('name-msg').textContent.length > 8);
    env.type('player-name', 'Zed');
    env.$('player-name').key('Enter');
    check('... Enter with a good name does: the room panel opens (the address is rewritten to the room), the step goes away', env.roomStarted() && env.replaced.length === 1 && env.$('who-go').hidden && env.$('who-title').hidden && env.storage.data['ants.name'] === 'Zed');
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
    check('a link that hosts a match on a map asks for the name first too (button: Host)', !env.$('who-go').hidden && env.$('who-go').textContent === 'Host' && !env.roomStarted() && env.replaced.length === 0);
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
    check('an address that is no room code is the plain page (nothing is asked)', !env.$('setup').hidden && !env.$('join').hidden && env.$('who-go').hidden && !env.roomStarted());
}
{
    const env = runLobby('?aspect=4:3', {});
    check('an address with only a picture is the plain page', !env.$('setup').hidden && env.$('who-go').hidden);
}

// ---- the front page: a first visit, and single player (the owner: "you can play single on the play online tab by setting to 1 player"); one level for each of the three other bases and the Teams
const SEAT_IDS = ['opponent-1', 'opponent-2', 'opponent-3'];
const seatLevels = (env) => SEAT_IDS.map((id) => env.$(id).value);
const setSeats = (env, levels) => levels.forEach((level, i) => { env.$(SEAT_IDS[i]).value = level; env.$(SEAT_IDS[i]).fire('change', {}); });
const teamChoices = (env) => env.$('teams').children.map((o) => [o.value, o.textContent]);
const FFA_ONLY = [['ffa', 'Free for all']];
const ALL_TEAMS = [['ffa', 'Free for all'], ['0+1', 'You + Red'], ['0+2', 'You + Blue'], ['0+3', 'You + Black']];
{
    const env = runLobby('', {}, { firstVisit: true });
    same('a first visit: 1 player, Treasure, Medium in all three bases, and the button reads Play', [env.$('players').value, env.$('map').value, seatLevels(env), env.$('create').textContent], ['1', 'treasure', ['medium', 'medium', 'medium'], 'Play']);
    same('... the row of the opponents is shown (not the empty seats\') and so is the single player\'s hint', [env.$('solo-line').hidden, env.$('fill-line').hidden, env.$('setup-hint-solo').hidden, env.$('setup-hint').hidden], [false, true, false, true]);
    same('... with three bots the Teams select is shown: free for all (chosen), and the player with each bot', [env.$('teams-line').hidden, teamChoices(env), env.$('teams').value], [false, ALL_TEAMS, 'ffa']);
    check('... the choice of the empty seats of a room is what it always was: leave empty', env.$('fill').value === '');
    check('... the front page shows the setup, the join form and the one name field', !env.$('setup').hidden && !env.$('join').hidden && !!env.$('player-name') && env.$('who-go').hidden);
    check('nothing was written by merely loading', Object.keys(env.storage.data).length === 0, JSON.stringify(env.storage.data));
    env.$('create').click();
    check('Play: this tab goes to the game page of this computer (one assignment, no window opened)', env.assigned.length === 1 && env.opened.length === 0, JSON.stringify(env.assigned));
    const url = new URL(env.assigned[0] || 'https://x/');
    check('... /play.html, the map, the Medium bots (one word: all three alike), no teams, the name Player and the 16:9 picture', url.pathname === '/play.html' && env.param(url.href, 'map') === 'treasure' && env.param(url.href, 'bots') === 'medium' && env.param(url.href, 'teams') === null && env.param(url.href, 'name') === 'Player' && env.param(url.href, 'aspect') === '16:9', url.href);
    check('... no room, no server, no seat, no frame (nothing of a match on the game server)', ['join', 'room', 'seat', 'embed', 'fill'].every((k) => env.param(url.href, k) === null) && !env.roomStarted() && env.replaced.length === 0 && env.frames().length === 0);
    same('... and the choices are remembered: the map, one player, the three levels, the teams (the multiplayer choice of bots is not touched, and the old key is never written)',
         [env.storage.data['ants-four-map'], env.storage.data['ants-four-players'], env.storage.data['ants-solo-seats'], env.storage.data['ants-solo-teams'], 'ants-solo-bots' in env.storage.data, 'ants-four-fill' in env.storage.data], ['treasure', '1', 'medium,medium,medium', 'ffa', false, false]);
}
{
    const env = runLobby('', { 'ants-four-map': 'islands' }, { firstVisit: true });
    env.type('player-name', '  Bob  ');
    setSeats(env, ['', '', '']);
    same('Opponents: None in all three bases: no teams to choose', [env.$('teams-line').hidden, teamChoices(env)], [true, FFA_ONLY]);
    env.$('create').click();
    const u = env.assigned[0] || '';
    check('... a typed name, another map: no bots parameter (alone, as in the original), no teams, the name, the map', env.param(u, 'bots') === null && env.param(u, 'teams') === null && env.param(u, 'name') === 'Bob' && env.param(u, 'map') === 'islands', u);
    same('... "none" three times is what the browser remembers for it, and the name too', [env.storage.data['ants-solo-seats'], env.storage.data['ants.name']], ['none,none,none', 'Bob']);
    const again = runLobby('', env.storage.data, { firstVisit: true });
    same('the next visit comes back with those choices: None in all three (not Medium), Islands, 1 player, the name', [seatLevels(again), again.$('map').value, again.$('players').value, again.$('player-name').value], [['', '', ''], 'islands', '1', 'Bob']);
}
{   // a level for each base, and the team: the player with Black
    const env = runLobby('', {}, { firstVisit: true });
    setSeats(env, ['easy', '', 'hard']);
    same('a level for each base (Red Easy, Blue None, Black Hard): the Teams select offers the player with the two bots that play', [env.$('teams-line').hidden, teamChoices(env), env.$('teams').value], [false, [['ffa', 'Free for all'], ['0+1', 'You + Red'], ['0+3', 'You + Black']], 'ffa']);
    env.$('teams').value = '0+3';
    env.$('teams').fire('change', {});
    env.$('create').click();
    const u = env.assigned[0] || '';
    check('Play: the three words for seats 1, 2, 3 and the team (the + is encoded: it is read back as 0+3)', env.param(u, 'bots') === 'easy,none,hard' && env.param(u, 'teams') === '0+3' && /[?&]teams=0%2B3(&|$)/.test(u), u);
    same('... remembered: the levels and the team', [env.storage.data['ants-solo-seats'], env.storage.data['ants-solo-teams']], ['easy,none,hard', '0+3']);
    const again = runLobby('', env.storage.data, { firstVisit: true });
    same('the next visit shows them again: the levels, the team chosen, the same game', [seatLevels(again), again.$('teams').value, again.$('teams-line').hidden, (again.$('create').click(), again.param(again.assigned[0], 'bots')), again.param(again.assigned[0], 'teams')], [['easy', '', 'hard'], '0+3', false, 'easy,none,hard', '0+3']);
}
{   // a team that the bots no longer allow is free for all, and stays so
    const env = runLobby('', { 'ants-solo-seats': 'easy,medium,hard', 'ants-solo-teams': '0+1' }, { firstVisit: true });
    same('a remembered team that the remembered bots allow is chosen', [env.$('teams').value, env.$('teams-line').hidden], ['0+1', false]);
    setSeats(env, ['', 'medium', 'hard']);
    same('Red set to None: "You + Red" is gone and the team is free for all', [teamChoices(env), env.$('teams').value], [[['ffa', 'Free for all'], ['0+2', 'You + Blue'], ['0+3', 'You + Black']], 'ffa']);
    setSeats(env, ['easy', 'medium', 'hard']);
    same('... and it stays free for all when Red comes back (the player chooses a team again)', [teamChoices(env), env.$('teams').value], [ALL_TEAMS, 'ffa']);
    setSeats(env, ['', '', 'hard']);
    same('one bot only: nothing to choose, the Teams select is hidden', [env.$('teams-line').hidden, teamChoices(env)], [true, FFA_ONLY]);
    env.$('create').click();
    const u = env.assigned[0] || '';
    check('... and Play carries no teams', env.param(u, 'bots') === 'none,none,hard' && env.param(u, 'teams') === null, u);
}
{   // a remembered team that is no choice (a seat with no bot, a text that is no team) is free for all
    for (const [seats, team] of [['easy,none,hard', '0+2'], ['easy,medium,none', '0+3'], ['none,none,hard', '0+3'], ['easy,medium,hard', '0+4'], ['easy,medium,hard', '1+2'], ['easy,medium,hard', 'junk'], ['easy,medium,hard', '0 1']]) {
        const env = runLobby('', { 'ants-solo-seats': seats, 'ants-solo-teams': team }, { firstVisit: true });
        check('remembered bots ' + seats + ' and team ' + JSON.stringify(team) + ': the team is free for all, and Play carries none', env.$('teams').value === 'ffa' && (env.$('create').click(), env.param(env.assigned[0], 'teams') === null), env.$('teams').value);
    }
}
for (const level of ['easy', 'medium', 'hard']) {
    const env = runLobby('', { 'ants-solo-bots': level }, { firstVisit: true });
    check('the opponents that the first versions remembered (' + level + ') are shown in all three bases and played', seatLevels(env).every((l) => l === level) && (env.$('create').click(), env.param(env.assigned[0], 'bots') === level));
    same('... the old key is left as it was, and the new one holds what was played', [env.storage.data['ants-solo-bots'], env.storage.data['ants-solo-seats']], [level, [level, level, level].join(',')]);
}
{
    const env = runLobby('', { 'ants-solo-bots': 'none' }, { firstVisit: true });
    same('the old key\'s none is None in all three bases', seatLevels(env), ['', '', '']);
}
for (const junk of ['extreme', 'MEDIUM2', '1', ' hard']) {
    const env = runLobby('', { 'ants-solo-bots': junk }, { firstVisit: true });
    check('a remembered value of the old key that is no level (' + JSON.stringify(junk) + ') is None, not a bot', seatLevels(env).every((l) => l === ''));
}
{
    const env = runLobby('', { 'ants-solo-seats': 'hard,none,easy', 'ants-solo-bots': 'medium' }, { firstVisit: true });
    same('the new key beats the old one', seatLevels(env), ['hard', '', 'easy']);
}
for (const junk of ['easy', 'easy,medium', 'easy,medium,hard,easy', 'easy,,hard', 'easy,medium,extreme', 'EXTREME', '']) {
    const env = runLobby('', { 'ants-solo-seats': junk, 'ants-solo-bots': 'hard' }, { firstVisit: true });
    check('a remembered ants-solo-seats that is not three levels (' + JSON.stringify(junk) + ') is not used: the old key (Hard) is', seatLevels(env).every((l) => l === 'hard'), JSON.stringify(seatLevels(env)));
}
{
    const env = runLobby('', { 'ants-four-players': '3', 'ants-four-fill': 'easy', 'ants-solo-bots': 'hard' }, { firstVisit: true });
    same('a browser that chose 3 players, Easy empty seats and Hard solo opponents: the form is the room\'s (Create the match, the empty seats row), each choice in its own row', [env.$('players').value, env.$('create').textContent, env.$('fill').value, seatLevels(env), env.$('fill-line').hidden, env.$('solo-line').hidden, env.$('teams-line').hidden], ['3', 'Create the match', 'easy', ['hard', 'hard', 'hard'], false, true, true]);
    env.$('players').value = '1';
    env.$('players').fire('change', {});
    same('... setting the players to 1 turns it into the single player\'s: Play, Hard in all three bases, the Teams (the empty seats\' choice stays Easy)', [env.$('create').textContent, seatLevels(env), env.$('fill').value, env.$('solo-line').hidden, env.$('fill-line').hidden, env.$('teams-line').hidden], ['Play', ['hard', 'hard', 'hard'], 'easy', false, true, false]);
    env.$('players').value = '4';
    env.$('players').fire('change', {});
    same('... and back to 4 the room\'s again (no Teams)', [env.$('create').textContent, env.$('fill').value, env.$('setup-hint').hidden, env.$('setup-hint-solo').hidden, env.$('teams-line').hidden], ['Create the match', 'easy', false, true, true]);
    env.$('create').click();
    check('... and Create the match makes the room (the empty seats\' bots Easy in the code\'s links), as ever', env.roomStarted() && env.assigned.length === 0 && /fill=easy/.test(env.$('any-link').value) && /room=demo-treasure-4p-/.test(env.$('any-link').value), env.$('any-link').value);
    same('... and remembers the room\'s choices without touching the opponents\' (the old key still Hard, nothing written for the new ones)', [env.storage.data['ants-four-players'], env.storage.data['ants-four-fill'], env.storage.data['ants-solo-bots'], 'ants-solo-seats' in env.storage.data, 'ants-solo-teams' in env.storage.data], ['4', 'easy', 'hard', false, false]);
}
for (const bad of ['Bot (Medium)', 'Zoë', 'x'.repeat(33)]) {
    const env = runLobby('', {}, { firstVisit: true });
    env.type('player-name', bad);
    env.$('create').click();
    check('Play with a bad name (' + JSON.stringify(bad).slice(0, 16) + ') goes nowhere and says why under the field', env.assigned.length === 0 && env.$('name-msg').textContent.length > 8 && env.$('player-name').getAttribute('aria-invalid') === 'true');
}
{
    const env = runLobby('', THROWS, { firstVisit: true });
    env.$('create').click();
    check('a browser that refuses its storage still plays (nothing is remembered, nothing breaks)', env.assigned.length === 1 && env.param(env.assigned[0], 'bots') === 'medium');
}
{
    const env = runLobby('?aspect=4:3', {}, { firstVisit: true });
    env.$('create').click();
    check('the picture of the page goes with the game: ?aspect=4:3 gives aspect=4:3', env.param(env.assigned[0], 'aspect') === '4:3', env.assigned[0]);
    const remembered = runLobby('', { 'ants.aspect.v2': '4:3' }, { firstVisit: true });
    remembered.$('create').click();
    check('... so does the choice that the selector remembered', remembered.param(remembered.assigned[0], 'aspect') === '4:3', remembered.assigned[0]);
}
// ... the addresses: ?map=...&players=1 plays on this computer after the name step; an address that names no players still hosts 4
{
    const env = runLobby('?map=small&players=1&fill=hard', { 'ants.name': 'Maya' }, { firstVisit: true });
    check('?map=small&players=1&fill=hard asks for the name first (button Play), and nothing starts before', !env.$('who-go').hidden && env.$('who-go').textContent === 'Play' && /computer/.test(env.$('who-title').textContent) && env.assigned.length === 0);
    env.$('who-go').click();
    const u = env.assigned[0] || '';
    check('... then the game of this computer on that map with the Hard bots and the name', env.assigned.length === 1 && env.param(u, 'map') === 'small' && env.param(u, 'bots') === 'hard' && env.param(u, 'name') === 'Maya', u);
}
{   // an address names one level for all three bases and no teams: that game is played so, and what the form remembered for the Teams is not touched (the form's own Play writes it)
    const env = runLobby('?map=small&players=1&fill=hard', { 'ants.name': 'Maya', 'ants-solo-seats': 'easy,medium,hard', 'ants-solo-teams': '0+1' }, { firstVisit: true });
    env.$('who-go').click();
    const u = env.assigned[0] || '';
    check('an address with &fill=hard: Hard in all three bases, no teams', env.param(u, 'bots') === 'hard' && env.param(u, 'teams') === null, u);
    same('... the levels that were played are remembered, the team that the form remembered is left as it was', [env.storage.data['ants-solo-seats'], env.storage.data['ants-solo-teams']], ['hard,hard,hard', '0+1']);
}
{
    const env = runLobby('?map=treasure&players=1&play=here', {}, { firstVisit: true });
    check('the test mode ?play=here asks nobody: ?map=...&players=1 plays at once', env.assigned.length === 1 && env.$('who-go').hidden);
    const room = runLobby('?map=small', {}, { firstVisit: true });
    check('an address that names a map and no players hosts 4, as it always did (after the name step)', !room.$('who-go').hidden && room.$('who-go').textContent === 'Host');
    room.$('who-go').click();
    check('... the room of 4 on that map', room.roomStarted() && /demo-small-4p-/.test(room.$('room-code').textContent) && room.assigned.length === 0);
}
// ... the host's own seat in this tab: the address that Join makes, with the bots of the leader's START
{
    const env = runLobby('', {}, {});
    env.$('players').value = '3';
    env.$('players').fire('change', {});
    env.$('fill').value = 'medium';
    env.$('fill').fire('change', {});
    env.type('player-name', 'Ann');
    env.$('create').click();
    check('Create the match with 3 players makes the room panel, and the play-here note of the frames is not shown yet', env.roomStarted() && env.$('frames-note').hidden === true);
    env.$('play-tab').click();
    const u = env.assigned[0] || '';
    check('"Play in this tab": this tab goes to the game page with the room, the bots of the leader\'s START and the name (no seat, no embed, no new window)',
          env.assigned.length === 1 && env.opened.length === 0 && env.param(u, 'join') === '/ws' && env.param(u, 'room') === env.$('room-code').textContent && env.param(u, 'fill') === 'medium' && env.param(u, 'name') === 'Ann' && env.param(u, 'seat') === null && env.param(u, 'embed') === null, u);
    const bad = runLobby('', {}, {});
    bad.$('create').click();
    bad.type('player-name', 'Bot (x)');
    bad.$('play-tab').click();
    check('... a bad name starts nothing', bad.assigned.length === 0 && bad.$('name-msg').textContent.length > 8);
    const shown = runLobby('', {}, {});
    shown.$('create').click();
    shown.rowButton(1, 'Play here').click();
    check('a game that plays on the page shows the note about the frames', shown.$('frames-note').hidden === false);
}
check('the front page says no more that the page opens a window by default (its only window.open is the seat\'s explicit one)', (lobbyText.match(/window\.open\(/g) || []).length === 1);

console.log('web name check: ' + checks + ' checks, ' + failures + ' failures');
process.exit(failures === 0 ? 0 : 1);
