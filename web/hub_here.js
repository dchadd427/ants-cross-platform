// What the game page tells the chat hub (docs/HOME_SCREEN.md, "What the page tells the hub"): that its visitor is in a match, so that the players list of Home shows them under Playing. A small socket to the site's own hub
// door (/hub/ws, the same login and limits as the chat dock): it says only {"t":"here",...} and listens only for `welcome`, `bye` and `err`; no chat line is read or written, nothing is drawn, nothing is logged, and the game never waits
// for it. ES5, no module system: a page gets AntsHubHere, node gets module.exports (tests/scripts/web_hub_here_check.js).
//   * When: a match of a room (the address has a game server's door and a room code) or a game against the computer on this device (a bot, an ordinary map). Never a recording, a live match, a frame of another page or a test map.
//   * What: the front page leaves a note in this tab's sessionStorage when it hands the visitor over (`ants.here`: { v:1, named, r, pub, map, n, of }: no code, no name, no id; `r` only tells that the note is this room's).
//     A room with no note (a shared link) is looked up once in the public list (GET /lobbies): a room that is in it is public, with the list's map and seats; one that is not is private (the safe way).
//     A visitor who is not logged in has no row: the note says so and no socket is opened; a page with no note learns it from the welcome (`you` is null) and closes for good.
//   * How: one socket for the tab, opened at page start. A welcome with `you` -> one `here` (at most one every 3 seconds); the match over (window.antsMatchOver) -> `here` wait; a close or an error -> a new try after 1 s,
//     doubling to 60 s with jitter (the chat dock's rule); a `bye` (kick, logout) or a leaving page -> it stops. A private room or match says `pub`:false and nothing more; a game against the computer says `solo` and its map.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHubHere = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var PATH = '/hub/ws', KEY = 'ants.here', VERSION = 1, LOBBIES = '/lobbies';
    var BACKOFF = { min: 1000, max: 60000, low: 0.5, high: 1.5 };
    var MS = { tell: 3000, connect: 15000, stable: 10000, lobbies: 8000 };       // (tell: one `here` this often at the most; stable: a socket that stays this long after its welcome is a good one, the backoff starts again)
    var ORIGINALS = { 'TINY.LVL': 1, 'SMALL.LVL': 1, 'MEDIUM.LVL': 1, 'GAUNTLET.LVL': 1, 'TREASURE.LVL': 1, 'ISLANDS.LVL': 1 };
    var ROOM = /^[A-Za-z0-9_-]{1,32}$/, FRAME = /^\s*\{\s*"t"\s*:\s*"(welcome|bye|err)"/;

    // ---- the pure parts ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    function whole(v, low, high) { return typeof v === 'number' && v === Math.floor(v) && v >= low && v <= high; }
    // a map's file name as the game takes it (printable ASCII, 5 - 64 characters, no / \ : * ? " < > |, no leading dot, ends in .lvl or .LVL)
    function validMap(name) {
        return typeof name === 'string' && name.length >= 5 && name.length <= 64 && /^[\x20-\x7e]*$/.test(name) && !/[\/\\:*?"<>|]/.test(name) && name.charAt(0) !== '.' && /\.(lvl|LVL)$/.test(name);
    }
    // what a note tells of a room without being the room's code: eight hex digits of a plain hash (FNV-1a), the same on the front page (the front page's players script)
    function hashOf(code) {
        var h = 0x811c9dc5, i;
        for (i = 0; i < code.length; i++) { h ^= code.charCodeAt(i) & 255; h = (h + ((h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24))) >>> 0; }
        return ('00000000' + h.toString(16)).slice(-8);
    }
    // The note of the front page for this room: { named, pub, map, n, of } (only what is of the right kind), or null (none, damaged, or another room's)
    function noteOf(text, code) {
        var m;
        try { m = JSON.parse(text); } catch (e) { return null; }
        if (!m || typeof m !== 'object' || m.v !== VERSION || (m.named !== 0 && m.named !== 1) || typeof m.pub !== 'boolean' || m.r !== hashOf(code)) return null;
        var out = { named: m.named, pub: m.pub };
        if (m.pub) {
            if (validMap(m.map)) out.map = m.map;
            if (whole(m.n, 0, 4) && whole(m.of, 1, 4) && m.n <= m.of) { out.n = m.n; out.of = m.of; }
        }
        return out;
    }
    // What a room's code is in the public list (GET /lobbies, { format: 1, lobbies: [...] }): public with the list's own map and seats when it is there, private when it is not or the answer is not that shape
    function listed(data, code) {
        var out = { pub: false }, rows = data && data.format === 1 && Array.isArray(data.lobbies) ? data.lobbies : [], i, r;
        for (i = 0; i < rows.length && i < 200; i++) {
            r = rows[i];
            if (!r || typeof r !== 'object' || r.code !== code) continue;
            out = { pub: true };
            if (validMap(r.map)) out.map = r.map;
            if (whole(r.people, 0, 4) && whole(r.bots, 0, 4) && whole(r.size, 1, 4) && r.people + r.bots <= r.size) { out.n = r.people + r.bots; out.of = r.size; }
            break;
        }
        return out;
    }
    // Where this page is: { kind: 'room', code } for a match of a room, { kind: 'solo', map } for a game against the computer (map: an original's file name, else ''), null for every other page.
    // `args` are the game's arguments as the page built them (the page has checked them), `replay` a recording / live match / frame, `test` a test map.
    function modeOf(args, replay, test) {
        if (replay || test || !Array.isArray(args)) return null;
        var at = function (name) { var i = args.indexOf(name); return i === -1 || i + 1 >= args.length ? null : args[i + 1]; };
        if (args.indexOf('--join-url') !== -1) {
            var code = at('--room');
            return typeof code === 'string' && ROOM.test(code) ? { kind: 'room', code: code } : null;
        }
        if (args.indexOf('--bot') === -1) return null;
        var file = String(at('--map') || '').replace(/^.*\//, '').toUpperCase();
        return { kind: 'solo', map: ORIGINALS[file] === 1 ? file : '' };
    }
    function delayOf(attempt, random) {                                  // the wait before retry number `attempt` (0 is the first): the step is 1 s, doubling to 60 s; from half to one and a half steps, within 1 to 60 s
        var base = Math.min(BACKOFF.max, BACKOFF.min * Math.pow(2, Math.min(attempt, 12)));
        var low = Math.max(BACKOFF.min, base * BACKOFF.low), high = Math.min(BACKOFF.max, base * BACKOFF.high);
        return Math.round(low + (high - low) * (random >= 0 && random <= 1 ? random : 0.5));
    }
    // The words for the hub: while the match is on (`over` false) it is `play` with the details, when it is over (the results screen holds a seat in the room) `wait` with no seats and no code
    function frameOf(details, over) {
        var f = { t: 'here' }, k;
        if (!over) {
            f.at = 'play';
            for (k in details) f[k] = details[k];
        } else {
            f.at = 'wait';
            f.pub = details.pub === true;
            if (f.pub && details.map) f.map = details.map;
        }
        return f;
    }

    // ---- the socket --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // begin({ win, args, replay, test, WebSocket, fetch, now, random, setTimeout, clearTimeout, session }) returns { state(), over(), stop() }, or null where the page says nothing (and opens nothing)
    function begin(env) {
        var win = env.win, mode;
        try { mode = modeOf(env.args, env.replay, env.test); } catch (e) { return null; }
        if (!mode) return null;
        var Socket = env.WebSocket || win.WebSocket, now = env.now || function () { return Date.now(); }, rnd = env.random || Math.random;
        var later = env.setTimeout || function (f, ms) { return win.setTimeout(f, ms); }, drop = env.clearTimeout || function (t) { win.clearTimeout(t); };
        var details = null, note = null;
        if (typeof Socket !== 'function') return null;
        if (mode.kind === 'room') {
            try { note = noteOf((env.session || win.sessionStorage).getItem(KEY), mode.code); } catch (e) { note = null; }
            if (note && note.named === 0) return null;                    // (a visitor who is not logged in is no row: nothing is opened)
            if (note) { details = { pub: note.pub }; copy(note, details, ['map', 'n', 'of']); }
        } else details = mode.map ? { solo: true, map: mode.map } : { solo: true };
        var sock = null, stopped = false, done = false, welcomed = false, attempt = 0, sentSig = null, lastSent = -1e15, over = false, timers = { retry: 0, connect: 0, stable: 0, tell: 0, lobbies: 0 };

        function stopTimer(name) { if (timers[name]) { drop(timers[name]); timers[name] = 0; } }
        function guard(fn) { return function () { try { return fn.apply(this, arguments); } catch (e) { /* what goes wrong in here stays here: the game never waits for it */ return undefined; } }; }
        function copy(from, to, keys) { keys.forEach(function (k) { if (from[k] !== undefined) to[k] = from[k]; }); }
        function close() { var s = sock; sock = null; welcomed = false; if (s) { s.onmessage = s.onclose = s.onerror = null; try { s.close(); } catch (e) { /* closed already */ } } }
        function stop() { stopped = true; ['retry', 'connect', 'stable', 'tell', 'lobbies'].forEach(stopTimer); close(); }
        function flush() {                                               // one `here` when it changed, and not oftener than every 3 seconds
            stopTimer('tell');
            if (stopped || !sock || !welcomed || !details) return;
            var frame = frameOf(details, over), sig = JSON.stringify(frame), wait = lastSent + MS.tell - now();
            if (sig === sentSig) return;
            if (wait > 0) { timers.tell = later(guard(flush), wait); return; }
            try { sock.send(sig); sentSig = sig; lastSent = now(); } catch (e) { /* the close that follows is the answer */ }
        }
        function lost() {
            close(); ['connect', 'stable', 'tell'].forEach(stopTimer);
            if (stopped) return;
            timers.retry = later(guard(go), delayOf(attempt, rnd()));
            attempt++;
        }
        function take(data) {
            if (typeof data !== 'string' || !FRAME.test(data.slice(0, 40))) return;       // (every other frame, the chat lines most of all, is not looked at)
            var m;
            try { m = JSON.parse(data); } catch (e) { return; }
            if (!m || typeof m !== 'object') return;
            if (m.t === 'bye') { stop(); return; }                      // (kicked or logged out: no retry)
            if (m.t !== 'welcome' || welcomed) return;
            if (m.v !== VERSION || !m.you || typeof m.you !== 'object') { stop(); return; }       // (no login or another kind of hub: nothing to say, for good)
            welcomed = true; sentSig = null; stopTimer('connect');
            timers.stable = later(guard(function () { timers.stable = 0; attempt = 0; }), MS.stable);
            flush();
        }
        function go() {
            timers.retry = 0;
            if (stopped || sock) return;
            var s, scheme = win.location.protocol === 'https:' ? 'wss://' : 'ws://';
            try { s = new Socket(scheme + win.location.host + PATH); } catch (e) { lost(); return; }
            sock = s;
            s.onmessage = guard(function (ev) { if (s === sock) take(ev && ev.data); });
            s.onerror = s.onclose = guard(function () { if (s === sock) lost(); });
            timers.connect = later(guard(function () { timers.connect = 0; if (s === sock && !welcomed) lost(); }), MS.connect);
        }
        function known(found) { if (stopped || done || details) return; done = true; stopTimer('lobbies'); details = found; flush(); }
        function overNow() { if (over) return; over = true; flush(); }

        if (!details) {                                                  // a room with no note: look it up once in the public list; not there (or no answer) is private
            var fetcher = env.fetch || win.fetch;
            timers.lobbies = later(guard(function () { timers.lobbies = 0; known({ pub: false }); }), MS.lobbies);
            try {
                fetcher.call(win, LOBBIES, { credentials: 'omit', cache: 'no-store' }).then(function (r) { return r && r.ok ? r.json() : null; }).then(guard(function (data) { known(listed(data, mode.code)); }), guard(function () { known({ pub: false }); }));
            } catch (e) { known({ pub: false }); }
        }
        if (mode.kind === 'room') {                                      // the match over: the results screen holds a seat in the room's lobby
            var previous = win.antsMatchOver;
            win.antsMatchOver = function () {
                try { if (typeof previous === 'function') return previous.apply(this, arguments); } finally { guard(overNow)(); }
                return undefined;
            };
        }
        if (win.addEventListener) win.addEventListener('pagehide', guard(stop));
        go();
        return { state: function () { return { stopped: stopped, welcomed: welcomed, socket: !!sock, mode: mode.kind, attempt: attempt, details: details }; }, over: guard(overNow), stop: guard(stop) };
    }

    return { KEY: KEY, PATH: PATH, MS: MS, BACKOFF: BACKOFF, hashOf: hashOf, validMap: validMap, noteOf: noteOf, listed: listed, modeOf: modeOf, delayOf: delayOf, frameOf: frameOf, begin: begin };
});
