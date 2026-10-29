// The public Room and the Games list (docs/HOME_SCREEN.md, docs/PUBLIC_ROOMS.md): the Public | Private switch of the Room's card, Quick host, and the Games popover under the Home bar (the list of GET /lobbies, Join,
// Join by code). Loaded by web/home/home.js after the first paint, with games.css; until it is there the buttons that it lights stay disabled. ES5, no module system: a page gets AntsHomeGames, node gets module.exports
// (tests/scripts/web_home_games_check.js). The client, the model and the session of the page are the page's own (web/lobby.html): this script reads them through `page` and sends what the leader may send.
//   * The room's state is the server's word: the switch is drawn from the last Room message (never from what a press asked), a notice follows a Room message, and a request that the page's pace holds back sends nothing.
//   * Everything that comes from the list is UNTRUSTED text (a host's name may hold < > " \ and have 32 characters; a map is a file name): it is checked field by field (parseList), drawn with textContent only,
//     never into markup, never into a link or an address, and a row that is not exactly of the shape is dropped. At most 50 rows.
//   * The list is polled: 30 s with the popover closed, 5 s while it is open and at once when it opens, never while the tab is hidden or the Room shows, +-20 % of jitter, and the wait doubles after a failure.
//     Nothing that goes wrong here reaches Home, the Room or the hand-off to the game: every call out of this file is guarded and a failed look is only "the list is off".
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
    var ACTIVE_MS = 1200000;                             // a leader who has touched nothing for this long (no pointer, key or touch) is not at the Room: the repeat stops, and the server's idle rule goes on
    var ASK_MS = 10000;                                  // a Private that was asked for is told from a room that went private by itself for this long
    var RETRY_MS = 1300;                                 // a request that the page's pace held back is made again after this long (the pace is one every 1.25 s)
    var PENDING_MS = 20000;                              // Quick host waits this long for a room that is not ready (then it is forgotten: a room must not turn public much later)
    var NARROW = 720;                                    // px: the page's phone layout (web/lobby.html)
    var SEAT_PERSON = 1, SEAT_CLIENT = 2;                // the Room message's states of a color that a person holds (lobby_net.js SLOT)
    var TEXT = {
        title: 'Open games', loading: 'Looking for games…', off: 'Games list is off right now', offHint: 'Join by code still works.', none: 'none waiting',
        emptyTitle: 'No open games right now.', emptyLine: 'Rooms show up here when their host switches them to Public.', hostOne: 'Host one!',
        legendPlayer: 'player', legendBot: 'bot', legendOpen: 'open seat', code: 'Join by code', codePlaceholder: 'Room code, like k7m 2xq', join: 'Join',
        private: 'Private', privateSub: 'link only', public: 'Public', publicSub: 'listed', publicWide: ' in Open games', publicRoom: 'Public room', privateRoom: 'Private room', visLabel: 'Who can find this room',
        noteHow: 'Public rooms show your name and map in the Games list.', noteListed: 'Listed. Anyone on the front page can join from Open games. The link works too.', noteWait: 'One moment, then press again.',
        ok: 'OK', makePublic: 'Make it public', rejoin: 'Rejoin', stay: 'Stay here', keepPrivate: 'Keep it private', leaveJoin: 'Leave and join', leaveHost: 'Leave and host', cancel: 'Cancel', joiningCode: 'Joining the room…',
        already: 'Your room is already public.', running: 'Your match is running.', idle: 'Your room went private after 30 minutes. Make it public again?',
        hostChanged: 'This room is private again because the host changed.', stillPrivate: 'Your room is still private. Use the Public switch in the room.',
        full: 'That room is full.', started: 'The match in that room has already started.', gone: 'That game is not open any more.', silent: 'The server did not answer. Try again in a moment.'
    };

    // ---- the pure parts -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
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

    // ---- the screens ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // mount({ doc, win, host (Home's div), page (web/lobby.html's bridge: client(), make(), leave(), join(code, name, done), typed(text, done, before), name()), parts: { quickHost, games, pill } (Home's buttons),
    //         ctl: { show(screen, how), screen() }, handoff (START is on its way already: the part mounts waiting), later, cancel, random, now (the clocks: tests give their own) }) returns { sync, shown, opening, stalled, inert, poll, state }
    function mount(env) {
        var doc = env.doc, win = env.win, host = env.host, page = env.page, parts = env.parts, ctl = env.ctl;
        var rules = win.AntsLobbyRules, Net = win.AntsLobbyNet;
        var later = env.later || function (fn, ms) { return setTimeout(fn, ms); }, cancel = env.cancel || function (id) { clearTimeout(id); };
        var random = env.random || Math.random, now = env.now || function () { return Date.now(); };

        var rows = [], loaded = false, off = false, failures = 0, isOpen = false, polling = false, round = 0, pollTimer = null, gone = !!env.handoff, joining = false, joinButtons = [];
        var tracked = null, lastRoom = null, lastSent = 0, refreshTimer = null, refreshFor = null, pending = null, pendingTimer = null, askedAt = 0, locked = false, lockTimer = null, hint = '', lastInput = now(), lastLook = 0, floorTimer = null, joinSeq = 0, paintedSig = null;
        var dialog = null, noticeKind = '', quiet = [];

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

        // ---- the notices: one strip with the page's own look, above both screens ------------------------------------------------------------------------------------------------------------------------
        var strip = el('div', 'banner gstrip'), stripText = el('span'), stripBtns = el('div', 'gbtns');
        strip.setAttribute('role', 'status'); strip.hidden = true; strip.appendChild(stripText); strip.appendChild(stripBtns);
        function placeStrip() {                            // Home: with the page's own strips above the bar; the Room: under its slim bar, as drawn
            var banner = doc.getElementById('banner'), lobby = doc.getElementById('lobby');
            if (ctl.screen() === 'room' && lobby && lobby.parentNode) lobby.parentNode.insertBefore(strip, lobby);
            else banner.parentNode.insertBefore(strip, banner.nextSibling || null);
        }
        placeStrip();
        function clearNotice(kind) { if (!kind || kind === noticeKind) { strip.hidden = true; noticeKind = ''; } }
        // kind: what the notice is (a later one replaces it), tone: '' or 'warn', actions: [{ text, run }]: run says false when it could not do it (the notice stays); OK is always the last button
        function notice(kind, text, tone, actions) {
            noticeKind = kind;
            stripText.textContent = text;
            strip.className = 'banner gstrip' + (tone ? ' ' + tone : '');
            while (stripBtns.firstChild) stripBtns.removeChild(stripBtns.firstChild);
            (actions || []).concat([{ text: TEXT.ok, run: null }]).forEach(function (a) {
                stripBtns.appendChild(button('sm', a.text, guard(function () { if (!a.run || a.run() !== false) clearNotice(); })));
            });
            strip.hidden = false;
        }
        function rejoinOffered() { var s = doc.getElementById('rejoin'); return !!s && !s.hidden; }
        function matchRunning(c) { return !!(c && c.room && c.room.starting); }
        function runningNotice() {
            notice('running', TEXT.running, 'warn', rejoinOffered() ? [{ text: TEXT.rejoin, run: function () { doc.getElementById('rejoin-go').click(); } }] : []);
        }

        // ---- the Public | Private switch of the Room's card (a guest reads the state; the leader changes it) ---------------------------------------------------------------------------------------------
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

        // ---- what the room says (every Room message of the page's client) -----------------------------------------------------------------------------------------------------------------------------------
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

        // ---- the repeat that keeps a public room public: every 10 minutes, while the leader looks at the Room and the tab is visible ------------------------------------------------------------------------------
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

        // ---- Quick host -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
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
        function quickHost() {
            if (joining || over()) return;
            closeAll();
            if (pending) { ctl.show('room', 'push'); return; }          // (a second press: the first one is on its way)
            var c = client();
            if (matchRunning(c) || rejoinOffered()) { runningNotice(); return; }
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

        // ---- the confirm card (the approved picture): the question, what it is about, and the answer that changes nothing has the focus ----------------------------------------------------------------------------
        function inertOthers(on, except) {
            var kids = host.children || [], i;
            if (on) for (i = 0; i < kids.length; i++) if (except.indexOf(kids[i]) === -1) { kids[i].setAttribute('inert', ''); quiet.push(kids[i]); }
            if (on) { strip.setAttribute('inert', ''); quiet.push(strip); }               // (the notice is outside Home's div, and just as much behind the question)
            if (!on) { quiet.forEach(function (k) { k.removeAttribute('inert'); }); quiet = []; }
        }
        function ask(o, yes) {                             // o: { title, line, row (a row of the list to show), yes (the words of the button), stay (the words of the other) }
            if (dialog) return;
            var shade = el('div', 'gshade'), box = el('div', 'gconfirm'), title = el('h2', 'gctitle', o.title), line = el('p', 'gcline', o.line), actions = el('div', 'gcbtns');
            var go = button('gyes', o.yes, guard(function () { closeAsk(false); yes(); })), stay = button('gstay', o.stay || TEXT.stay, guard(function () { closeAsk(true); }));
            box.setAttribute('role', 'dialog'); box.setAttribute('aria-modal', 'true'); title.id = 'gc-title'; box.setAttribute('aria-labelledby', 'gc-title');
            box.appendChild(title); box.appendChild(line);
            if (o.row) box.appendChild(rowNode(o.row, null));
            actions.appendChild(go); actions.appendChild(stay); box.appendChild(actions);
            shade.appendChild(box);
            host.appendChild(shade);
            dialog = { shade: shade, opener: doc.activeElement };
            inertOthers(true, [shade]);
            if (stay.focus) stay.focus();
        }
        function closeAsk(back) {
            if (!dialog) return;
            var d = dialog; dialog = null;
            if (d.shade.parentNode) d.shade.parentNode.removeChild(d.shade);
            inertOthers(false, []);
            if (back && d.opener && d.opener.focus) d.opener.focus();
        }

        // ---- the list's popover -------------------------------------------------------------------------------------------------------------------------------------------------------------------
        var pop = el('section', 'gpop'), popHead = el('div', 'gphead'), popTitle = el('h2', 'gptitle', TEXT.title), popCount = el('span', 'gpcount'), popNote = el('p', 'gpnote');
        var popBody = el('div', 'gpbody'), popList = el('ul', 'glist'), popLoad = el('p', 'gemline', TEXT.loading), popEmpty = el('div', 'gempty'), popOff = el('div', 'goff'), popLegend = el('p', 'glegend');
        var popJoin = el('div', 'gjoining'), popJoinText = el('span'), popJoinStop = button('sm', TEXT.cancel, guard(function () { cancelJoin(); }));
        var codeBox = el('div', 'gcode'), codeLabel = el('label', 'gclabel', TEXT.code), codeRow = el('div', 'joinbox'), codeInput = el('input'), codeGo = button('sm gcgo', TEXT.join), codeHint = el('p', 'hint');
        pop.hidden = true; pop.setAttribute('role', 'dialog'); pop.setAttribute('aria-label', TEXT.title); popNote.setAttribute('role', 'status'); codeHint.setAttribute('role', 'status'); popNote.hidden = true;
        popHead.appendChild(popTitle); popHead.appendChild(popCount);
        popJoin.hidden = true; popJoin.setAttribute('role', 'status'); popJoin.appendChild(popJoinText); popJoin.appendChild(popJoinStop);
        popEmpty.appendChild(el('span', 'ant ant-green')); popEmpty.firstChild.setAttribute('aria-hidden', 'true');
        popEmpty.appendChild(el('p', 'gemtitle', TEXT.emptyTitle)); popEmpty.appendChild(el('p', 'gemline', TEXT.emptyLine)); popEmpty.appendChild(button('ghostbtn', TEXT.hostOne, guard(function () { quickHost(); })));
        popOff.appendChild(el('p', 'gemtitle', TEXT.off)); popOff.appendChild(el('p', 'gemline', TEXT.offHint));
        [['player', TEXT.legendPlayer], ['bot', TEXT.legendBot], ['open', TEXT.legendOpen]].forEach(function (k) { var s = el('span', 'gleg'); s.appendChild(el('i', 'gdot ' + k[0])); s.appendChild(doc.createTextNode(k[1])); popLegend.appendChild(s); });
        codeInput.type = 'text'; codeInput.setAttribute('placeholder', TEXT.codePlaceholder); codeInput.setAttribute('aria-label', TEXT.code); codeInput.setAttribute('autocomplete', 'off'); codeInput.setAttribute('autocapitalize', 'off');
        codeInput.setAttribute('spellcheck', 'false'); codeInput.setAttribute('aria-describedby', 'gc-hint'); codeHint.id = 'gc-hint';
        codeRow.appendChild(codeInput); codeRow.appendChild(codeGo); codeBox.appendChild(codeLabel); codeBox.appendChild(codeRow); codeBox.appendChild(codeHint);
        [popList, popLoad, popEmpty, popOff].forEach(function (n) { popBody.appendChild(n); });
        [popHead, popJoin, popNote, popBody, popLegend, codeBox].forEach(function (n) { pop.appendChild(n); });
        host.appendChild(pop);
        codeInput.addEventListener('input', guard(function () { codeHint.textContent = ''; }));
        codeInput.addEventListener('keydown', guard(function (e) { if (e && e.key === 'Enter') joinTyped(); }));
        codeGo.addEventListener('click', guard(function () { joinTyped(); }));

        function visibleRows() { var own = ownCode(); return rows.filter(function (r) { return r.code !== own; }); }
        function paintPill() {
            var n = visibleRows().length, shown = loaded && !off;
            parts.pill.hidden = !shown;
            parts.pill.textContent = shown ? countText(n) : '';
            parts.games.setAttribute('aria-label', shown ? 'Games, ' + countText(n) + ' open' : 'Games');
        }
        // One row: the map's picture (an original's own), who hosts, which map, the seats as dots and as n/size, the bots, and (in the list) the Join button
        function rowNode(row, onList) {
            var li = el(onList ? 'li' : 'div', 'gitem'), seats = seatsOf(row), pic = el('span', 'gmap'), text = el('span', 'gtxt'), top = el('span', 'gtop'), line = el('span', 'gline'), dots = el('span', 'gdots');
            if (row.mapKey) { var img = el('img'); img.src = 'front/preview_' + row.mapKey + '.png'; img.alt = ''; img.setAttribute('width', '300'); img.setAttribute('height', '300'); pic.appendChild(img); }
            pic.setAttribute('aria-hidden', 'true'); dots.setAttribute('aria-hidden', 'true');
            top.appendChild(el('span', 'gname', row.host)); top.appendChild(el('span', 'gmapname', row.mapName));
            seats.dots.forEach(function (kind, i) { dots.appendChild(el('i', 'gdot ' + kind + ' c' + i)); });
            line.appendChild(dots); line.appendChild(el('span', 'gcount', seats.count)); line.appendChild(el('span', 'gbots', seats.bots));
            text.appendChild(top); text.appendChild(line); li.appendChild(pic); li.appendChild(text);
            if (onList) {
                var b = button('gjoin', TEXT.join, guard(function () { joinRow(row); }));
                b.setAttribute('aria-label', 'Join ' + possessive(row.host) + ' room, ' + seats.count + ' seats taken, ' + seats.bots + ', ' + row.mapName); b.setAttribute('data-code', row.code);
                joinButtons.push(b); li.appendChild(b);
            }
            return li;
        }
        function paintList() {
            var shown = visibleRows(), active = doc.activeElement, focusCode = active && active.getAttribute ? active.getAttribute('data-code') : null;
            popCount.textContent = !loaded || off ? '' : shown.length === 0 ? TEXT.none : countText(shown.length) + ' waiting';
            popLoad.hidden = loaded; popOff.hidden = !(loaded && off); popEmpty.hidden = !(loaded && !off && !shown.length); popList.hidden = !(loaded && !off && shown.length); popLegend.hidden = popList.hidden;
            popNote.hidden = !popNote.textContent;
            var sig = JSON.stringify(shown.map(function (r) { return [r.code, r.host, r.map, r.people, r.bots, r.free, r.size]; }));       // (the rows are made again only when what they show changed: a press that straddles a repaint is not lost, and a focus stays)
            if (sig === paintedSig) return;
            paintedSig = sig;
            while (popList.firstChild) popList.removeChild(popList.firstChild);
            joinButtons = [];
            shown.forEach(function (r) { popList.appendChild(rowNode(r, true)); });
            busy(joining, popJoinText.textContent);
            if (focusCode) { var again = popList.querySelector('[data-code="' + focusCode + '"]'); if (again && again.focus) again.focus(); }
        }
        function place() {                                 // under the bar, at the Games button (a phone: the whole width); no taller than what is left of the window
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
            place(); paintList(); poll();
            var first = popList.querySelector('button') || popEmpty.querySelector('button') || codeInput;
            if (first && first.focus) first.focus();
        }
        function closeList(back) {
            if (!isOpen) return;
            isOpen = false; pop.hidden = true;
            parts.games.setAttribute('aria-expanded', 'false');
            if (back && parts.games.focus) parts.games.focus();
            schedule();
        }
        function closeAll() { closeAsk(false); closeList(false); }

        // ---- Join: the page's probe (a second client that only joins: the old room is let go of when the new one welcomes it) and the words of what it says -------------------------------------------------------------
        function needsAsk(c) {                             // somebody is in the room that this page leaves: a guest always (the host is there), the leader when others are
            return !!(c && c.room && c.room.lobby && (!c.isLeader() || others(c).length > 0));
        }
        function askLeave(c, row, go) {
            var names = others(c), leader = c.isLeader();
            ask({
                title: leader ? 'Leave your room?' : 'Leave ' + hostLabel(c) + ' room?',
                line: leader ? (names.length === 1 ? names[0] + ' is in your room. If you go, ' + names[0] + ' becomes the host.' : names.length + ' people are in your room. If you go, the one who has been there longest becomes the host.')
                    : 'You are in ' + hostLabel(c) + ' room. You can come back by its link while there is a free color.',
                row: row, yes: TEXT.leaveJoin
            }, go);
        }
        function busy(on, what) {                          // a Join is on its way (up to 15 s): the buttons wait, and the list says what it waits for, with a way out
            joining = on; joinButtons.forEach(function (b) { b.disabled = on; }); codeGo.disabled = on;
            popJoin.hidden = !on; popJoinText.textContent = on ? what || TEXT.joiningCode : '';
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
            if (joining || over()) return;
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
            if (joining || over()) return;
            var c = client();
            if (matchRunning(c)) { closeAll(); runningNotice(); return; }
            var mine = ++joinSeq;
            codeHint.textContent = page.typed(codeInput.value, guard(function (res) {
                if (mine !== joinSeq) return;
                busy(false);
                if (res && res.ok) joined('room ' + rules.codeText(res.code)); else if (res) refused(res, false);
            }), function (go) { function start() { busy(true); go(); } if (needsAsk(c)) askLeave(c, null, start); else start(); }) || '';
        }

        // ---- the looks at GET /lobbies ------------------------------------------------------------------------------------------------------------------------------------------------------------
        function wanted() { return !gone && visible() && onHome(); }
        function schedule() {
            if (pollTimer !== null) { cancel(pollTimer); pollTimer = null; }
            if (!wanted() || polling) return;
            pollTimer = later(guard(function () { pollTimer = null; poll(); }), delayOf(isOpen, failures, random()));
        }
        function look() {                                  // one request: a promise of the parsed JSON, or of null for anything that is not a good answer (it never fails)
            return new Promise(function (resolve) {
                var finished = false, ctrl = null, timer = null;
                function finish(v) { if (finished) return; finished = true; if (timer !== null) cancel(timer); resolve(v); }
                try { ctrl = win.AbortController ? new win.AbortController() : null; } catch (e) { ctrl = null; }
                timer = later(function () { finish(null); try { if (ctrl) ctrl.abort(); } catch (e) { /* it was over */ } }, FETCH_MS);
                try {
                    win.fetch(LIST_URL, { cache: 'no-store', credentials: 'omit', signal: ctrl ? ctrl.signal : undefined }).then(function (res) {
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
        function poll() {                                  // a look at once (the popover opened, Home came back, the tab came back, the page started): one at a time, a second apart at least, never while the tab is hidden or the Room shows
            if (!wanted() || polling) return;
            var wait = lastLook ? FLOOR_MS - (now() - lastLook) : 0;
            if (floorTimer !== null) { cancel(floorTimer); floorTimer = null; }
            if (wait > 0) { floorTimer = later(guard(function () { floorTimer = null; poll(); }), wait); return; }
            if (pollTimer !== null) { cancel(pollTimer); pollTimer = null; }
            lastLook = now();
            if (typeof win.fetch !== 'function') { settle(null); return; }
            polling = true;
            var mine = ++round;
            look().then(function (data) { if (mine === round) { polling = false; settle(data); } }, function () { if (mine === round) { polling = false; settle(null); } });
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

        // ---- the page's own events ------------------------------------------------------------------------------------------------------------------------------------------------------------
        function enable(b) { b.disabled = false; b.removeAttribute('aria-disabled'); }
        parts.games.setAttribute('aria-haspopup', 'dialog'); parts.games.setAttribute('aria-expanded', 'false');
        parts.pill.hidden = true;
        parts.games.addEventListener('click', guard(function () { if (isOpen) closeList(true); else openList(); }));
        parts.quickHost.addEventListener('click', guard(quickHost));
        enable(parts.games); enable(parts.quickHost);
        doc.addEventListener('click', guard(function (e) {         // a press anywhere else puts the list away (seen in the capture phase: before the buttons of the question card and of a Join that is on its way change anything, and take themselves out of the page)
            var t = e && e.target;
            if (!isOpen || dialog || joining || !t) return;
            if ((pop.contains && pop.contains(t)) || (parts.games.contains && parts.games.contains(t))) return;
            closeList(false);
        }), true);
        ['pointerdown', 'keydown', 'touchstart'].forEach(function (t) { doc.addEventListener(t, guard(touched), true); });
        doc.addEventListener('keydown', guard(function (e) {
            if (!e || e.key !== 'Escape') return;
            if (dialog) closeAsk(true); else if (isOpen) closeList(true);
        }));
        doc.addEventListener('visibilitychange', guard(function () {
            if (!visible() && pollTimer !== null) { cancel(pollTimer); pollTimer = null; }
            poll(); schedule(); armRefresh();
        }));
        win.addEventListener('resize', guard(function () { if (isOpen) place(); placeVis(); }));

        function sync() { track(client()); paintVis(); armRefresh(); paintPill(); if (isOpen) { paintList(); place(); } }
        function shown(name) {
            placeStrip();
            if (name === 'room') { closeAll(); placeVis(); } else poll();
            sync();
            schedule();
        }
        function opening() {                               // START reached the page: nothing here takes a press or a timer or looks at the list any more (the page is frozen: it is on its way to the game, or it is stuck and says so)
            closeAll(); clearNotice(); gone = true;
            [pollTimer, refreshTimer, pendingTimer, lockTimer, floorTimer].forEach(function (t) { if (t !== null) cancel(t); });
            pollTimer = refreshTimer = pendingTimer = lockTimer = floorTimer = null;
        }
        function stalled(words) {                          // the hand-off did not come in 15 s (Home takes presses again, the page's room is still frozen): the buttons that need the page's room say why they do not work
            [parts.quickHost, parts.games].forEach(function (b) { b.disabled = true; b.setAttribute('aria-disabled', 'true'); b.setAttribute('title', words); });
            hint = words; paintVis();
        }
        function inert(on) { [strip, pop].forEach(function (n) { if (on) n.setAttribute('inert', ''); else n.removeAttribute('inert'); }); }      // (a dialog of the page, or the hand-off, is over this part: it takes no key or tap)
        sync();
        poll();
        return {
            sync: guard(sync), shown: guard(shown), opening: opening, stalled: stalled, inert: inert, poll: poll,
            state: function () { return { rows: rows, loaded: loaded, off: off, failures: failures, open: isOpen, pending: !!pending, joining: joining, notice: noticeKind }; }
        };
    }

    return { TEXT: TEXT, rowOf: rowOf, parseList: parseList, delayOf: delayOf, countText: countText, seatsOf: seatsOf, mount: mount, EVERY_CLOSED: EVERY_CLOSED, EVERY_OPEN: EVERY_OPEN, REFRESH_MS: REFRESH_MS, ROWS_MAX: ROWS_MAX };
});
