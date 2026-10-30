// Runs the front page's PUBLIC ROOM and GAMES LIST (web/home/games.js and games.css with web/lobby.html's own script, docs/HOME_SCREEN.md, docs/PUBLIC_ROOMS.md) in the fake browser of tests/scripts/web_page_harness.js,
// against a scripted game server and a fake GET /lobbies:
//   1. the pure parts: a row of the list on a table (every field of the shape, every wrong type and range, markup and look-alike characters in a name), the list (extra fields, duplicates, 10,000 rows, a top level that is
//      no list), the waits (30 s, 5 s, doubled after a failure up to a cap, +-20 %), the count ("50+"), the seats;
//   2. the way in: nothing is lit before the script comes, Quick host, Quick join and Games are lit by it, a script that does not come leaves the page as it was;
//   3. the looks at /lobbies with the fake clock: the first one when the page is up, 30 s, 5 s while the popover is open and at once when it opens, none while the tab is hidden or the Room shows, one at a time,
//      the jitter's bounds, the wait that doubles after every failure and starts again after a good answer, a look that never ends, the list that is off (404, 503, garbage, a network that fails);
//   4. the popover: the rows as text (a name with markup, a map's file name), the seats as dots, the count on the Games button, the page's own room left out, the empty state, "Games list is off right now", Escape and a
//      press elsewhere put it away; a hostile list (markup, look-alike characters, 10,000 rows, wrong types, duplicate codes);
// The Room's switch, Quick host and Join are web_home_games_room_check.js. tests/scripts/test_web_home_games.py runs both with node (the quick tier).
// usage: node web_home_games_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const R = require('./web_home_games_rig.js')(process.argv[2]);
const { check, same, flush, fakeSite, row, listOf, page, wait, classes, find, first, home, tap, pressEscape, pressElsewhere, popOf, rowsOf, joinOf, textOf, pillOf, myRoom, Games, RULES0, done, broke, helloRoom, runLobby } = R;

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
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true, fakeClock: true, lazy: true, fetch: site.fetch });
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
    same('... an original map has its own picture (front/preview_<key>.png), a map of the players has none', rows.map((li) => find(li, 'gmap')[0].children.map((i) => i.src)), [['front/preview_islands.png'], ['front/preview_treasure.png'], [], ['front/preview_medium.png']]);
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
{   // the page's own room is not in its list, and the count follows
    const site = fakeSite();
    const env = await page({ site });
    const sock = myRoom(env, ['Maya']);
    const code = helloRoom(sock.sent[0]);
    site.body = listOf([row(code, 'Maya', {}), row('abc234', 'Clover')]);
    env.roomBar('rhome').click();
    await flush();
    await wait(env, 30000);
    check('the page\'s own room (by its code) is left out of the list and of the count', pillOf(env).textContent === '1' && env.home('games').getAttribute('aria-label') === 'Games, 1 open');
    tap(env.home('games'));
    await flush();
    same('... and of the popover', rowsOf(env).map((li) => textOf(first(li, 'gname'))), ['Clover']);
    check('... the heading says 1 waiting', first(popOf(env), 'gpcount').textContent === '1 waiting · updates live');
    site.body = listOf([row(code, 'Maya', {})]);
    await wait(env, 5000);
    check('a list with nothing but the page\'s own room is empty: "none waiting", the empty state with Host one!, no list, the count 0', first(popOf(env), 'gpcount').textContent === 'none waiting' && first(popOf(env), 'gempty').hidden === false && first(popOf(env), 'glist').hidden === true && pillOf(env).textContent === '0');
    check('... the empty state says it and offers Host one!', textOf(first(popOf(env), 'gempty')).indexOf('No open games right now.') !== -1 && textOf(first(popOf(env), 'gempty')).indexOf('Rooms show up here when their host switches them to Public.') !== -1 && first(first(popOf(env), 'gempty'), 'ghostbtn').textContent === 'Host one!');
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
{   // a hostile list
    const site = fakeSite();
    const hostile = [
        row('aaaaaa', '<img src=x onerror=alert(1)>'), row('a2a2a2', 'Evil map', { map: 'EVIL<script>.LVL' }), row('bbbbbb', '"><svg/onload=1>'), row('cccccc', '\\u0041\\x41&amp;&lt;b&gt;'), row('dddddd', 'Ann\u202eBob'), row('eeee', 'a'.repeat(33)), row('ffffff', 'Zo\u00eb'),
        row('gggggg', 'Ok', { people: '3' }), row('hhhhhh', 'Ok2', { map: '../../etc/passwd.LVL' }), row('aaaaaa', 'Same code'), row('jjjjjj', 'javascript:alert(1)'), row('kkkkkk', 'http://evil.example/x', { map: 'http.LVL' }), row('K7M2XQ', 'Capital'),
    ];
    site.body = listOf(hostile);
    const env = await page({ site });
    tap(env.home('games'));
    await flush();
    const names = rowsOf(env).map((li) => textOf(first(li, 'gname')));
    same('a list with markup in the names shows the names that are of the shape, as the text that they are, and drops the rest (a map that is no file name, look-alike characters, too long, wrong types, a path, a code twice, a capital)', names,
        ['<img src=x onerror=alert(1)>', '"><svg/onload=1>', '\\u0041\\x41&amp;&lt;b&gt;', 'javascript:alert(1)', 'http://evil.example/x']);
    same('... and a map that is a file name is its name without the ending, as text', rowsOf(env).map((li) => textOf(first(li, 'gmapname'))), ['Treasure', 'Treasure', 'Treasure', 'Treasure', 'http']);
    check('... nothing was written as markup, nothing has a link, no script element was made, and the only attributes that carry a field are the code (checked), the text of a label and the map\'s own picture', env.innerHTMLWrites.length === 0
          && popOf(env).findAll((e) => e.tagName === 'a' || e.tagName === 'script' || e.tagName === 'iframe' || e.hasAttribute('href') || e.hasAttribute('onclick') || e.hasAttribute('style') && e !== popOf(env)).length === 0);
    check('... every address that a row makes is a picture of the page\'s own folder', popOf(env).findAll((e) => e.tagName === 'img').every((i) => /^front\/preview_[a-z]+\.png$/.test(i.src)));
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

done('the games list');
})().catch(broke);
