// Runs the page's twin of the table of a name's letters (web/front/name_letters.js) without a browser: usage: node web_name_letters_check.js RANGES_JSON, where RANGES_JSON is the table of hub/name_letters.py as
// [[first, last, alphabet], ...] (tests/scripts/test_name_letters.py hands it over). Every code point up to U+1FFFF is asked and must be answered as the table says; UTF-8 is the strict kind (no overlong form, no
// surrogate, nothing cut, nothing past U+10FFFF) and a name is the printable ASCII and the letters, at most 32 characters and 96 bytes. The C++ decoders answer the same cases (tests/test_net/test_lobby_names.cpp N4.70).
'use strict';
const assert = require('assert');
const path = require('path');
const L = require(path.join(__dirname, '..', '..', 'web', 'front', 'name_letters.js'));

const table = JSON.parse(process.argv[2] || '[]');
assert(table.length > 0, 'no table was given');
const expected = (cp) => { for (const [a, b, kind] of table) { if (cp >= a && cp <= b) return kind; } return -1; };
for (let cp = 0; cp <= 0x1ffff; cp++) {
    assert.strictEqual(L.alphabetOf(cp), expected(cp), 'alphabetOf(U+' + cp.toString(16) + ')');
    assert.strictEqual(L.isWireCodePoint(cp), (cp >= 0x20 && cp <= 0x7e) || expected(cp) >= 0, 'isWireCodePoint(U+' + cp.toString(16) + ')');
}
assert.strictEqual(L.MAX_NAME_CHARS, 32);
assert.strictEqual(L.MAX_NAME_BYTES, 96);

// names
const yes = ['Zoë', '[ÄNT] Zoë', '[РУС] Ёжик', 'Wren (Guest)', 'a', '', 'Ann Lee ~!{}', 'Ạ'.repeat(32), 'x'.repeat(32)];
const no = ['Αλέξης', '龍', '😀', 'á', 'a b', 'a\nb', 'a\u007fb', '×', '÷', 'ſ', 'x'.repeat(33), 'Ạ'.repeat(33), '\ud800', 'a\udc00'];
yes.forEach((name) => assert.strictEqual(L.isWireName(name), true, JSON.stringify(name)));
no.forEach((name) => assert.strictEqual(L.isWireName(name), false, JSON.stringify(name)));
assert.strictEqual(L.isWireName('\u1ea0'.repeat(33), 40), false, 'the bytes are limited too (96), whatever the limit of characters is');
assert.strictEqual(L.isWireName('x'.repeat(40), 40), true);
assert.strictEqual(L.isWireName('x'.repeat(100), 100), false, 'a name is at most 96 bytes by default');
assert.strictEqual(L.isWireName('x'.repeat(100), 100, 240), true, 'a room\'s notice has 240 bytes');
assert.strictEqual(L.isWireName('\u1ea0'.repeat(80), 100, 240), true);
assert.strictEqual(L.isWireName('\u1ea0'.repeat(81), 100, 240), false, 'the bytes are limited by the limit that is given');
assert.strictEqual(L.isWireName(123), false);
assert.strictEqual(L.isWireName(null), false);

// cutting what a name may not hold (the way the game's client makes its name)
assert.strictEqual(L.wireOnly('Zoë αβ 😀X'), 'Zoë  X');
assert.strictEqual(L.wireOnly('x'.repeat(50)), 'x'.repeat(32));
assert.strictEqual(L.wireOnly('Ạ'.repeat(50)), 'Ạ'.repeat(32));
assert.strictEqual(L.wireOnly('Wolfgang Amadeus Mozart', 16), 'Wolfgang Amadeus');
assert.strictEqual(L.wireOnly(undefined), '');
assert.strictEqual(L.wireOnly('\ud800'), '');

// UTF-8, strictly
const hex = (bytes) => Array.from(bytes).map((b) => (b < 16 ? '0' : '') + b.toString(16)).join('');
assert.strictEqual(hex(L.utf8Of(L.codePoints('Zoë'))), '5a6fc3ab');
assert.strictEqual(hex(L.utf8Of(L.codePoints('Ё'))), 'd081');
assert.strictEqual(hex(L.utf8Of(L.codePoints('Ạ'))), 'e1baa0');
assert.strictEqual(hex(L.utf8Of(L.codePoints('😀'))), 'f09f9880');
assert.strictEqual(L.codePoints('a\ud800'), null);
assert.strictEqual(L.codePoints('\udc00'), null);
assert.strictEqual(L.fromUtf8([0x5a, 0x6f, 0xc3, 0xab]), 'Zoë');
assert.strictEqual(L.fromUtf8(new Uint8Array([0xe1, 0xba, 0xa0])), 'Ạ');
assert.strictEqual(L.fromUtf8([0xf0, 0x9f, 0x98, 0x80]), '😀');
[[0xc0, 0x80], [0xc1, 0xbf], [0xe0, 0x80, 0x80], [0xe0, 0x9f, 0xbf], [0xed, 0xa0, 0x80], [0xf0, 0x80, 0x80, 0x80], [0xf4, 0x90, 0x80, 0x80], [0xf5, 0x80, 0x80, 0x80], [0xc3], [0xe1, 0xba], [0x80], [0xbf], [0xc3, 0x28], [0xff]]
    .forEach((bytes) => assert.strictEqual(L.fromUtf8(bytes), null, hex(bytes)));
for (const cp of [0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xffff, 0x10000, 0x10ffff]) {      // (the edges of every length round trip)
    const text = String.fromCodePoint(cp);
    assert.strictEqual(L.fromUtf8(L.utf8Of(L.codePoints(text))), text, 'U+' + cp.toString(16));
}
console.log('name letters: ok');
