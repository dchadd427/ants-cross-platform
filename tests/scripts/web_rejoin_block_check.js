// Runs the front page's OWN code for the Rejoin button (the owner: "if your browser crashed or your power went out, you could ... hold the game paused until you get back"): the block REJOIN_BEGIN ..
// REJOIN_END of web/lobby.html, on tables, with a fake storage (length, key(i), getItem, setItem, removeItem: every call counted, any of them can be made to throw) and a clock that is a number:
//   * which entries of the storage count: ants.rejoin.<room>.<seat> with a room by the page's own rule (1 - 32 letters, digits, - and _) and a seat 0 - 3, whose value is exactly the JSON that the game
//     page writes ({"k": 32 hex digits that are not all zero, "s": a printable server, "t": a whole number of milliseconds}: no member more or less, no other type), and nothing else;
//   * the server: an entry is for THIS site when its "s" is what the game page joins from this origin (web/shell.html, ANTS_PAGE.joinArguments: wss:// for https, ws:// otherwise, the host, /ws): the
//     same text, checked against the game page's own function on both schemes; every other text (another scheme, host, port, path, case, a trailing slash) is another server's and is not offered;
//   * the age: three hours to the millisecond is fresh (it was 24 hours: a key that outlives its match is a button for a match that is gone), a millisecond more is too old and the entry is REMOVED (the game's rule when it reads), whatever its server; a time up to a minute ahead of the clock is
//     fresh, a millisecond more is a wrong clock: not offered, and left in the storage; an entry of another server, a malformed one and a foreign item are left as they are;
//   * the newest of several (by time; on a tie the lower seat, then the room), whatever the order of the storage; a good entry among bad ones is found; and the same question for one room and one seat
//     (the game page asks it: does this browser hold the key of this very seat?), which leaves the entries of other rooms and seats where they are and still removes the old ones;
//   * a storage that refuses (a call that throws: the browser's private window, a blocked storage) offers nothing or what it can, and never throws;
//   * the words of the button and of the note (the room's code as a screen shows it: eight characters in two groups of four, "k7m2 xq9p", any other length as it is), the query that the button opens (the page's
//     other join links: /ws, the plain room code, the seat, the name always there, the shape; no create block: the room exists, and a Hello with a key of a seat never makes one) and the game page reading that
//     query as a join of this room and seat that asks for no name;
//   * THE KEY IS IN NOTHING THAT COMES OUT: not the offer (exactly room, seat, t), not the words, not the query; nothing is written to the storage.
// tests/scripts/test_web_rejoin.py runs this with node (the quick tier). The words use codeText of the block LOBBY_BEGIN .. LOBBY_END, which is loaded before the others. usage: node web_rejoin_block_check.js web/lobby.html web/shell.html     (exit 0: every check holds; failures are printed)
'use strict';
const fs = require('fs');

const lobbyPath = process.argv[2];
const shellPath = process.argv[3];
if (!lobbyPath || !shellPath) { console.log('usage: web_rejoin_block_check.js lobby.html shell.html'); process.exit(2); }

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
function between(text, begin, end, path) {
    const at = text.indexOf(begin);
    if (at < 0) throw new Error(path + ': the marker ' + begin + ' is missing');
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) throw new Error(path + ': the marker ' + end + ' is missing');
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
}

const lobbyText = fs.readFileSync(lobbyPath, 'utf8');
const shellText = fs.readFileSync(shellPath, 'utf8');
const keyBlock = between(lobbyText, 'REJOINKEY_BEGIN', 'REJOINKEY_END', lobbyPath);
const block = between(lobbyText, 'REJOIN_BEGIN', 'REJOIN_END', lobbyPath);
const lobbyBlock = between(lobbyText, 'LOBBY_BEGIN', 'LOBBY_END', lobbyPath);          // (rejoinWords shows the code as codeText does; nothing else of the block is called here)
const R = new Function(lobbyBlock + '\n' + keyBlock + '\n' + block + '\nreturn { REJOIN_PREFIX: REJOIN_PREFIX, REJOIN_MAX_AGE_MS: REJOIN_MAX_AGE_MS, REJOIN_FUTURE_MS: REJOIN_FUTURE_MS, rejoinServer: rejoinServer, rejoinParse: rejoinParse, rejoinOffer: rejoinOffer, rejoinWords: rejoinWords, rejoinQuery: rejoinQuery, codeText: codeText };')();

// the game page's own ANTS_PAGE (the same fakes as tests/scripts/web_lobby_check.js: it reads the address, the storage and two elements while it runs)
function loadShell() {
    const text = between(shellText, 'ANTS_PAGE_BEGIN', 'ANTS_PAGE_END', shellPath);
    const element = () => ({ attributes: {}, style: {}, getAttribute(n) { return this.attributes[n] === undefined ? null : this.attributes[n]; }, setAttribute(n, v) { this.attributes[n] = String(v); }, addEventListener() {}, classList: { add() {}, contains() { return false; } } });
    const win = { location: { search: '', protocol: 'https:', host: 'example.test', href: 'https://example.test/', assign() {} }, localStorage: { getItem() { return null; }, setItem() {} }, confirm() { return true; } };
    const stage = element();
    const doc = { body: element(), getElementById(id) { if (id === 'game-stage') return stage; throw new Error('the page asked for #' + id); }, querySelectorAll() { return []; } };
    return new Function('window', 'document', text + '\nreturn ANTS_PAGE;')(win, doc);
}

const NOW = 1790000000000;                                  // the clock of the checks, ms since 1970
const MIN = 60 * 1000;
const HOUR = 60 * MIN;
const DAY = 24 * HOUR;
const AGE = 3 * HOUR;                      // how long a key is offered (the game's rule: src/ants_app/rejoin_store.hpp, kRejoinMaxAgeMs)
const SERVER = 'wss://play.test/ws';
const HEX = '00112233445566778899aabbccddeeff';
const entryText = (t, server, key) => '{"k":"' + (key || HEX) + '","s":"' + (server === undefined ? SERVER : server) + '","t":' + t + '}';        // as src/ants_app/rejoin_store.cpp (value_of) writes it
const nameOf = (room, seat) => 'ants.rejoin.' + room + '.' + seat;

// A storage with the browser's calls: what is in it, what was written or removed, and what is made to throw ({ length, key, getItem: [names], removeItem: [names] })
function makeStorage(items, throwing) {
    const data = new Map(Object.entries(items || {}));
    const bad = throwing || {};
    const s = {
        calls: { getItem: [], removeItem: [], setItem: [], key: 0 },
        data,
        get length() { if (bad.length) throw new Error('storage is blocked'); return data.size; },
        key(i) { s.calls.key++; if (bad.key) throw new Error('storage is blocked'); return Array.from(data.keys())[i] === undefined ? null : Array.from(data.keys())[i]; },
        getItem(k) { s.calls.getItem.push(k); if (bad.getItem === true || (bad.getItem && bad.getItem.indexOf(k) !== -1)) throw new Error('storage is blocked'); return data.has(k) ? data.get(k) : null; },
        setItem(k, v) { s.calls.setItem.push(k); data.set(k, String(v)); },
        removeItem(k) { s.calls.removeItem.push(k); if (bad.removeItem === true || (bad.removeItem && bad.removeItem.indexOf(k) !== -1)) throw new Error('storage is blocked'); data.delete(k); },
    };
    return s;
}
const offer = (items, server, now, throwing) => { const st = makeStorage(items, throwing); return { got: R.rejoinOffer(st, now === undefined ? NOW : now, server === undefined ? SERVER : server), storage: st }; };

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the constants and the server
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
check('the entries are the game page\'s: ants.rejoin.<room>.<seat>', R.REJOIN_PREFIX === 'ants.rejoin.');
check('a key is of use for three hours to the millisecond, and a clock that was wrong by more than a minute is not trusted', R.REJOIN_MAX_AGE_MS === AGE && R.REJOIN_FUTURE_MS === MIN);
same('the server of an https page is wss://<host>/ws, of an http page ws://<host>/ws (with the port the host has)', [R.rejoinServer(true, 'beta.playants.org'), R.rejoinServer(false, '127.0.0.1:8080'), R.rejoinServer(true, 'play.test:8443')],
     ['wss://beta.playants.org/ws', 'ws://127.0.0.1:8080/ws', 'wss://play.test:8443/ws']);
{
    const P = loadShell();
    for (const [secure, host] of [[true, 'beta.playants.org'], [false, '127.0.0.1:8080'], [true, 'play.test:8443'], [false, 'localhost']]) {
        const joined = P.joinArguments('?join=/ws&room=k7m2xq9p&seat=1&name=Ann', secure, host).args;
        check('the server of an entry is the game page\'s own join of this origin (' + (secure ? 'https' : 'http') + ', ' + host + ')', joined[joined.indexOf('--join-url') + 1] === R.rejoinServer(secure, host), joined.join(' '));
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// what an entry is
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
same('an entry as the game page writes it', R.rejoinParse(nameOf('k7m2xq9p', 1), entryText(NOW - 5000)), { room: 'k7m2xq9p', seat: 1, server: SERVER, t: NOW - 5000 });
same('the parts that are read: the room, the seat 0 .. 3, the server and the time (never the key)', Object.keys(R.rejoinParse(nameOf('r', 0), entryText(5))).sort(), ['room', 'seat', 'server', 't']);
for (const seat of [0, 1, 2, 3]) check('seat ' + seat + ' is a seat', R.rejoinParse(nameOf('r', seat), entryText(5)) !== null && R.rejoinParse(nameOf('r', seat), entryText(5)).seat === seat);
for (const room of ['a', 'A', '_', '-', 'k7m2xq9p', 'k7m2-xq9p', 'x'.repeat(32), 'Room_1-B', '0']) check('the room code ' + JSON.stringify(room) + ' is one by the page\'s own rule', R.rejoinParse(nameOf(room, 0), entryText(5)) !== null);
const BAD_NAMES = ['ants.rejoin.r.4', 'ants.rejoin.r.-1', 'ants.rejoin.r.01', 'ants.rejoin.r.10', 'ants.rejoin.r.', 'ants.rejoin.r', 'ants.rejoin..0', 'ants.rejoin.a.b.0', 'ants.rejoin.' + 'x'.repeat(33) + '.0', 'ants.rejoin.r s.0', 'ants.rejoin.r/s.0',
                   'ants.rejoin.é.0', 'ants.rejoin.r.0 ', ' ants.rejoin.r.0', 'ants.rejoin.r.0.0', 'ants.rejoin.r.a', 'ants.rejoin.r.٠', 'Ants.rejoin.r.0', 'ants.rejoin2.r.0', 'ants.name', 'ants.rejoin.r.0\n', '', 'rejoin.r.0', null, undefined, 5, {}];
for (const name of BAD_NAMES) check('the storage name ' + JSON.stringify(name) + ' is no entry of the game', R.rejoinParse(name, entryText(5)) === null);
const GOOD_T = [0, 1, NOW, 999999999999999];
for (const t of GOOD_T) check('a time of ' + t + ' ms is a whole number: it parses', R.rejoinParse(nameOf('r', 0), entryText(t)) !== null);
const BAD_VALUES = [
    ['not json', 'not json'], ['empty', ''], ['null', 'null'], ['a number', '5'], ['a string', '"x"'], ['true', 'true'], ['an array', '[]'], ['an array of the members', '[' + entryText(5) + ']'], ['an empty object', '{}'], ['a truncated object', entryText(5).slice(0, -1)],
    ['text after the object', entryText(5) + ' x'], ['a member too many', '{"k":"' + HEX + '","s":"' + SERVER + '","t":5,"v":2}'], ['a member too few: no time', '{"k":"' + HEX + '","s":"' + SERVER + '"}'], ['a member too few: no server', '{"k":"' + HEX + '","t":5}'],
    ['a member too few: no key', '{"s":"' + SERVER + '","t":5}'], ['a member renamed', '{"k":"' + HEX + '","s":"' + SERVER + '","time":5}'], ['the same member twice (one is lost)', '{"k":"' + HEX + '","k":"' + HEX + '","s":"' + SERVER + '"}'],
    ['a key of 31 digits', entryText(5, SERVER, HEX.slice(1))], ['a key of 33 digits', entryText(5, SERVER, HEX + '0')], ['a key with a letter that is no hex digit', entryText(5, SERVER, 'g' + HEX.slice(1))], ['a key of zeros', entryText(5, SERVER, '0'.repeat(32))],
    ['a key with a blank', entryText(5, SERVER, ' ' + HEX.slice(1))], ['a key that is a number', '{"k":5,"s":"' + SERVER + '","t":5}'], ['a key that is null', '{"k":null,"s":"' + SERVER + '","t":5}'],
    ['an empty server', entryText(5, '')], ['a server that is a number', '{"k":"' + HEX + '","s":5,"t":5}'], ['a server that is an object', '{"k":"' + HEX + '","s":{},"t":5}'], ['a server of 256 characters', entryText(5, 'w'.repeat(256))],
    ['a server with an accent', '{"k":"' + HEX + '","s":"w\\u00e9ss://x","t":5}'], ['a server with a control character', '{"k":"' + HEX + '","s":"w\\u0001","t":5}'], ['a server with a line end', '{"k":"' + HEX + '","s":"w\\n","t":5}'],
    ['a time that is text', '{"k":"' + HEX + '","s":"' + SERVER + '","t":"5"}'], ['a time with a fraction', '{"k":"' + HEX + '","s":"' + SERVER + '","t":5.5}'], ['a negative time', '{"k":"' + HEX + '","s":"' + SERVER + '","t":-5}'],
    ['a time that is null', '{"k":"' + HEX + '","s":"' + SERVER + '","t":null}'], ['a time of 16 digits', entryText(1234567890123456)], ['a time that is huge', '{"k":"' + HEX + '","s":"' + SERVER + '","t":1e300}'],
    ['a time that is an array', '{"k":"' + HEX + '","s":"' + SERVER + '","t":[5]}'], ['a time that is a boolean', '{"k":"' + HEX + '","s":"' + SERVER + '","t":true}'], ['a member that pollutes the prototype', '{"__proto__":{"k":1},"s":"' + SERVER + '","t":5}'],
    [null, null], [undefined, undefined], [5, 5], [{}, {}],
];
for (const [what, text] of BAD_VALUES) check('a value that is ' + what + ' is no entry', R.rejoinParse(nameOf('r', 0), text) === null);
check('a server of 255 printable characters is one', R.rejoinParse(nameOf('r', 0), entryText(5, 'w'.repeat(255))) !== null);
check('the key may be written in capitals (the game reads both)', R.rejoinParse(nameOf('r', 0), entryText(5, SERVER, HEX.toUpperCase())) !== null);
check('the members may come in any order, with blanks (JSON)', R.rejoinParse(nameOf('r', 0), ' { "t" : 5 , "s" : "' + SERVER + '" , "k" : "' + HEX + '" } ') !== null);
same('a server with an escaped quote and backslash is read as the text it holds', R.rejoinParse(nameOf('r', 0), '{"k":"' + HEX + '","s":"a\\"b\\\\c","t":5}').server, 'a"b\\c');

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// which match is offered
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const one = offer({ [nameOf('k7m2xq9p', 1)]: entryText(NOW - 2000) });
    same('one fresh entry of this site is offered: room, seat and time', one.got, { room: 'k7m2xq9p', seat: 1, t: NOW - 2000 });
    same('... and exactly these three things (no key)', Object.keys(one.got).sort(), ['room', 'seat', 't']);
    check('... and nothing was written, removed or changed', one.storage.calls.setItem.length === 0 && one.storage.calls.removeItem.length === 0 && one.storage.data.size === 1);
}
check('an empty storage offers nothing', offer({}).got === null);
check('a storage with only other items offers nothing (the settings, the name, the picture)', offer({ 'ants.name': 'Ann', 'ants.aspect.v2': '16:9', 'ants-four-map': 'small', 'other': entryText(NOW) }).got === null);

// the age
{
    const exactly = offer({ [nameOf('a', 0)]: entryText(NOW - AGE) });
    same('an entry that is exactly three hours old is fresh (the game\'s rule: older than that is of no use)', exactly.got, { room: 'a', seat: 0, t: NOW - AGE });
    check('... and is not removed', exactly.storage.data.has(nameOf('a', 0)) && exactly.storage.calls.removeItem.length === 0);
    const over = offer({ [nameOf('a', 0)]: entryText(NOW - AGE - 1) });
    check('an entry a millisecond older than three hours is not offered', over.got === null);
    check('... and is REMOVED, as the game does when it reads', !over.storage.data.has(nameOf('a', 0)) && over.storage.calls.removeItem.length === 1 && over.storage.calls.removeItem[0] === nameOf('a', 0));
    const mixed = offer({ [nameOf('old', 0)]: entryText(NOW - 3 * DAY), [nameOf('new', 1)]: entryText(NOW - HOUR) });
    same('an old entry does not hide a fresh one, and goes while the fresh one stays', [mixed.got, Array.from(mixed.storage.data.keys())], [{ room: 'new', seat: 1, t: NOW - HOUR }, [nameOf('new', 1)]]);
    const oldOther = offer({ [nameOf('a', 0)]: entryText(NOW - 2 * DAY, 'wss://elsewhere.test/ws') });
    check('an old entry of another server is removed too (a key that old is of no use to any server)', oldOther.got === null && oldOther.storage.data.size === 0);
    const oldBad = offer({ [nameOf('a', 0)]: '{"k":"' + HEX + '","s":"' + SERVER + '","t":' + (NOW - 2 * DAY) + ',"v":2}', [nameOf('b', 1)]: 'junk' });
    check('what is no entry of the game is never removed, however it looks', oldBad.got === null && oldBad.storage.data.size === 2 && oldBad.storage.calls.removeItem.length === 0);
    check('a time of 0 (1970) is far too old', offer({ [nameOf('a', 0)]: entryText(0) }).got === null);
}

// the future
{
    const ahead = offer({ [nameOf('a', 0)]: entryText(NOW + MIN) });
    same('an entry a minute ahead of the clock is fresh (the clocks of two tabs, of a page and a game, differ a little)', ahead.got, { room: 'a', seat: 0, t: NOW + MIN });
    const wrong = offer({ [nameOf('a', 0)]: entryText(NOW + MIN + 1) });
    check('an entry a millisecond further ahead is a clock that was wrong: not offered', wrong.got === null);
    check('... and left in the storage (the clock may be right again: nothing is removed for it)', wrong.storage.data.has(nameOf('a', 0)) && wrong.storage.calls.removeItem.length === 0);
    check('an entry a year ahead is not offered either, and stays', (() => { const r = offer({ [nameOf('a', 0)]: entryText(NOW + 365 * DAY) }); return r.got === null && r.storage.data.size === 1; })());
    same('an entry from the future does not hide a good one', offer({ [nameOf('future', 0)]: entryText(NOW + HOUR), [nameOf('ok', 1)]: entryText(NOW - 5000) }).got, { room: 'ok', seat: 1, t: NOW - 5000 });
}

// the server
{
    for (const other of ['ws://play.test/ws', 'wss://play.test/ws/', 'wss://play.test/ws2', 'wss://play.test/', 'wss://play.test', 'wss://PLAY.test/ws', 'WSS://play.test/ws', 'wss://play.test:443/ws', 'wss://play.test:8443/ws', 'wss://other.test/ws', 'wss://play.test.evil.test/ws',
                         'wss://evil.test/play.test/ws', 'wss://play.test/ws ', ' wss://play.test/ws', 'play.test:4001', '127.0.0.1:4001', 'wss://play.test/ws/room', 'https://play.test/ws']) {
        const r = offer({ [nameOf('a', 0)]: entryText(NOW - 5000, other) });
        check('an entry of the server ' + JSON.stringify(other) + ' is another server\'s: not offered', r.got === null);
        check('... and left alone', r.storage.data.size === 1 && r.storage.calls.removeItem.length === 0 && r.storage.calls.setItem.length === 0);
    }
    check('a site on http: its own server is ws://<host>/ws (and wss:// is another one)', offer({ [nameOf('a', 0)]: entryText(NOW - 5000, 'ws://127.0.0.1:8080/ws') }, 'ws://127.0.0.1:8080/ws').got !== null && offer({ [nameOf('a', 0)]: entryText(NOW - 5000, 'wss://127.0.0.1:8080/ws') }, 'ws://127.0.0.1:8080/ws').got === null);
    same('an entry of another server does not hide this site\'s, though it is newer', offer({ [nameOf('other', 0)]: entryText(NOW - 1000, 'wss://other.test/ws'), [nameOf('mine', 2)]: entryText(NOW - 90000) }).got, { room: 'mine', seat: 2, t: NOW - 90000 });
}

// the newest of several
{
    const items = { [nameOf('first', 0)]: entryText(NOW - 5 * MIN), [nameOf('second', 3)]: entryText(NOW - 1 * MIN), [nameOf('third', 1)]: entryText(NOW - 3 * MIN), [nameOf('fourth', 2)]: entryText(NOW - 2 * HOUR) };
    same('of several the newest by time is offered', offer(items).got, { room: 'second', seat: 3, t: NOW - MIN });
    const keys = Object.keys(items);
    let agree = true;
    for (let rotation = 0; rotation < keys.length; rotation++) {                          // (the storage has no order that a page can rely on: every order gives the same answer)
        const rotated = {};
        for (let i = 0; i < keys.length; i++) rotated[keys[(i + rotation) % keys.length]] = items[keys[(i + rotation) % keys.length]];
        if (JSON.stringify(offer(rotated).got) !== JSON.stringify({ room: 'second', seat: 3, t: NOW - MIN })) agree = false;
    }
    check('... in whatever order the storage lists them', agree);
    const tie = { [nameOf('b-room', 2)]: entryText(NOW - 1000), [nameOf('a-room', 2)]: entryText(NOW - 1000), [nameOf('z-room', 1)]: entryText(NOW - 1000), [nameOf('y-room', 3)]: entryText(NOW - 1000) };
    same('on a tie the lower seat wins', offer(tie).got, { room: 'z-room', seat: 1, t: NOW - 1000 });
    delete tie[nameOf('z-room', 1)];
    same('... then, at the same seat, the room that comes first', offer(tie).got, { room: 'a-room', seat: 2, t: NOW - 1000 });
    same('two seats of one room: the one that was written last', offer({ [nameOf('r', 0)]: entryText(NOW - 20000), [nameOf('r', 1)]: entryText(NOW - 10000) }).got, { room: 'r', seat: 1, t: NOW - 10000 });
    same('a good entry among bad ones is found', offer({ 'ants.rejoin.bad.9': entryText(NOW), [nameOf('junk', 0)]: 'junk', [nameOf('nokey', 1)]: '{"s":"' + SERVER + '","t":' + NOW + '}', [nameOf('good', 2)]: entryText(NOW - 7000), 'ants.name': 'Ann' }).got, { room: 'good', seat: 2, t: NOW - 7000 });
}

// the same question for one room and one seat (the game page's name step: a reload of a game that this browser is playing holds the key of that very seat)
{
    const items = { [nameOf('room-a', 0)]: entryText(NOW - 1000), [nameOf('room-a', 2)]: entryText(NOW - 5000), [nameOf('room-b', 1)]: entryText(NOW - 9000), [nameOf('room-c', 3)]: entryText(NOW - 2000, 'wss://other.test/ws') };
    const ask = (room, seat, extra) => R.rejoinOffer(makeStorage(Object.assign({}, items, extra || {})), NOW, SERVER, room, seat);
    same('no room, no seat: the newest of all (what the front page asks)', ask(), { room: 'room-a', seat: 0, t: NOW - 1000 });
    same('a room: the newest of that room', [ask('room-a'), ask('room-b'), ask('room-c'), ask('room-none')], [{ room: 'room-a', seat: 0, t: NOW - 1000 }, { room: 'room-b', seat: 1, t: NOW - 9000 }, null, null]);
    same('a room and a seat: that seat only (seat 0 is a seat: the filter is for numbers from 0)', [ask('room-a', 2), ask('room-a', 0), ask('room-a', 1), ask('room-b', 1), ask('room-b', 0)], [{ room: 'room-a', seat: 2, t: NOW - 5000 }, { room: 'room-a', seat: 0, t: NOW - 1000 }, null, { room: 'room-b', seat: 1, t: NOW - 9000 }, null]);
    same('a seat without a room: any room\'s seat', [ask('', 1), ask(undefined, 2), ask(null, 3)], [{ room: 'room-b', seat: 1, t: NOW - 9000 }, { room: 'room-a', seat: 2, t: NOW - 5000 }, null]);
    same('no seat in the question (nothing, -1, null, NaN, a text) is any seat', [ask('room-a', undefined), ask('room-a', -1), ask('room-a', null), ask('room-a', NaN), ask('room-a', '2')].map((o) => o && o.seat), [0, 0, 0, 0, 0]);
    check('the server still counts: a room of another server is not held', ask('room-c', 3) === null && ask('room-c') === null);
    const old = makeStorage({ [nameOf('room-x', 0)]: entryText(NOW - 2 * DAY), [nameOf('room-y', 0)]: entryText(NOW - 1000) });
    same('a question about one room still removes the old entries of the others (the game does when it reads), and is answered by the room\'s own', [R.rejoinOffer(old, NOW, SERVER, 'room-y', 0), Array.from(old.data.keys())], [{ room: 'room-y', seat: 0, t: NOW - 1000 }, [nameOf('room-y', 0)]]);
    check('an entry of the room that is too old is removed, not held', R.rejoinOffer(makeStorage({ [nameOf('room-x', 0)]: entryText(NOW - AGE - 1) }), NOW, SERVER, 'room-x', 0) === null);
    check('an entry of the room from the future is not held', R.rejoinOffer(makeStorage({ [nameOf('room-x', 0)]: entryText(NOW + MIN + 1) }), NOW, SERVER, 'room-x', 0) === null);
}

// a storage that refuses
{
    check('a storage whose length throws offers nothing (and does not throw)', offer({ [nameOf('a', 0)]: entryText(NOW) }, SERVER, NOW, { length: true }).got === null);
    check('a storage whose key() throws offers nothing', offer({ [nameOf('a', 0)]: entryText(NOW) }, SERVER, NOW, { key: true }).got === null);
    same('a storage whose getItem throws for one name: the others are offered', offer({ [nameOf('blocked', 0)]: entryText(NOW - 1000), [nameOf('open', 1)]: entryText(NOW - 9000) }, SERVER, NOW, { getItem: [nameOf('blocked', 0)] }).got, { room: 'open', seat: 1, t: NOW - 9000 });
    check('a storage whose getItem throws for everything offers nothing', offer({ [nameOf('a', 0)]: entryText(NOW) }, SERVER, NOW, { getItem: true }).got === null);
    const stuck = offer({ [nameOf('old', 0)]: entryText(NOW - 2 * DAY), [nameOf('new', 1)]: entryText(NOW - 5000) }, SERVER, NOW, { removeItem: true });
    same('a storage that will not remove: the old entry is not offered, the fresh one is, and nothing throws', stuck.got, { room: 'new', seat: 1, t: NOW - 5000 });
    check('a storage that is null offers nothing', R.rejoinOffer(null, NOW, SERVER) === null && R.rejoinOffer(undefined, NOW, SERVER) === null);
    check('a storage with no calls at all (an object) offers nothing', R.rejoinOffer({}, NOW, SERVER) === null);
    check('a key() that answers null or a number for a name is skipped', R.rejoinOffer({ length: 3, key(i) { return [null, 5, nameOf('a', 0)][i]; }, getItem() { return entryText(NOW - 1000); }, removeItem() {} }, NOW, SERVER) !== null);
}

// the words and the address
same('the button says Rejoin your match and the room, the code in two groups of four as a screen shows it', R.rejoinWords({ room: 'k7m2xq9p', seat: 1, t: 1 }).button, 'Rejoin your match (k7m2 xq9p)');
same('the note under it is one line that says the match still runs and what to do', R.rejoinWords({ room: 'k7m2xq9p', seat: 1, t: 1 }).note, 'Your match in room k7m2 xq9p is still running: go back to your seat.');
same('a code of another length is shown as it is (a room that was made some other way)', [R.rejoinWords({ room: 'tiny-2p-abc', seat: 1, t: 1 }).button, R.rejoinWords({ room: 'k7m2xq9', seat: 1, t: 1 }).button, R.rejoinWords({ room: 'k7m2xq9pz', seat: 1, t: 1 }).note],
     ['Rejoin your match (tiny-2p-abc)', 'Rejoin your match (k7m2xq9)', 'Your match in room k7m2xq9pz is still running: go back to your seat.']);
check('the words show the code as codeText does (the same blank, nothing else added), whatever the room', ['a', 'Room_1-B', 'x'.repeat(32), 'k7m2xq9p', 'abcdefgh'].every((room) => R.rejoinWords({ room, seat: 0, t: 1 }).button === 'Rejoin your match (' + R.codeText(room) + ')' && R.rejoinWords({ room, seat: 0, t: 1 }).note === 'Your match in room ' + R.codeText(room) + ' is still running: go back to your seat.'));
for (const room of ['a', 'Room_1-B', 'x'.repeat(32), 'k7m2xq9p']) {
    const w = R.rejoinWords({ room, seat: 0, t: 1 });
    check('the words for the room ' + room + ' hold the room (as codeText shows it) and no markup', w.button.indexOf(R.codeText(room)) !== -1 && w.note.indexOf(R.codeText(room)) !== -1 && !/[<>&"]/.test(w.button + w.note));
}
same('the query: the door, the room, the seat, the name and the shape (the plain code, and no create block)', R.rejoinQuery({ room: 'k7m2xq9p', seat: 1, t: 1 }, 'Ann', '16:9'), '?join=/ws&room=k7m2xq9p&seat=1&name=Ann&aspect=16:9');
check('the query keeps the plain code whatever the words show (no blank in it)', ['k7m2xq9p', 'abcdefgh', 'Room_1-B'].every((room) => R.rejoinQuery({ room, seat: 0, t: 1 }, '', '16:9').indexOf('room=' + room + '&') !== -1 && R.rejoinQuery({ room, seat: 0, t: 1 }, '', '16:9').indexOf('%20') === -1 && !/roommap|roomseats|roomteams|roomleaderstart/.test(R.rejoinQuery({ room, seat: 0, t: 1 }, '', '16:9'))));
same('the query: the name is encoded, whatever it holds', R.rejoinQuery({ room: 'r', seat: 3, t: 1 }, 'A&B=c d#e', '4:3'), '?join=/ws&room=r&seat=3&name=A%26B%3Dc%20d%23e&aspect=4:3');
same('the query: the room is encoded too, whatever the function is given (the page only offers rooms that need no encoding)', R.rejoinQuery({ room: 'a b&c=d#e', seat: 1, t: 1 }, 'Ann', '16:9'), '?join=/ws&room=a%20b%26c%3Dd%23e&seat=1&name=Ann&aspect=16:9');
same('the query: no name is a name that was chosen (empty), so the game page does not ask for one', R.rejoinQuery({ room: 'r', seat: 0, t: 1 }, '', '16:9'), '?join=/ws&room=r&seat=0&name=&aspect=16:9');
same('the query: a shape that is not 4:3 is 16:9', [R.rejoinQuery({ room: 'r', seat: 0, t: 1 }, '', 'wide'), R.rejoinQuery({ room: 'r', seat: 0, t: 1 }, '', undefined), R.rejoinQuery({ room: 'r', seat: 0, t: 1 }, '', '4:3 ')].map((q) => q.slice(q.indexOf('&aspect='))), ['&aspect=16:9', '&aspect=16:9', '&aspect=16:9']);
{
    const P = loadShell();
    for (const [name, shape] of [['Ann', '16:9'], ['', '4:3'], ['Maria Elena', '16:9'], ['A&B=c', '4:3']]) {
        const q = R.rejoinQuery({ room: 'k7m2xq9p', seat: 2, t: 1 }, name, shape);
        const got = P.joinArguments(q, true, 'play.test');
        const args = got.args;
        const at = (flag) => args[args.indexOf(flag) + 1];
        check('the game page reads the query as a join of this room and seat (' + JSON.stringify(name) + ', ' + shape + ')', at('--join-url') === SERVER && at('--room') === 'k7m2xq9p' && at('--seat') === '2' && args.indexOf('--room-map') === -1 && args.indexOf('--room-seats') === -1 && (name ? at('--name') === name : args.indexOf('--name') === -1), args.join(' '));
        check('... that asks for no name (the page chose it, empty or not)', P.asksForName(q) === false);
        check('... and carries the shape (the game page reads ?aspect=)', new URLSearchParams(q).get('aspect') === shape);
    }
}

// the key is in nothing that comes out
{
    const keys = ['00112233445566778899aabbccddeeff', 'f0e1d2c3b4a5968778695a4b3c2d1e0f', 'abcdefabcdefabcdefabcdefabcdef01'];
    for (const key of keys) {
        const st = makeStorage({ [nameOf('k7m2xq9p', 1)]: entryText(NOW - 1000, SERVER, key) });
        const got = R.rejoinOffer(st, NOW, SERVER);
        const words = R.rejoinWords(got);
        const query = R.rejoinQuery(got, 'Ann', '16:9');
        const everything = (JSON.stringify(got) + JSON.stringify(words) + query).toLowerCase();
        check('the key ' + key.slice(0, 8) + '... is not in the offer, the words or the query (nor a part of it)', everything.indexOf(key) === -1 && everything.indexOf(key.slice(0, 8)) === -1 && everything.indexOf(key.slice(-8)) === -1);
        check('... and nothing was written to the storage', st.calls.setItem.length === 0);
    }
    const st = makeStorage({ [nameOf('r', 0)]: entryText(NOW - 1000, SERVER, 'ABCDEFABCDEFABCDEFABCDEFABCDEF01') });
    const got = R.rejoinOffer(st, NOW, SERVER);
    check('a key in capitals is not in them either', (JSON.stringify(got) + JSON.stringify(R.rejoinWords(got)) + R.rejoinQuery(got, '', '16:9')).toLowerCase().indexOf('abcdefabcdef') === -1);
}

// the game page carries the same text of the shared block (it asks the same question of the same storage)
check('the keys block is the same text in the front page and the game page', between(shellText, 'REJOINKEY_BEGIN', 'REJOINKEY_END', shellPath) === keyBlock);

console.log('web rejoin block: ' + checks + ' checks, ' + failures + ' failures');
process.exit(failures === 0 ? 0 : 1);
