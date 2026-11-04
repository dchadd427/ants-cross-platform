// Runs the code that opens a replay FILE of the visitor's own (web/replay_page.js: "a replay file of the visitor's own", docs/REPLAYS.md "A file of your own") on tables and on fakes:
//   - the cheap questions that are asked before the game is loaded for a file: its size (from File.size, before one byte is read, and again on the bytes), the 8 bytes that open a replay, a HEAD chunk
//     that follows them at once, not longer than 4096 bytes, with the right checksum: on the real file of tests/data/, on every cut of its head, on every flipped byte of its head, at the size limit,
//     with the line ends changed, and with heads that claim what they cannot hold;
//   - the name of a file as the page shows it (text, nothing that hides or reorders the words around it) and the size in words.
// tests/scripts/test_web_replay_open.py runs this with node (the quick tier), on the real script and on broken copies of it.
// usage: node web_replay_open_check.js web/replay_page.js tests/data/replay_bots_tiny.antsrep    (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
const scriptPath = process.argv[2], fixturePath = process.argv[3];
if (!scriptPath || !fixturePath) { console.log('usage: web_replay_open_check.js replay_page.js fixture.antsrep'); process.exit(2); }
const R = require(path.resolve(scriptPath));
const FIXTURE = new Uint8Array(fs.readFileSync(path.resolve(fixturePath)));

let checks = 0, failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function same(label, got, want) {
    const a = JSON.stringify(got), b = JSON.stringify(want);
    check(label, a === b, 'got ' + a + ', wanted ' + b);
}

// ---- a CRC-32 and a chunk writer of the check's own (the page's crc32 is the thing under test: this one is the plain bit by bit definition)
function crcOf(bytes, seed) {
    let c = ~(seed || 0) >>> 0;
    for (const b of bytes) {
        c = (c ^ b) >>> 0;
        for (let k = 0; k < 8; k++) c = (c & 1 ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1)) >>> 0;
    }
    return ~c >>> 0;
}
const le32 = (n) => [n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255];
const MAGIC = [0x89, 0x41, 0x52, 0x50, 0x4c, 0x0d, 0x0a, 0x1a];
function chunk(tag, payload, o) {                       // o.length: the length field that is written; o.crc: the checksum that is written (else the right one)
    const t = Array.from(tag, (ch) => ch.charCodeAt(0));
    const crc = o && o.crc !== undefined ? o.crc : crcOf(Uint8Array.from(payload), crcOf(Uint8Array.from(t)));
    return t.concat(le32(o && o.length !== undefined ? o.length : payload.length), Array.from(payload), le32(crc));
}
const file = (...parts) => Uint8Array.from([].concat(MAGIC, ...parts.map((p) => Array.from(p))));
const zeros = (n) => new Array(n).fill(0);
const verdict = (bytes) => R.localCheck(bytes);
const accepted = (bytes) => verdict(bytes).ok === true;
const kindOf = (bytes) => { const v = verdict(bytes); return v.ok ? 'ok' : v.kind; };

// ---- the real file: a match on Tiny between four computer players (tests/data/replay_bots_tiny.antsrep, made by bot_arena --save-replays)
const headLength = FIXTURE[12] | (FIXTURE[13] << 8) | (FIXTURE[14] << 16) | (FIXTURE[15] << 24);
const headEnd = 8 + 8 + headLength + 4;               // the end of the HEAD chunk: after its checksum
same('the real file is a replay', verdict(FIXTURE), { ok: true });
check('the real file has a head of a plain size and is longer than it', headLength > 100 && headLength < R.HEAD_MAX_BYTES && FIXTURE.length > headEnd + 200, 'head ' + headLength + ', file ' + FIXTURE.length);
check('the limit of a file is the game\'s kMaxFileBytesWithMap, 2 MiB and the frame of a chunk', R.LOCAL_MAX_BYTES === 2 * 1048576 + 12 && R.LOCAL_MAX_BYTES === 2097164, R.LOCAL_MAX_BYTES);
check('the limit of a head is the game\'s kMaxHeadBytes', R.HEAD_MAX_BYTES === 4096);
same('the page\'s checksum is the definition\'s (the vector of the standard, and the head of the file)', [R.crc32(Uint8Array.from('123456789', (ch) => ch.charCodeAt(0)), 0, 9, 0), R.crc32(FIXTURE, 16, 16 + headLength, R.crc32(FIXTURE, 8, 12, 0))],
     [0xcbf43926, (FIXTURE[8 + 8 + headLength] | (FIXTURE[9 + 8 + headLength] << 8) | (FIXTURE[10 + 8 + headLength] << 16) | (FIXTURE[11 + 8 + headLength] << 24)) >>> 0]);
same('a checksum goes on from the one before it', R.crc32(Uint8Array.from([1, 2, 3, 4]), 2, 4, R.crc32(Uint8Array.from([1, 2, 3, 4]), 0, 2, 0)), crcOf(Uint8Array.from([1, 2, 3, 4])));

// ---- the size, asked from File.size before a byte is read
for (const [size, want] of [[0, false], [1, true], [3180, true], [1048576, true], [2097163, true], [2097164, true], [2097165, false], [14900000, false], [-1, false], [NaN, false], [Infinity, false], ['3000', false], [null, false], [undefined, false]]) {
    same('a file of ' + String(size) + ' bytes may be a replay: ' + want, R.localSizeOk(size), want);
}

// ---- every cut of the head: a copy that stopped inside the head is no replay, one that stopped after it plays as far as it goes
for (let n = 0; n < headEnd; n++) {
    if (kindOf(FIXTURE.slice(0, n)) !== 'bad') { check('the file cut at ' + n + ' bytes, inside its head, is damaged', false, kindOf(FIXTURE.slice(0, n))); break; }
}
check('every cut inside the head (' + headEnd + ' of them) is damaged', true);
for (const n of [headEnd, headEnd + 1, headEnd + 11, headEnd + 12, headEnd + 100, FIXTURE.length - 1, FIXTURE.length]) {
    same('the file cut at ' + n + ' bytes, after its head, still passes (the game plays it to the cut)', kindOf(FIXTURE.slice(0, n)), 'ok');
}

// ---- every byte of the head that changes breaks the checksum, the length, the tag or the magic
{
    let missed = [];
    for (let i = 0; i < headEnd; i++) {
        for (const bit of [1, 128]) {
            const c = FIXTURE.slice(); c[i] ^= bit;
            if (accepted(c)) missed.push(i + '/' + bit);
        }
    }
    same('no flipped bit in the magic, the tag, the length, the head or its checksum is accepted', missed, []);
    const c = FIXTURE.slice(); c[headEnd + 5] ^= 255;
    same('a flipped byte AFTER the head is not this check\'s business (the game reads every chunk)', kindOf(c), 'ok');
}

// ---- the size limit, on the bytes too (the second line: whatever the caller did before)
function padded(n) { const out = new Uint8Array(n); out.set(FIXTURE.slice(0, Math.min(n, FIXTURE.length))); return out; }
same('a replay of exactly the longest size passes', kindOf(padded(R.LOCAL_MAX_BYTES)), 'ok');
same('one byte more is too big', kindOf(padded(R.LOCAL_MAX_BYTES + 1)), 'big');
same('a file of 14 MB is too big', kindOf(new Uint8Array(14 * 1048576)), 'big');
same('too big wins over every other fault (no magic at all)', kindOf(new Uint8Array(R.LOCAL_MAX_BYTES + 1)), 'big');
same('the verdict of too big has no reason text to show', Object.keys(verdict(padded(R.LOCAL_MAX_BYTES + 1))).sort(), ['kind', 'ok']);

// ---- what is no replay
const text = (s) => Uint8Array.from(s, (ch) => ch.charCodeAt(0));
for (const [label, bytes] of [
    ['nothing', new Uint8Array(0)], ['one byte', Uint8Array.from([0x89])], ['the magic alone', Uint8Array.from(MAGIC)], ['the magic and 11 bytes', file(zeros(11)).slice(0, 19)],
    ['a text', text('hello, this is a text file and no replay at all, though it is long enough to hold a head if it were one')], ['zeros', new Uint8Array(500)],
    ['a PNG', Uint8Array.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a].concat(zeros(40)))], ['a zip', text('PK\u0003\u0004' + 'x'.repeat(60))],
    ['the magic with the line ends changed (a text-mode transfer: 0D 0A became 0A)', Uint8Array.from(Array.from(FIXTURE).filter((b, i) => !(i === 5))).slice(0, 400)],
    ['the magic of a lower case replay (89 61 ...)', Uint8Array.from([0x89, 0x61, 0x72, 0x70, 0x6c, 0x0d, 0x0a, 0x1a].concat(Array.from(FIXTURE.slice(8))))],
    ['the head before the magic', Uint8Array.from(Array.from(FIXTURE.slice(8)).concat(MAGIC))], ['the magic twice', Uint8Array.from(MAGIC.concat(Array.from(FIXTURE)))],
]) {
    same('no replay: ' + label, kindOf(bytes), 'bad');
}
same('the magic of a PDF-sized hostile file with a head tag that is not HEAD', kindOf(file(chunk('CMDS', zeros(40)))), 'bad');
same('a lower case head is not a head', kindOf(file(chunk('head', zeros(40)))), 'bad');
same('a head with a space in the tag is not one', kindOf(file(chunk('HEA ', zeros(40)))), 'bad');
same('the first chunk is another one and HEAD comes second', kindOf(file(chunk('tmap', zeros(5)), chunk('HEAD', zeros(40)))), 'bad');

// ---- heads that claim what they cannot hold (every one with the checksum that fits what it says, so that only the claim is wrong)
same('a head of the longest allowed size, whole, passes', kindOf(file(chunk('HEAD', zeros(R.HEAD_MAX_BYTES)))), 'ok');
same('a head of one byte more than allowed is refused even with the right checksum', kindOf(file(chunk('HEAD', zeros(R.HEAD_MAX_BYTES + 1)))), 'bad');
same('... and the reason is its length', verdict(file(chunk('HEAD', zeros(R.HEAD_MAX_BYTES + 1)))).why, 'head length');
same('a head that claims 4 GB', kindOf(file(chunk('HEAD', zeros(40), { length: 0xffffffff }))), 'bad');
same('a head that claims 2^31', kindOf(file(chunk('HEAD', zeros(40), { length: 0x80000000 }))), 'bad');
same('a head that claims more than the file holds', kindOf(file(chunk('HEAD', zeros(40), { length: 100 }))), 'bad');
same('a head that claims less than it holds (the checksum is then of the wrong bytes)', kindOf(file(chunk('HEAD', zeros(40), { length: 30 }))), 'bad');
same('a head whose checksum is 0', kindOf(file(chunk('HEAD', zeros(40), { crc: 0 }))), 'bad');
same('a head whose checksum is the checksum of the payload alone (the tag is part of it)', kindOf(file(chunk('HEAD', zeros(40), { crc: crcOf(Uint8Array.from(zeros(40))) }))), 'bad');
same('a head whose checksum counts the length field too', kindOf(file(chunk('HEAD', zeros(40), { crc: crcOf(Uint8Array.from(le32(40).concat(zeros(40))), crcOf(Uint8Array.from([0x48, 0x45, 0x41, 0x44]))) }))), 'bad');
same('an empty head with the right checksum passes this gate (its fields are the game\'s to judge)', kindOf(file(chunk('HEAD', []))), 'ok');
same('a head of 40 zeros with the right checksum passes this gate too', kindOf(file(chunk('HEAD', zeros(40)))), 'ok');
same('a head that ends exactly at the end of the file (nothing after it)', kindOf(file(chunk('HEAD', zeros(40)))), 'ok');
same('a head that is one byte short of its checksum', kindOf(file(chunk('HEAD', zeros(40))).slice(0, -1)), 'bad');

// ---- what is not bytes at all
for (const [label, v] of [['null', null], ['undefined', undefined], ['a string', 'ants'], ['a number', 42], ['an object', {}], ['an object with a huge length', { length: 1e12 }]]) {
    same('not bytes: ' + label, R.localCheck(v).ok, false);
}
same('a plain array of the numbers of a replay is read as bytes', R.localCheck(Array.from(FIXTURE)).ok, true);

// ---- the name of the file, as text
const bidi = String.fromCharCode(0x202e), zw = String.fromCharCode(0x200b), bom = String.fromCharCode(0xfeff), ls = String.fromCharCode(0x2028), iso = String.fromCharCode(0x2066);
same('a plain name', R.ownName('ants-TREASURE-20261006-143209.antsrep'), 'ants-TREASURE-20261006-143209.antsrep');
same('a name with a space and a bracket', R.ownName('my game (2).antsrep'), 'my game (2).antsrep');
same('the right-to-left override is taken out', R.ownName('ants-' + bidi + 'perstna.antsrep'), 'ants-perstna.antsrep');
same('zero-width marks, the byte order mark, a line separator and an isolate are taken out', R.ownName(zw + 'a' + bom + 'b' + ls + 'c' + iso + 'd'), 'abcd');
same('controls and a newline are taken out', R.ownName('a\u0000b\nc\td\u007fe'), 'abcde');
same('the ends are trimmed', R.ownName('   x.antsrep  '), 'x.antsrep');
same('a name of 100 characters stays', R.ownName('a'.repeat(100)), 'a'.repeat(100));
same('a name of 101 characters is cut to 100 with a mark', R.ownName('a'.repeat(101)), 'a'.repeat(99) + String.fromCharCode(0x2026));
same('letters of other scripts stay', R.ownName('r' + String.fromCharCode(0x00e9) + 'play ' + String.fromCharCode(0x65e5) + '.antsrep'), 'r' + String.fromCharCode(0x00e9) + 'play ' + String.fromCharCode(0x65e5) + '.antsrep');
same('a name that is only controls is nothing', R.ownName(bidi + zw), '');
same('not a string is nothing', [R.ownName(null), R.ownName(undefined), R.ownName(7), R.ownName({})], ['', '', '', '']);
same('markup is just letters here (it is only ever put in as text)', R.ownName('<img src=x onerror=alert(1)>.antsrep'), '<img src=x onerror=alert(1)>.antsrep');

// ---- the size in words
for (const [n, want] of [[0, '1 KB'], [100, '1 KB'], [1024, '1 KB'], [3180, '3 KB'], [524288, '512 KB'], [1048575, '1024 KB'], [1048576, '1.0 MB'], [2097165, '2.0 MB'], [14900000, '14.2 MB'], [1073741823, '1024.0 MB'], [1073741824, '1.0 GB'], [-5, '1 KB'], [NaN, '1 KB'], ['x', '1 KB']]) {
    same('the size of ' + n, R.sizeText(n), want);
}

console.log('web_replay_open_check: ' + checks + ' checks, ' + failures + ' failures');
process.exit(failures ? 1 : 0);
