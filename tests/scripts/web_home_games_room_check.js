// Runs the front page's PUBLIC ROOM, QUICK HOST and JOIN (web/home/games.js with web/lobby.html's own script, docs/HOME_SCREEN.md, docs/PUBLIC_ROOMS.md) in the fake browser of tests/scripts/web_page_harness.js against a
// scripted game server and a fake GET /lobbies (the list itself, its looks and its words are web_home_games_check.js):
//   1. the Room's switch: the leader's Public | Private, the guest's chip, the state that is the Room message's and never the press's, a double press, the page's pace, the place of the switch on a wide and a narrow window;
//   2. what the room says: a room that went private after 30 minutes (the notice and its button), a host that changed, a leader's own "private", a guest; the repeat every 10 minutes, only while the Room shows, the tab is
//      visible and the leader looks at a public room;
//   3. Quick host in every state: no room, a private room (alone, with others), a public room, a room of somebody else (the question), START waiting, a match that runs, a full server, a double press, a room that
//      is not ready yet;
//   4. Join from the list and by code: the second client that only joins, the old room kept until the new one welcomes the page and let go of then, every refusal in plain words and the list asked again, a server that
//      does not answer, the question when others are in the room, a double press;
//   5. START that reaches the page and a script that is broken: Home, the Room and the hand-off to the game go on.
// tests/scripts/test_web_home_games.py runs this with node (the quick tier). usage: node web_home_games_room_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const R = require('./web_home_games_rig.js')(process.argv[2]);
const { check, same, flush, fakeSite, row, listOf, page, wait, classes, find, first, home, tap, popOf, rowsOf, joinOf, textOf, stripOf, stripText, stripButtons, pillOf, dialogOf, visibilities, leaves, seatsOf, myRoom, guestRoom, roomAs,
        PUBLIC, PRIVATE, STARTING, NET0, KEY, E, C, hex, roomMessage, welcome, reject, helloBytes, helloName, helloRoom, grouped, RULES0, done, broke, pressEscape } = R;

const KEY2 = Uint8Array.from(Array.from({ length: 16 }, (_, i) => 0x60 + i));
const visOf = (env) => first(env.root, 'vis');
const pressed = (env) => visOf(env).children.map((b) => b.getAttribute('aria-pressed'));
const state = (env) => pressed(env).join();
const noteOf = (env) => first(env.root, 'visnote');
const tagOf = (env) => first(env.root, 'vistag');
const gone = (sock) => sock.closed === true;
const openGames = async (env) => { tap(env.home('games')); await flush(); };
const OTHERS = ['Maya', 'Sage'];

(async () => {

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The Room's switch
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await page();
    check('before the page has a room the Room card has no switch and no chip (nothing of it is shown)', visOf(env).hidden === true && tagOf(env).hidden === true && noteOf(env).hidden === true);
    const sock = myRoom(env, ['Maya']);
    const vis = visOf(env);
    check('the leader of a new room sees the switch: Private (link only) is the state, Public (listed in Open games) is the other, both can be pressed', vis.hidden === false && vis.getAttribute('role') === 'group' && vis.getAttribute('aria-label') === 'Who can find this room'
          && textOf(vis.children[0]) === 'Privatelink only' && textOf(vis.children[1]) === 'Publiclisted in Open games' && state(env) === 'true,false' && vis.children.every((b) => b.disabled === false && b.type === 'button'));
    check('... the line under it says what Public does ("Public rooms show your name and map in the Games list."), and no chip is shown', noteOf(env).hidden === false && noteOf(env).textContent === 'Public rooms show your name and map in the Games list.' && tagOf(env).hidden === true);
    tap(vis.children[1]);
    same('Public sends one Visibility message with the byte 1 (to this room\'s socket)', visibilities(sock), [1]);
    same('... and the switch still shows what the Room message last said (private) until the server says otherwise: nothing is assumed', pressed(env), ['true', 'false']);
    check('... the buttons take no press until then (a second press is no second message, whatever the browser does)', vis.children.every((b) => b.disabled === true));
    vis.children[1].click(); vis.children[1].click();
    same('(a press that a browser did let through changes nothing)', visibilities(sock), [1]);
    sock.receive(roomAs(['Maya'], 0, PUBLIC));
    same('the Room message says public: the switch follows it, and the buttons are back', [pressed(env), vis.children.every((b) => b.disabled === false)], [['false', 'true'], true]);
    check('... the line says that the room is listed (and that the link works too), and no notice strip is made for a press of the switch', noteOf(env).textContent === 'Listed. Anyone on the front page can join from Open games. The link works too.' && stripOf(env).hidden === true);
    tap(vis.children[1]);
    same('Public on a public room sends nothing (the switch is where the room is)', visibilities(sock), [1]);
    tap(vis.children[0]);
    same('Private sends the byte 0', visibilities(sock), [1, 0]);
    same('... and the switch waits for the room', pressed(env), ['false', 'true']);
    sock.receive(roomAs(['Maya'], 0, PRIVATE));
    same('... a Room message that says private is private', pressed(env), ['true', 'false']);
    check('... and nothing says that the room "went private": the leader chose it', stripOf(env).hidden === true);
    tap(vis.children[1]);
    check('a request that the page\'s pace holds back (two were sent a moment ago) sends nothing and says so under the switch', visibilities(sock).length === 2 && noteOf(env).textContent === 'One moment, then press again.' && state(env) === 'true,false');
    await wait(env, 1500);
    tap(vis.children[1]);
    same('... a moment later it goes', visibilities(sock), [1, 0, 1]);
    const waiting = await page();
    const ws = myRoom(waiting, ['Maya', 'Sage'], STARTING, null, true);
    check('START waiting (and the page has no seat to hand over): the switch cannot be pressed', visOf(waiting).children.every((b) => b.disabled === true) && waiting.assigned.length === 0);
    sock.drop();
    check('a link that is lost: the switch cannot be pressed (the page is not in the room), and it shows no change', vis.children.every((b) => b.disabled === true) && state(env) === 'true,false');
    check('no markup was written for any of this', env.innerHTMLWrites.length === 0);
}
{   // a guest sees the state and cannot change it
    const env = await page({ search: '?room=k7m2xq', options: { stayHome: false } });
    const sock = guestRoom(env, 'Pebble', PUBLIC);
    check('a guest sees "Public room" in the heading line and no switch and no line under it', tagOf(env).hidden === false && tagOf(env).textContent === 'Public room' && visOf(env).hidden === true && noteOf(env).hidden === true);
    sock.receive(roomAs(['Pebble', 'Maya'], 1, PRIVATE));
    check('... and "Private room" when the room says private', tagOf(env).textContent === 'Private room' && visOf(env).hidden === true);
    check('a guest sends no Visibility, however it is asked (the page does not send it)', visibilities(sock).length === 0 && env.last().sent.every((b) => b[0] !== 35));
    sock.receive(roomAs(['Pebble', 'Maya'], 1, PUBLIC));
    sock.receive(roomMessage([[E, ''], [C, 'Maya'], [E, ''], [E, '']], { you: 1, leader: 1, flags: PRIVATE }));
    check('a guest whose host went and who leads now (the room is private again) has the switch, in the state that the room says', visOf(env).hidden === false && tagOf(env).hidden === true && state(env) === 'true,false');
}
{   // the place of the switch: in the heading line on a wide window, under the invitation on a phone
    const wide = await page({ options: { innerWidth: 1280 } });
    myRoom(wide, ['Maya']);
    const orderWide = wide.root.children.map((c) => c.id || classes(c)[0]);
    const narrow = await page({ options: { innerWidth: 390 } });
    myRoom(narrow, ['Maya']);
    const orderNarrow = narrow.root.children.map((c) => c.id || classes(c)[0]);
    const at = (order, name) => order.indexOf(name);
    check('on a phone the switch is placed right before the colors and its line right after it', at(orderNarrow, 'vis') === at(orderNarrow, 'slots') - 2 && at(orderNarrow, 'visnote') === at(orderNarrow, 'slots') - 1, JSON.stringify(orderNarrow));
    check('on a wide window it is not (it is in the heading line, with its line under the heading)', at(orderWide, 'vis') !== at(orderWide, 'slots') - 2, JSON.stringify(orderWide));
    check('(there is one switch, one chip and one line, however often the window is laid out again)', [orderWide, orderNarrow].every((o) => o.filter((n) => n === 'vis').length === 1 && o.filter((n) => n === 'vistag').length === 1 && o.filter((n) => n === 'visnote').length === 1));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. What the room says, and the repeat
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await page();
    const sock = myRoom(env, OTHERS, PUBLIC);
    sock.receive(roomAs(OTHERS, 0, PRIVATE));
    check('a public room that the server made private (nobody did anything for 30 minutes) says so to its leader, with a button that makes it public again', stripText(env) === 'Your room went private after 30 minutes. Make it public again?' && textOf(stripButtons(env)[0]) === 'Make it public' && textOf(stripButtons(env)[1]) === 'OK' && stripOf(env).className.indexOf('warn') !== -1);
    check('... the switch says private (the Room message)', state(env) === 'true,false');
    tap(stripButtons(env)[0]);
    same('one tap sends Public, and the strip is gone', [visibilities(sock), stripOf(env).hidden], [[1], true]);
    sock.receive(roomAs(OTHERS, 0, PUBLIC));
    check('... and the room is public when the room says so', state(env) === 'false,true' && stripOf(env).hidden === true);
    sock.receive(roomAs(OTHERS, 0, PRIVATE));
    check('(the next time is a new notice)', stripText(env).indexOf('went private after 30 minutes') !== -1);
    tap(stripButtons(env)[1]);
    check('OK puts it away and sends nothing', stripOf(env).hidden === true && visibilities(sock).length === 1);
    sock.receive(roomAs(OTHERS, 0, PUBLIC));
    sock.receive(roomAs(OTHERS, 0, PRIVATE));
    sock.receive(roomAs(OTHERS, 0, PUBLIC));
    check('a notice goes when the room is public again by itself', stripOf(env).hidden === true);
}
{   // the one tap that makes it public again is held back by the page's pace: the notice stays until it has gone
    const env = await page();
    const sock = myRoom(env, OTHERS, PUBLIC);
    tap(visOf(env).children[0]);
    sock.receive(roomAs(OTHERS, 0, PRIVATE));
    tap(visOf(env).children[1]);
    sock.receive(roomAs(OTHERS, 0, PUBLIC));
    sock.receive(roomAs(OTHERS, 0, PRIVATE));
    tap(stripButtons(env)[0]);
    check('"Make it public" with no request left to spend sends nothing and the notice stays (it is not gone for a tap that did nothing)', visibilities(sock).length === 2 && stripOf(env).hidden === false && stripText(env).indexOf('went private after 30 minutes') !== -1);
    await wait(env, 1500);
    tap(stripButtons(env)[0]);
    check('... a moment later the same tap sends it, and the notice goes', visibilities(sock).length === 3 && stripOf(env).hidden === true);
}
{   // the leader's own Private, the leader moving to another color, and a guest
    const env = await page();
    const sock = myRoom(env, OTHERS, PUBLIC);
    tap(visOf(env).children[0]);
    sock.receive(roomAs(OTHERS, 0, PRIVATE));
    check('a room that the leader made private is private, and the strip says nothing of it (no "went private")', stripOf(env).hidden === true);
    const e2 = await page();
    const s2 = myRoom(e2, OTHERS, PUBLIC);
    s2.receive(roomMessage([[E, ''], [C, 'Maya'], [C, 'Sage'], [E, '']], { you: 1, leader: 1, flags: PUBLIC }));
    check('the leader who moves to another color is the same host: the room stays public and nothing is said', stripOf(e2).hidden === true && state(e2) === 'false,true');
}
{   // a host that changed, from the point of view of a guest and of the new host
    const env = await page({ search: '?room=k7m2xq', options: { stayHome: false } });
    const sock = guestRoom(env, 'Pebble', PUBLIC);
    sock.receive(roomMessage([[E, ''], [C, 'Maya'], [C, 'Sage'], [E, '']], { you: 1, leader: 1, flags: PRIVATE }));
    check('the host leaves and the player who has been there longest leads: the room is private again, and the strip says why', stripText(env) === 'This room is private again because the host changed.' && stripOf(env).className.indexOf('warn') === -1);
    check('... and the new host has the switch, private', visOf(env).hidden === false && state(env) === 'true,false');
    const guest = await page({ search: '?room=k7m2xq', options: { stayHome: false } });
    const gs = guestRoom(guest, 'Pebble', PUBLIC);
    gs.receive(roomMessage([[E, ''], [C, 'Sage'], [C, 'Maya'], [E, '']], { you: 2, leader: 1, flags: PRIVATE }));
    check('a guest who stays a guest is told too (the room is private again because the host changed) and sees "Private room"', stripText(guest) === 'This room is private again because the host changed.' && tagOf(guest).textContent === 'Private room' && visOf(guest).hidden === true);
}
{   // a guest sees the room go private by itself: the chip, no strip, no button
    const env = await page({ search: '?room=k7m2xq', options: { stayHome: false } });
    const sock = guestRoom(env, 'Pebble', PUBLIC);
    sock.receive(roomAs(['Pebble', 'Maya'], 1, PRIVATE));
    check('a guest sees the idle room go private as the chip and nothing else (the one tap is the leader\'s)', stripOf(env).hidden === true && tagOf(env).textContent === 'Private room');
}
{   // the repeat: every 10 minutes, only while the Room shows, the tab is visible and the leader looks at a public room
    const env = await page({ options: { fakeClock: true } });
    const sock = myRoom(env, OTHERS, PUBLIC);
    await wait(env, 599500);
    same('a public room that its leader looks at is told "public" again after 10 minutes, not before', visibilities(sock), []);
    await wait(env, 500);
    same('... after 10 minutes, once (the byte 1)', visibilities(sock), [1]);
    await wait(env, 599500);
    same('... and not again until 10 more', visibilities(sock), [1]);
    await wait(env, 500);
    same('... every 10 minutes', visibilities(sock), [1, 1]);
    check('(the repeat is no change that anybody sees: the room is as it was and no strip, no hint)', stripOf(env).hidden === true && state(env) === 'false,true');
    env.hide(true);
    await wait(env, 1500000);
    same('a tab that is out of sight says nothing, however long it stays so (25 minutes: nothing)', visibilities(sock), [1, 1]);
    env.hide(false);
    await wait(env, 500);
    same('... and the tab that is back repeats it at once (more than 10 minutes have gone by)', visibilities(sock), [1, 1, 1]);
    env.roomBar('rhome').click();
    await wait(env, 1800000);
    same('Home shows (the Room does not): nothing is said in half an hour', visibilities(sock), [1, 1, 1]);
    env.home('mine').click();
    await wait(env, 500);
    same('... and the Room back repeats it at once', visibilities(sock), [1, 1, 1, 1]);
    sock.receive(roomAs(OTHERS, 0, PRIVATE));
    await wait(env, 1300000);
    same('a room that is private is not told anything', visibilities(sock), [1, 1, 1, 1]);
}
{
    const waiting = await page({ options: { fakeClock: true } });
    const ws = myRoom(waiting, OTHERS, STARTING, null, true);
    check('(a room whose START waits has no repeat to make: no timer is waiting for 10 minutes)', waiting.timers.every((t) => t.at - waiting.clock < 500000));
    await wait(waiting, 1300000);
    same('a public room whose START waits (and whose page has no seat to hand over) is told nothing', visibilities(ws), []);
}
{
    const env = await page({ search: '?room=k7m2xq', options: { stayHome: false, fakeClock: true } });
    const sock = guestRoom(env, 'Pebble', PUBLIC);
    check('a guest has no repeat to make: no timer is waiting for 10 minutes', env.timers.every((t) => t.at - env.clock < 500000));
    await wait(env, 1300000);
    check('a guest never says "public": 20 minutes of a public room on the Room screen send nothing', visibilities(sock).length === 0);
}
{   // the repeat shares the page's pace with the presses
    const env = await page({ options: { fakeClock: true } });
    const sock = myRoom(env, OTHERS, PUBLIC);
    tap(visOf(env).children[0]);
    sock.receive(roomAs(OTHERS, 0, PRIVATE));
    await wait(env, 1500);
    tap(visOf(env).children[1]);
    sock.receive(roomAs(OTHERS, 0, PUBLIC));
    await wait(env, 599500);
    same('a press counts as use: the repeat is 10 minutes after the last "public", not after the room was made', visibilities(sock), [0, 1]);
    await wait(env, 1000);
    same('... then it goes', visibilities(sock), [0, 1, 1]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. Quick host
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{   // no room: it makes one, then sends Visibility(public) after the Welcome
    const env = await page();
    tap(env.home('host'));
    check('Quick host with no room makes a room of the page\'s own (a new code, the remembered name: My room\'s machinery, one socket) and shows the Room', env.sockets.length === 1 && env.screen() === 'room' && helloName(env.last().sent[0] || env.last().open().sent[0]) === 'Maya');
    const sock = env.last();
    check('... nothing is sent about the room before it exists (Visibility waits for the Welcome)', visibilities(sock).length === 0);
    tap(env.home('host'));
    check('(a second press is no second room, and nothing else)', env.sockets.length === 1);
    env.arrive({ key: KEY, seats: seatsOf(['Maya']), room: { flags: PRIVATE } });
    same('after the Welcome and the first Room message, Visibility(public) is sent, once', visibilities(sock), [1]);
    same('... the switch still says private (what the room said)', pressed(env), ['true', 'false']);
    check('... no notice yet: the room has not said that it is public', stripOf(env).hidden === true);
    tap(env.home('host'));
    same('a second press after the request was sent sends nothing more (the room has not said "public" yet)', visibilities(sock), [1]);
    sock.receive(roomAs(['Maya'], 0, PUBLIC));
    same('the room says public: the switch follows, and the strip says it ("Your room is public now. It shows in Open games.")', [pressed(env), stripText(env)], [['false', 'true'], 'Your room is public now. It shows in Open games.']);
    check('... one button (OK), and the strip is the page\'s own look', stripButtons(env).length === 1 && textOf(stripButtons(env)[0]) === 'OK' && stripOf(env).className.indexOf('banner') !== -1);
    const order = env.root.children;
    check('... the strip stands under the Room\'s slim bar and before its card, as the approved picture draws it (on Home it stands with the page\'s own strips, under the first of them)', order.indexOf(stripOf(env)) === order.indexOf(env.root.children.find((c) => classes(c).indexOf('rbar') !== -1)) + 1 && order.indexOf(env.$('lobby')) === order.indexOf(stripOf(env)) + 1);
    env.roomBar('rhome').click();
    check('... and on Home it is with the page\'s own strips (right after the first)', env.root.children.indexOf(stripOf(env)) === env.root.children.indexOf(env.$('banner')) + 1);
    env.home('mine').click();
    sock.receive(roomAs(['Maya', 'Sage'], 0, PUBLIC));
    same('(nothing more is sent)', visibilities(sock), [1]);
}
{   // a leader of a private room, with others: it flips the room, the others stay
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage', 'Pip']);
    env.roomBar('rhome').click();
    tap(env.home('host'));
    check('Quick host with a private room of yours flips it: Visibility(public) at once, the Room shows, no second room, nobody leaves', visibilities(sock).length === 1 && visibilities(sock)[0] === 1 && env.screen() === 'room' && env.sockets.length === 1 && leaves(sock) === 0);
    sock.receive(roomAs(['Maya', 'Sage', 'Pip'], 0, PUBLIC));
    same('... and says who stays in it, as the picture does', stripText(env), 'Your room is public now. It shows in Open games; Sage and Pip stay in it.');
    const one = await page();
    const s1 = myRoom(one, ['Maya', 'Sage']);
    tap(one.home('host'));
    s1.receive(roomAs(['Maya', 'Sage'], 0, PUBLIC));
    same('(one other: "Sage stays in it")', stripText(one), 'Your room is public now. It shows in Open games; Sage stays in it.');
}
{   // already public
    const env = await page();
    const sock = myRoom(env, OTHERS, PUBLIC);
    env.roomBar('rhome').click();
    tap(env.home('host'));
    check('Quick host with a room that is public already sends nothing, says so and shows the Room', visibilities(sock).length === 0 && stripText(env) === 'Your room is already public.' && env.screen() === 'room' && env.sockets.length === 1);
}
{   // somebody else's room: ask first
    const env = await page({ search: '?room=k7m2xq', options: { stayHome: false } });
    const guestSock = guestRoom(env, 'Pebble', PUBLIC);
    env.roomBar('rhome').click();
    tap(env.home('host'));
    const box = dialogOf(env);
    check('Quick host as a guest asks first, in the approved card: "Leave Pebble’s room and host your own?", the answer that changes nothing has the focus, and nothing has happened yet (no socket, no Leave)', !!box
          && textOf(first(box, 'gctitle')) === 'Leave Pebble’s room and host your own?' && textOf(first(box, 'gyes')) === 'Leave and host' && textOf(first(box, 'gstay')) === 'Stay here' && env.focused === first(box, 'gstay') && box.getAttribute('role') === 'dialog'
          && box.getAttribute('aria-modal') === 'true' && env.sockets.length === 1 && leaves(guestSock) === 0 && !guestSock.closed);
    check('... the rest of Home takes no press while it is up (inert)', env.$('home').children.filter((c) => !classes(c).includes('gshade')).every((c) => c.getAttribute('inert') !== null));
    tap(first(box, 'gstay'));
    check('"Stay here" changes nothing: the card is gone, nobody left, no socket, Home is as it was', dialogOf(env) === null && env.sockets.length === 1 && leaves(guestSock) === 0 && env.screen() === 'home' && env.$('home').children.every((c) => c.getAttribute('inert') === null));
    tap(env.home('host'));
    pressEscape(env);
    check('Escape is "Stay here"', dialogOf(env) === null && env.sockets.length === 1 && leaves(guestSock) === 0);
    tap(env.home('host'));
    tap(first(dialogOf(env), 'gyes'));
    check('"Leave and host" leaves the room (a Leave, the socket closed), makes a room of its own (a new socket and code, not the room of the link) and shows the Room', leaves(guestSock) === 1 && guestSock.closed && env.sockets.length === 2 && env.screen() === 'room' && helloRoom(env.last().sent[0] || env.last().open().sent[0]) !== 'k7m2xq');
    const mine = env.last();
    env.arrive({ key: KEY2, seats: seatsOf(['Maya']), room: { flags: PRIVATE } });
    same('... and Visibility(public) goes after its Welcome', visibilities(mine), [1]);
}
{   // START waiting, a match of yours that runs
    const env = await page();
    const sock = myRoom(env, OTHERS, STARTING, null, true);
    env.roomBar('rhome').click();
    tap(env.home('host'));
    check('Quick host while START waits says "Your match is running." and sends nothing (no Rejoin button: there is nothing to go back to yet)', stripText(env) === 'Your match is running.' && stripButtons(env).length === 1 && visibilities(sock).length === 0 && env.sockets.length === 1);
    const later = await page({ stored: { 'ants.name': 'Maya', 'ants.rejoin.k7m2xq.1': JSON.stringify({ k: hex(KEY), s: 'wss://play.test/ws', t: Date.now() }) } });
    tap(later.home('host'));
    check('a match of yours that is still running (the Rejoin strip is up): "Your match is running." with a Rejoin button, and no room is made', stripText(later) === 'Your match is running.' && textOf(stripButtons(later)[0]) === 'Rejoin' && textOf(stripButtons(later)[1]) === 'OK' && later.sockets.length === 0 && later.screen() === 'home');
    tap(stripButtons(later)[0]);
    check('... and Rejoin is the strip\'s own: the tab goes to the match', later.assigned.length === 1 && /[?&]room=k7m2xq&seat=1/.test(later.assigned[0]));
}
{   // the server is full
    const env = await page();
    tap(env.home('host'));
    env.last().open().receive(reject(6));
    check('a server with no place for a room (NoSuchRoom): the page\'s own words, "The server is full right now", and no Visibility is ever sent', !env.$('banner').hidden && /^The server is full right now/.test(env.$('banner-text').textContent) && visibilities(env.last()).length === 0);
    await wait(env, 30000);
    tap(env.$('banner-x'));
    env.arrive({ key: KEY, seats: seatsOf(['Maya']), room: { flags: PRIVATE } });
    check('... and the room that "Try again" makes later stays private (the request was given up with the refusal)', visibilities(env.last()).length === 0);
}
{   // a request is for the room that it was made for: a room that is left before it is there is not made public, and the next room is not either
    const env = await page();
    tap(env.home('host'));
    env.roomBar('leave').click();
    env.home('mine').click();
    const second = env.last();
    env.arrive({ key: KEY, seats: seatsOf(['Maya']), room: { flags: PRIVATE } });
    check('Quick host, Leave room before the room is there, then My room: the second room stays private (the request was for the first)', env.sockets.length === 2 && visibilities(second).length === 0 && state(env) === 'true,false');
}
{   // a room that is not ready (the page's seat is coming back), and a request that the pace holds back
    const env = await page({ stored: { 'ants.name': 'Maya' }, options: { session: { 'ants.lobby': JSON.stringify({ c: 'abc234', k: hex(KEY), n: 'Maya', o: 1 }) } } });
    const waiting = env.sockets[0];
    tap(env.home('host'));
    check('Quick host while the page\'s room is coming back (a reload of a tab that holds a seat) makes no second room and waits for it', env.sockets.length === 1 && env.screen() === 'room');
    env.arrive({ key: KEY, seats: seatsOf(['Maya']), room: { flags: PRIVATE } });
    same('... and sends Visibility(public) when the room is there', visibilities(waiting), [1]);
}
{   // the seat that comes back is in a room that is public already
    const env = await page({ stored: { 'ants.name': 'Maya' }, options: { session: { 'ants.lobby': JSON.stringify({ c: 'abc234', k: hex(KEY), n: 'Maya', o: 1 }) } } });
    const sock = env.sockets[0];
    tap(env.home('host'));
    env.arrive({ key: KEY, seats: seatsOf(['Maya', 'Sage']), room: { flags: PUBLIC } });
    check('Quick host while the page\'s room comes back, and the room turns out to be public already: nothing is sent, and the strip says so', visibilities(sock).length === 0 && stripText(env) === 'Your room is already public.');
}
{
    const env = await page({ stored: { 'ants.name': 'Maya' }, options: { session: { 'ants.lobby': JSON.stringify({ c: 'abc234', k: hex(KEY), n: 'Maya', o: 1 }) } } });
    const sock = env.sockets[0];
    tap(env.home('host'));
    env.arrive({ seat: 1, key: KEY, seats: seatsOf(['Pebble', 'Maya']), room: { you: 1, leader: 0, flags: PRIVATE } });
    check('a request that waited for a room which turned out to be the room of somebody else is dropped: it never takes it over', visibilities(sock).length === 0);
    await wait(env, 30000);
    check('... and it does not come back later', visibilities(sock).length === 0 && stripOf(env).hidden === true);
}
{   // ... not even when the guest becomes the host a moment later (the host leaves)
    const env = await page({ stored: { 'ants.name': 'Maya' }, options: { session: { 'ants.lobby': JSON.stringify({ c: 'abc234', k: hex(KEY), n: 'Maya', o: 1 }) } } });
    const sock = env.sockets[0];
    tap(env.home('host'));
    env.arrive({ seat: 1, key: KEY, seats: seatsOf(['Pebble', 'Maya']), room: { you: 1, leader: 0, flags: PRIVATE } });
    sock.receive(roomMessage([[E, ''], [C, 'Maya'], [E, ''], [E, '']], { you: 1, leader: 1, flags: PRIVATE }));
    await wait(env, 30000);
    check('a request that waited for a room which was somebody else\'s is given up for good: when the host leaves and the page leads, the room stays private', visibilities(sock).length === 0 && state(env) === 'true,false');
}
{   // the page's pace holds the request back: it is made again a moment later
    const env = await page({ options: { fakeClock: true } });
    const sock = myRoom(env, ['Maya'], PRIVATE);
    tap(visOf(env).children[1]);
    sock.receive(roomAs(['Maya'], 0, PUBLIC));
    tap(visOf(env).children[0]);
    sock.receive(roomAs(['Maya'], 0, PRIVATE));
    env.roomBar('rhome').click();
    tap(env.home('host'));
    check('Quick host when the page has no more requests to spend sends nothing yet', visibilities(sock).length === 2);
    await wait(env, 1500);
    same('... it is made a moment later (once)', visibilities(sock), [1, 0, 1]);
}
{   // a request that nobody answers is forgotten after a while: no room turns public much later
    const env = await page({ options: { fakeClock: true } });
    const sock = myRoom(env, ['Maya'], PRIVATE);
    env.roomBar('rhome').click();
    tap(env.home('host'));
    same('Quick host sends Visibility(public)', visibilities(sock), [1]);
    await wait(env, 20500);
    check('a room that has not said "public" after 20 seconds is told so ("Your room is still private. Use the Public switch in the room.")', stripText(env) === 'Your room is still private. Use the Public switch in the room.' && stripOf(env).className.indexOf('warn') !== -1);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. Join
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
async function withList(rows, o) {
    const site = fakeSite();
    site.body = listOf(rows || [row('k7m2xq', 'Pebble', { map: 'ISLANDS.LVL', people: 2, free: 2 }), row('abc234', 'Clover', { people: 1, bots: 1, free: 2 })]);
    const env = await page(Object.assign({ site }, o || {}));
    return env;
}
const welcomed = (sock, seat, host, flags) => { sock.open(); sock.receive(welcome(seat, KEY2, 0)); sock.receive(roomAs(host, seat, flags === undefined ? PRIVATE : flags)); };
{
    const env = await withList();
    const own = myRoom(env, ['Maya']);
    const ownCode = helloRoom(own.sent[0]);
    env.roomBar('rhome').click();
    await openGames(env);
    tap(joinOf(rowsOf(env)[0]));
    check('Join in the list, from a room of your own with nobody else in it, asks nothing and opens a second client that only joins (a Hello with no lobby block, the room of the row, the name of the room that is yours)', env.sockets.length === 2 && !dialogOf(env)
          && hex(env.last().open().sent[0]) === hex(helloBytes('Maya', 'k7m2xq', { join: true })));
    check('... and the own room is kept until the other welcomes the page: no Leave, the socket open, the code and the screen the same', leaves(own) === 0 && !own.closed && env.$('code').textContent === grouped(ownCode) && env.screen() === 'home');
    check('... the buttons of the list take no press while it looks (a double press is no second client)', joinOf(rowsOf(env)[0]).disabled === true);
    joinOf(rowsOf(env)[0]).click();
    check('(a press that a browser did let through opens no second client)', env.sockets.length === 2);
    const probe = env.last();
    probe.receive(welcome(1, KEY2, 0));
    check('the other room has welcomed the page: now the own room is let go of (a Leave, the socket closed), and the page is in the other', leaves(own) === 1 && own.closed && env.$('code').textContent === 'k7m 2xq' && JSON.parse(env.session.data['ants.lobby']).c === 'k7m2xq');
    probe.receive(roomAs(['Pebble', 'Maya'], 1, PRIVATE));
    check('... it shows the Room, with the notice "Joined Pebble’s room." and the list put away', env.screen() === 'room' && stripText(env) === 'Joined Pebble’s room.' && popOf(env).hidden === true && env.$('room-h').textContent === 'Pebble’s room');
    check('... and as a guest of it the page has no switch, and the chip says what the room says', visOf(env).hidden === true && tagOf(env).textContent === 'Private room');
    check('(the page asked nobody for a name: the one the room showed went along)', env.$('name-step').hidden === true);
}
for (const [what, reason, words] of [['full', 1, 'That room is full.'], ['a match that has started', 3, 'The match in that room has already started.'], ['no such room (the game was taken off the list meanwhile)', 6, 'That game is not open any more.']]) {
    const env = await withList();
    const own = myRoom(env, ['Maya']);
    env.roomBar('rhome').click();
    await openGames(env);
    const looks = env.looks();
    tap(joinOf(rowsOf(env)[0]));
    env.last().open().receive(reject(reason));
    await flush();
    check('a room that is ' + what + ': "' + words + '" in the list, the own room is as it was (no Leave, open), the page is where it was', textOf(first(popOf(env), 'gpnote')) === words && first(popOf(env), 'gpnote').hidden === false && leaves(own) === 0 && !own.closed && env.screen() === 'home' && popOf(env).hidden === false);
    check('... the list is looked at again at once, and its buttons take presses again', env.looks() === looks + 1 && joinOf(rowsOf(env)[0]).disabled === false);
    check('... and the page costs nothing: no banner, the same session', env.$('banner').hidden === true && JSON.parse(env.session.data['ants.lobby']).c === helloRoom(own.sent[0]));
}
{
    const env = await withList();
    const own = myRoom(env, ['Maya']);
    env.roomBar('rhome').click();
    await openGames(env);
    tap(joinOf(rowsOf(env)[0]));
    env.last().open().receive(reject(2));
    await flush();
    check('a server that speaks another version: the page\'s own strip asks for a reload, the list says so too, the own room is kept', !env.$('banner').hidden && env.$('banner-x').textContent === 'Reload' && /newer version of Ants/.test(textOf(first(popOf(env), 'gpnote'))) && leaves(own) === 0);
}
{   // a server that does not answer
    const env = await withList();
    const own = myRoom(env, ['Maya']);
    env.roomBar('rhome').click();
    await openGames(env);
    tap(joinOf(rowsOf(env)[0]));
    const probe = env.last();
    await wait(env, 14500);
    check('a server that does not answer the second client: nothing yet at 14.5 s', textOf(first(popOf(env), 'gpnote')) === '' && joinOf(rowsOf(env)[0]).disabled === true);
    await wait(env, 500);
    check('... at 15 s the look is given up ("The server did not answer. Try again in a moment."), the second client is let go of, the own room is as it was and the buttons are back', textOf(first(popOf(env), 'gpnote')) === 'The server did not answer. Try again in a moment.' && probe.closed && leaves(own) === 0 && !own.closed && joinOf(rowsOf(env)[0]).disabled === false);
    probe.receive(welcome(1, KEY2, 0));
    check('... and a Welcome that comes after it does nothing (the page does not move house late)', leaves(own) === 0 && env.$('code').textContent === grouped(helloRoom(own.sent[0])));
}
{   // the question: others in the room
    const env = await withList();
    const own = myRoom(env, ['Maya', 'Sage']);
    env.roomBar('rhome').click();
    await openGames(env);
    tap(joinOf(rowsOf(env)[0]));
    const box = dialogOf(env);
    check('Join from a room that has another person in it asks first, in the approved card: "Leave your room?" "Sage is in your room. If you go, Sage becomes the host.", the game that is joined is shown, and nothing has happened yet', !!box
          && textOf(first(box, 'gctitle')) === 'Leave your room?' && textOf(first(box, 'gcline')) === 'Sage is in your room. If you go, Sage becomes the host.' && textOf(first(first(box, 'gitem'), 'gname')) === 'Pebble' && textOf(first(first(box, 'gitem'), 'gcount')) === '2/4'
          && textOf(first(box, 'gyes')) === 'Leave and join' && env.focused === first(box, 'gstay') && env.sockets.length === 1 && leaves(own) === 0);
    check('... the buttons of the row inside the card are not there (the card has the answers)', first(box, 'gjoin') === null);
    tap(first(box, 'gstay'));
    check('"Stay here": no second client, nothing left', dialogOf(env) === null && env.sockets.length === 1 && leaves(own) === 0 && popOf(env).hidden === false);
    tap(joinOf(rowsOf(env)[0]));
    pressEscape(env);
    check('Escape is "Stay here" and gives the focus back to the button that asked', dialogOf(env) === null && env.sockets.length === 1 && env.focused === joinOf(rowsOf(env)[0]));
    tap(joinOf(rowsOf(env)[0]));
    tap(first(dialogOf(env), 'gyes'));
    check('"Leave and join" opens the second client (and still leaves nothing: the old room goes when the new one welcomes the page)', dialogOf(env) === null && env.sockets.length === 2 && leaves(own) === 0 && hex(env.last().open().sent[0]) === hex(helloBytes('Maya', 'k7m2xq', { join: true })));
    env.last().receive(welcome(1, KEY2, 0));
    check('... then it leaves', leaves(own) === 1 && own.closed);
}
{   // three or more in the room: no names, the one who has been there longest
    const env = await withList();
    myRoom(env, ['Maya', 'Sage', 'Pip']);
    env.roomBar('rhome').click();
    await openGames(env);
    tap(joinOf(rowsOf(env)[0]));
    same('with two others the card names no one', textOf(first(dialogOf(env), 'gcline')), '2 people are in your room. If you go, the one who has been there longest becomes the host.');
}
{   // a guest: the question names the host
    const env = await withList(null, { search: '?room=zzz999', options: { stayHome: false } });
    const sock = guestRoom(env, 'Zed', PRIVATE);
    env.roomBar('rhome').click();
    await openGames(env);
    tap(joinOf(rowsOf(env)[0]));
    check('Join as a guest of somebody else asks too: "Leave Zed’s room?" with a line that says that the link brings the guest back while there is a free color', textOf(first(dialogOf(env), 'gctitle')) === 'Leave Zed’s room?' && /^You are in Zed’s room\. You can come back by its link while there is a free color\.$/.test(textOf(first(dialogOf(env), 'gcline'))) && leaves(sock) === 0);
}
{   // no room at all: nothing to leave, nothing to ask
    const env = await withList();
    await openGames(env);
    tap(joinOf(rowsOf(env)[1]));
    check('Join with no room asks nothing and opens the second client for the room of the row (the name that the page picked or remembered)', !dialogOf(env) && env.sockets.length === 1 && hex(env.last().open().sent[0]) === hex(helloBytes('Maya', 'abc234', { join: true })));
    env.last().receive(welcome(0 + 1, KEY2, 0));
    env.last().receive(roomAs(['Clover', 'Maya'], 1, PRIVATE));
    check('... it is the page\'s room when it welcomes it', env.screen() === 'room' && stripText(env) === 'Joined Clover’s room.' && env.$('code').textContent === 'abc 234');
}
{   // a room that is in the list as the page\'s own is not in it (no join of the own room); a START that waits
    const env = await withList();
    const own = myRoom(env, ['Maya', 'Sage'], STARTING, null, true);
    env.roomBar('rhome').click();
    await openGames(env);
    tap(joinOf(rowsOf(env)[0]));
    check('Join while START waits: "Your match is running." and no second client', stripText(env) === 'Your match is running.' && env.sockets.length === 1 && leaves(own) === 0 && popOf(env).hidden === true);
}
{   // Join by code: the page checks the code, the name card follows, then the second client
    const env = await withList();
    const own = myRoom(env, ['Maya']);
    const ownCode = helloRoom(own.sent[0]);
    env.roomBar('rhome').click();
    await openGames(env);
    const input = first(popOf(env), 'joinbox').children[0], go = first(popOf(env), 'gcgo'), hint = first(popOf(env), 'hint');
    check('Join by code is a field and a button at the bottom of the list (the page\'s own look of a code field), with a line under it', input.tagName === 'input' && go.textContent === 'Join' && input.getAttribute('placeholder') === 'Room code, like k7m 2xq' && input.getAttribute('aria-label') === 'Join by code'
          && input.getAttribute('aria-describedby') === 'gc-hint' && hint.getAttribute('role') === 'status' && hint.id === 'gc-hint');
    for (const [what, typed, why] of [['nothing', '', 'Type or paste the code first.'], ['a line of blanks', '   \n', 'Type or paste the code first.'], ['text that is no code', 'not a code!', 'A room code has letters and numbers only, like k7m 2xq.'],
                                       ['a path', 'k7m2/xq', 'A room code has letters and numbers only, like k7m 2xq.'], ['33 characters', 'x'.repeat(33), 'A room code has letters and numbers only, like k7m 2xq.']]) {
        input.value = typed; input.fire('input', {}); tap(go);
        check('typing ' + what + ' and pressing Join explains it under the field and opens no card, no socket, no question', hint.textContent === why && env.$('name-step').hidden && env.sockets.length === 1 && !dialogOf(env), hint.textContent);
    }
    input.value = '  ' + grouped(ownCode).toUpperCase() + ' '; tap(go);
    check('the code of the own room (as shown, in capitals) is "That is your own room."', hint.textContent === 'That is your own room.' && env.$('name-step').hidden && env.sockets.length === 1);
    input.fire('input', {});
    check('typing takes the line away', hint.textContent === '');
    input.value = 'K7M 2xq'; input.key('Enter');
    check('Enter asks for the name too: the card is up, titled with the code in two groups, and nothing connects yet', !env.$('name-step').hidden && env.$('name-step-title').textContent === 'Join the room k7m 2xq' && env.$('name-step-input').value === 'Maya' && env.sockets.length === 1 && input.value === 'K7M 2xq');
    env.type('name-step-input', 'Ann & <b>Bob</b>');
    env.$('name-step-go').click();
    const probe = env.last();
    check('a good name looks at the room: a second socket, a Hello that only joins (no lobby block), under the name typed (< > & are just characters); the own room stays', env.sockets.length === 2 && hex(probe.open().sent[0]) === hex(helloBytes('Ann & <b>Bob</b>', 'k7m2xq', { join: true })) && leaves(own) === 0 && !own.closed && go.disabled === true);
    probe.receive(reject(6));
    check('a room that does not exist: the line under the field says so, and the own room is as it was', hint.textContent === 'There is no room with that code. Check it, or ask the host to send the link again.' && !own.closed && go.disabled === false && env.innerHTMLWrites.length === 0);
    for (const [typed, want] of [['k7m2xq', 'k7m2xq'], ['k7m 2xq', 'k7m2xq'], ['  k7m 2xq  ', 'k7m2xq'], ['K7M 2XQ', 'k7m2xq'], ['my_room-1', 'my_room-1'], ['demo-small-2p-x7k2', 'demo-small-2p-x7k2'], ['a'.repeat(32), 'a'.repeat(32)]]) {
        const e = await withList();
        myRoom(e, ['Maya']);
        e.roomBar('rhome').click();
        await openGames(e);
        const field = first(popOf(e), 'joinbox').children[0];
        field.value = typed; tap(first(popOf(e), 'gcgo'));
        e.$('name-step-go').click();
        check('Join takes ' + JSON.stringify(typed) + ' as the code ' + want + ' (blanks dropped, capitals lowered): a joining Hello for it', e.sockets.length === 2 && hex(e.last().open().sent[0]) === hex(helloBytes('Maya', want, { join: true })));
    }
    for (const [what, reason, words] of [['full', 1, 'That room is full.'], ['a match that has started', 3, 'The match in that room has already started.']]) {
        const e = await withList();
        myRoom(e, ['Maya']);
        e.roomBar('rhome').click();
        await openGames(e);
        first(popOf(e), 'joinbox').children[0].value = 'k7m2xq'; tap(first(popOf(e), 'gcgo'));
        e.$('name-step-go').click();
        e.last().open().receive(reject(reason));
        check('a room that is ' + what + ': "' + words + '" under the field', textOf(first(popOf(e), 'hint')) === words);
    }
    const old = await withList();
    myRoom(old, ['Maya']);
    old.roomBar('rhome').click();
    await openGames(old);
    first(popOf(old), 'joinbox').children[0].value = 'k7m2xq'; tap(first(popOf(old), 'gcgo'));
    old.$('name-step-go').click();
    old.last().open().receive(reject(2));
    check('a server of another version: the strip asks for a reload', !old.$('banner').hidden && /A newer version of Ants is out/.test(old.$('banner-text').textContent) && old.$('banner-x').textContent === 'Reload');
    const ok = await withList();
    const okOwn = myRoom(ok, ['Maya']);
    ok.roomBar('rhome').click();
    await openGames(ok);
    first(popOf(ok), 'joinbox').children[0].value = 'k7m 2xq'; tap(first(popOf(ok), 'gcgo'));
    ok.env = null;
    ok.type('name-step-input', 'Zed');
    ok.$('name-step-go').click();
    const p2 = ok.last().open();
    p2.receive(welcome(1, KEY2, 0));
    p2.receive(roomAs(['Maya', 'Zed'], 1, PRIVATE));
    check('a room that takes the page: the own room is let go of then (a Leave, the socket closed), the Room shows that one, the list is put away and the field is empty', leaves(okOwn) === 1 && okOwn.closed && ok.screen() === 'room' && ok.$('code').textContent === 'k7m 2xq' && ok.$('room-h').textContent === 'Maya’s room'
          && popOf(ok).hidden === true && first(popOf(ok), 'joinbox').children[0].value === '' && stripText(ok) === 'Joined room k7m 2xq.');
    same('... this tab keeps that room (the code, the key, the name, and that the page did not make it)', JSON.parse(ok.session.data['ants.lobby']), { c: 'k7m2xq', k: hex(KEY2), n: 'Zed', o: 0 });
    const away = await withList();
    const awayOwn = myRoom(away, ['Maya']);
    away.roomBar('rhome').click();
    await openGames(away);
    first(popOf(away), 'joinbox').children[0].value = 'k7m2xq'; tap(first(popOf(away), 'gcgo'));
    away.$('name-step-own').click();
    check('"Start a room of my own instead" on the card closes it: the own room stays and Join is back', away.$('name-step').hidden && away.sockets.length === 1 && !awayOwn.closed && first(popOf(away), 'gcgo').disabled === false);
}
{   // Join by code with others in the room: the question first, then the name card
    const env = await withList();
    const own = myRoom(env, OTHERS);
    env.roomBar('rhome').click();
    await openGames(env);
    const input = first(popOf(env), 'joinbox').children[0];
    input.value = ''; tap(first(popOf(env), 'gcgo'));
    check('a code that is no code is explained before any question', !dialogOf(env) && textOf(first(popOf(env), 'hint')) === 'Type or paste the code first.');
    input.value = 'k7m2xq'; tap(first(popOf(env), 'gcgo'));
    check('a code that can be tried asks first when others are in the room, and the name card waits for the answer', !!dialogOf(env) && textOf(first(dialogOf(env), 'gctitle')) === 'Leave your room?' && env.$('name-step').hidden === true && first(popOf(env), 'gcgo').disabled === false);
    tap(first(dialogOf(env), 'gstay'));
    check('"Stay here": no card, no socket, Join is still on', env.$('name-step').hidden === true && env.sockets.length === 1 && first(popOf(env), 'gcgo').disabled === false);
    tap(first(popOf(env), 'gcgo'));
    tap(first(dialogOf(env), 'gyes'));
    check('"Leave and join": the name card comes, and the own room is still there', env.$('name-step').hidden === false && leaves(own) === 0 && !own.closed);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. START reaches the page; a script that is broken
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await withList();
    const sock = myRoom(env, OTHERS, PRIVATE, { you: 1, leader: 0 });
    env.roomBar('rhome').click();
    await openGames(env);
    const looks = env.looks();
    sock.receive(roomMessage(seatsOf(OTHERS), { you: 0, leader: 0, flags: STARTING }));
    check('START reaches a page whose list is open: the hand-off goes (the tab goes to the game page), Home says that the game is opening, and the list is put away', env.assigned.length === 1 && popOf(env).hidden === true && env.$('home').children.some((c) => c.textContent === 'Opening the game…' && !c.hidden));
    await wait(env, 14000);
    env.hide(true); env.hide(false);
    await wait(env, 500);
    tap(env.home('games'));
    check('... and nothing is asked of /lobbies any more, whatever the tab does, and Games opens nothing', env.looks() === looks && popOf(env).hidden === true);
    await wait(env, 14000);
    check('a START that has not taken the page away after 15 s has failed: the list is back (a look, and Games opens it again)', env.looks() === looks + 1 && (tap(env.home('games')), true) && popOf(env).hidden === false);
}
{
    const env = await page({ options: { lazy: { AntsHomeGames: { mount() { throw new Error('boom'); } } } } });
    check('a script that throws when it starts leaves the page as it was: the buttons stay disabled, Home and My room work, and the error does not reach the page\'s own handlers of the load', env.home('host').disabled && env.home('games').disabled && env.screen() === 'home' && env.lazyErrors.length === 0, JSON.stringify(env.lazyErrors));
    env.home('mine').click();
    const sock = env.arrive({ key: KEY, seats: seatsOf(['Maya', 'Sage']), room: { flags: PRIVATE } });
    check('... and a START that comes reaches the hand-off', (sock.receive(roomMessage(seatsOf(OTHERS), { you: 0, leader: 0, flags: STARTING })), env.assigned.length === 1));
}

done('public room, Quick host and Join');
})().catch(broke);
