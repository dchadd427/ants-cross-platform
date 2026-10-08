// Runs the lobby page's client of network protocol 16 (web/front/lobby_net.js, LobbyClient) without a browser, against a scripted server: the Hello it sends, the Welcome and Room messages it
// takes in, the answers to the server's pings, what changed between two Room messages (joined, left, renamed, moved, swapped, a new host), the requests that only the leader may make and the
// bytes they put on the wire (the guard of a colour move is the room's seating hash), the way back with the key after a lost link (the waits grow, the link that says nothing is given up, a page
// that comes back to the front tries at once), what each refusal means (the room is gone: made again without the key; removed; another window; full), and the entry that the game page reads.
// usage: node web_lobby_client_check.js web/front/lobby_net.js     (exit 0: every check holds; failures are printed)
'use strict';
const path = require('path');

const modulePath = process.argv[2];
if (!modulePath) { console.log('usage: web_lobby_client_check.js lobby_net.js'); process.exit(2); }
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
const seq = (first) => Uint8Array.from(Array.from({ length: 16 }, (_, i) => (first + i) & 255));

// ---- a scripted server side: the messages that the C++ server writes (the Room message is checked against the C++ bytes in web_lobby_net_check.js; this builder is the check's own) -----------------

function str8(w, s) { w.push(s.length); for (let i = 0; i < s.length; i++) w.push(s.charCodeAt(i)); }
function welcome(player, key, flags) { return Uint8Array.from([2, player, 4].concat(Array.from(key), [flags || 0])); }
function reject(reason) { return Uint8Array.from([3, reason]); }
function ping(nonce, sent) { return Uint8Array.from([10, nonce & 255, (nonce >>> 8) & 255, (nonce >>> 16) & 255, nonce >>> 24, sent & 255, (sent >>> 8) & 255, (sent >>> 16) & 255, sent >>> 24]); }
function chatFromRoom(text) { const w = [9, 255, 0]; str8(w, text); return Uint8Array.from(w); }
const E = N.SLOT.Empty, C = N.SLOT.Client, B = N.SLOT.Bot;
// seats: [state, name] x 4
function room(seats, o) {
    o = o || {};
    const w = [12];
    for (const s of seats) { w.push(s[0]); str8(w, s[1]); w.push(20, 0, 0); }       // rtt 20 ms, no platform
    str8(w, o.map === undefined ? 'TREASURE.LVL' : o.map);
    w.push(0, o.you === undefined ? 0 : o.you, o.leader === undefined ? 0 : o.leader, o.teamA === undefined ? 255 : o.teamA, o.teamB === undefined ? 255 : o.teamB, o.flags === undefined ? 3 : o.flags);
    for (const k of (o.plan || [0, 0, 0, 0])) w.push(k);
    w.push(o.inGame || 0);
    return Uint8Array.from(w);
}
// the independent builder agrees with the C++ bytes of one golden Room message (tests/data/lobby_messages.txt, room-lobby-alone-no-map)
{
    const want = '0c02055072697961' + '0c0000' + '00';
    const mine = room([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { map: '', you: 0, leader: 0, flags: 3, plan: [0, 4, 1, 0] });
    check('the scripted Room message starts like the C++ one', hex(mine).startsWith(want.slice(0, 14)) && N.decode(mine) !== null && N.decode(mine).plan.join() === '0,4,1,0');
}

// ---- a fake browser: sockets and timers ---------------------------------------------------------------------------------------------------------------------------------------------------------------

function world() {
    const w = { sockets: [], timers: [], clock: 1000000, nextId: 1 };
    w.WebSocket = function FakeSocket(url) {
        this.url = url;
        this.sent = [];
        this.closed = false;
        this.readyState = 0;
        w.sockets.push(this);
    };
    w.WebSocket.prototype.send = function (bytes) { if (this.closed) throw new Error('send on a closed socket'); this.sent.push(Uint8Array.from(bytes)); };
    w.WebSocket.prototype.close = function () { this.closed = true; };
    w.WebSocket.prototype.open = function () { this.readyState = 1; if (this.onopen) this.onopen(); };
    w.WebSocket.prototype.receive = function (bytes) { if (this.onmessage) this.onmessage({ data: bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) }); };
    w.WebSocket.prototype.drop = function () { this.closed = true; if (this.onclose) this.onclose({ code: 1006 }); };
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
function client(w, extra) {
    const opts = Object.assign({ url: 'wss://beta.example/ws', code: 'k7m2xq', map: 'TREASURE.LVL', name: 'Priya', WebSocket: w.WebSocket, setTimeout: w.setTimeout, clearTimeout: w.clearTimeout, now: w.now }, extra || {});
    const c = new N.LobbyClient(opts);
    c.log = [];
    for (const ev of ['status', 'welcome', 'room', 'joined', 'left', 'renamed', 'moved', 'swapped', 'host', 'plan', 'starting', 'games', 'notice', 'chat', 'gone', 'refused', 'removed', 'superseded']) {
        c.on(ev, (data) => c.log.push([ev, ev === 'room' ? undefined : (ev === 'welcome' ? { seat: data.seat, created: data.created, rejoin: data.rejoin } : data)]));
    }
    return c;
}
const events = (c, name) => c.log.filter((e) => e[0] === name).map((e) => e[1]);

// ---- the first minute: Hello, Welcome, Room, ping, the requests of the leader -------------------------------------------------------------------------------------------------------------------------

{
    const w = world();
    const c = client(w);
    check('a new client is idle', c.status === 'idle');
    c.connect();
    check('connect opens one socket to the url', w.sockets.length === 1 && w.sockets[0].url === 'wss://beta.example/ws' && c.status === 'connecting');
    c.connect();
    check('connect twice opens no second socket', w.sockets.length === 1);
    w.last().open();
    same('the Hello of a page that makes a lobby room', hex(w.last().sent[0]), hex(N.encodeHello({ name: 'Priya', room: 'k7m2xq', create: { map: 'TREASURE.LVL' } })));
    check('the Hello has no key', w.last().sent[0].slice(1 + 2 + 1 + 5 + 2 + 1 + 1 + 6 + 1, 1 + 2 + 1 + 5 + 2 + 1 + 1 + 6 + 1 + 16).every((b) => b === 0));
    const key = seq(0x30);
    w.last().receive(welcome(0, key, 2));
    check('the Welcome puts the page online as the first seat, and says that its Hello made the room', c.status === 'online' && c.seat === 0 && events(c, 'welcome')[0].created === true && hex(c.key) === hex(key));
    const solo = room([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { you: 0, leader: 0 });
    w.last().receive(solo);
    check('the first Room message gives the room and no event but itself', c.room !== null && c.room.lobby && c.isLeader() && c.log.filter((e) => ['joined', 'left', 'host', 'plan'].includes(e[0])).length === 0);
    // pings are answered at once, with the same two numbers
    w.last().receive(ping(0x01020304, 0x0A0B0C0D));
    same('a ping is answered with a pong that echoes it', hex(w.last().sent[1]), hex(N.encodePong(0x01020304, 0x0A0B0C0D)));
    // a person joins
    const two = room([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0 });
    w.last().receive(two);
    same('a joiner is announced with the seat and the name', events(c, 'joined'), [{ type: 'joined', seat: 1, name: 'Sam' }]);
    // the leader's requests
    const guard = N.seatingHash(N.decode(two));
    check('move 1 to 2 sends a SeatMove with the seating hash as the guard', c.move(1, 2) && hex(w.last().sent[2]) === hex(N.encodeSeatMove(1, 2, guard)));
    check('move 0 to 3 (the leader to an empty colour) is made too', c.move(0, 3) && hex(w.last().sent[3]) === hex(N.encodeSeatMove(0, 3, guard)));
    check('move from an empty colour is refused', c.move(2, 3) === false && w.last().sent.length === 4);
    check('move to the same colour is refused', c.move(1, 1) === false);
    check('move to a colour that does not exist is refused', c.move(1, 4) === false && c.move(-1, 1) === false);
    check('remove 1 sends a Remove with the guard', c.remove(1) && hex(w.last().sent[4]) === hex(N.encodeRemove(1, guard)));
    check('the leader cannot remove itself', c.remove(0) === false);
    check('an empty colour cannot be removed', c.remove(2) === false);
    const plan = { map: 'ISLANDS.LVL', kinds: [N.PLAN.Open, N.PLAN.Open, N.PLAN.Medium, N.PLAN.Hard], teamA: 0, teamB: 2 };
    check('setPlan sends the plan', c.setPlan(plan) && hex(w.last().sent[5]) === hex(N.encodePlan('ISLANDS.LVL', plan.kinds, 0, 2)));
    check('setPlan keeps the map with an empty name', c.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }) && hex(w.last().sent[6]) === hex(N.encodePlan('', [0, 0, 0, 0], 255, 255)));
    check('setPlan refuses a map that is no map name', c.setPlan({ map: '../x', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }) === false);
    check('setPlan refuses a kind above Nobody', c.setPlan({ map: '', kinds: [0, 0, 0, 5], teamA: 255, teamB: 255 }) === false);
    check('setPlan refuses teams that are not a pair a < b', c.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 2, teamB: 1 }) === false && c.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 0, teamB: 255 }) === false);
    check('rename sends a Name', c.rename('Pri') && hex(w.last().sent[7]) === hex(N.encodeName('Pri')));
    check('rename refuses a name with a space at the end, an empty one and a long one', c.rename('Pri ') === false && c.rename('') === false && c.rename('x'.repeat(33)) === false);
    check('start sends the fill levels and the teams of the room', c.start() && hex(w.last().sent[8]) === hex(N.encodeStartRequest([0, 0, 0, 0], 255, 255)));
    check('nothing else was sent', w.last().sent.length === 9);
    // a plan that bots fill: the StartRequest follows the room's plan
    w.last().receive(room([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0, plan: [0, 0, 2, 3], teamA: 0, teamB: 2 }));
    check('a plan change is an event', events(c, 'plan').length === 1);
    check('start follows the plan of the room', c.start() && hex(w.last().sent[9]) === hex(N.encodeStartRequest([0, 0, N.FILL.Medium, N.FILL.Hard], 0, 2)));
    // the leader's START waits for the games: the event comes once, nothing can be moved or renamed
    w.last().receive(room([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0, plan: [0, 0, 2, 3], teamA: 0, teamB: 2, flags: 7, inGame: 1 }));
    check('the START that waits is an event, once', events(c, 'starting').length === 1 && events(c, 'games').length === 1);
    w.last().receive(room([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0, plan: [0, 0, 2, 3], teamA: 0, teamB: 2, flags: 7, inGame: 3 }));
    check('the games coming in are events and the start is not announced again', events(c, 'starting').length === 1 && events(c, 'games').length === 2 && events(c, 'games')[1].mask === 3);
    const count = w.last().sent.length;
    check('while the START waits: no move, no plan, no rename, no start', c.move(1, 2) === false && c.setPlan(plan) === false && c.rename('Zed') === false && c.start() === false && w.last().sent.length === count);
    check('while the START waits a person can still be removed (a game that never opens)', c.remove(1) && w.last().sent.length === count + 1);
    // a line of the room itself is a notice
    w.last().receive(chatFromRoom('Sam\'s game did not come in time.'));
    same('a line of the room is a notice', events(c, 'notice'), [{ text: 'Sam\'s game did not come in time.' }]);
    w.last().receive(Uint8Array.from([9, 1, 0, 2, 104, 105]));
    same('a line of a player is chat', events(c, 'chat'), [{ sender: 1, team: false, text: 'hi' }]);
}

{   // the guard of a request on the screen that the leader saw: kept when the row is picked up or the question opens (guardNow), given back with the request; a seating that changed meanwhile sends nothing
    const w = world();
    const c = client(w);
    check('before any room there is no guard', c.guardNow() === 0);
    c.connect();
    w.last().open();
    w.last().receive(welcome(0, seq(0x30), 2));
    w.last().receive(room([[C, 'Priya'], [C, 'Sam'], [C, 'Tess'], [E, '']], { you: 0, leader: 0 }));
    const seen = c.guardNow();
    check('guardNow is the seating hash of the room as it is shown', seen !== 0 && seen === N.seatingHash(c.room));
    const base = w.last().sent.length;
    check('a removal with the guard of the screen is sent, with that guard', c.remove(1, seen) && hex(w.last().sent[base]) === hex(N.encodeRemove(1, seen)));
    check('a move with the guard of the screen is sent, with that guard', c.move(1, 3, seen) && hex(w.last().sent[base + 1]) === hex(N.encodeSeatMove(1, 3, seen)));
    w.last().receive(room([[C, 'Priya'], [C, 'Sammy'], [C, 'Tess'], [E, '']], { you: 0, leader: 0 }));      // (the player of colour 2 goes by another name now)
    const after = c.guardNow();
    check('a changed seating is another guard', after !== 0 && after !== seen);
    check('the old guard sends nothing: no removal and no move', c.remove(1, seen) === false && c.move(1, 3, seen) === false && w.last().sent.length === base + 2);
    check('the new guard is sent as it is', c.remove(1, after) && hex(w.last().sent[base + 2]) === hex(N.encodeRemove(1, after)));
    check('a guard that is not the seating\'s sends nothing (0, a number, a string)', c.remove(1, 0) === false && c.remove(1, 12345) === false && c.move(1, 3, '1') === false && w.last().sent.length === base + 3);
    check('without a guard the room as it is now counts (as before)', c.remove(2) && hex(w.last().sent[base + 3]) === hex(N.encodeRemove(2, after)));
    check('a guard does not make a request possible that is not: the leader cannot be removed', c.remove(0, after) === false && w.last().sent.length === base + 4);
}

// ---- a page that is not the leader ------------------------------------------------------------------------------------------------------------------------------------------------------------------

{
    const w = world();
    const c = client(w, { name: 'Sam' });
    c.connect();
    w.last().open();
    w.last().receive(welcome(1, seq(0x50), 0));
    w.last().receive(room([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 0 }));
    check('a joiner is not told that its Hello made the room', events(c, 'welcome')[0].created === false && !c.isLeader());
    const sent = w.last().sent.length;
    check('a player who does not lead cannot move, plan, remove or start', c.move(0, 2) === false && c.remove(0) === false && c.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }) === false && c.start() === false && w.last().sent.length === sent);
    check('but can rename itself', c.rename('Samuel') && hex(w.last().sent[sent]) === hex(N.encodeName('Samuel')));
    // the leader goes: the next one leads, and this page is told that it is the host now
    w.last().receive(room([[E, ''], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 1 }));
    same('the leader left and this page is the host now', [events(c, 'left'), events(c, 'host')], [[{ type: 'left', seat: 0, name: 'Priya' }], [{ type: 'host', seat: 1, name: 'Sam', you: true, before: 'Priya' }]]);
    check('this page leads now', c.isLeader());
}

// ---- what changed between two Room messages ---------------------------------------------------------------------------------------------------------------------------------------------------------

{
    const r = (seats, o) => N.decode(room(seats, o));
    const a = r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 0 });
    same('nothing changed: no event', N.diffRooms(a, r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 0 })), []);
    same('a first Room message has no diff', N.diffRooms(null, a), []);
    same('a joiner', N.diffRooms(a, r([[C, 'Priya'], [C, 'Sam'], [C, 'Juniper'], [E, '']], { leader: 0 })), [{ type: 'joined', seat: 2, name: 'Juniper' }]);
    same('a person who leaves', N.diffRooms(a, r([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { leader: 0 })), [{ type: 'left', seat: 1, name: 'Sam' }]);
    same('a person who is dragged to an empty colour is moved, not gone and new', N.diffRooms(a, r([[C, 'Priya'], [E, ''], [C, 'Sam'], [E, '']], { leader: 0 })), [{ type: 'moved', from: 1, to: 2, name: 'Sam' }]);
    same('two persons who changed places are swapped', N.diffRooms(a, r([[C, 'Sam'], [C, 'Priya'], [E, ''], [E, '']], { leader: 1 })), [{ type: 'swapped', a: 0, b: 1, names: ['Sam', 'Priya'] }]);
    same('a name that changed', N.diffRooms(a, r([[C, 'Priya'], [C, 'Samuel'], [E, ''], [E, '']], { leader: 0 })), [{ type: 'renamed', seat: 1, from: 'Sam', to: 'Samuel' }]);
    same('the leader dragged to another colour leads still: no host event', N.diffRooms(a, r([[E, ''], [C, 'Sam'], [C, 'Priya'], [E, '']], { leader: 2 })).map((e) => e.type), ['moved']);
    same('the leader leaves, the next one leads', N.diffRooms(a, r([[E, ''], [C, 'Sam'], [E, ''], [E, '']], { leader: 1 })).map((e) => e.type), ['left', 'host']);
    same('the leader leaves, nobody is left to lead', N.diffRooms(r([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { leader: 0 }), r([[E, ''], [E, ''], [E, ''], [E, '']], { leader: 255, you: 255 })).map((e) => e.type), ['left']);
    same('a player of the same name as the leader leads after the leader left', N.diffRooms(r([[C, 'Player'], [C, 'Player'], [E, ''], [E, '']], { leader: 0 }), r([[E, ''], [C, 'Player'], [E, ''], [E, '']], { leader: 1 })).map((e) => e.type), ['left', 'host']);
    same('a plan change: a map, a kind, the teams', [
        N.diffRooms(a, r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 0, map: 'ISLANDS.LVL' })).map((e) => e.type),
        N.diffRooms(a, r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 0, plan: [0, 0, 1, 0] })).map((e) => e.type),
        N.diffRooms(a, r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 0, teamA: 0, teamB: 1 })).map((e) => e.type)
    ], [['plan'], ['plan'], ['plan']]);
}

// ---- a lost link: the way back with the key, the waits, the link that says nothing -----------------------------------------------------------------------------------------------------------------------

{
    const w = world();
    const c = client(w);
    c.connect();
    w.last().open();
    const key = seq(0x70);
    w.last().receive(welcome(0, key, 2));
    w.last().receive(room([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { you: 0, leader: 0 }));
    w.last().drop();
    check('a lost link puts the page offline', c.status === 'offline' && w.sockets.length === 1);
    w.advance(499);
    check('it waits half a second before the next try', w.sockets.length === 1);
    w.advance(1);
    check('it tries again then', w.sockets.length === 2 && c.status === 'connecting');
    w.last().open();
    same('the Hello of the way back shows the key and the block', hex(w.last().sent[0]), hex(N.encodeHello({ name: 'Priya', room: 'k7m2xq', key: key, create: { map: 'TREASURE.LVL' } })));
    w.last().drop();                                                       // (the server did not answer: the waits grow)
    w.advance(999);
    check('a second failure waits a second', w.sockets.length === 2);
    w.advance(1);
    check('and tries again', w.sockets.length === 3);
    w.last().drop();
    w.advance(2000);
    w.last().drop();
    w.advance(4000);
    w.last().drop();
    w.advance(8000);
    check('the waits grow 0.5, 1, 2, 4, 8 seconds', w.sockets.length === 6, String(w.sockets.length));
    w.last().drop();
    w.advance(7999);
    check('and stay at eight', w.sockets.length === 6);
    w.advance(1);
    check('(the sixth failure waits eight seconds too)', w.sockets.length === 7);
    w.last().open();
    w.last().receive(welcome(0, key, 0));
    check('the Welcome of the same seat puts the page back online and the waits start again', c.status === 'online' && c.failures === 0 && c.seat === 0);
    check('the key is still the same', hex(c.key) === hex(key));
    w.last().drop();
    w.advance(500);
    check('after a success the next wait is half a second again', w.sockets.length === 8);
    // sockets that were given up do not speak any more
    const old = w.sockets[w.sockets.length - 2];
    const before = c.log.length;
    old.receive(welcome(3, seq(1), 0));
    old.drop();
    check('an old socket is ignored', c.log.length === before && c.seat === 0);
}
{
    const w = world();
    const c = client(w);
    c.connect();
    w.last().open();
    w.last().receive(welcome(0, seq(0x70), 2));
    w.last().receive(room([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { you: 0, leader: 0 }));
    w.advance(7000);
    check('a link that has been quiet for 7 seconds is still alive', c.status === 'online' && w.sockets.length === 1);
    w.last().receive(ping(1, 1));
    w.advance(7999);
    check('every message from the server counts as a sign of life', c.status === 'online' && w.sockets.length === 1);
    w.advance(1);
    check('8 seconds of silence: the link is given up and closed', c.status === 'offline' && w.sockets[0].closed);
    w.advance(500);
    check('and the way back is tried', w.sockets.length === 2);
}
{   // the page comes back to the front
    const w = world();
    const c = client(w);
    c.connect();
    w.last().open();
    w.last().receive(welcome(0, seq(0x70), 2));
    w.last().receive(room([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { you: 0, leader: 0 }));
    w.last().drop();
    check('offline after the drop', c.status === 'offline');
    c.wake();
    check('wake tries at once, without the wait', w.sockets.length === 2 && c.status === 'connecting');
    w.last().open();
    w.last().receive(welcome(0, seq(0x70), 0));
    w.clock += 20000;                                                       // (a phone that slept: the timers did not run)
    c.wake();
    check('wake gives up a link that has said nothing for longer than it should', c.status === 'offline' && w.sockets[1].closed);
    c.wake();
    check('and tries again at once', w.sockets.length === 3);
    w.last().open();
    w.last().receive(welcome(0, seq(0x70), 0));
    c.wake();
    check('wake does nothing to a link that is fine', c.status === 'online' && w.sockets.length === 3);
}

// ---- what each refusal means -----------------------------------------------------------------------------------------------------------------------------------------------------------------------

{   // the room of our key is gone: made again, without the key, with the same code
    const w = world();
    const c = client(w, { key: seq(0x11) });
    c.connect();
    w.last().open();
    check('a page that comes with a key shows it', hex(w.last().sent[0]) === hex(N.encodeHello({ name: 'Priya', room: 'k7m2xq', key: seq(0x11), create: { map: 'TREASURE.LVL' } })));
    w.last().receive(reject(N.REJECT.NoSuchRoom));
    check('NoSuchRoom to a key: the room is gone, the old socket is let go and a new one is opened', events(c, 'gone').length === 1 && w.sockets.length === 2 && w.sockets[0].closed && c.key === null && c.status === 'connecting');
    w.last().open();
    same('the Hello that makes the room again has no key and has the block', hex(w.last().sent[0]), hex(N.encodeHello({ name: 'Priya', room: 'k7m2xq', create: { map: 'TREASURE.LVL' } })));
    w.last().receive(welcome(0, seq(0x22), 2));
    check('the new room is announced as made by us, with a new key', c.status === 'online' && events(c, 'welcome')[0].created === true && hex(c.key) === hex(seq(0x22)));
    // the second NoSuchRoom, without a key, is the server having no place: not tried again
    const w2 = world();
    const d = client(w2);
    d.connect();
    w2.last().open();
    w2.last().receive(reject(N.REJECT.NoSuchRoom));
    check('NoSuchRoom without a key is a refusal (the server has no place for a lobby)', d.status === 'refused' && events(d, 'refused')[0].reason === N.REJECT.NoSuchRoom && w2.sockets.length === 1 && w2.sockets[0].closed);
    w2.advance(100000);
    check('and the page does not try again by itself', w2.sockets.length === 1);
    d.connect();
    check('not even when asked to connect: a refusal is final until the page makes a new client', w2.sockets.length === 1);
}
for (const [name, reason] of [['Full', N.REJECT.Full], ['MatchRunning', N.REJECT.MatchRunning], ['VersionMismatch', N.REJECT.VersionMismatch], ['BadRequest', N.REJECT.BadRequest], ['Dropped', N.REJECT.Dropped], ['RejoinFailed', N.REJECT.RejoinFailed]]) {
    const w = world();
    const c = client(w);
    c.connect();
    w.last().open();
    w.last().receive(reject(reason));
    check(name + ' is a final refusal with its reason', c.status === 'refused' && events(c, 'refused')[0].reason === reason && w.sockets[0].closed);
    w.advance(100000);
    check(name + ': no more tries', w.sockets.length === 1);
}
{   // removed by the leader: Kicked
    const w = world();
    const c = client(w);
    c.connect();
    w.last().open();
    w.last().receive(welcome(2, seq(0x41), 0));
    w.last().receive(room([[C, 'Sam'], [C, 'Juniper'], [C, 'Priya'], [E, '']], { you: 2, leader: 0 }));
    w.last().receive(reject(N.REJECT.Kicked));
    check('Kicked: removed, the key is forgotten, the page is told once', c.status === 'removed' && events(c, 'removed').length === 1 && c.key === null && c.rejoinEntry() === null);
    w.advance(100000);
    check('and the page does not come back by itself', w.sockets.length === 1);
    check('nothing can be asked of a removed client', c.rename('Zed') === false && c.move(0, 3) === false);
}
{   // another window took the seat
    const w = world();
    const c = client(w);
    c.connect();
    w.last().open();
    w.last().receive(welcome(0, seq(0x41), 2));
    w.last().receive(room([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { you: 0, leader: 0 }));
    w.last().receive(reject(N.REJECT.Superseded));
    check('Superseded: final, said once, no more tries', c.status === 'superseded' && events(c, 'superseded').length === 1);
    w.advance(100000);
    check('(no tries)', w.sockets.length === 1);
    c.wake();
    check('(not even when the page wakes)', w.sockets.length === 1);
}

// ---- leaving on purpose -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

{
    const w = world();
    const c = client(w);
    c.connect();
    w.last().open();
    w.last().receive(welcome(0, seq(0x41), 2));
    w.last().receive(room([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { you: 0, leader: 0 }));
    const socket = w.last();
    check('leave sends Leave and closes the link', c.leave() && hex(socket.sent[socket.sent.length - 1]) === hex(N.encodeLeave()) && socket.closed && c.status === 'closed');
    w.advance(100000);
    check('a page that left does not come back', w.sockets.length === 1);
    check('leave once more does nothing', c.leave() === false);
}
{   // a client that is dropped while it waits to retry leaves nothing behind
    const w = world();
    const c = client(w);
    c.connect();
    w.last().open();
    w.last().receive(welcome(0, seq(0x41), 2));
    w.last().drop();
    check('offline with a retry waiting', c.status === 'offline' && w.timers.length === 1);
    c.leave();
    check('leaving cancels the retry', w.timers.length === 0 && w.sockets.length === 1);
}

// ---- a socket that cannot be made, a link that goes before the first word ---------------------------------------------------------------------------------------------------------------------------

{
    const w = world();
    const c = client(w, { WebSocket: function () { throw new Error('insecure'); } });
    c.connect();
    check('an address that no socket takes is a final refusal with reason 0 and the error', c.status === 'refused' && events(c, 'refused')[0].reason === 0 && events(c, 'refused')[0].error === 'insecure');
    w.advance(100000);
    check('and is not tried again', c.status === 'refused' && w.sockets.length === 0);
}
{
    const w = world();
    const c = client(w);
    c.connect();
    const s = w.last();
    s.send = function () { throw new Error('closing'); };
    s.open();
    check('a link that cannot take the Hello is given up and tried again', c.status === 'offline' && s.closed && w.timers.length === 1);
    w.advance(500);
    check('(the way back is the next socket)', w.sockets.length === 2 && c.status === 'connecting');
}

// ---- the name that the room shows is the one the page comes back with ----------------------------------------------------------------------------------------------------------------------------

{
    const w = world();
    const c = client(w, { name: 'Priya' });
    c.connect();
    w.last().open();
    w.last().receive(welcome(0, seq(0x41), 2));
    w.last().receive(room([[C, 'Player'], [E, ''], [E, ''], [E, '']], { you: 0, leader: 0 }));       // (the room showed another name than the one we sent)
    check('the page takes the name that the room shows', c.name === 'Player');
    w.last().drop();
    w.advance(500);
    w.last().open();
    check('and sends it on the way back', hex(w.last().sent[0]) === hex(N.encodeHello({ name: 'Player', room: 'k7m2xq', key: seq(0x41), create: { map: 'TREASURE.LVL' } })));
}

// ---- the entry that the game page reads to take the seat over -------------------------------------------------------------------------------------------------------------------------------------

{
    const w = world();
    const c = client(w);
    check('no entry before the room has welcomed the page', c.rejoinEntry() === null);
    c.connect();
    w.last().open();
    w.last().receive(welcome(2, seq(0xA0), 0));
    const e = c.rejoinEntry(1760000000123);
    same('the entry is named by room and seat', e.name, 'ants.rejoin.k7m2xq.2');
    same('and holds the key, the server and the time as the game writes them', e.text, '{"k":"a0a1a2a3a4a5a6a7a8a9aaabacadaeaf","s":"wss://beta.example/ws","t":1760000000123}');
    // the page's own rule for such an entry (web/shell.html REJOINKEY block) accepts it
    const m = /^ants\.rejoin\.([A-Za-z0-9_-]{1,32})\.([0-3])$/.exec(e.name);
    const v = JSON.parse(e.text);
    check('it is an entry by the page\'s own rules (members k, s, t; 32 hex digits that are not zero; a printable server; a whole time)',
        m && Object.keys(v).sort().join() === 'k,s,t' && /^[0-9a-f]{32}$/.test(v.k) && !/^0+$/.test(v.k) && /^[\x20-\x7e]{1,255}$/.test(v.s) && Number.isInteger(v.t) && v.t >= 0);
    check('a key of zeros makes no entry', N.rejoinEntry('k7m2xq', 0, new Uint8Array(16), 'wss://x/ws', 1) === null);
    check('a seat beyond 3 makes no entry', N.rejoinEntry('k7m2xq', 4, seq(1), 'wss://x/ws', 1) === null);
    check('an empty code makes no entry', N.rejoinEntry('', 0, seq(1), 'wss://x/ws', 1) === null);
    check('a code with a space makes no entry', N.rejoinEntry('k7m 2xq', 0, seq(1), 'wss://x/ws', 1) === null);
}

console.log('LobbyClient: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
