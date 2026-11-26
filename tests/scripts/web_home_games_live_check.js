// Runs the matches IN PROGRESS of the Games list (web/home/games.js, docs/HOME_GAMES.md "The Games list", docs/REPLAYS.md "Live") in the fake browser of tests/scripts/web_page_harness.js, against a fake GET /lobbies and a fake GET /live:
//   1. the pure parts: a match of the door's answer on a table (the id's shape, the map, the players' words, markup and look-alike characters in a name), the list (50 at the most, an id once, not a list), the waits (5 s at the least);
//   2. the rows: a red Live mark, the map, who plays, how long it has run, Watch = /play?live=<id> (the same tab; a NEW one only while the page holds a room), the head "N open, M in progress", the Games button that counts the open rooms only, the empty line;
//   3. the looks at /live: never faster than 5 s apart (1.5 s when a room has gone from the list), none while the list is not shown (a closed popover, a hidden tab, the Room, after the hand-off), a server with no live door (404) or a failed look hides the
//      section with no error and the list goes on;
//   4. no switch and nothing kept: the rows always show, an old stored word is ignored and left as it was, a blocked storage changes nothing.
// usage: node web_home_games_live_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const R = require('./web_home_games_rig.js')(process.argv[2]);
const { check, same, flush, row, listOf, page, wait, find, first, home, tap, popOf, rowsOf, slotsOf, classes, textOf, myRoom, Games, RULES0, done, broke, roomMessage, seatsOf, STARTING, KEY } = R;

const LIVE = (id, o) => Object.assign({ id, map: 'TREASURE.LVL', started: 1790000000, turns: 1200, seconds: 60, players: ['Green (Ann)', 'Red (Bot (Medium))'] }, o || {});
const SAMPLE = [LIVE('TREASURE-20261109-120000Z', { seconds: 125, players: ['Green (Ann)', 'Red (Bot (Medium))'] }), LIVE('ISLANDS-20261109-115900Z', { map: 'ISLANDS.LVL', seconds: 7, players: ['Blue (Cy)', 'Black'] })];
const answer = (rows) => ({ live: rows, count: rows.length, sim_rules: 9 });
const liveRows = (env) => find(popOf(env), 'glive');
const watchOf = (li) => first(li, 'gwatch');
const boxOf = (env) => first(popOf(env), 'glivebox');
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
    same('the mode: only the word 187 marks a match of mode 187; highest-score, no word, another word, a number, a list, markup and a word with a blank are a plain match',
        ['187', 'highest-score', undefined, null, '', ' 187', '187 ', '1870', 187, ['187'], '<b>187</b>', 'Highest-Score', true].map((m) => Games.liveOf(LIVE('A-20261109-120000Z', { mode: m }), RULES0).mode187),
        [true, false, false, false, false, false, false, false, false, false, false, false, false]);
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
    same('the waits: 5 s doubled for every failure up to 5 minutes, then 0 to +20 % (never less than 5 s)', [Games.liveDelay(0, 0), Games.liveDelay(0, 1), Games.liveDelay(0, 0.5), Games.liveDelay(1, 0), Games.liveDelay(2, 0), Games.liveDelay(6, 0), Games.liveDelay(50, 0), Games.liveDelay(0, -5), Games.liveDelay(0, 9)],
        [5000, 6000, 5500, 10000, 20000, 300000, 300000, 5000, 6000]);
    same('the seconds are at most 359999 (99:59:59), and a number below it is whole', [Games.liveOf({ id: 'A-20261109-120000Z', seconds: 1e9 }, RULES0).seconds, Games.liveOf({ id: 'A-20261109-120000Z', seconds: 359999.9 }, RULES0).seconds, Games.liveOf({ id: 'A-20261109-120000Z', turns: 1e12 }, RULES0).seconds], [359999, 359999, 359999]);
    same('the least wait is the door\'s own pace: 5 s (and 1.5 s when a room has gone)', [Games.LIVE_EVERY, Games.LIVE_SOON], [5000, 1500]);
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
    same('Watch is a link to the player on that match, in this very tab (the page holds no room: nothing is lost, and Back and Home come back to the one front page)', [w.tagName, w.getAttribute('href'), w.getAttribute('target'), w.getAttribute('rel')], ['a', '/play?live=TREASURE-20261109-120000Z', null, null]);
    check('... and its name for a screen reader says the match', /^Watch the match on Treasure, Ann, Bot \(Medium\), 2:05 so far$/.test(w.getAttribute('aria-label')));
    check('a row in progress has no Join (nobody can take a seat of a running match): its one button is the strip of the scores', rows.every((li) => !first(li, 'gjoin') && li.findAll((e) => e.tagName === 'button').every((b) => classes(b).indexOf('gsum') !== -1)));
    check('the heading of the list says how many are open and how many are in progress', headOf(env) === '1 open, 2 in progress', headOf(env));
    check('the count beside Games is the open games and the matches in progress together (1 open and 2 in progress: 3), and says both to a screen reader', R.pillOf(env).textContent === '3' && R.pillOf(env).hidden === false && env.home('games').getAttribute('aria-label') === 'Games, 1 open, 2 in progress');
    check('the open rooms are the rows of the first list and the matches are under it, in a box with its own heading', find(popOf(env), 'gitem').filter((li) => classes(li).indexOf('gslot') === -1).length === 3 && first(boxOf(env), 'gsub').children.length === 1 && first(boxOf(env), 'gsub').textContent.indexOf('In progress') === 0 && boxOf(env).hidden === false);
    check('a row of an open room still has its Join', first(popOf(env), 'gjoin') !== null && first(popOf(env), 'gjoin').textContent === 'Join');
    check('the picture of a match is the map\'s square (an original\'s own)', first(rows[0], 'gmap').findAll((e) => e.tagName === 'img').length === 1 && /preview_treasure\.png$/.test(first(rows[0], 'gmap').findAll((e) => e.tagName === 'img')[0].src));
    await wait(env, 5500);
    check('a look later the clock of a match is the door\'s (the rows stay, only the time is written again)', looksAtLive(env) === 2 && liveRows(env)[0] === rows[0]);
    env.site.live.body = answer([LIVE('TREASURE-20261109-120000Z', { seconds: 135, players: ['Green (Ann)', 'Red (Bot (Medium))'] })]);
    await wait(env, 6500);
    same('a match that ended is gone from the list and the one that goes on says its new time', liveRows(env).map((li) => [first(li, 'gname').textContent, first(li, 'glen').textContent]), [['Treasure', '2:15 so far']]);
    check('... and the head follows', headOf(env) === '1 open, 1 in progress', headOf(env));
    await wait(env, 31000);
    check('the count follows the matches: one ended, so 1 open and 1 in progress is 2, after the list and the matches have both been looked at again', R.pillOf(env).textContent === '2' && env.home('games').getAttribute('aria-label') === 'Games, 1 open, 1 in progress');
}
{   // text only: names with markup, a very long name, a map of the players
    const markup = [LIVE('Pop_Corn-20261109-120000Z', { map: 'Pop Corn.lvl', players: ['Green (<img src=x onerror=alert(1)>)', 'Red (' + 'W'.repeat(60) + ')', 'Blue (a "b" & \'c\' <\/script>)'] })];
    const env = await livePage({ rows: markup });
    const li = liveRows(env)[0];
    same('names are text: the markup of a name is shown as it was typed and nothing is made of it', first(li, 'gwho').textContent, '<img src=x onerror=alert(1)>, ' + 'W'.repeat(60) + ', a "b" & \'c\' <\/script>');
    check('... nothing was written as markup, the row holds no element that a name could make, and the one address is the id\'s', env.innerHTMLWrites.length === 0 && li.all().every((e) => ['li', 'span', 'a', 'i', 'img', 'div', 'button', 'b'].indexOf(e.tagName) !== -1) && watchOf(li).getAttribute('href') === '/play?live=Pop_Corn-20261109-120000Z');
    check('a map of the players: its name without the ending, and a picture only where the page has the picker\'s rule', first(li, 'gname').textContent === 'Pop Corn' && first(li, 'gmap').findAll((e) => e.tagName === 'img').every((img) => /^\/maps\/previews\/Pop%20Corn\.webp$/.test(img.src.replace(/^.*(?=\/maps)/, ''))));
}
{   // nothing in progress; nothing open
    const env = await livePage({ rows: [], lobbies: [row('k7m2xq', 'Pebble')] });
    check('nothing in progress: no box at all (an empty box would grow the panel), and the heading is the plain one, "1 waiting · updates live"', boxOf(env).hidden === true && headOf(env) === '1 waiting \u00b7 updates live', headOf(env));
    const both = await livePage({ rows: [] });
    check('nothing open and nothing in progress: the empty cards of the open games (two rows), one line "none waiting", and no box of matches', headOf(both) === 'none waiting' && slotsOf(both).length === 6 && boxOf(both).hidden === true);
    const only = await livePage({ lobbies: [] });
    check('nothing open, two in progress: no "No open games right now." card at all; one row of empty cards, then the box of matches; the head says "0 open, 2 in progress"', slotsOf(only).length === 3 && liveRows(only).length === 2 && headOf(only) === '0 open, 2 in progress' && textOf(popOf(only)).indexOf('No open games right now.') === -1, headOf(only));
    const openOnes = await livePage({ lobbies: [row('k7m2xq', 'Pebble')] });
    check('with an open game the open rows come first, then the empty cards that fill one row, and the box of matches follows', rowsOf(openOnes).length === 3 && slotsOf(openOnes).length === 2 && first(popOf(openOnes), 'gpbody').children.indexOf(first(popOf(openOnes), 'glist')) < first(popOf(openOnes), 'gpbody').children.indexOf(boxOf(openOnes)));
}

{   // the tag of a match of mode 187: after the red Live mark, text only, none for any other mode
    const KILLS = LIVE('TREASURE-20261109-120000Z', { mode: '187', seconds: 125 }), SCORE = LIVE('ISLANDS-20261109-115900Z', { map: 'ISLANDS.LVL', mode: 'highest-score', seconds: 7 });
    const OLD = Object.assign(LIVE('MEDIUM-20261109-115800Z', { map: 'MEDIUM.LVL', seconds: 61 }), { mode: undefined }), ODD = LIVE('TINY-20261109-115700Z', { map: 'TINY.LVL', mode: '<b>187</b>', seconds: 62 });
    const env = await livePage({ rows: [KILLS, SCORE, OLD, ODD] });
    const rows = liveRows(env);
    same('four rows in the door\'s order', rows.map((li) => first(li, 'gname').textContent), ['Treasure', 'Islands', 'Medium', 'Tiny']);
    same('the match of mode 187 has the tag 187; a highest-score match, an answer with no mode and one with a word that is not a mode have none', rows.map((li) => find(li, 'gmode').map((t) => t.textContent)), [['187'], [], [], []]);
    same('the tag stands in one pair with the Live mark, after it, in the top line of the row; every row has the pair', rows.map((li) => first(li, 'gmarks').children.map((c) => c.className)), [['glive-mark', 'gmode'], ['glive-mark'], ['glive-mark'], ['glive-mark']]);
    check('the pair is in the top line, after the map\'s name', rows.every((li) => { const top = first(li, 'gtop').children; return top.length === 2 && top[0].className === 'gname' && top[1].className === 'gmarks'; }));
    same('Watch says the mode for a screen reader: "the 187 match" for a 187 match, "the match" for the others', rows.map((li) => watchOf(li).getAttribute('aria-label')),
        ['Watch the 187 match on Treasure, Ann, Bot (Medium), 2:05 so far', 'Watch the match on Islands, Ann, Bot (Medium), 0:07 so far', 'Watch the match on Medium, Ann, Bot (Medium), 1:01 so far', 'Watch the match on Tiny, Ann, Bot (Medium), 1:02 so far']);
    check('the tag is text that the page wrote itself: nothing was written as markup, and the word of the door never reached the row', env.innerHTMLWrites.length === 0 && !rows.some((li) => li.all().some((e) => /<b>/.test(e.textContent || ''))));
    env.site.live.body = answer([Object.assign({}, KILLS, { seconds: 135 }), SCORE, OLD, ODD]);
    await wait(env, 12500);
    check('a look later the clock is written again and the 187 row keeps its tag and its name for a screen reader (the rows stay)', liveRows(env)[0] === rows[0] && find(rows[0], 'gmode').length === 1 && first(rows[0], 'glen').textContent === '2:15 so far' && watchOf(rows[0]).getAttribute('aria-label') === 'Watch the 187 match on Treasure, Ann, Bot (Medium), 2:15 so far');
    env.site.live.body = answer([Object.assign({}, KILLS, { mode: 'highest-score', seconds: 140 }), SCORE, OLD, ODD]);
    await wait(env, 12500);
    check('a match that is given another mode by a later answer (it never is: the id is one match) is drawn again as the answer says', find(liveRows(env)[0], 'gmode').length === 0 && watchOf(liveRows(env)[0]).getAttribute('aria-label') === 'Watch the match on Treasure, Ann, Bot (Medium), 2:20 so far');
}

// ---- 2a. the count beside Games (and on the phone's tab, which copies it) ----
{   // matches in progress and no open room: the count is the matches, not 0
    const env = await livePage({});
    check('0 open and 2 in progress: the count is 2 (it showed 0 for them), the head says both, and a screen reader hears both', R.pillOf(env).textContent === '2' && R.pillOf(env).hidden === false && headOf(env) === '0 open, 2 in progress' && env.home('games').getAttribute('aria-label') === 'Games, 0 open, 2 in progress', [R.pillOf(env).textContent, headOf(env)].join());
    env.site.live.body = answer([]);
    await wait(env, 12500);
    check('the matches end: the count goes back to the open games, here none', R.pillOf(env).textContent === '0' && env.home('games').getAttribute('aria-label') === 'Games, 0 open');
    env.site.live.body = answer(SAMPLE);
    await wait(env, 12500);
    check('and comes back with the next look at the matches', R.pillOf(env).textContent === '2');
}
{   // the door of the matches fails after it has answered: the section goes, and so do the matches from the count
    const env = await livePage({ lobbies: [row('k7m2xq', 'Pebble')] });
    check('(1 open and 2 in progress: 3)', R.pillOf(env).textContent === '3');
    env.site.live.reject = true;
    await wait(env, 12500);
    check('a failed look at the matches hides them, and the count is the open games alone again', boxOf(env).hidden === true && R.pillOf(env).textContent === '1' && env.home('games').getAttribute('aria-label') === 'Games, 1 open', R.pillOf(env).textContent);
}
{   // the count stops at 50+ as the list does, a page with a room of its own counts it once, and no live door leaves the open games
    const many = Array.from({ length: 60 }, (_, i) => row('r' + String(i).padStart(5, '0'), 'Host ' + i));
    const env = await livePage({ lobbies: many });
    check('60 open rooms and 2 matches: "50+" (the server lists 50 at most, so more may be open)', R.pillOf(env).textContent === '50+' && env.home('games').getAttribute('aria-label') === 'Games, 50+ open, 2 in progress', R.pillOf(env).textContent);
    const near = await livePage({ lobbies: Array.from({ length: 48 }, (_, i) => row('r' + String(i).padStart(5, '0'), 'Host ' + i)) });
    check('48 open rooms and 2 matches are "50+": the sum is what is counted', R.pillOf(near).textContent === '50+', R.pillOf(near).textContent);
    const none = await livePage({ rows: [], lobbies: [row('k7m2xq', 'Pebble')] });
    check('no match runs: the count is the open games, as it was', R.pillOf(none).textContent === '1' && none.home('games').getAttribute('aria-label') === 'Games, 1 open');
}

// ---- 2b. which tab Watch uses, and the watch tab that asks for this page ----
// A tab that this page opened (web/site_nav.js: a live view, or Matches) asks on the channel "ants-watch" for the page that opened it; this page answers for a match that it listed while it held a room, and for Matches
// once it has held a room. A fake of the browser's channel: a message goes to every other
// channel of the same name that is open (it is not given to the one that sent it, as in a browser).
function channels() {
    const open = [];
    class Channel {
        constructor(name) { this.name = name; this.closed = false; this.onmessage = null; open.push(this); }
        postMessage(data) { const copy = JSON.parse(JSON.stringify(data)); for (const c of open) if (c !== this && c.name === this.name && !c.closed && c.onmessage) c.onmessage({ data: copy }); }
        close() { this.closed = true; }
    }
    return { Channel, open };
}
// a tab that this page opened: asks for the page that opened it, by a match's id or "matches" (ask returns how many answers came back for that token: an answer goes to every channel, and a tab takes only the one that carries its own token)
function watchTab(bus, side) {
    const tab = new bus.Channel('ants-watch'), heard = [];
    tab.onmessage = (m) => heard.push(m.data);
    return { heard, ask: (token) => { tab.postMessage({ t: 'back', side, token }); return heard.filter((m) => m.token === token).length; } };
}
{
    const ID = 'TREASURE-20261109-120000Z', OTHER = 'ISLANDS-20261109-115900Z';
    const bus = channels();
    const env = await livePage({ options: { win: { BroadcastChannel: bus.Channel } } });
    const link = () => watchOf(liveRows(env)[0]);
    const outTab = (a) => [a.getAttribute('target'), a.getAttribute('rel')];
    const tab = watchTab(bus, ID);
    same('with no room Watch is a plain link of the same tab, and a press does not change that', [outTab(link()), (link().click(), outTab(link()))], [[null, null], [null, null]]);
    same('... and nobody that asks for this page is answered (no seat was given up for a tab to come back to)', tab.ask('t0'), 0);
    myRoom(env, ['Maya'], R.PRIVATE);
    env.toggleRoom();
    await flush();
    check('(Home shows, with the room held)', env.screen() === 'home' && env.holdsRoom());
    same('while the page holds a room Watch is a new tab with no opener (leaving would give the seat up)', outTab(link()), ['_blank', 'noopener']);
    same('... the address is the same one', link().getAttribute('href'), '/play?live=' + ID);
    same('a watch tab that asks for the match listed now is answered, with its own token', [tab.ask('t1'), tab.heard.filter((m) => m.token === 't1')], [1, [{ t: 'here', token: 't1' }]]);
    same('... so is a match of another id that this page listed; a match that it never listed, and a watch tab with no match, are not', [watchTab(bus, OTHER).ask('t2'), watchTab(bus, 'NEVER-20261109-000000Z').ask('t3'), watchTab(bus, undefined).ask('t4')], [1, 0, 0]);
    same('... a token that is not text is not answered (nothing is made of it)', watchTab(bus, ID).ask(5), 0);
    env.roomBar('leave').click();
    await flush();
    same('the room is left: Watch is a plain link again at once', outTab(link()), [null, null]);
    same('... and the page still answers for the match that it listed while it held a room (the watch tab that is open goes back to this very tab)', tab.ask('t6'), 1);
    check('none of it reloaded or navigated this page', env.reloads === 0 && env.assigned.length === 0 && env.opened.length === 0);
    const asked = watchTab(bus, 'matches');
    same('Matches (the Menu\'s link of home.js: a new tab while a room is held) is answered for once the page has held a room, also after the room was left', asked.ask('m1'), 1);
    check('(a press that comes while the page holds a room decides then: the link is a new tab at the press, whatever was drawn)', (() => { myRoom(env, ['Maya'], R.PRIVATE); link().removeAttribute('target'); link().removeAttribute('rel'); link().click(); return outTab(link()).join() === '_blank,noopener'; })());
}
{   // Matches: a page that never held a room answers nobody for it (its Matches links are plain links of the same tab)
    const bus = channels();
    const env = await livePage({ options: { win: { BroadcastChannel: bus.Channel } } });
    same('a page that has held no room answers a Matches tab nobody, and a tab with no key nobody', [watchTab(bus, 'matches').ask('m0'), watchTab(bus, '').ask('m2'), watchTab(bus, undefined).ask('m3')], [0, 0, 0]);
    myRoom(env, ['Maya'], R.PRIVATE);
    same('... and answers it once it holds one', watchTab(bus, 'matches').ask('m4'), 1);
}
{   // START reaches the page: it leaves, so it answers nobody
    const bus = channels();
    const env = await livePage({ search: '?room=k7m2xq', options: { stayHome: false, win: { BroadcastChannel: bus.Channel } } });
    const sock = R.guestRoom(env, 'Pebble', R.PUBLIC);
    env.toggleRoom();
    await wait(env, 1000);
    const tab = watchTab(bus, 'TREASURE-20261109-120000Z'), before = tab.ask('a');
    const matches = watchTab(bus, 'matches'), beforeMatches = matches.ask('c');
    sock.receive(roomMessage(seatsOf(['Pebble', 'Maya']), { you: 1, leader: 0, flags: STARTING }));
    same('a guest holds a room too: a watch tab and a Matches tab are answered; once START is on its way the page answers nobody', [before, tab.ask('b'), beforeMatches, matches.ask('d')], [1, 0, 1, 0]);
}
{   // a browser with no channel, a channel that refuses: nothing breaks, Watch is the same as ever
    const none = await livePage({ options: { win: { BroadcastChannel: undefined } } });
    myRoom(none, ['Maya'], R.PRIVATE); none.toggleRoom(); await flush();
    same('no channel in the browser: the list shows and Watch is a new tab in a room', [watchOf(liveRows(none)[0]).getAttribute('target'), none.screen()], ['_blank', 'home']);
    const broken = await livePage({ options: { win: { BroadcastChannel: function () { throw new Error('not allowed'); } } } });
    check('a channel that cannot be made changes nothing: the rows are there', liveRows(broken).length === 2 && watchOf(liveRows(broken)[0]).getAttribute('target') === null);
}

// ---- 3. the looks at /live ----
{
    const env = await livePage();
    await wait(env, 4500);
    check('the next look is 5 s after the first, not before', looksAtLive(env) === 1);
    await wait(env, 1600);
    await wait(env, 6000); await wait(env, 6000);
    const t = env.site.live.times;
    check('the looks are 5 s apart at the least (and not more than 6 s: +20 %)', t.length >= 4 && t.every((x, i) => i === 0 || (x - t[i - 1] >= 5000 && x - t[i - 1] <= 6250)), JSON.stringify(t));
    env.hide(true);
    const n = looksAtLive(env);
    await wait(env, 120000);
    check('a tab that is out of sight looks at /live not at all', looksAtLive(env) === n);
    env.hide(false);
    await flush();
    check('... and looks again when it is back (and the 5 s have gone)', looksAtLive(env) === n + 1);
}
{   // a popover: no look while it is closed, and every 5 s while it is open
    const site = R.fakeSite(); site.live.reject = false; site.live.body = answer(SAMPLE);
    const env = await page({ site, options: { popover: true } });
    await wait(env, 60000);
    check('the popover that is closed looks at /live not at all', looksAtLive(env) === 0);
    tap(env.home('games'));
    await flush();
    check('... it looks when it opens', looksAtLive(env) === 1);
    await wait(env, 4500);
    check('... and 5 s later, not before', looksAtLive(env) === 1);
    await wait(env, 1600);
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
    check('a server with ' + what + ' at /live: the section is hidden and the open rooms go on as they were', boxOf(env).hidden === true && openRows(env).length === 1 && headOf(env) === '1 waiting \u00b7 updates live', headOf(env));
    check('... with no error, no notice and no toast', R.toastTextOf(env) === '' && first(popOf(env), 'gpnote').hidden === true);
    const times = site.live.times.slice();
    await wait(env, 100000);
    check('... and the looks go on slowly (doubled after every failure, up to 5 minutes), never faster than 5 s', site.live.times.length <= times.length + 3 && site.live.times.every((x, i) => i === 0 || x - site.live.times[i - 1] >= 5000), JSON.stringify(site.live.times));
    check('... the count beside Games is the open game alone (no match is shown, so none is counted)', R.pillOf(env).textContent === '1' && env.home('games').getAttribute('aria-label') === 'Games, 1 open');
}
{   // a door that answers again after a failure
    const site = R.fakeSite(); site.live.reject = true;
    const env = await page({ site, options: { popover: false } });
    check('(no live door at first: hidden)', boxOf(env).hidden === true);
    site.live.reject = false; site.live.body = answer(SAMPLE);
    await wait(env, 13000);
    check('a door that answers later brings the rows', liveRows(env).length === 2 && boxOf(env).hidden === false);
    await wait(env, 6500);
    const t = site.live.times;
    check('a good answer ends the slow pace: the next look is 5 to 6 s after it, not 10 s or more', t.length === 3 && t[2] - t[1] >= 5000 && t[2] - t[1] <= 6250, JSON.stringify(t));
}
{   // the hand-off: START reaches the page; no more looks
    const env = await livePage({ search: '?room=k7m2xq', options: { stayHome: false } });
    const sock = R.guestRoom(env, 'Pebble', R.PUBLIC);
    env.toggleRoom();
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
    env.toggleRoom();
    await flush();
    check('Home comes back: a look at once (the 5 s have gone)', env.screen() === 'home' && looksAtLive(env) === n + 1);
}

{   // the 5 s hold across the page's own comings and goings: the Room and back, a tab that hides and shows
    const env = await livePage();
    await wait(env, 1500);
    myRoom(env, ['Maya'], R.PRIVATE);
    await wait(env, 1500);
    env.toggleRoom();
    await flush();
    check('Home is back 3 s after the look: no look yet (5 s must have gone since the last one)', env.screen() === 'home' && looksAtLive(env) === 1, looksAtLive(env));
    await wait(env, 1000);
    env.hide(true); env.hide(false);
    await flush();
    check('... nor for a tab that hides and shows at 4 s', looksAtLive(env) === 1, looksAtLive(env));
    await wait(env, 1700);
    check('... the look comes when the 5 s have gone (once, not once for each coming back)', looksAtLive(env) === 2, looksAtLive(env));
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
    await wait(env, 1500);
    myRoom(env, ['Maya'], R.PRIVATE);
    await wait(env, 6000);
    env.toggleRoom();
    await flush();
    check('Home is back 7.5 s after a failed look: no look (a failure waits 10 s, not 5)', env.screen() === 'home' && looksAtLive(env) === 1, looksAtLive(env));
    env.hide(true); env.hide(false);
    await flush();
    check('... nor for a tab that shows again', looksAtLive(env) === 1, looksAtLive(env));
    await wait(env, 3000);
    check('... it comes when the 10 s have gone', looksAtLive(env) === 2, looksAtLive(env));
    await wait(env, 12000);
    check('... and a second failure waits 20 s: nothing 12 s after it', looksAtLive(env) === 2, looksAtLive(env));
}
{   // the 5 s are counted from the END of a look: a look that takes 7 s holds the next one back until 5 s after its end
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
    check('a tab that shows again 10 s after the START of a look and 3 s after its END asks nothing (the 5 s run from the end)', looksAtLive(env) === 1, looksAtLive(env));
    await wait(env, 3000);
    check('... the next look comes 5 to 6 s after the end', looksAtLive(env) === 2, looksAtLive(env));
}
{   // a match whose room has just started it (no turn run) says "Starting..."; the clock comes with its first turn; a screen reader is told too
    const starting = LIVE('TREASURE-20261109-120000Z', { turns: 0, seconds: 0, players: ['Green (Ann)', 'Red (Cy)'] });
    const youngest = LIVE('ISLANDS-20261109-115900Z', { map: 'ISLANDS.LVL', turns: 20, seconds: 1, players: ['Blue (Dee)', 'Black (Eve)'] });
    same('a match with turns 0 is starting; one with a turn or more, one with no turns in its answer and one whose turns are not 0 as a number are not', [0, 1, undefined, null, '0', '', false, -0].map((t) => Games.liveOf(LIVE('A-20261109-120000Z', { turns: t, seconds: undefined }), RULES0).starting),
        [true, false, false, false, false, false, false, true]);
    const env = await livePage({ rows: [starting, youngest] });
    same('the row of a match that has run no turn says it is starting (and the next one has its clock)', liveRows(env).map((li) => first(li, 'glen').textContent), ['Starting\u2026', '0:01 so far']);
    same('... and a screen reader is told the same', liveRows(env).map((li) => watchOf(li).getAttribute('aria-label')), ['Watch the match on Treasure, Ann, Cy, Starting\u2026', 'Watch the match on Islands, Dee, Eve, 0:01 so far']);
    check('... with Watch and the Live mark as for any match', liveRows(env).every((li) => first(li, 'glive-mark').textContent === 'Live' && watchOf(li).textContent === 'Watch'));
    env.site.live.body = answer([Object.assign({}, starting, { turns: 40, seconds: 2 }), youngest]);
    await wait(env, 6200);
    same('the first turns have run: the row has its clock, in the row that is still there', liveRows(env).map((li) => first(li, 'glen').textContent), ['0:02 so far', '0:01 so far']);
}
{   // a room that is gone from the list may have started its match: /live is looked at in 1.5 s, not at the next 5 s; a room that came, a failed look and an unchanged list bring nothing forward
    const room = (code, host) => row(code, host);
    const look = async (what, mutate, expectSoon) => {
        const site = R.fakeSite(); site.live.reject = false; site.live.defer = true; site.live.body = answer(SAMPLE);
        site.body = listOf([room('k7m2xq', 'Pebble'), room('p3d9ha', 'Fern')]);
        const env = await page({ site, options: { popover: false } });
        await wait(env, 4000);
        site.live.waiting.splice(0).forEach((release) => release());                // (the first look at /live ends at 4 s: the next one is due 5 s after that, at 9 s)
        await flush();
        mutate(site);
        await wait(env, 2600);                                                       // (the list is looked at again at 5 to 6 s)
        await wait(env, 2300);
        site.live.waiting.splice(0).forEach((release) => release());
        await flush();
        check(what + (expectSoon ? ': /live is looked at before the next regular look (at 9 s)' : ': nothing is brought forward'), expectSoon ? looksAtLive(env) === 2 : looksAtLive(env) === 1, looksAtLive(env));
        return env;
    };
    await look('a room of the list has gone', (site) => { site.body = listOf([room('p3d9ha', 'Fern')]); }, true);
    await look('every room has gone', (site) => { site.body = listOf([]); }, true);
    await look('a room has come', (site) => { site.body = listOf([room('k7m2xq', 'Pebble'), room('p3d9ha', 'Fern'), room('m4pl33', 'Maple')]); }, false);
    await look('the list is the same', () => {}, false);
    await look('a room is replaced by another (one has gone)', (site) => { site.body = listOf([room('k7m2xq', 'Pebble'), room('m4pl33', 'Maple')]); }, true);
    await look('the look at the list fails (an empty list that is no answer)', (site) => { site.status = 503; }, false);
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

// ---- 4. no switch, nothing kept ----
{
    const env = await livePage({ lobbies: [row('k7m2xq', 'Pebble')] });
    check('the rows are there with no switch in the list (nothing to turn off, no checkbox) and the browser holds nothing', liveRows(env).length === 2 && find(popOf(env), 'gfilter').length === 0 && find(popOf(env), 'gfrow').length === 0 && popOf(env).findAll((e) => e.tagName === 'input' && e.type === 'checkbox').length === 0 && headOf(env) === '1 open, 2 in progress' && env.storage.getItem('ants.home.games.live') === null, headOf(env));
}
{   // a word kept by an earlier version: ignored, and neither read nor changed
    for (const word of ['0', 'yes']) {
        const env = await livePage({ stored: { 'ants.name': 'Maya', 'ants.home.games.live': word } });
        check('an old stored "' + word + '": the rows show all the same and the word is left as it was', liveRows(env).length === 2 && looksAtLive(env) === 1 && env.storage.getItem('ants.home.games.live') === word);
    }
}
{   // a browser that keeps nothing: no difference
    const env = await livePage({ stored: R.H.THROWS });
    check('a browser that refuses its storage: the rows are there (nothing threw)', liveRows(env).length === 2);
}

// ---- 5. the scores of the matches (the door's `scores` and `sides`: the score boxes, live): the strip, the scoreboard that it opens, and the pure parts ----
{
    const TEXT = Games.TEXT, P = (list) => list;                                          // (the page's own words; a list of players, as the door writes them)
    const withScores = (o, scores, sides) => Object.assign({}, o, { scores, sides });
    const four = (n) => withScores(LIVE('TREASURE-20261124-091500Z', { seconds: 725, turns: 14500, players: P(['Green (Sage)', 'Red (Rowan)', 'Blue (Kit (bot, Medium))', 'Black (moss)']) }), n || [2040, 1310, 900, 455], [0, 1, 2, 3]);
    const team = (n) => withScores(LIVE('ISLANDS-20261124-090200Z', { map: 'ISLANDS.LVL', seconds: 905, turns: 18100, players: P(['Green (Pebble)', 'Red (Wren)', 'Blue (juniper)', 'Black (Pip (bot, Hard))']) }), n || [2310, 2310, 2680, 2680], [0, 0, 2, 2]);
    const kills = (n) => withScores(LIVE('SMALL-20261124-091900Z', { map: 'SMALL.LVL', mode: '187', seconds: 341, turns: 6820, players: P(['Green (tess)', 'Red (Fern)', 'Blue (Bo (bot, Easy))']) }), n || [14, 9, null], [0, 1, 2]);
    const duel = () => withScores(LIVE('TINY-20261124-092300Z', { map: 'TINY.LVL', seconds: 125, turns: 2500, players: P(['Green (maple.leaf)', 'Red (Pip (bot, Hard))']) }), [300, 300], [0, 1]);
    const fresh = () => LIVE('MEDIUM-20261124-092500Z', { map: 'MEDIUM.LVL', seconds: 0, turns: 0, players: P(['Green (Wren)', 'Red (moss)']) });
    const old = () => LIVE('GAUNTLET-20261124-092600Z', { map: 'GAUNTLET.LVL', seconds: 60, turns: 1200, players: P(['Green (Ann)', 'Red (Bob)']) });       // (a server that gives no scores)
    const cardOf = (env, id) => liveRows(env).filter((li) => watchOf(li).getAttribute('data-live') === id)[0];
    const toggleOf = (li) => first(li, 'gsum');
    const nums = (li) => first(li, 'gnums').children.filter((c) => c.className.indexOf('gpair') !== -1).map((p) => first(p, 'gn').textContent);
    const gold = (li) => find(first(li, 'gnums'), 'glead').map((n) => n.textContent);
    const rowsOfBoard = (li) => find(first(li, 'gboard'), 'gside');
    const openIt = (li) => toggleOf(li).click();
    const inside = (root, tag) => root.findAll((e) => e.tagName === tag);                  // (the elements of a tag; find() is by class)

    // the pure parts
    const door = (extra) => Object.assign({ id: 'A-20261109-120000Z', turns: 100, players: ['Green (Ann)', 'Red (Bob)', 'Blue'] }, extra || {});
    same('a match with scores: a number and a side for each player, in the door\'s order', [Games.liveOf(door({ scores: [10, 20, 30], sides: [0, 1, 2] }), RULES0).scores, Games.liveOf(door({ scores: [10, 20, 30], sides: [0, 1, 2] }), RULES0).sides], [[10, 20, 30], [0, 1, 2]]);
    same('a match with none, or with a shape that does not fit (not lists, one missing, the wrong length, no players), has no scores at all', [{}, { scores: [1, 2, 3] }, { sides: [0, 1, 2] }, { scores: 'x', sides: [0, 1, 2] }, { scores: [1, 2], sides: [0, 1, 2] }, { scores: [1, 2, 3], sides: [0, 1] }, { scores: {}, sides: {} }, { players: [], scores: [], sides: [] }].map((x) => Games.liveOf(door(x), RULES0).scores), [null, null, null, null, null, null, null, null]);
    same('a number that is no box (below 0, a fraction, over 999999, a text, null: a covered box, none) is no number; 999999 and 0 are', Games.liveOf(door({ scores: [-1, 1.5, 1000000], sides: [0, 1, 2] }), RULES0).scores.concat(Games.liveOf(door({ scores: ['9', null, undefined], sides: [0, 1, 2] }), RULES0).scores, Games.liveOf(door({ scores: [999999, 0, 7], sides: [0, 1, 2] }), RULES0).scores), [null, null, null, null, null, null, 999999, 0, 7]);
    same('a side that is no seat (4, -1, a fraction, a text) is a side of its own, never one that another player is in', Games.liveOf(door({ scores: [1, 2, 3], sides: [4, -1, 'a'] }), RULES0).sides.map((x) => x >= 4 && x < 100), [true, true, true]);
    same('a player that the page drops keeps the others\' numbers where they were (the numbers go with the strings of the door, not with what is kept)', [Games.liveOf(door({ players: ['Green (A)', 7, 'Red (B)'], scores: [1, 2, 3], sides: [0, 1, 2] }), RULES0).scores, Games.liveOf(door({ players: ['Green (A)', 7, 'Red (B)'], scores: [1, 2, 3], sides: [0, 1, 2] }), RULES0).sides], [[1, 3], [0, 2]]);
    const board = (o) => Games.boardOf(Games.liveOf(o, RULES0));
    same('the leader is the side alone at the top with a number above 0', [board(four()).sides.map((s) => s.lead), board(four([5, 40, 3, 2])).sides.map((s) => s.lead), board(duel()).sides.map((s) => s.lead), board(four([0, 0, 0, 0])).sides.map((s) => s.lead), board(four([7, 7, 3, 1])).sides.map((s) => s.lead)],
        [[true, false, false, false], [false, true, false, false], [false, false], [false, false, false, false], [false, false, false, false]]);
    same('a side that is the only one with a box leads when above 0; a covered box never leads and is no number', [board(kills()).sides.map((s) => [s.n, s.lead]), board(kills([0, null, null])).sides.map((s) => s.lead)], [[[14, true], [9, false], [null, false]], [false, false, false]]);
    same('allies are one side, a player alone is one: Pebble + Wren and juniper + Pip, four players, and the column\'s word is the score (Kills in 187)', [board(team()).sides.map((s) => s.who.length), board(four()).sides.map((s) => s.who.length), board(team()).label, board(kills()).label, board(team()).team, board(four()).team], [[2, 2], [1, 1, 1, 1], 'Score', 'Kills', true, false]);
    same('a match with no score yet has a side for each player and no number', [board(fresh()).scored, board(fresh()).sides.map((s) => [s.n, s.lead])], [false, [[null, false], [null, false]]]);
    same('the digits are grouped: 0, 999, 1,000, 12,345, 999,999', [0, 999, 1000, 12345, 999999].map(Games.groupDigits), ['0', '999', '1,000', '12,345', '999,999']);
    same('a computer player is named without its level: Kit (bot, Medium), Kit (bot), Bot (Medium); a person with "bot" in a name is not cut', ['Kit (bot, Medium)', 'Kit (bot)', 'Bot (Medium)', 'Abbot', 'Bo (bot, Easy)', 'robot (x)'].map(Games.plainName), ['Kit', 'Kit', 'Bot', 'Abbot', 'Bo', 'robot (x)']);

    // the cards: collapsed they have the strip
    const env = await livePage({ rows: [four(), team(), kills(), duel(), fresh(), old()] });
    const ids = { four: 'TREASURE-20261124-091500Z', team: 'ISLANDS-20261124-090200Z', kills: 'SMALL-20261124-091900Z', duel: 'TINY-20261124-092300Z', fresh: 'MEDIUM-20261124-092500Z', old: 'GAUNTLET-20261124-092600Z' };
    const L = (k) => cardOf(env, ids[k]);
    same('a 4-way: the strip has each player\'s number, the leader\'s in gold, and the word of the column', [nums(L('four')), gold(L('four')), first(L('four'), 'glabel').textContent], [['2,040', '1,310', '900', '455'], ['2,040'], TEXT.score]);
    same('... a square of each seat\'s color before its number, a ring for the computer player; one thin bar in the shares, a segment for each', [find(first(L('four'), 'gnums'), 'gsq').map((q) => q.className), inside(first(L('four'), 'gshare'), 'i').map((q) => q.className)], [['gsq c0', 'gsq c1', 'gsq c2 bot', 'gsq c3'], ['c0', 'c1', 'c2', 'c3']]);
    check('... the share of a side is its number against the whole (the segments are in the proportion of the numbers, none under 2 % of it)', inside(first(L('four'), 'gshare'), 'i').map((q) => Number(/^([\d.]+) 0 0$/.exec(q.style.flex)[1])).join() === '2040,1310,900,455');
    same('a 2v2: two pairs of squares and two numbers with "vs" between them, the leader\'s in gold, and a bar of two colors for each team', [first(L('team'), 'gnums').children.map((c) => c.className), nums(L('team')), gold(L('team')), inside(first(L('team'), 'gshare'), 'i').map((q) => q.className), find(first(L('team'), 'gnums'), 'gsq').map((q) => q.className)],
        [['gpair', 'gvs', 'gpair'], ['2,310', '2,680'], ['2,680'], ['c0 two d1', 'c2 two d3'], ['gsq c0', 'gsq c1', 'gsq c2', 'gsq c3 bot']]);
    same('a 187 match: the column says Kills, the leader is in gold, a covered box is a dash and dimmed and leads nowhere', [first(L('kills'), 'glabel').textContent, nums(L('kills')), gold(L('kills')), find(first(L('kills'), 'gnums'), 'gout').length, inside(first(L('kills'), 'gshare'), 'i').length], [TEXT.kills, ['14', '9', '–'], ['14'], 1, 2]);
    same('a tie marks nobody', [nums(L('duel')), gold(L('duel')), rowsOfBoard(L('duel')).map((r) => r.className)], [['300', '300'], [], ['gside', 'gside']]);
    same('a match that is starting says "no score yet" in place of numbers, and has no bar', [first(L('fresh'), 'gnums').children.map((c) => c.textContent), inside(first(L('fresh'), 'gshare'), 'i').length, first(L('fresh'), 'gscore').hidden], [[TEXT.noScore], 0, false]);
    check('a match that has run and that the door gives no scores for has no strip (the card is what it was: an older server)', first(L('old'), 'gscore').hidden === true);
    check('"scores are live" stands beside the heading of the matches when the door gives scores, and is not drawn when it gives none', first(boxOf(env), 'gnote').hidden === false && first(boxOf(env), 'gnote').textContent.trim() === 'scores are live');
    same('the strip is a button that names the numbers for a screen reader (the squares and bars are pictures), says it is closed, and stands in the card of the match', [toggleOf(L('four')).tagName, toggleOf(L('four')).getAttribute('aria-expanded'), toggleOf(L('four')).getAttribute('aria-label'), toggleOf(L('duel')).getAttribute('aria-label')],
        ['button', 'false', 'Scores of the match on Treasure: Sage 2,040 leading, Rowan 1,310, Kit (bot) 900, moss 455. ' + TEXT.showBoard, 'Scores of the match on Tiny: maple.leaf 300, Pip (bot) 300. ' + TEXT.showBoard]);
    same('... for a 187 match, a team, a covered box and no score yet', [toggleOf(L('kills')).getAttribute('aria-label'), toggleOf(L('team')).getAttribute('aria-label'), toggleOf(L('fresh')).getAttribute('aria-label')],
        ['Scores of the 187 match on Small: tess 14 leading, Fern 9, Bo (bot) left. ' + TEXT.showBoard, 'Scores of the match on Islands: Pebble + Wren 2,310, juniper + Pip (bot) 2,680 leading. ' + TEXT.showBoard, 'Scores of the match on Medium: no score yet. ' + TEXT.showBoard]);
    check('Watch still names the match as it did, and has no score in its name', watchOf(L('four')).getAttribute('aria-label') === 'Watch the match on Treasure, Sage, Rowan, Kit (bot, Medium), moss, 12:05 so far');
    check('the card starts closed: no board, the names of the players in their line, the strip', classes(L('four')).indexOf('gopen') === -1 && first(L('four'), 'gboard').hidden === true && first(L('four'), 'gwho').textContent === 'Sage, Rowan, Kit (bot, Medium), moss');

    // the scoreboard that the strip opens
    const card = L('four'), button = toggleOf(card);
    check('the strip controls the scoreboard (aria-controls is the id of the card\'s board, which is in the card)', button.getAttribute('aria-controls') === first(card, 'gboard').id && /^gboard-TREASURE-20261124-091500Z$/.test(first(card, 'gboard').id) && card.all().indexOf(first(card, 'gboard')) !== -1);
    card.classList.add('mine');
    openIt(card);
    check('... a press opens it and the name says what the next press does ("Hide the scoreboard"), the numbers staying in it', /Rowan 1,310, Kit \(bot\) 900, moss 455\. Hide the scoreboard$/.test(button.getAttribute('aria-label')) && button.getAttribute('aria-expanded') === 'true');
    openIt(card);
    check('... and a press closes it again with the name of the first (the card has opened and closed by its one class: another class that it has is still there)', button.getAttribute('aria-label').slice(-TEXT.showBoard.length) === TEXT.showBoard && classes(card).indexOf('gopen') === -1 && classes(card).indexOf('mine') !== -1 && classes(card).indexOf('glive') !== -1);
    openIt(card);
    check('a press opens the card: the scoreboard shows, the button says it is open, the line of names is out (the board has them)', classes(card).indexOf('gopen') !== -1 && button.getAttribute('aria-expanded') === 'true' && first(card, 'gboard').hidden === false);
    same('the scoreboard has a line for each player: its square, its name (a computer player marked "(bot)", with no level), the number, the leader\'s with a triangle and said to a screen reader', [rowsOfBoard(card).map((r) => first(r, 'gpl').textContent), rowsOfBoard(card).map((r) => first(r, 'gn').children.length ? 'leads' : ''), rowsOfBoard(card).map((r) => first(r, 'gn').textContent), rowsOfBoard(card).map((r) => r.className)],
        [['Sage', 'Rowan', 'Kit (bot)', 'moss'], ['leads', '', '', ''], ['2,040 leading', '1,310', '900', '455'], ['gside glead', 'gside', 'gside', 'gside']]);
    same('... a bar against the best side: 100 %, 64 %, 44 %, 22 %; the colors of the seats; the head says Player and the column\'s word', [rowsOfBoard(card).map((r) => inside(first(r, 'gtrack'), 'i')[0].style.width), rowsOfBoard(card).map((r) => inside(first(r, 'gtrack'), 'i')[0].className), first(card, 'ghead').children.map((c) => c.textContent)],
        [['100%', '64%', '44%', '22%'], ['c0', 'c1', 'c2', 'c3'], [TEXT.player, TEXT.score]]);
    openIt(L('team'));
    same('a team\'s line has both names, both squares and a bar of two colors, the number once; the head says Team', [rowsOfBoard(L('team')).map((r) => first(r, 'gpl').textContent), rowsOfBoard(L('team')).map((r) => find(r, 'gsq').length), rowsOfBoard(L('team')).map((r) => inside(first(r, 'gtrack'), 'i')[0].className), rowsOfBoard(L('team')).map((r) => first(r, 'gn').textContent), first(L('team'), 'ghead').children.map((c) => c.textContent)],
        [['Pebble + Wren', 'juniper + Pip (bot)'], [2, 2], ['c0 two d1', 'c2 two d3'], ['2,310', '2,680 leading'], [TEXT.team, TEXT.score]]);
    openIt(L('kills')); openIt(L('duel')); openIt(L('fresh'));
    same('a 187 board counts Kills; a covered box is a dash with no bar; a tie marks nobody; a match with no score yet has dashes and no bar', [first(L('kills'), 'ghead').children[1].textContent, rowsOfBoard(L('kills')).map((r) => first(r, 'gn').textContent), rowsOfBoard(L('kills')).map((r) => inside(r, 'i').filter((i) => i.className.indexOf('gsq') === -1).length), rowsOfBoard(L('duel')).map((r) => r.className), rowsOfBoard(L('fresh')).map((r) => first(r, 'gn').textContent), rowsOfBoard(L('fresh')).map((r) => inside(first(r, 'gtrack'), 'i').length)],
        [TEXT.kills, ['14 leading', '9', '–'], [1, 1, 0], ['gside', 'gside'], ['–', '–'], [0, 0]]);
    check('a side with a zero has no bar in the board (a number of 0 is no length)', (() => { const z = board(four([100, 0, 0, 0])); return z.sides.length === 4 && z.top === 100; })());
    openIt(card);
    check('a second press closes the card again: the strip, and the names in their line', classes(card).indexOf('gopen') === -1 && button.getAttribute('aria-expanded') === 'false' && first(card, 'gboard').hidden === true);

    // the numbers move in place, a card that is open stays open, a focus stays, and nothing else is asked
    openIt(card);
    const hereLi = card, hereBtn = button;
    hereBtn.focus();
    env.site.live.body = answer([four([2100, 2200, 905, 455]), team([2400, 2400, 2680, 2680]), kills([15, 9, null]), duel(), fresh(), old()]);
    await wait(env, 6500);
    check('a look later the card is the same and still open, the numbers are the door\'s new ones and the lead has moved, and the focus is where it was', L('four') === hereLi && toggleOf(L('four')) === hereBtn && classes(L('four')).indexOf('gopen') !== -1 && env.focused === hereBtn
        && rowsOfBoard(L('four')).map((r) => first(r, 'gn').textContent).join() === '2,100,2,200 leading,905,455' && rowsOfBoard(L('four')).map((r) => r.className).join() === 'gside,gside glead,gside,gside', rowsOfBoard(L('four')).map((r) => first(r, 'gn').textContent).join());
    check('... the strip behind it has the new numbers and the leader, and its name for a screen reader too', nums(L('four')).join() === '2,100,2,200,905,455' && gold(L('four')).join() === '2,200' && /Rowan 2,200 leading/.test(toggleOf(L('four')).getAttribute('aria-label')));
    same('... a card that was not opened is closed still, and the team that took the lead has it', [classes(L('kills')).indexOf('gopen') !== -1, classes(L('duel')).indexOf('gopen') !== -1, gold(L('team'))], [true, true, ['2,680']]);
    env.site.live.body = answer([withScores(Object.assign({}, fresh(), { turns: 40, seconds: 2 }), [0, 0], [0, 1]), four(), team(), kills(), duel(), old()]);
    await wait(env, 6500);
    same('a match that has run its first turn shows its numbers where "no score yet" was (zeros are numbers; a tie marks nobody)', [nums(L('fresh')), gold(L('fresh')), first(L('fresh'), 'gscore').hidden], [['0', '0'], [], false]);
    env.site.live.body = answer([four(), team(), kills(), duel(), old()]);
    await wait(env, 6500);
    env.site.live.body = answer([four(), team(), kills(), duel(), fresh(), old()]);
    await wait(env, 6500);
    check('a match that has left the list loses its choice: when it is back it is closed', classes(L('fresh')).indexOf('gopen') === -1 && first(L('fresh'), 'gboard').hidden === true && classes(L('four')).indexOf('gopen') !== -1);
    check('the looks at /live are the same as ever: 5 to 6 s apart, no other address, no new timer or connection for the scores', env.site.live.calls.every((c) => c.url === '/live') && env.site.live.times.every((t, i) => i === 0 || (t - env.site.live.times[i - 1] >= 5000 && t - env.site.live.times[i - 1] <= 6250)) && env.site.calls.every((c) => c.url === '/lobbies'), JSON.stringify(env.site.live.times));
}
{   // text only: a name with markup is shown as typed in the board and the strip's name; nothing is written as markup
    const markup = Object.assign(LIVE('Pop_Corn-20261109-120000Z', { map: 'Pop Corn.lvl', players: ['Green (<img src=x onerror=alert(1)>)', 'Red (Kit (bot, Medium))'] }), { scores: [5, 7], sides: [0, 1] });
    const env = await livePage({ rows: [markup] });
    const li = liveRows(env)[0], openIt = (card) => first(card, 'gsum').click(), rowsOfBoard = (card) => find(first(card, 'gboard'), 'gside');
    openIt(li);
    same('a name with markup is text in the board, and a computer player is marked "(bot)"', rowsOfBoard(li).map((r) => first(r, 'gpl').textContent), ['<img src=x onerror=alert(1)>', 'Kit (bot)']);
    check('... and the page wrote no markup', env.innerHTMLWrites.length === 0 && li.all().every((e) => ['li', 'span', 'a', 'i', 'img', 'div', 'button', 'b'].indexOf(e.tagName) !== -1));
}
{   // an older server: no scores for any match: no note, no strip, the cards as they were
    const env = await livePage({ rows: [LIVE('TREASURE-20261124-091500Z'), LIVE('ISLANDS-20261124-090200Z', { map: 'ISLANDS.LVL' })] });
    check('a door that gives no scores: the heading has no "scores are live", no card has a strip, and the rows are as they were', first(boxOf(env), 'gnote').hidden === true && liveRows(env).every((li) => first(li, 'gscore').hidden === true && classes(li).indexOf('gopen') === -1));
}
done('matches in progress');
})().catch(broke);
