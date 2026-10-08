// Runs the lobby page's codec of network protocol 16 (web/front/lobby_net.js) without a browser, against the bytes that the C++ encoders wrote (tests/data/lobby_messages.txt, which
// tests/test_net/test_lockstep.cpp N2.111 keeps true): every message a page sends is made from its fields and must be those bytes, every message a page hears is decoded to the fields
// that the C++ decoder reads from them, the guard of a colour move is the C++ seating_hash of the Room message, and what the C++ decoders refuse (every truncation, every extra byte, every
// value out of its range) is refused here too. usage: node web_lobby_net_check.js web/front/lobby_net.js tests/data/lobby_messages.txt     (exit 0: every check holds; failures are printed)
'use strict';
const fs = require('fs');
const path = require('path');

const modulePath = process.argv[2];
const goldenPath = process.argv[3];
if (!modulePath || !goldenPath) { console.log('usage: web_lobby_net_check.js lobby_net.js lobby_messages.txt'); process.exit(2); }
const N = require(path.resolve(modulePath));

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function same(label, got, want) {
    const a = JSON.stringify(got);
    const b = JSON.stringify(want);
    check(label, a === b, 'got ' + a + ', want ' + b);
}
const hex = (bytes) => N.hexOf(bytes);
const seq = (first) => Array.from({ length: 16 }, (_, i) => (first + i) & 255);
const arr = (u8) => Array.from(u8);

// ---- the golden file ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

const golden = {};                 // name -> { kind, hex }
const hashes = {};                 // room name -> the C++ seating_hash (8 hex digits)
for (const raw of fs.readFileSync(goldenPath, 'utf8').split('\n')) {
    const line = raw.trim();
    if (line === '' || line[0] === '#') continue;
    const parts = line.split(/\s+/);
    check('golden line has three words: ' + line.slice(0, 30), parts.length === 3);
    if (parts[0] === 'hash') hashes[parts[1]] = parts[2];
    else golden[parts[1]] = { kind: parts[0], hex: parts[2] };
}
const names = Object.keys(golden);
check('the golden file has messages of every kind', ['hello', 'leave', 'pong', 'ping', 'seatmove', 'plan', 'name', 'remove', 'startrequest', 'chat', 'welcome', 'reject', 'room', 'other'].every((k) => names.some((n) => golden[n].kind === k)));
const bytesOf = (name) => N.bytesOfHex(golden[name].hex);

// ---- what a page sends: made from the fields, the same bytes ---------------------------------------------------------------------------------------------------------------------------------------

const sends = {
    'page-new-room': () => N.encodeHello({ name: 'Priya', room: 'k7m2xq', create: { map: 'TREASURE.LVL' } }),
    'page-with-key': () => N.encodeHello({ name: 'Sam', room: 'abc234', key: Uint8Array.from(seq(1)), platform: N.PLATFORM_BROWSER | N.OS.Linux, create: { map: '' } }),
    'page-no-block': () => N.encodeHello({ name: '', room: 'xx' }),
    'leave': () => N.encodeLeave(),
    'pong': () => N.encodePong(0x01020304, 0x0A0B0C0D),
    'ping': () => N.encodePing(0xFFFFFFFF, 7),
    'move-1-to-3': () => N.encodeSeatMove(1, 3, 0xDEADBEEF),
    'move-3-to-0': () => N.encodeSeatMove(3, 0, 1),
    'plan-ffa': () => N.encodePlan('ISLANDS.LVL', [N.PLAN.Open, N.PLAN.Easy, N.PLAN.Hard, N.PLAN.Nobody], 255, 255),
    'plan-teams-keep-map': () => N.encodePlan('', [N.PLAN.Open, N.PLAN.Medium, N.PLAN.Open, N.PLAN.Medium], 0, 1),
    'plan-teams-2-3': () => N.encodePlan('TREASURE.LVL', [N.PLAN.Nobody, N.PLAN.Nobody, N.PLAN.Open, N.PLAN.Open], 2, 3),
    'name-plain': () => N.encodeName('Priya'),
    'name-32-chars-with-spaces': () => N.encodeName('Sam the Great, 3rd of his name!!'),
    'name-one-char': () => N.encodeName('Z'),
    'remove-2': () => N.encodeRemove(2, 1),
    'remove-0': () => N.encodeRemove(0, 0xFFFFFFFF),
    'start-teams': () => N.encodeStartRequest([N.FILL.None, N.FILL.Easy, N.FILL.Medium, N.FILL.Hard], 0, 1),
    'start-ffa-nobody': () => N.encodeStartRequest([0, 0, 0, 0], 255, 255),
    'chat-plain': () => N.encodeChat(0, false, 'hello there')
};
for (const name of Object.keys(sends)) {
    check('golden has ' + name, golden[name] !== undefined);
    if (golden[name]) same('sends ' + name, hex(sends[name]()), golden[name].hex);
}
check('no golden message of a page is left unchecked (but a game\'s Hello, which a page never makes)',
    names.filter((n) => ['hello', 'leave', 'pong', 'seatmove', 'plan', 'name', 'remove', 'startrequest'].includes(golden[n].kind) && sends[n] === undefined).join() === 'game-joins');

// fill levels of a plan: Open and Nobody are none
same('fillOfPlan', N.fillOfPlan([N.PLAN.Open, N.PLAN.Easy, N.PLAN.Medium, N.PLAN.Hard]), [0, 1, 2, 3]);
same('fillOfPlan nobody', N.fillOfPlan([N.PLAN.Nobody, N.PLAN.Open, N.PLAN.Nobody, N.PLAN.Hard]), [0, 0, 0, 3]);

// a long name is cut to the byte that str8 holds, a name with a character beyond Latin-1 never reaches the wire as a wrong byte count
check('encodeName cuts at 255 bytes at most', N.encodeName('x'.repeat(300)).length === 2 + 255);

// ---- what a page hears: the fields that the C++ decoder reads ----------------------------------------------------------------------------------------------------------------------------------------

const slot = (state, name, rtt, platform) => ({ state, name, rtt, platform });
const E = N.SLOT.Empty, C = N.SLOT.Client, B = N.SLOT.Bot;
const roomWant = {
    'room-lobby-waiting': {
        slots: [slot(C, 'Priya', 12, 0), slot(C, 'Sam', 40, 0x12), slot(B, 'Bot (Medium)', 0, 0), slot(E, '', 0xFFFF, 0)], map: 'TREASURE.LVL', fog: false, you: 1, leader: 0, teamA: 255, teamB: 255,
        flags: 3, plan: [0, 0, 2, 0], inGame: 0, lobby: true, starting: false, leaderStarts: true
    },
    'room-lobby-starting': {
        slots: [slot(C, 'Priya', 12, 0), slot(C, 'Sam', 40, 0), slot(C, 'Juniper', 0xFFFF, 0), slot(B, 'Bot (Hard)', 0, 0)], map: 'ISLANDS.LVL', fog: false, you: 2, leader: 1, teamA: 0, teamB: 2,
        flags: 7, plan: [0, 0, 0, 3], inGame: 5, lobby: true, starting: true, leaderStarts: true
    },
    'room-lobby-alone-no-map': {
        slots: [slot(C, 'Priya', 12, 0), slot(E, '', 0xFFFF, 0), slot(E, '', 0xFFFF, 0), slot(E, '', 0xFFFF, 0)], map: '', fog: false, you: 0, leader: 0, teamA: 255, teamB: 255,
        flags: 3, plan: [0, 4, 1, 0], inGame: 0, lobby: true, starting: false, leaderStarts: true
    },
    'room-plain-not-a-lobby': {
        slots: [slot(C, 'A', 0xFFFF, 0), slot(C, 'B', 5, 0), slot(C, 'C', 6, 0), slot(C, 'D', 7, 0)], map: 'SMALL.LVL', fog: false, you: 255, leader: 255, teamA: 0, teamB: 1,
        flags: 0, plan: [0, 0, 0, 0], inGame: 0, lobby: false, starting: false, leaderStarts: false
    }
};
for (const name of Object.keys(roomWant)) {
    const got = N.decode(bytesOf(name));
    check('decodes ' + name, got !== null && got.type === 'room');
    if (got) {
        const want = Object.assign({ type: 'room' }, roomWant[name]);
        same('room fields ' + name, got, want);
        check('hash of ' + name + ' is the C++ seating_hash', hashes[name] !== undefined && (N.seatingHash(got) >>> 0).toString(16).padStart(8, '0') === hashes[name], hex(new Uint8Array([N.seatingHash(got) & 255])));
    }
}
same('welcome-created', N.decode(bytesOf('welcome-created')), { type: 'welcome', player: 0, players: 4, key: Uint8Array.from(seq(0x11)), flags: 2, created: true, rejoin: false });
same('welcome-joined', N.decode(bytesOf('welcome-joined')), { type: 'welcome', player: 2, players: 4, key: Uint8Array.from(seq(0xA0)), flags: 0, created: false, rejoin: false });
same('welcome-rejoin', N.decode(bytesOf('welcome-rejoin')), { type: 'welcome', player: 3, players: 4, key: Uint8Array.from(seq(0x40)), flags: 1, created: false, rejoin: true });
const rejectNames = ['full', 'version', 'match-running', 'kicked', 'bad-request', 'no-such-room', 'dropped', 'rejoin-failed', 'superseded'];
rejectNames.forEach((n, i) => same('reject ' + n, N.decode(bytesOf('reject-' + n)), { type: 'reject', reason: i + 1 }));
same('reject names are the REJECT table', rejectNames.map((n, i) => i + 1), [N.REJECT.Full, N.REJECT.VersionMismatch, N.REJECT.MatchRunning, N.REJECT.Kicked, N.REJECT.BadRequest, N.REJECT.NoSuchRoom, N.REJECT.Dropped, N.REJECT.RejoinFailed, N.REJECT.Superseded]);
same('ping from the server', N.decode(bytesOf('ping-from-server')), { type: 'ping', nonce: 12345, sentMs: 99999 });
same('pong decodes as a pong', N.decode(bytesOf('pong')), { type: 'pong', nonce: 0x01020304, sentMs: 0x0A0B0C0D });
same('chat notice of the room', N.decode(bytesOf('chat-notice-from-room')), { type: 'chat', sender: 255, team: false, text: 'Priya\'s game did not come in time.' });
same('chat of a player', N.decode(bytesOf('chat-plain')), { type: 'chat', sender: 0, team: false, text: 'hello there' });
check('the server sentence of the back-off fits a chat line', (N.decode(bytesOf('chat-notice-fits')) || {}).text === 'The match could not start a few times in a row: try again in a minute.');
same('begin is a message that a page does not use', N.decode(bytesOf('begin')), { type: 'other', code: 15 });
check('welcome of a game\'s Hello is not in the golden file of a page', golden['game-joins'] !== undefined);

// ---- what the C++ decoders refuse, the page refuses ---------------------------------------------------------------------------------------------------------------------------------------------

for (const name of names) {
    const kind = golden[name].kind;
    if (!['welcome', 'reject', 'room', 'ping', 'pong'].includes(kind) && name !== 'chat-notice-from-room' && name !== 'chat-plain') continue;
    const b = bytesOf(name);
    check('decodes ' + name, N.decode(b) !== null);
    let accepted = -1;
    for (let n = 0; n < b.length && accepted < 0; n++) {
        if (N.decode(b.subarray(0, n)) !== null) accepted = n;
    }
    check('refuses ' + name + ' cut at every length', accepted < 0, 'accepted at ' + accepted + ' bytes');
    const longer = new Uint8Array(b.length + 1);
    longer.set(b);
    check('refuses ' + name + ' with a byte more', N.decode(longer) === null);
}
check('an empty message is refused', N.decode(new Uint8Array(0)) === null);
check('type 0 is refused', N.decode(Uint8Array.from([0])) === null);
check('type 35 is refused', N.decode(Uint8Array.from([35, 1, 2])) === null);
check('type 255 is refused', N.decode(Uint8Array.from([255])) === null);
check('a bare reject is refused', N.decode(Uint8Array.from([3])) === null && N.decode(Uint8Array.from([3, 0])) === null && N.decode(Uint8Array.from([3, 10])) === null);
check('an ArrayBuffer is read like the bytes', N.decode(bytesOf('reject-full').buffer.slice(0)) !== null);

function mutate(name, at, value) { const b = Uint8Array.from(bytesOf(name)); b[at] = value; return b; }
// welcome: a seat beyond 3, fewer than 2 or more than 4 players, a flag above 2, a flag without a key
check('welcome player 4 is refused', N.decode(mutate('welcome-created', 1, 4)) === null);
check('welcome players 1 is refused', N.decode(mutate('welcome-created', 2, 1)) === null);
check('welcome players 5 is refused', N.decode(mutate('welcome-created', 2, 5)) === null);
check('welcome flags 3 is refused', N.decode(mutate('welcome-created', 19, 3)) === null);
{
    const b = Uint8Array.from(bytesOf('welcome-created'));
    b.fill(0, 3, 19);
    check('welcome flags without a key are refused', N.decode(b) === null);
    b[19] = 0;
    check('welcome without a key and without flags is a welcome', N.decode(b) !== null);
}
// room: the checks of the C++ decoder, one value at a time (offsets are those of the 4 slots of 1 + 1 + n + 2 + 1 bytes; the tail starts after the map)
{
    const room = bytesOf('room-lobby-waiting');
    const r = Array.from(room);
    const tail = r.length - (1 + 4 + 1); // flags is 5 bytes before the end: flags, plan x4, in_game
    check('the tail of the room message is where the check thinks', r[r.length - 6] === 3 && r[r.length - 1] === 0);
    const with_ = (i, v) => { const c = Uint8Array.from(room); c[i] = v; return c; };
    const L = r.length;
    check('room: a state above 3', N.decode(with_(1, 4)) === null);
    check('room: a plan value of 5', N.decode(with_(L - 3, 5)) === null);
    check('room: a plan value in a room that is no lobby (flags 1)', N.decode((() => { const c = Uint8Array.from(room); c[L - 6] = 1; return c; })()) === null);
    check('room: starting without lobby (flags 5)', N.decode(with_(L - 6, 5)) === null);
    check('room: lobby without leader-starts (flags 2)', N.decode(with_(L - 6, 2)) === null);
    check('room: an unknown flag bit', N.decode(with_(L - 6, 11)) === null);
    check('room: a game for a seat that holds nobody (seat 3)', N.decode(with_(L - 1, 8)) === null);
    check('room: a game for a bot (seat 2)', N.decode(with_(L - 1, 4)) === null);
    check('room: a game for a person (seat 1)', N.decode(with_(L - 1, 2)) !== null);
    check('room: an in_game bit above 3', N.decode(with_(L - 1, 16)) === null);
    check('room: you = 4', N.decode(with_(L - 10, 4)) === null);
    check('room: you = 255 is fine', N.decode(with_(L - 10, 255)) !== null);
    check('room: a leader that is a bot (seat 2)', N.decode(with_(L - 9, 2)) === null);
    check('room: a leader that holds nobody (seat 3)', N.decode(with_(L - 9, 3)) === null);
    check('room: no leader is fine', N.decode(with_(L - 9, 255)) !== null);
    check('room: teams 1,0 are refused', N.decode((() => { const c = Uint8Array.from(room); c[L - 8] = 1; c[L - 7] = 0; return c; })()) === null);
    check('room: teams 0,4 are refused', N.decode((() => { const c = Uint8Array.from(room); c[L - 8] = 0; c[L - 7] = 4; return c; })()) === null);
    check('room: teams 0,255 are refused', N.decode((() => { const c = Uint8Array.from(room); c[L - 8] = 0; c[L - 7] = 255; return c; })()) === null);
    check('room: teams 1,3 are fine', N.decode((() => { const c = Uint8Array.from(room); c[L - 8] = 1; c[L - 7] = 3; return c; })()) !== null);
    check('room: fog = 2', N.decode(with_(L - 11, 2)) === null);
}
// a name with a character outside printable ASCII, a platform byte out of range, a platform on a bot
{
    const room = bytesOf('room-lobby-waiting');
    const c = Uint8Array.from(room);
    c[3] = 0x07;                                   // 'P' of Priya becomes BEL
    check('room: a control character in a name', N.decode(c) === null);
    const d = Uint8Array.from(room);
    d[1 + 1 + 1 + 5 + 2] = 0x20;                   // the platform byte of seat 0: bit 5 is no platform
    check('room: a platform beyond the bits', N.decode(d) === null);
    const e = Uint8Array.from(room);
    e[1 + 1 + 1 + 5 + 2] = 7;                      // an operating system that does not exist
    check('room: an operating system of 7', N.decode(e) === null);
}

// ---- the validators, as the C++ ones -----------------------------------------------------------------------------------------------------------------------------------------------------------------

check('map names', N.validMapName('TREASURE.LVL') && N.validMapName('a b!.lvl') && N.validMapName('~#&.LVL') && !N.validMapName('.LVL') && N.validMapName('a.LVL') && !N.validMapName('LVL') && !N.validMapName('.hidden.LVL') && !N.validMapName('x/y.LVL')
    && !N.validMapName('x\\y.LVL') && !N.validMapName('x:y.LVL') && !N.validMapName('x*y.LVL') && !N.validMapName('x?y.LVL') && !N.validMapName('x"y.LVL') && !N.validMapName('x<y.LVL') && !N.validMapName('x>y.LVL')
    && !N.validMapName('x|y.LVL') && !N.validMapName('TREASURE.TXT') && !N.validMapName('a'.repeat(61) + '.LVL') && N.validMapName('a'.repeat(60) + '.LVL') && !N.validMapName('café.LVL') && !N.validMapName(''));
check('room codes', N.validRoomCode('') && N.validRoomCode('k7m2xq') && N.validRoomCode('A_b-9') && !N.validRoomCode('k7m 2xq') && !N.validRoomCode('a'.repeat(33)) && N.validRoomCode('a'.repeat(32)) && !N.validRoomCode('a.b'));
check('public room codes have no capital', N.publicRoomCode('k7m2xq') && !N.publicRoomCode('K7m2xq') && !N.publicRoomCode('') && !N.publicRoomCode('a b'));
check('person names', N.validPersonName('Priya') && N.validPersonName('A b') && N.validPersonName('x'.repeat(32)) && !N.validPersonName('x'.repeat(33)) && !N.validPersonName('') && !N.validPersonName(' Sam') && !N.validPersonName('Sam ')
    && !N.validPersonName('Samé') && !N.validPersonName('S\tm'));
check('teams', N.validTeams(255, 255) && N.validTeams(0, 1) && N.validTeams(2, 3) && !N.validTeams(1, 0) && !N.validTeams(0, 0) && !N.validTeams(0, 4) && !N.validTeams(0, 255) && !N.validTeams(255, 1));
check('platforms', N.validPlatform(0) && N.validPlatform(0x16) && N.validPlatform(0x10) && !N.validPlatform(7) && !N.validPlatform(0x20) && !N.validPlatform(0x17));
check('teams are whole numbers (a number that is none, a string, null and NaN are refused)', !N.validTeams(-1, 2) && !N.validTeams('0', '2') && !N.validTeams(null, 2) && !N.validTeams(0.5, 2) && !N.validTeams(0, 2.5)
    && !N.validTeams(undefined, undefined) && !N.validTeams(NaN, NaN) && !N.validTeams(0, Infinity));
check('a platform is one byte that is a whole number (1.5, a string, NaN, -1, 256 and 300 are refused; 0x13 and 0x00 are not)', !N.validPlatform(1.5) && !N.validPlatform('3') && !N.validPlatform(NaN) && !N.validPlatform(null) && !N.validPlatform(undefined)
    && !N.validPlatform(-1) && !N.validPlatform(256) && !N.validPlatform(300) && N.validPlatform(0x13) && N.validPlatform(0));

// ---- the Hello says only what the server's decoder takes -----------------------------------------------------------------------------------------------------------------------------------------------
// (the C++ decoder refuses a Hello whose name is not printable ASCII or is longer than 32, and a create block whose map is no map name: the page would be refused with BadRequest for a name it was given)

{
    const hello = (h) => N.hexOf(N.encodeHello(Object.assign({ name: 'Priya', room: 'k7m2xq', create: { map: 'TREASURE.LVL' } }, h)));
    const zero = hello({});
    check('the name of a Hello is printable ASCII: the other characters are left out, as the game\'s own client leaves them out', hello({ name: 'José' }) === hello({ name: 'Jos' }) && hello({ name: 'Łukasz' }) === hello({ name: 'ukasz' })
        && hello({ name: '你好' }) === hello({ name: '' }) && hello({ name: 'a\tb' }) === hello({ name: 'ab' }) && hello({ name: 'a\u007fb' }) === hello({ name: 'ab' }) && hello({ name: 'a\u{1F600}b' }) === hello({ name: 'ab' }));
    check('... at most 32 of them, counted after the others were left out', hello({ name: 'x'.repeat(40) }) === hello({ name: 'x'.repeat(32) }) && hello({ name: '\u00e9'.repeat(5) + 'y'.repeat(40) }) === hello({ name: 'y'.repeat(32) }));
    check('... and a name with spaces at its ends is left as it is (the server trims what it takes)', hello({ name: '  Pri  ' }) !== hello({ name: 'Pri' }) && hello({ name: '' }) !== zero && hello({ name: 5 }) === hello({ name: '' }) && hello({ name: null }) === hello({ name: '' }));
    const bytes = N.encodeHello({ name: 'Jos\u00e9\u00e9 and 40 more', room: 'k7m2xq' });
    check('(the name length byte is the length of the name that was written)', bytes[3] === 'Jos and 40 more'.length && Array.from(bytes.slice(4, 4 + bytes[3])).every((b) => b >= 0x20 && b <= 0x7e));
    check('a map that is no map name is left out of the block: the room is made with the server\'s own map', hello({ create: { map: 'tiny' } }) === hello({ create: { map: '' } }) && hello({ create: { map: 'caf\u00e9.LVL' } }) === hello({ create: { map: '' } })
        && hello({ create: { map: '../x.LVL' } }) === hello({ create: { map: '' } }) && hello({ create: { map: 5 } }) === hello({ create: { map: '' } }) && hello({ create: { map: 'ISLANDS.LVL' } }) !== hello({ create: { map: '' } }));
    check('a platform that is none is "not told"; one that fits is kept', hello({ platform: 7 }) === hello({ platform: 0 }) && hello({ platform: 1.5 }) === hello({ platform: 0 }) && hello({ platform: '3' }) === hello({ platform: 0 }) && hello({ platform: NaN }) === hello({ platform: 0 })
        && hello({ platform: 300 }) === hello({ platform: 0 }) && hello({ platform: N.PLATFORM_BROWSER | N.OS.Linux }) !== hello({ platform: 0 }));
    const key = Uint8Array.from(seq(1));
    check('a key that is no key (15 bytes, 17 bytes, zeros, a string, a list, a number) is a new player\'s Hello; a key of 16 bytes is kept', [key.slice(0, 15), new Uint8Array(17), new Uint8Array(16), 'abcdefghijklmnop', Array.from(key), 5, {}, null].every((k) => hello({ key: k }) === zero)
        && hello({ key }) !== zero && hello({ key }).includes(N.hexOf(key)));
}

// ---- the guard is made from the state and the name of each seat, nothing else ---------------------------------------------------------------------------------------------------------------------------

{
    const base = N.decode(bytesOf('room-lobby-waiting'));
    const h0 = N.seatingHash(base);
    const copy = () => JSON.parse(JSON.stringify(base));
    const a = copy(); a.map = 'OTHER.LVL'; a.plan = [4, 4, 4, 4]; a.slots[1].rtt = 999; a.slots[1].platform = 0x11; a.you = 0; a.leader = 1; a.teamA = 0; a.teamB = 1; a.inGame = 3;
    check('the guard ignores the map, the plan, the thumbs, the platforms, who you are, the leader, the teams and the games', N.seatingHash(a) === h0);
    const b = copy(); b.slots[1].name = 'Sal';
    check('the guard follows a name', N.seatingHash(b) !== h0);
    const c = copy(); c.slots[3] = { state: N.SLOT.Client, name: '', rtt: 0, platform: 0 };
    check('the guard follows a state', N.seatingHash(c) !== h0);
    const d = copy(); d.slots[2].state = N.SLOT.Empty; d.slots[2].name = '';
    check('the guard follows a bot going', N.seatingHash(d) !== h0);
    const swapped = copy(); [swapped.slots[0], swapped.slots[1]] = [swapped.slots[1], swapped.slots[0]];
    check('the guard follows a swap', N.seatingHash(swapped) !== h0);
    const split = copy(); split.slots[0].name = 'Pri'; split.slots[1].name = 'yaSam';
    check('a boundary between two names counts (the 0 after each name)', N.seatingHash(split) !== h0);
    const empty = { slots: [0, 1, 2, 3].map(() => ({ state: 0, name: '' })) };
    check('the hash is never 0 and is a 32-bit number', N.seatingHash(empty) > 0 && N.seatingHash(empty) <= 0xFFFFFFFF);
}

// ---- nothing a server could send makes the decoder throw ---------------------------------------------------------------------------------------------------------------------------------------------

{
    let seed = 12345;
    const rnd = () => { seed = (Math.imul(seed, 1103515245) + 12345) >>> 0; return seed >>> 8; };
    let threw = 0;
    let accepted = 0;
    for (let i = 0; i < 20000; i++) {
        const len = rnd() % 80;
        const b = new Uint8Array(len);
        for (let k = 0; k < len; k++) b[k] = rnd() & 255;
        if (len > 0 && i % 2 === 0) b[0] = 1 + (rnd() % 34);
        try { if (N.decode(b) !== null) accepted++; } catch (e) { threw++; }
    }
    check('random bytes never throw', threw === 0, String(threw));
    for (const name of names) {
        const b = bytesOf(name);
        for (let i = 0; i < 300; i++) {
            const c = Uint8Array.from(b);
            c[rnd() % c.length] = rnd() & 255;
            try { N.decode(c); } catch (e) { threw++; }
        }
    }
    check('changed bytes of every golden message never throw', threw === 0, String(threw));
}

console.log('lobby_net.js: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
