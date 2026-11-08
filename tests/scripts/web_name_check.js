// Runs the pages' OWN code for the player's name (nobody types a name on the web: a person who is logged in plays under the login's name, anybody else under a guest name that the browser keeps; a link
// from somebody else still asks first, with a card that shows the name):
//   1. the rules of a name (the block NAME_BEGIN .. NAME_END, the same text in web/lobby.html and web/shell.html) on a table of names: the rules of the desktop start menu (printable ASCII, trimmed, at most
//      32, nothing that starts with "Bot (" with blanks and case ignored); the login's name is made to fit them by front/player_name.js (tests/scripts/web_player_name_check.js);
//   2. the name step (runNameStep): the card shows the person's name and cannot be typed in, the words under it for a login, a switch and a guest, the button or Enter goes on once, only when the name is there;
//   3. web/shell.html: which addresses ask for a name (a shared link: ANTS_PAGE.asksForName), the address without its name (withoutName), and the gate that holds the game back until the card is done
//      (makeNameGate): the game is not started, so it does not connect, before the button; the name that the card showed goes into the game's arguments as --name;
//   4. web/lobby.html (the front page, the lobby: a room on the game server, protocol 16) read from the file: its scripts, its ids, the name card's wiring, a name only ever as text (textContent, a value, an
//      attribute, encodeURIComponent in an address), no field for a name and no pencil, and the links that are made for somebody else (no name in them);
//   5. web/lobby.html as a whole, run with the fake browser of tests/scripts/web_page_harness.js (a small fake of the DOM, the page's real scripts and a scripted server and hub): a plain visit is Home and makes
//      no room; My room makes one under the login's name or the guest name (Hello's name, room, key and the session storage that keeps them), a name that this browser remembered from earlier typing is ignored,
//      a login that is made to fit, a hub that does not answer (the limit), a log out without a reload, a seat that is held keeps its name, a link of somebody else's room (?room=) and a typed code (Join by code,
//      in the Games list) ask first with the card and send nothing before it, START hands the seat to the game page (ants.rejoin.<room>.<seat>; no leave()), the Rejoin strip, and the line of numbers in the
//      footer. What a fake DOM cannot say (the look, the phone widths, the pointer, the real focus and the real WebSocket) is the job of the browser checks (web_home_check.py and the web_lobby_*_check.js
//      against the server), and the block REJOIN has its own check, web_rejoin_block_check.js.
// tests/scripts/test_web_name.py runs this with node (the quick tier). usage: node web_name_check.js web/shell.html web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
// the pages load web/shapes.js (the one table of the shapes) before their own code: it is the global AntsShapes of the code that is run here
globalThis.AntsShapes = require(require('path').resolve(__dirname, '..', '..', 'web', 'shapes.js'));

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
const LOBBY_NOTICE_HTML = 'Online matches are recorded. Recordings are kept for 30 days. Results and names are kept for good and are public.';       // (the front page's: its footer and the card of a link)
const NOTICE_HTML = 'Online matches are recorded. Anybody can watch them, live or later. Recordings are kept for 30 days. Results and names are kept for good and are public.';       // (the game page's line under its name field; the lobby's own sentence is LOBBY_NOTICE_HTML)

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The rules of a name: the same text in both pages, on a table
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const nameBlockShell = between(shellText, 'NAME_BEGIN', 'NAME_END', shellPath);
const nameBlockLobby = between(lobbyText, 'NAME_BEGIN', 'NAME_END', lobbyPath);
check('the rules and the step of a name are the same text in both pages', nameBlockShell === nameBlockLobby);
const names = new Function(nameBlockLobby + '\nreturn { nameCheck: nameCheck, runNameStep: runNameStep, MAX: NAME_MAX, HINT: NAME_HINT };')();
const nameCheck = names.nameCheck;
check('the name step keeps nothing in the browser: no key of a remembered name in the block, which takes no store', !/NAME_KEY|ants\.name|localStorage|recall|remember/.test(nameBlockLobby) && names.runNameStep.length === 3);
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
// 2. The name step: the card shows the name that the person plays under and cannot be typed in
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function fakeUi() {
    return {
        shown: 0, hidden: 0, hints: [],
        input: { value: '', listeners: {}, addEventListener(t, fn) { this.listeners[t] = fn; } },
        button: { listeners: {}, addEventListener(t, fn) { this.listeners[t] = fn; } },
        message: { textContent: '' },
        said(text) { this.hints.push(text); },
        show() { this.shown++; },
        hide() { this.hidden++; },
    };
}
// The person of a page as the card sees it (front/player_name.js): ready(fn) waits until answer(name, kind, swap) is called (a question on its way), or answers at once when `now` is given
function fakePerson(now) {
    const waiting = [];
    return { asked: 0, ready(fn) { this.asked++; if (now) fn(now[0], now[1], now[2]); else waiting.push(fn); }, answer(name, kind, swap) { waiting.splice(0).forEach((fn) => fn(name, kind, swap)); } };
}
const HINT_LOGIN = 'This is your Discord name. The other players see it.';
const HINT_SWAP = ' The chat menu switches it between your display name and your username.';
const HINT_GUEST = 'You play as a guest. Log in with Discord to play under your own name.';
same('the words under the name: a login, the switch that the hub says exists, a guest', [names.HINT.login, names.HINT.swap, names.HINT.guest], [HINT_LOGIN, HINT_SWAP, HINT_GUEST]);
check('the words are plain text of the page\'s characters, short, and none of them is a name that nameCheck takes (they are hints, not names)', Object.values(names.HINT).every((w) => /^[\x20-\x7e]+$/.test(w) && w.length < 90 && !/[<>&]/.test(w)));
{
    const ui = fakeUi();
    const person = fakePerson();
    const done = [];
    names.runNameStep(ui, person, function (n) { done.push(n); });
    check('the step is shown once, with an empty field until the person is known (the question is asked, once)', ui.shown === 1 && ui.input.value === '' && person.asked === 1 && ui.hints.length === 0);
    person.answer('maya', 'login', false);
    same('a login: the field shows the name and the words say that it is the Discord name', [ui.input.value, ui.hints], ['maya', [HINT_LOGIN]]);
    check('nothing is done before the button', done.length === 0 && ui.hidden === 0);
    ui.button.listeners.click();
    same('the button goes on with the name that the card showed', done, ['maya']);
    check('... the step goes away', ui.hidden === 1);
    ui.button.listeners.click();
    ui.input.listeners.keydown({ key: 'Enter', preventDefault() {} });
    check('a second click or Enter does nothing more (once)', done.length === 1 && ui.hidden === 1);
}
{
    const ui = fakeUi();
    const person = fakePerson();
    names.runNameStep(ui, person, function () {});
    person.answer('maya_99', 'login', true);
    same('a login whose hub says that there is another name: the words say where it is changed (the chat menu)', ui.hints, [HINT_LOGIN + HINT_SWAP]);
}
{
    const ui = fakeUi();
    const done = [];
    names.runNameStep(ui, fakePerson(['Guest 550000', 'guest', false]), function (n) { done.push(n); });
    same('a guest: the guest name and the words that a login gives a name of one\'s own', [ui.input.value, ui.hints], ['Guest 550000', [HINT_GUEST]]);
    ui.input.listeners.keydown({ key: 'a', preventDefault() {} });
    check('another key does not go on', done.length === 0);
    let prevented = false;
    ui.input.listeners.keydown({ key: 'Enter', preventDefault() { prevented = true; } });
    same('Enter goes on (and is not typed into anything)', [done, prevented], [['Guest 550000'], true]);
}
{
    const ui = fakeUi();
    const person = fakePerson();
    const done = [];
    names.runNameStep(ui, person, function (n) { done.push(n); });
    ui.button.listeners.click();
    check('the button before the name is known waits: nothing is done and the card stays', done.length === 0 && ui.hidden === 0);
    person.answer('Jose', 'login', false);
    same('... and goes on by itself when the name is there, with that name', [done, ui.hidden, ui.input.value], [['Jose'], 1, 'Jose']);
    person.answer('Other', 'login', false);
    check('... once', done.length === 1);
}
{
    const ui = fakeUi();
    const done = [];
    names.runNameStep(ui, fakePerson(['maya', 'login', false]), function (n) { done.push(n); });
    ui.input.value = 'Mallory';
    ui.button.listeners.click();
    same('the field cannot be typed in, and what is written into it changes nothing: the name that goes on is the person\'s', done, ['maya']);
    check('the step listens to the button and to Enter and to nothing else (no input event: there is no typing to follow)', Object.keys(ui.button.listeners).join() === 'click' && Object.keys(ui.input.listeners).join() === 'keydown');
}
{
    const ui = fakeUi();
    ui.message.textContent = 'earlier line';
    names.runNameStep(ui, fakePerson(['maya', 'login', false]), function () {});
    ui.button.listeners.click();
    check('the line under the field is cleared when the step goes on', ui.message.textContent === '');
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
    body: { style: { setProperty() {} } },
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
    ['embed=1 is nothing special any more (the pages that were framed are gone): a shared link still asks', '?join=/ws&room=ABC&embed=1', true],
    ['... with a name it is chosen, as for any link', '?join=/ws&room=ABC&seat=1&name=Bob&embed=1', false],
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
    const doc = { getElementById() { return { setAttribute() {}, classList: { add() {} } }; }, querySelectorAll() { return []; }, body: { classList: { add() {} }, style: { setProperty() {} } } };
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
    const gate = shell.makeNameGate(true, args, ui, fakePerson(['maya', 'login', false]));
    let started = 0;
    check('a shared link: the gate is closed and the step is shown (with the name that the person plays under)', gate.pending() && ui.shown === 1 && ui.input.value === 'maya');
    gate.when(function () { started++; });
    check('... the game is NOT started while the step is up (it cannot connect)', started === 0 && args.indexOf('--name') === -1);
    ui.input.value = 'Zed';
    check('... nor while something is written into the field (it cannot be typed in)', started === 0);
    ui.button.listeners.click();
    check('... the button starts it, once, with the name that the card showed as --name', started === 1 && args.join(' ') === '--join-url ws://h/ws --room ABC --aspect 16:9 --name maya', args.join(' '));
    gate.when(function () { started++; });
    check('... and what asks after that is started at once', started === 2 && !gate.pending());
}
{
    const ui = fakeUi();
    const args = ['--join-url', 'ws://h/ws'];
    const person = fakePerson();
    const gate = shell.makeNameGate(true, args, ui, person);
    let started = 0;
    gate.when(function () { started++; });
    ui.button.listeners.click();
    check('the button before the person is known starts nothing', started === 0 && gate.pending() && args.length === 2);
    person.answer('Guest 550000', 'guest', false);
    check('... the game starts when the name is there, under it (a guest name is a name: --name)', started === 1 && !gate.pending() && args.join(' ') === '--join-url ws://h/ws --name Guest 550000', args.join(' '));
}
{
    const ui = fakeUi();
    const args = ['--join-url', 'ws://h/ws', '--name', 'Bob'];
    const gate = shell.makeNameGate(false, args, ui, null);
    let started = 0;
    gate.when(function () { started++; });
    check('any other address: the game starts at once, no step is shown, nothing is asked of anybody (no person), the arguments are untouched', started === 1 && ui.shown === 0 && args.join(' ') === '--join-url ws://h/ws --name Bob' && !gate.pending());
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
    check('the step asks, unless the browser holds this very seat: the page looks at the keys of this browser', /var asks = ANTS_PAGE\.asksForName\(window\.location\.search\);\s*if \(asks\) \{[^}]*holdsThisSeat\(window\.location\.search, ANTS_ARGS, window\.localStorage, Date\.now\(\)\)/.test(shellText));
    check('the keys block is the same text in the front page and the game page', keysBlockShell === between(lobbyText, 'REJOINKEY_BEGIN', 'REJOINKEY_END', lobbyPath));
}
// what the page does with the gate (read from the file): the game starts through it, and only through it
{
    const startCalls = [...shellText.matchAll(/\bstartGame\(\)/g)].length;
    const defs = [...shellText.matchAll(/function startGame\(\)/g)].length;
    const gated = [...shellText.matchAll(/ANTS_NAME_GATE\.when\(startGame\)/g)].length;
    check('the game of the page starts only through the gate (no direct startGame() call, two gated starts)', startCalls === defs && gated === 2, startCalls + ' calls, ' + defs + ' definitions, ' + gated + ' gated');
    check('the page makes the gate from the address and puts the name in the game\'s own arguments; only a page that asks makes the person that asks the hub', /makeNameGate\(asks, ANTS_ARGS, ui, asks \? AntsPlayerName\.forWindow\(window, nameCheck, \{ later: setTimeout, cancel: clearTimeout, random: Math\.random \}\) : null\)/.test(shellText) && /var asks = ANTS_PAGE\.asksForName\(window\.location\.search\)/.test(shellText));
    check('the game page\'s card says its words under the name (the hint element, as text) and puts the focus on the Join button, not on the read-only field; the field looks read-only (the cursor of the page, not of a text field)',
          /said: function \(text\) \{ document\.getElementById\('name-step-hint'\)\.textContent = text; \},/.test(shellText) && /try \{ ui\.button\.focus\(\); \} catch \(e\) \{ \/\* nothing to focus \*\/ \}\s*\},\s*hide: function \(\) \{\s*card\.hidden = true;/.test(shellText) && /\.name-step input \{[^}]*cursor: default;[^}]*\}/.test(shellText));
    check('the game page loads the script of the person (front/player_name.js) with the other scripts of front/, before its own code', /<script src="front\/lobby_net\.js"><\/script><script src="front\/player_name\.js"><\/script>/.test(shellText) && shellText.indexOf('front/player_name.js') < shellText.indexOf('var ANTS_NAME_GATE'));
    for (const id of ['name-step', 'name-step-title', 'name-step-input', 'name-step-go', 'name-step-msg']) check('the card of the step has #' + id, new RegExp('id="' + id + '"').test(shellText));
    check('the name field of the card is read-only (the name is shown, never typed), described by the line under it and by the words under it', /<input type="text" id="name-step-input" readonly aria-describedby="name-step-msg name-step-hint">/.test(shellText) && !/id="name-step-input"[^>]*(maxlength|placeholder|autocomplete)/.test(shellText));
    check('the card is hidden until the step shows it', /id="name-step"[^>]*hidden/.test(shellText));
    // The invitation of the front page's card is a link to THIS page's step (?join=/ws&room=..&seat=..: no name), so a friend who opens it on a phone sees this card and nothing else: the way out is here too,
    // as the front page's own step has it ("Whatever the one-card page does with the name step, it keeps a visible way out of it")
    const stepMarkup = shellText.slice(shellText.indexOf('id="name-step"'), shellText.indexOf('<div class="view-bar"'));
    check('the card has a way out: "Back to the front page", a plain link to the front page (no query: nothing of the match goes with it), after the hint and inside the card',
          /<a class="btn sm name-step-back" id="name-step-back" href="\/">&larr; Back to the front page<\/a>\s*<\/div>\s*<\/div>/.test(stepMarkup) && stepMarkup.indexOf('name-step-hint') < stepMarkup.indexOf('name-step-back'));
    // The notice (decided on 2026-10-08): where a page asks for a name it also says what the server does with an online match, in the hint's own type under the name field. The
    // words are the ones of the approved picture, in both pages; 30 days is the stack's default (tests/scripts/test_nginx_replays.py pins it to docker-compose.stack.yml)
    const noticeAt = stepMarkup.indexOf('<div class="name-step-hint name-step-notice">' + NOTICE_HTML + '</div>');
    const hintAt = stepMarkup.indexOf('<div class="name-step-hint" id="name-step-hint">The name the other players see.');
    check('the card tells a player who joins by a link what the server does with an online match: one more paragraph of the hint\'s own type, after the hint and before the way out, once',
          hintAt !== -1 && noticeAt > hintAt && noticeAt < stepMarkup.indexOf('name-step-back') && shellText.split(NOTICE_HTML).length === 2);
}



// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. web/lobby.html, read from the file: its scripts, its ids, the wiring of the name card, what it does with a name and what it puts into a link
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const path = require('path');
// the fake browser, in tests/scripts/web_page_harness.js (the page's own markup and scripts, a fake DOM, a scripted game server): runLobby(search, stored, options) and the messages of a game server
const H = require('./web_page_harness.js')(lobbyPath);
const { lobbyDir, lobbyMarkup, lobbyScripts, lobbyScript, RULES0, NET0, CHAT0, GUEST, hex, E, C, str8, KEY, welcome, reject, roomMessage, helloBytes, helloName, helloRoom, CODE, grouped, BLOCK_PARAMS, blockText, noBlockOf, THROWS, runLobby } = H;
{
    same('the page loads its scripts (the rules, the network client, the chat box, the rematch\'s strip, the map list, the shapes, the file fetcher, the Home screen) and then runs its own', lobbyScripts.map((m) => { const s = /src="([^"]*)"/.exec(m[1]); return s ? s[1] : 'inline'; }), ['front/map_maker_link.js', 'front/lobby_rules.js', 'front/lobby_net.js', 'front/player_name.js', 'front/lobby_chat.js', 'front/rematch.js', 'front/game_modes.js', 'front/game_type.js', 'front/map_picker.js', 'shapes.js', 'map_files.js', 'home/home.js', 'inline']);
    const markupIds = [...lobbyMarkup.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]);
    check('no id is used twice in the markup', new Set(markupIds).size === markupIds.length, markupIds.filter((id, i) => markupIds.indexOf(id) !== i).join(','));
    const asked = new Set([...lobbyScript.matchAll(/\$\('([\w-]+)'\)/g), ...lobbyScript.matchAll(/freshNode\('([\w-]+)'\)/g), ...lobbyScript.matchAll(/getElementById\('([\w-]+)'\)/g)].map((m) => m[1]));
    const missing = [...asked].filter((id) => markupIds.indexOf(id) === -1);
    check('every id that the script asks for is in the markup (' + asked.size + ' of them)', missing.length === 0, missing.join(','));
    for (const id of ['name-step', 'name-step-title', 'name-step-input', 'name-step-go', 'name-step-msg', 'name-step-hint', 'who-notice', 'name-step-own', 'rejoin', 'rejoin-note', 'rejoin-go', 'link', 'copy', 'slots', 'start', 'banner', 'banner-text', 'stats', 'stats-live', 'stats-played'])
        check('the page has #' + id + ' once', lobbyMarkup.split('id="' + id + '"').length === 2);
    for (const gone of ['more', 'morebox', 'more-here', 'more-windows', 'room-panel', 'sync', 'grid', 'frames-note', 'any-link', 'seat-rows', 'play-tab', 'all-here', 'all-windows', 'new-room', 'room-code', 'room-map'])
        check('the test room\'s #' + gone + ' is gone with "More ways to play"', lobbyMarkup.indexOf('id="' + gone + '"') === -1);
    for (const gone of ['player-name', 'join-name', 'who', 'who-go', 'who-back', 'who-title', 'cards', 'map-pick', 'play', 'seat-you-0', 'sit-0', 'invite-list', 'name-msg', 'join-go', 'join-code'])
        check('the one-card page\'s #' + gone + ' is gone', lobbyMarkup.indexOf('id="' + gone + '"') === -1);
    check('the markup has no name field but the card\'s: one text input with the id name-step-input and no other input for a name', (lobbyMarkup.match(/<input[^>]*>/g) || []).filter((t) => /\bid="[^"]*name[^"]*"|placeholder="[^"]*name/i.test(t)).length === 1);
    // The name card is the game page's step in the lobby page: the NAME block's runNameStep runs it, over the card's own field, button and message
    check('the name card is run by the NAME block\'s runNameStep, over the card\'s own field, button and message (the field and the button are fresh nodes: no listener of an earlier use stays)',
          /input: freshNode\('name-step-input'\), button: freshNode\('name-step-go'\),\s*message: \{ get textContent\(\) \{ return \$\('name-step-msg'\)\.textContent; \}, set textContent\(v\) \{ \$\('name-step-msg'\)\.textContent = v; ui\.input\.setAttribute\('aria-invalid', v \? 'true' : 'false'\); \} \},/.test(lobbyScript) &&
          /runNameStep\(ui, person, o\.done\);/.test(lobbyScript) &&
          /function freshNode\(id\) \{\s*var old = \$\(id\), copy = old\.cloneNode\(true\);\s*old\.parentNode\.replaceChild\(copy, old\);\s*return copy;\s*\}/.test(lobbyScript));
    check('two things ask for a name: a link of somebody else\'s room and a code that was typed (askName is called twice, and no other way starts a seat of a stranger)', (lobbyScript.match(/\baskName\(\{/g) || []).length === 2);
    check('the card says its words as text: the title, the button, the words under the name (from the NAME block), the way out; the field is read-only and takes the focus away from itself: the button has it', /\$\('name-step-title'\)\.textContent = o\.title;/.test(lobbyScript) && /ui\.button\.textContent = o\.button;/.test(lobbyScript) && /said: function \(text\) \{ \$\('name-step-hint'\)\.textContent = text; \},/.test(lobbyScript)
          && /\$\('name-step-own'\)\.textContent = o\.backText \|\| 'Start a room of my own instead';/.test(lobbyScript) && /try \{ ui\.button\.focus\(\); \}/.test(lobbyScript) && /<input type="text" id="name-step-input" readonly aria-describedby="name-step-msg name-step-hint">/.test(lobbyMarkup) && !/id="name-step-input"[^>]*(maxlength|placeholder|autocomplete)/.test(lobbyMarkup));
    check('the front page\'s card field looks read-only: the cursor of the page, not of a text field', /\.name-step input \{[^}]*cursor: default; \}/.test(lobbyText));
    check('nobody types a name and nobody is given a made-up one: no suggestion, no picked name, no remembered name, no pencil, no rename, no key of a name in the storage', !/suggestion|pickName|savedName|NAME_KEY|editName|data-edit|\.rename\(|'ants\.name'/.test(lobbyScript) && !/\.edit\b/.test(lobbyText) && !/\u270e/.test(lobbyText));
    check('the person of the page (front/player_name.js) is made once, with the page\'s own rules of a name and its own clock and generator, and asks when the page opens; the page names no address of the hub', (lobbyScript.match(/PlayerName\.forWindow\(/g) || []).length === 1 && /var person = PlayerName\.forWindow\(window, nameCheck, \{ later: setTimeout, cancel: clearTimeout, random: Math\.random \}\);\s*person\.ask\(\);/.test(lobbyScript) && lobbyText.indexOf('/hub') === -1);
    check('the name for a new seat is the person\'s sure one, and a seat connects only through open(): at once with a name, else when the person is known (one call of connect() on a client in the whole script)', /function myName\(\) \{ return person\.known\(\); \}/.test(lobbyScript) && /function open\(c, name\) \{ if \(name\) c\.connect\(\); else person\.ready\(function \(own\) \{ c\.name = own; c\.connect\(\); \}\); \}/.test(lobbyScript) && (lobbyScript.match(/\.connect\(\)/g) || []).length === 2 && /open\(c, p\.name\);/.test(lobbyScript) && /open\(p, name\);/.test(lobbyScript));
    check('a match for one is played under the seat\'s own name, and the Rejoin button sends none (the key takes the seat)', /soloAddress\(model, client\.name, pickedShape\)/.test(lobbyScript) && /rejoinQuery\(rejoinOffered, '', pickedShape\)/.test(lobbyScript));
    // What the page does with a name: nothing is put into markup. A name is set as text (el() sets textContent), through a value or an attribute, and goes into an address only through encodeURIComponent
    check('el() makes an element with its text as textContent, and a person\'s name goes onto a card through it', /function el\(tag, cls, text\) \{[^}]*e\.textContent = text;[^}]*\}/.test(lobbyScript) && /card\.who\.forEach\(function \(w\) \{ who\.appendChild\(el\('span', w\.cls, w\.text\)\); \}\);/.test(lobbyScript));
    check('the toast, the strip\'s sentence and the removal question are text too', /t\.textContent = text; t\.hidden = false;/.test(lobbyScript) && /\$\('banner-text'\)\.textContent = b\.text;/.test(lobbyScript) && /ask\.appendChild\(el\('span', '', 'Remove ' \+ card\.who\[0\]\.text \+ ' from the room\?'\)\);/.test(lobbyScript));
    check('nothing in the script makes markup from text', !/\.innerHTML\s*[+]?=|\.outerHTML|insertAdjacentHTML|document\.write/.test(lobbyScript));
    // What goes into an address: a link that is made for somebody else carries the room and no name; the addresses of this page's own games and the way back carry the name that this page's person chose
    check('the link of the room is the page\'s own address with the plain code and nothing else (no name, no key): ?room=<code>', /function roomLink\(\) \{ return window\.location\.origin \+ window\.location\.pathname\.replace\(\/\^\\\/\+\/, '\/'\) \+ '\?room=' \+ encodeURIComponent\(client \? client\.code : ''\); \}/.test(lobbyScript));
    check('the Copy button, the Share sheet and the link field all give that link', /copyButton\(\$\('copy'\), roomLink\);/.test(lobbyScript) && /navigator\.share\(\{ url: roomLink\(\) \}\)/.test(lobbyScript) && /if \(\$\('link'\)\.value !== roomLink\(\)\) \$\('link'\)\.value = roomLink\(\);/.test(lobbyScript));
    check('the page has no test room: no game address of its own for a seat, no frame of the game page, no window, no sync between pages', ['gameUrl', 'testRoom', 'runTestRoom', 'iframe', 'embed=1', 'window.open', 'BroadcastChannel', 'ants-sync', 'legacyRoom', 'play=here'].every((word) => lobbyScript.indexOf(word) === -1), ['gameUrl', 'testRoom', 'runTestRoom', 'iframe', 'embed=1', 'window.open', 'BroadcastChannel', 'ants-sync', 'legacyRoom', 'play=here'].filter((word) => lobbyScript.indexOf(word) !== -1).join(','));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. web/lobby.html as a whole, with a small fake of the browser, its two scripts (front/lobby_rules.js and front/lobby_net.js, the real files) and a scripted game server
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
check('the three scripts of front/ give the page its globals', !!RULES0 && !!NET0 && !!CHAT0 && typeof RULES0.nameCheck === 'function' && typeof NET0.LobbyClient === 'function' && typeof CHAT0.mount === 'function');


(async () => {

const keysOf = (env) => Object.keys(env.storage.data).filter((k) => k !== 'ants.guest');         // (the keys of the browser's storage but the guest name's, which a page that made a room under it has kept)

// ---- 5.1 the plain visit: the page opens on Home and makes no room; My room makes it (under a guest name for a visitor who is not logged in); nothing is asked first
// (the checks of Home itself are in tests/scripts/web_home_screen_check.js; here and below runLobby presses My room for a plain visit, as a person does, so that the room's own checks have a room)
{
    const home = runLobby('', {}, { stayHome: true });
    check('a plain visit asks nothing first: no name card, no banner; Home is up and the room is there behind it, and no room is made (no socket)', home.$('name-step').hidden && home.$('banner').hidden && !home.$('lobby').hidden && home.screen() === 'home' && home.sockets.length === 0);
    home.home('mine').click();
    same('My room makes the room on the game server: one socket, to this site\'s /ws', home.sockets.map((s) => s.url), ['wss://play.test/ws']);
    const env = runLobby('', {});
    check('the room is up for the checks below: no card, no strip, no banner', env.$('name-step').hidden && env.$('rejoin').hidden && env.$('banner').hidden && !env.$('lobby').hidden);
    same('... it is made on the game server at once: one socket, to this site\'s /ws', env.sockets.map((s) => s.url), ['wss://play.test/ws']);
    const hello = env.last().open().sent[0];
    const code = helloRoom(hello);
    check('... the Hello makes the room: a new code of six characters, the guest name of a visitor who is not logged in, no key, the lobby block', CODE.test(code) && helloName(hello) === GUEST && hex(hello) === hex(helloBytes(GUEST, code)), hex(hello));
    check('... the invitation is the page\'s address and ?room=<code>: no name, no key, no seat (whoever opens it is asked), and the code is shown in two groups of three', env.$('link').value === 'https://play.test/?room=' + code && env.$('code').textContent === grouped(code), env.$('link').value);
}
for (const [what, stored] of [['a remembered name', 'Maya'], ['a remembered name with blanks at its ends', '  Maya  '], ['a name that looks like markup', '<b>Hi</b> & "you"'], ['a remembered name that the rules refuse (a computer player\'s)', 'Bot (x)'], ['one with an accent', 'Zo\u00eb'], ['one of 33 characters', 'a'.repeat(33)], ['an empty one', '']]) {
    const env = runLobby('', { 'ants.name': stored });
    const hello = env.last().open().sent[0];
    check('a name that this browser remembered from earlier typing is ignored (' + what + '): the page makes its room under the guest name', helloName(hello) === GUEST, helloName(hello));
    check('... and the page does not touch it: the browser keeps what it kept, and the only key that the page writes is the guest name\'s', env.storage.data['ants.name'] === stored && Object.keys(env.storage.data).sort().join() === 'ants.guest,ants.name' && env.storage.data['ants.guest'] === '550000', JSON.stringify(env.storage.data));
}
{
    let env = null;
    try { env = runLobby('', THROWS); } catch (e) { check('a browser that refuses its storage still runs the page', false, e.message); }
    if (env) {
        const hello = env.last().open().sent[0];
        check('a browser that refuses its storage still makes its room, under a guest name that lasts for the page (nothing is kept, nothing breaks)', helloName(hello) === GUEST && CODE.test(helloRoom(hello)));
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
        check('a session entry that is ' + what + ' is ignored: a new room with a new code and no key', CODE.test(helloRoom(h)) && helloRoom(h) !== 'k7m2xq' && hex(h) === hex(helloBytes(GUEST, helloRoom(h))));
    }
    const codes = new Set();
    for (let i = 0; i < 20; i++) codes.add(helloRoom(runLobby('', {}).last().open().sent[0]));
    check('twenty visits have twenty codes, each of six characters of the alphabet without look-alikes (no i, l, o, 0 or 1)', codes.size === 20 && [...codes].every((c) => /^[abcdefghjkmnpqrstuvwxyz23456789]{6}$/.test(c)), [...codes].slice(0, 3).join(','));
}

// ---- 5.1b whose name the page plays under: the login's, else a guest name that this browser keeps (front/player_name.js, tests/scripts/web_player_name_check.js; the hub's page is scripted by options.me)
const MAYA = { login: true, you: { name: 'maya', mod: false } };                // (the hub's answer for a person who is logged in)
const ZED = { login: true, you: { name: 'Zed', mod: false } };
const answer = (body) => ({ ok: true, status: 200, json: () => Promise.resolve(body) });
const afterIt = () => new Promise((resolve) => setImmediate(resolve));
{
    const options = { me: MAYA, stayHome: true };
    const env = runLobby('', {}, options);
    same('the page asks for the login when it opens: one request, a GET of the site\'s own page for it, with the cookie of this site and no cache (and it is none of the site\'s other requests)', [env.meFetches.length, env.meFetches[0].url, env.meFetches[0].init.credentials, env.meFetches[0].init.cache, env.meFetches[0].init.method, env.fetches.some((f) => f.url === '/hub/me')], [1, '/hub/me', 'same-origin', 'no-store', undefined, false]);
    check('... and makes no room for it: Home is up and no socket is open', env.sockets.length === 0 && env.screen() === 'home');
    env.home('mine').click();
    check('My room asks again (a login may have ended since: a log out leaves the page where it is)', env.meFetches.length === 2 && env.sockets.length === 1);
    const hello = env.last().open().sent[0];
    check('a visitor who is logged in plays under the login\'s name: the Hello has it', helloName(hello) === 'maya' && hex(hello) === hex(helloBytes('maya', helloRoom(hello))), hex(hello));
    check('... and no guest name is made or kept for a person who never needs one', Object.keys(env.storage.data).length === 0);
}
for (const [what, you, want] of [['an accent', { name: 'José', other: 'jose_1' }, 'Jose'], ['a display name that cannot fit and a username that does', { name: '名前', other: 'user_name' }, 'user_name'], ['today\'s shape of the answer, a username', { name: 'user_name' }, 'user_name'], ['a name that cannot fit and no other name', { name: '名前' }, GUEST]]) {
    const env = runLobby('', {}, { me: { login: true, you: Object.assign({ mod: false }, you) } });
    check('the room of a login with ' + what + ': the Hello has ' + JSON.stringify(want), helloName(env.last().open().sent[0]) === want);
}
for (const [what, me] of [['a hub where nobody is logged in', 'guest'], ['a site with no hub', undefined], ['a hub that fails', 'fail'], ['an answer that is no answer', { login: 'maybe' }], ['an answer with no login word', { you: null }]]) {
    const env = runLobby('', {}, { me });
    const hello = env.last().open().sent[0];
    check('a visitor with ' + what + ' plays under the guest name, at once: the Hello has it, and the browser keeps it under its own key', helloName(hello) === GUEST && env.storage.data['ants.guest'] === '550000' && Object.keys(env.storage.data).join() === 'ants.guest', JSON.stringify(env.storage.data));
}
{   // the guest name is the same on the next visit, whatever the generator says, and a login and a log out never change it
    const first = runLobby('', {}, { me: 'guest' });
    helloName(first.last().open().sent[0]);
    const kept = first.storage.data;
    const login = runLobby('', kept, { me: MAYA, random: 0.1 });
    const out = runLobby('', kept, { me: 'guest', random: 0.9 });
    same('a guest keeps the same guest name when the page opens again, as a login (the login\'s own name) and as a guest again after a log out', [first.storage.data['ants.guest'], helloName(login.last().open().sent[0]), helloName(out.last().open().sent[0])], ['550000', 'maya', GUEST]);
    check('... and nothing wrote the key again', kept['ants.guest'] === '550000' && Object.keys(kept).join() === 'ants.guest');
    const stored = runLobby('', { 'ants.guest': '123456' }, { me: 'guest' });
    same('a guest name that the browser kept is the one that is used', helloName(stored.last().open().sent[0]), 'Guest 123456');
    const junk = runLobby('', { 'ants.guest': 'Guest 123456' }, { me: 'guest' });
    same('a kept value that is not six digits is made again', [helloName(junk.last().open().sent[0]), junk.storage.data['ants.guest']], [GUEST, '550000']);
}
{   // a log out without a reload: the chat leaves the page where it is, and the next room is a guest's
    const options = { me: MAYA, stayHome: true };
    const env = runLobby('', {}, options);
    env.home('mine').click();
    const s = env.arrive();
    same('the first room is the login\'s', helloName(s.sent[0]), 'maya');
    env.roomBar('leave').click();
    options.me = 'guest';
    env.home('mine').click();
    const second = env.last().open().sent[0];
    check('after a log out with no reload the next room is a guest\'s: the guest name that this browser keeps, and the old name is nowhere in it', helloName(second) === GUEST && hex(second) === hex(helloBytes(GUEST, helloRoom(second))));
    const asked = env.meFetches.length;
    env.roomBar('leave').click();
    options.me = MAYA;
    env.home('mine').click();
    check('... a login in another tab is not this page\'s (its chat stays a reader too): a guest is not asked again, and the next room is a guest\'s, under the same guest name', env.meFetches.length === asked && helloName(env.last().open().sent[0]) === GUEST);
}
{   // a seat that is held keeps its name: a reload of the tab takes it back under the name that the room showed, and does not ask for a login first
    const entry = { 'ants.lobby': JSON.stringify({ c: 'k7m2xq', k: hex(KEY), n: 'Sam', o: 1 }) };
    const env = runLobby('', {}, { me: MAYA, session: entry });
    const hello = env.last().open().sent[0];
    check('a reload of the tab with a seat in a room: the Hello is the seat\'s own name ("Sam"), with its key, though the login is another; nothing waits for the hub', hex(hello) === hex(helloBytes('Sam', 'k7m2xq', { key: KEY })) && env.sockets.length === 1);
}
{   // a person who presses My room before the hub has answered waits for it, at most a short time
    const late = [];
    const options = { me: () => new Promise((resolve) => { late.push(resolve); }), stayHome: true };
    const env = runLobby('', {}, options);
    env.home('mine').click();
    check('My room before the hub has answered: the Room is there, and no socket is opened yet (the name is not known)', env.sockets.length === 0 && env.screen() === 'room' && late.length === 1);
    env.advance(1999);
    check('... it waits', env.sockets.length === 0);
    late[0](answer(MAYA));
    await afterIt();
    check('... the answer comes in time: the page connects under the login\'s name', env.sockets.length === 1 && helloName(env.last().open().sent[0]) === 'maya');
}
{
    const late = [];
    const options = { me: () => new Promise((resolve) => { late.push(resolve); }), stayHome: true };
    const env = runLobby('', {}, options);
    env.home('mine').click();
    env.advance(2000);
    check('an answer that does not come in time: the room goes on as a guest (a socket, the guest name)', env.sockets.length === 1 && helloName(env.last().open().sent[0]) === GUEST);
    late[0](answer(MAYA));
    await afterIt();
    check('... and the late answer changes nothing: no second socket, the same name', env.sockets.length === 1 && helloName(env.last().sent[0]) === GUEST && late.length === 1);
    env.arrive();
    env.roomBar('leave').click();
    env.home('mine').click();
    check('... the next room asks again, and a login that comes in time is the login\'s', late.length === 2 && env.sockets.length === 1);
    late[1](answer(MAYA));
    await afterIt();
    check('... the answer is the login\'s', env.sockets.length === 2 && helloName(env.last().open().sent[0]) === 'maya');
}
{   // a hub that does not answer at all: the page does not wait for more than the limit
    const env = runLobby('', {}, { me: 'hang', stayHome: true });
    env.home('mine').click();
    env.advance(1999);
    check('a hub that never answers: nothing connects before the limit', env.sockets.length === 0);
    env.advance(1);
    check('... then the room goes on as a guest', env.sockets.length === 1 && helloName(env.last().open().sent[0]) === GUEST);
}
{   // no fetch at all
    const env = runLobby('', {}, { noFetch: true });
    check('a browser with no fetch plays under the guest name, at once', env.sockets.length === 1 && helloName(env.last().open().sent[0]) === GUEST && env.meFetches.length === 0);
}
{   // a link, while the hub's answer is late, and Start a room of my own: the own room waits for the name too
    const late = [];
    const env = runLobby('?room=k7m2xq', {}, { me: () => new Promise((resolve) => { late.push(resolve); }) });
    env.$('name-step-own').click();
    check('"a room of my own instead" before the hub has answered: no socket yet', env.sockets.length === 0);
    late[1] ? late[1](answer(MAYA)) : late[0](answer(MAYA));
    await afterIt();
    check('... and the room is made under the login\'s name when it has', env.sockets.length === 1 && helloName(env.last().open().sent[0]) === 'maya');
}

// ---- 5.2 the room as the page paints it: a name is only ever text; the pencil changes the player's own name; a link made for somebody else carries no name
{
    const evil = '<img src=x onerror=alert(1)>';
    const env = runLobby('', { 'ants.name': 'Maya' });
    const s = env.arrive({ key: KEY, seats: [[C, 'Maya'], [C, evil], [E, ''], [E, '']] });
    same('the four cards are built in the reading order of the colors on the map: Black, Green, Red, Blue (seats 3, 0, 1, 2)', env.$('slots').children.map((li) => li.getAttribute('data-i')), ['3', '0', '1', '2']);
    check('a card shows the color and the name of the player who has it, as text', env.card(0).textContent.indexOf('Maya') !== -1 && env.card(0).textContent.indexOf('Green') !== -1 && env.card(1).textContent.indexOf(evil) !== -1);
    check('... a name with < > & makes no element and no markup: the page made no markup at all', env.innerHTMLWrites.length === 0 && env.$('slots').findAll((e) => e.tagName === 'img').length === 0);
    check('... the text of the card of the players, to a screen reader, is an attribute (never markup): the color and the name', env.card(1).getAttribute('aria-label') === 'Red: ' + evil);
    check('no card has a pencil or a field for a name: nobody types a name, and the own card shows the name as text like the others', [0, 1, 2, 3].every((i) => env.inCard(i, 'data-edit') === null && env.card(i).findAll((e) => e.tagName === 'input').length === 0 && env.card(i).textContent.indexOf('\u270e') === -1));
    const done = env.sockets.length;
    s.receive(roomMessage([[C, 'Mia'], [C, evil], [E, ''], [E, '']], { you: 0, leader: 0 }));
    check('the card shows the name that the room says for the own color (the room may take another than the one that was sent), as text', env.card(0).textContent.indexOf('Mia') !== -1 && env.card(0).textContent.indexOf('Maya') === -1);
    s.receive(roomMessage([[C, 'Mia'], [C, evil], [C, 'Newcomer'], [E, '']], { you: 0, leader: 0 }));
    check('a room that changes is drawn at once: nothing holds the cards back (no name is being typed)', env.card(2).textContent.indexOf('Newcomer') !== -1 && env.sockets.length === done);
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
{   // a player's screen: no pencil, no Remove, and the host's name is the host's
    const env = runLobby('', {});
    const s = env.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    check('a player\'s screen has no pencil on the own card, no field for a name and no Remove button anywhere', env.$('slots').findAll((e) => e.hasAttribute('data-edit') || e.hasAttribute('data-remove') || e.tagName === 'input').length === 0);
    same('... the room is named after the host, and START waits for the host', [env.$('room-h').textContent, env.$('start').disabled, env.$('start').textContent], ['Maya\u2019s room', true, 'Waiting for Maya']);
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

// ---- 5.2c the chat box (front/lobby_chat.js, tests/scripts/web_lobby_chat_check.js): the page mounts it once and gives it every client; it is there when the room is a lobby, a line goes out as a Chat message at the
// server's pace, the room's lines are text, and it is shut while START waits and while the link is down
{
    const evil = '<img src=x onerror=alert(1)>';
    const env = runLobby('', { 'ants.name': 'Maya' });
    const box = env.$('chat');
    check('before the room has welcomed the page the chat box is not there', box.hidden === true && box.children.length > 0);
    const s = env.arrive({ key: KEY, seats: [[C, 'Maya'], [C, evil], [E, ''], [E, '']] });
    check('a room that is a lobby: the chat box is there, with its field open', box.hidden === false);
    const find = (pred) => box.findAll(pred)[0];
    const input = find((e) => e.getAttribute('id') === 'chat-input');
    const form = find((e) => e.tagName === 'form');
    const hint = find((e) => e.getAttribute('id') === 'chat-hint');
    const log = find((e) => e.getAttribute('id') === 'chat-log');
    check('the field is open and the log says to say hello', input.disabled === false && input.readOnly === false && log.children.length === 1 && /Say hello/.test(log.textContent));
    const sentChat = () => s.sent.filter((b) => b[0] === 9);
    input.value = 'hello there';
    form.fire('submit', { preventDefault() {} });
    same('a line goes out as a Chat message of this seat (Green, 0), no team', sentChat().map(hex), [hex(NET0.encodeChat(0, false, 'hello there'))]);
    check('the field is empty again', input.value === '');
    const say = (text) => Uint8Array.from([9, 1, 0, text.length].concat(Array.from(text).map((c) => c.charCodeAt(0))));
    s.receive(say('hi Maya'));
    same('the room\'s line comes with the name of the seat, as text and not as markup', log.children.map((li) => li.textContent), [evil + ': hi Maya']);
    check('(a name that is markup made no element: the page wrote no innerHTML)', env.innerHTMLWrites.length === 0 && log.children[0].getAttribute('data-c') === 'red');
    // the pace (the client's: tests/scripts/web_lobby_client_check.js has its clock): six more at once; three go (the burst of four is spent with the first), the others stay and the hint says so
    for (let i = 1; i <= 6; i++) { input.value = 'line ' + i; form.fire('submit', { preventDefault() {} }); }
    check('lines at once: four in all go out, the line that was refused stays in the field, and the hint asks for a little slower', sentChat().length === 4 && input.value === 'line 6' && /A little slower/.test(hint.textContent), sentChat().length + ' ' + input.value + ' ' + hint.textContent);
    // START waits: the field is shut; the start is given up: it opens
    s.receive(roomMessage([[C, 'Maya'], [C, evil], [E, ''], [E, '']], { you: 0, leader: 0, flags: 7, inGame: 1 }));
    check('while START waits the field is shut (read-only) and says that the game is opening', input.readOnly === true && input.disabled === false && /Opening the game/.test(input.placeholder));
    s.receive(roomMessage([[C, 'Maya'], [C, evil], [E, ''], [E, '']], { you: 0, leader: 0 }));
    check('the start is given up: the field is open', input.readOnly === false);
    // the link is down: shut, and the log stays
    s.drop();
    check('while the link is down the field is shut (read-only: it keeps its focus), and the lines stay', input.readOnly === true && input.disabled === false && /back when you are in the room again/.test(input.placeholder) && log.children.length === 1 && log.children[0].textContent === evil + ': hi Maya', [input.readOnly, input.placeholder, log.children.length].join(' | '));
    check('no markup was ever written by the page for a name or a line', env.innerHTMLWrites.length === 0);
}
{   // a person who joins is a small line of the system, a person who goes too; a room that is made again (after a restart) starts an empty box
    const env = runLobby('', { 'ants.name': 'Maya' });
    const s = env.arrive({ key: KEY, seats: [[C, 'Maya'], [E, ''], [E, ''], [E, '']] });
    const log = env.$('chat').findAll((e) => e.getAttribute('id') === 'chat-log')[0];
    check('alone: the hint says to send the link', /Send the link above/.test(log.textContent));
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0 }));
    s.receive(roomMessage([[C, 'Maya'], [E, ''], [E, ''], [E, '']], { you: 0, leader: 0 }));
    same('Sam comes and goes: two small lines of the system', log.children.map((li) => li.textContent), ['Sam joined as Red.', 'Sam left.']);
}

{   // a team press that the page's pace holds back (the plan budget is empty): the screen must show the ROOM's teams, not a split that the room never got (START sends the room's teams, not the screen's)
    const env = runLobby('', { 'ants.name': 'Maya' });
    const s = env.arrive({ key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [C, 'Tess'], [E, '']] });
    const plans = () => s.sent.filter((b) => b[0] === 32);
    const button = (seat, side) => env.$('slots').querySelector('[data-seat="' + seat + '"][data-side="' + side + '"]');
    const lit = (seat, side) => button(seat, side).getAttribute('aria-pressed') === 'true';
    check('the leader of three has the two team buttons on every color, and the room has no teams', button(0, 1) !== null && button(1, 1) !== null && button(2, 2) !== null && !lit(0, 1) && !lit(1, 1) && /Free for all/.test(env.$('teams-note').textContent));
    for (let i = 0; i < 8; i++) env.$('next').click();
    check('the map arrow held down (8 presses) sends 5 plans: the plan budget of the page is empty', plans().length === 5, String(plans().length));
    env.press(button(0, 1));
    check('a team button pressed alone is the page\'s own (a pair is what the room holds): lit, nothing sent', lit(0, 1) && plans().length === 5);
    env.press(button(1, 1));
    check('the second button makes a pair, which the page\'s pace refuses: no plan goes out', plans().length === 5);
    check('... and the screen shows the ROOM\'s teams (none), not a pair that the room never got: Sam\'s button is not lit and the line under the colors does not name a team', !lit(1, 1) && !/^Teams:/.test(env.$('teams-note').textContent), 'Sam lit: ' + lit(1, 1) + ', "' + env.$('teams-note').textContent + '"');
    check('... the button that was pressed alone before stays as it was (the page\'s own)', lit(0, 1));
    // the room's teams are the truth: a room that has a pair shows it, also after a press that the pace refused (a press that clears the teams)
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [C, 'Tess'], [E, '']], { you: 0, leader: 0, teamA: 0, teamB: 1 }));
    check('a room with a pair shows it: Green and Red on Team 1', lit(0, 1) && lit(1, 1) && /^Teams: Green \+ Red against Blue/.test(env.$('teams-note').textContent), env.$('teams-note').textContent);
    env.press(button(1, 1));
    check('a press that would clear the teams is refused by the pace too: nothing goes out and the screen still shows the room\'s pair', plans().length === 5 && lit(0, 1) && lit(1, 1) && /^Teams: Green \+ Red against Blue/.test(env.$('teams-note').textContent), 'plans ' + plans().length + ', "' + env.$('teams-note').textContent + '"');
}

// ---- 5.3 the name card of a link that somebody sent (?room=): asked first, every time; nothing connects before the button
{
    const env = runLobby('?room=k7m2xq', { 'ants.name': 'Maya' });
    check('a link to a room asks first, every time (a name that this browser remembers from earlier typing changes nothing): the card is up over the page', !env.$('name-step').hidden && env.$('name-step').getAttribute('role') === 'dialog' && env.$('name-step').getAttribute('aria-modal') === 'true');
    same('... titled with the room as a screen shows it (two groups of three), a Join button, the guest name in the field', [env.$('name-step-title').textContent, env.$('name-step-go').textContent, env.$('name-step-input').value], ['Join the room k7m 2xq', 'Join', GUEST]);
    same('... the words say that it is a guest name and what a login gives; the way out is a button', [env.$('name-step-hint').textContent, env.$('name-step-own').textContent], [HINT_GUEST, 'Start a room of my own instead']);
    check('... the field has no placeholder and no length (nothing to type: the markup makes it read-only, as section 4 reads) and the button has the focus', !env.$('name-step-input').hasAttribute('placeholder') && !env.$('name-step-input').hasAttribute('maxlength') && env.focused === env.$('name-step-go'));
    check('... the notice that online matches are recorded is shown, and there is no message yet', !env.$('who-notice').hidden && env.$('name-step-msg').textContent === '');
    check('nothing connects before the button: no socket, the address is not rewritten, and the page behind the card is "Joining a room" with START off and the colors greyed', env.sockets.length === 0 && env.replaced.length === 0 && env.$('room-h').textContent === 'Joining a room' && env.$('start').disabled === true && env.body.classList.contains('offline') && env.body.classList.contains('joining'));
    env.$('name-step-input').value = 'Bot (x)';
    env.$('name-step-input').key('Enter');
    check('Enter joins, whatever was written into the field: the card goes away, the page connects (one socket), and the browser is told nothing of a name (the one key that the page keeps is the guest\'s)', env.$('name-step').hidden && env.sockets.length === 1 && env.storage.data['ants.name'] === 'Maya' && Object.keys(env.storage.data).sort().join() === 'ants.guest,ants.name');
    const hello = env.last().open().sent[0];
    check('... the Hello is the guest name, in the room of the link, with no lobby block (a link only joins: a code with no room is not made) and no key', hex(hello) === hex(helloBytes(GUEST, 'k7m2xq', { join: true })) && env.replaced.length === 0, hex(hello));
}
{
    const env = runLobby('?room=k7m2xq', {}, { me: MAYA });
    same('a link for a person who is logged in: the field shows the login\'s name, and the words say that it is the Discord name', [env.$('name-step-input').value, env.$('name-step-hint').textContent], ['maya', HINT_LOGIN]);
    env.$('name-step-go').click();
    const hello = env.last().open().sent[0];
    check('... Join plays under it: the Hello has the login\'s name, the room of the link, no block, no key; the browser kept no guest name for a person who never needed one', hex(hello) === hex(helloBytes('maya', 'k7m2xq', { join: true })) && env.$('name-step').hidden && Object.keys(env.storage.data).length === 0, JSON.stringify(env.storage.data));
}
{
    const env = runLobby('?room=k7m2xq', {}, { me: { login: true, you: { name: 'Jos\u00e9', mod: false, pick: 'display', other: 'jose_77', key: 'k1' } } });
    same('a login whose name has an accent: the field shows it made to fit, and the words say where the other name is changed (the hub says that there is one)', [env.$('name-step-input').value, env.$('name-step-hint').textContent], ['Jose', HINT_LOGIN + HINT_SWAP]);
    const env2 = runLobby('?room=k7m2xq', {}, { me: { login: true, you: { name: '\u540d\u524d', mod: false, pick: 'display', other: 'user_name', key: 'k1' } } });
    same('a login whose display name cannot fit: the username that does', env2.$('name-step-input').value, 'user_name');
}
{
    const late = [];
    const env = runLobby('?room=k7m2xq', {}, { me: () => new Promise((resolve) => { late.push(resolve); }) });
    check('a link while the hub has not answered: the card is up with an empty field, and nothing connects', !env.$('name-step').hidden && env.$('name-step-input').value === '' && env.sockets.length === 0);
    env.$('name-step-go').click();
    check('Join before the answer waits: the card stays and no socket is opened', !env.$('name-step').hidden && env.sockets.length === 0);
    late[0]({ ok: true, status: 200, json: () => Promise.resolve(MAYA) });
    await new Promise((r) => setImmediate(r));
    check('... the answer comes: the card goes away and the page connects under the login\'s name', env.$('name-step').hidden && env.sockets.length === 1 && helloName(env.last().open().sent[0]) === 'maya');
    const slow = runLobby('?room=k7m2xq', {}, { me: () => new Promise(() => {}) });
    slow.$('name-step-go').click();
    slow.advance(1999);
    check('an answer that never comes: Join waits for the limit', !slow.$('name-step').hidden && slow.sockets.length === 0);
    slow.advance(1);
    check('... then goes on as a guest: the card shows the guest name and the page connects under it', slow.$('name-step').hidden && slow.sockets.length === 1 && slow.$('name-step-input').value === GUEST && helloName(slow.last().open().sent[0]) === GUEST);
}
{   // a link whose room is gone, full, running or a seat that was refused: a room of the visitor's own, under the same name, with the strip's sentence
    const link = () => { const e = runLobby('?room=k7m2xq', {}, { me: ZED }); e.$('name-step-go').click(); return e; };
    const NOROOM = 'There is no room with that code. Check it, or ask the host to send the link again.';
    const none = link();
    check('a link is a Hello that only joins (no lobby block), so that a code with no room is not made', none.sockets.length === 1 && hex(none.last().open().sent[0]) === hex(helloBytes('Zed', 'k7m2xq', { join: true })));
    none.last().receive(reject(6));
    check('a link to a code that no room has (NoSuchRoom): no room is made (one socket, no second Hello) and the name card is back, with the same name, the line for a code with no room (aria-invalid) and the button for a room of one\'s own', none.sockets.length === 1 && !none.$('name-step').hidden && none.$('name-step-input').value === 'Zed' && none.$('name-step-msg').textContent === NOROOM && none.$('name-step-input').getAttribute('aria-invalid') === 'true' && none.$('name-step-own').textContent === 'Start a room of my own instead' && none.$('name-step-go').textContent === 'Join', none.$('name-step-msg').textContent);
    check('... the address stays the link (nothing replaced) and no strip is shown', none.replaced.length === 0 && none.$('banner').hidden);
    none.$('name-step-own').click();
    const mine = none.last().open().sent[0];
    check('... "Start a room of my own instead" then makes a room of its own under the same name (a new code, no key, the lobby block) and puts the address of the Room ("/#room": no link in it) in the bar', none.sockets.length === 2 && none.$('name-step').hidden && CODE.test(helloRoom(mine)) && helloRoom(mine) !== 'k7m2xq' && hex(mine) === hex(helloBytes('Zed', helloRoom(mine))) && none.replaced.length === 1 && none.replaced[0][2] === '/#room');
    const retry = link();
    retry.last().receive(reject(6));
    retry.$('name-step-go').click();
    check('... and Join on the card asks the room again, as a link does (a Hello with no block, no new code)', retry.sockets.length === 2 && retry.$('name-step').hidden && hex(retry.last().open().sent[0]) === hex(helloBytes('Zed', 'k7m2xq', { join: true })));
    retry.last().receive(reject(6));
    check('... and if the room is still not there the card comes back again', !retry.$('name-step').hidden && retry.$('name-step-msg').textContent === NOROOM && retry.sockets.length === 2);
    const full = link();
    full.last().open().receive(reject(1));
    full.last().open().receive(reject(6));
    check('a link to a code that is full, on a server that has no place for a lobby: the page makes its own room once, that is refused too, and the strip is the busy one with its Try again button (no third socket, no card)', full.sockets.length === 2 && full.$('name-step').hidden && !full.$('banner').hidden && full.$('banner-x').textContent === 'Try again' && /server is full/.test(full.$('banner-text').textContent));
    const gone = link();
    gone.arrive({ seat: 1, key: KEY, flags: 0, seats: [[C, 'Maya'], [C, 'Zed'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    gone.last().receive(reject(6));
    const again = gone.last().open().sent[0];
    check('a link whose room took the page and is lost later (a restart) makes the room again, as before: no key, the lobby block, the same code, no card', gone.sockets.length === 2 && gone.$('name-step').hidden && hex(again) === hex(helloBytes('Zed', 'k7m2xq')), hex(again));
    const retry2 = link();
    retry2.arrive({ seat: 1, key: KEY, flags: 0, seats: [[C, 'Maya'], [C, 'Zed'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    retry2.last().receive(reject(6));
    retry2.last().open().receive(reject(6));
    check('a joined link, a restart, and the server has no place to make the room again: the busy strip with Try again', !retry2.$('banner').hidden && retry2.$('banner-x').textContent === 'Try again' && retry2.$('name-step').hidden && retry2.sockets.length === 2);
    retry2.$('banner-x').click();
    check('... a lobby frees up: Try again sends the create block (the room is made again, not asked for with a Hello that only joins) and no card appears', retry2.sockets.length === 3 && hex(retry2.last().open().sent[0]) === hex(helloBytes('Zed', 'k7m2xq')) && retry2.$('name-step').hidden);
    const lost = link();
    lost.arrive({ seat: 1, key: KEY, flags: 0, seats: [[C, 'Maya'], [C, 'Zed'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    lost.last().receive(reject(6));
    lost.last().open().receive(reject(6));
    check('a link that took a seat, lost its room and finds the server with no place for the room: the strip is the busy one (not the card with the line for a code with no room, which is for the first Hello only), and no third socket', lost.sockets.length === 2 && lost.$('name-step').hidden && !lost.$('banner').hidden && lost.$('banner-x').textContent === 'Try again' && /server is full/.test(lost.$('banner-text').textContent));
    gone.arrive({ flags: 2, seats: [[C, 'Zed'], [E, ''], [E, ''], [E, '']] });
    same('... and the strip says that the room is gone, with the page\'s own sentence', [gone.$('banner').hidden, gone.$('banner-text').textContent, gone.$('banner-x').textContent], [false, 'That room is gone, because everybody left, so this one is yours now. Send the link on if you like.', 'OK']);
    const joined = link();
    joined.arrive({ seat: 1, flags: 0, seats: [[C, 'Maya'], [C, 'Zed'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    check('a link to a room that is there joins it: no strip, the room is the host\'s', joined.$('banner').hidden && joined.$('room-h').textContent === 'Maya’s room' && joined.$('code').textContent === 'k7m 2xq');
    for (const [what, reason, sentence] of [['full', 1, 'That room is full, so this one is yours now. Send the link on if you like.'], ['whose match has started', 3, 'The match in that room has already started, so this room is yours instead. Send the link on if you like.']]) {
        const e = link();
        e.last().open().receive(reject(reason));
        const next = e.last().open().sent[0];
        check('a link to a room that is ' + what + ': the page makes a room of its own under the same name (a new code, no key), and the strip says why', e.sockets.length === 2 && CODE.test(helloRoom(next)) && helloRoom(next) !== 'k7m2xq' && hex(next) === hex(helloBytes('Zed', helloRoom(next))) && e.$('banner-text').textContent === sentence, e.$('banner-text').textContent);
        check('... and its address is the Room\'s, "/#room" (the link is no longer in the bar)', e.replaced.length === 1 && e.replaced[0][2] === '/#room');
    }
    const kicked = runLobby('', {});
    kicked.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0 } }).receive(reject(4));
    const own = kicked.last().open().sent[0];
    check('a player whom the host removed gets a room of their own under their name (the guest name, which the page kept): a new code, no key (the server forgot it), and the strip says why', kicked.sockets.length === 2 && hex(own) === hex(helloBytes(GUEST, helloRoom(own))) && CODE.test(helloRoom(own)) && kicked.$('banner-text').textContent === 'The host removed you from the room, so this one is yours now. Send the link on if you like.');
    check('... and that session entry is the old room\'s no longer (the key of the old room is let go of)', !(kicked.session.data['ants.lobby'] && JSON.parse(kicked.session.data['ants.lobby']).k === hex(KEY)));
}
{   // "Start a room of my own instead": the page's own room, and the one that this tab already had when it has one
    const env = runLobby('?room=k7m2xq', {});
    env.$('name-step-own').click();
    const hello = env.last().open().sent[0];
    check('"Start a room of my own instead" closes the card, makes a room of the visitor\'s own (a new code, the guest name) and puts the address of the Room ("/#room": no link in it) in the bar', env.$('name-step').hidden && env.sockets.length === 1 && CODE.test(helloRoom(hello)) && helloRoom(hello) !== 'k7m2xq' && hex(hello) === hex(helloBytes(GUEST, helloRoom(hello))) && env.replaced.length === 1 && env.replaced[0][2] === '/#room');
    const kept = { 'ants.lobby': JSON.stringify({ c: 'abc234', k: hex(KEY), n: 'Maya', o: 1 }) };
    const env2 = runLobby('?room=k7m2xq', {}, { session: kept });
    check('a tab that has a room of its own is asked first too, and nothing connects before the card is done (the own room waits)', !env2.$('name-step').hidden && env2.sockets.length === 0);
    env2.$('name-step-own').click();
    check('... "my own instead" takes the own room back with its key, under the name that the room showed for it (a seat keeps its name)', hex(env2.last().open().sent[0]) === hex(helloBytes('Maya', 'abc234', { key: KEY })));
    const env3 = runLobby('?room=k7m2xq', {}, { session: { 'ants.lobby': JSON.stringify({ c: 'k7m2xq', k: hex(KEY), n: 'Sam', o: 0 }) } });
    check('a reload of the tab that joined that very room takes the seat back at once: no card, the key, the name that the room showed', env3.$('name-step').hidden && hex(env3.last().open().sent[0]) === hex(helloBytes('Sam', 'k7m2xq', { key: KEY })));
    for (const bad of ['?room=a%20b', '?room=k7m%202xq', '?room=', '?room=' + 'x'.repeat(33), '?room=k7m2xq%00', '?room=..%2Fx']) {
        const e = runLobby(bad, {});
        check('an address with no room code that a room can have (' + bad.slice(0, 20) + ') is the plain page: nothing is asked, the page makes a room of its own', e.$('name-step').hidden && e.sockets.length === 1 && !e.$('lobby').hidden && e.replaced.length === 0);
    }
    check('an address with only a picture is the plain page too', runLobby('?aspect=4:3', {}).$('name-step').hidden);
}

// ---- 5.4 "Join by code" (the Games list's, web/home/games.js): the name card first, then a look at the room while the page's own room stays
// (the page loads the script after its first paint: env.advance(0); Games opens the list, whose last row is the field and its button; tests/scripts/web_home_games_room_check.js has the question that comes first when others are in the room)
const idle = async () => { for (let i = 0; i < 40; i++) await Promise.resolve(); };
async function codeBox(env) {
    env.advance(0);
    await idle();
    env.home('games').click();
    const pop = env.$('home').findAll((e) => String(e.className).split(/\s+/).indexOf('gpop') !== -1)[0];
    const part = (cls) => pop.findAll((e) => String(e.className).split(/\s+/).indexOf(cls) !== -1)[0];
    return { input: part('joinbox').children[0], go: part('gcgo'), hint: part('hint') };
}
{
    const env = runLobby('', {}, { lazy: true });
    const own = env.arrive({ key: KEY });
    const ownCode = helloRoom(own.sent[0]);
    env.roomBar('rhome').click();
    const box = await codeBox(env);
    check('Join by code is in the Games list: a field (empty, with the cursor in the list) and a button', box.input.value === '' && box.go.textContent === 'Join' && box.hint.textContent === '');
    for (const [what, typed, why] of [['nothing', '', 'Type or paste the code first.'], ['a line of blanks', '   \n', 'Type or paste the code first.'], ['text that is no code', 'not a code!', 'A room code has letters and numbers only, like k7m 2xq.'], ['a path', 'k7m2/xq', 'A room code has letters and numbers only, like k7m 2xq.'],
                                       ['an accent', 'abé', 'A room code has letters and numbers only, like k7m 2xq.'], ['33 characters', 'x'.repeat(33), 'A room code has letters and numbers only, like k7m 2xq.'], ['an address', '?room=x&seat=1', 'A room code has letters and numbers only, like k7m 2xq.']]) {
        box.input.value = typed;
        box.input.fire('input', {});
        box.go.click();
        check('typing ' + what + ' and pressing Join explains it under the box and opens no card, no socket', box.hint.textContent === why && env.$('name-step').hidden && env.sockets.length === 1, box.hint.textContent);
    }
    box.input.value = '  ' + grouped(ownCode).toUpperCase() + ' ';
    box.go.click();
    check('the code of the page\'s own room (typed as shown, in capitals) is its own: "That is your own room." and no card', box.hint.textContent === 'That is your own room.' && env.$('name-step').hidden && env.sockets.length === 1);
    box.input.value = 'K7M 2xq';
    box.input.fire('input', {});
    check('typing again takes the hint away', box.hint.textContent === '');
    box.input.key('Enter');
    check('Enter in the box asks for the name too: the card is up, titled with the code as typed in two groups, and nothing connects yet (blanks and capitals of a typed or pasted code do not matter)', !env.$('name-step').hidden && env.$('name-step-title').textContent === 'Join the room k7m 2xq' && env.$('name-step-go').textContent === 'Join' && env.$('name-step-input').value === GUEST && env.sockets.length === 1);
    check('... the code in the box stays as it was typed', box.input.value === 'K7M 2xq');
    env.$('name-step-input').value = 'Ann & <b>Bob</b>';
    env.$('name-step-go').click();
    const probe = env.last();
    check('Join looks at the room: a second socket, with the Hello of a page that only joins (no lobby block: a code with no room is answered "no such room", never made), under the guest name that the card showed (what is written into the read-only field changes nothing)', env.sockets.length === 2 && env.$('name-step').hidden && hex(probe.open().sent[0]) === hex(helloBytes(GUEST, 'k7m2xq', { join: true })), hex(probe.sent[0] || []));
    check('... the page\'s own room stays while it looks: no Leave, the socket open, the code and the link still the own room\'s', !own.closed && own.sent.length === 1 && env.$('code').textContent === grouped(ownCode) && env.$('link').value === 'https://play.test/?room=' + ownCode && !('ants.name' in env.storage.data));
    probe.receive(reject(6));
    check('a room that does not exist: the box says so, and the own room is as it was', box.hint.textContent === 'There is no room with that code. Check it, or ask the host to send the link again.' && !own.closed && env.$('code').textContent === grouped(ownCode) && env.innerHTMLWrites.length === 0);
}
{
    const typedJoin = async (typed) => {
        const e = runLobby('', {}, { lazy: true });
        e.arrive({ key: KEY });
        e.roomBar('rhome').click();
        const box = await codeBox(e);
        box.input.value = typed;
        box.go.click();
        e.$('name-step-go').click();
        e.box = box;
        return e;
    };
    for (const [typed, want] of [['k7m2xq', 'k7m2xq'], ['k7m 2xq', 'k7m2xq'], ['  k7m 2xq  ', 'k7m2xq'], ['k7m\t2xq\n', 'k7m2xq'], ['k7m 2xq', 'k7m2xq'], [' k 7 m 2 x q ', 'k7m2xq'], ['K7M 2XQ', 'k7m2xq'], ['my_room-1', 'my_room-1'], ['a', 'a'], ['--', '--'],
                                 ['a'.repeat(32), 'a'.repeat(32)], ['demo-small-2p-x7k2', 'demo-small-2p-x7k2'], ['demo-tiny-2p-t01-abcdef', 'demo-tiny-2p-t01-abcdef'], ['abc def ghi', 'abcdefghi']]) {
        const e = await typedJoin(typed);
        const hello = e.last().open().sent[0];
        check('Join takes ' + JSON.stringify(typed) + ' as the code ' + want + ' (blanks dropped, capitals lowered; nothing else is made of it, and an older, longer code is a code too): a joining Hello for it, under the guest name', e.sockets.length === 2 && hex(hello) === hex(helloBytes(GUEST, want, { join: true })) && e.box.hint.textContent === '', hex(hello));
    }
    const full = await typedJoin('k7m2xq');
    full.last().open().receive(reject(1));
    check('a room that is full: "That room is full." under the box', full.box.hint.textContent === 'That room is full.');
    const running = await typedJoin('k7m2xq');
    running.last().open().receive(reject(3));
    check('a room whose match has started: "The match in that room has already started."', running.box.hint.textContent === 'The match in that room has already started.');
    const old = await typedJoin('k7m2xq');
    old.last().open().receive(reject(2));
    check('a server that speaks another version: the strip asks for a reload', !old.$('banner').hidden && /A newer version of Ants is out/.test(old.$('banner-text').textContent) && old.$('banner-x').textContent === 'Reload');
    // the probe is welcomed: the page leaves its own room and shows that one
    const ok = await typedJoin('k7m2xq');
    const ownSocket = ok.sockets[0];
    const p = ok.last().open();
    p.receive(welcome(1, Uint8Array.from(Array.from({ length: 16 }, (_, i) => 0x60 + i)), 0));
    p.receive(roomMessage([[C, 'Maya'], [C, 'Zed'], [E, ''], [E, '']], { you: 1, leader: 0 }));
    check('a room that takes the player: the page leaves its own room (a Leave on the old socket, which closes) and shows that room: the code, the host\'s name, the link to it', ownSocket.sent.length === 2 && ownSocket.sent[1][0] === NET0.MSG.Leave && ownSocket.closed && ok.$('code').textContent === 'k7m 2xq' && ok.$('room-h').textContent === 'Maya’s room' && ok.$('link').value === 'https://play.test/?room=k7m2xq' && ok.box.input.value === '');
    same('... and this tab keeps that room (session storage), not the old one: the code, the key, the name, and that the page did not make it', JSON.parse(ok.session.data['ants.lobby']), { c: 'k7m2xq', k: hex(Uint8Array.from(Array.from({ length: 16 }, (_, i) => 0x60 + i))), n: 'Zed', o: 0 });
    p.receive(reject(6));
    const asked = ok.last().open();
    asked.receive(reject(6));
    check('a typed code whose room is lost later (a restart): the Hello that asks again has no block, and its NoSuchRoom is the busy strip, not the card of a link (only a link\'s first Hello is told "no room with that code")', ok.sockets.length === 3 && hex(ok.sockets[2].sent[0]) === hex(helloBytes('Zed', 'k7m2xq', { join: true })) && !ok.$('banner').hidden && ok.$('banner-x').textContent === 'Try again' && ok.$('name-step').hidden);
    const away = runLobby('', {}, { lazy: true });
    away.arrive({ key: KEY });
    away.roomBar('rhome').click();
    const awayBox = await codeBox(away);
    awayBox.input.value = 'k7m2xq';
    awayBox.go.click();
    away.$('name-step-own').click();
    check('"Start a room of my own instead" on that card closes it and the own room stays (no new socket)', away.$('name-step').hidden && away.sockets.length === 1 && !away.sockets[0].closed && awayBox.go.disabled === false);
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
    const want = 'https://play.test/?join=/ws&room=' + code + '&seat=0&name=Maya';
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
    same('a player\'s page, when the room says START, goes to the game page with its own seat and name', env.assigned, ['https://play.test/?join=/ws&room=' + code + '&seat=1&name=Sam']);
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
    same('START with nobody else in the room plays a game on this computer: the game page\'s own single player, on the map of the room, under the name of the player', env.assigned, ['https://play.test/play.html?map=treasure&name=Maya']);
    check('... the room is let go of (a Leave, the socket closes), the tab forgets it, and no key is written for the game page', s.sent.length === 2 && s.sent[1][0] === NET0.MSG.Leave && s.closed && env.session.data['ants.lobby'] === undefined && Object.keys(env.storage.data).every((k) => k.indexOf('ants.rejoin.') !== 0));
    const evil = runLobby('', { 'ants.name': '<b>x</b>&y' });
    evil.arrive({ key: KEY, seats: [[C, '<b>x</b>&y'], [E, ''], [E, ''], [E, '']] });
    evil.$('start').click();
    check('a name with < > & goes into that address encoded (a plain value in the query, never markup)', evil.assigned[0] === 'https://play.test/play.html?map=treasure&name=%3Cb%3Ex%3C%2Fb%3E%26y' && evil.param(evil.assigned[0], 'name') === '<b>x</b>&y', evil.assigned[0]);
}

// ---- 5.5c the rematch (front/rematch.js, docs/PLAY_IN_BROWSER.md "After a match"): the tab comes back from its match with the room's code and the key of its seat (the game page's hand-over in the
// tab's session storage); the strip "Back from the match." at the top, the colors' words, the note under START, START that the room answers, the host who comes back
{
    const roomChat = (text) => { const w = [9, 255, 0]; str8(w, text); return Uint8Array.from(w); };
    const statOf = (env, seat) => { const p = env.card(seat).findAll((e) => /\bpstat\b/.test(e.className))[0]; return p ? [p.textContent, p.className] : null; };
    const barOf = (env) => env.root.children.find((c) => c.className === 'backbar') || null;
    const handed = (name, w, t) => ({ 'ants.lobby': JSON.stringify({ c: 'k7m2xq', k: hex(KEY), n: name, o: 0 }), 'ants.back': JSON.stringify({ c: 'k7m2xq', t: t === undefined ? Date.now() - 5000 : t, m: 'TREASURE.LVL', p: ['Maya', 'Sam', '', ''], w: w }) });
    const env = runLobby('?room=k7m2xq', { 'ants.name': 'Maya' }, { session: handed('Maya', 'Maya won with 2,040.') });
    const bar = barOf(env);
    const s = env.last().open();
    check('the tab comes back from its match: no name card, the seat is taken back with the key', env.$('name-step').hidden && hex(s.sent[0]) === hex(helloBytes('Maya', 'k7m2xq', { key: KEY })));
    check('... and the strip "Back from the match." is at the top of the page, beside the frame (not in it), with the winner and the link to the matches', !!bar && !bar.hidden && bar.parentNode === env.$('screen').parentNode && bar.getAttribute('role') === 'status'
          && bar.textContent === 'Back from the match. Maya won with 2,040. Same room, same colors, same map.Stats and replay');
    s.receive(welcome(0, KEY, 0));
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0, inGame: 2 }));
    same('Sam is still on the results screen: his color says so (picture R2)', statOf(env, 1), ['Still on the results screen…', 'pstat away']);
    check('... and the note under START says that START waits for him', !env.$('start-note').hidden && env.$('start-note').textContent === 'START waits for Sam. You can wait, or press Remove on their color to start without them.');
    env.$('start').click();
    check('START goes to the room as ever (the room decides)', s.sent[s.sent.length - 1][0] === NET0.MSG.StartRequest);
    s.receive(roomChat('Sam is not back from the match yet.'));
    check('the room\'s answer is the gold strip of a START that waits', !env.$('banner').hidden && /\bwarn\b/.test(env.$('banner').className)
          && env.$('banner-text').textContent === 'Sam is not back from the match yet. START waits for them: wait, or press Remove on their color to start without them.');
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0, inGame: 0 }));
    same('Sam is back: "Back in the room", and nothing is waited for', [statOf(env, 1), env.$('start-note').hidden], [['Back in the room', 'pstat back'], true]);
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0, flags: 7 }));
    check('START: the page goes to the game page, and the match that it came back from is let go of (the strip goes, the entry too)', env.assigned.length === 1 && bar.hidden && env.session.data['ants.back'] === undefined);
}
{   // Sam came back first and leads; Maya, the host of the match, comes back: Sam reads that she is the host again
    const env = runLobby('?room=k7m2xq', { 'ants.name': 'Sam' }, { session: { 'ants.lobby': JSON.stringify({ c: 'k7m2xq', k: hex(KEY), n: 'Sam', o: 0 }), 'ants.back': JSON.stringify({ c: 'k7m2xq', t: Date.now() - 5000, m: 'TREASURE.LVL', p: ['Maya', 'Sam', '', ''], w: '' }) } });
    const s = env.last().open();
    s.receive(welcome(1, KEY, 0));
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 1, inGame: 1 }));
    check('the first one back leads while the host of the match is on the results screen (no winner: the strip says the rest)', env.$('start').disabled === false && env.$('start-note').textContent === 'START waits for Maya. You can wait, or press Remove on their color to start without them.'
          && env.root.children.find((c) => c.className === 'backbar').textContent === 'Back from the match. Same room, same colors, same map.Stats and replay');
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 0, inGame: 0 }));
    check('Maya is back and leads again: Sam reads so (not that somebody left)', env.$('banner-text').textContent === 'Maya is back, and is the host again.' && env.$('start').disabled === true);
}
{   // a match that is not this room's, or too old, shows nothing; another room that the page goes to lets it go
    const old = runLobby('?room=k7m2xq', { 'ants.name': 'Maya' }, { session: { 'ants.lobby': JSON.stringify({ c: 'k7m2xq', k: hex(KEY), n: 'Maya', o: 0 }), 'ants.back': JSON.stringify({ c: 'k7m2xq', t: Date.now() - 3 * 3600 * 1000 - 60000, m: 'TREASURE.LVL', p: ['Maya', 'Sam', '', ''], w: '' }) } });
    const other = runLobby('?room=k7m2xq', { 'ants.name': 'Maya' }, { session: { 'ants.lobby': JSON.stringify({ c: 'k7m2xq', k: hex(KEY), n: 'Maya', o: 0 }), 'ants.back': JSON.stringify({ c: 'abc234', t: Date.now(), m: 'TREASURE.LVL', p: ['Maya', 'Sam', '', ''], w: '' }) } });
    check('a match of more than three hours ago, or of another room, has no strip', [old, other].every((e) => !e.root.children.some((c) => c.className === 'backbar')));
}

// ---- 5.5b what the review of the page found: a room that waits for the games when the page first hears of it, a team that is left alone twice, the line of a move, a refusal that nothing knows
{   // a page that is answered with a room that waits for the games already (a reload, or a lost link that came back with its key during the wait) goes to its game as the others did
    const env = runLobby('', { 'ants.name': 'Sam' });
    const s = env.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0, flags: 7 } });
    const code = helloRoom(s.sent[0]);
    same('the first Room that a page hears already says START was pressed: the page goes to the game page of its seat, as if it had seen the change', env.assigned, ['https://play.test/?join=/ws&room=' + code + '&seat=1&name=Sam']);
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
    check('"Maya moved you to Blue." shows no strip (the color and the toast say it)', env.$('banner').hidden);
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
        same('pressing it takes THIS tab to the game page: the plain code, the seat, the (empty) name and the shape', env.assigned, ['https://play.test/?join=/ws&room=k7m2xq&seat=1&name=']);
        check('... with no key in the address, no create block (the seat is already in the room: the game finds its key by itself), nothing opened in another window, and nothing written to the storage', env.assigned[0].indexOf(RKEY) === -1 && noBlockOf(env, env.assigned[0]) && env.opened.length === 0 && env.storage.data['ants.rejoin.k7m2xq.1'] === stored['ants.rejoin.k7m2xq.1'] && keysOf(env).length === 1);
    }
    {
        const env = runLobby('', { 'ants.rejoin.r-1.3': entry(1000), 'ants.name': 'Maya', 'ants.aspect.v2': '4:3' });
        env.$('rejoin-go').click();
        same('the remembered shape goes with it, and no name (a name that this browser remembers from earlier typing is ignored: the key takes the seat, which the room has named)', env.assigned, ['https://play.test/?join=/ws&room=r-1&seat=3&name=&aspect=4:3']);
    }
    {
        const env = runLobby('?aspect=4:3', { 'ants.rejoin.r-1.0': entry(1000), 'ants.name': 'Maya' });
        env.$('rejoin-go').click();
        same('the shape of the address is the page\'s shape, so it is the one that goes with the button', env.assigned, ['https://play.test/?join=/ws&room=r-1&seat=0&name=&aspect=4:3']);
    }
    {
        for (const remembered of ['Zo\u00eb', 'A&B <i>', 'Maya']) {
            const env = runLobby('', { 'ants.rejoin.r-1.0': entry(1000), 'ants.name': remembered });
            env.$('rejoin-go').click();
            check('a remembered name (' + JSON.stringify(remembered) + ') is no part of the address: the name is empty', env.assigned.length === 1 && env.assigned[0] === 'https://play.test/?join=/ws&room=r-1&seat=0&name=', env.assigned[0]);
        }
    }
    {
        const env = runLobby('', { 'ants.rejoin.old-match.0': entry(4 * 3600 * 1000), 'ants.rejoin.mid-match.1': entry(2 * 3600 * 1000) });
        same('an entry older than three hours is not offered and is removed (as the game does); one a little younger is the offer (a code of another length than six is shown as it is)', [rejoinView(env).note, keysOf(env)], ['Your match in room mid-match is still running: go back to your seat.', ['ants.rejoin.mid-match.1']]);
    }
    {
        const env = runLobby('', { 'ants.rejoin.other.0': entry(1000, 'wss://other.test/ws'), 'ants.rejoin.mine.1': entry(9000) });
        same('an entry of another server is not offered and stays, even when it is newer', [/room mine /.test(rejoinView(env).note), keysOf(env).sort()], [true, ['ants.rejoin.mine.1', 'ants.rejoin.other.0']]);
    }
    {
        const env = runLobby('', { 'ants.rejoin.other.0': entry(1000, 'ws://play.test/ws'), 'ants.rejoin.bad.7': entry(1000), 'ants.rejoin.junk.1': 'not json' });
        check('only entries of the game count: another scheme, a seat that is none and a value that is no JSON show nothing', rejoinView(env).hidden === true && keysOf(env).length === 3);
    }
    {
        const env = runLobby('', { 'ants.rejoin.a.0': entry(60000), 'ants.rejoin.b.2': entry(1000), 'ants.rejoin.c.1': entry(30000) });
        same('of several entries the newest is offered, and the seat goes with it', [/room b /.test(rejoinView(env).note), (env.$('rejoin-go').click(), env.assigned.map((u) => env.param(u, 'seat')))], [true, ['2']]);
    }
    {   // a link to a room: the page is the lobby with a card over it; the way back is offered behind the card
        const env = runLobby('?room=k7m2xq', { 'ants.rejoin.r-1.1': entry(1000) });
        check('the lobby page offers the way back to a running match also when it was opened by a link (the strip is above the page, the name card over it)', rejoinView(env).hidden === false && !env.$('name-step').hidden);
    }
    for (const [label, search, asked] of [['the old address of a match to host (?map=)', '?map=tiny&players=2', false], ['... with ?play=here', '?map=tiny&players=2&play=here', false], ['the old address of a room with its create block (?room=, ?roommap=)', '?room=k7m2xq&roommap=tiny&roomseats=2', true], ['... with ?play=here', '?room=k7m2xq&roommap=tiny&roomseats=2&play=here', true]]) {
        const env = runLobby(search, { 'ants.rejoin.k7m2xq.1': entry(1000) });
        check(label + ' is the lobby now (the test room is gone): the strip offers the way back as for any visit, and the name card is there only for the link to a room', rejoinView(env).hidden === false && !env.$('lobby').hidden && env.$('name-step').hidden === !asked && env.replaced.length === 0);
        (env.win.listeners.storage || []).forEach((fn) => fn({}));
        check('... and it looks again when another tab writes to the storage', rejoinView(env).hidden === false);
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

// ---- 5.8 a name that the game would not take never reaches a room, whichever way it was asked for: a login's name is made to fit, and when it cannot be, the other name (the username) is used, else the guest name
for (const [what, you, want] of [['a computer player\'s name as the display name', { name: 'Bot (Medium)', other: 'maya_99' }, 'maya_99'], ['the same with blanks and capitals', { name: ' bOt(x', other: 'maya_99' }, 'maya_99'],
                                 ['an accent (made to fit)', { name: 'Zo\u00eb', other: 'zoe_1' }, 'Zoe'], ['a name in another script', { name: '\u540d\u524d', other: 'user_name' }, 'user_name'],
                                 ['a name of 33 characters', { name: 'x'.repeat(33), other: 'maya_99' }, 'maya_99'], ['a control character in it (dropped)', { name: 'a\u0001b', other: 'maya_99' }, 'ab'],
                                 ['a name that cannot fit and no other name', { name: '\u540d\u524d' }, GUEST]]) {
    const me = { login: true, you: Object.assign({ mod: false }, you) };
    const link = runLobby('?room=k7m2xq', {}, { me });
    check('a link, with ' + what + ': the card shows ' + JSON.stringify(want) + ', which nameCheck takes, and no line of complaint', link.$('name-step-input').value === want && nameCheck(want).ok && link.sockets.length === 0 && link.$('name-step-msg').textContent === '' && link.$('name-step-input').getAttribute('aria-invalid') !== 'true');
    link.$('name-step-go').click();
    check('... Join plays under it: the Hello has that name', link.sockets.length === 1 && helloName(link.last().open().sent[0]) === want);
    const typed = runLobby('', {}, { lazy: true, me });
    typed.arrive();
    typed.roomBar('rhome').click();
    const typedBox = await codeBox(typed);
    typedBox.input.value = 'ABC-1';
    typedBox.go.click();
    check('... the card of a typed code shows the same name, and nothing is looked at before the button', typed.$('name-step-input').value === want && typed.sockets.length === 1 && typed.$('name-step-msg').textContent === '' && !typed.$('name-step').hidden);
}

// ---- the page opens no window and writes no markup (a name is only ever text: section 5.2 and the markup checks above)
check('the front page opens no window of its own (the test room of "More ways to play", which did, is gone)', (lobbyText.match(/window\.open\(/g) || []).length === 0);

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
    same('/stats answers: the line is shown, the dot is green, the numbers are the example\'s', statsView(env), { hidden: false, dot: 'live on', live: '3 matches being played · 7 players online', played: '1,284 games played (21 today)', title: 'Counted since 2026-10-04. Today means the last 24 hours.' });
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
{   // the lobby has the line too (it is the footer's)
    const lobby = runLobby('', {}, { fetch: answers({ '/stats': GOOD_STATS }) });
    await settle();
    check('the lobby has the line', !lobby.$('lobby').hidden && statsView(lobby).hidden === false);
}

})().then(() => {
    console.log('web name check: ' + checks + ' checks, ' + failures + ' failures');
    process.exit(failures === 0 ? 0 : 1);
}, (e) => {
    console.log('FAIL ' + e.message + '\n' + e.stack);
    process.exit(1);
});
