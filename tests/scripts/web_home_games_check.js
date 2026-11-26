// Runs the front page's PUBLIC ROOM and GAMES LIST (web/home/games.js and games.css with web/lobby.html's own script, docs/HOME_GAMES.md, docs/PUBLIC_ROOMS.md) in the fake browser of tests/scripts/web_page_harness.js,
// against a scripted game server and a fake GET /lobbies:
//   1. the pure parts: a row of the list on a table (every field of the shape, every wrong type and range, markup and look-alike characters in a name), the list (extra fields, duplicates, 10,000 rows, a top level that is
//      no list), the waits (30 s, 5 s, doubled after a failure up to a cap, +-20 %), the count ("50+"), the seats;
//   2. the way in: nothing is lit before the script comes, Quick host, Quick join and Games are lit by it, a script that does not come leaves the page as it was;
//   3. the looks at /lobbies with the fake clock: the first one when the page is up, 30 s, 5 s while the popover is open and at once when it opens, none while the tab is hidden or the Room shows, one at a time,
//      the jitter's bounds, the wait that doubles after every failure and starts again after a good answer, a look that never ends, the list that is off (404, 503, garbage, a network that fails);
//   4. the popover: the rows as text (a name with markup, a map's file name), the seats as dots, the count on the Games button, the page's own public room as the first row (Yours), the empty state, "Games list is off right now", Escape and a
//      press elsewhere put it away; a malformed list (markup, look-alike characters, 10,000 rows, wrong types, duplicate codes);
//  10. the pictures of the rows: an original's own, the library's for a map of the players at the address that the server's own list gives (every map of Community-Maps/ and a table of the awkward names), a picture that does not
//      come taken away, and nothing of an odd name that is not percent-encoded;
//  11. the locked cards of private rooms (rows with `private: true` and no code): what a row must be to be drawn, a row that carries a code dropped, the card (no button, no link, no code anywhere in the page), the count line,
//      the strip when nothing can be joined, the line after six cards;
// The Room's switch, Quick host and Join are web_home_games_room_check.js. tests/scripts/test_web_home_games.py runs both with node (the quick tier).
// usage: node web_home_games_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
const R = require('./web_home_games_rig.js')(process.argv[2]);
const { GUEST, check, same, flush, fakeSite, row, locked, lockedCards, listOf, page, wait, classes, find, first, home, tap, chooseHost, hostCard, pressEscape, pressElsewhere, popOf, rowsOf, slotsOf, joinOf, textOf, pillOf, myRoom, guestRoom, PUBLIC, PRIVATE, KEY, E, C, Games, RULES0, done, broke, helloRoom, runLobby, hex, helloBytes } = R;
const TEXT = Games.TEXT;                          // (the page's own words: a check names the tag, the button or the chip that the page has for a situation, and does not spell it)
const LOCKED = TEXT.privateTag.toLowerCase(), LOCKED_ADJ = LOCKED.replace(' ', '-');      // the setting's one name: the tag of a locked card says it, and so do the list's head ("3 open, 3 invite only"), "and 6 more invite-only rooms", Quick join's line and the screen reader's sentence

// the address of a picture of the library (map_picker.js: the one shape that the page loads for a map of the players): /maps/previews/ and a name of unreserved characters and %XX (never an escaped slash, backslash or NUL), no leading dot
const LIBRARY = /^\/maps\/previews\/(?:[A-Za-z0-9_~-]|%(?!2F|5C|00)[0-9A-F]{2})(?:[A-Za-z0-9._~-]|%(?!2F|5C|00)[0-9A-F]{2})*\.webp$/;

(async () => {

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The pure parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const good = row('k7m2xq', 'Pebble', { map: 'ISLANDS.LVL', people: 3, bots: 0, free: 1 });
    const parsed = Games.rowOf(good, RULES0);
    same('a good row: the code, the host, the map (an original by its name and its key), the seats', parsed, { code: 'k7m2xq', host: 'Pebble', map: 'ISLANDS.LVL', mapKey: 'islands', mapName: 'Islands', people: 3, bots: 0, free: 1, size: 4, age: 10, fog: false });
    same('a map of the players has its file name without the ending and no picture of its own', [Games.rowOf(row('k7m2xq', 'A', { map: 'My Map.LVL' }), RULES0).mapKey, Games.rowOf(row('k7m2xq', 'A', { map: 'My Map.LVL' }), RULES0).mapName], ['', 'My Map']);
    check('fields that are not in the shape are ignored (the row stays)', Games.rowOf(Object.assign({}, good, { extra: '<script>', platform: 16, key: 'ff' }), RULES0) !== null && !('extra' in Games.rowOf(Object.assign({}, good, { extra: 1 }), RULES0)));
    same('a name of printable ASCII with every sign that markup uses stays as it is (text, never markup)', Games.rowOf(row('k7m2xq', '<b>"x"\\&</b>'), RULES0).host, '<b>"x"\\&</b>');
    same('a host with no name is Player', Games.rowOf(row('k7m2xq', ''), RULES0).host, 'Player');
    same('... and one of blanks is too (the blanks at the ends are cut)', [Games.rowOf(row('k7m2xq', '   '), RULES0).host, Games.rowOf(row('k7m2xq', ' Ann '), RULES0).host], ['Player', 'Ann']);
    const bad = [
        ['no object', null], ['an array', [1]], ['a text', 'row'],
        ['a code with a capital', row('K7m2xq', 'A')], ['a code with a space', row('k7m 2xq', 'A')], ['an empty code', row('', 'A')], ['a code of 33 characters', row('a'.repeat(33), 'A')], ['a code that is no text', row(7, 'A')],
        ['a host of 33 characters', row('k7m2xq', 'x'.repeat(33))], ['a host with a right-to-left mark', row('k7m2xq', 'Ann\u202eBob')], ['a host with a zero-width space', row('k7m2xq', 'A\u200bB')], ['a host with an accent', row('k7m2xq', 'Zo\u00eb')],
        ['a host with a line end', row('k7m2xq', 'A\nB')], ['a host with a control character', row('k7m2xq', 'A\u0007')], ['a host that is a number', row('k7m2xq', 7)],
        ['a map that is no file name', row('k7m2xq', 'A', { map: '../x.LVL' })], ['a map with markup', row('k7m2xq', 'A', { map: '<b>.LVL' })], ['a map that is a number', row('k7m2xq', 'A', { map: 5 })], ['an empty map', row('k7m2xq', 'A', { map: '' })],
        ['a mode that is a number', row('k7m2xq', 'A', { mode: 1 })], ['a mode of 33 characters', row('k7m2xq', 'A', { mode: 'm'.repeat(33) })],
        ['people as text', row('k7m2xq', 'A', { people: '2' })], ['people negative', row('k7m2xq', 'A', { people: -1, free: 3 })], ['people of 5', row('k7m2xq', 'A', { people: 5, free: -1 })], ['people with a fraction', row('k7m2xq', 'A', { people: 1.5, free: 2.5 })],
        ['people that is NaN', row('k7m2xq', 'A', { people: NaN })], ['bots that is null', row('k7m2xq', 'A', { bots: null })], ['a size of 0', row('k7m2xq', 'A', { people: 0, bots: 0, free: 0, size: 0 })], ['a size of 5', row('k7m2xq', 'A', { people: 1, bots: 0, free: 4, size: 5 })],
        ['seats that do not add up to the size', row('k7m2xq', 'A', { people: 2, bots: 0, free: 1, size: 4 })], ['no free color (a game that cannot be joined)', row('k7m2xq', 'A', { people: 4, bots: 0, free: 0, size: 4 })],
        ['an age that is text', row('k7m2xq', 'A', { age: '5' })], ['an age that is negative', row('k7m2xq', 'A', { age: -1 })], ['an age that is Infinity', row('k7m2xq', 'A', { age: Infinity })],
    ];
    for (const [what, value] of bad) check('a row with ' + what + ' is dropped', Games.rowOf(value, RULES0) === null);
    for (const field of ['code', 'host', 'map', 'mode', 'people', 'bots', 'free', 'size', 'age']) {
        const without = Object.assign({}, good); delete without[field];
        check('a row without its ' + field + ' is dropped', Games.rowOf(without, RULES0) === null);
    }
    same('a list: its rows in the order given, an own row of the shape and a bad one dropped, the same code once', Games.parseList(listOf([row('aaaaaa', 'A'), 'x', row('bbbbbb', 'B'), row('aaaaaa', 'Again'), { code: 'cccccc' }, row('dddddd', 'D')]), RULES0).map((r) => r.code + ':' + r.host), ['aaaaaa:A', 'bbbbbb:B', 'dddddd:D']);
    const many = Games.parseList(listOf(Array.from({ length: 10000 }, (_, i) => row('r' + String(i).padStart(5, '0'), 'H' + i))), RULES0);
    check('10,000 rows are 50 rows (the first 50, in order)', many.length === 50 && many[0].code === 'r00000' && many[49].code === 'r00049');
    const proto = Games.parseList(listOf([row('__proto__', 'A'), row('__proto__', 'B'), row('constructor', 'C'), row('constructor', 'D'), row('toString', 'E')]), RULES0);
    same('a code that is a name of Object ("__proto__", "constructor", "toString" are good codes) is a code once, as any other', proto.map((r) => r.code + ':' + r.host), ['__proto__:A', 'constructor:C']);
    const junk = Games.parseList(listOf(Array.from({ length: 10000 }, () => 'x')), RULES0);
    check('10,000 rows that are no rows are no rows, and are not all looked at (the look is bounded)', junk.length === 0);
    {   // a list of 10,000 entries is not read to its end: the rows that are looked at are bounded (the answer is a proxy that counts the entries that are asked for)
        const touched = { n: 0 };
        const entries = new Proxy(Array.from({ length: 10000 }, () => 'x'), { get(target, key, receiver) { if (/^\d+$/.test(String(key))) touched.n++; return Reflect.get(target, key, receiver); } });
        const none = Games.parseList({ format: 1, lobbies: entries }, RULES0);
        check('10,000 entries that are no rows are looked at only up to a bound (500), and the answer is an empty list, not an error', none.length === 0 && touched.n <= 501, 'looked at ' + touched.n);
    }
    for (const [what, value] of [['null', null], ['a list', []], ['a number', 5], ['a text', 'lobbies'], ['an object with no lobbies', { format: 1 }], ['a format of 2', { format: 2, lobbies: [] }], ['a format that is text', { format: '1', lobbies: [] }],
                                  ['lobbies that is an object', { format: 1, lobbies: {} }], ['lobbies that is a text', { format: 1, lobbies: 'x' }]]) {
        check('an answer that is ' + what + ' is no list (the list is off)', Games.parseList(value, RULES0) === null);
    }
    check('an answer with no count is a list too (the count is the rows)', Games.parseList({ format: 1, lobbies: [row('aaaaaa', 'A')] }, RULES0).length === 1);
    same('the waits: 30 s closed and 5 s open, each doubled for every failure up to the cap, +-20 %', [
        Games.delayOf(false, 0, 0.5), Games.delayOf(false, 1, 0.5), Games.delayOf(false, 2, 0.5), Games.delayOf(false, 3, 0.5), Games.delayOf(false, 4, 0.5), Games.delayOf(false, 50, 0.5),
        Games.delayOf(true, 0, 0.5), Games.delayOf(true, 1, 0.5), Games.delayOf(true, 2, 0.5), Games.delayOf(true, 3, 0.5), Games.delayOf(true, 4, 0.5), Games.delayOf(true, 50, 0.5)],
        [30000, 60000, 120000, 240000, 300000, 300000, 5000, 10000, 20000, 40000, 60000, 60000]);
    same('the jitter is 80 % at random 0 and 120 % at random 1, and a random that is outside 0 to 1 is held to it', [Games.delayOf(false, 0, 0), Games.delayOf(false, 0, 1), Games.delayOf(true, 0, 0), Games.delayOf(true, 0, 1), Games.delayOf(false, 0, -5), Games.delayOf(false, 0, 9)], [24000, 36000, 4000, 6000, 24000, 36000]);
    same('the count: the number, and "50+" from 50 (the server lists 50 at most)', [Games.countText(0), Games.countText(7), Games.countText(49), Games.countText(50), Games.countText(51)], ['0', '7', '49', '50+', '50+']);
    same('the seats: persons, then computer players, then open seats; 3/4 is the persons and the computer players of the 4', [Games.seatsOf(good), Games.seatsOf(row('x', 'A', { people: 1, bots: 1, free: 2 })), Games.seatsOf(row('x', 'A', { people: 1, bots: 2, free: 1 }))], [
        { dots: ['player', 'player', 'player', 'open'], count: '3/4', bots: 'no bots' }, { dots: ['player', 'bot', 'open', 'open'], count: '2/4', bots: '1 bot' }, { dots: ['player', 'bot', 'bot', 'open'], count: '3/4', bots: '2 bots' }]);
    same('the ceilings of the plain-text rules are the picture\'s: at most 50 rows, a look every 30 s and every 5 s, a repeat every 10 minutes', [Games.ROWS_MAX, Games.EVERY_CLOSED, Games.EVERY_OPEN, Games.REFRESH_MS], [50, 30000, 5000, 600000]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. The way in: the script comes after the first paint
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const site = fakeSite();
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true, fakeClock: true, lazy: true, fetch: site.fetch, popover: true });
    check('before the page has painted, nothing of the list is asked for: no script, no style, no look at /lobbies', env.lazyLoads.filter((f) => /games/.test(f)).length === 0 && site.calls.length === 0);
    check('... Quick host, Quick join and Games are drawn and disabled (nothing on the bar is a button that does nothing)', ['host', 'join', 'games'].every((c) => env.home(c).disabled === true && env.home(c).getAttribute('aria-disabled') === 'true'));
    env.advance(0);
    await flush();
    same('after the first paint the page loads the style and the script, once (from its own folder)', env.lazyLoads.filter((f) => /games/.test(f)), ['home/games.css', 'home/games.js']);
    check('... then Quick host, Quick join and Games are lit (a plain button that takes a press, with no mark of being disabled)', [env.home('host'), env.home('join'), env.home('games')].every((b) => b.disabled === false && b.getAttribute('aria-disabled') === null));
    check('... Games names its popup, says that it is closed and has its count (none yet: the first look has not answered) beside its name', env.home('games').getAttribute('aria-haspopup') === 'dialog' && env.home('games').getAttribute('aria-expanded') === 'false' && env.home('games').children[0].textContent === 'Games');
    check('... and the page looked at the list once, at its own address, with no query and no cookies', site.calls.length === 1 && site.calls[0].url === '/lobbies' && site.calls[0].init.credentials === 'omit' && site.calls[0].init.cache === 'no-store');
    env.advance(0);
    check('(the page loads the script once, however often it is shown)', env.lazyLoads.filter((f) => /games/.test(f)).length === 2);
}
for (const [what, lazy] of [['never comes', false], ['fails to load', 'script']]) {
    const env = await page({ options: { lazy } });
    check('a script that ' + what + ' leaves Quick host and Games disabled, asks nothing of /lobbies, and the page as it was (the page makes a room, Home and the Room work)', env.home('host').disabled && env.home('games').disabled && env.site.calls.length === 0);
    env.makeRoom();
    check('... (the page still makes a room and shows it)', env.sockets.length === 1 && env.screen() === 'room');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. The looks at /lobbies
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const env = await page();
    check('the first look is at once, when the page is up (the count on the button is there at first sight)', env.site.times.length === 1 && env.site.times[0] === 0);
    await wait(env, 29500);
    check('... the next is 30 s after it, not before', env.looks() === 1);
    await wait(env, 500);
    same('... then every 30 s with the popover closed', [env.looks()], [2]);
    await wait(env, 30000); await wait(env, 30000);
    same('(the moments of the looks)', env.site.times, [0, 30000, 60000, 90000]);
    tap(env.home('games'));
    await flush();
    same('the popover that opens in the same second as a look waits for the second to be over (two looks are a second apart at least): no look yet', env.looks(), 4);
    await wait(env, 1000);
    same('... and looks at once when the second is over (the list is there when it shows)', [env.looks(), env.site.times[4]], [5, 91000]);
    await wait(env, 4500);
    check('... and again 5 s later, not before', env.looks() === 5);
    await wait(env, 500);
    await wait(env, 5000); await wait(env, 5000);
    same('... every 5 s while it is open', env.site.times.slice(4), [91000, 96000, 101000, 106000]);
    tap(env.home('games'));
    await flush();
    await wait(env, 29500);
    check('the popover closed goes back to every 30 s', env.looks() === 8);
    await wait(env, 500);
    check('... (the next look comes 30 s after the last one that was made while it was open)', env.looks() === 9 && env.site.times[8] === 136000, JSON.stringify(env.site.times));
    env.hide(true);
    const before = env.looks();
    await wait(env, 120000);
    check('a tab that is out of sight looks at nothing, however long it stays so', env.looks() === before);
    env.hide(false);
    await flush();
    check('... and the tab that is back looks at once', env.looks() === before + 1);
    await wait(env, 30000);
    check('... and goes on every 30 s', env.looks() === before + 2);
    env.makeRoom();
    const onRoom = env.looks();
    await wait(env, 100000);
    check('while the Room shows (the count is not on screen) nothing is asked either', env.screen() === 'room' && env.looks() === onRoom);
    env.toggleRoom();
    await flush();
    check('Home back: a look at once, then the rhythm again', env.looks() === onRoom + 1);
    await wait(env, 30000);
    check('... (every 30 s)', env.looks() === onRoom + 2);
}
{   // a popover that is opened while a look is on its way does not double it; a hidden tab with a look on its way starts no more
    const site = fakeSite(); site.hang = true;
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    check('the popover opened while a look has not answered asks no second look (one at a time)', env.looks() === 1);
    await wait(env, 8000);
    check('a look that never answers is given up after 8 s: the list is off, and the next look is one failure later', env.looks() === 1 && textOf(popOf(env)).indexOf('Games list is off right now') !== -1);
    site.hang = false;
    await wait(env, 10000);
    check('... the next look is 10 s later (the open wait of 5 s, doubled once)', env.looks() === 2 && env.site.times[1] === 18000, JSON.stringify(env.site.times));
    await flush();
    check('... and a good answer takes the list back (the empty cards, no "off" line)', first(popOf(env), 'goff').hidden === true && first(popOf(env), 'glist').hidden === false && slotsOf(env).length > 0);
}
for (const [what, random, closed, open] of [['the lowest', 0, 24000, 4000], ['the highest', 1, 36000, 6000]]) {
    const env = await page({ options: { random } });
    await wait(env, closed - 500);
    const early = env.looks();
    await wait(env, 500);
    check('the jitter is ' + what + ' at random ' + random + ': the closed rhythm is ' + closed + ' ms', early === 1 && env.looks() === 2 && env.site.times[1] === closed, JSON.stringify(env.site.times));
    tap(env.home('games'));
    await flush();
    const at = env.looks();
    await wait(env, open);
    check('... and the open rhythm is ' + open + ' ms', env.looks() === at + 1);
}
{   // failures double the wait, a good answer starts it again; the open popover has its own cap
    const site = fakeSite(); site.reject = true;
    const env = await page({ site });
    for (const gap of [60000, 120000, 240000, 300000, 300000]) {
        await wait(env, gap - 500);
        const was = env.looks();
        await wait(env, 500);
        check('with a look that fails every time the next one is ' + gap + ' ms after the last (the wait doubles for every failure, up to 5 minutes)', env.looks() === was + 1);
    }
    same('(the moments)', env.site.times.map((t, i) => (i ? t - env.site.times[i - 1] : t)), [0, 60000, 120000, 240000, 300000, 300000]);
    site.reject = false; site.body = listOf([row('aaaaaa', 'Ann')]);
    await wait(env, 300000);
    check('a good answer takes the list back, with its count', pillOf(env).hidden === false && pillOf(env).textContent === '1');
    const good = env.looks();
    await wait(env, 29500);
    check('... and the rhythm starts again at 30 s', env.looks() === good);
    await wait(env, 500);
    check('... (a good answer forgets the failures)', env.looks() === good + 1);
    site.reject = true;
    await wait(env, 1000);
    tap(env.home('games'));
    await flush();
    const open = env.looks();
    for (const gap of [10000, 20000, 40000, 60000, 60000]) {
        await wait(env, gap - 500);
        const was = env.looks();
        await wait(env, 500);
        check('the open popover with a look that fails: ' + gap + ' ms (up to a minute)', env.looks() === was + 1);
    }
}
for (const [what, setup] of [['a 404', (s) => { s.status = 404; s.body = { error: 'lobbies_off' }; }], ['a 503', (s) => { s.status = 503; s.text = 'unavailable'; }], ['a 200 with a text that is no JSON', (s) => { s.text = 'not json {'; }],
                              ['a 200 with an empty body', (s) => { s.text = ''; }], ['a 200 that is a list of rows only', (s) => { s.text = '[]'; }], ['a 200 with another format', (s) => { s.body = { format: 2, lobbies: [] }; }],
                              ['a 404 that carries a good list', (s) => { s.status = 404; s.body = listOf([row('aaaaaa', 'Ann')]); }], ['a 503 that carries a good list', (s) => { s.status = 503; s.body = listOf([row('aaaaaa', 'Ann')]); }],
                              ['a 500 that carries a good list', (s) => { s.status = 500; s.body = listOf([row('aaaaaa', 'Ann')]); }], ['a network that fails', (s) => { s.reject = true; }], ['a fetch that throws at once', (s) => { s.throws = true; }], ['a body that is far too long', (s) => { s.text = '{"format":1,"lobbies":[],"x":"' + 'a'.repeat(2100000) + '"}'; }]]) {
    const site = fakeSite(); setup(site);
    const env = await page({ site });
    check('the list that answers with ' + what + ' is off: the button says no count, and nothing throws', pillOf(env).hidden === true && pillOf(env).textContent === '' && env.home('games').getAttribute('aria-label') === 'Games' && env.screen() === 'home');
    tap(env.home('games'));
    await flush();
    const off = find(popOf(env), 'goff')[0];
    check('... the popover says "Games list is off right now" (and that Join by code still works), and shows no list and no empty card', off.hidden === false && off.textContent.indexOf('Games list is off right now') !== -1 && off.textContent.indexOf('Join by code still works.') !== -1
          && find(popOf(env), 'glist')[0].hidden === true && slotsOf(env).length === 0 && find(popOf(env), 'gpcount')[0].textContent === '');
    check('... and Join by code is still there', first(first(popOf(env), 'gphead'), 'gcodelink') !== null && first(popOf(env), 'joinbox') !== null);
}
{   // no fetch at all
    const env = await page({ options: { noFetch: true } });
    check('a browser with no fetch has the list off, and the page works', pillOf(env).hidden === true && env.home('games').disabled === false && env.screen() === 'home');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. The popover
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const site = fakeSite();
    site.body = listOf([row('k7m2xq', 'Pebble', { map: 'ISLANDS.LVL', people: 3, free: 1 }), row('abc234', 'Clover', { map: 'TREASURE.LVL', people: 2, bots: 1, free: 1 }), row('zzz999', 'Juniper', { map: 'My Own Map.LVL', people: 1, bots: 1, free: 2 }),
                           row('fff222', 'Fern', { map: 'MEDIUM.LVL', people: 1, free: 3 })]);
    const env = await page({ site });
    check('the Games button has the count of the open games beside its name, in the page\'s pill, and says so to a screen reader', pillOf(env).textContent === '4' && pillOf(env).hidden === false && env.home('games').getAttribute('aria-label') === 'Games, 4 open');
    const pop = popOf(env);
    check('the popover is not shown until Games is pressed', pop.hidden === true);
    tap(env.home('games'));
    await flush();
    same('it is put under the bar, no taller than what is left of the window (a wide window: 450 px wide, at the Games button; the fake window is 1280 x 800 and the bar ends at 100)', [pop.style.top, pop.style.left, pop.style.width, pop.style.maxHeight], ['108px', '10px', '450px', '680px']);
    check('Games opens it: it is a dialog that is not modal, named "Open games", and the button says that it is open', pop.hidden === false && pop.getAttribute('role') === 'dialog' && pop.getAttribute('aria-label') === 'Open games' && pop.getAttribute('aria-modal') === null && env.home('games').getAttribute('aria-expanded') === 'true');
    check('... the heading says how many are waiting', first(pop, 'gptitle').textContent === 'Open games' && first(pop, 'gpcount').textContent === '4 waiting · updates live');
    const rows = rowsOf(env);
    same('... the rows are the list\'s own, in its order (the best first)', rows.map((li) => textOf(first(li, 'gname'))), ['Pebble', 'Clover', 'Juniper', 'Fern']);
    same('... each names its map (an original by its name, a map of the players by its file without the ending)', rows.map((li) => textOf(first(li, 'gmapname'))), ['Islands', 'Treasure', 'My Own Map', 'Medium']);
    same('... and its seats, as n of the size (persons and computer players) and as the bots', rows.map((li) => textOf(first(li, 'gcount')) + ' ' + textOf(first(li, 'gbots'))), ['3/4 no bots', '3/4 1 bot', '2/4 1 bot', '1/4 no bots']);
    same('... the dots: a color for a person, a ring for a computer player, a dashed ring for an open seat, in the colors of the four, and none of them is read out', rows.map((li) => find(first(li, 'gdots'), 'gdot').map((d) => classes(d).filter((c) => c !== 'gdot').join('.')).join(' ')),
        ['player.c0 player.c1 player.c2 open.c3', 'player.c0 player.c1 bot.c2 open.c3', 'player.c0 bot.c1 open.c2 open.c3', 'player.c0 open.c1 open.c2 open.c3']);
    check('... the dots are hidden from a screen reader (the words say it), and so is the map\'s picture', rows.every((li) => first(li, 'gdots').getAttribute('aria-hidden') === 'true' && first(li, 'gmap').getAttribute('aria-hidden') === 'true'));
    same('... an original map has its own picture (front/preview_<key>.png); a map of the players has none while the page has no map picker (section 10 has the picture that it has with one)', rows.map((li) => find(li, 'gmap')[0].children.map((i) => i.src)), [['front/preview_islands.png'], ['front/preview_treasure.png'], [], ['front/preview_medium.png']]);
    same('... each row has a Join button that names the room (a name is only text)', rows.map((li) => joinOf(li).textContent + '|' + joinOf(li).getAttribute('aria-label')), [
        'Join|Join Pebble’s room, 3/4 seats taken, no bots, Islands', 'Join|Join Clover’s room, 3/4 seats taken, 1 bot, Treasure', 'Join|Join Juniper’s room, 2/4 seats taken, 1 bot, My Own Map', 'Join|Join Fern’s room, 1/4 seats taken, no bots, Medium']);
    check('... the legend and Join by code are there, and the "off" line is not', find(pop, 'glegend')[0].hidden !== true && textOf(find(pop, 'glegend')[0]).indexOf('open seat') !== -1 && first(pop, 'goff').hidden === true && first(pop, 'gcodelink').textContent === 'Have a code?');
    check('the first button of the list has the focus when the popover opens', env.focused === joinOf(rows[0]));
    pressEscape(env);
    check('Escape puts the popover away and gives the focus back to Games', pop.hidden === true && env.home('games').getAttribute('aria-expanded') === 'false' && env.focused === env.home('games'));
    tap(env.home('games'));
    await flush();
    pressElsewhere(env, env.$('yard') || env.$('home'));
    check('a press anywhere else puts it away too, and a press inside it does not', pop.hidden === true);
    tap(env.home('games'));
    await flush();
    pressElsewhere(env, pop);
    pressElsewhere(env, joinOf(rowsOf(env)[1]));
    pressElsewhere(env, env.home('games'));
    check('... (a press inside the popover, or on its own button, leaves it open)', pop.hidden === false);
    env.home('games').click();
    check('Games again closes it', pop.hidden === true);
    tap(env.home('games'));
    await flush();
    env.makeRoom();
    check('the Room shows: the popover is put away (the screen that has the Games button is not there)', env.screen() === 'room' && pop.hidden === true);
    check('no markup was written into the page by any of this, and no row has a link', env.innerHTMLWrites.length === 0 && find(env.$('home'), 'gitem').every((li) => li.findAll((e) => e.tagName === 'a' || e.hasAttribute('href')).length === 0));
}
{   // a phone: the whole width, and what is left of the height
    const site = fakeSite();
    site.body = listOf([row('aaaaaa', 'Ann')]);
    const env = await page({ site, options: { innerWidth: 390, innerHeight: 844 } });
    tap(env.home('games'));
    await flush();
    same('on a phone the popover has the width of the window less its margins, at the bar\'s end, and no taller than what is left', [popOf(env).style.top, popOf(env).style.left, popOf(env).style.width, popOf(env).style.maxHeight], ['108px', '10px', '370px', '724px']);
    const low = await page({ site, options: { innerWidth: 844, innerHeight: 390 } });
    tap(low.home('games'));
    await flush();
    same('a short window (a phone held sideways, 844 x 390) has it 450 px wide and 270 px high at most: the list scrolls inside it', [popOf(low).style.width, popOf(low).style.maxHeight], ['450px', '270px']);
}
{   // the page's own public room is the first row of its list, tagged Yours (no Join), and the count follows; the other rooms are as before
    const site = fakeSite();
    const env = await page({ site });
    const sock = myRoom(env, ['Maya']);
    const code = helloRoom(sock.sent[0]);
    site.body = listOf([row(code, 'Maya', {}), row('abc234', 'Clover')]);
    env.toggleRoom();
    await flush();
    await wait(env, 30000);
    check('the page\'s own room (by its code) counts: the button says 2', pillOf(env).textContent === '2' && env.home('games').getAttribute('aria-label') === 'Games, 2 open');
    tap(env.home('games'));
    await flush();
    same('... the popover lists it first, then the other room', rowsOf(env).map((li) => textOf(first(li, 'gname'))), ['Maya', 'Clover']);
    check('... the heading says 2 waiting', first(popOf(env), 'gpcount').textContent === '2 waiting · updates live');
    const own = rowsOf(env)[0];
    check('... the own row is tagged Yours and has no Join button and no BEST tag; one other room is not BEST either', textOf(first(own, 'gyours')) === 'Yours' && find(own, 'gjoin').length === 0 && find(own, 'gbest').length === 0 && find(rowsOf(env)[1], 'gbest').length === 0 && find(rowsOf(env)[1], 'gyours').length === 0 && joinOf(rowsOf(env)[1]) !== null);
    check('... the own row shows what the others show (the map, the seats as n/size, the bots)', textOf(first(own, 'gmapname')) === 'Treasure' && textOf(first(own, 'gcount')) === '2/4' && textOf(first(own, 'gbots')) === 'no bots' && find(own, 'gdot').length === 4);
    check('... the focus is on the Join of the other room, not on the own row', env.focused === joinOf(rowsOf(env)[1]));
    site.body = listOf([row(code, 'Maya', {})]);
    await wait(env, 5000);
    check('a list with nothing but the page\'s own room is one row, Yours: "1 waiting", the count 1, no "No open games right now."', first(popOf(env), 'gpcount').textContent === '1 waiting · updates live' && first(popOf(env), 'glist').hidden === false && rowsOf(env).length === 1 && textOf(first(rowsOf(env)[0], 'gyours')) === 'Yours' && pillOf(env).textContent === '1');
    site.body = listOf([row(code, 'Maya', {}), row('abc234', 'Clover', {}), row('def567', 'Fern', { free: 3, people: 1 })]);
    await wait(env, 5000);
    same('two other rooms: the own row first, BEST on the best other row only', rowsOf(env).map((li) => textOf(first(li, 'gname')) + (find(li, 'gbest').length ? '+BEST' : '') + (find(li, 'gyours').length ? '+Yours' : '')), ['Maya+Yours', 'Clover+BEST', 'Fern']);
    site.body = listOf([row('abc234', 'Clover')]);
    await wait(env, 5000);
    check('the own room goes private (it is not in the next answer): its row is gone, the count is 1', rowsOf(env).length === 1 && textOf(first(rowsOf(env)[0], 'gname')) === 'Clover' && pillOf(env).textContent === '1');
    site.body = listOf([]);
    await wait(env, 5000);
    check('no rows at all: "none waiting", the count 0, and the empty cards stand in the place of the rooms (no big "No open games right now." card)', first(popOf(env), 'gpcount').textContent === 'none waiting' && first(popOf(env), 'glist').hidden === false && rowsOf(env).length === 0 && slotsOf(env).length > 0 && pillOf(env).textContent === '0' && textOf(popOf(env)).indexOf('No open games right now.') === -1);
}
{   // the count: 50 rows are "50+"
    const site = fakeSite();
    site.body = listOf(Array.from({ length: 60 }, (_, i) => row('r' + String(i).padStart(5, '0'), 'H' + i)));
    const env = await page({ site });
    check('60 rows are 50 rows and the count says "50+" (the server lists 50 at most, and there may be more)', pillOf(env).textContent === '50+' && env.home('games').getAttribute('aria-label') === 'Games, 50+ open');
    tap(env.home('games'));
    await flush();
    check('... 50 rows are drawn and the heading says "50+ waiting"', rowsOf(env).length === 50 && first(popOf(env), 'gpcount').textContent === '50+ waiting · updates live');
    site.body = listOf(Array.from({ length: 49 }, (_, i) => row('r' + String(i).padStart(5, '0'), 'H' + i)));
    await wait(env, 5000);
    check('49 are "49"', pillOf(env).textContent === '49' && rowsOf(env).length === 49);
}
{   // a malformed list
    const site = fakeSite();
    const untrusted = [
        row('aaaaaa', '<img src=x onerror=alert(1)>'), row('a2a2a2', 'Evil map', { map: 'EVIL<script>.LVL' }), row('bbbbbb', '"><svg/onload=1>'), row('cccccc', '\\u0041\\x41&amp;&lt;b&gt;'), row('dddddd', 'Ann\u202eBob'), row('eeee', 'a'.repeat(33)), row('ffffff', 'Zo\u00eb'),
        row('gggggg', 'Ok', { people: '3' }), row('hhhhhh', 'Ok2', { map: '../../etc/passwd.LVL' }), row('aaaaaa', 'Same code'), row('jjjjjj', 'javascript:alert(1)'), row('kkkkkk', 'http://evil.example/x', { map: 'http.LVL' }), row('K7M2XQ', 'Capital'),
    ];
    site.body = listOf(untrusted);
    const env = await page({ site, options: { picker: true } });
    tap(env.home('games'));
    await flush();
    const names = rowsOf(env).map((li) => textOf(first(li, 'gname')));
    same('a list with markup in the names shows the names that are of the shape, as the text that they are, and drops the rest (a map that is no file name, look-alike characters, too long, wrong types, a path, a code twice, a capital)', names,
        ['<img src=x onerror=alert(1)>', '"><svg/onload=1>', '\\u0041\\x41&amp;&lt;b&gt;', 'javascript:alert(1)', 'http://evil.example/x']);
    same('... and a map that is a file name is its name without the ending, as text', rowsOf(env).map((li) => textOf(first(li, 'gmapname'))), ['Treasure', 'Treasure', 'Treasure', 'Treasure', 'http']);
    check('... nothing was written as markup, nothing has a link, no script element was made, and the only attributes that carry a field are the code (checked), the text of a label and the map\'s own picture', env.innerHTMLWrites.length === 0
          && popOf(env).findAll((e) => e.tagName === 'a' || e.tagName === 'script' || e.tagName === 'iframe' || e.hasAttribute('href') || e.hasAttribute('onclick') || e.hasAttribute('style') && e !== popOf(env)).length === 0);
    check('... every address that a row makes is a picture: one of the page\'s own folder (an original\'s), or the library\'s of a checked file name (section 10)', popOf(env).findAll((e) => e.tagName === 'img').length === 5 && popOf(env).findAll((e) => e.tagName === 'img').every((i) => /^front\/preview_[a-z]+\.png$/.test(i.src) || LIBRARY.test(i.src)));
    check('... and the codes that reach an attribute are the checked ones', popOf(env).findAll((e) => e.hasAttribute('data-code')).every((e) => /^[a-z0-9_-]{1,32}$/.test(e.getAttribute('data-code'))));
}
{   // an answer that is far bigger than any list (10,000 rows, one text)
    const site = fakeSite();
    site.body = listOf(Array.from({ length: 10000 }, (_, i) => row('q' + String(i).padStart(5, '0'), 'Host ' + i)));
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    check('10,000 rows are 50 rows on screen, and "50+" on the button', rowsOf(env).length === 50 && pillOf(env).textContent === '50+' && textOf(first(rowsOf(env)[0], 'gname')) === 'Host 0');
}
{   // the list that changes while it is open keeps the focus on the same button
    const site = fakeSite();
    site.body = listOf([row('aaaaaa', 'Ann'), row('bbbbbb', 'Bea'), row('cccccc', 'Cy')]);
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    joinOf(rowsOf(env)[1]).focus();
    site.body = listOf([row('zzzzzz', 'New'), row('aaaaaa', 'Ann'), row('bbbbbb', 'Bea')]);
    await wait(env, 5000);
    check('a list that changes while it is open keeps the focus on the button of the same room (and shows the new row)', env.focused && env.focused.getAttribute('data-code') === 'bbbbbb' && rowsOf(env).length === 3 && textOf(first(rowsOf(env)[0], 'gname')) === 'New');
}
{   // an answer that says that it is huge is not read; one that is the same list again does not make the rows again
    const site = fakeSite();
    site.body = listOf([row('aaaaaa', 'Ann')]);
    site.length = 3000000;
    const env = await page({ site });
    check('an answer whose length is over 2 MB (it says so) is not read at all, and the list is off', site.calls.length === 1 && site.reads === 0 && pillOf(env).hidden === true);
    site.length = 1999999;
    await wait(env, 90000);
    check('(one that says 1,999,999 is read: the limit is 2,000,000)', site.reads >= 1 && pillOf(env).hidden === false && pillOf(env).textContent === '1');
    site.length = 2000000;
    const before = site.reads;
    await wait(env, 90000);
    check('(one that says 2,000,000 is read too: only more than that is not)', site.reads > before);
    site.length = undefined;
}
{   // the rows are made again only when what they show has changed: the age of a row is not shown, and a press that straddles a look is not lost
    const site = fakeSite();
    site.body = listOf([row('aaaaaa', 'Ann', { age: 30 }), row('bbbbbb', 'Bea', { age: 10 })]);
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    const before = rowsOf(env), button = joinOf(before[0]);
    button.focus();
    site.body = listOf([row('aaaaaa', 'Ann', { age: 60 }), row('bbbbbb', 'Bea', { age: 40 })]);
    await wait(env, 6000);
    check('a look that finds the same rooms (only older) leaves the rows and their buttons as they were: the same nodes, the focus where it was', site.calls.length >= 3 && rowsOf(env).length === 2 && rowsOf(env)[0] === before[0] && joinOf(rowsOf(env)[0]) === button && env.focused === button);
    site.body = listOf([row('aaaaaa', 'Ann', { people: 3, free: 1 }), row('bbbbbb', 'Bea')]);
    await wait(env, 6000);
    check('... a room that fills up is drawn again (the count shows it: 3/4)', textOf(first(rowsOf(env)[0], 'gcount')) === '3/4' && rowsOf(env)[0] !== before[0]);
}
{   // two looks are a second apart at least: a tab that comes and goes, and Home and the Room, do not make a look each
    const env = await page();
    await wait(env, 5000);
    const base = env.looks();
    for (let i = 0; i < 6; i++) { env.hide(true); env.hide(false); }
    await flush();
    check('six comings and goings of the tab in one moment make one look at most', env.looks() <= base + 1, env.looks() + ' looks, ' + base + ' before');
    await wait(env, 1000);
    const again = env.looks();
    env.makeRoom();
    env.toggleRoom();
    env.makeRoom();
    env.toggleRoom();
    await flush();
    check('... and so do the toggles between Home and the Room', env.looks() <= again + 1, env.looks() + ' looks, ' + again + ' before');
    check('... and the look comes when its second is over (nothing is lost)', (await wait(env, 1000), env.looks() >= again + 1 || env.looks() === again));
    const times = env.site.times;
    let tightest = Infinity;
    for (let i = 1; i < times.length; i++) tightest = Math.min(tightest, times[i] - times[i - 1]);
    check('no two looks of the whole run are less than a second apart (the smallest gap is ' + tightest + ' ms)', tightest >= 1000);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 9. Home docks the list in its body (#hmain), always there, as the games panel; the popover (sections 3 to 8 run it, mounted with no dock) is what the list does where it is given none
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const site = fakeSite();
    site.body = listOf([row('p4n8dz', 'Pebble', { people: 1, free: 3, age: 10 }), row('c1v3rr', 'Clover', { people: 2, bots: 1, free: 1, age: 200 }), row('j7n1pr', 'Juniper', { free: 2, age: 120 })]);
    const env = await page({ site, options: { popover: false } });
    const pop = popOf(env), yard = env.$('home').children[1];
    check('the list is in Home\'s body, not a popover: #hmain holds it, it is shown from the first look on (nobody pressed Games), and it is a region named "Open games", not a dialog', yard.id === 'hmain' && pop.parent === yard && classes(pop).indexOf('gdock') !== -1 && pop.hidden === false
          && pop.getAttribute('role') === 'region' && pop.getAttribute('aria-label') === 'Open games' && classes(yard).join() === 'hmain');
    check('... the body holds nothing else but the room\'s panel (no stand-in), and Games names no popup and says nothing of being open (it is not a popover)', yard.children.length === 2 && yard.children[0] === env.$('lobby') && env.home('games').getAttribute('aria-haspopup') === null && env.home('games').getAttribute('aria-expanded') === null);
    same('... the rows are the Games list\'s own, best first (the fewest open seats, then the oldest), BEST on the first, with a Join button each', [rowsOf(env).map((li) => textOf(first(li, 'gname'))), find(popOf(env), 'gbest').length, rowsOf(env).map((li) => !!joinOf(li))], [['Clover', 'Juniper', 'Pebble'], 1, [true, true, true]]);
    same('... the count beside the heading and on the Games button are the same 3', [textOf(first(popOf(env), 'gpcount')).indexOf('3 waiting') === 0, textOf(first(env.home('games'), 'pill'))], [true, '3']);
    check('... and the "Join by code" box is under the heading, shut, with "Have a code?" in the heading', !!first(popOf(env), 'gcode') && popOf(env).children[1] === first(popOf(env), 'gcode') && first(popOf(env), 'gcode').hidden === true && first(first(popOf(env), 'gphead'), 'gcodelink').getAttribute('aria-expanded') === 'false');
    tap(env.home('games'));
    check('Games takes the focus to the list (the first Join) and the list stays; pressed again, the same; Escape and a press elsewhere do not put it away', env.focused === joinOf(rowsOf(env)[0]) && pop.hidden === false);
    tap(env.home('games')); pressEscape(env); pressElsewhere(env, env.$('home'));
    check('... (still there)', pop.hidden === false && env.looks() >= 1 && popOf(env) === pop);
    await wait(env, 5000); await wait(env, 5000);
    check('the list is looked at every 5 s while Home shows, without any press (the pace of an open list), and the same rows stay (the same nodes)', env.site.times.slice(0, 3).join() === '0,5000,10000' && rowsOf(env).length === 3);
    tap(joinOf(rowsOf(env)[0]));
    check('Join in the docked list is the popover\'s Join: a second client that only joins, with the room of the row', env.sockets.length === 1 && hex(env.last().sent[0] || env.last().open().sent[0]) === hex(helloBytes(GUEST, 'c1v3rr', { join: true })));
    env.makeRoom();
    const onRoom = env.looks();
    await wait(env, 20000);
    check('while the Room shows nothing is asked (the list is not on screen)', env.screen() === 'room' && env.looks() === onRoom);
}
{
    const env = await page({ options: { popover: false } });     // no rooms: the empty cards are in the body, and Join by code under them
    check('no open games: the empty cards ("Open lobby") and Join by code are in the docked list', slotsOf(env).length > 0 && !!first(popOf(env), 'gcode') && textOf(first(popOf(env), 'gpcount')) === 'none waiting');
    same('... each card has the one button, Host here, and nothing else to press (the bar above has Quick host and Quick join)', slotsOf(env).map((li) => li.findAll((e) => e.tagName === 'button').map(textOf).join()), slotsOf(env).map(() => TEXT.slotHost));
    slotsOf(env)[0].findAll((e) => e.tagName === 'button')[0].click();
    const asked = hostCard(env);
    same('... a press on it does what Quick host does with no room: the card "Host a game" with Public room, Invite only and Cancel, nothing made yet', [!!asked, asked ? find(asked, 'btn').map(textOf) : [], env.sockets.length], [true, [TEXT.hostPublic, TEXT.hostPrivate, TEXT.cancel], 0]);
    pressEscape(env);
}
{
    const env = await page({ options: { popover: true } });         // the test site: the popover, closed until Games is pressed, as ever
    check('a list that is given no dock is the popover that it was: not in the body, hidden until Games is pressed, a dialog', popOf(env).parent !== env.$('home').children[1] && popOf(env).hidden === true && popOf(env).getAttribute('role') === 'dialog' && classes(popOf(env)).indexOf('gdock') === -1
          && env.home('games').getAttribute('aria-haspopup') === 'dialog');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 10. The pictures of the rows. An original has the page's own (front/preview_<key>.png). A map of the players has the library's: /maps/previews/<its file name without .lvl, percent-encoded>.webp, the address that the
//     server's own GET /maps gives (AntsMapPicker.previewPath, as the match pages make it from a name: no list is fetched for it). A picture that does not come (a 404: a map that the library has no picture of, an approved
//     map, a test map) is taken away, so that no broken image shows and the square stays.
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const fixture = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'data', 'home_games_thumbs.json'), 'utf8')).maps;
    const library = JSON.parse(fs.readFileSync(path.join(__dirname, '..', '..', 'Community-Maps', 'previews.json'), 'utf8'));       // (file -> { picture }: what the server turns into the "thumb" of its list)
    const serverEncode = (s) => Array.from(s).map((c) => (/[A-Za-z0-9\-._~]/.test(c) ? c : '%' + c.charCodeAt(0).toString(16).toUpperCase().padStart(2, '0'))).join('');       // (map_store.cpp percent_encode)
    const rowsFor = (files) => files.map((file, i) => row('r' + String(i).padStart(5, '0'), 'H' + i, { map: file, people: 1, free: 3, age: 5000 - i }));
    const LOOK = 7000;          // (the open list is looked at every 5 s, 6 s at the latest with the jitter: a look is done by then)
    const site = fakeSite();
    site.body = listOf(rowsFor(fixture.map((m) => m.file)));
    const env = await page({ site, options: { popover: false, picker: true } });
    const srcOf = (li) => find(li, 'gmap')[0].children.map((i) => i.src);
    same('a map of the players has the library\'s picture, at the address that the server\'s list gives it (spaces, dots, dashes, !, #, $, &, \', ~, capitals and an ending in .LVL: ' + fixture.length + ' maps)', rowsOf(env).map((li) => srcOf(li)), fixture.map((m) => [m.thumb]));
    check('... and the address is of the one shape that the page loads, with a picture to a row and nothing but the picture in its square', rowsOf(env).every((li) => srcOf(li).length === 1 && LIBRARY.test(srcOf(li)[0]) && find(li, 'gmap')[0].tagName === 'span'));
    check('... a picture is loaded when it is near (lazily: a list of 50 rows is not 50 requests), has no text for a screen reader, and keeps its size', rowsOf(env).every((li) => { const i = find(li, 'gmap')[0].children[0]; return i.getAttribute('loading') === 'lazy' && i.alt === '' && i.getAttribute('width') === '300' && i.getAttribute('height') === '300'; }));
    check('... the page asked for no list of maps for it (no request for /maps or /maps/all, the 96 KB list)', env.fetches.length > 0 && env.fetches.every((f) => !/^\/maps/.test(f.url)), JSON.stringify(env.fetches.map((f) => f.url)));

    const files = Object.keys(library);
    let seen = 0, wrong = [];
    for (let at = 0; at < files.length; at += 50) {          // every map of the library, 50 rows (the list's cap) at a time
        const batch = files.slice(at, at + 50);
        site.body = listOf(rowsFor(batch));
        await wait(env, LOOK);
        const li = rowsOf(env);
        if (li.length !== batch.length) { wrong.push('rows ' + li.length + ' of ' + batch.length + ' at ' + at); continue; }
        li.forEach((node, i) => { const want = '/maps/previews/' + serverEncode(library[batch[i]].picture); const got = srcOf(node); seen++; if (got.length !== 1 || got[0] !== want) wrong.push(batch[i] + ' -> ' + got.join() + ' (the server: ' + want + ')'); });
    }
    check('every one of the ' + files.length + ' maps of Community-Maps/ has the address that the server gives it as the "thumb" (percent-encoded from previews.json\'s picture): ' + seen + ' rows looked at', seen === files.length && files.length >= 500 && wrong.length === 0, wrong.slice(0, 3).join(' | ') + ' ... ' + wrong.length);

    // a picture that does not come: the picture goes, the square, the row and the other pictures stay (no broken image)
    site.body = listOf([row('aaaaaa', 'Has', { map: 'POPcOrN.lvl', people: 1, free: 3, age: 30 }), row('bbbbbb', 'Hasnt', { map: 'No Picture Yet.lvl', people: 1, free: 3, age: 20 }), row('cccccc', 'Own', { map: 'TINY.LVL', people: 1, free: 3, age: 10 })]);
    await wait(env, LOOK);
    let li = rowsOf(env);
    same('(three rows: the library\'s picture, a map that the library has no picture of, an original)', li.map(srcOf), [['/maps/previews/POPcOrN.webp'], ['/maps/previews/No%20Picture%20Yet.webp'], ['front/preview_tiny.png']]);
    const lost = find(li[1], 'gmap')[0].children[0];
    lost.fire('error');
    li = rowsOf(env);
    same('a picture that is not at the library\'s address (a 404: the image says error) is asked for in the other ending, once (an approved map\'s picture is a PNG), and the others stay', li.map(srcOf), [['/maps/previews/POPcOrN.webp'], ['/maps/previews/No%20Picture%20Yet.png'], ['front/preview_tiny.png']]);
    // (the picker asks a picture of a map of the players again after a pause before it gives it up: the pauses are the page's timers, run by moving its clock)
    const giveUp = async (image) => { image.fire('error'); for (let i = 0; i < 6; i++) { await wait(env, 21000); image.fire('error'); } };   // (the page's clock: the picker's pauses run as time goes by, each followed by the error of a picture that is still not there)
    await giveUp(lost);
    li = rowsOf(env);
    same('... and when that does not come either (after the pauses that the picker waits for the server to draw it), it is taken away, and the others stay', li.map(srcOf), [['/maps/previews/POPcOrN.webp'], [], ['front/preview_tiny.png']]);
    check('... the square is still there, hidden from a screen reader, and the row is whole (the name, the seats, Join)', li.every((x) => find(x, 'gmap').length === 1 && find(x, 'gmap')[0].getAttribute('aria-hidden') === 'true') && textOf(first(li[1], 'gname')) === 'Hasnt' && !!joinOf(li[1]) && textOf(first(li[1], 'gcount')) === '1/4');
    find(li[2], 'gmap')[0].children[0].fire('error');           // (an original has no other ending: gone at once)
    find(li[0], 'gmap')[0].children[0].fire('error');
    await giveUp(find(li[0], 'gmap')[0].children[0]);
    check('... an original\'s picture that does not come goes the same way (any picture of the list)', rowsOf(env).every((x) => srcOf(x).length === 0 && find(x, 'gmap').length === 1));
    lost.fire('error');
    check('... and a second word of the same image does no harm (the list is still there)', rowsOf(env).length === 3);

    // a test map of the Map Maker (<name> ~test-<8 hex>) has only the server's PNG, asked at once; an approved map's picture, found in the other ending, is remembered: the list drawn again asks for the ending that came
    const fight = (extra) => listOf([row('dddddd', 'Tess', { map: 'Fight Island ~test-0a1b2c3d.lvl', people: 1, free: 3, age: 30 }), row('eeeeee', 'Appr', { map: 'Fight Island.lvl', people: 1, free: 3, age: 20 })].concat(extra || []));
    const by = (name) => rowsOf(env).find((x) => textOf(first(x, 'gname')) === name);
    site.body = fight();
    await wait(env, LOOK);
    same('a test map is asked in the server\'s ending at once, an approved map (not known to be one) in the library\'s', [srcOf(by('Tess')), srcOf(by('Appr'))], [['/maps/previews/Fight%20Island%20~test-0a1b2c3d.png'], ['/maps/previews/Fight%20Island.webp']]);
    const appr = find(by('Appr'), 'gmap')[0].children[0];
    appr.fire('error');
    same('... which, when it is not there, is asked in the other ending, and no more', srcOf(by('Appr')), ['/maps/previews/Fight%20Island.png']);
    appr.fire('load');
    site.body = fight([row('ffffff', 'Newc', { map: 'POPcOrN.lvl', people: 1, free: 3, age: 10 })]);
    await wait(env, LOOK);
    same('... and a list that is drawn again asks for the ending that came (no second 404 for the same picture)', [srcOf(by('Tess')), srcOf(by('Appr')), srcOf(by('Newc'))], [['/maps/previews/Fight%20Island%20~test-0a1b2c3d.png'], ['/maps/previews/Fight%20Island.png'], ['/maps/previews/POPcOrN.webp']]);

    // names that mean something to an address: all of it is escaped, nothing of it can name another place, a name that is not a map's file name makes no row
    const nasty = ['a%2Fb.lvl', 'a%5Cb.lvl', 'a%00b.lvl', '100% #1 & (2)+3;4=5@6[7]{8}^`9,0.lvl', "it's a ~~..lvl", 'a b.LVL', 'x'.repeat(60) + '.lvl', '%41.lvl', 'a..b.lvl', '-.lvl', '~.lvl'];
    site.body = listOf(rowsFor(nasty.concat(['.hidden.lvl', 'a/b.lvl', 'a\\b.lvl', 'a?b.lvl', 'a<b>.lvl', 'a"b.lvl', 'a:b.lvl', 'a|b.lvl', 'a*b.lvl', '.lvl', 'noending'])));
    await wait(env, LOOK);
    li = rowsOf(env);
    same('(the names that are not a map\'s file name make no row at all: a leading dot, / \\ ? < > " : | *, a name that is too short, no ending)', li.length, nasty.length);
    check('names with %, #, &, +, ;, =, @, brackets, an apostrophe, a name of 64 characters: every address is the one shape, escaped (%25 for a percent, never a raw sign, never an escaped slash)', li.every((x) => srcOf(x).length === 1 && LIBRARY.test(srcOf(x)[0]) && !/[^A-Za-z0-9._~%\/-]/.test(srcOf(x)[0])), li.map(srcOf).join(' '));
    same('... a percent sign is %25 (so "a%2Fb" never reads as a slash), a space is %20, dots and dashes stay', [srcOf(li[0])[0], srcOf(li[1])[0], srcOf(li[2])[0], srcOf(li[5])[0], srcOf(li[8])[0], srcOf(li[9])[0], srcOf(li[10])[0]],
        ['/maps/previews/a%252Fb.webp', '/maps/previews/a%255Cb.webp', '/maps/previews/a%2500b.webp', '/maps/previews/a%20b.webp', '/maps/previews/a..b.webp', '/maps/previews/-.webp', '/maps/previews/~.webp']);
}
{   // the test site's popover: nothing is drawn (so nothing is asked for) until Games is pressed, and then the rows have the library's picture as the docked list's do
    const site = fakeSite();
    site.body = listOf([row('aaaaaa', 'Has', { map: 'POPcOrN.lvl', people: 1, free: 3, age: 30 })]);
    const env = await page({ site, options: { popover: true, picker: true } });
    check('a popover that is closed has no picture in it (no row is drawn: the page asks for none)', popOf(env).findAll((e) => e.tagName === 'img').length === 0);
    tap(env.home('games'));
    await flush();
    same('... opened, its row has the library\'s picture', rowsOf(env).map((li) => find(li, 'gmap')[0].children.map((i) => i.src)), [['/maps/previews/POPcOrN.webp']]);
}
{   // a page that has no map picker (its script did not come): the rows have the originals' pictures and no other, and nothing breaks
    const site = fakeSite();
    site.body = listOf([row('aaaaaa', 'Has', { map: 'POPcOrN.lvl', people: 1, free: 3, age: 30 }), row('cccccc', 'Own', { map: 'TINY.LVL', people: 1, free: 3, age: 10 })]);
    const env = await page({ site, options: { popover: false } });
    same('without AntsMapPicker an original has its picture and a map of the players has none (the list works)', rowsOf(env).map((li) => find(li, 'gmap')[0].children.map((i) => i.src)), [[], ['front/preview_tiny.png']]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 11. The locked cards of private rooms: a row that says private: true and has no code. It is a card with a padlock and nothing to press, and the room's key is nowhere in the page.
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const good = locked('Maple', { map: 'SMALL.LVL', mode: '187', people: 2, bots: 1, free: 1 });
    const parsed = Games.lockedOf(good, RULES0);
    same('a locked row: the host, the map (an original by its name and its key), the mode, the seats; nothing else (no code, no age)', parsed, { host: 'Maple', map: 'SMALL.LVL', mapKey: 'small', mapName: 'Small', people: 2, bots: 1, free: 1, size: 4, fog: false, mode187: true });
    same('a mode that is not 187 is a plain match', Games.lockedOf(locked('A'), RULES0).mode187, false);
    same('a map of the players has its file name without the ending', Games.lockedOf(locked('A', { map: 'My Map.LVL' }), RULES0).mapName, 'My Map');
    const extra = Games.lockedOf(Object.assign({}, good, { url: '/room/x', id: 'abc', href: 'http://x', link: 'y', room: 'z', key: 'ff', age: 9 }), RULES0);
    same('fields that are not in the shape (an address, an id, a link, a key) are not read, and nothing of them is in the row', Object.keys(extra).sort(), Object.keys(parsed).sort());
    for (const code of ['k7m2xq', '', null, 7, undefined]) check('a row that has a code (' + JSON.stringify(code) + ') is dropped: a private row has none, and one that has is not drawn', Games.lockedOf(Object.assign({}, good, { code }), RULES0) === null);
    for (const [what, value] of [['private missing', locked('A', { private: undefined })], ['private false', locked('A', { private: false })], ['private as text', locked('A', { private: 'true' })], ['private as 1', locked('A', { private: 1 })],
        ['no object', null], ['an array', [1]], ['a text', 'row'],
        ['a host of 33 characters', locked('x'.repeat(33))], ['a host with a right-to-left mark', locked('Ann‮Bob')], ['a host that is a number', locked(7)],
        ['a map that is no file name', locked('A', { map: '../x.LVL' })], ['a mode that is a number', locked('A', { mode: 1 })],
        ['people as text', locked('A', { people: '2' })], ['seats that do not add up to the size', locked('A', { people: 2, bots: 0, free: 1, size: 4 })], ['no free color', locked('A', { people: 4, bots: 0, free: 0, size: 4 })], ['a size of 5', locked('A', { people: 1, bots: 0, free: 4, size: 5 })]]) {
        check('a locked row with ' + what + ' is dropped', Games.lockedOf(value, RULES0) === null);
    }
    for (const field of ['host', 'map', 'mode', 'people', 'bots', 'free', 'size']) {
        const without = Object.assign({}, good); delete without[field];
        check('a locked row without its ' + field + ' is dropped', Games.lockedOf(without, RULES0) === null);
    }
    const mixed = listOf([row('aaaaaa', 'Open'), locked('Maple'), 'x', locked('Quill', { mode: '187' }), Object.assign(locked('Leak'), { code: 'bbbbbb' }), locked('Maple'), row('cccccc', 'Open2'), { code: 'dddddd' }]);
    same('the locked rows of a list: in its order, a look-alike twice is two cards, a row with a code and a public row are not among them', Games.parseLocked(mixed, RULES0).map((r) => r.host), ['Maple', 'Quill', 'Maple']);
    same('... and the joinable rows are the public ones alone (a locked row is no row of the open list)', Games.parseList(mixed, RULES0).map((r) => r.code), ['aaaaaa', 'cccccc']);
    same('50 locked rows at most, however many the answer holds', Games.parseLocked(listOf(Array.from({ length: 10000 }, (_, i) => locked('H' + i))), RULES0).length, 50);
    {   // as for the open rooms: a list of 10,000 entries is not read to its end
        const touched = { n: 0 };
        const entries = new Proxy(Array.from({ length: 10000 }, () => 'x'), { get(target, key, receiver) { if (/^\d+$/.test(String(key))) touched.n++; return Reflect.get(target, key, receiver); } });
        check('10,000 entries that are no locked rows are looked at only up to a bound (500), and the answer is an empty list', Games.parseLocked({ format: 1, lobbies: entries }, RULES0).length === 0 && touched.n <= 501, 'looked at ' + touched.n);
    }
    same('an answer that is no list has no locked rows', [Games.parseLocked(null, RULES0), Games.parseLocked({ format: 2, lobbies: [locked('A')] }, RULES0), Games.parseLocked({ format: 1, lobbies: 'x' }, RULES0), Games.parseLocked([locked('A')], RULES0)], [[], [], [], []]);
}
{   // Fog of War (protocol 28): a row says it with the boolean true and nothing else; a card shows the tag FOG, an open card and a locked one, and a row that flips repaints
    same('a row\'s fog is true only for the boolean true', [true, false, 'true', 1, 0, null, undefined, {}, []].map((fog) => Games.rowOf(row('k7m2xq', 'Pebble', { fog }), RULES0).fog), [true, false, false, false, false, false, false, false, false]);
    same('a locked row\'s fog is true only for the boolean true', [true, false, 'true', 1, null, undefined].map((fog) => Games.lockedOf(locked('Maple', { fog }), RULES0).fog), [true, false, false, false, false, false]);
    same('a row with no fog key (an older server) is a room without fog', [Games.rowOf(row('k7m2xq', 'Pebble'), RULES0).fog, Games.lockedOf(locked('Maple'), RULES0).fog], [false, false]);
    const site = fakeSite();
    site.body = listOf([row('k7m2xq', 'Pebble', { fog: true }), row('abc234', 'Clover', { map: 'TREASURE.LVL', people: 2, bots: 1, free: 1 }), locked('Maple', { fog: true, mode: '187' }), locked('Rowan', { map: 'TINY.LVL' })]);
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    const fogOf = (li) => find(li, 'gfog').map(textOf).join();
    const hostOf = (li) => textOf(first(li, 'gname'));
    same('an open card with fog has the tag FOG beside the map\'s name, one without has none', rowsOf(env).slice(0, 2).map((li) => hostOf(li) + ':' + fogOf(li)).sort(), ['Clover:', 'Pebble:' + TEXT.fogTag]);
    same('a locked card with fog has the tag after the invite-only tag and 187, one without has none', lockedCards(env).map((li) => [find(li, 'gmarks')[0].children.map(textOf).join('|'), fogOf(li)]), [[TEXT.privateTag + '|187|' + TEXT.fogTag, TEXT.fogTag], [TEXT.privateTag, '']]);
    same('the tag is the word Fog', TEXT.fogTag, 'Fog');
    check('a card still has its Join button and a locked one none, with fog or without', rowsOf(env).slice(0, 2).every((li) => joinOf(li) !== undefined) && rowsOf(env).slice(2).every((li) => joinOf(li) === undefined));
    site.body = listOf([row('k7m2xq', 'Pebble', { fog: false }), row('abc234', 'Clover', { map: 'TREASURE.LVL', people: 2, bots: 1, free: 1 }), locked('Maple', { fog: true, mode: '187' }), locked('Rowan', { map: 'TINY.LVL' })]);
    await wait(env, 8000);
    same('an open room whose leader switched fog off is repainted without the tag, though nothing else of its row changed', rowsOf(env).slice(0, 2).map((li) => hostOf(li) + ':' + fogOf(li)).sort(), ['Clover:', 'Pebble:']);
    same('... and the locked card that still has fog still has it', lockedCards(env).map(fogOf), [TEXT.fogTag, '']);
    site.body = listOf([row('k7m2xq', 'Pebble', { fog: false }), row('abc234', 'Clover', { map: 'TREASURE.LVL', people: 2, bots: 1, free: 1 }), locked('Maple', { fog: false, mode: '187' }), locked('Rowan', { map: 'TINY.LVL' })]);
    await wait(env, 8000);
    same('a locked room whose leader switched fog off is repainted without the tag', lockedCards(env).map(fogOf), ['', '']);
}
{   // the cards in the list: joinable rooms first, locked ones after them; nothing to press on a locked one
    const site = fakeSite();
    site.body = listOf([row('k7m2xq', 'Pebble', { map: 'ISLANDS.LVL', people: 3, free: 1 }), row('abc234', 'Clover', { map: 'TREASURE.LVL', people: 2, bots: 1, free: 1 }),
                        locked('Maple', { map: 'SMALL.LVL' }), locked('Quill', { map: 'TREASURE.LVL', mode: '187', people: 1, bots: 2, free: 1 }), locked('Rowan', { map: 'TINY.LVL', people: 1, free: 3 })]);
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    const cards = lockedCards(env);
    same('the rows: the joinable rooms in their order, then the locked ones in the answer\'s', rowsOf(env).map((li) => textOf(first(li, 'gname'))), ['Pebble', 'Clover', 'Maple', 'Quill', 'Rowan']);
    same('a locked card names the host and the map, has the invite-only tag (and 187 for that mode), and the seats as n of the size and the bots', cards.map((li) => [textOf(first(li, 'gname')), textOf(first(li, 'gmapname')), find(li, 'gprivtag').map(textOf).join(), find(li, 'gmode').map(textOf).join(), textOf(first(li, 'gcount')), textOf(first(li, 'gbots'))].join('|')),
        ['Maple|Small|' + TEXT.privateTag + '||2/4|no bots', 'Quill|Treasure|' + TEXT.privateTag + '|187|3/4|2 bots', 'Rowan|Tiny|' + TEXT.privateTag + '||1/4|no bots']);
    same('... the dots as an open room draws them', cards.map((li) => find(first(li, 'gdots'), 'gdot').map((d) => classes(d).filter((c) => c !== 'gdot').join('.')).join(' ')),
        ['player.c0 player.c1 open.c2 open.c3', 'player.c0 bot.c1 bot.c2 open.c3', 'player.c0 open.c1 open.c2 open.c3']);
    same('the joinable rooms have their Join buttons and the locked cards have none: no button, no link, no control of any kind, no focus stop, no data-code, no address', [rowsOf(env).filter((li) => joinOf(li)).length,
        cards.map((li) => li.findAll((e) => e.tagName === 'button' || e.tagName === 'a' || e.tagName === 'input' || e.tagName === 'select' || e.hasAttribute('tabindex') || e.hasAttribute('href') || e.hasAttribute('data-code') || e.hasAttribute('onclick') || e.hasAttribute('role')).length)], [2, [0, 0, 0]]);
    check('a press on a locked card does nothing (no listener for a press, a key or a touch on it or on a part of it)', cards.every((li) => li.all().every((e) => !e.listeners || ['click', 'pointerdown', 'pointerup', 'mousedown', 'touchstart', 'keydown', 'keyup', 'keypress'].every((t) => !(e.listeners[t] || []).length))));
    same('a screen reader hears one sentence for a card (the pieces are hidden from it): the room is called what its tag says, and it cannot be joined', cards.map((li) => [find(li, 'sr').map(textOf).join(), first(li, 'gtxt').getAttribute('aria-hidden'), first(li, 'glockbox').getAttribute('aria-hidden')].join('|')),
        [TEXT.privateRoom + ', cannot join. Maple’s room, Small, 2/4 seats taken, no bots.|true|true', TEXT.privateRoom + ', cannot join. Quill’s room, Treasure, 3/4 seats taken, 2 bots.|true|true', TEXT.privateRoom + ', cannot join. Rowan’s room, Tiny, 1/4 seats taken, no bots.|true|true']);
    same('the heading counts the joinable rooms, the invite-only ones (and the matches in progress when the door shows them)', textOf(first(popOf(env), 'gpcount')), '2 open, 3 ' + LOCKED);
    same('the button of the Games list counts the joinable rooms alone (a locked room is not a game to join)', [pillOf(env).textContent, env.home('games').getAttribute('aria-label')], ['2', 'Games, 2 open']);
    check('the first button of the list is the first Join (the locked cards are no stop)', env.focused === joinOf(rowsOf(env)[0]));
}
{   // with a match in progress (the door of /live answers) the heading adds it, and the box of matches stands under the cards
    const site = fakeSite();
    site.live.reject = false;
    site.live.body = { live: [{ id: 'TINY-20261119-120000Z', map: 'TINY.LVL', mode: 'highest-score', started: 1, turns: 400, seconds: 20, players: ['Green (Ann)', 'Red (Bob)'] }], count: 1 };
    site.body = listOf([row('k7m2xq', 'Pebble', { map: 'ISLANDS.LVL', people: 3, free: 1 }), locked('Maple')]);
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    same('with a match in progress the heading adds it', textOf(first(popOf(env), 'gpcount')), '1 open, 1 ' + LOCKED + ', 1 in progress');
    same('... and the box of matches is under the cards, unchanged (a watchable match, with its Watch link)', [find(popOf(env), 'glive').length, find(popOf(env), 'gpriv').length, find(popOf(env), 'gwatch').length], [1, 1, 1]);
    same('... the count on the Games button is the open games and the matches, as it was (a locked room is in neither)', pillOf(env).textContent, '2');
}
{   // what the answer carries is never drawn: a row that holds a code is dropped, and the other names of a way to the room are not read; the whole page is searched, every text and every attribute
    const site = fakeSite();
    site.body = listOf([locked('Maple', { code: 'sec00001', url: '/?room=sec00002', id: 'sec00003', href: 'sec00004', link: 'sec00005', join: 'sec00006', key: 'sec00007', room: 'sec00008' }), locked('Quill', { url: '/?room=sec00012', id: 'sec00013', href: 'sec00014', link: 'sec00015', join: 'sec00016', key: 'sec00017', room: 'sec00018', roomCode: 'sec00019' })]);
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    const all = env.dumpAll();
    check('a row with a code is not drawn at all; the one without is, and no code, address, id, link or key of either is anywhere in the page (every text and every attribute)', lockedCards(env).length === 1 && textOf(first(lockedCards(env)[0], 'gname')) === 'Quill' && !/sec000[0-9][0-9]/.test(all) && !/\/\?room=|room=/.test(all), all.match(/sec000[0-9][0-9]/));
    check('the page has asked for nothing but the list (no address was made of a row)', env.site.calls.every((c) => c.url === '/lobbies'));
}
{   // nothing can be joined: the empty cards stand where the rooms would be, and the locked cards stay after them (a locked card takes no place: it is nothing to press)
    const site = fakeSite();
    site.body = listOf([locked('Maple', { map: 'SMALL.LVL' }), locked('Quill', { map: 'TREASURE.LVL', people: 3, free: 1 })]);
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    const pop = popOf(env);
    const kinds = () => first(pop, 'glist').children.map((li) => classes(li).indexOf('gslot') !== -1 ? 'slot' : classes(li).indexOf('gpriv') !== -1 ? 'locked' : 'room');
    same('the empty cards stand first and the two locked cards after them (no strip, no big card)', kinds(), ['slot', 'slot', 'slot', 'locked', 'locked']);
    same('the list is shown with the two locked cards, and the heading says none open', [first(pop, 'glist').hidden, lockedCards(env).length, textOf(first(pop, 'gpcount'))], [false, 2, 'none open, 2 ' + LOCKED]);
    same('the count on the Games button is 0 joinable rooms (the button shows no count of locked ones, and none of the empty cards)', [pillOf(env).textContent, env.home('games').getAttribute('aria-label')], ['0', 'Games, 0 open']);
    check('the first button is Host here of the first empty card', env.focused === joinOf(slotsOf(env)[0]));
    tap(joinOf(slotsOf(env)[0]));
    check('Host here does what Quick host does (the question: Public room or Invite only)', hostCard(env) !== null);
    pressEscape(env);
    check('... and Escape gives the focus back to the card that was pressed', env.focused === joinOf(slotsOf(env)[0]));
    tap(env.home('games'));
    await flush();
    site.body = listOf([]);
    await wait(env, 6000);
    same('when the last locked card goes the empty cards are all that is left, and the heading says none waiting', [kinds(), textOf(first(pop, 'gpcount'))], [['slot', 'slot', 'slot'], 'none waiting']);
}
{   // private rooms, nothing to join and a match in progress: the empty cards (one row now) stand over the locked cards, and the matches under them
    const site = fakeSite();
    site.live.reject = false;
    site.live.body = { live: [{ id: 'TINY-20261119-120000Z', map: 'TINY.LVL', mode: 'highest-score', started: 1, turns: 400, seconds: 20, players: ['Green (Ann)', 'Red (Bob)'] }], count: 1 };
    site.body = listOf([locked('Maple')]);
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    await wait(env, 6000);
    const pop = popOf(env);
    const kinds = () => first(pop, 'glist').children.map((li) => classes(li).indexOf('gslot') !== -1 ? 'slot' : classes(li).indexOf('gpriv') !== -1 ? 'locked' : 'room');
    same('private rooms and a match, nothing to join: the empty cards (two: the list is one card wide, with a match in progress) stand over the locked card, and the match is in its own box', [kinds(), find(pop, 'glive').length, textOf(first(pop, 'gpcount'))], [['slot', 'slot', 'locked'], 1, 'none open, 1 ' + LOCKED + ', 1 in progress']);
    site.body = listOf([]);
    await wait(env, 6000);
    same('no private room, no open game, a match in progress: the empty cards (two) and the match box', [kinds(), find(pop, 'glive').length, textOf(first(pop, 'gpcount'))], [['slot', 'slot'], 1, '0 open, 1 in progress']);
}
{   // six cards, then one line for the rest
    const site = fakeSite();
    const many = (n) => Array.from({ length: n }, (_, i) => locked('H' + i, { map: ['SMALL.LVL', 'TINY.LVL', 'TREASURE.LVL'][i % 3] }));
    site.body = listOf([row('k7m2xq', 'Pebble', { map: 'ISLANDS.LVL', people: 3, free: 1 })].concat(many(12)));
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    const pop = popOf(env);
    const more = () => find(pop, 'gmore').map(textOf);
    same('12 invite-only rooms: the first 6 cards and one line that counts the rest', [lockedCards(env).length, more(), textOf(first(pop, 'gpcount'))], [6, ['and 6 more ' + LOCKED_ADJ + ' rooms'], '1 open, 12 ' + LOCKED]);
    same('... the line is no card, no button and no link', find(pop, 'gmore').map((li) => [li.tagName, li.findAll((e) => e.tagName === 'button' || e.tagName === 'a').length].join()), ['li,0']);
    site.body = listOf(many(7));
    await wait(env, 6000);
    same('7: the line says "room" for one', [lockedCards(env).length, more()], [6, ['and 1 more ' + LOCKED_ADJ + ' room']]);
    site.body = listOf(many(6));
    await wait(env, 6000);
    same('6: all six and no line', [lockedCards(env).length, more()], [6, []]);
    site.body = listOf(many(50));
    await wait(env, 6000);
    same('50 (the most that is sent): 44+ more, and "50+" in the heading', [lockedCards(env).length, more(), textOf(first(pop, 'gpcount'))], [6, ['and 44+ more ' + LOCKED_ADJ + ' rooms'], 'none open, 50+ ' + LOCKED]);
}
{   // the words of the list: the tag, the head, the line of the rest and the sentence of a screen reader call the setting Invite only (the maintainer's word), and none of them says "private" or "link only" (web_home_games_room_check.js holds the same rule for the Room)
    const site = fakeSite();
    site.body = listOf([row('k7m2xq', 'Pebble', { map: 'ISLANDS.LVL', people: 3, free: 1 })].concat(Array.from({ length: 8 }, (_, i) => locked('H' + i))));
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    const words = textOf(popOf(env));
    same('the tag of a locked card is Invite only', TEXT.privateTag, 'Invite only');
    check('the list, with its locked cards, the head and the line of the rest, says it that way and never says "private" or "link only"', words.indexOf(TEXT.privateTag) !== -1 && find(popOf(env), 'gmore').length === 1 && !/link only|\bprivate\b/i.test(words), words.slice(0, 160));
}
{   // a list that is off shows no locked card, and a failed look keeps none
    const site = fakeSite();
    site.body = listOf([locked('Maple')]);
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    check('one locked card to begin with', lockedCards(env).length === 1);
    site.status = 503;
    await wait(env, 6000);
    same('the list is off: no card, no empty card, "Games list is off right now"', [lockedCards(env).length, slotsOf(env).length, first(popOf(env), 'goff').hidden, first(popOf(env), 'glist').hidden], [0, 0, false, true]);
}
{   // the page's own private room: first among the locked cards, tagged Yours (a locked row has no code, so the page knows it by its host, map, mode and seats: a look-alike that differs in ANY ONE of them is not it)
    const site = fakeSite();
    const env = await page({ site });
    myRoom(env, ['Maya']);
    const own = { map: 'TREASURE.LVL', people: 1, bots: 0, free: 3, size: 4 };
    const alike = [['Mara', {}], ['Maya', { map: 'TINY.LVL' }], ['Maya', { mode: '187' }], ['Maya', { people: 2, free: 2 }], ['Maya', { bots: 1, free: 2 }], ['Maya', { free: 2, size: 3 }]];       // (each is Maya's room but for one thing)
    const body = (looks) => listOf([row('abc234', 'Clover')].concat(looks.map(([h, o]) => locked(h, Object.assign({}, own, o))), [locked('Maya', own)]));
    site.body = body(alike.slice(0, 5));
    env.toggleRoom();
    await flush();
    await wait(env, 30000);
    tap(env.home('games'));
    await flush();
    const tags = () => lockedCards(env).map((li) => [textOf(first(li, 'gname')), textOf(first(li, 'gmapname')), textOf(first(li, 'gcount')), find(li, 'gmode').length ? '187' : '', find(li, 'gyours').length ? 'Yours' : ''].join('|'));
    same('the page\'s own private room is the first locked card, tagged Yours; the rooms that differ in one thing each (the host, the map, the mode, the people, the bots) stay plain, in their order', tags(),
        ['Maya|Treasure|1/4||Yours', 'Mara|Treasure|1/4||', 'Maya|Tiny|1/4||', 'Maya|Treasure|1/4|187|', 'Maya|Treasure|2/4||', 'Maya|Treasure|2/4||']);
    same('... the tags stand in the order invite only, Yours (the room\'s own tag is the style of the public Yours)', find(lockedCards(env)[0], 'gmarks')[0].children.map(textOf), [TEXT.privateTag, TEXT.yours]);
    check('... it has no button, no link, no press either (it is the page\'s own room: nothing to join), and its sentence says that it is yours', lockedCards(env)[0].findAll((e) => e.tagName === 'button' || e.tagName === 'a').length === 0 && textOf(first(lockedCards(env)[0], 'sr')).indexOf(TEXT.privateRoom + ', cannot join. Yours. ') === 0);
    same('the heading counts it with the others: 1 open and 6 private (the page\'s own room is one of them), and the button counts the joinable rooms alone', [textOf(first(popOf(env), 'gpcount')), pillOf(env).textContent], ['1 open, 6 ' + LOCKED, '1']);
    site.body = body(alike.slice(5));
    await wait(env, 6000);
    same('the room that differs in the colors of the match (three, not four) stays plain too', tags(), ['Maya|Treasure|1/4||Yours', 'Maya|Treasure|1/3||']);
    site.body = listOf([locked('Maya', Object.assign({}, own, { people: 2, free: 2 })), locked('Quill', { people: 3, free: 1 })]);
    await wait(env, 6000);
    same('a stale answer (the room has one person, the row says two) tags nothing: plain until the next look says otherwise', tags(), ['Maya|Treasure|2/4||', 'Quill|Treasure|3/4||']);
    site.body = listOf([locked('Maya', own), locked('Maya', own)]);
    await wait(env, 6000);
    same('two rooms that look just alike: one is tagged (the first), not both', tags(), ['Maya|Treasure|1/4||Yours', 'Maya|Treasure|1/4||']);
}
{   // a room of the page's own with one person, a computer player, a color that is Nobody's and a free color, in mode 187: the seats and the mode are read from the Room message as the server counts them (a color that is Nobody's is no color of the match)
    const site = fakeSite();
    const env = await page({ site });
    env.makeRoom();
    env.arrive({ key: KEY, seats: [[C, 'Maya'], [E, ''], [E, ''], [E, '']], room: { flags: PRIVATE, plan: [0, 0, 2, 4], mode: 1, map: 'ISLANDS.LVL' } });
    const base = { map: 'ISLANDS.LVL', mode: '187', people: 1, bots: 1, free: 1, size: 3 };
    site.body = listOf([locked('Maya', Object.assign({}, base, { mode: 'highest-score' })), locked('Maya', Object.assign({}, base, { people: 2, size: 4 })), locked('Maya', Object.assign({}, base, { bots: 0, size: 2 })), locked('Maya', Object.assign({}, base, { free: 2, size: 4 })), locked('Maya', base)]);
    env.toggleRoom();
    await flush();
    await wait(env, 30000);
    tap(env.home('games'));
    await flush();
    same('one person, a medium computer player, a free color and a color that is Nobody\'s, mode 187: the row that says exactly that is tagged and moves to the front; each row that differs in the mode, the people, the bots or the free colors stays plain',
        lockedCards(env).map((li) => [textOf(first(li, 'gcount')), find(li, 'gmode').length ? '187' : '', find(li, 'gyours').length ? 'Yours' : ''].join('|')), ['2/3|187|Yours', '2/3||', '3/4|187|', '1/2|187|', '2/4|187|']);
}
{   // a guest in somebody else's private room: the room is the guest's own too (the host's name, the map, the seats)
    const site = fakeSite();
    const env = await page({ site, search: '?room=k7m2xq', options: { stayHome: false } });
    guestRoom(env, 'Hana');
    site.body = listOf([locked('Hana', { map: 'TREASURE.LVL', people: 2, free: 2 }), locked('Maya', { map: 'TREASURE.LVL', people: 2, free: 2 })]);
    env.toggleRoom();
    await flush();
    await wait(env, 30000);
    tap(env.home('games'));
    await flush();
    same('a guest in Hana\'s private room: Hana\'s card is tagged Yours (and goes first), a card of the same seats that another person hosts is not', lockedCards(env).map((li) => textOf(first(li, 'gname')) + (find(li, 'gyours').length ? '+Yours' : '')), ['Hana+Yours', 'Maya']);
}
{   // a public room of the page's own is a row by its code, and no locked card is tagged for it
    const site = fakeSite();
    const env = await page({ site });
    const sock = myRoom(env, ['Maya'], PUBLIC);
    site.body = listOf([row(helloRoom(sock.sent[0]), 'Maya', { people: 1, free: 3 }), locked('Maya', { map: 'TREASURE.LVL', people: 1, free: 3 })]);
    env.toggleRoom();
    await flush();
    await wait(env, 30000);
    tap(env.home('games'));
    await flush();
    same('the own public room is the Yours row of the open rooms, and a private room that looks like it is another room\'s card (not tagged)', [rowsOf(env).map((li) => textOf(first(li, 'gname')) + (find(li, 'gyours').length ? '+Yours' : '')), textOf(first(popOf(env), 'gpcount'))], [['Maya+Yours', 'Maya'], '1 open, 1 ' + LOCKED]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 12. The empty cards ("lobby slots"): the list is padded with dashed cards up to two rows of its grid (one row while matches run), real rooms take the first places
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const OPEN = (n) => Array.from({ length: n }, (_, i) => row('r' + String(i).padStart(5, '0'), 'Host' + i, { age: 100 - i }));
    const kinds = (env) => first(popOf(env), 'glist').children.map((li) => classes(li).indexOf('gslot') !== -1 ? 'slot' : classes(li).indexOf('gpriv') !== -1 ? 'locked' : 'room');
    const count = (list, kind) => list.filter((k) => k === kind).length;
    // a docked list in a window of `width` px (the fake has no layout, so the style's own rule says the columns: up to 700 px one, under 1250 two, else three), with `rooms` open rooms and `live` matches in progress
    async function docked(width, rooms, live, extra) {
        const site = fakeSite();
        site.body = listOf(OPEN(rooms).concat(extra || []));
        if (live) { site.live.reject = false; site.live.body = { live: Array.from({ length: live }, (_, i) => ({ id: 'TINY-20261119-12000' + i + 'Z', map: 'TINY.LVL', mode: 'highest-score', started: 1, turns: 400, seconds: 20, players: ['Green (Ann)', 'Red (Bob)'] })), count: live }; }
        const env = await page({ site, options: { popover: false, innerWidth: width } });
        await wait(env, 6000);
        env.site = site;
        return env;
    }
    same('a list that is on, with no room at all: the empty cards fill two rows of the grid: 4 at 1000 px (two columns), 6 at 1366 px (three), 3 on a phone (one column)',
        [count(kinds(await docked(1000, 0, 0)), 'slot'), count(kinds(await docked(1366, 0, 0)), 'slot'), count(kinds(await docked(390, 0, 0)), 'slot')], [4, 6, 3]);
    same('real rooms take the first places and the empty cards fill what is left: two rooms at 1000 px leave 2, at 1366 px 4, on a phone 1', [count(kinds(await docked(1000, 2, 0)), 'slot'), count(kinds(await docked(1366, 2, 0)), 'slot'), count(kinds(await docked(390, 2, 0)), 'slot')], [2, 4, 1]);
    same('the rooms never leave fewer than none: as many rooms as places, or more (they are never capped or hidden: the list goes on as it did), leave no empty card', [kinds(await docked(1000, 4, 0)).join(), count(kinds(await docked(1000, 9, 0)), 'slot'), count(kinds(await docked(1000, 9, 0)), 'room'), count(kinds(await docked(390, 3, 0)), 'slot')],
        ['room,room,room,room', 0, 9, 0]);
    same('while matches are in progress the empty cards fill one row only: 2 at 1000 px, 3 at 1366 px, 2 on a phone (with no room)', [count(kinds(await docked(1000, 0, 2)), 'slot'), count(kinds(await docked(1366, 0, 2)), 'slot'), count(kinds(await docked(390, 0, 2)), 'slot')], [2, 3, 2]);
    same('... a room takes one of them, and rooms that fill the row leave none', [count(kinds(await docked(1000, 1, 2)), 'slot'), count(kinds(await docked(1366, 2, 1)), 'slot'), count(kinds(await docked(390, 2, 1)), 'slot'), count(kinds(await docked(1000, 2, 1)), 'slot')], [1, 1, 0, 0]);
    {   // the order: the rooms first (as they were), then the empty cards, then the locked cards (which take no place and are no card to press), then the matches in their own box
        const env = await docked(1000, 1, 1, [locked('Maple'), locked('Quill', { map: 'SMALL.LVL' })]);
        same('the rooms, the empty cards, the locked cards; the match box follows the list', [kinds(env).join(), first(popOf(env), 'gpbody').children.indexOf(first(popOf(env), 'glist')) < first(popOf(env), 'gpbody').children.indexOf(first(popOf(env), 'glivebox'))], ['room,slot,locked,locked', true]);
        same('the heading and the count beside Games say the rooms and the matches only, never the empty cards', [textOf(first(popOf(env), 'gpcount')), pillOf(env).textContent], ['1 open, 2 invite only, 1 in progress', '2']);
    }
    {   // a card: no name, no count, no filled dot (nobody waits), one button; hidden from a screen reader but for the button, which says what it does
        const env = await docked(1000, 0, 0);
        const li = slotsOf(env)[0];
        same('an empty card says "Open lobby" and "Empty", has four open seats and no filled one, and the one button Host here, whose name says what it does', [textOf(first(li, 'gname')), textOf(first(li, 'gsay')), find(li, 'gdot').map((d) => d.className.replace(/ c\d$/, '')), find(li, 'btn').map(textOf), joinOf(li).getAttribute('aria-label'), first(li, 'gdots').getAttribute('aria-hidden'), first(li, 'gmap').getAttribute('aria-hidden')],
            [TEXT.slotName, TEXT.slotSay, ['gdot open', 'gdot open', 'gdot open', 'gdot open'], [TEXT.slotHost], TEXT.slotAria, 'true', 'true']);
        check('... it has no code, no count of seats and no name of a host (it is not a room)', joinOf(li).getAttribute('data-code') === null && find(li, 'gcount').length === 0 && find(li, 'gname').length === 1);
        check('the cards that are empty are not counted anywhere: "none waiting", and the count beside Games is 0', textOf(first(popOf(env), 'gpcount')) === 'none waiting' && pillOf(env).textContent === '0');
    }
    {   // a press asks first (Quick host's question), changes nothing until it is answered, and Escape gives the focus back to the card
        const env = await docked(1000, 0, 0);
        tap(joinOf(slotsOf(env)[1]));
        const asked = hostCard(env);
        same('a press on Host here asks "Host a game": Public room, Invite only, Cancel; no room is made yet', [!!asked, asked ? find(asked, 'btn').map(textOf) : [], env.sockets.length], [true, [TEXT.hostPublic, TEXT.hostPrivate, TEXT.cancel], 0]);
        pressEscape(env);
        check('... Escape closes it, nothing is made, and the focus is on the card that was pressed', hostCard(env) === null && env.sockets.length === 0 && env.focused === joinOf(slotsOf(env)[1]));
        tap(joinOf(slotsOf(env)[0]));
        check('... Public room makes the room (the Room shows), as Quick host does', chooseHost(env, TEXT.hostPublic) && env.sockets.length === 1 && env.screen() === 'room');
    }
    {   // while the page holds a room an empty card does what Quick host does then: it shows the Room, and asks and opens nothing new
        const env = await docked(1000, 0, 0);
        myRoom(env, ['Maya'], PRIVATE);
        env.toggleRoom();
        await flush();
        const before = env.sockets.length;
        tap(joinOf(slotsOf(env)[0]));
        check('a press while the page holds a room shows the Room and asks nothing (no second room, no question)', env.screen() === 'room' && hostCard(env) === null && env.sockets.length === before);
    }
    {   // the page's own public room (the row tagged Yours) is a room of the list and takes a place like any other
        const site = fakeSite();
        const env = await page({ site, options: { popover: false, innerWidth: 1000 } });
        const sock = myRoom(env, ['Maya'], PUBLIC);
        site.body = listOf([row(helloRoom(sock.sent[0]), 'Maya', { people: 1, free: 3 })]);
        env.toggleRoom();
        await flush();
        await wait(env, 30000);
        same('the page\'s own public room (Yours) takes a place of the empty cards: it and three empty cards', [kinds(env).join(), find(popOf(env), 'gyours').length], ['room,slot,slot,slot', 1]);
    }
    {   // a room appears: it takes a place; a match starts: the room goes from the list and the empty cards fall to one row
        const env = await docked(1000, 0, 0);
        const site = env.site;
        check('(four empty cards to begin with)', count(kinds(env), 'slot') === 4);
        site.body = listOf(OPEN(1));
        await wait(env, 6000);
        same('a room is listed: it is first and one card fewer is empty', [kinds(env).join()], ['room,slot,slot,slot']);
        site.body = listOf([]);
        site.live.reject = false; site.live.body = { live: [{ id: 'TINY-20261119-120000Z', map: 'TINY.LVL', mode: 'highest-score', started: 1, turns: 40, seconds: 2, players: ['Green (Ann)', 'Red (Bob)'] }], count: 1 };
        await wait(env, 70000);       // (the door of the matches had failed before: its looks were slowed down, and come back at their pace after the first good answer)
        same('the room has started its match: it is in the box of matches, and the empty cards fill one row now', [kinds(env).join(), find(popOf(env), 'glive').length], ['slot,slot', 1]);
        site.live.body = { live: [], count: 0 };
        await wait(env, 12000);
        check('the match is over: two rows of empty cards again', count(kinds(env), 'slot') === 4);
    }
    {   // the list is off or has not answered: no empty card (nothing is known of the list); the popover (one card wide) has three
        const site = fakeSite();
        site.status = 503;
        const env = await page({ site, options: { popover: false } });
        await wait(env, 6000);
        check('a list that is off shows no empty card', slotsOf(env).length === 0 && first(popOf(env), 'goff').hidden === false);
        site.status = 200;
        await wait(env, 70000);
        check('... and takes them when it is on again', slotsOf(env).length > 0 && first(popOf(env), 'goff').hidden === true);
        const pop = await page({ options: { popover: true } });
        tap(pop.home('games'));
        await flush();
        check('the popover (a list that is given no dock) is one card wide: three empty cards', slotsOf(pop).length === 3);
    }
    {   // the grid takes more or fewer columns with no change of the window (the players' column comes in after the list was first painted): the list counts its empty cards again
        const mine = [];
        class Watch { constructor(fn) { this.fn = fn; } observe(el) { if (String(el.className).indexOf('glist') !== -1) mine.push(this); } unobserve() {} disconnect() {} }
        const site = fakeSite();
        site.body = listOf(OPEN(1));
        const env = await page({ site, options: { popover: false, innerWidth: 1000, win: { ResizeObserver: Watch } } });
        await wait(env, 6000);
        check('(two columns: one room and three empty cards; the list is watched for its size)', kinds(env).join() === 'room,slot,slot,slot' && mine.length === 1);
        env.win.innerWidth = 1366;       // (the fake has no layout: the width stands in for the grid's own columns)
        mine[0].fn([]);
        check('a change in the grid\'s size that is not the window\'s counts the empty cards again: three columns hold one room and five empty cards', kinds(env).join() === 'room,slot,slot,slot,slot,slot');
        const before = first(popOf(env), 'glist').children.slice();
        mine[0].fn([]);
        check('... a change that leaves the columns as they are paints nothing again', first(popOf(env), 'glist').children.every((li, i) => li === before[i]) && first(popOf(env), 'glist').children.length === before.length);
    }
    {   // the window changes (a phone turned, a window made wider): the empty cards are painted again for the columns that there are
        const env = await docked(1000, 1, 0);
        check('(two columns: one room and three empty cards)', kinds(env).join() === 'room,slot,slot,slot');
        env.win.innerWidth = 1366;
        (env.win.listeners.resize || []).forEach((fn) => fn());
        check('a wider window has three columns: one room and five empty cards', kinds(env).join() === 'room,slot,slot,slot,slot,slot');
        env.win.innerWidth = 390;
        (env.win.listeners.resize || []).forEach((fn) => fn());
        check('a phone has one column: one room and two empty cards', kinds(env).join() === 'room,slot,slot');
    }
}

done('the games list');
})().catch(broke);
