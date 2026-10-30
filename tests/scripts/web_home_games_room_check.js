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
        PUBLIC, PRIVATE, STARTING, NET0, KEY, E, C, hex, roomMessage, welcome, reject, helloBytes, helloName, helloRoom, grouped, RULES0, done, broke, pressEscape, pressElsewhere } = R;

const KEY2 = Uint8Array.from(Array.from({ length: 16 }, (_, i) => 0x60 + i));
const visOf = (env) => first(env.root, 'vis');
const pressed = (env) => visOf(env).children.map((b) => b.getAttribute('aria-pressed'));
const state = (env) => pressed(env).join();
const noteOf = (env) => first(env.root, 'visnote');
const tagOf = (env) => first(env.root, 'vistag');
const gone = (sock) => sock.closed === true;
const touch = (env, kind) => (env.doc.capturing[kind || 'pointerdown'] || []).forEach((fn) => fn({}));       // (a press, a key or a touch of the person: what the repeat of a public room asks for)
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
{   // the repeat: every 10 minutes, only while the Room shows, the tab is visible and the leader looks at a public room (and is there: touches something now and then)
    const env = await page({ options: { fakeClock: true } });
    const sock = myRoom(env, OTHERS, PUBLIC);
    await wait(env, 599500);
    same('a public room that its leader looks at is told "public" again after 10 minutes, not before', visibilities(sock), []);
    await wait(env, 500);
    same('... after 10 minutes, once (the byte 1)', visibilities(sock), [1]);
    touch(env);
    await wait(env, 599500);
    same('... and not again until 10 more', visibilities(sock), [1]);
    await wait(env, 500);
    same('... every 10 minutes', visibilities(sock), [1, 1]);
    check('(the repeat is no change that anybody sees: the room is as it was and no strip, no hint)', stripOf(env).hidden === true && state(env) === 'false,true');
    touch(env);
    env.hide(true);
    await wait(env, 1500000);
    same('a tab that is out of sight says nothing, however long it stays so (25 minutes: nothing)', visibilities(sock), [1, 1]);
    env.hide(false);
    touch(env, 'keydown');
    await wait(env, 500);
    same('... and the tab that is back repeats it at once (more than 10 minutes have gone by)', visibilities(sock), [1, 1, 1]);
    env.roomBar('rhome').click();
    await wait(env, 1800000);
    same('Home shows (the Room does not): nothing is said in half an hour', visibilities(sock), [1, 1, 1]);
    env.home('mine').click();
    touch(env);
    await wait(env, 500);
    same('... and the Room back repeats it at once', visibilities(sock), [1, 1, 1, 1]);
    sock.receive(roomAs(OTHERS, 0, PRIVATE));
    await wait(env, 1300000);
    same('a room that is private is not told anything', visibilities(sock), [1, 1, 1, 1]);
}
{   // a leader who has gone away from a visible tab: the repeat does not keep the room public for ever
    const env = await page({ options: { fakeClock: true } });
    const sock = myRoom(env, OTHERS, PUBLIC);
    await wait(env, 1199500);
    same('nothing touched for 20 minutes less half a second: the repeat of the 10th minute went, and no other', visibilities(sock), [1]);
    await wait(env, 500);
    same('... and at 20 minutes the repeat that would have kept the room public a third 10 minutes is not made (the server\'s 30-minute idle rule goes on)', visibilities(sock), [1]);
    await wait(env, 3600000);
    same('... an hour more, a tab in sight all the time: nothing', visibilities(sock), [1]);
    check('(no timer waits for the next repeat: nothing is asked of the clock for an abandoned room)', env.timers.every((t) => t.at - env.clock < 500000));
    touch(env);
    await wait(env, 500);
    same('a touch of the person starts it again at once (it is long overdue)', visibilities(sock), [1, 1]);
    for (const kind of ['pointerdown', 'keydown', 'touchstart']) {
        const e2 = await page({ options: { fakeClock: true } });
        const s2 = myRoom(e2, OTHERS, PUBLIC);
        await wait(e2, 1300000);
        touch(e2, kind);
        await wait(e2, 500);
        same('(a ' + kind + ' is a touch of the person)', visibilities(s2), [1, 1]);
    }
    const e3 = await page({ options: { fakeClock: true } });
    const s3 = myRoom(e3, OTHERS, PUBLIC);
    await wait(e3, 900000);
    touch(e3);
    await wait(e3, 300000);
    same('a leader who touched something at the 15th minute is there at the 20th: the repeat goes (10 minutes, and 20)', visibilities(s3), [1, 1]);
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
{   // a leader of a private room, with others: it asks first, and then flips the room, the others stay
    const env = await page();
    const sock = myRoom(env, ['Maya', 'Sage', 'Pip']);
    env.roomBar('rhome').click();
    tap(env.home('host'));
    const box = dialogOf(env);
    check('Quick host with a private room of yours that has others in it asks first: "Make your room public?", who is in it, and the answer that changes nothing has the focus; nothing was sent', !!box && textOf(first(box, 'gctitle')) === 'Make your room public?'
          && textOf(first(box, 'gcline')) === 'Sage and Pip are in it. Anybody on the front page will be able to join it.' && textOf(first(box, 'gyes')) === 'Make it public' && textOf(first(box, 'gstay')) === 'Keep it private' && env.focused === first(box, 'gstay')
          && visibilities(sock).length === 0 && env.screen() === 'home');
    tap(first(box, 'gstay'));
    check('"Keep it private" changes nothing: the card is gone, nothing was sent, Home stays, no timer waits for the room to turn public', dialogOf(env) === null && visibilities(sock).length === 0 && env.screen() === 'home' && env.sockets.length === 1);
    tap(env.home('host'));
    pressEscape(env);
    check('Escape is "Keep it private"', dialogOf(env) === null && visibilities(sock).length === 0);
    tap(env.home('host'));
    tap(first(dialogOf(env), 'gyes'));
    check('"Make it public" flips it: Visibility(public) at once, the Room shows, no second room, nobody leaves', visibilities(sock).length === 1 && visibilities(sock)[0] === 1 && env.screen() === 'room' && env.sockets.length === 1 && leaves(sock) === 0);
    sock.receive(roomAs(['Maya', 'Sage', 'Pip'], 0, PUBLIC));
    same('... and says who stays in it, as the picture does', stripText(env), 'Your room is public now. It shows in Open games; Sage and Pip stay in it.');
    const one = await page();
    const s1 = myRoom(one, ['Maya', 'Sage']);
    one.roomBar('rhome').click();
    tap(one.home('host'));
    check('(one other: the card names him alone, "Sage is in it.")', textOf(first(dialogOf(one), 'gcline')) === 'Sage is in it. Anybody on the front page will be able to join it.');
    tap(first(dialogOf(one), 'gyes'));
    s1.receive(roomAs(['Maya', 'Sage'], 0, PUBLIC));
    same('(and "Sage stays in it")', stripText(one), 'Your room is public now. It shows in Open games; Sage stays in it.');
    const alone = await page();
    const sa = myRoom(alone, ['Maya']);
    alone.roomBar('rhome').click();
    tap(alone.home('host'));
    check('a leader who is alone in the room is not asked: it flips at once', dialogOf(alone) === null && visibilities(sa).length === 1 && alone.screen() === 'room');
    const bots = await page();
    bots.home('mine').click();
    const sb = bots.arrive({ key: KEY, seats: [[C, 'Maya'], [NET0.SLOT.Bot, 'Easy'], [E, ''], [E, '']], room: { flags: PRIVATE } });
    bots.roomBar('rhome').click();
    tap(bots.home('host'));
    check('(a computer player is not a person who is asked about: only people are in the question)', dialogOf(bots) === null && visibilities(sb).length === 1);
    const press = await page();
    const sp = myRoom(press, ['Maya', 'Sage']);
    press.roomBar('rhome').click();
    tap(press.home('host'));
    sp.receive(roomMessage(seatsOf(['Maya', 'Sage']), { you: 0, leader: 0, flags: STARTING }));
    check('START that reaches the page while the question is up takes the question away and nothing is sent', dialogOf(press) === null && visibilities(sp).length === 0);
}
{   // already public
    const env = await page();
    const sock = myRoom(env, OTHERS, PUBLIC);
    env.roomBar('rhome').click();
    tap(env.home('host'));
    check('Quick host with a room that is public already sends nothing, says so and shows the Room', visibilities(sock).length === 0 && stripText(env) === 'Your room is already public.' && env.screen() === 'room' && env.sockets.length === 1);
}
{   // START reaches a guest while the card "Leave ... and host your own?" is up
    const env = await page({ search: '?room=k7m2xq', options: { stayHome: false } });
    const sock = guestRoom(env, 'Pebble', PUBLIC);
    env.roomBar('rhome').click();
    tap(env.home('host'));
    const rbar = env.root.children.filter((c) => classes(c).indexOf('rbar') !== -1)[0];
    check('(the card is up, and the bar is inert behind it)', dialogOf(env) !== null && env.home('hbar').hasAttribute('inert'));
    sock.receive(roomMessage(seatsOf(['Pebble', 'Maya']), { you: 1, leader: 0, flags: STARTING }));
    check('START reaches the page: the card is gone, the hand-off goes, and both bars stay inert (the card gives back what it took before the hand-off holds them)', env.assigned.length === 1 && dialogOf(env) === null && env.home('hbar').hasAttribute('inert') && rbar.hasAttribute('inert'));
    await wait(env, 16000);
    check('... and 15 s later, when Home takes presses again, they are free', !env.home('hbar').hasAttribute('inert') && !rbar.hasAttribute('inert') && env.home('host').disabled === true && env.home('games').disabled === true);
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
    await wait(env, 2500);
    await openGames(env);
    const looks = env.looks();
    tap(joinOf(rowsOf(env)[0]));
    env.last().open().receive(reject(reason));
    await flush();
    await wait(env, 1000);
    check('a room that is ' + what + ': "' + words + '" in the list, the own room is as it was (no Leave, open), the page is where it was', textOf(first(popOf(env), 'gpnote')) === words && first(popOf(env), 'gpnote').hidden === false && leaves(own) === 0 && !own.closed && env.screen() === 'home' && popOf(env).hidden === false);
    check('... the list is looked at again (a second apart at most from the look at its opening), and its buttons take presses again', env.looks() === looks + 1 && joinOf(rowsOf(env)[0]).disabled === false);
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
    check('"Stay here": no second client, nothing left, the list stays open (a press on the card is not a press elsewhere) and the focus is back on the Join that asked', dialogOf(env) === null && env.sockets.length === 1 && leaves(own) === 0 && popOf(env).hidden === false && env.focused === joinOf(rowsOf(env)[0]));
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
        check('... and the list stays open to show it (the press on the name card is not a press elsewhere)', popOf(e).hidden === false);
    }
    const old = await withList();
    myRoom(old, ['Maya']);
    old.roomBar('rhome').click();
    await openGames(old);
    first(popOf(old), 'joinbox').children[0].value = 'k7m2xq'; tap(first(popOf(old), 'gcgo'));
    old.$('name-step-go').click();
    old.last().open().receive(reject(2));
    check('a server of another version: the strip asks for a reload', !old.$('banner').hidden && /A newer version of Ants is out/.test(old.$('banner-text').textContent) && old.$('banner-x').textContent === 'Reload');
    check('... and the list is still open, with the words under the field', popOf(old).hidden === false && /newer version of Ants/.test(textOf(first(popOf(old), 'hint'))));
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

{   // "Leave and join" into a room that cannot be joined: the refusal is shown in the list that stays open
    for (const [what, reason, words] of [['full', 1, 'That room is full.'], ['a match that has started', 3, 'The match in that room has already started.']]) {
        const env = await withList();
        const own = myRoom(env, OTHERS);
        env.roomBar('rhome').click();
        await openGames(env);
        tap(joinOf(rowsOf(env)[0]));
        tap(first(dialogOf(env), 'gyes'));
        check('after "Leave and join" the list stays open (the press on the card is not a press elsewhere)', popOf(env).hidden === false && first(popOf(env), 'gjoining').hidden === false);
        env.last().open().receive(reject(reason));
        await flush();
        check('a room that is ' + what + ' (Leave and join): "' + words + '" is in the list that is open, the own room is kept', textOf(first(popOf(env), 'gpnote')) === words && popOf(env).hidden === false && leaves(own) === 0 && !own.closed && first(popOf(env), 'gjoining').hidden === true);
    }
}
{   // a Join that is on its way says so, with a way out
    const env = await withList();
    const own = myRoom(env, ['Maya']);
    env.roomBar('rhome').click();
    await openGames(env);
    const bar = first(popOf(env), 'gjoining');
    check('(no Join is on its way: the line is not there)', bar.hidden === true);
    tap(joinOf(rowsOf(env)[0]));
    const probe = env.last().open();
    check('a Join says what it waits for ("Joining Pebble’s room…", as a status) with a Cancel button, and the buttons wait', bar.hidden === false && bar.getAttribute('role') === 'status' && textOf(bar.children[0]) === 'Joining Pebble’s room…' && textOf(bar.children[1]) === 'Cancel' && joinOf(rowsOf(env)[0]).disabled === true);
    check('(the Cancel stands where the Join button stood: it takes no press for a moment, so that the second press of a double press is not a Cancel)', bar.children[1].disabled === true);
    tap(bar.children[1]);
    check('a press on it in that moment does nothing: the Join goes on (the second client is there, nothing was left, the line is shown)', bar.hidden === false && !probe.closed && leaves(probe) === 0 && joinOf(rowsOf(env)[0]).disabled === true);
    await wait(env, 699);
    check('... not before 700 ms', bar.children[1].disabled === true);
    await wait(env, 1);
    check('... then Cancel can be pressed', bar.children[1].disabled === false);
    await wait(env, 5000);
    check('... for as long as the look takes (a second client waits up to 15 s)', bar.hidden === false && bar.children[1].disabled === false);
    tap(bar.children[1]);
    check('Cancel: the line goes, the buttons are back, the second client is let go of (a Leave, closed), the own room is as it was, and the focus is in the field', bar.hidden === true && joinOf(rowsOf(env)[0]).disabled === false && leaves(probe) === 1 && probe.closed && leaves(own) === 0 && !own.closed && env.focused === first(popOf(env), 'joinbox').children[0]);
    probe.receive(welcome(1, KEY2, 0));
    await wait(env, 20000);
    check('a Welcome (or the 15 s of silence) that comes after Cancel changes nothing: no notice, no Room, no words, the own room stays', env.screen() === 'home' && own.closed === false && leaves(own) === 0 && stripOf(env).hidden === true && textOf(first(popOf(env), 'gpnote')) === '' && bar.hidden === true);
    tap(joinOf(rowsOf(env)[0]));
    check('(and a new Join can be made)', env.sockets.length === 3 && bar.hidden === false && bar.children[1].disabled === true);
    await wait(env, 800);
    env.site.body = listOf([row('k7m2xq', 'Pebble', { map: 'ISLANDS.LVL', people: 3, free: 1 }), row('abc234', 'Clover', { people: 1, bots: 1, free: 2 })]);
    await wait(env, 5500);
    check('a look that finds the rooms changed while a Join waits draws the rows again, and does not take the Cancel away again for 700 ms (it is not a new Join)', textOf(first(rowsOf(env)[0], 'gcount')) === '3/4' && bar.hidden === false && bar.children[1].disabled === false && joinOf(rowsOf(env)[0]).disabled === true);
    tap(bar.children[1]);
    check('(Cancel again)', bar.hidden === true);
    const typed = await withList();
    myRoom(typed, ['Maya']);
    typed.roomBar('rhome').click();
    await openGames(typed);
    first(popOf(typed), 'joinbox').children[0].value = 'k7m2xq'; tap(first(popOf(typed), 'gcgo'));
    typed.$('name-step-go').click();
    const tp = typed.last().open();
    const tbar = first(popOf(typed), 'gjoining');
    check('Join by code says "Joining the room…" too, with its Cancel', tbar.hidden === false && textOf(tbar.children[0]) === 'Joining the room…');
    await wait(typed, 800);
    tap(tbar.children[1]);
    check('... and Cancel lets the second client go (the name card was answered already)', tbar.hidden === true && leaves(tp) === 1 && tp.closed && first(popOf(typed), 'gcgo').disabled === false);
    await wait(typed, 20000);
    check('(the 15 s of silence of a look that was given up say nothing under the field)', textOf(first(popOf(typed), 'hint')) === '' && tbar.hidden === true);
}
{   // the moment of a Join that waits belongs to that Join: a Join that was refused at once leaves no timer behind that frees the Cancel of the next one early
    const env = await withList();
    myRoom(env, ['Maya']);
    env.roomBar('rhome').click();
    await wait(env, 2500);
    await openGames(env);
    const bar = first(popOf(env), 'gjoining');
    tap(joinOf(rowsOf(env)[0]));
    env.last().open().receive(reject(1));
    await flush();
    await wait(env, 100);
    tap(joinOf(rowsOf(env)[0]));
    check('(a second Join, a moment after the first was refused: its Cancel waits)', bar.hidden === false && bar.children[1].disabled === true);
    await wait(env, 650);
    check('the Cancel of the second Join is still off 650 ms after it began (the first Join\'s 700 ms ran out 550 ms after the second began, and are nobody\'s)', bar.children[1].disabled === true);
    await wait(env, 100);
    check('... and it is on 700 ms after the second began', bar.children[1].disabled === false);
}
{   // MUST: a press inside the page's cards is not a press elsewhere, and the list stays open for the words that tell why a Join did not go
    const env = await withList(null, { search: '?room=zzz999', options: { stayHome: false } });
    const sock = guestRoom(env, 'Zed', PRIVATE);
    env.roomBar('rhome').click();
    await openGames(env);
    pressElsewhere(env, env.$('start'));
    check('(a press that really is elsewhere closes the list)', popOf(env).hidden === true);
}
{   // a match for one: the page is on its way to the game page, and Home makes no room and joins none, and says why (a navigation that is stopped leaves the page here)
    const env = await withList();
    const own = myRoom(env, ['Maya']);
    env.$('start').click();
    check('(START with nobody else goes to the game page on its own, and lets the room go)', env.assigned.length === 1 && own.closed === true);
    env.roomBar('rhome').click();
    check('(Home shows)', env.screen() === 'home');
    const before = env.sockets.length;
    tap(env.home('host'));
    check('Quick host does nothing while the page is on its way to a game: no socket, no Room, no room made; the strip says that the game is opening, with a Reload button', env.sockets.length === before && env.screen() === 'home' && dialogOf(env) === null
          && stripText(env) === 'The game is opening. If it does not, reload this page.' && stripOf(env).className.indexOf('warn') !== -1 && textOf(stripButtons(env)[0]) === 'Reload' && stripButtons(env).length === 2);
    tap(stripButtons(env)[0]);
    check('Reload reloads the page (a navigation that did not come leaves the page here: that is the way back)', env.reloads === 1);
    tap(stripButtons(env)[1]);
    check('(OK takes the strip away)', stripOf(env).hidden === true);
    await openGames(env);
    check('(the list opens)', popOf(env) !== null && popOf(env).hidden === false && rowsOf(env).length === 2);
    tap(joinOf(rowsOf(env)[0]));
    check('Join does nothing either (no second client) and says so; the list is put away', env.sockets.length === before && stripText(env) === 'The game is opening. If it does not, reload this page.' && popOf(env).hidden === true);
    tap(stripButtons(env)[1]);
    await openGames(env);
    const input = first(popOf(env), 'joinbox').children[0];
    input.value = 'k7m2xq'; tap(first(popOf(env), 'gcgo'));
    check('Join by code does nothing either (no name card, no second client) and says so', env.sockets.length === before && env.$('name-step').hidden === true && stripText(env) === 'The game is opening. If it does not, reload this page.');
}
{   // the page's bridge as the list sees it: a "handing" that throws is a page that is not on its way; a Cancel that comes when the second client is the page's own client lets nothing go
    let bridge = null;
    const site = fakeSite();
    site.body = listOf([row('k7m2xq', 'Pebble', { map: 'ISLANDS.LVL', people: 2, free: 2 })]);
    const wrapper = { mount: (e) => R.Games.mount(Object.assign({}, e, { page: (bridge = Object.assign(Object.create(e.page), { handing() { throw new Error('the page cannot say'); } })) })) };
    const env = await page({ site, options: { lazy: { AntsHomeGames: wrapper } } });
    tap(env.home('host'));
    check('a page whose handing() throws is a page that is not on its way: Quick host makes the room (and the error is not the user\'s)', env.sockets.length === 1 && env.screen() === 'room');
    env.roomBar('rhome').click();
    await openGames(env);
    tap(joinOf(rowsOf(env)[0]));
    const probe = env.last().open();
    probe.receive(welcome(1, KEY2, 0));
    probe.receive(roomAs(['Pebble', 'Maya'], 1, PRIVATE));
    check('(the Join went: the page is in the other room, and that second client is the page\'s own client now)', env.screen() === 'room' && env.$('room-h').textContent === 'Pebble’s room');
    bridge.cancel();
    check('a Cancel that reaches the page when the second client has become its own client lets nothing go (no Leave, the socket open)', leaves(probe) === 0 && !probe.closed);
}
{   // a second client that is welcomed by a page that is on its way to a game is let go of at once, and says nothing
    const env = await withList();
    const own = myRoom(env, ['Maya']);
    env.roomBar('rhome').click();
    await openGames(env);
    tap(joinOf(rowsOf(env)[0]));
    const probe = env.last().open();
    env.home('mine').click();
    env.$('start').click();
    check('(START with nobody else goes to the game page while the second client waits)', env.assigned.length === 1);
    probe.receive(welcome(1, KEY2, 0));
    check('the Welcome of a room that the page cannot take any more: the second client leaves at once (a Leave, closed), and nothing changes in the page', leaves(probe) === 1 && probe.closed && env.assigned.length === 1);
}
{   // a Private that the server did not take is not the cause of a room that goes private half an hour later; the hint of a held back press goes by itself
    const env = await page({ options: { fakeClock: true } });
    const sock = myRoom(env, ['Maya', 'Sage'], PUBLIC);
    sock.receive(roomAs(['Maya', 'Sage'], 0, PUBLIC));
    tap(visOf(env).children[0]);
    await wait(env, 11000);
    sock.receive(roomAs(['Maya', 'Sage'], 0, PRIVATE));
    check('a Private press that nobody answered for 10 s is forgotten: the room that goes private then says so ("went private after 30 minutes") with its Make it public button', stripText(env) === 'Your room went private after 30 minutes. Make it public again?' && stripButtons(env).length === 2);
    const quick = await page({ options: { fakeClock: true } });
    const qs = myRoom(quick, ['Maya', 'Sage'], PUBLIC);
    qs.receive(roomAs(['Maya', 'Sage'], 0, PUBLIC));
    tap(visOf(quick).children[0]);
    qs.receive(roomAs(['Maya', 'Sage'], 0, PRIVATE));
    check('(a Private that was answered is the leader\'s own: nothing is said)', stripOf(quick).hidden === true);
    const hold = await page({ options: { fakeClock: true } });
    const hs = myRoom(hold, ['Maya']);
    tap(visOf(hold).children[1]); hs.receive(roomAs(['Maya'], 0, PUBLIC)); tap(visOf(hold).children[0]); hs.receive(roomAs(['Maya'], 0, PRIVATE));
    tap(visOf(hold).children[1]);
    check('(a press that the page\'s pace holds back says "One moment, then press again.")', noteOf(hold).textContent === 'One moment, then press again.');
    await wait(hold, 1500);
    check('... and the words go by themselves when the moment is over (nobody has to wait for a message of the room), and the switch can be pressed again', noteOf(hold).textContent === 'Public rooms show your name and map in the Games list.' && visOf(hold).children.every((b) => b.disabled === false));
}
{   // the notice of the list is behind the question card too
    const env = await withList();
    myRoom(env, ['Maya', 'Sage'], PUBLIC);
    env.roomBar('rhome').click();
    tap(env.home('host'));
    env.roomBar('rhome').click();
    check('(a notice of the list stands, and takes presses)', stripOf(env).hidden === false && !stripOf(env).hasAttribute('inert'));
    await openGames(env);
    tap(joinOf(rowsOf(env)[0]));
    check('the question is up ("Leave your room?"): the notice, which is outside Home, is behind it and takes no key or tap', dialogOf(env) !== null && stripOf(env).hasAttribute('inert'));
    tap(first(dialogOf(env), 'gstay'));
    check('... and takes presses again when the question is answered', !stripOf(env).hasAttribute('inert') && stripOf(env).hidden === false);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. START reaches the page; a script that is broken
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const SLOW = 'The game is slow to open. Reload this page to get your seat back.', FAILED = 'The game did not open. Reload this page to get your seat back.';
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
    const banner = env.$('banner'), words = env.$('banner-text');
    check('a START that has not taken the page away after 15 s is slow (the page\'s strip says "slow to open", Home takes presses again): the buttons that need the page\'s room are OFF, each with that reason as its title, and nothing is asked of /lobbies', banner.hidden === false && words.textContent === SLOW
          && [env.home('host'), env.home('games')].every((b) => b.disabled === true && b.getAttribute('aria-disabled') === 'true' && b.getAttribute('title') === SLOW) && env.looks() === looks);
    tap(env.home('games')); env.home('games').click(); env.home('host').click();
    check('... a press that a browser did let through opens no list, asks no question, makes no room and no notice, and sends no Leave (not for a guest: Quick host would ask "Leave ... room and host your own?")', popOf(env).hidden === true && dialogOf(env) === null && stripOf(env).hidden === true && env.sockets.length === 1 && leaves(sock) === 0 && env.looks() === looks);
    await wait(env, 32000);
    check('a minute after START the strip says that the game did not open, and the buttons say the same; still no look at the list, and nothing was polled in the 30 s or the 5 s that the list used to wait', words.textContent === FAILED && [env.home('host'), env.home('games')].every((b) => b.disabled === true && b.getAttribute('title') === FAILED) && env.looks() === looks);
    await wait(env, 330000);
    env.hide(true); env.hide(false);
    await wait(env, 1000);
    check('... five more minutes, and a tab that goes out of sight and comes back: nothing is asked of /lobbies and nothing is sent (the page is frozen until it is reloaded)', env.looks() === looks && leaves(sock) === 0 && visibilities(sock).length === 0 && popOf(env).hidden === true && env.sockets.length === 1);
}
{   // the leader of a public room starts: the hand-off goes, the switch cannot be pressed, the repeat is over, and the page says why (as the guest's buttons do)
    const SLOW = 'The game is slow to open. Reload this page to get your seat back.', FAILED = 'The game did not open. Reload this page to get your seat back.';
    const env = await page();
    const sock = myRoom(env, OTHERS, PUBLIC);
    sock.receive(roomAs(OTHERS, 0, PUBLIC));
    const vis = visOf(env);
    check('(the leader of a public room: the switch can be pressed)', vis.hidden === false && vis.children.every((b) => b.disabled === false));
    env.roomBar('rhome').click();
    tap(env.home('host'));
    check('(a notice of the list stands: Quick host on a public room says so)', stripText(env) === 'Your room is already public.' && stripButtons(env).length === 1);
    sock.receive(roomMessage(seatsOf(OTHERS), { you: 0, leader: 0, flags: STARTING }));
    check('START reaches the page: the hand-off goes, the notice is taken away (its OK would take no press), and the switch is off at once', env.assigned.length === 1 && stripOf(env).hidden === true && vis.children.every((b) => b.disabled === true));
    vis.children[0].click(); vis.children[1].click();
    same('(a press that got through sends nothing)', visibilities(sock), []);
    await wait(env, 15000);
    check('15 s later the page says that the game is slow to open, and the line under the switch says so too (the switch stays off)', env.$('banner-text').textContent === SLOW && noteOf(env).textContent === SLOW && noteOf(env).hidden === false && vis.children.every((b) => b.disabled === true));
    await wait(env, 45000);
    check('... and "did not open" a minute after START, on both', env.$('banner-text').textContent === FAILED && noteOf(env).textContent === FAILED && vis.children.every((b) => b.disabled === true));
    await wait(env, 660000);
    same('... and the repeat that keeps a room public does not run for a page that is frozen (not in 11 minutes, a tab in sight)', visibilities(sock), []);
}
{   // START comes before the script of the list does, and the 15 s are over before it comes: it mounts into a page that waits for the game, and says so at once
    const SLOW = 'The game is slow to open. Reload this page to get your seat back.';
    const site = fakeSite();
    const env = R.runLobby('', { 'ants.name': 'Maya' }, { stayHome: true, fakeClock: true, fetch: site.fetch });
    env.advance(0);
    const parts = () => env.body.children.filter((c) => (c.tagName === 'script' && c.src === 'home/games.js') || (c.tagName === 'link' && c.href === 'home/games.css'));
    check('(the script and the style of the list were asked for, and have not come)', parts().length === 2 && env.home('host').disabled === true);
    env.home('mine').click();
    const sock = env.arrive({ key: KEY, seats: seatsOf(OTHERS), room: { flags: PRIVATE } });
    env.roomBar('rhome').click();
    sock.receive(roomMessage(seatsOf(OTHERS), { you: 0, leader: 0, flags: STARTING }));
    await wait(env, 16000);
    check('(START came while Home showed, and was not taken: the page says that the game is slow)', env.screen() === 'home' && env.assigned.length === 1 && env.$('banner-text').textContent === SLOW);
    env.win.AntsHomeGames = R.Games;
    parts().forEach((c) => c.onload && c.onload({}));
    await flush();
    check('the list comes now: its buttons are off with the same reason, and it asked for nothing (Home shows, and a list that is mounted on Home looks at once: this one does not)', [env.home('host'), env.home('games')].every((b) => b.disabled === true && b.getAttribute('title') === SLOW) && site.calls.length === 0);
    env.home('games').click();
    await wait(env, 60000);
    check('... even a press that got through opens nothing, and a minute later still nothing was asked of the list', popOf(env).hidden === true && site.calls.length === 0 && env.lazyErrors.length === 0);
}
{   // a script that comes while START is on its way (not slow yet) waits as well
    const site = fakeSite();
    const env = R.runLobby('', { 'ants.name': 'Maya' }, { stayHome: true, fakeClock: true, fetch: site.fetch });
    env.advance(0);
    env.home('mine').click();
    const sock = env.arrive({ key: KEY, seats: seatsOf(OTHERS), room: { flags: PRIVATE } });
    env.roomBar('rhome').click();
    sock.receive(roomMessage(seatsOf(OTHERS), { you: 0, leader: 0, flags: STARTING }));
    env.win.AntsHomeGames = R.Games;
    env.body.children.filter((c) => (c.tagName === 'script' && c.src === 'home/games.js') || (c.tagName === 'link' && c.href === 'home/games.css')).forEach((c) => c.onload && c.onload({}));
    await flush();
    env.home('games').click(); env.home('host').click();
    check('a list that comes after START (and before the 15 s), while Home shows, takes no press and asks for nothing: no look, no list, no notice', site.calls.length === 0 && popOf(env).hidden === true && stripOf(env).hidden === true && env.sockets.length === 1);
    await wait(env, 30000);
    check('... and nothing is asked in the 30 s that follow (the 15 s of the hand-off are over: the buttons are off)', site.calls.length === 0 && env.home('games').disabled === true && env.home('host').disabled === true);
}
{   // a script that comes while a dialog of the page is open is under it as well
    const site = fakeSite();
    const env = R.runLobby('', { 'ants.name': 'Maya' }, { stayHome: true, fakeClock: true, fetch: site.fetch });
    env.advance(0);
    env.home('mine').click();
    env.arrive({ key: KEY, seats: seatsOf(['Maya']), room: { flags: PUBLIC } });
    env.roomBar('rhome').click();
    env.$('how-open').click();
    env.win.AntsHomeGames = R.Games;
    env.body.children.filter((c) => (c.tagName === 'script' && c.src === 'home/games.js') || (c.tagName === 'link' && c.href === 'home/games.css')).forEach((c) => c.onload && c.onload({}));
    await flush();
    env.home('host').click();
    check('How it works is open when the list arrives: a notice that it makes then is inert with the rest of the page', stripText(env) === 'Your room is already public.' && stripOf(env).hasAttribute('inert'));
    env.$('how-close').click();
    check('... and takes presses again when the dialog is closed', !stripOf(env).hasAttribute('inert'));
}
{   // a dialog of the page (How it works, the map list) and the notice of the list: the strip is behind it, so it takes no key or tap
    const env = await page();
    myRoom(env, ['Maya'], PUBLIC);
    env.roomBar('rhome').click();
    tap(env.home('host'));
    check('(Quick host on a public room says so)', stripText(env) === 'Your room is already public.');
    env.$('how-open').click();
    check('How it works is open: the strip of the list is inert with the rest of the page (a Tab does not reach its OK)', stripOf(env).hasAttribute('inert') && env.$('how').hidden === false && !env.$('how').hasAttribute('inert'));
    env.$('how-close').click();
    check('... and it takes presses again when the dialog is closed', !stripOf(env).hasAttribute('inert') && stripText(env) === 'Your room is already public.');
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
