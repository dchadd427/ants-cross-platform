// Runs the front page's OWN rules without a browser (the owner: "when you click the play online button it opens in a new tab can we just make that the default. you can play single on the
// play online tab by setting to 1 player"):
//   * web/lobby.html, the block LOBBY_BEGIN .. LOBBY_END: what the two cards mean. playersChoice (1 .. 4, else the fallback), hostPlayers (the Host card's 2 .. 4: an old stored 1 is 2), soloBots (the opponents that the first versions remembered:
//     nothing remembered is Medium, anything that is no level is none), soloSeats (one level per seat from the new key, else the old one), soloTeamChoices / soloTeam (the Teams select), and
//     localGameQuery (the address of a game on THIS computer: ?map=<key>[&bots=<levels>][&teams=0%2BN]&name=<name>&aspect=<shape>, never a ?join=, a room or a server);
//   * the same block's Host card (protocol 13): hostSeats / hostSeatsText (the levels of the three seats after the leader's, as the browser remembers them: four words, or the one word of the first versions),
//     hostFillText (what a room of 2 - 4 players asks of START, as an address holds it), hostTeamChoices / hostTeam / hostTeamText (the room's teams), validFillPlan and validRoomTeams (what an address may say),
//     hostPlanText (the words of the room panel);
//   * web/shell.html, ANTS_PAGE.localArguments: the game page's local parameters through a WHITELIST (the map by its key out of the six shipped maps, the opponents by one level word or three,
//     the teams by ffa or 0+N, a name of printable ASCII): what reaches the game's own arguments is a file name from a fixed table, level words that were tested, a seat that has a bot, a
//     cleaned name and flags, never the text of the address; nothing at all for an address with no local parameter (today's front page: the setup screen);
//   * the two pages agree: the six maps (keys and files, which exist in Original-Ants/Maps), and every address that the lobby can make is read by the game page as the same map, the
//     same bot in each of the three other bases, the same teams and the same name; every address of a ROOM that the Host card can make (every room of 2 - 4 players, every set of levels, every team)
//     is read by the game page as the same --fill-bots plan and the same --teams.
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
    'return { MAPS: MAPS, LOCAL_PAGE: LOCAL_PAGE, playersChoice: playersChoice, hostPlayers: hostPlayers, soloBots: soloBots, soloSeats: soloSeats, soloSeatsText: soloSeatsText, soloTeamChoices: soloTeamChoices, soloTeam: soloTeam, SOLO_SEATS: SOLO_SEATS, localGameQuery: localGameQuery, validFill: validFill, validFillPlan: validFillPlan, hostSeats: hostSeats, hostSeatsText: hostSeatsText, hostFillText: hostFillText, hostTeamChoices: hostTeamChoices, hostTeam: hostTeam, hostTeamText: hostTeamText, validRoomTeams: validRoomTeams, hostPlanText: hostPlanText, SEAT_COLOURS: SEAT_COLOURS };',
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
    same('the seats are Red, Blue, Black (seats 1, 2, 3 of the game)', L.SOLO_SEATS, ['Red', 'Blue', 'Black']);
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
    // the Teams select: free for all always; the player with each bot only while two or more bots play (with one the team would be the whole match, which ends at once)
    const FFA = { value: 'ffa', text: 'Free for all' };
    same('soloTeamChoices: all three bots: free for all and the player with each', L.soloTeamChoices(['easy', 'medium', 'hard']), [FFA, { value: '0+1', text: 'You + Red' }, { value: '0+2', text: 'You + Blue' }, { value: '0+3', text: 'You + Black' }]);
    same('soloTeamChoices: two bots: the player with each of them (the seat that has none is not offered)', [L.soloTeamChoices(['easy', '', 'hard']), L.soloTeamChoices(['', 'medium', 'easy'])],
         [[FFA, { value: '0+1', text: 'You + Red' }, { value: '0+3', text: 'You + Black' }], [FFA, { value: '0+2', text: 'You + Blue' }, { value: '0+3', text: 'You + Black' }]]);
    same('soloTeamChoices: one bot or none: free for all only', [L.soloTeamChoices(['easy', '', '']), L.soloTeamChoices(['', '', 'hard']), L.soloTeamChoices(['', '', '']), L.soloTeamChoices([])], [[FFA], [FFA], [FFA], [FFA]]);
    {
        const three = ['easy', 'medium', ''];
        same('soloTeam: a remembered choice that is one of the choices stays', [L.soloTeam('0+1', three), L.soloTeam('0+2', three), L.soloTeam('ffa', three)], ['0+1', '0+2', 'ffa']);
        same('soloTeam: a choice that the bots no longer allow (a seat with no bot, one bot only) is free for all', [L.soloTeam('0+3', three), L.soloTeam('0+1', ['easy', '', '']), L.soloTeam('0+2', ['', '', ''])], ['ffa', 'ffa', 'ffa']);
        same('soloTeam: anything that is no choice is free for all', ['', null, undefined, '0+4', '0+0', '1+2', '0 1', ' 0+1', '0+1 ', 'FFA', 'constructor', 1, {}].map((w) => L.soloTeam(w, ['easy', 'easy', 'easy'])), Array(13).fill('ffa'));
    }

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
        const q = (levels, teams) => L.localGameQuery('small', levels, 'Bob', '16:9', teams);
        const tail = '&name=Bob&aspect=16:9';
        same('localGameQuery: three levels are three words, seat by seat, none for a base that has no bot', [q(['easy', 'medium', 'hard']), q(['hard', '', 'easy']), q(['', '', 'hard']), q(['easy', '', ''])],
             ['easy,medium,hard', 'hard,none,easy', 'none,none,hard', 'easy,none,none'].map((w) => '?map=small&bots=' + w + tail));
        same('localGameQuery: three of one level are that one word (the address of the first versions), no bot at all leaves &bots= out', [q(['medium', 'medium', 'medium']), q(['', '', '']), q([]), q(undefined), q('')],
             ['?map=small&bots=medium' + tail, '?map=small' + tail, '?map=small' + tail, '?map=small' + tail, '?map=small' + tail]);
        same('localGameQuery: a word that is no level is none, in a list too; a list of another length is read as far as it goes', [q(['EASY', 'junk', 'Hard']), q(['easy']), q(['easy', 'hard'])],
             ['easy,none,hard', 'easy,none,none', 'easy,hard,none'].map((w) => '?map=small&bots=' + w + tail));
        same('localGameQuery: the teams are ?teams=0%2BN, the player with a seat that has a bot, while two or more bots play (the + is %2B: a + would be read as a blank)', [q(['easy', 'medium', ''], '0+2'), q(['easy', 'medium', 'hard'], '0+3'), q('medium', '0+1')],
             ['?map=small&bots=easy,medium,none&teams=0%2B2' + tail, '?map=small&bots=easy,medium,hard&teams=0%2B3' + tail, '?map=small&bots=medium&teams=0%2B1' + tail]);
        check('localGameQuery: the address reads the teams back as 0+N', new URLSearchParams(q(['easy', 'medium', ''], '0+2')).get('teams') === '0+2');
        same('localGameQuery: free for all, a seat with no bot, one bot only and anything that is no choice leave &teams= out',
             [q(['easy', 'medium', 'hard'], 'ffa'), q(['easy', 'medium', ''], '0+3'), q(['easy', '', ''], '0+1'), q(['', '', ''], '0+1'), q(['easy', 'medium', 'hard'], '0+4'), q(['easy', 'medium', 'hard'], '1+2'), q(['easy', 'medium', 'hard'], '0+0'),
              q(['easy', 'medium', 'hard'], undefined), q(['easy', 'medium', 'hard'], '0 1'), q(['easy', 'medium', 'hard'], '0+1&join=/ws')],
             ['?map=small&bots=easy,medium,hard' + tail, '?map=small&bots=easy,medium,none' + tail, '?map=small&bots=easy,none,none' + tail, '?map=small' + tail, '?map=small&bots=easy,medium,hard' + tail, '?map=small&bots=easy,medium,hard' + tail,
              '?map=small&bots=easy,medium,hard' + tail, '?map=small&bots=easy,medium,hard' + tail, '?map=small&bots=easy,medium,hard' + tail, '?map=small&bots=easy,medium,hard' + tail]);
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
    {   // one level for each seat and the teams: every choice of the form (all 64 sets of levels, every team that the form offers for them) is read as the same bots in the same seats, the same teams, the same name
        const WORDS = ['', 'easy', 'medium', 'hard'];
        let wrong = 0;
        let sample = '';
        let made = 0;
        for (const m of L.MAPS) {
            for (const a of WORDS) for (const b of WORDS) for (const c of WORDS) {
                const levels = [a, b, c];
                for (const choice of L.soloTeamChoices(levels)) {
                    for (const name of ['', 'Bob', 'Ann & <b>Bob</b>', 'A=B&C=D']) {
                        for (const shape of ['16:9', '4:3']) {
                            const query = L.localGameQuery(m.key, levels, name, shape, choice.value);
                            const args = local(query);
                            const want = ['--map', 'Original-Ants/Maps/' + FILES[m.key], '--play', ...botArgs(levels), ...(choice.value !== 'ffa' ? ['--teams', choice.value] : []), '--name', name || 'Player'];
                            made++;
                            if (JSON.stringify(args) !== JSON.stringify(want)) { wrong++; if (!sample) sample = query + ' -> ' + JSON.stringify(args); }
                            if (new URLSearchParams(query).get('aspect') !== shape) { wrong++; if (!sample) sample = query; }
                        }
                    }
                }
            }
        }
        check('every choice of levels and teams that the lobby can make (' + made + ' addresses) is read by the game page as the same bots in the same seats, the same teams and the same name (' + sample + ')', wrong === 0);
        // what the form remembers (soloSeatsText) is what the form reads back, and the game page reads the same words as an address
        let drift = 0;
        for (const a of WORDS) for (const b of WORDS) for (const c of WORDS) {
            const text = L.soloSeatsText([a, b, c]);
            if (JSON.stringify(L.soloSeats(text, null)) !== JSON.stringify([a, b, c])) drift++;
            if (JSON.stringify(local('?bots=' + text)) !== JSON.stringify(botArgs([a, b, c]))) drift++;
        }
        check('the settings text of the levels is read back whole by the form and by the game page (' + drift + ' differ)', drift === 0);
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // the Host card (network protocol 13): a level for each seat after the leader's, and the room's teams
    // ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const N3 = ['', '', ''];
        same('the seats of the Host card are Red, Blue, Black and the colours of the seats of a room are Green, Red, Blue, Black', [L.SOLO_SEATS, L.SEAT_COLOURS], [['Red', 'Blue', 'Black'], ['Green', 'Red', 'Blue', 'Black']]);
        same('hostSeats: a first visit (nothing remembered) is none in all three seats: the empty seats stay empty until the leader chooses', [L.hostSeats(null), L.hostSeats(undefined), L.hostSeats('')], [N3, N3, N3]);
        same('hostSeats: four words are the levels of the seats 0 - 3 and the first (the leader\'s seat) counts for nothing; any case', [L.hostSeats('none,easy,none,hard'), L.hostSeats('hard,medium,medium,medium'), L.hostSeats('NONE,None,EASY,Hard'), L.hostSeats('none,none,none,none')],
             [['easy', '', 'hard'], ['medium', 'medium', 'medium'], ['', 'easy', 'hard'], N3]);
        same('hostSeats: one word is every seat (what the first versions of the page remembered); none and anything else is none', [L.hostSeats('easy'), L.hostSeats('HARD'), L.hostSeats('medium'), L.hostSeats('none'), L.hostSeats('junk')],
             [['easy', 'easy', 'easy'], ['hard', 'hard', 'hard'], ['medium', 'medium', 'medium'], N3, N3]);
        for (const bad of ['easy,hard', 'easy,hard,easy', 'none,easy,none,hard,none', 'none,easy,,hard', 'none, easy,none,hard', 'none,easy,none,loud', ',,,', 'none;easy;none;hard', 5, {}, ['easy', 'easy', 'easy', 'easy']]) {
            same('hostSeats: a remembered ' + JSON.stringify(bad) + ' is not one word or four: none in all three seats', L.hostSeats(bad), N3);
        }
        same('hostSeatsText: four words, the leader\'s seat none (what the browser remembers)', [L.hostSeatsText(['easy', '', 'hard']), L.hostSeatsText(N3), L.hostSeatsText(['EASY', 'junk', 'Medium']), L.hostSeatsText([])],
             ['none,easy,none,hard', 'none,none,none,none', 'none,easy,none,medium', 'none,none,none,none']);
        same('hostSeats and hostSeatsText read each other', ['none,easy,none,hard', 'none,none,none,none', 'none,hard,hard,easy'].map((t) => L.hostSeatsText(L.hostSeats(t))), ['none,easy,none,hard', 'none,none,none,none', 'none,hard,hard,easy']);

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
        }
    }
} catch (e) {
    failures++;
    console.log('FAIL ' + e.message + '\n' + e.stack);
}

console.log('web lobby check: ' + checks + ' checks, ' + failures + ' failures');
process.exit(failures === 0 ? 0 : 1);
