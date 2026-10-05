// Runs the front page's OWN rules without a browser (the owner, on a phone: "I don't see a way to change colors or send an invite to another person should all be right there. We don't need separate
// AI and online. Only do online."): the page has one card, "New match", for every game, and an invitation for each friend in it.
//   * web/lobby.html, the block CARD_BEGIN .. CARD_END: the card's rules. Four seats, exactly one of them You, every other seat Friend, Easy, Medium, Hard or Nobody; who plays and how many people to wait
//     for; the Teams that the playing seats allow (cardTeamChoices / cardTeam); what is remembered (cardParse / cardText) and what the first visit takes from the keys of the earlier pages (cardFromOld);
//     Sit here (cardSit); the room's code (cardCode: demo-<map>-4p-[t<a><b>-]<random>, the teams a word of it) and when a code still fits the choices (cardRoomFits); the plan (cardFill) and the addresses
//     of START and of an invitation (cardQuery); why START is off (cardWhy) and the line under it (cardNote). Every state of the card is checked, and every address that it can make is read by the game
//     page (web/shell.html, ANTS_PAGE.joinArguments) as the same seat, the same plan, the same number of people and no teams parameter (they are in the code), with a name only in START's;
//   * the block LOBBY_BEGIN .. LOBBY_END: what the old addresses mean and what the old pages left in the browser. playersChoice (1 .. 4, else the fallback), hostPlayers (an old stored 1 is 2), soloBots
//     (nothing remembered is Medium, anything that is no level is none), soloSeats (one level per seat from the new key, else the old one), localGameQuery (the address of a game on THIS computer, which
//     an address with &players=1 still plays: ?map=<key>[&bots=<levels>]&name=<name>&aspect=<shape>, never a ?join=, a room or a server), hostSeats / hostFillText / hostTeamChoices / hostTeam /
//     hostTeamText / validFillPlan / validRoomTeams / hostPlanText (a room that an address asks for), roomTeamWord / codeTeams (the room's teams are a word of its code, demo-treasure-4p-t01-k7m2xq: the
//     page reads a code as the server and every game do, every line of tests/data/room_code_teams.tsv, the table that net::room_code_teams is tested against too);
//   * web/shell.html, ANTS_PAGE.localArguments: the game page's local parameters through a WHITELIST (the map by its key out of the six shipped maps, the opponents by one level word or three,
//     the teams by ffa or 0+N, a name of printable ASCII): what reaches the game's own arguments is a file name from a fixed table, level words that were tested, a seat that has a bot, a
//     cleaned name and flags, never the text of the address; nothing at all for an address with no local parameter (today's front page: the setup screen);
//   * the two pages agree: the six maps (keys and files, which exist in Original-Ants/Maps), and every address that the lobby can make for a game on this computer is read by the game page as the same
//     map, the same bot in each of the three other bases and the same name; every address of a ROOM that the room panel's old addresses can make (every room of 2 - 4 players, every set of levels,
//     every team) is read by the game page as the same --fill-bots plan and the same --teams.
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

// ---- the lobby's block, with what it stands on (the list of maps, the default, mapByKey and validFill, which the page defines above it)
const maps = /var MAPS = \[([\s\S]*?)\];/.exec(lobbyText);
const defaultKey = /var DEFAULT_MAP_KEY = '([a-z]+)';/.exec(lobbyText);
check('the lobby has its list of maps and its default', !!maps && !!defaultKey);
const lobbyCode = [
    'var MAPS = [' + maps[1] + '];',
    "var DEFAULT_MAP_KEY = '" + defaultKey[1] + "';",
    'function mapByKey(key) { for (var i = 0; i < MAPS.length; i++) if (MAPS[i].key === key) return MAPS[i]; return null; }',
    between(lobbyText, 'FILL_BEGIN', 'FILL_END', lobbyPath),
    between(lobbyText, 'LOBBY_BEGIN', 'LOBBY_END', lobbyPath),
    between(lobbyText, 'CARD_BEGIN', 'CARD_END', lobbyPath),
    'return { MAPS: MAPS, DEFAULT_MAP_KEY: DEFAULT_MAP_KEY, LOCAL_PAGE: LOCAL_PAGE, playersChoice: playersChoice, hostPlayers: hostPlayers, soloBots: soloBots, soloSeats: soloSeats, soloSeatsText: soloSeatsText, localGameQuery: localGameQuery, validFill: validFill, validFillPlan: validFillPlan, hostSeats: hostSeats, hostFillText: hostFillText, hostTeamChoices: hostTeamChoices, hostTeam: hostTeam, hostTeamText: hostTeamText, validRoomTeams: validRoomTeams, hostPlanText: hostPlanText, roomTeamWord: roomTeamWord, codeTeams: codeTeams, SEAT_COLOURS: SEAT_COLOURS, teamTitle: teamTitle, CARD_KEY: CARD_KEY, CARD_ROOM_KEY: CARD_ROOM_KEY, CARD_WORDS: CARD_WORDS, cardPlaying: cardPlaying, cardFriends: cardFriends, cardPeople: cardPeople, cardTeamChoices: cardTeamChoices, cardTeam: cardTeam, cardFix: cardFix, cardNew: cardNew, cardParse: cardParse, cardText: cardText, cardFromOld: cardFromOld, cardSit: cardSit, cardSet: cardSet, cardMap: cardMap, cardTeams: cardTeams, cardFill: cardFill, cardCode: cardCode, cardRoomFits: cardRoomFits, cardQuery: cardQuery, cardWhy: cardWhy, cardNote: cardNote };',
].join('\n');
const L = new Function(lobbyCode)();

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
    // the players of the Host card: 2 - 4; a 1 that an earlier page remembered (its Players select had 1 for a game on this computer, which has its own card now) is 2
    same('hostPlayers: 2, 3 and 4 are themselves', [L.hostPlayers('2', 9), L.hostPlayers('3', 9), L.hostPlayers('4', 9)], [2, 3, 4]);
    same('hostPlayers: an old stored 1 is 2 (not the fallback: the person had chosen something)', [L.hostPlayers('1', 4), L.hostPlayers('1', 2)], [2, 2]);
    for (const bad of ['0', '5', '', ' 2', '2 ', '02', 'two', null, undefined, 3, {}, []]) same('hostPlayers: ' + JSON.stringify(bad) + ' is the fallback', L.hostPlayers(bad, 7), 7);

    same('soloBots: a browser that never chose starts with Medium', [L.soloBots(null), L.soloBots(undefined)], ['medium', 'medium']);
    same('soloBots: "none" is none (the original\'s single player, alone), and so is an empty value', [L.soloBots('none'), L.soloBots('')], ['', '']);
    same('soloBots: the three levels, in any case', [L.soloBots('easy'), L.soloBots('medium'), L.soloBots('hard'), L.soloBots('HARD'), L.soloBots('Medium')], ['easy', 'medium', 'hard', 'hard', 'medium']);
    for (const bad of ['junk', 'medium ', ' easy', 'easy,hard', 'nonee', 'medium;hard', 'extreme', '0', 3, {}, ['easy']]) same('soloBots: ' + JSON.stringify(bad) + ' is none', L.soloBots(bad), '');

    // one level per seat: Red (seat 1), Blue (seat 2), Black (seat 3); the key ants-solo-seats, else the old ants-solo-bots, else a first visit
    const M3 = ['medium', 'medium', 'medium'];
    same('soloSeats: a first visit is Medium for all three', [L.soloSeats(null, null), L.soloSeats(undefined, undefined)], [M3, M3]);
    same('soloSeats: the remembered three words are the levels of the three seats, none is none, any case', [L.soloSeats('easy,medium,hard', null), L.soloSeats('none,hard,none', null), L.soloSeats('EASY,None,Medium', 'hard'), L.soloSeats('none,none,none', null)],
         [['easy', 'medium', 'hard'], ['', 'hard', ''], ['easy', '', 'medium'], ['', '', '']]);
    same('soloSeats: without the new key the old one gives its level to all three seats (none is none), and is left as it is', [L.soloSeats(null, 'hard'), L.soloSeats(undefined, 'Easy'), L.soloSeats(null, 'none'), L.soloSeats(null, 'junk')],
         [['hard', 'hard', 'hard'], ['easy', 'easy', 'easy'], ['', '', ''], ['', '', '']]);
    same('soloSeats: the new key beats the old one', L.soloSeats('easy,none,hard', 'medium'), ['easy', '', 'hard']);
    for (const bad of ['easy', 'easy,medium', 'easy,medium,hard,easy', 'easy,,hard', ',,', 'easy,medium,junk', 'easy, medium,hard', 'easy;medium;hard', '', 'constructor,x,y', 5, {}, ['easy', 'easy', 'easy']]) {
        same('soloSeats: a remembered ' + JSON.stringify(bad) + ' is not three levels: the old key (hard) is used, and a first visit is Medium', [L.soloSeats(bad, 'hard'), L.soloSeats(bad, null)], [['hard', 'hard', 'hard'], M3]);
    }
    same('soloSeatsText: the three words, none for none (what the settings and the address hold)', [L.soloSeatsText(['easy', '', 'hard']), L.soloSeatsText(['', '', '']), L.soloSeatsText(['medium', 'medium', 'medium']), L.soloSeatsText(['EASY', 'junk', 'Hard'])],
         ['easy,none,hard', 'none,none,none', 'medium,medium,medium', 'easy,none,hard']);
    same('soloSeats and soloSeatsText read each other', ['easy,none,hard', 'none,none,none', 'hard,hard,easy'].map((t) => L.soloSeatsText(L.soloSeats(t, null))), ['easy,none,hard', 'none,none,none', 'hard,hard,easy']);
    const FFA = { value: 'ffa', text: 'Free for all' };

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
    same('alone (no bots): the map, the start and the name; no --bot at all (the original\'s single player)', local('?map=islands&name=Bob'), ['--map', 'Original-Ants/Maps/ISLANDS.LVL', '--play', '--name', 'Bob']);
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
        same('bots=' + text + ': each base has its own bot (seat 1 Red, 2 Blue, 3 Black), and no --bot for a none', local('?map=small&bots=' + text).slice(3), botArgs(levels));
    }
    same('three words and no map: the setup screen with those bots', local('?bots=easy,none,hard'), botArgs(['easy', '', 'hard']));
    same('what the page keeps of the bots, for whoever reads it: one word when all three are alike, else the three words, empty for none',
         ['?bots=hard', '?bots=hard,hard,hard', '?bots=easy,none,hard', '?bots=none,none,none', '?bots=none', '?bots=junk', '?bots=easy,junk,hard'].map((t) => P.localArguments(t, '').bots), ['hard', 'hard', 'easy,none,hard', '', '', '', '']);
    // all or nothing: a text that is not one level word or exactly three words of the four is no bots at all (not the words that happen to be right)
    for (const bad of ['easy,medium', 'easy,medium,hard,easy', 'easy,,hard', ',,', ',easy,medium', 'easy,medium,', 'easy,extreme,hard', 'easy, medium,hard', 'easy,medium ,hard', 'easy;medium;hard', 'easy|medium|hard', 'easy medium hard', 'easy,medium,hard\n',
                      'none', 'none,none', 'none,easy', 'easy,hard,constructor', 'easy,medium,__proto__', 'easy,medium,hard,', 'EASY,MEDIUM,HARD,', ',', 'easy,medium,hard,none', '--bot 1:easy', 'easy,medium,hard --name x']) {
        same('bots=' + JSON.stringify(bad) + ' is no bots at all', local('?map=small&bots=' + encodeURIComponent(bad)), ['--map', 'Original-Ants/Maps/SMALL.LVL', '--play']);
    }

    // the teams: ffa (the game's default) says nothing; 0+N says --teams 0+N when seat N has a bot (the + comes as a blank unless it is %2B); anything else is ignored
    same('teams=0+1 with bots in seats 1 and 2: the game is told to make the teams (%2B)', local('?map=small&bots=easy,medium,none&teams=0%2B1').slice(3), ['--bot', '1:easy', '--bot', '2:medium', '--teams', '0+1']);
    same('... a + that came as a blank is the same', local('?map=small&bots=easy,medium,none&teams=0+2').slice(3), ['--bot', '1:easy', '--bot', '2:medium', '--teams', '0+2']);
    same('... and with the bots of one word', local('?map=small&bots=hard&teams=0%2B3').slice(3), [...bots('hard'), '--teams', '0+3']);
    same('... the teams stand before the name', local('?map=small&bots=easy,medium,hard&teams=0%2B2&name=Bob').slice(3), [...botArgs(['easy', 'medium', 'hard']), '--teams', '0+2', '--name', 'Bob']);
    same('what the page keeps of the teams', ['?bots=easy,medium,hard&teams=0%2B2', '?bots=easy,medium,hard&teams=0+3', '?bots=easy,medium,hard&teams=ffa', '?bots=easy,medium,none&teams=0%2B3', '?teams=0%2B1'].map((t) => P.localArguments(t, '').teams), ['0+2', '0+3', '', '', '']);
    for (const text of ['ffa', 'FFA', 'Ffa']) same('teams=' + text + ' is free for all: no --teams', local('?map=small&bots=easy,medium,hard&teams=' + text).slice(3), botArgs(['easy', 'medium', 'hard']));
    same('teams for a seat with no bot is ignored (the game would be told about a seat that is empty)', [local('?map=small&bots=easy,medium,none&teams=0%2B3').slice(3), local('?map=small&bots=none,none,hard&teams=0%2B1').slice(3)], [botArgs(['easy', 'medium', '']), botArgs(['', '', 'hard'])]);
    same('teams with no bots at all (or with bots that are no bots) is ignored', [local('?map=small&teams=0%2B1').slice(3), local('?map=small&bots=junk&teams=0%2B1').slice(3), local('?map=small&bots=easy,medium&teams=0%2B1').slice(3)], [[], [], []]);
    for (const bad of ['', '0', '0+', '+1', '0+0', '0+4', '0+9', '1+2', '2+3', '1+0', '00+1', '0+11', '0++1', '0+1+2', ' 0+1', '0+1 ', '0 +1', '0+ 1', '0\t1', '0+1\n', '0+1\n--name x', '0+1;ls', '0+1&teams=ffa', 'a+b', '0+one', '0+\u0967', 'constructor', '__proto__', 'none', 'true']) {
        same('teams=' + JSON.stringify(bad.slice(0, 20)) + ' is no team: no --teams', local('?map=small&bots=easy,medium,hard&teams=' + encodeURIComponent(bad)).slice(3), botArgs(['easy', 'medium', 'hard']));
    }
    same('a repeated teams parameter: the first one', local('?map=small&bots=easy,medium,hard&teams=0%2B2&teams=0%2B3').slice(3), [...botArgs(['easy', 'medium', 'hard']), '--teams', '0+2']);

    // what is refused: no map argument, no bot argument; whatever is put in the address
    const NOT_MAPS = ['', 'nowhere', 'TREASURE.LVL', 'treasure.lvl', 'treasure ', ' treasure', '../treasure', '..%2F..%2Fetc%2Fpasswd', 'treasure;ls', 'treasure&x', 'constructor', '__proto__', 'hasOwnProperty', 'toString',
                      'valueOf', 'treasure\n--name x', 'treаsure', 'treasure'.repeat(500), '0', 'Original-Ants/Maps/TREASURE.LVL', '/etc/passwd', 'http://evil/x', 'null', 'undefined'];
    for (const bad of NOT_MAPS) same('a map "' + bad.slice(0, 24) + '" is no map: no --map, no --play', local('?map=' + encodeURIComponent(bad)), []);
    const NOT_LEVELS = ['', 'none', 'extreme', 'medium ', ' medium', 'medium\n', 'medium\n--name x', 'easy,hard', 'medium;--map x', 'medium --name x', 'mediu', 'mediumm', 'constructor', '__proto__', '1', '0', 'true', 'medium'.repeat(300)];
    for (const bad of NOT_LEVELS) same('a level "' + bad.slice(0, 24).replace(/\n/g, '\\n') + '" is no level: no --bot', local('?map=small&bots=' + encodeURIComponent(bad)), ['--map', 'Original-Ants/Maps/SMALL.LVL', '--play']);
    same('a repeated map parameter: the first one', local('?map=tiny&map=small')[1], 'Original-Ants/Maps/TINY.LVL');
    same('a map that is no text at all (an array in the query) is nothing', local('?map[]=tiny'), []);
    same('an address with no local parameter at all: nothing is given to the game (today\'s front page: the setup screen), whatever the browser remembered', [local('', 'Bob'), local('?aspect=4:3', 'Bob'), local('?x=1&y=2', 'Bob')], [[], [], []]);
    same('another page\'s parameters are not local ones (join, room, fill, seat, embed)', local('?join=/ws&room=abc&fill=hard&seat=2&embed=1'), []);

    // the name
    same('the name of the address', local('?map=tiny&name=Bob').slice(-2), ['--name', 'Bob']);
    same('the name is cut like the game\'s own: printable ASCII only, the blanks at both ends gone, 32 characters at most', [local('?map=tiny&name=' + encodeURIComponent('  Zoë Ann  ')).slice(-2), local('?map=tiny&name=' + 'x'.repeat(40)).slice(-2), local('?map=tiny&name=' + encodeURIComponent('a\tb\nc')).slice(-2)],
         [['--name', 'Zo Ann'], ['--name', 'x'.repeat(32)], ['--name', 'abc']]);
    same('a name with other characters than ASCII only is cleaned, and one of nothing but those is none', [local('?map=tiny&name=' + encodeURIComponent('名前')), local('?map=tiny&name=' + encodeURIComponent('   '))], [['--map', 'Original-Ants/Maps/TINY.LVL', '--play'], ['--map', 'Original-Ants/Maps/TINY.LVL', '--play']]);
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
        const allowed = new Set(['--map', '--play', '--bot', '--teams', '--name', ...Object.values(FILES).map((f) => 'Original-Ants/Maps/' + f), '1:easy', '2:easy', '3:easy', '1:medium', '2:medium', '3:medium', '1:hard', '2:hard', '3:hard', '0+1', '0+2', '0+3']);
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
            for (let k = 0; k < got.length; k++) {
                if (got[k] === '--name') k++;                                                  // (a name is whatever the rules above allow, even a word that looks like a flag)
                else if (got[k] === '--bot') seats.push(Number(got[++k][0]));
                else if (got[k] === '--teams') teams.push(got[++k]);
            }
            const ordered = seats.every((n, k) => k === 0 || n > seats[k - 1]);
            const teamsOk = teams.length === 0 || (teams.length === 1 && /^0\+[1-3]$/.test(teams[0]) && seats.indexOf(Number(teams[0][2])) !== -1);
            if (!ordered || !teamsOk) { wrong++; if (!sample) sample = search + ' -> ' + JSON.stringify(got); }
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
                        const want = ['--map', 'Original-Ants/Maps/' + FILES[m.key], '--play', ...(level ? bots(level) : []), '--name', name || 'Player'];
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
                        const want = ['--map', 'Original-Ants/Maps/' + FILES[m.key], '--play', ...botArgs(levels), '--name', name || 'Player'];
                        made++;
                        if (JSON.stringify(args) !== JSON.stringify(want)) { wrong++; if (!sample) sample = query + ' -> ' + JSON.stringify(args); }
                        if (new URLSearchParams(query).get('aspect') !== shape) { wrong++; if (!sample) sample = query; }
                    }
                }
            }
        }
        check('every choice of levels that the lobby can make for a game on this computer (' + made + ' addresses) is read by the game page as the same bots in the same seats and the same name (' + sample + ')', wrong === 0);
        // what an old browser remembered (soloSeatsText) is what soloSeats reads back, and the game page reads the same words as an address
        let drift = 0;
        for (const a of WORDS) for (const b of WORDS) for (const c of WORDS) {
            const text = L.soloSeatsText([a, b, c]);
            if (JSON.stringify(L.soloSeats(text, null)) !== JSON.stringify([a, b, c])) drift++;
            if (JSON.stringify(local('?bots=' + text)) !== JSON.stringify(botArgs([a, b, c]))) drift++;
        }
        check('the settings text of the levels is read back whole by the page and by the game page (' + drift + ' differ)', drift === 0);
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // a room that an address asks for (network protocol 13), and what the Host card of the earlier pages remembered: a level for each seat after the leader's, and the room's teams
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

        // the room's teams are a word of the code: what the card makes and what a code says (the same table as the game's reading: tests/data/room_code_teams.tsv)
        same('roomTeamWord: t<a><b> for the pairs of seats 0 - 3, the lower first; free for all, anything else and a pair the wrong way round are no word', [L.roomTeamWord('0+1'), L.roomTeamWord('0+3'), L.roomTeamWord('1+2'), L.roomTeamWord('2+3'), L.roomTeamWord('ffa'), L.roomTeamWord(''), L.roomTeamWord('1+0'), L.roomTeamWord('2+2'), L.roomTeamWord('0+4'), L.roomTeamWord('0 1'), L.roomTeamWord(null), L.roomTeamWord(undefined), L.roomTeamWord(['0+1'])],
             ['t01', 't03', 't12', 't23', '', '', '', '', '', '', '', '', '']);
        {
            const table = fs.readFileSync(path.join(repo, 'tests', 'data', 'room_code_teams.tsv'), 'utf8').split(/\r?\n/).filter((line) => line !== '' && line[0] !== '#');
            let lines = 0;
            let named = 0;
            let wrongLines = '';
            for (const line of table) {
                const [code, teams] = line.split('\t');
                lines++;
                named += teams !== 'ffa' ? 1 : 0;
                if ((L.codeTeams(code) || 'ffa') !== teams) wrongLines += ' ' + code + ' -> ' + JSON.stringify(L.codeTeams(code)) + ' (the table: ' + teams + ');';
            }
            check('codeTeams reads every code of the table as net::room_code_teams does (' + lines + ' codes, ' + named + ' with teams):' + wrongLines, lines >= 50 && named >= 15 && lines - named >= 30 && wrongLines === '');
        }
        {   // what the card makes is read back: every choice of a room of 2 - 4 players, on every map
            let words = 0;
            let bad = '';
            for (const m of L.MAPS) {
                for (const players of [2, 3, 4]) {
                    for (const choice of L.hostTeamChoices(players)) {
                        const word = L.roomTeamWord(choice.value);
                        const code = 'demo-' + m.key + '-' + players + 'p-' + (word ? word + '-' : '') + 'k7m2xq';
                        if (word) words++;
                        if ((L.codeTeams(code) || 'ffa') !== choice.value || (word !== '') !== (choice.value !== 'ffa') || code.length > 27 || !/^[A-Za-z0-9_-]{1,32}$/.test(code)) bad += ' ' + code;
                    }
                }
            }
            check('every choice of the Host card is a word of its code that codeTeams reads back (' + words + ' words; free for all and two players have none; at most 27 characters):' + bad, words === L.MAPS.length * 6 && bad === '');
        }
        same('codeTeams: a code that is no string names nothing', [L.codeTeams(null), L.codeTeams(undefined), L.codeTeams(5), L.codeTeams({}), L.codeTeams(['demo-small-4p-t01-x'])], ['', '', '', '', '']);

        // what an address may say
        same('validFillPlan: one level in any case, or four words (none for none): the tested lower case text; no bots at all and anything else is ""',
             [L.validFillPlan('easy'), L.validFillPlan('MEDIUM'), L.validFillPlan('none,easy,none,hard'), L.validFillPlan('NONE,Easy,None,HARD'), L.validFillPlan('hard,hard,hard,hard'), L.validFillPlan('none'), L.validFillPlan('none,none,none,none'), L.validFillPlan(''), L.validFillPlan(null)],
             ['easy', 'medium', 'none,easy,none,hard', 'none,easy,none,hard', 'hard,hard,hard,hard', '', '', '', '']);
        for (const bad of ['easy,hard', 'easy,hard,easy', 'none,easy,none,hard,none', 'none,easy,,hard', 'none, easy,none,hard', 'none,easy,none,loud', ',,,', ',', 'none;easy;none;hard', 'easy hard', 'none,easy,none,hard\n', 'none,easy,none,hard&seat=1', 'none,easy,none,--room x', 'none,easy,none,hard,', 5, {}, ['easy'], 'hard,'.repeat(2000)]) {
            same('validFillPlan: ' + JSON.stringify(bad).slice(0, 40) + ' is no plan', L.validFillPlan(bad), '');
        }
        same('validRoomTeams: two different seats 0 - 3 as A+B (a + that came as a blank too): the tested text', [L.validRoomTeams('0+1'), L.validRoomTeams('0 1'), L.validRoomTeams('1+2'), L.validRoomTeams('3+0'), L.validRoomTeams('2 3')], ['0+1', '0+1', '1+2', '3+0', '2+3']);
        for (const bad of ['', 'ffa', 'FFA', '0+0', '2+2', '0+4', '4+0', '01', '0++1', '0+1+2', ' 0+1', '0+1 ', '0 +1', '0+ 1', '0\t1', '0+1\n', '0+1&teams=ffa', 'a+b', '0+१', '0+１', 'constructor', null, undefined, 1, {}, ['0+1']]) {
            same('validRoomTeams: ' + JSON.stringify(bad) + ' is no team', L.validRoomTeams(bad), '');
        }

        // the two pages agree for rooms: whatever the Host card makes of its levels and its team (a room of 2, 3 or 4 players, every set of three levels, every team that the room offers), the game page
        // reads the address (&fill= &teams=) as the same plan, in the same seats, and the same teams for the game; and the plan means what the card chose for the seats that the room has
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
                    const address = '?join=/ws&room=demo-small-' + players + 'p-abc123' + (plan ? '&fill=' + plan : '') + (team ? '&teams=' + encodeURIComponent(team) : '') + '&name=Bob';
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
        check('every address of a room that the Host card can make (' + made + ': ' + planned + ' with bots, ' + teamed + ' with teams) is read by the game page as the same --fill-bots plan and the same --teams, the plan means the card\'s level in every seat that the room has, and the lobby reads it back whole (' + sample + ')', wrong === 0 && planned > 100 && teamed > 100);
        // the whitelist of the game page: a plan or a team that is not what the card makes never reaches the game's arguments
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
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // the card, "New match" (the block CARD): four seats, one of them You, a choice for each of the others
    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const st = (you, seats, teams, map) => ({ map: map || 'treasure', you, seats, teams: teams || 'ffa' });
        const M4 = ['medium', 'medium', 'medium', 'medium'];
        const COLOURS = ['Green', 'Red', 'Blue', 'Black'];
        const FOUR = [FFA, { value: '0+1', text: 'Green + Red against Blue + Black' }, { value: '0+2', text: 'Green + Blue against Red + Black' }, { value: '0+3', text: 'Green + Black against Red + Blue' }];
        const frozen = (state) => JSON.stringify(state);

        same('the card has five choices for a seat, in the order of its buttons, and keeps its choices under ants-match', [L.CARD_WORDS, L.CARD_KEY, L.CARD_ROOM_KEY], [['friend', 'easy', 'medium', 'hard', 'nobody'], 'ants-match', 'ants-match-room']);
        same('cardNew: Treasure, You at Green, a Medium bot in every seat (the seat of You keeps its choice for later), free for all; a map that is none of the six is Treasure', [L.cardNew('treasure'), L.cardNew('islands').map, L.cardNew(undefined).map, L.cardNew('nowhere').map, L.cardNew('Treasure').map],
             [st(0, M4), 'islands', 'treasure', 'treasure', 'treasure']);

        // who plays, and how many people the room waits for: You, and every seat that is not Nobody; the people are You and the Friends (the bots come with START)
        same('cardPlaying: You and every seat that is not Nobody, in the order of the seats (a Nobody seat that is You plays: You are in it)',
             [L.cardPlaying(st(0, ['medium', 'nobody', 'friend', 'nobody'])), L.cardPlaying(st(1, ['medium', 'nobody', 'friend', 'nobody'])), L.cardPlaying(st(3, ['nobody', 'nobody', 'nobody', 'nobody'])), L.cardPlaying(st(2, ['easy', 'hard', 'nobody', 'friend']))],
             [[0, 2], [0, 1, 2], [3], [0, 1, 2, 3]]);
        same('cardFriends and cardPeople: the seats with Friend (not the seat that is You), and You with them', [L.cardFriends(st(2, ['friend', 'friend', 'friend', 'friend'])), L.cardFriends(st(0, ['friend', 'easy', 'nobody', 'friend'])), L.cardFriends(st(1, M4)), L.cardPeople(st(2, ['friend', 'friend', 'friend', 'friend'])), L.cardPeople(st(0, ['friend', 'easy', 'nobody', 'friend'])), L.cardPeople(st(1, M4))],
             [[0, 1, 3], [3], [], 4, 2, 1]);

        // the Teams: only the pairs that the playing seats allow
        same('cardTeamChoices: four seats play: free for all and Green with Red, Blue or Black (the other two are the other team)', L.cardTeamChoices(st(0, M4)), FOUR);
        same('cardTeamChoices: three seats play: the three pairs of them, the third plays alone (Green, Blue and Black play: Green + Blue, Green + Black, Blue + Black)', L.cardTeamChoices(st(0, ['medium', 'nobody', 'friend', 'hard'])),
             [FFA, { value: '0+2', text: 'Green + Blue against Black' }, { value: '0+3', text: 'Green + Black against Blue' }, { value: '2+3', text: 'Blue + Black against Green' }]);
        same('... whichever seats they are, and a seat that is You plays (Red, Blue and Black play, You at Red)', L.cardTeamChoices(st(1, ['nobody', 'nobody', 'easy', 'friend'])),
             [FFA, { value: '1+2', text: 'Red + Blue against Black' }, { value: '1+3', text: 'Red + Black against Blue' }, { value: '2+3', text: 'Blue + Black against Red' }]);
        same('cardTeamChoices: two seats or one play: free for all only (a team of them would end the match at once)', [L.cardTeamChoices(st(0, ['medium', 'easy', 'nobody', 'nobody'])), L.cardTeamChoices(st(2, ['nobody', 'nobody', 'friend', 'nobody'])), L.cardTeamChoices(st(3, ['nobody', 'nobody', 'nobody', 'nobody']))], [[FFA], [FFA], [FFA]]);
        {
            let wrong = '';
            for (let you = 0; you < 4; you++) for (const a of L.CARD_WORDS) for (const b of L.CARD_WORDS) for (const c of L.CARD_WORDS) for (const d of L.CARD_WORDS) {
                const state = st(you, [a, b, c, d]);
                const playing = L.cardPlaying(state);
                for (const choice of L.cardTeamChoices(state)) {
                    if (choice.value === 'ffa') continue;
                    const [x, y] = choice.value.split('+').map(Number);
                    if (!(x < y && playing.indexOf(x) !== -1 && playing.indexOf(y) !== -1 && playing.length >= 3 && L.roomTeamWord(choice.value) !== '')) wrong += ' ' + frozen(state) + ' ' + choice.value;
                }
            }
            check('every pair that the card offers (all 2500 states) is two different seats that play, the lower first, with three or four seats playing, and has a word for the room\'s code:' + wrong.slice(0, 200), wrong === '');
        }
        same('cardTeam: a choice that is still one of the choices stays', [L.cardTeam('0+1', st(0, M4)), L.cardTeam('0+3', st(0, M4)), L.cardTeam('2+3', st(0, ['medium', 'nobody', 'friend', 'hard'])), L.cardTeam('ffa', st(0, M4))], ['0+1', '0+3', '2+3', 'ffa']);
        same('... a choice that the seats no longer allow (a seat that does not play, two seats only, a pair of four seats that does not hold Green) and anything that is no choice is free for all',
             [L.cardTeam('0+1', st(0, ['medium', 'nobody', 'friend', 'hard'])), L.cardTeam('0+2', st(0, ['medium', 'easy', 'nobody', 'nobody'])), L.cardTeam('1+2', st(0, M4)), L.cardTeam('2+3', st(0, M4)), L.cardTeam('', st(0, M4)), L.cardTeam(null, st(0, M4)), L.cardTeam('0 1', st(0, M4)), L.cardTeam('1+0', st(0, M4)), L.cardTeam('constructor', st(0, M4)), L.cardTeam(['0+1'], st(0, M4))],
             Array(10).fill('ffa'));

        // a state that is repaired: every part of it is valid
        same('cardFix: a map that is none of the six is Treasure, You is a seat, a word that is none of the five is Medium, a team that the seats do not allow is free for all, a short list is filled up with Medium',
             [L.cardFix({ map: 'nowhere', you: 7, seats: ['easy', 'x', 'HARD', null], teams: '0+1' }), L.cardFix({ map: 'small', you: 2, seats: ['easy'], teams: 'junk' }), L.cardFix({ map: 'tiny', you: 3, seats: ['friend', 'friend', 'friend', 'friend'], teams: '0+3' }),
              L.cardFix({ map: 'small', you: 0, seats: ['easy', 'nobody', 'nobody', 'nobody'], teams: '0+1' }), L.cardFix({ map: 'small', you: 0, seats: ['easy', 'nobody', 'friend', 'hard'], teams: '1+2' })],
             [st(0, ['easy', 'medium', 'medium', 'medium'], '0+1'), st(2, ['easy', 'medium', 'medium', 'medium'], 'ffa', 'small'), st(3, ['friend', 'friend', 'friend', 'friend'], '0+3', 'tiny'),
              st(0, ['easy', 'nobody', 'nobody', 'nobody'], 'ffa', 'small'), st(0, ['easy', 'nobody', 'friend', 'hard'], 'ffa', 'small')]);
        {
            const before = { map: 'small', you: 1, seats: ['easy', 'x', 'x', 'x'], teams: '9' };
            const text = frozen(before);
            L.cardFix(before);
            check('... and the state that it was given is left as it was (the rules make new states)', frozen(before) === text);
        }

        // what the browser remembers: the text that cardText writes is read back whole; anything else is not a state
        {
            let bad = '';
            let made = 0;
            for (const map of L.MAPS) for (let you = 0; you < 4; you++) for (const a of L.CARD_WORDS) for (const b of L.CARD_WORDS) {
                const state = L.cardFix(st(you, [a, b, 'friend', 'hard'], 'ffa', map.key));
                for (const choice of L.cardTeamChoices(state)) {
                    const full = L.cardFix(Object.assign({}, state, { teams: choice.value }));
                    made++;
                    if (frozen(L.cardParse(L.cardText(full))) !== frozen(full)) bad += ' ' + L.cardText(full);
                }
            }
            check('cardParse reads back what cardText wrote for ' + made + ' states (every map, every You, every team that is a choice):' + bad.slice(0, 200), bad === '' && made > 500);
        }
        same('cardText is JSON with the four parts (what the browser holds under ants-match)', JSON.parse(L.cardText(st(2, ['easy', 'friend', 'nobody', 'hard'], '0+1', 'islands'))), { map: 'islands', you: 2, seats: ['easy', 'friend', 'nobody', 'hard'], teams: '0+1' });
        for (const junk of [null, undefined, '', 'junk', '[]', '{}', 'null', '5', '"x"', '{"map":"treasure"}', '{"map":"Treasure","you":0,"seats":["medium","medium","medium","medium"],"teams":"ffa"}', '{"map":"nowhere","you":0,"seats":["medium","medium","medium","medium"],"teams":"ffa"}',
                            '{"map":"treasure","you":"0","seats":["medium","medium","medium","medium"],"teams":"ffa"}', '{"map":"treasure","you":4,"seats":["medium","medium","medium","medium"],"teams":"ffa"}', '{"map":"treasure","you":-1,"seats":["medium","medium","medium","medium"],"teams":"ffa"}',
                            '{"map":"treasure","you":0,"seats":["medium","medium","medium"],"teams":"ffa"}', '{"map":"treasure","you":0,"seats":["medium","medium","medium","medium","medium"],"teams":"ffa"}', '{"map":"treasure","you":0,"seats":["medium","medium","medium","HARD"],"teams":"ffa"}',
                            '{"map":"treasure","you":0,"seats":["medium","medium","medium","none"],"teams":"ffa"}', '{"map":"treasure","you":0,"seats":["medium","medium","medium",null],"teams":"ffa"}', '{"map":"treasure","you":0,"seats":"medium,medium,medium,medium","teams":"ffa"}',
                            '{"map":"treasure","you":0,"seats":{"0":"medium","1":"medium","2":"medium","3":"medium","length":4},"teams":"ffa"}', '{"map":["treasure"],"you":0,"seats":["medium","medium","medium","medium"],"teams":"ffa"}', 5, {}, ['treasure']]) {
            same('cardParse: ' + String(JSON.stringify(junk)).slice(0, 60) + ' is no state', L.cardParse(junk), null);
        }
        same('cardParse: a team that is none of the choices of those seats is free for all (the state is still read), and a missing team is free for all', [L.cardParse('{"map":"treasure","you":0,"seats":["medium","nobody","friend","nobody"],"teams":"0+1"}').teams, L.cardParse('{"map":"treasure","you":0,"seats":["medium","medium","friend","hard"]}').teams, L.cardParse('{"map":"treasure","you":0,"seats":["medium","medium","friend","hard"],"teams":7}').teams], ['ffa', 'ffa', 'ffa']);

        // the first visit, from what the earlier pages left in the browser (read, never written)
        const OLD = (o) => Object.assign({ solo: null, legacy: null, soloTeams: null, map: null, players: null, fill: null, teams: null }, o);
        same('cardFromOld: nothing remembered is a first visit (and so is nothing at all)', [L.cardFromOld(OLD({})), L.cardFromOld({}), L.cardFromOld(undefined), L.cardFromOld(null)], [st(0, M4), st(0, M4), st(0, M4), st(0, M4)]);
        same('... the map is the last one that was played or hosted (ants-four-map), and Treasure when that is none of the six', [L.cardFromOld(OLD({ map: 'islands' })).map, L.cardFromOld(OLD({ map: 'junk' })).map, L.cardFromOld(OLD({ map: 5 })).map, L.cardFromOld(OLD({ map: 'TINY' })).map], ['islands', 'treasure', 'treasure', 'treasure']);
        same('a browser that played a game on this computer: the opponents of Red, Blue and Black are its levels (None is Nobody), You at Green, Green\'s own seat Medium',
             [L.cardFromOld(OLD({ solo: 'easy,none,hard' })), L.cardFromOld(OLD({ solo: 'none,none,none' })), L.cardFromOld(OLD({ solo: 'EASY,Medium,none' }))],
             [st(0, ['medium', 'easy', 'nobody', 'hard']), st(0, ['medium', 'nobody', 'nobody', 'nobody']), st(0, ['medium', 'easy', 'medium', 'nobody'])]);
        same('... the first versions\' key gives its one level to the three seats (none is Nobody in all three), and the newer key beats it', [L.cardFromOld(OLD({ legacy: 'hard' })), L.cardFromOld(OLD({ legacy: 'none' })), L.cardFromOld(OLD({ legacy: 'junk' })), L.cardFromOld(OLD({ solo: 'easy,none,hard', legacy: 'medium' }))],
             [st(0, ['medium', 'hard', 'hard', 'hard']), st(0, ['medium', 'nobody', 'nobody', 'nobody']), st(0, ['medium', 'nobody', 'nobody', 'nobody']), st(0, ['medium', 'easy', 'nobody', 'hard'])]);
        same('... a remembered list that is not three levels is not used: the old key is, else Medium (as the old page did)', [L.cardFromOld(OLD({ solo: 'easy,hard', legacy: 'hard' })).seats, L.cardFromOld(OLD({ solo: 'junk' })).seats], [['medium', 'hard', 'hard', 'hard'], M4]);
        same('... and its team (You with a seat that plays) is kept when it is still a choice, else free for all', [L.cardFromOld(OLD({ solo: 'easy,medium,hard', soloTeams: '0+3' })).teams, L.cardFromOld(OLD({ solo: 'easy,none,hard', soloTeams: '0+3' })).teams, L.cardFromOld(OLD({ solo: 'easy,none,hard', soloTeams: '0+2' })).teams, L.cardFromOld(OLD({ solo: 'easy,medium,hard', soloTeams: 'junk' })).teams, L.cardFromOld(OLD({ solo: 'easy,none,none', soloTeams: '0+1' })).teams],
             ['0+3', '0+3', 'ffa', 'ffa', 'ffa']);
        same('a browser that only hosted rooms: the room that it had: a seat of it that had no bot is a Friend, one with a level is that bot, a seat beyond the room is Nobody (a 1 is 2 players; no players is 2)',
             [L.cardFromOld(OLD({ players: '3', fill: 'none,easy,none,hard' })), L.cardFromOld(OLD({ players: '2' })), L.cardFromOld(OLD({ players: '4', fill: 'medium' })), L.cardFromOld(OLD({ players: '1' })), L.cardFromOld(OLD({ fill: 'none,none,none,none' })), L.cardFromOld(OLD({ players: '4', fill: 'none,hard,none,easy' }))],
             [st(0, ['medium', 'easy', 'friend', 'nobody']), st(0, ['medium', 'friend', 'nobody', 'nobody']), st(0, ['medium', 'medium', 'medium', 'medium']), st(0, ['medium', 'friend', 'nobody', 'nobody']), st(0, ['medium', 'friend', 'nobody', 'nobody']), st(0, ['medium', 'hard', 'friend', 'easy'])]);
        same('... with its team when the room had one that the card still offers (four players: Green with Black), else free for all (a pair that three seats cannot make)', [L.cardFromOld(OLD({ players: '4', teams: '0+3' })).teams, L.cardFromOld(OLD({ players: '3', teams: '0+3' })).teams, L.cardFromOld(OLD({ players: '3', teams: '0+1' })).teams, L.cardFromOld(OLD({ players: '2', teams: '0+1' })).teams], ['0+3', 'ffa', '0+1', 'ffa']);
        same('both kinds of key: the game on this computer wins (it was the first card); a junk value for players or fill is the default room', [L.cardFromOld(OLD({ solo: 'easy,easy,easy', players: '4', fill: 'hard' })).seats, L.cardFromOld(OLD({ players: 'x', fill: 'x' })).seats], [['medium', 'easy', 'easy', 'easy'], ['medium', 'friend', 'nobody', 'nobody']]);

        // Sit here, and the other changes of the state: each makes a new state and leaves the old one as it was
        {
            const base = st(0, ['hard', 'easy', 'friend', 'nobody'], '0+1');
            const before = frozen(base);
            same('cardSit: You move to the seat; the seat that You left shows its own choice again (Hard at Green), the seat that You sit in keeps its choice (Nobody at Black: You are in it) and the others keep theirs',
                 [L.cardSit(base, 3), L.cardSit(base, 1), L.cardSit(base, 0)], [st(3, ['hard', 'easy', 'friend', 'nobody'], '0+1'), st(1, ['hard', 'easy', 'friend', 'nobody'], '0+1'), base]);
            same('... the team stays while it is a choice for the seats that play now (You at Green, Blue a Friend and Black Nobody: Green, Red and Blue play; sit at Black: all four play, 0+2 stays; sit at Red with Green a bot: 1+2 stays), else it is free for all (Green Nobody: Red and Blue are the two that play)',
                 [L.cardSit(st(0, ['hard', 'easy', 'friend', 'nobody'], '0+2'), 3).teams, L.cardSit(st(0, ['medium', 'easy', 'friend', 'nobody'], '1+2'), 1).teams, L.cardSit(st(0, ['nobody', 'easy', 'friend', 'nobody'], '1+2'), 1).teams], ['0+2', '1+2', 'ffa']);
            same('... a seat that is none of the four is not a seat to sit in', [L.cardSit(base, 4), L.cardSit(base, -1), L.cardSit(base, '2'), L.cardSit(base, null), L.cardSit(base, undefined), L.cardSit(base, 1.5)], Array(6).fill(base));
            check('... and the state that it was given is left as it was', frozen(base) === before);
            same('cardSet: a choice for a seat (the others stay); a word that is none of the five, a seat that is none of the four change nothing; the team goes when the seats no longer allow it',
                 [L.cardSet(base, 2, 'hard'), L.cardSet(base, 3, 'friend'), L.cardSet(base, 2, 'HARD'), L.cardSet(base, 2, 'none'), L.cardSet(base, 5, 'hard'), L.cardSet(base, 1, 'nobody').teams, L.cardSet(st(0, M4, '0+3'), 3, 'nobody').teams],
                 [st(0, ['hard', 'easy', 'hard', 'nobody'], '0+1'), st(0, ['hard', 'easy', 'friend', 'friend'], '0+1'), base, base, base, 'ffa', 'ffa']);
            same('cardMap and cardTeams: the map (one of the six), the team (one of the choices of the seats that play: Black is Nobody here, so 0+3 is not one; else free for all)', [L.cardMap(base, 'small').map, L.cardMap(base, 'nowhere').map, L.cardMap(base, 'Small').map, L.cardTeams(base, '0+2').teams, L.cardTeams(base, '0+3').teams, L.cardTeams(st(0, M4), '0+3').teams, L.cardTeams(base, 'junk').teams, L.cardTeams(base, 'ffa').teams],
                 ['small', 'treasure', 'treasure', '0+2', 'ffa', '0+3', 'ffa', 'ffa']);
            check('... and none of them changed the state that it was given', frozen(base) === before);
        }

        // the plan: four words for the seats 0 - 3, a bot's seat its level, none for You, a Friend and Nobody; nothing when no seat has a bot
        same('cardFill: the level of every bot seat, none for You (even when the seat that is You has a level of its own), a Friend and Nobody',
             [L.cardFill(st(0, ['hard', 'easy', 'friend', 'nobody'])), L.cardFill(st(2, ['easy', 'medium', 'hard', 'nobody'])), L.cardFill(st(3, ['medium', 'nobody', 'hard', 'friend'])), L.cardFill(st(1, M4)), L.cardFill(st(0, ['easy', 'hard', 'medium', 'easy']))],
             ['none,easy,none,none', 'easy,medium,none,none', 'medium,none,hard,none', 'medium,none,medium,medium', 'none,hard,medium,easy']);
        same('... and no bot at all is no plan', [L.cardFill(st(0, ['hard', 'friend', 'friend', 'nobody'])), L.cardFill(st(2, ['nobody', 'nobody', 'easy', 'nobody'])), L.cardFill(st(1, ['friend', 'hard', 'friend', 'friend']))], ['', '', '']);

        // the code of the room, and when a code still fits
        {
            let bad = '';
            let words = 0;
            let made = 0;
            for (const map of L.MAPS) for (const you of [0, 1, 2, 3]) for (const choice of L.cardTeamChoices(L.cardNew(map.key))) {
                const state = L.cardFix(st(you, M4, choice.value, map.key));
                const code = L.cardCode(state, 'k7m2xq');
                const word = L.roomTeamWord(state.teams);
                made++;
                if (word) words++;
                const want = 'demo-' + map.key + '-4p-' + (word ? word + '-' : '') + 'k7m2xq';
                if (code !== want || code.length > 27 || !/^[A-Za-z0-9_-]{1,32}$/.test(code) || (L.codeTeams(code) || 'ffa') !== state.teams || !L.cardRoomFits(code, state)) bad += ' ' + code;
            }
            check('cardCode: demo-<map>-4p-[t<a><b>-]<six characters> for every map and team (' + words + ' words in ' + made + ' codes), at most 27 characters, the teams read back by codeTeams, and the code fits its own state:' + bad, bad === '' && words === 6 * 4 * 3);
        }
        {
            const state = st(0, M4, '0+1', 'small');
            const code = L.cardCode(state, 'k7m2xq');
            same('cardRoomFits: the code of these choices fits them', code, 'demo-small-4p-t01-k7m2xq');
            same('... a code of another map, other teams (or none), another size or another shape does not', ['demo-tiny-4p-t01-k7m2xq', 'demo-small-4p-t02-k7m2xq', 'demo-small-4p-k7m2xq', 'demo-small-3p-t01-k7m2xq', 'demo-small-2p-t01-k7m2xq', 'demo-small-t01-k7m2xq', 'demo-small-4p-t01-k7m2x', 'demo-small-4p-t01-k7m2xqq', 'DEMO-small-4p-t01-k7m2xq', 'demo-small-4p-T01-k7m2xq', 'demo-small-4p-t10-k7m2xq', 'demo-small-4p-t01-K7M2XQ', 'demo-small-4p-t01-k7m2x!', 'demo-small-4p-t01-k7m2xq-', 'x demo-small-4p-t01-k7m2xq', '', 'constructor'].map((c) => L.cardRoomFits(c, state)), Array(17).fill(false));
            same('... and a value that is no text fits nothing', [null, undefined, 5, {}, ['demo-small-4p-t01-k7m2xq']].map((c) => L.cardRoomFits(c, state)), Array(5).fill(false));
            same('cardRoomFits: free for all fits a code with no team word only', [L.cardRoomFits('demo-small-4p-k7m2xq', st(0, M4, 'ffa', 'small')), L.cardRoomFits('demo-small-4p-t01-k7m2xq', st(0, M4, 'ffa', 'small'))], [true, false]);
        }

        // why START is off, and the line under it
        same('cardWhy: at least two players (You, the Friends and the bots) or the button is off, with the reason', [L.cardWhy(st(0, ['medium', 'nobody', 'nobody', 'nobody'])), L.cardWhy(st(2, ['nobody', 'nobody', 'hard', 'nobody'])), L.cardWhy(st(0, ['medium', 'nobody', 'friend', 'nobody'])), L.cardWhy(st(0, M4))],
             ['Pick at least one more seat: a friend or a bot.', 'Pick at least one more seat: a friend or a bot.', '', '']);
        same('cardNote: the reason when START is off; with bots only it starts at once; with one friend or more it waits for them and says what else starts it',
             [L.cardNote(st(0, ['medium', 'nobody', 'nobody', 'nobody'])), L.cardNote(st(0, M4)), L.cardNote(st(0, ['medium', 'friend', 'nobody', 'nobody'])), L.cardNote(st(0, ['medium', 'friend', 'friend', 'hard'])), L.cardNote(st(1, ['friend', 'friend', 'friend', 'friend']))],
             ['Pick at least one more seat: a friend or a bot.', 'Starts at once, in this tab. Bots gather food, raid and fight back.', 'Starts when your friend is in, or when you press START in the waiting room.',
              'Starts when your friends are in, or when you press START in the waiting room.', 'Starts when your friends are in, or when you press START in the waiting room.']);

        // the addresses: START and an invitation
        {
            const code = 'demo-treasure-4p-t12-k7m2xq';
            same('cardQuery: START carries the room, your seat, the plan, your name (URL-encoded), the shape and the number of people; an invitation carries no name; the teams are in the code, so no teams parameter; no key',
                 [L.cardQuery(st(2, ['easy', 'medium', 'x', 'nobody']), code, 2, 'Ann', '16:9'), L.cardQuery(st(0, ['x', 'friend', 'medium', 'friend']), code, 0, 'A&b <c>=', '4:3'), L.cardQuery(st(0, ['x', 'friend', 'medium', 'friend']), code, 3, null, '16:9'), L.cardQuery(st(0, ['x', 'friend', 'nobody', 'nobody']), code, 1, null, '4:3')],
                 ['?join=/ws&room=demo-treasure-4p-t12-k7m2xq&seat=2&fill=easy,medium,none,none&name=Ann&aspect=16:9&start=1',
                  '?join=/ws&room=demo-treasure-4p-t12-k7m2xq&seat=0&fill=none,none,medium,none&name=A%26b%20%3Cc%3E%3D&aspect=4:3&start=3',
                  '?join=/ws&room=demo-treasure-4p-t12-k7m2xq&seat=3&fill=none,none,medium,none&aspect=16:9&start=3',
                  '?join=/ws&room=demo-treasure-4p-t12-k7m2xq&seat=1&aspect=4:3&start=2']);
            same('... the shape is 16:9 or 4:3 and nothing else; with an empty name START still has its name parameter (the game page does not ask for it again)', [L.cardQuery(st(0, M4), code, 0, 'x', '21:9'), L.cardQuery(st(0, M4), code, 0, 'x', undefined), L.cardQuery(st(0, M4), code, 0, '', '16:9')],
                 ['?join=/ws&room=demo-treasure-4p-t12-k7m2xq&seat=0&fill=none,medium,medium,medium&name=x&aspect=16:9&start=1', '?join=/ws&room=demo-treasure-4p-t12-k7m2xq&seat=0&fill=none,medium,medium,medium&name=x&aspect=16:9&start=1', '?join=/ws&room=demo-treasure-4p-t12-k7m2xq&seat=0&fill=none,medium,medium,medium&name=&aspect=16:9&start=1']);
        }
        {   // every state of the card: the game page reads START and every invitation as the same seat, plan and number of people, no teams parameter, no name in an invitation
            const P2 = P;
            let states = 0;
            let queries = 0;
            let wrong = '';
            const note = (what) => { if (wrong.length < 400) wrong += ' ' + what; };
            for (const mapKey of ['treasure', 'tiny']) for (let you = 0; you < 4; you++) for (const a of L.CARD_WORDS) for (const b of L.CARD_WORDS) for (const c of L.CARD_WORDS) for (const d of L.CARD_WORDS) {
                const base = L.cardFix(st(you, [a, b, c, d], 'ffa', mapKey));
                for (const choice of L.cardTeamChoices(base)) {
                    const state = L.cardFix(Object.assign({}, base, { teams: choice.value }));
                    states++;
                    const code = L.cardCode(state, 'k7m2xq');
                    const plan = L.cardFill(state);
                    const people = L.cardPeople(state);
                    const playing = L.cardPlaying(state);
                    const friends = L.cardFriends(state);
                    const bots = [0, 1, 2, 3].filter((s) => plan !== '' && plan.split(',')[s] !== 'none');
                    // who is in the match: You, the friends and the bots, each seat once, and no seat that is Nobody
                    if (JSON.stringify([...new Set([you, ...friends, ...bots])].sort()) !== JSON.stringify(playing) || bots.length + people !== playing.length || bots.indexOf(you) !== -1 || friends.some((f) => bots.indexOf(f) !== -1)) note('roster ' + frozen(state));
                    if ((L.cardWhy(state) === '') !== (playing.length >= 2 && playing.length <= 4)) note('why ' + frozen(state));
                    const args = P2.joinArguments(L.cardQuery(state, code, you, 'Ann', '16:9'), true, 'play.test').args;
                    queries++;
                    const want = ['--join-url', 'wss://play.test/ws', '--room', code, '--seat', String(you), ...(plan ? ['--fill-bots', plan] : []), '--start-when', String(people), '--name', 'Ann'];
                    if (JSON.stringify(args) !== JSON.stringify(want)) note('start ' + frozen(state) + ' -> ' + JSON.stringify(args));
                    for (const friend of friends) {
                        const invited = P2.joinArguments(L.cardQuery(state, code, friend, null, '16:9'), true, 'play.test').args;
                        queries++;
                        const wantInvite = ['--join-url', 'wss://play.test/ws', '--room', code, '--seat', String(friend), ...(plan ? ['--fill-bots', plan] : []), '--start-when', String(people)];
                        if (JSON.stringify(invited) !== JSON.stringify(wantInvite) || invited.indexOf('--name') !== -1 || invited.indexOf('--teams') !== -1) note('invite ' + frozen(state) + ' ' + friend + ' -> ' + JSON.stringify(invited));
                    }
                    if (L.codeTeams(code) !== (state.teams === 'ffa' ? '' : state.teams)) note('code ' + code);
                }
            }
            check('every state of the card (' + states + ' of them, with every team that it offers, on two maps) makes a START and ' + (queries - states) + ' invitations that the game page reads as the card meant: the seat, the plan, the people to wait for (--start-when), no --teams (the code has them), no name in an invitation; You, the Friends and the bots are the seats that play, once each:' + wrong,
                  wrong === '' && states > 4000);
        }
        {   // what is made from text that was typed is only ever text: a name goes into START URL-encoded, and no text of an address becomes anything but a tested word, a seat, a code or the cleaned name
            const evil = 'x&join=/ws&room=a&seat=1&fill=hard&start=1&teams=0+1 --name y';
            const q = L.cardQuery(st(0, M4), 'demo-treasure-4p-abcdef', 0, evil, '16:9');
            const args = P.joinArguments(q, true, 'play.test').args;
            check('a name with & = and a flag in it is one name in START, and the game page makes one --name of it (no other argument)', args.filter((a) => a === '--name').length === 1 && args.filter((a) => a === '--seat').length === 1 && args.filter((a) => a === '--start-when').length === 1 && args.filter((a) => a === '--room').length === 1 && args[args.indexOf('--name') + 1] === evil.slice(0, 32), JSON.stringify(args));
        }
    }
} catch (e) {
    failures++;
    console.log('FAIL ' + e.message + '\n' + e.stack);
}

console.log('web lobby check: ' + checks + ' checks, ' + failures + ' failures');
process.exit(failures === 0 ? 0 : 1);
