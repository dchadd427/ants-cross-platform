// Runs the page's OWN code for the names of the computer players (web/front/bot_names.js) against the tables that the C++ code (include/ants_ai/bot_names.hpp,
// tests/test_ai/test_ai_names.cpp) is held to as well: the golden picks and the two digests of 10,000 keys (tests/data/bot_names_golden.txt), the cases of the label, its parser,
// the short form and the reserved-name rule (tests/data/bot_label_cases.tsv), and the list (shape, blocklist, the twelve names that the page offers a visitor).
// tests/scripts/test_bot_names.py runs this with node (the quick tier) and changes one thing at a time in a COPY of the library: the check must then fail.
// usage: node web_bot_names_check.js web/front/bot_names.js REPO_ROOT [--print-golden]          (exit 0: every check holds; FAIL lines say what does not)
'use strict';
const fs = require('fs');
const path = require('path');
const libPath = process.argv[2];
const root = process.argv[3];
const B = require(path.resolve(libPath));
const read = (...p) => fs.readFileSync(path.join(root, ...p), 'latin1');
let failures = 0, checks = 0;
function check(name, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + name + (detail === undefined ? '' : ' -- ' + detail)); }
}
const same = (name, got, want) => check(name, JSON.stringify(got) === JSON.stringify(want), 'got ' + JSON.stringify(got) + ' want ' + JSON.stringify(want));

// ---- the digests (the same in tests/test_ai/test_ai_names.cpp) -------------------------------------------------------------------------------------------------------------------------
function Fnv(basis) { this.h = basis >>> 0; }
Fnv.prototype.add = function (text) { for (let i = 0; i < text.length; i++) { this.h = Math.imul(this.h ^ (text.charCodeAt(i) & 255), 16777619) >>> 0; } };
const hex8 = (n) => ('00000000' + (n >>> 0).toString(16)).slice(-8);
const hex16 = (n) => ('0000000000000000' + n.toString(16)).slice(-16);
function picksDigest() {
    const a = new Fnv(0x811C9DC5), b = new Fnv(0xC0FFEE11);
    for (let i = 0; i < 10000; i++) {
        const key = i % 2 === 0 ? hex16(i) : B.mix64Hex(hex16(i));
        const line = B.pickBotNames(key, i % 5).join(',') + ';';
        a.add(line); b.add(line);
    }
    return hex8(a.h) + ' ' + hex8(b.h);
}
const TOKENS = ['Bot', 'bot', ' ', '(', ')', ',', ', ', 'Hard', 'hard', 'Easy', 'Medium', 'idle', 'Idle', 'worker', 'Worker', 'Pip', 'x', 'BOT', '(bot', '(bot)', '(bot, ', ' (bot, Hard)', 'Bot (', 'bot,', 'Seventeen letters'];
function fuzzDigest() {
    const a = new Fnv(0x811C9DC5), b = new Fnv(0xC0FFEE11);
    let s = 12345;
    const next = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s >>> 16; };
    for (let i = 0; i < 10000; i++) {
        let text = '';
        const n = 1 + (next() % 6);
        for (let k = 0; k < n; k++) text += TOKENS[next() % TOKENS.length];
        const p = B.parseBotLabel(text);
        const line = (p ? p.name + '|' + p.tag + '|' + (p.legacy ? 1 : 0) : 'FAIL') + '\t' + B.botShortLabel(text) + '\t' + (B.isReservedBotName(text) ? 1 : 0) + '\n';
        a.add(line); b.add(line);
    }
    return hex8(a.h) + ' ' + hex8(b.h);
}
const GOLDEN_KEYS = ['0000000000000000', '0000000000000001', '0000000000000002', 'deadbeefcafef00d', 'ffffffffffffffff', '8000000000000000', '00000000ffffffff', '0000000100000000', '0123456789abcdef', '000000000000002a'];
if (process.argv.includes('--print-golden')) {
    console.log('digest picks 10000 ' + picksDigest());
    console.log('digest fuzz 10000 ' + fuzzDigest());
    GOLDEN_KEYS.forEach((k, i) => { const c = 2 + (i % 3); console.log('pick ' + k + ' ' + c + ' ' + B.pickBotNames(k, c).join(',')); });
    process.exit(0);
}

// ---- the golden file ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const golden = read('tests', 'data', 'bot_names_golden.txt').split('\n').filter((l) => l !== '' && l[0] !== '#');
const digests = {};
golden.forEach((line) => {
    const f = line.split(' ');
    if (f[0] === 'digest') digests[f[1]] = f.slice(3).join(' ');
    else if (f[0] === 'pick') same('golden pick key ' + f[1] + ' count ' + f[2], B.pickBotNames(f[1], Number(f[2])), f[3].split(','));
});
same('the picks of 10,000 keys have the digest of the golden file (and of the C++ code)', picksDigest(), digests.picks);
same('the labels, parses, short forms and reserved answers for 10,000 strings have the digest of the golden file (and of the C++ code)', fuzzDigest(), digests.fuzz);
check('the golden file has ten vectors', golden.filter((l) => l.indexOf('pick ') === 0).length === GOLDEN_KEYS.length);

// ---- the list ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const N = B.NAMES;
check('240 to 260 names', N.length >= 240 && N.length <= 260 && B.count() === N.length, N.length);
check('every name is a capital letter and 2 to 8 small ASCII letters (3 to 9 letters)', N.every((n) => /^[A-Z][a-z]{2,8}$/.test(n)));
check('no two names are the same in any case', new Set(N.map((n) => n.toLowerCase())).size === N.length);
const placeholders = /var NAMES = \[([^\]]*)\]/.exec(read('web', 'front', 'lobby_rules.js'));
const web = placeholders ? placeholders[1].split(',').map((s) => s.trim().replace(/'/g, '')) : [];
check('the page offers twelve names to a visitor who typed none (lobby_rules.js NAMES)', web.length === 12 && web.every((n) => /^[A-Z][a-z]+$/.test(n)), web.join());
check('no bot name is one of those twelve', N.every((n) => web.indexOf(n) < 0), N.filter((n) => web.indexOf(n) >= 0).join());
const block = read('tests', 'data', 'bot_names_blocklist.txt').split('\n').map((l) => l.trim()).filter((l) => l !== '' && l[0] !== '#');
check('the blocklist is a real list (at least 100 words, whole-name and inside)', block.length >= 100 && block.some((w) => w[0] === '=') && block.some((w) => w[0] !== '='), block.length);
const hits = [];
N.forEach((n) => block.forEach((w) => { const l = n.toLowerCase(); if (w[0] === '=' ? l === w.slice(1) : l.indexOf(w) >= 0) hits.push(n + ' ~ ' + w); }));
check('no name is, or holds, a word of the blocklist', hits.length === 0, hits.join('; '));
check('every name makes a label that fits a player name (32 characters) and the HUD short form fits 15', N.every((n) => B.botLabel(n, 'medium').length <= 32 && B.botShortLabel(B.botLabel(n, 'hard')).length <= 15));

// ---- the picker -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    let distinct = true, prefix = true, firstSeen = {}, deterministic = true;
    for (let i = 0; i < 2000; i++) {
        const key = hex16(i * 7919);
        const four = B.pickBotNames(key, 4);
        if (new Set(four).size !== 4 || !four.every((n) => N.indexOf(n) >= 0)) distinct = false;
        if (JSON.stringify(B.pickBotNames(key, 2)) !== JSON.stringify(four.slice(0, 2)) || JSON.stringify(B.pickBotNames(key, 3)) !== JSON.stringify(four.slice(0, 3))) prefix = false;
        if (JSON.stringify(B.pickBotNames(key, 4)) !== JSON.stringify(four)) deterministic = false;
        firstSeen[four[0]] = (firstSeen[four[0]] || 0) + 1;
    }
    check('four names for a key are four different names of the list', distinct);
    check('pick(key, 2) and pick(key, 3) are the first of pick(key, 4)', prefix);
    check('the same key gives the same names', deterministic);
    const counts = N.map((n) => firstSeen[n] || 0);
    check('2,000 keys reach most of the list as the first name, none of them is far above the average of 8', counts.filter((c) => c > 0).length >= N.length * 0.9 && Math.max.apply(null, counts) <= 30, Math.max.apply(null, counts) + '/' + counts.filter((c) => c > 0).length);
    const all = B.pickBotNames('2a', N.length + 50);
    check('asking for more than the list holds gives the whole list once each, in an order of the key', all.length === N.length && new Set(all).size === N.length);
    same('count 0, a negative count and a count that is no number give nothing', [B.pickBotNames('1', 0), B.pickBotNames('1', -3), B.pickBotNames('1', NaN), B.pickBotNames('1', 'x')], [[], [], [], []]);
    same('a key can be given as a string, with 0x, in capitals, as a number or as [high, low]', [B.pickBotNames('0xDEADBEEFCAFEF00D', 2), B.pickBotNames([0xDEADBEEF, 0xCAFEF00D], 2), B.pickBotNames(255, 2)],
        [B.pickBotNames('deadbeefcafef00d', 2), B.pickBotNames('deadbeefcafef00d', 2), B.pickBotNames('00000000000000ff', 2)]);
    same('a key that is none of these counts as 0', [B.pickBotNames('xyz', 3), B.pickBotNames(null, 3), B.pickBotNames(-1, 3), B.pickBotNames(1.5, 3), B.pickBotNames('12345678901234567', 3)], [0, 0, 0, 0, 0].map(() => B.pickBotNames('0', 3)));
    same('mix64 of 0 is 0 and of 1 is the finalizer of splitmix64', [B.mix64Hex('0'), B.mix64Hex('1')], ['0000000000000000', '5692161d100b05e5']);
}

// ---- the label cases (the same table as the C++ test) -----------------------------------------------------------------------------------------------------------------------------------
{
    const un = (t) => (t === '<empty>' ? '' : t.replace(/\\x([0-9a-f]{2})/g, (m, h) => String.fromCharCode(parseInt(h, 16))));
    const seen = { label: 0, parse: 0, short: 0, reserved: 0 };
    read('tests', 'data', 'bot_label_cases.tsv').split('\n').filter((l) => l !== '' && l[0] !== '#').forEach((line) => {
        const f = line.split('\t').map(un);
        seen[f[0]]++;
        if (f[0] === 'label') same('label ' + JSON.stringify(f.slice(1, 3)), B.botLabel(f[1], f[2]), f[3]);
        else if (f[0] === 'parse') {
            const p = B.parseBotLabel(f[1]);
            same('parse ' + JSON.stringify(f[1]), p ? p.name + '|' + p.tag + '|' + (p.legacy ? 1 : 0) : 'FAIL', f[2]);
        } else if (f[0] === 'short') same('short ' + JSON.stringify(f[1]), B.botShortLabel(f[1]), f[2]);
        else if (f[0] === 'reserved') same('reserved ' + JSON.stringify(f[1]), B.isReservedBotName(f[1]) ? '1' : '0', f[2]);
        else check('a known kind of case', false, line);
    });
    check('the table has cases of all four kinds', seen.label >= 20 && seen.parse >= 40 && seen.short >= 8 && seen.reserved >= 40, JSON.stringify(seen));
    // every name, every tag: the label is read back, and is a reserved name (a person cannot take it)
    let roundtrip = true, reserved = true;
    N.forEach((n) => ['easy', 'medium', 'hard', 'idle', 'worker'].forEach((t) => {
        const label = B.botLabel(n, t), p = B.parseBotLabel(label);
        if (!p || p.name !== n || p.tag !== t || p.legacy) roundtrip = false;
        if (!B.isReservedBotName(label) || !B.isReservedBotName(B.botShortLabel(label))) reserved = false;
    }));
    check('every name with every tag: botLabel is read back by parseBotLabel', roundtrip);
    check('every name with every tag: the label and its short form are reserved names', reserved);
    check('no name of the list is a reserved name by itself', N.every((n) => !B.isReservedBotName(n)));
    check('the five legacy labels are reserved', ['Bot (Easy)', 'Bot (Medium)', 'Bot (Hard)', 'Bot (Idle)', 'Bot (Worker)'].every((l) => B.isReservedBotName(l)));
}

console.log((failures === 0 ? 'ok: ' : 'FAILED: ') + checks + ' checks, ' + failures + ' failures');
process.exit(failures === 0 ? 0 : 1);
