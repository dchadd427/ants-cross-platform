// Runs the matches IN PROGRESS of the Games list (web/home/games.js, docs/HOME_SCREEN.md "The Games list", docs/REPLAYS.md "Live") in the fake browser of tests/scripts/web_page_harness.js, against a fake GET /lobbies and a fake GET /live:
//   1. the pure parts: a match of the door's answer on a table (the id's shape, the map, the players' words, markup and look-alike characters in a name), the list (50 at the most, an id once, not a list), the waits (10 s at the least);
//   2. the rows: a red Live mark, the map, who plays, how long it has run, Watch = /play.html?live=<id> in a NEW tab, the head "N open, M in progress", the Games button that counts the open rooms only, the empty line;
//   3. the looks at /live: never faster than 10 s apart, none while the list is not shown (a closed popover, a hidden tab, the Room, the switch off, after the hand-off), a server with no live door (404) or a failed look hides the
//      section with no error and the list goes on;
//   4. the switch in the list's head: on by default, a real checkbox with a label, off puts the rows away and stops the looks, the choice is the one thing that the browser keeps (a blocked storage means the default).
// usage: node web_home_games_live_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const R = require('./web_home_games_rig.js')(process.argv[2]);
const { check, same, flush, row, listOf, page, wait, find, first, home, tap, popOf, textOf, myRoom, Games, RULES0, done, broke, roomMessage, seatsOf, STARTING, KEY } = R;

const LIVE = (id, o) => Object.assign({ id, map: 'TREASURE.LVL', started: 1790000000, turns: 1200, seconds: 60, players: ['Green (Ann)', 'Red (Bot (Medium))'] }, o || {});
const SAMPLE = [LIVE('TREASURE-20261109-120000Z', { seconds: 125, players: ['Green (Ann)', 'Red (Bot (Medium))'] }), LIVE('ISLANDS-20261109-115900Z', { map: 'ISLANDS.LVL', seconds: 7, players: ['Blue (Cy)', 'Black'] })];
const answer = (rows) => ({ live: rows, count: rows.length, sim_rules: 9 });
const liveRows = (env) => find(popOf(env), 'glive');
const watchOf = (li) => first(li, 'gwatch');
const boxOf = (env) => first(popOf(env), 'glivebox');
const filterOf = (env) => first(popOf(env), 'gfilter');
const checkboxOf = (env) => filterOf(env).findAll((e) => e.tagName === 'input')[0];
const headOf = (env) => first(popOf(env), 'gpcount').textContent;
// a page with the list shown (docked: Home's own mode) and the door of the matches that run now answering
async function livePage(o) {
    o = o || {};
    const site = R.fakeSite();
    site.live.reject = false; site.live.body = answer(o.rows === undefined ? SAMPLE : o.rows);
    if (o.lobbies) site.body = listOf(o.lobbies);
    return page({ site, stored: o.stored, search: o.search, options: Object.assign({ popover: false }, o.options || {}) });
}
const openRows = (env) => R.rowsOf(env).filter((li) => R.classes(li).indexOf('glive') === -1);
const looksAtLive = (env) => env.site.live.calls.length;

const M = 'TREASURE.LVL';
(async () => {

// ---- 1. the pure parts ----
{
    const good = Games.liveOf(LIVE('TREASURE-20261109-120000Z', { seconds: 125.9, players: ['Green (Ann)', 'Red (Bot (Medium))', 'Blue', 'Black (<b>x</b>)'] }), RULES0);
    same('a good match: the id, the map (an original by its key), the seconds (whole), the players as a color and a name (the name of a bot as the door writes it)', [good.id, good.map, good.mapKey, good.mapName, good.seconds, good.who],
        ['TREASURE-20261109-120000Z', 'TREASURE.LVL', 'treasure', 'Treasure', 125, [{ seat: 0, name: 'Ann' }, { seat: 1, name: 'Bot (Medium)' }, { seat: 2, name: 'Blue' }, { seat: 3, name: '<b>x</b>' }]]);
    same('the seconds come from the turns when they are not there; a number that is not one is 0', [Games.liveOf({ id: 'A-20261109-120000Z', turns: 400 }, RULES0).seconds, Games.liveOf({ id: 'A-20261109-120000Z', seconds: 'x' }, RULES0).seconds, Games.liveOf({ id: 'A-20261109-120000Z', seconds: -4 }, RULES0).seconds, Games.liveOf({ id: 'A-20261109-120000Z', seconds: Infinity }, RULES0).seconds], [20, 0, 0, 0]);
    same('the map is the one in the id when the entry names none; a map of the players has its name and no picture of its own', [Games.liveOf({ id: 'Pop_Corn-20261109-120000Z' }, RULES0).map, Games.liveOf({ id: 'Pop_Corn-20261109-120000Z' }, RULES0).mapKey], ['Pop_Corn.LVL', '']);
    same('at most four players, and one that is no text or has a character that a name has not is dropped', Games.liveOf(LIVE('A-20261109-120000Z', { players: ['Green', 7, 'Red\u202ex', 'Blue', 'Black', 'Green', 'Red'] }), RULES0).who.map((w) => w.name), ['Green', 'Blue', 'Black', 'Green']);
    same('a player that is not a color word keeps its text and takes no color of the four', Games.liveOf(LIVE('A-20261109-120000Z', { players: ['Purple (Sam)'] }), RULES0).who, [{ seat: -1, name: 'Purple (Sam)' }]);
    for (const [what, value] of [['no object', null], ['a list', []], ['a text', 'x'], ['no id', { map: 'A.LVL' }], ['an id that is a number', { id: 5, map: M }], ['an id with a slash', { id: 'A/../B-20261109-120000Z', map: M }], ['an id with no time', { id: 'A-2026', map: M }], ['an id of 40 characters', { id: 'A'.repeat(40) + '-20261109-120000Z', map: M }], ['an id with a stem of 25 characters (the door cuts a stem at 24)', { id: 'A'.repeat(25) + '-20261109-120000Z', map: M }], ['an id with a dot in its stem', { id: 'A.B-20261109-120000Z', map: M }],
                                 ['an id with markup', { id: '<b>-20261109-120000Z', map: M }], ['an id with a query', { id: 'A-20261109-120000Z?x=1', map: M }], ['an id with a second parameter', { id: 'A-20261109-120000Z&a=b', map: M }], ['an id with a fragment', { id: 'A-20261109-120000Z#x', map: M }],
                                 ['an id with five digits after the second', { id: 'A-20261109-120000Z-12345', map: M }], ['an id with a negative count', { id: 'A-20261109-120000Z--1', map: M }], ['an id that starts with javascript:', { id: 'javascript:A-20261109-120000Z', map: M }], ['an id with a blank in front', { id: ' A-20261109-120000Z', map: M }],
                                 ['an id with a line end after it', { id: 'A-20261109-120000Z\n', map: M }], ['a map with markup', { id: 'A-20261109-120000Z', map: '<b>' }], ['a map with a dot', { id: 'A-20261109-120000Z', map: '..' }], ['a map that is a bare name', { id: 'A-20261109-120000Z', map: 'TREASURE' }],
                                 ['a map with a path', { id: 'A-20261109-120000Z', map: '../TREASURE.LVL' }], ['a map with an accent (a file name is printable ASCII, as in a room\'s row)', { id: 'A-20261109-120000Z', map: 'M\u00e4p.LVL' }], ['a map of 65 characters', { id: 'A-20261109-120000Z', map: 'A'.repeat(61) + '.LVL' }]]) {
        check('a match with ' + what + ' is dropped', Games.liveOf(value, RULES0) === null);
    }
    same('a map that is no text is the one in the id', Games.liveOf({ id: 'Pop_Corn-20261109-120000Z', map: 5 }, RULES0).map, 'Pop_Corn.LVL');
    // the door sends the map as the whole file name (src/ants_server/live_board.cpp: the head's map name; "TINY.LVL" in tests/test_server/test_live_board.cpp): nothing is added to it and it is held to the rule of a room's row
    const named = [['TREASURE.LVL', 'treasure', 'Treasure'], ['DRY LAND.lvl', '', 'DRY LAND'], ['Dry Land.LVL', '', 'Dry Land'], ['A-B.LVL', '', 'A-B'], ['Pop Corn ~test-0a1b2c3d.lvl', '', 'Pop Corn']].map(([file]) => Games.liveOf({ id: 'X-20261109-120000Z', map: file }, RULES0));
    same('a map is the file name as the door sends it: an original by its key, a community map (with a blank, a dash or a test suffix) under its name', named.map((e) => [e.map, e.mapKey, e.mapName]), [['TREASURE.LVL', 'treasure', 'Treasure'], ['DRY LAND.lvl', '', 'DRY LAND'], ['Dry Land.LVL', '', 'Dry Land'], ['A-B.LVL', '', 'A-B'], ['Pop Corn ~test-0a1b2c3d.lvl', '', 'Pop Corn']]);
    check('the very rule of a room\'s row decides which file name is good (a name of 64 characters is, one of 65 is not; no ending is not)', Games.liveOf({ id: 'X-20261109-120000Z', map: 'A'.repeat(60) + '.LVL' }, RULES0) !== null && Games.liveOf({ id: 'X-20261109-120000Z', map: 'A'.repeat(61) + '.LVL' }, RULES0) === null && Games.liveOf({ id: 'X-20261109-120000Z', map: 'TREASURE.TXT' }, RULES0) === null);
    same('a match of a map with a long name has the name cut at 28 characters', Games.liveOf({ id: 'X-20261109-120000Z', map: 'A'.repeat(40) + '.lvl' }, RULES0).mapName, 'A'.repeat(28));
    check('an id with a stem of 24 characters is good (the longest the door makes)', Games.liveOf({ id: 'A'.repeat(24) + '-20261109-120000Z', map: M }, RULES0) !== null);
    same('an id with a count after the second is good', Games.liveOf({ id: 'A-20261109-120000Z-2' }, RULES0).id, 'A-20261109-120000Z-2');
    same('a list: its matches in the order given, a bad one dropped, an id once', Games.parseLive(answer([LIVE('A-20261109-120000Z'), 'x', LIVE('B-20261109-120001Z'), LIVE('A-20261109-120000Z', { map: 'Z' }), { id: 'bad' }]), RULES0).map((e) => e.id), ['A-20261109-120000Z', 'B-20261109-120001Z']);
    const many = Games.parseLive(answer(Array.from({ length: 10000 }, (_, i) => LIVE('M-20261109-' + String(100000 + i) + 'Z'))), RULES0);
    check('10,000 matches are 50 (the first 50, in order)', many.length === 50 && many[0].id === 'M-20261109-100000Z' && many[49].id === 'M-20261109-100049Z');
    const proto = Games.parseLive(answer([LIVE('__proto__-20261109-120000Z'), LIVE('__proto__-20261109-120000Z'), LIVE('constructor-20261109-120000Z')]), RULES0);
    same('an id that is a name of Object is an id once, as any other', proto.map((e) => e.id), ['__proto__-20261109-120000Z', 'constructor-20261109-120000Z']);
    for (const [what, value] of [['null', null], ['a list', []], ['a number', 5], ['no live', { count: 0 }], ['live as an object', { live: {} }], ['live as a text', { live: 'x' }]]) check('an answer that is ' + what + ' is no list (the section is hidden)', Games.parseLive(value, RULES0) === null);
    check('an empty list is a list (nothing is playing)', Games.parseLive({ live: [] }, RULES0).length === 0);
    same('the waits: 10 s doubled for every failure up to 5 minutes, then 0 to +20 % (never less than 10 s)', [Games.liveDelay(0, 0), Games.liveDelay(0, 1), Games.liveDelay(0, 0.5), Games.liveDelay(1, 0), Games.liveDelay(2, 0), Games.liveDelay(5, 0), Games.liveDelay(50, 0), Games.liveDelay(0, -5), Games.liveDelay(0, 9)],
        [10000, 12000, 11000, 20000, 40000, 300000, 300000, 10000, 12000]);
    same('the seconds are at most 359999 (99:59:59), and a number below it is whole', [Games.liveOf({ id: 'A-20261109-120000Z', seconds: 1e9 }, RULES0).seconds, Games.liveOf({ id: 'A-20261109-120000Z', seconds: 359999.9 }, RULES0).seconds, Games.liveOf({ id: 'A-20261109-120000Z', turns: 1e12 }, RULES0).seconds], [359999, 359999, 359999]);
    same('the least wait is the door\'s own pace: 10 s', Games.LIVE_EVERY, 10000);
    same('how long it has run, as a clock', [Games.clockText(0), Games.clockText(7), Games.clockText(60), Games.clockText(125), Games.clockText(3665)], ['0:00', '0:07', '1:00', '2:05', '61:05']);
}

// ---- 2. the rows ----
{
    const env = await livePage({ lobbies: [row('k7m2xq', 'Pebble', { people: 3, free: 1 })] });
    check('the list asked /live once, at the door\'s own address with no query and no cookies', looksAtLive(env) === 1 && env.site.live.calls[0].url === '/live' && env.site.live.calls[0].init.credentials === 'omit' && env.site.live.calls[0].init.cache === 'no-store');
    const rows = liveRows(env);
    same('two rows in progress, in the door\'s order, with the map', rows.map((li) => first(li, 'gname').textContent), ['Treasure', 'Islands']);
    check('each has a red Live mark, the players\' names, how long it has run and a Watch button', rows.every((li) => first(li, 'glive-mark').textContent === 'Live' && watchOf(li).textContent === 'Watch'));
    same('... who plays and for how long', rows.map((li) => [first(li, 'gwho').textContent, first(li, 'glen').textContent]), [['Ann, Bot (Medium)', '2:05 so far'], ['Cy, Black', '0:07 so far']]);
    same('... a dot of each player\'s color: a person is a filled dot, a computer player (the door writes "Bot (Medium)") a ring, as in the open rooms', rows.map((li) => find(li, 'gdot').map((d) => d.className.replace('gdot ', ''))), [['player c0', 'bot c1'], ['player c2', 'player c3']]);
    const w = watchOf(rows[0]);
    same('Watch is a link to the player on that match, in a NEW tab (the seat in a room stays), with no opener', [w.tagName, w.getAttribute('href'), w.getAttribute('target'), w.getAttribute('rel')], ['a', '/play.html?live=TREASURE-20261109-120000Z', '_blank', 'noopener']);
    check('... and its name for a screen reader says the match', /^Watch the match on Treasure, Ann, Bot \(Medium\), 2:05 so far$/.test(w.getAttribute('aria-label')));
    check('a row in progress has no Join (nobody can take a seat of a running match)', rows.every((li) => !li.findAll((e) => e.tagName === 'button').length && !first(li, 'gjoin')));
    check('the heading of the list says how many are open and how many are in progress', headOf(env) === '1 open, 2 in progress', headOf(env));
    check('the Games button counts the open games only (the number is what can be joined)', R.pillOf(env).textContent === '1' && env.home('games').getAttribute('aria-label') === 'Games, 1 open');
    check('the open rooms are the rows of the first list and the matches are under it, in a box with its own heading', find(popOf(env), 'gitem').length === 3 && first(boxOf(env), 'gsub').textContent === 'In progress' && boxOf(env).hidden === false);
    check('a row of an open room still has its Join', first(popOf(env), 'gjoin') !== null && first(popOf(env), 'gjoin').textContent === 'Join');
    check('the picture of a match is the map\'s square (an original\'s own)', first(rows[0], 'gmap').findAll((e) => e.tagName === 'img').length === 1 && /preview_treasure\.png$/.test(first(rows[0], 'gmap').findAll((e) => e.tagName === 'img')[0].src));
    await wait(env, 12500);
    check('a look later the clock of a match is the door\'s (the rows stay, only the time is written again)', looksAtLive(env) === 2 && liveRows(env)[0] === rows[0]);
    env.site.live.body = answer([LIVE('TREASURE-20261109-120000Z', { seconds: 135, players: ['Green (Ann)', 'Red (Bot (Medium))'] })]);
    await wait(env, 10500);
    same('a match that ended is gone from the list and the one that goes on says its new time', liveRows(env).map((li) => [first(li, 'gname').textContent, first(li, 'glen').textContent]), [['Treasure', '2:15 so far']]);
    check('... and the head follows', headOf(env) === '1 open, 1 in progress', headOf(env));
    await wait(env, 31000);
    check('the Games button still counts the open game only, after the list and the matches have both been looked at again', R.pillOf(env).textContent === '1' && env.home('games').getAttribute('aria-label') === 'Games, 1 open');
}
{   // text only: names with markup, a very long name, a map of the players
    const markup = [LIVE('Pop_Corn-20261109-120000Z', { map: 'Pop Corn.lvl', players: ['Green (<img src=x onerror=alert(1)>)', 'Red (' + 'W'.repeat(60) + ')', 'Blue (a "b" & \'c\' <\/script>)'] })];
    const env = await livePage({ rows: markup });
    const li = liveRows(env)[0];
    same('names are text: the markup of a name is shown as it was typed and nothing is made of it', first(li, 'gwho').textContent, '<img src=x onerror=alert(1)>, ' + 'W'.repeat(60) + ', a "b" & \'c\' <\/script>');
    check('... nothing was written as markup, the row holds no element that a name could make, and the one address is the id\'s', env.innerHTMLWrites.length === 0 && li.all().every((e) => ['li', 'span', 'a', 'i', 'img'].indexOf(e.tagName) !== -1) && watchOf(li).getAttribute('href') === '/play.html?live=Pop_Corn-20261109-120000Z');
    check('a map of the players: its name without the ending, and a picture only where the page has the picker\'s rule', first(li, 'gname').textContent === 'Pop Corn' && first(li, 'gmap').findAll((e) => e.tagName === 'img').every((img) => /^\/maps\/previews\/Pop%20Corn\.webp$/.test(img.src.replace(/^.*(?=\/maps)/, ''))));
}
{   // nothing in progress; nothing open
    const env = await livePage({ rows: [], lobbies: [row('k7m2xq', 'Pebble')] });
    check('nothing in progress: the box says so, with the heading "1 open, 0 in progress"', boxOf(env).hidden === false && first(boxOf(env), 'gnolive').hidden === false && first(boxOf(env), 'gnolive').textContent === 'No games in progress.' && headOf(env) === '1 open, 0 in progress', headOf(env));
    const both = await livePage({ rows: [] });
    check('nothing open and nothing in progress: the empty card of the open games, one line "none waiting", and the box says no games in progress', headOf(both) === 'none waiting' && first(popOf(both), 'gempty').hidden === false && first(boxOf(both), 'gnolive').hidden === false);
    const only = await livePage({ lobbies: [] });
    check('nothing open, two in progress: the empty card stays (Host one!) and the rows are under it; the head says "0 open, 2 in progress"', first(popOf(only), 'gempty').hidden === false && liveRows(only).length === 2 && headOf(only) === '0 open, 2 in progress', headOf(only));
}

// ---- 3. the looks at /live ----
{
    const env = await livePage();
    await wait(env, 9500);
    check('the next look is 10 s after the first, not before', looksAtLive(env) === 1);
    await wait(env, 2600);
    await wait(env, 12000); await wait(env, 12000);
    const t = env.site.live.times;
    check('the looks are 10 s apart at the least (and not more than 12 s: +20 %)', t.length >= 4 && t.every((x, i) => i === 0 || (x - t[i - 1] >= 10000 && x - t[i - 1] <= 12500)), JSON.stringify(t));
    env.hide(true);
    const n = looksAtLive(env);
    await wait(env, 120000);
    check('a tab that is out of sight looks at /live not at all', looksAtLive(env) === n);
    env.hide(false);
    await flush();
    check('... and looks again when it is back (and the 10 s have gone)', looksAtLive(env) === n + 1);
}
{   // a popover: no look while it is closed, and every 10 s while it is open
    const site = R.fakeSite(); site.live.reject = false; site.live.body = answer(SAMPLE);
    const env = await page({ site, options: { popover: true } });
    await wait(env, 60000);
    check('the popover that is closed looks at /live not at all', looksAtLive(env) === 0);
    tap(env.home('games'));
    await flush();
    check('... it looks when it opens', looksAtLive(env) === 1);
    await wait(env, 9500);
    check('... and 10 s later, not before', looksAtLive(env) === 1);
    await wait(env, 3000);
    check('... then again', looksAtLive(env) === 2);
    check('the rows are in the popover too', liveRows(env).length === 2);
    tap(env.home('games'));
    await flush();
    const n = looksAtLive(env);
    await wait(env, 90000);
    check('... and the closed popover looks no more', looksAtLive(env) === n);
}
for (const [what, tweak] of [['404', (s) => { s.live.reject = false; s.live.status = 404; s.live.body = { error: 'not found' }; }], ['a server error', (s) => { s.live.reject = false; s.live.status = 503; }], ['a network that fails', (s) => { s.live.reject = true; }],
                             ['an answer that is not JSON', (s) => { s.live.reject = false; s.live.text = '<html>'; }], ['an answer that is no list', (s) => { s.live.reject = false; s.live.body = { nothing: 1 }; }], ['a look that never ends', (s) => { s.live.reject = false; s.live.hang = true; }]]) {
    const site = R.fakeSite(); tweak(site);
    site.body = listOf([row('k7m2xq', 'Pebble')]);
    const env = await page({ site, options: { popover: false } });
    await wait(env, 9000);
    const text = env.dumpAll();
    check('a server with ' + what + ' at /live: the section is hidden, the switch is hidden, and the open rooms go on as they were', boxOf(env).hidden === true && first(popOf(env), 'gfrow').hidden === true && openRows(env).length === 1 && headOf(env) === '1 waiting \u00b7 updates live', headOf(env));
    check('... with no error, no notice and no toast', R.toastTextOf(env) === '' && first(popOf(env), 'gpnote').hidden === true);
    const times = site.live.times.slice();
    await wait(env, 100000);
    check('... and the looks go on slowly (doubled after every failure, up to 5 minutes), never faster than 10 s', site.live.times.length <= times.length + 3 && site.live.times.every((x, i) => i === 0 || x - site.live.times[i - 1] >= 10000), JSON.stringify(site.live.times));
    check('... the Games button still counts the open game', R.pillOf(env).textContent === '1');
}
{   // a door that answers again after a failure
    const site = R.fakeSite(); site.live.reject = true;
    const env = await page({ site, options: { popover: false } });
    check('(no live door at first: hidden)', boxOf(env).hidden === true);
    site.live.reject = false; site.live.body = answer(SAMPLE);
    await wait(env, 25000);
    check('a door that answers later brings the rows (the switch with them)', liveRows(env).length === 2 && boxOf(env).hidden === false && first(popOf(env), 'gfrow').hidden === false);
    await wait(env, 13000);
    const t = site.live.times;
    check('a good answer ends the slow pace: the next look is 10 to 12 s after it, not 20 s or more', t.length === 3 && t[2] - t[1] >= 10000 && t[2] - t[1] <= 12500, JSON.stringify(t));
}
{   // the hand-off: START reaches the page; no more looks
    const env = await livePage({ search: '?room=k7m2xq', options: { stayHome: false } });
    const sock = R.guestRoom(env, 'Pebble', R.PUBLIC);
    env.roomBar('rhome').click();
    await wait(env, 12500);
    check('(Home shows with the list, and it looks at /live)', env.screen() === 'home' && looksAtLive(env) >= 1);
    sock.receive(roomMessage(seatsOf(['Pebble', 'Maya']), { you: 1, leader: 0, flags: STARTING }));
    const n = looksAtLive(env), inert = popOf(env).hasAttribute('inert');
    await wait(env, 60000);
    check('START reaches the page: the list (and its Watch buttons) takes no press, and nothing looks at /live after it, not even when the hand-off does not come', env.assigned.length === 1 && inert && looksAtLive(env) === n, [env.assigned.length, inert, looksAtLive(env), n].join());
}
{   // the Room shows: no looks; Home again: a look
    const env = await livePage();
    await wait(env, 1000);
    myRoom(env, ['Maya'], R.PRIVATE);
    const n = looksAtLive(env);
    check('(the Room shows)', env.screen() === 'room');
    await wait(env, 60000);
    check('nothing looks at /live while the Room shows', looksAtLive(env) === n);
    env.roomBar('rhome').click();
    await flush();
    check('Home comes back: a look at once (the 10 s have gone)', env.screen() === 'home' && looksAtLive(env) === n + 1);
}

{   // the 10 s hold across the page's own comings and goings: the Room and back, a tab that hides and shows
    const env = await livePage();
    await wait(env, 3000);
    myRoom(env, ['Maya'], R.PRIVATE);
    await wait(env, 3000);
    env.roomBar('rhome').click();
    await flush();
    check('Home is back 6 s after the look: no look yet (10 s must have gone since the last one)', env.screen() === 'home' && looksAtLive(env) === 1, looksAtLive(env));
    await wait(env, 2000);
    env.hide(true); env.hide(false);
    await flush();
    check('... nor for a tab that hides and shows at 8 s', looksAtLive(env) === 1, looksAtLive(env));
    await wait(env, 3500);
    check('... the look comes when the 10 s have gone (once, not once for each coming back)', looksAtLive(env) === 2, looksAtLive(env));
}
{   // one look at a time: a look that is not over is not asked a second time, whatever comes back to Home meanwhile
    const site = R.fakeSite(); site.live.reject = false; site.live.hang = true;
    const env = await page({ site, options: { popover: false } });
    await wait(env, 1000);
    env.hide(true); env.hide(false);
    await flush();
    env.hide(true); env.hide(false);
    await flush();
    check('a look that has not ended is the only one: a tab that shows again twice asks nothing more', looksAtLive(env) === 1, looksAtLive(env));
    await wait(env, 8500);
    check('... and the one that never ends is a failed look after 8 s, with no row and no error', boxOf(env).hidden === true && R.toastTextOf(env) === '');
}
{   // a door that fails holds its own backoff across the page's comings and goings (not only while the page sits still)
    const site = R.fakeSite(); site.live.reject = false; site.live.status = 404; site.live.body = { error: 'not found' };
    const env = await page({ site, options: { popover: false } });
    check('(the first look is the 404)', looksAtLive(env) === 1);
    await wait(env, 3000);
    myRoom(env, ['Maya'], R.PRIVATE);
    await wait(env, 12000);
    env.roomBar('rhome').click();
    await flush();
    check('Home is back 15 s after a failed look: no look (a failure waits 20 s, not 10)', env.screen() === 'home' && looksAtLive(env) === 1, looksAtLive(env));
    env.hide(true); env.hide(false);
    await flush();
    check('... nor for a tab that shows again', looksAtLive(env) === 1, looksAtLive(env));
    await wait(env, 6000);
    check('... it comes when the 20 s have gone', looksAtLive(env) === 2, looksAtLive(env));
    await wait(env, 25000);
    check('... and a second failure waits 40 s: nothing 25 s after it', looksAtLive(env) === 2, looksAtLive(env));
}
{   // the 10 s are counted from the END of a look: a look that takes 7 s holds the next one back until 10 s after its end
    const site = R.fakeSite(); site.live.reject = false; site.live.defer = true; site.live.body = answer(SAMPLE);
    const env = await page({ site, options: { popover: false } });
    await wait(env, 7000);
    check('(a look is out for 7 s)', looksAtLive(env) === 1 && site.live.waiting.length === 1);
    site.live.waiting[0]();
    await flush();
    await wait(env, 3000);
    check('(it ended 3 s ago: 10 s after its start)', liveRows(env).length === 2 && looksAtLive(env) === 1, looksAtLive(env));
    env.hide(true); env.hide(false);
    await flush();
    check('a tab that shows again 10 s after the START of a look and 3 s after its END asks nothing (the 10 s run from the end)', looksAtLive(env) === 1, looksAtLive(env));
    await wait(env, 8500);
    check('... the next look comes 10 to 12 s after the end', looksAtLive(env) === 2, looksAtLive(env));
}
{   // an answer that comes after the switch was turned off is not taken
    const site = R.fakeSite(); site.live.reject = false; site.live.defer = true; site.live.body = answer([LIVE('STALE-20261109-120000Z')]);
    const env = await page({ site, options: { popover: false } });
    await wait(env, 2000);
    check('(a look is out)', looksAtLive(env) === 1 && site.live.waiting.length === 1);
    const box = checkboxOf(env);
    box.checked = false; (box.listeners.change || []).forEach((fn) => fn({ target: box }));
    await flush();
    site.live.waiting[0]();
    await flush();
    box.checked = true; (box.listeners.change || []).forEach((fn) => fn({ target: box }));
    await flush();
    check('the answer of a look that was out when the switch went off is thrown away: no row of it when the switch is on again, and a new look is asked at once', liveRows(env).length === 0 && looksAtLive(env) === 2, [liveRows(env).length, looksAtLive(env)].join());
}
{   // the focus on a Watch stays when the rows are made again
    const env = await livePage();
    const first0 = watchOf(liveRows(env)[0]), id0 = first0.getAttribute('data-live');
    first0.focus();
    env.site.live.body = answer([LIVE('NEWER-20261109-120100Z', { map: 'MEDIUM.LVL' })].concat(SAMPLE));
    await wait(env, 12500);
    const rows = liveRows(env), again = rows.map(watchOf).find((a) => a.getAttribute('data-live') === id0);
    check('a match comes (the rows are made again): the focus is on the same Watch as before', rows.length === 3 && !!again && again !== first0 && env.doc.activeElement === again, rows.length);
    env.doc.activeElement = null;
    env.site.live.body = answer([LIVE('NEWER-20261109-120100Z', { map: 'MEDIUM.LVL' })].concat(SAMPLE.slice(0, 1)));
    await wait(env, 12500);
    check('... and a focus that was elsewhere is not taken', liveRows(env).length === 2 && env.doc.activeElement === null);
}
{   // the dots of the players: a ring for a computer player, a neutral dot for a color that is none of the four
    const env = await livePage({ rows: [LIVE('TREASURE-20261109-120000Z', { players: ['Green (Ann)', 'Red (Bot (Hard))', 'Purple (Sam)', 'Black (Bot (Easy))'] })] });
    same('a person is a filled dot of the color, a bot a ring of the color, an unknown color a neutral dot (never the Black one)', find(liveRows(env)[0], 'gdot').map((d) => d.className.replace('gdot ', '')), ['player c0', 'bot c1', 'player cn', 'bot c3']);
    const modern = await livePage({ rows: [LIVE('TREASURE-20261109-120000Z', { players: ['Green (Ann)', 'Red (Pip (bot, Hard))', 'Blue (Kit (bot, idle))', 'Black (Abbott)'] }), LIVE('ISLANDS-20261109-115900Z', { map: 'ISLANDS.LVL', players: ['Green (Robot)', 'Red (Pip (bot))', 'Blue (O\'Neil (x))', 'Black (Bot (Medium))'] })] });
    same('a server names its computer players "Pip (bot, Hard)" or "Kit (bot, idle)" (a ring), and a person whose name only has bot in it (Abbott, Robot) or a bracket (O\'Neil (x)) stays a filled dot', liveRows(modern).map((li) => find(li, 'gdot').map((d) => d.className.replace('gdot ', ''))), [['player c0', 'bot c1', 'bot c2', 'player c3'], ['player c0', 'bot c1', 'player c2', 'bot c3']]);
}

// ---- 4. the switch ----
{
    const env = await livePage({ lobbies: [row('k7m2xq', 'Pebble')] });
    const box = checkboxOf(env);
    same('the switch is a checkbox with its label, "Show games in progress", on', [box.type, box.checked, filterOf(env).tagName, textOf(filterOf(env))], ['checkbox', true, 'label', 'Show games in progress']);
    check('... it is a native control (the keyboard and a screen reader reach it) and in the list\'s head, above the rows', box.tagName === 'input' && find(popOf(env), 'gfrow').length === 1 && !box.getAttribute('tabindex'));
    check('the browser holds nothing yet (the default is not kept)', env.storage.getItem('ants.home.games.live') === null);
    box.checked = false; (box.listeners.change || []).forEach((fn) => fn({ target: box }));
    await flush();
    check('off: the rows and their box are gone, the head is as it was without them, the "no games in progress" line is not shown', boxOf(env).hidden === true && liveRows(env).every((li) => li.hidden === true || boxOf(env).hidden) && headOf(env) === '1 waiting \u00b7 updates live', headOf(env));
    check('... the choice is kept in the browser (that word only), and the Games button has not changed', env.storage.getItem('ants.home.games.live') === '0' && R.pillOf(env).textContent === '1');
    const n = looksAtLive(env);
    await wait(env, 90000);
    check('... no look at /live while it is off', looksAtLive(env) === n);
    box.checked = true; (box.listeners.change || []).forEach((fn) => fn({ target: box }));
    await flush();
    check('on again: it looks at once (the 10 s have gone) and the rows come back, and the choice is forgotten (the default)', looksAtLive(env) === n + 1 && liveRows(env).length === 2 && env.storage.getItem('ants.home.games.live') === null && headOf(env) === '1 open, 2 in progress', headOf(env));
}
{   // a choice kept by an earlier visit: off, and /live is never asked
    const env = await livePage({ stored: { 'ants.name': 'Maya', 'ants.home.games.live': '0' } });
    check('a browser that kept "off": the box is not there, the switch is off, and /live is not asked at all', checkboxOf(env).checked === false && boxOf(env).hidden === true && looksAtLive(env) === 0);
    await wait(env, 60000);
    check('... for as long as the page is open', looksAtLive(env) === 0);
    const other = await livePage({ stored: { 'ants.name': 'Maya', 'ants.home.games.live': 'yes' } });
    check('any other word than "0" is the default: on', checkboxOf(other).checked === true && liveRows(other).length === 2);
}
{   // a browser that keeps nothing: the default, and a press does not throw
    const env = await livePage({ stored: R.H.THROWS });
    check('a browser that refuses its storage: the switch is on and the rows are there', checkboxOf(env).checked === true && liveRows(env).length === 2);
    const box = checkboxOf(env);
    box.checked = false; (box.listeners.change || []).forEach((fn) => fn({ target: box }));
    await flush();
    check('... a press on it still works for as long as the page is open (and nothing threw)', boxOf(env).hidden === true && looksAtLive(env) === 1);
}

done('matches in progress');
})().catch(broke);
