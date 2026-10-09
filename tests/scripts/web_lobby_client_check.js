// Runs the lobby page's client of network protocol 16 (web/front/lobby_net.js, LobbyClient) without a browser, against a scripted server: the Hello it sends, the Welcome and Room messages it
// takes in, the answers to the server's pings, what changed between two Room messages (joined, left, renamed, moved, swapped, a new host), the requests that only the leader may make and the
// bytes they put on the wire (the guard of a colour move is the room's seating hash), the way back with the key after a lost link (the waits grow, the link that says nothing is given up, a page
// that comes back to the front tries at once), what each refusal means (the room is gone: made again without the key; removed; another window; full), and the entry that the game page reads.
// usage: node web_lobby_client_check.js web/front/lobby_net.js     (exit 0: every check holds; failures are printed)
'use strict';
const fs = require('fs');
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
    w.WebSocket.prototype.send = function (bytes) { if (this.closed || this.readyState !== 1) throw new Error('send on a socket that is not open'); this.sent.push(Uint8Array.from(bytes)); };      // (a browser throws too: InvalidStateError)
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
    same('two persons who changed places are swapped (the page of the leader: its own seat moved with it)', N.diffRooms(a, r([[C, 'Sam'], [C, 'Priya'], [E, ''], [E, '']], { leader: 1, you: 1 })), [{ type: 'swapped', a: 0, b: 1, names: ['Sam', 'Priya'] }]);
    same('a name that changed', N.diffRooms(a, r([[C, 'Priya'], [C, 'Samuel'], [E, ''], [E, '']], { leader: 0 })), [{ type: 'renamed', seat: 1, from: 'Sam', to: 'Samuel' }]);
    same('the leader dragged to another colour leads still: no host event', N.diffRooms(a, r([[E, ''], [C, 'Sam'], [C, 'Priya'], [E, '']], { leader: 2, you: 2 })).map((e) => e.type), ['moved']);
    same('the leader leaves, the next one leads', N.diffRooms(a, r([[E, ''], [C, 'Sam'], [E, ''], [E, '']], { leader: 1 })).map((e) => e.type), ['left', 'host']);
    same('the leader leaves, nobody is left to lead', N.diffRooms(r([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { leader: 0 }), r([[E, ''], [E, ''], [E, ''], [E, '']], { leader: 255, you: 255 })).map((e) => e.type), ['left']);
    same('a player of the same name as the leader leads after the leader left', N.diffRooms(r([[C, 'Player'], [C, 'Player'], [E, ''], [E, '']], { leader: 0 }), r([[E, ''], [C, 'Player'], [E, ''], [E, '']], { leader: 1 })).map((e) => e.type), ['left', 'host']);
    same('a plan change: a map, a kind, the teams', [
        N.diffRooms(a, r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 0, map: 'ISLANDS.LVL' })).map((e) => e.type),
        N.diffRooms(a, r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 0, plan: [0, 0, 1, 0] })).map((e) => e.type),
        N.diffRooms(a, r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 0, teamA: 0, teamB: 1 })).map((e) => e.type)
    ], [['plan'], ['plan'], ['plan']]);
    // the host as the page's own seat tells it (every Room message says which seat is the page's: you)
    same('a host who came back after the hold ran out is a player now: another leads, and the page hears of the new host', N.diffRooms(r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 0, you: 0 }), r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 1, you: 0 })),
        [{ type: 'host', seat: 1, name: 'Sam', you: false, before: 'Priya' }]);
    same('a player who is the host now, though the seat that led is still held by somebody (the page gained the role)', N.diffRooms(r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 0, you: 1 }), r([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { leader: 1, you: 1 })),
        [{ type: 'host', seat: 1, name: 'Sam', you: true, before: 'Priya' }]);
    same('two players of one name swap and the host is one of them: the same host, no event', N.diffRooms(r([[C, 'Player'], [C, 'Player'], [E, ''], [E, '']], { leader: 0, you: 0 }), r([[C, 'Player'], [C, 'Player'], [E, ''], [E, '']], { leader: 1, you: 1 })), []);
    same('the same, seen by a third player', N.diffRooms(r([[C, 'Player'], [C, 'Player'], [C, 'Tess'], [E, '']], { leader: 0, you: 2 }), r([[C, 'Player'], [C, 'Player'], [C, 'Tess'], [E, '']], { leader: 1, you: 2 })), []);
    same('a host who is dragged to another colour and a joiner at once: the host is still the host', N.diffRooms(a, r([[C, 'Juniper'], [C, 'Sam'], [C, 'Priya'], [E, '']], { leader: 2, you: 2 })).map((e) => e.type).filter((t) => t === 'host'), []);
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
    check('the Welcome of the same seat puts the page back online', c.status === 'online' && c.seat === 0);
    check('the key is still the same', hex(c.key) === hex(key));
    w.last().drop();
    w.advance(7999);
    check('a link that goes at once after its Welcome has not proved itself: the wait is still eight seconds', w.sockets.length === 7, String(w.sockets.length));
    w.advance(1);
    check('(it tries again after them)', w.sockets.length === 8);
    w.last().open();
    w.last().receive(welcome(0, key, 0));
    w.advance(7000);
    w.last().receive(ping(2, 2));
    w.advance(7000);
    w.last().receive(ping(3, 3));
    w.last().drop();
    w.advance(499);
    check('a link that stayed up for 14 seconds has proved itself', w.sockets.length === 8);
    w.advance(1);
    check('after a success the next wait is half a second again', w.sockets.length === 9);
    w.last().open();
    w.last().receive(welcome(0, key, 0));
    w.last().drop();
    w.advance(499);
    w.advance(1);
    check('(and it is the first wait only: the next failure after a short Welcome waits a second)', w.sockets.length === 9);
    w.advance(500);
    check('(a second, to be exact)', w.sockets.length === 10);
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

// ---- a page that joins and never makes: join: true (a code that somebody typed) -----------------------------------------------------------------------------------------------------------------------
// The Hello of a page that makes a lobby room carries the create block (the map, four seats, no team, the leader starts, a lobby); a page that was only given a code to join sends none, so that a code
// with no room is answered NoSuchRoom instead of making one. The bytes are held to the lines that the C++ encoder wrote (tests/data/lobby_messages.txt, which web_lobby_net_check.js holds the codec
// to) and to a Hello written out by hand here: type 1, protocol 16, the name, 0 and 255, the code, an empty second string, the key (zeros without one), 0, the platform, the client kind 1 (a page).

{
    const golden = {};
    for (const raw of fs.readFileSync(path.join(__dirname, '..', 'data', 'lobby_messages.txt'), 'utf8').split('\n')) {
        const parts = raw.trim().split(/\s+/);
        if (parts[0] === 'hello' && parts.length === 3) golden[parts[1]] = parts[2];
    }
    check('the golden file has the three Hello lines of a page (new room, with a key, no block)', ['page-new-room', 'page-with-key', 'page-no-block'].every((n) => typeof golden[n] === 'string'));
    const block = (map) => [map.length].concat(Array.from(map, (ch) => ch.charCodeAt(0)), [4, 255, 255, 3]);        // the create block: the map, four seats, no team, leader starts + lobby
    const byHand = (name, code, o) => {
        o = o || {};
        const w = [1, 16, 0];
        str8(w, name);
        w.push(0, 0, 255);
        str8(w, code);
        str8(w, '');
        for (const b of (o.key || new Uint8Array(16))) w.push(b);
        w.push(0, 0, 0, 0, o.platform || 0, 1);
        if (o.map !== undefined) for (const b of block(o.map)) w.push(b);
        return Uint8Array.from(w);
    };
    const hello = (extra) => { const w = world(); const c = client(w, extra); c.connect(); w.last().open(); return { w, c, hex: hex(w.last().sent[0]), length: w.last().sent[0].length }; };
    const CREATE_SUFFIX = '0c54524541535552452e4c564c04ffff03';                       // the create block of the map TREASURE.LVL: 0c + the name + 04 ffff 03
    const make = hello({});
    same('without join the Hello carries the create block: the line of the C++ encoder (page-new-room)', make.hex, golden['page-new-room']);
    same('... and the Hello written out by hand', make.hex, hex(byHand('Priya', 'k7m2xq', { map: 'TREASURE.LVL' })));
    check('... which ends with the block of the map', make.hex.endsWith(CREATE_SUFFIX));
    const join = hello({ join: true });
    same('with join: true the Hello has no create block: it is the line of page-new-room without its block', join.hex, golden['page-new-room'].slice(0, golden['page-new-room'].length - CREATE_SUFFIX.length));
    same('... and the Hello written out by hand, without a block', join.hex, hex(byHand('Priya', 'k7m2xq')));
    same('... and the Hello that the codec writes for a page with no create block', join.hex, hex(N.encodeHello({ name: 'Priya', room: 'k7m2xq' })));
    check('... it is 17 bytes shorter, the map\'s name (12 characters) and its four bytes more the length byte', join.length === make.length - 17 && make.length - join.length === block('TREASURE.LVL').length);
    check('... the map that was given is not in it', join.hex.indexOf('54524541535552452e4c564c') < 0 && !join.hex.endsWith('03'));
    same('join: false is no join: the block is there', hello({ join: false }).hex, make.hex);
    same('no option is no join: the block is there', hello({ join: undefined }).hex, make.hex);
    same('only true is a join (the library says === true): 1, "true" and {} are not, the block is there', [hello({ join: 1 }).hex, hello({ join: 'true' }).hex, hello({ join: {} }).hex], [make.hex, make.hex, make.hex]);
    same('a join with a map that is none is the same Hello (the map is the block\'s, and there is no block)', hello({ join: true, map: 'tiny' }).hex, join.hex);
    same('the name goes in a join Hello as in the other (printable ASCII, cut to 32)', hello({ join: true, name: 'José ' + 'x'.repeat(40) }).hex, hex(N.encodeHello({ name: 'Jos ' + 'x'.repeat(28), room: 'k7m2xq' })));
    // the line of the golden file with the key and the platform of a browser on Linux: the block of an empty map is 00 04 ffff 03
    const keyed = { name: 'Sam', code: 'abc234', key: seq(1), platform: N.PLATFORM_BROWSER | N.OS.Linux, map: '' };
    same('with a key and a platform, without join: the line of the C++ encoder (page-with-key)', hello(keyed).hex, golden['page-with-key']);
    same('... with join: that line without its block (00 04 ffff 03)', hello(Object.assign({}, keyed, { join: true })).hex, golden['page-with-key'].slice(0, golden['page-with-key'].length - '0004ffff03'.length));
    same('... and the same written out by hand', hello(Object.assign({}, keyed, { join: true })).hex, hex(byHand('Sam', 'abc234', { key: seq(1), platform: N.PLATFORM_BROWSER | N.OS.Linux })));
    same('with no name and the code xx, join: the line of the C++ encoder that has no block (page-no-block)', hello({ join: true, name: '', code: 'xx' }).hex, golden['page-no-block']);
    check('a Hello without a block has no key, no platform: zeros', /^0110000[0-9a-f]*$/.test(join.hex) && join.w.last().sent[0].slice(1 + 2 + 1 + 5 + 2 + 1 + 1 + 6 + 1, 1 + 2 + 1 + 5 + 2 + 1 + 1 + 6 + 1 + 16).every((b) => b === 0));

    {   // the way back of a page that joined: every Hello it sends has the key and no block
        const w = world();
        const c = client(w, { join: true });
        c.connect();
        w.last().open();
        const key = seq(0x60);
        w.last().receive(welcome(1, key, 0));
        w.last().receive(room([[C, 'Priya'], [C, 'Priya2'], [E, ''], [E, '']], { you: 1, leader: 0 }));
        check('the Welcome of a page that joined says that its Hello made no room', events(c, 'welcome')[0].created === false && !c.isLeader() && c.status === 'online');
        w.last().drop();
        w.advance(500);
        w.last().open();
        same('the Hello of the way back has the key and still no block', hex(w.last().sent[0]), hex(byHand('Priya2', 'k7m2xq', { key: key })));
        same('... (the page goes by the name that the room shows)', hex(w.last().sent[0]), hex(N.encodeHello({ name: 'Priya2', room: 'k7m2xq', key: key })));
        w.last().drop();
        w.advance(1000);
        w.last().open();
        same('and the next way back is the same', hex(w.last().sent[0]), hex(byHand('Priya2', 'k7m2xq', { key: key })));
    }
    {   // a code that nobody holds: NoSuchRoom, and nothing is made
        const w = world();
        const c = client(w, { join: true });
        c.connect();
        w.last().open();
        w.last().receive(reject(N.REJECT.NoSuchRoom));
        check('NoSuchRoom to a join without a key is the answer: refused, with that reason, the socket closed', c.status === 'refused' && events(c, 'refused').length === 1 && events(c, 'refused')[0].reason === N.REJECT.NoSuchRoom && w.sockets.length === 1 && w.sockets[0].closed);
        w.advance(100000);
        check('... and the page does not try again by itself, nor make the room', w.sockets.length === 1 && w.sockets[0].sent.length === 1);
    }
    {   // the room that a joined page held is gone: asked again without the key, still without a block, so it is not made
        const w = world();
        const c = client(w, { join: true, key: seq(0x11) });
        c.connect();
        w.last().open();
        same('a join with a key shows the key and has no block', hex(w.last().sent[0]), hex(byHand('Priya', 'k7m2xq', { key: seq(0x11) })));
        w.last().receive(reject(N.REJECT.NoSuchRoom));
        check('NoSuchRoom to a key: the room is gone, the key is forgotten, a new socket is opened', events(c, 'gone').length === 1 && c.key === null && w.sockets.length === 2 && w.sockets[0].closed);
        w.last().open();
        same('the Hello that asks again has no key and still no block (a joiner does not make the room that was gone)', hex(w.last().sent[0]), hex(byHand('Priya', 'k7m2xq')));
        w.last().receive(reject(N.REJECT.NoSuchRoom));
        check('the second NoSuchRoom is final', c.status === 'refused' && events(c, 'refused')[0].reason === N.REJECT.NoSuchRoom && w.sockets.length === 2);
    }
    {   // a page that does not join makes the room again with the block (as before)
        const w = world();
        const c = client(w, { key: seq(0x11) });
        c.connect();
        w.last().open();
        w.last().receive(reject(N.REJECT.NoSuchRoom));
        w.last().open();
        same('without join the Hello that asks again has the block', hex(w.last().sent[0]), hex(byHand('Priya', 'k7m2xq', { map: 'TREASURE.LVL' })));
    }
    {   // joinFirst (a link that was sent): the first Hello only joins, as a typed code does; once the room has taken the page, the page is like any other
        const w = world();
        const c = client(w, { joinFirst: true });
        c.connect();
        w.last().open();
        same('with joinFirst the first Hello has no block, as with join', hex(w.last().sent[0]), hex(byHand('Priya', 'k7m2xq')));
        w.last().receive(reject(N.REJECT.NoSuchRoom));
        check('NoSuchRoom to it is the answer: refused, with that reason, nothing made, no second try', c.status === 'refused' && events(c, 'refused').length === 1 && events(c, 'refused')[0].reason === N.REJECT.NoSuchRoom && w.sockets.length === 1);
        w.advance(100000);
        check('... not even later', w.sockets.length === 1 && w.sockets[0].sent.length === 1);
    }
    {   // a socket that never got as far as the Welcome: the next Hello is the first still
        const w = world();
        const c = client(w, { joinFirst: true });
        c.connect();
        w.last().drop();
        w.advance(500);
        w.last().open();
        same('a link whose socket was lost before it opened asks again without a block', hex(w.last().sent[0]), hex(byHand('Priya', 'k7m2xq')));
    }
    {   // the Hello went and no Welcome came (the socket was lost, or the server said nothing): the next Hello is the first still
        const w = world();
        const c = client(w, { joinFirst: true });
        c.connect();
        w.last().open();
        w.last().drop();
        w.advance(500);
        w.last().open();
        same('a link whose Hello went and whose socket was lost before the Welcome asks again without a block', hex(w.last().sent[0]), hex(byHand('Priya', 'k7m2xq')));
        w.advance(8000);
        check('... and one whose Hello was not answered for 8 seconds is given up (closed, offline)', c.status === 'offline' && w.sockets[1].closed);
        w.advance(30000);
        check('... and asked again on a new socket', w.sockets.length >= 3);
        w.last().open();
        same('... again without a block', hex(w.last().sent[0]), hex(byHand('Priya', 'k7m2xq')));
    }
    {   // the room took the page; later the room is lost: the page makes it again
        const w = world();
        const c = client(w, { joinFirst: true });
        c.connect();
        w.last().open();
        const key = seq(0x60);
        w.last().receive(welcome(1, key, 0));
        w.last().receive(room([[C, 'Priya'], [C, 'Priya2'], [E, ''], [E, '']], { you: 1, leader: 0 }));
        check('the Welcome says that the Hello made no room (the page joined)', events(c, 'welcome')[0].created === false && !c.isLeader() && c.status === 'online');
        w.last().drop();
        w.advance(500);
        w.last().open();
        same('the Hello of the way back has the key and the block (the page has a seat now)', hex(w.last().sent[0]), hex(byHand('Priya2', 'k7m2xq', { key: key, map: 'TREASURE.LVL' })));
        w.last().receive(reject(N.REJECT.NoSuchRoom));
        check('NoSuchRoom to the key: the room is gone, the key is forgotten, a new socket is opened', events(c, 'gone').length === 1 && c.key === null && w.sockets.length === 3 && w.sockets[1].closed);
        w.last().open();
        same('the Hello that asks again has no key and the block: a page that had a seat makes the room again', hex(w.last().sent[0]), hex(byHand('Priya2', 'k7m2xq', { map: 'TREASURE.LVL' })));
        w.last().receive(welcome(0, seq(0x70), 2));
        check('... and is welcomed to a room that its Hello made', events(c, 'welcome')[1].created === true);
    }
    {   // join: true wins over joinFirst: the page never makes a room
        const w = world();
        const c = client(w, { join: true, joinFirst: true, key: seq(0x11) });
        c.connect();
        w.last().open();
        w.last().receive(reject(N.REJECT.NoSuchRoom));
        w.last().open();
        same('both options: a joiner that is asked again has no block, as with join alone', hex(w.last().sent[0]), hex(byHand('Priya', 'k7m2xq')));
        w.last().receive(welcome(1, seq(0x60), 0));
        w.last().receive(room([[C, 'Priya'], [C, 'Priya'], [E, ''], [E, '']], { you: 1, leader: 0 }));
        w.last().receive(reject(N.REJECT.NoSuchRoom));
        w.last().open();
        same('... and after a Welcome too: join wins, so a room that is lost is still not made', hex(w.last().sent[0]), hex(byHand('Priya', 'k7m2xq')));
    }
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

// ---- what the independent review of the client found ---------------------------------------------------------------------------------------------------------------------------------------------------
// (a name the server would refuse, a handler that throws, a socket that never opens, a seat that moved, a Leave that was not said, a key that anybody could scrub, a request that was no number, a page that spams)

const attempt = (fn) => { try { return { value: fn() }; } catch (e) { return { threw: String(e && e.message || e) }; } };
const online = (w, c, seats, o) => {            // a client that is in its room: Welcome (seat 0, created) and the Room message of `seats`
    c.connect();
    w.last().open();
    w.last().receive(welcome(0, seq(0x30), 2));
    w.last().receive(room(seats || [[C, 'Priya'], [E, ''], [E, ''], [E, '']], Object.assign({ you: 0, leader: 0 }, o || {})));
};

{   // the Hello says only what a server reads: a name of printable ASCII (the characters that are none are left out, as the game's own client leaves them out), a code, a map, a platform and a key that fit
    const sent = (extra) => { const w = world(); const c = client(w, extra); c.connect(); if (w.sockets.length > 0) w.last().open(); return { w, c, hello: w.sockets.length > 0 && w.last().sent.length > 0 ? hex(w.last().sent[0]) : '' }; };
    const hello = (o) => hex(N.encodeHello(Object.assign({ name: 'Priya', room: 'k7m2xq', create: { map: 'TREASURE.LVL' } }, o)));
    for (const [typed, shown] of [['José', 'Jos'], ['Łukasz', 'ukasz'], ['你好', ''], ['a\tb', 'ab'], ['x'.repeat(40), 'x'.repeat(32)], ['é'.repeat(5) + 'y'.repeat(40), 'y'.repeat(32)], ['  Pri  ', '  Pri  ']]) {
        const s = sent({ name: typed });
        same('the Hello of the name ' + JSON.stringify(typed) + ' says ' + JSON.stringify(shown), s.hello, hello({ name: shown }));
        const bytes = s.w.sockets.length > 0 && s.w.last().sent.length > 0 ? s.w.last().sent[0] : new Uint8Array(4);
        check('... and its bytes are printable ASCII, at most 32 (the C++ decoder refuses anything else)', bytes[3] <= 32 && Array.from(bytes.slice(4, 4 + bytes[3])).every((b) => b >= 0x20 && b <= 0x7e));
        check('... and the client goes by that name until the room says another', s.c.name === shown);
    }
    for (const code of ['', 'k7m 2xq', 'a'.repeat(33), 'k7m.2xq', undefined, null, 42, 'ké']) {
        const w = world();
        const c = client(w, { code });
        const r = attempt(() => c.connect());
        check('a code that no server takes (' + JSON.stringify(code) + ') is refused here: an error text, reason 0, no socket, nothing thrown', !r.threw && c.status === 'refused' && events(c, 'refused').length === 1 && events(c, 'refused')[0].reason === 0
            && typeof events(c, 'refused')[0].error === 'string' && events(c, 'refused')[0].error !== '' && w.sockets.length === 0, JSON.stringify(r) + ' ' + c.status);
    }
    check('a code with capitals is a code (the server decides on it)', sent({ code: 'K7M2XQ' }).hello === hello({ room: 'K7M2XQ' }));
    for (const map of ['tiny', 'café.LVL', '../x.LVL', 'a'.repeat(70) + '.LVL', 5, {}]) {
        same('a map that is no map name (' + JSON.stringify(map) + ') is left out of the block: the room is made with the server\'s own map, as the game\'s client does', sent({ map }).hello, hello({ create: { map: '' } }));
    }
    same('a map name that is one is kept', sent({ map: 'ISLANDS.LVL' }).hello, hello({ create: { map: 'ISLANDS.LVL' } }));
    for (const platform of [7, 0x20, 0x17, 1.5, -1, '3', NaN, 300]) {
        same('a platform that is none (' + JSON.stringify(platform) + ') is told as not told', sent({ platform }).hello, hello({ platform: 0 }));
    }
    same('a platform that fits is kept', sent({ platform: N.PLATFORM_BROWSER | N.OS.Linux }).hello, hello({ platform: N.PLATFORM_BROWSER | N.OS.Linux }));
    for (const key of [seq(1).slice(0, 15), new Uint8Array(17), new Uint8Array(16), 'abcdefghijklmnop', 5, {}]) {
        same('a key that is no key (' + (key.length === undefined ? JSON.stringify(key) : key.length + ' entries') + ') is no key: the Hello is a new player\'s', sent({ key }).hello, hello({}));
        check('... and the client holds none (it has no entry for a game page, and a Hello that gets no Welcome is followed by a Leave)', client(world(), { key }).key === null && client(world(), { key }).rejoinEntry() === null);
    }
    {
        const w = world();
        const k = seq(0x11);
        const c = client(w, { key: k });
        k.fill(0);
        c.connect();
        w.last().open();
        same('a key that is given is copied: the caller\'s array is the caller\'s', hex(w.last().sent[0]), hello({ key: seq(0x11) }));
    }
}

{   // a handler that throws takes nothing else down with it: the other handlers hear the event, the client goes on (status, socket, timers), the error goes to onError
    const w = world();
    const errors = [];
    const c = client(w, { onError: (e, event) => errors.push(event + ': ' + String(e && e.message || e)) });
    const late = [];
    c.on('status', (s) => { if (s === 'connecting') throw new Error('boom'); });
    c.on('status', (s) => late.push(s));
    const r = attempt(() => c.connect());
    check('a handler that throws on "connecting" does not stop the socket from being made', !r.threw && w.sockets.length === 1 && c.status === 'connecting', JSON.stringify(r));
    check('... the handler after it hears the status, and the error is handed to onError', late.join() === 'connecting' && errors.join() === 'status: boom', late.join() + ' / ' + errors.join());
    w.last().open();
    w.last().receive(welcome(0, seq(0x30), 2));
    const joined = [];
    c.on('room', () => { throw new Error('boom room'); });
    c.on('joined', (e) => joined.push(e.name));
    w.last().receive(room([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { you: 0, leader: 0 }));
    w.last().receive(room([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0 }));
    check('a handler that throws on a Room message does not take the events of that message with it', joined.join() === 'Sam' && c.room !== null && c.room.slots[1].name === 'Sam' && errors.filter((e) => e === 'room: boom room').length === 2, joined.join() + ' / ' + errors.join());
    c.on('status', (s) => { if (s === 'offline') throw new Error('boom offline'); });
    w.last().drop();
    check('a handler that throws on "offline" does not stop the way back', c.status === 'offline' && w.timers.length === 1);
    w.advance(500);
    check('(the way back is made)', w.sockets.length === 2 && c.status === 'connecting');
    // the 'gone' path
    const w2 = world();
    const d = client(w2, { key: seq(0x11), onError: () => {} });
    d.on('gone', () => { throw new Error('boom gone'); });
    d.connect();
    w2.last().open();
    w2.last().receive(reject(N.REJECT.NoSuchRoom));
    check('a handler that throws on "gone" does not stop the room from being made again', w2.sockets.length === 2 && d.status === 'connecting' && d.key === null);
    // an onError that throws is no reason to stop the client either
    const w4 = world();
    const f4 = client(w4, { onError: () => { throw new Error('boom onError'); } });
    const heard4 = [];
    f4.on('status', () => { throw new Error('boom first'); });
    f4.on('status', (s) => heard4.push(s));
    const r4 = attempt(() => f4.connect());
    check('an onError that throws does not stop the client either: the socket is made and the next handler hears the status', !r4.threw && w4.sockets.length === 1 && heard4.join() === 'connecting', JSON.stringify(r4) + ' ' + heard4.join());
    // without onError the error is written to the console, not thrown
    const w3 = world();
    const e = client(w3);
    const logged = [];
    const consoleError = console.error;
    console.error = (...args) => logged.push(args.map(String).join(' '));
    try {
        e.on('status', () => { throw new Error('boom default'); });
        const r3 = attempt(() => e.connect());
        check('without onError a throwing handler is reported on the console, and nothing is thrown', !r3.threw && logged.length === 1 && /boom default/.test(logged[0]) && w3.sockets.length === 1, JSON.stringify(r3) + ' ' + logged.join('|'));
    } finally { console.error = consoleError; }
    check('on() takes functions only', attempt(() => c.on('room', 5)).threw !== undefined && attempt(() => c.on('room', null)).threw !== undefined);
    check('an event named like a member of Object is an event like the others', attempt(() => c.on('__proto__', () => {})).threw === undefined && attempt(() => c.on('constructor', () => {})).threw === undefined && attempt(() => c.on('hasOwnProperty', () => {})).threw === undefined);
}

{   // a handler that leaves while the message is still being handed out stops the rest of it: no event reaches a page that has left
    const w = world();
    const c = client(w);
    online(w, c);
    c.on('room', () => c.leave());
    w.last().receive(room([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { you: 0, leader: 0 }));
    check('leave() in the handler of a Room message: the events of that message (joined) are not handed out', c.status === 'closed' && events(c, 'joined').length === 0);
}

{   // a socket that is made but never opens is given up after connectMs and tried again; a page that wakes after a long sleep does not wait for the timer
    const w = world();
    const c = client(w);
    c.connect();
    w.advance(19999);
    check('a socket that is still opening after 19.999 seconds is waited for', c.status === 'connecting' && w.sockets.length === 1 && !w.sockets[0].closed);
    w.advance(1);
    check('after 20 seconds it is given up and closed', c.status === 'offline' && w.sockets[0].closed);
    w.advance(500);
    check('and the way back is tried', w.sockets.length === 2 && c.status === 'connecting');
    w.sockets[0].open();                                                 // (a socket that was given up and opens all the same is nobody's now)
    check('a socket that was given up and opens all the same is ignored: it says nothing, and the new socket is not touched', w.sockets[0].sent.length === 0 && w.sockets.length === 2 && !w.sockets[1].closed && c.status === 'connecting');
    w.advance(14000);
    w.last().open();
    w.advance(7999);
    check('once the socket is open, the limit is the silence limit of a link (8 seconds from the Hello), whatever the opening took', c.status === 'connecting' && w.sockets.length === 2);
    w.advance(1);
    check('(8 seconds without a Welcome: given up)', c.status === 'offline' && w.sockets[1].closed);
    const d = world();
    const e = client(d);
    e.connect();
    d.clock += 30 * 60 * 1000;                                           // (a phone that slept: the timers did not run)
    e.wake();
    check('wake gives up a socket that has been opening for half an hour', e.status === 'offline' && d.sockets[0].closed);
    e.wake();
    check('and tries again at once', d.sockets.length === 2 && e.status === 'connecting');
    const f = world();
    const g = client(f, { connectMs: 0 });
    g.connect();
    f.advance(600000);
    check('connectMs 0 waits for the browser (no timer)', g.status === 'connecting' && f.timers.length === 0 && f.sockets.length === 1);
    const h = world();
    const i = client(h, { connectMs: 3000 });
    i.connect();
    h.advance(2999);
    check('connectMs is an option', i.status === 'connecting');
    h.advance(1);
    check('(3 seconds)', i.status === 'offline');
}

{   // the seat of a page is the seat that the last Room message says (the leader may have moved it to another colour): the entry for the game page names that seat
    const w = world();
    const c = client(w, { name: 'Sam' });
    c.connect();
    w.last().open();
    w.last().receive(welcome(1, seq(0x50), 0));
    w.last().receive(room([[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']], { you: 1, leader: 0 }));
    check('the Welcome gives the seat', c.seat === 1 && c.rejoinEntry(1).name === 'ants.rejoin.k7m2xq.1');
    w.last().receive(room([[C, 'Priya'], [E, ''], [C, 'Sam'], [E, '']], { you: 2, leader: 0 }));
    check('the leader moves the page to the colour 2: the seat is 2', c.seat === 2);
    check('... and so is the entry that the game page reads', c.rejoinEntry(1).name === 'ants.rejoin.k7m2xq.2', c.rejoinEntry(1).name);
    w.last().receive(room([[C, 'Priya'], [E, ''], [C, 'Sam'], [E, '']], { you: 255, leader: 0 }));
    check('a Room message that does not say who we are leaves the seat as it was', c.seat === 2);
}

{   // leaving between the Hello and the Welcome: the server has the person already, so the Leave is said (else the seat is held for a minute with nobody in it)
    const w = world();
    const c = client(w);
    c.connect();
    const s = w.last();
    check('before the socket is open there is nobody to say Leave to: leave() sends nothing and the client is closed', c.leave() === false && c.status === 'closed' && s.sent.length === 0 && s.closed);
    const w2 = world();
    const d = client(w2);
    d.connect();
    w2.last().open();
    const s2 = w2.last();
    check('between the Hello and the Welcome leave() says Leave, then closes the link', d.leave() === true && s2.sent.length === 2 && hex(s2.sent[1]) === hex(N.encodeLeave()) && s2.closed && d.status === 'closed');
    // the Welcome is slow: the link is given up after the silence limit; the Hello went, the key is not known: a Leave goes before the close, so the retry does not seat the name twice
    const w3 = world();
    const e = client(w3);
    e.connect();
    w3.last().open();
    w3.advance(8000);
    const s3 = w3.sockets[0];
    check('a Welcome that does not come in 8 seconds: the link is given up, and the seat that it may have is given up with a Leave', e.status === 'offline' && s3.closed && s3.sent.length === 2 && hex(s3.sent[1]) === hex(N.encodeLeave()), s3.sent.map(hex).join('|'));
    w3.advance(500);
    check('(and the retry is a new socket)', w3.sockets.length === 2 && e.status === 'connecting');
    // a page that comes with a key is rejoining: its seat is held for it, no Leave
    const w4 = world();
    const f = client(w4, { key: seq(0x11) });
    f.connect();
    w4.last().open();
    w4.advance(8000);
    check('a page that rejoins with a key does not give its seat up when a Welcome is slow', f.status === 'offline' && w4.sockets[0].sent.length === 1);
    // a link that was online and goes silent: the key is known, the seat is held for the way back
    const w5 = world();
    const g = client(w5);
    online(w5, g);
    w5.advance(8000);
    check('an online link that goes silent keeps its seat: no Leave', g.status === 'offline' && w5.sockets[0].sent.length === 1);
}

{   // a handler that leaves while the client opens stops the opening
    const w = world();
    const c = client(w);
    c.on('status', (s) => { if (s === 'connecting') c.leave(); });
    c.connect();
    check('leave() in the handler of "connecting": no socket is made and the client stays closed', w.sockets.length === 0 && c.status === 'closed');
    const w2 = world();
    const d = client(w2, { key: seq(0x11) });
    d.on('gone', () => d.leave());
    d.connect();
    w2.last().open();
    w2.last().receive(reject(N.REJECT.NoSuchRoom));
    check('leave() in the handler of "gone": the room is not made again', w2.sockets.length === 1 && d.status === 'closed');
    w2.advance(100000);
    check('(and nothing comes later)', w2.sockets.length === 1 && w2.timers.length === 0);
}

{   // the key leaves the client for the server and for the entry of the game page, nowhere else
    const w = world();
    const c = client(w);
    let seen = null;
    c.on('welcome', (ev) => { seen = ev; });
    c.connect();
    w.last().open();
    w.last().receive(welcome(0, seq(0x30), 2));
    check('the welcome payload has no key (the page reaches it through rejoinEntry only)', seen !== null && !('key' in seen) && seen.seat === 0 && seen.created === true && seen.rejoin === false, JSON.stringify(seen));
    const copy = c.key;
    copy.fill(0xee);
    check('client.key is a copy: scrubbing it changes nothing', hex(c.key) === hex(seq(0x30)) && c.rejoinEntry(1).text.indexOf(hex(seq(0x30))) > 0);
    w.last().receive(room([[C, 'Priya'], [E, ''], [E, ''], [E, '']], { you: 0, leader: 0 }));
    w.last().drop();
    w.advance(500);
    w.last().open();
    check('the Hello of the way back has the key', hex(w.last().sent[0]) === hex(N.encodeHello({ name: 'Priya', room: 'k7m2xq', key: seq(0x30), create: { map: 'TREASURE.LVL' } })));
    check('the key is a property that cannot be set from outside', attempt(() => { c.key = seq(1); }).threw === undefined && hex(c.key) === hex(seq(0x30)));
}

{   // every number of a request is a whole number in its range; anything else is false, never an exception, nothing sent
    const w = world();
    const c = client(w);
    online(w, c, [[C, 'Priya'], [C, 'Sam'], [C, 'Tess'], [E, '']]);
    const base = w.last().sent.length;
    const plan = (o) => Object.assign({ map: '', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }, o);
    const bad = [
        ['move(0, 1.5)', () => c.move(0, 1.5)], ['move(1.5, 0)', () => c.move(1.5, 0)], ['move(0, null)', () => c.move(0, null)], ['move("1", "3")', () => c.move('1', '3')], ['move(1, "3")', () => c.move(1, '3')],
        ['move(NaN, 1)', () => c.move(NaN, 1)], ['move(1, Infinity)', () => c.move(1, Infinity)], ['move()', () => c.move()],
        ['remove(null)', () => c.remove(null)], ['remove("1")', () => c.remove('1')], ['remove(1.5)', () => c.remove(1.5)], ['remove(NaN)', () => c.remove(NaN)], ['remove()', () => c.remove()],
        ['setPlan(null)', () => c.setPlan(null)], ['setPlan()', () => c.setPlan()], ['setPlan({})', () => c.setPlan({})], ['setPlan with kinds null', () => c.setPlan(plan({ kinds: null }))],
        ['setPlan with kinds of strings', () => c.setPlan(plan({ kinds: ['0', '0', '0', '0'] }))], ['setPlan with a kind of 1.5', () => c.setPlan(plan({ kinds: [0, 1.5, 0, 0] }))], ['setPlan with a kind of -1', () => c.setPlan(plan({ kinds: [0, -1, 0, 0] }))],
        ['setPlan with teams (-1, 2)', () => c.setPlan(plan({ teamA: -1, teamB: 2 }))], ['setPlan with teams ("0", "2")', () => c.setPlan(plan({ teamA: '0', teamB: '2' }))], ['setPlan with teams (null, 2)', () => c.setPlan(plan({ teamA: null, teamB: 2 }))],
        ['setPlan with teams (0.5, 2)', () => c.setPlan(plan({ teamA: 0.5, teamB: 2 }))], ['setPlan with a map of 5', () => c.setPlan(plan({ map: 5 }))],
        ['rename(null)', () => c.rename(null)], ['rename(5)', () => c.rename(5)], ['rename({})', () => c.rename({})], ['rename()', () => c.rename()]
    ];
    for (const [label, fn] of bad) {
        const r = attempt(fn);
        check(label + ' is false: no exception, nothing sent', r.value === false && !r.threw, JSON.stringify(r));
    }
    check('nothing was sent by any of them', w.last().sent.length === base, String(w.last().sent.length - base));
    check('the validators say the same: teams are whole numbers a < b < 4 or two 255', !N.validTeams(-1, 2) && !N.validTeams('0', '2') && !N.validTeams(null, 2) && !N.validTeams(0.5, 2) && !N.validTeams(0, 2.5) && !N.validTeams(undefined, undefined) && N.validTeams(0, 2) && N.validTeams(255, 255));
    check('a platform is a whole number', !N.validPlatform(1.5) && !N.validPlatform('3') && !N.validPlatform(null) && !N.validPlatform(NaN) && !N.validPlatform(-1) && !N.validPlatform(256) && N.validPlatform(0x13));
}

{   // a page that sends too fast is held back here (the server's budgets: names 3 then 1 a second, plans and colour moves 6 then 4 a second, removals 3 then 1 a second), not thrown out of the room
    const w = world();
    const c = client(w, { silenceMs: 0 });                 // (a minute of rest without a ping from the scripted server is the budgets' business here, not the watchdog's)
    online(w, c, [[C, 'Priya'], [C, 'Sam'], [C, 'Tess'], [E, '']]);
    const base = w.last().sent.length;
    const count = (fn, n) => { let ok = 0; for (let i = 0; i < n; i++) { if (fn(i)) ok++; } return ok; };
    check('rename: a burst of 3, then no more', count((i) => c.rename('N' + i), 6) === 3 && w.last().sent.length === base + 3);
    w.advance(999);
    check('... not before the second is over', c.rename('Late') === false);
    w.advance(1);
    check('... one a second then', c.rename('Late') === true && c.rename('Later') === false);
    check('the budget of a plan is another one (6 then 4 a second)', count(() => c.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }), 9) === 6);
    w.advance(249);
    check('... a plan every 250 milliseconds', c.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }) === false);
    w.advance(1);
    check('... (250)', c.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }) === true && c.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }) === false);
    check('the budget of a colour move is another one (6 then 4 a second)', count(() => c.move(1, 3), 9) === 6);
    w.advance(250);
    check('... a move every 250 milliseconds', c.move(1, 3) === true && c.move(1, 3) === false);
    check('the budget of a removal is another one (3 then 1 a second)', count(() => c.remove(1), 6) === 3);
    w.advance(999);
    check('... not before the second is over', c.remove(1) === false);
    w.advance(1);
    check('... one a second then', c.remove(1) === true && c.remove(1) === false);
    w.advance(60000);
    check('a long rest fills each budget again, to its burst and no further', count((i) => c.rename('M' + i), 6) === 3 && count(() => c.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }), 9) === 6);
    // a request that cannot be made costs nothing; one that could not be written costs nothing
    const w2 = world();
    const d = client(w2);
    online(w2, d, [[C, 'Priya'], [C, 'Sam'], [E, ''], [E, '']]);
    for (let i = 0; i < 10; i++) { d.rename('Pri '); d.rename(''); d.remove(0); d.remove(3); d.move(2, 3); }
    const sock = w2.last();
    const real = sock.send;
    sock.send = function () { throw new Error('closing'); };
    for (let i = 0; i < 10; i++) { d.rename('Zed'); d.remove(1); d.move(1, 2); d.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }); }
    sock.send = real;
    check('requests that cannot be made, and ones whose write failed, spend nothing of the budgets', count((i) => d.rename('R' + i), 4) === 3 && count(() => d.remove(1), 4) === 3 && count(() => d.move(1, 2), 7) === 6 && count(() => d.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }), 7) === 6);
}

{   // a link that does not have a WebSocket at all, a line of chat that no player said
    const w = world();
    const c = client(w, { WebSocket: null });
    const r = attempt(() => c.connect());
    check('no WebSocket in the browser: refused, with an error text and reason 0 (not idle for ever)', !r.threw && c.status === 'refused' && events(c, 'refused').length === 1 && events(c, 'refused')[0].reason === 0 && /WebSocket/.test(events(c, 'refused')[0].error));
    const w2 = world();
    const d = client(w2);
    online(w2, d);
    for (const bytes of [[9, 4, 0, 2, 104, 105], [9, 254, 0, 2, 104, 105], [9, 1, 0, 0], [9, 255, 0, 0]]) w2.last().receive(Uint8Array.from(bytes));
    check('a line from a seat that no player holds (4 to 254) and an empty line are no chat and no notice (the game\'s own client drops them too)', events(d, 'chat').length === 0 && events(d, 'notice').length === 0);
    w2.last().receive(Uint8Array.from([9, 3, 1, 2, 104, 105]));
    w2.last().receive(Uint8Array.from([9, 255, 0, 2, 104, 105]));
    same('a line from a player and a line of the room are heard', [events(d, 'chat'), events(d, 'notice')], [[{ sender: 3, team: true, text: 'hi' }], [{ text: 'hi' }]]);
}

console.log('LobbyClient: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
