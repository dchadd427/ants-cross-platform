// The front page's side of network protocol 16 (docs/NETWORK_PORT.md "Protocol 16"): the codec of the messages that a lobby page sends and hears, the guard of a colour move, and the client that keeps
// a page in its lobby room (a keyed way back, the answers to the server's pings, what each refusal means). No screen is drawn here: the page reads the client's state and its events.
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsLobbyNet, node gets module.exports (tests/scripts/web_lobby_net_check.js runs it).
//   * Every layout is the C++ one (src/ants_net/protocol.cpp), little endian, and every decoder refuses what the C++ decoder refuses; tests/data/lobby_messages.txt holds bytes that the C++ encoders wrote,
//     which both sides read (tests/test_net/test_lockstep.cpp N2.111, tests/scripts/web_lobby_net_check.js).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsLobbyNet = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var PROTOCOL = 16;                                             // kProtocolVersion
    var MAX_NAME = 32, MAX_ROOM_CODE = 32, MAX_MAP_NAME = 64, MAX_CHAT = 100;
    var PLAYERS = 4;                                               // sim::MAX_PLAYERS
    var NO_TEAM = 255, NO_LEADER = 255, ROOM_SENDER = 255;
    var CLIENT_PAGE = 1;                                           // kClientPage
    var CREATE_LEADER_STARTS = 1, CREATE_LOBBY = 2;                // CreateBlock.flags
    var WELCOME_REJOIN = 1, WELCOME_CREATED = 2;                   // Welcome.flags
    var ROOM_LEADER_STARTS = 1, ROOM_LOBBY = 2, ROOM_STARTING = 4; // Room.flags

    var MSG = { Hello: 1, Welcome: 2, Reject: 3, Chat: 9, Ping: 10, Pong: 11, Room: 12, Start: 13, Cancel: 16, Leave: 17, StartRequest: 24, SeatMove: 31, Plan: 32, Name: 33, Remove: 34, Last: 34 };
    var REJECT = { Full: 1, VersionMismatch: 2, MatchRunning: 3, Kicked: 4, BadRequest: 5, NoSuchRoom: 6, Dropped: 7, RejoinFailed: 8, Superseded: 9 };
    var SLOT = { Empty: 0, Host: 1, Client: 2, Bot: 3 };
    var PLAN = { Open: 0, Easy: 1, Medium: 2, Hard: 3, Nobody: 4 };
    var FILL = { None: 0, Easy: 1, Medium: 2, Hard: 3 };
    var OS = { Unknown: 0, Windows: 1, Macos: 2, Linux: 3, Android: 4, Ios: 5, Other: 6 }, PLATFORM_BROWSER = 0x10;

    // ---- bytes ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    function Writer() { this.b = []; }
    Writer.prototype.u8 = function (v) { this.b.push(v & 255); return this; };
    Writer.prototype.u16 = function (v) { this.b.push(v & 255, (v >>> 8) & 255); return this; };
    Writer.prototype.u32 = function (v) { this.b.push(v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255); return this; };
    Writer.prototype.str8 = function (s) {
        var n = Math.min(s.length, 255);
        this.b.push(n);
        for (var i = 0; i < n; i++) this.b.push(s.charCodeAt(i) & 255);
        return this;
    };
    Writer.prototype.bytes = function (a) { for (var i = 0; i < a.length; i++) this.b.push(a[i] & 255); return this; };
    Writer.prototype.done = function () { return new Uint8Array(this.b); };

    function Reader(bytes, from) { this.a = bytes; this.i = from || 0; this.bad = false; }
    Reader.prototype.u8 = function () { if (this.i >= this.a.length) { this.bad = true; return 0; } return this.a[this.i++]; };
    Reader.prototype.u16 = function () { var lo = this.u8(); var hi = this.u8(); return lo | (hi << 8); };
    Reader.prototype.u32 = function () { var b0 = this.u8(), b1 = this.u8(), b2 = this.u8(), b3 = this.u8(); return (b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)) >>> 0; };
    Reader.prototype.str8 = function () {
        var n = this.u8();
        if (this.i + n > this.a.length) { this.bad = true; this.i = this.a.length; return ''; }
        var s = '';
        for (var k = 0; k < n; k++) s += String.fromCharCode(this.a[this.i++]);
        return s;
    };
    Reader.prototype.bytes = function (n) {
        if (this.i + n > this.a.length) { this.bad = true; this.i = this.a.length; return new Uint8Array(n); }
        var out = new Uint8Array(n);
        for (var k = 0; k < n; k++) out[k] = this.a[this.i++];
        return out;
    };
    Reader.prototype.left = function () { return this.a.length - this.i; };
    Reader.prototype.done = function () { return !this.bad && this.i === this.a.length; };

    function toBytes(data) { return data instanceof Uint8Array ? data : new Uint8Array(data); }
    function hexOf(bytes) { var s = ''; for (var i = 0; i < bytes.length; i++) s += (bytes[i] < 16 ? '0' : '') + bytes[i].toString(16); return s; }
    function bytesOfHex(hex) {
        var out = new Uint8Array(hex.length >> 1);
        for (var i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(i * 2, 2), 16);
        return out;
    }
    function zeroKey() { return new Uint8Array(16); }
    function isZeroKey(key) { for (var i = 0; i < key.length; i++) { if (key[i] !== 0) return false; } return true; }

    // ---- what the C++ decoders accept (valid_map_name, valid_room_code, printable_name, valid_platform) ---------------------------------------------------------------------------------------------------

    function printable(s, max) {
        if (typeof s !== 'string' || s.length > max) return false;
        for (var i = 0; i < s.length; i++) { var c = s.charCodeAt(i); if (c < 0x20 || c > 0x7E) return false; }
        return true;
    }
    function validMapName(name) {
        if (typeof name !== 'string' || name.length < 5 || name.length > MAX_MAP_NAME || !printable(name, MAX_MAP_NAME)) return false;
        if (/[\/\\:*?"<>|]/.test(name) || name.charAt(0) === '.') return false;
        var tail = name.slice(-4);
        return tail === '.LVL' || tail === '.lvl';
    }
    function validRoomCode(code) { return typeof code === 'string' && code.length <= MAX_ROOM_CODE && /^[A-Za-z0-9_-]*$/.test(code); }
    function publicRoomCode(code) { return validRoomCode(code) && code.length > 0 && !/[A-Z]/.test(code); }   // the codes a page makes: no capital (a capital is the control interface's)
    function isWhole(n) { return typeof n === 'number' && isFinite(n) && Math.floor(n) === n; }
    function isSeat(n) { return isWhole(n) && n >= 0 && n < PLAYERS; }
    function validPlatform(p) { return isWhole(p) && p >= 0 && p <= 255 && (p & 0xE0) === 0 && (p & 0x0F) <= OS.Other; }
    // a key that a server can have given: 16 bytes of a Uint8Array (a string, a plain list or a short array is no key), not all zero
    function isKey(key) { return Object.prototype.toString.call(key) === '[object Uint8Array]' && key.length === 16 && !isZeroKey(key); }
    // what the game's own client makes of a name before it sends it (lobby.cpp, printable()): the characters outside 0x20 - 0x7E are left out and at most `max` are kept
    function printableOnly(s, max) {
        var out = '';
        if (typeof s !== 'string') return out;
        for (var i = 0; i < s.length && out.length < max; i++) { var c = s.charCodeAt(i); if (c >= 0x20 && c <= 0x7E) out += s.charAt(i); }
        return out;
    }
    // a name that a person may go by: 1 - 32 printable characters, no space at either end (NameMsg)
    function validPersonName(name) { return printable(name, MAX_NAME) && name.length > 0 && name.charAt(0) !== ' ' && name.charAt(name.length - 1) !== ' '; }
    function validTeams(a, b) { return (a === NO_TEAM && b === NO_TEAM) || (isWhole(a) && isWhole(b) && a >= 0 && a < b && b < PLAYERS); }

    // ---- messages that a page sends ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

    // Hello of a lobby page (client kind 1). h: { name, room, key (16 bytes, none: a new player), platform (0: not told), create: { map ('' keeps the server's) } or none }.
    // A lobby block is four seats with the leader starting (valid_create_block); its teams are ignored by the server (the plan decides), so they travel as "none".
    // The server refuses a Hello whose name is not printable ASCII (or is longer than 32) and a block whose map is no map name: here the name is made as the game's client makes it (the characters outside
    // the table are left out), a map that is no map name is left out (the room is made with the server's own), a platform that is none is "not told" and a key that is none is a new player's.
    function encodeHello(h) {
        var w = new Writer().u8(MSG.Hello).u16(PROTOCOL).str8(printableOnly(h.name, MAX_NAME)).u16(0).u8(255).str8(typeof h.room === 'string' ? h.room : '').str8('');
        w.bytes(isKey(h.key) ? h.key : zeroKey()).u32(0).u8(validPlatform(h.platform) ? h.platform : 0).u8(CLIENT_PAGE);
        if (h.create) w.str8(validMapName(h.create.map) ? h.create.map : '').u8(PLAYERS).u8(NO_TEAM).u8(NO_TEAM).u8(CREATE_LEADER_STARTS | CREATE_LOBBY);
        return w.done();
    }
    function encodeLeave() { return new Writer().u8(MSG.Leave).done(); }
    function encodePing(type, nonce, sentMs) { return new Writer().u8(type).u32(nonce).u32(sentMs).done(); }
    function encodePong(nonce, sentMs) { return encodePing(MSG.Pong, nonce, sentMs); }
    // the guard of a colour move and of a removal: the FNV-1a hash of the seats as the leader's screen shows them (SeatMoveMsg.guard, RemoveMsg.guard); 0 never travels
    function seatingHash(room) {
        var h = 2166136261;
        function put(byte) { h = Math.imul((h ^ byte) >>> 0, 16777619) >>> 0; }
        for (var s = 0; s < PLAYERS; s++) {
            put(room.slots[s].state);
            var name = room.slots[s].name;
            for (var i = 0; i < name.length; i++) put(name.charCodeAt(i) & 255);
            put(0);
        }
        return h !== 0 ? h : 1;
    }
    function encodeSeatMove(from, to, guard) { return new Writer().u8(MSG.SeatMove).u8(from).u8(to).u32(guard).done(); }
    // the leader's plan: the map ('' keeps the room's), what each colour is (PLAN.*) and the teams (NO_TEAM twice, or two seats a < b)
    function encodePlan(map, kinds, teamA, teamB) {
        var w = new Writer().u8(MSG.Plan).str8(map || '');
        for (var i = 0; i < PLAYERS; i++) w.u8(kinds[i]);
        return w.u8(teamA).u8(teamB).done();
    }
    function encodeName(name) { return new Writer().u8(MSG.Name).str8(name).done(); }
    function encodeRemove(seat, guard) { return new Writer().u8(MSG.Remove).u8(seat).u32(guard).done(); }
    // the leader's START: the levels of the bots of the plan (FILL.*; Open and Nobody are none) and its teams; the room decides by its own plan, the message must still be a valid StartRequest
    function encodeStartRequest(fill, teamA, teamB) {
        var w = new Writer().u8(MSG.StartRequest);
        for (var i = 0; i < PLAYERS; i++) w.u8(fill[i]);
        return w.u8(teamA).u8(teamB).done();
    }
    function fillOfPlan(plan) {
        var out = [];
        for (var i = 0; i < PLAYERS; i++) out.push(plan[i] === PLAN.Easy ? FILL.Easy : plan[i] === PLAN.Medium ? FILL.Medium : plan[i] === PLAN.Hard ? FILL.Hard : FILL.None);
        return out;
    }
    function encodeChat(sender, team, text) { return new Writer().u8(MSG.Chat).u8(sender).u8(team ? 1 : 0).str8(text.slice(0, MAX_CHAT)).done(); }

    // ---- messages that a page hears (null: the message is no message of this protocol; { type: 'other' }: a message that a page does not use) --------------------------------------------------------------------

    function decodeWelcome(r) {
        var m = { type: 'welcome', player: r.u8(), players: r.u8(), key: r.bytes(16), flags: r.u8() };
        if (!r.done() || m.player >= PLAYERS || m.players < 2 || m.players > PLAYERS) return null;
        if (m.flags > WELCOME_CREATED || (m.flags !== 0 && isZeroKey(m.key))) return null;
        m.created = (m.flags & WELCOME_CREATED) !== 0;
        m.rejoin = (m.flags & WELCOME_REJOIN) !== 0;
        return m;
    }
    function decodeReject(r) {
        var reason = r.u8();
        if (!r.done() || reason < REJECT.Full || reason > REJECT.Superseded) return null;
        return { type: 'reject', reason: reason };
    }
    function decodeRoom(r) {
        var slots = [];
        for (var s = 0; s < PLAYERS; s++) {
            var slot = { state: r.u8(), name: r.str8(), rtt: r.u16(), platform: r.u8() };
            if (slot.state > SLOT.Bot || !printable(slot.name, MAX_NAME)) return null;
            if (!validPlatform(slot.platform) || (slot.platform !== 0 && slot.state !== SLOT.Host && slot.state !== SLOT.Client)) return null;
            slots.push(slot);
        }
        var m = { type: 'room', slots: slots, map: r.str8(), fog: r.u8(), you: r.u8(), leader: r.u8(), teamA: r.u8(), teamB: r.u8(), flags: r.u8(), plan: [], inGame: 0 };
        for (var k = 0; k < PLAYERS; k++) m.plan.push(r.u8());
        m.inGame = r.u8();
        if (!r.done() || m.fog > 1 || (m.map !== '' && !validMapName(m.map)) || (m.you !== 255 && m.you >= PLAYERS)) return null;
        if (m.leader !== NO_LEADER && (m.leader >= PLAYERS || slots[m.leader].state !== SLOT.Client)) return null;
        if ((m.teamA !== NO_TEAM || m.teamB !== NO_TEAM) && (m.teamA >= m.teamB || m.teamB >= PLAYERS)) return null;
        if ((m.flags & ~(ROOM_LEADER_STARTS | ROOM_LOBBY | ROOM_STARTING)) !== 0) return null;
        m.lobby = (m.flags & ROOM_LOBBY) !== 0;
        m.starting = (m.flags & ROOM_STARTING) !== 0;
        m.leaderStarts = (m.flags & ROOM_LEADER_STARTS) !== 0;
        if (m.starting && !m.lobby) return null;
        if (m.lobby && !m.leaderStarts) return null;
        for (var p = 0; p < PLAYERS; p++) {
            if (m.plan[p] > PLAN.Nobody || (m.plan[p] !== 0 && !m.lobby)) return null;
            if (((m.inGame >> p) & 1) !== 0 && (!m.lobby || slots[p].state !== SLOT.Client)) return null;
        }
        if ((m.inGame & ~15) !== 0) return null;
        m.fog = m.fog === 1;
        return m;
    }
    function decodePing(bytes) {
        if (bytes.length !== 9) return null;
        var r = new Reader(bytes, 1);
        var m = { type: bytes[0] === MSG.Ping ? 'ping' : 'pong', nonce: r.u32(), sentMs: r.u32() };
        return r.done() ? m : null;
    }
    function decodeChat(r) {
        var m = { type: 'chat', sender: r.u8(), team: r.u8(), text: r.str8() };
        if (!r.done() || m.team > 1 || !printable(m.text, MAX_CHAT)) return null;
        m.team = m.team === 1;
        return m;
    }
    // one message of the server; bytes: a Uint8Array or an ArrayBuffer
    function decode(data) {
        var bytes = toBytes(data);
        if (bytes.length === 0) return null;
        var type = bytes[0];
        if (type === MSG.Ping || type === MSG.Pong) return decodePing(bytes);
        var r = new Reader(bytes, 1);
        if (type === MSG.Welcome) return decodeWelcome(r);
        if (type === MSG.Reject) return decodeReject(r);
        if (type === MSG.Room) return decodeRoom(r);
        if (type === MSG.Chat) return decodeChat(r);
        if (type >= MSG.Hello && type <= MSG.Last) return { type: 'other', code: type };      // (Start, Cancel, Begin ... : a lobby page leaves the match to the game page)
        return null;
    }

    // ---- what changed between two Room messages ----------------------------------------------------------------------------------------------------------------------------------------------------------
    // The server sends a Room message for every change, so two in a row differ by one thing. A person has no number in the message, only a seat and a name: a person who is in another seat with the
    // same name is a move, two seats that swapped their names are a swap. Events: { type: 'joined'|'left'|'renamed'|'moved'|'swapped'|'host'|'plan'|'starting'|'games', ... }.
    function personIn(room, seat) { return room.slots[seat].state === SLOT.Client || room.slots[seat].state === SLOT.Host; }
    function diffRooms(prev, next) {
        var events = [];
        if (!prev || !next) return events;
        var gone = [], came = [], changed = [], s;
        for (s = 0; s < PLAYERS; s++) {
            var was = personIn(prev, s), is = personIn(next, s);
            if (was && !is) gone.push(s);
            else if (!was && is) came.push(s);
            else if (was && is && prev.slots[s].name !== next.slots[s].name) changed.push(s);
        }
        if (gone.length === 1 && came.length === 1 && prev.slots[gone[0]].name === next.slots[came[0]].name) {
            events.push({ type: 'moved', from: gone[0], to: came[0], name: next.slots[came[0]].name });
        } else {
            gone.forEach(function (seat) { events.push({ type: 'left', seat: seat, name: prev.slots[seat].name }); });
            came.forEach(function (seat) { events.push({ type: 'joined', seat: seat, name: next.slots[seat].name }); });
        }
        if (changed.length === 2 && prev.slots[changed[0]].name === next.slots[changed[1]].name && prev.slots[changed[1]].name === next.slots[changed[0]].name) {
            events.push({ type: 'swapped', a: changed[0], b: changed[1], names: [next.slots[changed[0]].name, next.slots[changed[1]].name] });
        } else {
            changed.forEach(function (seat) { events.push({ type: 'renamed', seat: seat, from: prev.slots[seat].name, to: next.slots[seat].name }); });
        }
        // the host: a new one is a person who was not the one that led. The one who led is the same person when the seat still leads (a rename is no change), when the person moved or swapped to the seat that
        // leads now (the name that leads is the name that led: a message has no number for a person, and two of one name cannot be told apart), and is another when the seat of the one who led was left (gone).
        if (prev.leader !== NO_LEADER && next.leader !== NO_LEADER && prev.leader !== next.leader) {
            var movedFrom = events.length > 0 && events[0].type === 'moved' ? events[0].from : -1;
            var leaderLeft = prev.leader !== movedFrom && gone.indexOf(prev.leader) !== -1;
            var before = prev.slots[prev.leader].name;
            if (leaderLeft || next.slots[next.leader].name !== before) {
                events.push({ type: 'host', seat: next.leader, name: next.slots[next.leader].name, you: next.leader === next.you, before: before });
            }
        }
        var planChanged = prev.map !== next.map || prev.teamA !== next.teamA || prev.teamB !== next.teamB;
        for (s = 0; s < PLAYERS; s++) { if (prev.plan[s] !== next.plan[s]) planChanged = true; }
        if (planChanged) events.push({ type: 'plan' });
        if (!prev.starting && next.starting) events.push({ type: 'starting' });
        if (prev.inGame !== next.inGame) events.push({ type: 'games', mask: next.inGame });
        return events;
    }

    // ---- the entry that the game page reads to take the seat over ----------------------------------------------------------------------------------------------------------------------------------------
    // ants.rejoin.<room>.<seat> = {"k": the key (32 hex digits), "s": the server's address, "t": epoch ms}: what src/ants_app/rejoin_store.cpp writes and web/shell.html reads (REJOINKEY block)
    function rejoinEntry(code, seat, key, server, nowMs) {
        if (!validRoomCode(code) || code === '' || seat < 0 || seat >= PLAYERS || isZeroKey(key) || key.length !== 16) return null;
        return { name: 'ants.rejoin.' + code + '.' + seat, text: JSON.stringify({ k: hexOf(key), s: server, t: Math.floor(nowMs) }) };
    }

    // ---- the client ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // A page's seat in a lobby room. new LobbyClient({ url, code, map, name, key, join, platform, WebSocket, setTimeout, clearTimeout, now, backoff, connectMs, silenceMs, stableMs, onError }).connect(); then on(event, fn):
    // (join: true sends no lobby block, so that a code with no room is answered NoSuchRoom instead of making one: a code that a person typed. joinFirst: true is that for the first Hello only, a link that
    // was sent: once the room has taken us, a room that is lost (a restart) is made again, as a page that came back does.)
    //   'status' (status)           idle -> connecting -> online; offline (reconnecting); refused, removed, superseded, closed (final)
    //   'welcome' ({ seat, created, rejoin })   the room took us (created: our Hello made it); the key is not in it (rejoinEntry() is how a page hands it to the game)
    //   'room' (room)               every Room message, then the events of diffRooms for it
    //   'joined' | 'left' | 'renamed' | 'moved' | 'swapped' | 'host' | 'plan' | 'starting' | 'games'
    //   'notice' ({ text })         a line that the room itself says (the leader's notices, "<name>'s game did not come in time.")
    //   'chat' ({ sender, team, text })   a line of a player (a sender that is no colour, and an empty line, are not heard: the game's own client drops them too)
    //   'gone'                      the room of our key does not exist any more (everybody left, a restart): the client makes the room again, without the key, and says 'welcome' with created set
    //   'refused' ({ reason })      the room does not take us (Full, MatchRunning, VersionMismatch, BadRequest, NoSuchRoom when the server has no place for a lobby, ...); reason 0 with an `error` text: no socket
    //                               could be made (no WebSocket in the browser, an address that no socket takes, a code that is no code)
    //   'removed'                   the leader took us out of the room (Kicked, which nothing else is answered with: a flood is BadRequest): the page forgets the key and makes a room of its own with a new code
    //   'superseded'                another window of this browser took the seat
    // A handler that throws does not stop the client or the other handlers: the error goes to onError(error, event), else to console.error. After leave() (or any final status) no event is handed out any more.
    // Requests (move, setPlan, rename, remove, start, leave) return false when they cannot be made now (not connected, not the leader, a START waits, the arguments do not fit), and send nothing. The numbers
    // of a request are whole numbers; the client keeps the server's own pace (a name 3, then 1 a second; a plan or a colour move 6, then 4 a second; a removal 3, then 1 a second) and answers false to a
    // request that is too fast, which spends nothing (a page that sends faster would be put out of its room).
    // move and remove act on the person that the leader's screen showed in that colour: the page takes guardNow() when the leader picks the row up or opens the question and gives it back as the last
    // argument, and a request whose guard is not the seating of the room as it is now (somebody joined, left, was renamed or moved meanwhile) is not sent (false); without a guard the room as it is now counts.
    // Limits: connectMs (20 s) is how long a socket may take to open, silenceMs (8 s) how long a link that is open may say nothing (the server pings every second), stableMs (10 s) how long a link must stay
    // online before its loss counts as a first loss again (else the waits of the way back go on growing); 0 for a limit waits for the browser.
    var BUDGETS = { move: [6, 4], plan: [6, 4], rename: [3, 1], remove: [3, 1] };      // [burst, a second]: include/ants_net/flood.hpp
    function LobbyClient(opts) {
        this.url = opts.url;
        this.code = opts.code;
        this.map = validMapName(opts.map) ? opts.map : '';
        this.name = printableOnly(opts.name, MAX_NAME);
        this.platform = validPlatform(opts.platform) ? opts.platform : 0;
        this.keyBytes = isKey(opts.key) ? new Uint8Array(opts.key) : null;      // a copy: the caller's array is the caller's; read it with .key (a copy again)
        this.joinOnly = opts.join === true || opts.joinFirst === true;     // a Hello with no lobby block: the room of the code is joined and never made (a code that somebody typed: no such room is NoSuchRoom)
        this.joinOnce = opts.joinFirst === true && opts.join !== true;     // ... but only until the room has welcomed us (a link): after that a room that is lost is made again
        this.WS = 'WebSocket' in opts ? opts.WebSocket : (typeof WebSocket !== 'undefined' ? WebSocket : null);
        this.setTimer = opts.setTimeout || function (fn, ms) { return setTimeout(fn, ms); };
        this.clearTimer = opts.clearTimeout || function (id) { clearTimeout(id); };
        this.now = opts.now || function () { return Date.now(); };
        this.backoff = opts.backoff || [500, 1000, 2000, 4000, 8000];
        this.connectMs = opts.connectMs === undefined ? 20000 : opts.connectMs;
        this.silenceMs = opts.silenceMs === undefined ? 8000 : opts.silenceMs;
        this.stableMs = opts.stableMs === undefined ? 10000 : opts.stableMs;
        this.onError = typeof opts.onError === 'function' ? opts.onError : null;
        this.handlers = Object.create(null);                         // (no Object.prototype: an event may be called anything)
        this.budgets = {};
        this.status = 'idle';
        this.room = null;
        this.seat = -1;
        this.created = false;
        this.failures = 0;
        this.ws = null;
        this.helloSent = false;
        this.retryTimer = null;
        this.silenceTimer = null;
        this.limit = 0;                                              // the limit that the timer of the socket has now (connectMs, then silenceMs)
        this.heard = 0;                                              // when the socket was made, then when it last said something
        this.onlineSince = null;
        this.generation = 0;                                         // a socket that was given up must not speak any more
    }
    // The key of the seat, or null: a copy (the page must not change the client's key, nor keep one that it has not been given)
    Object.defineProperty(LobbyClient.prototype, 'key', {
        get: function () { return this.keyBytes === null ? null : new Uint8Array(this.keyBytes); },
        set: function () { /* the key is the server's word, not the page's */ }
    });
    LobbyClient.prototype.on = function (event, fn) {
        if (typeof fn !== 'function') throw new TypeError('the handler of "' + event + '" is not a function');
        (this.handlers[event] = this.handlers[event] || []).push(fn);
        return this;
    };
    LobbyClient.prototype.emit = function (event, data) {
        var list = (this.handlers[event] || []).slice();
        for (var i = 0; i < list.length; i++) {
            try { list[i](data); } catch (e) { this.report(e, event); }
        }
    };
    LobbyClient.prototype.report = function (error, event) {
        try {
            if (this.onError) this.onError(error, event);
            else if (typeof console !== 'undefined' && console.error) console.error('lobby_net: a handler of "' + event + '" threw', error);
        } catch (e) { /* a handler of errors that throws is no reason to stop the client either */ }
    };
    LobbyClient.prototype.setStatus = function (status) { if (this.status !== status) { this.status = status; this.emit('status', status); } };
    LobbyClient.prototype.final = function () { return this.status === 'refused' || this.status === 'removed' || this.status === 'superseded' || this.status === 'closed'; };
    LobbyClient.prototype.refuse = function (reason, error) {
        this.setStatus('refused');
        this.emit('refused', { reason: reason, error: error });
        return this;
    };
    LobbyClient.prototype.connect = function () {
        if (this.final() || this.status === 'connecting' || this.status === 'online') return this;
        if (!this.WS) return this.refuse(0, 'This browser has no WebSocket.');
        if (!validRoomCode(this.code) || this.code === '') return this.refuse(0, 'The room code is not one that a server takes (1 to 32 letters, digits, - and _).');
        if (this.retryTimer !== null) { this.clearTimer(this.retryTimer); this.retryTimer = null; }
        return this.open();
    };
    LobbyClient.prototype.open = function () {
        if (this.final()) return this;
        this.setStatus('connecting');
        if (this.final()) return this;                               // (a handler of that status left: there is nothing to open)
        var self = this;
        var generation = ++this.generation;
        var ws;
        try {
            ws = new this.WS(this.url);
        } catch (e) {                                                // an address that no socket takes (a page on https cannot open ws://): trying again would not help
            return this.refuse(0, String(e && e.message || e));
        }
        this.ws = ws;
        this.helloSent = false;
        this.heard = this.now();
        this.arm(this.connectMs);
        if ('binaryType' in ws) ws.binaryType = 'arraybuffer';
        ws.onopen = function () {
            if (generation !== self.generation) return;
            self.heard = self.now();
            self.arm(self.silenceMs);
            try {
                ws.send(encodeHello({ name: self.name, room: self.code, key: self.keyBytes, platform: self.platform, create: self.joinOnly ? null : { map: self.map } }));
                self.helloSent = true;
            } catch (e) {
                self.giveUpSocket();                                // (the link went between its opening and our first word: the way back)
            }
        };
        ws.onmessage = function (ev) { if (generation === self.generation) self.onData(ev.data); };
        ws.onclose = function () { if (generation === self.generation) self.onClosed(); };
        ws.onerror = function () { /* a close follows */ };
        return this;
    };
    // The limit of a socket: connectMs while it opens, silenceMs once it is open (every word of the server starts it again); past it the socket is given up and the way back is tried
    LobbyClient.prototype.arm = function (ms) {
        if (this.silenceTimer !== null) this.clearTimer(this.silenceTimer);
        this.silenceTimer = null;
        this.limit = ms;
        if (!(ms > 0)) return;
        var self = this;
        var generation = this.generation;
        this.silenceTimer = this.setTimer(function () {
            self.silenceTimer = null;
            if (generation === self.generation && (self.status === 'online' || self.status === 'connecting')) self.giveUpSocket();      // the server pings every second: a link that says nothing is dead
        }, ms);
    };
    LobbyClient.prototype.armSilence = function () { this.arm(this.silenceMs); };
    LobbyClient.prototype.giveUpSocket = function () {
        // The Hello went and no Welcome came, and we have no key to come back with: the server may have seated us all the same, and nobody would ever ask for that seat again. Say that we go (it
        // costs nothing if it did not: a person who is not there has nothing to leave); the retry is a new person's Hello.
        if (this.status === 'connecting' && this.keyBytes === null) this.sendBytes(encodeLeave());
        this.giveUpSocketQuietly();
        this.onClosed(true);
    };
    LobbyClient.prototype.onClosed = function (already) {
        if (!already) { this.generation++; this.ws = null; }
        if (this.silenceTimer !== null) { this.clearTimer(this.silenceTimer); this.silenceTimer = null; }
        if (this.final()) return;
        if (this.onlineSince !== null && this.now() - this.onlineSince >= this.stableMs) this.failures = 0;       // a link that stayed up has proved itself; one that was lost at once has not
        this.onlineSince = null;
        this.setStatus('offline');
        if (this.final()) return;                                    // (a handler of that status left)
        var delay = this.backoff[Math.min(this.failures, this.backoff.length - 1)];
        this.failures++;
        var self = this;
        if (this.retryTimer !== null) this.clearTimer(this.retryTimer);
        this.retryTimer = this.setTimer(function () { self.retryTimer = null; self.connect(); }, delay);
    };
    LobbyClient.prototype.onData = function (data) {
        this.heard = this.now();
        this.armSilence();
        var m = decode(data);
        if (!m) return;
        if (m.type === 'ping') { this.sendBytes(encodePong(m.nonce, m.sentMs)); return; }
        if (m.type === 'welcome') {
            this.seat = m.player;
            this.keyBytes = m.key;
            this.created = m.created;
            if (this.joinOnce) this.joinOnly = false;                // we have a seat: a room that is lost later is made again (the Hello of 'gone' carries the block)
            this.onlineSince = this.now();
            this.setStatus('online');
            if (this.final()) return;
            this.emit('welcome', { seat: m.player, created: m.created, rejoin: m.rejoin });
        } else if (m.type === 'room') {
            var prev = this.room;
            this.room = m;
            if (m.you !== 255) {                                     // every Room message says which colour is ours: the leader may have moved us
                this.seat = m.you;
                this.name = m.slots[m.you].name;                     // a reconnect says the name that the room shows (the server may have taken another than the one we sent)
            }
            this.emit('room', m);
            var events = diffRooms(prev, m);
            for (var i = 0; i < events.length && !this.final(); i++) this.emit(events[i].type, events[i]);
        } else if (m.type === 'chat') {
            if (m.text === '' || (m.sender >= PLAYERS && m.sender !== ROOM_SENDER)) return;
            if (m.sender === ROOM_SENDER) this.emit('notice', { text: m.text });
            else this.emit('chat', { sender: m.sender, team: m.team, text: m.text });
        } else if (m.type === 'reject') {
            this.onReject(m.reason);
        }
    };
    LobbyClient.prototype.onReject = function (reason) {
        if (reason === REJECT.NoSuchRoom && this.keyBytes !== null) {      // our room is gone: make it again (a Hello without the key and with the block)
            this.keyBytes = null;
            this.room = null;
            this.seat = -1;
            this.giveUpSocketQuietly();
            this.emit('gone', {});
            this.open();                                             // (nothing, when a handler left)
            return;
        }
        this.giveUpSocketQuietly();
        if (reason === REJECT.Kicked) { this.keyBytes = null; this.setStatus('removed'); this.emit('removed', {}); return; }
        if (reason === REJECT.Superseded) { this.setStatus('superseded'); this.emit('superseded', {}); return; }
        this.refuse(reason);
    };
    LobbyClient.prototype.giveUpSocketQuietly = function () {
        var ws = this.ws;
        this.generation++;
        this.ws = null;
        this.helloSent = false;
        this.onlineSince = null;
        if (this.silenceTimer !== null) { this.clearTimer(this.silenceTimer); this.silenceTimer = null; }
        if (ws) { try { ws.close(); } catch (e) { /* it was closed already */ } }
    };
    // What may be said: anything while online; between the Hello and the Welcome only the answer to a ping and a Leave
    LobbyClient.prototype.sendBytes = function (bytes) {
        var allowed = this.ws !== null && (this.status === 'online' || (this.status === 'connecting' && this.helloSent && (bytes[0] === MSG.Pong || bytes[0] === MSG.Leave)));
        if (!allowed) return false;
        try { this.ws.send(bytes); } catch (e) { return false; }
        return true;
    };
    // The page came back to the front (a phone that was in another app) or the network came back: a link that is down is tried at once, one that has been quiet too long (or has been opening too long) is given up
    LobbyClient.prototype.wake = function () {
        if (this.final()) return;
        if (this.status === 'offline') this.connect();
        else if ((this.status === 'online' || this.status === 'connecting') && this.limit > 0 && this.now() - this.heard > this.limit) this.giveUpSocket();
    };
    LobbyClient.prototype.isLeader = function () { return this.room !== null && this.room.you !== 255 && this.room.leader === this.room.you; };
    LobbyClient.prototype.canAct = function () { return this.status === 'online' && this.room !== null && this.room.lobby; };
    // The guard of the room as it is shown now (the Room message last heard), 0 before there is one: what the page keeps while the leader decides, and passes to move and remove
    LobbyClient.prototype.guardNow = function () { return this.room === null ? 0 : seatingHash(this.room); };
    // The server's pace: a request is allowed while the budget of its kind (thousandths of a request, as the server counts) has one; it is spent when the request was written, not before
    LobbyClient.prototype.canSpend = function (kind) {
        var burst = BUDGETS[kind][0], perSecond = BUDGETS[kind][1];
        var now = this.now();
        var b = this.budgets[kind];
        if (b === undefined) b = this.budgets[kind] = { tokens: burst * 1000, at: now };
        b.tokens = Math.min(burst * 1000, b.tokens + Math.max(0, now - b.at) * perSecond);
        b.at = now;
        return b.tokens >= 1000;
    };
    LobbyClient.prototype.send = function (kind, bytes) {
        if (!this.canSpend(kind) || !this.sendBytes(bytes)) return false;
        this.budgets[kind].tokens -= 1000;
        return true;
    };
    LobbyClient.prototype.move = function (from, to, guard) {
        var r = this.room;
        if (!this.canAct() || !this.isLeader() || r.starting || !isSeat(from) || !isSeat(to) || from === to) return false;
        if (r.slots[from].state !== SLOT.Client || (r.slots[to].state !== SLOT.Empty && r.slots[to].state !== SLOT.Client)) return false;
        var now = seatingHash(r);
        if (guard !== undefined && guard !== now) return false;
        return this.send('move', encodeSeatMove(from, to, now));
    };
    // The plan as the leader wants it: { map ('' keeps the room's), kinds [4] (PLAN.*), teamA, teamB }
    LobbyClient.prototype.setPlan = function (plan) {
        var r = this.room;
        if (!this.canAct() || !this.isLeader() || r.starting || !plan || typeof plan !== 'object') return false;
        var map = plan.map === undefined ? '' : plan.map;
        if (typeof map !== 'string' || (map !== '' && !validMapName(map)) || !validTeams(plan.teamA, plan.teamB)) return false;
        var kinds = plan.kinds;
        if (!kinds || kinds.length !== PLAYERS) return false;
        for (var i = 0; i < PLAYERS; i++) { if (!isWhole(kinds[i]) || kinds[i] < PLAN.Open || kinds[i] > PLAN.Nobody) return false; }
        return this.send('plan', encodePlan(map, kinds, plan.teamA, plan.teamB));
    };
    LobbyClient.prototype.rename = function (name) {
        if (!this.canAct() || this.room.starting || !validPersonName(name)) return false;
        return this.send('rename', encodeName(name));
    };
    LobbyClient.prototype.remove = function (seat, guard) {
        var r = this.room;
        if (!this.canAct() || !this.isLeader() || !isSeat(seat) || seat === r.you || r.slots[seat].state !== SLOT.Client) return false;
        var now = seatingHash(r);
        if (guard !== undefined && guard !== now) return false;
        return this.send('remove', encodeRemove(seat, now));
    };
    LobbyClient.prototype.start = function () {
        var r = this.room;
        if (!this.canAct() || !this.isLeader() || r.starting) return false;
        return this.sendBytes(encodeStartRequest(fillOfPlan(r.plan), r.teamA, r.teamB));
    };
    // On purpose: the seat is given up at once (a closed tab would hold it for a minute), also between the Hello and the Welcome (the server has the person already); before the Hello there is
    // nobody to tell, and nothing is sent
    LobbyClient.prototype.leave = function () {
        if (this.final()) return false;
        var sent = this.sendBytes(encodeLeave());
        this.giveUpSocketQuietly();
        if (this.retryTimer !== null) { this.clearTimer(this.retryTimer); this.retryTimer = null; }
        this.keyBytes = null;
        this.setStatus('closed');
        return sent;
    };
    // The entry for the game page's storage, { name, text } (rejoinEntry), or null before the room has welcomed us. The page writes it when the leader's START is heard (the 'starting' event), under
    // the seat that the last Room message gave, and goes to the game page: the game takes the seat over with the key.
    LobbyClient.prototype.rejoinEntry = function (nowMs) {
        return this.keyBytes && this.seat >= 0 ? rejoinEntry(this.code, this.seat, this.keyBytes, this.url, nowMs === undefined ? this.now() : nowMs) : null;
    };

    return {
        PROTOCOL: PROTOCOL, MSG: MSG, REJECT: REJECT, SLOT: SLOT, PLAN: PLAN, FILL: FILL, OS: OS, PLATFORM_BROWSER: PLATFORM_BROWSER, PLAYERS: PLAYERS,
        NO_TEAM: NO_TEAM, NO_LEADER: NO_LEADER, ROOM_SENDER: ROOM_SENDER, MAX_NAME: MAX_NAME, MAX_CHAT: MAX_CHAT,
        validMapName: validMapName, validRoomCode: validRoomCode, publicRoomCode: publicRoomCode, validPersonName: validPersonName, validTeams: validTeams, validPlatform: validPlatform,
        encodeHello: encodeHello, encodeLeave: encodeLeave, encodePong: encodePong, encodePing: function (nonce, sentMs) { return encodePing(MSG.Ping, nonce, sentMs); },
        encodeSeatMove: encodeSeatMove, encodePlan: encodePlan, encodeName: encodeName, encodeRemove: encodeRemove, encodeStartRequest: encodeStartRequest, encodeChat: encodeChat,
        fillOfPlan: fillOfPlan, seatingHash: seatingHash, decode: decode, diffRooms: diffRooms, rejoinEntry: rejoinEntry, LobbyClient: LobbyClient,
        hexOf: hexOf, bytesOfHex: bytesOfHex, zeroKey: zeroKey, isZeroKey: isZeroKey
    };
});
