// Runs the pages' OWN code for the player's name (the owner: "the ability for somebody to type in their name ... so their name goes into the game instead of random", and "when joining a
// link from somebody else, it should ask you first what you want your name to be"):
//   1. the rules of a name (the block NAME_BEGIN .. NAME_END, the same text in web/lobby.html and web/shell.html) on a table of names: the rules of the desktop start menu
//      (printable ASCII, trimmed, at most 32, nothing that starts with "Bot (" with blanks and case ignored; an empty name means "none chosen");
//   2. the name step (runNameStep): filled in from what the browser remembered, a bad name explained and not accepted, the button or Enter accepts once, the name is remembered;
//   3. web/shell.html: which addresses ask for a name (a shared link: ANTS_PAGE.asksForName), the address without its name (withoutName), and the gate that holds the game back until the name
//      is chosen (makeNameGate): the game is not started, so it does not connect, before the button; the chosen name goes into the game's arguments as --name;
//   4. web/lobby.html (the front page, the owner's lobby: a room on the game server, protocol 16) read from the file: its scripts, its ids, the name card's wiring, a name only ever as text
//      (textContent, a value, an attribute, encodeURIComponent in an address), and the links that are made for somebody else (no name in them);
//   5. web/lobby.html as a whole, run with a small fake of the browser's DOM, its two scripts (front/lobby_rules.js and front/lobby_net.js, the real files) and a scripted game server:
//      a plain visit makes a room at once under the remembered or picked name (Hello's name, room, key and the session storage that keeps them), the pencil renames the host, a link of somebody
//      else's room (?room=) and a typed code (Have a code?) ask for a name first and send nothing before it, START hands the seat to the game page (ants.rejoin.<room>.<seat>; no leave()), the
//      Rejoin strip, the test room of the old addresses (?map=, ?room= with a create block, ?play=here: the typed name goes to the first seat that the page starts, the others have random
//      names, a link made for somebody else carries none, bad names start nothing, a name with < > & is only ever text, the room codes and the create block that every link of a room that the
//      page made carries) and the line of numbers in the footer. What a fake DOM cannot say (the look, the phone widths, the pointer, the real focus and the real WebSocket) is the job of
//      the browser checks (web_home_check.py and the web_lobby_*_check.js against the server), and the block REJOIN has its own check, web_rejoin_block_check.js.
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
const LOBBY_NOTICE_HTML = 'Online matches are recorded and kept for 30 days. The recordings are public and show the players&rsquo; names.';       // (the front page's: its footer and the card of a link)
const NOTICE_HTML = 'Online matches are recorded and kept for 30 days. Anybody can watch them, live or later, and they show the players&rsquo; names.';       // (the game page's line under its name field; the lobby's own sentence is LOBBY_NOTICE_HTML)

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
    ['... with a longer door', '?join=/ws/room-1&room=k7m2xq', true],
    ['... with the create block of a room that the front page made (the map, the seats, the teams, the leader-starts flag) and the platform', '?join=/ws&room=k7m2xq&roommap=small&roomseats=2&roomteams=0%2B1&roomleaderstart=1&platform=linux', true],
    ['... the same with a name (the page that made the address put it there)', '?join=/ws&room=k7m2xq&roommap=small&roomseats=2&roomteams=0%2B1&seat=1&name=Bob', false],
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
same('... and a shared link of a room that the front page made carries its create block to the game, after the room (the game makes the room of it when it has none)', P.joinArguments('?join=/ws&room=k7m2xq&roommap=small&roomseats=2&roomteams=0%2B1&seat=1&fill=easy&start=2', false, 'h').args,
     ['--join-url', 'ws://h/ws', '--room', 'k7m2xq', '--room-map', 'small', '--room-seats', '2', '--room-teams', '0+1', '--seat', '1', '--fill-bots', 'easy', '--start-when', '2']);
same('... the create block goes with a room only: with no room, or a room that is no room code, the game gets the door and the rest (the platform, the seat), and no map, seats, teams or flag',
     [P.joinArguments('?join=/ws&roommap=small&roomseats=2&roomteams=0%2B1&roomleaderstart=1&platform=linux&seat=1', false, 'h').args, P.joinArguments('?join=/ws&room=a%20b&roommap=small&roomseats=2&roomleaderstart=1', false, 'h').args],
     [['--join-url', 'ws://h/ws', '--platform', 'linux', '--seat', '1'], ['--join-url', 'ws://h/ws']]);
const STRIP = [
    ['?join=/ws&room=ABC&name=Bob', '?join=/ws&room=ABC'], ['?name=Bob&join=/ws', '?join=/ws'], ['?join=/ws&name=Bob&room=ABC', '?join=/ws&room=ABC'],
    ['?name=Bob', ''], ['?join=/ws&name=', '?join=/ws'], ['?join=/ws&room=ABC', '?join=/ws&room=ABC'], ['', ''], ['?username=Bob&join=/ws', '?username=Bob&join=/ws'],
    ['?join=/ws&name=a&name=b&room=ABC', '?join=/ws&room=ABC'], ['?join=/ws&name=Zo%C3%AB%20X&seat=1', '?join=/ws&seat=1'],
    ['?join=/ws&room=k7m2xq&roommap=small&roomseats=2&roomteams=0%2B1&name=Bob&seat=1', '?join=/ws&room=k7m2xq&roommap=small&roomseats=2&roomteams=0%2B1&seat=1'],
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
    ['the create block of the room stays where it is (the game of a room that the front page made keeps it in its address)', '?join=/ws&room=k7m2xq&roommap=small&roomseats=2&roomteams=0%2B1&name=Bob&seat=1', 0, '?join=/ws&room=k7m2xq&roommap=small&roomseats=2&roomteams=0%2B1&name=Bob&seat=0'],
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
    // The notice (the owner chose "Add the line" on 2026-10-08): where a page asks for a name it also says what the server does with an online match, in the hint's own type under the name field. The
    // words are the ones of the picture that he approved, in both pages; 30 days is the stack's default (tests/scripts/test_nginx_replays.py pins it to docker-compose.stack.yml)
    const noticeAt = stepMarkup.indexOf('<div class="name-step-hint name-step-notice">' + NOTICE_HTML + '</div>');
    const hintAt = stepMarkup.indexOf('<div class="name-step-hint">The name the other players see.');
    check('the card tells a player who joins by a link what the server does with an online match: one more paragraph of the hint\'s own type, after the hint and before the way out, once',
          hintAt !== -1 && noticeAt > hintAt && noticeAt < stepMarkup.indexOf('name-step-back') && shellText.split(NOTICE_HTML).length === 2);
}



// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. web/lobby.html, read from the file: its scripts, its ids, the wiring of the name card, what it does with a name and what it puts into a link
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const path = require('path');
const lobbyDir = path.dirname(path.resolve(lobbyPath));
const lobbyMarkup = lobbyText.slice(lobbyText.indexOf('<body>'), lobbyText.indexOf('<script'));
const lobbyScripts = [...lobbyText.matchAll(/<script([^>]*)>([\s\S]*?)<\/script>/g)];
const lobbyScript = lobbyScripts[lobbyScripts.length - 1][2];
{
    same('the page loads its two scripts from front/ (the rules, then the network client) and then runs its own', lobbyScripts.map((m) => { const s = /src="([^"]*)"/.exec(m[1]); return s ? s[1] : 'inline'; }), ['front/lobby_rules.js', 'front/lobby_net.js', 'inline']);
    const markupIds = [...lobbyMarkup.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]);
    check('no id is used twice in the markup', new Set(markupIds).size === markupIds.length, markupIds.filter((id, i) => markupIds.indexOf(id) !== i).join(','));
    const asked = new Set([...lobbyScript.matchAll(/\$\('([\w-]+)'\)/g), ...lobbyScript.matchAll(/freshNode\('([\w-]+)'\)/g), ...lobbyScript.matchAll(/getElementById\('([\w-]+)'\)/g)].map((m) => m[1]));
    const missing = [...asked].filter((id) => markupIds.indexOf(id) === -1);
    check('every id that the script asks for is in the markup (' + asked.size + ' of them)', missing.length === 0, missing.join(','));
    for (const id of ['name-step', 'name-step-title', 'name-step-input', 'name-step-go', 'name-step-msg', 'name-step-hint', 'who-notice', 'name-step-own', 'rejoin', 'rejoin-note', 'rejoin-go', 'link', 'copy', 'havecode', 'joinbox', 'joincode', 'joingo', 'joinhint', 'slots', 'start', 'banner', 'banner-text', 'room-panel', 'grid', 'any-link', 'seat-rows', 'play-tab', 'stats', 'stats-live', 'stats-played'])
        check('the page has #' + id + ' once', lobbyMarkup.split('id="' + id + '"').length === 2);
    for (const gone of ['player-name', 'join-name', 'who', 'who-go', 'who-back', 'who-title', 'cards', 'map-pick', 'play', 'seat-you-0', 'sit-0', 'invite-list', 'name-msg', 'join-go', 'join-code'])
        check('the one-card page\'s #' + gone + ' is gone', lobbyMarkup.indexOf('id="' + gone + '"') === -1);
    check('the markup has no name field but the card\'s: one text input with the id name-step-input and no other input for a name', (lobbyMarkup.match(/<input[^>]*>/g) || []).filter((t) => /\bid="[^"]*name[^"]*"|placeholder="[^"]*name/i.test(t)).length === 1);
    // The name card is the game page's step in the lobby page: the NAME block's runNameStep runs it, over the card's own field, button and message
    check('the name card is run by the NAME block\'s runNameStep, over the card\'s own field, button and message (the field and the button are fresh nodes: no listener of an earlier use stays)',
          /input: freshNode\('name-step-input'\), button: freshNode\('name-step-go'\),\s*message: \{ get textContent\(\) \{ return \$\('name-step-msg'\)\.textContent; \}, set textContent\(v\) \{ \$\('name-step-msg'\)\.textContent = v; ui\.input\.setAttribute\('aria-invalid', v \? 'true' : 'false'\); \} \},/.test(lobbyScript) &&
          /runNameStep\(ui, \{ recall: recall, remember: remember \}, function \(name\) \{ o\.done\(name \|\| suggestion\); \}\);/.test(lobbyScript) &&
          /function freshNode\(id\) \{\s*var old = \$\(id\), copy = old\.cloneNode\(true\);\s*old\.parentNode\.replaceChild\(copy, old\);\s*return copy;\s*\}/.test(lobbyScript));
    check('three things ask for a name: a link of somebody else\'s room, a code that was typed, and an address of the test room (askName is called three times, and no other way starts a seat of a stranger)', (lobbyScript.match(/\baskName\(\{/g) || []).length === 3);
    check('the card says its words as text: the title, the button, the hint, the suggestion in the field, the way out', /\$\('name-step-title'\)\.textContent = o\.title;/.test(lobbyScript) && /ui\.button\.textContent = o\.button;/.test(lobbyScript) && /ui\.input\.placeholder = suggestion;/.test(lobbyScript)
          && /\$\('name-step-hint'\)\.textContent = o\.hint \|\| 'The name the other players see\. Leave it empty to be called ' \+ suggestion \+ '\.';/.test(lobbyScript) && /\$\('name-step-own'\)\.textContent = o\.backText \|\| 'Start a room of my own instead';/.test(lobbyScript));
    check('an empty name is the page\'s picked name (the one the field\'s placeholder and the hint say), and the browser remembers what was typed (NAME_KEY, by the NAME block)', /o\.done\(name \|\| suggestion\)/.test(lobbyScript) && /var suggestion = Rules\.pickName\(Math\.random\(\)\);/.test(lobbyScript));
    // What the page does with a name: nothing is put into markup. A name is set as text (el() sets textContent), through a value or an attribute, and goes into an address only through encodeURIComponent
    check('el() makes an element with its text as textContent, and a person\'s name goes onto a card through it', /function el\(tag, cls, text\) \{[^}]*e\.textContent = text;[^}]*\}/.test(lobbyScript) && /card\.who\.forEach\(function \(w\) \{ who\.appendChild\(el\('span', w\.cls, w\.text\)\); \}\);/.test(lobbyScript));
    check('the toast, the strip\'s sentence and the removal question are text too', /t\.textContent = text; t\.hidden = false;/.test(lobbyScript) && /\$\('banner-text'\)\.textContent = b\.text;/.test(lobbyScript) && /ask\.appendChild\(el\('span', '', 'Remove ' \+ card\.who\[0\]\.text \+ ' from the room\?'\)\);/.test(lobbyScript));
    check('the pencil turns the name into a field by value (not into markup), asks nameCheck before the room hears of it, and the room is asked to rename only for a name that the rules take and that is new',
          /input\.value = nm\.textContent; input\.maxLength = 32;/.test(lobbyScript) && /var checked = nameCheck\(input\.value\);\s*if \(!checked\.ok\) say\(checked\.why\);\s*else if \(checked\.name && checked\.name !== nm\.textContent\) \{\s*if \(client && client\.rename\(checked\.name\)\) remember\(NAME_KEY, checked\.name\);/.test(lobbyScript));
    check('nothing in the script makes markup from text', !/\.innerHTML\s*[+]?=|\.outerHTML|insertAdjacentHTML|document\.write/.test(lobbyScript));
    // What goes into an address: a link that is made for somebody else carries the room and no name; the addresses of this page's own games and the way back carry the name that this page's person chose
    check('the link of the room is the page\'s own address with the plain code and nothing else (no name, no key): ?room=<code>', /function roomLink\(\) \{ return window\.location\.origin \+ window\.location\.pathname\.replace\(\/\^\\\/\+\/, '\/'\) \+ '\?room=' \+ encodeURIComponent\(client \? client\.code : ''\); \}/.test(lobbyScript));
    check('the Copy button, the Share sheet and the link field all give that link', /copyButton\(\$\('copy'\), roomLink\);/.test(lobbyScript) && /navigator\.share\(\{ url: roomLink\(\) \}\)/.test(lobbyScript) && /if \(\$\('link'\)\.value !== roomLink\(\)\) \$\('link'\)\.value = roomLink\(\);/.test(lobbyScript));
    check('a game address of the test room has a name only for a seat that this page starts itself (own), and the links for somebody else (Copy link, the link for anybody) are made without it',
          /if \(own && seat >= 0\) q \+= '&name=' \+ encodeURIComponent\(seatNames\.start\(/.test(lobbyScript) && /copyButton\(copy, function \(\) \{ return gameUrl\(i\); \}\);/.test(lobbyScript) && /\$\('any-link'\)\.value = gameUrl\(-1\);/.test(lobbyScript) &&
          (lobbyScript.match(/gameUrl\([^)]*\btrue\b/g) || []).length === 3 && /iframe\.src = gameUrl\(seat, true\) \+ '&embed=1';/.test(lobbyScript) && /window\.open\(gameUrl\(seat, true\),/.test(lobbyScript) && /frames\[seat\]\.src = gameUrl\(frameSeat\[seat\] === null \? seat : frameSeat\[seat\], true, seat\) \+ '&embed=1';/.test(lobbyScript));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. web/lobby.html as a whole, with a small fake of the browser, its two scripts (front/lobby_rules.js and front/lobby_net.js, the real files) and a scripted game server
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The scripts of front/ are run as they are, in a sandbox that has the fake WebSocket and the fake clock of one page (they are ES5 with no module system: a page gets a global, node gets module.exports)
function loadFront(file, scope) {
    const root = {};
    new Function('self', 'WebSocket', 'setTimeout', 'clearTimeout', fs.readFileSync(path.join(lobbyDir, 'front', file), 'utf8'))(root, scope.WebSocket, scope.setTimeout, scope.clearTimeout);
    return root;
}
const NOSCOPE = { WebSocket: function () { throw new Error('no socket here'); }, setTimeout() { return 0; }, clearTimeout() {} };
const RULES0 = loadFront('lobby_rules.js', NOSCOPE).AntsLobbyRules;
const NET0 = loadFront('lobby_net.js', NOSCOPE).AntsLobbyNet;
check('the two scripts of front/ give the page its globals', !!RULES0 && !!NET0 && typeof RULES0.nameCheck === 'function' && typeof NET0.LobbyClient === 'function');
const NAMES_OF_THE_PAGE = RULES0.NAMES;
const SUGGESTION = RULES0.pickName(0.5);               // the page's picked name when Math.random() says 0.5 (the fake page's does)
const hex = (bytes) => NET0.hexOf(bytes);

// the messages of a game server, as the C++ server writes them (the check of the client, tests/scripts/web_lobby_client_check.js, runs the client against the same builders)
const E = NET0.SLOT.Empty, C = NET0.SLOT.Client;
function str8(w, s) { w.push(s.length); for (let i = 0; i < s.length; i++) w.push(s.charCodeAt(i)); }
const KEY = Uint8Array.from(Array.from({ length: 16 }, (_, i) => 0x30 + i));
const welcome = (player, key, flags) => Uint8Array.from([2, player, 4].concat(Array.from(key || KEY), [flags === undefined ? 2 : flags]));
const reject = (reason) => Uint8Array.from([3, reason]);
// seats: [state, name] x 4; o: { map, you, leader, teamA, teamB, flags (3: a lobby that the leader starts; 7: and START was pressed), plan, inGame }
function roomMessage(seats, o) {
    o = o || {};
    const w = [12];
    for (const s of seats) { w.push(s[0]); str8(w, s[1]); w.push(20, 0, 0); }
    str8(w, o.map === undefined ? 'TREASURE.LVL' : o.map);
    w.push(0, o.you === undefined ? 0 : o.you, o.leader === undefined ? 0 : o.leader, o.teamA === undefined ? 255 : o.teamA, o.teamB === undefined ? 255 : o.teamB, o.flags === undefined ? 3 : o.flags);
    for (const k of (o.plan || [0, 0, 0, 0])) w.push(k);
    w.push(o.inGame || 0);
    return Uint8Array.from(w);
}
// What the page's Hello says: its name, its room, whether it brings a key and a lobby block (a page that makes the room, or one that only joins)
const helloBytes = (name, room, o) => NET0.encodeHello({ name, room, key: (o && o.key) || null, platform: 0x13, create: o && o.join ? null : { map: 'TREASURE.LVL' } });
const helloName = (b) => String.fromCharCode(...b.slice(4, 4 + b[3]));
const helloRoom = (b) => { const at = 4 + b[3] + 2 + 1; return String.fromCharCode(...b.slice(at + 1, at + 1 + b[at])); };
const CODE = /^[a-z2-9]{6}$/;
const grouped = (code) => code.slice(0, 3) + ' ' + code.slice(3);
const BLOCK_PARAMS = ['roommap', 'roomseats', 'roomteams', 'roomleaderstart'];
const blockText = (link) => { const m = /[?&]room=[^&#]*((?:&room(?:map|seats|teams|leaderstart)=[^&#]*)*)/.exec(String(link)); return m ? m[1] : null; };
const noBlockOf = (env, link) => blockText(link) === '' && BLOCK_PARAMS.every((k) => env.param(link, k) === null) && !/room(map|seats|teams|leaderstart)/.test(String(link));

class El {
    constructor(env, tag, id) {
        this.env = env; this.tagName = tag; this.id = id || ''; this.children = []; this.parent = null; this.attributes = {}; this.listeners = {};
        this._text = ''; this.style = {}; this.hidden = false; this.value = ''; this.className = ''; this.src = ''; this.alt = ''; this.readOnly = false; this._checked = false; this.disabled = false; this.selected = false;
    }
    get parentNode() { return this.parent; }
    get checked() { return this._checked; }
    set checked(on) {                                    // a radio button: checking it unchecks the others of its group (the same name), as a browser does
        const radio = (e) => e.tagName === 'input' && e.attributes.type === 'radio' && e.attributes.name;
        if (on && radio(this)) for (const other of Object.values(this.env.elements)) if (other !== this && radio(other) && other.attributes.name === this.attributes.name) other._checked = false;
        this._checked = !!on;
    }
    get placeholder() { return Object.prototype.hasOwnProperty.call(this.attributes, 'placeholder') ? this.attributes.placeholder : ''; }
    set placeholder(v) { this.attributes.placeholder = String(v); }
    get maxLength() { return Object.prototype.hasOwnProperty.call(this.attributes, 'maxlength') ? Number(this.attributes.maxlength) : -1; }
    set maxLength(v) { this.attributes.maxlength = String(v); }
    get textContent() { return this._text + this.children.map((c) => c.textContent).join(''); }
    set textContent(v) { this.children = []; this._text = String(v); }
    set innerHTML(v) { this.env.innerHTMLWrites.push(String(v)); this.children = []; this._text = String(v); }
    get innerHTML() { return this._text; }
    get firstChild() { return this.children[0] || null; }
    appendChild(c) { c.parent = this; this.children.push(c); return c; }
    removeChild(c) { this.children = this.children.filter((x) => x !== c); c.parent = null; return c; }
    replaceChild(fresh, old) {
        const at = this.children.indexOf(old);
        if (at !== -1) this.children[at] = fresh;
        fresh.parent = this; old.parent = null;
        if (this.env.elements[old.id] === old) this.env.elements[old.id] = fresh;     // (what the page asks for by that id is the new node)
        return old;
    }
    replaceWith(fresh) { if (this.parent) this.parent.replaceChild(fresh, this); }
    cloneNode(deep) {
        const copy = new El(this.env, this.tagName, this.id);
        copy.attributes = Object.assign({}, this.attributes); copy.className = this.className; copy.hidden = this.hidden; copy.value = this.value; copy._checked = this._checked; copy._text = this._text; copy.readOnly = this.readOnly; copy.disabled = this.disabled;
        if (deep) for (const c of this.children) copy.appendChild(c.cloneNode ? c.cloneNode(true) : Object.assign({}, c, { parent: null }));
        return copy;
    }
    setAttribute(k, v) { this.attributes[k] = String(v); if (k === 'class') this.className = String(v); }
    getAttribute(k) { return Object.prototype.hasOwnProperty.call(this.attributes, k) ? this.attributes[k] : null; }
    removeAttribute(k) { delete this.attributes[k]; }
    hasAttribute(k) { return Object.prototype.hasOwnProperty.call(this.attributes, k); }
    addEventListener(t, fn) { (this.listeners[t] = this.listeners[t] || []).push(fn); }
    fire(t, ev) { (this.listeners[t] || []).forEach((fn) => fn(ev || {})); }
    click() { this.fire('click', { target: this, detail: 1 }); }
    key(k) { this.fire('keydown', { key: k, preventDefault() {} }); }
    focus() { this.env.focused = this; this.env.doc.activeElement = this; }
    blur() { if (this.env.doc.activeElement === this) this.env.doc.activeElement = null; this.fire('blur', {}); }
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
    // a selector of this fake: a list of simple selectors (a tag, .class, [attribute], [attribute="value"], in any mix)
    matches(selector) {
        return selector.split(',').some((part) => {
            const m = /^\s*([a-z0-9]*)((?:\.[\w-]+|\[[\w-]+(?:="[^"]*")?\])*)\s*$/i.exec(part);
            if (!m) throw new Error('the fake DOM does not know the selector ' + selector);
            if (m[1] && this.tagName !== m[1].toLowerCase()) return false;
            for (const q of m[2].match(/\.[\w-]+|\[[\w-]+(?:="[^"]*")?\]/g) || []) {
                if (q[0] === '.') { if (String(this.className).split(/\s+/).indexOf(q.slice(1)) === -1) return false; }
                else {
                    const a = /^\[([\w-]+)(?:="([^"]*)")?\]$/.exec(q);
                    if (!this.hasAttribute(a[1]) || (a[2] !== undefined && this.getAttribute(a[1]) !== a[2])) return false;
                }
            }
            return true;
        });
    }
    closest(selector) { for (let n = this; n && n.matches; n = n.parent) if (n.matches(selector)) return n; return null; }
    querySelector(selector) { return this.all().find((e) => e !== this && e.tagName && e.matches(selector)) || null; }
}

const THROWS = 'THROWS';
// One page: the markup of the page and its script, run in a fake browser. search is the address's query; stored is what local storage holds (THROWS: a browser that refuses its storage);
// options: session (what this tab's session storage holds), noSession, clipboard (false: none), copyResult, share, fetch, noFetch, ua, random (what Math.random() says: 0.5), noWebSocket.
// env.sockets are the sockets that the page opened (env.last() the newest); env.arrive() opens the newest and plays a server's Welcome and Room.
function runLobby(search, stored, options) {
    options = options || {};
    const env = { innerHTMLWrites: [], assigned: [], replaced: [], opened: [], copied: [], commanded: [], shared: [], elements: {}, focused: null, confirms: [], reloads: 0, sockets: [], timers: [], clock: 0, nextTimer: 1 };
    env.root = new El(env, 'html', '');
    for (const m of lobbyMarkup.matchAll(/<(\w+)([^>]*)>/g)) {
        const idm = /\bid="([^"]+)"/.exec(m[2]);
        if (!idm) continue;
        const el = new El(env, m[1], idm[1]);
        el.hidden = /\bhidden\b/.test(m[2].replace(/aria-hidden/g, '').replace(/="[^"]*"/g, ''));       // (the boolean attribute, not a word inside a value)
        el._checked = /\bchecked\b/.test(m[2].replace(/="[^"]*"/g, ''));                                      // (the same for a radio button that is checked in the markup)
        for (const a of m[2].matchAll(/([\w-]+)="([^"]*)"/g)) { el.attributes[a[1]] = a[2]; if (a[1] === 'class') el.className = a[2]; }
        if (m[1] === 'input' && el.attributes.value !== undefined) el.value = el.attributes.value;
        el.parent = env.root;
        env.elements[idm[1]] = el;
    }
    // the markup's own text (the buttons say Join, Rejoin it, START!): what stands between a tag with an id and the next tag
    for (const m of lobbyMarkup.matchAll(/<(\w+)[^>]*\bid="([^"]+)"[^>]*>([^<]*)</g)) env.elements[m[2]]._text = m[3].replace(/&rsquo;/g, '’').replace(/&amp;/g, '&').replace(/&#8249;/g, '‹').replace(/&#8250;/g, '›');
    const doc = {
        body: new El(env, 'body', ''),
        getElementById(id) { if (!env.elements[id]) throw new Error('the page asked for #' + id + ', which its markup does not have'); return env.elements[id]; },
        createElement(tag) { return new El(env, tag, ''); },
        createTextNode(text) { return { isText: true, textContent: String(text), parent: null }; },
        activeElement: null,
        elementFromPoint() { return null; },
        execCommand(cmd) {                                // the copy command copies what the last field of the page holds (options.copyResult: false is a browser that did not copy, 'throws' one that refuses)
            const field = doc.body.children[doc.body.children.length - 1];
            env.commanded.push({ cmd, value: field ? field.value : null });
            if (options.copyResult === 'throws') throw new Error('not allowed');
            return options.copyResult !== false;
        },
        hidden: false, listeners: {},
        addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); },
        removeEventListener() {},
    };
    env.doc = doc;
    const data = stored === THROWS ? {} : Object.assign({}, stored);
    const storage = {
        data,
        getItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); return Object.prototype.hasOwnProperty.call(data, k) ? data[k] : null; },
        setItem(k, v) { if (stored === THROWS) throw new Error('storage is blocked'); data[k] = String(v); },
        get length() { if (stored === THROWS) throw new Error('storage is blocked'); return Object.keys(data).length; },
        key(i) { if (stored === THROWS) throw new Error('storage is blocked'); const names = Object.keys(data); return i < names.length ? names[i] : null; },
        removeItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); delete data[k]; },
    };
    // the tab's own storage (the lobby keeps its room and its key there): options.session is what it holds, options.noSession is a browser that has none; where the other storage throws this one does too
    const sessionData = stored === THROWS ? {} : Object.assign({}, options.session);
    const session = {
        data: sessionData,
        getItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); return Object.prototype.hasOwnProperty.call(sessionData, k) ? sessionData[k] : null; },
        setItem(k, v) { if (stored === THROWS) throw new Error('storage is blocked'); sessionData[k] = String(v); },
        removeItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); delete sessionData[k]; },
    };
    const history = { replaceState(...a) { env.replaced.push(a); } };
    const win = {
        location: { search, href: 'https://play.test/' + search, pathname: '/', origin: 'https://play.test', protocol: 'https:', host: 'play.test', assign(u) { env.assigned.push(u); }, reload() { env.reloads++; } },
        localStorage: storage, history,
        crypto: nodeCrypto.webcrypto,
        listeners: {},
        addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); },
        removeEventListener() {},
        open(url, name, features) { env.opened.push({ url, name, features }); return { closed: false, focus() {} }; },
        confirm(q) { env.confirms.push(q); return true; },
    };
    if (!options.noSession) win.sessionStorage = session;
    // a clock that stands still until env.advance(ms): the page's toasts and the client's waits
    const setTimer = (fn, ms) => { const id = env.nextTimer++; env.timers.push({ id, at: env.clock + ms, fn }); return id; };
    const clearTimer = (id) => { env.timers = env.timers.filter((t) => t.id !== id); };
    env.advance = (ms) => {
        const until = env.clock + ms;
        for (;;) {
            const due = env.timers.filter((t) => t.at <= until).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            env.timers = env.timers.filter((t) => t !== due);
            env.clock = Math.max(env.clock, due.at);
            due.fn();
        }
        env.clock = until;
    };
    class FakeSocket {
        constructor(url) { this.url = url; this.sent = []; this.closed = false; this.readyState = 0; env.sockets.push(this); }
        send(bytes) { if (this.closed || this.readyState !== 1) throw new Error('send on a socket that is not open'); this.sent.push(Uint8Array.from(bytes)); }
        close() { this.closed = true; }
        open() { this.readyState = 1; if (this.onopen) this.onopen(); return this; }
        receive(bytes) { if (this.onmessage) this.onmessage({ data: bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) }); return this; }
        drop() { this.closed = true; if (this.onclose) this.onclose({ code: 1006 }); }
    }
    const scope = { WebSocket: options.noWebSocket ? undefined : FakeSocket, setTimeout: setTimer, clearTimeout: clearTimer };
    win.AntsLobbyRules = loadFront('lobby_rules.js', scope).AntsLobbyRules;
    win.AntsLobbyNet = loadFront('lobby_net.js', scope).AntsLobbyNet;
    const nav = { userAgent: options.ua || 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36', platform: 'Linux x86_64' };
    if (options.clipboard !== false) nav.clipboard = { writeText(t) { env.copied.push(t); return { then() {} }; } };       // (options.clipboard: false is a browser without the clipboard's own API: the copy command is used)
    // options.share: a browser that has navigator.share (true: it takes the data; 'throws': it throws; 'rejects': the person closes the sheet, a promise that fails)
    if (options.share === true) nav.share = (d) => { env.shared.push(d); return Promise.resolve(); };
    if (options.share === 'throws') nav.share = () => { throw new Error('not allowed'); };
    if (options.share === 'rejects') nav.share = (d) => { env.shared.push(d); return Promise.reject(new Error('AbortError')); };
    // the site's answers (options.fetch(url, init) -> a promise of a response; the default is no network at all; options.noFetch is a browser that has no fetch)
    env.fetches = [];
    const network = options.fetch || (() => Promise.reject(new Error('the test has no network')));
    const fetchSpy = options.noFetch ? undefined : (url, init) => { env.fetches.push({ url, init }); return network(url, init); };
    const fakeMath = Object.assign(Object.create(Math), { random: () => (options.random === undefined ? 0.5 : options.random) });     // (the page's picked name, and the order of its random names, are the same on every run)
    new Function('window', 'document', 'history', 'navigator', 'setInterval', 'setTimeout', 'clearTimeout', 'fetch', 'BroadcastChannel', 'Math', lobbyScript)(win, doc, history, nav, () => 0, setTimer, clearTimer, fetchSpy, undefined, fakeMath);
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
    env.last = () => env.sockets[env.sockets.length - 1];
    // The newest socket opens and the server welcomes it as seat `seat` and tells it the room: seats [[state, name] x 4], o as roomMessage takes it (you: the seat, leader: the first)
    env.arrive = (o) => {
        o = o || {};
        const seat = o.seat === undefined ? 0 : o.seat;
        const s = env.last().open();
        s.receive(welcome(seat, o.key, o.flags));
        s.receive(roomMessage(o.seats || [[C, 'Maya'], [E, ''], [E, ''], [E, '']], Object.assign({ you: seat, leader: 0 }, o.room || {})));
        return s;
    };
    // a person presses a button of the page by its id; the card of a colour by its seat; the buttons inside it
    env.card = (seat) => env.$('slots').children.find((li) => li.getAttribute('data-i') === String(seat));
    env.inCard = (seat, attr) => env.card(seat).findAll((e) => e.hasAttribute(attr))[0] || null;
    env.press = (el) => env.$('slots').fire('click', { target: el, detail: 1 });
    env.pagePlays = () => env.assigned.length > 0;
    env.dumpAll = () => Object.values(env.elements).flatMap((e) => e.all()).map((e) => e.textContent + '|' + JSON.stringify(e.attributes) + '|' + e.value).join('\n') + env.innerHTMLWrites.join('') + JSON.stringify(env.replaced) + JSON.stringify(env.assigned);
    return env;
}
function randomName(n) { return NAMES_OF_THE_PAGE.indexOf(n) !== -1; }


(async () => {

// ---- 5.1 the plain visit: the page is the lobby, its room is made at once, under the name that this browser remembers or a picked one; nothing is asked first
{
    const env = runLobby('', {});
    check('a plain visit asks nothing first: no name card, no strip, no banner; the lobby is up and the page is not in its test-room mode', env.$('name-step').hidden && env.$('rejoin').hidden && env.$('banner').hidden && !env.$('lobby').hidden && env.$('room-panel').hidden && !env.body.classList.contains('in-room'));
    same('... it makes a room on the game server at once: one socket, to this site\'s /ws', env.sockets.map((s) => s.url), ['wss://play.test/ws']);
    const hello = env.last().open().sent[0];
    const code = helloRoom(hello);
    check('... the Hello makes the room: a new code of six characters, the name picked for a visitor who has none, no key, the lobby block', CODE.test(code) && helloName(hello) === SUGGESTION && hex(hello) === hex(helloBytes(SUGGESTION, code)), hex(hello));
    check('... the invitation is the page\'s address and ?room=<code>: no name, no key, no seat (whoever opens it is asked), and the code is shown in two groups of three', env.$('link').value === 'https://play.test/?room=' + code && env.$('code').textContent === grouped(code), env.$('link').value);
}
for (const [what, stored, want] of [['a remembered name', 'Maya', 'Maya'], ['a remembered name with blanks at its ends (they are cut off)', '  Maya  ', 'Maya'], ['a name that looks like markup (it is printable ASCII: a name, shown as text)', '<b>Hi</b> & "you"', '<b>Hi</b> & "you"'],
                                     ['a remembered name that the rules refuse (a computer player\'s)', 'Bot (x)', SUGGESTION], ['one with an accent', 'Zoë', SUGGESTION], ['one of 33 characters', 'a'.repeat(33), SUGGESTION], ['an empty one (none was chosen)', '', SUGGESTION]]) {
    const env = runLobby('', { 'ants.name': stored });
    const hello = env.last().open().sent[0];
    check('the page makes its room under ' + what + ': ' + JSON.stringify(want), helloName(hello) === want, helloName(hello));
}
{
    let env = null;
    try { env = runLobby('', THROWS); } catch (e) { check('a browser that refuses its storage still runs the page', false, e.message); }
    if (env) {
        const hello = env.last().open().sent[0];
        check('a browser that refuses its storage still makes its room (nothing is remembered, nothing breaks)', helloName(hello) === SUGGESTION && CODE.test(helloRoom(hello)));
        env.arrive();
        check('... and shows it', env.$('code').textContent === grouped(helloRoom(hello)) && env.card(0).textContent.indexOf('Maya') !== -1 && env.$('rejoin').hidden);
    }
    const none = runLobby('', {}, { noWebSocket: true });
    check('a browser with no WebSocket says that the game server cannot be reached, and nothing breaks', none.sockets.length === 0 && !none.$('banner').hidden && /Cannot reach the game server/.test(none.$('banner-text').textContent));
}
{   // the tab keeps its room and the key of its seat in the SESSION storage; a reload takes the seat back; the key is nowhere else
    const env = runLobby('', {});
    const s = env.arrive({ key: KEY });
    const code = helloRoom(s.sent[0]);
    same('once the room has welcomed the page, this tab\'s session storage keeps the room: its code, the key of the seat, the name that the room shows, and that this page made it', JSON.parse(env.session.data['ants.lobby']), { c: code, k: hex(KEY), n: 'Maya', o: 1 });
    const everything = env.dumpAll() + JSON.stringify(env.storage.data) + JSON.stringify(env.copied);
    check('THE KEY IS A SECRET: it is not in this browser\'s local storage, not in the link, and not in any text, attribute, field or address of the page (the game page finds it itself: START writes it once, below)',
          !('ants.lobby' in env.storage.data) && Object.keys(env.storage.data).every((k) => k.indexOf('ants.rejoin.') !== 0) && everything.indexOf(hex(KEY)) === -1 && everything.indexOf(hex(KEY).slice(0, 8)) === -1);
    const again = runLobby('', {}, { session: env.session.data });
    const h2 = again.last().open().sent[0];
    check('a reload of the tab takes the same seat back, with no card: the same code, the key of the seat and the name that the room showed', hex(h2) === hex(helloBytes('Maya', code, { key: KEY })) && again.$('name-step').hidden, hex(h2));
    for (const [what, junk] of [['text that is no JSON', 'not json'], ['an empty object', '{}'], ['a code with a capital letter (a control interface\'s)', JSON.stringify({ c: 'K7M2XQ', k: hex(KEY), n: 'x', o: 1 })], ['a key of zeros', JSON.stringify({ c: 'k7m2xq', k: '0'.repeat(32), n: 'x', o: 1 })], ['a key that is too short', JSON.stringify({ c: 'k7m2xq', k: 'abc', n: 'x', o: 1 })]]) {
        const e2 = runLobby('', {}, { session: { 'ants.lobby': junk } });
        const h = e2.last().open().sent[0];
        check('a session entry that is ' + what + ' is ignored: a new room with a new code and no key', CODE.test(helloRoom(h)) && helloRoom(h) !== 'k7m2xq' && hex(h) === hex(helloBytes(SUGGESTION, helloRoom(h))));
    }
    const codes = new Set();
    for (let i = 0; i < 20; i++) codes.add(helloRoom(runLobby('', {}).last().open().sent[0]));
    check('twenty visits have twenty codes, each of six characters of the alphabet without look-alikes (no i, l, o, 0 or 1)', codes.size === 20 && [...codes].every((c) => /^[abcdefghjkmnpqrstuvwxyz23456789]{6}$/.test(c)), [...codes].slice(0, 3).join(','));
}

// ---- 5.2 the room as the page paints it: a name is only ever text; the pencil changes the player's own name; a link made for somebody else carries no name
{
    const evil = '<img src=x onerror=alert(1)>';
    const env = runLobby('', { 'ants.name': 'Maya' });
    const s = env.arrive({ key: KEY, seats: [[C, 'Maya'], [C, evil], [E, ''], [E, '']] });
    same('the four cards are built in the reading order of the colours on the map: Black, Green, Red, Blue (seats 3, 0, 1, 2)', env.$('slots').children.map((li) => li.getAttribute('data-i')), ['3', '0', '1', '2']);
    check('a card shows the colour and the name of the player who has it, as text', env.card(0).textContent.indexOf('Maya') !== -1 && env.card(0).textContent.indexOf('Green') !== -1 && env.card(1).textContent.indexOf(evil) !== -1);
    check('... a name with < > & makes no element and no markup: the page made no markup at all', env.innerHTMLWrites.length === 0 && env.$('slots').findAll((e) => e.tagName === 'img').length === 0);
    check('... the text of the card of the players, to a screen reader, is an attribute (never markup): the colour and the name', env.card(1).getAttribute('aria-label') === 'Red: ' + evil);
    const pencil = env.inCard(0, 'data-edit');
    check('the host\'s own card has the pencil (named "Change your name"); no other card has one', !!pencil && pencil.textContent === '✎' && pencil.getAttribute('aria-label') === 'Change your name' && [1, 2, 3].every((i) => env.inCard(i, 'data-edit') === null));
    const field = () => env.card(0).findAll((e) => e.tagName === 'input')[0];
    const before = s.sent.length;
    env.press(pencil);
    check('the pencil turns the name into a field that holds it (32 characters at the most, labelled)', !!field() && field().value === 'Maya' && field().maxLength === 32 && field().getAttribute('aria-label') === 'Your name');
    field().value = 'Bot (x)';
    field().key('Enter');
    check('a name that the rules refuse is explained (the toast) and the room is told nothing; the card shows the old name again and the browser keeps its own', /computer players/.test(env.$('toast').textContent) && !env.$('toast').hidden && s.sent.length === before && !field() && env.card(0).textContent.indexOf('Maya') !== -1 && env.storage.data['ants.name'] === 'Maya');
    env.press(env.inCard(0, 'data-edit'));
    field().value = 'Zoë';
    field().key('Enter');
    check('... an accent too (ASCII only)', /ASCII/.test(env.$('toast').textContent) && s.sent.length === before);
    env.press(env.inCard(0, 'data-edit'));
    field().value = 'Maya';
    field().key('Enter');
    env.press(env.inCard(0, 'data-edit'));
    field().value = '';
    field().key('Enter');
    check('the same name, or no name at all, asks the room for nothing', s.sent.length === before && env.storage.data['ants.name'] === 'Maya');
    env.press(env.inCard(0, 'data-edit'));
    field().value = '  Mia  ';
    field().key('Enter');
    check('a new name that the rules take (blanks cut off) asks the room to rename: one Name message, and the browser remembers it', s.sent.length === before + 1 && hex(s.sent[before]) === hex(NET0.encodeName('Mia')) && env.storage.data['ants.name'] === 'Mia', hex(s.sent[before] || []));
    s.receive(roomMessage([[C, 'Mia'], [C, evil], [E, ''], [E, '']], { you: 0, leader: 0 }));
    check('... and the card shows it when the room says so', env.card(0).textContent.indexOf('Mia') !== -1 && env.card(0).textContent.indexOf('Maya') === -1);
    const done = env.sockets.length;
    env.press(env.inCard(0, 'data-edit'));
    field().value = 'Zed';
    s.receive(roomMessage([[C, 'Mia'], [C, evil], [C, 'Newcomer'], [E, '']], { you: 0, leader: 0 }));
    check('a room that changes while the name is being typed does not take the field away (the cards are drawn again afterwards)', !!field() && field().value === 'Zed' && env.card(2).textContent.indexOf('Newcomer') === -1);
    field().key('Enter');
    check('... and the card of the newcomer is there when the field is done', env.card(2).textContent.indexOf('Newcomer') !== -1 && env.sockets.length === done);
    // Remove: the host asks first; only a yes removes; the name is text in the question and in the toast
    const rm = env.inCard(1, 'data-remove');
    check('the host\'s screen has a Remove button on another player\'s card and none on the own card', !!rm && rm.textContent === 'Remove' && env.inCard(0, 'data-remove') === null && rm.getAttribute('aria-label') === 'Remove ' + evil + ' from the room');
    const sentBefore = s.sent.length;
    env.press(rm);
    const ask = env.card(1).findAll((e) => e.hasAttribute('data-remove-yes'))[0];
    const keep = env.card(1).findAll((e) => e.hasAttribute('data-remove-no'))[0];
    check('Remove asks first ("Remove <name> from the room?" with Remove and Keep), the focus is on Keep, and nothing is sent', !!ask && !!keep && env.card(1).textContent.indexOf('Remove ' + evil + ' from the room?') !== -1 && env.focused === keep && s.sent.length === sentBefore && env.innerHTMLWrites.length === 0);
    env.press(keep);
    check('Keep puts the question away and sends nothing', env.card(1).findAll((e) => e.hasAttribute('data-remove-yes')).length === 0 && s.sent.length === sentBefore && !!env.inCard(1, 'data-remove'));
    env.press(env.inCard(1, 'data-remove'));
    env.press(env.card(1).findAll((e) => e.hasAttribute('data-remove-yes'))[0]);
    const guard = NET0.seatingHash(NET0.decode(roomMessage([[C, 'Mia'], [C, evil], [C, 'Newcomer'], [E, '']], { you: 0, leader: 0 })));
    check('a yes sends one Remove message with the guard of the seating that the host saw, and says so in the toast (the name as text)', s.sent.length === sentBefore + 1 && hex(s.sent[sentBefore]) === hex(NET0.encodeRemove(1, guard)) && env.$('toast').textContent === evil + ' was removed. Red is open again.', hex(s.sent[sentBefore] || []));
}
{   // a player's screen: the pencil on the own card, no Remove, and the host's name is the host's
    const env = runLobby('', { 'ants.name': 'Sam' });
    const s = env.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    check('a player\'s screen has the pencil on the own card only, and no Remove button anywhere', !!env.inCard(1, 'data-edit') && env.inCard(0, 'data-edit') === null && env.$('slots').findAll((e) => e.hasAttribute('data-remove')).length === 0);
    same('... the room is named after the host, and START waits for the host', [env.$('room-h').textContent, env.$('start').disabled, env.$('start').textContent], ['Maya’s room', true, 'Waiting for Maya']);
    env.press(env.inCard(1, 'data-edit'));
    const f = env.card(1).findAll((e) => e.tagName === 'input')[0];
    f.value = 'Sammy';
    f.key('Enter');
    check('a player renames too: the room is asked, with the name', hex(s.sent[s.sent.length - 1]) === hex(NET0.encodeName('Sammy')) && env.storage.data['ants.name'] === 'Sammy');
}
{   // the link, the Copy button and the Share sheet carry the room and nothing of the player
    const env = runLobby('', { 'ants.name': 'Maya' }, { share: true });
    env.arrive({ key: KEY });
    const code = env.$('code').textContent.replace(' ', '');
    env.$('copy').click();
    same('Copy link copies the invitation: the page\'s address and ?room=<code>', env.copied, ['https://play.test/?room=' + code]);
    check('... the Share button is there on a browser that can share, and shares the same link only', env.$('share').hidden === false && (env.$('share').click(), env.shared.length === 1 && JSON.stringify(env.shared[0]) === JSON.stringify({ url: 'https://play.test/?room=' + code })));
    check('... the field holds it (a name is never in it)', env.$('link').value === 'https://play.test/?room=' + code && env.param(env.$('link').value, 'name') === null);
    const plain = runLobby('', {});
    check('a browser that cannot share has no Share button', plain.$('share').hidden === true);
}

// ---- 5.3 the name card of a link that somebody sent (?room=): asked first, every time; nothing connects before the button
{
    const env = runLobby('?room=k7m2xq', { 'ants.name': 'Maya' });
    check('a link to a room asks for a name first, every time, even when this browser remembers one: the card is up over the page', !env.$('name-step').hidden && env.$('name-step').getAttribute('role') === 'dialog' && env.$('name-step').getAttribute('aria-modal') === 'true');
    same('... titled with the room as a screen shows it (two groups of three), a Join button, the remembered name in the field', [env.$('name-step-title').textContent, env.$('name-step-go').textContent, env.$('name-step-input').value], ['Join the room k7m 2xq', 'Join', 'Maya']);
    same('... the field\'s placeholder and the hint say the name that an empty field gets; the way out is a button', [env.$('name-step-input').placeholder, env.$('name-step-hint').textContent, env.$('name-step-own').textContent],
         [SUGGESTION, 'The name the other players see. Leave it empty to be called ' + SUGGESTION + '.', 'Start a room of my own instead']);
    check('... the notice that online matches are recorded is shown, and there is no message yet', !env.$('who-notice').hidden && env.$('name-step-msg').textContent === '');
    check('nothing connects before the button: no socket, the address is not rewritten, and the page behind the card is "Joining a room" with START off and the colours greyed', env.sockets.length === 0 && env.replaced.length === 0 && env.$('room-h').textContent === 'Joining a room' && env.$('start').disabled === true && env.body.classList.contains('offline') && env.body.classList.contains('joining'));
    env.type('name-step-input', 'Bot (x)');
    env.$('name-step-go').click();
    check('a name that is a computer player\'s is explained under the field, and nothing happens: the card stays, no socket, nothing remembered', /computer players/.test(env.$('name-step-msg').textContent) && !env.$('name-step').hidden && env.sockets.length === 0 && env.storage.data['ants.name'] === 'Maya');
    env.type('name-step-input', 'Zoë');
    env.$('name-step-input').key('Enter');
    check('... an accent too (Enter asks as the button does), and typing again takes the message away', /ASCII/.test(env.$('name-step-msg').textContent) && env.sockets.length === 0 && (env.type('name-step-input', 'Zed'), env.$('name-step-msg').textContent === ''));
    env.$('name-step-input').key('Enter');
    check('Enter with a good name joins: the card goes away, the page connects (one socket) and the browser remembers the name', env.$('name-step').hidden && env.sockets.length === 1 && env.storage.data['ants.name'] === 'Zed');
    const hello = env.last().open().sent[0];
    check('... the Hello is the name that was typed, in the room of the link, with the lobby block (a room that is gone is made again, as the owner\'s picture 9 shows) and no key', hex(hello) === hex(helloBytes('Zed', 'k7m2xq')) && env.replaced.length === 0, hex(hello));
}
{
    const env = runLobby('?room=k7m2xq', {});
    check('a link with nothing remembered: an empty field with the picked name as its placeholder', env.$('name-step-input').value === '' && env.$('name-step-input').placeholder === SUGGESTION && !env.$('name-step').hidden);
    env.$('name-step-go').click();
    const hello = env.last().open().sent[0];
    check('... an empty field is fine: the page plays under the picked name that the placeholder showed, and the browser remembers that none was chosen', helloName(hello) === SUGGESTION && env.storage.data['ants.name'] === '' && env.$('name-step').hidden);
}
{   // a link whose room is gone, full, running or a seat that was refused: a room of the visitor's own, under the same name, with the strip's sentence
    const link = (name) => { const e = runLobby('?room=k7m2xq', {}); e.type('name-step-input', name); e.$('name-step-go').click(); return e; };
    const gone = link('Zed');
    gone.arrive({ flags: 2, seats: [[C, 'Zed'], [E, ''], [E, ''], [E, '']] });
    same('a link to a room that has gone (the Hello made a new one): the strip says so, with the page\'s own sentence', [gone.$('banner').hidden, gone.$('banner-text').textContent, gone.$('banner-x').textContent], [false, 'That room is gone, because everybody left, so this one is yours now. Send the link on if you like.', 'OK']);
    const joined = link('Zed');
    joined.arrive({ seat: 1, flags: 0, seats: [[C, 'Maya'], [C, 'Zed'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    check('a link to a room that is there joins it: no strip, the room is the host\'s', joined.$('banner').hidden && joined.$('room-h').textContent === 'Maya’s room' && joined.$('code').textContent === 'k7m 2xq');
    for (const [what, reason, sentence] of [['full', 1, 'That room is full, so this one is yours now. Send the link on if you like.'], ['whose match has started', 3, 'The match in that room has already started, so this room is yours instead. Send the link on if you like.']]) {
        const e = link('Zed');
        e.last().open().receive(reject(reason));
        const next = e.last().open().sent[0];
        check('a link to a room that is ' + what + ': the page makes a room of its own under the same name (a new code, no key), and the strip says why', e.sockets.length === 2 && CODE.test(helloRoom(next)) && helloRoom(next) !== 'k7m2xq' && hex(next) === hex(helloBytes('Zed', helloRoom(next))) && e.$('banner-text').textContent === sentence, e.$('banner-text').textContent);
        check('... and its address is the plain page again (the link is no longer in the bar)', e.replaced.length === 1 && e.replaced[0][2] === '/');
    }
    const kicked = runLobby('', { 'ants.name': 'Sam' });
    kicked.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0 } }).receive(reject(4));
    const own = kicked.last().open().sent[0];
    check('a player whom the host removed gets a room of their own under their name: a new code, no key (the server forgot it), and the strip says why', kicked.sockets.length === 2 && hex(own) === hex(helloBytes('Sam', helloRoom(own))) && CODE.test(helloRoom(own)) && kicked.$('banner-text').textContent === 'The host removed you from the room, so this one is yours now. Send the link on if you like.');
    check('... and that session entry is the old room\'s no longer (the key of the old room is let go of)', !(kicked.session.data['ants.lobby'] && JSON.parse(kicked.session.data['ants.lobby']).k === hex(KEY)));
}
{   // "Start a room of my own instead": the page's own room, and the one that this tab already had when it has one
    const env = runLobby('?room=k7m2xq', { 'ants.name': 'Maya' });
    env.$('name-step-own').click();
    const hello = env.last().open().sent[0];
    check('"Start a room of my own instead" closes the card, makes a room of the visitor\'s own (a new code, the remembered name) and puts the plain address in the bar', env.$('name-step').hidden && env.sockets.length === 1 && CODE.test(helloRoom(hello)) && helloRoom(hello) !== 'k7m2xq' && hex(hello) === hex(helloBytes('Maya', helloRoom(hello))) && env.replaced.length === 1 && env.replaced[0][2] === '/');
    const kept = { 'ants.lobby': JSON.stringify({ c: 'abc234', k: hex(KEY), n: 'Maya', o: 1 }) };
    const env2 = runLobby('?room=k7m2xq', { 'ants.name': 'Maya' }, { session: kept });
    check('a tab that has a room of its own is asked first too, and nothing connects before the card is done (the own room waits)', !env2.$('name-step').hidden && env2.sockets.length === 0);
    env2.$('name-step-own').click();
    check('... "my own instead" takes the own room back with its key', hex(env2.last().open().sent[0]) === hex(helloBytes('Maya', 'abc234', { key: KEY })));
    const env3 = runLobby('?room=k7m2xq', {}, { session: { 'ants.lobby': JSON.stringify({ c: 'k7m2xq', k: hex(KEY), n: 'Sam', o: 0 }) } });
    check('a reload of the tab that joined that very room takes the seat back at once: no card, the key, the name that the room showed', env3.$('name-step').hidden && hex(env3.last().open().sent[0]) === hex(helloBytes('Sam', 'k7m2xq', { key: KEY })));
    for (const bad of ['?room=a%20b', '?room=k7m%202xq', '?room=', '?room=' + 'x'.repeat(33), '?room=k7m2xq%00', '?room=..%2Fx']) {
        const e = runLobby(bad, {});
        check('an address with no room code that a room can have (' + bad.slice(0, 20) + ') is the plain page: nothing is asked, the page makes a room of its own', e.$('name-step').hidden && e.sockets.length === 1 && !e.$('lobby').hidden && e.$('room-panel').hidden && e.replaced.length === 0);
    }
    check('an address with only a picture is the plain page too', runLobby('?aspect=4:3', {}).$('name-step').hidden);
}

// ---- 5.4 "Have a code?": the name card first, then a look at the room while the page's own room stays
{
    const env = runLobby('', { 'ants.name': 'Maya' });
    const own = env.arrive({ key: KEY });
    const ownCode = helloRoom(own.sent[0]);
    check('"Have a code?" opens the box (hidden until then) and puts the cursor in it', env.$('joinbox').hidden && (env.$('havecode').click(), !env.$('joinbox').hidden && env.focused === env.$('joincode')));
    for (const [what, typed, why] of [['nothing', '', 'Type or paste the code first.'], ['a line of blanks', '   \n', 'Type or paste the code first.'], ['text that is no code', 'not a code!', 'A room code has letters and numbers only, like k7m 2xq.'], ['a path', 'k7m2/xq', 'A room code has letters and numbers only, like k7m 2xq.'],
                                       ['an accent', 'abé', 'A room code has letters and numbers only, like k7m 2xq.'], ['33 characters', 'x'.repeat(33), 'A room code has letters and numbers only, like k7m 2xq.'], ['an address', '?room=x&seat=1', 'A room code has letters and numbers only, like k7m 2xq.']]) {
        env.type('joincode', typed);
        env.$('joingo').click();
        check('typing ' + what + ' and pressing Join explains it under the box and opens no card, no socket', env.$('joinhint').textContent === why && env.$('name-step').hidden && env.sockets.length === 1, env.$('joinhint').textContent);
    }
    env.type('joincode', '  ' + grouped(ownCode).toUpperCase() + ' ');
    env.$('joingo').click();
    check('the code of the page\'s own room (typed as shown, in capitals) is its own: "That is your own room." and no card', env.$('joinhint').textContent === 'That is your own room.' && env.$('name-step').hidden && env.sockets.length === 1);
    env.type('joincode', 'K7M 2xq');
    check('typing again takes the hint away', env.$('joinhint').textContent === '');
    env.$('joincode').key('Enter');
    check('Enter in the box asks for the name too: the card is up, titled with the code as typed in two groups, and nothing connects yet (blanks and capitals of a typed or pasted code do not matter)', !env.$('name-step').hidden && env.$('name-step-title').textContent === 'Join the room k7m 2xq' && env.$('name-step-go').textContent === 'Join' && env.$('name-step-input').value === 'Maya' && env.sockets.length === 1);
    check('... the code in the box stays as it was typed', env.$('joincode').value === 'K7M 2xq');
    env.type('name-step-input', 'Bot (x)');
    env.$('name-step-go').click();
    check('a bad name is explained on the card and nothing is looked at', /computer players/.test(env.$('name-step-msg').textContent) && env.sockets.length === 1 && !env.$('name-step').hidden);
    env.type('name-step-input', 'Ann & <b>Bob</b>');
    env.$('name-step-go').click();
    const probe = env.last();
    check('a good name looks at the room: a second socket, with the Hello of a page that only joins (no lobby block: a code with no room is answered "no such room", never made), under the name typed (< > & are just characters)', env.sockets.length === 2 && env.$('name-step').hidden && hex(probe.open().sent[0]) === hex(helloBytes('Ann & <b>Bob</b>', 'k7m2xq', { join: true })), hex(probe.sent[0] || []));
    check('... the page\'s own room stays while it looks: no Leave, the socket open, the code and the link still the own room\'s', !own.closed && own.sent.length === 1 && env.$('code').textContent === grouped(ownCode) && env.$('link').value === 'https://play.test/?room=' + ownCode && env.storage.data['ants.name'] === 'Ann & <b>Bob</b>');
    probe.receive(reject(6));
    check('a room that does not exist: the box says so, and the own room is as it was', env.$('joinhint').textContent === 'There is no room with that code. Check it, or ask the host to send the link again.' && !own.closed && env.$('code').textContent === grouped(ownCode) && env.innerHTMLWrites.length === 0);
}
{
    const typedJoin = (typed, name) => {
        const e = runLobby('', {});
        e.arrive({ key: KEY });
        e.$('havecode').click();
        e.type('joincode', typed);
        e.$('joingo').click();
        if (name !== undefined) e.type('name-step-input', name);
        e.$('name-step-go').click();
        return e;
    };
    for (const [typed, want] of [['k7m2xq', 'k7m2xq'], ['k7m 2xq', 'k7m2xq'], ['  k7m 2xq  ', 'k7m2xq'], ['k7m\t2xq\n', 'k7m2xq'], ['k7m 2xq', 'k7m2xq'], [' k 7 m 2 x q ', 'k7m2xq'], ['K7M 2XQ', 'k7m2xq'], ['my_room-1', 'my_room-1'], ['a', 'a'], ['--', '--'],
                                 ['a'.repeat(32), 'a'.repeat(32)], ['demo-small-2p-x7k2', 'demo-small-2p-x7k2'], ['demo-tiny-2p-t01-abcdef', 'demo-tiny-2p-t01-abcdef'], ['abc def ghi', 'abcdefghi']]) {
        const e = typedJoin(typed);
        const hello = e.last().open().sent[0];
        check('Join takes ' + JSON.stringify(typed) + ' as the code ' + want + ' (blanks dropped, capitals lowered; nothing else is made of it, and an older, longer code is a code too): a joining Hello for it, under the picked name', e.sockets.length === 2 && hex(hello) === hex(helloBytes(SUGGESTION, want, { join: true })) && e.$('joinhint').textContent === '', hex(hello));
    }
    const full = typedJoin('k7m2xq', 'Zed');
    full.last().open().receive(reject(1));
    check('a room that is full: "That room is full." under the box', full.$('joinhint').textContent === 'That room is full.');
    const running = typedJoin('k7m2xq', 'Zed');
    running.last().open().receive(reject(3));
    check('a room whose match has started: "The match in that room has already started."', running.$('joinhint').textContent === 'The match in that room has already started.');
    const old = typedJoin('k7m2xq', 'Zed');
    old.last().open().receive(reject(2));
    check('a server that speaks another version: the strip asks for a reload', !old.$('banner').hidden && /A newer version of Ants is out/.test(old.$('banner-text').textContent) && old.$('banner-x').textContent === 'Reload');
    // the probe is welcomed: the page leaves its own room and shows that one
    const ok = typedJoin('k7m2xq', 'Zed');
    const ownSocket = ok.sockets[0];
    const p = ok.last().open();
    p.receive(welcome(1, Uint8Array.from(Array.from({ length: 16 }, (_, i) => 0x60 + i)), 0));
    p.receive(roomMessage([[C, 'Maya'], [C, 'Zed'], [E, ''], [E, '']], { you: 1, leader: 0 }));
    check('a room that takes the player: the page leaves its own room (a Leave on the old socket, which closes) and shows that room: the code, the host\'s name, the link to it', ownSocket.sent.length === 2 && ownSocket.sent[1][0] === NET0.MSG.Leave && ownSocket.closed && ok.$('code').textContent === 'k7m 2xq' && ok.$('room-h').textContent === 'Maya’s room' && ok.$('link').value === 'https://play.test/?room=k7m2xq' && ok.$('joinbox').hidden);
    same('... and this tab keeps that room (session storage), not the old one: the code, the key, the name, and that the page did not make it', JSON.parse(ok.session.data['ants.lobby']), { c: 'k7m2xq', k: hex(Uint8Array.from(Array.from({ length: 16 }, (_, i) => 0x60 + i))), n: 'Zed', o: 0 });
    const away = runLobby('', {});
    away.arrive({ key: KEY });
    away.$('havecode').click();
    away.type('joincode', 'k7m2xq');
    away.$('joingo').click();
    away.$('name-step-own').click();
    check('"Start a room of my own instead" on that card closes it and the own room stays (no new socket)', away.$('name-step').hidden && away.sockets.length === 1 && !away.sockets[0].closed && away.$('joinbox').hidden === false);
}

// ---- 5.5 START: the page hands its seat to the game page with the key in the browser's storage, and does not leave the room
{
    const KEY_ENTRY = 'ants.rejoin.';
    const env = runLobby('', { 'ants.name': 'Maya', 'ants.rejoin.old-room.1': '{"k":"0f1e2d3c4b5a69788796a5b4c3d2e1f0","s":"wss://play.test/ws","t":' + (Date.now() - 600000) + '}' });
    check('before START: the way back to an older match is offered, and the page is waiting in its room', !env.$('rejoin').hidden);
    const s = env.arrive({ key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']] });
    const code = helloRoom(s.sent[0]);
    check('with another player in the room START is on, and says START!', env.$('start').disabled === false && env.$('start').textContent === 'START!');
    env.$('start').click();
    check('START asks the room (one StartRequest) and the page goes on waiting: nothing has moved yet', s.sent.length === 2 && s.sent[1][0] === NET0.MSG.StartRequest && env.assigned.length === 0 && Object.keys(env.storage.data).every((k) => k !== KEY_ENTRY + code + '.0'));
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0, flags: 7 }));
    const want = 'https://play.test/?join=/ws&room=' + code + '&seat=0&name=Maya&aspect=16:9';
    same('the room says that START was pressed: this tab goes to the game page of the room (the plain code, the seat, the name, the picture\'s shape; no key, no create block)', env.assigned, [want]);
    const entry = JSON.parse(env.storage.data[KEY_ENTRY + code + '.0']);
    check('... the seat\'s key is in the browser\'s local storage as ants.rejoin.<room>.<seat> = {"k", "s", "t"}: the game page takes the seat from it', Object.keys(entry).sort().join() === 'k,s,t' && entry.k === hex(KEY) && entry.s === 'wss://play.test/ws' && Math.abs(entry.t - Date.now()) < 5000);
    check('... and the key is in no address, text or attribute of the page', env.assigned[0].indexOf(hex(KEY)) === -1 && env.dumpAll().indexOf(hex(KEY)) === -1 && noBlockOf(env, env.assigned[0]));
    const P2 = P;
    const gameArgs = P2.joinArguments(new URL(want).search, true, 'play.test').args;
    check('... the game page reads that address as a join of this room and seat that asks for no name, and holds the seat (its own look at the storage finds the entry): it goes straight in', gameArgs.join(' ') === '--join-url wss://play.test/ws --room ' + code + ' --seat 0 --name Maya' && P2.asksForName(new URL(want).search) === false &&
          shell.holdsThisSeat(new URL(want).search, gameArgs, { length: Object.keys(env.storage.data).length, key: (i) => Object.keys(env.storage.data)[i] || null, getItem: (k) => (k in env.storage.data ? env.storage.data[k] : null), removeItem() {} }, Date.now()) === true);
    check('the lobby does not leave the room at START: no Leave message, the socket stays open (the game\'s own Hello takes the seat over), and the tab forgets its room (Back makes a new one)', s.sent.every((b) => b[0] !== NET0.MSG.Leave) && !s.closed && env.session.data['ants.lobby'] === undefined);
    check('... the strip of the older match is gone: the page is on its way', env.$('rejoin').hidden);
    (env.win.listeners.storage || []).forEach((fn) => fn({}));
    check('... and a change of the storage from another tab does not bring the strip back for the entry that was just written (the page is on its way to that very match)', env.$('rejoin').hidden);
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0, flags: 7 }));
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0, flags: 3 }));
    check('what the room says after that is not heard (the page has gone: one hand-over only)', env.assigned.length === 1);
}
{   // a player's page goes the same way when the room says START, with its own seat and name
    const env = runLobby('', { 'ants.name': 'Sam' });
    const s = env.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    const code = helloRoom(s.sent[0]);
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 0, flags: 7 }));
    same('a player\'s page, when the room says START, goes to the game page with its own seat and name', env.assigned, ['https://play.test/?join=/ws&room=' + code + '&seat=1&name=Sam&aspect=16:9']);
    check('... the entry is under that seat', !!env.storage.data['ants.rejoin.' + code + '.1'] && Object.keys(env.storage.data).filter((k) => k.indexOf('ants.rejoin.') === 0).length === 1);
    const shape = runLobby('?aspect=4:3', { 'ants.name': 'Maya' });
    const t = shape.arrive({ key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']] });
    t.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0, flags: 7 }));
    check('the picture\'s shape of the page goes with it', /&aspect=4:3$/.test(shape.assigned[0] || ''), shape.assigned[0]);
}
{   // a match for one: the game page's own single player; the room is let go of
    const env = runLobby('', { 'ants.name': 'Maya' });
    const s = env.arrive({ key: KEY });
    env.$('start').click();
    same('START with nobody else in the room plays a game on this computer: the game page\'s own single player, on the map of the room, under the name of the player', env.assigned, ['https://play.test/play.html?map=treasure&name=Maya&aspect=16:9']);
    check('... the room is let go of (a Leave, the socket closes), the tab forgets it, and no key is written for the game page', s.sent.length === 2 && s.sent[1][0] === NET0.MSG.Leave && s.closed && env.session.data['ants.lobby'] === undefined && Object.keys(env.storage.data).every((k) => k.indexOf('ants.rejoin.') !== 0));
    const evil = runLobby('', { 'ants.name': '<b>x</b>&y' });
    evil.arrive({ key: KEY, seats: [[C, '<b>x</b>&y'], [E, ''], [E, ''], [E, '']] });
    evil.$('start').click();
    check('a name with < > & goes into that address encoded (a plain value in the query, never markup)', evil.assigned[0] === 'https://play.test/play.html?map=treasure&name=%3Cb%3Ex%3C%2Fb%3E%26y&aspect=16:9' && evil.param(evil.assigned[0], 'name') === '<b>x</b>&y', evil.assigned[0]);
}

// ---- 5.5b what the review of the page found: a room that waits for the games when the page first hears of it, a team that is left alone twice, the line of a move, a refusal that nothing knows
{   // a page that is answered with a room that waits for the games already (a reload, or a lost link that came back with its key during the wait) goes to its game as the others did
    const env = runLobby('', { 'ants.name': 'Sam' });
    const s = env.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0, flags: 7 } });
    const code = helloRoom(s.sent[0]);
    same('the first Room that a page hears already says START was pressed: the page goes to the game page of its seat, as if it had seen the change', env.assigned, ['https://play.test/?join=/ws&room=' + code + '&seat=1&name=Sam&aspect=16:9']);
    check('... with its key in the browser\'s storage, and the room forgotten by the tab', !!env.storage.data['ants.rejoin.' + code + '.1'] && env.session.data['ants.lobby'] === undefined);
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 0, flags: 7 }));
    check('... once (a second Room of the same wait leads nowhere else)', env.assigned.length === 1);
}
{   // the host's page corrects a team that was left alone, and does so again the next time (the stamp of the correction is let go of when the room is right)
    const env = runLobby('', { 'ants.name': 'Maya' });
    const three = [[C, 'Maya'], [C, 'Sam'], [C, 'Alex'], [E, '']], two = [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']];
    const s = env.arrive({ key: KEY, seats: three, room: { you: 0, leader: 0, teamA: 1, teamB: 2 } });
    const plans = () => s.sent.filter((b) => b[0] === NET0.MSG.Plan).length;
    check('three players with a team of two need no correction', plans() === 0);
    s.receive(roomMessage(two, { you: 0, leader: 0, teamA: 1, teamB: 2 }));
    check('Alex leaves and the team is a player alone: the host\'s page sends the plan again, with the teams cleared (one Plan)', plans() === 1);
    s.receive(roomMessage(two, { you: 0, leader: 0, teamA: 1, teamB: 2 }));
    check('... and does not send it twice for the same room', plans() === 1);
    s.receive(roomMessage(two, { you: 0, leader: 0 }));
    s.receive(roomMessage(three, { you: 0, leader: 0 }));
    s.receive(roomMessage(three, { you: 0, leader: 0, teamA: 1, teamB: 2 }));
    s.receive(roomMessage(two, { you: 0, leader: 0, teamA: 1, teamB: 2 }));
    check('the same thing the next time (the room was right in between): the correction is sent again, so the room does not keep a team of one', plans() === 2);
}
{   // the line that the room sends a moved player is no strip; any other line of the room still is one
    const chatFromRoom = (text) => { const w = [9, 255, 0]; str8(w, text); return Uint8Array.from(w); };
    const env = runLobby('', { 'ants.name': 'Sam' });
    const s = env.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    s.receive(chatFromRoom('Maya moved you to Blue.'));
    check('"Maya moved you to Blue." shows no strip (the colour and the toast say it)', env.$('banner').hidden);
    s.receive(chatFromRoom('No teams: the teams need a pair.'));
    check('... another line of the room is a strip of its own, with the server\'s words', !env.$('banner').hidden && env.$('banner-text').textContent === 'No teams: the teams need a pair.');
}
{   // a refusal that the page has no word for: a link makes the page's own room; the page's own room that is refused says "try again" with a button that does
    const link = runLobby('?room=k7m2xq', {});
    link.type('name-step-input', 'Zed');
    link.$('name-step-go').click();
    link.last().open().receive(reject(5));
    const next = link.last().open().sent[0];
    check('a link whose room refuses the page (BadRequest): the page makes a room of its own with a new code, and says the match there is not for this page', link.sockets.length === 2 && CODE.test(helloRoom(next)) && helloRoom(next) !== 'k7m2xq' && !link.$('banner').hidden && link.$('banner-text').textContent.indexOf('trying again') === -1);
    const own = runLobby('', { 'ants.name': 'Sam' });
    own.last().open().receive(reject(5));
    check('the page\'s own room refused for a reason that nothing knows: no new room (no loop), the strip is the busy one with its Try again button, and no strip promises that the page is trying again', own.sockets.length === 1 && !own.$('banner').hidden && own.$('banner-x').textContent === 'Try again' && own.$('banner-text').textContent.indexOf('trying again') === -1);
}


// ---- 5.6 the Rejoin strip at the top of the page (the block REJOIN has its own check, web_rejoin_block_check.js; here is the page as a whole: when it is there, what it says, where it goes)
{
    const SERVER = 'wss://play.test/ws';                                    // (the fake page is https://play.test/)
    const RKEY = '0f1e2d3c4b5a69788796a5b4c3d2e1f0';
    const entry = (age, server) => '{"k":"' + RKEY + '","s":"' + (server || SERVER) + '","t":' + (Date.now() - age) + '}';
    const rejoinView = (env) => ({ hidden: env.$('rejoin').hidden, button: env.$('rejoin-go').textContent, note: env.$('rejoin-note').textContent });
    {
        const env = runLobby('', {});
        same('no entry: nothing of the strip is shown, and the lobby is made as ever', [rejoinView(env).hidden, env.sockets.length, env.$('lobby').hidden], [true, 1, false]);
    }
    {
        const stored = { 'ants.rejoin.k7m2xq.1': entry(5000) };
        const env = runLobby('', stored);
        same('a fresh entry of this site: the strip says that the match still runs (the room as a screen shows it, in two groups of three) and offers one button, "Rejoin it"', rejoinView(env), { hidden: false, button: 'Rejoin it', note: 'Your match in room k7m 2xq is still running: go back to your seat.' });
        check('... the lobby is there as ever (the strip is above it: the page still makes its room)', env.sockets.length === 1 && !env.$('lobby').hidden);
        check('... and the key is in no text or attribute of the page', env.dumpAll().indexOf(RKEY) === -1 && env.dumpAll().indexOf(RKEY.slice(0, 8)) === -1);
        env.$('rejoin-go').click();
        same('pressing it takes THIS tab to the game page: the plain code, the seat, the (empty) name and the shape', env.assigned, ['https://play.test/?join=/ws&room=k7m2xq&seat=1&name=&aspect=16:9']);
        check('... with no key in the address, no create block (the seat is already in the room: the game finds its key by itself), nothing opened in another window, and nothing written to the storage', env.assigned[0].indexOf(RKEY) === -1 && noBlockOf(env, env.assigned[0]) && env.opened.length === 0 && env.storage.data['ants.rejoin.k7m2xq.1'] === stored['ants.rejoin.k7m2xq.1'] && Object.keys(env.storage.data).length === 1);
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
        const env = runLobby('', { 'ants.rejoin.r-1.0': entry(1000), 'ants.name': 'Zoë' });
        env.$('rejoin-go').click();
        check('a remembered name that the rules refuse is no name', env.assigned.length === 1 && env.param(env.assigned[0], 'name') === '' && /&name=&aspect=/.test(env.assigned[0]), env.assigned[0]);
        const evil = runLobby('', { 'ants.rejoin.r-1.0': entry(1000), 'ants.name': 'A&B <i>' });
        evil.$('rejoin-go').click();
        check('a remembered name with < > & goes into the address encoded', evil.assigned[0] === 'https://play.test/?join=/ws&room=r-1&seat=0&name=A%26B%20%3Ci%3E&aspect=16:9', evil.assigned[0]);
    }
    {
        const env = runLobby('', { 'ants.rejoin.old-match.0': entry(4 * 3600 * 1000), 'ants.rejoin.mid-match.1': entry(2 * 3600 * 1000) });
        same('an entry older than three hours is not offered and is removed (as the game does); one a little younger is the offer (a code of another length than six is shown as it is)', [rejoinView(env).note, Object.keys(env.storage.data)], ['Your match in room mid-match is still running: go back to your seat.', ['ants.rejoin.mid-match.1']]);
    }
    {
        const env = runLobby('', { 'ants.rejoin.other.0': entry(1000, 'wss://other.test/ws'), 'ants.rejoin.mine.1': entry(9000) });
        same('an entry of another server is not offered and stays, even when it is newer', [/room mine /.test(rejoinView(env).note), Object.keys(env.storage.data).sort()], [true, ['ants.rejoin.mine.1', 'ants.rejoin.other.0']]);
    }
    {
        const env = runLobby('', { 'ants.rejoin.other.0': entry(1000, 'ws://play.test/ws'), 'ants.rejoin.bad.7': entry(1000), 'ants.rejoin.junk.1': 'not json' });
        check('only entries of the game count: another scheme, a seat that is none and a value that is no JSON show nothing', rejoinView(env).hidden === true && Object.keys(env.storage.data).length === 3);
    }
    {
        const env = runLobby('', { 'ants.rejoin.a.0': entry(60000), 'ants.rejoin.b.2': entry(1000), 'ants.rejoin.c.1': entry(30000) });
        same('of several entries the newest is offered, and the seat goes with it', [/room b /.test(rejoinView(env).note), (env.$('rejoin-go').click(), env.assigned.map((u) => env.param(u, 'seat')))], [true, ['2']]);
    }
    {   // a link to a room: the page is the lobby with a card over it; the way back is offered behind the card
        const env = runLobby('?room=k7m2xq', { 'ants.rejoin.r-1.1': entry(1000) });
        check('the lobby page offers the way back to a running match also when it was opened by a link (the strip is above the page, the name card over it)', rejoinView(env).hidden === false && !env.$('name-step').hidden);
    }
    for (const [label, search] of [['a room of the test mode (?room= with its create block)', '?room=k7m2xq&roommap=tiny&roomseats=2'], ['a room that starts at once (?play=here)', '?room=k7m2xq&roommap=tiny&roomseats=2&play=here'], ['a match to host (?map=)', '?map=tiny&players=2&play=here'], ['a match to host, after its card', '?map=tiny&players=2']]) {
        const env = runLobby(search, { 'ants.rejoin.k7m2xq.1': entry(1000) });
        check(label + ': the page is about that, not about the match of the key: no strip', rejoinView(env).hidden === true && env.$('name-step').hidden === (search.indexOf('play=here') !== -1));
        if (!env.$('name-step').hidden) env.$('name-step-go').click();
        check('... not even after the name card', rejoinView(env).hidden === true && env.roomStarted());
        (env.win.listeners.pageshow || []).forEach((fn) => fn({ persisted: true }));
        (env.win.listeners.storage || []).forEach((fn) => fn({}));
        check('... nor when another tab writes to the storage (the room of the test mode is on the page)', rejoinView(env).hidden === true && env.$('lobby').hidden === true);
    }
    {   // another tab that changes the storage: the match may have ended meanwhile; a page that comes back from the browser's memory (Back) is loaded again
        const env = runLobby('', { 'ants.rejoin.r-1.1': entry(1000) });
        same('the page listens for its return, for the storage, and for its visibility (the client\'s wake and the line of numbers\' own)', [(env.win.listeners.pageshow || []).length, (env.win.listeners.storage || []).length, (env.doc.listeners.visibilitychange || []).length], [1, 1, 2]);
        env.win.listeners.pageshow.forEach((fn) => fn({ persisted: false }));
        check('a page that is loaded anew is not loaded again by pageshow', env.reloads === 0 && rejoinView(env).hidden === false);
        env.win.listeners.pageshow.forEach((fn) => fn({ persisted: true }));
        check('a page that comes back from the browser\'s memory (Back) is loaded again: it looks again by itself', env.reloads === 1);
        delete env.storage.data['ants.rejoin.r-1.1'];
        env.win.listeners.storage.forEach((fn) => fn({}));
        check('the match ended in another tab: the strip is gone', rejoinView(env).hidden === true);
        env.storage.data['ants.rejoin.r-2.0'] = entry(500);
        env.win.listeners.storage.forEach((fn) => fn({}));
        same('a new match in another tab: the strip is there, for that match', [rejoinView(env).hidden, /room r-2 /.test(rejoinView(env).note)], [false, true]);
        delete env.storage.data['ants.rejoin.r-2.0'];
        env.win.listeners.storage.forEach((fn) => fn({}));
        check('... and gone again when the key is let go of', rejoinView(env).hidden === true);
        env.$('rejoin-go').click();
        check('a press that comes after the strip is gone goes nowhere', env.assigned.length === 0);
    }
    {
        let env = null;
        try { env = runLobby('', THROWS); } catch (e) { check('a browser that refuses its storage still runs the page, with no strip', false, e.message); }
        if (env) check('a storage that throws offers nothing, and the lobby is there', rejoinView(env).hidden === true && !env.$('lobby').hidden);
    }
    {
        const env = runLobby('', { 'ants.rejoin.r-1.1': entry(1000) });
        const before = JSON.stringify(env.storage.data);
        env.$('rejoin-go').click();
        check('the page writes nothing to the storage for it (the game page owns the entries)', JSON.stringify(env.storage.data) === before);
    }
}

// ---- 5.7 the test room of the old addresses (?map=, ?room= with a create block, ?play=here): the name card first, then the room's panel; the typed name goes to the first seat of the page
// An address that asks for a match is the page of somebody who was sent a link: they are asked for a name first, every time, and nothing starts before; the test mode ?play=here asks nobody
const openRoom = (search, stored, name) => {
    const env = runLobby(search, stored || {});
    if (name !== undefined) env.type('name-step-input', name);
    env.$('name-step-go').click();
    return env;
};
{
    const env = runLobby('?map=small&players=4', {});
    same('an address that hosts a match asks first: a card titled "Host a match" with a Host button, a hint that says what the name is for, the notice, and a way out that goes back to the front page', [env.$('name-step').hidden, env.$('name-step-title').textContent, env.$('name-step-go').textContent, env.$('name-step-hint').textContent, env.$('who-notice').hidden, env.$('name-step-own').textContent],
         [false, 'Host a match', 'Host', 'The name your ants play under in the match that you host from this page. Leave it empty for a random name. This browser remembers it.', false, '← Back to the front page']);
    check('... nothing starts before the button: no room panel, no frame, no socket, the address is not rewritten, and the page is in its room mode (the lobby is not shown)', !env.roomStarted() && env.frames().length === 0 && env.sockets.length === 0 && env.replaced.length === 0 && env.opened.length === 0 && env.body.classList.contains('in-room'));
    env.$('name-step-own').click();
    same('... the way out is a plain address of the front page (no query: nothing of the match goes with it)', env.assigned, ['https://play.test/']);
    const room = runLobby('?room=k7m2xq&roommap=small&roomseats=2', { 'ants.name': 'Maya' });
    same('an address with a room and its create block asks first too: "Join the match k7m 2xq", a Join button, the remembered name', [room.$('name-step-title').textContent, room.$('name-step-go').textContent, room.$('name-step-input').value, room.$('name-step-hint').textContent, room.$('who-notice').hidden],
         ['Join the match k7m 2xq', 'Join', 'Maya', 'The name your ants play under in this match: the seat that you play from this page takes it. Leave it empty for a random name. This browser remembers it.', false]);
    const solo = runLobby('?map=small&players=1&fill=hard', { 'ants.name': 'Maya' });
    same('an address that plays a game on this computer asks first: "Play a game on this computer", a Play button, its own hint, and no notice about recorded matches (the server records the matches of its rooms, not this game)', [solo.$('name-step-title').textContent, solo.$('name-step-go').textContent, solo.$('name-step-hint').textContent, solo.$('who-notice').hidden],
         ['Play a game on this computer', 'Play', 'The name your ants play under in the game on this computer. Leave it empty for a random name. This browser remembers it.', true]);
    check('... nothing starts before the button', solo.assigned.length === 0 && solo.sockets.length === 0);
    solo.$('name-step-go').click();
    const u = solo.assigned[0] || '';
    same('... then the game on this computer on that map with the Hard bots and the name', [solo.assigned.length, solo.param(u, 'map'), solo.param(u, 'bots'), solo.param(u, 'name'), solo.param(u, 'aspect')], [1, 'small', 'hard', 'Maya', '16:9']);
    check('... in no room: no door, no code, no create block, no seat (a game on this computer makes none), nothing written but the name, and the notice is back on the card (nothing stays half-changed if the browser shows this page again)', ['join', 'room', 'roommap', 'roomseats', 'roomteams', 'roomleaderstart', 'platform', 'seat', 'start'].every((k) => solo.param(u, k) === null) && solo.replaced.length === 0 && Object.keys(solo.storage.data).join() === 'ants.name' && !solo.$('who-notice').hidden && solo.$('name-step').hidden);
    check('the test mode ?play=here asks nobody: ?map=...&players=1 plays at once', runLobby('?map=treasure&players=1&play=here', {}).assigned.length === 1);
}
{   // the typed name goes to the first seat that the page starts; the others have random names
    const env = runLobby('?map=small&players=4', {});
    env.type('name-step-input', '  Alice  ');
    env.$('name-step-go').click();
    check('the card with a good name makes the room: the panel of the test room takes the place of the lobby, in the page\'s room mode', env.roomStarted() && env.$('name-step-input').value === '  Alice  ' && env.$('lobby').hidden && env.$('name-step').hidden && env.body.classList.contains('in-room'));
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
          [f2.src, f1.src, env.opened[0].url, anyLink, env.copied[0]].every((url) => blockText(url) === '&roommap=small&roomseats=4'), JSON.stringify([f2.src, f1.src, env.opened[0].url, anyLink, env.copied[0]]));
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
    check('all seats on this page: seat 0 plays as Alice, the others have random names, all different', env.param(urls[0], 'name') === 'Alice' && urls.slice(1).every((url) => randomName(env.param(url, 'name'))) && new Set(urls.slice(1).map((url) => env.param(url, 'name'))).size === 3);
}
{   // the random fallback
    const env = openRoom('?map=treasure&players=4', {});
    env.$('all-here').click();
    const got = [0, 1, 2, 3].map((s) => env.param(env.frameOf(s).src, 'name'));
    check('an empty field: every seat has a random name, as before', got.every(randomName) && new Set(got).size === 4, got.join(','));
    check('... and an empty field is remembered as none', env.storage.data['ants.name'] === '');
    for (const refused of ['Bot (x)', 'Zoë', 'z'.repeat(40)]) {
        const e = openRoom('?map=treasure&players=4', { 'ants.name': refused });
        check('a remembered name that the rules refuse (' + JSON.stringify(refused).slice(0, 14) + ') is not in the card\'s field and no seat plays under it', e.$('name-step-input').value === '' && (e.$('all-here').click(), [0, 1, 2, 3].every((s) => randomName(e.param(e.frameOf(s).src, 'name')))));
    }
}

// ---- bad names start nothing
for (const bad of ['Bot (Medium)', ' bOt(x', 'Zoë', '名前', 'x'.repeat(33), 'a\u0001b']) {
    const step = runLobby('?map=small&players=4', {});
    step.type('name-step-input', bad);
    step.$('name-step-go').click();
    check('the card of an address that hosts a match with a bad name (' + JSON.stringify(bad).slice(0, 16) + ') makes no room and says why', !step.roomStarted() && step.$('name-step-msg').textContent.length > 8 && step.replaced.length === 0 && !step.$('name-step').hidden && step.frames().length === 0 && step.$('name-step-input').getAttribute('aria-invalid') === 'true');
    step.type('name-step-input', 'Zed');
    step.$('name-step-go').click();
    check('... and the field says it is no longer wrong when the name is good (aria-invalid goes back to false and the message is empty)', step.roomStarted() && step.$('name-step-input').getAttribute('aria-invalid') === 'false' && step.$('name-step-msg').textContent === '');
    const link = runLobby('?room=k7m2xq', {});
    link.type('name-step-input', bad);
    link.$('name-step-go').click();
    check('... the card of a link to a room: no socket, and the same message', link.sockets.length === 0 && link.$('name-step-msg').textContent.length > 8 && !link.$('name-step').hidden);
    const typed = runLobby('', {});
    typed.arrive();
    typed.$('havecode').click();
    typed.type('joincode', 'ABC-1');
    typed.$('joingo').click();
    typed.type('name-step-input', bad);
    typed.$('name-step-go').click();
    check('... the card of a typed code: nothing is looked at, and the same message', typed.sockets.length === 1 && typed.$('name-step-msg').textContent.length > 8 && !typed.$('name-step').hidden);
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
check('the front page\'s only window.open is the seat\'s explicit one (no window is opened by default)', (lobbyText.match(/window\.open\(/g) || []).length === 1);

// ---- the addresses of the test room
{
    const env = openRoom('?room=k7m2xq&roommap=small&roomseats=2', { 'ants.name': 'Maya' });
    check('an address with a room and its create block: the card (with the remembered name) then the room\'s panel, with its name card gone and the notice back', env.roomStarted() && env.$('name-step').hidden);
    env.type('name-step-input', 'x');
    const bad = runLobby('?room=k7m2xq&roommap=small&roomseats=2', {});
    bad.type('name-step-input', 'Bot (x)');
    bad.$('name-step-input').key('Enter');
    check('... a bad name does not open it and says why; Enter with a good name does (the address is rewritten to the room, the card goes away, the browser remembers the name)', !bad.roomStarted() && bad.$('name-step-msg').textContent.length > 8 && (bad.type('name-step-input', 'Zed'), bad.$('name-step-input').key('Enter'), bad.roomStarted() && bad.replaced.length === 1 && bad.$('name-step').hidden && bad.storage.data['ants.name'] === 'Zed'));
    bad.rowButton(1, 'Play here').click();
    check('... and the seat that this person plays takes the name', bad.param(bad.frameOf(1).src, 'name') === 'Zed');
}
{
    const env = runLobby('?map=treasure&players=2', {});
    env.type('name-step-input', 'Alice');
    env.$('name-step-go').click();
    check('a match to host on a map: the room is made on that map: a new code of six characters and its create block (Treasure, two seats) are in the address of the page', env.roomStarted() && env.replaced.length === 1 && /^\/\?room=[a-z2-9]{6}&roommap=treasure&roomseats=2$/.test(String(env.replaced[0][2])), String(env.replaced[0] && env.replaced[0][2]));
    same('... the panel says it: the code in two groups of three, the map and two seats', [env.$('room-code').textContent === grouped(env.param(new URL(String(env.replaced[0][2]), 'https://play.test').href, 'room')), env.$('room-map').textContent, env.rows().length], [true, 'Treasure map, 2 players', 2]);
}
{
    const env = runLobby('?room=k7m2xq&roommap=small&roomseats=2&play=here', { 'ants.name': 'Maya' });
    check('the test mode ?play=here asks nobody: every seat starts at once (the two seats of the create block)', env.$('name-step').hidden && env.roomStarted() && env.frames().length === 2);
    const names2 = [0, 1].map((s) => env.param(env.frameOf(s).src, 'name'));
    check('... the first seat takes the remembered name, the other seat is random', names2[0] === 'Maya' && randomName(names2[1]), names2.join(','));
    const env2 = runLobby('?map=small&play=here', {});
    check('?map= with ?play=here asks nobody either', env2.roomStarted() && env2.frames().length === 4 && env2.$('name-step').hidden);
}
{   // a room that this page did not make (its address has no create block, and a code that is only played here): four seats, and nothing of a block anywhere: the page sends none, and a code that no room has is the game server's to refuse
    const env = runLobby('?room=k7m2xq&play=here', { 'ants.name': 'Zed' });
    check('a room with no create block that starts at once has four seats (the page does not know the room: nothing is trusted but the code)', env.roomStarted() && env.frames().length === 4 && env.$('name-step').hidden);
    same('... the panel shows the code as a screen shows it (two groups of three), four seats and says that the room was made some other way', [env.$('room-code').textContent, env.$('room-map').textContent, env.rows().length], ['k7m 2xq', 'a room made some other way, 4 players', 4]);
    same('... the address of the page is the room alone: no create block to carry (and the plain code)', env.replaced.map((a) => a[2]), ['/?room=k7m2xq']);
    env.rowButton(0, 'Copy link').click();
    env.rowButton(2, 'Open a window').click();
    env.$('play-tab').click();
    const made = [env.frameOf(1).src, env.opened[0].url, env.copied[0], env.$('any-link').value, env.assigned[0]];
    check('... and no game of it carries a create block: the frame, the window, the link of a seat, the link for anybody and "Play in this tab" are the plain room (the code is the plain one in all of them)', made.every((url) => noBlockOf(env, url) && env.param(url, 'room') === 'k7m2xq'), JSON.stringify(made));
    check('... and the seat that this person plays takes the name', env.param(env.frameOf(0).src, 'name') === 'Zed' && env.param(env.assigned[0], 'name') === 'Zed');
}
{   // ... a browser that refuses its storage still plays in the test room
    const env = runLobby('?map=small&play=here', THROWS);
    check('a browser that refuses its storage still plays in the test room (nothing is remembered, nothing breaks)', env.roomStarted() && env.frames().length === 4);
}
// ... an address that names a map and no players hosts 4 (the card plays on this computer only with &players=1)
{
    const room = openRoom('?map=small', {});
    const hostedAt = String(room.replaced[0] && room.replaced[0][2]);
    check('an address that names a map and no players hosts 4: the room of 4 on that map, a new code of six characters shown in two groups of three, and the map and four seats as its create block', room.roomStarted() && /^\/\?room=[a-z2-9]{6}&roommap=small&roomseats=4$/.test(hostedAt) && /^[a-z2-9]{3} [a-z2-9]{3}$/.test(room.$('room-code').textContent) && room.$('room-map').textContent === 'Small map, 4 players' && room.assigned.length === 0, hostedAt);
    check('... the lobby gives way to the room, in its room mode', room.$('lobby').hidden && !room.$('room-panel').hidden && room.body.classList.contains('in-room'));
    same('... nothing is remembered by it but the name', Object.keys(room.storage.data).sort(), ['ants.name']);
}
// ... the room's own seat in this tab: the address that Join makes, with the bots of the leader's START (the room panel of an old address)
{
    const env = openRoom('?map=treasure&players=3&fill=medium', {}, 'Ann');
    check('?map=treasure&players=3&fill=medium makes the room panel, and the play-here note of the frames is not shown yet', env.roomStarted() && env.$('frames-note').hidden === true);
    env.$('play-tab').click();
    const u = env.assigned[0] || '';
    check('"Play in this tab": this tab goes to the game page with the room (the plain code, not the two groups that the panel shows), the bots of the leader\'s START and the name (no seat, no embed, no new window)',
          env.assigned.length === 1 && env.opened.length === 0 && env.param(u, 'join') === '/ws' && env.param(u, 'room') === env.$('room-code').textContent.replace(' ', '') && /^[a-z2-9]{6}$/.test(env.param(u, 'room')) && env.param(u, 'fill') === 'medium' && env.param(u, 'name') === 'Ann' && env.param(u, 'seat') === null && env.param(u, 'embed') === null, u);
    same('... the whole address: the door, the code, the create block of the room (Treasure, three seats), the bots, the name', u, 'https://play.test/?join=/ws&room=' + env.param(u, 'room') + '&roommap=treasure&roomseats=3&fill=medium&name=Ann');
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
        ['?room=k7m2xq&roommap=small&roomseats=2', '&roommap=small&roomseats=2', ['--room-map', 'small', '--room-seats', '2'], 'Small map, 2 players', 2, '/?room=k7m2xq&roommap=small&roomseats=2'],
        ['?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=0%2B1', '&roommap=treasure&roomseats=4&roomteams=0%2B1', ['--room-map', 'treasure', '--room-seats', '4', '--room-teams', '0+1'], 'Treasure map, 4 players', 4, '/?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=0%2B1'],
        ['?room=k7m2xq&roommap=islands&roomseats=3&roomleaderstart=1', '&roommap=islands&roomseats=3&roomleaderstart=1', ['--room-map', 'islands', '--room-seats', '3', '--room-leader-start'], 'Islands map, 3 players', 3, '/?room=k7m2xq&roommap=islands&roomseats=3&roomleaderstart=1'],
        ['?room=k7m2xq&roommap=medium&roomseats=3&roomteams=1%2B2&roomleaderstart=1', '&roommap=medium&roomseats=3&roomteams=1%2B2&roomleaderstart=1', ['--room-map', 'medium', '--room-seats', '3', '--room-teams', '1+2', '--room-leader-start'], 'Medium map, 3 players', 3, '/?room=k7m2xq&roommap=medium&roomseats=3&roomteams=1%2B2&roomleaderstart=1'],
        // read as the game page reads them: the map in any case, a "+" that came as a blank, a part that is no good dropped (the server's own: no map, four seats, free for all, no flag), a block of teams that its seats cannot make still carried
        ['?room=k7m2xq&roommap=GAUNTLET&roomseats=2&roomteams=0+1', '&roommap=gauntlet&roomseats=2&roomteams=0%2B1', ['--room-map', 'gauntlet', '--room-seats', '2', '--room-teams', '0+1'], 'Gauntlet map, 2 players', 2, '/?room=k7m2xq&roommap=gauntlet&roomseats=2&roomteams=0%2B1'],
        ['?room=k7m2xq&roomseats=3', '&roomseats=3', ['--room-seats', '3'], 'the server\'s own map, 3 players', 3, '/?room=k7m2xq&roomseats=3'],
        ['?room=k7m2xq&roommap=tiny', '&roommap=tiny&roomseats=4', ['--room-map', 'tiny', '--room-seats', '4'], 'Tiny map, 4 players', 4, '/?room=k7m2xq&roommap=tiny&roomseats=4'],
        ['?room=k7m2xq&roomleaderstart=1', '&roomseats=4&roomleaderstart=1', ['--room-seats', '4', '--room-leader-start'], 'the server\'s own map, 4 players', 4, '/?room=k7m2xq&roomseats=4&roomleaderstart=1'],
        ['?room=k7m2xq&roomteams=0%2B1', '&roomseats=4&roomteams=0%2B1', ['--room-seats', '4', '--room-teams', '0+1'], 'the server\'s own map, 4 players', 4, '/?room=k7m2xq&roomseats=4&roomteams=0%2B1'],
        ['?room=k7m2xq&roommap=nowhere&roomseats=2&roomleaderstart=yes', '&roomseats=2', ['--room-seats', '2'], 'the server\'s own map, 2 players', 2, '/?room=k7m2xq&roomseats=2'],
        ['?room=k7m2xq&roommap=small&roomseats=2&aspect=4:3', '&roommap=small&roomseats=2', ['--room-map', 'small', '--room-seats', '2'], 'Small map, 2 players', 2, '/?room=k7m2xq&roommap=small&roomseats=2&aspect=4:3'],
    ];
    for (const [search, text, args, mapLine, seats, bar] of ROOMS) {
        const env = openRoom(search, {}, 'Ann');
        const label = search.replace('?room=k7m2xq', '?room=<code>');
        check('the panel of ' + label + ': the code in two groups of three, "' + mapLine + '", one row for each of its ' + seats + ' seats', env.roomStarted() && env.$('room-code').textContent === 'k7m 2xq' && env.$('room-map').textContent === mapLine && env.rows().length === seats, env.$('room-map').textContent);
        same('... the address of the page is the room with its create block (a reload, a bookmark or a copied address come back to it)', env.replaced.map((a) => a[2]), [bar]);
        const links = panelLinks(env);
        const wrong = kinds.filter((k) => !(blockText(links[k]) === text && env.param(links[k], 'room') === 'k7m2xq' && JSON.stringify(blockArgs(links[k])) === JSON.stringify(args) && env.param(links[k], 'teams') === null && teamsArg(links[k]) === null));
        check('... every address that it makes (the frame, the window, the link of a seat, the link for anybody, its Copy, "Play in this tab") has the block right after the plain code, and the game page turns it into ' + JSON.stringify(args.join(' ')) + ': ' + wrong.join(','), wrong.length === 0, JSON.stringify(links));
        const again = openRoom('?' + bar.split('?')[1], {}, 'Ann');
        check('... and the address that the page puts in its own bar brings back the same room: the same link for anybody, the same panel', again.$('any-link').value === env.$('any-link').value && again.$('room-map').textContent === mapLine && again.rows().length === seats && again.replaced.map((a) => a[2]).join() === bar, again.$('any-link').value);
    }
    // a link to a room with no create block is no room of the test mode: it is the lobby's link (the card of a link, then the lobby), however its other parts look
    for (const search of ['?room=k7m2xq', '?room=k7m2xq&roommap=nowhere&roomseats=9&roomteams=7%2B7&roomleaderstart=yes', '?room=k7m2xq&roommap=&roomseats=&roomteams=&roomleaderstart=', '?room=k7m2xq&platform=linux&roomleaderstart=0']) {
        const env = runLobby(search, {});
        check('the address ' + search.replace('?room=k7m2xq', '?room=<code>') + ' has no create block that says anything: it is a link to a lobby (the card of a link, "Join the room k7m 2xq"), not the panel of the test room', !env.roomStarted() && !env.$('name-step').hidden && env.$('name-step-title').textContent === 'Join the room k7m 2xq' && !env.body.classList.contains('in-room'));
    }
    {   // the first room, whole: the order of the parameters of every kind of address
        const env = openRoom(ROOMS[0][0], {}, 'Ann');
        const l = panelLinks(env);
        same('every address of a room that the page made, whole (Small, two seats): the door, the plain code, the block, then what the address is for (its seat, the name, the picture)',
             { frame: l.frame, seat: l.seat, anybody: l.anybody, copiedAnybody: l.copiedAnybody, tab: l.tab },
             { frame: 'https://play.test/?join=/ws&room=k7m2xq&roommap=small&roomseats=2&seat=1&name=Ann&aspect=16:9&embed=1', seat: 'https://play.test/?join=/ws&room=k7m2xq&roommap=small&roomseats=2&seat=1', anybody: 'https://play.test/?join=/ws&room=k7m2xq&roommap=small&roomseats=2',
               copiedAnybody: 'https://play.test/?join=/ws&room=k7m2xq&roommap=small&roomseats=2', tab: 'https://play.test/?join=/ws&room=k7m2xq&roommap=small&roomseats=2&name=Ann' });
        check('... the window of a seat too (its own random name)', /^https:\/\/play\.test\/\?join=\/ws&room=k7m2xq&roommap=small&roomseats=2&seat=0&name=[^&]+&aspect=16:9$/.test(l.window), l.window);
        const withShape = openRoom('?room=k7m2xq&roommap=small&roomseats=2&aspect=4:3', {}, 'Ann');
        const s = panelLinks(withShape);
        check('an address with a picture: it is in every link, after the block (and after the plan and the teams, when there are some)', [s.frame, s.seat, s.anybody, s.copiedAnybody, s.tab, s.window].every((url) => blockText(url) === '&roommap=small&roomseats=2' && withShape.param(url, 'aspect') === '4:3' && /&aspect=4:3(&embed=1)?$/.test(url)), JSON.stringify(s));
    }
    // &teams= is the leader's START choice for a room whose block names no teams of its own (every room that was made some other way); the teams that a block names are the room's own and win over it, and the links then
    // carry &roomteams= and no &teams=. Where the block says how many seats the room has, a pair that those seats cannot make is free for all (the page narrows it, as it narrows the teams of an address that makes a room:
    // for four seats the pairs with Green); a block whose own seats cannot make its teams is the server's to judge at START (the match starts without teams and the room says "No teams: ..."): the page carries it
    // as it is, does not announce it, and does not replace it with &teams=. (An address with no block at all is no room of the test mode now: it is a link to a lobby, and has no teams to carry.)
    // [the address of the page, the block that every link carries, the teams parameter that every link carries (null: none), the fill parameter, the address of the page, the line of the panel under the title ('' when it is hidden)]
    const BLOCKED = '&roommap=treasure&roomseats=4';
    const AT_START = (teams) => 'Teams at START: ' + teams + '. Every link of this room carries the teams; only the leader\'s START uses them.';
    const TEAMS = [
        ['?room=k7m2xq&roommap=treasure&roomseats=4&teams=0%2B2', BLOCKED, '0+2', null, '/?room=k7m2xq&roommap=treasure&roomseats=4&teams=0%2B2', AT_START('Green + Blue against Red + Black')],
        ['?room=k7m2xq&roommap=treasure&roomseats=4&teams=0+2', BLOCKED, '0+2', null, '/?room=k7m2xq&roommap=treasure&roomseats=4&teams=0%2B2', AT_START('Green + Blue against Red + Black')],
        ['?room=k7m2xq&roommap=treasure&roomseats=4&teams=2%2B3', BLOCKED, null, null, '/?room=k7m2xq&roommap=treasure&roomseats=4', ''],
        ['?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=0%2B1&teams=0%2B2', BLOCKED + '&roomteams=0%2B1', null, null, '/?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=0%2B1',
         'Teams of this room: Green + Red against Blue + Black. They are part of the room (set when it was made), so the match starts with them every time: when the room fills up as well as at START.'],
        // with four seats a block's pair without Green is the same two teams as the pair of the other two (a hand-made address, or the game's --room-teams): the room plays them, so the panel announces them as the pair with Green
        ['?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=1%2B2&teams=0%2B1', BLOCKED + '&roomteams=1%2B2', null, null, '/?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=1%2B2',
         'Teams of this room: Green + Black against Red + Blue. They are part of the room (set when it was made), so the match starts with them every time: when the room fills up as well as at START.'],
        ['?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=1%2B3', BLOCKED + '&roomteams=1%2B3', null, null, '/?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=1%2B3', 'Teams of this room: Green + Blue against Red + Black.'],
        ['?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=2%2B3', BLOCKED + '&roomteams=2%2B3', null, null, '/?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=2%2B3', 'Teams of this room: Green + Red against Blue + Black.'],
        ['?room=k7m2xq&roommap=small&roomseats=3&teams=1%2B2', '&roommap=small&roomseats=3', '1+2', null, '/?room=k7m2xq&roommap=small&roomseats=3&teams=1%2B2', AT_START('Red + Blue against Green')],
        ['?room=k7m2xq&roommap=small&roomseats=3&teams=2%2B3', '&roommap=small&roomseats=3', null, null, '/?room=k7m2xq&roommap=small&roomseats=3', ''],
        ['?room=k7m2xq&roommap=small&roomseats=2&teams=0%2B1', '&roommap=small&roomseats=2', null, null, '/?room=k7m2xq&roommap=small&roomseats=2', ''],
        ['?room=k7m2xq&roommap=small&roomseats=3&roomteams=2%2B3&teams=0%2B1', '&roommap=small&roomseats=3&roomteams=2%2B3', null, null, '/?room=k7m2xq&roommap=small&roomseats=3&roomteams=2%2B3', ''],
        ['?room=k7m2xq&roommap=treasure&roomseats=4&teams=1%2B1', BLOCKED, null, null, '/?room=k7m2xq&roommap=treasure&roomseats=4', ''],
        ['?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=3%2B3&teams=0%2B3', BLOCKED, '0+3', null, '/?room=k7m2xq&roommap=treasure&roomseats=4&teams=0%2B3', AT_START('Green + Black against Red + Blue')],
        ['?room=k7m2xq&roomleaderstart=1&teams=0%2B1', '&roomseats=4&roomleaderstart=1', '0+1', null, '/?room=k7m2xq&roomseats=4&roomleaderstart=1&teams=0%2B1', AT_START('Green + Red against Blue + Black')],
        ['?room=k7m2xq&roomseats=4&teams=2%2B3', '&roomseats=4', null, null, '/?room=k7m2xq&roomseats=4', ''],
        ['?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=0%2B1&fill=hard', BLOCKED + '&roomteams=0%2B1', null, 'hard', '/?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=0%2B1&fill=hard', 'Teams of this room: Green + Red against Blue + Black.'],
        ['?room=k7m2xq&roommap=treasure&roomseats=4&fill=none,easy,none,hard&teams=0%2B2', BLOCKED, '0+2', 'none,easy,none,hard', '/?room=k7m2xq&roommap=treasure&roomseats=4&fill=none,easy,none,hard&teams=0%2B2', 'Empty seats at START: Red Easy, Black Hard.'],
    ];
    for (const [search, text, teams, fill, bar, hint] of TEAMS) {
        const env = openRoom(search, {}, 'Ann');
        const label = search.replace('?room=k7m2xq', '?room=<code>');
        same('the panel of ' + label + ': the address of the page has the block, then the plan and the teams that START uses (none when the block has teams of its own)', env.replaced.map((a) => a[2]), [bar]);
        check('... the line under the title says ' + (hint ? JSON.stringify(hint.slice(0, 60)) : 'nothing (it is hidden)'), hint === '' ? env.$('fill-hint').hidden === true : env.$('fill-hint').hidden === false && env.$('fill-hint').textContent.indexOf(hint) !== -1, env.$('fill-hint').textContent);
        const links = panelLinks(env);
        const wrong = kinds.filter((k) => !(blockText(links[k]) === text && env.param(links[k], 'teams') === teams && teamsArg(links[k]) === teams && env.param(links[k], 'fill') === fill && fillArg(links[k]) === fill &&
                                            !(env.param(links[k], 'roomteams') !== null && env.param(links[k], 'teams') !== null)));
        check('... every address that it makes carries the block, the teams parameter ' + JSON.stringify(teams) + ' and the plan ' + JSON.stringify(fill) + ' (the game\'s --teams and --fill-bots) the same way, and never both roomteams and teams: ' + wrong.join(','), wrong.length === 0, JSON.stringify(links));
        check('... and the block comes right after the plain code, before everything else (the plan, the teams, the seat, the name, the picture)', kinds.every((k) => /^https:\/\/play\.test\/\?join=\/ws&room=k7m2xq(&room(map|seats|teams|leaderstart)=[^&]*)*(&(fill|teams|seat|name|aspect|embed)=|$)/.test(links[k])), JSON.stringify(links));
    }
    // ?map=<key>&players=<n>: a room of a NEW code (six characters) and a create block of its own: the map, the seats and the teams, shown and carried like those of a room that an address names; &players=1 stays a game on this computer
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
        check('the panel of ' + search + ': a new code of six characters in two groups of three, "' + mapLine + '", ' + seats + ' seats', env.roomStarted() && CODE.test(code) && env.$('room-code').textContent === grouped(code) && env.$('room-map').textContent === mapLine && env.rows().length === seats, env.$('room-map').textContent);
        same('... the address of the page: the code, the block of the room that was made, the plan', env.replaced.map((a) => a[2]), ['/?room=' + code + text + rest]);
        const links = panelLinks(env);
        const wrong = kinds.filter((k) => !(blockText(links[k]) === text && env.param(links[k], 'room') === code && env.param(links[k], 'teams') === null && teamsArg(links[k]) === null && env.param(links[k], 'fill') === fill));
        check('... every address that it makes carries that block (the teams of a room that an address makes are its block\'s: no teams parameter) and the plan ' + JSON.stringify(fill) + ': ' + wrong.join(','), wrong.length === 0, JSON.stringify(links));
    }
    {   // every room is a new one: two pages that make a room from the same address have two codes (and the code is one that the page made: six characters of its alphabet)
        const codes = new Set();
        for (let i = 0; i < 20; i++) { const e = openRoom('?map=small&players=2', {}, ''); codes.add(e.param(e.$('any-link').value, 'room')); }
        check('twenty rooms made from one address have twenty codes, each of six characters of the alphabet without look-alikes (no i, l, o, 0 or 1)', codes.size === 20 && [...codes].every((c) => /^[abcdefghjkmnpqrstuvwxyz23456789]{6}$/.test(c)), [...codes].slice(0, 3).join(','));
    }
}


// ---- 5.9 the line of numbers in the footer (the block STATS has its own check, web_stats_check.js; here is the page as a whole: what it asks, what it shows, when it looks)
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
{
    const env = runLobby('', {});
    await settle();
    same('no network: the line is not there and says nothing (no text of an error), the page itself works', [statsView(env).hidden, statsView(env).live, statsView(env).played, env.fetches.map((f) => f.url)], [true, '', '', ['/stats', '/busy']]);
    check('... and the lobby is made as ever (one socket, the lobby shown)', env.sockets.length === 1 && !env.$('lobby').hidden);
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
    const wake = () => (env.doc.listeners.visibilitychange || []).forEach((fn) => fn());
    check('the page listens for its visibility (the client\'s wake and the line of numbers\' own look)', (env.doc.listeners.visibilitychange || []).length === 2);
    const before = env.fetches.length;
    env.doc.hidden = true;
    wake();
    await settle();
    check('hidden: it asks nothing', env.fetches.length === before);
    now = { now: { matches: 0, players: 0 }, online: { day: 0, total: 1000 }, local: { day: 0, total: 234 }, since: '2026-10-04' };
    env.doc.hidden = false;
    wake();
    await settle();
    same('shown again: it looks at once and shows the new numbers (a grey dot, no match, 1,234 games, 0 today)', [env.fetches.length - before, statsView(env).dot, statsView(env).live, statsView(env).played], [1, 'live', '0 matches being played · 0 players online', '1,234 games played (0 today)']);
    now = undefined;                                                                  // the site stops answering: the next look finds neither /stats nor /busy
    env.doc.hidden = true;
    wake();
    env.doc.hidden = false;
    wake();
    await settle();
    same('... and when the site stops answering the line goes away again (it was shown before)', [statsView(env).hidden, env.fetches.length - before], [true, 3]);
}
{
    const env = runLobby('', {}, { noFetch: true });
    await settle();
    check('a browser with no fetch has no line and the page works', statsView(env).hidden === true && env.fetches.length === 0 && env.sockets.length === 1 && !env.$('lobby').hidden);
}
{   // the test room and the lobby have the line too (it is the footer's)
    const room = runLobby('?room=k7m2xq&roommap=small&roomseats=2&play=here', {}, { fetch: answers({ '/stats': GOOD_STATS }) });
    await settle();
    check('a room of the test mode has the line too', room.roomStarted() && statsView(room).hidden === false);
    const lobby = runLobby('', {}, { fetch: answers({ '/stats': GOOD_STATS }) });
    await settle();
    check('... and so has the lobby', !lobby.$('lobby').hidden && statsView(lobby).hidden === false);
}

})().then(() => {
    console.log('web name check: ' + checks + ' checks, ' + failures + ' failures');
    process.exit(failures === 0 ? 0 : 1);
}, (e) => {
    console.log('FAIL ' + e.message + '\n' + e.stack);
    process.exit(1);
});
