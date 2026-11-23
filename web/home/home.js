// The front page (web/lobby.html, docs/HOME_SCREEN.md): Home, a top bar over the games, the players and the chat, and the Room, which takes the games' place inside it while the page shows it (the players, the chat and the bar stay).
// One page, one WebSocket: the page's client, model and session storage are never touched here, so Games <-> Room is a class on <body> (data-screen, home.css) and one render(): no request, the seat is never dropped. Text and properties only, never markup. ES5:
// a page gets AntsHome, node gets module.exports (tests/scripts/web_home_screen_check.js).
//   * Home is "/", the Room "/#room" (pushState); a link ("?room=CODE") opens the Room; the page writes no history until a button is pressed.
//   * While the page holds a room the middle panel is ALWAYS the room, and Leave is the way out: there is no Games view then. Back, a reload, a link and the Menu's Home lead to the room, and so does Quick host (it changes nothing and asks nothing).
//   * A plain visit makes no room (Quick host makes one). The bar has Quick host, Quick join and Match history (a link to the matches). Quick host and Games are lit by games.js, which loads after the first paint. START reaches a guest on Home as on the Room: Home says so.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHome = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var MENU_MIN_HEIGHT = 120, MENU_GAP = 10;            // px: the Menu keeps this much at least, and this much of the window below it
    var HASH = '#room';                                  // the address of the Room screen
    var STATE_KEY = 'ants';                              // history.state.ants: 'home' or 'room' (the entries that this script made or marked)
    var CHAT = { script: 'home/chat.js', style: 'home/chat.css' };       // the chat dock: loaded once, after the first paint, when Home is first shown
    var OPENING_MS = 15000, GIVE_UP_MS = 60000;          // a hand-off that is still here after the first has been slow: Home takes presses again, and says so; after the second the page says that it did not come
    var GAMES = 'home/games';                            // the Games list, Quick host and the Room's switch (games.js, games.css): loaded once, after the first paint
    var TEXT = {
        name: 'Ants!', quickHost: 'Quick host', quickJoin: 'Quick join', history: 'Match history', games: 'Games', room: 'Room', menu: 'Menu', login: 'Log in', leave: 'Leave', left: 'You left the room.',
        slow: 'The game is slow to open. Reload this page to get your seat back.', failed: 'The game did not open. Reload this page to get your seat back.',
        players: 'Players', chat: 'Chat', views: 'Home views'
    };
    // The Menu's links: the footer's and the header's (test_web_home_screen.py keeps them equal), and the page's How it works (no address)
    var MENU_MAIN = [['Home', '/'], ['Matches', '/matches'], ['Map maker', '/mapmaker'], ['How it works', ''], ['Discord server', 'https://discord.gg/WaWrXQnKPj']];
    var MENU_SMALL = [['Sprites and sounds', '/asset_catalog/'], ['What\u2019s new', '/changelog'], ['GitHub', 'https://github.com/dchadd427/ants-cross-platform'], ['Feedback', 'https://github.com/dchadd427/ants-cross-platform/issues'], ['Legal', '/legal']];
    var TABS = [['games', 'Games'], ['players', 'Players'], ['chat', 'Chat']];      // the phone's strip (a window up to 700 px wide or up to 500 px high): one view at a time
    var PHONE = '(max-width: 700px), (max-height: 500px)';

    // ---- the pure parts ----
    // The screen that the page shows: the Room while it holds a room, Home when it does not (no entry of the history and no address decides: Back never shows the Games list to a person who holds a room)
    function screenOf(hasRoom) { return hasRoom ? 'room' : 'home'; }
    // The screen that a page opens on: a link (or a test room) and a seat that this tab holds open the Room; else Home
    function firstScreen(link, hasSeat) { return link || hasSeat ? 'room' : 'home'; }

    // The height that Home must not be taller than. box: Home's own height with no limit set; inner, client, vv: the window's three readings (0 or missing: none); scale: the visual viewport's zoom
    // (no reading of the window's height counts while it is not 1: a pinch must not shrink the page, and some phones' innerHeight follows it); typing: a text field has the focus. Returns a number of px (set --home-h), null (no limit) or 'keep' (change nothing).
    // The smallest reading wins, as the page cannot be taller than any true one. A field with the focus changes nothing (the keyboard takes its room and the page slides, as it did), and a reading
    // under two thirds of the box is a keyboard or a split window, not a page that is too tall: no limit.
    function fitHeight(m) {
        if (m.typing) return 'keep';
        var least = 0, one = m.scale > 0.99 && m.scale < 1.01, all = [one ? m.inner : 0, m.client, one ? m.vv : 0];
        for (var i = 0; i < all.length; i++) if (all[i] > 0 && isFinite(all[i]) && (!least || all[i] < least)) least = all[i];
        if (!least || !(m.box > least + 1) || least < m.box * 2 / 3) return null;
        return Math.floor(least);
    }
    // The page's height (home.css): the smaller of the window's own readings, never while a field has the focus. Home is measured with no limit, so the limit cannot argue with itself.
    // onHome() says whether Home shows (the Room's page is not as tall as the window). Returns { fit }; fit() is also what runs on load, on every resize of the window or of the visual
    // viewport, on a turn of the phone (twice: the new size comes a moment late) and shortly after a field loses the focus (the keyboard goes).
    function heightGuard(win, doc, onHome) {
        function typing() { var a = doc.activeElement, t = a && a.tagName; return !!a && (t === 'INPUT' || t === 'TEXTAREA' || t === 'SELECT' || a.isContentEditable === true); }
        function installed() { try { return !!win.matchMedia && win.matchMedia('(display-mode: standalone), (display-mode: fullscreen), (display-mode: minimal-ui)').matches; } catch (e) { return false; } }       // (a browser tab: the root's height stays the small window when the address bar hides, so it is not a reading there)
        function fit() {
            try {
                var root = doc.documentElement, v = win.visualViewport;
                if (typing()) return;
                root.style.removeProperty('--home-h');
                if (!onHome()) return;
                var h = fitHeight({ box: doc.body.getBoundingClientRect().height, inner: win.innerHeight, client: installed() ? root.clientHeight : 0, vv: v ? v.height : 0, scale: v ? v.scale : 1, typing: false });
                if (typeof h === 'number') root.style.setProperty('--home-h', h + 'px');
            } catch (e) { /* the page keeps its own height */ }
        }
        function soon() { fit(); setTimeout(fit, 300); }
        fit();
        try {
            win.addEventListener('resize', fit); win.addEventListener('orientationchange', soon); doc.addEventListener('focusout', function () { setTimeout(fit, 350); });
            if (win.visualViewport && win.visualViewport.addEventListener) win.visualViewport.addEventListener('resize', fit);
            setTimeout(fit, 300);
        } catch (e) { /* no events here */ }
        return { fit: fit };
    }
    // The address "/?vp" shows the numbers of the window (home/vp.js, loaded only for this address)
    function readout(win, doc) {
        if (!/[?&]vp(&|=|$)/.test((win.location && win.location.search) || '')) return false;
        try { var js = doc.createElement('script'); js.src = 'home/vp.js'; js.async = true; js.onload = function () { if (win.AntsHomeVp) win.AntsHomeVp.mount({ doc: doc, win: win }); }; (doc.head || doc.body).appendChild(js); } catch (e) { /* none */ }
        return true;
    }

    // ---- the screens ----
    // mount({ doc, win, host (Home's div), room (the page's room: it goes into Home's body, in the games' place while the Room shows), page: { info() -> { client, model, offline }, make(), leave(), how(), stalled(words) (optional), ... } })
    // returns { start, sync, opening, show, screen, inert, menu, dock }
    function mount(env) {
        var doc = env.doc, win = env.win, host = env.host, page = env.page;
        var fitting = null, roomChat = null, askedRoomChat = false, screen = 'home', menuOpen = false, menuFor = null, menuBox = null, factsBox = null, going = false, held = false, stalled = false, goingTimer = null, failTimer = null, said = '', dock = null, asked = false, games = null, askedGames = false, players = null, askedPlayers = false, askedTalk = false, split = null;

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
        function stub(cls, text) { var b = button(cls, text); b.disabled = true; b.setAttribute('aria-disabled', 'true'); return b; }     // (a button that a later piece lights: it takes no press)
        function heading() { return el('h1', 'hbrand', TEXT.name); }       // the page's heading, in the bar of the screen that shows: words for a screen reader, nothing drawn
        function menuButton() {
            var b = button('hmenu', '', function () { toggleMenu(b); });
            b.setAttribute('aria-label', TEXT.menu); b.setAttribute('aria-expanded', 'false');
            b.appendChild(el('i', 'bars'));
            return b;
        }

        // Home: the top bar, the body and the phone's strip
        var bar = el('header', 'hbar');
        var quickHost = stub('quick host', TEXT.quickHost), quickJoin = stub('quick join', TEXT.quickJoin);
        var histLink = el('a', 'btn quick hist', TEXT.history);       // (Match history: the page of the matches; a link, in a new tab while the page holds a room: marks())
        histLink.href = '/matches';
        var gamesBtn = stub('hchip games', ''), gamesPill = el('span', 'pill');       // (no Games button in the bar: the games list is always on Home, and a phone's strip has Games. The button and its count stay as the list's own handle and are not placed in the page)
        gamesPill.hidden = true; gamesBtn.appendChild(el('span', 'name', TEXT.games)); gamesBtn.appendChild(gamesPill);
        var right = el('div', 'hright'), login = button('hlogin', TEXT.login); login.hidden = true;       // (the login chip: the chat dock fills it and shows it where the hub offers a login)
        var menuHome = menuButton();
        right.appendChild(login); right.appendChild(menuHome);
        [heading(), quickHost, quickJoin, histLink, right].forEach(function (n) { bar.appendChild(n); });
        var main = el('main', 'hmain'); main.id = 'hmain';       // the games list and the players list mount here, side by side; the chat dock comes after it
        var tabs = el('nav', 'htabs'), tab = 'games', tabBtns = {}; tabs.setAttribute('aria-label', TEXT.views);
        TABS.forEach(function (t) {
            var b = button('htab t-' + t[0], '', function () { pick(t[0]); }), pill = el('span', 'pill'); pill.hidden = true;
            var label = el('span', 'name', t[1]); b.appendChild(label); b.appendChild(pill); tabs.appendChild(b); tabBtns[t[0]] = { b: b, pill: pill, name: label };
        });
        host.appendChild(bar); host.appendChild(main); host.appendChild(tabs); host.setAttribute('data-tab', tab);

        // The Room: its Leave room button, in the room's own header (the page's markup has the place: #r-acts), and the room itself in Home's body
        var leave = button('leave', TEXT.leave, function () { leaveFrom(leave); });
        function leaveFrom(opener) {                        // Leave (the Room's button): at once when alone, else the list asks first; then Home says that the room was left
            if (stalled) return;
            var go = function () { page.leave(); show('home', 'push'); gamesCall('said', TEXT.left); if (screen === 'home' && (!doc.activeElement || doc.activeElement === doc.body || doc.activeElement === opener)) away().focus(); };
            if (!asking(go, opener)) go();
        }
        var racts = doc.getElementById('r-acts');
        if (racts) racts.appendChild(leave);
        if (env.room) main.appendChild(env.room);

        // ---- the Menu ----
        function newTab(a, on) {
            if (on) { a.setAttribute('target', '_blank'); a.setAttribute('rel', 'noopener noreferrer'); } else { a.removeAttribute('target'); a.removeAttribute('rel'); }
        }
        function link(text, href, cls) {
            var a = el('a', cls, text);
            a.href = href;
            if (/^https?:/.test(href)) newTab(a, true);
            a.addEventListener('click', closeMenu);
            return a;
        }
        // The links to the matches (the Menu's, the masthead's, the footer's) open in a new tab while the page holds a room (docs/PLAY_IN_BROWSER.md, "Going away and coming back")
        var matchesItem = null;
        function marks() {
            var room = false;
            try { room = !!(page.holds && page.holds()); } catch (e) { room = false; }
            [matchesItem, histLink, doc.getElementById('watch-top'), doc.getElementById('watch-link')].forEach(function (a) { if (a) newTab(a, room); });
        }
        function buildMenu() {
            menuBox = el('nav', 'hmenubox'); menuBox.setAttribute('aria-label', TEXT.menu);
            MENU_MAIN.forEach(function (m) {
                var item;
                if (m[1]) item = link(m[0], m[1], 'mitem');
                else { item = el('button', 'mitem', m[0]); item.type = 'button'; item.addEventListener('click', function () { var back = menuFor; closeMenu(); if (back && back.focus) back.focus(); page.how(); }); }      // (the page's list gives the focus back to what had it: the Menu's button)
                if (m[1] === '/matches') matchesItem = item;
                if (m[1] === '/') { item.setAttribute('aria-current', 'page'); item.addEventListener('click', function (e) { e.preventDefault(); show(screenOf(holds()), 'push'); }); }      // (this page: the Room's Home goes back to the front page's own screen)
                menuBox.appendChild(item);
            });
            var small = el('p', 'msmall');
            MENU_SMALL.forEach(function (m) { small.appendChild(link(m[0], m[1], 'mlink')); });
            menuBox.appendChild(small);
            factsBox = el('p', 'mfacts');
            menuBox.appendChild(factsBox);
            menuBox.hidden = true;
            doc.body.appendChild(menuBox);
        }
        function facts() {                                  // the numbers and the version of the footer, read when the Menu opens
            var out = [], stats = doc.getElementById('stats'), live = doc.getElementById('stats-live'), version = doc.getElementById('game-version');
            if (stats && !stats.hidden && live && live.textContent) out.push(live.textContent);
            if (version && version.textContent && version.textContent.indexOf('@') === -1) out.push('Version ' + version.textContent);
            return out;
        }
        function placeMenu() {                              // under the whole bar (on a phone the bar has three rows), and no taller than what is left of the window: the rest scrolls inside it
            var r = bar.getBoundingClientRect(), top = Math.round(r.bottom + 6);
            menuBox.style.top = top + 'px';
            menuBox.style.maxHeight = Math.max(MENU_MIN_HEIGHT, (win.innerHeight || 0) - top - MENU_GAP) + 'px';
        }
        function openMenu(btn) {
            if (!menuBox) buildMenu();
            menuFor = btn; menuOpen = true;
            marks();
            var lines = facts();
            factsBox.textContent = lines.join('\n'); factsBox.hidden = !lines.length;
            menuBox.hidden = false;
            placeMenu();
            btn.setAttribute('aria-expanded', 'true');
            if (menuBox.children[0].focus) menuBox.children[0].focus();
        }
        function closeMenu() {
            if (!menuOpen) return;
            menuOpen = false;
            if (menuBox) menuBox.hidden = true;
            if (menuFor) menuFor.setAttribute('aria-expanded', 'false');
            menuFor = null;
        }
        function toggleMenu(btn) { if (menuOpen && menuFor === btn) closeMenu(); else { closeMenu(); openMenu(btn); } }
        doc.addEventListener('click', function (e) {          // a press anywhere else puts the Menu away
            if (!menuOpen) return;
            var t = e && e.target;
            if (t && ((menuBox && menuBox.contains && menuBox.contains(t)) || (menuFor && menuFor.contains && menuFor.contains(t)))) return;
            closeMenu();
        });
        doc.addEventListener('keydown', function (e) {
            if (e && e.key === 'Escape' && menuOpen) { var back = menuFor; closeMenu(); if (back && back.focus) back.focus(); }
        });

        // ---- the screens and the address ----
        function path() { return win.location.pathname.replace(/^\/+/, '/'); }
        function stateOf(name) { var state = {}; state[STATE_KEY] = name; return state; }
        function mark(name) { try { win.history.replaceState(stateOf(name), '', path() + (name === 'room' ? HASH : '')); } catch (e) { /* the address stays */ } }       // (Back came to an entry that said Home while the page holds a room: the entry says Room now)
        // A button makes a new history entry for the screen it goes to ("/" for Home, "/#room" for the Room), so that Back comes back. The entry that the page opened on is marked with its screen
        // the first time that we leave it, and its address stays. A browser that refuses (a sandboxed frame) keeps its address.
        function push(from, to) {
            try {
                var now = win.history.state;
                if (!(now && typeof now === 'object' && now[STATE_KEY])) win.history.replaceState(stateOf(from), '', path() + win.location.search + (from === 'room' ? win.location.hash : ''));      // ("/#room" in a tab with no seat is Home: its address says so)
                win.history.pushState(stateOf(to), '', path() + (to === 'room' ? HASH : ''));
            } catch (e) { /* the address stays */ }
        }
        // The phone's strip: Games, Players and Chat, one at a time (home.css shows it up to 700 px wide or up to 500 px high; a wider, taller window shows all three and ignores it). The chat is open only on its own view, so what comes while it is away counts as unread.
        function phone() { try { return !!win.matchMedia && win.matchMedia(PHONE).matches; } catch (e) { return false; } }
        function pick(name) {
            tab = name; host.setAttribute('data-tab', name);
            Object.keys(tabBtns).forEach(function (k) { tabBtns[k].b.setAttribute('aria-pressed', k === name ? 'true' : 'false'); });
            if (dock && phone()) dock.open(name === 'chat');
        }
        function mirror(from, to) {                         // a count that games.js or players.js keeps: the strip shows the same
            var copy = function () { var t = from.textContent; to.hidden = from.hidden; to.textContent = /^\d{3}/.test(t) ? '99+' : t; };
            copy();
            try { if (win.MutationObserver) new win.MutationObserver(copy).observe(from, { childList: true, characterData: true, attributes: true, subtree: true }); } catch (e) { /* the strip has no count */ }
        }
        var was = phone();
        pick(tab);
        mirror(gamesPill, tabBtns.games.pill);
        gamesBtn.addEventListener('click', function () { pick('games'); });
        // how: 'push' (a button: a new entry), 'pop' (the browser moved: no entry), 'first' (the page's first look: none either)
        function show(name, how) {
            closeMenu();
            if (how === 'push' && name === screen) { sync(); return; }      // (already there: no second entry)
            var from = screen;
            screen = name;
            doc.body.setAttribute('data-screen', name);
            if (fitting) fitting.fit();
            if (how === 'push') push(from, name);
            sync();
            wantDock();
            dockShown();
            gamesCall('shown', name);
            if (how !== 'first') {
                try { win.scrollTo(0, 0); } catch (e) { /* no scrolling here */ }
                var next = away();
                if (next.focus) next.focus();
            }
        }
        function holds() { try { return !!page.info().client; } catch (e) { return false; } }
        function away() { return !quickHost.disabled ? quickHost : menuHome; }       // (where the focus goes when a screen changes under it: the first button that takes a press)
        function sync() {
            var info = page.info();
            if (!askedSheet && askedMod) wantSheet();       // (a room was made or joined: the sheet that invites comes with it)
            tabBtns.games.name.textContent = screen === 'room' && info.client ? TEXT.room : TEXT.games;
            marks();
            gamesCall('sync');
            if (roomChat) roomChat.sync();
        }
        // A part that loads after the first paint (its script and style): once both are there onReady mounts it; a part that does not come leaves its buttons as they are
        function lazy(base, onReady) {
            var parts = 2;
            function one() { if (--parts === 0) { try { onReady(); } catch (e) { /* the part is its own */ } } }
            function load() {
                try {
                    var tail = doc.head || doc.body, css = el('link'), js = el('script');
                    css.rel = 'stylesheet'; css.href = base + '.css'; css.onload = one;
                    js.src = base + '.js'; js.async = true; js.onload = one;
                    tail.appendChild(css); tail.appendChild(js);
                } catch (e) { /* none */ }
            }
            var afterPaint = typeof win.requestAnimationFrame === 'function' ? function (fn) { win.requestAnimationFrame(function () { setTimeout(fn, 0); }); } : function (fn) { setTimeout(fn, 0); };
            afterPaint(load);
        }
        function asking(go, opener) { try { return !!games && !!games.leaving(go, opener || leave); } catch (e) { return false; } }       // (the Games list asks before a room with other people in it is left; it says whether it asks)
        function gamesCall(name, arg) { try { if (games) games[name](arg); } catch (e) { /* the list is its own */ } }
        function wantGames() {
            if (askedGames) return;
            askedGames = true;
            lazy(GAMES, function () {
                if (!win.AntsHomeGames) return;
                games = win.AntsHomeGames.mount({ doc: doc, win: win, host: host, dock: main, page: page, parts: { quickHost: quickHost, quickJoin: quickJoin, games: gamesBtn, pill: gamesPill }, ctl: { show: show, screen: function () { return screen; } }, handoff: going || stalled });
                gamesCall('shown', screen);
                if (stalled) gamesCall('stalled', said);          // (the part came after START: it mounts waiting for the game, and after 15 s it says why its buttons are off)
                gamesCall('inert', held || going);
            });
        }
        // The page decided what it connects to: link (a link or a test room: the Room), seat (it holds a room of this tab). Called once, before the page connects.
        function start(link, seat) {
            wantAlerts();
            show(firstScreen(!!link, !!seat), 'first');
            wantSplit();
            wantGames();
            win.addEventListener('popstate', function () { var name = screenOf(holds()); show(name, 'pop'); if (name === 'room') mark('room'); });
            fitting = heightGuard(win, doc, function () { return true; });
            readout(win, doc);
            win.addEventListener('resize', function () { if (menuOpen) placeMenu(); var ph = phone(); if (dock && ph !== was) dock.open(ph ? tab === 'chat' : (win.innerHeight || 0) >= 601); was = ph; });
        }
        // START reached this page: it is on its way to the game page, so both bars take no press, and Home says so
        function say() {
            going = true; closeMenu(); gamesCall('opening'); inert(held);
            marks();
            if (goingTimer !== null) clearTimeout(goingTimer);
            if (failTimer !== null) clearTimeout(failTimer);
            failTimer = null;
            goingTimer = setTimeout(function () {                // not here yet: Home takes presses again, but the seat may still be the game's, so Leave room stays off, and the page says so
                goingTimer = null; going = false; stalled = true; leave.disabled = true; leave.setAttribute('aria-disabled', 'true'); inert(held);
                tell(TEXT.slow);
                failTimer = setTimeout(function () { failTimer = null; tell(TEXT.failed); }, GIVE_UP_MS - OPENING_MS);
            }, OPENING_MS);
        }
        function tell(words) {                                   // the page's strip, and the Games list's buttons that wait for the game
            said = words;
            try { if (page.stalled) page.stalled(words); } catch (e) { /* what the page says is its own */ }
            gamesCall('stalled', words);
        }
        function inert(on) { held = !!on; [bar, racts, tabs, dock && dock.root, players && players.root].forEach(function (n) { if (n) { if (held || going) n.setAttribute('inert', ''); else n.removeAttribute('inert'); } }); gamesCall('inert', held || going); }      // (a page that is on its way to the game takes no press again, whatever a dialog that closes says)
        // The chat dock (chat.js, chat.css) mounts into Home's div and is told which screen shows
        function dockShown() { try { if (dock) dock.shown(true); } catch (e) { /* the dock is its own */ } }
        function wantDock() {
            if (asked) return;
            asked = true;
            var parts = 2;
            function mountDock() { if (--parts) return; try { if (win.AntsHomeChat) { dock = win.AntsHomeChat.mount({ doc: doc, win: win, host: host, chip: login, screen: function () { return screen; }, load: lazy }); inert(held); dockShown(); pick(tab); wantPlayers(); wantTalk(); wantRoomChat(); } } catch (e) { dock = null; } }
            function load() {
                try {
                    var tail = doc.head || doc.body, css = el('link'), js = el('script');
                    css.rel = 'stylesheet'; css.href = CHAT.style; css.onload = mountDock;
                    js.src = CHAT.script; js.async = true; js.onload = mountDock;
                    tail.appendChild(css); tail.appendChild(js);
                } catch (e) { /* no chat */ }
            }
            if (typeof win.requestAnimationFrame === 'function') win.requestAnimationFrame(function () { setTimeout(load, 0); }); else setTimeout(load, 0);
        }

        function wantRoomChat() {                           // the room's tab of the chat box (roomchat.js, roomchat.css), after the dock: the room's chat moves into the box and the two are one tap apart
            if (askedRoomChat || !dock || !doc.getElementById('chat')) return;
            askedRoomChat = true;
            lazy('home/roomchat', function () {
                if (!win.AntsHomeRoomChat) return;
                roomChat = win.AntsHomeRoomChat.mount({ doc: doc, win: win, dock: dock, chat: doc.getElementById('chat'), news: function (n) { var p = tabBtns.chat.pill; p.textContent = n > 99 ? '99+' : String(n); p.hidden = !n || (tab === 'chat' && phone()); } });
                if (roomChat) roomChat.sync();
            });
        }

        function wantPlayers() {                            // the players list (players.js, players.css), after the dock: it uses its socket
            if (askedPlayers || !dock) return;
            askedPlayers = true;
            lazy('home/players', function () { if (win.AntsHomePlayers) players = win.AntsHomePlayers.mount({ doc: doc, win: win, host: host, yard: main, quickHost: quickHost, games: gamesBtn, dock: dock, page: page }); if (players && dock && dock.directory) dock.directory(players.directory); wantMod(); meet(); inert(held); var c = players && players.root.querySelector ? players.root.querySelector('.pl-count') : null; if (c) mirror(c, tabBtns.players.pill); });
        }

        var alerts = null, askedAlerts = false, waitingOver = [];
        function openChat() {                               // an alert that is about the chat (a mention): the chat comes to the screen, the front page's own tab where the box has two
            closeMenu();
            if (screen !== 'home' && roomChat && roomChat.select) roomChat.select('global');
            if (phone()) pick('chat'); else if (dock) dock.open(true);
        }
        function joinInvite(row, back) { try { return !!games && !!games.joinInvite && !!games.joinInvite({ code: row.code, host: row.from, opener: back }); } catch (e) { return false; } }       // (the Games list's own Join, with its question, for the room of an invitation)
        function wantAlerts() {                             // the bell, its list of alerts and the toasts (alerts.js, alerts.css, with web/alert_core.js): after the first paint
            if (askedAlerts) return;
            askedAlerts = true;
            lazy('home/alerts', function () {
                if (!win.AntsHomeAlerts) return;
                alerts = win.AntsHomeAlerts.mount({ doc: doc, win: win, core: win.AntsAlertCore, ctl: { show: show, screen: function () { return screen; }, join: joinInvite, chat: openChat }, bar: right, menuBtn: menuHome, dock: function () { return dock; }, signedIn: function () { return !!(dock && dock.you && dock.you()); } });
                waitingOver.splice(0).forEach(function (d) { alerts.alert('over', d); });         // (the match that this tab came back from: told before the part was here)
            });
        }

        function wantSplit() {                              // the two drag handles (split.js, split.css): sizes kept in this browser, put on Home before the lists come
            lazy('home/split', function () { if (win.AntsHomeSplit) split = win.AntsHomeSplit.mount({ doc: doc, win: win, host: host, main: main }); });
        }

        var askedMod = false, askedSheet = false, mod = null;
        function meet() { if (mod && players && mod.people) mod.people(players); }       // (the sheet and the list, whichever is second)
        function loadMod() {
            if (askedSheet) return;
            askedSheet = true;
            lazy('home/mod', function () { if (win.AntsHomeMod) mod = win.AntsHomeMod.mount({ doc: doc, win: win, dock: dock, say: function (words, tone) { return !!alerts && alerts.say(words, tone) === true; } }); meet(); });
        }
        function wantSheet() { var y = dock && dock.you ? dock.you() : null; if ((y && y.role !== 'none') || (players && holds())) loadMod(); }       // (the sheet of a person: for staff, and for anybody who holds a room, so that they can invite; nobody else downloads it)
        function wantMod() {                                // the sheet (mod.js, mod.css): once, after the dock names a visitor with a role, or after the page holds a room
            if (askedMod || !dock || !dock.tap || !dock.you) return;
            askedMod = true;
            dock.tap(function (kind) { if (kind === 'live' || kind === 'you') wantSheet(); });
            wantSheet();
        }
        function wantTalk() {                               // the emoji and microphone buttons (talk.js, talk.css), after the dock: they stand in its form
            if (askedTalk || !dock) return;
            askedTalk = true;
            wantMod();
            if (win.AntsHomeTalk) { win.AntsHomeTalk.mount({ doc: doc, win: win, dock: dock }); return; }       // (the Room's chat brought the script already)
            lazy('home/talk', function () { if (win.AntsHomeTalk) win.AntsHomeTalk.mount({ doc: doc, win: win, dock: dock }); });
        }

        return { start: start, sync: sync, opening: say, joinInvite: joinInvite, openChat: openChat, alert: function (kind, data) { try { if (!alerts) { if (kind === 'over') waitingOver.push(data); return false; } return alerts.alert(kind, data); } catch (e) { return false; } }, show: show, screen: function () { return screen; }, inert: inert, menu: function () { return menuOpen; }, dock: function () { return dock; },
            roomChatSeen: function () { try { return roomChat ? roomChat.seen() : null; } catch (e) { return null; } },
            roomChatNews: function (text) { try { return !!roomChat && roomChat.news(text); } catch (e) { return false; } },
            tell: function (kind, text, tone, actions) { try { return !!games && games.told(kind, text, tone, actions) === true; } catch (e) { return false; } } }; // (the Room's Fog of War question)
    }

    return { HASH: HASH, TEXT: TEXT, MENU_MAIN: MENU_MAIN, MENU_SMALL: MENU_SMALL, fitHeight: fitHeight, heightGuard: heightGuard, readout: readout, screenOf: screenOf, firstScreen: firstScreen, mount: mount };
});
