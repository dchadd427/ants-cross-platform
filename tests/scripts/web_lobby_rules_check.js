// Runs the rules and the words of the lobby page (web/front/lobby_rules.js) without a browser, on tables: what a Room message of network protocol 16 shows on each of the four colour cards and on the
// map's side (the owner's pictures 1, 2, 3, 4, 5, 5b, 6, 12, 13, 13b, 14, 15b, 16 and 17, which the words below are compared with), who may press which of the Team 1 / Team 2 buttons and what is said when
// a press is refused, what the leader asks of the room (the plan, a colour moved or exchanged), the player's name (the same rules as the game page's own NAME block, which is read from web/shell.html and
// web/lobby.html and must be the same text in both), the room codes (the alphabet, the two groups of three, what a typed or pasted code may be), the maps (against the level files of Original-Ants/Maps),
// the sentences that the server says (noticeKind) and every message strip of picture 15 (text, tone and button).
//   - the Room messages are built as bytes and read by web/front/lobby_net.js (the codec of the same protocol), so a table row is a message that a server can send;
//   - every exported function of lobby_rules.js must be called by this check (a function that no table reaches is a failure);
//   - the random runs use a fixed seed: the same rooms every time.
// usage: node web_lobby_rules_check.js web/front/lobby_rules.js web/lobby.html web/shell.html [repository folder]     (exit 0: every check holds; every failure is printed; the level files of
// Original-Ants/Maps, the pictures of web/front and include/ants_net/protocol.hpp are found in the repository folder, which is the one above web/ when it is not given)
'use strict';
const fs = require('fs');
const path = require('path');

const rulesPath = process.argv[2];
const lobbyPath = process.argv[3];
const shellPath = process.argv[4];
if (!rulesPath || !lobbyPath || !shellPath) { console.log('usage: web_lobby_rules_check.js lobby_rules.js lobby.html shell.html [repository folder]'); process.exit(2); }
const repoDir = path.resolve(process.argv[5] || path.join(path.dirname(path.resolve(lobbyPath)), '..'));
const mapsDir = path.join(repoDir, 'Original-Ants', 'Maps');
const frontDir = path.join(repoDir, 'web', 'front');
process.on('uncaughtException', (e) => { console.log('FAIL the check stopped on an error: ' + (e && e.stack || e)); process.exit(1); });

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
// key order does not matter, array order does
function canon(x) {
    if (Array.isArray(x)) return x.map(canon);
    if (x && typeof x === 'object') { const o = {}; Object.keys(x).sort().forEach((k) => { o[k] = canon(x[k]); }); return o; }
    return x === undefined ? '(undefined)' : x;
}
function same(label, got, want) {
    const a = JSON.stringify(canon(got));
    const b = JSON.stringify(canon(want));
    check(label, a === b, 'got ' + a + ', want ' + b);
}
const attempt = (fn) => { try { return { value: fn() }; } catch (e) { return { threw: String(e && e.message || e) }; } };

// The library, every exported function counted: the check ends by asking that each one was called
const RAW = require(path.resolve(rulesPath));
const called = Object.create(null);
const R = {};
Object.keys(RAW).forEach((k) => {
    R[k] = typeof RAW[k] === 'function' ? function () { called[k] = (called[k] || 0) + 1; return RAW[k].apply(this, arguments); } : RAW[k];
});
const N = require(path.join(path.dirname(path.resolve(rulesPath)), 'lobby_net.js'));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 0. What the library offers
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
same('the library exports exactly these names (a new one needs a table here)', Object.keys(RAW).sort(), [
    'ACTS', 'CODE_CHARS', 'COLOURS', 'DEFAULT_MAP_KEY', 'GRID', 'HINT', 'KINDS', 'MAPS', 'META', 'NAMES', 'TEXT',
    'article', 'banner', 'codeText', 'hostName', 'levelWord', 'listOf', 'makeCode', 'mapByKey', 'mapOfFile', 'modelOf', 'moveOf', 'nameAt', 'nameCheck', 'nameOf', 'noticeKind', 'pairOf', 'pickName',
    'planWith', 'playing', 'pressSide', 'reconcileSides', 'shownOf', 'sideOpen', 'sideView', 'sidesFix', 'startsAlone', 'teamBytes', 'teamNeedsFix', 'teamsText', 'typedCode', 'viewOf'
]);

// The colours: the server's seats (Green 0, Red 1, Blue 2, Black 3); the cards lie Black, Green, Red, Blue
same('the colours are the server\'s seats: Green 0, Red 1, Blue 2, Black 3', R.COLOURS, [{ id: 'green', name: 'Green' }, { id: 'red', name: 'Red' }, { id: 'blue', name: 'Blue' }, { id: 'black', name: 'Black' }]);
same('the cards lie Black, Green, Red, Blue (top left to bottom right, as the four hills lie)', R.GRID.map((seat) => R.COLOURS[seat].name), ['Black', 'Green', 'Red', 'Blue']);
same('GRID is a way through the four seats, each once', R.GRID.slice().sort(), [0, 1, 2, 3]);
same('the plan words are the protocol\'s plan values 0 to 4', R.KINDS.map((k) => N.PLAN[k.charAt(0).toUpperCase() + k.slice(1)]), [0, 1, 2, 3, 4]);
check('the slot states of the protocol are the ones that the rules read (a person is a Host or a Client, a computer player a Bot)', N.SLOT.Empty === 0 && N.SLOT.Host === 1 && N.SLOT.Client === 2 && N.SLOT.Bot === 3);
check('Treasure is the default map (the owner\'s request)', R.DEFAULT_MAP_KEY === 'treasure' && R.mapByKey(R.DEFAULT_MAP_KEY) !== null);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. Names: the rules are the game page's own (the NAME block of web/shell.html and web/lobby.html), on one table
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
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
const nameBlockShell = between(shellText, 'NAME_BEGIN', 'NAME_END', shellPath);
const nameBlockLobby = between(lobbyText, 'NAME_BEGIN', 'NAME_END', lobbyPath);
check('the NAME block of web/shell.html and of web/lobby.html is the same text, byte for byte', nameBlockShell === nameBlockLobby);
check('... and it is the block of the rules (not an empty one)', nameBlockShell.length > 1500 && /function nameCheck\(/.test(nameBlockShell) && /function runNameStep\(/.test(nameBlockShell));
const pageOf = (block) => new Function(block + '\nreturn { nameCheck: nameCheck, KEY: NAME_KEY, MAX: NAME_MAX };')();
const pageShell = pageOf(nameBlockShell);
const pageLobby = pageOf(nameBlockLobby);
check('the name is remembered under ants.name and has at most 32 characters (both pages)', pageShell.KEY === 'ants.name' && pageLobby.KEY === 'ants.name' && pageShell.MAX === 32 && pageLobby.MAX === 32);

const NAME_TABLE = [
    // [what is typed, ok, the name that comes back]   (the table of tests/scripts/web_name_check.js, section 1, and a few rows more)
    ['', true, ''], ['   ', true, ''], ['Alice', true, 'Alice'], ['  Alice  ', true, 'Alice'], ['Mary Ann', true, 'Mary Ann'], ['a', true, 'a'],
    ['a'.repeat(32), true, 'a'.repeat(32)], [' ' + 'a'.repeat(32) + ' ', true, 'a'.repeat(32)], ['a'.repeat(33), false],
    ['x'.repeat(100), false],
    ['Bot (Medium)', false], ['bot (x)', false], ['BOT(HARD)', false], ['  Bot (', false], ['B o t (', false], ['b  o  t  (', false], ['bot(x', false],
    ['Bot', true, 'Bot'], ['Botany', true, 'Botany'], ['Bot Bob', true, 'Bot Bob'], ['Robot (x)', true, 'Robot (x)'], ['bot )', true, 'bot )'], ['The Bot (x)', true, 'The Bot (x)'],
    ['Zo\u00eb', false], ['\u540d\u524d', false], ['caf\u00e9', false], ['\ud83d\udc1c', false], ['\u00a0Bob', false], ['Bob\u200b', false],        // (written as escapes: a no-break space, a zero-width space)
    ['a\tb', true, 'ab'], ['a\nb', true, 'ab'], ['a\r\nb', true, 'ab'], ['\u0001x', false], ['x\u007f', false], ['x\u0000', false],
    ['<script>alert(1)</script>', true, '<script>alert(1)</script>'], ['a&b', true, 'a&b'], ['"q" \'q\' <b>', true, '"q" \'q\' <b>'], ['100%', true, '100%'], ['..\\/', true, '..\\/'],
    [null, true, ''], [undefined, true, ''], [42, true, ''], [{}, true, ''],
    ['Bot\t(x)', false], ['\tBot (x)', false], [' ' + 'a'.repeat(33) + ' ', false], ['~ ~', true, '~ ~'], ['a b', false], ['Sam’s', false], ['x'.repeat(31) + ' ', true, 'x'.repeat(31)]
];
// (the table of web_name_check.js has ' Bob' refused: a name that came in with a space in front of it is cut, not refused, by the rule of the block, which the next loop shows on the page's own text)
NAME_TABLE.forEach(([text, ok, name]) => {
    const shown = String(JSON.stringify(text)).slice(0, 40);
    const got = R.nameCheck(text);
    const page = pageLobby.nameCheck(text);
    check('nameCheck(' + shown + ') is ' + (ok ? 'accepted' : 'refused') + ', as the page\'s own block says', got.ok === ok && page.ok === ok, JSON.stringify(got) + ' / ' + JSON.stringify(page));
    if (ok) check('nameCheck(' + shown + ') gives ' + JSON.stringify(name), got.name === name && page.name === name, JSON.stringify(got));
    else check('nameCheck(' + shown + ') says why, in a short line', typeof got.why === 'string' && got.why.length > 8 && got.why.length < 120, JSON.stringify(got));
    same('nameCheck(' + shown + ') answers exactly what the NAME block of web/lobby.html answers', got, page);
    same('... and what the NAME block of web/shell.html answers', got, pageShell.nameCheck(text));
});
same('the three refusals say what they are about, in the game page\'s words', [R.nameCheck('Zo\u00eb').why, R.nameCheck('a'.repeat(33)).why, R.nameCheck('Bot (x)').why], [
    'A name can hold printable ASCII characters only: letters, digits and signs without accents.',
    'A name has at most 32 characters.',
    'A name that starts with "Bot (" is for computer players. Please choose another name.'
]);
check('a name that is too long and not ASCII is refused for the characters first; too long and "Bot (" for the length', /ASCII/.test(R.nameCheck('é' + 'a'.repeat(40)).why) && /32/.test(R.nameCheck('Bot (' + 'x'.repeat(40)).why));
check('the blanks that were cut are the only change: a name never comes back longer, changed or in another case', ['  Sam  ', 'sAm', 'Sam Lee'].every((t) => R.nameCheck(t).name === t.trim()));

// A name for somebody who typed none
check('the picked names are distinct, short, printable, and each is a name that nameCheck takes as it is', new Set(R.NAMES).size === R.NAMES.length && R.NAMES.length >= 8 && R.NAMES.every((n) => { const c = R.nameCheck(n); return c.ok && c.name === n && n.length <= 32 && n !== ''; }));
R.NAMES.forEach((n, k) => same('pickName(' + ((k + 0.5) / R.NAMES.length).toFixed(3) + ') is the name number ' + k, R.pickName((k + 0.5) / R.NAMES.length), n));
same('pickName(0) is the first name and pickName(0.999999) the last', [R.pickName(0), R.pickName(0.999999)], [R.NAMES[0], R.NAMES[R.NAMES.length - 1]]);
[1, 1.5, -0.1, NaN, Infinity, '0.5', null, undefined, {}].forEach((v) => same('pickName(' + String(v) + ') is a name all the same (the first)', R.pickName(v), R.NAMES[0]));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. Room codes
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const ALPHA = R.CODE_CHARS;
const LETTERS = 'abcdefghijklmnopqrstuvwxyz'.replace(/[ilo]/g, '');
same('the alphabet is the letters without i, l, o and the digits 2 to 9: 31 symbols, each once', ALPHA.split('').sort(), (LETTERS + '23456789').split('').sort());
check('... so it has no i, l, o, 0 or 1, and no capital', ALPHA.length === 31 && !/[ilo01A-Z]/.test(ALPHA));
const CODE_RE = /^[a-hj-km-np-z2-9]{6}$/;                                 // (written out: a to h, j, k, m, n, p to z, 2 to 9)
const floorMod = (v) => Number(((BigInt(Math.floor(v)) % 31n) + 31n) % 31n);
same('makeCode([0..5]) is the first six symbols', R.makeCode([0, 1, 2, 3, 4, 5]), ALPHA.slice(0, 6));
same('makeCode([0..5]) is abcdef', R.makeCode([0, 1, 2, 3, 4, 5]), 'abcdef');
same('makeCode([8..13]) skips i: jkmnpq', R.makeCode([8, 9, 10, 11, 12, 13]), 'jkmnpq');
same('makeCode([30, 30, 30, 30, 30, 30]) is the last symbol six times: 999999', R.makeCode([30, 30, 30, 30, 30, 30]), '999999');
same('makeCode wraps after 31: [31..36] is abcdef again', R.makeCode([31, 32, 33, 34, 35, 36]), 'abcdef');
for (let v = 0; v < 31; v++) same('makeCode of six ' + v + ' is the symbol number ' + v + ' six times', R.makeCode([v, v, v, v, v, v]), ALPHA.charAt(v).repeat(6));
same('makeCode takes the numbers in order (a code is not sorted or mixed)', R.makeCode([5, 4, 3, 2, 1, 0]), 'fedcba');
same('makeCode of a typed array of the browser\'s generator', R.makeCode(Uint32Array.from([4294967295, 1, 2, 3, 4, 5])), ALPHA.charAt(Number(4294967295n % 31n)) + 'bcdef');
same('makeCode drops a fraction (a number is a whole one: 2.9 is 2, 30.99 is 30)', R.makeCode([2.9, 0.1, 30.99, 1, 2, 3]), 'ca9bcd');
same('makeCode uses six numbers and no more (a seventh changes nothing)', R.makeCode([1, 2, 3, 4, 5, 6, 7, 8]), R.makeCode([1, 2, 3, 4, 5, 6]));
same('makeCode of too few numbers still makes six symbols from the alphabet', [R.makeCode([]).length, CODE_RE.test(R.makeCode([])), R.makeCode([1, 2]).length, CODE_RE.test(R.makeCode([1, 2]))], [6, true, 6, true]);
[[NaN, NaN, NaN, NaN, NaN, NaN], [Infinity, 1e300, 2 ** 53, 2 ** 64, 0.5, 1e-9], ['7', '8', 9, 10, 11, 12], [undefined, null, 1, 2, 3, 4]].forEach((vals, k) =>
    check('makeCode of odd numbers #' + k + ' (' + vals.map(String).join(', ') + ') is six symbols of the alphabet', CODE_RE.test(R.makeCode(vals)), R.makeCode(vals)));
same('makeCode of whole numbers below zero (an Int32Array of a generator would give them): six symbols, each the one that counting back from the end gives (-1 is the last, -31 is the first)', [R.makeCode([-1, -2, -31, -32, 5, 6]), R.makeCode([-1, -2, -31, -32, 5, 6]).length],
    [[-1, -2, -31, -32, 5, 6].map((v) => ALPHA.charAt(floorMod(v))).join(''), 6]);
check('makeCode of odd numbers below zero (-Infinity, -1e300, -(2^53), -0.5) is six symbols of the alphabet', CODE_RE.test(R.makeCode([-Infinity, -1e300, -(2 ** 53), -0.5, -7.5, -100])), R.makeCode([-Infinity, -1e300, -(2 ** 53), -0.5, -7.5, -100]));
{
    let seed = 12345;
    const rnd = () => { seed = (seed + 0x6D2B79F5) >>> 0; let t = seed; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
    const up = [() => Math.floor(rnd() * 4294967296), () => Math.floor(rnd() * 100), () => rnd() * 1e6, () => Math.floor(rnd() * 2 ** 31)];
    const down = [() => -Math.floor(rnd() * 4294967296), () => -Math.floor(rnd() * 100) - 1, () => -rnd() * 1e6, () => Math.floor(rnd() * 2 ** 32) - 2 ** 31];
    const run = (kinds) => {
        let bad = '';
        const seen = new Set();
        for (let i = 0; i < 3000; i++) {
            const vals = [0, 1, 2, 3, 4, 5].map(() => kinds[i % kinds.length]());
            const code = R.makeCode(vals);
            if (!bad && !CODE_RE.test(code)) bad = JSON.stringify(vals) + ' -> ' + JSON.stringify(code);
            else if (!bad && vals.every(Number.isInteger) && code !== vals.map((v) => ALPHA.charAt(floorMod(v))).join('')) bad = 'not the symbols of the numbers: ' + JSON.stringify(vals) + ' -> ' + code;
            code.split('').forEach((c) => seen.add(c));
        }
        return { bad, seen: seen.size };
    };
    const ups = run(up);
    check('3000 random sets of six numbers from 0 up (whole numbers to 2^32, fractions): always six symbols of the alphabet, and the symbols of the numbers when they are whole', ups.bad === '', ups.bad);
    check('... and every symbol of the alphabet came up (a code is not stuck on a few)', ups.seen === 31, String(ups.seen));
    const downs = run(down);
    check('3000 random sets of six numbers that may be below zero: always six symbols of the alphabet, and the symbols of the numbers when they are whole', downs.bad === '', downs.bad);
}

same('codeText shows a code in two groups of three', [R.codeText('k7m2xq'), R.codeText('abcdef'), R.codeText('222333')], ['k7m 2xq', 'abc def', '222 333']);
same('codeText leaves the older codes (eight, one to five, seven) and nothing as they are', ['abc', 'abcd', 'abcde', 'abcdefg', 'abcd1234', 'a-b_c-d_e-f_g-h', ''].map((c) => R.codeText(c)), ['abc', 'abcd', 'abcde', 'abcdefg', 'abcd1234', 'a-b_c-d_e-f_g-h', '']);
same('codeText of what is no text is empty', [null, undefined, 123456, {}, ['a', 'b']].map((c) => R.codeText(c)), ['', '', '', '', '']);
check('a code that was shown in two groups is a code that was typed: typedCode(codeText(code)) is the code', ['k7m2xq', 'abcdef', 'zzz999', 'a2b3c4'].every((c) => { const t = R.typedCode(R.codeText(c)); return t.ok && t.code === c; }));

const TYPED_WHY = 'A room code has letters and numbers only, like k7m 2xq.';
const FIRST = 'Type or paste the code first.';
[
    // [typed, the code that comes back, or null and the line under the field]
    ['k7m2xq', 'k7m2xq'], ['K7M2XQ', 'k7m2xq'], ['k7m 2xq', 'k7m2xq'], ['K7M 2XQ', 'k7m2xq'], ['K7m 2xQ', 'k7m2xq'], ['  k7m2xq  ', 'k7m2xq'], ['k7m\t2xq', 'k7m2xq'], ['k7m\n2xq\n', 'k7m2xq'],
    ['k7m 2xq', 'k7m2xq'], ['k 7 m 2 x q', 'k7m2xq'], ['k7m  2xq', 'k7m2xq'], [' k7m2xq ', 'k7m2xq'],
    ['a', 'a'], ['A', 'a'], ['abcd1234', 'abcd1234'], ['ABCD-1234', 'abcd-1234'], ['a_b-c', 'a_b-c'], ['-', '-'], ['_', '_'], ['ilo01', 'ilo01'], ['k0l1io', 'k0l1io'], ['0', '0'],
    ['a'.repeat(32), 'a'.repeat(32)], ['ab '.repeat(16), 'ab'.repeat(16)], ['A'.repeat(32), 'a'.repeat(32)],
    ['a'.repeat(33), null, TYPED_WHY], ['ab '.repeat(16) + 'c', null, TYPED_WHY], ['a'.repeat(100), null, TYPED_WHY],
    ['', null, FIRST], ['   ', null, FIRST], ['\n', null, FIRST], ['\t  ', null, FIRST], [null, null, FIRST], [undefined, null, FIRST], [42, null, FIRST], [{}, null, FIRST], [['k7m2xq'], null, FIRST],
    ['k7m.2xq', null, TYPED_WHY], ['k7m/2xq', null, TYPED_WHY], ['../x', null, TYPED_WHY], ['k7m2xq?', null, TYPED_WHY], ['?room=k7m2xq', null, TYPED_WHY], ['beta.playants.org/?room=k7m2xq', null, TYPED_WHY],
    ['ké', null, TYPED_WHY], ['ｋ7m2xq', null, TYPED_WHY], ['🐜', null, TYPED_WHY], ['k7m​2xq', null, TYPED_WHY], ['k7m\u00002xq', null, TYPED_WHY], ['<b>', null, TYPED_WHY],
    ['a%20b', null, TYPED_WHY], ['a+b', null, TYPED_WHY], ['k7m,2xq', null, TYPED_WHY], ['k7m\'2xq', null, TYPED_WHY], ['k7m;2xq', null, TYPED_WHY], ['k7m:2xq', null, TYPED_WHY], ['"k7m2xq"', null, TYPED_WHY]
].forEach(([typed, code, why]) => {
    const shown = typeof typed === 'string' ? JSON.stringify(typed).slice(0, 44) : String(JSON.stringify(typed));
    same('typedCode(' + shown + ')', R.typedCode(typed), code === null ? { ok: false, why: why } : { ok: true, code: code });
});

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. The maps, against the level files of the original game
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
same('the six maps, in the page\'s order', R.MAPS.map((m) => m.key), ['tiny', 'small', 'medium', 'gauntlet', 'treasure', 'islands']);
R.MAPS.forEach((m) => {
    check(m.key + ': the name is the key with a capital and the file is the key in capitals + .LVL', m.name === m.key.charAt(0).toUpperCase() + m.key.slice(1) && m.file === m.key.toUpperCase() + '.LVL', JSON.stringify(m));
    const file = path.join(mapsDir, m.file);
    check(m.key + ': the level file is in the original game\'s folder (' + path.basename(mapsDir) + ')', fs.existsSync(file), file);
    if (!fs.existsSync(file)) return;
    const head = fs.readFileSync(file).subarray(0, 40);
    const minutes = head.readUInt16LE(8);                                 // (version u32, mode u32, minutes u16, then the 30 bytes of the Map Info text: tests/scripts/test_web_front.py reads them the same way)
    const description = head.subarray(10, 40).toString('latin1').split('\0')[0];
    check(m.key + ': the Map Info line is "' + description + ' (' + minutes + ' min)" as the setup screen of the game shows it', m.info === description + ' (' + minutes + ' min)', m.info);
    check(m.key + ': the page has a preview picture of it', fs.existsSync(path.join(frontDir, 'preview_' + m.key + '.png')), path.join(frontDir, 'preview_' + m.key + '.png'));
    check(m.key + ': mapByKey and mapOfFile find it', R.mapByKey(m.key) === m && R.mapOfFile(m.file) === m);
});
same('mapByKey ignores the case and finds nothing for what is no key', [R.mapByKey('TREASURE').key, R.mapByKey('Islands').key, R.mapByKey('treasure '), R.mapByKey(''), R.mapByKey(null), R.mapByKey(undefined), R.mapByKey(5), R.mapByKey('treasure.lvl'), R.mapByKey('constructor'), R.mapByKey('__proto__')],
    ['treasure', 'islands', null, null, null, null, null, null, null, null]);
same('mapOfFile ignores the case of a file name and finds nothing for a map that the page does not list', [R.mapOfFile('treasure.lvl').key, R.mapOfFile('Tiny.Lvl').key, R.mapOfFile('MYMAP.LVL'), R.mapOfFile(''), R.mapOfFile(null), R.mapOfFile(7), R.mapOfFile('TREASURE'), R.mapOfFile('../TREASURE.LVL')], ['treasure', 'tiny', null, null, null, null, null, null]);
check('the Map Info lines are all different and none is empty (the page shows one under the picture)', new Set(R.MAPS.map((m) => m.info)).size === 6 && R.MAPS.every((m) => /\(\d+ min\)$/.test(m.info)));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. Rooms: the Room messages of the tables are bytes that the codec reads (a row is a message that a server can send)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const E = N.SLOT.Empty, C = N.SLOT.Client, B = N.SLOT.Bot;
const P = (name) => [C, name];
const O = [E, ''];
function str8(w, s) { w.push(s.length); for (let i = 0; i < s.length; i++) w.push(s.charCodeAt(i)); }
// seats: four of [state, name]; o: { map, you, leader, teamA, teamB, starting, plan, inGame }
function mk(seats, o) {
    o = o || {};
    const w = [12];
    for (const s of seats) { w.push(s[0]); str8(w, s[1]); w.push(20, 0, 0); }
    str8(w, o.map === undefined ? 'TREASURE.LVL' : o.map);
    w.push(0, o.you === undefined ? 0 : o.you, o.leader === undefined ? 0 : o.leader, o.teamA === undefined ? 255 : o.teamA, o.teamB === undefined ? 255 : o.teamB, o.starting ? 7 : 3);
    for (const k of (o.plan || [0, 0, 0, 0])) w.push(k);
    w.push(o.inGame || 0);
    const m = N.decode(Uint8Array.from(w));
    if (m === null) throw new Error('the check built a Room message that the codec refuses: ' + JSON.stringify([seats, o]));
    return m;
}
const model = (room) => R.modelOf(room);
const ALL = [0, 1, 2, 3];
const person = (name, me, leader, ingame) => ({ kind: 'person', name: name, leader: !!leader, me: !!me, ingame: !!ingame });

// the situations of the pictures
const S1 = mk([P('Juniper'), O, O, O]);                                                                               // picture 1: you alone, three colours open
const S2 = mk([P('Juniper'), P('Sam'), O, O], { plan: [0, 0, 2, 0] });                                                // pictures 2, 3 and 6: Sam in Red, a Medium bot in Blue, Black open
const S5 = mk([P('Juniper'), P('Sam'), O, P('Priya')], { plan: [0, 0, 2, 0], teamA: 0, teamB: 2 });                   // pictures 5 and 5b: Green + Blue against Red + Black
const S14 = mk([P('Juniper'), P('Sam'), O, P('Priya')], { plan: [0, 0, 2, 0] });                                      // picture 14 (and 13 before START): four play, no teams
const S13 = mk([P('Juniper'), P('Sam'), O, P('Priya')], { plan: [0, 0, 2, 0], starting: true, inGame: 2 });           // picture 13: START is pressed, Sam's game is open
const S13b = mk([P('Juniper'), P('Sam'), O, P('Priya')], { plan: [0, 0, 2, 0], starting: true, inGame: 1, you: 1 });  // picture 13b: the same on Sam's screen; Juniper's game is open
const S4 = mk([P('Juniper'), O, O, P('Sam')], { you: 3, leader: 0, plan: [0, 1, 2, 0], teamA: 0, teamB: 3 });         // picture 4: Sam's screen (Black), Juniper hosts, an Easy and a Medium bot, Green + Black
const S12 = mk([O, P('Sam'), O, P('Priya')], { you: 1, leader: 1, plan: [0, 0, 2, 0] });                              // picture 12: Juniper left, Sam is the host now in Red

// ---- modelOf ----
same('modelOf, picture 1: you alone at Green, the host', model(S1), {
    you: 0, leader: 0, guest: false, starting: false, map: 'treasure', mapFile: 'TREASURE.LVL', sides: [0, 0, 0, 0],
    slots: [person('Juniper', true, true), { kind: 'open' }, { kind: 'open' }, { kind: 'open' }]
});
same('modelOf, picture 5: a computer player where the plan says Medium, the team pair as switches', model(S5), {
    you: 0, leader: 0, guest: false, starting: false, map: 'treasure', mapFile: 'TREASURE.LVL', sides: [1, 0, 1, 0],
    slots: [person('Juniper', true, true), person('Sam'), { kind: 'bot', level: 'medium' }, person('Priya')]
});
same('modelOf, picture 4: a guest in Black, an Easy and a Medium bot, the pair Green + Black', model(S4), {
    you: 3, leader: 0, guest: true, starting: false, map: 'treasure', mapFile: 'TREASURE.LVL', sides: [1, 0, 0, 1],
    slots: [person('Juniper', false, true), { kind: 'bot', level: 'easy' }, { kind: 'bot', level: 'medium' }, person('Sam', true)]
});
same('modelOf, picture 13b: START is pressed, a guest; the game of Juniper is open (not its own)', model(S13b), {
    you: 1, leader: 0, guest: true, starting: true, map: 'treasure', mapFile: 'TREASURE.LVL', sides: [0, 0, 0, 0],
    slots: [person('Juniper', false, true, true), person('Sam', true), { kind: 'bot', level: 'medium' }, person('Priya')]
});
same('modelOf, picture 13: the page\'s own bit never counts, the others\' do', [model(mk([P('Juniper'), P('Sam'), O, P('Priya')], { starting: true, inGame: 11 })).slots.filter((s) => s.kind === 'person').map((s) => s.ingame), model(S13).slots.filter((s) => s.kind === 'person').map((s) => s.ingame)], [[false, true, true], [false, true, false]]);
same('modelOf, picture 12: the new host', model(S12).slots.map((s) => s.kind + (s.leader ? ':leader' : '') + (s.me ? ':me' : '')), ['open', 'person:leader:me', 'bot', 'person']);
same('modelOf: what each plan value is when no person holds the colour (open, easy, medium, hard, nobody)', model(mk([P('Juniper'), O, O, O], { plan: [0, 4, 1, 3] })).slots.slice(1), [{ kind: 'nobody' }, { kind: 'bot', level: 'easy' }, { kind: 'bot', level: 'hard' }]);
same('... Medium is value 2', model(mk([P('Juniper'), O, O, O], { plan: [0, 2, 0, 0] })).slots[1], { kind: 'bot', level: 'medium' });
same('modelOf: a person who holds a colour shows, whatever the plan of the colour says (it comes back when the person leaves)', model(mk([P('Juniper'), P('Sam'), O, O], { plan: [0, 3, 0, 4] })).slots.map((s) => s.kind), ['person', 'person', 'open', 'nobody']);
same('modelOf: a seat that the server filled with a computer player is a computer player (Medium when the plan does not say)', model(mk([P('Juniper'), [B, 'Bot (Easy)'], [B, 'Bot (Hard)'], [B, 'Bot (Medium)']], { plan: [0, 1, 3, 4] })).slots.slice(1), [{ kind: 'bot', level: 'easy' }, { kind: 'bot', level: 'hard' }, { kind: 'bot', level: 'medium' }]);
same('modelOf: a Host seat of a game room is a person like a Client seat', R.modelOf({ slots: [{ state: 1, name: 'A' }, { state: 2, name: 'B' }, { state: 0, name: '' }, { state: 0, name: '' }], plan: [0, 0, 0, 0], map: '', you: 0, leader: 0, teamA: 255, teamB: 255, inGame: 0, starting: false }).slots.map((s) => s.kind), ['person', 'person', 'open', 'open']);
same('modelOf: no leader (255) is -1, and the page is a guest then', [model(mk([P('A'), P('B'), O, O], { leader: 255 })).leader, model(mk([P('A'), P('B'), O, O], { leader: 255 })).guest], [-1, true]);
same('modelOf: the map of the file name, in any case, else none (and the file name as the room says it)', ['ISLANDS.LVL', 'islands.lvl', 'MYMAP.LVL', ''].map((f) => { const m = model(mk([P('A'), O, O, O], { map: f })); return [m.map, m.mapFile]; }), [['islands', 'ISLANDS.LVL'], ['islands', 'islands.lvl'], ['', 'MYMAP.LVL'], ['', '']]);
same('modelOf: the team pair is two switches on Team 1; no team is none', [model(mk([P('A'), P('B'), P('C'), P('D')], { teamA: 1, teamB: 3 })).sides, model(mk([P('A'), P('B'), P('C'), P('D')], { teamA: 0, teamB: 1 })).sides, model(S14).sides], [[0, 1, 0, 1], [1, 1, 0, 0], [0, 0, 0, 0]]);
same('modelOf: a team that names a colour that is none (a message that no server sends) is no team', [255, 9].map((b) => R.modelOf({ slots: ALL.map(() => ({ state: 2, name: 'x' })), plan: [0, 0, 0, 0], map: '', you: 0, leader: 0, teamA: 1, teamB: b, inGame: 0, starting: false }).sides), [[0, 0, 0, 0], [0, 0, 0, 0]]);
same('modelOf: starting is read from the Room message', [model(S13).starting, model(S14).starting], [true, false]);
{
    const before = JSON.stringify(S5);
    model(S5);
    check('modelOf does not change the Room message', JSON.stringify(S5) === before);
}

// ---- the small helpers ----
same('levelWord', ['easy', 'medium', 'hard'].map(R.levelWord), ['Easy', 'Medium', 'Hard']);
same('article: an Easy bot, a Medium bot, a Hard bot', ['easy', 'medium', 'hard'].map(R.article), ['an ', 'a ', 'a ']);
same('listOf: nothing, one, two, three, four', [[], ['a'], ['a', 'b'], ['a', 'b', 'c'], ['a', 'b', 'c', 'd']].map(R.listOf), ['', 'a', 'a and b', 'a, b and c', 'a, b, c and d']);
same('playing: every person and every computer player, in seat order (open and nobody do not play)', [model(S1), model(S2), model(S5), model(S12), model(mk([P('A'), O, O, O], { plan: [0, 4, 4, 1] }))].map(R.playing), [[0], [0, 1, 2], [0, 1, 2, 3], [1, 2, 3], [0, 3]]);
same('hostName: the leader\'s name, or "The host" when the room has none', [R.hostName(model(S4)), R.hostName(model(S12)), R.hostName(model(mk([P('A'), P('B'), O, O], { leader: 255 })))], ['Juniper', 'Sam', 'The host']);
same('nameOf: what a card is called (the question of a moved colour)', ALL.map((i) => R.nameOf(model(S2), i)), ['Juniper', 'Sam', 'Medium bot', 'the open Black']);
same('nameOf: Easy bot, Hard bot, the unused Red', [R.nameOf(model(mk([P('A'), O, O, O], { plan: [0, 4, 1, 3] })), 1), R.nameOf(model(mk([P('A'), O, O, O], { plan: [0, 4, 1, 3] })), 2), R.nameOf(model(mk([P('A'), O, O, O], { plan: [0, 4, 1, 3] })), 3)], ['the unused Red', 'Easy bot', 'Hard bot']);
same('nameAt: who a toast is about (You for the page itself)', [R.nameAt(model(S2), 0, true), R.nameAt(model(S2), 1, false), R.nameAt(model(S2), 2, false), R.nameAt(model(S2), 3, false), R.nameAt(model(mk([P('A'), O, O, O], { plan: [0, 4, 0, 0] })), 1, false)],
    ['You', 'Sam', 'The Medium bot', 'The open Black', 'The unused Red']);
same('startsAlone: START plays on this computer when nobody else is in the match (nobody else is a person or a computer player)', [
    S1, mk([P('A'), O, O, O], { plan: [0, 4, 4, 0] }), mk([O, O, O, P('A')], { you: 3, leader: 3 }), S2, mk([P('A'), P('B'), O, O]), mk([P('A'), O, O, O], { plan: [0, 0, 0, 1] }), mk([P('A'), O, O, O], { plan: [0, 4, 3, 0] }), mk([O, O, O, O], { you: 255, leader: 255 })
].map((r) => R.startsAlone(model(r))), [true, true, true, false, false, false, false, false]);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. Team 1 and Team 2: the rules of the live match card (a team is two colours on the same button)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
[
    ['four, two and two', [1, 1, 2, 2], ALL, [1, 1, 2, 2]], ['four, a pair and nothing', [1, 0, 1, 0], ALL, [1, 0, 1, 0]], ['three play, a pair', [1, 1, 2, 0], [0, 1, 2], [1, 1, 2, 0]],
    ['two play: no teams', [1, 1, 2, 2], [0, 1], [0, 0, 0, 0]], ['one plays', [1, 0, 0, 0], [0], [0, 0, 0, 0]], ['nobody plays', [1, 1, 2, 2], [], [0, 0, 0, 0]],
    ['a colour that does not play loses its button', [1, 1, 2, 2], [0, 1, 2], [1, 1, 2, 0]], ['three on Team 1 is too many: all clear', [1, 1, 1, 0], [0, 1, 2], [0, 0, 0, 0]],
    ['three on Team 1 of four: all clear', [1, 1, 1, 2], ALL, [0, 0, 0, 0]], ['three on Team 2: all clear', [2, 2, 2, 0], [0, 1, 2], [0, 0, 0, 0]],
    ['three on a button, but one of them does not play: two are left, which is fine', [1, 1, 1, 0], [0, 1, 3], [1, 1, 0, 0]],
    ['a value that is no button (3, -1, "1", null, 1.5) is none', [3, -1, '1', null], [0, 1, 2, 3], [0, 0, 0, 0]], ['a value that is no button next to a good one', [3, 1, 1.5, 2], ALL, [0, 1, 0, 2]],
    ['not a list of numbers: none', null, ALL, [0, 0, 0, 0]], ['not a list of numbers: a text', 'abcd', ALL, [0, 0, 0, 0]], ['not a list of numbers: nothing', undefined, ALL, [0, 0, 0, 0]],
    ['colours that do not touch', [1, 0, 1, 2], [0, 2, 3], [1, 0, 1, 2]]
].forEach(([label, sides, play, want]) => {
    const before = JSON.stringify(sides);
    same('sidesFix: ' + label, R.sidesFix(sides, play), want);
    check('sidesFix: ' + label + ' (the input is not changed)', JSON.stringify(sides) === before);
});
[
    ['a pair on Team 1', [1, 0, 1, 0], ALL, [0, 2]], ['a pair on Team 2', [0, 2, 0, 2], ALL, [1, 3]], ['Team 1 first when both are pairs', [1, 1, 2, 2], ALL, [0, 1]], ['... and when the pair of Team 1 is the higher one', [2, 2, 1, 1], ALL, [2, 3]],
    ['one colour on a button is no pair', [1, 0, 0, 0], ALL, null], ['three on a button is no pair', [1, 1, 1, 0], ALL, null], ['no button', [0, 0, 0, 0], ALL, null],
    ['two on a button, the second one does not play', [1, 0, 1, 0], [0, 1, 3], null], ['a pair of three that play', [1, 1, 0, 0], [0, 1, 2], [0, 1]], ['a pair on Team 2 of three that play', [0, 2, 0, 2], [1, 2, 3], [1, 3]]
].forEach(([label, sides, play, want]) => same('pairOf: ' + label, R.pairOf(sides, play), want));
[
    ['four, a pair on Team 1: the others show Team 2', [1, 0, 1, 0], ALL, [1, 2, 1, 2]], ['four, a pair on Team 2: the others show Team 1', [0, 2, 0, 2], ALL, [1, 2, 1, 2]],
    ['four, Green + Red on Team 1', [1, 1, 0, 0], ALL, [1, 1, 2, 2]], ['four, Blue + Black on Team 1', [0, 0, 1, 1], ALL, [2, 2, 1, 1]], ['four, Blue + Black on Team 2', [0, 0, 2, 2], ALL, [1, 1, 2, 2]],
    ['four, Red + Blue on Team 1', [0, 1, 1, 0], ALL, [2, 1, 1, 2]], ['four, one button pressed alone: nothing is added', [1, 0, 0, 0], ALL, [1, 0, 0, 0]], ['four, two singles on two buttons: nothing is added', [1, 2, 0, 0], ALL, [1, 2, 0, 0]],
    ['three, a pair: the third plays alone and shows nothing', [1, 1, 0, 0], [0, 1, 2], [1, 1, 0, 0]], ['nothing pressed', [0, 0, 0, 0], ALL, [0, 0, 0, 0]]
].forEach(([label, sides, play, want]) => same('shownOf: ' + label, R.shownOf(sides, play), want));
{
    const s = [1, 0, 1, 0];
    R.shownOf(s, ALL);
    same('shownOf does not change the switches it is given', s, [1, 0, 1, 0]);
}
[
    // [label, sides, play, seat, side, may it have it on]
    ['two play: no', [0, 0, 0, 0], [0, 1], 0, 1, false], ['a colour that does not play: no', [0, 0, 0, 0], [0, 1, 2], 3, 1, false], ['a button that is none: no', [0, 0, 0, 0], ALL, 0, 0, false], ['... or a third', [0, 0, 0, 0], ALL, 0, 3, false],
    ['... or a text', [0, 0, 0, 0], ALL, 0, '1', false], ['nothing pressed, four play: yes', [0, 0, 0, 0], ALL, 2, 2, true],
    ['one is on Team 1: another may join it', [1, 0, 0, 0], ALL, 1, 1, true], ['two on Team 1: a third may not', [1, 0, 1, 0], ALL, 1, 1, false], ['... nor the fourth', [1, 0, 1, 0], ALL, 3, 1, false],
    ['two on Team 1: those two have it, so they may keep it', [1, 0, 1, 0], ALL, 0, 1, true], ['... and the others are lit on Team 2 already', [1, 0, 1, 0], ALL, 1, 2, true], ['... but Team 2 is the other two colours: Green may not have it', [1, 0, 1, 0], ALL, 0, 2, false],
    ['three play, a pair on Team 1: the third may have Team 2', [1, 1, 0, 0], [0, 1, 2], 2, 2, true], ['... but not Team 1', [1, 1, 0, 0], [0, 1, 2], 2, 1, false], ['... and a colour of the pair may go to Team 2 (nobody is lit there)', [1, 1, 0, 0], [0, 1, 2], 0, 2, true],
    ['three play, two on Team 2 by their own press: the third may not', [0, 2, 2, 0], [0, 1, 2], 0, 2, false], ['four play, Team 2 pressed twice', [0, 2, 2, 0], ALL, 3, 2, false], ['... Team 1 is lit for them, so Green may have it', [0, 2, 2, 0], ALL, 0, 1, true]
].forEach(([label, sides, play, seat, side, want]) => same('sideOpen: ' + label, R.sideOpen(sides, play, seat, side), want));

const REFUSE_FULL = (k) => 'Team ' + k + ' has two colours already. Press one of them to take it off first.';
const REFUSE_OTHER = (k) => 'Team ' + k + ' is the other two colours already. Press a lit button to clear the teams first.';
function sequence(label, play, start, steps) {
    let sides = start;
    steps.forEach(([seat, side, want, refusal], i) => {
        const before = JSON.stringify(sides);
        const got = R.pressSide(sides, play, seat, side);
        same(label + ' #' + (i + 1) + ': press ' + R.COLOURS[seat].name + ' Team ' + side + (refusal ? ' is refused: ' + refusal.slice(0, 20) + '...' : ''), got, { sides: want, refusal: refusal });
        check(label + ' #' + (i + 1) + ': the switches that were given are not changed, a new list comes back', JSON.stringify(sides) === before && got.sides !== sides);
        sides = got.sides;
    });
}
sequence('four play (pictures 5 and 5b)', ALL, [0, 0, 0, 0], [
    [0, 1, [1, 0, 0, 0], ''],                                // Green on Team 1: one colour is no team yet
    [2, 1, [1, 0, 1, 0], ''],                                // Blue on Team 1: the two make the teams, Red and Black show Team 2
    [1, 1, [1, 0, 1, 0], REFUSE_FULL(1)],                    // picture 5b: a third colour on Team 1
    [3, 1, [1, 0, 1, 0], REFUSE_FULL(1)],                    // the fourth, too
    [0, 2, [1, 0, 1, 0], REFUSE_OTHER(2)],                   // Green on Team 2 when Team 2 is the other two colours
    [2, 2, [1, 0, 1, 0], REFUSE_OTHER(2)],
    [0, 1, [0, 0, 1, 0], ''],                                // a lit button goes off: Blue is alone on Team 1, so there are no teams
    [0, 1, [1, 0, 1, 0], ''],                                // ... and on again
    [1, 2, [0, 0, 0, 0], ''],                                // Red's Team 2 is lit only because the others are: pressing it clears the teams
    [0, 2, [2, 0, 0, 0], ''],                                // Green alone on Team 2
    [1, 2, [2, 2, 0, 0], ''],                                // Red with it: the teams are Green + Red, Blue and Black show Team 1
    [2, 2, [2, 2, 0, 0], REFUSE_FULL(2)],                    // a third on Team 2
    [3, 2, [2, 2, 0, 0], REFUSE_FULL(2)],
    [0, 1, [2, 2, 0, 0], REFUSE_OTHER(1)],                   // Team 1 is the other two colours now: Green may not take it
    [2, 1, [0, 0, 0, 0], '']                                 // Blue's Team 1 is lit only because the others are: pressing it clears the teams
]);
sequence('four play, the lit button of the other team', ALL, [0, 1, 1, 0], [
    [0, 2, [0, 0, 0, 0], ''],                                // Green shows Team 2 only because Red + Blue are on Team 1: the teams clear
    [1, 1, [0, 1, 0, 0], ''],
    [2, 1, [0, 1, 1, 0], ''],
    [3, 1, [0, 1, 1, 0], REFUSE_FULL(1)],
    [3, 2, [0, 0, 0, 0], '']                                 // Black's Team 2 is lit (the other team): pressing it clears
]);
sequence('three play', [0, 1, 2], [0, 0, 0, 0], [
    [0, 1, [1, 0, 0, 0], ''], [1, 1, [1, 1, 0, 0], ''], [2, 1, [1, 1, 0, 0], REFUSE_FULL(1)],
    [2, 2, [1, 1, 2, 0], ''],                                // the third on Team 2 on its own: it plays against the pair
    [2, 2, [1, 1, 0, 0], ''],                                // ... and off again (nothing else keeps it lit)
    [0, 2, [2, 1, 0, 0], ''],                                // Green changes over to Team 2
    [1, 2, [2, 2, 0, 0], '']
]);
sequence('a press that cannot count', [0, 1, 2], [0, 0, 0, 0], [[3, 1, [0, 0, 0, 0], ''], [3, 2, [0, 0, 0, 0], '']]);
sequence('two play: no buttons', [0, 1], [0, 0, 0, 0], [[0, 1, [0, 0, 0, 0], ''], [1, 2, [0, 0, 0, 0], '']]);
sequence('switches that no room can hold are mended first', [0, 1, 2], [1, 1, 1, 0], [[0, 2, [2, 0, 0, 0], '']]);
sequence('a colour that stopped playing loses its button on the next press', [0, 1, 2], [1, 0, 0, 1], [[1, 1, [1, 1, 0, 0], '']]);
same('a press of a button that is none (0, 3, "1") changes nothing', [0, 3, '1', null].map((side) => R.pressSide([1, 0, 0, 0], ALL, 1, side).sides), [[1, 0, 0, 0], [1, 0, 0, 0], [1, 0, 0, 0], [1, 0, 0, 0]]);

// teamBytes, teamsText, reconcileSides, teamNeedsFix
[
    ['a pair on Team 1', [1, 0, 1, 0], ALL, { teamA: 0, teamB: 2 }], ['a pair on Team 2', [0, 2, 0, 2], ALL, { teamA: 1, teamB: 3 }], ['Red + Blue', [0, 1, 1, 0], ALL, { teamA: 1, teamB: 2 }],
    ['the lower colour comes first whichever was pressed first', [0, 0, 1, 1], ALL, { teamA: 2, teamB: 3 }], ['two and two: the pair of Team 1 is the one the room is told', [2, 2, 1, 1], ALL, { teamA: 2, teamB: 3 }],
    ['a pair of three', [1, 1, 0, 0], [0, 1, 2], { teamA: 0, teamB: 1 }], ['one colour is no team', [1, 0, 0, 0], ALL, { teamA: 255, teamB: 255 }], ['two play: no team', [1, 1, 0, 0], [0, 1], { teamA: 255, teamB: 255 }],
    ['three on one button: no team', [1, 1, 1, 0], [0, 1, 2], { teamA: 255, teamB: 255 }], ['the second colour of the pair does not play: no team', [1, 0, 0, 1], [0, 1, 2], { teamA: 255, teamB: 255 }],
    ['no buttons', [0, 0, 0, 0], ALL, { teamA: 255, teamB: 255 }], ['no list', null, ALL, { teamA: 255, teamB: 255 }]
].forEach(([label, sides, play, want]) => same('teamBytes: ' + label, R.teamBytes(sides, play), want));
const FREE = 'Free for all. For a team, put two colours on the same team.';
[
    // [label, sides, play, guest, the line under the colours]
    ['nobody else: nothing to say', [0, 0, 0, 0], [0], false, ''], ['nobody else, a guest', [0, 0, 0, 0], [0], true, ''], ['nobody plays', [0, 0, 0, 0], [], false, ''],
    ['two play: the host is told why there are no buttons', [0, 0, 0, 0], [0, 1], false, 'Teams need three or four players.'], ['two play: a guest is told nothing', [0, 0, 0, 0], [0, 1], true, ''],
    ['three play, no team: the host is told how', [0, 0, 0, 0], [0, 1, 2], false, FREE], ['three play, no team: a guest is told nothing', [0, 0, 0, 0], [0, 1, 2], true, ''],
    ['four play, no team (picture 2, 14)', [0, 0, 0, 0], ALL, false, FREE], ['four play, one button pressed alone', [1, 0, 0, 0], ALL, false, FREE], ['four play, one button pressed alone, a guest', [1, 0, 0, 0], ALL, true, ''],
    ['picture 5: Green + Blue against Red + Black', [1, 0, 1, 0], ALL, false, 'Teams: Green + Blue against Red + Black.'], ['picture 4: Green + Black against Red + Blue (a guest is told the teams)', [1, 0, 0, 1], ALL, true, 'Teams: Green + Black against Red + Blue.'],
    ['a pair on Team 2 is the same teams: Green is named first', [0, 2, 0, 2], ALL, false, 'Teams: Green + Blue against Red + Black.'], ['Red + Blue on Team 1: Green + Black against Red + Blue', [0, 1, 1, 0], ALL, false, 'Teams: Green + Black against Red + Blue.'],
    ['Green + Red', [1, 1, 0, 0], ALL, false, 'Teams: Green + Red against Blue + Black.'], ['Blue + Black on Team 1: the same teams as Green + Red', [0, 0, 1, 1], ALL, false, 'Teams: Green + Red against Blue + Black.'],
    ['Blue + Black on Team 2: the same', [0, 0, 2, 2], ALL, false, 'Teams: Green + Red against Blue + Black.'], ['Green + Black', [1, 0, 0, 1], ALL, false, 'Teams: Green + Black against Red + Blue.'],
    ['three play: the pair against the one who plays alone (Green + Red against Blue)', [1, 1, 0, 0], [0, 1, 2], false, 'Teams: Green + Red against Blue.'], ['three play: Red + Blue against Green', [0, 1, 1, 0], [0, 1, 2], false, 'Teams: Red + Blue against Green.'],
    ['three play, Black, Blue and Green: Blue + Black against Green', [0, 0, 1, 1], [0, 2, 3], false, 'Teams: Blue + Black against Green.'], ['three play: Red + Black on Team 2 against Blue', [0, 2, 0, 2], [1, 2, 3], false, 'Teams: Red + Black against Blue.']
].forEach(([label, sides, play, guest, want]) => same('teamsText: ' + label, R.teamsText(sides, play, guest), want));

const M4 = (o) => model(mk([P('Juniper'), P('Sam'), P('Priya'), P('Leo')], o));
const M3 = (o) => model(mk([P('Juniper'), P('Sam'), P('Priya'), O], o));
[
    // [label, the leader's own switches, the room, what the page keeps]
    ['the room holds a pair: it is the truth', [0, 0, 0, 0], M4({ teamA: 0, teamB: 2 }), [1, 0, 1, 0]], ['the room holds a pair: the leader\'s own pair is replaced', [0, 1, 1, 0], M4({ teamA: 0, teamB: 2 }), [1, 0, 1, 0]],
    ['the room holds a pair: a button pressed alone is dropped with the rest of the leader\'s own', [1, 0, 0, 0], M4({ teamA: 1, teamB: 3 }), [0, 1, 0, 1]],
    ['no pair in the room: a button pressed alone stays (the room cannot hold it)', [1, 0, 0, 0], M4(), [1, 0, 0, 0]], ['no pair in the room: two singles on two buttons stay', [1, 2, 0, 0], M4(), [1, 2, 0, 0]],
    ['no pair in the room: a pair of the leader\'s own that the room does not hold goes (the room is told of pairs only)', [1, 0, 1, 0], M4(), [0, 0, 0, 0]],
    ['a button of a colour that does not play goes', [1, 0, 0, 1], M3(), [1, 0, 0, 0]], ['fewer than three play: no buttons', [1, 2, 0, 0], model(mk([P('Juniper'), P('Sam'), O, O])), [0, 0, 0, 0]],
    ['the room\'s pair names a colour that does not play: it is no pair, and the leader\'s own single stays', [1, 0, 0, 0], M3({ teamA: 0, teamB: 3 }), [1, 0, 0, 0]], ['no list of switches', null, M4(), [0, 0, 0, 0]],
    ['the room holds a pair of three: it is the truth', [0, 0, 0, 0], M3({ teamA: 1, teamB: 2 }), [0, 1, 1, 0]]
].forEach(([label, local, m, want]) => same('reconcileSides: ' + label, R.reconcileSides(local, m), want));
[
    ['no team: nothing to mend', mk([P('A'), P('B'), P('C'), P('D')]), false], ['no team, two play', mk([P('A'), P('B'), O, O]), false],
    ['a pair of colours that play', mk([P('A'), P('B'), P('C'), O], { teamA: 0, teamB: 2 }), false], ['a pair of four', mk([P('A'), P('B'), P('C'), P('D')], { teamA: 1, teamB: 3 }), false],
    ['a pair that names a colour that does not play (Black is open)', mk([P('A'), P('B'), P('C'), O], { teamA: 0, teamB: 3 }), true], ['... the other colour', mk([P('A'), O, P('C'), P('D')], { teamA: 1, teamB: 3 }), true],
    ['a pair, but two play', mk([P('A'), P('B'), O, O], { teamA: 0, teamB: 1 }), true], ['a pair of a person and a computer player', mk([P('A'), P('B'), O, P('D')], { teamA: 0, teamB: 2, plan: [0, 0, 1, 0] }), false],
    ['a pair that names a colour left Nobody', mk([P('A'), P('B'), O, P('D')], { teamA: 0, teamB: 2, plan: [0, 0, 4, 0] }), true]
].forEach(([label, room, want]) => same('teamNeedsFix: ' + label, R.teamNeedsFix(room, model(room)), want));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 6. What the leader asks of the room: the plan (planWith) and a colour moved or exchanged (moveOf)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const PW = (room, change) => R.planWith(room, model(room), change);
const plan = (map, kinds, teamA, teamB) => ({ map: map, kinds: kinds, teamA: teamA === undefined ? 255 : teamA, teamB: teamB === undefined ? 255 : teamB });
const T3 = mk([P('Juniper'), P('Sam'), P('Priya'), O]);                                                  // three persons: Green, Red, Blue; Black open
const T3t = mk([P('Juniper'), P('Sam'), P('Priya'), O], { teamA: 0, teamB: 2 });
const TWO = mk([P('Juniper'), P('Sam'), O, O]);
const BOT2 = mk([P('Juniper'), P('Sam'), O, O], { plan: [0, 0, 2, 0], teamA: 0, teamB: 2 });            // Blue is a Medium bot, the pair is Green + Blue
const BOTS = mk([P('Juniper'), P('Sam'), O, O], { plan: [0, 0, 2, 3], teamA: 0, teamB: 2 });            // Blue Medium and Black Hard, the pair is Green + Blue
[
    ['no change: the room as it is (the map stays the room\'s: no name)', T3, undefined, plan('', [0, 0, 0, 0])], ['an empty change', T3, {}, plan('', [0, 0, 0, 0])],
    ['a map: the file name that the server knows', T3, { map: 'islands' }, plan('ISLANDS.LVL', [0, 0, 0, 0])], ['a map in capitals', T3, { map: 'TINY' }, plan('TINY.LVL', [0, 0, 0, 0])],
    ['every map', T3, { map: 'gauntlet' }, plan('GAUNTLET.LVL', [0, 0, 0, 0])], ['a map that the page does not list changes nothing', T3, { map: 'nowhere' }, plan('', [0, 0, 0, 0])], ['no map (empty)', T3, { map: '' }, plan('', [0, 0, 0, 0])],
    ['Black becomes a Hard bot', T3, { seat: 3, kind: 'hard' }, plan('', [0, 0, 0, 3])], ['... an Easy bot', T3, { seat: 3, kind: 'easy' }, plan('', [0, 0, 0, 1])], ['... a Medium bot', T3, { seat: 3, kind: 'medium' }, plan('', [0, 0, 0, 2])],
    ['... Nobody', T3, { seat: 3, kind: 'nobody' }, plan('', [0, 0, 0, 4])], ['... open again', mk([P('Juniper'), P('Sam'), P('Priya'), O], { plan: [0, 0, 0, 3] }), { seat: 3, kind: 'open' }, plan('', [0, 0, 0, 0])],
    ['Red (Sam holds it) is set too: the plan comes back when Sam leaves', T3, { seat: 1, kind: 'hard' }, plan('', [0, 3, 0, 0])],
    ['a word that is none changes nothing', T3, { seat: 3, kind: 'bogus' }, plan('', [0, 0, 0, 0])], ['a number is not a word', T3, { seat: 3, kind: 3 }, plan('', [0, 0, 0, 0])], ['a word without a colour', T3, { kind: 'hard' }, plan('', [0, 0, 0, 0])],
    ['a colour that is a text is none', T3, { seat: '3', kind: 'hard' }, plan('', [0, 0, 0, 0])],
    ['a map and a colour at once', T3, { map: 'small', seat: 2, kind: 'easy' }, plan('SMALL.LVL', [0, 0, 1, 0])],
    ['the room\'s team stays when the map changes', T3t, { map: 'tiny' }, plan('TINY.LVL', [0, 0, 0, 0], 0, 2)], ['... and when a fourth colour plays', T3t, { seat: 3, kind: 'easy' }, plan('', [0, 0, 0, 1], 0, 2)],
    ['... and when a person\'s colour is set (it plays all the same)', T3t, { seat: 0, kind: 'nobody' }, plan('', [4, 0, 0, 0], 0, 2)],
    ['a computer player of the team is set to open: only two play, the team goes', BOT2, { seat: 2, kind: 'open' }, plan('', [0, 0, 0, 0])], ['... to Nobody', BOT2, { seat: 2, kind: 'nobody' }, plan('', [0, 0, 4, 0])],
    ['... to Hard: it plays on, the team stays', BOT2, { seat: 2, kind: 'hard' }, plan('', [0, 0, 3, 0], 0, 2)], ['a fourth colour joins: the team stays', BOT2, { seat: 3, kind: 'easy' }, plan('', [0, 0, 2, 1], 0, 2)],
    ['the map changes: the team stays', BOT2, { map: 'islands' }, plan('ISLANDS.LVL', [0, 0, 2, 0], 0, 2)],
    ['four play, the fourth is set to open: three play, the team stays', BOTS, { seat: 3, kind: 'open' }, plan('', [0, 0, 2, 0], 0, 2)], ['four play, the colour of the team is set to Nobody: it is gone', BOTS, { seat: 2, kind: 'nobody' }, plan('', [0, 0, 4, 3])],
    ['a room whose team names a colour that does not play: the plan that goes out has none', mk([P('A'), P('B'), P('C'), O], { teamA: 0, teamB: 3 }), {}, plan('', [0, 0, 0, 0])],
    ['teams pressed: Red + Blue', T3, { sides: [0, 1, 1, 0] }, plan('', [0, 0, 0, 0], 1, 2)], ['teams pressed: Green + Blue', T3, { sides: [1, 0, 1, 0] }, plan('', [0, 0, 0, 0], 0, 2)],
    ['three on one button is no team', T3, { sides: [1, 1, 1, 0] }, plan('', [0, 0, 0, 0])], ['teams cleared (the mend of a team that cannot be)', T3t, { sides: [0, 0, 0, 0] }, plan('', [0, 0, 0, 0])],
    ['teams that name a colour that does not play (Black is open)', T3, { sides: [1, 0, 0, 1] }, plan('', [0, 0, 0, 0])], ['teams with two persons only', TWO, { sides: [1, 1, 0, 0] }, plan('', [0, 0, 0, 0])],
    ['teams and the colour that makes the third player at once', TWO, { seat: 2, kind: 'easy', sides: [1, 0, 1, 0] }, plan('', [0, 0, 1, 0], 0, 2)], ['teams and the colour that makes the fourth', T3, { seat: 3, kind: 'hard', sides: [1, 0, 0, 1] }, plan('', [0, 0, 0, 3], 0, 3)],
    ['teams that name a computer player that the same change makes Nobody', T3, { seat: 3, kind: 'nobody', sides: [1, 0, 0, 1] }, plan('', [0, 0, 0, 4])],
    ['everything at once: map, colour, teams', T3, { map: 'small', seat: 3, kind: 'easy', sides: [1, 0, 1, 0] }, plan('SMALL.LVL', [0, 0, 0, 1], 0, 2)]
].forEach(([label, room, change, want]) => {
    const before = JSON.stringify(room);
    const got = PW(room, change);
    same('planWith: ' + label, got, want);
    check('planWith: ' + label + ' (the room\'s own plan is not changed, a new list of kinds goes out)', JSON.stringify(room) === before && got.kinds !== room.plan);
});
{   // the team goes with a person who is moved: the room (the server) does it, and the plan that the leader's page sends next keeps the pair
    const before = mk([P('Juniper'), P('Sam'), P('Priya'), O], { teamA: 1, teamB: 2 });
    same('a person moves (Sam from Red to the open Black): the page asks the room to move him', R.moveOf(before, model(before), 1, 3), { op: 'move', from: 1, to: 3 });
    const after = mk([P('Juniper'), O, P('Priya'), P('Sam')], { teamA: 2, teamB: 3 });                           // the room, after: the team went along (Blue + Black)
    same('the plan sent after the move keeps the pair that moved with him', PW(after, { map: 'tiny' }), plan('TINY.LVL', [0, 0, 0, 0], 2, 3));
    same('... and the room needs no mending', R.teamNeedsFix(after, model(after)), false);
    const swapped = mk([P('Juniper'), P('Leo'), P('Priya'), P('Sam')], { teamA: 0, teamB: 3 });                    // Sam (Green's team-mate, Red) swapped with Leo (Black): the team went with Sam
    same('two persons swap: the plan sent after keeps the pair', PW(swapped, { seat: 2, kind: 'open' }), plan('', [0, 0, 0, 0], 0, 3));
    const gone = mk([P('Juniper'), O, P('Priya'), O], { teamA: 0, teamB: 1 });                                    // Sam left: the team that needed him is mended
    same('a person leaves, and the team that needed the colour has to go', [R.teamNeedsFix(gone, model(gone)), PW(gone, {}).teamA, PW(gone, {}).teamB], [true, 255, 255]);
}

const MV = S2;                                                                                                   // Green Juniper, Red Sam, Blue a Medium bot, Black open
const exchanged = (kinds, teamA, teamB) => ({ op: 'plan', plan: { map: '', kinds: kinds, teamA: teamA === undefined ? 255 : teamA, teamB: teamB === undefined ? 255 : teamB } });
[
    ['a colour onto itself: nothing', MV, 1, 1, null], ['from a colour that is none (-1, 4, 7)', MV, -1, 1, null], ['... 4', MV, 4, 1, null], ['... 7', MV, 7, 0, null], ['to a colour that is none (-1)', MV, 1, -1, null], ['... 4', MV, 1, 4, null],
    ['a person onto an open colour: the person moves', MV, 1, 3, { op: 'move', from: 1, to: 3 }], ['a person onto a person: they swap places', MV, 1, 0, { op: 'move', from: 1, to: 0 }],
    ['the host onto an open colour: the host moves too', MV, 0, 3, { op: 'move', from: 0, to: 3 }], ['a person onto a computer player: the person goes there', MV, 1, 2, { op: 'move', from: 1, to: 2 }],
    ['an open colour onto a person: the person goes to the open colour (the person is `from`)', MV, 3, 1, { op: 'move', from: 1, to: 3 }], ['a computer player onto a person: the person goes to its colour', MV, 2, 1, { op: 'move', from: 1, to: 2 }],
    ['a computer player and an open colour exchange their plans: no person is in it, the plan carries it', MV, 2, 3, exchanged([0, 0, 0, 2])], ['... dragged the other way: the same exchange', MV, 3, 2, exchanged([0, 0, 0, 2])],
    ['two bots exchange levels', mk([P('A'), O, O, O], { plan: [0, 1, 3, 0] }), 1, 2, exchanged([0, 3, 1, 0])], ['an open colour and Nobody exchange', mk([P('A'), O, O, O], { plan: [0, 4, 0, 0] }), 1, 2, exchanged([0, 0, 4, 0])],
    ['two open colours exchange: nothing changes, but the plan is asked all the same', mk([P('A'), O, O, O]), 2, 3, exchanged([0, 0, 0, 0])],
    ['the pair of a computer player follows it (Blue + Green: Blue goes to Black)', mk([P('Juniper'), P('Sam'), O, O], { plan: [0, 0, 3, 2], teamA: 0, teamB: 2 }), 2, 3, exchanged([0, 0, 2, 3], 0, 3)],
    ['... and the lower colour comes first (Red + Blue: Red goes to Black, the pair is Blue + Black)', mk([P('Juniper'), O, O, O], { plan: [0, 1, 3, 0], teamA: 1, teamB: 2 }), 1, 3, exchanged([0, 0, 3, 1], 2, 3)],
    ['a pair of two computer players that exchange places stays a pair', mk([P('Juniper'), O, O, O], { plan: [0, 1, 2, 3], teamA: 1, teamB: 2 }), 1, 2, exchanged([0, 2, 1, 3], 1, 2)],
    ['the other colour of the pair does not move: Black and Blue exchange, the pair Green + Red stays', mk([P('Juniper'), P('Sam'), O, O], { plan: [0, 0, 2, 3], teamA: 0, teamB: 1 }), 2, 3, exchanged([0, 0, 3, 2], 0, 1)]
].forEach(([label, room, from, to, want]) => {
    const before = JSON.stringify(room);
    same('moveOf: ' + label, R.moveOf(room, model(room), from, to), want);
    check('moveOf: ' + label + ' (the room is not changed)', JSON.stringify(room) === before);
});
{
    const room = mk([P('Juniper'), O, O, O], { plan: [0, 1, 3, 0] });
    const got = R.moveOf(room, model(room), 1, 2);
    check('moveOf: the plan of an exchange is a list of its own (the room\'s plan is not the one that goes out)', got.plan.kinds !== room.plan);
    same('... and the room\'s plan is as it was', room.plan, [0, 1, 3, 0]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 7. What each card and the map's side show: the owner's pictures, word for word
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const ctx = (o) => Object.assign({ sides: [0, 0, 0, 0], picked: -1, removing: -1, offline: false, refusal: '' }, o || {});
const view = (room, o) => R.viewOf(model(room), ctx(o));
// a card in a few words: only what is on, so that a field that should be off but is on shows
function show(c) {
    const o = { seat: c.seat, colour: c.colour.name, kind: c.kind, who: c.who.map((w) => w.text + '/' + w.cls), stat: c.stat };
    if (c.statCls !== 'pstat') o.statCls = c.statCls;
    ['mode', 'tab', 'label', 'drop'].forEach((f) => { if (c[f]) o[f] = c[f]; });
    ['pencil', 'remove', 'ask', 'grip'].forEach((f) => { if (c[f]) o[f] = true; });
    if (c.teams) o.teams = c.teams.map((t) => (t.pressed ? '+' : '-') + t.side + (t.disabled ? 'x' : '')).join(' ');
    return o;
}
const seatName = ['Green', 'Red', 'Blue', 'Black'];
const youHost = (seat, extra, name) => Object.assign({ seat: seat, colour: seatName[seat], kind: 'person', who: [(name || 'Juniper') + '/nm', 'You/you'], stat: 'Host. You start the match.', pencil: true, grip: true, label: seatName[seat] + ': ' + (name || 'Juniper') + ', you, host' }, extra || {});
const sam = (seat, extra) => Object.assign({ seat: seat, colour: seatName[seat], kind: 'person', who: ['Sam/nm'], stat: 'In the room', remove: true, grip: true, label: seatName[seat] + ': Sam' }, extra || {});
const priya = (seat, extra) => Object.assign({ seat: seat, colour: seatName[seat], kind: 'person', who: ['Priya/nm'], stat: 'In the room', remove: true, grip: true, label: seatName[seat] + ': Priya' }, extra || {});
const open = (seat, extra) => Object.assign({ seat: seat, colour: seatName[seat], kind: 'open', who: [], stat: 'Takes the next player who joins.', mode: 'open', grip: true, label: seatName[seat] + ': open' }, extra || {});
const bot = (seat, level, extra) => Object.assign({ seat: seat, colour: seatName[seat], kind: 'bot', who: [], stat: 'Computer player', mode: level, grip: true, label: seatName[seat] + ': bot' }, extra || {});
const NOTE_INVITE = 'Players who open this link take the next free colour.';
const NOTE_DRAG = 'Drag a player onto another colour to move them. A colour that is taken swaps places.';
const NOTE_WAIT = 'Waiting for players. Drag a player onto another colour to move them.';
const TREASURE = { mapKey: 'treasure', mapName: 'Treasure', mapInfo: 'One person\'s trash... (12 min)' };
const side = (o) => Object.assign({ mapDisabled: false, startDisabled: false, startBusy: false, startLabel: 'START!' }, TREASURE, o);
function expectView(label, v, want) {
    same(label + ': the four cards, in seat order', v.cards.map(show), want.cards);
    same(label + ': the notes', v.notes, want.notes);
    same(label + ': the map\'s side', v.side, want.side);
    same(label + ': the heading and the state', { heading: v.heading, guest: v.guest, busy: v.busy, ro: v.ro, offline: v.offline, removing: v.removing }, Object.assign({ heading: 'Your room', guest: false, busy: false, ro: false, offline: false, removing: -1 }, want.top));
    same(label + ': the switches and the colours that play', { sides: v.sides, play: v.play }, { sides: want.sides || [0, 0, 0, 0], play: want.play });
}
const lit = (v) => v.cards.map((c) => c.teams.map((t) => (t.pressed ? '+' : '-') + t.side + (t.disabled ? 'x' : '')).join(' '));

// picture 1: you alone, three colours open
expectView('picture 1 (you alone)', view(S1), {
    cards: [youHost(0), open(1), open(2), open(3)], play: [0],
    notes: { invite: NOTE_INVITE, slots: NOTE_WAIT, teams: '', teamsWarn: false },
    side: side({ plan: 'A game for one. Red, Blue and Black are open and stay empty unless a player joins first.', planShort: 'You · Treasure · 3 open' })
});
// picture 2: Sam in Red, a Medium bot in Blue, Black open: Team 1 and Team 2 under the three colours that play
expectView('picture 2 (three play)', view(S2), {
    cards: [youHost(0, { teams: '-1 -2' }), sam(1, { teams: '-1 -2' }), bot(2, 'medium', { teams: '-1 -2' }), open(3)], play: [0, 1, 2],
    notes: { invite: NOTE_INVITE, slots: NOTE_DRAG, teams: FREE, teamsWarn: false },
    side: side({ plan: 'You, Sam and a Medium bot play on Treasure. Black is open and stays empty unless a player joins first.', planShort: 'You, Sam and Medium bot · Treasure · 1 open' })
});
// picture 6 (the phone) shows the same room: the four cards are one column in the order of the picture, the line under START is the short one
same('picture 6 (phone): the cards lie Black, Green, Red, Blue, so the column reads an open Black, then the host (Green), Sam, the bot', R.GRID.map((seat) => view(S2).cards[seat]).map((c) => c.colour.name + ':' + c.kind), ['Black:open', 'Green:person', 'Red:person', 'Blue:bot']);
same('picture 6 (phone): the line under START', view(S2).side.planShort, 'You, Sam and Medium bot · Treasure · 1 open');
// picture 3: dragging; the dots and then a colour (the tap way): the question in the line, a word on each card
{
    const v = view(S2, { picked: 1 });
    same('picture 3 (tap way, Sam picked): every card says what a tap does, the line says what to do', [v.cards.map((c) => c.drop), v.notes.slots], [['Tap to put Sam here', 'Tap to put Sam here', 'Tap to put Sam here', 'Tap to put Sam here'], 'Now tap the colour where Sam goes (tap the same player again to let go).']);
    same('picture 3 (tap way): nothing else changes on the cards', v.cards.map((c) => { const s = show(c); delete s.drop; return s; }), view(S2).cards.map(show));
    same('picture 3 (a drag): the cards carry no word of the tap way, the line is the drag line (the words Move here / Swap places are the page\'s own)', [view(S2).cards.map((c) => c.drop), view(S2).notes.slots], [['', '', '', ''], NOTE_DRAG]);
    same('picture 3 (tap way): what each kind of colour is called when it is the one that was picked', [0, 2, 3].map((i) => view(S2, { picked: i }).cards[0].drop), ['Tap to put Juniper here', 'Tap to put Medium bot here', 'Tap to put the open Black here']);
    same('picture 3 (tap way): the line names the one that was picked (a computer player, an open colour)', [2, 3].map((i) => view(S2, { picked: i }).notes.slots), ['Now tap the colour where Medium bot goes (tap the same player again to let go).', 'Now tap the colour where the open Black goes (tap the same player again to let go).']);
    same('a picked colour that is none (-1, nothing): the drag line', [view(S2, { picked: -1 }).notes.slots, R.viewOf(model(S2), { sides: [0, 0, 0, 0] }).notes.slots], [NOTE_DRAG, NOTE_DRAG]);
}
// picture 5: Green + Blue against Red + Black; the pressed buttons are lit, the other team shows lit on the other two, a full button is dashed
expectView('picture 5 (teams set)', view(S5, { sides: [1, 0, 1, 0] }), {
    cards: [youHost(0, { teams: '+1 -2x' }), sam(1, { teams: '-1x +2' }), bot(2, 'medium', { teams: '+1 -2x' }), priya(3, { teams: '-1x +2' })], play: [0, 1, 2, 3], sides: [1, 0, 1, 0],
    notes: { invite: NOTE_INVITE, slots: NOTE_DRAG, teams: 'Teams: Green + Blue against Red + Black.', teamsWarn: false },
    side: side({ plan: 'You, Sam, a Medium bot and Priya play on Treasure.', planShort: 'You, Sam, Medium bot and Priya · Treasure' })
});
// picture 5b: a third colour pressed on Team 1: the press is refused and the line says why (the page puts the refusal into the context)
{
    const pressed = R.pressSide([1, 0, 1, 0], ALL, 1, 1);
    same('picture 5b: the press of Red on Team 1 is refused with the words of the picture', pressed, { sides: [1, 0, 1, 0], refusal: 'Team 1 has two colours already. Press one of them to take it off first.' });
    const v = view(S5, { sides: pressed.sides, refusal: pressed.refusal });
    same('picture 5b: the line under the colours is the refusal, in the warning colour; the teams stay as they were', [v.notes.teams, v.notes.teamsWarn, v.cards.map((c) => c.teams.map((t) => (t.pressed ? '+' : '-') + t.side).join(' '))], ['Team 1 has two colours already. Press one of them to take it off first.', true, ['+1 -2', '-1 +2', '+1 -2', '-1 +2']]);
    same('a refusal that is empty is no warning', [view(S5, { sides: [1, 0, 1, 0], refusal: '' }).notes.teamsWarn, view(S5, { sides: [1, 0, 1, 0], refusal: undefined }).notes.teamsWarn], [false, false]);
}
// the leader's own press of one button (a room cannot hold it): lit alone, the others may still take it
expectView('one button pressed alone', view(S14, { sides: [1, 0, 0, 0] }), {
    cards: [youHost(0, { teams: '+1 -2' }), sam(1, { teams: '-1 -2' }), bot(2, 'medium', { teams: '-1 -2' }), priya(3, { teams: '-1 -2' })], play: [0, 1, 2, 3], sides: [1, 0, 0, 0],
    notes: { invite: NOTE_INVITE, slots: NOTE_DRAG, teams: FREE, teamsWarn: false },
    side: side({ plan: 'You, Sam, a Medium bot and Priya play on Treasure.', planShort: 'You, Sam, Medium bot and Priya · Treasure' })
});
// picture 14 (and 13 before START): four play, no teams
same('picture 14: four play, no teams: the line is free for all and every colour has both buttons, none lit or dashed', [view(S14).notes.teams, lit(view(S14))], [FREE, ['-1 -2', '-1 -2', '-1 -2', '-1 -2']]);
// picture 12: Juniper left, Sam leads from Red
expectView('picture 12 (the host left)', view(S12), {
    cards: [open(0), youHost(1, { teams: '-1 -2' }, 'Sam'), bot(2, 'medium', { teams: '-1 -2' }), priya(3, { teams: '-1 -2' })], play: [1, 2, 3],
    notes: { invite: NOTE_INVITE, slots: NOTE_DRAG, teams: FREE, teamsWarn: false },
    side: side({ plan: 'You, a Medium bot and Priya play on Treasure. Green is open and stays empty unless a player joins first.', planShort: 'You, Medium bot and Priya · Treasure · 1 open' })
});
// picture 4: Sam's screen: the teams are read-only tabs, nothing can be pressed, dragged or set; START waits for the host
expectView('picture 4 (a player\'s screen)', view(S4), {
    cards: [
        { seat: 0, colour: 'Green', kind: 'person', who: ['Juniper/nm', 'Host/you'], stat: 'Starts the match.', tab: 'Team 1', label: 'Green: Juniper, host' },
        { seat: 1, colour: 'Red', kind: 'bot', who: ['Easy bot/'], stat: 'Computer player', tab: 'Team 2', label: 'Red: Easy bot' },
        { seat: 2, colour: 'Blue', kind: 'bot', who: ['Medium bot/'], stat: 'Computer player', tab: 'Team 2', label: 'Blue: Medium bot' },
        { seat: 3, colour: 'Black', kind: 'person', who: ['Sam/nm', 'You/you'], stat: 'Waiting for the host to start.', pencil: true, tab: 'Team 1', label: 'Black: Sam, you' }
    ], play: [0, 1, 2, 3], sides: [1, 0, 0, 1],
    top: { heading: 'Juniper’s room', guest: true, ro: true },
    notes: { invite: NOTE_INVITE, slots: 'Juniper arranges the colours and starts the match.', teams: 'Teams: Green + Black against Red + Blue.', teamsWarn: false },
    side: side({ mapDisabled: true, startDisabled: true, startLabel: 'Waiting for Juniper', plan: 'Juniper starts the match when everybody is in. Juniper, an Easy bot, a Medium bot and you play on Treasure.', planShort: 'Juniper starts the match' })
});
same('picture 4: the leader\'s own switches do not show on a player\'s screen, the room\'s do', view(S4, { sides: [2, 2, 0, 0] }).cards.map((c) => c.tab), ['Team 1', 'Team 2', 'Team 2', 'Team 1']);
// picture 13: START is pressed (the host's screen): the room is frozen, each player is opening the game or in it
expectView('picture 13 (START pressed, the host)', view(S13), {
    cards: [
        { seat: 0, colour: 'Green', kind: 'person', who: ['Juniper/nm', 'You/you'], stat: 'Opening the game…', statCls: 'pstat wait', label: 'Green: Juniper, you, host' },
        { seat: 1, colour: 'Red', kind: 'person', who: ['Sam/nm'], stat: 'In the game', statCls: 'pstat in', label: 'Red: Sam' },
        { seat: 2, colour: 'Blue', kind: 'bot', who: ['Medium bot/'], stat: 'Computer player', label: 'Blue: Medium bot' },
        { seat: 3, colour: 'Black', kind: 'person', who: ['Priya/nm'], stat: 'Opening the game…', statCls: 'pstat wait', label: 'Black: Priya' }
    ], play: [0, 1, 2, 3],
    top: { busy: true, ro: true },
    notes: { invite: 'New players cannot join while the game opens.', slots: 'The match begins as soon as all of you are in the game.', teams: FREE, teamsWarn: false },
    side: side({ mapDisabled: true, startDisabled: true, startBusy: true, startLabel: 'Getting ready…', plan: 'Opening the game for everybody. The match begins as soon as all of you are in.', planShort: 'Opening the game for everybody' })
});
// picture 13b: the same on Sam's phone
expectView('picture 13b (START pressed, a player)', view(S13b), {
    cards: [
        { seat: 0, colour: 'Green', kind: 'person', who: ['Juniper/nm', 'Host/you'], stat: 'In the game', statCls: 'pstat in', label: 'Green: Juniper, host' },
        { seat: 1, colour: 'Red', kind: 'person', who: ['Sam/nm', 'You/you'], stat: 'Opening the game…', statCls: 'pstat wait', label: 'Red: Sam, you' },
        { seat: 2, colour: 'Blue', kind: 'bot', who: ['Medium bot/'], stat: 'Computer player', label: 'Blue: Medium bot' },
        { seat: 3, colour: 'Black', kind: 'person', who: ['Priya/nm'], stat: 'Opening the game…', statCls: 'pstat wait', label: 'Black: Priya' }
    ], play: [0, 1, 2, 3],
    top: { heading: 'Juniper’s room', guest: true, busy: true, ro: true },
    notes: { invite: 'New players cannot join while the game opens.', slots: 'The match begins as soon as all of you are in the game.', teams: '', teamsWarn: false },
    side: side({ mapDisabled: true, startDisabled: true, startBusy: true, startLabel: 'Getting ready…', plan: 'Juniper pressed START. Opening the game for everybody. The match begins as soon as all of you are in.', planShort: 'Juniper pressed START' })
});
same('START is pressed: a pair that is in the room shows as read-only tabs on the host\'s frozen cards, too', view(mk([P('Juniper'), P('Sam'), O, P('Priya')], { plan: [0, 0, 2, 0], starting: true, teamA: 0, teamB: 2 }), { sides: [1, 0, 1, 0] }).cards.map((c) => c.tab), ['Team 1', 'Team 2', 'Team 1', 'Team 2']);
// picture 15b: the connection dropped (the host's screen): the room goes grey and read-only, START says Reconnecting
expectView('picture 15b (the connection is lost)', view(S14, { offline: 'lost' }), {
    cards: [
        { seat: 0, colour: 'Green', kind: 'person', who: ['Juniper/nm', 'You/you'], stat: 'Host. You start the match.', label: 'Green: Juniper, you, host' },
        { seat: 1, colour: 'Red', kind: 'person', who: ['Sam/nm'], stat: 'In the room', label: 'Red: Sam' },
        { seat: 2, colour: 'Blue', kind: 'bot', who: ['Medium bot/'], stat: 'Computer player', label: 'Blue: Medium bot' },
        { seat: 3, colour: 'Black', kind: 'person', who: ['Priya/nm'], stat: 'In the room', label: 'Black: Priya' }
    ], play: [0, 1, 2, 3],
    top: { ro: true, offline: 'lost' },
    notes: { invite: NOTE_INVITE, slots: 'Your colour is kept for a minute while the page gets you back in.', teams: FREE, teamsWarn: false },
    side: side({ mapDisabled: true, startDisabled: true, startLabel: 'Reconnecting…', plan: 'START comes back as soon as you are in.', planShort: 'Getting you back in' })
});
same('the connection is lost on a player\'s screen: START says Reconnecting, the pencil is gone', [view(S4, { offline: 'lost' }).side.startLabel, view(S4, { offline: 'lost' }).cards.map((c) => c.pencil)], ['Reconnecting…', [false, false, false, false]]);
// picture 16: the page that waits for a name (a room of nobody: four open colours, nothing to press)
{
    const blank = R.modelOf({ slots: ALL.map(() => ({ state: 0, name: '', rtt: 0, platform: 0 })), plan: [0, 0, 0, 0], map: 'TREASURE.LVL', you: 0, leader: 0, teamA: 255, teamB: 255, inGame: 0, starting: false });
    const v = R.viewOf(blank, ctx({ offline: 'join' }));
    same('picture 16 (joining a room): four open colours, nothing to press, no names, START off',
        [v.heading, v.cards.map((c) => [c.kind, c.who.map((w) => w.text), c.stat, c.grip, c.mode, !!c.teams, c.pencil, c.remove]), v.notes, v.side.startLabel, v.side.startDisabled, v.side.mapDisabled, v.side.plan, v.side.planShort],
        ['Joining a room', ALL.map(() => ['open', ['Open'], 'Takes the next player who joins.', false, '', false, false, false]), { invite: NOTE_INVITE, slots: '', teams: '', teamsWarn: false }, 'START!', true, true, '', '']);
    const none = R.viewOf(blank, ctx({ offline: 'none' }));
    same('a page with no room (the server refused, or cannot be reached): no line of its own, START off, "No room yet"', [none.heading, none.notes.slots, none.side.startLabel, none.side.startDisabled, none.side.plan, none.side.planShort], ['Your room', '', 'START!', true, 'There is no room yet.', 'No room yet']);
}
// picture 17: the host's Remove buttons and the question
{
    same('picture 17: a Remove button on every other person of the host\'s screen: not on its own colour, a computer player or an open colour', view(S14).cards.map((c) => c.remove), [false, true, false, true]);
    same('... and none on the open colour of picture 2', view(S2).cards.map((c) => c.remove), [false, true, false, false]);
    const asks = view(S14, { removing: 3 });
    same('picture 17b (the question): Priya\'s card asks and has no button, Sam keeps his', [asks.cards.map((c) => c.remove), asks.cards.map((c) => c.ask), asks.removing], [[false, true, false, false], [false, false, false, true], 3]);
    same('a question about the host\'s own colour, a computer player, an open colour, a colour that does not exist or none: nobody is asked', [0, 2, 9, -1, undefined].map((r) => view(S14, { removing: r }).removing).concat(view(S2, { removing: 3 }).removing), [-1, -1, -1, -1, -1, -1]);
    same('a question goes with its player: when the colour holds nobody (Priya left) the question is gone', view(S2, { removing: 3 }).cards.map((c) => c.ask), [false, false, false, false]);
    same('a player\'s screen has no Remove button and no question', [view(S4, { removing: 0 }), view(S4, { removing: 1 })].map((v) => [v.removing, v.cards.some((c) => c.remove || c.ask)]), [[-1, false], [-1, false]]);
    same('while START waits and while the link is lost (or there is no room) there is no Remove button and no question', [view(S13, { removing: 1 }), view(S14, { removing: 1, offline: 'lost' }), view(S14, { removing: 1, offline: 'none' }), view(S14, { removing: 1, offline: 'join' })].map((v) => [v.removing, v.cards.some((c) => c.remove || c.ask)]),
        [[-1, false], [-1, false], [-1, false], [-1, false]]);
}
// picture 16c: the pencil of a player's own name, on a player's screen too; not while START waits, not while the link is lost
same('picture 16c: the pencil is on the page\'s own name only (the host\'s, a player\'s), not while START waits or the link is lost', [
    view(S14), view(S4), view(S13), view(S13b), view(S14, { offline: 'lost' }), view(S4, { offline: 'lost' })
].map((v) => v.cards.map((c) => c.pencil ? 1 : 0).join('')), ['1000', '0001', '0000', '0000', '0000', '0000']);
// the grips (the dots: drag a player onto another colour) and the drop-downs are the host's, while the room is open
same('only the host of an open room can drag (grips) and set the colours (drop-downs): a player, START, a lost link have none', [view(S2), view(S4), view(S13), view(S2, { offline: 'lost' }), view(S2, { offline: 'none' })].map((v) => [v.cards.filter((c) => c.grip).length, v.cards.filter((c) => c.mode).length]), [[4, 2], [0, 0], [0, 0], [0, 0], [0, 0]]);

// ---- the map's side: who plays and what an open colour does ----
[
    // [label, room, context, the line under START, the short line (a phone)]
    ['alone, three colours open (picture 1)', S1, {}, 'A game for one. Red, Blue and Black are open and stay empty unless a player joins first.', 'You · Treasure · 3 open'],
    ['alone, two open (one is Nobody)', mk([P('Juniper'), O, O, O], { plan: [0, 0, 4, 0] }), {}, 'A game for one. Red and Black are open and stay empty unless a player joins first.', 'You · Treasure · 2 open'],
    ['alone, one open', mk([P('Juniper'), O, O, O], { plan: [0, 4, 4, 0] }), {}, 'A game for one. Black is open and stays empty unless a player joins first.', 'You · Treasure · 1 open'],
    ['alone, nobody else may come', mk([P('Juniper'), O, O, O], { plan: [0, 4, 4, 4] }), {}, 'A game for one: only your colony is on the map.', 'You · Treasure'],
    ['you and an Easy bot: an Easy, never a Easy', mk([P('Juniper'), O, O, O], { plan: [0, 1, 0, 4] }), {}, 'You and an Easy bot play on Treasure. Blue is open and stays empty unless a player joins first.', 'You and Easy bot · Treasure · 1 open'],
    ['a Hard bot and a Medium bot', mk([P('Juniper'), O, O, O], { plan: [0, 3, 2, 4] }), {}, 'You, a Hard bot and a Medium bot play on Treasure.', 'You, Hard bot and Medium bot · Treasure'],
    ['another map, two open', mk([P('Juniper'), P('Sam'), O, O], { map: 'ISLANDS.LVL' }), {}, 'You and Sam play on Islands. Blue and Black are open and stay empty unless a player joins first.', 'You and Sam · Islands · 2 open'],
    ['the room has no map yet: Treasure, the default', mk([P('Juniper'), P('Sam'), P('Priya'), P('Leo')], { map: '' }), {}, 'You, Sam, Priya and Leo play on Treasure.', 'You, Sam, Priya and Leo · Treasure'],
    ['a player\'s screen: the host starts it (no line about open colours)', mk([P('Juniper'), P('Sam'), O, O], { you: 1 }), {}, 'Juniper starts the match when everybody is in. Juniper and you play on Treasure.', 'Juniper starts the match'],
    ['a player\'s screen, a room of four', mk([P('Juniper'), P('Sam'), P('Priya'), P('Leo')], { you: 2 }), {}, 'Juniper starts the match when everybody is in. Juniper, Sam, you and Leo play on Treasure.', 'Juniper starts the match'],
    ['the host pressed START (a player\'s screen)', mk([P('Juniper'), P('Sam'), O, O], { you: 1, starting: true }), {}, 'Juniper pressed START. Opening the game for everybody. The match begins as soon as all of you are in.', 'Juniper pressed START'],
    ['the connection is lost, a player\'s screen', mk([P('Juniper'), P('Sam'), O, O], { you: 1 }), { offline: 'lost' }, 'START comes back as soon as you are in.', 'Getting you back in'],
    ['no room', S1, { offline: 'none' }, 'There is no room yet.', 'No room yet'],
    ['joining a room', S1, { offline: 'join' }, '', '']
].forEach(([label, room, o, plan, short]) => {
    const v = view(room, o);
    same('the map\'s side, ' + label + ': the line under START', v.side.plan, plan);
    same('the map\'s side, ' + label + ': the short line', v.side.planShort, short);
});
R.MAPS.forEach((m) => {
    const s = view(mk([P('Juniper'), O, O, O], { map: m.file })).side;
    same('the map\'s side shows ' + m.key + ': its name and its Map Info line', [s.mapKey, s.mapName, s.mapInfo], [m.key, m.name, m.info]);
});
same('the map\'s side asked directly is the same as inside viewOf', R.sideView(model(S2), ctx(), R.playing(model(S2))), view(S2).side);
same('START waits for the host: "Waiting for" and the name of the host', [view(S4).side.startLabel, view(mk([P('Zed'), P('Sam'), O, O], { you: 1 })).side.startLabel], ['Waiting for Juniper', 'Waiting for Zed']);
same('START of a page that has no room is off (like the map)', [view(S1, { offline: 'none' }).side.startDisabled, view(S1, { offline: 'none' }).side.mapDisabled, view(S1, { offline: 'none' }).side.startBusy], [true, true, false]);

// ---- a name is text and nothing more ----
{
    const NAME_A = '<b>Juniper</b>', NAME_B = 'a&b "q" \'x\' <i on=1>', NAME_C = 'W'.repeat(32);
    const room = mk([P(NAME_A), P(NAME_B), P(NAME_C), O], { you: 1 });
    const v = view(room);
    same('names go through as they are: no markup is made, no letter is changed, nothing is cut', [v.cards[0].who[0].text, v.cards[1].who[0].text, v.cards[2].who[0].text], [NAME_A, NAME_B, NAME_C]);
    same('... in the heading, the labels and the lines', [v.heading, v.cards[0].label, v.cards[1].label, v.side.startLabel, v.side.plan === NAME_A + ' starts the match when everybody is in. ' + NAME_A + ', you and ' + NAME_C + ' play on Treasure.', v.notes.slots],
        [NAME_A + '’s room', 'Green: ' + NAME_A + ', host', 'Red: ' + NAME_B + ', you', 'Waiting for ' + NAME_A, true, NAME_A + ' arranges the colours and starts the match.']);
    same('... in the question about a colour that is picked', R.viewOf(model(mk([P(NAME_A), P(NAME_B), O, O])), ctx({ picked: 1 })).cards[0].drop, 'Tap to put ' + NAME_B + ' here');
}
same('the page does not change the room it is shown or the switches it is given', (() => { const m = model(S5), c = ctx({ sides: [1, 0, 1, 0] }), a = JSON.stringify([m, c]); R.viewOf(m, c); return JSON.stringify([m, c]) === a; })(), true);
same('a context with nothing in it is the plain host screen', R.viewOf(model(S2), {}).cards.map(show), view(S2).cards.map(show));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 8. The sentences of the server (noticeKind) and every message strip of picture 15 (banner)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the words of the C++ server, read from include/ants_net/protocol.hpp (the page matches what the server says, so the check holds it to the server's own text)
const protocolPath = path.join(repoDir, 'include', 'ants_net', 'protocol.hpp');
const protocolText = fs.existsSync(protocolPath) ? fs.readFileSync(protocolPath, 'utf8') : '';
const said = (name) => { const m = new RegExp(name + ' = "((?:[^"\\\\]|\\\\.)*)"').exec(protocolText); return m ? m[1] : null; };
const SAID = { late: said('kNoticeGameLate'), lates: said('kNoticeGamesLate'), maplost: said('kNoticeMapLost'), mapcolours: said('kNoticeMapColours'), noplace: said('kNoticeNoPlace'), startsfailed: said('kNoticeStartsFailed'), noteams: said('kNoticeNoTeams'), overFirst: said('kNoticeMatchOverFirst'), overLast: said('kNoticeMatchOverLast') };
same('the sentences of the server\'s START, as protocol.hpp has them', SAID, {
    late: '\'s game did not come in time.', lates: 'These games did not come in time: ', maplost: 'This map could not be loaded: choose another map.', mapcolours: 'This map cannot be played with these colours.',
    noplace: 'The server has no place for another match right now: try again in a minute.', startsfailed: 'The match could not start a few times in a row: try again in a minute.', noteams: 'No teams: ',
    overFirst: 'Your match on ', overLast: ' is over.'
});
const PRIYA_LATE = 'Priya' + SAID.late;
[
    // [the line that the room said, the kind]
    [PRIYA_LATE, 'late'], ['Sam' + SAID.late, 'late'], ['x' + SAID.late, 'late'], [SAID.lates + 'Priya, Sam.', 'late'], [SAID.lates + 'Priya.', 'late'], [SAID.lates, 'late'],
    [SAID.noplace, 'noplace'], ['The server has no place for another match right now', 'noplace'], [SAID.noplace + ' (later)', 'noplace'], [SAID.startsfailed, 'startsfailed'], [SAID.maplost, 'maplost'], [SAID.mapcolours, 'mapcolours'],
    // near misses are only notices: the sentences are matched whole, from the first letter
    [SAID.maplost.slice(0, -1), 'notice'], [SAID.mapcolours.slice(0, -1), 'notice'], [SAID.maplost + ' ', 'notice'], [' ' + SAID.mapcolours, 'notice'], [SAID.mapcolours.toLowerCase(), 'notice'], [SAID.mapcolours + ' Later.', 'notice'],
    [PRIYA_LATE.slice(0, -1), 'notice'], [PRIYA_LATE + ' Press START.', 'notice'], ['Priya’s game did not come in time.', 'notice'], ['These games did not come in time', 'notice'], ['These games did not come in time:', 'notice'],
    ['  ' + SAID.noplace, 'notice'], ['the server has no place for another match right now', 'notice'], [SAID.startsfailed.slice(0, 20), 'notice'], ['The match could not start', 'notice'],
    [SAID.noteams + 'the teams need a pair.', 'notice'], ['Hello', 'notice'], ['', 'notice'],
    // the line that a moved player is sent (HostLobby::move_seat) has no strip: the colour and the toast say it
    ['Sam moved you to Blue.', 'moved'], ['The room moved you to Red.', 'moved'], ['Player 2 moved you to Black.', 'moved'], ['Sam moved you to Blue', 'notice'], [' moved you to Blue.', 'notice'], ['moved you to Blue.', 'notice'],
    ['Sam moved you to Blue. Later.', 'notice'], ['Sam moved you to blue 2.', 'notice'], ['Sam moved you', 'notice']
].forEach(([text, kind]) => same('noticeKind(' + JSON.stringify(text).slice(0, 60) + ') is ' + kind + ', with the text as the room said it', R.noticeKind(text), { kind: kind, server: text }));
// the line that the room says once to a person who comes back to the room after its match (HostLobby::restore_return): the file of the map, between the server's two halves
[['TREASURE.LVL', 'Treasure'], ['treasure.lvl', 'Treasure'], ['SMALL.LVL', R.mapOfFile('SMALL.LVL') ? R.mapOfFile('SMALL.LVL').name : 'SMALL'], ['SOMEWHERE.LVL', 'SOMEWHERE'], ['my_map.lvl', 'my_map'], ['TWO WORDS.LVL', 'TWO WORDS'], ['NOFILE', 'NOFILE']].forEach(([file, name]) =>
    same('noticeKind of the room after a match on ' + file + ' is "over" with the map\'s name ' + name, R.noticeKind(SAID.overFirst + file + SAID.overLast), { kind: 'over', server: SAID.overFirst + file + SAID.overLast, map: name }));
[SAID.overFirst + 'TREASURE.LVL' + SAID.overLast + ' Later.', ' ' + SAID.overFirst + 'TREASURE.LVL' + SAID.overLast, SAID.overFirst + SAID.overLast, SAID.overFirst + 'TREASURE.LVL is over', 'your match on TREASURE.LVL is over.'].forEach((text) =>
    same('noticeKind(' + JSON.stringify(text).slice(0, 60) + ') is only a notice (the sentence is matched whole, with a file name in it)', R.noticeKind(text), { kind: 'notice', server: text }));
same('the strip for that line is picture 8\'s, whatever map the page lists', R.MAPS.map((m) => R.banner('over', R.noticeKind(SAID.overFirst + m.file + SAID.overLast)).text), R.MAPS.map((m) => 'Welcome back. Your match on ' + m.name + ' is over. Everything is as you left it: change what you like and press START for another.'));
same('noticeKind of what is no text is a notice with no words', [null, undefined, 42, {}].map((t) => R.noticeKind(t)), [{ kind: 'notice', server: '' }, { kind: 'notice', server: '' }, { kind: 'notice', server: '' }, { kind: 'notice', server: '' }]);

// picture 15, strip by strip: [kind, what the page knows (the arguments), the words, the colour of the strip ('' green, 'warn' gold), the button ('' none)]
const ARGS_HOST = { guest: false, leaver: 'Leo', host: 'Sam' };
const BANNERS = [
    ['host', ARGS_HOST, 'Leo left, so you are the host now. The map, the colours and START are yours.', '', 'OK'],
    ['host', { guest: true, leaver: 'Juniper', host: 'Sam' }, 'Juniper left. Sam is the host now.', '', 'OK'],
    ['old', {}, 'That room is gone, because everybody left, so this one is yours now, with the same map. Send the link on if you like.', '', 'OK'],
    ['old', { sameMap: true }, 'That room is gone, because everybody left, so this one is yours now, with the same map. Send the link on if you like.', '', 'OK'],
    ['old', { sameMap: false }, 'That room is gone, because everybody left, so this one is yours now. Send the link on if you like.', '', 'OK'],
    ['over', { map: 'Treasure' }, 'Welcome back. Your match on Treasure is over. Everything is as you left it: change what you like and press START for another.', '', 'OK'],
    ['late', { server: PRIYA_LATE }, 'Priya\'s game did not come in time. Everybody is back in the room, as you left it. Press START to try again.', 'warn', 'OK'],
    ['late', { server: SAID.lates + 'Priya, Sam.' }, 'These games did not come in time: Priya, Sam. Everybody is back in the room, as you left it. Press START to try again.', 'warn', 'OK'],
    ['lateplayer', { host: 'Juniper' }, 'The match did not start, so everybody is back in the room. Juniper can press START to try again.', 'warn', 'OK'],
    ['noplace', { server: SAID.noplace }, 'The server has no place for another match right now: try again in a minute. Everybody is back in the room.', 'warn', 'OK'],
    ['startsfailed', { server: SAID.startsfailed }, 'The match could not start a few times in a row: try again in a minute. Everybody is back in the room.', 'warn', 'OK'],
    ['maplost', { server: SAID.maplost }, 'This map could not be loaded: choose another map.', 'warn', 'OK'],
    ['mapcolours', { server: SAID.mapcolours }, 'This map cannot be played with these colours.', 'warn', 'OK'],
    ['full', {}, 'That room is full, so this one is yours now. Send the link on if you like.', '', 'OK'],
    ['running', {}, 'The match in that room has already started, so this room is yours instead. Send the link on if you like.', '', 'OK'],
    ['removed', {}, 'The host removed you from the room, so this one is yours now. Send the link on if you like.', '', 'OK'],
    ['lost', {}, 'Connection lost. Getting you back in…', 'warn', ''],
    ['unreachable', {}, 'Cannot reach the game server. Check your connection; trying again…', 'warn', ''],
    ['busy', {}, 'The server is full right now, so there is no room for you yet. Try again in a minute.', 'warn', 'Try again'],
    ['away', {}, 'This room was opened in another window, so it is closed here.', 'warn', 'Use this window'],
    ['version', {}, 'A newer version of Ants is out. Reload the page to update.', 'warn', 'Reload'],
    ['notice', { server: 'Some other line of the room.' }, 'Some other line of the room.', 'warn', 'OK']
];
BANNERS.forEach(([kind, args, text, tone, btn]) => same('banner ' + kind + ' ' + JSON.stringify(args).slice(0, 40), R.banner(kind, args), { kind: kind, text: text, tone: tone, btn: btn }));
same('picture 15 lists seventeen messages: these are their kinds', BANNERS.map((b) => b[0]).filter((k, i, a) => a.indexOf(k) === i && k !== 'notice').sort(),
    ['away', 'busy', 'full', 'host', 'late', 'lateplayer', 'lost', 'mapcolours', 'maplost', 'noplace', 'old', 'over', 'removed', 'running', 'startsfailed', 'unreachable', 'version']);
same('green is the page changing something for you (the host, an old room, a match that is over, a full room, a running match, a removal); gold is something that did not work', [R.META.host, R.META.old, R.META.over, R.META.full, R.META.running, R.META.removed].map((m) => m.tone).concat([R.META.late, R.META.lost, R.META.busy, R.META.version, R.META.notice].map((m) => m.tone)), ['', '', '', '', '', '', 'warn', 'warn', 'warn', 'warn', 'warn']);
same('the kinds that have a strip: the seventeen of the picture and the line of the room that has no kind of its own', Object.keys(R.META).sort(),
    ['away', 'busy', 'full', 'host', 'late', 'lateplayer', 'lost', 'mapcolours', 'maplost', 'noplace', 'notice', 'old', 'over', 'removed', 'running', 'startsfailed', 'unreachable', 'version']);
same('every strip has its words; the one more is the line under the code field (no room with that code), which is no strip', Object.keys(R.TEXT).sort(), Object.keys(R.META).concat('noroom').sort());
same('every kind that noticeKind can say has a strip', ['late', 'noplace', 'startsfailed', 'maplost', 'mapcolours', 'notice'].map((k) => k in R.META), [true, true, true, true, true, true]);
same('... but "moved" is the one kind that says nothing: no strip and no words (the page drops it)', [('moved' in R.META), ('moved' in R.TEXT)], [false, false]);
same('a button that is not OK or none says what it does, in words (the page does it; here the words are read): Try again, Use this window, Reload', Object.keys(R.META).filter((k) => R.META[k].btn !== '' && R.META[k].btn !== 'OK').sort(), Object.keys(R.ACTS).sort());
same('what those buttons do', R.ACTS, { busy: 'This asks the server again.', away: 'This window takes the room back, and the other window closes.', version: 'This reloads the page.' });
same('the buttons are OK, Try again, Use this window, Reload or none', Object.keys(R.META).map((k) => R.META[k].btn).filter((b, i, a) => a.indexOf(b) === i).sort(), ['', 'OK', 'Reload', 'Try again', 'Use this window']);
same('a strip that has no button is one that goes by itself (the connection is back): lost and unreachable', Object.keys(R.META).filter((k) => R.META[k].btn === '').sort(), ['lost', 'unreachable']);
same('the line under the "Have a code?" field', [R.TEXT.noroom(), R.HINT.own, R.HINT.same, R.HINT.full, R.HINT.running], [
    'There is no room with that code. Check it, or ask the host to send the link again.', 'That is your own room.', 'You are in that room already.', 'That room is full.', 'The match in that room has already started.'
]);
same('banner of a kind that has no strip of its own (the code field\'s) is a gold strip with OK', [R.banner('noroom', {}).tone, R.banner('noroom', {}).btn], ['warn', 'OK']);
{
    const args = { server: 'A sentence of the server.', host: 'H', leaver: 'L', guest: false, map: 'M' };
    check('every banner has the four fields and words (nothing is undefined or empty)', Object.keys(R.META).every((k) => { const b = R.banner(k, args); return Object.keys(b).sort().join() === 'btn,kind,text,tone' && typeof b.text === 'string' && b.text.length > 10 && b.kind === k; }));
    check('the banners that need to know nothing say their words without arguments (the others, which name a person, a map or the server\'s sentence, are given them)', ['old', 'full', 'running', 'removed', 'lost', 'unreachable', 'busy', 'away', 'version'].every((k) => { const b = attempt(() => R.banner(k)); return !b.threw && typeof b.value.text === 'string' && b.value.text.length > 10 && !/undefined/.test(b.value.text); }));
    check('a banner about a person leaving names both: "L left" and the new host', R.banner('host', { guest: true, leaver: 'L', host: 'H' }).text === 'L left. H is the host now.');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 9. Random rooms: what must hold whatever the room is (a fixed seed, so the same rooms every time)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function prng(seed) { return () => { seed = (seed + 0x6D2B79F5) >>> 0; let t = seed; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
const rnd = prng(20251008);
const pick = (a) => a[Math.floor(rnd() * a.length)];
const NAMES_R = ['Juniper', 'Sam', 'Priya', 'Leo', 'Zed', 'a', '<b>', 'W'.repeat(32), 'Bot', 'x y', 'Maple'];
function randomRoom() {
    const kinds = ALL.map(() => pick(['person', 'person', 'open', 'bot', 'nobody']));
    if (kinds.indexOf('person') < 0) kinds[Math.floor(rnd() * 4)] = 'person';
    const persons = ALL.filter((s) => kinds[s] === 'person');
    const plan = kinds.map((k) => (k === 'bot' ? 1 + Math.floor(rnd() * 3) : k === 'nobody' ? 4 : 0));
    const seats = ALL.map((s) => (kinds[s] === 'person' ? P(NAMES_R[(s * 3 + Math.floor(rnd() * 11)) % 11]) : O));
    const leader = pick(persons);
    const play = ALL.filter((s) => kinds[s] === 'person' || kinds[s] === 'bot');
    let teamA = 255, teamB = 255;
    const r = rnd();
    if (r < 0.4 && play.length >= 2) { const a = pick(play); const b = pick(play.filter((s) => s !== a)); teamA = Math.min(a, b); teamB = Math.max(a, b); }
    else if (r < 0.5) { const a = Math.floor(rnd() * 3); teamA = a; teamB = a + 1 + Math.floor(rnd() * (3 - a)); }
    const starting = rnd() < 0.2;
    let inGame = 0;
    if (starting) persons.forEach((s) => { if (rnd() < 0.5) inGame |= 1 << s; });
    return mk(seats, { you: rnd() < 0.5 ? leader : pick(persons), leader: leader, plan: plan, teamA: teamA, teamB: teamB, starting: starting, inGame: inGame });
}
const props = {};
const tally = { rooms: 0, views: 0, pairViews: 0, hostViews: 0, accepted: 0, refused: 0, exchanges: 0, moves: 0, mends: 0, teamed: 0 };
const holds = (name, ok, detail) => { if (props[name] === undefined) props[name] = null; if (!ok && props[name] === null) props[name] = detail === undefined ? 'broken' : String(detail); };
const strings = new Set();
const collect = (x) => { if (typeof x === 'string') strings.add(x); else if (Array.isArray(x)) x.forEach(collect); else if (x && typeof x === 'object') Object.keys(x).forEach((k) => collect(x[k])); };
const KINDS_OF = (room) => ALL.map((s) => (room.slots[s].state === C ? 'person' : room.plan[s] >= 1 && room.plan[s] <= 3 ? 'bot' : room.plan[s] === 4 ? 'nobody' : 'open'));
const J = JSON.stringify;
const JC = (x) => JSON.stringify(canon(x));
for (let i = 0; i < 700; i++) {
    const room = randomRoom();
    const m = model(room);
    const kinds = KINDS_OF(room);
    tally.rooms++;
    if (room.teamA !== 255) tally.teamed++;
    const D = (extra) => 'room ' + J(room.slots.map((s) => s.name)) + ' plan ' + J(room.plan) + ' you ' + room.you + ' leader ' + room.leader + ' teams ' + room.teamA + ',' + room.teamB + (extra ? ' ' + extra : '');
    // the model
    holds('modelOf says what each colour is (a person, a computer player, nobody, open)', J(m.slots.map((s) => s.kind)) === J(kinds), D());
    holds('modelOf: the page is a guest exactly when it is not the leader, and the leader is the leader', m.guest === (room.you !== room.leader) && m.leader === room.leader && m.starting === room.starting, D());
    holds('modelOf: the switches are the room\'s pair or none', (room.teamA === 255 ? J(m.sides) === '[0,0,0,0]' : m.sides.filter((v) => v === 1).length === 2 && m.sides[room.teamA] === 1 && m.sides[room.teamB] === 1), D());
    holds('startsAlone is true exactly when one colour plays (a person or a computer player)', R.startsAlone(m) === (kinds.filter((k) => k === 'person' || k === 'bot').length === 1), D());
    holds('playing lists the persons and the computer players, in seat order', J(R.playing(m)) === J(ALL.filter((s) => kinds[s] === 'person' || kinds[s] === 'bot')), D());
    // the views
    for (const offline of [false, false, 'lost', 'none', 'join']) {
        const o = { sides: ALL.map(() => pick([0, 0, 1, 2])), picked: pick([-1, -1, 0, 1, 2, 3]), removing: pick([-1, 0, 1, 2, 3, 7]), offline: offline, refusal: rnd() < 0.2 ? 'A refusal.' : '' };
        const r = attempt(() => R.viewOf(m, o));
        holds('viewOf does not throw for any room and context', !r.threw, r.threw);
        if (r.threw) continue;
        const v = r.value;
        const ro = m.guest || m.starting || !!offline;
        const Dv = D('ctx ' + J(o));
        collect(v);
        tally.views++;
        if (!ro) tally.hostViews++;
        if (!ro && R.playing(m).length >= 3 && R.pairOf(v.sides, R.playing(m))) tally.pairViews++;
        holds('viewOf: four cards, in seat order, each with its colour', v.cards.length === 4 && v.cards.every((c, s) => c.seat === s && c.colour === R.COLOURS[s] && c.kind === m.slots[s].kind), Dv);
        holds('viewOf: read-only (a player, START, no link): no dots, no drop-down, no Remove, no question, no buttons', !ro || v.cards.every((c) => !c.grip && c.mode === '' && !c.remove && !c.ask && c.teams === null), Dv);
        holds('viewOf: the host of an open room can drag every card and set every colour that no person holds', ro || v.cards.every((c) => c.grip && (c.mode !== '') === (c.kind !== 'person')), Dv);
        holds('viewOf: the pencil is on the page\'s own name, while the room is open and the link is there', v.cards.every((c, s) => c.pencil === (m.slots[s].kind === 'person' && !!m.slots[s].me && !m.starting && !offline)), Dv);
        holds('viewOf: Remove is on persons that are neither the page nor the leader, never on a read-only screen; one question at a time, and it is the one that removing says', (() => {
            const ask = v.cards.filter((c) => c.ask).map((c) => c.seat);
            const okRemove = v.cards.every((c, s) => (c.remove || c.ask) === (!ro && m.slots[s].kind === 'person' && !m.slots[s].me && !m.slots[s].leader));
            return okRemove && ask.length <= 1 && v.removing === (ask.length ? ask[0] : -1) && v.cards.every((c) => !(c.remove && c.ask));
        })(), Dv);
        const play = R.playing(m), can = play.length >= 3;
        const shown = R.shownOf(v.sides, play);
        holds('viewOf: the switches that it shows are mended ones (a pair at most, never three on a button, none for a colour that does not play)', J(R.sidesFix(v.sides, play)) === J(v.sides) && v.sides.every((x, s) => x === 0 || play.indexOf(s) >= 0), Dv);
        holds('viewOf: the buttons are under the colours that play when three or four play, and only then', v.cards.every((c, s) => (c.teams !== null) === (!ro && can && play.indexOf(s) >= 0)), Dv);
        holds('viewOf: a lit button is the one that shownOf says (a pair makes the other two light the other button), never more than two on a button', v.cards.every((c, s) => c.teams === null || c.teams.every((t) => t.pressed === (shown[s] === t.side))) && [1, 2].every((k) => v.cards.filter((c) => c.teams && c.teams[k - 1].pressed).length <= 2), Dv);
        holds('viewOf: a dashed button is exactly one that a press would refuse (and a refusal says why)', v.cards.every((c, s) => c.teams === null || c.teams.every((t) => t.disabled === (R.pressSide(v.sides, play, s, t.side).refusal !== ''))), Dv);
        holds('viewOf: a read-only card shows its team as a tab, only when it has one', v.cards.every((c, s) => c.tab === (ro && can && play.indexOf(s) >= 0 && shown[s] ? 'Team ' + shown[s] : '')), Dv);
        holds('viewOf: a person\'s card shows the name as it is, every label starts with the colour', v.cards.every((c, s) => (m.slots[s].kind !== 'person' || c.who[0].text === m.slots[s].name) && c.label.indexOf(R.COLOURS[s].name + ': ') === 0), Dv);
        holds('viewOf: a card says what a tap does only while a colour is picked', v.cards.every((c) => (o.picked >= 0 ? c.drop.indexOf('Tap to put ') === 0 : c.drop === '')), Dv);
        holds('viewOf: the warning line is the refusal that the page has, in the warning colour', v.notes.teamsWarn === (o.refusal !== '') && (o.refusal === '' || v.notes.teams === o.refusal), Dv);
        holds('viewOf: START and the map are off for a player, while START waits and without a room; START waits ("Getting ready") only after START', v.side.startDisabled === ro && v.side.mapDisabled === ro && v.side.startBusy === m.starting && (v.side.startLabel === 'Getting ready…') === m.starting, Dv);
        holds('viewOf: the lines of the side are text, and there is one unless the page is joining', typeof v.side.plan === 'string' && typeof v.side.planShort === 'string' && (offline === 'join' || (v.side.plan !== '' && v.side.planShort !== '')), Dv);
        holds('viewOf: the heading is "Joining a room" while joining, the host\'s room for a player, else "Your room"', v.heading === (offline === 'join' ? 'Joining a room' : m.guest ? R.hostName(m) + '’s room' : 'Your room'), Dv);
    }
    // the presses
    for (let k = 0; k < 12; k++) {
        const play = ALL.filter(() => rnd() < 0.8);
        const sides = ALL.map(() => pick([0, 0, 1, 2, 1, 2, 3]));
        const seat = Math.floor(rnd() * 4), button = 1 + Math.floor(rnd() * 2);
        const fixed = R.sidesFix(sides, play), before = R.shownOf(fixed, play);
        const res = R.pressSide(sides, play, seat, button);
        const after = R.shownOf(res.sides, play);
        const Dp = 'sides ' + J(sides) + ' play ' + J(play) + ' press ' + seat + ',' + button + ' -> ' + J(res);
        const live = play.length >= 3 && play.indexOf(seat) >= 0;
        if (live) { if (res.refusal === '') tally.accepted++; else tally.refused++; }
        holds('pressSide: what comes back is a mended list, a list of its own, and never three lit on a button', J(R.sidesFix(res.sides, play)) === J(res.sides) && res.sides !== sides && [1, 2].every((b) => play.filter((s) => after[s] === b).length <= 2), Dp);
        holds('pressSide: a press that is accepted always changes what is lit', !live || res.refusal !== '' || J(after) !== J(before), Dp);
        holds('pressSide: a press that is refused changes nothing and says why in one of the two ways', !live || res.refusal === '' || (J(res.sides) === J(fixed) && (res.refusal === REFUSE_FULL(button) || res.refusal === REFUSE_OTHER(button))), Dp);
        holds('pressSide: a colour that does not play, or fewer than three that play, is no press: nothing changes, nothing is said', live || (J(res.sides) === J(fixed) && res.refusal === ''), Dp);
        holds('pressSide: a refusal is the press of a button that sideOpen says is closed, and only that', !live || (res.refusal !== '') === !R.sideOpen(fixed, play, seat, button), Dp);
        holds('teamBytes: a pair that the room is told is two colours that play, the lower first, or two 255', (() => { const t = R.teamBytes(sides, play); return (t.teamA === 255 && t.teamB === 255) || (t.teamA < t.teamB && t.teamB < 4 && play.indexOf(t.teamA) >= 0 && play.indexOf(t.teamB) >= 0 && play.length >= 3); })(), Dp);
        holds('teamsText: a line that names a pair names the colours of the pair, once', (() => { const t = R.teamsText(fixed, play, false); const pair = R.pairOf(fixed, play); return pair === null || (/^Teams: .* against .*\.$/.test(t) && play.every((s) => t.split(R.COLOURS[s].name).length === 2)); })(), Dp);
    }
    // the plans and the moves
    for (let k = 0; k < 6; k++) {
        const change = pick([undefined, {}, { map: pick(['tiny', 'small', 'medium', 'gauntlet', 'treasure', 'islands', 'nowhere']) }, { seat: Math.floor(rnd() * 4), kind: pick(R.KINDS) }, { sides: ALL.map(() => pick([0, 1, 2])) }, { seat: Math.floor(rnd() * 4), kind: pick(R.KINDS), sides: ALL.map(() => pick([0, 1, 1, 2])) }]);
        const got = R.planWith(room, m, change);
        const Dw = D('change ' + J(change) + ' -> ' + J(got));
        const playAfter = ALL.filter((s) => kinds[s] === 'person' || (got.kinds[s] >= 1 && got.kinds[s] <= 3));
        holds('planWith: four plan values, a map that is none or a file of the page, teams that the codec takes', got.kinds.length === 4 && got.kinds.every((x) => Number.isInteger(x) && x >= 0 && x <= 4) && (got.map === '' || R.MAPS.some((x) => x.file === got.map)) && N.validTeams(got.teamA, got.teamB) && attempt(() => N.encodePlan(got.map, got.kinds, got.teamA, got.teamB)).threw === undefined, Dw);
        holds('planWith: a team that goes out is two colours that play, with three or four playing', (got.teamA === 255 && got.teamB === 255) || (playAfter.length >= 3 && playAfter.indexOf(got.teamA) >= 0 && playAfter.indexOf(got.teamB) >= 0), Dw);
        holds('planWith: only the colour that was named changes its plan, and to the word that was said', J(got.kinds) === J(ALL.map((s) => (change && typeof change.seat === 'number' && change.seat === s ? R.KINDS.indexOf(change.kind) : room.plan[s]))), Dw);
        holds('planWith: without a map the room\'s own stays (an empty name)', (change && change.map && change.map !== 'nowhere') || got.map === '', Dw);
        const none = R.planWith(room, m, { sides: [0, 0, 0, 0] });
        holds('planWith: the plan that clears the teams has none', none.teamA === 255 && none.teamB === 255 && J(none.kinds) === J(room.plan), Dw);
    }
    {
        const same0 = R.planWith(room, m, {});
        if (R.teamNeedsFix(room, m)) tally.mends++;
        holds('teamNeedsFix is true exactly when the plan that the page would send as it is has other teams than the room\'s', R.teamNeedsFix(room, m) === (same0.teamA !== room.teamA || same0.teamB !== room.teamB), D('-> ' + J(same0)));
        holds('planWith with no change is the room\'s plan (the map stays the room\'s: no name)', J(same0.kinds) === J(room.plan) && same0.map === '', D());
    }
    for (let k = 0; k < 6; k++) {
        const from = Math.floor(rnd() * 4), to = Math.floor(rnd() * 4);
        const op = R.moveOf(room, m, from, to);
        const Dm = D('move ' + from + '->' + to + ' = ' + J(op));
        if (from === to) { holds('moveOf: a colour onto itself is nothing', op === null, Dm); continue; }
        const aPerson = kinds[from] === 'person', bPerson = kinds[to] === 'person';
        if (aPerson || bPerson) {
            tally.moves++;
            holds('moveOf: a move that holds a person is asked of the room, with the person as `from` (the colours of the two are the two that were named)', op !== null && op.op === 'move' && kinds[op.from] === 'person' && ((op.from === from && op.to === to) || (op.from === to && op.to === from && !aPerson)) && (!aPerson || op.from === from), Dm);
            continue;
        }
        tally.exchanges++;
        holds('moveOf: two colours that no person holds exchange their plans, and nothing else changes', op !== null && op.op === 'plan' && op.plan.map === '' && ALL.every((s) => op.plan.kinds[s] === (s === from ? room.plan[to] : s === to ? room.plan[from] : room.plan[s])) && op.plan.kinds !== room.plan, Dm);
        holds('moveOf: the team pair follows the colours (the same two plans, the lower colour first), none stays none', (() => {
            const moved = (s) => (s === from ? to : s === to ? from : s);
            if (room.teamA === 255) return op.plan.teamA === 255 && op.plan.teamB === 255;
            return op.plan.teamA === Math.min(moved(room.teamA), moved(room.teamB)) && op.plan.teamB === Math.max(moved(room.teamA), moved(room.teamB));
        })(), Dm);
        holds('moveOf: the codec takes the plan that goes out', op !== null && N.validTeams(op.plan.teamA, op.plan.teamB), Dm);
        const seatsOf = ALL.map((s) => (kinds[s] === 'person' ? P(room.slots[s].name) : O));
        const room2 = mk(seatsOf, { you: room.you, leader: room.leader, plan: op.plan.kinds, teamA: op.plan.teamA, teamB: op.plan.teamB, starting: room.starting, inGame: room.inGame });
        const back = R.moveOf(room2, model(room2), from, to);
        holds('moveOf: the same exchange twice is what it was before (plans and team)', JC(back.plan) === JC({ map: '', kinds: room.plan, teamA: room.teamA, teamB: room.teamB }), Dm);
        const flipped = R.moveOf(room, m, to, from);
        holds('moveOf: dragged the other way it is the same exchange', JC(flipped) === JC(op), Dm);
        holds('moveOf: a team that needed no mending needs none after the exchange, and the same the other way round', R.teamNeedsFix(room2, model(room2)) === R.teamNeedsFix(room, m), Dm);
    }
    {
        const local = ALL.map(() => pick([0, 0, 1, 2]));
        const got = R.reconcileSides(local, m);
        const play = R.playing(m);
        const server = R.sidesFix(m.sides, play);
        holds('reconcileSides: a mended list; the room\'s pair when it holds one; else no pair', J(R.sidesFix(got, play)) === J(got) && (R.pairOf(server, play) ? J(got) === J(server) : R.pairOf(got, play) === null), D('local ' + J(local) + ' -> ' + J(got)));
    }
}
Object.keys(props).forEach((name) => check('for 700 random rooms: ' + name, props[name] === null, props[name]));
check('the random rooms reached every rule (screens of a host, a pair on a host\'s screen, presses taken and refused, moves, exchanges, teams to mend)', tally.rooms === 700 && tally.views >= 3000 && tally.hostViews >= 800 && tally.pairViews >= 100 && tally.accepted >= 1000 && tally.refused >= 300 && tally.exchanges >= 300 && tally.moves >= 800 && tally.mends >= 20 && tally.teamed >= 250, J(tally));
check('the random runs held ' + Object.keys(props).length + ' rules at 700 rooms (a rule that never ran is a failure)', Object.keys(props).length >= 30 && Object.keys(props).every((k) => props[k] === null || typeof props[k] === 'string'), String(Object.keys(props).length));
check('the random rooms were many and of every kind (the generator is not stuck)', strings.size > 150, String(strings.size));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 10. The words of the screen: "player", never "friend" (the owner's rule), no stray blanks in the fixed lines
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const fixed = new Set();
    const keep = (x) => { if (typeof x === 'string') fixed.add(x); else if (Array.isArray(x)) x.forEach(keep); else if (x && typeof x === 'object') Object.keys(x).forEach((k) => keep(x[k])); };
    [S1, S2, S5, S14, S13, S13b, S4, S12].forEach((room) => [{}, { picked: 1 }, { removing: 1 }, { offline: 'lost' }, { offline: 'none' }, { offline: 'join' }, { refusal: 'A refusal.' }].forEach((o) => keep(R.viewOf(model(room), ctx(o)))));
    BANNERS.forEach((b) => keep(R.banner(b[0], b[1])));
    keep([R.TEXT.noroom(), R.HINT, R.ACTS, R.MAPS.map((x) => x.info), R.NAMES, R.nameCheck('é').why, R.nameCheck('a'.repeat(40)).why, R.nameCheck('Bot (x)').why, R.typedCode('').why, R.typedCode('!').why, R.teamsText([0, 0, 0, 0], [0, 1], false), R.teamsText([0, 0, 0, 0], ALL, false)]);
    Object.keys(R.TEXT).forEach((k) => keep(R.TEXT[k]({ server: 'S.', host: 'H', leaver: 'L', guest: true, map: 'M' })));
    const all = Array.from(fixed).concat(Array.from(strings));
    check('no line of the screen says "friend" (the owner\'s rule: player)', all.every((s) => !/friend/i.test(s)), all.filter((s) => /friend/i.test(s)).join(' | '));
    check('the fixed lines (the pictures\' words) have no stray blank: none at the ends or doubled', Array.from(fixed).filter((s) => s !== '' && (/^\s|\s$|\s\s/.test(s))).length === 0, Array.from(fixed).filter((s) => /^\s|\s$|\s\s/.test(s)).join('|'));
    check('the fixed lines are plain text: no markup in them', Array.from(fixed).every((s) => !/[<>]/.test(s)));
    check('the words use the characters of the pictures only: ASCII and the ellipsis, the apostrophe of a possessive and the middle dot', Array.from(fixed).every((s) => /^[\x20-\x7e…’·]*$/.test(s)), Array.from(fixed).filter((s) => !/^[\x20-\x7e…’·]*$/.test(s)).join('|'));
}

// ---- END OF SECTIONS ----
for (const name of Object.keys(RAW)) if (typeof RAW[name] === 'function') check('the check called ' + name + '()', called[name] > 0);
console.log('LobbyRules: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
