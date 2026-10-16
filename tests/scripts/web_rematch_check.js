// Runs the pages' side of the rematch (web/front/rematch.js; docs/SERVER.md "Lobby rooms", docs/PLAY_IN_BROWSER.md "After a match") without a browser: what the game says when the match of a server's room
// is over (the lines of tests/data/match_over.txt, which test_network_app N5.99 holds the game's own text to), who won in words, the key of the seat in the game's storage, the hand-over to the front page
// (this tab's session storage) and what the front page reads back, the people of the game page's strip (back in the room, or still on the results screen: the same rule as the server's and as
// web/front/lobby_rules.js modelOf), the front page's strip and its link to the match's stats and replay, and both strips built and driven with a small fake of the DOM (a name is TEXT: the fake throws when
// markup is assigned), the game page's with the real client of web/front/lobby_net.js on a fake socket (the game's Hello with the key, the seat of that key or nothing, the quiet hand-over).
//   - every exported function of rematch.js must be called by this check (a function that no table reaches is a failure).
// usage: node web_rematch_check.js web/front/rematch.js web/front/lobby_net.js tests/data/match_over.txt web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');

const [rematchPath, netPath, goldenPath, lobbyPath] = process.argv.slice(2);
if (!rematchPath || !netPath || !goldenPath || !lobbyPath) { console.log('usage: web_rematch_check.js rematch.js lobby_net.js match_over.txt lobby.html'); process.exit(2); }
process.on('uncaughtException', (e) => { console.log('FAIL the check stopped on an error: ' + (e && e.stack || e)); process.exit(1); });
process.on('unhandledRejection', (e) => { console.log('FAIL the check stopped on an error: ' + (e && e.stack || e)); process.exit(1); });

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
const flush = () => new Promise((resolve) => setImmediate(resolve));

const RAW = require(path.resolve(rematchPath));
const N = require(path.resolve(netPath));
const called = Object.create(null);
const R = {};
Object.keys(RAW).forEach((k) => {
    R[k] = typeof RAW[k] === 'function' ? function () { called[k] = (called[k] || 0) + 1; return RAW[k].apply(this, arguments); } : RAW[k];
});
const golden = {};
fs.readFileSync(goldenPath, 'utf8').split('\n').forEach((line) => {
    if (!line || line[0] === '#') return;
    const blank = line.indexOf(' ');
    golden[line.slice(0, blank)] = line.slice(blank + 1);
});
// tests/data/replay_players.txt (beside match_over.txt): the names of a match by seat and the labels that the server's public list of replays gives its players (test_server S3.202 holds them to the server)
const listed = [];
fs.readFileSync(path.join(path.dirname(goldenPath), 'replay_players.txt'), 'utf8').split('\n').forEach((line) => {
    if (!line || line[0] === '#') return;
    const [names, labels] = line.split(' => ');
    listed.push({ names: names.split('|'), labels: labels.split('|') });
});

// ---- a fake storage, a fake DOM -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function storage(items) {
    const s = { items: Object.assign({}, items || {}) };
    s.getItem = (k) => (k in s.items ? s.items[k] : null);
    s.setItem = (k, v) => { s.items[k] = String(v); };
    s.removeItem = (k) => { delete s.items[k]; };
    return s;
}
const broken = { getItem() { throw new Error('blocked'); }, setItem() { throw new Error('blocked'); }, removeItem() { throw new Error('blocked'); } };
function node(tag) {
    const n = { tag, children: [], attrs: {}, listeners: {}, className: '', textValue: '', hidden: false, parentNode: null, style: {}, href: '', target: '', rel: '', type: '', id: '' };
    for (const bad of ['innerHTML', 'outerHTML']) Object.defineProperty(n, bad, { get() { return ''; }, set() { throw new Error(bad + ' was assigned: a name must be text only'); } });
    n.insertAdjacentHTML = () => { throw new Error('insertAdjacentHTML was called'); };
    Object.defineProperty(n, 'textContent', {
        get() { return n.children.length ? n.children.map((c) => c.textContent).join('') : n.textValue; },
        set(v) { n.children.forEach((c) => { c.parentNode = null; }); n.children = []; n.textValue = String(v); }
    });
    n.appendChild = (c) => { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = n; n.children.push(c); return c; };
    n.removeChild = (c) => { const i = n.children.indexOf(c); if (i >= 0) { n.children.splice(i, 1); c.parentNode = null; } return c; };
    n.insertBefore = (c, ref) => { if (c.parentNode) c.parentNode.removeChild(c); const i = n.children.indexOf(ref); c.parentNode = n; n.children.splice(i < 0 ? n.children.length : i, 0, c); return c; };
    n.setAttribute = (k, v) => { n.attrs[k] = String(v); };
    n.getAttribute = (k) => (k in n.attrs ? n.attrs[k] : null);
    n.addEventListener = (type, fn) => { (n.listeners[type] = n.listeners[type] || []).push(fn); };
    n.fire = (type) => (n.listeners[type] || []).forEach((fn) => fn({ type, preventDefault() {} }));
    return n;
}
function textNode(text) { const t = node('#text'); t.textValue = String(text); return t; }
const doc = { createElement: node, createTextNode: textNode };
const find = (n, pred) => { if (pred(n)) return n; for (const c of n.children) { const r = find(c, pred); if (r) return r; } return null; };
const byClass = (n, cls) => find(n, (x) => (' ' + x.className + ' ').indexOf(' ' + cls + ' ') !== -1);
const all = (n, pred, out) => { out = out || []; if (pred(n)) out.push(n); n.children.forEach((c) => all(c, pred, out)); return out; };

// ---- a fake browser of sockets and timers (as tests/scripts/web_lobby_client_check.js), and the server's messages ----------------------------------------------------------------------------------------
function world() {
    const w = { sockets: [], timers: [], clock: 1000000, nextId: 1 };
    w.WebSocket = function FakeSocket(url) { this.url = url; this.sent = []; this.closed = false; this.readyState = 0; w.sockets.push(this); };
    w.WebSocket.prototype.send = function (bytes) { if (this.closed || this.readyState !== 1) throw new Error('send on a socket that is not open'); this.sent.push(Uint8Array.from(bytes)); };
    w.WebSocket.prototype.close = function () { this.closed = true; };
    w.WebSocket.prototype.open = function () { this.readyState = 1; if (this.onopen) this.onopen(); };
    w.WebSocket.prototype.receive = function (bytes) { if (this.onmessage) this.onmessage({ data: bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) }); };
    w.setTimeout = (fn, ms) => { const id = w.nextId++; w.timers.push({ id, at: w.clock + ms, fn }); return id; };
    w.clearTimeout = (id) => { w.timers = w.timers.filter((t) => t.id !== id); };
    w.now = () => w.clock;
    w.advance = (ms) => {
        const until = w.clock + ms;
        for (;;) {
            const due = w.timers.filter((t) => t.at <= until).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            w.timers = w.timers.filter((t) => t !== due);
            w.clock = Math.max(w.clock, due.at);
            due.fn();
        }
        w.clock = until;
    };
    w.last = () => w.sockets[w.sockets.length - 1];
    return w;
}
const seq = (first) => Uint8Array.from(Array.from({ length: 16 }, (_, i) => (first + i) & 255));
const KEY = seq(0xA0);
const KEYHEX = N.hexOf(KEY);
function welcome(player, key, flags) { return Uint8Array.from([2, player, 4].concat(Array.from(key), [flags || 0])); }
function reject(reason) { return Uint8Array.from([3, reason]); }
function str8(w, s) { w.push(s.length); for (let i = 0; i < s.length; i++) w.push(s.charCodeAt(i)); }
const E = N.SLOT.Empty, C = N.SLOT.Client;
// seats: four of [state, name]; o: { you, leader, starting, plan, inGame, platforms }
function roomBytes(seats, o) {
    o = o || {};
    const w = [12];
    seats.forEach((s, i) => { w.push(s[0]); str8(w, s[1]); w.push(20, 0, o.platforms ? o.platforms[i] : 0); });
    str8(w, 'TREASURE.LVL');
    w.push(0, o.you === undefined ? 0 : o.you, o.leader === undefined ? 0 : o.leader, 255, 255, o.starting ? 7 : 3);
    for (const k of (o.plan || [0, 0, 0, 0])) w.push(k);
    w.push(o.inGame || 0, 0);
    return Uint8Array.from(w);
}
const room = (seats, o) => { const m = N.decode(roomBytes(seats, o)); if (m === null) throw new Error('the check built a Room message that the codec refuses'); return m; };

async function main() {
    // ---- 0. what the library offers ---------------------------------------------------------------------------------------------------------------------------------------------------------------
    same('the library exports exactly these names (a new one needs a table here)', Object.keys(RAW).sort(), ['ASK_AFTER_MS', 'BACK_KEY', 'COLOURS', 'DOTS', 'LOBBY_KEY', 'backLine', 'chipText', 'chipsOf', 'findReplay', 'forgetBack',
        'frontStrip', 'gamePage', 'handOver', 'parseOver', 'platformOf', 'readBack', 'seatKey', 'statsHref', 'winnerWords']);
    same('the two keys of this tab\'s session storage', [R.BACK_KEY, R.LOBBY_KEY], ['ants.back', 'ants.lobby']);
    same('the results screen asks for its seat two seconds after the game said that the match is over (the server ends the match in a pass of its own)', R.ASK_AFTER_MS, 2000);
    check('the front page keeps its room under the same key (web/lobby.html LOBBY_KEY): the hand-over is the front page\'s own entry', fs.readFileSync(lobbyPath, 'utf8').indexOf("var LOBBY_KEY = 'ants.lobby';") !== -1);
    same('the colours of the seats and the dots of the strips (picture R1: green, red, blue, black)', [R.COLOURS, R.DOTS], [['Green', 'Red', 'Blue', 'Black'], ['#2e8f56', '#b43a4c', '#5a56c8', '#4a4a50']]);

    // ---- 1. what the game says (tests/data/match_over.txt: the game's own text, test_network_app N5.99) ---------------------------------------------------------------------------------------------------
    same('the golden file has the three cases', Object.keys(golden).sort(), ['alliance-187', 'tie-with-a-bot', 'two-players']);
    const two = R.parseOver(golden['two-players']);
    same('two players: the room, the seat, the map, the mode, the four names and the rows, best first', two,
        { room: 'k7m2xq', seat: 1, map: 'TREASURE.LVL', mode: 0, names: ['Maple', 'Sam', '', ''], rows: [{ seats: [0], score: 2040, won: true }, { seats: [1], score: 420, won: false }] });
    same('an alliance in 187: one row for the pair, a name with a quote and one with a backslash, a map of the players', R.parseOver(golden['alliance-187']),
        { room: 'r2d2c3', seat: 3, map: 'Bomb Island.lvl', mode: 1, names: ['Maple', 'Pi"p', 'Juniper', 'a\\b'], rows: [{ seats: [0, 2], score: 1100, won: true }, { seats: [1], score: 900, won: false }, { seats: [3], score: 100, won: false }] });
    same('a tie with a bot: both rows at the top won, a seat that did not play has no name', R.parseOver(golden['tie-with-a-bot']),
        { room: 't1e000', seat: 0, map: 'SMALL.LVL', mode: 0, names: ['Maple', 'Sam', '', 'Pip (bot, Hard)'], rows: [{ seats: [0], score: 1500, won: true }, { seats: [1], score: 1500, won: true }, { seats: [3], score: 300, won: false }] });
    {
        const base = JSON.parse(golden['two-players']);
        const variant = (change) => { const v = JSON.parse(JSON.stringify(base)); change(v); return JSON.stringify(v); };
        [
            ['no JSON', 'nope'], ['nothing', ''], ['a cut text', golden['two-players'].slice(0, 40)], ['a list', '[]'], ['a number', '42'], ['null', 'null'],
            ['no room', variant((v) => { delete v.room; })], ['an empty room', variant((v) => { v.room = ''; })], ['a room with a blank', variant((v) => { v.room = 'a b'; })], ['a room of 33', variant((v) => { v.room = 'x'.repeat(33); })],
            ['a seat of -1', variant((v) => { v.seat = -1; })], ['a seat of 4', variant((v) => { v.seat = 4; })], ['a seat of 1.5', variant((v) => { v.seat = 1.5; })], ['a seat in words', variant((v) => { v.seat = '1'; })],
            ['a map without .lvl', variant((v) => { v.map = 'TREASURE'; })], ['a map with a folder', variant((v) => { v.map = '../x.lvl'; })], ['a map with a slash', variant((v) => { v.map = 'a/b.lvl'; })],
            ['a hidden map', variant((v) => { v.map = '.xyz.lvl'; })], ['a map of 65', variant((v) => { v.map = 'x'.repeat(61) + '.lvl'; })], ['a map with an accent', variant((v) => { v.map = 'café.lvl'; })],
            ['mode 2', variant((v) => { v.mode = 2; })], ['mode in words', variant((v) => { v.mode = '0'; })], ['no mode', variant((v) => { delete v.mode; })],
            ['three names', variant((v) => { v.names.pop(); })], ['five names', variant((v) => { v.names.push(''); })], ['a name of 33', variant((v) => { v.names[0] = 'x'.repeat(33); })],
            ['a name with an accent', variant((v) => { v.names[0] = 'José'; })], ['a name that is a number', variant((v) => { v.names[0] = 5; })],
            ['rows that are no list', variant((v) => { v.rows = {}; })], ['five rows', variant((v) => { v.rows = [0, 1, 2, 3, 0].map((s) => ({ seats: [s], score: 1, won: false })); })],
            ['a row of no seat', variant((v) => { v.rows[0].seats = []; })], ['a row of three seats', variant((v) => { v.rows[0].seats = [0, 1, 2]; })], ['a row of seat 4', variant((v) => { v.rows[0].seats = [4]; })],
            ['a score of 1.5', variant((v) => { v.rows[0].score = 1.5; })], ['a score in words', variant((v) => { v.rows[0].score = '10'; })], ['won in words', variant((v) => { v.rows[0].won = 'true'; })], ['a row that is null', variant((v) => { v.rows[0] = null; })]
        ].forEach(([label, text]) => same('parseOver refuses ' + label, R.parseOver(text), null));
        same('parseOver takes a match without rows, and leaves out what it does not know', [R.parseOver(variant((v) => { v.rows = []; })).rows, R.parseOver(variant((v) => { v.extra = 1; })).extra], [[], undefined]);
        check('parseOver gives lists of its own (a change of the text\'s lists changes nothing of what it gave)', (() => { const o = R.parseOver(golden['two-players']); return o.names !== base.names && o.rows[0].seats !== base.rows[0].seats; })());
    }

    // ---- 2. who won, in words ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    same('the winner of each golden line, as the front page\'s strip says it', ['two-players', 'alliance-187', 'tie-with-a-bot'].map((k) => R.winnerWords(R.parseOver(golden[k]))),
        ['Maple won with 2,040.', 'Maple and Juniper won.', 'Maple and Sam tied with 1,500.']);
    {
        const over = (rows, names, mode) => ({ room: 'abc', seat: 0, map: 'TINY.LVL', mode: mode || 0, names: names || ['A', 'B', 'C', 'D'], rows });
        same('no row, or no row that won: no words', [R.winnerWords(over([])), R.winnerWords(over([{ seats: [0], score: 5, won: false }]))], ['', '']);
        same('a seat without a name is its colour', [R.winnerWords(over([{ seats: [0], score: 10, won: true }], ['', '', '', ''])), R.winnerWords(over([{ seats: [3], score: 10, won: true }], ['', '', '', '']))], ['Green won with 10.', 'Black won with 10.']);
        same('the score in groups of three, a negative one too', [R.winnerWords(over([{ seats: [1], score: 1234567, won: true }])), R.winnerWords(over([{ seats: [1], score: -1500, won: true }])), R.winnerWords(over([{ seats: [1], score: 0, won: true }]))],
            ['B won with 1,234,567.', 'B won with -1,500.', 'B won with 0.']);
        same('two alliances that tied: four names, one "and"', R.winnerWords(over([{ seats: [0, 2], score: 5, won: true }, { seats: [1, 3], score: 5, won: true }])), 'A, C, B and D tied with 5.');
        same('187 says no score', R.winnerWords(over([{ seats: [2], score: 9, won: true }], null, 1)), 'C won.');
    }

    // ---- 3. the key of the seat, as the game keeps it (ants.rejoin.<room>.<seat>) --------------------------------------------------------------------------------------------------------------------
    {
        const entry = (k) => JSON.stringify({ k: k, s: 'wss://beta.example/ws', t: 1 });
        const st = storage({ 'ants.rejoin.k7m2xq.1': entry(KEYHEX.toUpperCase()), 'ants.rejoin.k7m2xq.2': entry('0'.repeat(32)), 'ants.rejoin.k7m2xq.3': entry(KEYHEX.slice(0, 31)), 'ants.rejoin.abc.0': '{nope',
            'ants.rejoin.abc.1': JSON.stringify({ s: 'x' }), 'ants.rejoin.abc.2': JSON.stringify({ k: 5 }), 'ants.rejoin.abc.3': entry('g'.repeat(32)) });
        same('the key of the seat, in lower case', R.seatKey(st, 'k7m2xq', 1), KEYHEX);
        same('none: a zero key, a short one, no entry, another seat or room, broken JSON, no key, a key that is a number or no hex', [R.seatKey(st, 'k7m2xq', 2), R.seatKey(st, 'k7m2xq', 3), R.seatKey(st, 'k7m2xq', 0), R.seatKey(st, 'k7m2xq9', 1),
            R.seatKey(st, 'abc', 0), R.seatKey(st, 'abc', 1), R.seatKey(st, 'abc', 2), R.seatKey(st, 'abc', 3)], [null, null, null, null, null, null, null, null]);
        same('none from a storage that is not there or that throws', [R.seatKey(null, 'k7m2xq', 1), R.seatKey(broken, 'k7m2xq', 1)], [null, null]);
    }

    // ---- 4. the hand-over to the front page, and what the front page reads back ------------------------------------------------------------------------------------------------------------------------
    {
        const T = 1760600000000;
        const s = storage();
        check('the hand-over is written', R.handOver(s, two, KEYHEX, 'Sam', T + 0.6) === true);
        same('ants.lobby is the front page\'s own entry: the code, the key of the seat, the name, not the room\'s maker', JSON.parse(s.items['ants.lobby']), { c: 'k7m2xq', k: KEYHEX, n: 'Sam', o: 0 });
        same('ants.back is the match: the code, the time, the map, the four names and the winner in words', JSON.parse(s.items['ants.back']), { c: 'k7m2xq', t: T, m: 'TREASURE.LVL', p: ['Maple', 'Sam', '', ''], w: 'Maple won with 2,040.' });
        same('the front page reads it back: the people are the names that played, the names are the four by seat', R.readBack(s, 'k7m2xq', T + 5000),
            { code: 'k7m2xq', t: T, map: 'TREASURE.LVL', people: ['Maple', 'Sam'], names: ['Maple', 'Sam', '', ''], winner: 'Maple won with 2,040.' });
        same('... for its own room only, for three hours, and not from the future (a minute of clocks that disagree is let through)',
            [R.readBack(s, 'other1', T), R.readBack(s, 'k7m2xq', T + 3 * 3600 * 1000) !== null, R.readBack(s, 'k7m2xq', T + 3 * 3600 * 1000 + 1), R.readBack(s, 'k7m2xq', T - 60000) !== null, R.readBack(s, 'k7m2xq', T - 60001)],
            [null, true, null, true, null]);
        const damaged = (change) => { const v = JSON.parse(s.items['ants.back']); change(v); const d = storage({ 'ants.back': JSON.stringify(v) }); return R.readBack(d, 'k7m2xq', T); };
        same('a damaged entry is none: a time that is no whole number, a map that is no map, three names, a name that is no text, a winner that is too long or no text',
            [damaged((v) => { v.t = 'x'; }), damaged((v) => { v.m = '../a.lvl'; }), damaged((v) => { v.p.pop(); }), damaged((v) => { v.p[0] = 7; }), damaged((v) => { v.w = 'x'.repeat(161); }), damaged((v) => { delete v.w; })],
            [null, null, null, null, null, null]);
        same('... and so is broken JSON, nothing, or a storage that throws', [R.readBack(storage({ 'ants.back': '{' }), 'k7m2xq', T), R.readBack(storage(), 'k7m2xq', T), R.readBack(broken, 'k7m2xq', T), R.readBack(null, 'k7m2xq', T)], [null, null, null, null]);
        same('a name that the game cannot show is not handed over (the front page asks the room\'s)', (() => { const t = storage(); R.handOver(t, two, KEYHEX, 'José', T); return JSON.parse(t.items['ants.lobby']).n; })(), '');
        check('a storage that refuses: the hand-over says so', R.handOver(broken, two, KEYHEX, 'Sam', T) === false);
        {
            const half = storage();
            half.setItem = (k, v) => { if (k === 'ants.back') throw new Error('full'); half.items[k] = String(v); };
            check('a storage that takes the room and not the match: the hand-over is made (the front page takes the seat back, it only has no strip)', R.handOver(half, two, KEYHEX, 'Sam', T) === true
                && JSON.parse(half.items['ants.lobby']).k === KEYHEX && half.items['ants.back'] === undefined && R.readBack(half, 'k7m2xq', T) === null);
        }
        R.forgetBack(s);
        same('forgetBack takes the match away and leaves the room', [s.items['ants.back'], typeof s.items['ants.lobby']], [undefined, 'string']);
        R.forgetBack(broken);
        check('forgetBack of a storage that throws does not throw', true);
    }

    // ---- 5. the people of the game page's strip -------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const P = (name) => [C, name], O = [E, ''];
        // Maple (Green) leads, back (a page); Sam (Red) is this results screen (a game in a browser: the seat is kept for him); Juniper (Blue) is on hers (no platform said); Pip (Black) plays on the desktop and joined by the code
        const r = room([P('Maple'), P('Sam'), P('Juniper'), P('Pip')], { you: 1, leader: 0, inGame: 14, platforms: [0x13, 0x13, 0, 0x01] });
        same('the chips: the host and who is here, who is still on the results screen; a game of the desktop is here', R.chipsOf(r), [
            { seat: 0, name: 'Maple', dot: '#2e8f56', host: true, here: true }, { seat: 1, name: 'Sam', dot: '#b43a4c', host: false, here: false },
            { seat: 2, name: 'Juniper', dot: '#5a56c8', host: false, here: false }, { seat: 3, name: 'Pip', dot: '#4a4a50', host: false, here: true }]);
        same('the words of each chip (picture R1)', R.chipsOf(r).map(R.chipText), ['Maple · host · here', 'Sam · on the results screen', 'Juniper · on the results screen', 'Pip · here']);
        same('START is pressed: every game is a game, nobody is on the results screen', R.chipsOf(room([P('Maple'), P('Sam'), O, O], { you: 1, leader: 0, inGame: 3, starting: true, platforms: [0x13, 0x13, 0, 0] })).map((c) => c.here), [true, true]);
        same('an open colour, a planned bot and nobody have no chip; no room is no chip', [R.chipsOf(room([P('Maple'), O, O, O], { plan: [0, 2, 4, 0] })).length, R.chipsOf(null).length, R.chipsOf({}).length], [1, 0, 0]);
        same('nobody leads (the room before anybody is back): no "host"', R.chipsOf(room([P('Maple'), P('Sam'), O, O], { you: 1, leader: 255, inGame: 3 })).map(R.chipText), ['Maple · on the results screen', 'Sam · on the results screen']);
    }

    // ---- 6. the front page's strip, its words and its link ----------------------------------------------------------------------------------------------------------------------------------------------
    same('the strip of the front page (picture R2)', [R.backLine({ winner: 'Maple won with 2,040.' }), R.backLine({ winner: '' })], [
        { lead: 'Back from the match.', text: 'Maple won with 2,040. Same room, same colours, same map.' }, { lead: 'Back from the match.', text: 'Same room, same colours, same map.' }]);
    {
        const T = 1760600000000;
        check('the labels of the public list of replays: the golden file has its cases', listed.length >= 3 && listed.every((l) => l.names.length === 4 && l.labels.length >= 1));
        listed.forEach((l, i) => {
            const back = { t: T, map: 'TINY.LVL', people: l.names.filter((n) => n !== ''), names: l.names };
            const entry = (players) => ({ file: 'ants-TINY-20261016-071320Z.antsrep', ended: Math.floor(T / 1000) - 20, map: 'TINY.LVL', players, finished: true });
            same('the match of line ' + (i + 1) + ' of replay_players.txt is found by the labels that the server gives its players (' + l.labels.join(', ') + ')', R.findReplay({ replays: [entry(l.labels)] }, back), 'ants-TINY-20261016-071320Z');
            same('... and not when one of its seats is missing from the list, or listed under a bare name', l.labels.map((x, k) => R.findReplay({ replays: [entry(l.labels.filter((_, j) => j !== k))] }, back))
                .concat(R.findReplay({ replays: [entry(l.names.filter((n) => n !== ''))] }, back)), l.labels.map(() => '').concat(''));
        });
        const back = { t: T, map: 'TREASURE.LVL', people: ['Maple', 'Sam', 'Player 3'], names: ['Maple', 'Sam', 'Player 3', ''] };
        const entry = (o) => Object.assign({ file: 'ants-TREASURE-20261016-071320Z.antsrep', ended: Math.floor(T / 1000) - 20, map: 'TREASURE.LVL', players: ['Green (Maple)', 'Red (Sam)', 'Blue', 'Black (Pip (bot, Hard))'], finished: true }, o);
        same('the newest finished match of the map, ended within ten minutes of the hand-over, with every seat of it', R.findReplay({ replays: [entry({ file: 'ants-TREASURE-20261016-071322Z-2.antsrep' }), entry()] }, back), 'ants-TREASURE-20261016-071322Z-2');
        same('not: another map, a match that was cut short, one that ended more than ten minutes before or after, one without Sam, Sam in another colour, a file of another shape, an entry that is none',
            [{ map: 'SMALL.LVL' }, { finished: false }, { ended: Math.floor(T / 1000) - 601 }, { ended: Math.floor(T / 1000) + 601 }, { players: ['Green (Maple)', 'Blue'] }, { players: ['Green (Maple)', 'Blue (Sam)', 'Blue'] },
                { file: '../ants.antsrep' }, { file: 7 }].map((o) => R.findReplay({ replays: [entry(o)] }, back)).concat(R.findReplay({ replays: [null] }, back)),
            ['', '', '', '', '', '', '', '', '']);
        same('a match of nobody named finds nothing', R.findReplay({ replays: [entry()] }, { t: T, map: 'TREASURE.LVL', people: [], names: ['', '', '', ''] }), '');
        same('a list that is none: no match (the link goes to the list of matches)', [R.findReplay(null, back), R.findReplay({}, back), R.findReplay({ replays: 'x' }, back)], ['', '', '']);
        same('the link: the match\'s page, else the list of matches', [R.statsHref('ants-TREASURE-20261016-071320Z'), R.statsHref('')], ['/match.html?id=ants-TREASURE-20261016-071320Z', '/matches.html']);
    }

    // ---- 7. what the results screen's Hello tells of the browser (protocol 15; web/lobby.html's rule) -----------------------------------------------------------------------------------------------------
    same('the platform byte: a browser on each system', ['Windows NT 10.0', 'Macintosh; Intel Mac OS X', 'X11; Linux x86_64', 'Linux; Android 14', 'iPhone; CPU iPhone OS 17', 'CrOS-like'].map((ua) => R.platformOf({ userAgent: ua, platform: '' })),
        [N.PLATFORM_BROWSER | N.OS.Windows, N.PLATFORM_BROWSER | N.OS.Macos, N.PLATFORM_BROWSER | N.OS.Linux, N.PLATFORM_BROWSER | N.OS.Android, N.PLATFORM_BROWSER | N.OS.Ios, N.PLATFORM_BROWSER | N.OS.Other]);
    same('... and without a navigator', R.platformOf(null), N.PLATFORM_BROWSER | N.OS.Other);

    // ---- 8. the game page, built and driven ---------------------------------------------------------------------------------------------------------------------------------------------------------------
    function page(o) {
        o = o || {};
        const w = world();
        const win = { navigator: { userAgent: 'X11; Linux x86_64' }, resized: 0, Event: function (type) { this.type = type; }, listeners: {} };
        win.top = o.framed ? {} : win;
        win.addEventListener = (type, fn) => { (win.listeners[type] = win.listeners[type] || []).push(fn); };
        win.fire = (type) => (win.listeners[type] || []).forEach((fn) => fn({ type }));
        win.dispatchEvent = (e) => { if (e.type === 'resize') win.resized++; };
        const loc = { search: o.search === undefined ? '?join=/ws&room=k7m2xq&seat=1&name=Sam&aspect=16:9' : o.search, protocol: 'https:', host: 'beta.example', assigned: [] };
        loc.assign = (u) => loc.assigned.push(u);
        const local = storage(o.noKey ? {} : { 'ants.rejoin.k7m2xq.1': JSON.stringify({ k: KEYHEX, s: 'wss://beta.example/ws', t: 1 }) });
        const session = storage();
        const host = node('section');
        host.hidden = true;
        const env = { win, doc, Net: N, loc, local, session, host, now: () => 1760600000000, later: w.setTimeout, clientOptions: { WebSocket: w.WebSocket, setTimeout: w.setTimeout, clearTimeout: w.clearTimeout, now: w.now } };
        if (o.noHost) delete env.host;
        const gp = R.gamePage(env);
        return { w, win, loc, local, session, host, gp };
    }
    {
        same('no game page without a join of the game server, or with a join elsewhere, or without the strip\'s element', ['', '?room=k7m2xq', '?join=/elsewhere&room=k7m2xq', '?join=//evil.example/ws', '?join=/ws/../x', '?join=/ws//x'].map((q) => page({ search: q }).gp)
            .concat(page({ noHost: true }).gp), [null, null, null, null, null, null, null]);
        check('... and it does not listen for the game', page({ search: '' }).win.antsMatchOver === undefined);
        const p = page();
        check('a game page listens for the game\'s word', typeof p.win.antsMatchOver === 'function' && p.gp !== null);
        p.win.antsMatchOver('nope');
        p.win.antsMatchOver(golden['two-players'].replace('"seat":1', '"seat":2'));                // (no key of that seat in the game's storage)
        p.w.advance(R.ASK_AFTER_MS);
        check('a word that is no match, or a seat whose key the game does not keep: nothing is opened', p.w.sockets.length === 0 && p.host.hidden);
        p.win.antsMatchOver(golden['two-players']);
        p.w.advance(1999);
        check('the match is over: nothing is asked for two seconds (the server ends the match in a pass of its own)', p.w.sockets.length === 0);
        p.w.advance(1);
        check('... then one link to the server of the page', p.w.sockets.length === 1 && p.w.last().url === 'wss://beta.example/ws');
        p.win.antsMatchOver(golden['two-players']);
        check('... once', p.w.sockets.length === 1);
        p.w.last().open();
        same('the Hello: a game\'s, with the key of the seat, the name of the address, the browser\'s platform, no block', N.hexOf(p.w.last().sent[0]),
            N.hexOf(N.encodeHello({ name: 'Sam', room: 'k7m2xq', key: KEY, kind: 'game', platform: N.PLATFORM_BROWSER | N.OS.Linux })));
        check('nothing shows before the room has taken the seat', p.host.hidden && p.host.children.length === 0);
        p.w.last().receive(welcome(1, KEY, 0));
        check('the seat of the key is ours: the strip shows, and the page lays the picture out again', !p.host.hidden && p.win.resized === 1);
        same('the strip\'s words (picture R1)', [byClass(p.host, 'rmt-title').textContent, byClass(p.host, 'rmt-text').textContent, byClass(p.host, 'btn').textContent, byClass(p.host, 'rmt-note').textContent], [
            'The match is over', 'Everybody can go back to the room together: same host, same colours, same map. Your seat is kept for you.', 'Back to the lobby', 'The game’s own “Leave Game” still goes to the menu.']);
        same('the button is a button of the page\'s look', [byClass(p.host, 'btn').tag, byClass(p.host, 'btn').type, byClass(p.host, 'btn').className], ['button', 'button', 'btn big']);
        p.w.last().receive(roomBytes([[C, 'Maple'], [C, 'Sam'], [C, '<b>Juniper</b>'], [E, '']], { you: 1, leader: 0, inGame: 6, platforms: [0x13, 0x13, 0x16, 0] }));
        const chips = all(p.host, (x) => (' ' + x.className + ' ').indexOf(' rmt-chip ') !== -1);
        same('the chips: who is back, who is on the results screen (a name is text)', chips.map((c) => [c.className, c.textContent, c.children[0].style.background]), [
            ['rmt-chip', 'Maple · host · here', '#2e8f56'], ['rmt-chip away', 'Sam · on the results screen', '#b43a4c'], ['rmt-chip away', '<b>Juniper</b> · on the results screen', '#5a56c8']]);
        p.w.last().receive(roomBytes([[C, 'Maple'], [C, 'Sam'], [C, '<b>Juniper</b>'], [E, '']], { you: 1, leader: 0, inGame: 2, platforms: [0x13, 0x13, 0x13, 0] }));
        same('... and they follow the room (Juniper is back)', all(p.host, (x) => (' ' + x.className + ' ').indexOf(' rmt-chip ') !== -1).map((c) => c.textContent), ['Maple · host · here', 'Sam · on the results screen', '<b>Juniper</b> · here']);
        const socket = p.w.last();
        const sentBefore = socket.sent.length;
        byClass(p.host, 'btn').fire('click');
        check('"Back to the lobby": the link goes quietly (no Leave: the seat is kept for the front page) and is not made again', socket.closed && socket.sent.length === sentBefore && p.gp.state.client.status === 'closed');
        same('... the front page of this tab is handed the room, the key, the name the room shows, and the match', [JSON.parse(p.session.items['ants.lobby']), JSON.parse(p.session.items['ants.back'])],
            [{ c: 'k7m2xq', k: KEYHEX, n: 'Sam', o: 0 }, { c: 'k7m2xq', t: 1760600000000, m: 'TREASURE.LVL', p: ['Maple', 'Sam', '', ''], w: 'Maple won with 2,040.' }]);
        same('... and the tab goes to the room\'s address', p.loc.assigned, ['/?room=k7m2xq']);
        p.w.advance(600000);
        check('... where nothing of this page comes back', p.w.sockets.length === 1);
        p.win.fire('pagehide');
        check('... and the page that goes says nothing more (the seat is the front page\'s)', socket.sent.length === sentBefore && p.w.sockets.length === 1);
    }
    {
        const p = page();
        p.win.antsMatchOver(golden['two-players']);
        p.w.advance(R.ASK_AFTER_MS);
        p.w.last().open();
        p.w.last().receive(welcome(1, KEY, 0));
        const socket = p.w.last();
        p.win.fire('pagehide');
        check('the page goes some other way (Leave Game, a closed tab, a reload): the seat is given up with a Leave, and the link closes', socket.closed && N.hexOf(socket.sent[socket.sent.length - 1]) === N.hexOf(N.encodeLeave())
            && p.gp.state.client.status === 'closed' && p.session.items['ants.lobby'] === undefined);
        const q = page();
        q.win.fire('pagehide');
        check('a page whose match is not over (no results screen) says nothing when it goes', q.w.sockets.length === 0);
    }
    {
        const p = page();
        p.win.antsMatchOver(golden['two-players']);
        p.w.advance(R.ASK_AFTER_MS);
        p.w.last().open();
        p.w.last().receive(welcome(1, seq(0x10), 0));
        const said = p.w.last().sent;
        check('a Welcome to another seat (the room took us as a newcomer: the seat of the key is gone) is left at once, with a Leave, and shows nothing', p.host.hidden && p.w.last().closed && said.length === 2
            && N.hexOf(said[1]) === N.hexOf(N.encodeLeave()) && p.gp.state.client.status === 'refused', said.map((b) => N.hexOf(b)).join(' ') + ' ' + p.gp.state.client.status);
        p.w.advance(600000);
        check('... and is not tried again', p.w.sockets.length === 1);
    }
    {
        const p = page();
        p.win.antsMatchOver(golden['two-players']);
        p.w.advance(R.ASK_AFTER_MS);
        const first = p.w.last();
        first.open();
        first.receive(welcome(1, KEY, 1));                                                          // the match's Welcome (kWelcomeRejoin: only a match that still runs gives it)
        check('a Welcome of the match (the server had not ended it yet) is let go of without a word, and nothing shows', first.closed && first.sent.length === 1 && p.host.hidden);
        p.w.advance(R.ASK_AFTER_MS - 1);
        check('... the seat is asked for again two seconds later, not sooner', p.w.sockets.length === 1);
        p.w.advance(1);
        check('... then', p.w.sockets.length === 2);
        p.w.last().open();
        same('... with the same Hello', N.hexOf(p.w.last().sent[0]), N.hexOf(first.sent[0]));
        p.w.last().receive(welcome(1, KEY, 0));
        check('... and the lobby of the rematch takes it: the strip shows', !p.host.hidden && !p.w.last().closed);
        const q = page();
        q.win.antsMatchOver(golden['two-players']);
        for (let i = 0; i < 4; i++) {
            q.w.advance(R.ASK_AFTER_MS);
            if (q.w.sockets.length === i + 1 && !q.w.last().closed) { q.w.last().open(); q.w.last().receive(welcome(1, KEY, 1)); }
        }
        q.w.advance(600000);
        check('the match answers every time: three tries in all, then nothing more, and nothing shows', q.w.sockets.length === 3 && q.w.sockets.every((x) => x.closed && x.sent.length === 1) && q.host.hidden);
    }
    {
        const p = page();
        p.session.setItem = () => { throw new Error('blocked'); };
        p.win.antsMatchOver(golden['two-players']);
        p.w.advance(R.ASK_AFTER_MS);
        const socket = p.w.last();
        socket.open();
        socket.receive(welcome(1, KEY, 0));
        const sent = socket.sent.length;
        byClass(p.host, 'btn').fire('click');
        check('a storage that refuses the hand-over: the tab stays (no key in any address), the link stays (the seat stays held from here)', p.loc.assigned.length === 0 && !socket.closed && socket.sent.length === sent);
        check('... and the strip says why, with no button', byClass(p.host, 'rmt-go').hidden && p.win.resized === 2
            && byClass(p.host, 'rmt-text').textContent === 'This browser blocks the storage that the front page needs, so your seat cannot be taken back from here.');
        byClass(p.host, 'btn').fire('click');
        check('... a second press does nothing', p.loc.assigned.length === 0 && !socket.closed);
        p.win.fire('pagehide');
        check('... and the page that goes gives the seat up (a Leave)', socket.closed && N.hexOf(socket.sent[socket.sent.length - 1]) === N.hexOf(N.encodeLeave()));
    }
    {
        const p = page();
        p.win.antsMatchOver(golden['two-players']);
        p.w.advance(R.ASK_AFTER_MS);
        p.w.last().open();
        p.w.last().receive(reject(N.REJECT.NoSuchRoom));
        p.w.advance(600000);
        check('no room of that code (no rematch, or it is gone): nothing shows, nothing is made', p.host.hidden && p.w.sockets.length === 1 && p.w.sockets[0].sent.length === 1);
    }
    {
        const p = page();
        p.win.antsMatchOver(golden['two-players']);
        p.w.advance(R.ASK_AFTER_MS);
        p.w.last().open();
        p.w.last().receive(welcome(1, KEY, 0));
        p.w.last().receive(roomBytes([[C, 'Maple'], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 0, inGame: 2, platforms: [0x13, 0x13, 0, 0] }));
        p.w.last().receive(reject(N.REJECT.Kicked));
        check('the host took this player out (or the seat waited three hours): the strip says so, with no button and no chips', !p.host.hidden && byClass(p.host, 'rmt-go').hidden && byClass(p.host, 'rmt-who').children.length === 0
            && byClass(p.host, 'rmt-text').textContent === 'Your seat in the room is not kept any more. The game’s own “Leave Game” goes to the menu.');
    }
    {
        const p = page();
        p.win.antsMatchOver(golden['two-players']);
        p.w.advance(R.ASK_AFTER_MS);
        p.w.last().open();
        p.w.last().receive(welcome(1, KEY, 0));
        p.w.last().receive(reject(N.REJECT.Superseded));
        check('another window took the seat (the front page of another tab): the strip goes, and the page is laid out again', p.host.hidden && p.win.resized === 2);
    }
    {
        const p = page({ framed: true });
        p.win.antsMatchOver(golden['two-players']);
        p.w.advance(R.ASK_AFTER_MS);
        check('a game in a frame of another page has no strip', p.w.sockets.length === 0);
        const q = page({ search: '?join=/ws&room=k7m2xq&seat=1' });
        q.win.antsMatchOver(golden['two-players']);
        q.w.advance(R.ASK_AFTER_MS);
        q.w.last().open();
        same('an address without a name: the Hello says the match\'s name of the seat', N.hexOf(q.w.last().sent[0]), N.hexOf(N.encodeHello({ name: 'Sam', room: 'k7m2xq', key: KEY, kind: 'game', platform: N.PLATFORM_BROWSER | N.OS.Linux })));
        q.w.last().receive(welcome(1, KEY, 0));
        q.gp.back();
        same('... and the hand-over names the seat as the match did (no Room message yet)', JSON.parse(q.session.items['ants.lobby']).n, 'Sam');
        const r = page({ noKey: true });
        r.win.antsMatchOver(golden['two-players']);
        r.w.advance(R.ASK_AFTER_MS);
        check('a game that keeps no key of the seat (a page that cannot store it): nothing opens', r.w.sockets.length === 0);
    }

    // ---- 9. the front page's strip, built and driven -----------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const T = 1760600000000;
        const body = node('body');
        const screen = node('div');
        body.appendChild(screen);
        const later = [];
        const fetched = [];
        let answer = () => Promise.resolve({ ok: true, json: () => Promise.resolve({ replays: [] }) });
        const strip = R.frontStrip({ doc, before: screen, fetch: (u, i) => { fetched.push([u, i]); return answer(); }, later: (fn, ms) => later.push([fn, ms]) });
        const back = { code: 'k7m2xq', t: T, map: 'TREASURE.LVL', people: ['Maple', 'Sam'], names: ['Maple', 'Sam', '', ''], winner: 'Maple won with 2,040.' };
        strip.show(back);
        const bar = body.children[0];
        check('the strip goes at the top of the page, above the frame', body.children.length === 2 && bar !== screen && body.children[1] === screen && bar.className === 'backbar' && !bar.hidden);
        same('it is a status that says what happened, with a link (picture R2)', [bar.attrs.role, bar.id, byClass(bar, 'backbar-text').children[0].tag, byClass(bar, 'backbar-text').textContent], ['status', 'backbar', 'b',
            'Back from the match. Maple won with 2,040. Same room, same colours, same map.']);
        const link = find(bar, (x) => x.tag === 'a');
        same('the link opens the matches in a new tab while the match is looked up', [link.textContent, link.href, link.target, link.rel], ['Stats and replay', '/matches.html', '_blank', 'noopener noreferrer']);
        same('the public list of replays is asked, with no cookies and no cache', fetched, [['/replays', { cache: 'no-store', credentials: 'omit' }]]);
        await flush(); await flush();
        same('not in the list yet: asked again in four seconds', later.map((l) => l[1]), [4000]);
        answer = () => Promise.resolve({ ok: true, json: () => Promise.resolve({ replays: [{ file: 'ants-TREASURE-20261016-071320Z.antsrep', ended: Math.floor(T / 1000) + 10, map: 'TREASURE.LVL', players: ['Green (Maple)', 'Red (Sam)'], finished: true }] }) });
        later.shift()[0]();
        await flush(); await flush();
        same('found: the link opens the match\'s page', link.href, '/match.html?id=ants-TREASURE-20261016-071320Z');
        check('... and nothing more is asked', fetched.length === 2 && later.length === 0);
        answer = () => Promise.reject(new Error('offline'));
        strip.show(back);
        check('shown again: the same strip (one), a new look', body.children.length === 2 && body.children[0] === bar && fetched.length === 3 && find(bar, (x) => x.tag === 'a').href === '/matches.html');
        await flush(); await flush();
        later.shift()[0](); await flush(); await flush();
        later.shift()[0](); await flush(); await flush();
        check('a list that cannot be had is asked three times in all, then the link stays on the matches', fetched.length === 5 && later.length === 0 && find(bar, (x) => x.tag === 'a').href === '/matches.html');
        let release = null;
        answer = () => new Promise((resolve) => { release = () => resolve({ ok: true, json: () => Promise.resolve({ replays: [{ file: 'ants-TREASURE-20261016-071320Z.antsrep', ended: Math.floor(T / 1000), map: 'TREASURE.LVL', players: ['Green (Maple)', 'Red (Sam)'], finished: true }] }) }); });
        strip.show(back);
        strip.hide();
        release();
        await flush(); await flush();
        check('hidden: the strip goes, and an answer that comes after it changes nothing', bar.hidden && find(bar, (x) => x.tag === 'a').href === '/matches.html');
        const quiet = R.frontStrip({ doc, before: screen, fetch: null, later: () => { throw new Error('nothing to wait for'); } });
        quiet.show(back);
        check('a browser without fetch: the strip and the link to the matches', body.children.length === 3 && find(body.children[1], (x) => x.tag === 'a').href === '/matches.html');
    }

    for (const name of Object.keys(RAW)) if (typeof RAW[name] === 'function') check('the check called ' + name + '()', called[name] > 0);
    console.log('rematch.js: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
}
main();
