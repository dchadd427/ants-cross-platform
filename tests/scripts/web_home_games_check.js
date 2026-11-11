// Runs the front page's PUBLIC ROOM and GAMES LIST (web/home/games.js and games.css with web/lobby.html's own script, docs/HOME_SCREEN.md, docs/PUBLIC_ROOMS.md) in the fake browser of tests/scripts/web_page_harness.js,
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
// The Room's switch, Quick host and Join are web_home_games_room_check.js. tests/scripts/test_web_home_games.py runs both with node (the quick tier).
// usage: node web_home_games_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
const R = require('./web_home_games_rig.js')(process.argv[2]);
const { GUEST, check, same, flush, fakeSite, row, listOf, page, wait, classes, find, first, home, tap, chooseHost, hostCard, pressEscape, pressElsewhere, popOf, rowsOf, joinOf, textOf, pillOf, myRoom, Games, RULES0, done, broke, helloRoom, runLobby, hex, helloBytes } = R;

// the address of a picture of the library (map_picker.js: the one shape that the page loads for a map of the players): /maps/previews/ and a name of unreserved characters and %XX (never an escaped slash, backslash or NUL), no leading dot
const LIBRARY = /^\/maps\/previews\/(?:[A-Za-z0-9_~-]|%(?!2F|5C|00)[0-9A-F]{2})(?:[A-Za-z0-9._~-]|%(?!2F|5C|00)[0-9A-F]{2})*\.webp$/;

(async () => {

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The pure parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const good = row('k7m2xq', 'Pebble', { map: 'ISLANDS.LVL', people: 3, bots: 0, free: 1 });
    const parsed = Games.rowOf(good, RULES0);
    same('a good row: the code, the host, the map (an original by its name and its key), the seats', parsed, { code: 'k7m2xq', host: 'Pebble', map: 'ISLANDS.LVL', mapKey: 'islands', mapName: 'Islands', people: 3, bots: 0, free: 1, size: 4, age: 10 });
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
    check('a script that ' + what + ' leaves Quick host and Games disabled, asks nothing of /lobbies, and the page as it was (My room makes a room, Home and the Room work)', env.home('host').disabled && env.home('games').disabled && env.site.calls.length === 0);
    env.home('mine').click();
    check('... (My room still makes the room and shows it)', env.sockets.length === 1 && env.screen() === 'room');
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
    env.home('mine').click();
    const onRoom = env.looks();
    await wait(env, 100000);
    check('while the Room shows (the count is not on screen) nothing is asked either', env.screen() === 'room' && env.looks() === onRoom);
    env.roomBar('rhome').click();
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
    check('... and a good answer takes the list back (the empty state, no "off" line)', first(popOf(env), 'goff').hidden === true && first(popOf(env), 'gempty').hidden === false);
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
    check('... the popover says "Games list is off right now" (and that Join by code still works), and shows no list and no empty state', off.hidden === false && off.textContent.indexOf('Games list is off right now') !== -1 && off.textContent.indexOf('Join by code still works.') !== -1
          && find(popOf(env), 'glist')[0].hidden === true && find(popOf(env), 'gempty')[0].hidden === true && find(popOf(env), 'gpcount')[0].textContent === '');
    check('... and Join by code is still there', find(popOf(env), 'gcode')[0].hidden !== true && first(popOf(env), 'joinbox') !== null);
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
    check('... the legend and Join by code are there, and the empty state and the "off" line are not', find(pop, 'glegend')[0].hidden !== true && textOf(find(pop, 'glegend')[0]).indexOf('open seat') !== -1 && first(pop, 'gempty').hidden === true && first(pop, 'goff').hidden === true && first(pop, 'gclabel').textContent === 'Join by code');
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
    env.home('mine').click();
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
    env.roomBar('rhome').click();
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
    check('a list with nothing but the page\'s own room is one row, Yours: "1 waiting", the count 1, no "No open games right now."', first(popOf(env), 'gpcount').textContent === '1 waiting · updates live' && first(popOf(env), 'gempty').hidden === true && first(popOf(env), 'glist').hidden === false && rowsOf(env).length === 1 && textOf(first(rowsOf(env)[0], 'gyours')) === 'Yours' && pillOf(env).textContent === '1');
    site.body = listOf([row(code, 'Maya', {}), row('abc234', 'Clover', {}), row('def567', 'Fern', { free: 3, people: 1 })]);
    await wait(env, 5000);
    same('two other rooms: the own row first, BEST on the best other row only', rowsOf(env).map((li) => textOf(first(li, 'gname')) + (find(li, 'gbest').length ? '+BEST' : '') + (find(li, 'gyours').length ? '+Yours' : '')), ['Maya+Yours', 'Clover+BEST', 'Fern']);
    site.body = listOf([row('abc234', 'Clover')]);
    await wait(env, 5000);
    check('the own room goes private (it is not in the next answer): its row is gone, the count is 1', rowsOf(env).length === 1 && textOf(first(rowsOf(env)[0], 'gname')) === 'Clover' && pillOf(env).textContent === '1');
    site.body = listOf([]);
    await wait(env, 5000);
    check('no rows at all: "none waiting", the empty state with Host one!, the count 0', first(popOf(env), 'gpcount').textContent === 'none waiting' && first(popOf(env), 'gempty').hidden === false && first(popOf(env), 'glist').hidden === true && pillOf(env).textContent === '0');
    check('... the empty state says it and has one button, Host a game (the bar has Quick host and Quick join)', textOf(first(popOf(env), 'gempty')).indexOf('No open games right now.') !== -1 && textOf(first(popOf(env), 'gempty')).indexOf('Rooms show up here when their host switches them to Public.') !== -1 && first(popOf(env), 'gempty').findAll((e) => e.tagName === 'button').map(textOf).join() === 'Host a game');
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
    env.home('mine').click();
    env.roomBar('rhome').click();
    env.home('mine').click();
    env.roomBar('rhome').click();
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
    check('... the body holds nothing else (no stand-in), and Games names no popup and says nothing of being open (it is not a popover)', yard.children.length === 1 && env.home('games').getAttribute('aria-haspopup') === null && env.home('games').getAttribute('aria-expanded') === null);
    same('... the rows are the Games list\'s own, best first (the fewest open seats, then the oldest), BEST on the first, with a Join button each', [rowsOf(env).map((li) => textOf(first(li, 'gname'))), find(popOf(env), 'gbest').length, rowsOf(env).map((li) => !!joinOf(li))], [['Clover', 'Juniper', 'Pebble'], 1, [true, true, true]]);
    same('... the count beside the heading and on the Games button are the same 3', [textOf(first(popOf(env), 'gpcount')).indexOf('3 waiting') === 0, textOf(first(env.home('games'), 'pill'))], [true, '3']);
    check('... and the "Join by code" box is at the bottom of it', !!first(popOf(env), 'gcode') && popOf(env).children[popOf(env).children.length - 1] === first(popOf(env), 'gcode'));
    tap(env.home('games'));
    check('Games takes the focus to the list (the first Join) and the list stays; pressed again, the same; Escape and a press elsewhere do not put it away', env.focused === joinOf(rowsOf(env)[0]) && pop.hidden === false);
    tap(env.home('games')); pressEscape(env); pressElsewhere(env, env.$('home'));
    check('... (still there)', pop.hidden === false && env.looks() >= 1 && popOf(env) === pop);
    await wait(env, 5000); await wait(env, 5000);
    check('the list is looked at every 5 s while Home shows, without any press (the pace of an open list), and the same rows stay (the same nodes)', env.site.times.slice(0, 3).join() === '0,5000,10000' && rowsOf(env).length === 3);
    tap(joinOf(rowsOf(env)[0]));
    check('Join in the docked list is the popover\'s Join: a second client that only joins, with the room of the row', env.sockets.length === 1 && hex(env.last().sent[0] || env.last().open().sent[0]) === hex(helloBytes(GUEST, 'c1v3rr', { join: true })));
    env.home('mine').click();
    const onRoom = env.looks();
    await wait(env, 20000);
    check('while the Room shows nothing is asked (the list is not on screen)', env.screen() === 'room' && env.looks() === onRoom);
}
{
    const env = await page({ options: { popover: false } });     // no rooms: the empty state is in the body too, words only, and Join by code under it
    check('no open games: the empty state ("No open games right now.") and Join by code are in the docked list', textOf(popOf(env)).indexOf('No open games right now.') !== -1 && !first(popOf(env), 'gempty').hidden && !!first(popOf(env), 'gcode') && textOf(first(popOf(env), 'gpcount')) === 'none waiting');
    check('... and the card has the one button, Host a game (the bar above has Quick host and Quick join)', first(popOf(env), 'gempty').findAll((e) => e.tagName === 'button').map(textOf).join() === 'Host a game');
    first(popOf(env), 'gempty').findAll((e) => e.tagName === 'button')[0].click();
    const asked = hostCard(env);
    same('... a press on it does what Quick host does with no room: the card "Host a game" with Public room, Private room and Cancel, nothing made yet', [!!asked, asked ? find(asked, 'btn').map(textOf) : [], env.sockets.length], [true, ['Public room', 'Private room', 'Cancel'], 0]);
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
    same('a picture that the server does not have (a 404: the image says error) is taken away, and the others stay', li.map(srcOf), [['/maps/previews/POPcOrN.webp'], [], ['front/preview_tiny.png']]);
    check('... the square is still there, hidden from a screen reader, and the row is whole (the name, the seats, Join)', li.every((x) => find(x, 'gmap').length === 1 && find(x, 'gmap')[0].getAttribute('aria-hidden') === 'true') && textOf(first(li[1], 'gname')) === 'Hasnt' && !!joinOf(li[1]) && textOf(first(li[1], 'gcount')) === '1/4');
    find(li[2], 'gmap')[0].children[0].fire('error');
    find(li[0], 'gmap')[0].children[0].fire('error');
    check('... an original\'s picture that does not come goes the same way (any picture of the list)', rowsOf(env).every((x) => srcOf(x).length === 0 && find(x, 'gmap').length === 1));
    lost.fire('error');
    check('... and a second word of the same image does no harm (the list is still there)', rowsOf(env).length === 3);

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

done('the games list');
})().catch(broke);
