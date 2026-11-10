// The two screens of the front page (web/lobby.html, docs/HOME_SCREEN.md): Home (a top bar over the games, the players and the chat) and the Room (the room page under a slim bar). One page, one WebSocket: the page's client, model and
// session storage are never touched here, so Home <-> Room is a class on <body> (data-screen, home.css) and one render(): no request, the seat is never dropped. Text and properties only, never markup. ES5:
// a page gets AntsHome, node gets module.exports (tests/scripts/web_home_screen_check.js).
//   * Home is "/", the Room "/#room" (pushState: Back goes to Home); a link ("?room=CODE") opens the Room; the page writes no history until a button is pressed.
//   * A plain visit makes no room (Quick host makes one; My room is there only while the page holds a room). Quick host and Games are lit by games.js, which loads after the first paint. START reaches a guest on Home as on the Room: Home says so.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHome = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var MENU_MIN_HEIGHT = 120, MENU_GAP = 10;            // px: the Menu keeps this much at least, and this much of the window below it
    var HASH = '#room';                                  // the address of the Room screen
    var STATE_KEY = 'ants';                              // history.state.ants: 'home' or 'room' (the entries that this script made or marked)
    var LOGO = { src: 'front/logo.png', width: 581, height: 218 };      // (the page's own picture: front/logo.png, the size of its file)
    var CHAT = { script: 'home/chat.js', style: 'home/chat.css' };       // the chat dock: loaded once, after the first paint, when Home is first shown
    var OPENING_MS = 15000, GIVE_UP_MS = 60000;          // a hand-off that is still here after the first has been slow: Home takes presses again, and says so; after the second the page says that it did not come
    var GAMES = 'home/games';                            // the Games list, Quick host and the Room's switch (games.js, games.css): loaded once, after the first paint
    var TEXT = {
        quickHost: 'Quick host', quickJoin: 'Quick join', mine: 'My room', games: 'Games', menu: 'Menu', login: 'Log in', home: 'Home', leave: 'Leave room', opening: 'Opening the game…',
        slow: 'The game is slow to open. Reload this page to get your seat back.', failed: 'The game did not open. Reload this page to get your seat back.',
        players: 'Players', chat: 'Chat', views: 'Home views'
    };
    // The Menu's links: the footer's and the header's (test_web_home_screen.py keeps them equal), and the page's How it works (no address)
    var MENU_MAIN = [['Matches', '/matches.html'], ['Live now', '/live.html'], ['Map maker', '/mapmaker.html'], ['How it works', ''], ['Discord server', 'https://discord.gg/WaWrXQnKPj']];
    var MENU_SMALL = [['Sprites and sounds', '/asset_catalog/'], ['Changelog', '/changelog.html'], ['GitHub', 'https://github.com/dchadd427/ants-cross-platform'], ['Feedback', 'https://github.com/dchadd427/ants-cross-platform/issues'], ['Legal', '/legal.html']];
    var TABS = [['games', 'Games'], ['players', 'Players'], ['chat', 'Chat']];      // the phone's strip (a window up to 700 px wide): one view at a time
    var PHONE = '(max-width: 700px)';

    // ---- the pure parts ----
    // "3/4": the colors that play (a person or a computer player) of the colors that are not out of the match, from the page's model; null before the room has told its plan
    function countOf(model) {
        if (!model || !model.slots || model.slots.length !== 4) return null;
        var people = 0, size = 0;
        for (var i = 0; i < 4; i++) {
            var kind = model.slots[i].kind;
            if (kind !== 'nobody') size++;
            if (kind === 'person' || kind === 'bot') people++;
        }
        return { people: people, size: size };
    }
    // The screen that an address and a history entry name: the entry that this script wrote (state.ants), else the hash. 'room' only when the page has a room to show.
    function screenOf(state, hash, hasRoom) {
        var named = state && typeof state === 'object' && (state[STATE_KEY] === 'room' || state[STATE_KEY] === 'home') ? state[STATE_KEY] : hash === HASH ? 'room' : 'home';
        return named === 'room' && hasRoom ? 'room' : 'home';
    }
    // The screen that a page opens on: a link (or a test room) opens the Room; a reload keeps the screen that the address names; else Home
    function firstScreen(link, hasSeat, hash) { return link ? 'room' : hasSeat && hash === HASH ? 'room' : 'home'; }

    // ---- the screens ----
    // mount({ doc, win, host (Home's div), lobby (the page's grid: the Room's bar goes before it), page: { info() -> { client, model, offline }, make(), leave(), how(), stalled(words) (optional), ... } })
    // returns { start, sync, opening, show, screen, inert, menu, dock }
    function mount(env) {
        var doc = env.doc, win = env.win, host = env.host, page = env.page;
        var screen = 'home', menuOpen = false, menuFor = null, menuBox = null, factsBox = null, going = false, held = false, stalled = false, goingTimer = null, failTimer = null, said = '', dock = null, asked = false, games = null, askedGames = false, players = null, askedPlayers = false;

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
        function logo() {                                   // the page's heading: the logo (its words are the picture's alt), in the bar of the screen that shows
            var h = el('h1', 'hbrand'), img = el('img', 'hlogo');
            img.src = LOGO.src; img.alt = 'Ants!'; img.setAttribute('width', String(LOGO.width)); img.setAttribute('height', String(LOGO.height));
            h.appendChild(img);
            return h;
        }
        function menuButton() {
            var b = button('hmenu', '', function () { toggleMenu(b); });
            b.setAttribute('aria-label', TEXT.menu); b.setAttribute('aria-expanded', 'false');
            b.appendChild(el('i', 'bars'));
            return b;
        }

        // Home: the top bar, the body and the card that says that the game is opening
        var bar = el('header', 'hbar');
        var quickHost = stub('quick host', TEXT.quickHost), quickJoin = stub('quick join', TEXT.quickJoin);
        var mine = button('hchip mine', '', function () { openRoom(); });
        var mineName = el('span', 'name', TEXT.mine), minePill = el('span', 'pill'); minePill.hidden = true;
        mine.appendChild(mineName); mine.appendChild(minePill); mine.hidden = true;       // (My room is there only while the page holds a room: sync() shows it)
        var gamesBtn = stub('hchip games', ''), gamesPill = el('span', 'pill');
        gamesPill.hidden = true; gamesBtn.appendChild(el('span', 'name', TEXT.games)); gamesBtn.appendChild(gamesPill);
        var right = el('div', 'hright'), login = button('hlogin', TEXT.login); login.hidden = true;       // (the login chip: the chat dock fills it and shows it where the hub offers a login)
        var menuHome = menuButton();
        right.appendChild(login); right.appendChild(menuHome);
        [logo(), quickHost, quickJoin, mine, gamesBtn, right].forEach(function (n) { bar.appendChild(n); });
        var main = el('main', 'hmain'); main.id = 'hmain';       // the games list and the players list mount here, side by side; the chat dock comes after it
        var tabs = el('nav', 'htabs'), tab = 'games', tabBtns = {}; tabs.setAttribute('aria-label', TEXT.views);
        TABS.forEach(function (t) {
            var b = button('htab t-' + t[0], '', function () { pick(t[0]); }), pill = el('span', 'pill'); pill.hidden = true;
            b.appendChild(el('span', 'name', t[1])); b.appendChild(pill); tabs.appendChild(b); tabBtns[t[0]] = { b: b, pill: pill };
        });
        var opening = el('div', 'hopen', TEXT.opening); opening.setAttribute('role', 'status'); opening.setAttribute('aria-live', 'polite'); opening.hidden = true;
        host.appendChild(bar); host.appendChild(main); host.appendChild(opening); host.appendChild(tabs); host.setAttribute('data-tab', tab);

        // The Room: the slim bar before the page's own room card
        var rbar = el('header', 'rbar');
        var homeBtn = button('rhome', '', function () { show('home', 'push'); });
        var chev = el('span', 'chev', '‹'); chev.setAttribute('aria-hidden', 'true');
        homeBtn.appendChild(chev); homeBtn.appendChild(el('span', '', TEXT.home));
        var leave = button('leave', TEXT.leave, function () { if (stalled) return; var go = function () { page.leave(); show('home', 'push'); }; if (!asking(go)) go(); });
        var menuRoom = menuButton();
        [homeBtn, logo(), el('span', 'grow'), leave, menuRoom].forEach(function (n) { rbar.appendChild(n); });
        env.lobby.parentNode.insertBefore(rbar, env.lobby);

        // ---- the Menu: one box for both bars ----
        function link(text, href, cls) {
            var a = el('a', cls, text);
            a.href = href;
            if (/^https?:/.test(href)) { a.setAttribute('target', '_blank'); a.setAttribute('rel', 'noopener noreferrer'); }
            a.addEventListener('click', closeMenu);
            return a;
        }
        function buildMenu() {
            menuBox = el('nav', 'hmenubox'); menuBox.setAttribute('aria-label', TEXT.menu);
            MENU_MAIN.forEach(function (m) {
                var item;
                if (m[1]) item = link(m[0], m[1], 'mitem');
                else { item = el('button', 'mitem', m[0]); item.type = 'button'; item.addEventListener('click', function () { var back = menuFor; closeMenu(); if (back && back.focus) back.focus(); page.how(); }); }      // (the page's list gives the focus back to what had it: the Menu's button)
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
            var r = (menuFor === menuHome ? bar : rbar).getBoundingClientRect(), top = Math.round(r.bottom + 6);
            menuBox.style.top = top + 'px';
            menuBox.style.maxHeight = Math.max(MENU_MIN_HEIGHT, (win.innerHeight || 0) - top - MENU_GAP) + 'px';
        }
        function openMenu(btn) {
            if (!menuBox) buildMenu();
            menuFor = btn; menuOpen = true;
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
        // A button makes a new history entry for the screen it goes to ("/" for Home, "/#room" for the Room), so that Back comes back. The entry that the page opened on is marked with its screen
        // the first time that we leave it, and its address stays. A browser that refuses (a sandboxed frame) keeps its address.
        function push(from, to) {
            try {
                var now = win.history.state;
                if (!(now && typeof now === 'object' && now[STATE_KEY])) win.history.replaceState(stateOf(from), '', path() + win.location.search + (from === 'room' ? win.location.hash : ''));      // ("/#room" in a tab with no seat is Home: its address says so)
                win.history.pushState(stateOf(to), '', path() + (to === 'room' ? HASH : ''));
            } catch (e) { /* the address stays */ }
        }
        // The phone's strip: Games, Players and Chat, one at a time (home.css shows it up to 700 px wide; a wider window shows all three and ignores it). The chat is open only on its own view, so what comes while it is away counts as unread.
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
            if (how === 'push') push(from, name);
            sync();
            if (name === 'home') wantDock();
            dockShown();
            gamesCall('shown', name);
            if (how !== 'first') {
                try { win.scrollTo(0, 0); } catch (e) { /* no scrolling here */ }
                var next = name === 'room' ? homeBtn : mine.hidden ? away() : mine;
                if (next.focus) next.focus();
            }
        }
        function openRoom() {                               // My room: the first press makes the room; it and every later press show it
            if (!page.info().client) page.make();
            show('room', 'push');
        }
        function away() { return !quickHost.disabled ? quickHost : !gamesBtn.disabled ? gamesBtn : menuHome; }       // (where the focus goes when My room is not there: the first button that takes a press)
        function sync() {
            var info = page.info(), n = info.client ? countOf(info.model) : null;
            mine.hidden = !info.client;
            if (mine.hidden && doc.activeElement === mine) away().focus();       // (the room ended under the focus: it goes to Quick host, not to nowhere)
            minePill.textContent = n ? n.people + '/' + n.size : '';
            minePill.hidden = !n;
            mine.setAttribute('aria-label', n ? TEXT.mine + ', ' + n.people + ' of ' + n.size : TEXT.mine);
            gamesCall('sync');
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
        function asking(go) { try { return !!games && !!games.leaving(go, leave); } catch (e) { return false; } }       // (the Games list asks before a room with other people in it is left; it says whether it asks)
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
            show(firstScreen(!!link, !!seat, win.location.hash), 'first');
            wantGames();
            win.addEventListener('popstate', function (e) { show(screenOf(e && e.state, win.location.hash, !!page.info().client), 'pop'); });
            win.addEventListener('resize', function () { if (menuOpen) placeMenu(); var ph = phone(); if (dock && ph !== was) dock.open(ph ? tab === 'chat' : (win.innerHeight || 0) >= 601); was = ph; });
        }
        // START reached this page: it is on its way to the game page, so both bars take no press, and Home says so
        function say() {
            going = true; closeMenu(); gamesCall('opening'); inert(held); if (screen === 'home') opening.hidden = false;
            if (goingTimer !== null) clearTimeout(goingTimer);
            if (failTimer !== null) clearTimeout(failTimer);
            failTimer = null;
            goingTimer = setTimeout(function () {                // not here yet: Home takes presses again, but the seat may still be the game's, so Leave room stays off, and the page says so
                goingTimer = null; going = false; stalled = true; opening.hidden = true; leave.disabled = true; leave.setAttribute('aria-disabled', 'true'); inert(held);
                tell(TEXT.slow);
                failTimer = setTimeout(function () { failTimer = null; tell(TEXT.failed); }, GIVE_UP_MS - OPENING_MS);
            }, OPENING_MS);
        }
        function tell(words) {                                   // the page's strip, and the Games list's buttons that wait for the game
            said = words;
            try { if (page.stalled) page.stalled(words); } catch (e) { /* what the page says is its own */ }
            gamesCall('stalled', words);
        }
        function inert(on) { held = !!on; [bar, rbar, tabs, dock && dock.root, players && players.root].forEach(function (n) { if (n) { if (held || going) n.setAttribute('inert', ''); else n.removeAttribute('inert'); } }); gamesCall('inert', held || going); }      // (a page that is on its way to the game takes no press again, whatever a dialog that closes says)
        // The chat dock (chat.js, chat.css) mounts into Home's div and is told which screen shows
        function dockShown() { try { if (dock) dock.shown(screen === 'home'); } catch (e) { /* the dock is its own */ } }
        function wantDock() {
            if (asked) return;
            asked = true;
            var parts = 2;
            function mountDock() { if (--parts) return; try { if (win.AntsHomeChat) { dock = win.AntsHomeChat.mount({ doc: doc, win: win, host: host, chip: login, screen: function () { return screen; } }); inert(held); dockShown(); pick(tab); wantPlayers(); } } catch (e) { dock = null; } }
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

        function wantPlayers() {                            // the players list (players.js, players.css), after the dock: it uses its socket
            if (askedPlayers || !dock) return;
            askedPlayers = true;
            lazy('home/players', function () { if (win.AntsHomePlayers) players = win.AntsHomePlayers.mount({ doc: doc, win: win, host: host, yard: main, quickHost: quickHost, games: gamesBtn, dock: dock, page: page }); inert(held); var c = players && players.root.querySelector ? players.root.querySelector('.pl-count') : null; if (c) mirror(c, tabBtns.players.pill); });
        }

        return { start: start, sync: sync, opening: say, show: show, screen: function () { return screen; }, inert: inert, menu: function () { return menuOpen; }, dock: function () { return dock; } };
    }

    return { HASH: HASH, TEXT: TEXT, MENU_MAIN: MENU_MAIN, MENU_SMALL: MENU_SMALL, countOf: countOf, screenOf: screenOf, firstScreen: firstScreen, mount: mount };
});
