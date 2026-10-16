// The rematch on the two pages (docs/PLAY_IN_BROWSER.md "After a match", docs/SERVER.md "Lobby rooms"): when the rules end a match that a lobby room began, the game server opens a lobby room with the same code at
// once, with the match's plan and a seat reserved for each person's key. This file is the pages' side of it, with no game in it:
//   * the game page (web/shell.html): the game tells the page that its match is over (window.antsMatchOver, src/ants_app/application.cpp); the page reads the key of its seat (the game's own entry, before the game
//     lets go of it), holds its reserved seat from the results screen with that key (a game's Hello: the seat stays reserved, and the others see "on the results screen"), and shows the strip under the game:
//     "The match is over", who is back, and "Back to the lobby", which hands the key to the front page (this tab's session storage) and goes there;
//   * the front page (web/lobby.html): the strip "Back from the match." at the top of the page, with the winner and a link to the match's stats and replay.
// ES5 and no module system, as the site's other scripts: a page gets the global AntsRematch, node gets module.exports (tests/scripts/web_rematch_check.js). A name is only ever set as text.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsRematch = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var BACK_KEY = 'ants.back';                     // sessionStorage: the match that this tab comes back from { c: code, t: epoch ms, m: map file, p: [four names], w: the winner's words }
    var LOBBY_KEY = 'ants.lobby';                   // sessionStorage: the front page's room { c, k, n, o } (web/lobby.html): the hand-over puts the reserved seat there
    var REJOIN_PREFIX = 'ants.rejoin.';             // localStorage: the game's key of a seat (src/ants_app/rejoin_store.cpp)
    var MAX_AGE_MS = 3 * 60 * 60 * 1000;            // a key lives three hours (the game's kRejoinMaxAgeMs), and so does the server's reservation
    var ASK_AFTER_MS = 2000;                        // the results screen asks for its seat this long after the game said that the match is over: the server ends the match in a pass of its own, which
                                                    // a game can be a moment ahead of, and the Hello must find the lobby that follows the match, not the match (where a game's key takes its seat back)
    var COLOURS = ['Green', 'Red', 'Blue', 'Black'];
    var DOTS = ['#2e8f56', '#b43a4c', '#5a56c8', '#4a4a50'];   // the colour of each seat's dot in the strips
    var ID_RE = /^ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?$/;      // a match's id (web/history_page.js): its replay's file name without .antsrep

    function printable(s, max) { return typeof s === 'string' && s.length <= max && /^[\x20-\x7e]*$/.test(s); }
    function isWhole(n) { return typeof n === 'number' && isFinite(n) && Math.floor(n) === n; }
    function validRoom(code) { return typeof code === 'string' && /^[A-Za-z0-9_-]{1,32}$/.test(code); }
    function validMap(name) { return printable(name, 64) && name.length >= 5 && !/[\/\\:*?"<>|]/.test(name) && name.charAt(0) !== '.' && /\.(lvl|LVL)$/.test(name); }
    function groupDigits(n) { var s = String(Math.abs(n)).replace(/\B(?=(\d{3})+(?!\d))/g, ','); return n < 0 ? '-' + s : s; }
    function listOf(a) { return a.length < 2 ? a.join('') : a.slice(0, -1).join(', ') + ' and ' + a[a.length - 1]; }
    function storageOf(win, name) { try { return win[name] || null; } catch (e) { return null; } }      // (a browser that blocks storage throws on the property itself)
    function storageGet(storage, key) { try { return storage ? storage.getItem(key) : null; } catch (e) { return null; } }
    function storageSet(storage, key, value) { try { storage.setItem(key, value); return true; } catch (e) { return false; } }

    // ---- what the game says when its match is over --------------------------------------------------------------------------------------------------------------------------------------------------
    // The game's words (window.antsMatchOver): { room, seat, map, mode (0 the highest score, 1 "187"), names: the four seats' names ('' for a seat that did not play), rows: the results screen's rows, best first
    // (as the server orders them: no seat's own tie rule), each { seats: [one or two seats], score, won } }. null for anything else.
    function parseOver(text) {
        var v;
        try { v = JSON.parse(text); } catch (e) { return null; }
        if (!v || typeof v !== 'object' || !validRoom(v.room) || !isWhole(v.seat) || v.seat < 0 || v.seat > 3 || !validMap(v.map) || (v.mode !== 0 && v.mode !== 1)) return null;
        if (!Array.isArray(v.names) || v.names.length !== 4 || !v.names.every(function (n) { return printable(n, 32); })) return null;
        if (!Array.isArray(v.rows) || v.rows.length > 4) return null;
        var rows = [];
        for (var i = 0; i < v.rows.length; i++) {
            var r = v.rows[i];
            if (!r || !Array.isArray(r.seats) || r.seats.length < 1 || r.seats.length > 2 || !isWhole(r.score) || typeof r.won !== 'boolean') return null;
            if (!r.seats.every(function (s) { return isWhole(s) && s >= 0 && s < 4; })) return null;
            rows.push({ seats: r.seats.slice(), score: r.score, won: r.won });
        }
        return { room: v.room, seat: v.seat, map: v.map, mode: v.mode, names: v.names.slice(), rows: rows };
    }
    // Who won, in words: "Maple won with 2,040.", "Maple and Pip won with 2,040." (an alliance), "Maple and Sam tied with 1,500." (two winning rows), and without the score in 187 ("Maple won."); '' without a winner
    function winnerWords(over) {
        var won = over.rows.filter(function (r) { return r.won; });
        if (won.length === 0) return '';
        var nameOf = function (seat) { return over.names[seat] || COLOURS[seat]; };
        var who = [];
        won.forEach(function (r) { r.seats.forEach(function (s) { who.push(nameOf(s)); }); });
        var verb = won.length === 1 ? ' won' : ' tied';
        return listOf(who) + verb + (over.mode === 1 ? '.' : ' with ' + groupDigits(won[0].score) + '.');
    }

    // ---- the key of the seat ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // The game's own entry of the seat (ants.rejoin.<room>.<seat> = {"k": 32 hex digits, ...}), read when the game says that the match is over and before it lets go of it: the key (32 lower-case hex digits), or null.
    // THE KEY IS A SECRET: it goes from here to the seat's Hello and to this tab's session storage (the front page's own place for it), nowhere else.
    function seatKey(storage, room, seat) {
        var v;
        try { v = JSON.parse(storageGet(storage, REJOIN_PREFIX + room + '.' + seat)); } catch (e) { return null; }
        if (!v || typeof v !== 'object' || typeof v.k !== 'string' || !/^[0-9A-Fa-f]{32}$/.test(v.k) || /^0+$/.test(v.k)) return null;
        return v.k.toLowerCase();
    }

    // ---- the hand-over to the front page ---------------------------------------------------------------------------------------------------------------------------------------------------------------
    // "Back to the lobby": the front page of this tab finds its room (the code and the key: it takes the reserved seat back with them) and the match it comes back from. False when the storage refuses
    // the room (the front page could not take the seat back: nothing may go); a match that is not kept only leaves the front page without its strip.
    function handOver(session, over, key, name, nowMs) {
        var lobby = JSON.stringify({ c: over.room, k: key, n: printable(name, 32) ? name : '', o: 0 });
        var back = JSON.stringify({ c: over.room, t: Math.floor(nowMs), m: over.map, p: over.names, w: winnerWords(over) });
        if (!storageSet(session, LOBBY_KEY, lobby)) return false;
        storageSet(session, BACK_KEY, back);
        return true;
    }
    // The match that the room `code` comes back from: { code, t, map, people: the names of the match, names: the four by seat ('' for a seat that did not play), winner }, or null (none, another room's, older
    // than three hours or from the future, or not this shape)
    function readBack(session, code, nowMs) {
        var v;
        try { v = JSON.parse(storageGet(session, BACK_KEY)); } catch (e) { return null; }
        if (!v || typeof v !== 'object' || v.c !== code || !validRoom(code) || !isWhole(v.t) || nowMs - v.t > MAX_AGE_MS || v.t - nowMs > 60000) return null;
        if ((v.m !== '' && !validMap(v.m)) || !Array.isArray(v.p) || v.p.length !== 4 || !v.p.every(function (n) { return printable(n, 32); }) || !printable(v.w, 160)) return null;
        return { code: code, t: v.t, map: v.m, people: v.p.filter(function (n) { return n !== ''; }), names: v.p.slice(), winner: v.w };
    }
    function forgetBack(session) { try { session.removeItem(BACK_KEY); } catch (e) { /* nothing was kept */ } }

    // ---- what the strips say --------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // The people of a lobby room's Room message (web/front/lobby_net.js decode) for the game page's strip, in the order of the colours: { seat, name, dot, host, here }. Not here: still on the results screen
    // (the room counts the reserved seat as a game before any START, and its game runs in a browser or did not say: web/front/lobby_rules.js modelOf has the same rule); here: everybody else, a game of the
    // desktop that joined by the code too
    function chipsOf(room) {
        var out = [];
        if (!room || !room.slots) return out;
        for (var s = 0; s < 4; s++) {
            var st = room.slots[s].state, platform = room.slots[s].platform || 0;
            if (st !== 1 && st !== 2) continue;
            var results = ((room.inGame >> s) & 1) === 1 && !room.starting && (platform === 0 || (platform & 0x10) !== 0);
            out.push({ seat: s, name: room.slots[s].name, dot: DOTS[s], host: room.leader === s, here: !results });
        }
        return out;
    }
    function chipText(chip) { return chip.name + (chip.host ? ' · host' : '') + ' · ' + (chip.here ? 'here' : 'on the results screen'); }
    // The front page's strip: { lead, text } ("Back from the match." and "Maple won with 2,040. Same room, same colours, same map.")
    function backLine(back) { return { lead: 'Back from the match.', text: (back.winner ? back.winner + ' ' : '') + 'Same room, same colours, same map.' }; }
    // How the public list of replays names a seat of the match: the colour, and the name in brackets ("Green (Maple)"); a seat without a name of its own (none, "Player", "Player 2") is its colour. The
    // server's replay::seat_label of the name that the recording keeps (replay_person_name; tests/data/replay_players.txt holds the two to the server's own output).
    function replayLabel(seat, name) {
        var kept = name.replace(/^ +| +$/g, '');
        if (/^Player( [1-4])?$/.test(kept)) kept = '';
        return kept ? COLOURS[seat] + ' (' + kept + ')' : COLOURS[seat];
    }
    // The match's id in the public list of replays (GET /replays: { replays: [{ file, ended (s), map, players: the labels of the seats, finished }, ...] }, newest first): the newest finished match on the map,
    // ended within ten minutes of the hand-over, in which every seat of the match played as it did (its label); '' when there is none (the strip links the list of matches then)
    function findReplay(list, back) {
        var want = [];
        for (var s = 0; s < 4; s++) if (back.names[s]) want.push(replayLabel(s, back.names[s]));
        var entries = list && Array.isArray(list.replays) ? list.replays : [];
        for (var i = 0; i < entries.length; i++) {
            var e = entries[i];
            if (!e || typeof e.file !== 'string' || e.map !== back.map || e.finished !== true || !isWhole(e.ended) || Math.abs(e.ended * 1000 - back.t) > 10 * 60 * 1000) continue;
            var players = Array.isArray(e.players) ? e.players : [];
            if (want.length === 0 || !want.every(function (l) { return players.indexOf(l) !== -1; })) continue;
            var id = e.file.replace(/\.antsrep$/, '');
            if (ID_RE.test(id)) return id;
        }
        return '';
    }
    function statsHref(id) { return id ? '/match.html?id=' + encodeURIComponent(id) : '/matches.html'; }

    // ---- the browser's platform (what the Hello tells the room: web/lobby.html's rule) ----
    function platformOf(nav) {
        var ua = ((nav && nav.userAgent) || '') + ' ' + ((nav && nav.platform) || '');
        var os = /Android/i.test(ua) ? 4 : /iPhone|iPad|iPod/i.test(ua) ? 5 : /Windows/i.test(ua) ? 1 : /Mac/i.test(ua) ? 2 : /Linux|X11/i.test(ua) ? 3 : 6;
        return 0x10 | os;
    }

    function el(doc, tag, cls, text) {
        var e = doc.createElement(tag);
        if (cls) e.className = cls;
        if (text !== undefined) e.textContent = text;
        return e;
    }

    // ---- the game page ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // env: { win, doc, Net (AntsLobbyNet), host (the strip's element); for the checks also loc (location: search, protocol, host, assign; else win.location), local and session (the storages; else the window's),
    // now() (else Date.now), later(fn, ms) (else setTimeout), clientOptions (more options of the LobbyClient: WebSocket, setTimeout, clearTimeout, now) }. Sets win.antsMatchOver; the strip is built in `host`
    // when the room of the match is there again and our seat is ours. Returns the state and back(), for the checks.
    function gamePage(env) {
        var win = env.win, loc = env.loc || win.location;
        var local = 'local' in env ? env.local : storageOf(win, 'localStorage'), session = 'session' in env ? env.session : storageOf(win, 'sessionStorage');
        var now = env.now || function () { return Date.now(); };
        var later = env.later || function (fn, ms) { return setTimeout(fn, ms); };
        var q = null;
        try { q = new URLSearchParams(loc.search); } catch (e) { return null; }
        var join = q.get('join');
        if (!join || !/^\/ws(\/[A-Za-z0-9._~\/-]*)?$/.test(join) || join.indexOf('//') !== -1 || join.indexOf('..') !== -1 || !env.host || !env.Net) return null;
        var name = (q.get('name') || '').replace(/[^\x20-\x7e]/g, '').trim().substring(0, 32);
        var url = (loc.protocol === 'https:' ? 'wss://' : 'ws://') + loc.host + join;
        var st = { over: null, key: null, client: null, shown: false, removed: false, blocked: false, room: null };
        var nodes = null;
        function relayout() { try { win.dispatchEvent(new win.Event('resize')); } catch (e) { /* no layout to tell */ } }
        function build() {
            var doc = env.doc;
            env.host.textContent = '';
            var main = el(doc, 'div', 'rmt-main');
            main.appendChild(el(doc, 'div', 'rmt-title', 'The match is over'));
            var text = el(doc, 'div', 'rmt-text');
            text.appendChild(doc.createTextNode('Everybody can go back to the room together: '));
            text.appendChild(el(doc, 'b', '', 'same host, same colours, same map'));
            text.appendChild(doc.createTextNode('. Your seat is kept for you.'));
            main.appendChild(text);
            var who = el(doc, 'div', 'rmt-who');
            who.setAttribute('aria-label', 'Who is back in the room');
            main.appendChild(who);
            var go = el(doc, 'div', 'rmt-go');
            var button = el(doc, 'button', 'btn big', 'Back to the lobby');
            button.type = 'button';
            button.addEventListener('click', back);
            go.appendChild(button);
            go.appendChild(el(doc, 'div', 'rmt-note', 'The game’s own “Leave Game” still goes to the menu.'));
            env.host.appendChild(main);
            env.host.appendChild(go);
            nodes = { text: text, who: who, button: button, go: go };
        }
        function paint() {
            if (!nodes) return;
            nodes.who.textContent = '';
            chipsOf(st.room).forEach(function (c) {
                var chip = el(env.doc, 'span', 'rmt-chip' + (c.here ? '' : ' away'));
                var dot = el(env.doc, 'i');
                dot.style.background = c.dot;
                chip.appendChild(dot);
                chip.appendChild(env.doc.createTextNode(chipText(c)));
                nodes.who.appendChild(chip);
            });
        }
        function show() {
            if (!nodes) build();
            if (!st.shown) { st.shown = true; env.host.hidden = false; relayout(); }
            paint();
        }
        function hide() {
            if (!st.shown) return;
            st.shown = false;
            env.host.hidden = true;
            relayout();
        }
        function removed() {                                       // the host took this player out of the room (or the seat waited three hours): the strip says so, and has no button
            st.removed = true;
            if (!nodes) return;
            nodes.text.textContent = 'Your seat in the room is not kept any more. The game’s own “Leave Game” goes to the menu.';
            nodes.who.textContent = '';
            nodes.go.hidden = true;
            relayout();
        }
        function blocked() {                                       // the browser refused this tab's storage: the front page could not take the seat back, so the tab stays (no key goes into an address)
            st.blocked = true;
            if (!nodes) return;
            nodes.text.textContent = 'This browser blocks the storage that the front page needs, so your seat cannot be taken back from here.';
            nodes.go.hidden = true;
            relayout();
        }
        function back() {
            if (!st.over || !st.key || st.removed || st.blocked) return;
            var shown = st.room && st.room.you < 4 ? st.room.slots[st.room.you].name : '';           // (the name that the room shows for the seat, else the address's, else the match's)
            if (!handOver(session, st.over, st.key, shown || name || st.over.names[st.over.seat], now())) { blocked(); return; }      // (the link stays: the seat stays held from here)
            if (st.client) st.client.drop();                       // (no Leave: the seat is the front page's in a moment, with the key)
            loc.assign('/?room=' + encodeURIComponent(st.over.room));
        }
        // The results screen's link: a game's Hello with the key of the seat. A Welcome of the MATCH (the rejoin flag: only a match that still runs says it, when the server had not ended it yet) is let go
        // of without a word, and the seat is asked for again a moment later, `tries` times in all
        function ask(opts, tries) {
            var c = new env.Net.LobbyClient(opts);
            st.client = c;
            c.on('welcome', function (w) {
                if (w && w.rejoin) {
                    c.drop();
                    if (tries > 1) later(function () { ask(opts, tries - 1); }, ASK_AFTER_MS);
                    return;
                }
                show();
            });
            c.on('room', function (room) { st.room = room; if (st.shown) paint(); });
            c.on('removed', removed);
            c.on('refused', hide);
            c.on('superseded', hide);
            c.connect();
        }
        win.antsMatchOver = function (text) {
            if (st.over) return;
            try { if (win.top !== win) return; } catch (e) { return; }               // (a game in a frame of another page has no strip)
            var over = parseOver(text);
            if (!over) return;
            var key = seatKey(local, over.room, over.seat);
            if (!key) return;
            st.over = over;
            st.key = key;
            var opts = { url: url, code: over.room, name: name || over.names[over.seat], key: env.Net.bytesOfHex(key), join: true, kind: 'game', keyOnly: true, platform: platformOf(win.navigator) };
            var extra = env.clientOptions || {};
            for (var k in extra) if (Object.prototype.hasOwnProperty.call(extra, k)) opts[k] = extra[k];
            later(function () { ask(opts, 3); }, ASK_AFTER_MS);
        };
        // The page goes some other way (the game's Leave Game, a closed tab, a reload): the seat is given up (Leave), so that START does not wait for a person who cannot come back to it (the game let go of
        // the key when the match ended, and this page's copy goes with the page). After "Back to the lobby", which hands the seat over, the client has let go already and says nothing.
        if (win.addEventListener) win.addEventListener('pagehide', function () { if (st.client) st.client.leave(); });
        return { state: st, back: back };
    }

    // ---- the front page ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // The strip at the top of the page: "Back from the match." The match's stats and replay are looked up in the public list (a moment after the match, twice more if they are not there yet: the list is cached
    // for five seconds). env: { doc, before (the element that the strip goes in front of), fetch (window.fetch or null), later(fn, ms) }. show(back) and hide().
    function frontStrip(env) {
        var bar = null, link = null, round = 0;
        function look(back, mine, tries) {
            if (!env.fetch || mine !== round) return;
            var done = function (id) {
                if (mine !== round) return;
                if (id) link.href = statsHref(id);
                else if (tries > 1) env.later(function () { look(back, mine, tries - 1); }, 4000);
            };
            try {
                env.fetch('/replays', { cache: 'no-store', credentials: 'omit' }).then(function (r) { return r.ok ? r.json() : null; }).then(function (list) { done(findReplay(list, back)); }, function () { done(''); });
            } catch (e) { /* no list: the link goes to the list of matches */ }
        }
        return {
            show: function (back) {
                var mine = ++round;
                if (!bar) {
                    bar = el(env.doc, 'div', 'backbar');
                    bar.setAttribute('role', 'status');
                    bar.id = 'backbar';
                    env.before.parentNode.insertBefore(bar, env.before);
                }
                bar.textContent = '';
                var words = backLine(back);
                var span = el(env.doc, 'span', 'backbar-text');
                span.appendChild(el(env.doc, 'b', '', words.lead));
                span.appendChild(env.doc.createTextNode(' ' + words.text));
                bar.appendChild(span);
                link = el(env.doc, 'a', '', 'Stats and replay');
                link.href = statsHref('');
                link.target = '_blank';
                link.rel = 'noopener noreferrer';
                bar.appendChild(link);
                bar.hidden = false;
                look(back, mine, 3);
            },
            hide: function () { ++round; if (bar) bar.hidden = true; }
        };
    }

    return {
        BACK_KEY: BACK_KEY, LOBBY_KEY: LOBBY_KEY, ASK_AFTER_MS: ASK_AFTER_MS, COLOURS: COLOURS, DOTS: DOTS,
        parseOver: parseOver, winnerWords: winnerWords, seatKey: seatKey, handOver: handOver, readBack: readBack, forgetBack: forgetBack,
        chipsOf: chipsOf, chipText: chipText, backLine: backLine, findReplay: findReplay, statsHref: statsHref, platformOf: platformOf,
        gamePage: gamePage, frontStrip: frontStrip
    };
});
