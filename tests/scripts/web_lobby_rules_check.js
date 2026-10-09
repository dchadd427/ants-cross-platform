// Runs the rules and the words of the lobby page (web/front/lobby_rules.js) without a browser, on tables: what a Room message of network protocol 16 shows on each of the four colour cards and on the
// map's side (the owner's pictures 1, 2, 3, 4, 5, 5b, 6, 12, 13, 13b, 14, 15b, 16 and 17, which the words below are compared with), who may press which of the Team 1 / Team 2 buttons and what is said when
// a press is refused, what the leader asks of the room (the plan, a colour moved or exchanged), the player's name (the same rules as the game page's own NAME block, which is read from web/shell.html and
// web/lobby.html and must be the same text in both), the room codes (the alphabet, the two groups of three, what a typed or pasted code may be), the maps (against the level files of Original-Ants/Maps),
// the sentences that the server says (noticeKind) and every message strip of picture 15 (text, tone and button).
//   - the Room messages are built as bytes and read by web/front/lobby_net.js (the codec of the same protocol), so a table row is a message that a server can send;
//   - every exported function of lobby_rules.js must be called by this check (a function that no table reaches is a failure);
//   - the random runs use a fixed seed: the same rooms every time.
// usage: node web_lobby_rules_check.js web/front/lobby_rules.js web/lobby.html web/shell.html [Original-Ants/Maps]     (exit 0: every check holds; every failure is printed; the folder of the level
// files is found next to web/ when it is not given)
'use strict';
const fs = require('fs');
const path = require('path');

const rulesPath = process.argv[2];
const lobbyPath = process.argv[3];
const shellPath = process.argv[4];
if (!rulesPath || !lobbyPath || !shellPath) { console.log('usage: web_lobby_rules_check.js lobby_rules.js lobby.html shell.html [Original-Ants/Maps]'); process.exit(2); }
const mapsDir = process.argv[5] || path.join(path.dirname(path.resolve(lobbyPath)), '..', 'Original-Ants', 'Maps');
const frontDir = path.join(path.dirname(path.resolve(lobbyPath)), 'front');

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

// ---- END OF SECTIONS ----
for (const name of Object.keys(RAW)) if (typeof RAW[name] === 'function') check('the check called ' + name + '()', called[name] > 0);
console.log('LobbyRules: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
