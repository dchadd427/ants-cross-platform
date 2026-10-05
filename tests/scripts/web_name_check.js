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
    const env = { innerHTMLWrites: [], assigned: [], replaced: [], opened: [], copied: [], elements: {}, focused: null, confirms: [] };
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
    // the site's answers (options.fetch(url, init) -> a promise of a response; the default is no network at all; options.noFetch is a browser that has no fetch)
    env.fetches = [];
    const network = options.fetch || (() => Promise.reject(new Error('the test has no network')));
    const fetchSpy = options.noFetch ? undefined : (url, init) => { env.fetches.push({ url, init }); return network(url, init); };
    new Function('window', 'document', 'history', 'navigator', 'setInterval', 'setTimeout', 'fetch', code)(win, doc, history, nav, function () { return 0; }, function () { return 0; }, fetchSpy);
    env.doc = doc;
    env.storage = storage;
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
    env.hostPlayers = (n) => env.pick('players-' + n);
    env.hostPlayersNow = () => [2, 3, 4].filter((n) => env.$('players-' + n).checked).join('+');
    // The Host card starts with 2 players: the tests below that host a match with four seats choose 4 first, as a person does; options.firstVisit keeps the page as it comes
    if (!options.firstVisit) env.hostPlayers(4);
    return env;
}
function randomName(n) { return NAMES_OF_THE_PAGE.indexOf(n) !== -1; }

// ---- the plain page (nothing in the address)
{
    const env = runLobby('', {});
    check('the page has ONE name field, near the top, in the same box for Host and Join', !!env.$('player-name') && !env.$('join-name') && env.$('who') && !env.$('who').hidden);
    check('nothing is asked first: the two cards and "How it works" are there, the step\'s button is not', !env.$('cards').hidden && !env.$('how').hidden && env.$('who-go').hidden && env.$('who-title').hidden);
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
        env.$('host').click();
        check('... and still hosts (nothing is remembered, nothing breaks)', env.roomStarted());
    }
}

// ---- hosting: the typed name goes to the first seat of the page; the others keep random names
{
    const env = runLobby('', { 'ants-four-map': 'small' });
    env.type('player-name', '  Alice  ');
    env.$('host').click();
    check('Create with a good name makes the room', env.roomStarted() && env.$('player-name').value === '  Alice  ');
    check('... and the room takes the place of the two cards and of "How it works"', env.$('cards').hidden && env.$('how').hidden && !env.$('room-panel').hidden);
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
    const env = runLobby('', {});
    env.type('player-name', 'Zed');
    env.$('host').click();
    env.rowButton(3, 'Open a window').click();
    check('a window can be the first seat: it takes the name', env.param(env.opened[0].url, 'name') === 'Zed' && env.rows()[3].children[0].textContent.indexOf('Zed') !== -1);
    env.rowButton(0, 'Play here').click();
    check('... and then a frame is random', randomName(env.param(env.frameOf(0).src, 'name')));
}
{   // "All seats on this page": only the first seat takes the name
    const env = runLobby('', {});
    env.type('player-name', 'Alice');
    env.$('host').click();
    env.$('all-here').click();
    const urls = [0, 1, 2, 3].map((s) => env.frameOf(s).src);
    check('all seats on this page: seat 0 plays as Alice, the others have random names, all different', env.param(urls[0], 'name') === 'Alice' && urls.slice(1).every((u) => randomName(env.param(u, 'name'))) && new Set(urls.slice(1).map((u) => env.param(u, 'name'))).size === 3);
}
{   // the random fallback
    const env = runLobby('', {});
    env.$('host').click();
    env.$('all-here').click();
    const got = [0, 1, 2, 3].map((s) => env.param(env.frameOf(s).src, 'name'));
    check('an empty field: every seat has a random name, as before', got.every(randomName) && new Set(got).size === 4, got.join(','));
    check('... and an empty field is remembered as none', env.storage.data['ants.name'] === '');
}

// ---- bad names start nothing
for (const bad of ['Bot (Medium)', ' bOt(x', 'Zoë', '名前', 'x'.repeat(33), 'a\u0001b']) {
    const env = runLobby('', {});
    env.type('player-name', bad);
    env.$('host').click();
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
    env.$('host').click();
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
    const env = runLobby('', {});
    env.type('player-name', evil.slice(0, 32));
    const typed = evil.slice(0, 32);
    env.$('host').click();
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
    check('... nothing starts before the button: no room panel, no frame, the address is not rewritten', !env.roomStarted() && env.frames().length === 0 && env.replaced.length === 0 && env.opened.length === 0 && env.$('cards').hidden && env.$('how').hidden);
    check('... the name step is the full page, not a room (the room class comes with the room)', !env.body.classList.contains('in-room'));
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
    check('an address that is no room code is the plain page (nothing is asked)', !env.$('cards').hidden && !env.$('how').hidden && env.$('who-go').hidden && !env.roomStarted());
}
{
    const env = runLobby('?aspect=4:3', {});
    check('an address with only a picture is the plain page', !env.$('cards').hidden && env.$('who-go').hidden);
}

// ---- the front page: two cards. "Play vs the computer" (the owner: "you can play single on the play online tab by setting to 1 player": it has a card of its own now): a map with its preview, a group of four buttons
// (None, Easy, Medium, Hard) for each of the three other bases, and the Teams. "Play online": host (a map, 2 - 4 players, the empty seats at START) and join (a code)
const lobbyScript = (() => { const all = [...lobbyText.matchAll(/<script>([\s\S]*?)<\/script>/g)]; return all[all.length - 1][1]; })();
const LEVEL_WORDS = ['none', 'easy', 'medium', 'hard'];
const levelId = (seat, word) => 'opponent-' + seat + '-' + (word || 'none');
// the level that a seat's group shows: the one button that is checked ('?n' when it is not exactly one)
const seatLevels = (env) => [1, 2, 3].map((seat) => { const on = LEVEL_WORDS.filter((w) => env.$(levelId(seat, w)).checked); return on.length === 1 ? env.$(levelId(seat, on[0])).value : '?' + on.length; });
// the person clicks the button of a level in each seat's group (a word that is none: the first button)
const setSeats = (env, levels) => levels.forEach((level, i) => env.pick(levelId(i + 1, level)));
const teamChoices = (env) => env.$('teams').children.map((o) => [o.value, o.textContent]);
// the Host card's empty seats (protocol 13): a group of four buttons for each seat after the leader's (host-seat-N-<word>), shown for the seats that the room has, and the Teams select of the room
const hostId = (seat, word) => 'host-seat-' + seat + '-' + (word || 'none');
const hostLevels = (env) => [1, 2, 3].map((seat) => { const on = LEVEL_WORDS.filter((w) => env.$(hostId(seat, w)).checked); return on.length === 1 ? env.$(hostId(seat, on[0])).value : '?' + on.length; });
const setHostSeats = (env, levels) => levels.forEach((level, i) => env.pick(hostId(i + 1, level)));
const hostRowsShown = (env) => [1, 2, 3].filter((seat) => !env.$('host-seat-row-' + seat).hidden).join('+');
const hostTeamChoices = (env) => env.$('host-teams').children.map((o) => [o.value, o.textContent]);
const FOUR_TEAMS = [['ffa', 'Free for all'], ['0+1', 'Green + Red against Blue + Black'], ['0+2', 'Green + Blue against Red + Black'], ['0+3', 'Green + Black against Red + Blue']];
const THREE_TEAMS = [['ffa', 'Free for all'], ['0+1', 'Green + Red against Blue'], ['0+2', 'Green + Blue against Red'], ['1+2', 'Red + Blue against Green']];
const FFA_ONLY = [['ffa', 'Free for all']];
const ALL_TEAMS = [['ffa', 'Free for all'], ['0+1', 'You + Red'], ['0+2', 'You + Blue'], ['0+3', 'You + Black']];
const MAP_KEYS = ['tiny', 'small', 'medium', 'gauntlet', 'treasure', 'islands'];
{
    const env = runLobby('', {}, { firstVisit: true });
    same('a first visit: Treasure on both cards, Medium in all three bases, 2 players on the Host card and no bots for its empty seats', [env.$('map-solo').value, env.$('map-host').value, seatLevels(env), env.hostPlayersNow(), hostLevels(env)], ['treasure', 'treasure', ['medium', 'medium', 'medium'], '2', ['', '', '']]);
    same('... the Host card has the Red seat only (two players) and no Teams line: a team of two would end the match at once', [hostRowsShown(env), env.$('host-teams-line').hidden, hostTeamChoices(env)], ['1', true, [['ffa', 'Free for all']]]);
    same('... START (named for a screen reader: the button is the original\'s own picture), the two cards and "How it works" all there at once', [env.$('play').getAttribute('aria-label'), env.$('cards').hidden, env.$('how').hidden, env.$('who-go').hidden], ['Start the game', false, false, true]);
    check('... and the Host card\'s button reads Host the match', /<button id="host"[^>]*>Host the match<\/button>/.test(lobbyText));
    same('... with three bots the Teams select is shown: free for all (chosen), and the player with each bot', [env.$('teams-line').hidden, teamChoices(env), env.$('teams').value], [false, ALL_TEAMS, 'ffa']);
    same('... the preview and the Map Info line are those of Treasure', [env.$('map-preview').src, env.$('map-info').textContent], ['front/preview_treasure.png', "One person's trash... (12 min)"]);
    check('... the front page shows both cards and the one name field', !env.$('cards').hidden && !!env.$('player-name') && !!env.$('join-code'));
    check('nothing was written by merely loading', Object.keys(env.storage.data).length === 0, JSON.stringify(env.storage.data));
    env.$('play').click();
    check('START: this tab goes to the game page of this computer (one assignment, no window opened)', env.assigned.length === 1 && env.opened.length === 0, JSON.stringify(env.assigned));
    const url = new URL(env.assigned[0] || 'https://x/');
    check('... /play.html, the map, the Medium bots (one word: all three alike), no teams, the name Player and the 16:9 picture', url.pathname === '/play.html' && env.param(url.href, 'map') === 'treasure' && env.param(url.href, 'bots') === 'medium' && env.param(url.href, 'teams') === null && env.param(url.href, 'name') === 'Player' && env.param(url.href, 'aspect') === '16:9', url.href);
    check('... no room, no server, no seat, no frame (nothing of a match on the game server)', ['join', 'room', 'seat', 'embed', 'fill'].every((k) => env.param(url.href, k) === null) && !env.roomStarted() && env.replaced.length === 0 && env.frames().length === 0);
    same('... and the choices are remembered: the map, the three levels, the teams (what the Host card remembers is not touched, and the old key is never written)',
         [env.storage.data['ants-four-map'], env.storage.data['ants-solo-seats'], env.storage.data['ants-solo-teams'], 'ants-solo-bots' in env.storage.data, 'ants-four-fill' in env.storage.data, 'ants-four-teams' in env.storage.data, 'ants-four-players' in env.storage.data], ['treasure', 'medium,medium,medium', 'ffa', false, false, false, false]);
}
{
    const env = runLobby('', { 'ants-four-map': 'islands' }, { firstVisit: true });
    env.type('player-name', '  Bob  ');
    setSeats(env, ['', '', '']);
    same('Opponents: None in all three bases: no teams to choose', [env.$('teams-line').hidden, teamChoices(env)], [true, FFA_ONLY]);
    env.$('play').click();
    const u = env.assigned[0] || '';
    check('... a typed name, another map: no bots parameter (alone, as in the original), no teams, the name, the map', env.param(u, 'bots') === null && env.param(u, 'teams') === null && env.param(u, 'name') === 'Bob' && env.param(u, 'map') === 'islands', u);
    same('... "none" three times is what the browser remembers for it, and the name too', [env.storage.data['ants-solo-seats'], env.storage.data['ants.name']], ['none,none,none', 'Bob']);
    const again = runLobby('', env.storage.data, { firstVisit: true });
    same('the next visit comes back with those choices: None in all three (not Medium), Islands on both cards, the name', [seatLevels(again), again.$('map-solo').value, again.$('map-host').value, again.$('player-name').value], [['', '', ''], 'islands', 'islands', 'Bob']);
}
{   // a level for each base, and the team: the player with Black
    const env = runLobby('', {}, { firstVisit: true });
    setSeats(env, ['easy', '', 'hard']);
    same('a level for each base (Red Easy, Blue None, Black Hard): the Teams select offers the player with the two bots that play', [env.$('teams-line').hidden, teamChoices(env), env.$('teams').value], [false, [['ffa', 'Free for all'], ['0+1', 'You + Red'], ['0+3', 'You + Black']], 'ffa']);
    env.$('teams').value = '0+3';
    env.$('teams').fire('change', {});
    env.$('play').click();
    const u = env.assigned[0] || '';
    check('START: the three words for seats 1, 2, 3 and the team (the + is encoded: it is read back as 0+3)', env.param(u, 'bots') === 'easy,none,hard' && env.param(u, 'teams') === '0+3' && /[?&]teams=0%2B3(&|$)/.test(u), u);
    same('... remembered: the levels and the team', [env.storage.data['ants-solo-seats'], env.storage.data['ants-solo-teams']], ['easy,none,hard', '0+3']);
    const again = runLobby('', env.storage.data, { firstVisit: true });
    same('the next visit shows them again: the levels, the team chosen, the same game', [seatLevels(again), again.$('teams').value, again.$('teams-line').hidden, (again.$('play').click(), again.param(again.assigned[0], 'bots')), again.param(again.assigned[0], 'teams')], [['easy', '', 'hard'], '0+3', false, 'easy,none,hard', '0+3']);
}
{   // the buttons of the levels: each seat's group gives its word of the address (the words for seats 1, 2, 3 = Red, Blue, Black; one word when all three are alike; none of them when all are none), one button checked in each group
    let wrong = 0;
    let sample = '';
    let made = 0;
    for (const a of ['', 'easy', 'medium', 'hard']) for (const b of ['', 'easy', 'medium', 'hard']) for (const c of ['', 'easy', 'medium', 'hard']) {
        const env = runLobby('', {}, { firstVisit: true });
        setSeats(env, [a, b, c]);
        const shown = seatLevels(env);
        env.$('play').click();
        const got = env.param(env.assigned[0] || '', 'bots');
        const want = !(a || b || c) ? null : (a === b && b === c) ? a : [a, b, c].map((l) => l || 'none').join(',');
        made++;
        if (JSON.stringify(shown) !== JSON.stringify([a, b, c]) || got !== want) { wrong++; if (!sample) sample = JSON.stringify([a, b, c]) + ' shows ' + JSON.stringify(shown) + ' and plays bots=' + got + ', wanted ' + want; }
    }
    check('the level buttons: all ' + made + ' choices of the three groups show what was clicked (one button checked in each group) and START plays the address\'s bots= word for them (' + sample + ')', wrong === 0 && made === 64);
    const env = runLobby('', {}, { firstVisit: true });
    setSeats(env, ['hard', 'easy', 'medium']);
    env.pick(levelId(1, 'easy'));
    same('a click on another button of a group moves the check inside that group only', seatLevels(env), ['easy', 'easy', 'medium']);
    const ids = [...lobbyText.matchAll(/<input type="radio" name="(opponent-\d)" id="(opponent-\d-\w+)" value="(\w*)"/g)].map((m) => m.slice(1, 4).join(' '));
    same('the buttons are radio buttons of three groups (names opponent-1 to 3), None, Easy, Medium, Hard in this order (the arrow keys and Tab are the browser\'s own)', ids,
         [1, 2, 3].flatMap((seat) => LEVEL_WORDS.map((w) => 'opponent-' + seat + ' opponent-' + seat + '-' + w + ' ' + (w === 'none' ? '' : w))));
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
    env.$('play').click();
    const u = env.assigned[0] || '';
    check('... and START carries no teams', env.param(u, 'bots') === 'none,none,hard' && env.param(u, 'teams') === null, u);
}
{   // a remembered team that is no choice (a seat with no bot, a text that is no team) is free for all
    for (const [seats, team] of [['easy,none,hard', '0+2'], ['easy,medium,none', '0+3'], ['none,none,hard', '0+3'], ['easy,medium,hard', '0+4'], ['easy,medium,hard', '1+2'], ['easy,medium,hard', 'junk'], ['easy,medium,hard', '0 1']]) {
        const env = runLobby('', { 'ants-solo-seats': seats, 'ants-solo-teams': team }, { firstVisit: true });
        check('remembered bots ' + seats + ' and team ' + JSON.stringify(team) + ': the team is free for all, and START carries none', env.$('teams').value === 'ffa' && (env.$('play').click(), env.param(env.assigned[0], 'teams') === null), env.$('teams').value);
    }
}
for (const level of ['easy', 'medium', 'hard']) {
    const env = runLobby('', { 'ants-solo-bots': level }, { firstVisit: true });
    check('the opponents that the first versions remembered (' + level + ') are shown in all three bases and played', seatLevels(env).every((l) => l === level) && (env.$('play').click(), env.param(env.assigned[0], 'bots') === level));
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
{   // the map of the first card: its preview and its Map Info line, the setup screen's own
    const info = (() => { const m = /var MAP_INFO = \{([\s\S]*?)\};/.exec(lobbyText); return new Function('return {' + m[1] + '};')(); })();
    same('the page has the Map Info line of each of the six maps', Object.keys(info).sort(), MAP_KEYS.slice().sort());
    const env = runLobby('', {}, { firstVisit: true });
    let wrong = 0;
    let sample = '';
    for (const key of MAP_KEYS) {
        env.$('map-solo').value = key;
        env.$('map-solo').fire('change', {});
        const alt = env.$('map-preview').getAttribute('alt');
        if (env.$('map-preview').src !== 'front/preview_' + key + '.png' || env.$('map-info').textContent !== info[key] || alt.toLowerCase().indexOf(key) === -1) { wrong++; if (!sample) sample = key + ': ' + env.$('map-preview').src + ' / ' + env.$('map-info').textContent + ' / ' + alt; }
    }
    check('choosing a map on the first card shows its own preview, Map Info line and a text for it (' + sample + ')', wrong === 0);
    check('... and the Host card\'s map is its own choice (it did not move)', env.$('map-host').value === 'treasure');
    const remembered = runLobby('', { 'ants-four-map': 'small' }, { firstVisit: true });
    same('a remembered map is on both cards and in the preview from the start', [remembered.$('map-solo').value, remembered.$('map-host').value, remembered.$('map-preview').src, remembered.$('map-info').textContent], ['small', 'small', 'front/preview_small.png', info.small]);
    const both = runLobby('', {}, { firstVisit: true });
    both.$('map-solo').value = 'tiny';
    both.$('map-solo').fire('change', {});
    both.$('play').click();
    same('START remembers the map of the first card, Host the map of the second (one key: the map that was played last)', [both.param(both.assigned[0], 'map'), both.storage.data['ants-four-map']], ['tiny', 'tiny']);
    both.$('map-host').value = 'gauntlet';
    both.$('host').click();
    same('... the room is made on the map of the Host card', [/room=demo-gauntlet-2p-/.test(both.$('any-link').value), both.storage.data['ants-four-map']], [true, 'gauntlet']);
}
{   // the Host card's players: the buttons 2 - 4; what an earlier page remembered for its Players select (1 - 4) is read as 2 - 4: a 1 was the game on this computer, which has its own card now
    for (const [stored, want] of [['1', '2'], ['2', '2'], ['3', '3'], ['4', '4'], ['0', '2'], ['5', '2'], ['junk', '2'], ['', '2'], [null, '2']]) {
        const env = runLobby('', stored === null ? {} : { 'ants-four-players': stored }, { firstVisit: true });
        const shown = env.hostPlayersNow();
        env.$('host').click();
        check('a remembered ' + JSON.stringify(stored) + ' players: the Host card shows ' + want + ', makes the room of ' + want + ' and remembers ' + want, shown === want && new RegExp('room=demo-treasure-' + want + 'p-').test(env.$('any-link').value) && env.storage.data['ants-four-players'] === want,
              shown + ' ' + env.$('any-link').value + ' ' + env.storage.data['ants-four-players']);
    }
    const env = runLobby('', { 'ants-four-players': '3' }, { firstVisit: true });
    env.$('play').click();
    check('a game against the computer leaves the Host card\'s players as they were', env.storage.data['ants-four-players'] === '3' && env.hostPlayersNow() === '3');
    const ids = [...lobbyText.matchAll(/<input type="radio" name="players" id="(players-\d)" value="(\d)"/g)].map((m) => m[1] + ' ' + m[2]);
    same('the players are three radio buttons, 2, 3 and 4', ids, ['players-2 2', 'players-3 3', 'players-4 4']);
}
{
    const env = runLobby('', { 'ants-four-players': '3', 'ants-four-fill': 'easy', 'ants-solo-bots': 'hard' }, { firstVisit: true });
    same('a browser that chose 3 players, Easy empty seats (the one word of the first versions: every seat) and Hard solo opponents: each card shows its own choice (Host: 3 players, Red and Blue Easy and the room\'s Teams; the first card: Hard in all three bases and the Teams)',
         [env.hostPlayersNow(), hostLevels(env), hostRowsShown(env), env.$('host-teams-line').hidden, seatLevels(env), env.$('teams-line').hidden], ['3', ['easy', 'easy', 'easy'], '1+2', false, ['hard', 'hard', 'hard'], false]);
    env.$('host').click();
    check('Host the match makes the room of 3 (the empty seats\' bots Easy in the code\'s links), as ever', env.roomStarted() && env.assigned.length === 0 && /fill=easy/.test(env.$('any-link').value) && /room=demo-treasure-3p-/.test(env.$('any-link').value), env.$('any-link').value);
    same('... and remembers the Host card\'s choices (four words for the seats, the room\'s team) without touching the opponents\' (the old key still Hard, nothing written for the new ones)', [env.storage.data['ants-four-players'], env.storage.data['ants-four-fill'], env.storage.data['ants-four-teams'], env.storage.data['ants-solo-bots'], 'ants-solo-seats' in env.storage.data, 'ants-solo-teams' in env.storage.data], ['3', 'none,easy,easy,easy', 'ffa', 'hard', false, false]);
}
{   // the Host card (protocol 13): a group for each seat after the leader's that the room has, the room's Teams, and what they make of the room's links, address and panel
    const env = runLobby('', {}, { firstVisit: true });
    same('two players: only the Red seat has a group and there is no Teams line (a team of two would end the match at once)', [hostRowsShown(env), env.$('host-teams-line').hidden, hostTeamChoices(env)], ['1', true, [['ffa', 'Free for all']]]);
    env.hostPlayers(3);
    same('three players: Red and Blue, and the Teams of three (the third seat plays alone)', [hostRowsShown(env), env.$('host-teams-line').hidden, hostTeamChoices(env), env.$('host-teams').value], ['1+2', false, THREE_TEAMS, 'ffa']);
    env.hostPlayers(4);
    same('four players: all three seats, and the Teams of four (Green with Red, Blue or Black)', [hostRowsShown(env), env.$('host-teams-line').hidden, hostTeamChoices(env), env.$('host-teams').value], ['1+2+3', false, FOUR_TEAMS, 'ffa']);
    same('the first visit leaves every seat empty (the choice of the leader\'s START is made on the card)', hostLevels(env), ['', '', '']);
    env.$('host-teams').value = '0+1';
    env.$('host-teams').fire('change', {});
    env.hostPlayers(3);
    same('a team that the room of three offers too stays when the players change (Green + Red against Blue)', [env.$('host-teams').value, hostRowsShown(env)], ['0+1', '1+2']);
    env.$('host-teams').value = '1+2';
    env.$('host-teams').fire('change', {});
    env.hostPlayers(4);
    same('a team that the room of four does not offer is free for all again (and stays so)', env.$('host-teams').value, 'ffa');
    env.hostPlayers(3);
    same('... also when the players go back', env.$('host-teams').value, 'ffa');
    setHostSeats(env, ['easy', 'hard', 'medium']);
    env.hostPlayers(2);
    same('the levels of the seats that the room does not have are kept for the next time', [hostRowsShown(env), hostLevels(env)], ['1', ['easy', 'hard', 'medium']]);
}
{   // a room made with a level for each seat and a team: the code's links, the address of the page, the room's panel and what is remembered
    const env = runLobby('', {}, {});
    setHostSeats(env, ['easy', '', 'hard']);
    env.$('host-teams').value = '0+2';
    env.$('host-teams').fire('change', {});
    env.type('player-name', 'Ann');
    env.$('host').click();
    const link = env.$('any-link').value;
    check('Host makes the room of 4', env.roomStarted() && /room=demo-treasure-4p-/.test(link), link);
    same('the link for anybody carries the plan (four words, the leader\'s seat none) and the team (the + is %2B)', [env.param(link, 'fill'), env.param(link, 'teams'), /&teams=0%2B2/.test(link)], ['none,easy,none,hard', '0+2', true]);
    same('the address of the page names the room with both', [String(env.replaced[0][2]).indexOf('fill=none,easy,none,hard') !== -1, String(env.replaced[0][2]).indexOf('teams=0%2B2') !== -1], [true, true]);
    const hint = env.$('fill-hint');
    check('the room panel says the seats and the teams in words', !hint.hidden && /Empty seats at START: Red Easy, Black Hard\./.test(hint.textContent) && /Teams at START: Green \+ Blue against Red \+ Black\./.test(hint.textContent) && /Every link of this room carries the choices/.test(hint.textContent), hint.textContent);
    env.rowButton(1, 'Play here').click();
    const frame = env.frameOf(1);
    same('a seat that plays on the page gets both in its game address', [env.param(frame.src, 'fill'), env.param(frame.src, 'teams')], ['none,easy,none,hard', '0+2']);
    env.rowButton(0, 'Copy link').click();
    same('the link of a seat for somebody else carries both', [env.param(env.copied[0], 'fill'), env.param(env.copied[0], 'teams')], ['none,easy,none,hard', '0+2']);
    env.$('play-tab').click();
    same('"Play in this tab" carries both', [env.param(env.assigned[0], 'fill'), env.param(env.assigned[0], 'teams')], ['none,easy,none,hard', '0+2']);
    same('what is remembered: the four words and the team (the opponents of the first card are not touched)', [env.storage.data['ants-four-fill'], env.storage.data['ants-four-teams'], 'ants-solo-seats' in env.storage.data], ['none,easy,none,hard', '0+2', false]);
    // the same card, 3 players: the seats beyond the room are none in the link, and one level for the seats that the room has is one word
    const three = runLobby('', {}, {});
    three.hostPlayers(3);
    setHostSeats(three, ['hard', 'hard', 'easy']);
    three.$('host').click();
    same('three players, Red and Blue Hard (Black is no seat of the room): one word, no teams parameter for free for all', [three.param(three.$('any-link').value, 'fill'), three.param(three.$('any-link').value, 'teams')], ['hard', null]);
    const mixed = runLobby('', {}, {});
    mixed.hostPlayers(3);
    setHostSeats(mixed, ['easy', 'hard', 'easy']);
    mixed.$('host').click();
    same('three players, Red Easy and Blue Hard: four words with Black none', mixed.param(mixed.$('any-link').value, 'fill'), 'none,easy,hard,none');
    const quiet = runLobby('', {}, {});
    quiet.$('host').click();
    same('no bots and free for all: no fill and no teams in the room\'s links, the address or the hint', [quiet.param(quiet.$('any-link').value, 'fill'), quiet.param(quiet.$('any-link').value, 'teams'), /fill=|teams=/.test(String(quiet.replaced[0][2])), quiet.$('fill-hint').hidden], [null, null, false, true]);
}
{   // what the card remembered: four words, one word (the first versions), a team; anything else is none and free for all
    const four = runLobby('', { 'ants-four-players': '4', 'ants-four-fill': 'none,easy,none,hard', 'ants-four-teams': '0+3' }, { firstVisit: true });
    same('four remembered words and a team: the levels of the seats after the leader\'s and the team are on the card', [hostLevels(four), hostRowsShown(four), four.$('host-teams').value], [['easy', '', 'hard'], '1+2+3', '0+3']);
    const one = runLobby('', { 'ants-four-players': '4', 'ants-four-fill': 'medium' }, { firstVisit: true });
    same('the one word of the first versions is every seat', hostLevels(one), ['medium', 'medium', 'medium']);
    for (const junk of ['easy,hard', 'none,easy,none,loud', 'loud', 'none,easy,none,hard,none', '']) {
        const env = runLobby('', { 'ants-four-players': '4', 'ants-four-fill': junk, 'ants-four-teams': 'x' }, { firstVisit: true });
        same('a remembered ' + JSON.stringify(junk) + ' is none in all three seats, and a team that is none of the choices is free for all', [hostLevels(env), env.$('host-teams').value], [['', '', ''], 'ffa']);
    }
    const stale = runLobby('', { 'ants-four-players': '2', 'ants-four-teams': '0+1' }, { firstVisit: true });
    same('a team that the room of two cannot make is free for all', [stale.$('host-teams-line').hidden, stale.$('host-teams').value], [true, 'ffa']);
}
{   // an address that asks for a room: the plan and the team are read through the whitelist (lower case, valid or nothing) and every link of the room carries them
    const env = runLobby('?room=demo-small-4p-abc12&fill=NONE,Easy,none,hard&teams=0%2B1', {});
    env.$('who-go').click();
    const link = env.$('any-link').value;
    same('?room= &fill= &teams=: the room opens with both, the plan in lower case', [env.roomStarted(), env.param(link, 'fill'), env.param(link, 'teams'), String(env.replaced[0][2]).indexOf('teams=0%2B1') !== -1], [true, 'none,easy,none,hard', '0+1', true]);
    check('... and the panel says them', /Red Easy, Black Hard/.test(env.$('fill-hint').textContent) && /Green \+ Red against Blue \+ Black/.test(env.$('fill-hint').textContent), env.$('fill-hint').textContent);
    const old = runLobby('?room=demo-small-2p-abc12&fill=medium', {});
    old.$('who-go').click();
    same('the old address with one word still opens a room that carries that one word', [old.param(old.$('any-link').value, 'fill'), old.param(old.$('any-link').value, 'teams')], ['medium', null]);
    const bad = runLobby('?room=demo-small-4p-abc12&fill=easy,hard&teams=0%2B0', {});
    bad.$('who-go').click();
    same('a plan of two words and a team of one seat are nothing: no fill and no teams in the links', [bad.param(bad.$('any-link').value, 'fill'), bad.param(bad.$('any-link').value, 'teams'), bad.$('fill-hint').hidden], [null, null, true]);
    const host = runLobby('?map=small&players=3&fill=hard&teams=1%2B2', {});
    host.$('who-go').click();
    same('?map= &players=3 &fill=hard &teams=1+2 hosts the room of three with both (one word for the seats that the room has)', [/room=demo-small-3p-/.test(host.$('any-link').value), host.param(host.$('any-link').value, 'fill'), host.param(host.$('any-link').value, 'teams')], [true, 'hard', '1+2']);
    const far = runLobby('?map=small&players=4&teams=1%2B2', {});
    far.$('who-go').click();
    same('a team that the room of four does not offer is dropped when the address hosts it', far.param(far.$('any-link').value, 'teams'), null);
}
check('"How it works" is a details element that no script opens or closes (the script only hides it with the cards, and shows it with them)', /<details class="how" id="how" hidden>\s*<summary>/.test(lobbyText) && (lobbyScript.match(/\$\('how'\)[.\w]*/g) || []).length === 2 && (lobbyScript.match(/\$\('how'\)[.\w]*/g) || []).every((u) => u === "$('how').hidden") && !/getElementById\('how'\)/.test(lobbyScript));
for (const bad of ['Bot (Medium)', 'Zoë', 'x'.repeat(33)]) {
    const env = runLobby('', {}, { firstVisit: true });
    env.type('player-name', bad);
    env.$('play').click();
    check('START with a bad name (' + JSON.stringify(bad).slice(0, 16) + ') goes nowhere and says why under the field', env.assigned.length === 0 && env.$('name-msg').textContent.length > 8 && env.$('player-name').getAttribute('aria-invalid') === 'true');
    env.type('player-name', bad);
    env.$('host').click();
    check('Host the match with a bad name (' + JSON.stringify(bad).slice(0, 16) + ') makes no room and says why under the field', !env.roomStarted() && env.$('name-msg').textContent.length > 8 && env.$('player-name').getAttribute('aria-invalid') === 'true');
}
{
    const env = runLobby('', THROWS, { firstVisit: true });
    env.$('play').click();
    check('a browser that refuses its storage still plays (nothing is remembered, nothing breaks)', env.assigned.length === 1 && env.param(env.assigned[0], 'bots') === 'medium');
}
{
    const env = runLobby('?aspect=4:3', {}, { firstVisit: true });
    env.$('play').click();
    check('the picture of the page goes with the game: ?aspect=4:3 gives aspect=4:3', env.param(env.assigned[0], 'aspect') === '4:3', env.assigned[0]);
    check('... and the picture\'s button for 4:3 is the one that is checked', env.$('aspect-4-3').checked && !env.$('aspect-16-9').checked);
    const remembered = runLobby('', { 'ants.aspect.v2': '4:3' }, { firstVisit: true });
    remembered.$('play').click();
    check('... so does the choice that the buttons remembered', remembered.param(remembered.assigned[0], 'aspect') === '4:3', remembered.assigned[0]);
}
// ... the addresses: ?map=...&players=1 plays on this computer after the name step; an address that names no players still hosts 4
{
    const env = runLobby('?map=small&players=1&fill=hard', { 'ants.name': 'Maya' }, { firstVisit: true });
    check('?map=small&players=1&fill=hard asks for the name first (button Play), and nothing starts before', !env.$('who-go').hidden && env.$('who-go').textContent === 'Play' && /computer/.test(env.$('who-title').textContent) && env.assigned.length === 0 && env.$('cards').hidden && env.$('how').hidden);
    check('... the step explains itself in its own line, and the general line is not shown', !env.$('who-step-hint').hidden && /game on this computer/.test(env.$('who-step-hint').textContent) && env.$('who-general').hidden);
    env.$('who-go').click();
    const u = env.assigned[0] || '';
    check('... then the game of this computer on that map with the Hard bots and the name', env.assigned.length === 1 && env.param(u, 'map') === 'small' && env.param(u, 'bots') === 'hard' && env.param(u, 'name') === 'Maya', u);
    check('... and the step is gone, the general line is back', env.$('who-step-hint').hidden && !env.$('who-general').hidden && env.$('who-go').hidden && env.$('who-title').hidden);
}
{   // an address names one level for all three bases and no teams: that game is played so, and what the form remembered for the Teams is not touched (the form's own START writes it)
    const env = runLobby('?map=small&players=1&fill=hard', { 'ants.name': 'Maya', 'ants-solo-seats': 'easy,medium,hard', 'ants-solo-teams': '0+1' }, { firstVisit: true });
    env.$('who-go').click();
    const u = env.assigned[0] || '';
    check('an address with &fill=hard: Hard in all three bases, no teams', env.param(u, 'bots') === 'hard' && env.param(u, 'teams') === null, u);
    same('... the levels that were played are remembered, the team that the form remembered is left as it was', [env.storage.data['ants-solo-seats'], env.storage.data['ants-solo-teams']], ['hard,hard,hard', '0+1']);
    same('... and the Host card\'s players are not touched by a game on this computer', 'ants-four-players' in env.storage.data, false);
}
{
    const env = runLobby('?map=treasure&players=1&play=here', {}, { firstVisit: true });
    check('the test mode ?play=here asks nobody: ?map=...&players=1 plays at once', env.assigned.length === 1 && env.$('who-go').hidden);
    const room = runLobby('?map=small', {}, { firstVisit: true });
    check('an address that names a map and no players hosts 4, as it always did (after the name step)', !room.$('who-go').hidden && room.$('who-go').textContent === 'Host');
    room.$('who-go').click();
    check('... the room of 4 on that map', room.roomStarted() && /demo-small-4p-/.test(room.$('room-code').textContent) && room.assigned.length === 0);
    check('... and the page is in its room mode', room.body.classList.contains('in-room'));
    check('... the cards and "How it works" give way to the room', room.$('cards').hidden && room.$('how').hidden && !room.$('room-panel').hidden);
}
// ... the host's own seat in this tab: the address that Join makes, with the bots of the leader's START
{
    const env = runLobby('', {}, {});
    env.hostPlayers(3);
    setHostSeats(env, ['medium', 'medium', 'medium']);
    env.type('player-name', 'Ann');
    env.$('host').click();
    check('Create the match with 3 players makes the room panel, and the play-here note of the frames is not shown yet', env.roomStarted() && env.$('frames-note').hidden === true);
    env.$('play-tab').click();
    const u = env.assigned[0] || '';
    check('"Play in this tab": this tab goes to the game page with the room, the bots of the leader\'s START and the name (no seat, no embed, no new window)',
          env.assigned.length === 1 && env.opened.length === 0 && env.param(u, 'join') === '/ws' && env.param(u, 'room') === env.$('room-code').textContent && env.param(u, 'fill') === 'medium' && env.param(u, 'name') === 'Ann' && env.param(u, 'seat') === null && env.param(u, 'embed') === null, u);
    const bad = runLobby('', {}, {});
    bad.$('host').click();
    bad.type('player-name', 'Bot (x)');
    bad.$('play-tab').click();
    check('... a bad name starts nothing', bad.assigned.length === 0 && bad.$('name-msg').textContent.length > 8);
    const shown = runLobby('', {}, {});
    shown.$('host').click();
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
        const env = runLobby('', {}, { firstVisit: true });
        await settle();
        same('no network: the line is not there and says nothing (no text of an error), the page itself works', [statsView(env).hidden, statsView(env).live, statsView(env).played, env.fetches.map((f) => f.url)], [true, '', '', ['/stats', '/busy']]);
        env.$('play').click();
        check('... and START plays', env.assigned.length === 1);
    }
    {
        const env = runLobby('', {}, { firstVisit: true, fetch: answers({ '/stats': GOOD_STATS }) });
        check('before the answer the line is hidden (the markup says so)', /<p class="stats" id="stats" hidden>/.test(lobbyText) && env.$('stats').hidden === true);
        await settle();
        same('/stats answers: the line is shown, the dot is green, the numbers are the owner\'s example', statsView(env), { hidden: false, dot: 'live on', live: '3 matches being played · 7 players online', played: '1,284 games played (21 today)', title: 'Counted since 2026-10-04. Today means the last 24 hours.' });
        same('... one request, to the site\'s own /stats, with no cache and no cookies', env.fetches.map((f) => [f.url, f.init.cache, f.init.credentials]), [['/stats', 'no-store', 'omit']]);
        check('... and the page made no markup from the numbers', env.innerHTMLWrites.length === 0);
    }
    {
        const env = runLobby('', {}, { firstVisit: true, fetch: answers({ '/stats': 'page', '/busy': { matches: 0, players: 2 } }) });
        await settle();
        same('/stats is the game page (the site has no such route) and /busy answers: the live part, a grey dot, no totals', [env.fetches.map((f) => f.url), statsView(env)],
             [['/stats', '/busy'], { hidden: false, dot: 'live', live: '0 matches being played · 2 players online', played: '', title: null }]);
    }
    for (const [label, table] of [['/stats answers 404 and /busy 200', { '/stats': 404, '/busy': { matches: 1, players: 1 } }], ['/stats fails and /busy answers', { '/busy': { matches: 1, players: 1 } }]]) {
        const env = runLobby('', {}, { firstVisit: true, fetch: answers(table) });
        await settle();
        same(label + ': 1 match, 1 player, singular', [statsView(env).hidden, statsView(env).live], [false, '1 match being played · 1 player online']);
    }
    {
        const env = runLobby('', {}, { firstVisit: true, fetch: answers({ '/stats': { now: { matches: '<img src=x onerror=alert(1)>', players: 1 }, online: { day: 0, total: 0 }, local: { day: 0, total: 0 } }, '/busy': 500 }) });
        await settle();
        check('an answer with text where numbers belong shows nothing, and nothing of it is in the page', statsView(env).hidden === true && !/[<>]/.test(env.$('stats').textContent + env.$('stats-live').textContent + env.$('stats-played').textContent) && env.innerHTMLWrites.length === 0);
    }
    {   // the page looks again when it is shown after being hidden, and not while it is hidden
        let now = GOOD_STATS;
        const env = runLobby('', {}, { firstVisit: true, fetch: (url) => answers({ '/stats': now })(url) });
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
        const env = runLobby('', {}, { firstVisit: true, noFetch: true });
        await settle();
        check('a browser with no fetch has no line and the page works', statsView(env).hidden === true && env.fetches.length === 0 && (env.$('play').click(), env.assigned.length === 1));
    }
    {   // a page that is a room, or asks for a name, has the line too (it is the header's)
        const room = runLobby('?room=demo-small-2p-abc12&play=here', {}, { firstVisit: true, fetch: answers({ '/stats': GOOD_STATS }) });
        await settle();
        check('a room has the line too', room.roomStarted() && statsView(room).hidden === false);
    }
})().then(() => {
    console.log('web name check: ' + checks + ' checks, ' + failures + ' failures');
    process.exit(failures === 0 ? 0 : 1);
}, (e) => {
    console.log('FAIL ' + e.message + '\n' + e.stack);
    process.exit(1);
});
