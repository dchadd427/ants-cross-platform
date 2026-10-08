// Runs the front page's OWN rules without a browser (the owner, on a phone: "I don't see a way to change colors or send an invite to another person should all be right there. We don't need separate
// AI and online. Only do online."): the page has one card, "New match", for every game, and an invitation for each friend in it.
//   * web/lobby.html, the block CARD_BEGIN .. CARD_END: the card's rules. Four seats, exactly one of them You, every other seat Friend, Easy, Medium, Hard or Nobody; who plays and how many people to wait
//     for; the Team 1 and Team 2 switches of the seats that play (a team is two seats on the same switch: cardSide, cardShownSides, cardSideOpen, cardTeamOf); what is remembered (cardParse / cardText) and what the first visit takes from the keys of the earlier pages (cardFromOld);
//     Sit here (cardSit); the room's create block (cardBlock: the card's map, four seats, the card's teams) and when the room that the tab kept still fits the choices (cardRoomFits: its code is eight characters of the page's alphabet,
//     its map and teams are the card's); the plan (cardFill) and the addresses of START and of an invitation (cardQuery, which carries the block); who is alone (cardAlone: START then plays a game for one on this computer) and the
//     line under START (cardNote). Every state of the card is checked, and every address that it can make is read by the game page (web/shell.html, ANTS_PAGE.joinArguments) as the same seat, the same plan, the same number of people,
//     the same create block (--room-map, --room-seats, --room-teams) and no --teams (the teams are in the block), with a name only in START's;
//   * the block LOBBY_BEGIN .. LOBBY_END: what the old addresses mean and what the old pages left in the browser. playersChoice (1 .. 4, else the fallback), hostPlayers (an old stored 1 is 2), soloBots
//     (nothing remembered is Medium, anything that is no level is none), soloSeats (one level per seat from the new key, else the old one), localGameQuery (the address of a game on THIS computer, which
//     an address with &players=1 still plays: ?map=<key>[&bots=<levels>]&name=<name>&aspect=<shape>, never a ?join=, a room or a server), hostSeats / hostFillText / hostTeamChoices / hostTeam /
//     hostTeamText / validFillPlan / validRoomTeams / hostPlanText (a room that an address asks for), codeText (a room code as a screen shows it: two groups of four) and roomBlockQuery / roomBlockOf (a room's create
//     block as the links of the page carry it, &roommap= &roomseats= &roomteams= &roomleaderstart=, and as an address's block is read back: the game page turns it into --room-map, --room-seats, --room-teams and
//     --room-leader-start; a room code is a name and nothing more, so no code carries the map, the seats or the teams);
//   * web/shell.html, ANTS_PAGE.localArguments: the game page's local parameters through a WHITELIST (the map by its key out of the six shipped maps, the opponents by one level word or three,
//     the teams by ffa or 0+N, a name of printable ASCII): what reaches the game's own arguments is a file name from a fixed table, level words that were tested, a seat that has a bot, a
//     cleaned name and flags, never the text of the address; nothing at all for an address with no local parameter (today's front page: the setup screen);
//   * web/shell.html, ANTS_PAGE.joinArguments: the parameters of a join of a room (&roommap= &roomseats= &roomteams= &roomleaderstart= &platform= next to the room, the seat, the plan, the teams, the people
//     to wait for and the name), each through its own whitelist, only with a valid door, and in the order that the game is given them;
//   * the two pages agree: the six maps (keys and files, which exist in Original-Ants/Maps), and every address that the lobby can make for a game on this computer is read by the game page as the same
//     map, the same bot in each of the three other bases and the same name; every address of a ROOM that the room panel's old addresses can make (every room of 2 - 4 players, every set of levels,
//     every team) is read by the game page as the same --fill-bots plan and the same --teams; every create block that the page can put into a link (every map, 2 - 4 seats, every team that the seats offer, the
//     leader-starts flag) is read by the game page as the same --room-map, --room-seats, --room-teams and --room-leader-start, and by the page itself as the same block.
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
    'return { MAPS: MAPS, DEFAULT_MAP_KEY: DEFAULT_MAP_KEY, LOCAL_PAGE: LOCAL_PAGE, playersChoice: playersChoice, hostPlayers: hostPlayers, soloBots: soloBots, soloSeats: soloSeats, soloSeatsText: soloSeatsText, localGameQuery: localGameQuery, validFill: validFill, validFillPlan: validFillPlan, hostSeats: hostSeats, hostFillText: hostFillText, hostTeamChoices: hostTeamChoices, hostTeam: hostTeam, hostTeamText: hostTeamText, validRoomTeams: validRoomTeams, hostPlanText: hostPlanText, codeText: codeText, roomBlockQuery: roomBlockQuery, roomBlockOf: roomBlockOf, SEAT_COLOURS: SEAT_COLOURS, teamTitle: teamTitle, CARD_KEY: CARD_KEY, CARD_ROOM_KEY: CARD_ROOM_KEY, CARD_WORDS: CARD_WORDS, cardPlaying: cardPlaying, cardFriends: cardFriends, cardPeople: cardPeople, cardSidesFix: cardSidesFix, cardSidesOf: cardSidesOf, cardPair: cardPair, cardTeamOf: cardTeamOf, cardShownSides: cardShownSides, cardSideOpen: cardSideOpen, cardRefusal: cardRefusal, cardSide: cardSide, cardTeamsText: cardTeamsText, cardFix: cardFix, cardNew: cardNew, cardParse: cardParse, cardText: cardText, cardFromOld: cardFromOld, cardSit: cardSit, cardSet: cardSet, cardMap: cardMap, cardFill: cardFill, cardBlock: cardBlock, cardRoomFits: cardRoomFits, cardQuery: cardQuery, cardAlone: cardAlone, cardNote: cardNote };',
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

        // a room code is a name and nothing more: eight characters that a screen shows in two groups of four (the plain code is what a link and a field hold)
        same('codeText: eight characters are two groups of four ("k7m2xq9p" is "k7m2 xq9p"); a code of any other length is shown as it is',
             [L.codeText('k7m2xq9p'), L.codeText('abcdefgh'), L.codeText('A_b-1234'), L.codeText('k7m2xq9'), L.codeText('k7m2xq9pz'), L.codeText('a'), L.codeText('x'.repeat(32)), L.codeText('x-k7m2xq9p-xyz'), L.codeText('')],
             ['k7m2 xq9p', 'abcd efgh', 'A_b- 1234', 'k7m2xq9', 'k7m2xq9pz', 'a', 'x'.repeat(32), 'x-k7m2xq9p-xyz', '']);
        same('codeText: a value that is no text shows nothing', [L.codeText(null), L.codeText(undefined), L.codeText(5), L.codeText({}), L.codeText(['k7m2xq9p'])], ['', '', '', '', '']);
        check('codeText: only one blank is added, in the middle (the plain code is what it was made of)', ['k7m2xq9p', 'abcdefgh', 'Room_1-B', '23456789'].every((c) => L.codeText(c).replace(' ', '') === c && L.codeText(c).length === 9 && L.codeText(c).charAt(4) === ' '));
        {   // the game page's name step shows a code the same way as the front page does (ANTS_PAGE.codeText and the lobby's say the same on every kind of text)
            const TEXTS = ['k7m2xq9p', 'K7M2XQ9P', 'abcdefgh', 'A_b-1234', 'mid-room', 'k7m2xq9', 'k7m2xq9pz', 'k7m2 xq9p', 'a', 'x'.repeat(32), 'demo-small-2p-x7k2', '', null, undefined, 5, {}, ['k7m2xq9p']];
            const differ = TEXTS.filter((t) => P.codeText(t) !== L.codeText(t));
            check('the game page shows a code as the front page does: eight characters in two groups of four, any other text as it is, no text as nothing (' + differ.length + ' of ' + TEXTS.length + ' differ)', differ.length === 0);
            same('the game page\'s codeText: "k7m2xq9p" is "k7m2 xq9p", seven and nine characters stay as they are, a value that is no text is nothing', [P.codeText('k7m2xq9p'), P.codeText('k7m2xq9'), P.codeText('k7m2xq9pz'), P.codeText(null)], ['k7m2 xq9p', 'k7m2xq9', 'k7m2xq9pz', '']);
        }

        // the create block of a room (protocol 15): the text that every link of the room carries after the code, and what an address's block says
        same('roomBlockQuery: the map and the seats, then the teams and the leader-starts flag when they are set (the + of the teams is encoded: an address reads a bare one as a blank)',
             [L.roomBlockQuery(BLOCK('treasure', 4, '', false)), L.roomBlockQuery(BLOCK('small', 2, '', false)), L.roomBlockQuery(BLOCK('tiny', 3, '0+2', false)), L.roomBlockQuery(BLOCK('islands', 4, '', true)), L.roomBlockQuery(BLOCK('gauntlet', 4, '1+3', true))],
             ['&roommap=treasure&roomseats=4', '&roommap=small&roomseats=2', '&roommap=tiny&roomseats=3&roomteams=0%2B2', '&roommap=islands&roomseats=4&roomleaderstart=1', '&roommap=gauntlet&roomseats=4&roomteams=1%2B3&roomleaderstart=1']);
        same('roomBlockQuery: a room that was made some other way has no block, and no text', [L.roomBlockQuery(null), L.roomBlockQuery(undefined), L.roomBlockQuery(''), L.roomBlockQuery(5), L.roomBlockQuery(false), L.roomBlockQuery('map')], ['', '', '', '', '', '']);
        same('roomBlockQuery: a block with no map has no &roommap= (the server picks its own); a part that is no good is the default one, and never goes into a link as it came',
             [L.roomBlockQuery(BLOCK('', 3, '', false)), L.roomBlockQuery(BLOCK('nowhere', 3, '', false)), L.roomBlockQuery(BLOCK('Treasure', 3, '', false)), L.roomBlockQuery(BLOCK('&room=x', 3, '', false)), L.roomBlockQuery(BLOCK(undefined, 3, '', false)),
              L.roomBlockQuery(BLOCK('tiny', 7, '', false)), L.roomBlockQuery(BLOCK('tiny', '3', '', false)), L.roomBlockQuery(BLOCK('tiny', undefined, '', false)), L.roomBlockQuery(BLOCK('tiny', 1, '', false)),
              L.roomBlockQuery(BLOCK('tiny', 4, 'ffa', false)), L.roomBlockQuery(BLOCK('tiny', 4, '0+0', false)), L.roomBlockQuery(BLOCK('tiny', 4, '0+1&x=1', false)), L.roomBlockQuery(BLOCK('tiny', 4, null, false)),
              L.roomBlockQuery(BLOCK('tiny', 4, '', 'yes')), L.roomBlockQuery(BLOCK('tiny', 4, '', 1)), L.roomBlockQuery(BLOCK('tiny', 4, '', undefined)), L.roomBlockQuery({ map: 'tiny' })],
             ['&roomseats=3', '&roomseats=3', '&roomseats=3', '&roomseats=3', '&roomseats=3',
              '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4',
              '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4',
              '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4', '&roommap=tiny&roomseats=4']);
        same('roomBlockQuery: a pair of seats the wrong way round is written with the lower seat first (a pair has one spelling, as in the block)', L.roomBlockQuery(BLOCK('tiny', 4, '3+1', false)), '&roommap=tiny&roomseats=4&roomteams=1%2B3');
        same('the six keys are the six map files by the game\'s rule for --room-map (the word in capitals and .LVL)', L.MAPS.map((m) => m.key.toUpperCase() + '.LVL').sort(), Object.values(FILES).sort());
        same('roomBlockOf: the block of the page\'s own links is read back (the key in any case, the seats 2 - 4, the teams as A+B, a + that came as a blank, the flag exactly 1)',
             [L.roomBlockOf('?join=/ws&room=k7m2xq9p&roommap=treasure&roomseats=4'), L.roomBlockOf('?room=k7m2xq9p&roommap=TINY&roomseats=2'), L.roomBlockOf('?room=k7m2xq9p&roommap=Islands&roomseats=3&roomteams=0%2B2'), L.roomBlockOf('?room=k7m2xq9p&roommap=small&roomseats=3&roomteams=1+2'),
              L.roomBlockOf('?room=k7m2xq9p&roommap=gauntlet&roomseats=4&roomleaderstart=1&fill=easy&teams=0%2B1&name=Bob'), L.roomBlockOf('roommap=medium&roomseats=2')],
             [BLOCK('treasure', 4, '', false), BLOCK('tiny', 2, '', false), BLOCK('islands', 3, '0+2', false), BLOCK('small', 3, '1+2', false), BLOCK('gauntlet', 4, '', true), BLOCK('medium', 2, '', false)]);
        same('roomBlockOf: what an address leaves out is the server\'s own: no map, four seats, free for all, not the leader\'s start (any one of the four makes the block)',
             [L.roomBlockOf('?room=a&roomseats=3'), L.roomBlockOf('?room=a&roommap=gauntlet'), L.roomBlockOf('?room=a&roomteams=0%2B3'), L.roomBlockOf('?room=a&roomleaderstart=1'), L.roomBlockOf('?room=a&roommap=nowhere&roomseats=3'), L.roomBlockOf('?room=a&roommap=small&roomseats=9')],
             [BLOCK('', 3, '', false), BLOCK('gauntlet', 4, '', false), BLOCK('', 4, '0+3', false), BLOCK('', 4, '', true), BLOCK('', 3, '', false), BLOCK('small', 4, '', false)]);
        same('roomBlockOf: an address with none of the four parameters has no block: the room was made some other way (null)',
             ['', '?', '?room=k7m2xq9p', '?room=k7m2xq9p&fill=easy&teams=0%2B1', '?join=/ws&room=abc&seat=1&name=Bob', '?map=small&players=2', '?platform=linux'].map((s) => L.roomBlockOf(s)), Array(7).fill(null));
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
                const address = '?join=/ws&room=k7m2xq9p' + query + '&seat=0&name=Bob';
                const args = P.joinArguments(address, true, 'play.test').args;
                const want = ['--join-url', 'wss://play.test/ws', '--room', 'k7m2xq9p', '--room-map', m.key, '--room-seats', String(seats), ...(teams ? ['--room-teams', teams] : []), ...(leaderStart ? ['--room-leader-start'] : []), '--seat', '0', '--name', 'Bob'];
                blocks++;
                if (teams) withTeams++;
                if (leaderStart) flagged++;
                if (JSON.stringify(args) !== JSON.stringify(want)) wrong += ' ' + address + ' -> ' + JSON.stringify(args);
                if (JSON.stringify(L.roomBlockOf(address)) !== JSON.stringify(block)) wrong += ' (not read back) ' + address;
                if (L.hostTeam(teams || 'ffa', seats) !== (teams || 'ffa') || query.indexOf('+') !== -1) wrong += ' (not a block that the card can make) ' + address;
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
                    const address = '?join=/ws&room=k7m2xq9p' + (plan ? '&fill=' + plan : '') + (team ? '&teams=' + encodeURIComponent(team) : '') + '&name=Bob';
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
                join: ['/ws', '/ws', '/ws', '/ws/a', '/other', '//evil/ws', '/ws/../x', 'ws://evil/', ''], room: ['abc', 'k7m2xq9p', 'my_room-1', 'ABC', 'a b', '', 'x'.repeat(33), 'a&b'],
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

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // the card, "New match" (the block CARD): four seats, one of them You, a choice for each of the others
    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        // a state of the card as the rules make it: `teams` is one that the seats allow (a pair of seats that play, three or four of them playing); the two seats of it are on Team 1 unless `sides` says otherwise
        const sidesOf = (you, seats, teams) => {
            const playing = [0, 1, 2, 3].filter((x) => x === you || seats[x] !== 'nobody');
            const out = [0, 0, 0, 0];
            if (teams && teams !== 'ffa' && playing.length >= 3) for (const x of teams.split('+').map(Number)) out[x] = 1;
            return out;
        };
        const st = (you, seats, teams, map, sides) => ({ map: map || 'treasure', you, seats, sides: sides || sidesOf(you, seats, teams), teams: teams || 'ffa' });
        const M4 = ['medium', 'medium', 'medium', 'medium'];
        const F4 = ['friend', 'friend', 'friend', 'friend'];          // (a first visit: a Friend in every seat)
        const COLOURS = ['Green', 'Red', 'Blue', 'Black'];
        // every team that the switches can make for the seats that play, as an independent list: free for all, the three pairs of three seats, Green with each of the others when four play
        const teamsOf = (state) => { const p = L.cardPlaying(state); return p.length === 4 ? ['ffa', '0+1', '0+2', '0+3'] : p.length === 3 ? ['ffa', p[0] + '+' + p[1], p[0] + '+' + p[2], p[1] + '+' + p[2]] : ['ffa']; };
        const frozen = (state) => JSON.stringify(state);
        // a state with the two seats of a team word on Team 1 (the page keeps the switches; only an older save has a team and no switches, and cardFix makes those)
        const withTeams = (state, team) => L.cardFix(Object.assign({}, state, { sides: L.cardSidesOf(team, L.cardPlaying(state)) }));

        same('the card has five choices for a seat, in the order of its buttons, and keeps its choices under ants-match', [L.CARD_WORDS, L.CARD_KEY, L.CARD_ROOM_KEY], [['friend', 'easy', 'medium', 'hard', 'nobody'], 'ants-match', 'ants-match-room']);
        same('cardNew: Treasure, You at Green, a Friend in every seat (the seat of You keeps its choice for later), free for all; a map that is none of the six is Treasure', [L.cardNew('treasure'), L.cardNew('islands').map, L.cardNew(undefined).map, L.cardNew('nowhere').map, L.cardNew('Treasure').map],
             [st(0, F4), 'islands', 'treasure', 'treasure', 'treasure']);

        // who plays, and how many people the room waits for: You, and every seat that is not Nobody; the people are You and the Friends (the bots come with START)
        same('cardPlaying: You and every seat that is not Nobody, in the order of the seats (a Nobody seat that is You plays: You are in it)',
             [L.cardPlaying(st(0, ['medium', 'nobody', 'friend', 'nobody'])), L.cardPlaying(st(1, ['medium', 'nobody', 'friend', 'nobody'])), L.cardPlaying(st(3, ['nobody', 'nobody', 'nobody', 'nobody'])), L.cardPlaying(st(2, ['easy', 'hard', 'nobody', 'friend']))],
             [[0, 2], [0, 1, 2], [3], [0, 1, 2, 3]]);
        same('cardFriends and cardPeople: the seats with Friend (not the seat that is You), and You with them', [L.cardFriends(st(2, ['friend', 'friend', 'friend', 'friend'])), L.cardFriends(st(0, ['friend', 'easy', 'nobody', 'friend'])), L.cardFriends(st(1, M4)), L.cardPeople(st(2, ['friend', 'friend', 'friend', 'friend'])), L.cardPeople(st(0, ['friend', 'easy', 'nobody', 'friend'])), L.cardPeople(st(1, M4))],
             [[0, 1, 3], [3], [], 4, 2, 1]);

        // the Teams: a Team 1 and a Team 2 switch on each seat that plays (three or four of them play); a team is two seats on the same switch
        const sideState = (you, seats, sides) => L.cardFix({ map: 'treasure', you, seats, sides, teams: 'ffa' });
        const ONE = ['medium', 'medium', 'medium', 'nobody'];            // (Green, Red and Blue play: three)
        same('cardSidesFix: only the seats that play have a switch on, from three seats on (two would be the whole match); what is no 1 or 2 is neither; a list that puts three seats on a switch is no list',
             [L.cardSidesFix([1, 1, 2, 2], [0, 1, 2, 3]), L.cardSidesFix([1, 1, 2, 2], [0, 1, 3]), L.cardSidesFix([1, 1, 2, 2], [0, 1]), L.cardSidesFix([1, 1, 1, 0], [0, 1, 2, 3]), L.cardSidesFix([2, 2, 2, 0], [0, 1, 2]), L.cardSidesFix([3, '1', null, 2], [0, 1, 2, 3]), L.cardSidesFix(undefined, [0, 1, 2, 3]), L.cardSidesFix('1122', [0, 1, 2, 3]), L.cardSidesFix([1, 1, 2, 2], [0, 1, 2, 3])],
             [[1, 1, 2, 2], [1, 1, 0, 2], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 2], [0, 0, 0, 0], [0, 0, 0, 0], [1, 1, 2, 2]]);
        same('cardSidesOf: a team value puts its two seats on Team 1 (free for all, a pair with a seat that does not play, a pair of two seats only, the higher seat first and anything else are no switch)',
             [L.cardSidesOf('0+1', [0, 1, 2, 3]), L.cardSidesOf('1+3', [1, 2, 3]), L.cardSidesOf('ffa', [0, 1, 2, 3]), L.cardSidesOf('0+1', [0, 2, 3]), L.cardSidesOf('0+1', [0, 1]), L.cardSidesOf('1+0', [0, 1, 2, 3]), L.cardSidesOf('0+0', [0, 1, 2, 3]), L.cardSidesOf('0+4', [0, 1, 2, 3]), L.cardSidesOf(null, [0, 1, 2, 3]), L.cardSidesOf(['0+1'], [0, 1, 2, 3])],
             [[1, 1, 0, 0], [0, 1, 0, 1], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]]);
        same('cardPair: the two seats that are on the same switch, the lower first; none when no switch has exactly two seats on it',
             [L.cardPair([1, 1, 0, 0], [0, 1, 2, 3]), L.cardPair([0, 2, 0, 2], [0, 1, 2, 3]), L.cardPair([1, 2, 0, 0], [0, 1, 2, 3]), L.cardPair([0, 0, 0, 0], [0, 1, 2, 3]), L.cardPair([2, 1, 1, 0], [0, 1, 2, 3]), L.cardPair([2, 2, 1, 1], [0, 1, 2, 3]), L.cardPair([1, 0, 0, 0], [0, 1, 2])],
             [[0, 1], [1, 3], null, null, [1, 2], [2, 3], null]);
        same('cardTeamOf: what the switches make: the pair when three play; when four play the team that Green is in (two on one switch are Green with the other two when they are not Green, and the other pair is a team too); free for all when no switch has exactly two seats',
             [L.cardTeamOf([1, 1, 0, 0], [0, 1, 2, 3]), L.cardTeamOf([0, 1, 1, 0], [0, 1, 2, 3]), L.cardTeamOf([2, 2, 1, 1], [0, 1, 2, 3]), L.cardTeamOf([0, 1, 1, 0], [1, 2, 3]), L.cardTeamOf([0, 1, 0, 1], [1, 2, 3]), L.cardTeamOf([0, 1, 2, 0], [0, 1, 2, 3]), L.cardTeamOf([0, 0, 0, 0], [0, 1, 2, 3]), L.cardTeamOf([0, 0, 2, 2], [0, 1, 2, 3])],
             ['0+1', '0+3', '0+1', '1+2', '1+3', 'ffa', 'ffa', '0+1']);
        same('cardShownSides: what the switches show: each seat\'s own, and with four players and a pair on one switch the other two seats on the other one (nobody pressed those); with three players the third shows what it has',
             [L.cardShownSides(sideState(0, M4, [1, 1, 0, 0])), L.cardShownSides(sideState(0, M4, [0, 2, 2, 0])), L.cardShownSides(sideState(0, M4, [1, 1, 0, 2])), L.cardShownSides(sideState(0, M4, [1, 0, 0, 2])), L.cardShownSides(sideState(0, ONE, [1, 1, 0, 0])), L.cardShownSides(sideState(0, ONE, [1, 1, 2, 0])), L.cardShownSides(sideState(0, M4, [0, 0, 0, 0]))],
             [[1, 1, 2, 2], [1, 2, 2, 1], [1, 1, 2, 2], [1, 0, 0, 2], [1, 1, 0, 0], [1, 1, 2, 0], [0, 0, 0, 0]]);
        same('cardTeamsText: the line under the switches says what they make: free for all and how to make a team, or the teams in words (the other seats that play are the other team)',
             [L.cardTeamsText(st(0, M4)), L.cardTeamsText(sideState(0, M4, [1, 2, 0, 0])), L.cardTeamsText(st(0, M4, '0+1')), L.cardTeamsText(st(0, M4, '0+3')), L.cardTeamsText(sideState(0, M4, [0, 1, 1, 0])), L.cardTeamsText(st(0, ONE, '1+2')), L.cardTeamsText(st(1, ['nobody', 'easy', 'hard', 'friend'], '2+3'))],
             ['Free for all. For a team, put two colours on the same team.', 'Free for all. For a team, put two colours on the same team.', 'Teams: Green + Red against Blue + Black.', 'Teams: Green + Black against Red + Blue.', 'Teams: Green + Black against Red + Blue.', 'Teams: Red + Blue against Green.', 'Teams: Blue + Black against Red.']);

        // pressing a switch: one that is off goes on, unless its team has two other seats (then nothing happens); one that is lit goes off, unless the other seats keep it lit (the other team of a pair, or a press that
        // a pair has made useless): then the teams go away, so that a press that is accepted always shows
        {
            const free = L.cardNew('treasure');
            const g1 = L.cardSide(free, 0, 1);
            const g1r1 = L.cardSide(g1, 1, 1);
            same('cardSide: Team 1 on Green (a team of one seat is no team), then on Red: Green + Red against Blue + Black, and Blue and Black show Team 2 although nobody pressed it',
                 [g1.sides, g1.teams, g1r1.sides, g1r1.teams, L.cardShownSides(g1r1)], [[1, 0, 0, 0], 'ffa', [1, 1, 0, 0], '0+1', [1, 1, 2, 2]]);
            same('... Team 1 is full: nobody else can press it (nothing happens; Team 2 of Green and Red is full the same way), while Blue and Black can press the Team 2 that they show (that makes it theirs)',
                 [L.cardSideOpen(g1r1, 2, 1), L.cardSideOpen(g1r1, 3, 1), L.cardSideOpen(g1r1, 0, 2), L.cardSideOpen(g1r1, 1, 2), L.cardSideOpen(g1r1, 2, 2), L.cardSideOpen(g1r1, 3, 2), L.cardSideOpen(g1r1, 0, 1), L.cardSideOpen(g1r1, 1, 1)],
                 [false, false, false, false, true, true, true, true]);
            same('... pressing a full switch changes nothing', [L.cardSide(g1r1, 2, 1), L.cardSide(g1r1, 3, 1), L.cardSide(g1r1, 0, 2), L.cardSide(g1r1, 1, 2)], Array(4).fill(g1r1));
            same('... pressing Team 1 on Green again takes Green off it (Red alone on Team 1 is no team: free for all, nothing shown for Blue and Black)', [L.cardSide(g1r1, 0, 1).sides, L.cardSide(g1r1, 0, 1).teams, L.cardShownSides(L.cardSide(g1r1, 0, 1))], [[0, 1, 0, 0], 'ffa', [0, 1, 0, 0]]);
            same('... pressing the Team 2 that Blue shows takes the teams away: every switch is off (free for all)', [L.cardSide(g1r1, 2, 2).sides, L.cardSide(g1r1, 3, 2).sides, L.cardSide(g1r1, 3, 2).teams], [[0, 0, 0, 0], [0, 0, 0, 0], 'ffa']);
            const moved = L.cardSide(L.cardSide(g1r1, 1, 1), 2, 1);
            same('... to put Blue with Green instead: Red off Team 1, then Blue on Team 1 (Green + Blue against Red + Black, Red and Black show Team 2)', [moved.sides, moved.teams, L.cardShownSides(moved)], [[1, 0, 1, 0], '0+2', [1, 2, 1, 2]]);
            const two = L.cardSide(L.cardSide(free, 1, 2), 2, 2);
            same('Team 2 makes a team as well (Red and Blue on Team 2: Red + Blue against Green + Black, which is Green + Black as the team of Green)', [two.sides, two.teams, L.cardShownSides(two)], [[0, 2, 2, 0], '0+3', [1, 2, 2, 1]]);
            const apart = L.cardSide(L.cardSide(free, 0, 1), 1, 2);
            same('one seat on each switch is no team (free for all, nothing is shown for the other two)', [apart.sides, apart.teams, L.cardShownSides(apart)], [[1, 2, 0, 0], 'ffa', [1, 2, 0, 0]]);
            const three = L.cardSide(L.cardSide(L.cardNew('treasure'), 0, 'x'), 0, 3);
            same('a switch that is none of the two (1 and 2) and a seat that is none of the four change nothing', [three, L.cardSide(free, 4, 1), L.cardSide(free, -1, 1), L.cardSide(free, '0', 1), L.cardSide(free, 0, 0), L.cardSide(free, 0, '1'), L.cardSide(free, undefined, 1)], Array(7).fill(free));
            const t3 = L.cardSide(L.cardSide(sideState(0, ONE, [0, 0, 0, 0]), 0, 1), 2, 1);
            same('three seats play: Team 1 on Green and Blue is Green + Blue against Red (Red plays alone and shows nothing); the third seat cannot press that switch; Red may press Team 2, which makes no difference to the teams',
                 [t3.sides, t3.teams, L.cardShownSides(t3), L.cardSideOpen(t3, 1, 1), L.cardSideOpen(t3, 1, 2), L.cardSide(t3, 1, 2).teams, L.cardTeamsText(t3)], [[1, 0, 1, 0], '0+2', [1, 0, 1, 0], false, true, '0+2', 'Teams: Green + Blue against Red.']);
            same('a seat that does not play has no switch (Black is Nobody): pressing it changes nothing, and with two seats playing nothing can be pressed', [L.cardSide(t3, 3, 1), L.cardSide(st(0, ['medium', 'easy', 'nobody', 'nobody']), 0, 1), L.cardSideOpen(t3, 3, 1), L.cardSideOpen(st(0, ['medium', 'easy', 'nobody', 'nobody']), 1, 2)], [t3, st(0, ['medium', 'easy', 'nobody', 'nobody']), false, false]);
            // a press that the other seats hold: Team 1 on Green, then Team 2 on Red and on Blue make Green + Black against Red + Blue, and Green's own press is useless now (Black shows the same lit switch):
            // pressing either of them does the same, it takes the teams away (a press that is accepted shows)
            const held = L.cardSide(L.cardSide(L.cardSide(free, 0, 1), 1, 2), 2, 2);
            same('Team 1 on Green, then Team 2 on Red and on Blue: Green + Black against Red + Blue, Black shows Team 1 and Green\'s own press is held by the other two now', [held.sides, held.teams, L.cardShownSides(held)], [[1, 2, 2, 0], '0+3', [1, 2, 2, 1]]);
            same('... pressing Team 1 on Green and pressing it on Black do the same: every switch goes off (free for all), while Red\'s Team 2 (held by nothing else) goes off alone',
                 [L.cardSide(held, 0, 1).sides, L.cardSide(held, 3, 1).sides, L.cardSide(held, 0, 1).teams, L.cardSide(held, 1, 2).sides, L.cardSide(held, 1, 2).teams], [[0, 0, 0, 0], [0, 0, 0, 0], 'ffa', [1, 0, 2, 0], 'ffa']);
            // why a press does nothing: the line under the switches says it. Two colours that were pressed on the switch: take one off. The two that a pair leaves to it: press a lit switch (the teams go)
            same('cardRefusal: a press that may be made has no reason; a switch of a team that two colours pressed says to take one of them off; the team that a pair leaves to the other two says to clear the teams; a seat with no switch has none',
                 [L.cardRefusal(g1r1, 0, 1), L.cardRefusal(g1r1, 2, 2), L.cardRefusal(g1r1, 2, 1), L.cardRefusal(g1r1, 0, 2), L.cardRefusal(held, 3, 2), L.cardRefusal(held, 0, 2), L.cardRefusal(st(0, ['medium', 'easy', 'nobody', 'nobody']), 1, 1), L.cardRefusal(sideState(0, ONE, [0, 0, 0, 0]), 3, 1), L.cardRefusal(g1r1, 0, 3)],
                 ['', '', 'Team 1 has two colours already. Press one of them to take it off first.', 'Team 2 is the other two colours already. Press a lit switch to clear the teams first.', 'Team 2 has two colours already. Press one of them to take it off first.', 'Team 2 has two colours already. Press one of them to take it off first.', '', '', '']);
            const before = frozen(g1r1);
            L.cardSide(g1r1, 2, 2); L.cardSide(g1r1, 0, 1); L.cardShownSides(g1r1);
            check('... and the state that was given is left as it was (cardSide makes a new state)', frozen(g1r1) === before);
        }
        {   // every state of the card with any switches: pressing is closed (the result is a valid state), moves only the switch that was pressed, and never makes a team of three; the teams are what the switches make
            const W3 = ['friend', 'easy', 'nobody'];
            let states = 0;
            let presses = 0;
            let bad = '';
            const note = (what) => { if (bad.length < 400) bad += ' ' + what; };
            const oracle = (state) => {           // (what the switches make, written again: the pair is the two seats that are on one switch; with four the team that Green is in)
                const p = L.cardPlaying(state);
                if (p.length < 3) return 'ffa';
                for (const k of [1, 2]) {
                    const on = p.filter((x) => state.sides[x] === k);
                    if (on.length !== 2) continue;
                    const rest = p.filter((x) => on.indexOf(x) === -1);
                    return (p.length === 4 && on[0] !== 0 ? rest : on).join('+');
                }
                return 'ffa';
            };
            for (let you = 0; you < 4; you++) for (const a of W3) for (const b of W3) for (const c of W3) for (const d of W3) for (let code = 0; code < 81; code++) {
                const sides = [code % 3, Math.floor(code / 3) % 3, Math.floor(code / 9) % 3, Math.floor(code / 27) % 3];
                const state = L.cardFix({ map: 'treasure', you, seats: [a, b, c, d], sides, teams: 'ffa' });
                states++;
                const playing = L.cardPlaying(state);
                if (frozen(L.cardFix(state)) !== frozen(state)) note('fix ' + frozen(state));
                if (state.teams !== oracle(state)) note('teams ' + frozen(state));
                if (state.sides.some((v, x) => v !== 0 && (playing.indexOf(x) === -1 || playing.length < 3)) || [1, 2].some((k) => state.sides.filter((v) => v === k).length > 2)) note('sides ' + frozen(state));
                if (state.teams !== 'ffa' && (L.validRoomTeams(state.teams) !== state.teams || state.teams.split('+').some((x) => playing.indexOf(Number(x)) === -1) || (playing.length === 4 && state.teams.charAt(0) !== '0'))) note('team ' + frozen(state));
                const shown = L.cardShownSides(state);
                if (playing.some((x) => shown[x] === 0 && state.teams !== 'ffa' && playing.length === 4)) note('shown ' + frozen(state));
                for (let seat = 0; seat < 4; seat++) for (const side of [1, 2]) {
                    presses++;
                    const out = L.cardSide(state, seat, side);
                    const open = L.cardSideOpen(state, seat, side);
                    const others = playing.filter((x) => x !== seat && shown[x] === side).length;
                    const want = playing.length >= 3 && playing.indexOf(seat) !== -1 && (shown[seat] === side || others < 2);
                    if (open !== want) note('open ' + frozen(state) + ' ' + seat + side);
                    if (frozen(L.cardFix(out)) !== frozen(out)) note('closed ' + frozen(state) + ' ' + seat + side);
                    if (out.map !== state.map || out.you !== state.you || frozen(out.seats) !== frozen(state.seats)) note('other parts ' + frozen(state));
                    const why = L.cardRefusal(state, seat, side);
                    if (open !== (why === '') && playing.length >= 3 && playing.indexOf(seat) !== -1) note('reason ' + frozen(state) + ' ' + seat + side + ' ' + why);
                    if (!open && playing.length >= 3 && playing.indexOf(seat) !== -1) {
                        const pressed = playing.filter((x) => state.sides[x] === side).length;                       // (the colours that were pressed on that switch, not the ones that a pair leaves to it)
                        if (why !== (pressed === 2 ? 'Team ' + side + ' has two colours already. Press one of them to take it off first.' : 'Team ' + side + ' is the other two colours already. Press a lit switch to clear the teams first.')) note('which reason ' + frozen(state) + ' ' + seat + side + ' ' + why);
                    }
                    if (!open) { if (frozen(out) !== frozen(state)) note('closed press moved ' + frozen(state) + ' ' + seat + side); continue; }
                    const expected = state.sides.slice();
                    if (shown[seat] !== side) expected[seat] = side;                                    // a switch that is off goes on
                    else {                                                                             // one that is lit goes off, unless the other seats keep it lit: then the teams go away
                        expected[seat] = 0;
                        if (L.cardShownSides(L.cardFix({ map: state.map, you: state.you, seats: state.seats, sides: expected, teams: 'ffa' }))[seat] === side) expected.fill(0);
                    }
                    if (frozen(out.sides) !== frozen(expected)) note('press ' + frozen(state) + ' ' + seat + side + ' -> ' + frozen(out.sides));
                    const after = L.cardShownSides(out);
                    if (frozen(after) === frozen(shown)) note('no change ' + frozen(state) + ' ' + seat + side);               // (a press that is accepted always shows: some switch goes on or off)
                    if (after[seat] !== (shown[seat] === side ? 0 : side)) note('the switch ' + frozen(state) + ' ' + seat + side + ' is not ' + (shown[seat] === side ? 'off' : 'on') + ' after its press');
                    if ([1, 2].some((k) => playing.filter((x) => after[x] === k).length > 2)) note('three ' + frozen(out));
                }
            }
            check('every state of the card with any switches (' + states + ' of them, ' + presses + ' presses): the teams are what the switches make (the pair; with four the team of Green; a value that a create block can say), a press is allowed exactly when the seat plays and its team has fewer than two other seats, an allowed press always shows (an off switch goes on; a lit one goes off, or takes every switch off when the other seats keep it lit), a refused one changes nothing and says why, the result is a valid state, and no team ever has three seats:' + bad, bad === '' && states === 4 * 81 * 81);
        }
        {   // the way in and the way out: any two seats make a team with two presses, and any state comes back to free for all by pressing the lit switches
            let bad = '';
            let reached = 0;
            for (let you = 0; you < 4; you++) for (const a of L.CARD_WORDS) for (const b of L.CARD_WORDS) for (const c of L.CARD_WORDS) for (const d of L.CARD_WORDS) {
                const state = L.cardFix(st(you, [a, b, c, d]));
                const playing = L.cardPlaying(state);
                if (playing.length < 3) { if (L.cardSide(L.cardSide(state, playing[0], 1), playing[1] === undefined ? 0 : playing[1], 1).teams !== 'ffa') bad += ' two ' + frozen(state); continue; }
                for (const x of playing) for (const y of playing) for (const side of [1, 2]) {
                    if (x >= y) continue;
                    const out = L.cardSide(L.cardSide(state, x, side), y, side);
                    const rest = playing.filter((z) => z !== x && z !== y);
                    const want = playing.length === 4 && x !== 0 ? rest.join('+') : x + '+' + y;
                    reached++;
                    if (out.teams !== want) bad += ' make ' + frozen(state) + ' ' + x + y + side + ' -> ' + out.teams;
                    let now = out;
                    for (let n = 0; n < 5 && now.sides.some((v) => v !== 0); n++) now = L.cardSide(now, now.sides.findIndex((v) => v !== 0), now.sides[now.sides.findIndex((v) => v !== 0)]);
                    if (now.teams !== 'ffa' || now.sides.some((v) => v !== 0)) bad += ' undo ' + frozen(out);
                }
            }
            check('two presses make any team of the seats that play (' + reached + ' ways), and pressing the switches that are on, one by one, ends in free for all with no switch on:' + bad.slice(0, 300), bad === '' && reached > 10000);
        }

        // a state that is repaired: every part of it is valid
        same('cardFix: a map that is none of the six is Treasure, You is a seat, a word that is none of the five is Friend, a team that the seats do not allow is free for all, a short list is filled up with Friend (a state of the first versions of the card has only its team: the two seats of it go on Team 1)',
             [L.cardFix({ map: 'nowhere', you: 7, seats: ['easy', 'x', 'HARD', null], teams: '0+1' }), L.cardFix({ map: 'small', you: 2, seats: ['easy'], teams: 'junk' }), L.cardFix({ map: 'tiny', you: 3, seats: ['friend', 'friend', 'friend', 'friend'], teams: '0+3' }),
              L.cardFix({ map: 'small', you: 0, seats: ['easy', 'nobody', 'nobody', 'nobody'], teams: '0+1' }), L.cardFix({ map: 'small', you: 0, seats: ['easy', 'nobody', 'friend', 'hard'], teams: '1+2' })],
             [st(0, ['easy', 'friend', 'friend', 'friend'], '0+1'), st(2, ['easy', 'friend', 'friend', 'friend'], 'ffa', 'small'), st(3, ['friend', 'friend', 'friend', 'friend'], '0+3', 'tiny'),
              st(0, ['easy', 'nobody', 'nobody', 'nobody'], 'ffa', 'small'), st(0, ['easy', 'nobody', 'friend', 'hard'], 'ffa', 'small')]);
        same('... the switches rule when a state has them (the team that it says is not read), and are read when they are four numbers from 0 to 2 only: a pair of Red and Blue with four players is Green + Black as a team (the team that Green is in); three seats on a switch are none',
             [L.cardFix({ map: 'small', you: 0, seats: F4, sides: [1, 1, 0, 0], teams: '0+2' }).teams, L.cardFix({ map: 'small', you: 0, seats: F4, sides: [0, 1, 1, 0], teams: 'ffa' }), L.cardFix({ map: 'small', you: 0, seats: F4, sides: [1, 1, 1, 0], teams: '0+1' }), L.cardFix({ map: 'small', you: 0, seats: F4, teams: '1+2' }), L.cardFix({ map: 'small', you: 0, seats: F4, sides: 'x', teams: '1+2' }).sides],
             ['0+1', st(0, F4, '0+3', 'small', [0, 1, 1, 0]), st(0, F4, 'ffa', 'small', [0, 0, 0, 0]), st(0, F4, '0+3', 'small', [0, 1, 1, 0]), [0, 1, 1, 0]]);
        {
            const before = { map: 'small', you: 1, seats: ['easy', 'x', 'x', 'x'], sides: [1, 2, 1, 2], teams: '9' };
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
                for (const team of teamsOf(state)) {
                    const full = withTeams(state, team);
                    made++;
                    if (frozen(L.cardParse(L.cardText(full))) !== frozen(full)) bad += ' ' + L.cardText(full);
                    // (and with the switches pressed by hand: Team 2 for the pair, or one switch on its own)
                    for (const x of L.cardPlaying(full)) {
                        const pressed = L.cardSide(full, x, 2);
                        made++;
                        if (frozen(L.cardParse(L.cardText(pressed))) !== frozen(pressed)) bad += ' ' + L.cardText(pressed);
                    }
                }
            }
            check('cardParse reads back what cardText wrote for ' + made + ' states (every map, every You, every team and the switches pressed by hand):' + bad.slice(0, 200), bad === '' && made > 500);
        }
        same('cardText is JSON with the five parts (what the browser holds under ants-match): the map, You, the seats, the team that the switches make (for an older page of the card) and the switches', JSON.parse(L.cardText(st(2, ['easy', 'friend', 'nobody', 'hard'], '0+1', 'islands'))), { map: 'islands', you: 2, seats: ['easy', 'friend', 'nobody', 'hard'], teams: '0+1', sides: [1, 1, 0, 0] });
        same('... in this order, so that the text is the same whenever the same choices were made', L.cardText(st(2, ['easy', 'friend', 'nobody', 'hard'], '0+1', 'islands')), '{"map":"islands","you":2,"seats":["easy","friend","nobody","hard"],"teams":"0+1","sides":[1,1,0,0]}');
        for (const junk of [null, undefined, '', 'junk', '[]', '{}', 'null', '5', '"x"', '{"map":"treasure"}', '{"map":"Treasure","you":0,"seats":["medium","medium","medium","medium"],"teams":"ffa"}', '{"map":"nowhere","you":0,"seats":["medium","medium","medium","medium"],"teams":"ffa"}',
                            '{"map":"treasure","you":"0","seats":["medium","medium","medium","medium"],"teams":"ffa"}', '{"map":"treasure","you":4,"seats":["medium","medium","medium","medium"],"teams":"ffa"}', '{"map":"treasure","you":-1,"seats":["medium","medium","medium","medium"],"teams":"ffa"}',
                            '{"map":"treasure","you":0,"seats":["medium","medium","medium"],"teams":"ffa"}', '{"map":"treasure","you":0,"seats":["medium","medium","medium","medium","medium"],"teams":"ffa"}', '{"map":"treasure","you":0,"seats":["medium","medium","medium","HARD"],"teams":"ffa"}',
                            '{"map":"treasure","you":0,"seats":["medium","medium","medium","none"],"teams":"ffa"}', '{"map":"treasure","you":0,"seats":["medium","medium","medium",null],"teams":"ffa"}', '{"map":"treasure","you":0,"seats":"medium,medium,medium,medium","teams":"ffa"}',
                            '{"map":"treasure","you":0,"seats":{"0":"medium","1":"medium","2":"medium","3":"medium","length":4},"teams":"ffa"}', '{"map":["treasure"],"you":0,"seats":["medium","medium","medium","medium"],"teams":"ffa"}', 5, {}, ['treasure']]) {
            same('cardParse: ' + String(JSON.stringify(junk)).slice(0, 60) + ' is no state', L.cardParse(junk), null);
        }
        same('cardParse: a team that is none of the choices of those seats is free for all (the state is still read), and a missing team is free for all', [L.cardParse('{"map":"treasure","you":0,"seats":["medium","nobody","friend","nobody"],"teams":"0+1"}').teams, L.cardParse('{"map":"treasure","you":0,"seats":["medium","medium","friend","hard"]}').teams, L.cardParse('{"map":"treasure","you":0,"seats":["medium","medium","friend","hard"],"teams":7}').teams], ['ffa', 'ffa', 'ffa']);
        {
            const seats = '["medium","medium","friend","hard"]';
            const read = (extra) => L.cardParse('{"map":"treasure","you":0,"seats":' + seats + extra + '}');
            same('cardParse: a state of the first versions of the card (a team and no switches) puts the two seats of the team on Team 1; the switches are read when there are four numbers from 0 to 2, and then the team that is written is not looked at',
                 [read(',"teams":"0+3"'), read(',"teams":"1+2"').sides, read(',"teams":"0+3","sides":[2,2,0,0]'), read(',"teams":"0+3","sides":[1,1,1,0]').sides, read(',"teams":"0+3","sides":[1,1,0]').sides, read(',"teams":"0+3","sides":"1100"').sides, read(',"teams":"0+3","sides":[1,1,0,3]').sides, read(',"sides":[1,"1",0,0]').sides, read(',"sides":[0,0,0,0],"teams":"0+3"').teams],
                 [st(0, ['medium', 'medium', 'friend', 'hard'], '0+3'), [0, 1, 1, 0], st(0, ['medium', 'medium', 'friend', 'hard'], '0+1', 'treasure', [2, 2, 0, 0]), [0, 0, 0, 0], [1, 0, 0, 1], [1, 0, 0, 1], [1, 0, 0, 1], [0, 0, 0, 0], 'ffa']);
        }

        // the first visit, from what the earlier pages left in the browser (read, never written)
        const OLD = (o) => Object.assign({ solo: null, legacy: null, soloTeams: null, map: null, players: null, fill: null, teams: null }, o);
        same('cardFromOld: nothing remembered is a first visit (and so is nothing at all)', [L.cardFromOld(OLD({})), L.cardFromOld({}), L.cardFromOld(undefined), L.cardFromOld(null)], [st(0, F4), st(0, F4), st(0, F4), st(0, F4)]);
        same('... the map is the last one that was played or hosted (ants-four-map), and Treasure when that is none of the six', [L.cardFromOld(OLD({ map: 'islands' })).map, L.cardFromOld(OLD({ map: 'junk' })).map, L.cardFromOld(OLD({ map: 5 })).map, L.cardFromOld(OLD({ map: 'TINY' })).map], ['islands', 'treasure', 'treasure', 'treasure']);
        same('a browser that played a game on this computer: the opponents of Red, Blue and Black are its levels (None is Nobody), You at Green, Green\'s own seat a Friend (the first visit\'s)',
             [L.cardFromOld(OLD({ solo: 'easy,none,hard' })), L.cardFromOld(OLD({ solo: 'none,none,none' })), L.cardFromOld(OLD({ solo: 'EASY,Medium,none' }))],
             [st(0, ['friend', 'easy', 'nobody', 'hard']), st(0, ['friend', 'nobody', 'nobody', 'nobody']), st(0, ['friend', 'easy', 'medium', 'nobody'])]);
        same('... the first versions\' key gives its one level to the three seats (none is Nobody in all three), and the newer key beats it', [L.cardFromOld(OLD({ legacy: 'hard' })), L.cardFromOld(OLD({ legacy: 'none' })), L.cardFromOld(OLD({ legacy: 'junk' })), L.cardFromOld(OLD({ solo: 'easy,none,hard', legacy: 'medium' }))],
             [st(0, ['friend', 'hard', 'hard', 'hard']), st(0, ['friend', 'nobody', 'nobody', 'nobody']), st(0, ['friend', 'nobody', 'nobody', 'nobody']), st(0, ['friend', 'easy', 'nobody', 'hard'])]);
        same('... a remembered list that is not three levels is not used: the old key is, else Medium (as the old page did)', [L.cardFromOld(OLD({ solo: 'easy,hard', legacy: 'hard' })).seats, L.cardFromOld(OLD({ solo: 'junk' })).seats], [['friend', 'hard', 'hard', 'hard'], ['friend', 'medium', 'medium', 'medium']]);
        same('... and its team (You with a seat that plays) is kept when it is still a choice, else free for all', [L.cardFromOld(OLD({ solo: 'easy,medium,hard', soloTeams: '0+3' })).teams, L.cardFromOld(OLD({ solo: 'easy,none,hard', soloTeams: '0+3' })).teams, L.cardFromOld(OLD({ solo: 'easy,none,hard', soloTeams: '0+2' })).teams, L.cardFromOld(OLD({ solo: 'easy,medium,hard', soloTeams: 'junk' })).teams, L.cardFromOld(OLD({ solo: 'easy,none,none', soloTeams: '0+1' })).teams],
             ['0+3', '0+3', 'ffa', 'ffa', 'ffa']);
        same('a browser that only hosted rooms: the room that it had: a seat of it that had no bot is a Friend, one with a level is that bot, a seat beyond the room is Nobody (a 1 is 2 players; no players is 2)',
             [L.cardFromOld(OLD({ players: '3', fill: 'none,easy,none,hard' })), L.cardFromOld(OLD({ players: '2' })), L.cardFromOld(OLD({ players: '4', fill: 'medium' })), L.cardFromOld(OLD({ players: '1' })), L.cardFromOld(OLD({ fill: 'none,none,none,none' })), L.cardFromOld(OLD({ players: '4', fill: 'none,hard,none,easy' }))],
             [st(0, ['friend', 'easy', 'friend', 'nobody']), st(0, ['friend', 'friend', 'nobody', 'nobody']), st(0, ['friend', 'medium', 'medium', 'medium']), st(0, ['friend', 'friend', 'nobody', 'nobody']), st(0, ['friend', 'friend', 'nobody', 'nobody']), st(0, ['friend', 'hard', 'friend', 'easy'])]);
        same('... with its team when the room had one that the card still offers (four players: Green with Black), else free for all (a pair that three seats cannot make)', [L.cardFromOld(OLD({ players: '4', teams: '0+3' })).teams, L.cardFromOld(OLD({ players: '3', teams: '0+3' })).teams, L.cardFromOld(OLD({ players: '3', teams: '0+1' })).teams, L.cardFromOld(OLD({ players: '2', teams: '0+1' })).teams], ['0+3', 'ffa', '0+1', 'ffa']);
        same('both kinds of key: the game on this computer wins (it was the first card); a junk value for players or fill is the default room', [L.cardFromOld(OLD({ solo: 'easy,easy,easy', players: '4', fill: 'hard' })).seats, L.cardFromOld(OLD({ players: 'x', fill: 'x' })).seats], [['friend', 'easy', 'easy', 'easy'], ['friend', 'friend', 'nobody', 'nobody']]);

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
            same('cardMap, and the switches that a team word makes: the map (one of the six), the team (one of the choices of the seats that play: Black is Nobody here, so 0+3 is not one; else free for all)', [L.cardMap(base, 'small').map, L.cardMap(base, 'nowhere').map, L.cardMap(base, 'Small').map, withTeams(base, '0+2').teams, withTeams(base, '0+3').teams, withTeams(st(0, M4), '0+3').teams, withTeams(base, 'junk').teams, withTeams(base, 'ffa').teams],
                 ['small', 'treasure', 'treasure', '0+2', 'ffa', '0+3', 'ffa', 'ffa']);
            check('... and none of them changed the state that it was given', frozen(base) === before);
        }

        // the plan: four words for the seats 0 - 3, a bot's seat its level, none for You, a Friend and Nobody; nothing when no seat has a bot
        same('cardFill: the level of every bot seat, none for You (even when the seat that is You has a level of its own), a Friend and Nobody',
             [L.cardFill(st(0, ['hard', 'easy', 'friend', 'nobody'])), L.cardFill(st(2, ['easy', 'medium', 'hard', 'nobody'])), L.cardFill(st(3, ['medium', 'nobody', 'hard', 'friend'])), L.cardFill(st(1, M4)), L.cardFill(st(0, ['easy', 'hard', 'medium', 'easy']))],
             ['none,easy,none,none', 'easy,medium,none,none', 'medium,none,hard,none', 'medium,none,medium,medium', 'none,hard,medium,easy']);
        same('... and no bot at all is no plan', [L.cardFill(st(0, ['hard', 'friend', 'friend', 'nobody'])), L.cardFill(st(2, ['nobody', 'nobody', 'easy', 'nobody'])), L.cardFill(st(1, ['friend', 'hard', 'friend', 'friend']))], ['', '', '']);

        // the room of the card: its create block, and when the room that the tab kept still fits the choices
        const B4 = '&roommap=treasure&roomseats=4';                // (the block of a card on Treasure, free for all)
        {
            let bad = '';
            let withTeams = 0;
            let made = 0;
            for (const map of L.MAPS) for (const you of [0, 1, 2, 3]) for (const team of ['ffa', '0+1', '0+2', '0+3']) {
                const state = L.cardFix(st(you, M4, team, map.key));
                const block = L.cardBlock(state);
                const query = L.roomBlockQuery(block);
                const want = { map: map.key, seats: 4, teams: state.teams === 'ffa' ? '' : state.teams, leaderStart: false };
                made++;
                if (block.teams) withTeams++;
                if (JSON.stringify(block) !== JSON.stringify(want) || JSON.stringify(L.roomBlockOf('?room=k7m2xq9p' + query)) !== JSON.stringify(want) || !/^&roommap=[a-z]+&roomseats=4(&roomteams=[0-3]%2B[0-3])?$/.test(query)) bad += ' ' + query;
            }
            check('cardBlock: the card\'s map, always four seats (any colour can be taken), the card\'s teams (ffa is none) and never the leader-starts flag, for every map and team (' + withTeams + ' with teams in ' + made + ' blocks); the page reads its link text back whole:' + bad, bad === '' && withTeams === 6 * 4 * 3 && made === 6 * 4 * 4);
            same('cardBlock: free for all is no teams, and the choices of the seats that play (Nobody seats, a seat that is You) do not change the seats of the room: four', [L.cardBlock(st(0, M4, 'ffa', 'small')), L.cardBlock(st(2, M4, '0+1', 'islands')), L.cardBlock(st(0, ['medium', 'nobody', 'nobody', 'nobody'], 'ffa', 'tiny')), L.cardBlock(st(1, ['friend', 'friend', 'friend', 'friend'], '0+3', 'gauntlet'))],
                 [BLOCK('small', 4, '', false), BLOCK('islands', 4, '0+1', false), BLOCK('tiny', 4, '', false), BLOCK('gauntlet', 4, '0+3', false)]);
        }
        {
            const state = st(0, M4, '0+1', 'small');
            const room = { code: 'k7m2xq9p', map: 'small', teams: '0+1', used: {} };
            check('cardRoomFits: the room that was made for these choices fits them', L.cardRoomFits(room, state));
            same('... a room of another map, or of other teams (or none), does not', [{ ...room, map: 'tiny' }, { ...room, teams: '0+2' }, { ...room, teams: 'ffa' }, { ...room, map: 'Small' }, { ...room, teams: '' }].map((r) => L.cardRoomFits(r, state)), Array(5).fill(false));
            same('... free for all fits a room that was made for free for all (ffa), and only that', [L.cardRoomFits({ code: 'k7m2xq9p', map: 'small', teams: 'ffa' }, st(0, M4, 'ffa', 'small')), L.cardRoomFits({ code: 'k7m2xq9p', map: 'small', teams: '0+1' }, st(0, M4, 'ffa', 'small')), L.cardRoomFits({ code: 'k7m2xq9p', map: 'small', teams: '' }, st(0, M4, 'ffa', 'small'))], [true, false, false]);
            same('... a code that is not eight characters of the page\'s alphabet does not fit (the code of an earlier page, another length, capitals, a sign, a blank, a digit that the alphabet has not)',
                 ['demo-small-4p-t01-k7m2xq', 'k7m2xq', 'k7m2xq9', 'k7m2xq9pp', 'K7M2XQ9P', 'k7m2xq9!', 'k7m2 xq9p', 'k7m2xq9p ', ' k7m2xq9p', 'k7m2xq9p\n', 'k7m2xq-p', 'k7m2xq_p', 'k7m2xq90', 'k7m2xq91', '12345678', '', 'constructor'].map((c) => L.cardRoomFits({ ...room, code: c }, state)), Array(17).fill(false));
            same('... and other codes of the alphabet do fit (it is only a name)', ['abcdefgh', 'zzzzzzzz', '22222222', '99999999', 'k7m2xq9p', 'ilo2ilo2'].map((c) => L.cardRoomFits({ ...room, code: c }, state)), Array(6).fill(true));
            same('... an object of an earlier page\'s shape (a code and the links that were sent: nothing of the map and the teams) and a value that is no room fit nothing',
                 [{ code: 'k7m2xq9p', used: {} }, { code: 'demo-small-4p-t01-k7m2xq', used: {} }, { code: 'k7m2xq9p', map: 'small', used: {} }, { code: 'k7m2xq9p', teams: '0+1' }, null, undefined, 5, 'k7m2xq9p', [], {}, { code: 5, map: 'small', teams: '0+1' }, { code: ['k7m2xq9p'], map: 'small', teams: '0+1' }].map((r) => L.cardRoomFits(r, state)), Array(12).fill(false));
            check('... and the room that it was given is left as it was', JSON.stringify(room) === '{"code":"k7m2xq9p","map":"small","teams":"0+1","used":{}}');
        }

        // who is alone, and the line under START
        same('cardAlone: nobody else plays (every other seat is Nobody; a Friend or a bot is a player, and the choice of the seat of You counts for nothing): START then plays a game for one on this computer',
             [L.cardAlone(st(0, ['medium', 'nobody', 'nobody', 'nobody'])), L.cardAlone(st(2, ['nobody', 'nobody', 'hard', 'nobody'])), L.cardAlone(st(3, ['nobody', 'nobody', 'nobody', 'nobody'])), L.cardAlone(st(0, ['medium', 'nobody', 'friend', 'nobody'])), L.cardAlone(st(0, M4)), L.cardAlone(st(1, ['friend', 'nobody', 'nobody', 'nobody']))],
             [true, true, true, false, false, false]);
        same('cardNote: alone it is a game for one on this computer (Green\'s, which the line says when You sit elsewhere); with bots only it starts at once; with one friend or more it waits for them and says what else starts it',
             [L.cardNote(st(0, ['medium', 'nobody', 'nobody', 'nobody'])), L.cardNote(st(2, ['nobody', 'nobody', 'hard', 'nobody'])), L.cardNote(st(0, M4)), L.cardNote(st(0, ['medium', 'friend', 'nobody', 'nobody'])), L.cardNote(st(0, ['medium', 'friend', 'friend', 'hard'])), L.cardNote(st(1, ['friend', 'friend', 'friend', 'friend']))],
             ['Starts at once, on this computer: just you on the map, no opponents.', 'Starts at once, on this computer: just you on the map, no opponents. Alone you play Green.', 'Starts at once, in this tab. Bots gather food, raid and fight back.',
              'Starts when your friend is in (the first player in the room can start sooner).', 'Starts when your friends are in (the first player in the room can start sooner).', 'Starts when your friends are in (the first player in the room can start sooner).']);


        // the addresses: START and an invitation
        {
            const code = 'k7m2xq9p';
            const T12 = B4 + '&roomteams=1%2B2';
            same('cardQuery: START carries the room and its create block (the map, four seats and the teams that the card chose), your seat, the plan, your name (URL-encoded), the shape and the number of people; an invitation carries no name; the teams are in the block, so no teams parameter; no key',
                 [L.cardQuery(st(2, ['easy', 'medium', 'x', 'nobody']), code, 2, 'Ann', '16:9'), L.cardQuery(st(0, ['x', 'friend', 'medium', 'friend'], '1+2'), code, 0, 'A&b <c>=', '4:3'), L.cardQuery(st(0, ['x', 'friend', 'medium', 'friend'], '1+2'), code, 3, null, '16:9'), L.cardQuery(st(0, ['x', 'friend', 'nobody', 'nobody']), code, 1, null, '4:3')],
                 ['?join=/ws&room=k7m2xq9p' + B4 + '&seat=2&fill=easy,medium,none,none&name=Ann&aspect=16:9&start=1',
                  '?join=/ws&room=k7m2xq9p' + T12 + '&seat=0&fill=none,none,medium,none&name=A%26b%20%3Cc%3E%3D&aspect=4:3&start=3',
                  '?join=/ws&room=k7m2xq9p' + T12 + '&seat=3&fill=none,none,medium,none&aspect=16:9&start=3',
                  '?join=/ws&room=k7m2xq9p' + B4 + '&seat=1&aspect=4:3&start=2']);
            same('... the block follows the map and the teams of the card, never the room\'s code: the same code on another map is another block', [L.cardQuery(st(0, M4, 'ffa', 'islands'), code, 0, null, '16:9'), L.cardQuery(st(0, M4, '0+3', 'tiny'), code, 0, null, '16:9')],
                 ['?join=/ws&room=k7m2xq9p&roommap=islands&roomseats=4&seat=0&fill=none,medium,medium,medium&aspect=16:9&start=1', '?join=/ws&room=k7m2xq9p&roommap=tiny&roomseats=4&roomteams=0%2B3&seat=0&fill=none,medium,medium,medium&aspect=16:9&start=1']);
            same('... the shape is 16:9 or 4:3 and nothing else; with an empty name START still has its name parameter (the game page does not ask for it again)', [L.cardQuery(st(0, M4), code, 0, 'x', '21:9'), L.cardQuery(st(0, M4), code, 0, 'x', undefined), L.cardQuery(st(0, M4), code, 0, '', '16:9')],
                 ['?join=/ws&room=k7m2xq9p' + B4 + '&seat=0&fill=none,medium,medium,medium&name=x&aspect=16:9&start=1', '?join=/ws&room=k7m2xq9p' + B4 + '&seat=0&fill=none,medium,medium,medium&name=x&aspect=16:9&start=1', '?join=/ws&room=k7m2xq9p' + B4 + '&seat=0&fill=none,medium,medium,medium&name=&aspect=16:9&start=1']);
            same('... the code is encoded whatever it holds (the page only makes eight characters of its alphabet, which need none)', L.cardQuery(st(0, M4), 'a b&c', 0, null, '16:9'), '?join=/ws&room=a%20b%26c' + B4 + '&seat=0&fill=none,medium,medium,medium&aspect=16:9&start=1');
        }
        {   // every state of the card: the game page reads START and every invitation as the same create block, seat, plan and number of people, no teams parameter, no name in an invitation
            const P2 = P;
            let states = 0;
            let queries = 0;
            let wrong = '';
            const note = (what) => { if (wrong.length < 400) wrong += ' ' + what; };
            for (const mapKey of ['treasure', 'tiny']) for (let you = 0; you < 4; you++) for (const a of L.CARD_WORDS) for (const b of L.CARD_WORDS) for (const c of L.CARD_WORDS) for (const d of L.CARD_WORDS) {
                const base = L.cardFix(st(you, [a, b, c, d], 'ffa', mapKey));
                for (const team of teamsOf(base)) {
                    const state = withTeams(base, team);
                    states++;
                    const code = 'k7m2xq9p';
                    const roomPart = ['--room-map', state.map, '--room-seats', '4', ...(state.teams === 'ffa' ? [] : ['--room-teams', state.teams])];          // (the create block of the card's room: its map, four seats, its teams)
                    const plan = L.cardFill(state);
                    const people = L.cardPeople(state);
                    const playing = L.cardPlaying(state);
                    const friends = L.cardFriends(state);
                    const bots = [0, 1, 2, 3].filter((s) => plan !== '' && plan.split(',')[s] !== 'none');
                    // who is in the match: You, the friends and the bots, each seat once, and no seat that is Nobody
                    if (JSON.stringify([...new Set([you, ...friends, ...bots])].sort()) !== JSON.stringify(playing) || bots.length + people !== playing.length || bots.indexOf(you) !== -1 || friends.some((f) => bots.indexOf(f) !== -1)) note('roster ' + frozen(state));
                    if (L.cardAlone(state) !== (playing.length === 1)) note('alone ' + frozen(state));
                    const args = P2.joinArguments(L.cardQuery(state, code, you, 'Ann', '16:9'), true, 'play.test').args;
                    queries++;
                    const want = ['--join-url', 'wss://play.test/ws', '--room', code, ...roomPart, '--seat', String(you), ...(plan ? ['--fill-bots', plan] : []), '--start-when', String(people), '--name', 'Ann'];
                    if (JSON.stringify(args) !== JSON.stringify(want)) note('start ' + frozen(state) + ' -> ' + JSON.stringify(args));
                    for (const friend of friends) {
                        const invited = P2.joinArguments(L.cardQuery(state, code, friend, null, '16:9'), true, 'play.test').args;
                        queries++;
                        const wantInvite = ['--join-url', 'wss://play.test/ws', '--room', code, ...roomPart, '--seat', String(friend), ...(plan ? ['--fill-bots', plan] : []), '--start-when', String(people)];
                        if (JSON.stringify(invited) !== JSON.stringify(wantInvite) || invited.indexOf('--name') !== -1 || invited.indexOf('--teams') !== -1) note('invite ' + frozen(state) + ' ' + friend + ' -> ' + JSON.stringify(invited));
                    }
                    const carried = L.roomBlockOf(L.cardQuery(state, code, you, 'Ann', '16:9'));
                    if (JSON.stringify(carried) !== JSON.stringify(L.cardBlock(state)) || carried.teams !== (state.teams === 'ffa' ? '' : state.teams) || carried.seats !== 4 || carried.map !== state.map) note('block ' + frozen(state));
                }
            }
            check('every state of the card (' + states + ' of them, with every team that it offers, on two maps) makes a START and ' + (queries - states) + ' invitations that the game page reads as the card meant: the create block (--room-map, four --room-seats, --room-teams), the seat, the plan, the people to wait for (--start-when), no --teams (the block has them), no name in an invitation; the page reads the block back; You, the Friends and the bots are the seats that play, once each:' + wrong,
                  wrong === '' && states > 4000);
        }
        {   // what is made from text that was typed is only ever text: a name goes into START URL-encoded, and no text of an address becomes anything but a tested word, a seat, a code or the cleaned name
            const evil = 'x&join=/ws&room=a&seat=1&fill=hard&start=1&teams=0+1&roommap=tiny&roomseats=2&roomteams=0+1&roomleaderstart=1&platform=linux --name y';
            const q = L.cardQuery(st(0, M4), 'k7m2xq9p', 0, evil, '16:9');
            const args = P.joinArguments(q, true, 'play.test').args;
            check('a name with & = and a flag in it is one name in START, and the game page makes one --name of it (no other argument)', args.filter((a) => a === '--name').length === 1 && args.filter((a) => a === '--seat').length === 1 && args.filter((a) => a === '--start-when').length === 1 && args.filter((a) => a === '--room').length === 1 && args[args.indexOf('--name') + 1] === evil.slice(0, 32), JSON.stringify(args));
            check('... and none of the block\'s or the platform\'s parameters inside the name reaches the game: the room is the card\'s (Treasure, four seats, no teams, the card does not make the leader start)', args[args.indexOf('--room-map') + 1] === 'treasure' && args[args.indexOf('--room-seats') + 1] === '4' && args.filter((a) => a === '--room-map' || a === '--room-seats').length === 2 && args.indexOf('--room-teams') === -1 && args.indexOf('--room-leader-start') === -1 && args.indexOf('--platform') === -1, JSON.stringify(args));
        }
    }
} catch (e) {
    failures++;
    console.log('FAIL ' + e.message + '\n' + e.stack);
}

console.log('web lobby check: ' + checks + ' checks, ' + failures + ' failures');
process.exit(failures === 0 ? 0 : 1);
