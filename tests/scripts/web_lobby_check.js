// Runs the front page's OWN code that is still in the page, without a browser. The front page is the lobby now (a room on the game server, protocol 16): what decides what its cards show and what it asks of the
// room lives in web/front/lobby_rules.js (run by tests/scripts/web_lobby_rules_check.js) and the client of the game server in web/front/lobby_net.js (web_lobby_net_check.js, web_lobby_client_check.js).
// What this file holds is the rest of the page:
//   * the page and the two scripts it loads before its own: every member of the rules script (Rules.x), of the net script (Net.x, Net.REJECT.x, Net.OS.x), of the lobby client (client.x) and every event of the client
//     (c.on('x')) that the page uses is there, so that a name that is mistyped or gone fails here and not in a browser;
//   * web/lobby.html, the block LOBBY_BEGIN .. LOBBY_END (the old addresses of the TEST ROOM and of a game on this computer, which the lobby still opens): playersChoice (1 .. 4, else the fallback), soloSeatsText,
//     localGameQuery (the address of a game on THIS computer, which an address with &players=1 still plays and which START of a lobby with nobody else plays too: ?map=<key>[&bots=<levels>]&name=<name>&aspect=<shape>,
//     never a ?join=, a room or a server), hostSeats / hostFillText / hostTeamChoices / hostTeam / hostTeamText / shownRoomTeams / validFillPlan / validRoomTeams / hostPlanText (a room that an address asks for)
//     and roomBlockQuery / roomBlockOf (a room's create block as the links of the test room carry it, &roommap= &roomseats= &roomteams= &roomleaderstart=, and as an address's block is read back: the game page turns
//     it into --room-map, --room-seats, --room-teams and --room-leader-start; a room code is a name and nothing more, so no code carries the map, the seats or the teams). The page's three aliases of the rules
//     (the maps, the default map, mapByKey) are taken from the page itself and run against the real front/lobby_rules.js;
//   * web/shell.html, ANTS_PAGE.localArguments: the game page's local parameters through a WHITELIST (the map by its key out of the six shipped maps, the opponents by one level word or three,
//     the teams by ffa or 0+N, a name of printable ASCII): what reaches the game's own arguments is a file name from a fixed table, level words that were tested, a seat that has a bot, a
//     cleaned name and flags, never the text of the address; nothing at all for an address with no local parameter (today's front page: the setup screen);
//   * web/shell.html, ANTS_PAGE.joinArguments: the parameters of a join of a room (&roommap= &roomseats= &roomteams= &roomleaderstart= &platform= next to the room, the seat, the plan, the teams, the people
//     to wait for and the name), each through its own whitelist, only with a valid door, and in the order that the game is given them;
//   * the pages agree: the six maps (keys and files, which exist in Original-Ants/Maps), the game page's codeText and the rules' (a room code in two groups of three), and every address that the page can make for a
//     game on this computer is read by the game page as the same map, the same bot in each of the three other bases and the same name; every address of a ROOM that the test room's old addresses can make (every room
//     of 2 - 4 players, every set of levels, every team) is read by the game page as the same --fill-bots plan and the same --teams; every create block that the page can put into a link (every map, 2 - 4 seats, every
//     team that the seats offer, the leader-starts flag) is read by the game page as the same --room-map, --room-seats, --room-teams and --room-leader-start, and by the page itself as the same block.
// tests/scripts/test_web_lobby.py runs this with node (the quick tier). usage: node web_lobby_check.js web/shell.html web/lobby.html     (exit 0: every check holds; failures are printed)
'use strict';
const fs = require('fs');
const path = require('path');

const shellPath = process.argv[2];
const lobbyPath = process.argv[3];
if (!shellPath || !lobbyPath) { console.log('usage: web_lobby_check.js shell.html lobby.html'); process.exit(2); }

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
function between(text, begin, end, file) {
    const at = text.indexOf(begin);
    if (at < 0) throw new Error(file + ': the marker ' + begin + ' is missing');
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) throw new Error(file + ': the marker ' + end + ' is missing');
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
}

const shellText = fs.readFileSync(shellPath, 'utf8');
const lobbyText = fs.readFileSync(lobbyPath, 'utf8');
const repo = path.resolve(path.dirname(shellPath), '..');

// ---- the lobby's block, with what it stands on: the rules script (front/lobby_rules.js, which the page loads before its own script) and the page's three aliases of it (the list of maps, the default, mapByKey),
// taken from the page's own text; validFill and validFillPlan, which the block uses, are in the page's FILL block
const rulesPath = path.join(path.dirname(lobbyPath), 'front', 'lobby_rules.js');
const Rules = require(path.resolve(rulesPath));
const aliases = lobbyText.match(/^[ \t]*var (?:MAPS|DEFAULT_MAP_KEY|mapByKey) = Rules\.\w+;$/gm) || [];
same('the page takes its list of maps, its default map and mapByKey from the rules script', aliases.map((a) => a.trim()),
     ['var MAPS = Rules.MAPS;', 'var DEFAULT_MAP_KEY = Rules.DEFAULT_MAP_KEY;', 'var mapByKey = Rules.mapByKey;']);
const lobbyBlock = between(lobbyText, 'LOBBY_BEGIN', 'LOBBY_END', lobbyPath);
const lobbyCode = [
    aliases.join('\n'),
    between(lobbyText, 'FILL_BEGIN', 'FILL_END', lobbyPath),
    lobbyBlock,
    'return { MAPS: MAPS, DEFAULT_MAP_KEY: DEFAULT_MAP_KEY, LOCAL_PAGE: LOCAL_PAGE, playersChoice: playersChoice, soloSeatsText: soloSeatsText, localGameQuery: localGameQuery, validFill: validFill, validFillPlan: validFillPlan, hostSeats: hostSeats, hostFillText: hostFillText, hostTeamChoices: hostTeamChoices, hostTeam: hostTeam, hostTeamText: hostTeamText, shownRoomTeams: shownRoomTeams, validRoomTeams: validRoomTeams, hostPlanText: hostPlanText, roomBlockQuery: roomBlockQuery, roomBlockOf: roomBlockOf, SEAT_COLOURS: SEAT_COLOURS, teamTitle: teamTitle };',
].join('\n');
const L = new Function('Rules', lobbyCode)(Rules);
{   // the block holds what is tested below and nothing else: a function that is added to it needs its own checks here
    const declared = Array.from(lobbyBlock.matchAll(/^ {8}(?:function (\w+)\(|var (\w+) =)/gm)).map((m) => m[1] || m[2]).sort();
    same('the block LOBBY holds these functions and constants and no other (each has its checks below)', declared,
         ['LOCAL_PAGE', 'SEAT_COLOURS', 'hostFillText', 'hostPlanText', 'hostSeats', 'hostTeam', 'hostTeamChoices', 'hostTeamText', 'localGameQuery', 'playersChoice', 'roomBlockOf', 'roomBlockQuery', 'shownRoomTeams', 'soloSeatsText', 'teamTitle', 'validRoomTeams']);
}

// ---- the page and the two scripts it loads before its own (front/lobby_rules.js and front/lobby_net.js): everything that the page reads from them is there. The scripts' own rules are held by their own checks
// (web_lobby_rules_check.js, web_lobby_net_check.js, web_lobby_client_check.js); a name that the page asks for and the script does not have would be a page that fails in the browser and in no check of the script.
{
    const netPath = path.join(path.dirname(lobbyPath), 'front', 'lobby_net.js');
    const Net = require(path.resolve(netPath));
    const netText = fs.readFileSync(netPath, 'utf8');
    const uniq = (list) => Array.from(new Set(list)).sort();
    const usedRules = uniq(Array.from(lobbyText.matchAll(/\bRules\.(\w+)/g)).map((m) => m[1]));
    const lostRules = usedRules.filter((name) => !(name in Rules));
    check('every member of the rules script that the page uses is there (' + usedRules.length + ' of them; missing: ' + lostRules.join(', ') + ')', lostRules.length === 0 && usedRules.length > 20);
    const usedNet = uniq(Array.from(lobbyText.matchAll(/\bNet\.(\w+)(?:\.(\w+))?/g)).map((m) => m[1] + (m[2] ? '.' + m[2] : '')));
    const lostNet = usedNet.filter((name) => { const [a, b] = name.split('.'); return b === undefined ? !(a in Net) : !(Net[a] && b in Net[a]); });
    check('every member of the net script that the page uses is there (' + usedNet.length + ' of them; missing: ' + lostNet.join(', ') + ')', lostNet.length === 0 && usedNet.length > 8);
    const refusals = uniq(Array.from(lobbyText.matchAll(/\bR\.(\w+)/g)).map((m) => m[1]));       // (var R = Net.REJECT in the page's handlers of a refusal)
    check('the page says R = Net.REJECT where it uses R.Full and the others, and every refusal it names is one of the net script\'s (' + refusals.join(', ') + ')', /var R = Net\.REJECT;/.test(lobbyText) && refusals.length >= 6 && refusals.every((name) => name in Net.REJECT));
    const client = new Net.LobbyClient({ url: 'ws://127.0.0.1:1/ws', code: 'k7m2xq', map: 'TREASURE.LVL', name: 'Ann', WebSocket: null });
    const usedClient = uniq(Array.from(lobbyText.matchAll(/\bclient\.(\w+)/g)).map((m) => m[1]));
    const lostClient = usedClient.filter((name) => !(name in client));
    check('every member of the lobby client that the page uses is there (' + usedClient.length + ' of them; missing: ' + lostClient.join(', ') + ')', lostClient.length === 0 && usedClient.length >= 12);
    const listens = uniq(Array.from(lobbyText.matchAll(/\b(?:c|p)\.on\('(\w+)'/g)).map((m) => m[1]));
    const makes = uniq(Array.from(netText.matchAll(/emit\('(\w+)'/g)).map((m) => m[1]).concat(Array.from(netText.matchAll(/type: '(\w+)'/g)).map((m) => m[1])));
    const deaf = listens.filter((name) => makes.indexOf(name) === -1);
    check('every event that the page listens for is one that the lobby client makes (' + listens.length + ' of them; never made: ' + deaf.join(', ') + ')', deaf.length === 0 && listens.length >= 12);
}

// ---- the game page's ANTS_PAGE (the block that the other checks run too, with the same fakes: it reads the address, the storage and two elements while it runs)
function makeStorage(initial) {
    const data = Object.assign({}, initial);
    return { data, getItem(k) { return Object.prototype.hasOwnProperty.call(data, k) ? data[k] : null; }, setItem(k, v) { data[k] = String(v); } };
}
function element(attributes) {
    const el = {
        attributes: Object.assign({}, attributes || {}), listeners: {}, style: {},
        getAttribute(name) { return Object.prototype.hasOwnProperty.call(el.attributes, name) ? el.attributes[name] : null; },
        setAttribute(name, value) { el.attributes[name] = String(value); },
        addEventListener(type, fn) { el.listeners[type] = fn; },
        classList: { add() {}, contains() { return false; } },
    };
    return el;
}
function loadShell() {
    const block = between(shellText, 'ANTS_PAGE_BEGIN', 'ANTS_PAGE_END', shellPath);
    const win = { location: { search: '', protocol: 'https:', host: 'example.test', href: 'https://example.test/', assign() {} }, localStorage: makeStorage({}), confirm() { return true; } };
    const stage = element({ 'data-aspect': '16:9' });
    const doc = {
        body: element(),
        getElementById(id) { if (id === 'game-stage') return stage; throw new Error('the page asked for #' + id); },
        querySelectorAll(selectorText) { if (selectorText === '.seg button[data-aspect]') return []; throw new Error('the page asked for ' + selectorText); },
    };
    return new Function('window', 'document', block + '\nreturn ANTS_PAGE;')(win, doc);
}

try {
    const P = loadShell();
    check('the game page has the whitelist', typeof P.localArguments === 'function');

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // the lobby: the form's meaning
    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    for (const n of [1, 2, 3, 4]) same('playersChoice: "' + n + '" is ' + n, L.playersChoice(String(n), 9), n);
    for (const bad of ['0', '5', '', ' 1', '1 ', '01', '1.0', '+1', 'one', 'x', null, undefined, 1, 2.5, {}, [], '1,2', '١']) same('playersChoice: ' + JSON.stringify(bad) + ' is the fallback', L.playersChoice(bad, 7), 7);
    same('playersChoice: the fallback is what the caller says (the form starts with 1, an address that names no players hosts 4)', [L.playersChoice(null, 1), L.playersChoice('9', 4)], [1, 4]);
    same('soloSeatsText: the three words, none for none (what the settings and the address hold)', [L.soloSeatsText(['easy', '', 'hard']), L.soloSeatsText(['', '', '']), L.soloSeatsText(['medium', 'medium', 'medium']), L.soloSeatsText(['EASY', 'junk', 'Hard'])],
         ['easy,none,hard', 'none,none,none', 'medium,medium,medium', 'easy,none,hard']);
    const FFA = { value: 'ffa', text: 'Free for all' };
    const BLOCK = (map, seats, teams, leaderStart) => ({ map, seats, teams, leaderStart });          // (a room's create block as the page holds it)

    same('the page of a game on this computer is play.html', L.LOCAL_PAGE, 'play.html');
    same('localGameQuery: the whole choice', L.localGameQuery('treasure', 'medium', 'Bob', '16:9'), '?map=treasure&bots=medium&name=Bob&aspect=16%3A9'.replace('%3A', ':'));
    same('localGameQuery: no opponents leaves the parameter out (alone)', L.localGameQuery('tiny', '', 'Bob', '16:9'), '?map=tiny&name=Bob&aspect=16:9');
    same('localGameQuery: every level', ['easy', 'medium', 'hard'].map((l) => L.localGameQuery('small', l, 'Bob', '4:3')), ['easy', 'medium', 'hard'].map((l) => '?map=small&bots=' + l + '&name=Bob&aspect=4:3'));
    same('localGameQuery: a level that is none of the three words is left out', [L.localGameQuery('small', 'none', 'Bob', '16:9'), L.localGameQuery('small', 'MEDIUM2', 'Bob', '16:9'), L.localGameQuery('small', undefined, 'Bob', '16:9')],
         ['?map=small&name=Bob&aspect=16:9', '?map=small&name=Bob&aspect=16:9', '?map=small&name=Bob&aspect=16:9']);
    same('localGameQuery: a level in capitals is the lower case word that was tested', L.localGameQuery('small', 'HARD', 'Bob', '16:9'), '?map=small&bots=hard&name=Bob&aspect=16:9');
    same('localGameQuery: a key that is no map is Treasure (the default of everything)', [L.localGameQuery('nowhere', '', 'Bob', '16:9'), L.localGameQuery('', '', 'Bob', '16:9'), L.localGameQuery(undefined, '', 'Bob', '16:9'), L.localGameQuery('TREASURE', '', 'Bob', '16:9')],
         ['?map=treasure&name=Bob&aspect=16:9', '?map=treasure&name=Bob&aspect=16:9', '?map=treasure&name=Bob&aspect=16:9', '?map=treasure&name=Bob&aspect=16:9']);
    same('localGameQuery: an empty name is Player', [L.localGameQuery('tiny', '', '', '16:9'), L.localGameQuery('tiny', '', undefined, '16:9')], ['?map=tiny&name=Player&aspect=16:9', '?map=tiny&name=Player&aspect=16:9']);
    same('localGameQuery: the shape is 16:9 or 4:3 and nothing else', [L.localGameQuery('tiny', '', 'B', '4:3'), L.localGameQuery('tiny', '', 'B', '21:9'), L.localGameQuery('tiny', '', 'B', undefined), L.localGameQuery('tiny', '', 'B', '4:3&x=1')],
         ['?map=tiny&name=B&aspect=4:3', '?map=tiny&name=B&aspect=16:9', '?map=tiny&name=B&aspect=16:9', '?map=tiny&name=B&aspect=16:9']);
    {
        const q = (levels) => L.localGameQuery('small', levels, 'Bob', '16:9');
        const tail = '&name=Bob&aspect=16:9';
        same('localGameQuery: three levels are three words, seat by seat, none for a base that has no bot', [q(['easy', 'medium', 'hard']), q(['hard', '', 'easy']), q(['', '', 'hard']), q(['easy', '', ''])],
             ['easy,medium,hard', 'hard,none,easy', 'none,none,hard', 'easy,none,none'].map((w) => '?map=small&bots=' + w + tail));
        same('localGameQuery: three of one level are that one word (the address of the first versions), no bot at all leaves &bots= out', [q(['medium', 'medium', 'medium']), q(['', '', '']), q([]), q(undefined), q('')],
             ['?map=small&bots=medium' + tail, '?map=small' + tail, '?map=small' + tail, '?map=small' + tail, '?map=small' + tail]);
        same('localGameQuery: a word that is no level is none, in a list too; a list of another length is read as far as it goes', [q(['EASY', 'junk', 'Hard']), q(['easy']), q(['easy', 'hard'])],
             ['easy,none,hard', 'easy,none,none', 'easy,hard,none'].map((w) => '?map=small&bots=' + w + tail));
        same('localGameQuery: the page offers no teams for a game on this computer any more: a fifth argument makes no &teams=', [L.localGameQuery('small', ['easy', 'medium', 'hard'], 'Bob', '16:9', '0+2'), L.localGameQuery('small', 'medium', 'Bob', '16:9', '0+1')],
             ['?map=small&bots=easy,medium,hard' + tail, '?map=small&bots=medium' + tail]);
    }
    {
        const evil = 'Ann & <b>Bob</b>?=#%';
        const q = L.localGameQuery('tiny', 'easy', evil, '16:9');
        check('localGameQuery: a name goes in URL-encoded: no raw < > & ? = # in it, and it comes back whole', !/[<>]/.test(q) && q.split('&').length === 4 && new URLSearchParams(q).get('name') === evil, q);
        check('localGameQuery: the query is never a game of the server (no join, room, seat, embed)', !/[?&](join|room|seat|embed|fill)=/.test(L.localGameQuery('tiny', 'easy', 'x&join=/ws&room=a', '16:9')), L.localGameQuery('tiny', 'easy', 'x&join=/ws&room=a', '16:9'));
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // the game page: what the local parameters become
    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    const FILES = { tiny: 'TINY.LVL', small: 'SMALL.LVL', medium: 'MEDIUM.LVL', gauntlet: 'GAUNTLET.LVL', treasure: 'TREASURE.LVL', islands: 'ISLANDS.LVL' };
    const bots = (level) => [1, 2, 3].flatMap((seat) => ['--bot', seat + ':' + level]);
    const local = (search, remembered) => P.localArguments(search, remembered).args;
    same('the whole of Play: the map, the setup screen\'s own start, the bots of the other three bases, the name', local('?map=treasure&bots=medium&name=Bob&aspect=16:9'),
         ['--map', 'Original-Ants/Maps/TREASURE.LVL', '--play', ...bots('medium'), '--name', 'Bob']);
    same('alone (no bots): the map, the start, --alone (a game for one: no colony but the player\'s) and the name; no --bot at all', local('?map=islands&name=Bob'), ['--map', 'Original-Ants/Maps/ISLANDS.LVL', '--play', '--alone', '--name', 'Bob']);
    for (const [key, file] of Object.entries(FILES)) {
        same('the map ' + key + ' is ' + file, local('?map=' + key).slice(0, 3), ['--map', 'Original-Ants/Maps/' + file, '--play']);
        check('... and that file is in the repository (the game will find it in its data)', fs.existsSync(path.join(repo, 'Original-Ants', 'Maps', file)), file);
    }
    same('the key is read in any case', [local('?map=Treasure')[1], local('?map=TINY')[1]], ['Original-Ants/Maps/TREASURE.LVL', 'Original-Ants/Maps/TINY.LVL']);
    for (const level of ['easy', 'medium', 'hard']) same('the level ' + level + ' seats that bot in the three other bases (seats 1, 2, 3; the player is seat 0)', local('?map=small&bots=' + level).slice(3), bots(level));
    same('the level is read in any case and comes out as the lower case word', local('?map=small&bots=HARD').slice(3), bots('hard'));
    same('bots with no map: the setup screen with those bots (the player chooses the map there)', local('?bots=easy'), bots('easy'));

    // one bot per seat: three words for seats 1, 2, 3 (none is a base with no bot), any case; the arguments name only the seats that have a bot
    const botArgs = (levels) => levels.flatMap((l, i) => (l ? ['--bot', (i + 1) + ':' + l] : []));
    for (const [text, levels] of [['easy,medium,hard', ['easy', 'medium', 'hard']], ['hard,none,easy', ['hard', '', 'easy']], ['none,none,hard', ['', '', 'hard']], ['none,easy,none', ['', 'easy', '']], ['EASY,Medium,HARD', ['easy', 'medium', 'hard']],
                                  ['medium,medium,medium', ['medium', 'medium', 'medium']], ['None,NONE,none', ['', '', '']], ['easy,none,none', ['easy', '', '']]]) {
        same('bots=' + text + ': each base has its own bot (seat 1 Red, 2 Blue, 3 Black), and no --bot for a none (all none: a game for one)', local('?map=small&bots=' + text).slice(3), levels.some(Boolean) ? botArgs(levels) : ['--alone']);
    }
    same('three words and no map: the setup screen with those bots', local('?bots=easy,none,hard'), botArgs(['easy', '', 'hard']));
    same('what the page keeps of the bots, for whoever reads it: one word when all three are alike, else the three words, empty for none',
         ['?bots=hard', '?bots=hard,hard,hard', '?bots=easy,none,hard', '?bots=none,none,none', '?bots=none', '?bots=junk', '?bots=easy,junk,hard'].map((t) => P.localArguments(t, '').bots), ['hard', 'hard', 'easy,none,hard', '', '', '', '']);
    // all or nothing: a text that is not one level word or exactly three words of the four is no bots at all (not the words that happen to be right)
    for (const bad of ['easy,medium', 'easy,medium,hard,easy', 'easy,,hard', ',,', ',easy,medium', 'easy,medium,', 'easy,extreme,hard', 'easy, medium,hard', 'easy,medium ,hard', 'easy;medium;hard', 'easy|medium|hard', 'easy medium hard', 'easy,medium,hard\n',
                      'none', 'none,none', 'none,easy', 'easy,hard,constructor', 'easy,medium,__proto__', 'easy,medium,hard,', 'EASY,MEDIUM,HARD,', ',', 'easy,medium,hard,none', '--bot 1:easy', 'easy,medium,hard --name x']) {
        same('bots=' + JSON.stringify(bad) + ' is no bots at all: a game for one', local('?map=small&bots=' + encodeURIComponent(bad)), ['--map', 'Original-Ants/Maps/SMALL.LVL', '--play', '--alone']);
    }

    // the teams: ffa (the game's default) says nothing; 0+N says --teams 0+N when seat N has a bot (the + comes as a blank unless it is %2B); anything else is ignored
    same('teams=0+1 with bots in seats 1 and 2: the game is told to make the teams (%2B)', local('?map=small&bots=easy,medium,none&teams=0%2B1').slice(3), ['--bot', '1:easy', '--bot', '2:medium', '--teams', '0+1']);
    same('... a + that came as a blank is the same', local('?map=small&bots=easy,medium,none&teams=0+2').slice(3), ['--bot', '1:easy', '--bot', '2:medium', '--teams', '0+2']);
    same('... and with the bots of one word', local('?map=small&bots=hard&teams=0%2B3').slice(3), [...bots('hard'), '--teams', '0+3']);
    same('... the teams stand before the name', local('?map=small&bots=easy,medium,hard&teams=0%2B2&name=Bob').slice(3), [...botArgs(['easy', 'medium', 'hard']), '--teams', '0+2', '--name', 'Bob']);
    same('what the page keeps of the teams', ['?bots=easy,medium,hard&teams=0%2B2', '?bots=easy,medium,hard&teams=0+3', '?bots=easy,medium,hard&teams=ffa', '?bots=easy,medium,none&teams=0%2B3', '?teams=0%2B1'].map((t) => P.localArguments(t, '').teams), ['0+2', '0+3', '', '', '']);
    for (const text of ['ffa', 'FFA', 'Ffa']) same('teams=' + text + ' is free for all: no --teams', local('?map=small&bots=easy,medium,hard&teams=' + text).slice(3), botArgs(['easy', 'medium', 'hard']));
    same('teams for a seat with no bot is ignored (the game would be told about a seat that is empty)', [local('?map=small&bots=easy,medium,none&teams=0%2B3').slice(3), local('?map=small&bots=none,none,hard&teams=0%2B1').slice(3)], [botArgs(['easy', 'medium', '']), botArgs(['', '', 'hard'])]);
    same('teams with no bots at all (or with bots that are no bots) is ignored: a game for one, no --teams', [local('?map=small&teams=0%2B1').slice(3), local('?map=small&bots=junk&teams=0%2B1').slice(3), local('?map=small&bots=easy,medium&teams=0%2B1').slice(3)], [['--alone'], ['--alone'], ['--alone']]);
    for (const bad of ['', '0', '0+', '+1', '0+0', '0+4', '0+9', '1+2', '2+3', '1+0', '00+1', '0+11', '0++1', '0+1+2', ' 0+1', '0+1 ', '0 +1', '0+ 1', '0\t1', '0+1\n', '0+1\n--name x', '0+1;ls', '0+1&teams=ffa', 'a+b', '0+one', '0+\u0967', 'constructor', '__proto__', 'none', 'true']) {
        same('teams=' + JSON.stringify(bad.slice(0, 20)) + ' is no team: no --teams', local('?map=small&bots=easy,medium,hard&teams=' + encodeURIComponent(bad)).slice(3), botArgs(['easy', 'medium', 'hard']));
    }
    same('a repeated teams parameter: the first one', local('?map=small&bots=easy,medium,hard&teams=0%2B2&teams=0%2B3').slice(3), [...botArgs(['easy', 'medium', 'hard']), '--teams', '0+2']);

    // what is refused: no map argument, no bot argument; whatever is put in the address
    const NOT_MAPS = ['', 'nowhere', 'TREASURE.LVL', 'treasure.lvl', 'treasure ', ' treasure', '../treasure', '..%2F..%2Fetc%2Fpasswd', 'treasure;ls', 'treasure&x', 'constructor', '__proto__', 'hasOwnProperty', 'toString',
                      'valueOf', 'treasure\n--name x', 'treаsure', 'treasure'.repeat(500), '0', 'Original-Ants/Maps/TREASURE.LVL', '/etc/passwd', 'http://evil/x', 'null', 'undefined'];
    for (const bad of NOT_MAPS) same('a map "' + bad.slice(0, 24) + '" is no map: no --map, no --play', local('?map=' + encodeURIComponent(bad)), []);
    const NOT_LEVELS = ['', 'none', 'extreme', 'medium ', ' medium', 'medium\n', 'medium\n--name x', 'easy,hard', 'medium;--map x', 'medium --name x', 'mediu', 'mediumm', 'constructor', '__proto__', '1', '0', 'true', 'medium'.repeat(300)];
    for (const bad of NOT_LEVELS) same('a level "' + bad.slice(0, 24).replace(/\n/g, '\\n') + '" is no level: no --bot, a game for one', local('?map=small&bots=' + encodeURIComponent(bad)), ['--map', 'Original-Ants/Maps/SMALL.LVL', '--play', '--alone']);
    same('a repeated map parameter: the first one', local('?map=tiny&map=small')[1], 'Original-Ants/Maps/TINY.LVL');
    same('a map that is no text at all (an array in the query) is nothing', local('?map[]=tiny'), []);
    same('an address with no local parameter at all: nothing is given to the game (today\'s front page: the setup screen), whatever the browser remembered', [local('', 'Bob'), local('?aspect=4:3', 'Bob'), local('?x=1&y=2', 'Bob')], [[], [], []]);
    same('another page\'s parameters are not local ones (join, room, fill, seat, embed)', local('?join=/ws&room=abc&fill=hard&seat=2&embed=1'), []);

    // the name
    same('the name of the address', local('?map=tiny&name=Bob').slice(-2), ['--name', 'Bob']);
    same('the name is cut like the game\'s own: printable ASCII only, the blanks at both ends gone, 32 characters at most', [local('?map=tiny&name=' + encodeURIComponent('  Zoë Ann  ')).slice(-2), local('?map=tiny&name=' + 'x'.repeat(40)).slice(-2), local('?map=tiny&name=' + encodeURIComponent('a\tb\nc')).slice(-2)],
         [['--name', 'Zo Ann'], ['--name', 'x'.repeat(32)], ['--name', 'abc']]);
    same('a name with other characters than ASCII only is cleaned, and one of nothing but those is none', [local('?map=tiny&name=' + encodeURIComponent('名前')), local('?map=tiny&name=' + encodeURIComponent('   '))], [['--map', 'Original-Ants/Maps/TINY.LVL', '--play', '--alone'], ['--map', 'Original-Ants/Maps/TINY.LVL', '--play', '--alone']]);
    same('with no name in the address the name that this browser remembered is used (a reload keeps it: the page strips the name from its address bar)', local('?map=tiny&bots=easy', 'Maya').slice(-2), ['--name', 'Maya']);
    same('the address\'s name beats the remembered one, an empty address name (a name that was chosen: none) beats it too', [local('?map=tiny&name=Bob', 'Maya').slice(-2), local('?map=tiny&name=', 'Maya').indexOf('--name')], [['--name', 'Bob'], -1]);
    same('a remembered name is cleaned the same way', local('?map=tiny', 'Mäya   ').slice(-2), ['--name', 'Mya']);
    same('a remembered name that is no text is nothing', [local('?map=tiny', null).indexOf('--name'), local('?map=tiny', undefined).indexOf('--name'), local('?map=tiny', 5).indexOf('--name')], [-1, -1, -1]);
    same('the name alone (no map, no bots) is given to the game: the setup screen under that name', local('?name=Bob'), ['--name', 'Bob']);

    // what can reach the game: only the fixed table, the tested word, the cleaned name and flags
    {
        let wrong = 0;
        let sample = '';
        let teamed = 0;
        let listed = 0;
        const pool = ['treasure', 'tiny', 'nowhere', '../x', 'easy', 'medium', 'HARD', 'x', ' ', '--name', '--map', '--join-url', 'ws://evil/', '\n', '&', '=', '%00', '%0a', 'a b', 'é', '1', 'null',
                      'easy,none,hard', 'none,medium,none', 'None,NONE,Easy', 'easy,medium', ',,', '0+1', '0 2', '0+3', '0+9', 'ffa', '--teams', 'none'];
        let seed = 7;
        const rnd = (n) => { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return (seed >>> 0) % n; };       // (xorshift32: the product of the old multiplier lost its low bits in a double, and the table was hardly visited)
        const allowed = new Set(['--map', '--play', '--alone', '--bot', '--teams', '--name', ...Object.values(FILES).map((f) => 'Original-Ants/Maps/' + f), '1:easy', '2:easy', '3:easy', '1:medium', '2:medium', '3:medium', '1:hard', '2:hard', '3:hard', '0+1', '0+2', '0+3']);
        for (let i = 0; i < 20000; i++) {
            const parts = [];
            for (const key of ['map', 'bots', 'teams', 'name', 'join', 'room', 'fill', 'x']) if (rnd(3) !== 0) parts.push(key + '=' + encodeURIComponent(pool[rnd(pool.length)] + (rnd(4) === 0 ? pool[rnd(pool.length)] : '')));
            const search = '?' + parts.join('&');
            const got = local(search, rnd(2) ? 'Bob' : null);
            for (let k = 0; k < got.length; k++) {
                const prev = got[k - 1];
                const ok = allowed.has(got[k]) ? true : prev === '--name' && /^[\x20-\x7e]{1,32}$/.test(got[k]) && got[k] === got[k].trim();
                if (!ok) { wrong++; if (!sample) sample = search + ' -> ' + JSON.stringify(got); }
            }
            // the rules between the arguments: a seat has at most one bot, in the order of the seats; --teams comes once, with a seat that has a bot
            const seats = [];
            const teams = [];
            let alones = 0;
            let mapped = false;
            for (let k = 0; k < got.length; k++) {
                if (got[k] === '--name') k++;                                                  // (a name is whatever the rules above allow, even a word that looks like a flag)
                else if (got[k] === '--bot') seats.push(Number(got[++k][0]));
                else if (got[k] === '--teams') teams.push(got[++k]);
                else if (got[k] === '--alone') alones++;
                else if (got[k] === '--map') mapped = true;
            }
            const ordered = seats.every((n, k) => k === 0 || n > seats[k - 1]);
            const teamsOk = teams.length === 0 || (teams.length === 1 && /^0\+[1-3]$/.test(teams[0]) && seats.indexOf(Number(teams[0][2])) !== -1);
            const aloneOk = alones === (mapped && seats.length === 0 ? 1 : 0);                 // a game for one is a game of a map that has no bot, and only that
            if (!ordered || !teamsOk || !aloneOk) { wrong++; if (!sample) sample = search + ' -> ' + JSON.stringify(got); }
            if (teams.length) teamed++;
            if (seats.length && seats.length < 3 || (seats.length === 3 && new Set(got.filter((a) => /^\d:/.test(a)).map((a) => a.slice(2))).size > 1)) listed++;
        }
        check('the random addresses do reach the teams and the lists of bots (so the rules above are not empty): ' + teamed + ' with --teams, ' + listed + ' with bots of different levels or seats', teamed > 20 && listed > 200);
        check('20000 random addresses: every argument that comes out is a flag of the table, a file of the six maps, a tested bot seat (once, in order), a team of the player and a seat that has a bot, or the cleaned name after --name (' + sample + ')', wrong === 0);
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // the two pages agree
    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    same('the lobby offers the six shipped maps, by the keys of the game page', L.MAPS.map((m) => m.key).sort(), Object.keys(FILES).sort());
    {
        let wrong = 0;
        let sample = '';
        for (const m of L.MAPS) {
            for (const level of ['', 'easy', 'medium', 'hard']) {
                for (const name of ['', 'Bob', 'Ann & <b>Bob</b>', 'x'.repeat(32), 'with space', 'A=B&C=D']) {
                    for (const shape of ['16:9', '4:3']) {
                        const query = L.localGameQuery(m.key, level, name, shape);
                        const args = local(query);
                        const want = ['--map', 'Original-Ants/Maps/' + FILES[m.key], '--play', ...(level ? bots(level) : ['--alone']), '--name', name || 'Player'];
                        if (JSON.stringify(args) !== JSON.stringify(want)) { wrong++; if (!sample) sample = query + ' -> ' + JSON.stringify(args); }
                        if (new URLSearchParams(query).get('aspect') !== shape) { wrong++; if (!sample) sample = query; }
                    }
                }
            }
        }
        check('every address that the lobby makes is read by the game page as the same map, the same bots in the other three bases and the same name (' + sample + ')', wrong === 0);
    }
    {   // one level for each seat: every choice (all 64 sets of levels, every map, name and shape) is read as the same bots in the same seats and the same name
        const WORDS = ['', 'easy', 'medium', 'hard'];
        let wrong = 0;
        let sample = '';
        let made = 0;
        for (const m of L.MAPS) {
            for (const a of WORDS) for (const b of WORDS) for (const c of WORDS) {
                const levels = [a, b, c];
                for (const name of ['', 'Bob', 'Ann & <b>Bob</b>', 'A=B&C=D']) {
                    for (const shape of ['16:9', '4:3']) {
                        const query = L.localGameQuery(m.key, levels, name, shape);
                        const args = local(query);
                        const want = ['--map', 'Original-Ants/Maps/' + FILES[m.key], '--play', ...(levels.some(Boolean) ? botArgs(levels) : ['--alone']), '--name', name || 'Player'];
                        made++;
                        if (JSON.stringify(args) !== JSON.stringify(want)) { wrong++; if (!sample) sample = query + ' -> ' + JSON.stringify(args); }
                        if (new URLSearchParams(query).get('aspect') !== shape) { wrong++; if (!sample) sample = query; }
                    }
                }
            }
        }
        check('every choice of levels that the lobby can make for a game on this computer (' + made + ' addresses) is read by the game page as the same bots in the same seats and the same name (' + sample + ')', wrong === 0);
        // the words that soloSeatsText makes of the three levels are read by the game page as the same bots in the same seats
        let drift = 0;
        for (const a of WORDS) for (const b of WORDS) for (const c of WORDS) {
            const text = L.soloSeatsText([a, b, c]);
            if (JSON.stringify(local('?bots=' + text)) !== JSON.stringify(botArgs([a, b, c]))) drift++;
        }
        check('the words of soloSeatsText are read by the game page as the same bots in the same seats (' + drift + ' differ)', drift === 0);
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // a room that an address asks for (network protocol 13; the test room's old addresses): a level for each seat after the leader's, and the room's teams
    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const N3 = ['', '', ''];
        same('the colours of the seats of a room are Green, Red, Blue, Black', L.SEAT_COLOURS, ['Green', 'Red', 'Blue', 'Black']);
        same('hostSeats: a first visit (nothing remembered) is none in all three seats: the empty seats stay empty until the leader chooses', [L.hostSeats(null), L.hostSeats(undefined), L.hostSeats('')], [N3, N3, N3]);
        same('hostSeats: four words are the levels of the seats 0 - 3 and the first (the leader\'s seat) counts for nothing; any case', [L.hostSeats('none,easy,none,hard'), L.hostSeats('hard,medium,medium,medium'), L.hostSeats('NONE,None,EASY,Hard'), L.hostSeats('none,none,none,none')],
             [['easy', '', 'hard'], ['medium', 'medium', 'medium'], ['', 'easy', 'hard'], N3]);
        same('hostSeats: one word is every seat (what the first versions of the page remembered); none and anything else is none', [L.hostSeats('easy'), L.hostSeats('HARD'), L.hostSeats('medium'), L.hostSeats('none'), L.hostSeats('junk')],
             [['easy', 'easy', 'easy'], ['hard', 'hard', 'hard'], ['medium', 'medium', 'medium'], N3, N3]);
        for (const bad of ['easy,hard', 'easy,hard,easy', 'none,easy,none,hard,none', 'none,easy,,hard', 'none, easy,none,hard', 'none,easy,none,loud', ',,,', 'none;easy;none;hard', 5, {}, ['easy', 'easy', 'easy', 'easy']]) {
            same('hostSeats: a remembered ' + JSON.stringify(bad) + ' is not one word or four: none in all three seats', L.hostSeats(bad), N3);
        }

        // what a room asks of START: nothing, one word when every seat that the room has gets the same level, else four words; the seats beyond the room are none
        same('hostFillText: no bots in the seats of the room is no plan at all', [L.hostFillText(N3, 4), L.hostFillText(N3, 2), L.hostFillText(['', '', 'hard'], 2), L.hostFillText(['', '', 'hard'], 3), L.hostFillText(undefined, 4)], ['', '', '', '', '']);
        same('hostFillText: the same level in every seat of the room is that one word (the address of the first versions), however many seats the room has', [L.hostFillText(['easy', 'easy', 'easy'], 4), L.hostFillText(['hard', 'hard', 'hard'], 3), L.hostFillText(['medium', 'junk', 'easy'], 2),
              L.hostFillText(['hard', 'hard', 'easy'], 3)], ['easy', 'hard', 'medium', 'hard']);
        same('hostFillText: any other plan is four words for the seats 0 - 3, the leader\'s seat none and the seats beyond the room none', [L.hostFillText(['easy', '', 'hard'], 4), L.hostFillText(['easy', 'hard', 'medium'], 4), L.hostFillText(['easy', '', 'hard'], 3), L.hostFillText(['', 'hard', ''], 4), L.hostFillText(['easy', 'hard', 'easy'], 3)],
             ['none,easy,none,hard', 'none,easy,hard,medium', 'none,easy,none,none', 'none,none,hard,none', 'none,easy,hard,none']);
        // the words of the room panel
        same('hostPlanText: one word is "Medium bots", four words name the seats that get a bot (the seats of the room only)', [L.hostPlanText('medium', 4), L.hostPlanText('none,easy,none,hard', 4), L.hostPlanText('none,easy,none,hard', 3), L.hostPlanText('none,easy,hard,medium', 4), L.hostPlanText('', 4)],
             ['Medium bots', 'Red Easy, Black Hard', 'Red Easy', 'Red Easy, Blue Hard, Black Medium', '']);

        // the teams: a choice for each room size (the game's room_team_choices): none but free for all with two players
        same('hostTeamText: the pair first, then the other seats that play', [L.hostTeamText([0, 1], 4), L.hostTeamText([0, 2], 4), L.hostTeamText([0, 3], 4), L.hostTeamText([0, 1], 3), L.hostTeamText([0, 2], 3), L.hostTeamText([1, 2], 3)],
             ['Green + Red against Blue + Black', 'Green + Blue against Red + Black', 'Green + Black against Red + Blue', 'Green + Red against Blue', 'Green + Blue against Red', 'Red + Blue against Green']);
        same('hostTeamChoices: four players: free for all and Green with Red, Blue or Black', L.hostTeamChoices(4), [FFA, { value: '0+1', text: 'Green + Red against Blue + Black' }, { value: '0+2', text: 'Green + Blue against Red + Black' }, { value: '0+3', text: 'Green + Black against Red + Blue' }]);
        same('hostTeamChoices: three players: free for all and the pairs 0+1, 0+2 and 1+2 (the third seat plays alone)', L.hostTeamChoices(3), [FFA, { value: '0+1', text: 'Green + Red against Blue' }, { value: '0+2', text: 'Green + Blue against Red' }, { value: '1+2', text: 'Red + Blue against Green' }]);
        same('hostTeamChoices: two players: free for all only (a team of them would end the match at once)', [L.hostTeamChoices(2), L.hostTeamChoices(1), L.hostTeamChoices(undefined)], [[FFA], [FFA], [FFA]]);
        same('hostTeam: a choice of the room stays, a choice that the room does not offer (another size, a pair of seats that do not play) and anything that is no choice is free for all',
             [L.hostTeam('0+1', 4), L.hostTeam('0+3', 4), L.hostTeam('1+2', 3), L.hostTeam('1+2', 4), L.hostTeam('0+3', 3), L.hostTeam('0+1', 2), L.hostTeam('ffa', 4), L.hostTeam('0+4', 4), L.hostTeam('1+3', 4), L.hostTeam('', 4), L.hostTeam(null, 4), L.hostTeam('0 1', 4), L.hostTeam('constructor', 4)],
             ['0+1', '0+3', '1+2', 'ffa', 'ffa', 'ffa', 'ffa', 'ffa', 'ffa', 'ffa', 'ffa', 'ffa', 'ffa']);
        // the teams as the test room shows them (a block or an address can name a pair without Green; with four seats the room plays the same two teams as the pair of the other two seats, which has Green)
        same('shownRoomTeams: with four seats a pair without Green is shown as the pair of the other two seats (1+2 is 0+3, 1+3 is 0+2, 2+3 is 0+1); a pair with Green, free for all and any other seat count come back as they are',
             [L.shownRoomTeams('1+2', 4), L.shownRoomTeams('1+3', 4), L.shownRoomTeams('2+3', 4), L.shownRoomTeams('0+1', 4), L.shownRoomTeams('0+2', 4), L.shownRoomTeams('0+3', 4), L.shownRoomTeams('', 4), L.shownRoomTeams('1+2', 3), L.shownRoomTeams('1+2', 2), L.shownRoomTeams('0+3', 3), L.shownRoomTeams('junk', 4)],
             ['0+3', '0+2', '0+1', '0+1', '0+2', '0+3', '', '1+2', '1+2', '0+3', 'junk']);
        {   // every pair that an address can name for four seats is, as shown, one of the four choices of the room (the Green pairs)
            let bad = '';
            for (let a = 0; a < 4; a++) for (let b = a + 1; b < 4; b++) {
                const pair = a + '+' + b;
                const shown = L.shownRoomTeams(L.validRoomTeams(pair), 4);
                if (L.hostTeam(shown, 4) !== shown || L.hostTeamChoices(4).every((c) => c.value !== shown)) bad += ' ' + pair + ' -> ' + shown;
            }
            check('shownRoomTeams: every pair of seats that an address can name for a room of four is shown as one of the room\'s four choices:' + bad, bad === '');
        }

        // a room code as a screen shows it (two groups of three) is Rules.codeText (front/lobby_rules.js, pinned by tests/scripts/web_lobby_rules_check.js); what is held here is that the game page says the same
        {   // ANTS_PAGE.codeText and the rules' codeText say the same on every kind of text
            const TEXTS = ['k7m2xq', 'K7M2XQ', 'abcdef', 'abcdefgh', 'A_b-12', 'A_b-1234', 'mid-room', 'k7m2xq9', 'k7m2xq9pz', 'k7m 2xq', 'a', 'x'.repeat(32), 'demo-small-2p-x7k2', '', null, undefined, 5, {}, ['k7m2xq']];
            const differ = TEXTS.filter((t) => P.codeText(t) !== Rules.codeText(t));
            check('the game page shows a code as the rules\' codeText does (six characters in two groups of three, any other text as it is, no text as nothing) (' + differ.length + ' of ' + TEXTS.length + ' differ)', differ.length === 0);
            same('the game page\'s codeText: "k7m2xq" is "k7m 2xq", five, seven, eight and nine characters stay as they are, a value that is no text is nothing', [P.codeText('k7m2xq'), P.codeText('abcde'), P.codeText('k7m2xq9'), P.codeText('abcdefgh'), P.codeText('k7m2xq9pz'), P.codeText(null)], ['k7m 2xq', 'abcde', 'k7m2xq9', 'abcdefgh', 'k7m2xq9pz', '']);
        }

        // the create block of a room (protocol 15): the text that every link of the room carries after the code, and what an address's block says
        same('roomBlockQuery: the map and the seats, then the teams and the leader-starts flag when they are set (the + of the teams is encoded: an address reads a bare one as a blank)',
             [L.roomBlockQuery(BLOCK('treasure', 4, '', false)), L.roomBlockQuery(BLOCK('small', 2, '', false)), L.roomBlockQuery(BLOCK('tiny', 3, '0+2', false)), L.roomBlockQuery(BLOCK('islands', 4, '', true)), L.roomBlockQuery(BLOCK('gauntlet', 4, '1+3', true))],
             ['&roommap=treasure&roomseats=4', '&roommap=small&roomseats=2', '&roommap=tiny&roomseats=3&roomteams=0%2B2', '&roommap=islands&roomseats=4&roomleaderstart=1', '&roommap=gauntlet&roomseats=4&roomteams=1%2B3&roomleaderstart=1']);
        same('roomBlockQuery: a room that was made some other way has no block, and no text', [L.roomBlockQuery(null), L.roomBlockQuery(undefined), L.roomBlockQuery(''), L.roomBlockQuery(5), L.roomBlockQuery(false), L.roomBlockQuery('map')], ['', '', '', '', '', '']);
        same('roomBlockQuery: a block with no map has no &roommap= (the server picks its own); a part that is no good is the default one, and never goes into a link as it came',
             [L.roomBlockQuery(BLOCK('', 3, '', false)), L.roomBlockQuery(BLOCK('nowhere', 3, '', false)), L.roomBlockQuery(BLOCK('&room=x', 3, '', false)), L.roomBlockQuery(BLOCK(undefined, 3, '', false)),
              L.roomBlockQuery(BLOCK('tiny', 7, '', false)), L.roomBlockQuery(BLOCK('tiny', '3', '', false)), L.roomBlockQuery(BLOCK('tiny', undefined, '', false)), L.roomBlockQuery(BLOCK('tiny', 1, '', false)),
              L.roomBlockQuery(BLOCK('tiny', 4, 'ffa', false)), L.roomBlockQuery(BLOCK('tiny', 4, '0+0', false)), L.roomBlockQuery(BLOCK('tiny', 4, '0+1&x=1', false)), L.roomBlockQuery(BLOCK('tiny', 4, null, false)),
              L.roomBlockQuery(BLOCK('tiny', 4, '', 'yes')), L.roomBlockQuery(BLOCK('tiny', 4, '', 1)), L.roomBlockQuery(BLOCK('tiny', 4, '', undefined)), L.roomBlockQuery({ map: 'tiny' })],
             ['&roomseats=3', '&roomseats=3', '&roomseats=3', '&roomseats=3',
              '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4',
              '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4',
              '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4']);
        // the rules' mapByKey takes a key in any case (the game page's whitelist does too), so a block that says 'Treasure' names a map; what goes into a link is the tested lower case key, never the text that came
        same('roomBlockQuery: a map key in any case names that map, and the link carries the tested lower case key (as roomBlockOf reads it back), never the caller\'s text', [L.roomBlockQuery(BLOCK('Treasure', 3, '', false)), L.roomBlockQuery(BLOCK('TINY', 2, '', false)), L.roomBlockQuery(BLOCK('Islands', 4, '0+1', true))],
             ['&roommap=treasure&roomseats=3', '&roommap=tiny&roomseats=2', '&roommap=islands&roomseats=4&roomteams=0%2B1&roomleaderstart=1']);
        same('roomBlockQuery: a pair of seats the wrong way round is written with the lower seat first (a pair has one spelling, as in the block)', L.roomBlockQuery(BLOCK('tiny', 4, '3+1', false)), '&roommap=tiny&roomseats=4&roomteams=1%2B3');
        same('the six keys are the six map files by the game\'s rule for --room-map (the word in capitals and .LVL)', L.MAPS.map((m) => m.key.toUpperCase() + '.LVL').sort(), Object.values(FILES).sort());
        same('roomBlockOf: the block of the page\'s own links is read back (the key in any case, the seats 2 - 4, the teams as A+B, a + that came as a blank, the flag exactly 1)',
             [L.roomBlockOf('?join=/ws&room=k7m2xq&roommap=treasure&roomseats=4'), L.roomBlockOf('?room=k7m2xq&roommap=TINY&roomseats=2'), L.roomBlockOf('?room=k7m2xq&roommap=Islands&roomseats=3&roomteams=0%2B2'), L.roomBlockOf('?room=k7m2xq&roommap=small&roomseats=3&roomteams=1+2'),
              L.roomBlockOf('?room=k7m2xq&roommap=gauntlet&roomseats=4&roomleaderstart=1&fill=easy&teams=0%2B1&name=Bob'), L.roomBlockOf('roommap=medium&roomseats=2')],
             [BLOCK('treasure', 4, '', false), BLOCK('tiny', 2, '', false), BLOCK('islands', 3, '0+2', false), BLOCK('small', 3, '1+2', false), BLOCK('gauntlet', 4, '', true), BLOCK('medium', 2, '', false)]);
        same('roomBlockOf: what an address leaves out is the server\'s own: no map, four seats, free for all, not the leader\'s start (any one of the four makes the block)',
             [L.roomBlockOf('?room=a&roomseats=3'), L.roomBlockOf('?room=a&roommap=gauntlet'), L.roomBlockOf('?room=a&roomteams=0%2B3'), L.roomBlockOf('?room=a&roomleaderstart=1'), L.roomBlockOf('?room=a&roommap=nowhere&roomseats=3'), L.roomBlockOf('?room=a&roommap=small&roomseats=9')],
             [BLOCK('', 3, '', false), BLOCK('gauntlet', 4, '', false), BLOCK('', 4, '0+3', false), BLOCK('', 4, '', true), BLOCK('', 3, '', false), BLOCK('small', 4, '', false)]);
        same('roomBlockOf: an address with none of the four parameters has no block: the room was made some other way (null)',
             ['', '?', '?room=k7m2xq', '?room=k7m2xq&fill=easy&teams=0%2B1', '?join=/ws&room=abc&seat=1&name=Bob', '?map=small&players=2', '?platform=linux'].map((s) => L.roomBlockOf(s)), Array(7).fill(null));
        for (const bad of ['?roommap=', '?roommap=nowhere', '?roommap=TREASURE.LVL', '?roommap=treasure.lvl', '?roommap=../treasure', '?roommap=treasure%20', '?roommap=constructor', '?roommap=__proto__', '?roommap=tiny,small', '?roomseats=', '?roomseats=0', '?roomseats=1', '?roomseats=5', '?roomseats=03', '?roomseats=%203',
                           '?roomseats=2.0', '?roomseats=two', '?roomseats=%D9%A2', '?roomteams=', '?roomteams=ffa', '?roomteams=0%2B0', '?roomteams=0%2B4', '?roomteams=0%2B1%2B2', '?roomteams=01', '?roomleaderstart=', '?roomleaderstart=0', '?roomleaderstart=true', '?roomleaderstart=11', '?roomleaderstart=%201',
                           '?roommap=nowhere&roomseats=9&roomteams=ffa&roomleaderstart=0']) {
            same('roomBlockOf: ' + bad + ' says nothing that is a block part: no block', L.roomBlockOf(bad), null);
        }
        same('roomBlockOf: the first of a repeated parameter counts, as it does for the game page', L.roomBlockOf('?roommap=tiny&roommap=small&roomseats=3&roomseats=2'), BLOCK('tiny', 3, '', false));
        same('roomBlockOf: an address that is no text at all has no block', [L.roomBlockOf(null), L.roomBlockOf(undefined), L.roomBlockOf(5), L.roomBlockOf({}), L.roomBlockOf(['roommap'])], Array(5).fill(null));
        {   // every block that the page can put into a link, through the game page's whitelist: the same --room-map, --room-seats, --room-teams and --room-leader-start, and read back whole by the page
            let blocks = 0;
            let wrong = '';
            let withTeams = 0;
            let flagged = 0;
            for (const m of L.MAPS) for (const seats of [2, 3, 4]) for (const choice of L.hostTeamChoices(seats)) for (const leaderStart of [false, true]) {
                const teams = choice.value === 'ffa' ? '' : choice.value;
                const block = BLOCK(m.key, seats, teams, leaderStart);
                const query = L.roomBlockQuery(block);
                const address = '?join=/ws&room=k7m2xq' + query + '&seat=0&name=Bob';
                const args = P.joinArguments(address, true, 'play.test').args;
                const want = ['--join-url', 'wss://play.test/ws', '--room', 'k7m2xq', '--room-map', m.key, '--room-seats', String(seats), ...(teams ? ['--room-teams', teams] : []), ...(leaderStart ? ['--room-leader-start'] : []), '--seat', '0', '--name', 'Bob'];
                blocks++;
                if (teams) withTeams++;
                if (leaderStart) flagged++;
                if (JSON.stringify(args) !== JSON.stringify(want)) wrong += ' ' + address + ' -> ' + JSON.stringify(args);
                if (JSON.stringify(L.roomBlockOf(address)) !== JSON.stringify(block)) wrong += ' (not read back) ' + address;
                if (L.hostTeam(teams || 'ffa', seats) !== (teams || 'ffa') || query.indexOf('+') !== -1) wrong += ' (not a block that the page can make) ' + address;
            }
            check('every create block that the page can put into a link (' + blocks + ': ' + withTeams + ' with teams, ' + flagged + ' that wait for the leader) is read by the game page as the same --room-map, --room-seats, --room-teams and --room-leader-start, and by the page as the same block:' + wrong,
                  wrong === '' && blocks === 6 * 9 * 2 && withTeams === 6 * 6 * 2 && flagged === 6 * 9);
        }

        // what an address may say
        same('validFillPlan: one level in any case, or four words (none for none): the tested lower case text; no bots at all and anything else is ""',
             [L.validFillPlan('easy'), L.validFillPlan('MEDIUM'), L.validFillPlan('none,easy,none,hard'), L.validFillPlan('NONE,Easy,None,HARD'), L.validFillPlan('hard,hard,hard,hard'), L.validFillPlan('none'), L.validFillPlan('none,none,none,none'), L.validFillPlan(''), L.validFillPlan(null)],
             ['easy', 'medium', 'none,easy,none,hard', 'none,easy,none,hard', 'hard,hard,hard,hard', '', '', '', '']);
        for (const bad of ['easy,hard', 'easy,hard,easy', 'none,easy,none,hard,none', 'none,easy,,hard', 'none, easy,none,hard', 'none,easy,none,loud', ',,,', ',', 'none;easy;none;hard', 'easy hard', 'none,easy,none,hard\n', 'none,easy,none,hard&seat=1', 'none,easy,none,--room x', 'none,easy,none,hard,', 5, {}, ['easy'], 'hard,'.repeat(2000)]) {
            same('validFillPlan: ' + JSON.stringify(bad).slice(0, 40) + ' is no plan', L.validFillPlan(bad), '');
        }
        same('validRoomTeams: two different seats 0 - 3 as A+B, the lower seat first (a + that came as a blank too): the tested text', [L.validRoomTeams('0+1'), L.validRoomTeams('0 1'), L.validRoomTeams('1+2'), L.validRoomTeams('3+0'), L.validRoomTeams('2 3'), L.validRoomTeams('1+0'), L.validRoomTeams('3 1')], ['0+1', '0+1', '1+2', '0+3', '2+3', '0+1', '1+3']);
        for (const bad of ['', 'ffa', 'FFA', '0+0', '2+2', '0+4', '4+0', '01', '0++1', '0+1+2', ' 0+1', '0+1 ', '0 +1', '0+ 1', '0\t1', '0+1\n', '0+1&teams=ffa', 'a+b', '0+१', '0+１', 'constructor', null, undefined, 1, {}, ['0+1']]) {
            same('validRoomTeams: ' + JSON.stringify(bad) + ' is no team', L.validRoomTeams(bad), '');
        }

        // the two pages agree for rooms: whatever the old addresses make of their levels and their team (a room of 2, 3 or 4 players, every set of three levels, every team that the room offers), the game page
        // reads the address (&fill= &teams=) as the same plan, in the same seats, and the same teams for the game; and the plan means what the address chose for the seats that the room has
        const WORDS = ['', 'easy', 'medium', 'hard'];
        let wrong = 0;
        let sample = '';
        let made = 0;
        let planned = 0;
        let teamed = 0;
        const planLevels = (text) => { const w = text.split(','); return w.length === 1 ? [w[0], w[0], w[0], w[0]] : w.map((x) => (x === 'none' ? '' : x)); };       // the game's parse_fill_plan: one word is every seat, four words are the seats 0 - 3
        for (const players of [2, 3, 4]) {
            for (const a of WORDS) for (const b of WORDS) for (const c of WORDS) {
                const levels = [a, b, c];
                const plan = L.hostFillText(levels, players);
                for (const choice of L.hostTeamChoices(players)) {
                    const team = choice.value === 'ffa' ? '' : choice.value;
                    const address = '?join=/ws&room=k7m2xq' + (plan ? '&fill=' + plan : '') + (team ? '&teams=' + encodeURIComponent(team) : '') + '&name=Bob';
                    const args = P.joinArguments(address, true, 'play.test').args;
                    const fillAt = args.indexOf('--fill-bots');
                    const teamsAt = args.indexOf('--teams');
                    made++;
                    if ((fillAt === -1) !== (plan === '') || (fillAt !== -1 && args[fillAt + 1] !== plan)) { wrong++; if (!sample) sample = address + ' -> ' + JSON.stringify(args); }
                    if ((teamsAt === -1) !== (team === '') || (teamsAt !== -1 && args[teamsAt + 1] !== team)) { wrong++; if (!sample) sample = address + ' -> ' + JSON.stringify(args); }
                    if (plan) {
                        planned++;
                        const got = planLevels(L.validFillPlan(plan));                           // (what the game seats: the seats after the leader's that the room has)
                        for (let seat = 1; seat < players; seat++) if (got[seat] !== levels[seat - 1]) { wrong++; if (!sample) sample = address + ' seat ' + seat + ' -> ' + got[seat]; }
                        if (got[0] !== '' && !(plan.split(',').length === 1)) { wrong++; if (!sample) sample = address + ' the leader\'s seat has a level'; }
                    }
                    if (team) teamed++;
                    if (L.validFillPlan(plan) !== plan || L.validRoomTeams(team) !== team || L.hostTeam(team || 'ffa', players) !== (team || 'ffa')) { wrong++; if (!sample) sample = address + ' is not read back whole'; }
                }
            }
        }
        check('every address of a room that the old addresses of the test room can make (' + made + ': ' + planned + ' with bots, ' + teamed + ' with teams) is read by the game page as the same --fill-bots plan and the same --teams, the plan means the chosen level in every seat that the room has, and the lobby reads it back whole (' + sample + ')', wrong === 0 && planned > 100 && teamed > 100);
        // the whitelist of the game page: a plan or a team that is not what the page makes never reaches the game's arguments
        {
            const rooms = (search) => P.joinArguments('?join=/ws&room=abc' + search, true, 'play.test').args;
            same('the game page passes the plan and the teams of a room: --fill-bots, --teams (the + of an address arrives as a blank: both are read)', [rooms('&fill=none,easy,none,hard&teams=0%2B2'), rooms('&fill=EASY&teams=1+2')],
                 [['--join-url', 'wss://play.test/ws', '--room', 'abc', '--fill-bots', 'none,easy,none,hard', '--teams', '0+2'], ['--join-url', 'wss://play.test/ws', '--room', 'abc', '--fill-bots', 'easy', '--teams', '1+2']]);
            for (const bad of ['easy,hard', 'none,none,none,none', 'none,easy,none', 'easy;--room x', 'none,easy,none,hard,none', 'none, easy,none,hard']) same('the game page: a plan ' + JSON.stringify(bad) + ' is no --fill-bots', rooms('&fill=' + encodeURIComponent(bad)), ['--join-url', 'wss://play.test/ws', '--room', 'abc']);
            for (const bad of ['ffa', '0+0', '0+4', '1+1', '01', '0++1', 'a+b', '0+1+2', '0+1;ls', '0+1\n--name x', '']) same('the game page: teams ' + JSON.stringify(bad) + ' is no --teams', rooms('&teams=' + encodeURIComponent(bad)), ['--join-url', 'wss://play.test/ws', '--room', 'abc']);
            same('without a join nothing about a plan or teams goes to the game', [P.joinArguments('?fill=easy&teams=0%2B1', true, 'play.test').args, P.joinArguments('?room=abc&fill=hard&teams=0%2B1', true, 'play.test').args], [[], []]);
            // the number of people that the leader's game waits for before it presses START (the front page's card: &start=<1 + its Friend rows>): one digit 1 - 4 becomes --start-when, after the teams
            const base = ['--join-url', 'wss://play.test/ws', '--room', 'abc'];
            same('the game page passes &start= (one digit 1 - 4) as --start-when, after the plan and the teams', [rooms('&start=1'), rooms('&start=4'), rooms('&fill=none,easy,none,hard&teams=0%2B2&start=3')],
                 [[...base, '--start-when', '1'], [...base, '--start-when', '4'], [...base, '--fill-bots', 'none,easy,none,hard', '--teams', '0+2', '--start-when', '3']]);
            for (const bad of ['', '0', '5', '9', '12', '01', 'two', ' 2', '2 ', '2\n--name x', '2;ls', '2&seat=1', '-1', '2.5', '\u0662']) same('the game page: &start=' + JSON.stringify(bad) + ' is no --start-when', rooms('&start=' + encodeURIComponent(bad)), base);
            same('... and without a join nothing about start goes to the game', [P.joinArguments('?start=2', true, 'play.test').args, P.joinArguments('?room=abc&start=2', true, 'play.test').args], [[], []]);
            // the room's create block (protocol 15: --room-map, --room-seats, --room-teams, --room-leader-start) and the platform word (--platform): each through its own whitelist, right after the room
            same('the game page passes the create block of a room, right after the room: the map by its key (in any case: the tested lower case key comes out), the seats 2 - 4, the teams (a + that came as a blank is read too) and the leader-starts flag',
                 [rooms('&roommap=treasure&roomseats=4'), rooms('&roommap=Treasure&roomseats=3&roomteams=0%2B2'), rooms('&roommap=ISLANDS&roomseats=2&roomleaderstart=1'), rooms('&roomteams=1+2'), rooms('&roomseats=4'), rooms('&roommap=gauntlet'), rooms('&roomleaderstart=1')],
                 [[...base, '--room-map', 'treasure', '--room-seats', '4'], [...base, '--room-map', 'treasure', '--room-seats', '3', '--room-teams', '0+2'], [...base, '--room-map', 'islands', '--room-seats', '2', '--room-leader-start'], [...base, '--room-teams', '1+2'], [...base, '--room-seats', '4'], [...base, '--room-map', 'gauntlet'], [...base, '--room-leader-start']]);
            same('... in the order that the game is given them whatever the order of the address: the door, the room, the map, the seats, the teams, the flag, the platform, the seat, the plan, the teams of START, the number to wait for and the name',
                 [rooms('&platform=Browser-Windows&roomleaderstart=1&roomteams=0%2B1&roomseats=4&roommap=small&seat=2&fill=easy&teams=1%2B2&start=3&name=Bob'), rooms('&name=Bob&start=3&teams=1%2B2&fill=easy&seat=2&platform=Browser-Windows&roomleaderstart=1&roomteams=0%2B1&roomseats=4&roommap=small')],
                 Array(2).fill([...base, '--room-map', 'small', '--room-seats', '4', '--room-teams', '0+1', '--room-leader-start', '--platform', 'browser-windows', '--seat', '2', '--fill-bots', 'easy', '--teams', '1+2', '--start-when', '3', '--name', 'Bob']));
            same('a frame of the front page (embed) still starts with the audio argument, then the door and the room and its block',
                 P.joinArguments('?join=/ws&room=abc&roommap=tiny&roomseats=2&embed=1&seat=0', true, 'play.test').args, ['--audio-focus', ...base, '--room-map', 'tiny', '--room-seats', '2', '--seat', '0']);
            same('the first of a repeated parameter counts', rooms('&roommap=tiny&roommap=small&roomseats=3&roomseats=2&platform=linux&platform=macos'), [...base, '--room-map', 'tiny', '--room-seats', '3', '--platform', 'linux']);
            for (const bad of ['', 'nowhere', 'TREASURE.LVL', 'treasure.lvl', 'treasure ', ' treasure', '../treasure', '..%2F..%2Fetc%2Fpasswd', 'treasure;ls', 'treasure&x', 'constructor', '__proto__', 'hasOwnProperty', 'treasure\n--name x', 'treаsure', 'İslands', 'ｔｉｎｙ', 'tiny,small', 'tiny small', 'treasure'.repeat(500), '0', 'null'])
                same('the game page: the map ' + JSON.stringify(bad.slice(0, 24)) + ' is no --room-map', rooms('&roommap=' + encodeURIComponent(bad)), base);
            for (const bad of ['', '0', '1', '5', '9', '10', '12', '02', '03', '-2', '+2', '2.0', '2.5', '1e1', '0x2', ' 2', '2 ', '2\n', '\n2', '2,3', '2;ls', '2&seat=1', '2 --room x', 'two', '٢', '２', 'constructor', '2'.repeat(2000)])
                same('the game page: ' + JSON.stringify(bad.slice(0, 24)) + ' is no --room-seats', rooms('&roomseats=' + encodeURIComponent(bad)), base);
            for (const bad of ['', 'ffa', 'FFA', 'none', '0+0', '1+1', '0+4', '4+0', '01', '0++1', 'a+b', '0+1+2', ' 0+1', '0+1 ', '0+1\n--name x', '0+1;ls', '0+1&teams=ffa', '0+१', 'constructor', '__proto__'])
                same('the game page: the teams ' + JSON.stringify(bad) + ' are no --room-teams', rooms('&roomteams=' + encodeURIComponent(bad)), base);
            for (const bad of ['', '0', '2', 'true', 'yes', 'on', '11', '01', '1 ', ' 1', '1\n', '1,1', 'one', '١', '１', '1&seat=1', '1;ls'])
                same('the game page: roomleaderstart=' + JSON.stringify(bad) + ' is no --room-leader-start (only 1, exactly)', rooms('&roomleaderstart=' + encodeURIComponent(bad)), base);
            // what this game tells the room about itself: [browser-]windows, macos, linux, android, ios or other, in any case, as the lower case word that was tested; no detection, only the address's word
            for (const os of ['windows', 'macos', 'linux', 'android', 'ios', 'other']) {
                same('the game page: platform=' + os + ' (and browser-' + os + ', in any case) is --platform, as the lower case word', [rooms('&platform=' + os), rooms('&platform=browser-' + os), rooms('&platform=' + os.toUpperCase()), rooms('&platform=Browser-' + os.charAt(0).toUpperCase() + os.slice(1))],
                     [[...base, '--platform', os], [...base, '--platform', 'browser-' + os], [...base, '--platform', os], [...base, '--platform', 'browser-' + os]]);
            }
            for (const bad of ['', 'browser', 'browser-', 'browser-browser-linux', 'win', 'windows10', 'windows ', ' windows', 'windows\n', 'linux;ls', 'linux&x', 'Linux --name x', 'ubuntu', 'chromeos', 'browser_windows', 'browser windows', 'browser-windows-x', 'constructor', '__proto__', 'window', 'mac', 'darwin',
                              'İOS', 'ＩＯＳ', 'browser--ios', 'ios,linux', 'iosios', 'windows'.repeat(300)])
                same('the game page: the platform ' + JSON.stringify(bad.slice(0, 24)) + ' is no --platform', rooms('&platform=' + encodeURIComponent(bad)), base);
            same('without a valid join nothing about a create block or the platform goes to the game',
                 [P.joinArguments('?room=abc&roommap=small&roomseats=2&roomteams=0%2B1&roomleaderstart=1&platform=linux', true, 'play.test').args, P.joinArguments('?roommap=small&roomseats=2&roomteams=0%2B1&roomleaderstart=1&platform=linux', true, 'play.test').args,
                  P.joinArguments('?join=/other&room=abc&roommap=small&roomseats=2&platform=linux', true, 'play.test').args, P.joinArguments('?join=//evil/ws&room=abc&roommap=small&platform=linux', true, 'play.test').args, P.joinArguments('?join=/ws/../x&room=abc&roomseats=2&platform=linux', true, 'play.test').args],
                 Array(5).fill([]));
        }
        {   // 60000 random addresses of a join: whatever the pool puts into the parameters, every argument that comes out is a flag of the table with a value that passed its own test, once, in the order that the game is given them
            const pools = {
                join: ['/ws', '/ws', '/ws', '/ws/a', '/other', '//evil/ws', '/ws/../x', 'ws://evil/', ''], room: ['abc', 'k7m2xq', 'my_room-1', 'ABC', 'a b', '', 'x'.repeat(33), 'a&b'],
                roommap: ['treasure', 'TINY', 'Small', 'islands', 'nowhere', '../x', 'gauntlet ', '', 'treasure.lvl', '--room-map', 'constructor'], roomseats: ['2', '3', '4', '5', '0', '1', '04', ' 3', '', '3 ', '\n4'],
                roomteams: ['0+1', '1 2', '3+0', '0+0', 'ffa', '0+4', '', '0+1 --name x'], roomleaderstart: ['1', '0', 'true', '11', '', ' 1'], platform: ['linux', 'Browser-Windows', 'macos', 'browser-', 'windows ', 'ios', 'OTHER', '', '--name x', 'browser-other'],
                seat: ['0', '1', '2', '3', '4', '-1', '', '1 '], fill: ['easy', 'none,none,easy,hard', 'none,none,none,none', 'x', '', 'EASY'], teams: ['0+1', '1 2', 'ffa', '0+0', ''], start: ['1', '2', '3', '4', '5', '', '2 '],
                name: ['Bob', ' Bob ', 'x'.repeat(40), 'é', '--name', 'a&b=c', '', ' ', 'two words'], embed: ['1', '0', ''], x: ['y'],
            };
            const keys = Object.keys(pools);
            const order = ['--audio-focus', '--join-url', '--room', '--room-map', '--room-seats', '--room-teams', '--room-leader-start', '--platform', '--seat', '--fill-bots', '--teams', '--start-when', '--name'];
            const tests = {
                '--join-url': (v) => /^wss:\/\/play\.test\/ws(\/[A-Za-z0-9._~\/-]*)?$/.test(v), '--room': (v) => /^[A-Za-z0-9_-]{1,32}$/.test(v), '--room-map': (v) => Object.keys(FILES).indexOf(v) !== -1, '--room-seats': (v) => /^[2-4]$/.test(v),
                '--room-teams': (v) => /^[0-3]\+[0-3]$/.test(v) && v[0] !== v[2], '--platform': (v) => /^(browser-)?(windows|macos|linux|android|ios|other)$/.test(v), '--seat': (v) => /^[0-3]$/.test(v), '--fill-bots': (v) => L.validFillPlan(v) === v && v !== '',
                '--teams': (v) => /^[0-3]\+[0-3]$/.test(v) && v[0] !== v[2], '--start-when': (v) => /^[1-4]$/.test(v), '--name': (v) => /^[\x20-\x7e]{1,32}$/.test(v) && v === v.trim(),
            };
            let seed = 11;
            const rnd = (n) => { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return (seed >>> 0) % n; };
            let wrong = 0;
            let sample = '';
            let blocks = 0;
            let teamed = 0;
            let flagged = 0;
            let rich = 0;
            for (let i = 0; i < 60000; i++) {
                const parts = [];
                for (const key of keys) if (rnd(4) !== 0) parts.push(key + '=' + encodeURIComponent(pools[key][rnd(pools[key].length)] + (rnd(8) === 0 ? pools[key][rnd(pools[key].length)] : '')));
                const search = '?' + parts.join('&');
                const args = P.joinArguments(search, true, 'play.test').args;
                let at = -1;
                let bad = false;
                for (let k = 0; k < args.length; k++) {
                    const flag = args[k];
                    const rank = order.indexOf(flag);
                    if (rank === -1 || rank <= at) { bad = true; break; }
                    at = rank;
                    if (flag === '--audio-focus' || flag === '--room-leader-start') continue;
                    if (!tests[flag] || k + 1 >= args.length || !tests[flag](args[++k])) { bad = true; break; }
                }
                if (bad) { wrong++; if (!sample) sample = search + ' -> ' + JSON.stringify(args); }
                if (args.indexOf('--room-map') !== -1 || args.indexOf('--room-seats') !== -1 || args.indexOf('--room-teams') !== -1 || args.indexOf('--room-leader-start') !== -1) blocks++;
                if (args.indexOf('--room') === -1 && args.some((a) => ['--room-map', '--room-seats', '--room-teams', '--room-leader-start'].indexOf(a) !== -1)) { wrong++; if (!sample) sample = search + ' -> ' + JSON.stringify(args) + ' (a create block with no room)'; }
                if (args.indexOf('--room-teams') !== -1) teamed++;
                if (args.indexOf('--room-leader-start') !== -1) flagged++;
                if (['--room-map', '--room-seats', '--platform', '--seat'].every((f) => args.indexOf(f) !== -1)) rich++;
                if (args.indexOf('--join-url') === -1 && args.some((a) => a !== '--audio-focus')) { wrong++; if (!sample) sample = search + ' -> ' + JSON.stringify(args) + ' (no door)'; }
            }
            check('60000 random addresses of a join: every argument is a flag of the table, in its place, once, with a value that passed its own test; none without a door (' + sample + ')', wrong === 0);
            check('... and the random addresses do reach the create block (' + blocks + ' with a part of it, ' + teamed + ' with teams, ' + flagged + ' with the leader-starts flag, ' + rich + ' with the map, the seats, the platform and a seat), so the rules above are not empty', blocks > 1000 && teamed > 100 && flagged > 100 && rich > 20);
        }
    }

} catch (e) {
    failures++;
    console.log('FAIL ' + e.message + '\n' + e.stack);
}

console.log('web lobby check: ' + checks + ' checks, ' + failures + ' failures');
process.exit(failures === 0 ? 0 : 1);
