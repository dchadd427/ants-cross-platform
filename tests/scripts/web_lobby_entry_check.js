// The entry that the lobby page writes for its seat (web/front/lobby_net.js, rejoinEntry: ants.rejoin.<room>.<seat> = {"k": key, "s": server, "t": ms}) is the entry that the GAME page reads
// (web/shell.html, the blocks REJOINKEY and NAMEGATE, run here as they stand): the game page that a lobby page goes to on START finds the key of the seat in this browser's storage, so it asks no name
// (the name was chosen in the lobby) and takes the seat over with the key (the game itself reads the same entry, src/ants_app/rejoin_store.cpp: the text of the server must be what its --join-url
// is, which is what the game page's own joinArguments builds from the address of the page). The proof is on both schemes, with the controls that must fail: another scheme, host or path, another room
// or seat, a key that three hours have passed over, a link that has no key for it (the name step is asked, as ever), a key that is all zero.
// usage: node web_lobby_entry_check.js web/front/lobby_net.js web/shell.html       exit 0: every check holds; the failures are printed
'use strict';
const fs = require('fs');
const path = require('path');

const [modulePath, shellPath] = process.argv.slice(2);
if (!modulePath || !shellPath) { console.log('usage: web_lobby_entry_check.js lobby_net.js shell.html'); process.exit(2); }
const N = require(path.resolve(modulePath));

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function between(text, begin, end) {
    const at = text.indexOf(begin);
    if (at < 0) throw new Error(shellPath + ': the marker ' + begin + ' is missing');
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) throw new Error(shellPath + ': the marker ' + end + ' is missing');
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
}

// the game page's own code, as web_name_check.js loads it
const shellText = fs.readFileSync(shellPath, 'utf8');
const code = ['ANTS_PAGE', 'NAME', 'REJOINKEY', 'NAMEGATE'].map((m) => between(shellText, m + '_BEGIN', m + '_END')).join('\n')
    + '\nreturn { P: ANTS_PAGE, holdsThisSeat: holdsThisSeat };';
const shell = new Function('window', 'document', code)({ location: { search: '' }, localStorage: null }, {
    getElementById() { return { setAttribute() {}, classList: { add() {} } }; },
    querySelectorAll() { return []; },
});
const P = shell.P;

const storageOf = (items) => {
    const data = Object.assign({}, items);
    return { data, get length() { return Object.keys(data).length; }, key(i) { const k = Object.keys(data); return i < k.length ? k[i] : null; }, getItem(k) { return k in data ? data[k] : null; }, removeItem(k) { delete data[k]; } };
};
const KEY = Uint8Array.from([0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0]);
const NOW = 1791470000000;
const ROOM = 'k7m2xq';

for (const secure of [false, true]) {
    const host = 'play.example.test:8080';
    const scheme = secure ? 'wss' : 'ws';
    const label = scheme + ': ';
    const server = scheme + '://' + host + '/ws';                                       // (what the lobby page connects to, and writes as the server of the seat)
    const link = (room, seat) => '?join=/ws&room=' + room + '&seat=' + seat;           // (the game link that a lobby page goes to on START: the room and the seat, no create block, no name, no key)
    const argsOf = (search) => P.joinArguments(search, secure, host).args;
    const entry = N.rejoinEntry(ROOM, 1, KEY, server, NOW - 5000);
    check(label + 'the lobby page makes an entry for its seat', entry !== null && entry.name === 'ants.rejoin.' + ROOM + '.1');
    check(label + 'the game page joins the server that the lobby page named in the entry (the same text)', argsOf(link(ROOM, 1)).join(' ').indexOf('--join-url ' + server) >= 0, argsOf(link(ROOM, 1)).join(' '));
    const holds = (search, items, now) => shell.holdsThisSeat(search, argsOf(search), storageOf(items), now === undefined ? NOW : now);
    const mine = { [entry.name]: entry.text };
    check(label + 'a link to that room and seat asks for a name when the browser holds no key', P.asksForName(link(ROOM, 1)) === true && holds(link(ROOM, 1), {}) === false);
    check(label + '... and with the lobby page\'s entry in the storage the game page holds the seat: it asks no name and takes the seat over with the key', holds(link(ROOM, 1), mine) === true);
    check(label + '... also from a link with the room only (any seat of the room that the browser holds)', holds('?join=/ws&room=' + ROOM, mine) === true);
    check(label + 'a link to another room asks', holds(link('abc123', 1), mine) === false);
    check(label + 'a link to another seat of the room asks', holds(link(ROOM, 2), mine) === false);
    const other = N.rejoinEntry(ROOM, 1, KEY, (secure ? 'ws' : 'wss') + '://' + host + '/ws', NOW - 5000);
    check(label + 'an entry of the other scheme is another server\'s: it does not count', holds(link(ROOM, 1), { [other.name]: other.text }) === false);
    const elsewhere = N.rejoinEntry(ROOM, 1, KEY, scheme + '://other.example.test/ws', NOW - 5000);
    check(label + 'an entry of another host does not count', holds(link(ROOM, 1), { [elsewhere.name]: elsewhere.text }) === false);
    const slash = N.rejoinEntry(ROOM, 1, KEY, server + '/', NOW - 5000);
    check(label + 'an entry whose server has a slash more does not count (the text must be the game\'s own)', holds(link(ROOM, 1), { [slash.name]: slash.text }) === false);
    const old = N.rejoinEntry(ROOM, 1, KEY, server, NOW - 3 * 3600 * 1000 - 1000);
    check(label + 'an entry that three hours have passed over does not count', holds(link(ROOM, 1), { [old.name]: old.text }) === false);
    const edge = N.rejoinEntry(ROOM, 1, KEY, server, NOW - 3 * 3600 * 1000);
    check(label + 'one of three hours to the millisecond does', holds(link(ROOM, 1), { [edge.name]: edge.text }) === true);
    check(label + 'an entry for the same seat of two rooms: the one for the link\'s room counts', holds(link(ROOM, 1), Object.assign({}, mine, { 'ants.rejoin.zzz999.1': entry.text })) === true);
    check(label + 'the key is nowhere in the arguments of the game page\'s join (it finds the key in the storage by itself)', argsOf(link(ROOM, 1)).join(' ').indexOf(N.hexOf(KEY)) < 0);
}
check('a seat the page cannot hold has no entry (a seat of 4, a zero key, a room that is no code)', N.rejoinEntry(ROOM, 4, KEY, 'ws://h/ws', NOW) === null && N.rejoinEntry(ROOM, 1, new Uint8Array(16), 'ws://h/ws', NOW) === null && N.rejoinEntry('a b', 1, KEY, 'ws://h/ws', NOW) === null);

console.log('the lobby page\'s entry and the game page\'s reader: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
