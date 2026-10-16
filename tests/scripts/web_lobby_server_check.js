// Runs the lobby page's client of network protocol 16 (web/front/lobby_net.js, LobbyClient) against a REAL ants_server over a WebSocket, as a browser would reach it behind the site's proxy:
// pages that make a lobby room and join it by one code, the leader's colour moves (a move to an empty colour, a change of places), the plan and the teams, a new name (and a name that looks like a
// bot's), the leader taking a person out, the leader going (the player who has been there the longest leads), a link that drops (the seat is held and taken back with the key), a key for a room that is
// gone (the page makes the room again), a second window with the same key, START that waits for the games, and the people of the lobbies in GET /busy.
// usage: node web_lobby_server_check.js web/front/lobby_net.js ws://127.0.0.1:PORT/ws http://127.0.0.1:WSPORT     (the server: --demo-rooms N --demo-map TINY.LVL --demo-maps TINY.LVL,MEDIUM.LVL --demo-lobbies M)
//        exit 0: every check holds; the failures are printed
'use strict';
const path = require('path');

const [modulePath, wsUrl, httpBase] = process.argv.slice(2);
if (!modulePath || !wsUrl) { console.log('usage: web_lobby_server_check.js lobby_net.js ws://host:port/ws [http://host:port]'); process.exit(2); }
const N = require(path.resolve(modulePath));

// The WebSocket client for a page's code: over a plain socket, the same on every node (the global WebSocket arrived in node 22); mini_websocket.js
const MiniWebSocket = require('./mini_websocket.js');
// ANTS_NATIVE_WEBSOCKET=1 runs the same scenarios with node's own WebSocket (node 22 or later), as a browser does
const WS = process.env.ANTS_NATIVE_WEBSOCKET && typeof WebSocket !== 'undefined' ? WebSocket : MiniWebSocket;

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
function until(label, fn, ms) {
    return new Promise((resolve, reject) => {
        const t0 = Date.now();
        const tick = () => {
            let v;
            try { v = fn(); } catch (e) { reject(e); return; }
            if (v) { resolve(v); return; }
            if (Date.now() - t0 > (ms || 5000)) { reject(new Error('timeout: ' + label)); return; }
            setTimeout(tick, 15);
        };
        tick();
    });
}
// One step of a scenario: fn returns a condition to wait for (a function), or a value that is false when the step failed
async function step(label, fn, ms) {
    try {
        const r = fn();
        if (typeof r === 'function') await until(label, r, ms);
        else if (r === false) { check(label, false); return false; }
        check(label, true);
        return true;
    } catch (e) {
        check(label, false, e.message);
        return false;
    }
}

// A code that no earlier run of this check used (a lobby that was left stays a minute: a server may be used for several runs)
const RUN = Math.random().toString(36).slice(2, 4).padEnd(2, '0');
const code = (n) => 'e2e' + RUN + n;

// A scenario that breaks (an event that never came and so has no data) is one failed check; the next scenario still runs
async function scenario(title, fn) {
    try { await fn(); } catch (e) { check('scenario ' + title + ' ran to its end', false, e && e.message); }
}

// A link that is slow: every word of the server reaches the page `ms` milliseconds late (the page's own words go at once)
function delayedWebSocket(ms) {
    return class extends MiniWebSocket {
        get onmessage() { return this.handler ? (event) => setTimeout(() => { if (this.handler) this.handler(event); }, ms) : null; }
        set onmessage(handler) { this.handler = handler; }
    };
}

// A link that jitters: every word of the page reaches the server 0 to `ms` milliseconds late, the words of one socket in the order they were sent (a TCP link keeps the order), the delays from a fixed seed
function jitteredWebSocket(ms, seed) {
    let state = seed >>> 0;
    const random = () => { state = (Math.imul(state, 1664525) + 1013904223) >>> 0; return state / 4294967296; };
    return class extends MiniWebSocket {
        send(bytes) {
            const at = Math.max(this.lastAt || 0, Date.now() + Math.floor(random() * (ms + 1)));
            this.lastAt = at;
            this.tail = (this.tail || Promise.resolve()).then(() => sleep(Math.max(0, at - Date.now()))).then(() => { if (this.readyState === 1) super.send(bytes); });
        }
    };
}

const ALL = [];
function page(name, code, extra) {
    const c = new N.LobbyClient(Object.assign({ url: wsUrl, code: code, map: 'TINY.LVL', name: name, WebSocket: WS }, extra || {}));
    c.pageName = name;
    c.events = [];
    for (const ev of ['status', 'welcome', 'joined', 'left', 'renamed', 'moved', 'swapped', 'host', 'plan', 'starting', 'games', 'notice', 'chat', 'gone', 'refused', 'removed', 'superseded']) {
        c.on(ev, (data) => c.events.push({ ev: ev, data: data }));
    }
    c.count = (ev) => c.events.filter((e) => e.ev === ev).length;
    c.of = (ev) => c.events.filter((e) => e.ev === ev).map((e) => e.data);
    c.seatOf = (who) => c.room ? c.room.slots.findIndex((s) => s.state === N.SLOT.Client && s.name === who) : -1;
    ALL.push(c);
    return c;
}
function names(c) { return c.room ? c.room.slots.map((s) => (s.state === N.SLOT.Client ? s.name : s.state === N.SLOT.Bot ? '(bot)' : '-')).join('|') : '(no room)'; }
const online = (c) => () => c.status === 'online' && c.room !== null;

(async () => {
    // ---- 1. two pages in one lobby: the code makes the room, and the same link joins it ---------------------------------------------------------------------------------------------------------------
    await scenario('1. two pages in one lobby: the code makes the room, and the same link joins it', async () => {
        const A = page('Ada', code('a'));
        const B = page('Bea', code('a'));
        A.connect();
        await step('the first page is welcomed to a room that its Hello made, as seat 0', () => online(A), 5000);
        check('(it leads, the room is a lobby room, on the server\'s map, and no key is zero)', A.created === true && A.seat === 0 && A.isLeader() && A.room.lobby && A.room.map === 'TINY.LVL' && !N.isZeroKey(A.key), 'seat ' + A.seat + ', map ' + (A.room && A.room.map));
        check('(four colours, the plan says Open for each)', A.room.slots.length === 4 && A.room.plan.join() === '0,0,0,0', A.room.plan.join());
        B.connect();
        await step('the second page finds the room by the same code: seat 1, not created, and sees the first', () => online(B), 5000);
        check('(B is not told that its Hello made the room, and does not lead)', B.created === false && B.seat === 1 && !B.isLeader() && B.room.leader === 0, 'seat ' + B.seat);
        check('(B sees Ada in seat 0 and itself in seat 1)', names(B) === 'Ada|Bea|-|-', names(B));
        await step('A is told that Bea joined, in seat 1', () => () => A.count('joined') === 1 && A.of('joined')[0].seat === 1 && A.of('joined')[0].name === 'Bea');
        // the leader drags Bea to colour 2 (an empty colour)
        check('A moves Bea from colour 1 to the empty colour 2 (the request is sent)', A.move(1, 2) === true);
        await step('both pages see Bea in colour 2 (moved, not left and joined)', () => () => names(A) === 'Ada|-|Bea|-' && names(B) === 'Ada|-|Bea|-' && A.count('moved') === 1 && B.count('moved') === 1 && B.seat !== undefined);
        check('(the event names the colours)', JSON.stringify(A.of('moved')[0]) === JSON.stringify({ type: 'moved', from: 1, to: 2, name: 'Bea' }), JSON.stringify(A.of('moved')));
        check('(B knows that it sits in colour 2 now)', B.room.you === 2, 'you ' + B.room.you);
        // the leader changes places with Bea: the leader drags its own ant onto Bea's colour
        check('A drags its own ant (colour 0) onto Bea\'s colour 2', A.move(0, 2) === true);
        await step('the two change places, and both pages say so', () => () => names(A) === 'Bea|-|Ada|-' && names(B) === 'Bea|-|Ada|-' && A.count('swapped') === 1 && B.count('swapped') === 1);
        check('(Ada leads still, from colour 2)', A.isLeader() && A.room.leader === 2 && B.room.leader === 2 && A.room.you === 2 && B.room.you === 0, 'leader ' + A.room.leader);
        // the plan: another map, a bot for colour 1 and 3, the two persons a team
        const plan = { map: 'MEDIUM.LVL', kinds: [N.PLAN.Open, N.PLAN.Medium, N.PLAN.Open, N.PLAN.Hard], teamA: 0, teamB: 2 };
        check('A sets the plan: MEDIUM.LVL, a Medium bot in colour 1 and a Hard one in colour 3, colours 0 and 2 a team', A.setPlan(plan) === true);
        await step('both pages see the plan', () => () => A.room.map === 'MEDIUM.LVL' && B.room.map === 'MEDIUM.LVL' && B.room.plan.join() === '0,2,0,3' && B.room.teamA === 0 && B.room.teamB === 2 && B.count('plan') >= 1);
        check('(B cannot change it: only the leader\'s plan is sent)', B.setPlan(plan) === false);
        // a name
        check('B takes the name Beatrice', B.rename('Beatrice') === true);
        await step('A is told of the new name', () => () => A.count('renamed') === 1 && A.of('renamed')[0].to === 'Beatrice' && names(A) === 'Beatrice|-|Ada|-' && B.name === 'Beatrice');
        check('(the event names the colour and both names)', JSON.stringify(A.of('renamed')[0]) === JSON.stringify({ type: 'renamed', seat: 0, from: 'Bea', to: 'Beatrice' }), JSON.stringify(A.of('renamed')));
        const renamed = A.count('renamed');
        check('the page itself does not ask for a name that looks like a bot\'s (protocol 20)', B.rename('Bot (Hard)') === false && B.rename('Pip (bot)') === false);
        check('B sends the bytes of such a name by hand all the same (what no honest page does): the server must refuse it', B.sendBytes(N.encodeName('Bot (Hard)')) === true && B.sendBytes(N.encodeName('Pip (bot, Hard)')) === true);
        await sleep(500);
        check('(the room keeps Beatrice and says nothing)', names(A) === 'Beatrice|-|Ada|-' && A.count('renamed') === renamed && B.room.slots[0].name === 'Beatrice', names(A));
        // the leader takes Beatrice out
        const guardBefore = N.seatingHash(A.room);
        check('A removes the person in colour 0', A.remove(0) === true);
        await step('Beatrice is told that she was removed, and the page lets her key go', () => () => B.status === 'removed' && B.count('removed') === 1 && B.key === null && B.rejoinEntry() === null);
        await step('A sees colour 0 free', () => () => names(A) === '-|-|Ada|-' && A.count('left') === 1 && A.of('left')[0].seat === 0 && A.of('left')[0].name === 'Beatrice');
        check('(the plan stays: colours 1 and 3 are still the bots\')', A.room.plan.join() === '0,2,0,3' && A.room.map === 'MEDIUM.LVL', A.room.plan.join());
        check('(the seating changed, so the guard of the room is another number)', N.seatingHash(A.room) !== guardBefore);
        await sleep(300);
        check('(a removed page does not come back by itself)', B.status === 'removed' && B.events.filter((e) => e.ev === 'welcome').length === 1);
        // the same person, opening the link again, is a new person
        const B2 = page('Beatrice', code('a'));
        B2.connect();
        await step('a new page of the same code is seated in the first open colour (0), as a new person', () => online(B2), 5000);
        check('(colour 0, not the leader, no word of the old key)', B2.seat === 0 && !B2.isLeader() && B2.created === false, 'seat ' + B2.seat);
        await step('A sees it join', () => () => A.count('joined') === 2 && A.of('joined')[1].seat === 0);
        // the pages live on the server's pings: nothing has been sent for a while and nobody was dropped
        await sleep(2500);
        check('the server speaks to a page every second (the page has heard it less than 1.5 s ago)', A.now() - A.heard < 1500 && B2.now() - B2.heard < 1500, (A.now() - A.heard) + ' ms');
        check('(and nobody was dropped)', A.status === 'online' && B2.status === 'online');
        A.leave();
        B2.leave();
    });

    // ---- 2. the leader goes: the player who has been there the longest leads --------------------------------------------------------------------------------------------------------------------------
    await scenario('2. the leader goes: the player who has been there the longest leads', async () => {
        const A = page('Ada', code('b'));
        const B = page('Bea', code('b'));
        const C = page('Cy', code('b'));
        A.connect();
        await step('A makes the room', () => online(A));
        B.connect();
        await step('B joins', () => online(B));
        C.connect();
        await step('C joins', () => online(C));
        check('(seats 0, 1, 2; Ada leads)', A.seat === 0 && B.seat === 1 && C.seat === 2 && C.room.leader === 0, [A.seat, B.seat, C.seat].join());
        await step('A sees both join', () => () => A.count('joined') === 2);
        A.leave();
        await step('A is out: B and C are told that Ada left, and that Bea leads now', () => () => B.count('left') === 1 && C.count('left') === 1 && B.count('host') === 1 && C.count('host') === 1);
        check('(B is told "you", C is not)', B.of('host')[0].you === true && C.of('host')[0].you === false, JSON.stringify([B.of('host'), C.of('host')]));
        check('(the event names the new leader and the one before)', B.of('host')[0].name === 'Bea' && B.of('host')[0].before === 'Ada' && B.of('host')[0].seat === 1, JSON.stringify(B.of('host')));
        check('(B leads, C does not, and the Room messages agree)', B.isLeader() && !C.isLeader() && C.room.leader === 1 && B.room.leader === 1);
        check('(the new leader can move, the other cannot)', B.move(2, 0) === true && C.move(2, 0) === false);
        await step('B moves Cy to colour 0 and both see it', () => () => names(B) === 'Cy|Bea|-|-' && names(C) === 'Cy|Bea|-|-');
        B.leave();
        await step('B is out: Cy leads', () => () => C.count('host') === 2 && C.isLeader());
        check('(the second notice names Cy as the leader, and "you")', C.of('host')[1].you === true && C.of('host')[1].name === 'Cy' && C.of('host')[1].before === 'Bea', JSON.stringify(C.of('host')));
        C.leave();
    });

    // ---- 3. a link that drops: the seat is held, and the key takes it back --------------------------------------------------------------------------------------------------------------------------
    await scenario('3. a link that drops: the seat is held, and the key takes it back', async () => {
        const A = page('Ada', code('c'));
        const B = page('Bea', code('c'));
        A.connect();
        await step('A makes the room', () => online(A));
        B.connect();
        await step('B joins', () => online(B));
        const key = N.hexOf(B.key);
        await step('A sees B', () => () => A.count('joined') === 1);
        const welcomes = B.count('welcome');
        B.ws.close();                                                       // (the page\'s link goes: not Leave, a closed socket)
        await step('B notices and is offline', () => () => B.status === 'offline' || B.status === 'connecting' || B.count('welcome') > welcomes, 3000);
        await step('B takes its seat back with the key (a second Welcome, the same seat and key)', () => () => B.status === 'online' && B.count('welcome') === welcomes + 1 && B.seat === 1 && N.hexOf(B.key) === key && B.room !== null);
        check('(A was told of nothing: nobody left, nobody joined)', A.count('left') === 0 && A.count('joined') === 1, A.count('left') + ' left');
        check('(and B is in its colour, not the leader)', names(A) === 'Ada|Bea|-|-' && B.room.you === 1 && !B.isLeader(), names(A));
        check('(the entry for the game page is B\'s key and seat)', B.rejoinEntry().name === 'ants.rejoin.' + code('c') + '.1' && JSON.parse(B.rejoinEntry().text).k === key);
        A.leave();
        B.leave();
    });

    // ---- 4. a key for a room that is gone: the page makes the room again --------------------------------------------------------------------------------------------------------------------------------
    await scenario('4. a key for a room that is gone: the page makes the room again', async () => {
        const stale = Uint8Array.from([9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6]);
        const A = page('Ada', code('d'), { key: stale });
        A.connect();
        await step('a page with the key of a room that does not exist is told so, and makes the room again without the key', () => () => A.count('gone') === 1 && A.status === 'online' && A.room !== null, 6000);
        check('(the new room is its own: created, seat 0, a key that is not the old one, and it leads)', A.created === true && A.seat === 0 && N.hexOf(A.key) !== N.hexOf(stale) && A.isLeader(), 'key ' + N.hexOf(A.key));
        A.leave();
    });

    // ---- 5. another window with the same key takes the seat; the first is told ------------------------------------------------------------------------------------------------------------------------
    await scenario('5. another window with the same key takes the seat; the first is told', async () => {
        const A = page('Ada', code('e'));
        A.connect();
        await step('A makes the room', () => online(A));
        const A2 = page('Ada', code('e'), { key: A.key });
        A2.connect();
        await step('a second window with the key takes the seat (seat 0, online)', () => () => A2.status === 'online' && A2.seat === 0 && A2.room !== null);
        await step('the first window is told that another took its seat, and does not come back', () => () => A.status === 'superseded' && A.count('superseded') === 1);
        await sleep(1200);
        check('(the second window is still the one that has the seat)', A2.status === 'online' && A.status === 'superseded' && A2.count('welcome') === 1, A2.status + ' ' + A.status);
        A2.leave();
    });

    // ---- 6. START waits for the games; the leader can still take a person out ----------------------------------------------------------------------------------------------------------------------
    await scenario('6. START waits for the games; the leader can still take a person out', async () => {
        const A = page('Ada', code('f'));
        const B = page('Bea', code('f'));
        A.connect();
        await step('A makes the room', () => online(A));
        B.connect();
        await step('B joins', () => online(B));
        await step('A sees B', () => () => A.count('joined') === 1);
        check('A presses START (the request carries the plan\'s fill and teams)', A.start() === true);
        await step('both pages are told that the START waits, and that no game is in yet', () => () => A.room.starting && B.room.starting && A.count('starting') === 1 && B.count('starting') === 1 && A.room.inGame === 0);
        check('(the leader cannot move, plan, rename or start again while it waits)', A.move(1, 2) === false && A.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: 255, teamB: 255 }) === false && A.rename('Zed') === false && A.start() === false);
        check('(a newcomer is told MatchRunning: the room takes nobody while it waits)', await (async () => {
            const D = page('Dee', code('f'));
            D.connect();
            try { await until('refused', () => D.status === 'refused', 4000); } catch (e) { return false; }
            return D.of('refused')[0].reason === N.REJECT.MatchRunning;
        })());
        check('A takes Bea out while the START waits', A.remove(1) === true);
        await step('Bea is told that she was removed', () => () => B.status === 'removed');
        await step('A sees her go', () => () => A.count('left') === 1);
        A.leave();
    });

    // ---- 7. the people of the lobbies are players in GET /busy, and never a match ----------------------------------------------------------------------------------------------------------------------
    await scenario('7. the people of the lobbies are players in GET /busy, and never a match', async () => {
        if (!httpBase) return;
        const A = page('Ada', code('g'));
        const B = page('Bea', code('g'));
        A.connect();
        await step('A makes a room', () => online(A));
        B.connect();
        await step('B joins', () => online(B));
        let busy = null;
        try { busy = await (await fetch(httpBase + '/busy')).json(); } catch (e) { check('GET /busy answers', false, e.message); }
        if (busy) check('GET /busy counts the two pages as players and no match (a lobby never holds a deploy back)', busy.matches === 0 && busy.players >= 2, JSON.stringify(busy));
        A.leave();
        B.leave();
    });

    // ---- 8. a code with a capital letter is no visitor's code ---------------------------------------------------------------------------------------------------------------------------------
    await scenario('8. a code with a capital letter is no visitor\'s code', async () => {
        const A = page('Ada', code('h').toUpperCase());
        A.connect();
        await step('a code with a capital letter makes no room (the server says NoSuchRoom)', () => () => A.status === 'refused' && A.of('refused')[0].reason === N.REJECT.NoSuchRoom);
    });

    // ---- 9. a name with characters outside the table is cut to what the server takes ---------------------------------------------------------------------------------------------------------------
    await scenario('9. a name with characters outside the table is cut to what the server takes', async () => {
        const A = page('José', code('i'));
        const B = page('ÀÉ', code('i'));
        A.connect();
        await step('a page named "José" is seated (the Hello did not break the decoder)', () => online(A));
        check('(the room and the page say "Jos": the accent is left out, not the person)', A.room.slots[0].name === 'Jos' && A.name === 'Jos', JSON.stringify(A.room.slots[0].name));
        B.connect();
        await step('a page whose name is nothing but accents is seated too', () => online(B));
        check('(it has no name yet: the room shows an empty one)', B.room.slots[1].name === '' && B.name === '', JSON.stringify(B.room.slots[1].name));
        check('(and it can give itself a name)', B.rename('Bea') === true);
        await step('both pages see "Bea"', () => () => B.name === 'Bea' && names(A) === 'Jos|Bea|-|-' && names(B) === 'Jos|Bea|-|-');
        A.leave();
        B.leave();
    });

    // ---- 10. Leave between the Hello and the Welcome gives the seat up at once ------------------------------------------------------------------------------------------------------------------
    await scenario('10. Leave between the Hello and the Welcome gives the seat up at once', async () => {
        const A = page('Ada', code('j'));
        A.connect();
        await step('A makes the room', () => online(A));
        // a page whose link is slow: the server's words reach it 600 ms late, so the Hello has gone and the Welcome has not come
        const B = page('Bea', code('j'), { WebSocket: delayedWebSocket(600) });
        B.connect();
        await step('the Hello of B is out and no Welcome is in', () => () => B.ws !== null && B.ws.readyState === 1 && B.status === 'connecting');
        check('B leaves now (it has no key: the Leave goes after the Hello and the server has the person)', B.leave() === true && B.status === 'closed');
        await step('A sees B come and go (the server had seated her, and gave the seat up at the Leave)', () => () => A.count('joined') === 1 && A.count('left') === 1 && names(A) === 'Ada|-|-|-');
        const C = page('Cy', code('j'));
        C.connect();
        await step('a page that comes next is seated in colour 1: the seat was not held', () => online(C));
        check('(colour 1, not colour 2)', C.seat === 1, 'seat ' + C.seat);
        check('(B holds no key and wrote no entry for a game)', B.key === null && B.rejoinEntry() === null);
        A.leave();
        C.leave();
    });

    // ---- the chat: pages talk in the lobby, at the server's pace (docs/NETWORK_PORT.md "Chat in the waiting room": the server already relays a page's line; the page's say() is the whole of it) ------------------------------
    await scenario('the chat: pages talk in the lobby, at the server\'s pace', async () => {
        const A = page('Ada', code('t'));
        const B = page('Bea', code('t'));
        const C = page('Cy', code('t'));
        A.connect();
        await step('Ada makes the room', () => online(A), 5000);
        B.connect();
        C.connect();
        await step('Bea and Cy join it', () => () => online(B)() && online(C)() && names(A) === 'Ada|Bea|Cy|-');
        const said = (p) => p.of('chat').map((e) => e.sender + ':' + e.text);
        check('Ada says a line', A.say('hello there') === true);
        await step('everybody in the room hears it, Ada too (the room\'s word that it was heard), with the seat of her connection and no team', () => () => [A, B, C].every((p) => said(p).join('|') === '0:hello there'));
        check('(and no team flag)', [A, B, C].every((p) => p.of('chat')[0].team === false));
        check('Bea answers; the lines come in the order they were said, the same for all three', B.say('hi Ada') === true);
        await step('... in the same order for all three', () => () => [A, B, C].every((p) => said(p).join('|') === '0:hello there|1:hi Ada'));
        check('a line of 100 characters is relayed whole (the server\'s own limit)', C.say('c'.repeat(100)) === true);
        await step('... whole', () => () => A.of('chat').length === 3 && A.of('chat')[2].text === 'c'.repeat(100) && A.of('chat')[2].sender === 2);
        // the pace: eight lines at once from the page: four go, the page refuses the others (it keeps a line in reserve below the server's five: lobby_net.js BUDGETS); the server drops none (the budget of the
        // connection is whole again after five quiet seconds)
        await sleep(5200);
        const before = A.of('chat').length;
        const results = [1, 2, 3, 4, 5, 6, 7, 8].map((i) => A.say('burst ' + i));
        check('eight lines at once: the first four are sent, the others refused by the page', results.join() === 'true,true,true,true,false,false,false,false', results.join());
        await step('the others hear the four, in order (the server dropped none)', () => () => B.of('chat').length === before + 4 && C.of('chat').length === before + 4);
        check('(exactly the four: burst 1 .. burst 4)', B.of('chat').slice(before).map((e) => e.text).join('|') === 'burst 1|burst 2|burst 3|burst 4');
        check('(Ada is still in the room)', A.status === 'online' && A.count('refused') === 0);
        // what no honest page does: lines by hand beyond the pace; the server drops them without a word, and a connection that goes on is thrown out
        await sleep(5200);
        const x = B.of('chat').length;
        for (let i = 1; i <= 8; i++) B.sendBytes(N.encodeChat(B.seat, false, 'by hand ' + i));
        await step('the five that the server\'s budget allows are relayed', () => () => A.of('chat').slice(x).filter((e) => /^by hand/.test(e.text)).length >= 5);
        await sleep(600);                                                // (and the time for a sixth that must not come: the three others are dropped without a word)
        check('eight lines by hand at once from Bea: five are relayed, three are dropped (the budget of her connection)', A.of('chat').slice(x).filter((e) => /^by hand/.test(e.text)).length === 5, String(A.of('chat').length - x));
        check('(Bea is not thrown out for it: a few lines beyond the pace are tolerated)', B.status === 'online');
        // a page that keeps asking as fast as it can sends no more than the pace allows and is never thrown out
        await sleep(6000);
        const start = Date.now();
        let sent = 0;
        while (Date.now() - start < 4000) { if (C.say('flood ' + sent)) sent++; await sleep(5); }
        check('four seconds of asking every 5 ms: the page sent 4 and then a line every 1.25 s (6 to 8)', sent >= 6 && sent <= 8, String(sent));
        await step('every line that the page sent was heard by the others', () => () => A.of('chat').filter((e) => /^flood/.test(e.text)).length === sent && B.of('chat').filter((e) => /^flood/.test(e.text)).length === sent);
        check('(and nobody was thrown out)', A.status === 'online' && B.status === 'online' && C.status === 'online');
        // a link that jitters: the page keeps the pace below the server's, so that a line that is handled early is not lost. The server counts the moment at which it HANDLES a line; a page that paced itself at
        // exactly the server's rate (5, then one a second) lost a line whenever one line was handled late and the next was not (it failed on the macOS CI, and now and then in a local run), and the server drops a line without a word.
        // Dee's words reach the server 0 to 150 ms late (in order, from a fixed seed); she asks every 5 ms for eight seconds; every line she sent has to be heard, by all four.
        const D = page('Dee', code('t'), { WebSocket: jitteredWebSocket(150, 20251015) });
        D.connect();
        await step('Dee joins the room on a link that jitters', () => () => online(D)() && names(A) === 'Ada|Bea|Cy|Dee', 8000);
        const jitterStart = Date.now();
        let jitterSent = 0;
        while (Date.now() - jitterStart < 8000) { if (D.say('jitter ' + jitterSent)) jitterSent++; await sleep(5); }
        check('eight seconds of asking every 5 ms: the page sent 4 and then a line every 1.25 s (9 to 11)', jitterSent >= 9 && jitterSent <= 11, String(jitterSent));
        const jitterLines = (p) => p.of('chat').filter((e) => /^jitter /.test(e.text)).map((e) => e.sender + ':' + e.text).join('|');
        const jitterWant = Array.from({ length: jitterSent }, (_, i) => '3:jitter ' + i).join('|');
        await until('every line heard', () => [A, B, C, D].every((p) => jitterLines(p) === jitterWant), 3000).catch(() => null);
        check('every line that Dee\'s page sent was heard by all four, in order (the server dropped none)', [A, B, C, D].every((p) => jitterLines(p) === jitterWant), 'heard ' + [A, B, C, D].map((p) => p.of('chat').filter((e) => /^jitter /.test(e.text)).length).join('/') + ' of ' + jitterSent);
        check('(and nobody was thrown out, Dee neither)', [A, B, C, D].every((p) => p.status === 'online') && D.count('refused') === 0);
        D.leave();
        // the lines are said to the room as it is: a seat that is taken over with its key hears nothing of what was said before (the room does not replay) and hears what is said after
        const key = A.key;
        const A2 = page('Ada', code('t'), { key: key });
        A2.connect();
        await step('Ada\'s seat is taken over by a new page with her key (a reload)', () => () => online(A2)() && A.status === 'superseded');
        await sleep(300);
        check('it hears none of the earlier lines', A2.of('chat').length === 0, String(A2.of('chat').length));
        check('Cy says a line', C.say('after the reload') === true);
        await step('and the new page hears what is said after', () => () => A2.of('chat').length === 1 && A2.of('chat')[0].text === 'after the reload' && A2.of('chat')[0].sender === 2);
        check('Ada\'s new page says one, and the others hear it from seat 0', A2.say('back again') === true);
        await step('... from seat 0', () => () => B.of('chat').some((e) => e.text === 'back again' && e.sender === 0));
        A2.leave();
        B.leave();
        C.leave();
    });

    // ---- the other requests at the page's pace: a rename, a plan and a colour move are dropped by the server without a word when they arrive "too early" (docs/NETWORK_PORT.md "Flood control") ------------------------
    // The server counts the moment at which it HANDLES a request. A page that paced a kind of request at exactly the server's rate (a name 3, then 1 a second; a plan or a colour move 6, then 4 a second) lost one
    // whenever the link delayed a request and not the next: the room did not change and the person saw the change vanish. Every kind of the page keeps a margin now (lobby_net.js BUDGETS), as the chat does.
    // Pages that ask as fast as the page lets them, for eight seconds, on a link that delays their words by 0 to 150 ms (in order, from a fixed seed): every request that the page SENT has to be applied by the room.
    await scenario('the renames, the plans and the colour moves: a page that asks as fast as it can, on a link that jitters, loses none', async () => {
        const L = page('Lia', code('u'), { WebSocket: jitteredWebSocket(150, 20251016) });         // the leader: the plans, the colour moves
        const R = page('Rex', code('u'), { WebSocket: jitteredWebSocket(150, 20251017) });         // a guest who renames herself
        const O = page('Oda', code('u'));                                                          // a guest on a good link: she hears the plans, and is moved about
        L.connect();
        await step('Lia makes the room', () => online(L), 5000);
        R.connect();
        await step('Rex joins it (colour 1)', () => () => online(R)() && names(L) === 'Lia|Rex|-|-', 8000);
        O.connect();
        await step('Oda joins it (colour 2; colour 3 is free)', () => () => online(O)() && names(L) === 'Lia|Rex|Oda|-', 8000);
        await sleep(1200);                                                                         // (the pages' links are quiet; every budget of the server is whole)
        const planBase = O.count('plan'), renameBase = L.count('renamed');
        const started = Date.now();
        let planSent = 0, renameSent = 0;
        const sentNames = [];
        while (Date.now() - started < 8500) {
            if (L.setPlan({ map: '', kinds: [N.PLAN.Open, N.PLAN.Open, N.PLAN.Open, (planSent + 1) % 5], teamA: N.NO_TEAM, teamB: N.NO_TEAM })) planSent++;       // (colour 3 changes every time, from Open at the start: no plan equals the one before it)
            if (R.rename('Rex ' + renameSent)) { sentNames.push('Rex ' + renameSent); renameSent++; }
            await sleep(5);
        }
        check('8.5 seconds of asking every 5 ms: the plans were 5 at once and then 3.2 a second (30 to 34)', planSent >= 30 && planSent <= 34, String(planSent));
        check('... the renames 2 at once and then one every 1.25 s (8 to 10)', renameSent >= 8 && renameSent <= 10, String(renameSent));
        const renamed = () => L.of('renamed').slice(renameBase).map((e) => e.to);
        await until('every plan and every name heard', () => O.count('plan') - planBase === planSent && renamed().length === renameSent, 3000).catch(() => null);
        check('every plan that Lia\'s page sent was applied and shown to the others (the server dropped none)', O.count('plan') - planBase === planSent, 'heard ' + (O.count('plan') - planBase) + ' of ' + planSent);
        check('(the last plan is the room\'s: colour 3 is ' + (planSent % 5) + ')', O.room.plan[3] === planSent % 5 && L.room.plan[3] === planSent % 5, 'plan ' + O.room.plan.join());
        check('every name that Rex\'s page sent was taken, in order (the server dropped none)', renamed().join('|') === sentNames.join('|'), 'heard ' + renamed().length + ' of ' + renameSent + ': ' + renamed().join('|'));
        check('(and nobody was thrown out)', [L, R, O].every((p) => p.status === 'online') && [L, R, O].every((p) => p.count('refused') === 0 && p.count('removed') === 0));
        // the colour moves: Lia puts Oda in colour 3 and back, as fast as the page lets her (and waits for the room to show the move before she asks for the next, as a screen does: a guard of an older seating is no move)
        await sleep(1200);
        check('Lia sets every colour to Open again (a planned bot on a colour is not a place to move to)', L.setPlan({ map: '', kinds: [0, 0, 0, 0], teamA: N.NO_TEAM, teamB: N.NO_TEAM }) === true);
        await step('... and the room shows it', () => () => L.room.plan.join() === '0,0,0,0' && O.room.plan.join() === '0,0,0,0');
        const moveBase = L.count('moved');
        const moveStarted = Date.now();
        let moveSent = 0, odaAt = 2;                                                                // (Oda sits in colour 2; colour 3 is free)
        while (Date.now() - moveStarted < 6000) {
            if (L.seatOf('Oda') === odaAt) {                                                       // (the room has shown the last move)
                const to = odaAt === 2 ? 3 : 2;
                if (L.move(odaAt, to)) { moveSent++; odaAt = to; }
            }
            await sleep(5);
        }
        check('6 seconds of moving Oda back and forth as fast as the page lets Lia: 5 at once and then 3.2 a second at most (10 to 25 moves)', moveSent >= 10 && moveSent <= 25, String(moveSent));
        await until('the last move shown', () => L.seatOf('Oda') === odaAt && L.count('moved') - moveBase === moveSent, 3000).catch(() => null);
        check('every colour move that Lia\'s page sent was made, and shown (the server dropped none)', L.count('moved') - moveBase === moveSent && L.seatOf('Oda') === odaAt, 'heard ' + (L.count('moved') - moveBase) + ' of ' + moveSent + ', Oda in colour ' + L.seatOf('Oda') + ', wanted ' + odaAt);
        check('(and nobody was thrown out)', [L, R, O].every((p) => p.status === 'online') && [L, R, O].every((p) => p.count('refused') === 0));
        L.leave();
        R.leave();
        O.leave();
    });

    for (const c of ALL) { try { c.leave(); } catch (e) { /* closed already */ } }
    await sleep(100);
    console.log('lobby page client against a real server: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
})().catch((e) => {
    console.log('FAIL the check itself broke: ' + (e && e.stack || e));
    process.exit(1);
});

setTimeout(() => { console.log('FAIL the check did not end in 150 s'); process.exit(1); }, 150000).unref();
