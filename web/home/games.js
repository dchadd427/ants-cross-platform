// The public Room and the Games list (docs/HOME_SCREEN.md, docs/PUBLIC_ROOMS.md): the Room's Public | Private switch, Quick host, and the Games list (GET /lobbies: rows, Join, Join by code), a popover under the Home bar or docked
// in Home's body. Loaded by web/home/home.js after the first paint, with games.css (until then the buttons that it lights stay disabled). ES5: a page gets AntsHomeGames, node gets module.exports
// (tests/scripts/web_home_games_check.js). The client, model and session are the page's own (web/lobby.html), read through `page`.
//   * The room's state is the server's word: the switch is drawn from the last Room message, never from what a press asked, and a request that the page's pace holds back sends nothing.
//   * Everything from the list is UNTRUSTED text: checked field by field (parseList), drawn with textContent, never as markup or a link, and a row that is not of the shape is dropped. At most 50 rows. The one address that a
//     row's text makes is its map's picture (a checked file name, percent-encoded: AntsMapPicker.previewPath).
//   * Matches that run now (GET /live, docs/REPLAYS.md "Live") stand in the list below the open rooms as rows with a Live mark and Watch (a new tab on /play.html?live=<id>); a switch in the list's head (kept in this browser
//     only) turns them off. The door carries no room code and a room that is listed is waiting, so a running match is never a room's row. /live is looked at every 10 s at the most, only while the list shows, and a
//     server without that door (404) or a failed look only hides the section.
//   * The list is polled (30 s closed, 5 s open, never while the tab is hidden or the Room shows, +-20 % of jitter, doubled after a failure). Nothing that goes wrong here reaches Home, the Room or the hand-off:
//     every call out of this file is guarded, and a failed look is only "the list is off".
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeGames = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var LIST_URL = '/lobbies';                           // the site's own address, no query, no credentials
    var EVERY_CLOSED = 30000, EVERY_OPEN = 5000;         // ms between two looks while the popover is closed / open
    var CAP_CLOSED = 300000, CAP_OPEN = 60000;           // the longest wait after failures
    var JITTER = 0.2;                                    // every wait is its base +-20 %
    var FETCH_MS = 8000;                                 // a look that takes longer is a failed look
    var BODY_MAX = 2000000;                              // characters of an answer that are read at all (and the length that an answer may say that it has)
    var FLOOR_MS = 1000;                                 // two looks are at least this far apart, whatever the tab and the screens do
    var SCAN_MAX = 500;                                  // rows that are looked at before the cap, whatever the answer holds
    var ROWS_MAX = 50;                                   // rows that are shown (the server lists 50 at most: the count says "50+")
    var REFRESH_MS = 600000;                             // a public room that the leader looks at says "public" again every 10 minutes (the server's idle rule is 30)
    var ACTIVE_MS = 1200000;                             // a leader who touched nothing (pointer, key, touch) for this long is not at the Room: the repeat stops
    var ASK_MS = 10000;                                  // a Private that was asked for is told from a room that went private by itself for this long
    var TRIES_MAX = 3, TRY_GAP_MS = 300;                 // Quick join tries this many rooms, one after the other, with a wait of 0 to this many ms (random) between two
    var JOINED_MS = 1500;                                // the card "Joined Pebble's room" stands this long, then the Room shows (a press on its button is quicker)
    var CANCEL_AFTER_MS = 700;                           // the Cancel of a Join that waits takes no press for this long (it stands where Join stood: a double press is not a Cancel)
    var TOAST_MS = 6000;                                 // a toast with no question goes after this long
    var RETRY_MS = 1300;                                 // a request that the page's pace held back is made again after this long (the pace is one every 1.25 s)
    var PENDING_MS = 20000;                              // Quick host waits this long for a room that is not ready (then it is forgotten: a room must not turn public much later)
    var LIVE_URL = '/live';                              // the site's own address of the matches that run now (no query, no credentials)
    var LIVE_EVERY = 10000, LIVE_CAP = 300000;           // ms between two looks at /live (never less: the page's own door asks no faster), the longest wait after failures
    var LIVE_MAX = 50;                                   // matches that are shown (the door lists 50 at the most)
    var LIVE_ID = /^[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?$/;           // the shape of a match's id (docs/SERVER.md "Live matches"): nothing else is ever put into an address
    var SEATS = ['Green', 'Red', 'Blue', 'Black'];       // the colors of seats 0 - 3, in the words of the door's players strings
    var CHOICE_KEY = 'ants.home.games.live';             // the one thing that the Games list keeps in this browser: '0' when the matches in progress are turned off
    var NARROW = 720;                                    // px: the page's phone layout (web/lobby.html)
    var SEAT_PERSON = 1, SEAT_CLIENT = 2;                // the Room message's states of a color that a person holds (lobby_net.js SLOT)
    var TEXT = {
        title: 'Open games', loading: 'Looking for games…', off: 'Games list is off right now', offHint: 'Join by code still works.', none: 'none waiting',
        emptyTitle: 'No open games right now.', emptyLine: 'Rooms show up here when their host switches them to Public.', hostOne: 'Host one!', quickHost: 'Quick host', quickJoin: 'Quick join',
        legendPlayer: 'player', legendBot: 'bot', legendOpen: 'open seat', code: 'Join by code', codePlaceholder: 'Room code, like k7m 2xq', join: 'Join',
        private: 'Private', privateSub: 'link only', public: 'Public', publicSub: 'listed', publicWide: ' in Open games', publicRoom: 'Public room', privateRoom: 'Private room', visLabel: 'Who can find this room',
        noteHow: 'Public rooms show your name and map in the Games list.', noteListed: 'Listed. Anyone on the front page can join from Open games. The link works too.', noteWait: 'One moment, then press again.',
        ok: 'OK', makePublic: 'Make it public', rejoin: 'Rejoin', stay: 'Stay here', keepPrivate: 'Keep it private', leaveJoin: 'Leave and join', leaveHost: 'Leave and host', cancel: 'Cancel', reload: 'Reload', best: 'BEST', yours: 'Yours', live: ' · updates live', finding: 'Finding a game…', looking: 'Looking for open games…', roomOpening: 'Opening the room…', goRoom: 'Go to the room', leaveRoom: 'Leave room', notNow: 'Not now',
        noneLine: 'Nobody is waiting for a player. Host one, and others can join you.', noneYours: 'Your room goes public with its map and your friends in it.', noneNew: 'A new room opens for you, and it goes public.', joiningCode: 'Joining the room…', joinFirst: ' Wait for it, or cancel it in the list.', opening: 'The game is opening. If it does not, reload this page.',
        already: 'Your room is already public.', running: 'Your match is running.', idle: 'Your room went private after 30 minutes. Make it public again?',
        hostChanged: 'This room is private again because the host changed.', stillPrivate: 'Your room is still private. Use the Public switch in the room.',
        watch: 'Watch', liveMark: 'Live', inProgress: 'In progress', filter: 'Show games in progress', noLive: 'No games in progress.', soFar: ' so far', nobodyNamed: 'Players not named',
        full: 'That room is full.', started: 'The match in that room has already started.', gone: 'That game is not open any more.', silent: 'The server did not answer. Try again in a moment.'
    };

    // ---- the pure parts ----
    function whole(v, max) { return typeof v === 'number' && isFinite(v) && Math.floor(v) === v && v >= 0 && v <= max; }
    // One row of the list as the page may show it, or null. The nine fields of docs/PUBLIC_ROOMS.md, each of its type and in its range (unknown extra fields are ignored): code (what a room code may be, no capital), host (printable ASCII,
    // up to 32: the server's own rule for a name), map (a map file's name), mode, people, bots, free and size (people + bots + free = size, 1 to 4, a color for a newcomer), age.
    function rowOf(raw, rules) {
        if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return null;
        if (typeof raw.code !== 'string' || !/^[a-z0-9_-]{1,32}$/.test(raw.code)) return null;
        if (typeof raw.host !== 'string' || !/^[\x20-\x7e]{0,32}$/.test(raw.host)) return null;
        if (typeof raw.map !== 'string' || !rules.validMapFile(raw.map)) return null;
        if (typeof raw.mode !== 'string' || !/^[\x20-\x7e]{0,32}$/.test(raw.mode)) return null;
        if (!whole(raw.people, 4) || !whole(raw.bots, 4) || !whole(raw.free, 4) || !whole(raw.size, 4) || !whole(raw.age, 2147483647)) return null;
        if (raw.size < 1 || raw.free < 1 || raw.people + raw.bots + raw.free !== raw.size) return null;
        var known = rules.mapOfFile(raw.map);
        return { code: raw.code, host: raw.host.replace(/^ +| +$/g, '') || 'Player', map: raw.map, mapKey: known ? known.key : '', mapName: known ? known.name : rules.communityName(raw.map).slice(0, 28), people: raw.people, bots: raw.bots, free: raw.free, size: raw.size, age: raw.age };
    }
    // One match of the door's answer to GET /live as the page may show it, or null: the id of the server's shape, the map as the door sends it (the whole file name, like a room's: 'TREASURE.LVL', 'DRY LAND.lvl'; else the one in the id), the seconds it has run (or its turns / 20), and at most four
    // players as the door writes them ("Green (Ann)", "Red", "Blue (Bot (Medium))"): a color word and the name, printable ASCII only. Everything is text, and the only address that is made is /play.html?live=<id> from the checked id.
    function liveOf(raw, rules) {
        if (!raw || typeof raw !== 'object' || Array.isArray(raw) || typeof raw.id !== 'string' || !LIVE_ID.test(raw.id)) return null;
        var file = typeof raw.map === 'string' ? raw.map : raw.id.replace(/-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?$/, '') + '.LVL';
        if (!rules.validMapFile(file)) return null;
        var secs = typeof raw.seconds === 'number' ? raw.seconds : typeof raw.turns === 'number' ? raw.turns / 20 : 0, who = [], known = rules.mapOfFile(file), list = Array.isArray(raw.players) ? raw.players : [];
        for (var i = 0; i < list.length && who.length < 4; i++) {
            if (typeof list[i] !== 'string' || !/^[\x20-\x7e]{1,80}$/.test(list[i])) continue;
            var m = /^(Green|Red|Blue|Black)(?: \(([\s\S]*)\))?$/.exec(list[i]);
            who.push(m ? { seat: SEATS.indexOf(m[1]), name: m[2] || m[1] } : { seat: -1, name: list[i] });
        }
        return { id: raw.id, map: file, mapKey: known ? known.key : '', mapName: known ? known.name : rules.communityName(file).slice(0, 28), seconds: isFinite(secs) && secs > 0 ? Math.min(Math.floor(secs), 359999) : 0, who: who };
    }
    // The matches of an answer ({ live: [...] }), at most 50, an id once, in the door's order (newest first); null when the answer is not that shape (the section is hidden)
    function parseLive(data, rules) {
        if (!data || typeof data !== 'object' || Array.isArray(data) || !Array.isArray(data.live)) return null;
        var out = [], seen = Object.create(null);
        for (var i = 0; i < data.live.length && i < SCAN_MAX && out.length < LIVE_MAX; i++) {
            var e = liveOf(data.live[i], rules);
            if (!e || seen[e.id] === true) continue;
            seen[e.id] = true;
            out.push(e);
        }
        return out;
    }
    // How long a match has run, as a clock: 0:07, 12:00, 61:05
    function clockText(s) { return Math.floor(s / 60) + ':' + (s % 60 < 10 ? '0' : '') + (s % 60); }
    // The wait before the next look at /live: 10 s doubled for every failure in a row up to 5 minutes, then 0 to +20 % (random: 0 to 1): never under 10 s
    function liveDelay(failures, random) {
        var wait = Math.min(LIVE_CAP, LIVE_EVERY * Math.pow(2, Math.min(failures, 10)));
        return Math.round(wait * (1 + JITTER * Math.max(0, Math.min(1, random))));
    }
    // The rows of an answer ({ format: 1, lobbies: [...] }), at most 50, a code once; null when the answer is not that shape (the list is "off"). A row that is not of the shape is dropped, the others stay.
    function parseList(data, rules) {
        if (!data || typeof data !== 'object' || Array.isArray(data) || data.format !== 1 || !Array.isArray(data.lobbies)) return null;
        var rows = [], seen = Object.create(null);
        for (var i = 0; i < data.lobbies.length && i < SCAN_MAX && rows.length < ROWS_MAX; i++) {
            var row = rowOf(data.lobbies[i], rules);
            if (!row || seen[row.code] === true) continue;
            seen[row.code] = true;
            rows.push(row);
        }
        return rows;
    }
    // The wait before the next look: the base (30 s closed, 5 s open) doubled for every failure in a row, up to the cap, +-20 % (random: 0 to 1)
    function delayOf(open, failures, random) {
        var base = open ? EVERY_OPEN : EVERY_CLOSED, cap = open ? CAP_OPEN : CAP_CLOSED;
        var wait = Math.min(cap, base * Math.pow(2, Math.min(failures, 10)));
        return Math.round(wait * (1 - JITTER + 2 * JITTER * Math.min(1, Math.max(0, random))));
    }
    // The rooms, best first: the fullest that still has a free seat (the fewest free seats), then the oldest; rooms that tie keep the list's order (a sort is stable). Quick join takes the first, and the list says which that is.
    function bestOrder(rows) { return rows.slice().sort(function (a, b) { return a.free - b.free || b.age - a.age; }); }
    function countText(n) { return n >= ROWS_MAX ? ROWS_MAX + '+' : String(n); }                  // (the server lists 50 at most: 50 may be more)
    // What a row's seats say: the kinds of the dots (a person, a computer player, an open seat: in that order) and the words
    function seatsOf(row) {
        var dots = [], i;
        for (i = 0; i < row.people; i++) dots.push('player');
        for (i = 0; i < row.bots; i++) dots.push('bot');
        for (i = 0; i < row.free; i++) dots.push('open');
        return { dots: dots, count: (row.people + row.bots) + '/' + row.size, bots: row.bots === 0 ? 'no bots' : row.bots === 1 ? '1 bot' : row.bots + ' bots' };
    }
    function possessive(name) { return name + '’s'; }

    // ---- the screens ----
    // mount({ doc, win, host (Home's div), page (web/lobby.html's bridge: client(), make(), leave(), join(code, name, done), typed(text, done, before), name()), parts: { quickHost, games, pill } (Home's buttons),
    //         ctl: { show(screen, how), screen() }, handoff (START is on its way already: the part mounts waiting), later, cancel, random, now (the clocks: tests give their own) }) returns { sync, shown, leaving, opening, stalled, inert, poll, state }
    function mount(env) {
        var doc = env.doc, win = env.win, host = env.host, page = env.page, parts = env.parts, ctl = env.ctl;
        var rules = win.AntsLobbyRules, Net = win.AntsLobbyNet, Picker = win.AntsMapPicker;
        var later = env.later || function (fn, ms) { return setTimeout(fn, ms); }, cancel = env.cancel || function (id) { clearTimeout(id); };
        var random = env.random || Math.random, now = env.now || function () { return Date.now(); };

        var docked = !!env.dock, rows = [], loaded = false, off = false, failures = 0, isOpen = docked, polling = false, round = 0, pollTimer = null, gone = !!env.handoff, joining = false, joinButtons = [];
        var tracked = null, lastRoom = null, lastSent = 0, refreshTimer = null, refreshFor = null, pending = null, pendingTimer = null, askedAt = 0, locked = false, lockTimer = null, hint = '', lastInput = now(), lastLook = 0, floorTimer = null, joinSeq = 0, cancelTimer = null, paintedSig = null;
        var dialog = null, noticeKind = '', quiet = [], finding = false;
        var live = [], liveLoaded = false, liveOff = false, liveFails = 0, liveTimer = null, liveBusy = false, liveRound = 0, lastLive = 0, liveSig = null, liveLens = [], showLive = readChoice();
        function readChoice() { try { return win.localStorage.getItem(CHOICE_KEY) !== '0'; } catch (e) { return true; } }          // (a browser that keeps nothing: the default, on)
        function keepChoice(on) { try { if (on) win.localStorage.removeItem(CHOICE_KEY); else win.localStorage.setItem(CHOICE_KEY, '0'); } catch (e) { /* not kept: the choice lasts until the page is left */ } }

        function el(tag, cls, text) {
            var e = doc.createElement(tag);
            if (cls) e.className = cls;
            if (text !== undefined) e.textContent = text;
            return e;
        }
        function button(cls, text, onclick) {
            var b = el('button', 'btn ' + cls, text);
            b.type = 'button';
            if (onclick) b.addEventListener('click', onclick);
            return b;
        }
        function guard(fn) { return function () { if (gone) return; try { return fn.apply(null, arguments); } catch (e) { if (win.console && win.console.error) win.console.error(e); } }; }
        function client() { try { var c = page.client(); return c && !c.final() ? c : null; } catch (e) { return null; } }
        function ownCode() { var c = client(); return c ? c.code : ''; }
        function visible() { return !doc.hidden; }
        function itsWay() {                                // a press while the page is on its way to a game: it does nothing and says why (a stopped navigation leaves the page here: a reload gets it back)
            closeAll();
            notice('opening', TEXT.opening, 'warn', [{ text: TEXT.reload, run: function () { try { win.location.reload(); } catch (e) { /* none */ } return false; } }]);
        }
        function over() { try { return !!page.handing && !!page.handing(); } catch (e) { return false; } }         // the page is on its way to a game (a match for one too): it makes no room and joins none
        function onHome() { return ctl.screen() === 'home'; }
        function others(c) {                               // the names of the other people of the room that the page is in
            var out = [], room = c && c.room;
            if (room) for (var i = 0; i < room.slots.length; i++) if (i !== room.you && (room.slots[i].state === SEAT_PERSON || room.slots[i].state === SEAT_CLIENT)) out.push(room.slots[i].name);
            return out;
        }
        function hostLabel(c) {                            // "Pebble’s" (the leader's name), or "the host’s"
            var room = c && c.room, leader = room && room.slots[room.leader];
            return leader && leader.name ? possessive(leader.name) : 'the host’s';
        }

        // ---- the notices: one toast, over the page (it takes no room), a later one replaces it ----
        var toast = el('div', 'gtoast'), toastText = el('span'), toastBtns = el('div', 'gbtns'), toastTimer = null, toastAsks = false;
        toast.setAttribute('role', 'status'); toast.setAttribute('aria-live', 'polite'); toast.hidden = true; toast.appendChild(toastText); toast.appendChild(toastBtns);
        function rectOf(sel) { var n = doc.querySelector && doc.querySelector(sel), r = n && n.getBoundingClientRect && n.getBoundingClientRect(); return r && r.height ? r : null; }
        function placeToast() {                            // above what is at the bottom: the chat (a phone's Chat view: its field), the strip of views, the Room's START bar; never into the bar at the top
            if (toast.parentNode !== doc.body) doc.body.appendChild(toast);
            var h = win.innerHeight || 0, bar = rectOf('.hbar'), r = rectOf('#gchat') || rectOf('.htabs'), y = null, up;
            if (r) y = rectOf('#hmain') ? r.top : (rectOf('.gc-form') || { top: r.bottom - 6 }).top;
            else if (win.innerWidth <= 700 && (r = rectOf('.go'))) y = r.top;
            if (y === null || toast.hidden) { toast.style.bottom = ''; return; }
            up = h - y + 10;
            if (bar && toast.offsetHeight && h - up - toast.offsetHeight < bar.bottom + 2) up = h - bar.bottom - 2 - toast.offsetHeight;
            toast.style.bottom = Math.round(up) + 'px';
        }
        placeToast();
        var told = el('p', 'sr'); told.setAttribute('role', 'status'); doc.body.appendChild(told);       // (a join with no toast, the card that closes by itself: this line tells a screen reader)
        function armToast(ms) {                            // a toast with a question stays; the others go after 6 s (not under the pointer or the focus)
            if (toastTimer !== null) cancel(toastTimer);
            toastTimer = toast.hidden || toastAsks ? null : later(guard(function () { if (toast.matches && toast.matches(':hover, :focus-within')) armToast(1500); else clearNotice(); }), ms || TOAST_MS);
        }
        function clearNotice(kind) { if (!kind || kind === noticeKind) { toast.hidden = true; noticeKind = ''; armToast(); } }
        // kind: what the notice is (a later one replaces it), tone: '' or 'warn', actions: [{ text, run }]: run says false when it could not do it (the toast stays); OK is always the last button
        function notice(kind, text, tone, actions) {
            noticeKind = kind; toastAsks = !!(actions && actions.length);
            toastText.textContent = text;
            toast.className = 'gtoast' + (tone ? ' ' + tone : '');
            while (toastBtns.firstChild) toastBtns.removeChild(toastBtns.firstChild);
            (actions || []).concat([{ text: TEXT.ok, run: null }]).forEach(function (a) {
                toastBtns.appendChild(button('sm', a.text, guard(function () { if (!a.run || a.run() !== false) clearNotice(); })));
            });
            toast.hidden = false;
            placeToast(); armToast();
        }
        function rejoinOffered() { var s = doc.getElementById('rejoin'); return !!s && !s.hidden; }
        function matchRunning(c) { return !!(c && c.room && c.room.starting) || rejoinOffered(); }    // (a START that waits, or a match of this browser that is still running: the Rejoin strip is up)
        function runningNotice() {
            notice('running', TEXT.running, 'warn', rejoinOffered() ? [{ text: TEXT.rejoin, run: function () { doc.getElementById('rejoin-go').click(); } }] : []);
        }

        // ---- the Public | Private switch of the Room's card (a guest reads it; the leader changes it) ----
        var visBox = el('div', 'vis'), visPrivate = el('button', 'btn vbtn'), visPublic = el('button', 'btn vbtn'), visTag = el('span', 'vistag'), visNote = el('p', 'visnote');
        visBox.setAttribute('role', 'group'); visBox.setAttribute('aria-label', TEXT.visLabel); visBox.hidden = true; visTag.hidden = true; visNote.hidden = true; visNote.setAttribute('role', 'status');
        [visPrivate, visPublic].forEach(function (b) { b.type = 'button'; visBox.appendChild(b); });
        visPrivate.appendChild(el('b', '', TEXT.private)); visPrivate.appendChild(el('small', '', TEXT.privateSub));
        visPublic.appendChild(el('b', '', TEXT.public));
        (function () { var s = el('small', '', TEXT.publicSub); s.appendChild(el('span', 'vwide', TEXT.publicWide)); visPublic.appendChild(s); })();
        visPrivate.addEventListener('click', guard(function () { choose(false); }));
        visPublic.addEventListener('click', guard(function () { choose(true); }));
        function narrow() { try { return win.matchMedia ? win.matchMedia('(max-width: ' + NARROW + 'px)').matches : (win.innerWidth || 1000) <= NARROW; } catch (e) { return false; } }
        function placeVis() {                              // wide: in the heading line (before the code) and its note under it; a phone: under the link's note, before the colors
            var h = doc.getElementById('room-h'), slots = doc.getElementById('slots'), head = h && ((h.parentNode && h.parentNode.parentNode) || h.parentNode);
            if (!head || !slots || !slots.parentNode) return;
            if (narrow()) { slots.parentNode.insertBefore(visBox, slots); slots.parentNode.insertBefore(visNote, slots); }
            else { head.insertBefore(visBox, (head.querySelector && head.querySelector('.code')) || null); (head.parentNode || head).insertBefore(visNote, head.nextSibling || null); }
        }
        (function () { var h = doc.getElementById('room-h'); if (h && h.parentNode) h.parentNode.appendChild(visTag); placeVis(); })();
        function paintVis() {
            var c = client(), room = c && c.room;
            visBox.hidden = visTag.hidden = visNote.hidden = true;
            if (!room || !room.lobby) return;
            if (!c.isLeader()) { visTag.textContent = room.isPublic ? TEXT.publicRoom : TEXT.privateRoom; visTag.hidden = false; return; }
            var can = c.canAct() && !room.starting && !locked;
            visPrivate.setAttribute('aria-pressed', String(!room.isPublic)); visPublic.setAttribute('aria-pressed', String(room.isPublic));
            visPrivate.disabled = visPublic.disabled = !can;
            visBox.hidden = false;
            visNote.textContent = hint || (room.isPublic ? TEXT.noteListed : TEXT.noteHow);
            visNote.hidden = false;
        }
        function lock() { locked = true; if (lockTimer !== null) cancel(lockTimer); lockTimer = later(guard(function () { lockTimer = null; locked = false; hint = ''; paintVis(); }), RETRY_MS); }
        function unlock() { locked = false; if (lockTimer !== null) { cancel(lockTimer); lockTimer = null; } }
        function choose(want) {                            // a press: the request goes to the room; what the switch shows stays what the room said until its next Room message
            var c = client();
            if (!c || !c.room || c.room.isPublic === want || locked) return;
            if (c.setPublic(want)) { hint = ''; if (!want) askedAt = now(); else lastSent = now(); lock(); }
            else { hint = TEXT.noteWait; lock(); }
            paintVis();
        }

        // ---- what the room says (every Room message of the page's client) ----
        function track(c) {
            if (!c) { tracked = null; lastRoom = null; return; }
            if (c === tracked) return;
            tracked = c; lastRoom = c.room || null; lastSent = 0; askedAt = 0; unlock(); hint = '';
            c.on('room', guard(function (room) { if (c === tracked) heard(c, room); }));
            c.on('welcome', guard(function () { if (c === tracked) lastRoom = null; }));            // (a room that was made again is a new room)
            c.on('refused', guard(function () { if (c === tracked) endPending(); }));
        }
        function heard(c, room) {
            var prev = lastRoom;
            lastRoom = room; unlock(); hint = '';
            if (prev && prev.isPublic && !room.isPublic) wentPrivate(c, prev, room);
            else if (room.isPublic) {
                if (!prev || !prev.isPublic) lastSent = now();
                clearNotice('idle'); clearNotice('private');
                if (pending && pending.sent) { endPending(); publicNotice(c); }
            }
            doPending(c);
            paintVis(); armRefresh(); paintPill();
        }
        function publicNotice(c) {                         // "Your room is public now. It shows in Open games; Sage and Pip stay in it."
            var names = others(c);
            notice('public', 'Your room is public now. It shows in ' + TEXT.title + (names.length ? '; ' + rules.listOf(names) + (names.length > 1 ? ' stay' : ' stays') + ' in it.' : '.'), '', []);
        }
        function wentPrivate(c, prev, room) {              // the room was public a Room message ago: the leader asked, another person leads now, or nobody did anything for 30 minutes
            var asked = askedAt && now() - askedAt < ASK_MS;                   // (a Private that the server dropped is not the reason of a room that goes private half an hour later)
            askedAt = 0;
            if (asked) return;
            var changed = Net.diffRooms(prev, room).some(function (e) { return e.type === 'host'; });
            if (changed) notice('private', TEXT.hostChanged, '', []);
            else if (c.isLeader() && !room.starting) notice('idle', TEXT.idle, 'warn', [{ text: TEXT.makePublic, run: function () { var k = client(); return !!k && k.setPublic(true); } }]);
        }

        // ---- the repeat that keeps a public room public: every 10 minutes while the leader looks at the Room and the tab shows ----
        function wantRefresh() {
            var c = client(), room = c && c.room;
            return !!(!gone && room && room.lobby && room.isPublic && !room.starting && c.isLeader() && c.canAct() && visible() && !onHome() && now() - lastInput < ACTIVE_MS);
        }
        function touched() { var was = now() - lastInput >= ACTIVE_MS; lastInput = now(); if (was) armRefresh(); }          // (a leader who comes back to a tab that he left alone starts the repeat again)
        function armRefresh() {
            var c = client();
            if (!wantRefresh()) { if (refreshTimer !== null) { cancel(refreshTimer); refreshTimer = null; } refreshFor = null; return; }
            if (refreshTimer !== null && refreshFor === c) return;
            if (refreshTimer !== null) cancel(refreshTimer);
            if (!lastSent) lastSent = now();
            refreshFor = c;
            refreshTimer = later(guard(fireRefresh), Math.max(0, REFRESH_MS - (now() - lastSent)));
        }
        function fireRefresh() {
            refreshTimer = null; refreshFor = null;
            if (wantRefresh()) {
                if (client().setPublic(true)) lastSent = now();
                else { refreshFor = client(); refreshTimer = later(guard(fireRefresh), RETRY_MS); return; }
            }
            armRefresh();
        }

        // ---- Quick host ----
        function stopPending() { if (pendingTimer !== null) { cancel(pendingTimer); pendingTimer = null; } }
        function endPending() { pending = null; stopPending(); }
        function startPending(forClient) {                     // forClient: the client whose room is to be made public (a room that is left meanwhile is not the one: a later room is never made public by it)
            pending = { forClient: forClient || null, sent: false, tries: 0 };
            stopPending();
            pendingTimer = later(guard(function () {
                pendingTimer = null;
                var was = pending; pending = null;
                if (was && was.sent) notice('public', TEXT.stillPrivate, 'warn', []);
            }), PENDING_MS);
        }
        function doPending(c) {                            // a room of ours that is ready for Visibility(public): send it once, or give the request up
            if (!pending || pending.sent || c !== client() || (pending.forClient && pending.forClient !== c)) return;
            var room = c.room;
            if (!room || !room.lobby) return;
            if (!c.isLeader() || room.starting) { endPending(); return; }
            if (room.isPublic) { endPending(); notice('already', TEXT.already, '', []); return; }
            if (!c.canAct()) return;
            if (c.setPublic(true)) { pending.sent = true; lastSent = now(); lock(); return; }
            if (++pending.tries <= 3) later(guard(function () { var k = client(); if (pending && k === c) doPending(k); }), RETRY_MS);
            else endPending();
        }
        function startHost() {                             // a room of the page's own (My room's machinery), then Visibility(public) when the room is ready
            startPending();
            page.make();
            pending.forClient = client();
            track(client());
            ctl.show('room', 'push');
        }
        function stillJoining() {                          // a press while a Join of the list waits: the list shows it again (with its Cancel), and the toast says why nothing else starts
            openList();
            notice('joining', popJoinText.textContent + TEXT.joinFirst, 'warn', []);
        }
        function quickHost() {
            if (finding) return;
            if (joining) { stillJoining(); return; }
            if (over()) { itsWay(); return; }
            closeAll();
            if (pending) { ctl.show('room', 'push'); return; }          // (a second press: the first one is on its way)
            var c = client();
            if (matchRunning(c)) { runningNotice(); return; }
            if (!c || (c.room && !c.room.lobby)) { startHost(); return; }       // (no room, a refused one, or a room that a game made: a room of our own)
            if (!c.room) { startPending(c); ctl.show('room', 'push'); return; }  // (the page's room is on its way: it gets Visibility(public) when it is ready)
            if (!c.isLeader()) {
                ask({ title: 'Leave ' + hostLabel(c) + ' room and host your own?', line: 'A new room opens for you, with its own map and colors. It goes public.', yes: TEXT.leaveHost }, function () { page.leave(); startHost(); });
                return;
            }
            if (c.room.isPublic) { notice('already', TEXT.already, '', []); ctl.show('room', 'push'); return; }
            var names = others(c);
            function flip() { startPending(c); ctl.show('room', 'push'); doPending(c); }
            if (names.length) ask({ title: 'Make your room public?', line: rules.listOf(names) + (names.length > 1 ? ' are' : ' is') + ' in it. Anybody on the front page will be able to join it.', yes: TEXT.makePublic, stay: TEXT.keepPrivate }, flip);
            else flip();
        }

        // ---- the confirm card (the approved picture): the question, what it is about, and the answer that changes nothing has the focus ----
        function inertOthers(on, except) {
            var kids = host.children || [], i;
            if (on) for (i = 0; i < kids.length; i++) if (except.indexOf(kids[i]) === -1) { kids[i].setAttribute('inert', ''); quiet.push(kids[i]); }
            if (!on) { quiet.forEach(function (k) { k.removeAttribute('inert'); }); quiet = []; }
        }
        function hold(on) { try { if (page.hold) page.hold(on); } catch (e) { /* the page's own */ } }       // (the page behind a card takes no key or tap, whichever screen shows)
        function holdOff(b) {                              // a button that stands where another stood takes no press for a moment (a double press is not an answer)
            b.disabled = true;
            if (cancelTimer !== null) cancel(cancelTimer);
            cancelTimer = later(guard(function () { cancelTimer = null; b.disabled = false; }), CANCEL_AFTER_MS);
        }
        // One card over the page (the approved pictures): o = { mark (dots, check, shadow), title, lines, row, best, after (lines after the row), bar (busy, fill), stack, opener (has the focus again), focus (an act's index), onClose (closed with no answer that runs), timer (ms),
        // acts [{ text, cls, run, back (the focus goes back), before (a line above), wait (takes no press for a moment) }] }
        function card(o) {
            if (dialog) return;
            var shade = el('div', 'gshade'), box = el('div', 'gconfirm'), title = el('h2', 'gctitle', o.title), btns = el('div', 'gcbtns' + (o.stack || (o.acts || []).length === 1 ? ' solo' : '')), d, mark, bar, lines = [], focus = null, i;
            box.setAttribute('role', 'dialog'); box.setAttribute('aria-modal', 'true'); title.id = 'gc-title'; box.setAttribute('aria-labelledby', 'gc-title');
            if (o.mark) {
                mark = el('div', 'gmark ' + o.mark); mark.setAttribute('aria-hidden', 'true');
                if (o.mark === 'dots') for (i = 0; i < 4; i++) mark.appendChild(el('i', 'gdot ' + (i ? 'open' : 'player c0')));
                if (o.mark === 'shadow') mark.appendChild(el('span', 'ant ant-green'));
                box.appendChild(mark);
            }
            box.appendChild(title);
            (o.lines || []).forEach(function (t) { var p = el('p', 'gcline', t); lines.push(p); box.appendChild(p); });
            if (o.row) box.appendChild(rowNode(o.row, null, o.best));
            (o.after || []).forEach(function (t) { box.appendChild(el('p', 'gcline', t)); });
            var first = box.querySelector('.gcline');
            if (first) { first.id = 'gc-line'; box.setAttribute('aria-describedby', 'gc-line'); }       // (the first line is what a screen reader reads after the title)
            if (o.bar) { bar = el('div', 'gbar ' + o.bar); bar.appendChild(el('i')); box.appendChild(bar); }
            d = { shade: shade, opener: o.opener || doc.activeElement, onClose: o.onClose || null, bar: bar, lines: lines, timer: null };
            (o.acts || []).forEach(function (a, n) {
                var b = button(a.cls, a.text, guard(function () { if (a.run && dialog === d) d.onClose = null; closeAsk(!!a.back); if (a.run) a.run(); }));
                if (a.before) { box.appendChild(btns); box.appendChild(el('p', 'gcnote', a.before)); btns = el('div', 'gcbtns' + (o.stack || (o.acts || []).length === 1 ? ' solo' : '')); }
                btns.appendChild(b);
                if (a.wait) holdOff(b);
                if (n === o.focus) focus = b;
            });
            box.appendChild(btns);
            shade.appendChild(box);
            doc.body.appendChild(shade);
            dialog = d;
            inertOthers(true, [shade]); hold(true);
            if (o.timer) d.timer = later(guard(function () { d.timer = null; if (dialog === d) closeAsk(false); }), o.timer);
            box.setAttribute('tabindex', '-1');
            (focus || box).focus();       // (a card whose only button waits has the focus itself: Escape and a screen reader's reading start there)
        }
        function ask(o, yes) {                             // o: { title, line, row, best, yes (the words of the button), stay (the words of the other) }: the answer that changes nothing has the focus
            card({ title: o.title, lines: [o.line], row: o.row, best: o.best, opener: o.opener, acts: [{ text: o.yes, cls: 'gyes', run: yes }, { text: o.stay || TEXT.stay, cls: 'gstay', back: true }], focus: 1 });
        }
        function dropCard() { if (dialog) { dialog.onClose = null; closeAsk(false); } }       // (a card that gives way to another one: nothing that its closing would do is done)
        function closeAsk(back) {
            if (!dialog) return;
            var d = dialog; dialog = null;
            if (d.shade.parentNode) d.shade.parentNode.removeChild(d.shade);
            inertOthers(false, []); hold(false);
            if (back && d.opener && d.opener.focus) d.opener.focus();
            if (d.onClose) d.onClose();
        }

        // ---- the list's popover ----
        var pop = el('section', 'gpop'), popHead = el('div', 'gphead'), popTitle = el('h2', 'gptitle', TEXT.title), popCount = el('span', 'gpcount'), popNote = el('p', 'gpnote');
        var popBody = el('div', 'gpbody'), popList = el('ul', 'glist'), popLoad = el('p', 'gemline', TEXT.loading), popEmpty = el('div', 'gempty'), popOff = el('div', 'goff'), popLegend = el('p', 'glegend');
        var popFilter = el('div', 'gfrow'), filterLabel = el('label', 'gfilter'), filterBox = el('input'), liveBox = el('div', 'glivebox'), liveHead = el('h3', 'gsub', TEXT.inProgress), liveList = el('ul', 'glist glivelist'), liveNone = el('p', 'gemline gnolive', TEXT.noLive);
        var popJoin = el('div', 'gjoining'), popJoinText = el('span'), popJoinStop = button('sm', TEXT.cancel, guard(function () { cancelJoin(); }));
        var codeBox = el('div', 'gcode'), codeLabel = el('label', 'gclabel', TEXT.code), codeRow = el('div', 'joinbox'), codeInput = el('input'), codeGo = button('sm gcgo', TEXT.join), codeHint = el('p', 'hint');
        pop.hidden = !docked; pop.setAttribute('role', docked ? 'region' : 'dialog'); pop.setAttribute('aria-label', TEXT.title); popNote.setAttribute('role', 'status'); codeHint.setAttribute('role', 'status'); popNote.hidden = true;
        popHead.appendChild(popTitle); popHead.appendChild(popCount);
        filterBox.type = 'checkbox'; filterBox.checked = showLive; filterLabel.appendChild(filterBox); filterLabel.appendChild(el('span', '', TEXT.filter)); popFilter.appendChild(filterLabel); popHead.appendChild(popFilter);
        liveBox.hidden = true; popFilter.hidden = false; liveBox.appendChild(liveHead); liveBox.appendChild(liveList); liveBox.appendChild(liveNone);
        popJoin.hidden = true; popJoin.setAttribute('role', 'status'); popJoin.appendChild(popJoinText); popJoin.appendChild(popJoinStop);
        popEmpty.appendChild(el('span', 'ant ant-green')); popEmpty.firstChild.setAttribute('aria-hidden', 'true');
        popEmpty.appendChild(el('p', 'gemtitle', TEXT.emptyTitle)); popEmpty.appendChild(el('p', 'gemline', TEXT.emptyLine)); var emptyRow = el('div', 'gerow'); emptyRow.appendChild(button('ghostbtn', TEXT.quickHost, guard(function () { quickHost(); }))); emptyRow.appendChild(button('ghostbtn', TEXT.quickJoin, guard(function () { quickJoin(); }))); popEmpty.appendChild(emptyRow);
        popOff.appendChild(el('p', 'gemtitle', TEXT.off)); popOff.appendChild(el('p', 'gemline', TEXT.offHint));
        [['player', TEXT.legendPlayer], ['bot', TEXT.legendBot], ['open', TEXT.legendOpen]].forEach(function (k) { var s = el('span', 'gleg'); s.appendChild(el('i', 'gdot ' + k[0])); s.appendChild(doc.createTextNode(k[1])); popLegend.appendChild(s); });
        codeInput.type = 'text'; codeInput.setAttribute('placeholder', TEXT.codePlaceholder); codeInput.setAttribute('aria-label', TEXT.code); codeInput.setAttribute('autocomplete', 'off'); codeInput.setAttribute('autocapitalize', 'off');
        codeInput.setAttribute('spellcheck', 'false'); codeInput.setAttribute('aria-describedby', 'gc-hint'); codeHint.id = 'gc-hint';
        codeRow.appendChild(codeInput); codeRow.appendChild(codeGo); codeBox.appendChild(codeLabel); codeBox.appendChild(codeRow); codeBox.appendChild(codeHint);
        [popList, popLoad, popEmpty, popOff, liveBox].forEach(function (n) { popBody.appendChild(n); });
        [popHead, popJoin, popNote, popBody, popLegend, codeBox].forEach(function (n) { pop.appendChild(n); });
        if (docked) pop.className += ' gdock';
        (env.dock || host).appendChild(pop);
        codeInput.addEventListener('input', guard(function () { codeHint.textContent = ''; }));
        codeInput.addEventListener('keydown', guard(function (e) { if (e && e.key === 'Enter') joinTyped(); }));
        codeGo.addEventListener('click', guard(function () { joinTyped(); }));

        function visibleRows() { var own = ownCode(); return bestOrder(rows.filter(function (r) { return r.code !== own; })); }          // the OTHER rooms, best first (Quick join and BEST)
        function listRows() { var own = ownCode(); return rows.filter(function (r) { return r.code === own; }).concat(visibleRows()); }      // the list: the page's own public room first (tagged Yours), then the others
        function paintPill() {
            var n = listRows().length, shown = loaded && !off;
            parts.pill.hidden = !shown;
            parts.pill.textContent = shown ? countText(n) : '';
            parts.games.setAttribute('aria-label', shown ? 'Games, ' + countText(n) + ' open' : 'Games');
        }
        // One row: the map's picture (an original's, else the library's by file name; one that does not come leaves the square), who hosts, which map, the seats, the bots, and (in the list) Join
        function picOf(row) {                              // the square of a row (a room's or a running match's)
            var pic = el('span', 'gmap');
            var src = row.mapKey ? 'front/preview_' + row.mapKey + '.png' : Picker ? Picker.previewPath(row.map) : '';
            if (src) { var img = el('img'); img.setAttribute('loading', 'lazy'); img.src = src; img.alt = ''; img.setAttribute('width', '300'); img.setAttribute('height', '300'); img.addEventListener('error', function () { pic.removeChild(img); }); pic.appendChild(img); }
            pic.setAttribute('aria-hidden', 'true');
            return pic;
        }
        function rowNode(row, onList, best, yours) {
            var li = el(onList ? 'li' : 'div', 'gitem'), seats = seatsOf(row), pic = picOf(row), text = el('span', 'gtxt'), top = el('span', 'gtop'), line = el('span', 'gline'), dots = el('span', 'gdots');
            dots.setAttribute('aria-hidden', 'true');
            top.appendChild(el('span', 'gname', row.host)); top.appendChild(el('span', 'gmapname', row.mapName));
            seats.dots.forEach(function (kind, i) { dots.appendChild(el('i', 'gdot ' + kind + ' c' + i)); });
            line.appendChild(dots); line.appendChild(el('span', 'gcount', seats.count)); line.appendChild(el('span', 'gbots', seats.bots));
            if (best || yours) top.appendChild(el('span', yours ? 'gyours' : 'gbest', yours ? TEXT.yours : TEXT.best));       // (beside the map's name; a narrow row wraps it under)
            text.appendChild(top); text.appendChild(line); li.appendChild(pic); li.appendChild(text);
            if (onList && !yours) {
                var b = button('gjoin', TEXT.join, guard(function () { joinRow(row); }));
                b.setAttribute('aria-label', 'Join ' + possessive(row.host) + ' room, ' + seats.count + ' seats taken, ' + seats.bots + ', ' + row.mapName); b.setAttribute('data-code', row.code);
                joinButtons.push(b); li.appendChild(b);
            }
            return li;
        }
        // One match that runs now: the map's square, the map (the room's host is not known: the door carries no room), a red Live mark, the players (a dot of their color and their names) and how long it has run; Watch opens the
        // player on it in a NEW tab, so the seat in the room that this page holds stays
        function liveNode(e) {
            var li = el('li', 'gitem glive'), text = el('span', 'gtxt'), top = el('span', 'gtop'), line = el('span', 'gline'), dots = el('span', 'gdots'), who = el('span', 'gwho'), len = el('span', 'glen', clockText(e.seconds) + TEXT.soFar);
            var names = e.who.map(function (w) { return w.name; }).join(', ') || TEXT.nobodyNamed, go = el('a', 'btn gwatch', TEXT.watch);
            dots.setAttribute('aria-hidden', 'true');
            e.who.forEach(function (w) { var d = el('i', 'gdot ' + (/^Bot \(/.test(w.name) ? 'bot' : 'player')); d.className += w.seat < 0 ? ' cn' : ' c' + w.seat; dots.appendChild(d); });          // (a computer player is a ring, as in the open rooms; a color that the door does not know is a neutral dot)
            who.textContent = names; who.setAttribute('title', names);
            top.appendChild(el('span', 'gname', e.mapName)); top.appendChild(el('span', 'glive-mark', TEXT.liveMark));
            line.appendChild(dots); line.appendChild(who); line.appendChild(len);
            text.appendChild(top); text.appendChild(line); li.appendChild(picOf(e)); li.appendChild(text);
            go.setAttribute('href', '/play.html?live=' + encodeURIComponent(e.id)); go.setAttribute('target', '_blank'); go.setAttribute('rel', 'noopener'); go.setAttribute('data-live', e.id);
            go.setAttribute('aria-label', 'Watch the match on ' + e.mapName + ', ' + names + ', ' + clockText(e.seconds) + TEXT.soFar);
            li.appendChild(go);
            liveLens.push({ id: e.id, node: len, link: go, names: names, map: e.mapName });
            return li;
        }
        function liveShown() { return showLive && liveLoaded && !liveOff && loaded; }          // (the section stands only with the switch on, an answer of /live that was good, and the list's own first look done)
        function paintLive() {
            var on = liveShown(), active = doc.activeElement, focusId = active && active.getAttribute ? active.getAttribute('data-live') : null;
            liveBox.hidden = !on;
            popFilter.hidden = showLive && liveLoaded && liveOff;                 // (a server with no live door: nothing to switch)
            filterBox.checked = showLive;
            if (!on) { liveSig = null; liveLens = []; while (liveList.firstChild) liveList.removeChild(liveList.firstChild); return; }          // (nothing of a match stays in the page while the section is away)
            liveNone.hidden = live.length > 0; liveList.hidden = live.length === 0;
            var sig = JSON.stringify(live.map(function (e) { return [e.id, e.map, e.who]; }));
            if (sig !== liveSig) {                                              // (made again only when a match, its map or its players came or went: a focus stays, and the clocks tick in place)
                liveSig = sig; liveLens = [];
                while (liveList.firstChild) liveList.removeChild(liveList.firstChild);
                live.forEach(function (e) { liveList.appendChild(liveNode(e)); });
                if (focusId) { var again = liveList.querySelector('[data-live="' + focusId + '"]'); if (again && again.focus) again.focus(); }
            }
            live.forEach(function (e, i) {
                var l = liveLens[i];
                if (l) { l.node.textContent = clockText(e.seconds) + TEXT.soFar; l.link.setAttribute('aria-label', 'Watch the match on ' + l.map + ', ' + l.names + ', ' + clockText(e.seconds) + TEXT.soFar); }
            });
        }
        function countLine(n) {                            // the list's head: "3 open, 2 in progress" while the matches are shown, else "3 waiting - updates live"
            if (!loaded || off) return '';
            if (liveShown()) return !n && !live.length ? TEXT.none : countText(n) + ' open, ' + countText(live.length) + ' in progress';
            return n === 0 ? TEXT.none : countText(n) + ' waiting' + TEXT.live;
        }
        function paintList() {
            var shown = listRows(), lead = shown.length - visibleRows().length, active = doc.activeElement, focusCode = active && active.getAttribute ? active.getAttribute('data-code') : null;
            popCount.textContent = countLine(shown.length);
            paintLive();
            popLoad.hidden = loaded; popOff.hidden = !(loaded && off); popEmpty.hidden = !(loaded && !off && !shown.length); popList.hidden = !(loaded && !off && shown.length); popLegend.hidden = popList.hidden;
            popNote.hidden = !popNote.textContent;
            var sig = JSON.stringify(shown.map(function (r) { return [r.code, r.host, r.map, r.people, r.bots, r.free, r.size]; }).concat(lead));       // (the rows are made again only when what they show changed: a press that straddles a repaint is not lost, and a focus stays)
            if (sig === paintedSig) return;
            paintedSig = sig;
            while (popList.firstChild) popList.removeChild(popList.firstChild);
            joinButtons = [];
            shown.forEach(function (r, n) { popList.appendChild(rowNode(r, true, n === lead && shown.length - lead > 1, n < lead)); });
            busy(joining, popJoinText.textContent);
            if (focusCode) { var again = popList.querySelector('[data-code="' + focusCode + '"]'); if (again && again.focus) again.focus(); }
        }
        function place() {                                 // under the bar, at the Games button (a phone: the whole width); no taller than what is left of the window
            if (docked) return;
            var bar = parts.games.parentNode, r = parts.games.getBoundingClientRect(), b = (bar && bar.getBoundingClientRect ? bar : parts.games).getBoundingClientRect();
            var top = Math.round(b.bottom + 8), width = win.innerWidth || 1000, wide = width > NARROW, w = wide ? 450 : width - 20;
            var left = wide ? Math.max(10, Math.min(r.left - 4, width - w - 14)) : 10;
            pop.style.top = top + 'px'; pop.style.left = left + 'px'; pop.style.width = w + 'px';
            pop.style.maxHeight = Math.max(160, (win.innerHeight || 600) - top - 12) + 'px';
        }
        function openList() {
            if (isOpen || gone || !onHome()) return;
            isOpen = true; pop.hidden = false; popNote.textContent = ''; codeHint.textContent = '';
            parts.games.setAttribute('aria-expanded', 'true');
            place(); paintList(); poll(); pollLive();
            focusList();
        }
        function focusList() { var first = popList.querySelector('button') || popEmpty.querySelector('button') || codeInput; if (first && first.focus) first.focus(); }
        function closeList(back) {
            if (!isOpen || docked) return;
            isOpen = false; pop.hidden = true;
            parts.games.setAttribute('aria-expanded', 'false');
            if (back && parts.games.focus) parts.games.focus();
            schedule(); scheduleLive();
        }
        function closeAll() { closeAsk(false); closeList(false); }

        // ---- Join: the page's probe (a second client that only joins: the old room goes when the new one welcomes it) and the words of what it says ----
        function needsAsk(c) {                             // somebody is in the room that this page leaves: a guest always (the host is there), the leader when others are
            return !!(c && c.room && c.room.lobby && (!c.isLeader() || others(c).length > 0));
        }
        function askLeave(c, row, go, more) {             // more: { yes (the words of the answer), best (the row is the best), opener (what has the focus again when it is not answered) }
            var names = others(c), leader = c.isLeader();
            ask({
                title: leader ? 'Leave your room?' : 'Leave ' + hostLabel(c) + ' room?',
                line: leader ? (names.length === 1 ? names[0] + ' is in your room. If you go, ' + names[0] + ' becomes the host.' : names.length + ' people are in your room. If you go, the one who has been there longest becomes the host.')
                    : 'You are in ' + hostLabel(c) + ' room. You can come back by its link while there is a free color.',
                row: row, best: more && more.best, yes: (more && more.yes) || TEXT.leaveJoin, opener: more && more.opener
            }, go);
        }
        function busy(on, what) {                          // a Join is on its way (up to 15 s): the buttons wait, and the list says what it waits for, with a way out
            var was = joining;
            if (!on) clearNotice('joining');
            joining = on; joinButtons.forEach(function (b) { b.disabled = on; }); codeGo.disabled = on;
            popJoin.hidden = !on; popJoinText.textContent = on ? what || TEXT.joiningCode : '';
            if (on === was) return;
            if (cancelTimer !== null) { cancel(cancelTimer); cancelTimer = null; }
            popJoinStop.disabled = !!on;
            if (on) cancelTimer = later(guard(function () { cancelTimer = null; if (joining) popJoinStop.disabled = false; }), CANCEL_AFTER_MS);
        }
        function cancelJoin() {                            // the look at the room is given up: its answer, when it comes, is nobody's, and the second client is let go of
            joinSeq++;
            try { if (page.cancel) page.cancel(); } catch (e) { /* the page's own */ }
            busy(false);
            if (isOpen && codeInput.focus) codeInput.focus();
        }
        function refused(res, fromList) {
            var words = res.kind === 'full' ? TEXT.full : res.kind === 'running' ? TEXT.started : res.kind === 'silent' ? TEXT.silent : res.kind === 'version' ? res.text : fromList ? TEXT.gone : res.text;
            if (fromList) { popNote.textContent = words; popNote.hidden = false; } else codeHint.textContent = words;
            poll();
        }
        function joined(what) {                           // (the Room shows: the list is put away by that)
            codeInput.value = ''; codeHint.textContent = ''; popNote.textContent = '';
            notice('joined', 'Joined ' + what + '.', '', []);
            ctl.show('room', 'push');
        }
        function joinRow(row) {
            if (joining || finding) return;
            if (over()) { itsWay(); return; }
            var c = client();
            if (matchRunning(c)) { closeAll(); runningNotice(); return; }
            function go() {
                var mine = ++joinSeq;
                busy(true, 'Joining ' + possessive(row.host) + ' room…'); popNote.textContent = ''; popNote.hidden = true;
                page.join(row.code, page.name(), guard(function (res) { if (mine !== joinSeq) return; busy(false); if (res && res.ok) joined(possessive(row.host) + ' room'); else if (res) refused(res, true); }));
            }
            if (needsAsk(c)) askLeave(c, row, go); else go();
        }
        function joinTyped() {                             // the page checks the code first (a line for one that leads nowhere), then the question, then the name card, then the look at the room
            if (joining || finding) return;
            if (over()) { itsWay(); return; }
            var c = client();
            if (matchRunning(c)) { closeAll(); runningNotice(); return; }
            var mine = ++joinSeq;
            codeHint.textContent = page.typed(codeInput.value, guard(function (res) {
                if (mine !== joinSeq) return;
                busy(false);
                if (res && res.ok) joined('room ' + rules.codeText(res.code)); else if (res) refused(res, false);
            }), function (go) { function start() { busy(true); go(); } if (needsAsk(c)) askLeave(c, null, start); else start(); }) || '';
        }

        // ---- Quick join: the best open room, by one look at the list and the page's probe (the old room goes when the new one welcomes the page) ----
        function look1(done) {                             // a look for a press (one of less than a second ago is as good)
            if (loaded && lastLook && now() - lastLook < FLOOR_MS) { done(); return; }
            lastLook = now();
            if (typeof win.fetch !== 'function') { settle(null); done(); return; }
            look(LIST_URL).then(function (data) { settle(data); done(); });
        }
        function stopFinding() { finding = false; joinSeq++; try { if (page.cancel) page.cancel(); } catch (e) { /* the page's own */ } }
        function lookLine(n) { return 'Looking through ' + n + (n === 1 ? ' open game' : ' open games') + ' for the best one.'; }
        function findingCard(mine, n, opener) {
            card({ mark: 'dots', title: TEXT.finding, lines: [n ? lookLine(n) : TEXT.looking], bar: 'busy', acts: [{ text: TEXT.cancel, cls: 'gstay', back: true, wait: true }], opener: opener,
                onClose: function () { if (mine === joinSeq) stopFinding(); } });
        }
        function nobody(off, opener) {                             // nothing to join, or every room was filled meanwhile: Host one! is Quick host
            var c = client(), mine = c && c.room && c.room.lobby;
            finding = false;
            card({ mark: 'shadow', stack: true, title: off ? TEXT.off : TEXT.emptyTitle, lines: [TEXT.noneLine], acts: [{ text: TEXT.hostOne, cls: 'gyes big', run: quickHost }, { text: TEXT.notNow, cls: 'gstay', back: true, before: mine ? TEXT.noneYours : TEXT.noneNew }], focus: 0, opener: opener });
        }
        function welcomed(row) {                           // the page is in the room: the card says so, and the Room opens by itself or at a press
            finding = false;
            var seen = { code: row.code, host: row.host, map: row.map, mapKey: row.mapKey, mapName: row.mapName, people: row.people + 1, bots: row.bots, free: row.free - 1, size: row.size };
            dropCard(); told.textContent = 'Joined ' + possessive(row.host) + ' room.';
            card({ mark: 'check', title: 'Joined ' + possessive(row.host) + ' room', row: seen, after: [TEXT.roomOpening], bar: 'fill', acts: [{ text: TEXT.goRoom, cls: 'gyes' }], focus: 0, timer: JOINED_MS, onClose: function () { if (!gone) ctl.show('room', 'push'); } });
        }
        function tries(cands, mine) {                      // up to three rooms, one after the other (a room that is gone, full or started is a reason for the next)
            var n = Math.min(TRIES_MAX, cands.length), k = 0;
            function next() {
                if (mine !== joinSeq) return;
                if (k >= n) { var back = dialog && dialog.opener; dropCard(); nobody(false, back); return; }
                var row = cands[k++];
                if (dialog && dialog.bar) dialog.bar.firstChild.style.width = Math.round(100 * k / (n + 1)) + '%';
                page.join(row.code, page.name(), guard(function (res) {
                    if (mine !== joinSeq) return;
                    if (res && res.ok) { welcomed(row); return; }
                    if (res && (res.kind === 'version' || res.kind === 'silent')) {       // (every room would say the same; a newer version has been said by the page, with its reload)
                        var words = res.kind === 'silent' ? TEXT.silent : '';
                        finding = false; dropCard();
                        if (words) notice('silent', words, 'warn', []);
                        return;
                    }
                    later(guard(next), Math.round(random() * TRY_GAP_MS));
                }));
            }
            next();
        }
        function quickJoin() {
            if (finding) return;
            if (joining) { stillJoining(); return; }
            if (over()) { itsWay(); return; }
            var c = client();
            if (matchRunning(c)) { closeAll(); runningNotice(); return; }
            var opener = parts.quickJoin, mine;
            finding = true; mine = ++joinSeq; told.textContent = '';
            findingCard(mine, 0, opener);
            look1(guard(function () {
                if (mine !== joinSeq) return;
                var cands = visibleRows(), c2 = client();
                if (!cands.length) { dropCard(); nobody(off, opener); return; }
                function go() { findingCard(mine, cands.length, opener); tries(cands, mine); }
                if (needsAsk(c2)) {                      // (the question is about the best room)
                    dropCard();
                    askLeave(c2, cands[0], go, { best: true, opener: opener });
                    dialog.onClose = function () { if (mine === joinSeq) stopFinding(); };
                } else { dialog.lines[0].textContent = lookLine(cands.length); tries(cands, mine); }
            }));
        }
        function leaving(go, opener) {                     // Leave room: a room with other people in it is asked about first (alone: at once); true says that it asked
            var c = client();
            if (gone || !c || !c.room || !c.room.lobby || !others(c).length) return false;
            askLeave(c, null, go, { yes: TEXT.leaveRoom, opener: opener });
            return true;
        }

        // ---- the looks at GET /lobbies ----
        function wanted() { return !gone && visible() && onHome(); }
        function schedule() {
            if (pollTimer !== null) { cancel(pollTimer); pollTimer = null; }
            if (!wanted() || polling) return;
            pollTimer = later(guard(function () { pollTimer = null; poll(); }), delayOf(isOpen, failures, random()));
        }
        function look(url) {                               // one request: a promise of the parsed JSON, or of null for anything that is not a good answer (it never fails)
            return new Promise(function (resolve) {
                var finished = false, ctrl = null, timer = null;
                function finish(v) { if (finished) return; finished = true; if (timer !== null) cancel(timer); resolve(v); }
                try { ctrl = win.AbortController ? new win.AbortController() : null; } catch (e) { ctrl = null; }
                timer = later(function () { finish(null); try { if (ctrl) ctrl.abort(); } catch (e) { /* it was over */ } }, FETCH_MS);
                try {
                    win.fetch(url, { cache: 'no-store', credentials: 'omit', signal: ctrl ? ctrl.signal : undefined }).then(function (res) {
                        if (!res || res.status !== 200) { finish(null); return null; }
                        var length = res.headers && res.headers.get ? Number(res.headers.get('content-length')) : NaN;
                        if (length > BODY_MAX) { finish(null); try { if (ctrl) ctrl.abort(); } catch (e) { /* it was over */ } return null; }          // (an answer that says that it is huge is not read)
                        return res.text().then(function (text) {
                            var data = null;
                            try { data = typeof text === 'string' && text.length <= BODY_MAX ? JSON.parse(text) : null; } catch (e) { data = null; }
                            finish(data);
                        });
                    }).then(null, function () { finish(null); });
                } catch (e) { finish(null); }
            });
        }
        function poll() {                                  // a look at once (the popover opened, Home or the tab came back, the page started): one at a time, a second apart at least
            if (!wanted() || polling) return;
            var wait = lastLook ? FLOOR_MS - (now() - lastLook) : 0;
            if (floorTimer !== null) { cancel(floorTimer); floorTimer = null; }
            if (wait > 0) { floorTimer = later(guard(function () { floorTimer = null; poll(); }), wait); return; }
            if (pollTimer !== null) { cancel(pollTimer); pollTimer = null; }
            lastLook = now();
            if (typeof win.fetch !== 'function') { settle(null); return; }
            polling = true;
            var mine = ++round;
            look(LIST_URL).then(function (data) { if (mine === round) { polling = false; settle(data); } }, function () { if (mine === round) { polling = false; settle(null); } });
        }
        function settle(data) {
            var list = null;
            try { list = parseList(data, rules); } catch (e) { list = null; }
            loaded = true;
            if (list === null) { off = true; failures++; rows = []; } else { off = false; failures = 0; rows = list; }
            paintPill();
            if (isOpen) paintList();
            schedule();
        }

        // ---- the looks at GET /live: the matches that run now, only while the switch is on and the list shows (Home, a visible tab), 10 s apart at the least ----
        function drawn() { try { return !docked || !pop.getClientRects || pop.getClientRects().length > 0; } catch (e) { return true; } }          // (a phone's Chat or Players view covers the docked list: nothing shows the matches, so nothing asks)
        function liveWanted() { return showLive && !gone && visible() && onHome() && isOpen && drawn(); }
        function scheduleLive(ms) {
            if (liveTimer !== null) { cancel(liveTimer); liveTimer = null; }
            if (!liveWanted() || liveBusy) return;
            liveTimer = later(guard(function () { liveTimer = null; pollLive(); }), ms === undefined ? liveDelay(liveFails, random()) : ms);
        }
        function pollLive() {                              // a look at once when 10 s have gone since the last one ended (after failures: when their wait has gone), else when they have
            if (!liveWanted() || liveBusy) return;
            var wait = lastLive ? (liveFails ? liveDelay(liveFails, 0) : LIVE_EVERY) - (now() - lastLive) : 0;
            if (wait > 0) { if (liveTimer === null) scheduleLive(wait); return; }
            if (liveTimer !== null) { cancel(liveTimer); liveTimer = null; }
            if (typeof win.fetch !== 'function') { settleLive(null); return; }
            liveBusy = true;
            var mine = ++liveRound;
            look(LIVE_URL).then(function (data) { if (mine === liveRound) { liveBusy = false; settleLive(data); } }, function () { if (mine === liveRound) { liveBusy = false; settleLive(null); } });
        }
        function settleLive(data) {
            var list = null;
            try { list = parseLive(data, rules); } catch (e) { list = null; }
            lastLive = now();                              // (the wait before the next look is counted from the end of this one: while one is out, only liveBusy holds the next back)
            liveLoaded = true;
            if (list === null) { liveOff = true; liveFails++; live = []; } else { liveOff = false; liveFails = 0; live = list; }
            if (isOpen) paintList();
            scheduleLive();
        }
        function chooseLive(on) {                          // the switch: the choice is kept in this browser; off stops the looks at /live and puts the section away, on looks again
            if (on === showLive) return;
            showLive = on; keepChoice(on);
            if (!on) { liveRound++; liveBusy = false; live = []; liveLoaded = false; liveOff = false; liveFails = 0; if (liveTimer !== null) { cancel(liveTimer); liveTimer = null; } }
            if (isOpen) paintList();
            if (on) pollLive();
        }
        filterBox.addEventListener('change', guard(function () { chooseLive(!!filterBox.checked); }));

        // ---- the page's own events ----
        function enable(b) { b.disabled = false; b.removeAttribute('aria-disabled'); }
        if (!docked) { parts.games.setAttribute('aria-haspopup', 'dialog'); parts.games.setAttribute('aria-expanded', 'false'); }
        parts.pill.hidden = true;
        parts.games.addEventListener('click', guard(function () { if (docked) focusList(); else if (isOpen) closeList(true); else openList(); }));
        parts.quickHost.addEventListener('click', guard(quickHost));
        parts.quickJoin.addEventListener('click', guard(quickJoin));
        enable(parts.games); enable(parts.quickHost); enable(parts.quickJoin);
        doc.addEventListener('click', guard(function (e) {         // a press anywhere else puts the list away (seen in the capture phase: before the buttons of a card or a Join take themselves out of the page)
            var t = e && e.target;
            if (!isOpen || dialog || joining || !t) return;
            if ((pop.contains && pop.contains(t)) || (parts.games.contains && parts.games.contains(t))) return;
            closeList(false);
        }), true);
        ['pointerdown', 'keydown', 'touchstart'].forEach(function (t) { doc.addEventListener(t, guard(touched), true); });
        doc.addEventListener('keydown', guard(function (e) {
            if (!e || e.key !== 'Escape') return;
            if (!dialog && toast.contains(doc.activeElement)) { clearNotice(); if (lastFocus && lastFocus.focus) lastFocus.focus(); }       // (Escape puts a toast away that has the focus, and the focus goes back)
            if (dialog) closeAsk(true); else if (isOpen) closeList(true);
        }));
        doc.addEventListener('visibilitychange', guard(function () {
            if (!visible() && pollTimer !== null) { cancel(pollTimer); pollTimer = null; }
            if (!visible() && liveTimer !== null) { cancel(liveTimer); liveTimer = null; }
            poll(); schedule(); armRefresh(); pollLive();
        }));
        win.addEventListener('resize', guard(function () { if (isOpen) place(); placeVis(); placeToast(); }));
        var toastWatch = win.MutationObserver ? new win.MutationObserver(guard(function () { if (!toast.hidden) placeToast(); pollLive(); })) : null, lastFocus = null;
        if (toastWatch) toastWatch.observe(doc.body, { attributes: true, subtree: true, attributeFilter: ['data-tab', 'data-open', 'data-screen', 'hidden', 'class'] });       // (the list is drawn or covered again: the looks at /live follow; a phone's view, the chat's fold or one of the page's strips changes: the toast is placed again)
        doc.addEventListener('focusin', guard(function (e) { if (e && e.target && !toast.contains(e.target)) lastFocus = e.target; }));       // (where the focus was before it went to the toast)

        function sync() { track(client()); paintVis(); armRefresh(); paintPill(); if (isOpen) { paintList(); place(); } }
        function shown(name) {
            if (name === 'room') { closeAll(); placeVis(); scheduleLive(); } else { poll(); pollLive(); }
            sync();
            schedule();
        }
        function opening() {                               // START reached the page: nothing here takes a press or a timer or looks at the list any more
            gone = true; closeAll(); clearNotice();
            if (toastWatch) toastWatch.disconnect();
            [pollTimer, refreshTimer, pendingTimer, lockTimer, floorTimer, cancelTimer, liveTimer].forEach(function (t) { if (t !== null) cancel(t); });
            pollTimer = refreshTimer = pendingTimer = lockTimer = floorTimer = cancelTimer = liveTimer = null; liveRound++;
        }
        function stalled(words) {                          // the hand-off did not come in 15 s (the page's room is still frozen): the buttons that need it say why they do not work
            [parts.quickHost, parts.quickJoin, parts.games].forEach(function (b) { b.disabled = true; b.setAttribute('aria-disabled', 'true'); b.setAttribute('title', words); });
            hint = words; paintVis();
        }
        function inert(on) { [toast, pop].forEach(function (n) { if (on) n.setAttribute('inert', ''); else n.removeAttribute('inert'); }); }      // (a dialog of the page, or the hand-off, is over this part: it takes no key or tap)
        sync();
        poll();
        pollLive();
        return {
            sync: guard(sync), shown: guard(shown), leaving: leaving, opening: opening, stalled: stalled, inert: inert, poll: poll,
            state: function () { return { rows: rows, loaded: loaded, off: off, failures: failures, open: isOpen, pending: !!pending, joining: joining, notice: noticeKind, live: live, liveOff: liveOff, liveLoaded: liveLoaded, showLive: showLive }; }
        };
    }

    return { TEXT: TEXT, rowOf: rowOf, parseList: parseList, liveOf: liveOf, parseLive: parseLive, liveDelay: liveDelay, clockText: clockText, LIVE_EVERY: LIVE_EVERY, delayOf: delayOf, countText: countText, bestOrder: bestOrder, seatsOf: seatsOf, mount: mount, EVERY_CLOSED: EVERY_CLOSED, EVERY_OPEN: EVERY_OPEN, REFRESH_MS: REFRESH_MS, ROWS_MAX: ROWS_MAX };
});
