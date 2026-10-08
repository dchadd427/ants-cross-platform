// Runs the pages' OWN code for the player's name (the owner: "the ability for somebody to type in their name ... so their name goes into the game instead of random", and "when joining a
// link from somebody else, it should ask you first what you want your name to be"):
//   - the rules of a name (the block NAME_BEGIN .. NAME_END, the same text in web/lobby.html and web/shell.html) on a table of names: the rules of the desktop start menu
//     (printable ASCII, trimmed, at most 32, nothing that starts with "Bot (" with blanks and case ignored; an empty name means "none chosen");
//   - the name step (runNameStep): filled in from what the browser remembered, a bad name explained and not accepted, the button or Enter accepts once, the name is remembered;
//   - web/shell.html: which addresses ask for a name (a shared link: ANTS_PAGE.asksForName), the address without its name (withoutName), and the gate that holds the game back until the name
//     is chosen (makeNameGate): the game is not started, so it does not connect, before the button; the chosen name goes into the game's arguments as --name;
//   - web/lobby.html (the front page) as a whole, with a small fake of the browser's DOM: a first visit (Treasure, You at Green, a Friend in the other seats), the ONE card (a group of five buttons for each seat that is not You,
//     Sit here, the Teams, an invitation for each Friend; START takes this tab to the game page of the room), the room panel of an address that hosts a match (players 2 - 4), the host's own seat in this tab, and the field that every button shares is remembered and filled in, its name goes to the seat that this person plays
//     (the first seat of the page) and the other seats of the page keep random names, a link made for somebody else carries none, bad names start nothing, a name with < > & is only
//     ever text (the page writes no markup at all), an empty field falls back to a random name (and to Player for a join), a shared link of the page asks first and starts nothing before.
//     the room codes (protocol 15: eight characters, a name and nothing more, shown in two groups of four wherever a screen shows one, plain in every link and field; the Join box drops the blanks of a typed or
//     pasted code) and the create block (&roommap= &roomseats= &roomteams= &roomleaderstart=, right after &room=) that every link of a room that the page made carries: the invitations and START, the room
//     panel's links, windows and frames, the address of the panel itself and the "Play in this tab" address; a room that was made some other way (no block in its address, a code that is Joined) has none anywhere.
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
    ['... with a longer door', '?join=/ws/room-1&room=k7m2xq9p', true],
    ['... with the create block of a room that the front page made (the map, the seats, the teams, the leader-starts flag) and the platform', '?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2&roomteams=0%2B1&roomleaderstart=1&platform=linux', true],
    ['... the same with a name (the page that made the address put it there)', '?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2&roomteams=0%2B1&seat=1&name=Bob', false],
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
same('... and a shared link of a room that the front page made carries its create block to the game, after the room (the game makes the room of it when it has none)', P.joinArguments('?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2&roomteams=0%2B1&seat=1&fill=easy&start=2', false, 'h').args,
     ['--join-url', 'ws://h/ws', '--room', 'k7m2xq9p', '--room-map', 'small', '--room-seats', '2', '--room-teams', '0+1', '--seat', '1', '--fill-bots', 'easy', '--start-when', '2']);
const STRIP = [
    ['?join=/ws&room=ABC&name=Bob', '?join=/ws&room=ABC'], ['?name=Bob&join=/ws', '?join=/ws'], ['?join=/ws&name=Bob&room=ABC', '?join=/ws&room=ABC'],
    ['?name=Bob', ''], ['?join=/ws&name=', '?join=/ws'], ['?join=/ws&room=ABC', '?join=/ws&room=ABC'], ['', ''], ['?username=Bob&join=/ws', '?username=Bob&join=/ws'],
    ['?join=/ws&name=a&name=b&room=ABC', '?join=/ws&room=ABC'], ['?join=/ws&name=Zo%C3%AB%20X&seat=1', '?join=/ws&seat=1'],
    ['?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2&roomteams=0%2B1&name=Bob&seat=1', '?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2&roomteams=0%2B1&seat=1'],
];
for (const [search, want] of STRIP) same('withoutName(' + search + ')', P.withoutName(search), want);
check('a copy of the address of a game that was given a name is a shared link', P.asksForName(P.withoutName('?join=/ws&room=ABC&seat=1&name=Bob')) === true);

// The seat in the address (the review's L2): the game tells the page the seat that the room gave this window, and the address carries it, so that a reload of this window takes its own seat and its key
const SEATS = [
    ['a link with no seat gets it, at the end', '?join=/ws&room=ABC', 0, '?join=/ws&room=ABC&seat=0'],
    ['a seat that was asked for and is not the one given is replaced', '?join=/ws&room=ABC&seat=1', 2, '?join=/ws&room=ABC&seat=2'],
    ['... wherever it stood, the others keep their order', '?seat=1&join=/ws&room=ABC', 3, '?join=/ws&room=ABC&seat=3'],
    ['... every one of them', '?join=/ws&seat=1&room=ABC&seat=2', 0, '?join=/ws&room=ABC&seat=0'],
    ['the name and the picture stay', '?join=/ws&room=ABC&name=Bob&aspect=16:9', 1, '?join=/ws&room=ABC&name=Bob&aspect=16:9&seat=1'],
    ['a seat with no value is the seat parameter too', '?join=/ws&room=ABC&seat', 1, '?join=/ws&room=ABC&seat=1'],
    ['another key that ends in seat is not', '?join=/ws&myseat=1', 2, '?join=/ws&myseat=1&seat=2'],
    ['an encoded key is read', '?join=/ws&se%61t=1&room=ABC', 2, '?join=/ws&room=ABC&seat=2'],
    ['no address at all', '', 1, '?seat=1'],
    ['a seat that is the same stays', '?join=/ws&room=ABC&seat=1', 1, '?join=/ws&room=ABC&seat=1'],
    ['the create block of the room stays where it is (the game of a room that the front page made keeps it in its address)', '?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2&roomteams=0%2B1&name=Bob&seat=1', 0, '?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2&roomteams=0%2B1&name=Bob&seat=0'],
];
for (const [label, search, seat, want] of SEATS) same('withSeat: ' + label + ' (' + search + ', ' + seat + ')', P.withSeat(search, seat), want);
for (const seat of [4, -1, 255, 1.5, NaN, '1', null, undefined]) same('withSeat: ' + String(seat) + ' is no seat of a player: the address is as it was', P.withSeat('?join=/ws&room=ABC', seat), '?join=/ws&room=ABC');
{
    const doc = { getElementById() { return { setAttribute() {}, classList: { add() {} } }; }, querySelectorAll() { return []; }, body: { classList: { add() {} } } };
    const pageAt = (search, throws) => {
        const win = { location: { search, protocol: 'http:', host: 'h', pathname: '/play.html', hash: '#x' }, localStorage: null, urls: [] };
        win.history = {
            replaceState(state, title, url) {
                if (throws) throw new Error('refused');
                win.urls.push(url);
                const q = url.indexOf('?');
                win.location.search = q === -1 ? '' : url.slice(q).replace(/#.*/, '');
            },
        };
        new Function('window', 'document', shellCode)(win, doc);
        return win;
    };
    const told = pageAt('?join=/ws&room=ABC&name=Bob', false);
    check('antsSeatKnown is a function of the page: the game calls it', typeof told.antsSeatKnown === 'function');
    told.antsSeatKnown(1);
    same('a game of a server\'s room is told its seat: the address gets it (the hash stays)', told.urls, ['/play.html?join=/ws&room=ABC&name=Bob&seat=1#x']);
    told.antsSeatKnown(1);
    same('... told the same seat again, it leaves the address alone', told.urls.length, 1);
    told.antsSeatKnown(2);
    same('... another seat replaces it', told.urls[1], '/play.html?join=/ws&room=ABC&name=Bob&seat=2#x');
    const frame = pageAt('?join=/ws&room=ABC&seat=1&name=Bob&embed=1', false);
    frame.antsSeatKnown(1);
    frame.antsSeatKnown(0);
    same('a frame of another page leaves its address alone', frame.urls, []);
    const local = pageAt('?map=TINY.LVL&bots=easy', false);
    local.antsSeatKnown(0);
    same('a game on this computer leaves its address alone', local.urls, []);
    const refusing = pageAt('?join=/ws&room=ABC&name=Bob', true);
    let threw = false;
    try { refusing.antsSeatKnown(1); } catch (e) { threw = true; }
    check('an address that cannot be changed is left as it is, with no error', !threw);
}

// The keys of the game that the browser also uses (the review's L4): F1 (help), F2 and F3 (the vote's keys; F3 is the browser's search); never the player's own F5 and F12
for (const [label, e, want] of [
    ['F1', { key: 'F1' }, true], ['F2', { key: 'F2' }, true], ['F3', { key: 'F3' }, true], ['Shift+F3 (search backwards)', { key: 'F3', shiftKey: true }, true],
    ['F4', { key: 'F4' }, false], ['F5 (reload)', { key: 'F5' }, false], ['F11 (full screen)', { key: 'F11' }, false], ['F12 (tools)', { key: 'F12' }, false],
    ['a letter (the chat box)', { key: 'a' }, false], ['Space', { key: ' ' }, false], ['Ctrl+A', { key: 'a', ctrlKey: true }, true], ['Ctrl+S', { key: 's', ctrlKey: true }, true],
    ['Ctrl+R (reload)', { key: 'r', ctrlKey: true }, false], ['Ctrl+Shift+A', { key: 'A', ctrlKey: true, shiftKey: true }, false], ['an arrow scrolls the page', { key: 'ArrowUp' }, true],
    ['Page Down', { key: 'PageDown' }, true], ['Ctrl+Arrow (the browser\'s own)', { key: 'ArrowLeft', ctrlKey: true }, false], ['no key', {}, false],
]) check('cancelsBrowserKey: ' + label, P.cancelsBrowserKey(e) === want, String(P.cancelsBrowserKey(e)));

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
    check('a key of three hours and a second does not count (the game would not use it either)', asks('?join=/ws&room=ABC&seat=1', { 'ants.rejoin.ABC.1': entry(3 * 3600 * 1000 + 1000) }) === true);
    check('a key of three hours to the millisecond does', asks('?join=/ws&room=ABC&seat=1', { 'ants.rejoin.ABC.1': entry(3 * 3600 * 1000) }) === false);
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
    // The invitation of the front page's card is a link to THIS page's step (?join=/ws&room=..&seat=..: no name), so a friend who opens it on a phone sees this card and nothing else: the way out is here too,
    // as the front page's own step has it ("Whatever the one-card page does with the name step, it keeps a visible way out of it")
    const stepMarkup = shellText.slice(shellText.indexOf('id="name-step"'), shellText.indexOf('<div class="view-bar"'));
    check('the card has a way out: "Back to the front page", a plain link to the front page (no query: nothing of the match goes with it), after the hint and inside the card',
          /<a class="btn sm name-step-back" id="name-step-back" href="\/">&larr; Back to the front page<\/a>\s*<\/div>\s*<\/div>/.test(stepMarkup) && stepMarkup.indexOf('name-step-hint') < stepMarkup.indexOf('name-step-back'));
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
    const env = { innerHTMLWrites: [], assigned: [], replaced: [], opened: [], copied: [], commanded: [], shared: [], elements: {}, focused: null, confirms: [] };
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
        execCommand(cmd) {                                // the copy command copies what the last field of the page holds (options.copyResult: false is a browser that did not copy, 'throws' one that refuses)
            const field = doc.body.children[doc.body.children.length - 1];
            env.commanded.push({ cmd, value: field ? field.value : null });
            if (options.copyResult === 'throws') throw new Error('not allowed');
            return options.copyResult !== false;
        },
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
    const nav = options.clipboard === false ? {} : { clipboard: { writeText(t) { env.copied.push(t); return { then() {} }; } } };       // (options.clipboard: false is a browser without the clipboard's own API: the copy command is used)
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

// The codes and the create block (protocol 15). A code that the page makes is eight characters of an alphabet without look-alikes: a name and nothing more (a screen shows it in two groups of four, a link and a
// field keep it plain). What the room is, its map, its seats and its teams, travels in its CREATE BLOCK, which every link of a room that the page made carries right after &room=<code>:
// &roommap=<key>&roomseats=<n>[&roomteams=<A%2BB>][&roomleaderstart=1], made by ONE helper of the page (roomBlockQuery); a room that was made some other way has none.
const CODE = /^[a-z2-9]{8}$/;
const BLOCK_PARAMS = ['roommap', 'roomseats', 'roomteams', 'roomleaderstart'];
// what stands right after &room=<code> in a link: the block's own parameters, as the link spells them ('' when it has none, null when it has no room at all)
const blockText = (link) => { const m = /[?&]room=[^&#]*((?:&room(?:map|seats|teams|leaderstart)=[^&#]*)*)/.exec(String(link)); return m ? m[1] : null; };
// the same as the address decodes them: [map, seats, teams, flag], null for a parameter that is not there
const blockParams = (env, link) => BLOCK_PARAMS.map((k) => env.param(link, k));
// a link with no create block at all: none of the four parameters (a room that was made some other way, or a code that is only Joined)
const noBlockOf = (env, link) => blockText(link) === '' && blockParams(env, link).every((v) => v === null) && !/room(map|seats|teams|leaderstart)/.test(String(link));
// the code as a screen shows it: two groups of four
const grouped = (code) => code.slice(0, 4) + ' ' + code.slice(4);
// every link of a game page that a room makes says the same about the room: the code, then its block in the same place, then the seat (the tests below ask each of them)
const BLOCK_TREASURE4 = '&roommap=treasure&roomseats=4';

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
    check('every game of a room that the page made carries the room\'s create block (Small, four seats) right after the room: the frames, the window, the link for anybody and the link of a seat',
          [f2.src, f1.src, env.opened[0].url, anyLink, env.copied[0]].every((u) => blockText(u) === '&roommap=small&roomseats=4'), JSON.stringify([f2.src, f1.src, env.opened[0].url, anyLink, env.copied[0]]));
    env.rowButton(2, 'Play here').click();
    check('"Play here" again (a reconnect) keeps the seat\'s name', env.param(env.frameOf(2).src, 'name') === 'Alice');
}
{   // the leader of a room can put a player in another colour (protocol 14): the game in a frame reports the seat that it plays now, and "Play here" again brings it back as that seat (its key is kept under it)
    const env = openRoom('?map=treasure&players=4', {}, 'Alice');
    env.rowButton(1, 'Play here').click();
    const frame = env.frameOf(1);
    const opened = frame.src;                                         // (the address that the frame was opened with: a reconnect changes the address of this same element)
    const say = (source, seat, tick) => (env.win.listeners.message || []).forEach((fn) => fn({ origin: 'https://play.test', source, data: { ants: 'sync', seat, tick: tick || 40, hash: '0123456789abcdef' } }));
    frame.contentWindow = null;                                       // (a frame whose window is gone has none, and a window that was closed has no source: they are not the same window)
    say(null, 3);
    env.rowButton(1, 'Play here').click();
    check('a frame that has reported nothing comes back as the seat that it was opened for (a message from a window that is gone is nobody\'s)', env.param(env.frameOf(1).src, 'seat') === '1');
    frame.contentWindow = {};
    say(frame.contentWindow, 1);
    env.rowButton(1, 'Play here').click();
    check('... and so does one that reports that seat', env.param(env.frameOf(1).src, 'seat') === '1');
    say({}, 3);
    say(undefined, 3);
    env.rowButton(1, 'Play here').click();
    check('a report from a window that is no frame of this page (or from no window) changes nothing', env.param(env.frameOf(1).src, 'seat') === '1');
    say(frame.contentWindow, 7);
    env.rowButton(1, 'Play here').click();
    check('... and neither does a seat that no room has', env.param(env.frameOf(1).src, 'seat') === '1');
    say(frame.contentWindow, 3, 77);
    const cell1 = env.$('grid').children.find((c) => c.getAttribute('data-seat') === '1');
    check('the report of a game that plays another colour is shown on its own frame and row (the first seat of the page), not on the colour that it plays now', cell1.textContent.indexOf('tick 77') !== -1 && env.rows()[1].children[1].textContent === 'tick 77' && env.rows()[3].children[1].textContent !== 'tick 77', JSON.stringify([cell1.textContent, env.rows()[1].children[1].textContent, env.rows()[3].children[1].textContent]));
    env.rowButton(1, 'Play here').click();
    const src = env.frameOf(1).src;
    check('a game that the leader put in another colour comes back as the seat that it plays now, with the rest of its address as before', env.param(src, 'seat') === '3' && /&embed=1$/.test(src) && env.param(src, 'room') === env.param(opened, 'room') && env.param(src, 'join') === '/ws' && env.param(src, 'aspect') === env.param(opened, 'aspect'), src);
    check('... and under its own name, the one of its row (the typed name, not the name of the colour that it plays now)', env.param(opened, 'name') === 'Alice' && env.param(src, 'name') === 'Alice', src);
    // two games that the leader put in each other's colours (through the free colours) trade no names: each keeps the name of its row
    env.rowButton(2, 'Play here').click();
    const frame2 = env.frameOf(2);
    const opened2 = frame2.src;
    frame2.contentWindow = {};
    say(frame2.contentWindow, 1, 90);
    env.rowButton(2, 'Play here').click();
    const src2 = env.frameOf(2).src;
    check('a second game that plays the seat of the first one\'s row comes back as that seat, under the name of its own row', env.param(src2, 'seat') === '1' && randomName(env.param(opened2, 'name')) && env.param(src2, 'name') === env.param(opened2, 'name') && env.param(src2, 'name') !== 'Alice', src2);
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
    check('the Join box shows what a code looks like on a screen: two groups of four (k7m2 xq9p), and holds up to 32 characters', env.$('join-code').getAttribute('placeholder') === 'k7m2 xq9p' && env.$('join-code').getAttribute('maxlength') === '32');
    env.type('player-name', 'Alice');
    env.type('join-code', 'k7m2 xq9p');
    env.$('join-go').click();
    check('Join: the game page is opened with the room and the typed name; the code is typed as it is shown (two groups of four) and goes as the plain code', env.assigned.length === 1 && env.param(env.assigned[0], 'room') === 'k7m2xq9p' && env.param(env.assigned[0], 'name') === 'Alice' && env.param(env.assigned[0], 'join') === '/ws', env.assigned[0]);
    same('... the whole address: the door, the plain code, the name and no more (no create block: the page does not know what a room that somebody else made is, and a code that no room has is "no such room" for the game server; no seat, bots or teams either)', env.assigned, ['https://play.test/?join=/ws&room=k7m2xq9p&name=Alice']);
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
    same('... in words that name the letters, digits, - and _ of a code, its 32 characters and the way it is shown', env4.$('join-hint').textContent, 'A room code has letters, digits, - and _ only (up to 32), like k7m2 xq9p.');
    check('... in a notice (the class bad), and typing the code again takes the notice away', env4.$('join-hint').classList.contains('bad') && (env4.type('join-code', 'ABC'), !env4.$('join-hint').classList.contains('bad') && env4.$('join-hint').textContent === ''));
    // the blanks of what is typed or pasted are dropped before the code is checked (a code is shown as "k7m2 xq9p"; a paste may bring a blank or a line end at either end), and nothing else is made of the text
    for (const [typed, want] of [['k7m2xq9p', 'k7m2xq9p'], ['k7m2 xq9p', 'k7m2xq9p'], ['  k7m2 xq9p  ', 'k7m2xq9p'], ['k7m2\txq9p\n', 'k7m2xq9p'], ['k7m2 xq9p', 'k7m2xq9p'], [' k 7 m 2 x q 9 p ', 'k7m2xq9p'], ['K7M2 XQ9P', 'K7M2XQ9P'],
                                 ['my_room-1', 'my_room-1'], ['a', 'a'], ['a'.repeat(32), 'a'.repeat(32)], ['demo-small-2p-x7k2', 'demo-small-2p-x7k2'], ['demo-tiny-2p-t01-abcdef', 'demo-tiny-2p-t01-abcdef'], ['abc def ghi', 'abcdefghi']]) {
        const j = runLobby('', {});
        j.type('join-code', typed);
        j.$('join-go').click();
        check('Join drops the blanks of ' + JSON.stringify(typed) + ' and joins ' + want + ' (a code of any other kind that is valid is a name too: nothing is made of it, no create block)',
              j.assigned.length === 1 && j.param(j.assigned[0], 'room') === want && noBlockOf(j, j.assigned[0]) && j.$('join-hint').textContent === '' && !/\s/.test(j.assigned[0]), j.assigned[0]);
    }
    for (const bad of ['', '   ', '\n', 'not a code!', 'k7m2/xq9p', 'k7m2.xq9p', 'abé', 'x'.repeat(33), 'k7m2 xq9p ' + 'x'.repeat(30), 'a b!', '?room=x&seat=1']) {
        const j = runLobby('', {});
        j.type('join-code', bad);
        j.$('join-go').click();
        check('Join with ' + JSON.stringify(bad).slice(0, 24) + ' goes nowhere and says why', j.assigned.length === 0 && j.$('join-hint').textContent === 'A room code has letters, digits, - and _ only (up to 32), like k7m2 xq9p.' && j.$('join-hint').classList.contains('bad'));
    }
    const dashes = runLobby('', {});
    dashes.type('join-code', '--');
    dashes.$('join-go').click();
    check('... "--" is a valid code (letters, digits, - and _, one to 32 of them)', dashes.assigned.length === 1 && dashes.param(dashes.assigned[0], 'room') === '--');
    const dropped = runLobby('', {});
    dropped.type('join-code', 'k7m2 xq9p');
    dropped.$('join-code').key('Enter');
    check('Enter in the box joins the same way (the blank is dropped there too)', dropped.assigned.length === 1 && dropped.param(dropped.assigned[0], 'room') === 'k7m2xq9p');
    const blanked = runLobby('', {});
    blanked.type('join-code', 'k7m2 xq9p');
    check('the field keeps what was typed (the code as it is shown): only the address that Join makes drops the blank', blanked.$('join-code').value === 'k7m2 xq9p' && (blanked.$('join-go').click(), blanked.$('join-code').value === 'k7m2 xq9p'));
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
    const env = runLobby('?room=k7m2xq9p', { 'ants.name': 'Maya' });
    check('a shared room link: the step is up, with the remembered name in the field and a Join button', !env.$('who-go').hidden && env.$('who-go').textContent === 'Join' && !env.$('who-title').hidden && /k7m2 xq9p/.test(env.$('who-title').textContent) && env.$('player-name').value === 'Maya');
    same('... the step names the room by its code as a screen shows it (two groups of four)', env.$('who-title').textContent, 'Join the match k7m2 xq9p');
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
    // a room that this page did not make (its address has no create block): four seats, and nothing of a block anywhere: the page sends none, and a code that no room has is the game server's to refuse
    same('... the panel shows the code as a screen shows it (two groups of four), four seats and says that the room was made some other way', [env.$('room-code').textContent, env.$('room-map').textContent, env.rows().length], ['k7m2 xq9p', 'a room made some other way, 4 players', 4]);
    same('... the address of the page is the room alone: no create block to carry (and the plain code)', env.replaced.map((a) => a[2]), ['/?room=k7m2xq9p']);
    env.rowButton(0, 'Copy link').click();
    env.rowButton(2, 'Open a window').click();
    const made = [env.frameOf(1).src, env.opened[0].url, env.copied[0], env.$('any-link').value];
    check('... and no game of it carries a create block: the frame, the window, the link of a seat and the link for anybody are the plain room (the code is the plain one in all of them)', made.every((u) => noBlockOf(env, u) && env.param(u, 'room') === 'k7m2xq9p'), JSON.stringify(made));
    env.$('play-tab').click();
    check('... nor does "Play in this tab"', env.assigned.length === 1 && noBlockOf(env, env.assigned[0]) && env.param(env.assigned[0], 'room') === 'k7m2xq9p', env.assigned[0]);
}
{
    const env = runLobby('?room=k7m2xq9p', {});
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
    check('... then the room is made on that map: a new code of eight characters and its create block (Treasure, two seats) are in the address of the page', env.roomStarted() && env.replaced.length === 1 && /^\/\?room=[a-z2-9]{8}&roommap=treasure&roomseats=2$/.test(String(env.replaced[0][2])), String(env.replaced[0] && env.replaced[0][2]));
    same('... the panel says it: the code in two groups of four, the map and two seats', [env.$('room-code').textContent === grouped(env.param(new URL(String(env.replaced[0][2]), 'https://play.test').href, 'room')), env.$('room-map').textContent, env.rows().length], [true, 'Treasure map, 2 players', 2]);
}
{
    const env = runLobby('?room=k7m2xq9p&roommap=small&roomseats=2&play=here', { 'ants.name': 'Maya' });
    check('the test mode ?play=here asks nobody: every seat starts at once (the two seats of the create block)', !env.$('who-go').hidden === false && env.roomStarted() && env.frames().length === 2);
    const names2 = [0, 1].map((s) => env.param(env.frameOf(s).src, 'name'));
    check('... the first seat takes the remembered name, the other seat is random', names2[0] === 'Maya' && randomName(names2[1]), names2.join(','));
    const env2 = runLobby('?map=small&play=here', {});
    check('?map= with ?play=here asks nobody either', env2.roomStarted() && env2.frames().length === 4 && env2.$('who-go').hidden);
    const elsewhere = runLobby('?room=k7m2xq9p&play=here', {});
    check('a room with no create block that starts at once has four seats (the page does not know the room: nothing is trusted but the code)', elsewhere.roomStarted() && elsewhere.frames().length === 4);
}
{
    const env = runLobby('?room=a%20b', {});
    check('an address that is no room code is the plain page (nothing is asked)', !env.$('cards').hidden && !env.$('how').hidden && env.$('who-go').hidden && !env.roomStarted());
    for (const shown of ['?room=k7m2%20xq9p', '?room=k7m2+xq9p', '?room=k7m2%2Bxq9p', '?room=', '?room=' + 'x'.repeat(33), '?room=k7m2xq9p%00', '?room=..%2Fx']) {
        const e = runLobby(shown + '&roommap=small&roomseats=2', {});
        check('an address with the code as a screen shows it (a blank in the middle) or with no code that a room can have is no room: ' + shown + ' is the plain page, even with a create block', !e.$('cards').hidden && !e.$('how').hidden && e.$('who-go').hidden && !e.roomStarted() && e.replaced.length === 0);
    }
}
{
    const env = runLobby('?aspect=4:3', {});
    check('an address with only a picture is the plain page', !env.$('cards').hidden && env.$('who-go').hidden);
}

// ---- the front page: ONE card, "New match" (the owner, on a phone: "I don't see a way to change colors or send an invite to another person should all be right there. We don't need separate AI and
// online. Only do online."): a map with its preview, four seats (exactly one of them You, a choice for each of the others, and a Team 1 and a Team 2 switch), an invitation for each Friend seat and START; under it "Have a code?" and Join
const lobbyScript = (() => { const all = [...lobbyText.matchAll(/<script>([\s\S]*?)<\/script>/g)]; return all[all.length - 1][1]; })();
const WORDS5 = ['friend', 'easy', 'medium', 'hard', 'nobody'];
// A browser that remembered Medium bots for the other three seats: the first visit is Friends, so most of the scenarios below start from the bots, as the card used to (a fresh object each time: the page writes
// into the storage that it is given)
const BOTS = () => ({ 'ants-match': JSON.stringify({ map: 'treasure', you: 0, seats: ['medium', 'medium', 'medium', 'medium'], teams: 'ffa' }) });
const seatId = (seat, word) => 'seat-' + seat + '-' + word;
// what the four rows show: the seats whose You marker is up, and the one button that is checked in each row's group ('?n' when it is not exactly one)
const youSeats = (env) => [0, 1, 2, 3].filter((s) => !env.$('seat-you-' + s).hidden);
const seatWords = (env) => [0, 1, 2, 3].map((s) => { const on = WORDS5.filter((w) => env.$(seatId(s, w)).checked); return on.length === 1 ? on[0] : '?' + on.length; });
const choose = (env, seat, word) => env.pick(seatId(seat, word));
const sit = (env, seat) => env.$('sit-' + seat).click();
// the Team 1 / Team 2 switches: for each seat '' when it has none (the seat does not play, or fewer than three seats do), else which one is lit ('1', '2') or '-' when neither is; and which are dimmed ('' none, '1', '2', '12')
const switchesShown = (env) => [0, 1, 2, 3].map((s) => (env.$('team-set-' + s).hidden ? '' : [1, 2].filter((k) => env.$('team-' + s + '-' + k).getAttribute('aria-pressed') === 'true').join('') || '-'));
const switchesDimmed = (env) => [0, 1, 2, 3].map((s) => [1, 2].filter((k) => env.$('team-' + s + '-' + k).getAttribute('aria-disabled') === 'true').join(''));
const pressTeam = (env, seat, side) => env.$('team-' + seat + '-' + side).click();
const FFA_LINE = 'Free for all. For a team, put two colours on the same team.';
const invites = (env) => env.$('invite-list').children;
const inviteLinks = (env) => invites(env).map((row) => row.children[1].value);
const copyOf = (env, i) => invites(env)[i].children[2];
const pickMap = (env, key) => { env.$('map-pick').value = key; env.$('map-pick').fire('change', {}); };
const MAP_KEYS = ['tiny', 'small', 'medium', 'gauntlet', 'treasure', 'islands'];
// the create block that the card's room carries in every link: the card's map, four seats (every colour can be taken) and the card's teams (A+B: %2B in a link), never the leader-starts flag
const cardBlockText = (map, teams) => '&roommap=' + map + '&roomseats=4' + (teams && teams !== 'ffa' ? '&roomteams=' + teams.replace('+', '%2B') : '');
{
    const env = runLobby('', {});
    same('a first visit: Treasure, You at Green, a Friend in every other seat, free for all (no switch is on)', [env.$('map-pick').value, youSeats(env), seatWords(env), switchesShown(env)], ['treasure', [0], ['friend', 'friend', 'friend', 'friend'], ['-', '-', '-', '-']]);
    check('... the name under You is one of the page\'s random names (the field is empty: "Leave it empty for a random name"), and the marker says You', randomName(env.$('seat-name-0').textContent) && !env.$('seat-name-0').hidden && [0, 1, 2, 3].every((s) => new RegExp('<span class="you" id="seat-you-' + s + '" hidden> &middot; You</span>').test(lobbyText)));
    same('... the rows of the other seats have a Sit here and a group of five buttons; the row of You has neither', [0, 1, 2, 3].map((s) => [env.$('sit-' + s).hidden, env.$('seat-set-' + s).hidden]), [[true, true], [false, false], [false, false], [false, false]]);
    check('... the seat of You is marked (a class), the others are not', env.$('seat-row-0').classList.contains('is-you') && [1, 2, 3].every((s) => !env.$('seat-row-' + s).classList.contains('is-you')));
    same('... four seats play: every seat has a Team 1 and a Team 2 switch (the one of You too), none is on or dimmed, and the line under the seats says free for all and how to make a team', [env.$('teams-line').hidden, switchesShown(env), switchesDimmed(env), env.$('teams-line').textContent], [false, ['-', '-', '-', '-'], ['', '', '', ''], FFA_LINE]);
    same('... an invitation for each of the three Friends, START is on and says that it waits for them', [env.$('invites').hidden, invites(env).map((r) => r.children[0].textContent), !env.$('play').disabled, env.$('start-note').textContent], [false, ['Red', 'Blue', 'Black'], true, 'Starts when your friends are in (the first player in the room can start sooner).']);
    same('... the preview and the Map Info line are those of Treasure', [env.$('map-preview').src, env.$('map-info').textContent], ['front/preview_treasure.png', "One person's trash... (12 min)"]);
    check('... the card is there with the name field, "Have a code?" and the picture\'s buttons; "How it works" too', !env.$('cards').hidden && !env.$('how').hidden && !!env.$('player-name') && !!env.$('join-code') && !!env.$('aspect-16-9'));
    check('... START is a real button whose own text, START!, names it for a screen reader (no aria-label: it is no longer the original\'s picture) and the line under it describes it', /<button id="play" class="btn startbtn" type="button" aria-describedby="start-note">START!<\/button>/.test(lobbyText) && env.$('play').getAttribute('aria-label') === null && env.$('play').getAttribute('aria-describedby') === 'start-note');
    const heldRoom = JSON.parse(env.session.data['ants-match-room'] || 'null') || {};
    check('nothing was written by merely loading but the tab\'s own room for the invitations (no choice is remembered)', Object.keys(env.storage.data).length === 0 && Object.keys(env.session.data).join() === 'ants-match-room', JSON.stringify([env.storage.data, env.session.data]));
    same('... the room that the tab holds is { code, map, teams, used }: a code of eight characters, the card\'s map and teams (the create block of its links is made of them), no link copied yet', [Object.keys(heldRoom).sort(), CODE.test(heldRoom.code), heldRoom.map, heldRoom.teams, heldRoom.used],
         [['code', 'map', 'teams', 'used'], true, 'treasure', 'ffa', {}]);
    const ids = [...lobbyText.matchAll(/<input type="radio" name="(seat-\d)" id="(seat-\d-\w+)" value="(\w*)"/g)].map((m) => m.slice(1, 4).join(' '));
    same('the choices are radio buttons of four groups (names seat-0 to seat-3), Friend, Easy, Medium, Hard, Nobody in this order (the arrow keys and Tab are the browser\'s own)', ids,
         [0, 1, 2, 3].flatMap((s) => WORDS5.map((w) => 'seat-' + s + ' seat-' + s + '-' + w + ' ' + w)));
    same('every seat\'s group is a fieldset named by its colour for a screen reader (the legend), and every Sit here says which seat it is for',
         [0, 1, 2, 3].map((s) => [new RegExp('<fieldset class="seatset" id="seat-set-' + s + '"><legend class="sr">' + ['Green', 'Red', 'Blue', 'Black'][s] + '</legend>').test(lobbyText), env.$('sit-' + s).getAttribute('aria-label')]),
         [[true, 'Sit here as Green'], [true, 'Sit here as Red'], [true, 'Sit here as Blue'], [true, 'Sit here as Black']]);
}
{   // Sit here
    const env = runLobby('', BOTS());
    choose(env, 1, 'hard');
    choose(env, 2, 'friend');
    choose(env, 3, 'nobody');
    sit(env, 2);
    same('Sit here at Blue: You are at Blue (and only there); Green shows the choice that it kept (Medium), Blue\'s own choice (Friend) is kept out of sight, Red and Black keep theirs', [youSeats(env), seatWords(env)], [[2], ['medium', 'hard', 'friend', 'nobody']]);
    same('... Blue has the marker, the name and neither buttons nor Sit here; the others have Sit here and buttons', [[0, 1, 2, 3].map((s) => env.$('sit-' + s).hidden), [0, 1, 2, 3].map((s) => env.$('seat-set-' + s).hidden), [0, 1, 2, 3].map((s) => env.$('seat-name-' + s).hidden)], [[false, false, true, false], [false, false, true, false], [true, true, false, true]]);
    check('... and the focus goes to the Sit here of the seat that You left (the button that was pressed is gone)', env.focused === env.$('sit-0'));
    same('... what is remembered: You at Blue and the four choices', JSON.parse(env.storage.data['ants-match']), { map: 'treasure', you: 2, seats: ['medium', 'hard', 'friend', 'nobody'], teams: 'ffa', sides: [0, 0, 0, 0] });
    sit(env, 0);
    same('... and back at Green: Blue shows its Friend again', [youSeats(env), seatWords(env)], [[0], ['medium', 'hard', 'friend', 'nobody']]);
    const again = runLobby('', env.storage.data);
    same('the next visit comes back with them: You, the choices', [youSeats(again), seatWords(again)], [[0], ['medium', 'hard', 'friend', 'nobody']]);
}
{   // the name under You: the typed name, as text, and a random one while the field is empty
    const env = runLobby('', BOTS());
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
    const env = runLobby('', BOTS());
    choose(env, 1, 'friend');
    check('a Friend seat shows the invitations, one row for it', !env.$('invites').hidden && invites(env).length === 1);
    const link = inviteLinks(env)[0];
    const code = env.param(link, 'room');
    check('its room is a code of eight characters (a name and nothing more), and is in the tab\'s session', CODE.test(code) && JSON.parse(env.session.data['ants-match-room']).code === code, code);
    same('... and its create block, right after the code, is the map and four seats (every colour can be taken), free for all: no teams, no leader-starts flag', [blockText(link), blockParams(env, link)], [BLOCK_TREASURE4, ['treasure', '4', null, null]]);
    same('the link: this site\'s game page, the door, the room, the friend\'s seat, the plan (Green is You, Red the friend, Medium bots at Blue and Black), the picture and the people to wait for (You and the friend)',
         [new URL(link).origin + new URL(link).pathname, env.param(link, 'join'), env.param(link, 'seat'), env.param(link, 'fill'), env.param(link, 'aspect'), env.param(link, 'start')], ['https://play.test/', '/ws', '1', 'none,none,medium,medium', '16:9', '2']);
    same('... all of it, in this order (the door, the plain code, the create block, the seat, the plan, the picture, the people)', link, 'https://play.test/?join=/ws&room=' + code + BLOCK_TREASURE4 + '&seat=1&fill=none,none,medium,medium&aspect=16:9&start=2');
    check('... no name (whoever opens it is asked what they want to be called), no teams (a room\'s teams are in its block), no key, no leader-starts flag, no platform', ['name', 'teams', 'key', 'token', 'embed', 'roomteams', 'roomleaderstart', 'platform'].every((k) => env.param(link, k) === null) && !/key|token/i.test(link), link);
    same('the row: the friend\'s colour, the link in a field that cannot be typed in, Copy link; no Share where the browser has none', [invites(env)[0].children[0].textContent, invites(env)[0].children[1].value === link && invites(env)[0].children[1].attributes.readonly !== undefined && invites(env)[0].children[1].readOnly === true, copyOf(env, 0).textContent, invites(env)[0].children.length], ['Red', true, 'Copy link', 3]);
    check('... the field has a name for a screen reader', invites(env)[0].children[1].getAttribute('aria-label') === 'Invitation link for Red');
    copyOf(env, 0).click();
    same('Copy link copies that link (and nothing else)', env.copied, [link]);
    choose(env, 3, 'friend');
    same('a second Friend seat: two rows, in the order of the seats, the same room, the same plan (none for both friends) and the people to wait for (three)', [invites(env).map((r) => r.children[0].textContent), inviteLinks(env).map((l) => env.param(l, 'room')).every((r) => r === code), inviteLinks(env).map((l) => env.param(l, 'seat')), inviteLinks(env).map((l) => env.param(l, 'fill')), inviteLinks(env).map((l) => env.param(l, 'start'))],
         [['Red', 'Black'], true, ['1', '3'], ['none,none,medium,none', 'none,none,medium,none'], ['3', '3']]);
    check('... and the same create block in both (the first Hello of the room makes it from whichever link is opened first)', inviteLinks(env).length === 2 && inviteLinks(env).every((l) => blockText(l) === BLOCK_TREASURE4));
    choose(env, 1, 'easy');
    same('a seat that is no Friend any more loses its row, and the plan has its bot', [invites(env).map((r) => r.children[0].textContent), env.param(inviteLinks(env)[0], 'fill'), env.param(inviteLinks(env)[0], 'start')], [['Black'], 'none,easy,medium,none', '2']);
    choose(env, 3, 'hard');
    check('no Friend seat: no invitations', env.$('invites').hidden && invites(env).length === 0);
    check('the page made no markup of any of it', env.innerHTMLWrites.length === 0);
}
{   // Share, where the browser has it
    const env = runLobby('', BOTS(), { share: true });
    choose(env, 2, 'friend');
    same('the row has Share too where the browser has it', [invites(env)[0].children.length, invites(env)[0].children[3].textContent], [4, 'Share']);
    invites(env)[0].children[3].click();
    same('Share gives the browser the link of that seat (a title and a line of text as well, nothing private)', [env.shared.length, env.shared[0].url, env.shared[0].title, /Blue/.test(env.shared[0].text), Object.keys(env.shared[0]).sort()], [1, inviteLinks(env)[0], 'Ants', true, ['text', 'title', 'url']]);
    check('a link that was shared is in use like a copied one: no note yet', env.$('links-note').hidden);
    choose(env, 3, 'hard');
    check('... and a choice that changes the links after a Share brings the note', !env.$('links-note').hidden);
    const refused = runLobby('', BOTS(), { share: 'throws' });
    choose(refused, 2, 'friend');
    let ok = true;
    try { invites(refused)[0].children[3].click(); } catch (e) { ok = false; }
    check('a Share that fails (or is refused by the person: a promise that fails) breaks nothing', ok);
    const closed = runLobby('', BOTS(), { share: 'rejects' });
    choose(closed, 2, 'friend');
    invites(closed)[0].children[3].click();
    check('... a rejected promise is caught too', closed.shared.length === 1);
}
{   // the room's code: made when a Friend seat first needs it, kept while the map and the teams stay (they are its create block, and the first Hello of a code is the one that is read: another map or other teams
    // are another room), new when they change, with a note once a link was in use
    const env = runLobby('', BOTS());
    choose(env, 1, 'friend');
    const first = env.param(inviteLinks(env)[0], 'room');
    choose(env, 2, 'friend');
    choose(env, 3, 'hard');
    choose(env, 2, 'easy');
    sit(env, 3);
    same('the code stays while the map and the teams stay (other seats, other choices, You at another seat)', inviteLinks(env).map((l) => env.param(l, 'room')), [first]);
    same('... and so does the create block that every link carries: the map and four seats, whoever sits where', inviteLinks(env).map((l) => blockText(l)), [BLOCK_TREASURE4]);
    check('... and no note is up (no link was copied)', env.$('links-note').hidden);
    pickMap(env, 'small');
    const second = env.param(inviteLinks(env)[0], 'room');
    check('another map: another room (a new code of eight characters, with the block of Small), and the links are made again; no note yet (nothing was copied)', CODE.test(second) && second !== first && blockText(inviteLinks(env)[0]) === cardBlockText('small') && env.$('links-note').hidden, second);
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
    const third = env.param(inviteLinks(env)[0], 'room');
    check('another map after a copy: another room (a new code, the block of Tiny) and the note', CODE.test(third) && third !== second && blockText(inviteLinks(env)[0]) === cardBlockText('tiny') && !env.$('links-note').hidden);
    copyOf(env, 0).click();
    pressTeam(env, 0, 1);
    pressTeam(env, 1, 1);
    const fourth = env.param(inviteLinks(env)[0], 'room');
    check('the teams are in the block: Green + Red (all four seats play) makes another room (a new code) whose block names them (&roomteams=0%2B1, the "+" encoded), and the note is up again (the links changed after a copy)',
          CODE.test(fourth) && fourth !== third && blockText(inviteLinks(env)[0]) === cardBlockText('tiny', '0+1') && blockText(inviteLinks(env)[0]) === '&roommap=tiny&roomseats=4&roomteams=0%2B1' && !env.$('links-note').hidden);
    same('... decoded, the block of the link is the map, four seats and the pair (the teams are in no other parameter), and the code is plain', [blockParams(env, inviteLinks(env)[0]), env.param(inviteLinks(env)[0], 'teams'), /[?&]room=[a-z2-9]{8}&/.test(inviteLinks(env)[0])], [['tiny', '4', '0+1', null], null, true]);
    pressTeam(env, 1, 1);
    check('... and free for all again (Red\'s switch off) is another room again, with a block that has no teams', CODE.test(env.param(inviteLinks(env)[0], 'room')) && env.param(inviteLinks(env)[0], 'room') !== fourth && blockText(inviteLinks(env)[0]) === cardBlockText('tiny'));
}
{   // two Friends: the note stays up until every link that was sent is the one that is shown for its seat (a seat that was never sent has nothing to be out of date)
    const env = runLobby('', BOTS());
    choose(env, 1, 'friend');
    choose(env, 2, 'friend');
    copyOf(env, 0).click();
    copyOf(env, 1).click();
    check('both links copied: no note', env.$('links-note').hidden);
    pickMap(env, 'small');
    check('another map after both were sent: the note is up (both links are another room\'s now)', !env.$('links-note').hidden);
    copyOf(env, 0).click();
    check('... one of them copied again: the note stays (the link that was sent for the other seat is the old room\'s: its friend would wait alone)', !env.$('links-note').hidden);
    const half = runLobby('', env.storage.data, { session: env.session.data });
    check('... and a reload of the page does not lose that (the tab keeps what was copied for each seat)', !half.$('links-note').hidden);
    copyOf(env, 1).click();
    check('... both copied again: the note goes', env.$('links-note').hidden);
    check('... and a reload keeps it gone', runLobby('', env.storage.data, { session: env.session.data }).$('links-note').hidden);
    choose(env, 3, 'easy');
    check('Black is an Easy bot now (the plan is in every link) after both were sent: the note is up', !env.$('links-note').hidden);
    copyOf(env, 1).click();
    check('... the second link copied: the first one that was sent is still the old one, the note stays', !env.$('links-note').hidden);
    choose(env, 3, 'medium');
    check('... Black put back: the first link is the one that was sent for its seat again, the second is not (it was copied with Easy): the note stays for that one', !env.$('links-note').hidden);
    copyOf(env, 1).click();
    check('... and when that one is copied again the note goes', env.$('links-note').hidden);
    choose(env, 2, 'easy');
    check('Blue is a bot now: one row left, and its link changed (the plan, the people to wait for): the note is up', invites(env).length === 1 && !env.$('links-note').hidden);
    copyOf(env, 0).click();
    check('... the one link that is shown copied: the note goes (the seat that shows no link keeps nothing up)', env.$('links-note').hidden);
    const lone = runLobby('', BOTS());
    choose(lone, 1, 'friend');
    choose(lone, 2, 'friend');
    copyOf(lone, 1).click();
    check('a link copied for one seat only: no note (the other seat was never sent)', lone.$('links-note').hidden);
    choose(lone, 3, 'easy');
    check('... and a change that changes the link that was sent brings it (the link of the seat that was never sent changes too: it does not matter)', !lone.$('links-note').hidden);
    copyOf(lone, 0).click();
    check('... copying the other seat\'s link does not take it away: the link that was sent is still the old one', !lone.$('links-note').hidden);
    copyOf(lone, 1).click();
    check('... copying that seat\'s link does', lone.$('links-note').hidden);
}
{   // the copy: the browser's own API, else the copy command (and what that says is told: a copy that the browser did not do is not "Copied")
    const text = (env, i) => copyOf(env, i).textContent;
    const api = runLobby('', BOTS());
    choose(api, 1, 'friend');
    copyOf(api, 0).click();
    check('where the browser has the clipboard\'s API the copy command is not used', api.commanded.length === 0 && api.copied.length === 1);
    const plain = runLobby('', BOTS(), { clipboard: false });
    choose(plain, 1, 'friend');
    const link = inviteLinks(plain)[0];
    const before = plain.body.children.length;
    copyOf(plain, 0).click();
    same('a browser without it: the copy command copies the link (from a field that is gone again) and the button says Copied', [plain.commanded, plain.body.children.length, text(plain, 0)], [[{ cmd: 'copy', value: link }], before, 'Copied']);
    const no = runLobby('', BOTS(), { clipboard: false, copyResult: false });
    choose(no, 1, 'friend');
    copyOf(no, 0).click();
    same('... a copy command that says no (the browser did not copy): the button asks the person to select the link and copy it, and no field is left behind', [text(no, 0), no.body.children.length], ['Select the link and copy it', 0]);
    const refuses = runLobby('', BOTS(), { clipboard: false, copyResult: 'throws' });
    choose(refuses, 1, 'friend');
    copyOf(refuses, 0).click();
    same('... a copy command that throws says the same', [text(refuses, 0), refuses.body.children.length], ['Select the link and copy it', 0]);
    check('... and the link counts as sent in every case (the person copies it by hand): changing the map after it brings the note', [plain, no, refuses].every((e) => { pickMap(e, 'small'); return !e.$('links-note').hidden; }));
}
{   // the line under START is a live region: it is written only when its text changes (a screen reader says a live region again whenever it is written)
    const env = runLobby('', BOTS());
    const note = env.$('start-note');
    const own = Object.getOwnPropertyDescriptor(El.prototype, 'textContent');
    let writes = 0;
    Object.defineProperty(note, 'textContent', { get() { return own.get.call(note); }, set(v) { writes++; own.set.call(note, v); } });
    choose(env, 1, 'easy');
    choose(env, 2, 'hard');
    sit(env, 3);
    pickMap(env, 'small');
    pressTeam(env, 0, 1);
    pressTeam(env, 1, 1);
    check('choices that leave the line as it is (bots, a seat for You, a map, the teams) do not write it', writes === 0, String(writes));
    choose(env, 1, 'friend');
    same('a Friend seat changes it: one write, with the new text', [writes, note.textContent], [1, 'Starts when your friend is in (the first player in the room can start sooner).']);
    choose(env, 2, 'easy');
    choose(env, 0, 'hard');
    check('... and a choice that leaves it as it is again does not write it', writes === 1, String(writes));
    choose(env, 2, 'friend');
    check('... the second Friend seat changes the words: another write', writes === 2 && /^Starts when your friends are in/.test(note.textContent), String(writes));
    env.win.listeners.pageshow.forEach((fn) => fn({ persisted: true }));
    check('a page that comes back from the browser\'s memory shows the card again and writes nothing that did not change', writes === 2, String(writes));
}
{   // a reload keeps the room of the tab (the links that were sent stay good); START lets go of it; the page that comes back from memory has a room of its own
    const env = runLobby('', BOTS());
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
    same('a reload after the map changed keeps the room of that map (the tab saved it when it was made), with the block of that map', [blockText(inviteLinks(third)[0]), third.param(inviteLinks(third)[0], 'room') === reloaded.param(inviteLinks(reloaded)[0], 'room')], [cardBlockText('islands'), true]);
    same('... the tab saved it as { code, map, teams, used }: the card\'s map and teams (the block of its links is made of them) and the links that were copied', [JSON.parse(reloaded.session.data['ants-match-room']).map, JSON.parse(reloaded.session.data['ants-match-room']).teams, Object.keys(JSON.parse(reloaded.session.data['ants-match-room'])).sort()], ['islands', 'ffa', ['code', 'map', 'teams', 'used']]);
    // what a tab holds from another time: { code, map, teams, used } as the page writes it (a code of eight characters), here for another map than the card's
    const held = (extra) => ({ 'ants-match-room': JSON.stringify(Object.assign({ code: 'abcdefgh', map: 'tiny', teams: 'ffa', used: {} }, extra)) });
    const other = runLobby('', reloaded.storage.data, { session: held({}) });
    check('a room that the tab holds for another map is not used (the choices changed meanwhile: the block of its links would be the other map\'s): the page makes a room of its own, for the card\'s map', CODE.test(other.param(inviteLinks(other)[0], 'room')) && blockText(inviteLinks(other)[0]) === cardBlockText('islands') && other.param(inviteLinks(other)[0], 'room') !== 'abcdefgh');
    check('... and with no link copied there is no note', other.$('links-note').hidden);
    const fitting = runLobby('', reloaded.storage.data, { session: held({ map: 'islands' }) });
    same('a room that the tab holds for the card\'s map and teams is the one that the links use (its code is one that the page makes: eight characters)', [fitting.param(inviteLinks(fitting)[0], 'room'), blockText(inviteLinks(fitting)[0])], ['abcdefgh', cardBlockText('islands')]);
    const otherTeams = runLobby('', reloaded.storage.data, { session: held({ map: 'islands', teams: '0+1' }) });
    check('... another team is another room too (the teams are in the block)', otherTeams.param(inviteLinks(otherTeams)[0], 'room') !== 'abcdefgh' && blockText(inviteLinks(otherTeams)[0]) === cardBlockText('islands'));
    const kept = runLobby('', reloaded.storage.data, { session: held({ used: { 1: 'the link that was copied' } }) });
    check('... but what was copied stays known (the map changed in another tab): the link of that seat is not that any more, so the note is up', !kept.$('links-note').hidden && blockText(inviteLinks(kept)[0]) === cardBlockText('islands'));
    const unseen = runLobby('', reloaded.storage.data, { session: held({ used: { 0: 'a link of the seat of You', 3: 'a link of a seat that is a bot' } }) });
    check('... a link that was copied for a seat that shows no link (You, a bot) keeps nothing up', unseen.$('links-note').hidden);
    const longUsed = runLobby('', reloaded.storage.data, { session: held({ used: { 1: 'x'.repeat(1001) } }) });
    const textUsed = runLobby('', reloaded.storage.data, { session: held({ used: 'the links that were copied' }) });
    const numberUsed = runLobby('', reloaded.storage.data, { session: held({ used: 5 }) });
    const listUsed = runLobby('', reloaded.storage.data, { session: held({ used: ['a', 'the link that was copied'] }) });
    const badLink = runLobby('', reloaded.storage.data, { session: held({ used: { 1: 5, 2: null } }) });
    check('... a record that is too long, is no object or holds no text is not believed (the first versions of the page kept the links as one text: that is not read either)', [longUsed, textUsed, numberUsed, listUsed, badLink].every((e) => e.$('links-note').hidden));
    for (const junk of ['not json', '{}', '{"code":5}', '{"code":"x"}', 'null', '[]', '"abcdefgh"',
                        '{"code":"abcdefgh"}', '{"code":"abcdefgh","used":{}}',                                                        // (no map, no teams: an entry of an earlier shape does not fit)
                        '{"code":"abcdefgh","map":"tiny","teams":"ffa"}', '{"code":"abcdefgh","map":"islands","teams":"0+1"}',          // (another map, other teams)
                        '{"code":"abcdefgh","map":5,"teams":"ffa"}', '{"code":"abcdefgh","map":"islands","teams":5}', '{"code":"abcdefgh","map":"islands"}', '{"code":"abcdefgh","teams":"ffa"}',
                        '{"code":"ABCDEFGH","map":"islands","teams":"ffa"}', '{"code":"abcdefg","map":"islands","teams":"ffa"}', '{"code":"abcdefghj","map":"islands","teams":"ffa"}',      // (not eight characters of the page's alphabet)
                        '{"code":"abcd efg","map":"islands","teams":"ffa"}', '{"code":"abcdefg1","map":"islands","teams":"ffa"}', '{"code":"abcdef-h","map":"islands","teams":"ffa"}',
                        '{"code":"demo-islands-4p-abcdef","map":"islands","teams":"ffa"}', '{"code":"demo-islands-4p-t01-abcdef","map":"islands","teams":"0+1"}', '{"code":"demo-islands-4p-abcdef","used":{}}']) {
        const j = runLobby('', reloaded.storage.data, { session: { 'ants-match-room': junk } });
        check('a room entry that is junk or of another shape (' + junk.slice(0, 48) + ') is not used: the page makes its own', CODE.test(j.param(inviteLinks(j)[0], 'room')) && j.param(inviteLinks(j)[0], 'room') !== 'abcdefgh' && blockText(inviteLinks(j)[0]) === cardBlockText('islands'));
    }
    // START takes this tab into the match with the same room, and the tab lets go of it
    const start = runLobby('', BOTS());
    start.type('player-name', 'Ann');
    choose(start, 1, 'friend');
    const startRoom = start.param(inviteLinks(start)[0], 'room');
    start.$('play').click();
    check('START: one assignment, this tab (no window), the same room as the invitations, with the same create block', start.assigned.length === 1 && start.opened.length === 0 && start.param(start.assigned[0], 'room') === startRoom && blockText(start.assigned[0]) === blockText(inviteLinks(start)[0]) && blockText(start.assigned[0]) === BLOCK_TREASURE4, JSON.stringify(start.assigned));
    check('... the tab still holds the room while the game page loads (the navigation may not happen at all: the links on the card stay the room\'s)', start.session.data['ants-match-room'] !== undefined && JSON.parse(start.session.data['ants-match-room']).code === startRoom);
    start.$('play').click();
    check('a second click while the game page loads (a held Enter, a tap on a slow phone) goes to the same room, not to another one: the friend\'s link stays good', start.assigned.length === 2 && start.param(start.assigned[1], 'room') === startRoom && start.assigned[1] === start.assigned[0], JSON.stringify(start.assigned));
    (start.win.listeners.pagehide || []).forEach((fn) => fn({ persisted: false }));
    check('... the tab lets go of the room when it leaves the page (the next match is another room)', start.session.data['ants-match-room'] === undefined);
    start.win.listeners.pageshow.forEach((fn) => fn({ persisted: true }));
    check('a page that comes back from the browser\'s memory (Back) has a room of its own', start.param(inviteLinks(start)[0], 'room') !== startRoom && CODE.test(start.param(inviteLinks(start)[0], 'room')));
    let ok = true;
    try { const t = runLobby('', BOTS(), { noSession: true }); choose(t, 1, 'friend'); ok = inviteLinks(t).length === 1; } catch (e) { ok = false; }
    check('a browser with no session storage (or one that refuses it) still has its links', ok);
    const away = runLobby('', BOTS());                       // a page that is left for another reason than START (a link, a Join, a close) keeps its room: Back brings the same links
    choose(away, 1, 'friend');
    const awayRoom = away.param(inviteLinks(away)[0], 'room');
    (away.win.listeners.pagehide || []).forEach((fn) => fn({ persisted: true }));
    check('a page that is left without START keeps the room of the tab (the links that were sent stay good)', away.session.data['ants-match-room'] !== undefined && JSON.parse(away.session.data['ants-match-room']).code === awayRoom);
}
{   // Back from the browser's memory without START (from a Join, say): the card's room and what was copied stay
    const env = runLobby('', BOTS());
    choose(env, 1, 'friend');
    const link = inviteLinks(env)[0];
    copyOf(env, 0).click();
    env.win.listeners.pageshow.forEach((fn) => fn({ persisted: true }));
    same('a page that comes back from the browser\'s memory without START shows the same links, still in use: no note', [inviteLinks(env), env.$('links-note').hidden], [[link], true]);
    choose(env, 3, 'hard');
    check('... and a change after that brings the note (what was copied is not forgotten)', !env.$('links-note').hidden);
}
{   // START
    const env = runLobby('', BOTS());
    env.type('player-name', 'Ann');
    choose(env, 1, 'friend');
    choose(env, 2, 'hard');
    sit(env, 3);
    env.$('play').click();
    const u = env.assigned[0] || '';
    same('START: this tab goes to the game page with the room, your seat (Black), the plan (Green a Medium bot, Red the friend, Blue a Hard bot, You at Black), your name, the picture and the people to wait for (You and the friend)',
         [env.assigned.length, new URL(u).origin + new URL(u).pathname, env.param(u, 'join'), env.param(u, 'seat'), env.param(u, 'fill'), env.param(u, 'name'), env.param(u, 'aspect'), env.param(u, 'start')], [1, 'https://play.test/', '/ws', '3', 'medium,none,hard,none', 'Ann', '16:9', '2']);
    check('... a room of a new code (eight characters) whose create block, right after it, is four seats on the map; no teams parameter, no key, no embed, nothing opened in another window', CODE.test(env.param(u, 'room')) && blockText(u) === BLOCK_TREASURE4 && env.param(u, 'teams') === null && env.param(u, 'key') === null && env.param(u, 'embed') === null && env.opened.length === 0 && env.replaced.length === 0, u);
    same('... the whole address, in this order: the door, the code, the block, the seat, the plan, the name, the picture, the people', u, 'https://play.test/?join=/ws&room=' + env.param(u, 'room') + BLOCK_TREASURE4 + '&seat=3&fill=medium,none,hard,none&name=Ann&aspect=16:9&start=2');
    same('... the choices are remembered (ants-match) and the name (ants.name), and nothing else of the page\'s storage was written', [Object.keys(env.storage.data).sort(), JSON.parse(env.storage.data['ants-match']).you, env.storage.data['ants.name']], [['ants-match', 'ants.name'], 3, 'Ann']);
    const bots = runLobby('', BOTS());
    bots.$('play').click();
    const b = bots.assigned[0] || '';
    same('START with bots only: no Friend row, so one person to wait for (the leader\'s game presses START at once), a Medium bot in every other seat, an empty name is a random one of the page\'s', [bots.param(b, 'start'), bots.param(b, 'fill'), bots.param(b, 'seat'), randomName(bots.param(b, 'name'))], ['1', 'none,medium,medium,medium', '0', true]);
    check('... and nothing was remembered by it but the name (the choices are the ones that were remembered)', Object.keys(bots.storage.data).sort().join() === 'ants-match,ants.name' && bots.storage.data['ants-match'] === BOTS()['ants-match'], JSON.stringify(bots.storage.data));
    const first = runLobby('', {});
    first.$('play').click();
    const f = first.assigned[0] || '';
    same('START on a first visit: three Friends, so four people to wait for and no plan (no fill parameter), Green\'s seat, and the room of the invitations (its code and its create block)', [first.param(f, 'start'), first.param(f, 'fill'), first.param(f, 'seat'), first.param(f, 'room') === first.param(inviteLinks(first)[0], 'room'), blockText(f) === blockText(inviteLinks(first)[0]) && blockText(f) === BLOCK_TREASURE4], ['4', null, '0', true, true]);
    check('... and nothing was remembered by it but the name (no choice was made)', Object.keys(first.storage.data).join() === 'ants.name', JSON.stringify(first.storage.data));
    const four = runLobby('', BOTS());
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
{   // nobody else in the match: START plays a game for one on this computer (a room of the game server needs two people to start)
    const env = runLobby('', BOTS());
    choose(env, 1, 'nobody');
    choose(env, 2, 'nobody');
    check('two seats play: START is on and starts a room at once', !env.$('play').disabled && env.$('start-note').textContent === 'Starts at once, in this tab. Bots gather food, raid and fight back.');
    choose(env, 3, 'nobody');
    same('one seat plays (You): START is still on, and the line under it says that it is a game for one on this computer', [!env.$('play').disabled, env.$('start-note').textContent], [true, 'Starts at once, on this computer: just you on the map, no opponents.']);
    same('... the Team switches are gone (nothing to choose) and there is no invitation', [env.$('teams-line').hidden, switchesShown(env), env.$('invites').hidden], [true, ['', '', '', ''], true]);
    env.type('player-name', 'Ann');
    env.$('play').click();
    const u = env.assigned[0] || '';
    same('... and START goes to the game page of this computer (this tab): the map, your name and the picture, no room (so no create block either), no bots, no seat, no people to wait for', [env.assigned.length, new URL(u).origin + new URL(u).pathname, env.param(u, 'map'), env.param(u, 'name'), env.param(u, 'aspect'), ['join', 'room', 'roommap', 'roomseats', 'roomteams', 'roomleaderstart', 'platform', 'bots', 'teams', 'fill', 'start', 'seat', 'key'].map((k) => env.param(u, k))],
         [1, 'https://play.test/play.html', 'treasure', 'Ann', '16:9', [null, null, null, null, null, null, null, null, null, null, null, null, null]]);
    check('... in this tab (no window), with no room made for it (the tab holds none) and nothing remembered but the choices and the name', env.opened.length === 0 && env.session.data['ants-match-room'] === undefined && Object.keys(env.storage.data).sort().join() === 'ants-match,ants.name', JSON.stringify([env.session.data, env.storage.data]));
    const nameless = runLobby('', BOTS());
    for (const s of [1, 2, 3]) choose(nameless, s, 'nobody');
    pickMap(nameless, 'islands');
    nameless.$('play').click();
    const nu = nameless.assigned[0] || '';
    same('an empty name is the one that the card shows under You (a random one of the page\'s, as for a room: the line under the field says so), on the map that the card has', [nameless.param(nu, 'map'), randomName(nameless.param(nu, 'name')), nameless.param(nu, 'name') === nameless.$('seat-name-0').textContent], ['islands', true, true]);
    const classic = runLobby('?aspect=4:3', BOTS());
    for (const s of [1, 2, 3]) choose(classic, s, 'nobody');
    classic.$('play').click();
    check('the picture of the page goes with a game for one too (Classic 4:3)', classic.assigned.length === 1 && new URL(classic.assigned[0]).pathname === '/play.html' && classic.param(classic.assigned[0], 'aspect') === '4:3', JSON.stringify(classic.assigned));
    const kept = runLobby('', {});                         // a first visit: three Friends, so the tab holds a room for their links
    const keptRoom = kept.param(inviteLinks(kept)[0], 'room');
    for (const s of [1, 2, 3]) choose(kept, s, 'nobody');
    kept.$('play').click();
    (kept.win.listeners.pagehide || []).forEach((fn) => fn({ persisted: true }));
    check('a game for one is in no room: the tab keeps the room of its invitations when it leaves', kept.assigned.length === 1 && new URL(kept.assigned[0]).pathname === '/play.html' && kept.session.data['ants-match-room'] !== undefined && JSON.parse(kept.session.data['ants-match-room']).code === keptRoom, JSON.stringify(kept.session.data));
    kept.win.listeners.pageshow.forEach((fn) => fn({ persisted: true }));
    choose(kept, 1, 'friend');
    check('... and Back from the game brings the same links (the friends who were sent them wait in that room)', kept.param(inviteLinks(kept)[0], 'room') === keptRoom);
    choose(env, 2, 'friend');
    same('a Friend seat is a player: START says that it waits for the friend', [!env.$('play').disabled, env.$('start-note').textContent], [true, 'Starts when your friend is in (the first player in the room can start sooner).']);
    choose(env, 3, 'friend');
    check('... two friends: "your friends are in"', env.$('start-note').textContent === 'Starts when your friends are in (the first player in the room can start sooner).');
    sit(env, 1);
    choose(env, 2, 'nobody');
    choose(env, 3, 'nobody');
    same('Sit here moves what counts: You at Red and Green a Medium bot (it kept its choice): two players, START starts a room', [youSeats(env), env.$('start-note').textContent], [[1], 'Starts at once, in this tab. Bots gather food, raid and fight back.']);
    choose(env, 0, 'nobody');
    same('... Green set to Nobody: one player, a game for one (which is Green\'s colour: the line says so when You sit elsewhere)', env.$('start-note').textContent, 'Starts at once, on this computer: just you on the map, no opponents. Alone you play Green.');
    env.$('play').click();
    check('... and START plays it (on this computer, as ever)', env.assigned.length === 2 && new URL(env.assigned[1]).pathname === '/play.html' && env.param(env.assigned[1], 'join') === null, JSON.stringify(env.assigned));
}
{   // the Teams: a Team 1 and a Team 2 switch on each seat that plays; two seats on the same switch are a team, which is in the room's create block (roomteams), not in a teams parameter and not in the code
    const env = runLobby('', BOTS());
    choose(env, 1, 'nobody');
    same('Red is Nobody: three seats play (Green, Blue, Black) and have the switches, Red has none, and no switch is on', [env.$('teams-line').hidden, switchesShown(env), env.$('teams-line').textContent], [false, ['-', '', '-', '-'], FFA_LINE]);
    pressTeam(env, 2, 2);
    same('Team 2 on Blue: one seat on a switch is no team (free for all: the line still says how to make one)', [switchesShown(env), env.$('teams-line').textContent, env.$('team-2-2').getAttribute('aria-pressed'), env.$('team-2-1').getAttribute('aria-pressed')], [['-', '', '2', '-'], FFA_LINE, 'true', 'false']);
    pressTeam(env, 3, 2);
    same('... Team 2 on Black too: Blue + Black against Green (Green plays alone, nothing is on for it), and the line says so', [switchesShown(env), env.$('teams-line').textContent], [['-', '', '2', '2'], 'Teams: Blue + Black against Green.']);
    same('... a team of two is full: Green cannot take Team 2 (dimmed), everybody else can take the switches that they have', [switchesDimmed(env), env.$('team-0-2').getAttribute('aria-disabled'), env.$('team-0-1').getAttribute('aria-disabled')], [['2', '', '', ''], 'true', null]);
    choose(env, 2, 'friend');
    const teamed = env.param(inviteLinks(env)[0], 'room');
    same('a team stays while its two seats play; a Friend seat gives the room a create block that names it (&roomteams=2%2B3: the "+" is encoded), and no teams parameter', [env.$('teams-line').textContent, blockText(inviteLinks(env)[0]), blockParams(env, inviteLinks(env)[0]), env.param(inviteLinks(env)[0], 'teams'), CODE.test(teamed)],
         ['Teams: Blue + Black against Green.', '&roommap=treasure&roomseats=4&roomteams=2%2B3', ['treasure', '4', '2+3', null], null, true]);
    choose(env, 3, 'nobody');
    const ffa = env.param(inviteLinks(env)[0], 'room');
    same('a seat that leaves the match takes the team with it: two seats play, the switches are gone, a block with no teams (and another room: the first Hello of a code is the one that is read, so the teams are a new room\'s)', [env.$('teams-line').hidden, switchesShown(env), blockText(inviteLinks(env)[0]), CODE.test(ffa) && ffa !== teamed], [true, ['', '', '', ''], BLOCK_TREASURE4, true]);
    choose(env, 3, 'hard');
    same('... and it stays free for all when the seat comes back (a team is made again by a person)', [switchesShown(env), env.$('teams-line').textContent], [['-', '', '-', '-'], FFA_LINE]);
    sit(env, 1);
    same('Sit here changes the seats that play, and the switches follow (Red is You, Green a bot, Blue a Friend, Black Hard: four play, every seat has them)', [switchesShown(env), env.$('teams-line').hidden], [['-', '-', '-', '-'], false]);
    pressTeam(env, 0, 1);
    same('Team 1 on Green: one seat is no team, so nothing is on for the others', [switchesShown(env), switchesDimmed(env)], [['1', '-', '-', '-'], ['', '', '', '']]);
    pressTeam(env, 1, 1);
    same('Team 1 on Red (You are at Red): Green + Red against Blue + Black; Blue and Black show Team 2 although nobody pressed it, and Team 1 is dimmed on them (it is full), Team 2 on Green and Red',
         [switchesShown(env), switchesDimmed(env), env.$('teams-line').textContent], [['1', '1', '2', '2'], ['2', '2', '1', '1'], 'Teams: Green + Red against Blue + Black.']);
    pressTeam(env, 2, 1);
    same('pressing a dimmed switch changes nothing and says why in the line under the seats (a team is two colours)', [switchesShown(env), env.$('teams-line').textContent, JSON.parse(env.storage.data['ants-match']).sides], [['1', '1', '2', '2'], 'Team 1 has two colours already. Press one of them to take it off first.', [1, 1, 0, 0]]);
    {   // a person who presses it again asks again: the reason is written again each time (a live region says what is written, not what stays)
        const line = env.$('teams-line');
        const descriptor = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(line), 'textContent');
        let writes = 0;
        Object.defineProperty(line, 'textContent', { get() { return descriptor.get.call(line); }, set(v) { writes++; descriptor.set.call(line, v); }, configurable: true });
        pressTeam(env, 2, 1);
        pressTeam(env, 2, 1);
        same('... pressed again it is written again each time', [writes, line.textContent], [2, 'Team 1 has two colours already. Press one of them to take it off first.']);
        delete line.textContent;
    }
    choose(env, 2, 'hard');
    same('... and the next choice puts the teams back in the line', env.$('teams-line').textContent, 'Teams: Green + Red against Blue + Black.');
    pressTeam(env, 1, 1);
    pressTeam(env, 2, 1);
    same('to put Blue with Green instead: Red takes its Team 1 off, Blue takes Team 1 (Green + Blue against Red + Black; Red and Black show Team 2)', [switchesShown(env), env.$('teams-line').textContent], [['1', '2', '1', '2'], 'Teams: Green + Blue against Red + Black.']);
    env.$('play').click();
    const u = env.assigned[0] || '';
    check('START with a team: the room\'s create block names it (&roomteams=0%2B2: the pair, the lower seat first), and there is no teams parameter', blockText(u) === cardBlockText('treasure', '0+2') && JSON.stringify(blockParams(env, u)) === JSON.stringify(['treasure', '4', '0+2', null]) && env.param(u, 'teams') === null && !/[?&]teams=/.test(u), u);
    same('... remembered: the team that the switches make, and the switches', [JSON.parse(env.storage.data['ants-match']).teams, JSON.parse(env.storage.data['ants-match']).sides], ['0+2', [1, 0, 1, 0]]);
    const back = runLobby('', env.storage.data);
    same('the next visit comes back with the switches (the two that were pressed are on, the other two show the other team)', [switchesShown(back), back.$('teams-line').textContent], [['1', '2', '1', '2'], 'Teams: Green + Blue against Red + Black.']);
    choose(back, 3, 'friend');
    const withTeam = back.param(inviteLinks(back)[0], 'room');
    check('a Friend at Black gives the room a create block with the team (&roomteams=0%2B2: Green + Blue)', invites(back).length === 1 && CODE.test(withTeam) && blockText(inviteLinks(back)[0]) === cardBlockText('treasure', '0+2'), JSON.stringify(inviteLinks(back)));
    pressTeam(back, 3, 2);
    same('pressing the Team 2 that Black shows (nobody pressed it) takes the teams away: no switch is on, free for all', [switchesShown(back), switchesDimmed(back), back.$('teams-line').textContent, JSON.parse(back.storage.data['ants-match']).sides, JSON.parse(back.storage.data['ants-match']).teams], [['-', '-', '-', '-'], ['', '', '', ''], FFA_LINE, [0, 0, 0, 0], 'ffa']);
    check('... and the room made for those choices has a block with no teams, and a code of its own', invites(back).length === 1 && CODE.test(back.param(inviteLinks(back)[0], 'room')) && back.param(inviteLinks(back)[0], 'room') !== withTeam && blockText(inviteLinks(back)[0]) === BLOCK_TREASURE4, JSON.stringify(inviteLinks(back)));
    check('the switches are buttons that say whether they are on (aria-pressed) and are named by their seat (the button\'s own name: Team 1 for Green; and a group named for its colour)', [0, 1, 2, 3].every((x) => [1, 2].every((k) => new RegExp('<button type="button" class="tbtn" id="team-' + x + '-' + k + '" aria-label="Team ' + k + ' for ' + ['Green', 'Red', 'Blue', 'Black'][x] + '" aria-pressed="false">Team ' + k + '</button>').test(lobbyText)) && new RegExp('<span class="teamset" id="team-set-' + x + '" role="group" aria-label="' + ['Green', 'Red', 'Blue', 'Black'][x] + '\'s team" hidden>').test(lobbyText)));
    {   // a press that the other seats hold: Team 1 on Green, then Team 2 on Red and Blue show Black on Team 1 as well; a press that is accepted always shows
        const e = runLobby('', BOTS());
        pressTeam(e, 0, 1);
        pressTeam(e, 1, 2);
        pressTeam(e, 2, 2);
        same('Team 1 on Green, Team 2 on Red and on Blue: Green + Black against Red + Blue, and Green and Black show Team 1', [switchesShown(e), e.$('teams-line').textContent], [['1', '2', '2', '1'], 'Teams: Green + Black against Red + Blue.']);
        pressTeam(e, 0, 1);
        same('... pressing Team 1 on Green (what it pressed is held by the others now) takes the teams away, as Black\'s does: every switch is off', [switchesShown(e), e.$('teams-line').textContent], [['-', '-', '-', '-'], FFA_LINE]);
        pressTeam(e, 0, 1);
        pressTeam(e, 1, 1);
        pressTeam(e, 0, 2);
        same('Green + Red against Blue + Black: Green cannot take the Team 2 that Blue and Black show (nobody pressed it), and the line says to clear the teams first', [switchesShown(e), e.$('teams-line').textContent], [['1', '1', '2', '2'], 'Team 2 is the other two colours already. Press a lit switch to clear the teams first.']);
        pressTeam(e, 3, 2);
        same('... and a press on a lit switch does clear them', [switchesShown(e), e.$('teams-line').textContent], [['-', '-', '-', '-'], FFA_LINE]);
    }
    const old = runLobby('', { 'ants-match': JSON.stringify({ map: 'treasure', you: 0, seats: ['medium', 'medium', 'medium', 'medium'], teams: '0+3' }) });
    same('a state of the first versions of the card (a team and no switches) shows its pair on Team 1 and the other two on Team 2', [switchesShown(old), old.$('teams-line').textContent], [['1', '2', '2', '1'], 'Teams: Green + Black against Red + Blue.']);
    const junk = runLobby('', { 'ants-match': JSON.stringify({ map: 'treasure', you: 0, seats: ['medium', 'medium', 'medium', 'medium'], teams: '0+3', sides: [1, 1, 1, 0] }) });
    same('switches that put three seats on one switch are no switches: free for all (the team that was written is not used either)', [switchesShown(junk), junk.$('teams-line').textContent], [['-', '-', '-', '-'], FFA_LINE]);
}
{   // the map: its preview and Map Info line, the setup screen's own; it is remembered
    const info = (() => { const m = /var MAP_INFO = \{([\s\S]*?)\};/.exec(lobbyText); return new Function('return {' + m[1] + '};')(); })();
    same('the page has the Map Info line of each of the six maps', Object.keys(info).sort(), MAP_KEYS.slice().sort());
    const env = runLobby('', BOTS());
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
    same('the map is in START\'s room (its create block: &roommap=islands&roomseats=4) and remembered with the rest', [blockText(env.assigned[0]), JSON.parse(env.storage.data['ants-match']).map], [cardBlockText('islands'), 'islands']);
    const again = runLobby('', env.storage.data);
    same('the next visit comes back on that map, with its preview', [again.$('map-pick').value, again.$('map-preview').src], ['islands', 'front/preview_islands.png']);
}
{   // what the earlier pages left in the browser is read for a first visit, and never written
    const old = { 'ants-solo-seats': 'easy,none,hard', 'ants-solo-teams': '0+3', 'ants-four-map': 'small', 'ants-four-players': '4', 'ants-four-fill': 'none,medium,medium,medium', 'ants-four-teams': '0+1' };
    const env = runLobby('', old);
    same('the opponents of a game on this computer, its team and the last map: Red Easy, Blue Nobody, Black Hard, Green and You with Black as a team (Team 1 for both, three seats play: Red alone), Small', [seatWords(env), youSeats(env), switchesShown(env), env.$('teams-line').textContent, env.$('map-pick').value, env.$('map-preview').src], [['friend', 'easy', 'nobody', 'hard'], [0], ['1', '-', '', '1'], 'Teams: Green + Black against Red.', 'small', 'front/preview_small.png']);
    choose(env, 2, 'friend');
    same('... what the card remembers is its own key; the old keys are left as they were and nothing was added', [Object.keys(env.storage.data).sort(), env.storage.data['ants-solo-seats'], env.storage.data['ants-four-fill']], [Object.keys(old).concat(['ants-match']).sort(), 'easy,none,hard', 'none,medium,medium,medium']);
    const hosted = runLobby('', { 'ants-four-players': '3', 'ants-four-fill': 'none,easy,none,hard', 'ants-four-teams': '0+2', 'ants-four-map': 'tiny' });
    same('a browser that only hosted rooms: its room is the card (3 players: Red Easy, Blue a Friend, Black Nobody) with its team', [seatWords(hosted), switchesShown(hosted), hosted.$('teams-line').textContent, hosted.$('map-pick').value, hosted.$('invites').hidden === false && invites(hosted).length === 1 && invites(hosted)[0].children[0].textContent], [['friend', 'easy', 'friend', 'nobody'], ['1', '-', '1', ''], 'Teams: Green + Blue against Red.', 'tiny', 'Blue']);
    const legacy = runLobby('', { 'ants-solo-bots': 'hard' });
    same('the opponents that the first versions remembered (Hard) are in all three seats', seatWords(legacy), ['friend', 'hard', 'hard', 'hard']);
    const legacyNone = runLobby('', { 'ants-solo-bots': 'none' });
    same('... and None is Nobody in all three (the original\'s single player: START plays it, on this computer)', [seatWords(legacyNone), !legacyNone.$('play').disabled, legacyNone.$('start-note').textContent], [['friend', 'nobody', 'nobody', 'nobody'], true, 'Starts at once, on this computer: just you on the map, no opponents.']);
    const own = runLobby('', Object.assign({ 'ants-match': JSON.stringify({ map: 'gauntlet', you: 1, seats: ['hard', 'medium', 'friend', 'easy'], teams: 'ffa' }) }, old));
    same('a state of the card\'s own beats all of them', [youSeats(own), seatWords(own), own.$('map-pick').value], [[1], ['hard', 'medium', 'friend', 'easy'], 'gauntlet']);
    for (const junk of ['not json', '{}', '{"map":"treasure","you":9,"seats":["medium","medium","medium","medium"],"teams":"ffa"}', '{"map":"treasure","you":0,"seats":["medium","medium","medium","HARD"],"teams":"ffa"}', 'null', '[]']) {
        const j = runLobby('', Object.assign({ 'ants-match': junk }, old));
        same('a remembered state that is junk (' + junk.slice(0, 40) + ') is not used: the first visit\'s', [seatWords(j), j.$('map-pick').value], [['friend', 'easy', 'nobody', 'hard'], 'small']);
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
    let alone = 0;
    const env = runLobby('', {});
    const names = ['Ann', 'Bob & <b>x</b>', '', 'x'.repeat(40), 'a=b&seat=2&start=4&room=z&fill=h', 'Zoë', 'Bot (x)', '  Max  ', '--name y', '%00'];
    for (let step = 0; step < 4000; step++) {
        const what = rnd(10);
        if (what < 4) choose(env, rnd(4), WORDS5[rnd(5)]);
        else if (what < 6) sit(env, rnd(4));
        else if (what < 7) pickMap(env, MAP_KEYS[rnd(6)]);
        else if (what < 8) { const here = [0, 1, 2, 3].filter((x) => !env.$('team-set-' + x).hidden); if (here.length) pressTeam(env, here[rnd(here.length)], 1 + rnd(2)); }
        else if (what < 9) env.type('player-name', names[rnd(names.length)]);
        else if (invites(env).length) copyOf(env, rnd(invites(env).length)).click();
        // what the card shows now
        const words = seatWords(env);
        const you = youSeats(env);
        if (you.length !== 1 || words.some((w) => w.charAt(0) === '?')) { wrong += ' rows ' + step; break; }
        const friends = [0, 1, 2, 3].filter((s) => s !== you[0] && words[s] === 'friend');
        const bots = [0, 1, 2, 3].filter((s) => s !== you[0] && ['easy', 'medium', 'hard'].indexOf(words[s]) !== -1);
        const playing = 1 + friends.length + bots.length;
        if (env.$('play').disabled) wrong += ' start ' + step;                          // (START is always on: a game for one is a game too)
        let teamNow = 'ffa';                                   // what the switches make: the pair (with four players the team that Green is in), else free for all
        {   // the switches and the line under the seats say what the room's create block says (the team that Green is in when four play), and no team has three seats
            const sw = switchesShown(env);
            const line = env.$('teams-line');
            const plays = [0, 1, 2, 3].filter((x) => x === you[0] || words[x] !== 'nobody');
            if (line.hidden !== (plays.length < 3) || sw.some((v, x) => (v === '') !== (plays.indexOf(x) === -1 || plays.length < 3))) wrong += ' switches ' + step;
            if (!line.hidden && !/^(Free for all\. For a team|Teams: \w+ \+ \w+ against |Team [12] has two colours already\. Press one of them to take it off first\.|Team [12] is the other two colours already\. Press a lit switch to clear the teams first\.)/.test(line.textContent)) wrong += ' line ' + step + ' ' + line.textContent;
            const on = sw.map((v) => (v === '1' || v === '2' ? Number(v) : 0));
            const twos = [1, 2].map((k) => [0, 1, 2, 3].filter((x) => on[x] === k)).filter((list) => list.length === 2);
            if ([1, 2].some((k) => on.filter((v) => v === k).length > 2)) wrong += ' crowded ' + step;
            if (twos.length) { const pair = plays.length === 4 && twos[0][0] !== 0 ? [0, 1, 2, 3].filter((x) => twos[0].indexOf(x) === -1) : twos[0]; teamNow = pair[0] + '+' + pair[1]; }
            if (!line.hidden && !/^Team [12] (has two|is the other two) colours/.test(line.textContent) && /^Teams: /.test(line.textContent) !== (teamNow !== 'ffa')) wrong += ' teamline ' + step;
        }
        if (invites(env).length !== friends.length || env.$('invites').hidden !== (friends.length === 0)) wrong += ' invites ' + step;
        const plan = [0, 1, 2, 3].map((s) => (bots.indexOf(s) !== -1 ? words[s] : 'none')).join(',');
        const roomPart = ['--room-map', env.$('map-pick').value, '--room-seats', '4', ...(teamNow === 'ffa' ? [] : ['--room-teams', teamNow])];       // (the create block of the card's room: its map, four seats and its teams, in the game's own arguments)
        for (const [k, link] of inviteLinks(env).entries()) {
            const args = P.joinArguments(new URL(link).search, true, 'play.test').args;
            invited++;
            const want = ['--join-url', 'wss://play.test/ws', '--room', env.param(link, 'room'), ...roomPart, '--seat', String(friends[k]), ...(bots.length ? ['--fill-bots', plan] : []), '--start-when', String(1 + friends.length)];
            if (JSON.stringify(args) !== JSON.stringify(want) || !CODE.test(env.param(link, 'room')) || blockText(link) !== cardBlockText(env.$('map-pick').value, teamNow)) wrong += ' invite ' + step + ' ' + JSON.stringify(args);
        }
        if (step % 7 === 0) {
            const before = env.assigned.length;
            const typed = nameCheck(env.$('player-name').value);
            env.$('play').click();
            if (!typed.ok) {                                   // (a bad name starts nothing)
                if (env.assigned.length !== before) wrong += ' badname ' + step;
                continue;
            }
            const u = env.assigned[before];
            started++;
            if (playing === 1) {                               // nobody else: the game of this computer on the card's map, under the name that the card shows under You, with no room and nothing of one
                alone++;
                const same4 = new URL(u).pathname === '/play.html' && env.param(u, 'map') === env.$('map-pick').value && env.param(u, 'name') === (typed.name || env.$('seat-name-' + you[0]).textContent) && env.param(u, 'aspect') === '16:9' &&
                              ['join', 'room', 'roommap', 'roomseats', 'roomteams', 'roomleaderstart', 'platform', 'bots', 'teams', 'fill', 'start', 'seat', 'key'].every((k) => env.param(u, k) === null);
                if (!same4) wrong += ' alone ' + step + ' ' + u;
                env.win.listeners.pageshow.forEach((fn) => fn({ persisted: true }));
                continue;
            }
            const args = P.joinArguments(new URL(u).search, true, 'play.test').args;
            const nameAt = args.indexOf('--name');
            const rest = args.filter((a, i) => i !== nameAt && i !== nameAt + 1);
            const want = ['--join-url', 'wss://play.test/ws', '--room', env.param(u, 'room'), ...roomPart, '--seat', String(you[0]), ...(bots.length ? ['--fill-bots', plan] : []), '--start-when', String(1 + friends.length)];
            if (JSON.stringify(rest) !== JSON.stringify(want) || !CODE.test(env.param(u, 'room')) || blockText(u) !== cardBlockText(env.$('map-pick').value, teamNow) || nameAt === -1 || args[nameAt + 1] !== (typed.name || env.$('seat-name-' + you[0]).textContent)) wrong += ' start ' + step + ' ' + JSON.stringify(args);
            if (nameAt !== -1 && !/^[\x20-\x7e]{1,32}$/.test(args[nameAt + 1])) wrong += ' name ' + step;
            if (env.param(u, 'roomteams') !== null) teamed++;
            env.win.listeners.pageshow.forEach((fn) => fn({ persisted: true }));         // (Back after START: the card is there again, with a room of its own)
        }
        if (wrong.length > 300) break;
    }
    check('4000 random clicks (seats, Sit here, map, teams, names, copies): every START (' + started + ', ' + alone + ' of them a game for one) and invitation (' + invited + ', ' + teamed + ' of the STARTs with a team in the block) is read by the game page as exactly what the card shows: a new code of eight characters, the create block (the card\'s map, four seats, its teams), your seat, the plan, the people to wait for, no teams parameter, no key; a bad name starts nothing:' + wrong, wrong === '' && started > 200 && alone >= 3 && invited > 500 && teamed > 10);
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
    check('a browser that refuses its storage still plays (nothing is remembered, nothing breaks)', env.assigned.length === 1 && env.param(env.assigned[0], 'start') === '4' && env.param(env.assigned[0], 'fill') === null);
}
// ... the addresses: ?map=...&players=1 plays on this computer after the name step; an address that names no players still hosts 4 (the card plays on this computer only with nobody else in the match)
{
    const env = runLobby('?map=small&players=1&fill=hard', { 'ants.name': 'Maya' });
    check('?map=small&players=1&fill=hard asks for the name first (button Play), and nothing starts before', !env.$('who-go').hidden && env.$('who-go').textContent === 'Play' && /computer/.test(env.$('who-title').textContent) && env.assigned.length === 0 && env.$('cards').hidden && env.$('how').hidden);
    check('... the step explains itself in its own line, and the general line is not shown', !env.$('who-step-hint').hidden && /game on this computer/.test(env.$('who-step-hint').textContent) && env.$('who-general').hidden);
    env.$('who-go').click();
    const u = env.assigned[0] || '';
    check('... then the game of this computer on that map with the Hard bots and the name', env.assigned.length === 1 && env.param(u, 'map') === 'small' && env.param(u, 'bots') === 'hard' && env.param(u, 'name') === 'Maya', u);
    check('... in no room: no door, no code, and nothing of a create block (a game on this computer makes none)', ['join', 'room', 'roommap', 'roomseats', 'roomteams', 'roomleaderstart', 'platform', 'seat', 'start'].every((k) => env.param(u, k) === null) && env.replaced.length === 0, u);
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
    const hostedAt = String(room.replaced[0] && room.replaced[0][2]);
    check('... the room of 4 on that map: a new code of eight characters, shown in two groups of four, and the map and four seats as its create block (in the address of the page)', room.roomStarted() && /^\/\?room=[a-z2-9]{8}&roommap=small&roomseats=4$/.test(hostedAt) && /^[a-z2-9]{4} [a-z2-9]{4}$/.test(room.$('room-code').textContent) && room.$('room-map').textContent === 'Small map, 4 players' && room.assigned.length === 0, hostedAt);
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
    check('"Play in this tab": this tab goes to the game page with the room (the plain code, not the two groups that the panel shows), the bots of the leader\'s START and the name (no seat, no embed, no new window)',
          env.assigned.length === 1 && env.opened.length === 0 && env.param(u, 'join') === '/ws' && env.param(u, 'room') === env.$('room-code').textContent.replace(' ', '') && /^[a-z2-9]{8}$/.test(env.param(u, 'room')) && env.param(u, 'fill') === 'medium' && env.param(u, 'name') === 'Ann' && env.param(u, 'seat') === null && env.param(u, 'embed') === null, u);
    same('... the whole address: the door, the code, the create block of the room (Treasure, three seats), the bots, the name', u, 'https://play.test/?join=/ws&room=' + env.param(u, 'room') + '&roommap=treasure&roomseats=3&fill=medium&name=Ann');
    const bad = openRoom('?map=treasure&players=3', {});
    bad.type('player-name', 'Bot (x)');
    bad.$('play-tab').click();
    check('... a bad name starts nothing', bad.assigned.length === 0 && bad.$('name-msg').textContent.length > 8);
    const shown = openRoom('?map=treasure&players=3', {});
    shown.rowButton(1, 'Play here').click();
    check('a game that plays on the page shows the note about the frames', shown.$('frames-note').hidden === false);
}
// ... the CREATE BLOCK of a room that the page made (protocol 15) is in EVERY address that the room's panel makes, through one helper of the page: the games of its seats (a frame, a window), the link of a seat, the link
// for anybody (and its Copy), "Play in this tab", and the address of the page itself (a reload, a bookmark or a copied address come back to the same room, and a room that has ended is made again by the block). A room
// that was made some other way (no block in its address) has none anywhere. The game page reads each part of the block through its own whitelist (the arguments --room-map, --room-seats, --room-teams, --room-leader-start).
{
    // every address that a panel makes, by the button that makes it: seat 1 plays on this page, seat 0 in a window, the link of seat 1 and the link for anybody are copied, and this tab joins (the first two seats exist in every room)
    const panelLinks = (env) => {
        env.rowButton(1, 'Play here').click();
        env.rowButton(0, 'Open a window').click();
        env.rowButton(1, 'Copy link').click();
        env.$('copy-any').click();
        env.$('play-tab').click();
        return { frame: env.frameOf(1).src, window: env.opened[0].url, seat: env.copied[0], anybody: env.$('any-link').value, copiedAnybody: env.copied[1], tab: env.assigned[0] };
    };
    // what the game page makes of a link: the arguments that stand between the room and the seat (the block's: --room-map, --room-seats, --room-teams, --room-leader-start), and the value of --teams (null: none)
    const gameArgs = (link) => P.joinArguments(new URL(link).search, true, 'play.test').args;
    const blockArgs = (link) => {
        const args = gameArgs(link);
        const stops = ['--seat', '--fill-bots', '--teams', '--start-when', '--name'].map((s) => args.indexOf(s)).filter((i) => i !== -1);
        return args.slice(args.indexOf('--room') + 2, stops.length ? Math.min(...stops) : args.length);
    };
    const teamsArg = (link) => { const args = gameArgs(link); const at = args.indexOf('--teams'); return at === -1 ? null : args[at + 1]; };
    const fillArg = (link) => { const args = gameArgs(link); const at = args.indexOf('--fill-bots'); return at === -1 ? null : args[at + 1]; };
    const kinds = ['frame', 'window', 'seat', 'anybody', 'copiedAnybody', 'tab'];
    // [the address of the page, the block that every link carries, the game's arguments for it, the panel's map line, its seats, the address that the page puts in its own bar]
    const ROOMS = [
        ['?room=k7m2xq9p&roommap=small&roomseats=2', '&roommap=small&roomseats=2', ['--room-map', 'small', '--room-seats', '2'], 'Small map, 2 players', 2, '/?room=k7m2xq9p&roommap=small&roomseats=2'],
        ['?room=k7m2xq9p&roommap=treasure&roomseats=4&roomteams=0%2B1', '&roommap=treasure&roomseats=4&roomteams=0%2B1', ['--room-map', 'treasure', '--room-seats', '4', '--room-teams', '0+1'], 'Treasure map, 4 players', 4, '/?room=k7m2xq9p&roommap=treasure&roomseats=4&roomteams=0%2B1'],
        ['?room=k7m2xq9p&roommap=islands&roomseats=3&roomleaderstart=1', '&roommap=islands&roomseats=3&roomleaderstart=1', ['--room-map', 'islands', '--room-seats', '3', '--room-leader-start'], 'Islands map, 3 players', 3, '/?room=k7m2xq9p&roommap=islands&roomseats=3&roomleaderstart=1'],
        ['?room=k7m2xq9p&roommap=medium&roomseats=3&roomteams=1%2B2&roomleaderstart=1', '&roommap=medium&roomseats=3&roomteams=1%2B2&roomleaderstart=1', ['--room-map', 'medium', '--room-seats', '3', '--room-teams', '1+2', '--room-leader-start'], 'Medium map, 3 players', 3, '/?room=k7m2xq9p&roommap=medium&roomseats=3&roomteams=1%2B2&roomleaderstart=1'],
        // read as the game page reads them: the map in any case, a "+" that came as a blank, a part that is no good dropped (the server's own: no map, four seats, free for all, no flag), a block of teams that its seats cannot make still carried
        ['?room=k7m2xq9p&roommap=GAUNTLET&roomseats=2&roomteams=0+1', '&roommap=gauntlet&roomseats=2&roomteams=0%2B1', ['--room-map', 'gauntlet', '--room-seats', '2', '--room-teams', '0+1'], 'Gauntlet map, 2 players', 2, '/?room=k7m2xq9p&roommap=gauntlet&roomseats=2&roomteams=0%2B1'],
        ['?room=k7m2xq9p&roomseats=3', '&roomseats=3', ['--room-seats', '3'], 'the server\'s own map, 3 players', 3, '/?room=k7m2xq9p&roomseats=3'],
        ['?room=k7m2xq9p&roommap=tiny', '&roommap=tiny&roomseats=4', ['--room-map', 'tiny', '--room-seats', '4'], 'Tiny map, 4 players', 4, '/?room=k7m2xq9p&roommap=tiny&roomseats=4'],
        ['?room=k7m2xq9p&roomleaderstart=1', '&roomseats=4&roomleaderstart=1', ['--room-seats', '4', '--room-leader-start'], 'the server\'s own map, 4 players', 4, '/?room=k7m2xq9p&roomseats=4&roomleaderstart=1'],
        ['?room=k7m2xq9p&roomteams=0%2B1', '&roomseats=4&roomteams=0%2B1', ['--room-seats', '4', '--room-teams', '0+1'], 'the server\'s own map, 4 players', 4, '/?room=k7m2xq9p&roomseats=4&roomteams=0%2B1'],
        ['?room=k7m2xq9p&roommap=nowhere&roomseats=2&roomleaderstart=yes', '&roomseats=2', ['--room-seats', '2'], 'the server\'s own map, 2 players', 2, '/?room=k7m2xq9p&roomseats=2'],
        ['?room=k7m2xq9p&roommap=small&roomseats=2&aspect=4:3', '&roommap=small&roomseats=2', ['--room-map', 'small', '--room-seats', '2'], 'Small map, 2 players', 2, '/?room=k7m2xq9p&roommap=small&roomseats=2&aspect=4:3'],
        // a room that was made some other way: none of the four parameters says anything that is any good, so there is no block (four seats, "made some other way"), and none is sent
        ['?room=k7m2xq9p', '', [], 'a room made some other way, 4 players', 4, '/?room=k7m2xq9p'],
        ['?room=k7m2xq9p&roommap=nowhere&roomseats=9&roomteams=7%2B7&roomleaderstart=yes', '', [], 'a room made some other way, 4 players', 4, '/?room=k7m2xq9p'],
        ['?room=k7m2xq9p&roommap=&roomseats=&roomteams=&roomleaderstart=', '', [], 'a room made some other way, 4 players', 4, '/?room=k7m2xq9p'],
        ['?room=k7m2xq9p&platform=linux&roomleaderstart=0', '', [], 'a room made some other way, 4 players', 4, '/?room=k7m2xq9p'],
    ];
    for (const [search, text, args, mapLine, seats, bar] of ROOMS) {
        const env = openRoom(search, {}, 'Ann');
        const label = search.replace('?room=k7m2xq9p', '?room=<code>');
        check('the panel of ' + label + ': the code in two groups of four, "' + mapLine + '", one row for each of its ' + seats + ' seats', env.roomStarted() && env.$('room-code').textContent === 'k7m2 xq9p' && env.$('room-map').textContent === mapLine && env.rows().length === seats, env.$('room-map').textContent);
        same('... the address of the page is the room with its create block (a reload, a bookmark or a copied address come back to it)', env.replaced.map((a) => a[2]), [bar]);
        const links = panelLinks(env);
        const wrong = kinds.filter((k) => !(blockText(links[k]) === text && env.param(links[k], 'room') === 'k7m2xq9p' && JSON.stringify(blockArgs(links[k])) === JSON.stringify(args) && (text !== '' || noBlockOf(env, links[k])) && env.param(links[k], 'teams') === null && teamsArg(links[k]) === null));
        check('... every address that it makes (the frame, the window, the link of a seat, the link for anybody, its Copy, "Play in this tab") has the block right after the plain code' + (text === '' ? ' (none: nothing is carried)' : '') + ', and the game page turns it into ' + JSON.stringify(args.join(' ')) + ': ' + wrong.join(','), wrong.length === 0, JSON.stringify(links));
        const again = openRoom('?' + bar.split('?')[1], {}, 'Ann');
        check('... and the address that the page puts in its own bar brings back the same room: the same link for anybody, the same panel', again.$('any-link').value === env.$('any-link').value && again.$('room-map').textContent === mapLine && again.rows().length === seats && again.replaced.map((a) => a[2]).join() === bar, again.$('any-link').value);
    }
    {   // the first room, whole: the order of the parameters of every kind of address
        const env = openRoom(ROOMS[0][0], {}, 'Ann');
        const l = panelLinks(env);
        same('every address of a room that the page made, whole (Small, two seats): the door, the plain code, the block, then what the address is for (its seat, the name, the picture)',
             { frame: l.frame, seat: l.seat, anybody: l.anybody, copiedAnybody: l.copiedAnybody, tab: l.tab },
             { frame: 'https://play.test/?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2&seat=1&name=Ann&aspect=16:9&embed=1', seat: 'https://play.test/?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2&seat=1', anybody: 'https://play.test/?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2',
               copiedAnybody: 'https://play.test/?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2', tab: 'https://play.test/?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2&name=Ann' });
        check('... the window of a seat too (its own random name)', /^https:\/\/play\.test\/\?join=\/ws&room=k7m2xq9p&roommap=small&roomseats=2&seat=0&name=[^&]+&aspect=16:9$/.test(l.window), l.window);
        const withShape = openRoom('?room=k7m2xq9p&roommap=small&roomseats=2&aspect=4:3', {}, 'Ann');
        const s = panelLinks(withShape);
        check('an address with a picture: it is in every link, after the block (and after the plan and the teams, when there are some)', [s.frame, s.seat, s.anybody, s.copiedAnybody, s.tab, s.window].every((u) => blockText(u) === '&roommap=small&roomseats=2' && withShape.param(u, 'aspect') === '4:3' && /&aspect=4:3(&embed=1)?$/.test(u)), JSON.stringify(s));
    }
    // &teams= is the leader's START choice for a room whose block names no teams of its own (every room that was made some other way); the teams that a block names are the room's own and win over it, and the links then
    // carry &roomteams= and no &teams=. Where the block says how many seats the room has, a pair that those seats cannot make is free for all (the page narrows it, as it narrows the teams of an address that makes a room:
    // for four seats the pairs with Green); a block whose own seats cannot make its teams is the server's to judge at START (the match starts without teams and the room says "No teams: ..."): the page carries it
    // as it is, does not announce it, and does not replace it with &teams=; a room that was made some other way is the game's to judge, so its &teams= goes as it is.
    // [the address of the page, the block that every link carries, the teams parameter that every link carries (null: none), the fill parameter, the address of the page, the line of the panel under the title ('' when it is hidden)]
    const BLOCKED = '&roommap=treasure&roomseats=4';
    const AT_START = (teams) => 'Teams at START: ' + teams + '. Every link of this room carries the teams; only the leader\'s START uses them.';
    const TEAMS = [
        ['?room=k7m2xq9p&roommap=treasure&roomseats=4&teams=0%2B2', BLOCKED, '0+2', null, '/?room=k7m2xq9p&roommap=treasure&roomseats=4&teams=0%2B2', AT_START('Green + Blue against Red + Black')],
        ['?room=k7m2xq9p&roommap=treasure&roomseats=4&teams=0+2', BLOCKED, '0+2', null, '/?room=k7m2xq9p&roommap=treasure&roomseats=4&teams=0%2B2', AT_START('Green + Blue against Red + Black')],
        ['?room=k7m2xq9p&roommap=treasure&roomseats=4&teams=2%2B3', BLOCKED, null, null, '/?room=k7m2xq9p&roommap=treasure&roomseats=4', ''],
        ['?room=k7m2xq9p&roommap=treasure&roomseats=4&roomteams=0%2B1&teams=0%2B2', BLOCKED + '&roomteams=0%2B1', null, null, '/?room=k7m2xq9p&roommap=treasure&roomseats=4&roomteams=0%2B1',
         'Teams of this room: Green + Red against Blue + Black. They are part of the room (set when it was made), so the match starts with them every time: when the room fills up as well as at START.'],
        ['?room=k7m2xq9p&roommap=small&roomseats=3&teams=1%2B2', '&roommap=small&roomseats=3', '1+2', null, '/?room=k7m2xq9p&roommap=small&roomseats=3&teams=1%2B2', AT_START('Red + Blue against Green')],
        ['?room=k7m2xq9p&roommap=small&roomseats=3&teams=2%2B3', '&roommap=small&roomseats=3', null, null, '/?room=k7m2xq9p&roommap=small&roomseats=3', ''],
        ['?room=k7m2xq9p&roommap=small&roomseats=2&teams=0%2B1', '&roommap=small&roomseats=2', null, null, '/?room=k7m2xq9p&roommap=small&roomseats=2', ''],
        ['?room=k7m2xq9p&roommap=small&roomseats=3&roomteams=2%2B3&teams=0%2B1', '&roommap=small&roomseats=3&roomteams=2%2B3', null, null, '/?room=k7m2xq9p&roommap=small&roomseats=3&roomteams=2%2B3', ''],
        ['?room=k7m2xq9p&roommap=treasure&roomseats=4&teams=1%2B1', BLOCKED, null, null, '/?room=k7m2xq9p&roommap=treasure&roomseats=4', ''],
        ['?room=k7m2xq9p&roommap=treasure&roomseats=4&roomteams=3%2B3&teams=0%2B3', BLOCKED, '0+3', null, '/?room=k7m2xq9p&roommap=treasure&roomseats=4&teams=0%2B3', AT_START('Green + Black against Red + Blue')],
        ['?room=k7m2xq9p&teams=0%2B1', '', '0+1', null, '/?room=k7m2xq9p&teams=0%2B1', AT_START('Green + Red against Blue + Black')],
        ['?room=k7m2xq9p&teams=2%2B3', '', '2+3', null, '/?room=k7m2xq9p&teams=2%2B3', AT_START('Blue + Black against Green + Red')],
        ['?room=k7m2xq9p&roommap=treasure&roomseats=4&roomteams=0%2B1&fill=hard', BLOCKED + '&roomteams=0%2B1', null, 'hard', '/?room=k7m2xq9p&roommap=treasure&roomseats=4&roomteams=0%2B1&fill=hard', 'Teams of this room: Green + Red against Blue + Black.'],
        ['?room=k7m2xq9p&roommap=treasure&roomseats=4&fill=none,easy,none,hard&teams=0%2B2', BLOCKED, '0+2', 'none,easy,none,hard', '/?room=k7m2xq9p&roommap=treasure&roomseats=4&fill=none,easy,none,hard&teams=0%2B2', 'Empty seats at START: Red Easy, Black Hard.'],
    ];
    for (const [search, text, teams, fill, bar, hint] of TEAMS) {
        const env = openRoom(search, {}, 'Ann');
        const label = search.replace('?room=k7m2xq9p', '?room=<code>');
        same('the panel of ' + label + ': the address of the page has the block, then the plan and the teams that START uses (none when the block has teams of its own)', env.replaced.map((a) => a[2]), [bar]);
        check('... the line under the title says ' + (hint ? JSON.stringify(hint.slice(0, 60)) : 'nothing (it is hidden)'), hint === '' ? env.$('fill-hint').hidden === true : env.$('fill-hint').hidden === false && env.$('fill-hint').textContent.indexOf(hint) !== -1, env.$('fill-hint').textContent);
        const links = panelLinks(env);
        const wrong = kinds.filter((k) => !(blockText(links[k]) === text && env.param(links[k], 'teams') === teams && teamsArg(links[k]) === teams && env.param(links[k], 'fill') === fill && fillArg(links[k]) === fill &&
                                            !(env.param(links[k], 'roomteams') !== null && env.param(links[k], 'teams') !== null)));
        check('... every address that it makes carries the block, the teams parameter ' + JSON.stringify(teams) + ' and the plan ' + JSON.stringify(fill) + ' (the game\'s --teams and --fill-bots) the same way, and never both roomteams and teams: ' + wrong.join(','), wrong.length === 0, JSON.stringify(links));
        check('... and the block comes right after the plain code, before everything else (the plan, the teams, the seat, the name, the picture)', kinds.every((k) => /^https:\/\/play\.test\/\?join=\/ws&room=k7m2xq9p(&room(map|seats|teams|leaderstart)=[^&]*)*(&(fill|teams|seat|name|aspect|embed)=|$)/.test(links[k])), JSON.stringify(links));
    }
    // ?map=<key>&players=<n>: a room of a NEW code (eight characters) and a create block of its own: the map, the seats and the teams, shown and carried like those of a room that an address names; &players=1 stays a game on this computer
    // [the address, the panel's map line, its seats, the block that every link carries, the fill parameter, the rest of the address of the page after the block]
    const MADE = [
        ['?map=small&players=2', 'Small map, 2 players', 2, '&roommap=small&roomseats=2', null, ''],
        ['?map=SMALL&players=3', 'Small map, 3 players', 3, '&roommap=small&roomseats=3', null, ''],
        ['?map=gauntlet', 'Gauntlet map, 4 players', 4, '&roommap=gauntlet&roomseats=4', null, ''],
        ['?map=tiny&players=4&teams=0%2B3&fill=easy', 'Tiny map, 4 players', 4, '&roommap=tiny&roomseats=4&roomteams=0%2B3', 'easy', '&fill=easy'],
        ['?map=tiny&players=3&teams=1+2', 'Tiny map, 3 players', 3, '&roommap=tiny&roomseats=3&roomteams=1%2B2', null, ''],
        ['?map=tiny&players=2&teams=0%2B1', 'Tiny map, 2 players', 2, '&roommap=tiny&roomseats=2', null, ''],
        ['?map=islands&players=3&teams=2%2B3', 'Islands map, 3 players', 3, '&roommap=islands&roomseats=3', null, ''],
        ['?map=medium&players=4&fill=none,easy,none,hard', 'Medium map, 4 players', 4, '&roommap=medium&roomseats=4', 'none,easy,none,hard', '&fill=none,easy,none,hard'],
        ['?map=treasure&players=9', 'Treasure map, 4 players', 4, '&roommap=treasure&roomseats=4', null, ''],
        ['?map=treasure&players=0&teams=0%2B1', 'Treasure map, 4 players', 4, '&roommap=treasure&roomseats=4&roomteams=0%2B1', null, ''],
    ];
    for (const [search, mapLine, seats, text, fill, rest] of MADE) {
        const env = openRoom(search, {}, 'Ann');
        const code = env.roomStarted() ? env.param(env.$('any-link').value, 'room') : '';
        const label = search;
        check('the panel of ' + label + ': a new code of eight characters in two groups of four, "' + mapLine + '", ' + seats + ' seats', env.roomStarted() && CODE.test(code) && env.$('room-code').textContent === grouped(code) && env.$('room-map').textContent === mapLine && env.rows().length === seats, env.$('room-map').textContent);
        same('... the address of the page: the code, the block of the room that was made, the plan', env.replaced.map((a) => a[2]), ['/?room=' + code + text + rest]);
        const links = panelLinks(env);
        const wrong = kinds.filter((k) => !(blockText(links[k]) === text && env.param(links[k], 'room') === code && env.param(links[k], 'teams') === null && teamsArg(links[k]) === null && env.param(links[k], 'fill') === fill));
        check('... every address that it makes carries that block (the teams of a room that an address makes are its block\'s: no teams parameter) and the plan ' + JSON.stringify(fill) + ': ' + wrong.join(','), wrong.length === 0, JSON.stringify(links));
    }
    {   // every room is a new one: two pages that make a room from the same address have two codes (and the code is one that the page made: eight characters of its alphabet)
        const codes = new Set();
        for (let i = 0; i < 20; i++) { const e = openRoom('?map=small&players=2', {}, ''); codes.add(e.param(e.$('any-link').value, 'room')); }
        check('twenty rooms made from one address have twenty codes, each of eight characters of the alphabet without look-alikes (no i, l, o, 0 or 1)', codes.size === 20 && [...codes].every((c) => /^[abcdefghjkmnpqrstuvwxyz23456789]{8}$/.test(c)), [...codes].slice(0, 3).join(','));
    }
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
        const room = runLobby('?room=k7m2xq9p&roommap=small&roomseats=2&play=here', {}, { fetch: answers({ '/stats': GOOD_STATS }) });
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
        const stored = { 'ants.rejoin.k7m2xq9p.1': entry(5000) };
        const env = runLobby('', stored);
        same('a fresh entry of this site: ONE button with the room (its code as a screen shows it, in two groups of four), and the note under it', rejoinView(env), { hidden: false, button: 'Rejoin your match (k7m2 xq9p)', note: 'Your match in room k7m2 xq9p is still running: go back to your seat.' });
        check('... the cards stay where they were (the button is above them, in its own block, not in their grid)', !env.$('cards').hidden && lobbyText.indexOf('<section id="rejoin"') > lobbyText.indexOf('</header>') && lobbyText.indexOf('<section id="rejoin"') < lobbyText.indexOf('<main class="page">'));
        check('... and the key is in no text or attribute of the page', wholeText(env).indexOf(KEY) === -1 && wholeText(env).indexOf(KEY.slice(0, 8)) === -1);
        env.$('rejoin-go').click();
        same('pressing it takes THIS tab to the game page: the plain code, the seat, the (empty) name and the shape', env.assigned, ['https://play.test/?join=/ws&room=k7m2xq9p&seat=1&name=&aspect=16:9']);
        check('... with no key in the address, no create block (the seat is already in the room: the game finds its key by itself) and nothing opened in another window', env.assigned[0].indexOf(KEY) === -1 && noBlockOf(env, env.assigned[0]) && env.opened.length === 0 && env.storage.data['ants.rejoin.k7m2xq9p.1'] === stored['ants.rejoin.k7m2xq9p.1']);
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
        const env = runLobby('', { 'ants.rejoin.old-match.0': entry(4 * 3600 * 1000), 'ants.rejoin.mid-match.1': entry(2 * 3600 * 1000) });
        same('an entry older than three hours is not offered and is removed (as the game does); one a little younger is the offer (a code of another length than eight is shown as it is)', [rejoinView(env).button, Object.keys(env.storage.data)], ['Rejoin your match (mid-match)', ['ants.rejoin.mid-match.1']]);
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
    for (const [label, search] of [['a room (?room=)', '?room=k7m2xq9p&roommap=tiny&roomseats=2'], ['a room that starts at once (?play=here)', '?room=k7m2xq9p&roommap=tiny&roomseats=2&play=here'], ['a match to host (?map=)', '?map=tiny&players=2&play=here']]) {
        const env = runLobby(search, { 'ants.rejoin.k7m2xq9p.1': entry(1000) });
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
