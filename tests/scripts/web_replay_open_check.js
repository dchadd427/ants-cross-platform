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

// ---- a CRC-32 and a chunk writer that are not the page's (tests/scripts/replay_file_tools.js: the plain definition, bit by bit)
const tools = require('./replay_file_tools.js');
const crcOf = tools.crc32, le32 = tools.le32, MAGIC = tools.MAGIC, chunk = tools.chunk;
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

// ---- the tools that make the other recordings of these checks: they must be right themselves (a file made by hand from the real one is the real one with one thing changed)
same('FNV-1a 64 of nothing and of "a" (the published vectors)', [tools.fnv64([]), tools.fnv64([0x61])], ['cbf29ce484222325', 'af63dc4c8601ec8c']);
same('the chunks of the real file are the head, the commands, the hashes and the end', (() => { const c = tools.chunks(FIXTURE); return [c[0].tag, c[1].tag, c[c.length - 2].tag, c[c.length - 1].tag, c.some((x) => x.tag === 'tmap')]; })(), ['HEAD', 'CMDS', 'hash', 'ENDS', false]);
same('a head read and written again is the same head, byte for byte', Array.from(tools.withHead(FIXTURE, () => {})), Array.from(FIXTURE));
{
    const dune = Uint8Array.from(tools.text('a map file that is not one'));
    const named = tools.withMap(FIXTURE, 'Dune.lvl', dune, false), carried = tools.withMap(FIXTURE, 'Dune.lvl', dune, true), older = tools.withRules(FIXTURE, 4);
    const headOf = (f) => tools.readHead(tools.chunks(f)[0].payload).fields;
    same('a file that is given another map names it with the hash of its bytes', [String.fromCharCode(...headOf(named).find((f) => f.id === 3).bytes), headOf(named).find((f) => f.id === 4).bytes], ['Dune.lvl', tools.hashBytes(tools.fnv64(dune))]);
    same('... and the rest of the file is the real one', Array.from(named.subarray(tools.chunks(named)[1].at)), Array.from(FIXTURE.subarray(tools.chunks(FIXTURE)[1].at)));
    same('a file that carries its map has a tmap chunk right after the head, with the map in it', [tools.chunks(carried)[1].tag, tools.chunks(carried)[1].payload.length, tools.chunks(carried)[2].tag], ['tmap', dune.length, 'CMDS']);
    same('every file made by hand passes the page\'s check of its head', [accepted(named), accepted(carried), accepted(older)], [true, true, true]);
    same('the rules number of a file is changed in its head and nowhere else', [headOf(older).find((f) => f.id === 16).bytes, headOf(older).length], [[4, 0], headOf(FIXTURE).length]);
}

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

// ---- the reason that a file is refused is named (for a test and for nobody else: the card says one thing for all of them), so a check that is passed for the wrong reason is seen
for (const [label, bytes, why] of [['a text', text('hello, this is a text file and no replay at all, though it is long enough to hold a head if it were one'), 'magic'], ['a file of 5 bytes', Uint8Array.from([1, 2, 3, 4, 5]), 'short'],
                                   ['a replay cut inside its head', FIXTURE.slice(0, 100), 'head cut'], ['a replay cut inside its head checksum', FIXTURE.slice(0, headEnd - 1), 'head cut'],
                                   ['a head with a changed byte', Uint8Array.from(Array.from(FIXTURE).map((b, i) => (i === 40 ? b ^ 1 : b))), 'head checksum'], ['a first chunk that is no head', file(chunk('CMDS', zeros(40))), 'first chunk'],
                                   ['a head that claims 4 GB', file(chunk('HEAD', zeros(40), { length: 0xffffffff })), 'head length'], ['nothing', null, 'nothing']]) {
    same('why ' + label + ' is refused', verdict(bytes).why, why);
}

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

// ---- the page's check against a second reader written from the format's own words (the layout in include/ants_replay/replay.hpp), on thousands of damaged files: they must never differ, and the page's must never throw
{
    const reference = (b) => {                                   // (the same questions, asked the long way round)
        if (b.length > 2097164) return 'big';
        const head = [0x89, 0x41, 0x52, 0x50, 0x4c, 0x0d, 0x0a, 0x1a, 0x48, 0x45, 0x41, 0x44];
        if (b.length < 20 || head.some((v, i) => b[i] !== v)) return 'bad';
        const len = tools.read32(b, 12);
        if (len > 4096 || 20 + len > b.length) return 'bad';
        const tag = Array.from(b.subarray(8, 12)), payload = Array.from(b.subarray(16, 16 + len));
        return tools.crc32(Uint8Array.from(payload), tools.crc32(Uint8Array.from(tag))) === tools.read32(b, 16 + len) ? 'ok' : 'bad';
    };
    let seed = 20261104;
    const rnd = (n) => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed % n; };
    let differ = 0, threw = 0, okCount = 0, cases = 0;
    for (let i = 0; i < 4000; i++) {
        let b;
        const kind = rnd(5);
        if (kind === 0) { b = FIXTURE.slice(0, 1 + rnd(FIXTURE.length)); }                                                        // (cut anywhere)
        else if (kind === 1) { b = FIXTURE.slice(); for (let k = 0, n = 1 + rnd(4); k < n; k++) b[rnd(Math.min(b.length, 200))] ^= 1 << rnd(8); }          // (bits flipped near the front)
        else if (kind === 2) { b = FIXTURE.slice(); for (let k = 0, n = 1 + rnd(4); k < n; k++) b[rnd(b.length)] = rnd(256); }                     // (bytes set anywhere)
        else if (kind === 3) { b = Uint8Array.from({ length: rnd(64) }, () => rnd(256)); if (rnd(2)) b.set(MAGIC.concat([0x48, 0x45, 0x41, 0x44]).slice(0, b.length)); }  // (almost nothing, sometimes with the beginning of a replay)
        else { const body = Array.from({ length: rnd(300) }, () => rnd(256)); b = file(chunk('HEAD', body, { length: rnd(3) === 0 ? rnd(6000) : body.length, crc: rnd(4) === 0 ? rnd(0x7fffffff) : undefined })); }  // (a head with a claim and a checksum that are right or wrong)
        let got;
        try { const v = R.localCheck(b); got = v.ok ? 'ok' : v.kind; } catch (e) { threw++; continue; }
        cases++;
        if (got === 'ok') okCount++;
        if (got !== reference(b)) differ++;
    }
    same('thousands of damaged files: the page\'s check never throws and never differs from the second reader', [threw, differ], [0, 0]);
    check('... and the cases reach both answers (a real file now and then: ' + okCount + ' accepted of ' + cases + ')', okCount > 100 && okCount < cases - 1000, okCount + ' of ' + cases);
}

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


// ---- the browser's storage, as far as the hand-off uses it: a fake IndexedDB that keeps what a real one keeps (a copy of the value, one key, requests that answer later, a transaction that completes
// when its requests are done) and can fail the way a real one does (a refused open, an open that is blocked, a write that is aborted for want of room, a read that fails)
const allIdbs = [];
function fakeIndexedDB(o) {
    o = o || {};
    const dbs = {};
    const api = { opens: 0, writes: 0, deletes: 0, dbs, busy: 0 };
    const later = (fn) => { api.busy++; setTimeout(() => { try { fn(); } finally { api.busy--; } }, 0); };         // (every answer of the fake comes later, one at a time: settle() waits for all of them)
    allIdbs.push(api);
    api.open = (name, version) => {
        api.opens++;
        if (o.throwOnOpen) throw new Error('SecurityError');
        const request = { result: null, error: null };
        later(() => {
            if (o.errorOnOpen) { request.error = new Error('open failed'); if (request.onerror) request.onerror(); return; }
            if (o.blockedOnOpen) { if (request.onblocked) request.onblocked(); return; }
            let d = dbs[name];
            const fresh = !d;
            if (fresh) d = dbs[name] = { stores: {}, version };
            request.result = {
                createObjectStore(store) { d.stores[store] = new Map(); return {}; },
                transaction(store, mode) {
                    const tx = { error: null, pending: 0, finished: false };
                    const finish = () => later(() => { if (tx.pending === 0 && !tx.finished) { tx.finished = true; if (tx.oncomplete) tx.oncomplete(); } });
                    const map = d.stores[store];
                    if (!map) throw new Error('NotFoundError');
                    const op = (fn, writes) => {
                        const r = { result: undefined };
                        tx.pending++;
                        later(() => {
                            if (writes && mode !== 'readwrite') throw new Error('ReadOnlyError');
                            if ((writes && o.abortWrites) || (!writes && o.failReads)) {
                                tx.finished = true; tx.error = new Error(writes ? o.abortWrites : 'read failed');
                                if (tx.onabort) tx.onabort();
                                return;
                            }
                            r.result = fn();
                            if (r.onsuccess) r.onsuccess();
                            tx.pending--;
                            finish();
                        });
                        return r;
                    };
                    tx.objectStore = () => ({
                        put: (value, key) => op(() => { api.writes++; map.set(key, structuredClone(value)); return key; }, true),
                        get: (key) => op(() => (map.has(key) ? structuredClone(map.get(key)) : undefined), false),
                        delete: (key) => op(() => { api.deletes++; map.delete(key); return undefined; }, true),
                    });
                    return tx;
                },
            };
            if (fresh && request.onupgradeneeded) request.onupgradeneeded();
            if (request.onsuccess) request.onsuccess();
        });
        return request;
    };
    api.slot = () => { const d = dbs['ants-own-replay']; return d && d.stores.slot ? d.stores.slot.get('file') : undefined; };
    api.put = (rec) => { dbs['ants-own-replay'] = dbs['ants-own-replay'] || { stores: { slot: new Map() } }; dbs['ants-own-replay'].stores.slot.set('file', rec); };
    return api;
}
function fakeFile(bytes, name, o) {
    o = o || {};
    const f = { name, size: o.size !== undefined ? o.size : bytes.length, reads: 0 };
    f.arrayBuffer = () => { f.reads++; return o.failRead ? Promise.reject(new Error('read failed')) : Promise.resolve(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength)); };
    return f;
}
const hex32 = (n) => n.toString(16).padStart(32, '0');
function world(o) {
    const w = { t: 1790000000000, n: 0 };
    w.idb = fakeIndexedDB(o);
    w.store = R.ownStore({ indexedDB: w.idb, now: () => w.t, random: () => hex32(++w.n) });
    w.env = { store: w.store, FileReader: undefined };
    return w;
}
const DAY = 24 * 60 * 60 * 1000;
const sameBytes = (a, b) => a && b && a.length === b.length && a.every((x, i) => x === b[i]);

async function main() {
    // ---- an address asks for a record
    const ID = '0123456789abcdef0123456789abcdef';
    same('a record id', R.ownIdOf('?local=' + ID), ID);
    same('a record id among other parameters', R.ownIdOf('?aspect=16:9&local=' + ID + '&x=1'), ID);
    for (const bad of ['', '?', '?local=', '?local=x', '?local=' + ID.toUpperCase(), '?local=' + ID + '0', '?local=' + ID.slice(1), '?local=../' + ID, '?local=' + ID + '%0a', '?local=' + ID + '/', '?replay=' + ID, '?LOCAL=' + ID,
                       '?local=0123456789abcdef0123456789abcdeg', '?local=%30123456789abcdef0123456789abcdef0']) {
        same('refused ' + JSON.stringify(bad), R.ownIdOf(bad), null);
    }
    same('the first of two ids wins', R.ownIdOf('?local=' + ID + '&local=' + ID.replace(/0/g, '1')), ID);
    same('no string for an id', R.ownIdOf(undefined), null);
    {
        const ids = new Set();
        for (let i = 0; i < 50; i++) ids.add(R.ownRandomId({ crypto: { getRandomValues: (a) => { for (let k = 0; k < a.length; k++) a[k] = Math.floor(Math.random() * 256); return a; } } }));
        check('a new id is 32 hex digits and they differ', [...ids].every((x) => /^[0-9a-f]{32}$/.test(x)) && ids.size === 50);
        same('without a random source of the browser an id is still one (small bytes keep their leading 0)', /^[0-9a-f]{32}$/.test(R.ownRandomId({})), true);
        same('the browser\'s source fills all 16 bytes', R.ownRandomId({ crypto: { getRandomValues: (a) => { a.fill(1); return a; } } }), '01'.repeat(16));
    }

    // ---- put and read: one record, the bytes as they were, an id that an old link does not find
    {
        const w = world();
        const id = await w.store.put('a.antsrep', FIXTURE);
        check('put gives the new id', /^[0-9a-f]{32}$/.test(id), id);
        const rec = await w.store.read(id);
        check('the record is read back: the same bytes, the same name', !!rec && sameBytes(rec.bytes, FIXTURE) && rec.name === 'a.antsrep' && rec.id === id, JSON.stringify(rec && rec.name));
        check('what is read is a copy: the bytes the page passed are not the stored ones', rec.bytes !== FIXTURE);
        const again = await w.store.read(id);
        check('reading again works (a reload of the player must work: the record stays)', !!again && again.id === id);
        same('another id finds nothing', await w.store.read(hex32(999)), null);
        same('an id that is no id finds nothing and asks nothing of the storage', [await w.store.read('x'), await w.store.read(undefined), await w.store.read(ID.toUpperCase())], [null, null, null]);
        const second = await w.store.put('b.antsrep', FIXTURE.slice(0, 200));
        same('ONE record: the second file took the place of the first', [await w.store.read(id), (await w.store.read(second)).name], [null, 'b.antsrep']);
        same('... and the storage holds one record', w.idb.dbs['ants-own-replay'].stores.slot.size, 1);
    }

    // ---- a record is kept about a day, and every page that opens looks for old ones
    {
        const w = world();
        const id = await w.store.put('a.antsrep', FIXTURE);
        w.t += DAY - 1;
        check('a record of 23 h 59 min 59.999 s is still read', !!(await w.store.read(id)));
        same('... and a purge then keeps it', [await w.store.purge(), !!w.idb.slot()], [true, true]);
        w.t += 1;
        same('a record of a whole day is not read', await w.store.read(id), null);
        same('a purge takes the record of a whole day away', [await w.store.purge(), w.idb.slot()], [true, undefined]);
        same('a purge of an empty storage is fine', await w.store.purge(), true);
    }
    {
        const w = world();
        const id = await w.store.put('a.antsrep', FIXTURE);
        w.t += 30 * DAY;
        const before = w.idb.deletes;
        same('a record of a month is gone after the purge (it is not kept for ever)', [await w.store.purge(), w.idb.slot(), w.idb.deletes - before], [true, undefined, 1]);
        same('and is not read in the meantime either', await w.store.read(id), null);
    }
    {
        const w = world();
        const id = await w.store.put('a.antsrep', FIXTURE);
        w.t -= 30000;
        check('a clock that was put back by 30 s does not lose the record', !!(await w.store.read(id)) && (await w.store.purge()) && !!w.idb.slot());
        w.t -= DAY;
        same('one that was put back by a day does: a record "from the future" is not kept for ever', [await w.store.read(id), await w.store.purge(), w.idb.slot()], [null, true, undefined]);
    }
    {
        const w = world();
        w.idb.put({ id: ID, name: 'x', at: 'yesterday', bytes: FIXTURE });
        same('a record with no time is old', [await w.store.read(ID), await w.store.purge(), w.idb.slot()], [null, true, undefined]);
        w.idb.put('junk');
        same('a record that is not a record is thrown away and not read', [await w.store.read(ID), await w.store.purge(), w.idb.slot()], [null, true, undefined]);
        w.idb.put({ id: ID, name: 7, at: w.t, bytes: FIXTURE });
        same('a record with a name that is no text is not one', await w.store.read(ID), null);
        w.idb.put({ id: ID, name: 'x', at: w.t });
        same('a record with no bytes is not one', await w.store.read(ID), null);
    }

    // ---- a storage that fails: the calls reject (the page then says so), a purge never does
    for (const [label, o] of [['an IndexedDB that throws when it is opened (a browser that blocks site data)', { throwOnOpen: true }], ['one that answers with an error', { errorOnOpen: true }], ['one that is blocked', { blockedOnOpen: true }]]) {
        const w = world(o);
        let put = 'resolved', read = 'resolved';
        await w.store.put('a', FIXTURE).then(() => {}, () => { put = 'rejected'; });
        await w.store.read(ID).then(() => {}, () => { read = 'rejected'; });
        same(label + ': put rejects, read rejects', [put, read], ['rejected', 'rejected']);
        same(label + ': a purge says no and does not reject', await w.store.purge(), false);
    }
    {
        const w = world({ abortWrites: 'QuotaExceededError' });
        let put = 'resolved';
        await w.store.put('a', FIXTURE).then(() => {}, () => { put = 'rejected'; });
        same('a write that is aborted for want of room rejects, and nothing is kept', [put, w.idb.slot()], ['rejected', undefined]);
    }
    {
        const w = world({ failReads: true });
        let read = 'resolved';
        await w.store.read(ID).then(() => {}, () => { read = 'rejected'; });
        same('a read that fails rejects', read, 'rejected');
    }
    {
        const blocked = { get indexedDB() { throw new Error('SecurityError: the operation is insecure'); }, crypto: undefined };
        let made = 'made', put = 'resolved';
        try { const store = R.ownStoreOf(blocked); await store.put('a', FIXTURE).then(() => {}, () => { put = 'rejected'; }); same('a browser that throws at the mention of indexedDB: a store that fails, and a purge that says no', [put, await store.purge()], ['rejected', false]); } catch (e) { made = 'threw'; }
        same('... and making the store does not throw', made, 'made');
        const fine = R.ownStoreOf({ indexedDB: fakeIndexedDB(), crypto: undefined });
        const id = await fine.put('a.antsrep', FIXTURE);
        same('the store of a page with an IndexedDB keeps and gives the record', [/^[0-9a-f]{32}$/.test(id), (await fine.read(id)).name], [true, 'a.antsrep']);
    }
    {
        const w = { t: 0 };
        const store = R.ownStore({ indexedDB: undefined, now: () => w.t, random: () => ID });
        let put = 'resolved';
        await store.put('a', FIXTURE).then(() => {}, () => { put = 'rejected'; });
        same('no IndexedDB at all: put rejects', put, 'rejected');
    }
    {
        const w = world({ errorOnOpen: true });
        await w.store.put('a', FIXTURE).then(() => {}, () => {});
        w.idb.opens = 0;
        w.store = null;
        const w2 = world();
        await w2.store.put('a', FIXTURE); await w2.store.read(ID); await w2.store.purge();
        same('a storage that opened is opened once for all the calls', w2.idb.opens, 1);
    }

    // ---- taking a file over: the size is asked before a byte is read, a file that fails is no record, the record that was there stays
    {
        const w = world();
        const file = fakeFile(FIXTURE, 'ants-TINY-20261106-110000.antsrep');
        const r = await R.ownOpen(w.env, file);
        check('a replay is taken over: ok with an id, its name and its size', r.ok === true && /^[0-9a-f]{32}$/.test(r.id) && r.name === 'ants-TINY-20261106-110000.antsrep' && r.size === FIXTURE.length, JSON.stringify(r));
        const rec = await w.store.read(r.id);
        check('... and the player finds exactly its bytes under that id', !!rec && sameBytes(rec.bytes, FIXTURE) && rec.name === r.name);
        same('the file was read once', file.reads, 1);
    }
    {
        const w = world();
        const keep = await R.ownOpen(w.env, fakeFile(FIXTURE, 'keep.antsrep'));
        const huge = fakeFile(new Uint8Array(10), 'holiday-video.mp4', { size: 14900000 });
        const r = await R.ownOpen(w.env, huge);
        same('a file that is too big by its size: kind big, with its size and name', [r.ok, r.kind, r.size, r.name], [false, 'big', 14900000, 'holiday-video.mp4']);
        same('... and not one byte of it was read', huge.reads, 0);
        same('... the record that was there is still there', (await w.store.read(keep.id)).name, 'keep.antsrep');
        const edge = fakeFile(new Uint8Array(10), 'edge', { size: R.LOCAL_MAX_BYTES + 1 });
        same('a file of one byte more than the limit is too big and unread', [(await R.ownOpen(w.env, edge)).kind, edge.reads], ['big', 0]);
        const exact = fakeFile(padded(R.LOCAL_MAX_BYTES), 'exact.antsrep');
        same('a file of exactly the limit is read', [(await R.ownOpen(w.env, exact)).ok, exact.reads], [true, 1]);
    }
    {
        const w = world();
        const liar = fakeFile(new Uint8Array(R.LOCAL_MAX_BYTES + 5000), 'liar.antsrep', { size: 3000 });
        const r = await R.ownOpen(w.env, liar);
        same('a file that says 3,000 bytes and holds more than the limit is too big on its bytes (the second line)', [r.ok, r.kind, w.idb.slot()], [false, 'big', undefined]);
    }
    {
        const w = world();
        const keep = await R.ownOpen(w.env, fakeFile(FIXTURE, 'keep.antsrep'));
        for (const [label, file, kind] of [
            ['an empty file', fakeFile(new Uint8Array(0), 'empty.antsrep'), 'bad'], ['a text', fakeFile(text('this is not a replay, though it is long enough to hold a head if it were one, and then some'), 'notes.txt'), 'bad'],
            ['a replay with a damaged head', fakeFile(Uint8Array.from(Array.from(FIXTURE).map((b, i) => (i === 60 ? b ^ 255 : b))), 'damaged.antsrep'), 'bad'],
            ['a replay cut inside its head', fakeFile(FIXTURE.slice(0, 120), 'cut.antsrep'), 'bad'], ['a file that cannot be read', fakeFile(FIXTURE, 'gone.antsrep', { failRead: true }), 'bad'],
            ['no file at all', null, 'bad'], ['a size that is no number', { name: 'x', size: 'big', arrayBuffer: () => Promise.resolve(new ArrayBuffer(0)) }, 'bad'],
        ]) {
            const r = await R.ownOpen(w.env, file);
            same(label + ': refused as ' + kind, [r.ok, r.kind], [false, kind]);
        }
        same('none of them touched the record that was there', [(await w.store.read(keep.id)).name, w.idb.writes], ['keep.antsrep', 1]);
    }
    {
        const w = world({ abortWrites: 'QuotaExceededError' });
        const r = await R.ownOpen(w.env, fakeFile(FIXTURE, 'a.antsrep'));
        same('a browser that cannot keep the file: kind nostore, and the name is still there to show', [r.ok, r.kind, r.name], [false, 'nostore', 'a.antsrep']);
        const w2 = world({ throwOnOpen: true });
        same('and one with no usable storage the same', (await R.ownOpen(w2.env, fakeFile(FIXTURE, 'a.antsrep'))).kind, 'nostore');
    }
    {
        const w = world();
        const r = await R.ownOpen(w.env, fakeFile(FIXTURE, 'ants-' + bidi + 'perstna' + zw + '.antsrep'));
        same('the name that comes back is text that is safe to show', r.name, 'ants-perstna.antsrep');
        let reader = 0;
        function FakeReader() { this.readAsArrayBuffer = (f) => { reader++; this.result = f.buf; setTimeout(() => this.onload(), 0); }; }
        const noPromise = { name: 'old.antsrep', size: FIXTURE.length, buf: FIXTURE.buffer.slice(0) };
        const r2 = await R.ownOpen({ store: w.store, FileReader }, noPromise);
        function FileReader() { return new FakeReader(); }
        same('a File with no arrayBuffer() is read with a FileReader', [r2.ok, reader], [true, 1]);
    }

    // ---- the pages' own code on a small fake of the DOM: it records what was set and how, and THROWS when markup is made from text
    function fakeDom() {
        const textOf = (n) => (n.nodeType === 3 ? n.data : n.textContent);
        const mkNode = (tag) => {
            const n = { tag, nodeType: 1, children: [], attrs: {}, className: '', hidden: false, _on: {}, style: {}, tabIndex: 0, type: '', href: '', id: '', clicks: 0, files: [], _value: '', parent: null, _text: null };
            n.classList = { set: new Set(), toggle(c, on) { if (on === undefined ? !this.set.has(c) : on) this.set.add(c); else this.set.delete(c); }, add(c) { this.set.add(c); }, remove(c) { this.set.delete(c); }, contains(c) { return this.set.has(c); } };
            n.appendChild = (c) => { c.parent = n; n.children.push(c); return c; };
            n.setAttribute = (k, v) => { n.attrs[k] = String(v); };
            n.getAttribute = (k) => n.attrs[k];
            n.addEventListener = (type, fn) => { (n._on[type] = n._on[type] || []).push(fn); };
            n.fire = (type, ev) => { const e = Object.assign({ type, target: n, defaultPrevented: false, preventDefault() { this.defaultPrevented = true; } }, ev || {}); (n._on[type] || []).forEach((fn) => fn(e)); return e; };
            n.click = () => { n.clicks++; if (tag !== 'input') n.fire('click'); };
            n.querySelector = () => n._found || null;
            Object.defineProperty(n, 'textContent', { get() { return n._text !== null && !n.children.length ? n._text : n.children.map(textOf).join(''); }, set(v) { n.children = []; n._text = String(v); } });
            Object.defineProperty(n, 'innerHTML', { set() { throw new Error('markup was made from text'); }, get() { return ''; } });
            Object.defineProperty(n, 'value', { get() { return n._value; }, set(v) { n._value = String(v); } });
            return n;
        };
        const byId = {};
        const doc = mkNode('#document');
        doc.createElement = (tag) => mkNode(tag);
        doc.createTextNode = (data) => ({ nodeType: 3, data: String(data) });
        doc.getElementById = (id) => byId[id] || null;
        doc.body = mkNode('body');
        doc.add = (id, tag) => { const n = mkNode(tag || 'div'); n.id = id; byId[id] = n; return n; };
        doc.drag = (type, types, files) => doc.fire(type, { dataTransfer: { types, files: files || [], dropEffect: '' } });
        return doc;
    }
    // a clock that moves when it is told: setTimeout / clearTimeout of the page
    function fakeClock() {
        const c = { t: 0, next: 1, timers: {} };
        c.setTimeout = (fn, ms) => { c.timers[c.next] = { at: c.t + ms, fn }; return c.next++; };
        c.clearTimeout = (id) => { delete c.timers[id]; };
        c.advance = (ms) => { c.t += ms; Object.keys(c.timers).map(Number).sort((a, b) => a - b).forEach((id) => { const t = c.timers[id]; if (t && t.at <= c.t) { delete c.timers[id]; t.fn(); } }); };
        return c;
    }
    // waits until everything that the fakes were asked for is done (every fake storage has nothing left to answer, twice in a row): a fixed pause would be a race on a busy machine
const tick = () => new Promise((resolve) => setTimeout(resolve, 0));
async function settle() {
    let quiet = 0;
    for (let i = 0; i < 2000 && quiet < 3; i++) { await tick(); quiet = allIdbs.every((db) => db.busy === 0) ? quiet + 1 : 0; }
}
    const MAPS = (() => { try { return require(require('path').join(require('path').dirname(require('path').resolve(scriptPath)), 'map_files.js')); } catch (e) { return null; } })();

    // ---- the strip of the Matches page
    function stripWorld(o) {
        o = o || {};
        const w = world(o.idb);
        w.doc = fakeDom();
        w.strip = w.doc.add('opener', 'section'); w.strip.hidden = true;
        w.button = w.doc.add('open-btn', 'button'); w.input = w.doc.add('open-file', 'input'); w.veil = w.doc.add('dropveil'); w.veil.hidden = true; w.note = w.doc.add('open-note'); w.note.hidden = true;
        w.clock = fakeClock();
        w.went = [];
        w.purges = 0;
        const purge = w.store.purge;
        w.store.purge = () => { w.purges++; return purge(); };
        w.win = { File: o.noFile ? undefined : function File() {}, listeners: {}, addEventListener(type, fn) { (this.listeners[type] = this.listeners[type] || []).push(fn); } };
        w.env = { doc: w.doc, win: w.win, store: w.store, FileReader: undefined, go: (url) => w.went.push(url), setTimeout: w.clock.setTimeout, clearTimeout: w.clock.clearTimeout };
        w.opener = R.opener(w.env);
        return w;
    }
    const chooseOne = async (w, file) => { w.input.files = [file]; w.input.value = 'C:\\fakepath\\x'; w.input.fire('change'); await settle(); };
    {
        const w = stripWorld({ noFile: true });
        same('a browser that cannot read a file has no strip: nothing is wired and the strip stays hidden', [w.opener.wire(), w.strip.hidden, w.purges], [false, true, 0]);
    }
    {
        const w = stripWorld();
        same('wire: the strip shows, old records are looked for, the chooser is not opened by itself', [w.opener.wire(), w.strip.hidden, w.purges, w.input.clicks], [true, false, 1, 0]);
        same('the chooser has no accept filter (a phone greys out a file of an ending that it does not know)', w.input.attrs.accept, undefined);
        w.note.hidden = false; w.note.textContent = 'old card';
        w.button.fire('click');
        same('the button opens the chooser and clears the old card', [w.input.clicks, w.note.hidden, w.note.textContent], [1, true, '']);
        const file = fakeFile(FIXTURE, 'ants-TREASURE-20261006-143209.antsrep');
        await chooseOne(w, file);
        const rec = w.idb.slot();
        check('a file from the chooser is checked, kept for the player and the page goes to it at its own address', w.went.length === 1 && w.went[0] === '/play.html?local=' + (rec && rec.id) && /^\/play\.html\?local=[0-9a-f]{32}$/.test(w.went[0]), JSON.stringify(w.went));
        same('the chooser is emptied (the same file chosen again is a change)', w.input.value, '');
        check('what the player will find is the file', !!rec && sameBytes(rec.bytes, FIXTURE) && rec.name === 'ants-TREASURE-20261006-143209.antsrep');
    }
    {
        const w = stripWorld(); w.opener.wire();
        await chooseOne(w, fakeFile(text('this is a text file, long enough to look at, and no replay at all'), 'notes.txt'));
        const card = w.note.children[0];
        check('a file that is no replay: no navigation, nothing kept, and a card under the strip', w.went.length === 0 && w.idb.slot() === undefined && !w.note.hidden && !!card && card.attrs.role === 'alert');
        same('the card: the approved title and sentence, the name of the file, a button and the line that says nothing was sent', [card.children[0].textContent, card.children[1].textContent, card.children[2].textContent, card.children[3].children[0].textContent, card.children[3].children[1].textContent],
             ['That file is not a readable replay', 'It does not start like an Ants replay, or it is damaged (a download that stopped halfway, or a file that was changed).', 'notes.txt', 'Choose another file', 'Nothing was sent anywhere']);
        const again = card.children[3].children[0];
        again.fire('click');
        same('the button of the card opens the chooser again and takes the card away', [w.input.clicks, w.note.hidden, w.note.textContent], [1, true, '']);
    }
    {
        const w = stripWorld(); w.opener.wire();
        const video = fakeFile(new Uint8Array(10), 'holiday-video.mp4', { size: 14900000 });
        await chooseOne(w, video);
        const card = w.note.children[0];
        same('a file that is too big: the approved words with its size, and not a byte of it was read', [card.children[0].textContent, card.children[1].textContent, card.children[2].textContent, video.reads, w.went.length],
             ['That file is too big', 'A replay file is at most 2 MB. This one is 14.2 MB, so it is not a replay of a match.', 'holiday-video.mp4', 0, 0]);
    }
    {
        const w = stripWorld({ idb: { abortWrites: 'QuotaExceededError' } }); w.opener.wire();
        await chooseOne(w, fakeFile(FIXTURE, 'a.antsrep'));
        const card = w.note.children[0];
        same('a browser that cannot keep the file: its own card, no navigation', [card.children[0].textContent, w.went.length, /private or blocked site data/.test(card.children[1].textContent)], ['This browser will not keep the file for the player', 0, true]);
    }
    {
        const w = stripWorld(); w.opener.wire();
        const evil = '<img src=x onerror=alert(1)>.antsrep';
        await chooseOne(w, fakeFile(text('no replay at all, only a text that is long enough to be looked at, as it is'), evil));
        same('the name of a file is text in the card, whatever it holds (the fake throws on markup)', w.note.children[0].children[2].textContent, evil);
        const w2 = stripWorld(); w2.opener.wire();
        await chooseOne(w2, fakeFile(text('no replay at all, only a text that is long enough to be looked at, as it is'), String.fromCharCode(0x202e) + 'gnp.exe'));
        same('... and without the marks that reorder what is around it', w2.note.children[0].children[2].textContent, 'gnp.exe');
        const w3 = stripWorld(); w3.opener.wire();
        await chooseOne(w3, fakeFile(text('no replay at all, only a text that is long enough to be looked at, as it is'), String.fromCharCode(0x202e)));
        same('a name of nothing but such marks has no line', w3.note.children[0].children.length, 3);
    }
    // the drop
    {
        const w = stripWorld(); w.opener.wire();
        const over = w.doc.drag('dragenter', ['Files']);
        same('a file dragged onto the page: the page takes the drag over, the veil shows, the strip lights up', [over.defaultPrevented, w.veil.hidden, w.strip.classList.contains('drag')], [true, false, true]);
        const moving = w.doc.drag('dragover', ['Files']);
        same('dragover is taken over too (else the browser would not allow the drop) and says copy', [moving.defaultPrevented, moving.dataTransfer.dropEffect], [true, 'copy']);
        w.doc.drag('dragleave', ['Files']);
        same('it leaves: the veil goes', [w.veil.hidden, w.strip.classList.contains('drag')], [true, false]);
        w.doc.drag('dragenter', ['Files']); w.doc.drag('dragenter', ['Files']);
        w.doc.drag('dragleave', ['Files']);
        same('entering two elements and leaving one: the veil stays', w.veil.hidden, false);
        w.doc.drag('dragleave', ['Files']);
        same('... and leaving the second takes it down', w.veil.hidden, true);
        w.doc.drag('dragover', ['Files']);
        w.clock.advance(500); w.doc.drag('dragover', ['Files']); w.clock.advance(500);
        same('a drag that keeps moving keeps the veil (the timer starts again at every move)', w.veil.hidden, false);
        w.clock.advance(400);
        same('a drag that says nothing for 900 ms (cancelled) takes it down', w.veil.hidden, true);
        for (const type of ['dragenter', 'dragover', 'dragleave', 'drop']) {
            const e = w.doc.drag(type, ['text/plain', 'text/uri-list']);
            check('a drag of text or a link is none of the page\'s business: ' + type + ' is not taken over', !e.defaultPrevented);
        }
        same('... and no veil came for it', w.veil.hidden, true);
        same('a drag with no types at all is nothing either', w.doc.fire('dragover', { dataTransfer: {} }).defaultPrevented, false);
        same('and one with no dataTransfer', w.doc.fire('dragover', {}).defaultPrevented, false);
    }
    {
        const w = stripWorld(); w.opener.wire();
        w.doc.drag('dragenter', ['Files']);
        const drop = w.doc.drag('drop', ['Files'], [fakeFile(FIXTURE, 'dropped.antsrep'), fakeFile(FIXTURE.slice(0, 500), 'second.antsrep')]);
        await settle();
        const rec = w.idb.slot();
        same('a drop: taken over (the browser must not leave for the file), the veil goes, the FIRST file is taken', [drop.defaultPrevented, w.veil.hidden, rec && rec.name, w.went.length], [true, true, 'dropped.antsrep', 1]);
        check('... and the page goes to its record', w.went[0] === '/play.html?local=' + rec.id);
    }
    {
        const w = stripWorld(); w.opener.wire();
        const drop = w.doc.drag('drop', ['Files'], []);
        await settle();
        same('a drop with Files that holds none (a folder the browser would not hand over) is still taken over and does nothing', [drop.defaultPrevented, w.went.length, w.idb.slot()], [true, 0, undefined]);
        const bad = w.doc.drag('drop', ['Files'], [fakeFile(text('no replay at all, only a text that is long enough to be looked at, as it is'), 'a.txt')]);
        await settle();
        same('a dropped file that is no replay: the card, no navigation', [bad.defaultPrevented, w.went.length, w.note.hidden, w.note.children[0].children[0].textContent], [true, 0, false, 'That file is not a readable replay']);
    }
    {
        const w = stripWorld(); w.opener.wire();
        const a = w.doc.drag('drop', ['Files'], [fakeFile(FIXTURE, 'first.antsrep')]);
        const b = w.doc.drag('drop', ['Files'], [fakeFile(FIXTURE, 'second.antsrep')]);
        await settle();
        same('a second drop while the first is on its way is taken over and ignored', [a.defaultPrevented, b.defaultPrevented, w.went.length, w.idb.slot().name], [true, true, 1, 'first.antsrep']);
        check('the strip listens for the page coming back from the browser\'s memory', !!w.win.listeners.pageshow && w.win.listeners.pageshow.length === 1);
        (w.win.listeners.pageshow || []).forEach((fn) => fn({ persisted: false }));
        w.doc.drag('drop', ['Files'], [fakeFile(FIXTURE, 'third.antsrep')]); await settle();
        same('a page shown again WITHOUT the browser\'s memory of it is a new page: nothing changes here', w.went.length, 1);
        (w.win.listeners.pageshow || []).forEach((fn) => fn({ persisted: true }));
        w.doc.drag('drop', ['Files'], [fakeFile(FIXTURE, 'fourth.antsrep')]); await settle();
        same('a page that comes back from the browser\'s memory (Back) takes a file again', [w.went.length, w.idb.slot().name], [2, 'fourth.antsrep']);
    }

    // ---- the player's own mode
    const ownWorld = (o) => {
        const w = world(o && o.idb);
        w.doc = fakeDom();
        for (const id of ['r-title', 'r-meta']) w.doc.add(id);
        w.back = w.doc.add('all-btn', 'a'); w.back.textContent = '\u2039 All matches';
        w.more = w.doc.add('more-list'); w.doc.documentElement = null;
        const list = w.more; w.doc.querySelector = (sel) => (/more-list/.test(sel) ? list : null);
        w.said = []; w.went = []; w.reloads = 0;
        w.win = { location: { reload() { w.reloads++; } } };
        w.page = R.ownPage({ doc: w.doc, win: w.win, store: w.store, FileReader: undefined, search: '?local=' + (o && o.id !== undefined ? o.id : ID), say: (t) => w.said.push(t), go: (u) => w.went.push(u), infoOf: MAPS ? MAPS.infoOfReplay : undefined });
        return w;
    };
    const kids = (n) => n.children.map((c) => c.nodeType === 3 ? c.data : c.tag + ':' + c.textContent);
    const words = (n) => n.textContent, bolds = (n) => n.children.filter((c) => c.nodeType !== 3 && c.tag === 'b').map((c) => c.textContent);       // (a sentence may be several text nodes: its words and its bold parts are what a person sees)
    {
        const w = ownWorld({ id: '' });
        same('an address with no id has no record: the card to choose the file again', await w.page.read(), { error: 'again' });
        w.page.header(null, 0, false);
        same('and its header names no file (there is none): the title alone', [w.doc.getElementById('r-title').children.length, w.doc.getElementById('r-title').children[0].data, w.doc.getElementById('r-meta').children.length], [1, 'A match to watch', 0]);
        same('a reason that is no text is left out of the card, not a crash', w.page.card('cant', { why: 7 }).body.length, 1);
    }
    {
        const w = ownWorld({ id: '0'.repeat(32) });
        same('an id that nothing was kept under has none either', await w.page.read(), { error: 'again' });
    }
    {
        const w = ownWorld();
        const id = await w.store.put('ants-TINY-20261006-143209.antsrep', FIXTURE);
        const w2 = ownWorld({ id });
        w2.store = w.store; w2.page = R.ownPage({ doc: w2.doc, win: w2.win, store: w.store, search: '?local=' + id, say: () => {}, go: () => {}, infoOf: MAPS.infoOfReplay });
        const got = await w2.page.read();
        check('the record of the address is read: its bytes and its name', !!got.bytes && sameBytes(got.bytes, FIXTURE) && got.name === 'ants-TINY-20261006-143209.antsrep' && got.error === undefined, JSON.stringify(Object.keys(got)));
        same('the map of the head is known before the game reads the file', w2.page.map(), 'TINY.LVL');
        w2.page.header(null, 0, false);
        same('the header before the game reports: the map, Your file and the name; no length, no date', [w2.doc.getElementById('r-title').children[0].data, kids(w2.doc.getElementById('r-title').children[1])], ['Tiny', ['span:Your file', ' ants-TINY-20261006-143209.antsrep']]);
        same('... the chips are the players of the head', w2.doc.getElementById('r-meta').children[0].children.map((li) => li.children[1].textContent), ['Bot (Medium)', 'Bot (Medium)', 'Bot (Medium)', 'Bot (Medium)']);
        same('the pill is the pill of the page (class pill own)', w2.doc.getElementById('r-title').children[1].children[0].className, 'pill own');
        w2.page.header({ map: 'TREASURE.LVL', roster: 5, names: ['Mika', '', 'Pip', ''] }, 1003, false);
        same('what the game reports wins: its map, its players, the length of the match', [w2.doc.getElementById('r-title').children[0].data, kids(w2.doc.getElementById('r-title').children[1]), w2.doc.getElementById('r-meta').children[0].children.map((li) => li.children[1].textContent)],
             ['Treasure', ['span:Your file', ' ants-TINY-20261006-143209.antsrep \u00b7 16:43'], ['Mika', 'Pip']]);
        w2.page.header({ map: 'TREASURE.LVL', roster: 1, names: ['Mika'] }, 600, true);
        same('a recording that was cut short says so', kids(w2.doc.getElementById('r-title').children[1]).slice(-2), [' ', 'span:Cut short']);
        w2.page.chrome();
        same('the way back is Matches, and More has one thing: Open another file', [w2.back.textContent, w2.more.children.length, w2.more.children[0].textContent, w2.more.children[0].id, w2.more.children[0].attrs.type === undefined ? w2.more.children[0].type : w2.more.children[0].attrs.type], ['\u2039 Matches', 1, 'Open another file', 'own-more', 'button']);
        const key = w2.more.children[0];
        key.fire('keydown', { key: 'Enter', repeat: false }); key.fire('keydown', { key: ' ', repeat: false }); key.fire('keydown', { key: 'a' });
        same('Enter and Space press the button here (the game takes the browser\'s press of a focused button away); another key does not; a repeat does not press again', [key.clicks], [2]);
        key.fire('keydown', { key: 'Enter', repeat: true });
        same('a key held down presses once', key.clicks, 2);
    }
    {
        const w = ownWorld();
        const id = await w.store.put('x.antsrep', Uint8Array.from(Array.from(FIXTURE).map((b, i) => (i === 40 ? b ^ 255 : b))));
        const w2 = ownWorld({ id }); w2.page = R.ownPage({ doc: w2.doc, win: w2.win, store: w.store, search: '?local=' + id, say: () => {}, go: () => {} });
        same('a record whose bytes are damaged is read as a failure with its reason (whoever put them there, the page checks them again)', await w2.page.read(), { error: 'load', text: 'the file that this page kept is damaged' });
        const w3 = ownWorld({ idb: { failReads: true } });
        const r = await w3.page.read();
        same('a storage that fails: a failure with its reason', [r.error, /does not let the page read the file back/.test(r.text)], ['load', true]);
        const w4 = ownWorld(); const id4 = await w4.store.put('x.antsrep', FIXTURE); w4.t += 25 * 3600 * 1000;
        const w5 = ownWorld({ id: id4 }); w5.page = R.ownPage({ doc: w5.doc, win: w5.win, store: w4.store, search: '?local=' + id4, say: () => {}, go: () => {} });
        same('a record older than a day is not played: choose the file again (the page threw it away first)', [await w5.page.read(), w4.idb.slot()], [{ error: 'again' }, undefined]);
    }
    {
        const w = ownWorld();
        const plan = (kind, ctx) => w.page.card(kind, Object.assign({ made: 'v0.11.0', version: 'v0.0.0', map: 'Dune.lvl' }, ctx || {}));
        const shape = (p) => ({ title: p.title, body: p.body.map((n) => kids(n)), buttons: p.buttons.map((b) => b.tag + ':' + b.textContent + (b.href ? '>' + b.href : '')) });
        same('older: the approved words, Open another file and All matches, and no download', shape(plan('older')), { title: 'Recorded by an older version',
            body: [['#text-less'].length ? ['This match was played on ', 'b:v0.11.0', '. The rules of the game have changed since, and a replay only plays on a version with the same rules, so this version (', 'b:v0.0.0', ') can\u2019t show it.'] : []],
            buttons: ['button:Open another file', 'a:\u2039 All matches>/matches.html'] });
        same('newer: Open another file, Load the page again, All matches', shape(plan('newer', { made: 'v0.14.2' })), { title: 'Recorded by a newer version', body: [['This match was played on ', 'b:v0.14.2', ', and this page is ', 'b:v0.0.0', '. Load the page again to get the newer version.']],
            buttons: ['button:Open another file', 'a:Load the page again>#', 'a:\u2039 All matches>/matches.html'] });
        const again = plan('newer').buttons[1];
        const click = again.fire('click');
        same('Load the page again reloads and does not follow the link', [click.defaultPrevented, w.reloads], [true, 1]);
        same('a map that the site has not: This site doesn\u2019t have the map, with the map\'s name', [plan('nomap').title, plan('nomap').body.map(words), plan('nomap').body.map(bolds), shape(plan('nomap')).buttons],
             ['This site doesn\u2019t have the map', ['This match was played on the map Dune.lvl, and a replay needs the same map file. The site no longer has it (a map that was taken off, or one that was only on the recorder\u2019s own computer).'], [['Dune.lvl']], ['button:Open another file', 'a:\u2039 All matches>/matches.html']]);
        same('a map of that name that is another file', [plan('changed').body.map(words), plan('changed').body.map(bolds)], [['This match was played on the map Dune.lvl, and a replay needs the same map file. The site has a map of that name, but it is not the same file (the map was changed after this match was played).'], [['Dune.lvl']]]);
        same('a map with no name still has its words', [plan('nomap', { map: '' }).body.map(words), plan('nomap', { map: '' }).body.map(bolds)], [['This match was played on a map, and a replay needs the same map file. The site no longer has it (a map that was taken off, or one that was only on the recorder\u2019s own computer).'], [[]]]);
        same('no record: choose the file again, a button for a file and All matches', shape(plan('again')), { title: 'Choose the replay file again', body: [['The file you opened is no longer here: this page keeps it for a day, and only one file at a time. Choose it again. It stays on this device: nothing is uploaded.']],
            buttons: ['button:Open a replay file', 'a:\u2039 All matches>/matches.html'] });
        same('the rest: This replay can\u2019t be played here, the reason as a sentence, no download', shape(plan('cant', { why: 'chunk CMDS: the checksum is wrong' })), { title: 'This replay can\u2019t be played here', body: [['The match could not be set up on this page.'], ['Chunk CMDS: the checksum is wrong.']],
            buttons: ['button:Open another file', 'a:\u2039 All matches>/matches.html'] });
        same('the reason keeps its own full stop and an empty one is left out', [kids(plan('cant', { why: 'it broke.' }).body[1]), plan('cant', { why: '' }).body.length], [['It broke.'], 1]);
        same('a kind that a file of your own has no words for is none', [plan('gone'), plan('over'), plan('notlive')], [null, null, null]);
        check('no card offers the file for download', ['older', 'newer', 'nomap', 'changed', 'again', 'cant'].every((k) => plan(k).buttons.every((b) => !('download' in b.attrs) && !/download/i.test(b.textContent)) && plan(k).body.every((n) => !/download/i.test(n.textContent))));
        check('every button of a card is a real button or a link, and the buttons press with Enter and Space', plan('older').buttons[0].type === 'button' && (plan('older').buttons[0]._on.keydown || []).length === 1);
        const evilCard = plan('older', { made: '<img src=x onerror=alert(1)>', version: '<b>v</b>' });
        same('what the game reports of the file (who made it) is text in the card', kids(evilCard.body[0]), ['This match was played on ', 'b:<img src=x onerror=alert(1)>', '. The rules of the game have changed since, and a replay only plays on a version with the same rules, so this version (', 'b:<b>v</b>', ') can\u2019t show it.']);
        const pick = w.page.pick; pick();
        const input = w.doc.body.children[0];
        same('the chooser of the player is made once, hidden, and opened', [input.tag, input.type, input.attrs['aria-hidden'], input.clicks, w.doc.body.children.length], ['input', 'file', 'true', 1, 1]);
        pick();
        same('... and used again', [input.clicks, w.doc.body.children.length], [2, 1]);
        input.files = [fakeFile(FIXTURE, 'new.antsrep')]; input.fire('change'); await settle();
        check('a file from it: checked, kept, and the page goes to it (replace: the old card is not a place to come back to)', w.went.length === 1 && /^\/play\.html\?local=[0-9a-f]{32}$/.test(w.went[0]), JSON.stringify(w.went));
        const w6 = ownWorld(); w6.page.pick(); const in6 = w6.doc.body.children[0];
        in6.files = [fakeFile(text('no replay at all, only a text that is long enough to be looked at, as it is'), 'notes.txt')]; in6.fire('change'); await settle();
        same('a file that is refused from the player is a note and nothing else', [w6.said, w6.went.length], [['That file is not a readable replay.'], 0]);
        in6.files = [fakeFile(new Uint8Array(1), 'big.mp4', { size: 9000000 })]; in6.fire('change'); await settle();
        same('(and the page can take another file after a refusal)', [w6.said[1], w6.went.length], ['That file is too big.', 0]);
    }

    return;
}

main().then(() => {
    console.log('web_replay_open_check: ' + checks + ' checks, ' + failures + ' failures');
    process.exit(failures ? 1 : 0);
}, (e) => {
    console.log('FAIL the check itself broke: ' + (e && e.stack || e));
    process.exit(1);
});
