// Runs the game page's small hub socket (web/hub_here.js, docs/HOME_SCREEN.md "What the page tells the hub") against a FAKE socket, a fake clock and a fake page, in plain node. What it checks:
//   1. the pure parts: the note of the front page (only what is of the right kind, another room's note is none), the public list's answer for a room (public with its map and seats, or private), where the page is (a room,
//      a game against the computer, nothing for a recording, a live match, a test map or a page with no game), the backoff, the words for the hub;
//   2. a match of a room: one socket at page start, one `here` play with the note's details after a welcome that names the person, a private room says `pub`:false and nothing else, a guest (the note, or the welcome)
//      opens or keeps nothing, a note of another room is none, the match over says `wait` (no seats, no code), a `bye` stops for good;
//   3. a game against the computer: `solo` and an original's map only;
//   4. a shared link (no note): the public list is read once (credentials omitted); a room that is in it is public with the list's words, one that is not (or an answer that is wrong, or none) is private;
//   5. the socket: nothing but welcome, bye and err is looked at, never two `here` in 3 seconds, a close or an error is a new try with a backoff that grows to 60 s and starts again only after a good socket stays,
//      one socket at a time, a leaving page closes it, nothing is sent but `here`, and nothing breaks the page whatever comes;
//   6. the source: no markup, no storage written, no chat frame sent, nothing but the hub's own door.
// tests/scripts/test_web_hub_here.py runs this with node (the quick tier). usage: node web_hub_here_check.js web/hub_here.js     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');

const file = process.argv[2];
if (!file) { console.log('usage: web_hub_here_check.js hub_here.js'); process.exit(2); }
const source = fs.readFileSync(path.resolve(file), 'utf8');
const Here = require(path.resolve(file));

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

// ---- the fakes ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function makeClock() {
    const c = { t: 0, timers: [], next: 1 };
    c.setTimeout = (fn, ms) => { const id = c.next++; c.timers.push({ id, at: c.t + (ms || 0), fn }); return id; };
    c.clearTimeout = (id) => { c.timers = c.timers.filter((x) => x.id !== id); };
    c.advance = (ms) => {
        const until = c.t + ms;
        for (let turns = 0; ; turns++) {
            if (turns > 5000) throw new Error('a storm of timers');
            const due = c.timers.filter((x) => x.at <= until).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            c.timers = c.timers.filter((x) => x !== due);
            c.t = Math.max(c.t, due.at);
            due.fn();
        }
        c.t = until;
    };
    c.now = () => c.t;
    return c;
}
const ARGS_ROOM = (code) => ['--aspect', '16:9', '--join-url', 'wss://ants.example/ws', '--room', code || 'k7m2xq', '--seat', '1', '--name', 'maple'];
const ARGS_SOLO = (map) => ['--map', 'Original-Ants/Maps/' + (map || 'TREASURE.LVL'), '--play', '--bot', '1:easy', '--bot', '2:easy', '--name', 'maple'];
const NOTE = (o, code) => JSON.stringify(Object.assign({ v: 1, named: 1, r: Here.hashOf(code || 'k7m2xq'), pub: true, map: 'ISLANDS.LVL', n: 3, of: 4 }, o || {}));
const WELCOME = (o) => JSON.stringify(Object.assign({ t: 'welcome', v: 1, you: { name: 'maple', mod: false }, login: true, presence: true, me: 1, online: 2, history: [] }, o));

function rig(o) {
    o = o || {};
    const clock = makeClock();
    const sockets = [], fetches = [], opened = [], writes = [];
    class FakeSocket {
        constructor(url) { this.url = url; this.sent = []; this.closed = false; sockets.push(this); }
        send(text) { if (this.closed) throw new Error('closed'); this.sent.push(JSON.parse(text)); }
        close() { this.closed = true; }
        hear(text) { if (this.onmessage) this.onmessage({ data: text }); }
        drop() { if (this.onclose) this.onclose(); }
    }
    const store = { items: {}, getItem(k) { return Object.prototype.hasOwnProperty.call(store.items, k) ? store.items[k] : null; }, setItem(k, v) { writes.push(k); store.items[k] = String(v); }, removeItem(k) { delete store.items[k]; } };
    if (o.note !== undefined) store.items['ants.here'] = o.note;
    const win = { location: { protocol: o.protocol || 'https:', host: 'ants.example' }, listeners: {}, addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); } };
    if (o.matchOver) win.antsMatchOver = o.matchOver;
    const answer = o.lobbies;
    const fetchFn = (url, init) => { fetches.push({ url, init }); return o.fetchFails ? Promise.reject(new Error('down')) : Promise.resolve({ ok: o.fetchOk !== false, json: () => Promise.resolve(answer) }); };
    const env = { win, args: o.args || ARGS_ROOM(), replay: !!o.replay, test: !!o.test, WebSocket: o.noSocket ? undefined : FakeSocket, fetch: fetchFn, now: clock.now, random: () => (o.random === undefined ? 0.5 : o.random), setTimeout: clock.setTimeout, clearTimeout: clock.clearTimeout, session: store };
    const r = { clock, sockets, fetches, win, store, writes, env, FakeSocket };
    r.ctl = Here.begin(env);
    r.flush = async () => { for (let i = 0; i < 6; i++) await Promise.resolve(); };
    r.here = (i) => (sockets[i === undefined ? sockets.length - 1 : i] || { sent: [] }).sent;
    r.last = () => sockets[sockets.length - 1];
    r.welcome = (i, o2) => { (i === undefined ? r.last() : sockets[i]).hear(WELCOME(o2)); return r; };
    return r;
}

(async () => {
    // ---- 1. the pure parts ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const note = (o, code) => Here.noteOf(NOTE(o, code), code || 'k7m2xq');
        same('a note: the details of a public room', note(), { named: 1, pub: true, map: 'ISLANDS.LVL', n: 3, of: 4 });
        same('a note of a private room is only that', note({ pub: false, map: 'SECRET.LVL', n: 2, of: 3 }), { named: 1, pub: false });
        check('a note of another room, or a damaged one, is none', Here.noteOf(NOTE(), 'zzz999') === null && Here.noteOf('{', 'k7m2xq') === null && Here.noteOf(null, 'k7m2xq') === null && Here.noteOf(NOTE({ v: 2 }), 'k7m2xq') === null && Here.noteOf(NOTE({ named: 2 }), 'k7m2xq') === null && Here.noteOf(NOTE({ pub: 'yes' }), 'k7m2xq') === null && Here.noteOf(NOTE({ r: undefined }), 'k7m2xq') === null);
        same('odd parts of a note are left out, not kept', [note({ map: '../x.lvl' }), note({ n: 5 }), note({ n: 4, of: 2 }), note({ named: 0 })], [{ named: 1, pub: true, n: 3, of: 4 }, { named: 1, pub: true, map: 'ISLANDS.LVL' }, { named: 1, pub: true, map: 'ISLANDS.LVL' }, { named: 0, pub: true, map: 'ISLANDS.LVL', n: 3, of: 4 }]);
        const list = (rows) => ({ format: 1, lobbies: rows });
        const row = (o) => Object.assign({ code: 'k7m2xq', host: 'ann', map: 'TREASURE.LVL', mode: 'x', people: 1, bots: 1, free: 2, size: 4, age: 3 }, o);
        same('a room in the public list is public with the list\'s map and seats', Here.listed(list([row({ code: 'other1' }), row()]), 'k7m2xq'), { pub: true, map: 'TREASURE.LVL', n: 2, of: 4 });
        same('a room that is not in the list, or an answer that is not a list, is private', [Here.listed(list([row({ code: 'other1' })]), 'k7m2xq'), Here.listed(null, 'k7m2xq'), Here.listed({ format: 2, lobbies: [row()] }, 'k7m2xq'), Here.listed({ format: 1 }, 'k7m2xq')], [{ pub: false }, { pub: false }, { pub: false }, { pub: false }]);
        same('the list\'s odd words are not kept', Here.listed(list([row({ map: '../a.lvl', people: 9 })]), 'k7m2xq'), { pub: true });
        same('where the page is: a room, a game against the computer (an original\'s map or none), nothing else', [
            Here.modeOf(ARGS_ROOM(), false, false), Here.modeOf(ARGS_SOLO(), false, false), Here.modeOf(ARGS_SOLO('MY OWN.LVL'), false, false), Here.modeOf(ARGS_SOLO('treasure.lvl'), false, false),
            Here.modeOf(ARGS_ROOM(), true, false), Here.modeOf(ARGS_SOLO(), false, true), Here.modeOf(['--map', 'Original-Ants/Maps/TREASURE.LVL', '--play', '--alone'], false, false), Here.modeOf([], false, false), Here.modeOf(['--join-url', 'wss://x/ws'], false, false), Here.modeOf(['--join-url', 'wss://x/ws', '--room', 'a b'], false, false), Here.modeOf(null, false, false)],
        [{ kind: 'room', code: 'k7m2xq' }, { kind: 'solo', map: 'TREASURE.LVL' }, { kind: 'solo', map: '' }, { kind: 'solo', map: 'TREASURE.LVL' }, null, null, null, null, null, null, null]);
        same('the backoff (the chat dock\'s rule): 1 s, doubling, from half to one and a half steps, within 1 to 60 s', [Here.delayOf(0, 0.5), Here.delayOf(1, 0.5), Here.delayOf(3, 0.5), Here.delayOf(20, 0.5), Here.delayOf(20, 1), Here.delayOf(0, 0), Here.delayOf(0, 7)], [1250, 2000, 8000, 45000, 60000, 1000, 1250]);
        same('the words: play with the details; over: wait, with the map of a public room and no seats and no code; a private room says nothing', [
            Here.frameOf({ pub: true, map: 'ISLANDS.LVL', n: 3, of: 4 }, false), Here.frameOf({ pub: true, map: 'ISLANDS.LVL', n: 3, of: 4 }, true), Here.frameOf({ pub: false }, false), Here.frameOf({ pub: false }, true), Here.frameOf({ solo: true, map: 'TREASURE.LVL' }, false)],
        [{ t: 'here', at: 'play', pub: true, map: 'ISLANDS.LVL', n: 3, of: 4 }, { t: 'here', at: 'wait', pub: true, map: 'ISLANDS.LVL' }, { t: 'here', at: 'play', pub: false }, { t: 'here', at: 'wait', pub: false }, { t: 'here', at: 'play', solo: true, map: 'TREASURE.LVL' }]);
    }

    // ---- 2. a match of a room ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const r = rig({ note: NOTE() });
        same('a socket at page start, to the hub\'s door of this site (wss on https), no list read, nothing said before the welcome', [r.sockets.length, r.sockets[0].url, r.fetches.length, r.here()], [1, 'wss://ants.example/hub/ws', 0, []]);
        r.welcome();
        same('a welcome that names the person: one `here` play with the note\'s details and nothing more', r.here(), [{ t: 'here', at: 'play', pub: true, map: 'ISLANDS.LVL', n: 3, of: 4 }]);
        r.clock.advance(60000);
        same('and no more of them while nothing changes', r.here().length, 1);
        r.win.antsMatchOver('over');
        same('the match over: `wait`, no seats, no code (at once: the last was a minute ago)', r.here().slice(1), [{ t: 'here', at: 'wait', pub: true, map: 'ISLANDS.LVL' }]);
        check('nothing was written to the browser', r.writes.length === 0);
    }
    {
        const r = rig({ note: NOTE({ pub: false, map: 'SECRET.LVL', n: 2, of: 3 }) });
        r.welcome();
        same('a private room says it is private and nothing else (no map, no seats, no code, no name)', r.here(), [{ t: 'here', at: 'play', pub: false }]);
        r.win.antsMatchOver('over'); r.clock.advance(3000);
        same('and after the match, wait, still private', r.here().slice(1), [{ t: 'here', at: 'wait', pub: false }]);
        check('nothing of the room\'s code or the person\'s name was sent', JSON.stringify(r.here()).indexOf('k7m2xq') === -1 && JSON.stringify(r.here()).indexOf('maple') === -1);
    }
    {
        const g = rig({ note: NOTE({ named: 0 }) });
        check('a guest (the note says so): no socket, no list read', g.sockets.length === 0 && g.fetches.length === 0 && g.ctl === null);
        const w = rig({ note: NOTE() });
        w.welcome(undefined, { you: null });
        check('a guest (the welcome says so): nothing is said, the socket is closed and never tried again', w.here().length === 0 && w.last().closed && w.ctl.state().stopped);
        w.clock.advance(10 * 60000);
        check('... not in ten minutes either', w.sockets.length === 1);
        const x = rig({ note: NOTE() });
        x.welcome(undefined, { v: 2 });
        check('a hub of another version: nothing is said, for good', x.here().length === 0 && x.ctl.state().stopped);
        const b = rig({ note: NOTE() });
        b.welcome(); b.last().hear(JSON.stringify({ t: 'bye', why: 'kick' }));
        b.clock.advance(10 * 60000);
        check('a `bye` (kicked, logged out): stopped, no new try', b.last().closed && b.sockets.length === 1);
        const other = rig({ note: NOTE({}, 'zzz999') });
        other.welcome();
        check('a note of another room is no note: the list is asked (a shared link)', other.fetches.length === 1);
    }
    {
        const calls = [];
        const r = rig({ note: NOTE(), matchOver: function (t) { calls.push(t); return 'kept'; } });
        r.welcome();
        const back = r.win.antsMatchOver('the text');
        check('the page\'s own match-over keeps working (called first, with its words, its answer kept)', calls.join() === 'the text' && back === 'kept');
        const t = rig({ note: NOTE(), matchOver: function () { throw new Error('boom'); } });
        t.welcome();
        let thrown = false;
        try { t.win.antsMatchOver('x'); } catch (e) { thrown = true; }
        t.clock.advance(3000);
        check('and a page\'s own error is the page\'s: the socket still says wait', thrown && t.here().some((m) => m.at === 'wait'));
    }

    // ---- 3. a game against the computer -------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const r = rig({ args: ARGS_SOLO() });
        check('a socket at page start, the list is not read, no match-over hook', r.sockets.length === 1 && r.fetches.length === 0 && r.win.antsMatchOver === undefined);
        r.welcome();
        same('solo and an original\'s map, nothing else', r.here(), [{ t: 'here', at: 'play', solo: true, map: 'TREASURE.LVL' }]);
        const o = rig({ args: ARGS_SOLO('MY OWN.LVL') });
        o.welcome();
        same('a map of the players is not named', o.here(), [{ t: 'here', at: 'play', solo: true }]);
        const g = rig({ args: ARGS_SOLO() });
        g.welcome(undefined, { you: null });
        g.clock.advance(120000);
        check('a guest\'s game against the computer: the welcome closes it for good', g.here().length === 0 && g.last().closed && g.sockets.length === 1);
    }
    {
        const none = [rig({ args: ARGS_ROOM(), replay: true }), rig({ args: ARGS_SOLO(), replay: true }), rig({ args: ARGS_SOLO(), test: true }), rig({ args: [] }), rig({ args: ['--map', 'Original-Ants/Maps/TREASURE.LVL', '--play', '--alone'] }), rig({ args: ARGS_ROOM(), noSocket: true })];
        check('no socket and no list for a recording, a live match, a frame, a test map, a page with no game, a game for one with no computer player, or a browser with no sockets', none.every((n) => n.ctl === null && n.sockets.length === 0 && n.fetches.length === 0));
    }

    // ---- 4. a shared link ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const row = { code: 'k7m2xq', host: 'ann', map: 'TREASURE.LVL', mode: 'x', people: 1, bots: 0, free: 3, size: 4, age: 3 };
        const r = rig({ lobbies: { format: 1, lobbies: [row] } });
        same('no note: one read of the public list, no credentials, nothing stored', [r.fetches.length, r.fetches[0].url, r.fetches[0].init.credentials, r.sockets.length], [1, '/lobbies', 'omit', 1]);
        r.welcome();
        same('a welcome before the list has answered says nothing yet', r.here(), []);
        await r.flush();
        same('a room that is in the list: public with the list\'s map and seats', r.here(), [{ t: 'here', at: 'play', pub: true, map: 'TREASURE.LVL', n: 1, of: 4 }]);
        const n = rig({ lobbies: { format: 1, lobbies: [] } });
        await n.flush(); n.welcome();
        same('a room that is not in the list: private', n.here(), [{ t: 'here', at: 'play', pub: false }]);
        const f = rig({ fetchFails: true });
        await f.flush(); f.welcome();
        same('a list that cannot be read: private', f.here(), [{ t: 'here', at: 'play', pub: false }]);
        const bad = rig({ fetchOk: false });
        await bad.flush(); bad.welcome();
        same('a list that answers with an error: private', bad.here(), [{ t: 'here', at: 'play', pub: false }]);
        const slow = rig({ lobbies: { format: 1, lobbies: [row] } });
        slow.env.fetch = () => new Promise(() => {});
        const s2 = Here.begin(slow.env);
        slow.sockets[slow.sockets.length - 1].hear(WELCOME());
        slow.clock.advance(Here.MS.lobbies + 10);
        same('a list that never answers: private after a few seconds', slow.sockets[slow.sockets.length - 1].sent, [{ t: 'here', at: 'play', pub: false }]);
        s2.stop();
        r.ctl.over(); r.clock.advance(3000);
        same('a public room that is over: wait with the map and no seats', r.here().slice(1), [{ t: 'here', at: 'wait', pub: true, map: 'TREASURE.LVL' }]);
        const guest = rig({ lobbies: { format: 1, lobbies: [row] } });
        await guest.flush(); guest.welcome(undefined, { you: null });
        check('a guest of a shared link: closed for good, nothing said', guest.here().length === 0 && guest.ctl.state().stopped);
    }

    // ---- 5. the socket ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const r = rig({ note: NOTE() });
        r.welcome();
        ['{"t":"line","id":1,"text":"hi"}', '{"t":"who","full":true}', '{"t":"online","n":3}', 'not json', '{"t":"err","code":"here"}', '', '{"t":"welcome"'].forEach((t) => r.last().hear(t));
        r.last().hear(JSON.stringify({ x: 1 })); r.last().hear(42);
        check('chat lines, the list, odd frames and an err are ignored (and break nothing)', r.here().length === 1 && !r.ctl.state().stopped);
        check('nothing is sent but `here`', r.sockets.every((s) => s.sent.every((m) => m.t === 'here')));
        r.last().hear(WELCOME());
        check('a second welcome on the same socket is nothing', r.here().length === 1);
    }
    {
        const r = rig({ note: NOTE(), matchOver: null });
        r.welcome();
        r.win.antsMatchOver(); r.win.antsMatchOver(); r.clock.advance(3000);
        same('the match over twice is one `wait`', r.here().filter((m) => m.at === 'wait').length, 1);
        const p = rig({ note: NOTE() });
        const stamps = [];
        const send = p.last().send.bind(p.last());
        p.last().send = (t) => { stamps.push(p.clock.now()); send(t); };
        p.welcome();
        p.clock.advance(1000); p.ctl.over(); p.clock.advance(5000);
        same('never two `here` within 3 seconds: the second waits for the first to be 3 seconds old', stamps, [0, 3000]);
    }
    {
        const r = rig({ note: NOTE() });
        r.welcome();
        r.clock.advance(5000);
        const first = r.last();
        first.drop();
        check('a close: no new socket yet, the old one let go', r.sockets.length === 1 && first.closed);
        r.clock.advance(Here.delayOf(0, 0.5) - 1);
        check('... not before the wait (1 s)', r.sockets.length === 1);
        r.clock.advance(1);
        check('... then one new socket, and one only', r.sockets.length === 2);
        r.welcome();
        same('the new socket says it again at once (the last `here` is long ago)', r.here(), [{ t: 'here', at: 'play', pub: true, map: 'ISLANDS.LVL', n: 3, of: 4 }]);
    }
    {
        const r = rig({ note: NOTE() });
        const waits = [];
        for (let i = 0; i < 9; i++) {
            const before = r.clock.now();
            r.last().drop();
            let step = 0;
            while (r.sockets.length === i + 1 && step < 70000) { r.clock.advance(100); step += 100; }
            waits.push(r.clock.now() - before);
        }
        same('a socket that fails and fails: about 1, 2, 4, 8, 16, 32 s, then 45 s (half to one and a half of 60), never more', waits, [1300, 2000, 4000, 8000, 16000, 32000, 45000, 45000, 45000]);
        const q = rig({ note: NOTE() });
        q.last().drop(); q.clock.advance(1250);
        q.welcome(); q.last().drop(); q.clock.advance(2000);
        check('a socket that opens, is welcomed and closes at once does not start the backoff again', q.sockets.length === 3 && q.ctl.state().attempt === 2, 'attempt ' + q.ctl.state().attempt);
        const s = rig({ note: NOTE() });
        s.last().drop(); s.clock.advance(1250);
        s.welcome(); s.clock.advance(Here.MS.stable + 10);
        check('a socket that stays after its welcome starts it again', s.ctl.state().attempt === 0);
        const err = rig({ note: NOTE() });
        err.last().onerror();
        err.last().onclose && err.sockets[0].onclose;
        check('an error is a close', err.ctl.state().socket === false);
        const t = rig({ note: NOTE() });
        t.clock.advance(Here.MS.connect + 10);
        check('a socket that is never welcomed is let go after 15 s and tried again', t.sockets[0].closed && t.ctl.state().attempt === 1);
    }
    {
        const r = rig({ note: NOTE() });
        r.welcome();
        r.win.listeners.pagehide.forEach((fn) => fn());
        r.clock.advance(120000);
        check('a leaving page closes the socket and nothing is tried again', r.sockets.length === 1 && r.last().closed && r.ctl.state().stopped);
        const w = rig({ note: NOTE(), protocol: 'http:' });
        check('http: ws', w.sockets[0].url === 'ws://ants.example/hub/ws');
        const th = rig({ note: NOTE() });
        th.env.WebSocket = function () { throw new Error('refused'); };
        const t2 = Here.begin(th.env);
        check('a browser that refuses a socket does not break the page (and tries again later)', t2 !== null && t2.state().socket === false);
        t2.stop();
        const st = rig({ note: NOTE() });
        st.env.session = { getItem() { throw new Error('private window'); } };
        const s2 = Here.begin(st.env);
        check('a browser with no storage is a page with no note: the list is asked', s2 !== null && st.fetches.length === 1);
        s2.stop();
    }

    // ---- 6. the source ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    const code = source.split('\n').filter((l) => !/^\s*\/\//.test(l)).map((l) => l.replace(/\s\/\/\s.*$/, '')).join('\n');
    check('plain ES5', !/\bconst\b|\blet\b|=>|`|\bclass\b|\basync\b|\bawait\b|\bimport\b/.test(code));
    check('no markup, no storage written, no cookie, no beacon', !/innerHTML|outerHTML|document\.|setItem|removeItem|localStorage|document\.cookie|sendBeacon|XMLHttpRequest|eval\(|new Function/.test(code));
    check('only the hub\'s own door and the public list are named', (code.match(/'\/[a-z/]+'/g) || []).sort().join() === "'/hub/ws','/lobbies'");
    check('it says nothing but `here`', (code.match(/t: '[a-z]+'/g) || []).join() === "t: 'here'");
    console.log('hub_here: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.log('FAIL the check broke: ' + (e && e.stack || e)); process.exit(1); });
