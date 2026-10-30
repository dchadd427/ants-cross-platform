// Runs QUICK JOIN and the question before LEAVE ROOM (web/home/games.js with web/lobby.html's own script, docs/HOME_SCREEN.md) in the fake browser of tests/scripts/web_page_harness.js, with a scripted game server and a fake
// GET /lobbies (the rig is tests/scripts/web_home_games_rig.js):
//   1. the order of the rooms (the fullest that still has a free seat, then the oldest, then the list's own order), the BEST tag, "updates live";
//   2. the way that works: the card "Finding a game...", the second client for the best room, the card "Joined Pebble's room", the Room that opens by itself or at the press of its button, no strip, nothing left of the old room
//      before the new one welcomes the page;
//   3. the rooms that cannot be had (full, started, gone) and the next ones, at most three, the words when nothing is left, and what stops the tries (another version, no answer);
//   4. nothing to join (an empty list, a list that is off), Host one!, Not now;
//   5. the own room: left out by its code, asked about first when others are in it, kept until the Welcome;
//   6. Cancel (its 700 ms, the second client let go of, no seat left behind), Escape, a double press, going Home and back, START, a stalled hand-off, a page that is on its way to a game, a match that runs;
//   7. Leave room: asks only when other people are in the room, the same card, keyboard and touch.
// tests/scripts/test_web_home_games.py runs this with node (the quick tier). usage: node web_home_games_quickjoin_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const R = require('./web_home_games_rig.js')(process.argv[2]);
const { check, same, flush, fakeSite, row, listOf, page, wait, classes, find, first, home, tap, pressEscape, popOf, rowsOf, joinOf, textOf, stripOf, stripText, stripButtons, pillOf, dialogOf, leaves, seatsOf, myRoom, guestRoom, roomAs, Games,
        PUBLIC, PRIVATE, STARTING, NET0, KEY, E, C, hex, roomMessage, welcome, reject, helloBytes, helloRoom, grouped, RULES0, done, broke } = R;

const KEY2 = Uint8Array.from(Array.from({ length: 16 }, (_, i) => 0x60 + i));
const BEST = row('p4n8dz', 'Pebble', { map: 'ISLANDS.LVL', people: 3, free: 1, age: 312 });
const ROWS = [row('f3rn22', 'Fern', { map: 'MEDIUM.LVL', people: 1, free: 3, age: 30 }), row('b4s1l2', 'Basil', { map: 'SMALL.LVL', people: 2, free: 2, age: 90 }), row('j7n1pr', 'Juniper', { map: 'GAUNTLET.LVL', people: 1, bots: 1, free: 2, age: 120 }),
              row('c1v3rr', 'Clover', { map: 'TREASURE.LVL', people: 2, bots: 1, free: 1, age: 200 }), BEST];        // (in the list's own order, which is not the best first)
const CODES = ['p4n8dz', 'c1v3rr', 'j7n1pr', 'b4s1l2', 'f3rn22'];                     // (the best first)
const card = (env) => dialogOf(env);
const title = (env) => (card(env) ? textOf(first(card(env), 'gctitle')) : null);
const lineOf = (env) => (card(env) ? textOf(first(card(env), 'gcline')) : null);
const act = (env, cls) => (card(env) ? first(card(env), cls) : null);
const askedFor = (sock) => helloRoom(sock.sent[0] || sock.open().sent[0]);          // the room that a second client asks for (its Hello)
const welcomeOf = (sock, seat, host, flags) => { sock.open(); sock.receive(welcome(seat, KEY2, 0)); sock.receive(roomAs(host, seat, flags === undefined ? PRIVATE : flags)); };
const withRooms = async (rows, o) => { const site = fakeSite(); site.body = listOf(rows); const env = await page(Object.assign({ site }, o || {})); env.site = site; return env; };
const press = async (env) => { tap(env.home('join')); await flush(); };

(async () => {

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The order, the BEST tag, "updates live"
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const rows = [row('aaa', 'A', { free: 2, age: 10 }), row('bbb', 'B', { free: 1, age: 10 }), row('ccc', 'C', { free: 1, age: 50 }), row('ddd', 'D', { free: 3, age: 999 }), row('eee', 'E', { free: 1, age: 50 })];
    same('the fullest room that still has a free seat is the best (the fewest free seats), then the oldest, and where those are the same the list\'s own order', Games.bestOrder(rows).map((r) => r.code), ['ccc', 'eee', 'bbb', 'aaa', 'ddd']);
    same('(a list that is in order stays so, and nothing is changed in the list that was given)', [Games.bestOrder(Games.bestOrder(rows)).map((r) => r.code), rows.map((r) => r.code)], [['ccc', 'eee', 'bbb', 'aaa', 'ddd'], ['aaa', 'bbb', 'ccc', 'ddd', 'eee']]);
    same('an empty list is an empty list', Games.bestOrder([]), []);
    const env = await withRooms(ROWS);
    tap(env.home('games'));
    await flush();
    same('the Games list shows the rooms best first (not as the server listed them)', rowsOf(env).map((li) => textOf(first(li, 'gname'))), ['Pebble', 'Clover', 'Juniper', 'Basil', 'Fern']);
    check('the first row (what Quick join takes) is tagged BEST, beside the name and the map\'s name in the line of the name (a narrow row wraps it under, as the phone\'s picture draws it), and no other row is', first(rowsOf(env)[0], 'gbest') !== null && textOf(first(rowsOf(env)[0], 'gbest')) === 'BEST' && find(popOf(env), 'gbest').length === 1
          && first(rowsOf(env)[0], 'gbest').parent === first(rowsOf(env)[0], 'gtop') && first(rowsOf(env)[0], 'gtop').children.map((c) => classes(c)[0]).join() === 'gname,gmapname,gbest');
    check('the heading says that the list is live ("5 waiting · updates live")', textOf(first(popOf(env), 'gpcount')) === '5 waiting · updates live');
    env.site.reject = true;
    await wait(env, 12000);
    check('... and not while the looks fail (it is not live then; the rooms that were are still there)', textOf(first(popOf(env), 'gpcount')) === '' || textOf(first(popOf(env), 'gpcount')).indexOf('updates live') === -1);
    const one = await withRooms([BEST]);
    tap(one.home('games'));
    await flush();
    check('a list of one room has no BEST tag (a best of one is no choice to point at)', find(popOf(one), 'gbest').length === 0 && textOf(first(popOf(one), 'gpcount')) === '1 waiting · updates live');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. The way that works
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const quick = await withRooms(ROWS);
    const first = quick.looks();
    await press(quick);
    check('Quick join within a second of the look that the page made when it opened uses that look (two looks are a second apart at least): no new look, and the room asked for is the best of it', first === 1 && quick.looks() === 1 && title(quick) === 'Finding a game…' && askedFor(quick.last()) === 'p4n8dz');
    tap(act(quick, 'gstay'));
}
{
    const env = await withRooms(ROWS);
    await wait(env, 1200);
    const before = env.looks();
    await press(env);
    check('Quick join with no room opens the card "Finding a game…" over Home (the four seats, a bar, the line that says how many rooms it looks through, a Cancel), and asks the list once', !!card(env) && title(env) === 'Finding a game…' && env.looks() === before + 1
          && lineOf(env) === 'Looking through 5 open games for the best one.' && !!first(card(env), 'gbar') && find(card(env), 'gdot').length === 4 && textOf(act(env, 'gstay')) === 'Cancel' && card(env).getAttribute('role') === 'dialog' && card(env).getAttribute('aria-modal') === 'true');
    check('the second client asks for the BEST room (a Hello that only joins, with the name of the page), nothing else is opened, and the page asks the list again for nothing', env.sockets.length === 1 && hex(env.last().open().sent[0]) === hex(helloBytes('Maya', 'p4n8dz', { join: true })));
    check('the page behind the card takes no key or tap (Home\'s bar, the Room, the strips)', env.home('hbar').hasAttribute('inert') && env.$('lobby').hasAttribute('inert') && env.$('banner').hasAttribute('inert'));
    check('the card is the body\'s (over both screens), full over the window', card(env).parent.parent.parent === env.body || card(env).parent.parent === env.body);
    const probe = env.last();
    welcomeOf(probe, 1, ['Pebble', 'Maya'], PRIVATE);
    check('the room welcomes the page: the card says "Joined Pebble’s room" with the room as it is now (4 of 4, one more person), "Opening the room…", a bar and the button "Go to the room"; the Home screen shows still',
          title(env) === 'Joined Pebble’s room' && lineOf(env) === 'Opening the room…' && textOf(first(first(card(env), 'gitem'), 'gcount')) === '4/4' && textOf(act(env, 'gyes')) === 'Go to the room' && !!first(card(env), 'gbar') && env.screen() === 'home'
          && !!first(card(env), 'gmark') && first(card(env), 'gmark').className.indexOf('check') !== -1);
    same('the card is drawn in the order of the picture: the mark, the title, the room, the line, the bar, the button', card(env).children.map((c) => classes(c)[0]), ['gmark', 'gctitle', 'gitem', 'gcline', 'gbar', 'gcbtns']);
    check('(the page is in that room now: its code, its session; nothing is said on a strip)', env.$('code').textContent === 'p4n 8dz' && JSON.parse(env.session.data['ants.lobby']).c === 'p4n8dz' && stripOf(env).hidden === true);
    await wait(env, 1400);
    check('the Room is not opened before 1.5 s are gone', card(env) !== null && env.screen() === 'home');
    await wait(env, 200);
    check('... then the card goes and the Room opens as a guest of that room ("Pebble’s room", "Public room" is for the room\'s own flags: private here), with no strip', card(env) === null && env.screen() === 'room' && env.$('room-h').textContent === 'Pebble’s room' && stripOf(env).hidden === true);
    check('the page is not inert any more', !env.home('hbar').hasAttribute('inert') && !env.$('lobby').hasAttribute('inert'));
}
{
    const env = await withRooms(ROWS);
    await press(env);
    const probe = env.last();
    welcomeOf(probe, 1, ['Pebble', 'Maya'], PRIVATE);
    check('(the button of the card)', textOf(act(env, 'gyes')) === 'Go to the room' && env.focused === act(env, 'gyes'));
    tap(act(env, 'gyes'));
    check('"Go to the room" opens the Room at once, and nothing happens a second and a half later (the timer is taken back)', card(env) === null && env.screen() === 'room');
    await wait(env, 3000);
    check('... the Room is still the Room', env.screen() === 'room' && card(env) === null);
    const again = await withRooms(ROWS);
    await press(again);
    welcomeOf(again.last(), 1, ['Pebble', 'Maya'], PRIVATE);
    pressEscape(again);
    check('Escape on that card is "Go to the room" too (the page is in that room: the card is only the way there)', card(again) === null && again.screen() === 'room');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. The rooms that cannot be had
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
for (const [what, reason] of [['full', 1], ['started', 3], ['gone (the game was taken off the list meanwhile)', 6]]) {
    const env = await withRooms(ROWS);
    await press(env);
    const first1 = env.last();
    check('(a room that is ' + what + ': the best is tried first)', askedFor(first1) === 'p4n8dz');
    first1.open().receive(reject(reason));
    await flush();
    check('the room that is ' + what + ' is not the end: the card stays, and the page has not left anything (no Leave)', card(env) !== null && title(env) === 'Finding a game…' && env.sockets.length === 1);
    await wait(env, 400);
    check('... the next room is tried within 300 ms (the second best, a new second client)', env.sockets.length === 2 && askedFor(env.last()) === 'c1v3rr');
    welcomeOf(env.last(), 1, ['Clover', 'Maya'], PRIVATE);
    check('... and it takes the page: "Joined Clover’s room"', title(env) === 'Joined Clover’s room');
}
{
    const env = await withRooms(ROWS);
    const own = myRoom(env, ['Maya']);
    env.roomBar('rhome').click();
    await wait(env, 1500);
    await press(env);
    const codes = [];
    const widths = [];
    for (let k = 0; k < 3; k++) {
        await wait(env, 400);
        codes.push(askedFor(env.last()));
        widths.push(first(card(env), 'gbar').firstChild.style.width);
        env.last().open().receive(reject(k === 1 ? 3 : 1));
        await flush();
    }
    await wait(env, 1000);
    same('three rooms are tried, the best three in order, and no fourth', [codes, env.sockets.length], [['p4n8dz', 'c1v3rr', 'j7n1pr'], 4]);
    same('the bar of the card goes on with each try (a quarter, a half, three quarters)', widths, ['25%', '50%', '75%']);
    check('when all three are filled the card says that there is nothing: "No open games right now." with Host one! and Not now (the picture\'s words), and the page has not left its own room', title(env) === 'No open games right now.' && textOf(act(env, 'gyes')) === 'Host one!' && textOf(act(env, 'gstay')) === 'Not now'
          && leaves(own) === 0 && !own.closed);
    check('... and the line says what Host one! does with a room that the page has: "Your room goes public with its map and your friends in it."', textOf(first(card(env), 'gcnote')) === 'Your room goes public with its map and your friends in it.');
}
{
    const env = await withRooms(ROWS.slice(3));
    await press(env);
    env.last().open().receive(reject(1));
    await flush();
    await wait(env, 400);
    env.last().open().receive(reject(1));
    await flush();
    await wait(env, 400);
    check('two rooms in the list and both full: two are tried (not a third from nowhere), then the card says that nobody is waiting', env.sockets.length === 2 && title(env) === 'No open games right now.');
}
{   // the wait between two tries is a random 0 to 300 ms
    const gaps = [];
    for (const random of [0, 0.999]) {
        const env = await withRooms(ROWS, { options: { random } });
        await press(env);
        env.last().open().receive(reject(1));
        await flush();
        const t0 = env.clock;
        let waited = 0;
        while (env.sockets.length < 2 && waited < 1000) { await wait(env, 50); waited += 50; }
        gaps.push(waited);
    }
    check('the wait between two tries is at random between 0 and 300 ms (with the least random number the next try is at once, with the greatest it takes about 300)', gaps[0] <= 50 && gaps[1] >= 250 && gaps[1] <= 350, JSON.stringify(gaps));
}
{   // what stops the tries
    const env = await withRooms(ROWS);
    await press(env);
    env.last().open().receive(reject(2));
    await flush();
    await wait(env, 800);
    check('a server of another version: the page\'s own banner asks for a reload, the card goes, no other room is tried (all of them would say it), and no strip of the list says anything more', !env.$('banner').hidden && env.$('banner-x').textContent === 'Reload' && /newer version of Ants/.test(env.$('banner-text').textContent) && card(env) === null && env.sockets.length === 1 && stripOf(env).hidden === true);
    const silent = await withRooms(ROWS);
    await press(silent);
    silent.last().open();
    await wait(silent, 15500);
    check('a room that says nothing for 15 s: the card goes, the strip says "The server did not answer. Try again in a moment." and no other room is tried', card(silent) === null && stripText(silent) === 'The server did not answer. Try again in a moment.' && silent.sockets.length === 1);
    check('... and the page is as it was (Home, no room left, Quick join takes a press again)', silent.screen() === 'home' && silent.home('join').disabled === false);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. Nothing to join
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await withRooms([]);
    await press(env);
    check('an empty list: the card says "No open games right now." and "Nobody is waiting for a player. Host one, and others can join you.", with Host one! and Not now, and the ant of an empty color (no room: "A new room opens for you, and it goes public.")',
          title(env) === 'No open games right now.' && lineOf(env) === 'Nobody is waiting for a player. Host one, and others can join you.' && textOf(first(card(env), 'gcnote')) === 'A new room opens for you, and it goes public.' && !!first(card(env), 'ant') && env.sockets.length === 0);
    check('Host one! has the focus (the answer that is meant), and is the big button of the picture', env.focused === act(env, 'gyes') && classes(act(env, 'gyes')).indexOf('big') !== -1 && classes(act(env, 'gstay')).indexOf('big') === -1);
    tap(act(env, 'gstay'));
    check('Not now takes the card away and changes nothing (the focus is back on Quick join)', card(env) === null && env.sockets.length === 0 && env.screen() === 'home' && env.focused === env.home('join'));
    await press(env);
    tap(act(env, 'gyes'));
    check('Host one! is Quick host: a room of the page\'s own is made (My room\'s machinery) and the Room shows', card(env) === null && env.sockets.length === 1 && env.screen() === 'room');
}
{
    const env = await withRooms([]);
    const sock = myRoom(env, ['Maya']);
    env.roomBar('rhome').click();
    await wait(env, 1500);
    await press(env);
    tap(act(env, 'gyes'));
    check('Host one! with a room of the page\'s own (alone in it) flips it to public (Quick host\'s Visibility), with no second room', sock.sent.filter((b) => b[0] === 35).length === 1 && env.sockets.length === 1 && env.screen() === 'room');
}
{
    const site = fakeSite();
    site.status = 404; site.body = { error: 'lobbies_off' };
    const env = await page({ site });
    await wait(env, 1200);
    await press(env);
    check('a list that is off: the card says so ("Games list is off right now") with Host one!, and tries nothing', title(env) === 'Games list is off right now' && textOf(act(env, 'gyes')) === 'Host one!' && env.sockets.length === 0);
}
{   // the page's own room is not a room to join
    const env = await withRooms([BEST]);
    const own = myRoom(env, ['Maya']);
    const ownCode = helloRoom(own.sent[0]);
    env.site.body = listOf([row(ownCode, 'Maya', { people: 1, free: 3, age: 999 }), BEST]);
    env.roomBar('rhome').click();
    await wait(env, 1500);
    await press(env);
    check('the page\'s own room is in the list (a public room of its own) and is left out: the room that is asked for is the other one, whatever the order', askedFor(env.last()) === 'p4n8dz' && env.sockets.length === 2);
    const alone = await withRooms([]);
    const mine = myRoom(alone, ['Maya']);
    alone.site.body = listOf([row(helloRoom(mine.sent[0]), 'Maya', { people: 1, free: 3 })]);
    alone.roomBar('rhome').click();
    await wait(alone, 1500);
    await press(alone);
    check('a list that holds nothing but the own room is an empty list', title(alone) === 'No open games right now.' && alone.sockets.length === 1);
}

{   // what the page is told while the card stands
    const env = await withRooms(ROWS);
    await press(env);
    check('the card has the focus itself while its only button waits (a disabled button takes none): Escape and a screen reader start there', env.focused === card(env) && card(env).getAttribute('tabindex') === '-1');
    await wait(env, 800);
    tap(act(env, 'gstay'));
    check('(Cancel)', card(env) === null);
    const open = await withRooms(ROWS);
    tap(open.home('games'));
    await flush();
    check('(the list is open)', popOf(open).hidden === false);
    await press(open);
    check('Quick join with the list open puts the list away (a press elsewhere), and opens its card', popOf(open).hidden === true && title(open) === 'Finding a game…');
    const wait2 = await withRooms(ROWS);
    tap(wait2.home('games'));
    await flush();
    tap(joinOf(rowsOf(wait2)[0]));
    check('(a Join of the list waits for its room)', wait2.sockets.length === 1 && first(popOf(wait2), 'gjoining').hidden === false);
    wait2.home('join').click();
    await flush();
    check('Quick join pressed while a Join of the list waits does nothing (no card, no second client more)', card(wait2) === null && wait2.sockets.length === 1);
    const fresh = await withRooms([row('zz1111', 'Old', { free: 2, age: 5 })]);
    fresh.site.body = listOf([row('zz1111', 'Old', { free: 2, age: 5 }), row('yy2222', 'New', { people: 3, free: 1, age: 9 })]);
    await wait(fresh, 1500);
    await press(fresh);
    check('the list that Quick join looks at is the one that it asks for (not the one of the first look: a room that was listed since is the best)', askedFor(fresh.last()) === 'yy2222' && lineOf(fresh) === 'Looking through 2 open games for the best one.');
    const floor = await withRooms(ROWS);
    await wait(floor, 1500);
    const n = floor.looks();
    await press(floor);
    await wait(floor, 710);
    tap(act(floor, 'gstay'));
    tap(floor.home('games'));
    await flush();
    check('Quick join\'s look counts for the second between two looks: the list that opens 700 ms later waits for the second to be over (no look yet)', floor.looks() === n + 1);
    await wait(floor, 400);
    check('... and looks then', floor.looks() === n + 2);
}
{   // a hand-off, and the card that stands
    const env = await withRooms(ROWS);
    await press(env);
    welcomeOf(env.last(), 1, ['Pebble', 'Maya'], PRIVATE);
    check('(the card "Joined Pebble’s room" stands)', title(env) === 'Joined Pebble’s room');
    env.last().receive(roomMessage(seatsOf(['Pebble', 'Maya']), { you: 1, leader: 0, flags: STARTING }));
    check('START that reaches the page while the card stands takes it away, and does not open the Room (the page is on its way to the game: Home says so)', card(env) === null && env.screen() === 'home' && env.assigned.length === 1);
    await wait(env, 3000);
    check('... and nothing opens it later', env.screen() === 'home' && card(env) === null);
    const going = await withRooms(ROWS);
    const own = myRoom(going, ['Maya', 'Sage']);
    own.receive(roomMessage(seatsOf(['Maya', 'Sage']), { you: 0, leader: 0, flags: STARTING }));
    going.roomBar('leave').click();
    check('Leave room pressed while the hand-off is on its way (the first 15 s) asks nothing and leaves nothing', card(going) === null && leaves(own) === 0);
    const throwing = await page({ site: (() => { const site = fakeSite(); site.body = listOf(ROWS); return site; })(), options: { lazy: { AntsHomeGames: { mount: (e) => { const g = Games.mount(e); g.leaving = () => { throw new Error('the Games list cannot say'); }; return g; } } } } });
    const ts = myRoom(throwing, ['Maya', 'Sage']);
    throwing.roomBar('leave').click();
    check('a Games list that throws when asked is a list that does not ask: Leave room leaves', leaves(ts) === 1 && throwing.screen() === 'home');
}
{   // the cards: the buttons of a question side by side, the answers of a card with one button or a stack under each other
    const env = await withRooms(ROWS);
    const own = myRoom(env, ['Maya', 'Sage']);
    env.roomBar('rhome').click();
    await wait(env, 1500);
    await press(env);
    check('the two answers of a question stand side by side (one row: no "solo" row)', find(card(env), 'gcbtns').length === 1 && !find(card(env), 'gcbtns').some((b) => classes(b).indexOf('solo') !== -1));
    tap(act(env, 'gstay'));
    const one = await withRooms(ROWS);
    await press(one);
    check('a card with one button has it alone in its row', find(card(one), 'gcbtns').length === 1 && find(card(one), 'gcbtns').every((b) => classes(b).indexOf('solo') !== -1));
    const none = await withRooms([]);
    await press(none);
    check('the card of nothing to join has its two answers one under the other, each in its own row', find(card(none), 'gcbtns').length === 2 && find(card(none), 'gcbtns').every((b) => classes(b).indexOf('solo') !== -1));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. The own room
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await withRooms(ROWS);
    const own = myRoom(env, ['Maya']);
    const ownCode = helloRoom(own.sent[0]);
    env.roomBar('rhome').click();
    await wait(env, 1500);
    await press(env);
    check('alone in the own room: nothing is asked, the second client goes, and the own room is kept (no Leave, the socket open) until the other welcomes the page', card(env) !== null && title(env) === 'Finding a game…' && leaves(own) === 0 && !own.closed && env.sockets.length === 2);
    const probe = env.last();
    probe.open();
    await wait(env, 5000);
    check('... for as long as it takes', leaves(own) === 0 && !own.closed && env.$('code').textContent === grouped(ownCode));
    welcomeOf(probe, 1, ['Pebble', 'Maya'], PRIVATE);
    check('the other room welcomes the page: now the own room is let go of (a Leave, the socket closed), and the page is in the other', leaves(own) === 1 && own.closed && env.$('code').textContent === 'p4n 8dz');
}
{
    const env = await withRooms(ROWS);
    const own = myRoom(env, ['Maya', 'Sage']);
    env.roomBar('rhome').click();
    await wait(env, 1500);
    await press(env);
    check('with another person in the own room Quick join asks first, in the approved card: "Leave your room?", "Sage is in your room. If you go, Sage becomes the host.", the best room with its BEST tag, "Leave and join" and "Stay here" (which has the focus), and nothing was opened',
          title(env) === 'Leave your room?' && lineOf(env) === 'Sage is in your room. If you go, Sage becomes the host.' && textOf(first(first(card(env), 'gitem'), 'gname')) === 'Pebble' && textOf(first(first(card(env), 'gitem'), 'gbest')) === 'BEST'
          && textOf(first(first(card(env), 'gitem'), 'gcount')) === '3/4' && textOf(act(env, 'gyes')) === 'Leave and join' && textOf(act(env, 'gstay')) === 'Stay here' && env.focused === act(env, 'gstay') && env.sockets.length === 1 && leaves(own) === 0);
    tap(act(env, 'gstay'));
    check('"Stay here" changes nothing: the card is gone, no second client, the own room stays, the page is not inert, the focus is on Quick join, and Quick join takes a press again', card(env) === null && env.sockets.length === 1 && leaves(own) === 0 && !env.home('hbar').hasAttribute('inert') && env.focused === env.home('join'));
    await press(env);
    pressEscape(env);
    check('Escape is "Stay here"', card(env) === null && env.sockets.length === 1);
    await press(env);
    tap(act(env, 'gyes'));
    check('"Leave and join" goes on to the tries (the card "Finding a game…"; the second client for the best room), and the own room is still there', title(env) === 'Finding a game…' && env.sockets.length === 2 && askedFor(env.last()) === 'p4n8dz' && leaves(own) === 0 && !own.closed);
    welcomeOf(env.last(), 1, ['Pebble', 'Maya'], PRIVATE);
    check('... the Welcome lets the own room go (a Leave), and the page is in the other', leaves(own) === 1 && own.closed && title(env) === 'Joined Pebble’s room');
}
{
    const env = await withRooms(ROWS, { search: '?room=zzz999', options: { stayHome: false } });
    const sock = guestRoom(env, 'Zed', PRIVATE);
    env.roomBar('rhome').click();
    await wait(env, 1500);
    await press(env);
    check('as a guest of somebody else\'s room Quick join asks too ("Leave Zed’s room?", the way back by the link)', title(env) === 'Leave Zed’s room?' && /come back by its link/.test(lineOf(env)) && leaves(sock) === 0);
    tap(act(env, 'gyes'));
    env.last().open().receive(reject(1));
    await flush();
    await wait(env, 400);
    check('a guest whose first room is full: the next is tried, and the room of Zed is kept all along (the question was asked once)', env.sockets.length === 3 && leaves(sock) === 0 && !sock.closed && askedFor(env.last()) === 'c1v3rr');
}
{   // going Home and back never drops the seat
    const env = await withRooms(ROWS);
    const own = myRoom(env, ['Maya']);
    const ownCode = helloRoom(own.sent[0]);
    env.roomBar('rhome').click();
    await wait(env, 1500);
    await press(env);
    env.last().open();
    check('(a try is waiting)', title(env) === 'Finding a game…' && env.screen() === 'home');
    tap(act(env, 'gstay')); await wait(env, 800); tap(act(env, 'gstay'));
    env.home('mine').click();
    check('a try that is given up leaves the own room where it was: My room shows it, with its code, and the seat was never dropped (no Leave, the socket open)', card(env) === null && env.screen() === 'room' && env.$('code').textContent === grouped(ownCode) && leaves(own) === 0 && !own.closed);
    env.roomBar('rhome').click();
    env.home('mine').click();
    check('... Home and back again does not drop it either', env.screen() === 'room' && leaves(own) === 0 && !own.closed);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 6. Cancel, a double press, START, a stalled hand-off
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await withRooms(ROWS);
    const own = myRoom(env, ['Maya']);
    env.roomBar('rhome').click();
    await wait(env, 1500);
    await press(env);
    const probe = env.last().open();
    const cancelButton = act(env, 'gstay');
    check('the Cancel of the card takes no press for a moment (it stands where the button of Quick join was: the second press of a double press is not a Cancel)', cancelButton.disabled === true);
    tap(cancelButton);
    check('a press on it in that moment does nothing: the tries go on', card(env) !== null && !probe.closed);
    await wait(env, 699);
    check('... not before 700 ms', act(env, 'gstay').disabled === true);
    await wait(env, 1);
    check('... then Cancel can be pressed', act(env, 'gstay').disabled === false);
    tap(act(env, 'gstay'));
    check('Cancel: the card goes, the second client is let go of (a Leave, closed), the own room is as it was (no Leave, open), the focus is back on Quick join', card(env) === null && leaves(probe) === 1 && probe.closed && leaves(own) === 0 && !own.closed && env.focused === env.home('join'));
    probe.receive(welcome(1, KEY2, 0));
    await wait(env, 20000);
    check('a Welcome (or the 15 s of silence) that comes after Cancel changes nothing: no room, no notice, no ghost seat (the own room stays the page\'s)', env.screen() === 'home' && card(env) === null && stripOf(env).hidden === true && !own.closed && leaves(own) === 0 && env.sockets.length === 2);
    await press(env);
    check('(and Quick join can be pressed again)', card(env) !== null && title(env) === 'Finding a game…' && env.sockets.length === 3);
    pressEscape(env);
    check('Escape is Cancel (a card that is closed with no answer gives the try up)', card(env) === null && env.sockets[2].closed === true);
}
{   // Cancel between two tries
    const env = await withRooms(ROWS);
    await press(env);
    env.last().open().receive(reject(1));
    await flush();
    await wait(env, 800);
    check('(the second room is being tried)', env.sockets.length === 2);
    await wait(env, 700);
    tap(act(env, 'gstay'));
    check('Cancel while the second try waits lets that second client go, and no third is tried', card(env) === null && env.sockets[1].closed === true && env.sockets.length === 2);
    await wait(env, 2000);
    check('... never', env.sockets.length === 2 && card(env) === null);
}
{   // a double press on the button of Quick join
    const env = await withRooms(ROWS);
    await press(env);
    env.home('join').click();
    await flush();
    check('a second press on Quick join (a double press, a key that repeats) is no second try: one card, one second client', find(env.body, 'gconfirm').length === 1 && env.sockets.length === 1);
    check('(the buttons of Home take no press while the card stands: Quick host is nothing then)', (env.home('host').click(), find(env.body, 'gconfirm').length === 1 && env.sockets.length === 1));
}
{   // START and the stalled hand-off
    const env = await withRooms(ROWS);
    const own = myRoom(env, OTHERS_OF(['Maya', 'Sage']));
    env.roomBar('rhome').click();
    await wait(env, 1500);
    await press(env);
    check('(the question is up)', title(env) === 'Leave your room?');
    own.receive(roomMessage(seatsOf(['Maya', 'Sage']), { you: 0, leader: 0, flags: STARTING }));
    check('START that reaches the page takes the card away and no second client is opened', card(env) === null && env.sockets.length === 1 && env.assigned.length === 1);
    await wait(env, 16000);
    check('a hand-off that does not come: Quick join is off, with the page\'s words as its title ("The game is slow to open…"), like Quick host and Games', [env.home('join'), env.home('host'), env.home('games')].every((b) => b.disabled === true && b.getAttribute('aria-disabled') === 'true' && b.getAttribute('title') === 'The game is slow to open. Reload this page to get your seat back.'));
    env.home('join').click();
    check('a press that got through opens nothing (no card, no second client)', card(env) === null && env.sockets.length === 1);
    await wait(env, 50000);
    check('a minute after START the words are "The game did not open…" on Quick join too', env.home('join').getAttribute('title') === 'The game did not open. Reload this page to get your seat back.');
}
{   // a page that is on its way to a game
    const env = await withRooms(ROWS);
    const own = myRoom(env, ['Maya']);
    env.$('start').click();
    env.roomBar('rhome').click();
    const before = env.sockets.length;
    tap(env.home('join'));
    await flush();
    check('Quick join on a page that is on its way to a game (a match for one) opens nothing and says why, with a Reload', card(env) === null && env.sockets.length === before && stripText(env) === 'The game is opening. If it does not, reload this page.' && textOf(stripButtons(env)[0]) === 'Reload');
}
{   // a match that runs
    const env = await withRooms(ROWS);
    const sock = myRoom(env, ['Maya', 'Sage'], STARTING, null, true);
    env.roomBar('rhome').click();
    await wait(env, 1500);
    await press(env);
    check('START that waits (the match is on its way): Quick join says "Your match is running." and opens nothing', card(env) === null && stripText(env) === 'Your match is running.' && env.sockets.length === 1);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 7. Leave room
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await withRooms(ROWS);
    const sock = myRoom(env, ['Maya']);
    env.roomBar('leave').click();
    check('Leave room when alone in the room leaves at once (no card): a Leave, the socket closed, Home', card(env) === null && leaves(sock) === 1 && sock.closed && env.screen() === 'home');
}
{
    const env = await withRooms(ROWS);
    const sock = myRoom(env, ['Maya', 'Sage']);
    env.roomBar('leave').click();
    check('Leave room with another person in the room asks first, in the card of the other questions: "Leave your room?", "Sage is in your room. If you go, Sage becomes the host.", "Leave room" and "Stay here" (which has the focus); nothing was left', title(env) === 'Leave your room?'
          && lineOf(env) === 'Sage is in your room. If you go, Sage becomes the host.' && textOf(act(env, 'gyes')) === 'Leave room' && textOf(act(env, 'gstay')) === 'Stay here' && env.focused === act(env, 'gstay') && leaves(sock) === 0 && !sock.closed && env.screen() === 'room');
    check('the Room behind the card takes no key or tap', env.$('lobby').hasAttribute('inert') && env.root.children.filter((c) => classes(c).indexOf('rbar') !== -1).every((b) => b.hasAttribute('inert')));
    tap(act(env, 'gstay'));
    check('"Stay here" changes nothing: the card is gone, nothing was left, the Room stays, the page takes presses again, the focus is back on Leave room', card(env) === null && leaves(sock) === 0 && env.screen() === 'room' && !env.$('lobby').hasAttribute('inert') && env.focused === env.roomBar('leave'));
    env.roomBar('leave').click();
    pressEscape(env);
    check('Escape is "Stay here"', card(env) === null && leaves(sock) === 0 && env.screen() === 'room');
    env.roomBar('leave').click();
    tap(act(env, 'gyes'));
    check('"Leave room" leaves it: a Leave, the socket closed, Home shows', card(env) === null && leaves(sock) === 1 && sock.closed && env.screen() === 'home');
}
{
    const three = await withRooms(ROWS);
    const sock = myRoom(three, ['Maya', 'Sage', 'Pip']);
    three.roomBar('leave').click();
    check('with two others the card names no one ("2 people are in your room…")', /^2 people are in your room\./.test(lineOf(three)));
    const env = await withRooms(ROWS, { search: '?room=zzz999', options: { stayHome: false } });
    const guest = guestRoom(env, 'Zed', PRIVATE);
    env.roomBar('leave').click();
    check('a guest is always asked ("Leave Zed’s room?": the host is there)', title(env) === 'Leave Zed’s room?' && textOf(act(env, 'gyes')) === 'Leave room' && leaves(guest) === 0);
    tap(act(env, 'gyes'));
    check('... and leaves when it says so', leaves(guest) === 1 && env.screen() === 'home');
    const bots = await withRooms(ROWS);
    bots.home('mine').click();
    const bs = bots.arrive({ key: KEY, seats: [[C, 'Maya'], [NET0.SLOT.Bot, 'Easy'], [E, ''], [E, '']], room: { flags: PRIVATE } });
    bots.roomBar('leave').click();
    check('a computer player is no person to ask about: Leave room leaves at once', card(bots) === null && leaves(bs) === 1);
}
{   // a room that went public with others: the same
    const env = await withRooms(ROWS);
    const sock = myRoom(env, ['Maya', 'Sage'], PUBLIC);
    env.roomBar('leave').click();
    check('the question is the same for a public room', title(env) === 'Leave your room?');
}
{   // while the hand-off is stalled Leave room stays off (home.js)
    const env = await withRooms(ROWS);
    const own = myRoom(env, ['Maya', 'Sage'], PRIVATE);
    own.receive(roomMessage(seatsOf(['Maya', 'Sage']), { you: 0, leader: 0, flags: STARTING }));
    await wait(env, 16000);
    env.roomBar('leave').click();
    check('Leave room stays off on a page whose hand-off did not come (it never asks, it never leaves)', card(env) === null && leaves(own) === 0);
}

done('Quick join and Leave room');
function OTHERS_OF(names) { return names; }
})().catch(broke);
