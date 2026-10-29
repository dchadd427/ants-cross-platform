// Runs the front page's TWO SCREENS, Home and the Room (web/home/home.js with web/lobby.html's own script, docs/HOME_SCREEN.md), in the fake browser of tests/scripts/web_page_harness.js: the pure parts
// on tables, then the page as a whole against a scripted game server:
//   1. the pure parts: the count on My room, the screen that an address and a history entry name, the screen that a page opens on, the words of the Yard's size, and the links of the Menu against the page's
//      own (the footer's, the header's);
//   2. a plain visit: Home is up and no room is made (no socket, no history written); the buttons that later pieces light are disabled, the login chip is not there, the Room's slim bar is made;
//   3. My room: the first press makes a private room and shows it ("/#room"), the next ones only show it; Home, Back and Forward never touch the socket, the room or the seat; a link opens the Room;
//      a reload keeps the screen and the seat; Leave room leaves the room and makes the next one a new one;
//   4. START while a guest sits on Home (the highest risk): the hidden Room takes the hand-off (the key is written, the game page opens) and Home says "Opening the game" at once;
//   5. the notices that stay global (the Rejoin strip, a refusal) with Home up, the Menu (its links, its numbers, Escape, a press elsewhere), the map list's inert, and what Home never does.
// tests/scripts/test_web_home_screen.py runs this with node (the quick tier). usage: node web_home_screen_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
globalThis.AntsShapes = require(path.resolve(__dirname, '..', '..', 'web', 'shapes.js'));

const lobbyPath = process.argv[2];
if (!lobbyPath) { console.log('usage: web_home_screen_check.js lobby.html'); process.exit(2); }
const lobbyDir = path.dirname(path.resolve(lobbyPath));
const Home = require(path.join(lobbyDir, 'home', 'home.js'));
const H = require('./web_page_harness.js')(lobbyPath);
const { NET0, KEY, E, C, hex, roomMessage, helloBytes, helloName, helloRoom, CODE, THROWS, runLobby, lobbyText } = H;

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
const slot = (kind) => ({ kind });

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The pure parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
same('the count of My room: persons and computer players of the colors that are in the match', [
    Home.countOf({ slots: [slot('person'), slot('person'), slot('bot'), slot('open')] }), Home.countOf({ slots: [slot('person'), slot('open'), slot('open'), slot('open')] }),
    Home.countOf({ slots: [slot('person'), slot('nobody'), slot('nobody'), slot('bot')] }), Home.countOf({ slots: [slot('person'), slot('person'), slot('person'), slot('person')] })],
    [{ people: 3, size: 4 }, { people: 1, size: 4 }, { people: 2, size: 2 }, { people: 4, size: 4 }]);
same('no count before the room has told its plan (no model, a model of another shape)', [Home.countOf(null), Home.countOf({}), Home.countOf({ slots: [slot('person')] })], [null, null, null]);
same('the screen of an address and a history entry: the entry that Home wrote wins, else the hash; the Room only with a room to show',
    [Home.screenOf({ ants: 'room' }, '', true), Home.screenOf({ ants: 'home' }, '#room', true), Home.screenOf(null, '#room', true), Home.screenOf(null, '', true), Home.screenOf({ ants: 'room' }, '#room', false),
     Home.screenOf({ ants: 'other' }, '#room', true), Home.screenOf('room', '', true), Home.screenOf({}, '#nope', true)],
    ['room', 'home', 'room', 'home', 'home', 'room', 'home', 'home']);
same('the screen that a page opens on: a link (or a test room) opens the Room; a reload keeps the screen of the address when the tab holds a seat; else Home',
    [Home.firstScreen(true, false, ''), Home.firstScreen(true, true, ''), Home.firstScreen(false, true, '#room'), Home.firstScreen(false, false, '#room'), Home.firstScreen(false, true, ''), Home.firstScreen(false, false, '')],
    ['room', 'room', 'room', 'home', 'home', 'home']);
same('the words of the Yard\'s size', Home.reservedText(1280, 721.4), 'reserved: 1280 x 721 px (everything under the top bar)');
same('the address of the Room is the hash #room', Home.HASH, '#room');

// The Menu's links are the page's own: the footer's, the header's (Matches, Discord), the Map maker's and How it works (a button, not a link)
{
    const footer = lobbyText.slice(lobbyText.indexOf('<nav aria-label="Footer links">'), lobbyText.indexOf('</nav>', lobbyText.indexOf('<nav aria-label="Footer links">')));
    const links = [...footer.matchAll(/<a href="([^"]+)"[^>]*>([^<]*)<\/a>/g)].map((m) => [m[2], m[1]]);
    const header = lobbyText.slice(lobbyText.indexOf('<header class="mast">'), lobbyText.indexOf('</header>'));
    const discord = /<a class="btn sm discord" href="([^"]+)"/.exec(header)[1];
    const matches = /<a class="btn sm" href="([^"]+)" id="watch-top"/.exec(header)[1];
    const mapMaker = /link\.href = '([^']+)'/.exec(fs.readFileSync(path.join(lobbyDir, 'front', 'map_maker_link.js'), 'utf8'))[1];
    const want = (text) => links.filter((l) => l[0] === text).map((l) => l[1])[0];
    same('the Menu has the links of the header and the footer, in the order of the picture: Matches, Live now, Map maker, How it works (a button), Discord server',
        Home.MENU_MAIN, [['Matches', matches], ['Live now', want('Live')], ['Map maker', mapMaker], ['How it works', ''], ['Discord server', discord]].map((m) => (m[0] === 'Live now' ? ['Live now', want('Live now')] : m)));
    same('... and the small links are the footer\'s own, in its order', Home.MENU_SMALL, ['Sprites and sounds', 'Changelog', 'GitHub', 'Feedback', 'Legal'].map((t) => [t, want(t)]));
    check('... and the footer has a button for How it works (the Menu opens the page\'s own list)', /<button class="lnk" type="button" id="how-open">How it works<\/button>/.test(footer));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. A plain visit: Home, no room
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const classes = (e) => String(e.className).split(/\s+/);
const buttonsOf = (env) => env.$('home').findAll((e) => e.tagName === 'button');
{
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    check('a plain visit opens on Home (body data-screen="home")', env.screen() === 'home');
    check('... and makes no room: no socket, nothing in this tab\'s session storage', env.sockets.length === 0 && env.session.data['ants.lobby'] === undefined);
    check('... and writes nothing to the history (the address stays what it is until a button is pressed), and moves nothing (no scroll, no focus: the first look is not a press)', env.pushed.length === 0 && env.replaced.length === 0 && env.marked.length === 0 && env.entries.length === 1 && env.scrolled.length === 0 && env.focused === null);
    const bar = env.$('home').children[0];
    same('Home is a top bar, the Yard and the card that says that the game is opening, in that order', env.$('home').children.map((c) => c.tagName + '#' + (c.id || classes(c)[0])), ['header#hbar', 'main#yard', 'div#hopen']);
    same('the bar has the logo (in the page\'s heading), Quick host, Quick join, My room, Games and the right-hand group (the login chip and the Menu)', bar.children.map((c) => classes(c).slice(0, 2).join(' ')), ['hbrand', 'btn quick', 'btn quick', 'btn hchip', 'btn hchip', 'hright']);
    const quickHost = env.home('host'), quickJoin = env.home('join'), mine = env.home('mine'), games = env.home('games');
    same('the words of the buttons are the picture\'s', [quickHost.textContent, quickJoin.textContent, mine.children[0].textContent, games.textContent], ['Quick host', 'Quick join', 'My room', 'Games']);
    check('Quick host, Quick join and Games are drawn and take no press until the pieces that light them are in (disabled, and said so to a screen reader)', [quickHost, quickJoin, games].every((b) => b.disabled === true && b.getAttribute('aria-disabled') === 'true'));
    quickHost.click(); quickJoin.click(); games.click();
    check('... a press on one of them does nothing (no socket, no new screen, no history)', env.sockets.length === 0 && env.screen() === 'home' && env.pushed.length === 0);
    check('My room is the one button of the bar that is on, and it has no count before there is a room', mine.disabled === false && mine.children[1].hidden === true && mine.children[1].textContent === '' && mine.getAttribute('aria-label') === 'My room');
    const login = env.home('hlogin');
    check('the login chip is made for the piece that comes later, and is not shown', !!login && login.hidden === true);
    check('every button that Home makes is a plain button (type="button": nothing submits), and Home put no markup into the page', buttonsOf(env).concat(env.roomBar('rhome'), env.roomBar('leave')).every((b) => b.getAttribute('type') === 'button' || b.type === 'button') && env.innerHTMLWrites.length === 0);
    const logo = env.$('home').findAll((e) => e.tagName === 'img')[0];
    check('the logo is the page\'s own picture, at the size of its file, with its words', logo.src === 'front/logo.png' && logo.getAttribute('width') === '581' && logo.getAttribute('height') === '218' && logo.alt === 'Ants!');
    const yard = env.$('home').children[1];
    check('the Yard\'s container is #yard, and it holds the stand-in: the label, the tag and five ants (three of them yours, the others a red and a blue), none of them read out', yard.id === 'yard' && yard.findAll((e) => classes(e).indexOf('yant') !== -1).length === 5
          && yard.findAll((e) => classes(e).indexOf('ytag') !== -1)[0].textContent === 'Your 3 ants' && yard.findAll((e) => classes(e).indexOf('ant-red') !== -1).length === 1 && yard.findAll((e) => classes(e).indexOf('ant-blue') !== -1).length === 1
          && yard.findAll((e) => classes(e).indexOf('yant') !== -1).every((a) => a.getAttribute('aria-hidden') === 'true'));
    check('the label says that the Yard will live here and prints the size that it has', /The Yard will live here/.test(yard.textContent) && /reserved: \d+ x \d+ px \(everything under the top bar\)/.test(yard.textContent));
    const opening = env.$('home').children[2];
    check('the card that says that the game is opening is there and not shown, as a status', opening.hidden === true && opening.getAttribute('role') === 'status' && opening.textContent === 'Opening the game…');
    // the Room's slim bar: Home, the logo, Leave room, Menu, before the page's own room card
    const rbar = env.root.children.filter((c) => classes(c).indexOf('rbar') !== -1)[0];
    same('the Room has a slim bar before its card: Home, the logo, a spacer, Leave room, Menu', rbar.children.map((c) => c.tagName + ':' + (c.textContent || c.getAttribute('aria-label') || classes(c)[0])), ['button:‹Home', 'h1:hbrand', 'span:grow', 'button:Leave room', 'button:Menu']);
    check('... and the Menu button has its name and says whether it is open (it promises no other kind of popup)', rbar.children[4].getAttribute('aria-label') === 'Menu' && rbar.children[4].getAttribute('aria-haspopup') === null && rbar.children[4].getAttribute('aria-expanded') === 'false');
    check('each bar has the page\'s heading, an h1 around the logo (the screen that shows has one), and the chevron of the Home button is not read out', [bar, rbar].every((b) => b.children[b === bar ? 0 : 1].tagName === 'h1' && b.children[b === bar ? 0 : 1].children[0].tagName === 'img') && rbar.children[0].children[0].getAttribute('aria-hidden') === 'true');
}
{   // without the script of Home the page cannot do anything, and says so (as for any of its scripts)
    const env = runLobby('', {}, { noHome: true });
    check('a page whose home/home.js did not come says that it could not load all of its parts, and makes no room', !env.$('banner').hidden && /could not load all of its parts/.test(env.$('banner-text').textContent) && env.sockets.length === 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. My room, Home, Back and Forward, a link, a reload, Leave room
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    env.home('mine').click();
    check('My room makes a private room at once: one socket, a Hello that makes the room (a new code, the lobby block, the remembered name, no key)', env.sockets.length === 1
          && hex(env.last().open().sent[0]) === hex(helloBytes('Maya', helloRoom(env.last().sent[0]))) && CODE.test(helloRoom(env.last().sent[0])));
    check('... and shows the Room: data-screen="room", the address /#room, one new history entry (the first entry is marked as Home, its address as it was)', env.screen() === 'room' && env.win.location.hash === '#room'
          && env.entries.length === 2 && env.entries[1].url === '/#room' && JSON.stringify(env.entries[1].state) === '{"ants":"room"}' && JSON.stringify(env.entries[0].state) === '{"ants":"home"}' && env.entries[0].url === '/' && env.replaced.length === 0);
    check('... the page is scrolled to the top and the focus is on the Home button (the button that was pressed is not on this screen)', env.scrolled.length >= 1 && env.focused === env.roomBar('rhome'));
    const s = env.arrive({ key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { plan: [0, 0, 0, 2] } });
    same('the chip counts the colors that play: Maya, Sam and the computer player of the fourth color, of the four', [env.home('mine').children[1].textContent, env.home('mine').children[1].hidden, env.home('mine').getAttribute('aria-label')], ['3/4', false, 'My room, 3 of 4']);
    const code = helloRoom(s.sent[0]);
    const sent = s.sent.length;
    env.roomBar('rhome').click();
    check('Home shows Home: data-screen="home", the address "/", a new history entry', env.screen() === 'home' && env.win.location.hash === '' && env.entries.length === 3 && env.entries[2].url === '/' && env.focused === env.home('mine'));
    check('... and the room is not touched: the same socket, open, nothing sent, nobody left, the seat is still in the session storage', env.sockets.length === 1 && !s.closed && s.sent.length === sent && s.sent.every((b) => b[0] !== NET0.MSG.Leave) && !!env.session.data['ants.lobby']);
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [C, 'Tess'], [E, '']], { you: 0, leader: 0, plan: [0, 0, 0, 2] }));
    check('a room that changes while Home is up is followed: the chip says 4/4', env.home('mine').children[1].textContent === '4/4' && env.card(2).textContent.indexOf('Tess') !== -1);
    env.home('mine').click();
    check('My room, pressed again, only shows the Room: no second socket, no second room', env.screen() === 'room' && env.sockets.length === 1 && env.entries.length === 4 && env.entries[3].url === '/#room');
    check('Back from the Room is Home (the browser\'s Back goes to the entry before): the screen, the address, and the room still held', env.back() && env.screen() === 'home' && env.win.location.hash === '' && env.sockets.length === 1 && !s.closed && s.sent.every((b) => b[0] !== NET0.MSG.Leave));
    check('Forward is the Room again, with no new socket', env.forward() && env.screen() === 'room' && env.win.location.hash === '#room' && env.sockets.length === 1);
    env.back(); env.back(); env.back();
    check('Back to the first entry is Home (it was marked as Home when the first button was pressed), and Back past it goes nowhere', env.screen() === 'home' && env.win.location.hash === '' && env.back() === false && env.sockets.length === 1);
    check('the code of the room is the same all through, and the link on the Room is its link', env.$('code').textContent === code.slice(0, 3) + ' ' + code.slice(3) && env.$('link').value === 'https://play.test/?room=' + code);
}
{   // a link opens the Room directly; Home from there marks the first entry as the Room, and Back comes back to the link
    const env = runLobby('?room=k7m2xq', { 'ants.name': 'Maya' });
    check('a link to a room (?room=) opens on the Room, behind the name card, with nothing written to the history', env.screen() === 'room' && !env.$('name-step').hidden && env.pushed.length === 0 && env.replaced.length === 0 && env.marked.length === 0);
    env.$('name-step-go').click();
    env.arrive({ seat: 1, key: KEY, seats: [[C, 'Sam'], [C, 'Maya'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    check('after Join the Room is the guest\'s: no Home button was pressed, so the address is still the link', env.screen() === 'room' && env.win.location.search === '?room=k7m2xq' && env.entries.length === 1);
    env.roomBar('rhome').click();
    check('Home from the link: the first entry is marked as the Room with its address unchanged (the link), and Home is a new entry at "/"', env.screen() === 'home' && env.entries.length === 2 && env.entries[0].url === '/?room=k7m2xq' && JSON.stringify(env.entries[0].state) === '{"ants":"room"}' && env.entries[1].url === '/' && env.marked.length === 1 && env.replaced.length === 0);
    check('Back from there is the Room again, at the link', env.back() && env.screen() === 'room' && env.win.location.search === '?room=k7m2xq' && env.sockets.length === 1);
}
{   // a reload: the screen of the address, the seat of the tab
    const seat = { 'ants.lobby': JSON.stringify({ c: 'k7m2xq', k: hex(KEY), n: 'Maya', o: 1 }) };
    const onHome = runLobby('', { 'ants.name': 'Maya' }, { session: seat });
    check('a reload on Home: Home, and the room is taken back in the background (one socket, a Hello with the key of the seat, no card)', onHome.screen() === 'home' && onHome.sockets.length === 1 && onHome.$('name-step').hidden
          && hex(onHome.last().open().sent[0]) === hex(helloBytes('Maya', 'k7m2xq', { key: KEY })));
    const t = onHome.arrive({ key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']] });
    check('... and My room says how many are in it once the room has answered, and shows the room at once (no new socket)', onHome.home('mine').children[1].textContent === '2/4' && (onHome.home('mine').click(), onHome.screen() === 'room' && onHome.sockets.length === 1 && !t.closed));
    const onRoom = runLobby('', { 'ants.name': 'Maya' }, { session: seat, hash: '#room' });
    check('a reload on the Room (/#room, the tab holds a seat): the Room, and the seat is taken back with its key', onRoom.screen() === 'room' && onRoom.sockets.length === 1 && hex(onRoom.last().open().sent[0]) === hex(helloBytes('Maya', 'k7m2xq', { key: KEY })));
    const orphan = runLobby('', { 'ants.name': 'Maya' }, { hash: '#room', stayHome: true });
    check('/#room in a tab that holds no seat (a link that was copied) is Home: there is no room to show, and none is made', orphan.screen() === 'home' && orphan.sockets.length === 0);
}
{   // Leave room: the room is left, and the next My room is a new room
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    env.home('mine').click();
    const s = env.arrive({ key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']] });
    const first = helloRoom(s.sent[0]);
    env.roomBar('leave').click();
    check('Leave room leaves the room (one Leave message), shows Home, and forgets the seat: the session storage has no room', s.sent.filter((b) => b[0] === NET0.MSG.Leave).length === 1 && env.screen() === 'home' && env.session.data['ants.lobby'] === undefined);
    same('... the chip is back to "My room" with no count', [env.home('mine').children[0].textContent, env.home('mine').children[1].hidden], ['My room', true]);
    check('... and the page makes no room by itself: no new socket', env.sockets.length === 1);
    env.home('mine').click();
    check('My room then makes a NEW room (another code, a new socket, a Hello that makes it)', env.sockets.length === 2 && env.screen() === 'room' && helloRoom(env.last().open().sent[0]) !== first && CODE.test(helloRoom(env.last().sent[0])));
    check('a reload after Leave room opens Home with no room', runLobby('', { 'ants.name': 'Maya' }, { session: env.session.data, stayHome: true }).screen() === 'home' && true);
    const again = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    again.home('mine').click(); again.arrive({ key: KEY });
    again.roomBar('leave').click();
    again.back();
    check('Back after Leave room shows Home (there is no room for the Room to show)', again.screen() === 'home');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. START while a guest sits on Home: the hidden Room takes the hand-off
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = runLobby('?room=k7m2xq', { 'ants.name': 'Sam', 'ants.rejoin.old-room.1': '{"k":"0f1e2d3c4b5a69788796a5b4c3d2e1f0","s":"wss://play.test/ws","t":' + (Date.now() - 600000) + '}' });
    env.$('name-step-go').click();
    const s = env.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    env.roomBar('rhome').click();
    check('the guest is on Home, in the room (the socket is open, the room is welcomed)', env.screen() === 'home' && !s.closed && env.$('start').textContent === 'Waiting for Maya');
    check('... the Rejoin strip of an older match is shown on Home (it is above both screens)', env.$('rejoin').hidden === false);
    check('... and nobody says that the game is opening yet', env.$('home').children[2].hidden === true && env.assigned.length === 0);
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 0, flags: 7 }));
    same('the host presses START: the hidden Room takes the hand-off, and this tab goes to the game page with its own seat and name', env.assigned, ['https://play.test/?join=/ws&room=k7m2xq&seat=1&name=Sam']);
    const entry = JSON.parse(env.storage.data['ants.rejoin.k7m2xq.1'] || 'null');
    check('... the key of the seat is written for the game page (ants.rejoin.<room>.<seat> = {"k", "s", "t"}), and it is nowhere else', !!entry && Object.keys(entry).sort().join() === 'k,s,t' && entry.k === hex(KEY) && entry.s === 'wss://play.test/ws'
          && env.assigned[0].indexOf(hex(KEY)) === -1 && env.dumpAll().indexOf(hex(KEY)) === -1);
    check('... Home says "Opening the game…" (the card is shown, as a status), and stays Home: nothing is switched behind the person', env.$('home').children[2].hidden === false && env.$('home').children[2].textContent === 'Opening the game…' && env.screen() === 'home');
    check('... the Rejoin strip is gone (the page is on its way), the socket stays open for the game to take over, and the tab forgets its room', env.$('rejoin').hidden === true && !s.closed && s.sent.every((b) => b[0] !== NET0.MSG.Leave) && env.session.data['ants.lobby'] === undefined);
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 0, flags: 7 }));
    check('and what the room says after that is not heard (one hand-over only: the tab keeps no room in its session storage, as the page has gone)', env.assigned.length === 1 && env.session.data['ants.lobby'] === undefined);
}
{   // the same when the page was welcomed to a room that already waits for the games (a reload on Home after START), and when the Room is the screen (the Room says it itself; Home's card stays out of it)
    const seat = { 'ants.lobby': JSON.stringify({ c: 'k7m2xq', k: hex(KEY), n: 'Sam', o: 0 }) };
    const env = runLobby('', { 'ants.name': 'Sam' }, { session: seat });
    const s = env.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0, flags: 7 } });
    check('a reload on Home whose room already waits for the games: the hand-off is taken from the first Room message', env.screen() === 'home' && env.assigned.length === 1 && env.$('home').children[2].hidden === false && !!env.storage.data['ants.rejoin.k7m2xq.1'] && !s.closed);
    const room = runLobby('?room=k7m2xq', { 'ants.name': 'Sam' });
    room.$('name-step-go').click();
    const t = room.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    t.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 0, flags: 7, inGame: 0 }));
    check('on the Room the hand-off is the same, and Home\'s card is not shown (the Room\'s own cards say it)', room.assigned.length === 1 && room.screen() === 'room' && room.$('home').children[2].hidden === true && /Opening the game/.test(room.card(1).textContent));
    // a host on Home: its page gets the room's word too (START was pressed by it earlier, or from another window)
    const host = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    host.home('mine').click();
    const h = host.arrive({ key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']] });
    host.roomBar('rhome').click();
    h.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0, flags: 7 }));
    check('the leader\'s page on Home takes the hand-off the same way', host.assigned.length === 1 && host.$('home').children[2].hidden === false && !!host.storage.data['ants.rejoin.' + helloRoom(h.sent[0]) + '.0']);
}
{   // START given up (the games did not come in time): everybody is back in the room, and a guest on Home is not moved
    const env = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    env.home('mine').click();
    const s = env.arrive({ key: KEY, seats: [[C, 'Sam'], [E, ''], [E, ''], [E, '']] });
    env.roomBar('rhome').click();
    s.receive(roomMessage([[C, 'Sam'], [C, 'Maya'], [E, ''], [E, '']], { you: 0, leader: 0 }));
    check('a room that does not wait for the games does not move a page on Home: no hand-off, no card', env.assigned.length === 0 && env.$('home').children[2].hidden === true && env.screen() === 'home');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. The strips that stay global, a refusal, the Menu, the map list's inert, and what Home never does
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const entry = (age) => '{"k":"0f1e2d3c4b5a69788796a5b4c3d2e1f0","s":"wss://play.test/ws","t":' + (Date.now() - age) + '}';
    const env = runLobby('', { 'ants.rejoin.r-1.1': entry(1000), 'ants.name': 'Maya' }, { stayHome: true });
    check('the Rejoin strip is on Home too (above both screens): shown, with its words and its button', env.screen() === 'home' && env.$('rejoin').hidden === false && /room r-1/.test(env.$('rejoin-note').textContent) && env.$('rejoin-go').textContent === 'Rejoin it');
    env.$('rejoin-go').click();
    check('... and its button goes to the game page from Home', env.assigned.length === 1 && /\?join=\/ws&room=r-1&seat=1&name=Maya$/.test(env.assigned[0]));
}
{   // a room that refuses the page while it is on Home: a room of its own and a note, and Home stays
    const env = runLobby('', { 'ants.name': 'Maya' }, { session: { 'ants.lobby': JSON.stringify({ c: 'k7m2xq', k: hex(KEY), n: 'Maya', o: 0 }) } });
    const first = env.last();
    first.open().receive(Uint8Array.from([3, NET0.REJECT.MatchRunning]));
    check('a refusal while Home is up (the match of that room runs): the page makes a room of its own and says why in the strip above Home; the screen is not changed behind the person', env.screen() === 'home' && env.sockets.length === 2 && !env.$('banner').hidden && env.win.location.hash === '');
}
{
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    env.$('stats').hidden = false; env.$('stats-live').textContent = '3 matches being played · 7 players online'; env.$('game-version').textContent = 'v0.12.150';
    const menuButton = env.$('home').findAll((e) => classes(e).indexOf('hmenu') !== -1)[0];
    menuButton.click();
    const box = env.body.findAll((e) => classes(e).indexOf('hmenubox') !== -1)[0];
    check('the Menu opens under the bar, as a list of links named Menu, and the button says that it is open', !!box && box.hidden === false && menuButton.getAttribute('aria-expanded') === 'true' && box.getAttribute('aria-label') === 'Menu');
    same('... Matches, Live now, Map maker, How it works, Discord server, then the small links', box.children.slice(0, 5).map((c) => c.textContent).concat(box.children[5].children.map((c) => c.textContent)),
        ['Matches', 'Live now', 'Map maker', 'How it works', 'Discord server', 'Sprites and sounds', 'Changelog', 'GitHub', 'Feedback', 'Legal']);
    same('... then the live numbers and the version, as the footer has them', box.children[6].textContent, '3 matches being played · 7 players online\nVersion v0.12.150');
    check('the links of the Menu that leave the site open in a new tab without handing over the page; the ones of the site do not', box.children[4].getAttribute('target') === '_blank' && box.children[4].getAttribute('rel') === 'noopener noreferrer' && box.children[0].getAttribute('target') === null);
    env.doc.listeners.keydown.forEach((fn) => fn({ key: 'Escape' }));
    check('Escape puts the Menu away and the focus goes back to its button', box.hidden === true && menuButton.getAttribute('aria-expanded') === 'false' && env.focused === menuButton);
    menuButton.click();
    env.doc.listeners.click.forEach((fn) => fn({ target: env.$('start') }));
    check('a press anywhere else puts it away too, and a press on the button toggles it', box.hidden === true && (menuButton.click(), box.hidden === false) && (menuButton.click(), box.hidden === true));
    menuButton.click();
    env.doc.listeners.click.forEach((fn) => fn({ target: box.children[0] }));
    check('a press inside the Menu is not "elsewhere" (the link takes it away itself)', box.hidden === false);
    box.children[0].click();
    check('a link of the Menu closes it', box.hidden === true);
    menuButton.click();
    box.children[3].click();
    check('How it works closes the Menu and opens the page\'s own list', box.hidden === true && env.$('how').hidden === false);
    env.$('how-close').click();
    check('... and when the list is closed the focus is back on the Menu button (the page\'s list gives it back to what had it)', env.$('how').hidden === true && env.focused === menuButton);
    // the numbers are left out when the footer has none
    const bare = runLobby('', {}, { stayHome: true });
    bare.$('home').findAll((e) => classes(e).indexOf('hmenu') !== -1)[0].click();
    const bareBox = bare.body.findAll((e) => classes(e).indexOf('hmenubox') !== -1)[0];
    check('a page that has no numbers yet (and no build) says nothing under the links', bareBox.children[6].hidden === true && bareBox.children[6].textContent === '');
    // from the Room the Menu is the same box under the Room's bar
    const room = runLobby('', {});
    room.roomBar('hmenu').click();
    const roomBox = room.body.findAll((e) => classes(e).indexOf('hmenubox') !== -1)[0];
    check('the Menu of the Room is the same list, opened by the Room bar\'s button', !!roomBox && roomBox.hidden === false && room.roomBar('hmenu').getAttribute('aria-expanded') === 'true');
    room.roomBar('rhome').click();
    check('going to Home puts the Menu away', roomBox.hidden === true);
}
{   // the list of maps is a dialog: everything behind it is inert, Home's bars too (the page calls inert(on) of Home from its own; a second mount here, on a fresh host, shows what it does)
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    const host = new H.El(env, 'div', 'home2'), lobby = new H.El(env, 'div', 'lobby2');
    lobby.parent = env.root;
    const second = env.win.AntsHome.mount({ doc: env.doc, win: env.win, host, lobby, page: { info: () => ({ client: false, model: null, offline: 'none' }), make() {}, leave() {}, how() {} } });
    const bars = host.findAll((e) => e.tagName === 'header').concat(env.root.children.filter((c) => classes(c).indexOf('rbar') !== -1 && c !== undefined).slice(-1));
    check('(before: no bar is inert)', bars.length === 2 && bars.every((b) => !b.hasAttribute('inert')));
    second.inert(true);
    check('a dialog over the page makes both bars inert: no key and no tap reaches them', bars.every((b) => b.hasAttribute('inert')));
    second.inert(false);
    check('... and the dialog closing gives them back', bars.every((b) => !b.hasAttribute('inert')));
}
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 6. After the review: the hand-off takes no press, the Menu fits a short window, a second press, the address in the odd cases, a sandboxed frame, a test room, a dialog
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{   // Leave room, the Home button and the Menu take no press while the page is on its way to the game: a Leave would take the seat out from under the game that is about to take it over
    const env = runLobby('?room=k7m2xq', { 'ants.name': 'Sam' });
    env.$('name-step-go').click();
    const s = env.arrive({ seat: 1, key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], room: { you: 1, leader: 0 } });
    const rbar = env.root.children.filter((c) => classes(c).indexOf('rbar') !== -1)[0];
    check('(before START: no bar is inert, and Leave room is on)', !rbar.hasAttribute('inert') && !env.home('hbar').hasAttribute('inert'));
    env.roomBar('hmenu').click();
    const menuBox = env.body.findAll((e) => classes(e).indexOf('hmenubox') !== -1)[0];
    check('(the Menu is open when START comes)', menuBox.hidden === false);
    s.receive(roomMessage([[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 0, flags: 7 }));
    check('START: both bars are inert (no press reaches My room, Home, Leave room or the Menu), and the Menu is put away', rbar.hasAttribute('inert') && env.home('hbar').hasAttribute('inert') && menuBox.hidden === true && env.assigned.length === 1);
    env.roomBar('leave').click();
    check('a Leave room that reaches the page all the same sends NO Leave: the seat stays for the game to take over (the socket is open, the key is written)', s.sent.every((b) => b[0] !== NET0.MSG.Leave) && !s.closed && !!env.storage.data['ants.rejoin.k7m2xq.1']);
    const host = new H.El(env, 'div', 'home3'), lobby = new H.El(env, 'div', 'lobby3');
    lobby.parent = env.root;
    const gone = env.win.AntsHome.mount({ doc: env.doc, win: env.win, host, lobby, page: { info: () => ({ client: false, model: null, offline: 'none' }), make() {}, leave() {}, how() {} } });
    const bars = host.findAll((e) => e.tagName === 'header').concat(env.root.children.filter((c) => classes(c).indexOf('rbar') !== -1).slice(-1));
    gone.opening();
    gone.inert(false);
    check('a dialog that closes after START (the map list gives the bars back) does not free them: the page is on its way', bars.length === 2 && bars.every((b) => b.hasAttribute('inert')));
}
{   // the Menu is as tall as what the window leaves under the bar, and scrolls inside itself (a phone held sideways is 360 to 390 px high)
    const env = runLobby('', {}, { stayHome: true, innerHeight: 390 });
    const button = env.$('home').findAll((e) => classes(e).indexOf('hmenu') !== -1)[0];
    button.click();
    const box = env.body.findAll((e) => classes(e).indexOf('hmenubox') !== -1)[0];
    same('the Menu opens at the bar\'s foot and is no taller than the window under it (390 - 106 - 10 px)', [box.style.top, box.style.maxHeight], ['106px', '274px']);
    env.win.innerHeight = 300;
    (env.win.listeners.resize || []).forEach((fn) => fn({}));
    same('the window turns or shrinks while the Menu is open: its height follows', box.style.maxHeight, '184px');
    env.win.innerHeight = 100;
    (env.win.listeners.resize || []).forEach((fn) => fn({}));
    same('... and it never gets smaller than 120 px', box.style.maxHeight, '120px');
    button.click();
    env.win.innerHeight = 360;
    (env.win.listeners.resize || []).forEach((fn) => fn({}));
    check('a Menu that is closed is not measured again', box.hidden === true && box.style.maxHeight === '120px');
    env.roomBar('hmenu');
    const room = runLobby('', {}, { innerHeight: 375 });
    room.roomBar('hmenu').click();
    const roomBox = room.body.findAll((e) => classes(e).indexOf('hmenubox') !== -1)[0];
    same('the Room\'s Menu has the same rule (375 px high: 375 - 106 - 10)', roomBox.style.maxHeight, '259px');
}
{   // My room pressed twice before the room has said a word: one socket, one Hello, one room
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    env.home('mine').click();
    env.roomBar('rhome').click();
    env.home('mine').click();
    env.home('mine').click();
    const s = env.last().open();
    check('My room pressed again and again before the Welcome makes one room: one socket and one Hello (a second press only shows the Room)', env.sockets.length === 1 && s.sent.length === 1 && env.screen() === 'room');
    const pressed = env.entries.length;
    env.home('mine').click();
    check('... and a press while the Room is up makes no new entry of the history', env.entries.length === pressed);
}
{   // Leave room is a move to Home like the Home button: its entry, its address
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    env.home('mine').click();
    env.arrive({ key: KEY });
    env.roomBar('leave').click();
    check('Leave room makes its own history entry, Home at "/" (not the Room\'s address with Home on it)', env.screen() === 'home' && env.win.location.hash === '' && env.entries.length === 3 && env.entries[2].url === '/' && JSON.stringify(env.entries[2].state) === '{"ants":"home"}');
}
{   // a browser that refuses the history (a sandboxed frame): the screens still change
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true, historyThrows: true });
    let thrown = null;
    try { env.home('mine').click(); } catch (e) { thrown = e; }
    check('a history that refuses (a sandboxed frame) does not stop My room: the room is made and shown, the address stays', thrown === null && env.screen() === 'room' && env.sockets.length === 1 && env.win.location.hash === '');
    let again = null;
    try { env.roomBar('rhome').click(); } catch (e) { again = e; }
    check('... nor Home', again === null && env.screen() === 'home');
}
{   // the Menu goes when the browser moves too (Back), not only when a button does
    const env = runLobby('', {}, { stayHome: true });
    env.home('mine').click();
    env.roomBar('hmenu').click();
    const box = env.body.findAll((e) => classes(e).indexOf('hmenubox') !== -1)[0];
    check('(the Menu is open on the Room)', box.hidden === false);
    env.back();
    check('Back puts the Menu away', env.screen() === 'home' && box.hidden === true && env.home('hmenu') !== null);
}
{   // a test room (a map from the Map Maker) opens the Room like a link; the page makes no room until the map is ready
    const env = runLobby('?testroom=abc123', { 'ants.name': 'Maya' });
    check('a test room opens on the Room (the strip says what it is), and the page has made no room of its own yet', env.screen() === 'room' && env.sockets.length === 0);
}
{   // "Start a room of my own instead" and a removal rewrite the address (the room's code goes), and the address names the screen that is up: a reload keeps it
    const link = runLobby('?room=k7m2xq', { 'ants.name': 'Maya' });
    link.$('name-step-own').click();
    check('"a room of my own instead" from a link: the address is "/#room" (the Room is up), so a reload keeps the screen', link.win.location.pathname === '/' && link.win.location.search === '' && link.win.location.hash === '#room' && link.sockets.length === 1);
    link.arrive({ key: KEY });
    const reloaded = runLobby('', { 'ants.name': 'Maya' }, { session: link.session.data, hash: link.win.location.hash });
    check('... and the reload is the Room, with the seat taken back', reloaded.screen() === 'room' && reloaded.sockets.length === 1);
    const home = runLobby('', { 'ants.name': 'Maya' }, { session: { 'ants.lobby': JSON.stringify({ c: 'k7m2xq', k: hex(KEY), n: 'Maya', o: 0 }) } });
    home.last().open().receive(Uint8Array.from([3, NET0.REJECT.MatchRunning]));
    check('a refusal while Home is up rewrites the address to "/" (Home): no hash', home.screen() === 'home' && home.win.location.hash === '' && home.win.location.search === '');
    const away = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    away.home('mine').click();
    const s = away.arrive({ key: KEY, seats: [[C, 'Maya'], [C, 'Sam'], [E, ''], [E, '']] });
    s.receive(Uint8Array.from([3, NET0.REJECT.Kicked]));
    check('removed from the room while the Room is up: a room of the person\'s own, the address still "/#room"', away.screen() === 'room' && away.win.location.hash === '#room' && away.sockets.length === 2);
}
{   // "/#room" in a tab that holds no seat is Home; the first entry that a button marks says so in its address too (Back must not show Home at "/#room")
    const env = runLobby('', { 'ants.name': 'Maya' }, { hash: '#room', stayHome: true });
    env.home('mine').click();
    check('the first entry is marked as Home with the address "/" (the hash that named no room is gone), and the Room is a new entry', env.entries.length === 2 && env.entries[0].url === '/' && JSON.stringify(env.entries[0].state) === '{"ants":"home"}' && env.entries[1].url === '/#room');
    env.back();
    check('Back from the Room is Home at "/", not Home at "/#room"', env.screen() === 'home' && env.win.location.hash === '' && env.win.location.pathname === '/');
}
{   // the map list is a dialog: the page makes Home's bars inert while it is open (the real list, opened by the real button)
    const env = runLobby('', { 'ants.name': 'Maya' }, { picker: true });
    env.arrive({ key: KEY, seats: [[C, 'Maya'], [E, ''], [E, ''], [E, '']] });
    const rbar = env.root.children.filter((c) => classes(c).indexOf('rbar') !== -1)[0];
    check('(the list\'s button is there for the host, and no bar is inert)', !env.$('browse').hidden && !rbar.hasAttribute('inert') && !env.home('hbar').hasAttribute('inert'));
    env.$('browse').click();
    check('the page opens the list and makes Home\'s bars inert with the rest of the page behind it (and the footer, the logo row and the strips)', env.$('picker-panel').children.length > 0 && rbar.hasAttribute('inert') && env.home('hbar').hasAttribute('inert') && env.$('lobby').hasAttribute('inert'));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 7. A hand-off that fails or is stopped: after 15 s Home takes presses again (the page is still here)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{   // the hand-off that fails or is stopped: after 15 s Home is Home again
    const env = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    env.home('mine').click();
    const s = env.arrive({ key: KEY, seats: [[C, 'Sam'], [E, ''], [E, ''], [E, '']] });
    env.roomBar('rhome').click();
    s.receive(roomMessage([[C, 'Sam'], [C, 'Maya'], [E, ''], [E, '']], { you: 0, leader: 0, flags: 7 }));
    const rbar = env.root.children.filter((c) => classes(c).indexOf('rbar') !== -1)[0], hbar = env.home('hbar'), card = env.$('home').children[2];
    check('START: Home says "Opening the game…" and both bars are inert', card.hidden === false && rbar.hasAttribute('inert') && hbar.hasAttribute('inert'));
    env.advance(14999);
    check('14.999 s later it still does', card.hidden === false && hbar.hasAttribute('inert') && rbar.hasAttribute('inert'));
    env.advance(1);
    check('after 15 s (the page is still here: the hand-off failed or was stopped) the card goes and both bars take presses again', card.hidden === true && !hbar.hasAttribute('inert') && !rbar.hasAttribute('inert'));
    env.home('mine').click();
    check('... and My room works', env.screen() === 'room');
}
{   // the timer does not free what a dialog holds
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    const host = new H.El(env, 'div', 'home2'), lobby = new H.El(env, 'div', 'lobby2');
    lobby.parent = env.root;
    const second = env.win.AntsHome.mount({ doc: env.doc, win: env.win, host, lobby, page: { info: () => ({ client: false, model: null, offline: 'none' }), make() {}, leave() {}, how() {} } });
    const bars = host.findAll((e) => e.tagName === 'header').concat(env.root.children.filter((c) => classes(c).indexOf('rbar') !== -1).slice(-1));
    second.inert(true); second.opening();
    env.advance(15000);
    check('a dialog that is still open when the 15 s are up keeps the bars inert (the timer gives back only what START took)', bars.every((b) => b.hasAttribute('inert')) && (second.inert(false), bars.every((b) => !b.hasAttribute('inert'))));
    second.opening(); second.opening();
    env.advance(14000);
    second.opening();
    env.advance(14000);
    check('a second hand-off starts the 15 s again (and one timer only is running for each Home)', bars.every((b) => b.hasAttribute('inert')) && (env.advance(1000), bars.every((b) => !b.hasAttribute('inert'))));
}

{   // what Home never does: no network of its own, no storage, no markup, nothing that is the page's room (the client, the model, the session)
    const code = fs.readFileSync(path.join(lobbyDir, 'home', 'home.js'), 'utf8');
    check('home.js makes no request of its own and keeps nothing (no fetch, XMLHttpRequest, WebSocket, localStorage, sessionStorage; the chat dock\'s two files are asked for by a link and a script element) and writes no markup', !/\bfetch\b|XMLHttpRequest|WebSocket|localStorage|sessionStorage|innerHTML|outerHTML|insertAdjacentHTML|document\.write/.test(code.replace(/\/\/.*$/gm, '')));
    check('... its only calls into the page are info, make, leave and how', [...code.matchAll(/\bpage\.(\w+)/g)].map((m) => m[1]).filter((v, i, a) => a.indexOf(v) === i).sort().join() === 'how,info,leave,make');
}

console.log('home screens: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
