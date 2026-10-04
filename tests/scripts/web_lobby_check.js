// Runs the front page's OWN rules without a browser (the owner: "when you click the play online button it opens in a new tab can we just make that the default. you can play single on the
// play online tab by setting to 1 player"):
//   * web/lobby.html, the block LOBBY_BEGIN .. LOBBY_END: what the New match form means. playersChoice (1 .. 4, else the fallback), soloBots (the opponents that a browser remembered: nothing
//     remembered is Medium, anything that is no level is none), localGameQuery (the address of a game on THIS computer: ?map=<key>[&bots=<level>]&name=<name>&aspect=<shape>, never
//     a ?join=, a room or a server);
//   * web/shell.html, ANTS_PAGE.localArguments: the game page's local parameters through a WHITELIST (the map by its key out of the six shipped maps, the opponents by one of the three
//     levels, a name of printable ASCII): what reaches the game's own arguments is a file name from a fixed table, a level word that was tested, a cleaned name and flags, never the
//     text of the address; nothing at all for an address with no local parameter (today's front page: the setup screen);
//   * the two pages agree: the six maps (keys and files, which exist in Original-Ants/Maps), and every address that the lobby can make is read by the game page as the same map, the
//     same bots in the three other bases and the same name.
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
    'return { MAPS: MAPS, LOCAL_PAGE: LOCAL_PAGE, playersChoice: playersChoice, soloBots: soloBots, localGameQuery: localGameQuery, validFill: validFill };',
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

    same('soloBots: a browser that never chose starts with Medium', [L.soloBots(null), L.soloBots(undefined)], ['medium', 'medium']);
    same('soloBots: "none" is none (the original\'s single player, alone), and so is an empty value', [L.soloBots('none'), L.soloBots('')], ['', '']);
    same('soloBots: the three levels, in any case', [L.soloBots('easy'), L.soloBots('medium'), L.soloBots('hard'), L.soloBots('HARD'), L.soloBots('Medium')], ['easy', 'medium', 'hard', 'hard', 'medium']);
    for (const bad of ['junk', 'medium ', ' easy', 'easy,hard', 'nonee', 'medium;hard', 'extreme', '0', 3, {}, ['easy']]) same('soloBots: ' + JSON.stringify(bad) + ' is none', L.soloBots(bad), '');

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
        const pool = ['treasure', 'tiny', 'nowhere', '../x', 'easy', 'medium', 'HARD', 'x', ' ', '--name', '--map', '--join-url', 'ws://evil/', '\n', '&', '=', '%00', '%0a', 'a b', 'é', '1', 'null'];
        let seed = 7;
        const rnd = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
        const allowed = new Set(['--map', '--play', '--bot', '--name', ...Object.values(FILES).map((f) => 'Original-Ants/Maps/' + f), '1:easy', '2:easy', '3:easy', '1:medium', '2:medium', '3:medium', '1:hard', '2:hard', '3:hard']);
        for (let i = 0; i < 4000; i++) {
            const parts = [];
            for (const key of ['map', 'bots', 'name', 'join', 'room', 'fill', 'x']) if (rnd(3) !== 0) parts.push(key + '=' + encodeURIComponent(pool[rnd(pool.length)] + (rnd(4) === 0 ? pool[rnd(pool.length)] : '')));
            const search = '?' + parts.join('&');
            const got = local(search, rnd(2) ? 'Bob' : null);
            for (let k = 0; k < got.length; k++) {
                const prev = got[k - 1];
                const ok = allowed.has(got[k]) ? true : prev === '--name' && /^[\x20-\x7e]{1,32}$/.test(got[k]) && got[k] === got[k].trim();
                if (!ok) { wrong++; if (!sample) sample = search + ' -> ' + JSON.stringify(got); }
            }
        }
        check('4000 random addresses: every argument that comes out is a flag of the table, a file of the six maps, a tested bot seat, or the cleaned name after --name (' + sample + ')', wrong === 0);
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
} catch (e) {
    failures++;
    console.log('FAIL ' + e.message + '\n' + e.stack);
}

console.log('web lobby check: ' + checks + ' checks, ' + failures + ' failures');
process.exit(failures === 0 ? 0 : 1);
