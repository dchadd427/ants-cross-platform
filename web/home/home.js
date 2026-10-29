// The two screens of the front page (web/lobby.html, docs/HOME_SCREEN.md): Home, the top bar over a stand-in picture of the Yard, and the Room, today's room page under a slim bar. It is one page and one
// WebSocket: the page's client (web/front/lobby_net.js), its model and its session storage are the page's own and never touched here, so going from Home to the Room and back is a class on <body>
// (data-screen="home" or "room", the style is web/home/home.css) and one render() of the page: instant, no network, the seat is never dropped. Everything on Home is made here, and only as text and
// properties (never markup). The Quick host, Quick join and Games buttons are drawn as the approved pictures draw them and stay disabled until the pieces that light them are in.
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsHome, node gets module.exports (tests/scripts/web_home_screen_check.js runs it with a fake of the DOM).
//   * The address: Home is "/", the Room "/#room" (history.pushState: Back goes from the Room to Home); a link ("?room=CODE") opens the Room directly, a reload keeps the screen. The page writes
//     nothing to the history until a button is pressed.
//   * A plain visit makes no room: "My room" makes a private one the first time it is pressed (the page's make()); until then the button says only "My room".
//   * The page keeps taking the room's words while Home is up: START from the host reaches a guest on Home as on the Room (the page writes the key and goes to the game page) and Home says so at once.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHome = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var HASH = '#room';                                  // the address of the Room screen
    var STATE_KEY = 'ants';                              // history.state.ants: 'home' or 'room' (the entries that this script made or marked)
    var LOGO = { src: 'front/logo.png', width: 581, height: 218 };      // (the page's own picture: front/logo.png, the size of its file)
    var TEXT = {
        quickHost: 'Quick host', quickJoin: 'Quick join', mine: 'My room', games: 'Games', menu: 'Menu', login: 'Log in', home: 'Home', leave: 'Leave room', opening: 'Opening the game…',
        yardTitle: 'The Yard will live here', yardLine: 'Stand-in picture. The real Yard is a big shared map: your 3 ants appear in it as soon as the page opens. It fills everything under the top bar.',
        yourAnts: 'Your 3 ants'
    };
    // The links of the Menu: the footer's own (web/lobby.html), the ones of the header (Matches, Map maker, Discord) and the page's How it works. tests/scripts/test_web_home_screen.py keeps them equal to the footer's.
    var MENU_MAIN = [['Matches', '/matches.html'], ['Live now', '/live.html'], ['Map maker', '/mapmaker.html'], ['How it works', ''], ['Discord server', 'https://discord.gg/WaWrXQnKPj']];
    var MENU_SMALL = [['Sprites and sounds', '/asset_catalog/'], ['Changelog', '/changelog.html'], ['GitHub', 'https://github.com/dchadd427/ants-cross-platform'], ['Feedback', 'https://github.com/dchadd427/ants-cross-platform/issues'], ['Legal', '/legal.html']];
    var YARD_ANTS = ['', '', '', 'red', 'blue'];                  // the stand-in's five ants (their places are the style's): the first three are yours

    // ---- the pure parts -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
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
    function reservedText(width, height) { return 'reserved: ' + Math.round(width) + ' x ' + Math.round(height) + ' px (everything under the top bar)'; }

    // ---- the screens -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // mount({ doc, win, host (the div of the page that holds Home), lobby (the page's grid: the Room's bar goes before it), page }) with page: { info() -> { client: is there a room, model, offline },
    // make() (a room of this page's own: a new code), leave() (leave the room: no client after it), how() (open the page's How it works) } returns { start, sync, opening, show, screen, inert }
    function mount(env) {
        var doc = env.doc, win = env.win, host = env.host, page = env.page;
        var screen = 'home', menuOpen = false, menuFor = null, menuBox = null, factsBox = null;

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
        function logo() {
            var img = el('img', 'hlogo');
            img.src = LOGO.src; img.alt = 'Ants!'; img.setAttribute('width', String(LOGO.width)); img.setAttribute('height', String(LOGO.height));
            return img;
        }
        function menuButton() {
            var b = button('hmenu', '', function () { toggleMenu(b); });
            b.setAttribute('aria-label', TEXT.menu); b.setAttribute('aria-haspopup', 'true'); b.setAttribute('aria-expanded', 'false');
            b.appendChild(el('i', 'bars'));
            return b;
        }

        // Home: the top bar, the Yard's stand-in and the card that says that the game is opening
        var bar = el('header', 'hbar');
        var quickHost = stub('quick host', TEXT.quickHost), quickJoin = stub('quick join', TEXT.quickJoin);
        var mine = button('hchip mine', '', function () { openRoom(); });
        var mineName = el('span', 'name', TEXT.mine), minePill = el('span', 'pill'); minePill.hidden = true;
        mine.appendChild(mineName); mine.appendChild(minePill);
        var games = stub('hchip games', TEXT.games);
        var right = el('div', 'hright'), login = button('hlogin', TEXT.login); login.hidden = true;       // (the login chip: a piece that comes later, never shown before it)
        var menuHome = menuButton();
        right.appendChild(login); right.appendChild(menuHome);
        [logo(), quickHost, quickJoin, mine, games, right].forEach(function (n) { bar.appendChild(n); });
        var yard = el('main', 'yard'); yard.id = 'yard';
        var label = el('div', 'ylabel'), reserved = el('p', 'yres');
        label.appendChild(el('h2', 'ytitle', TEXT.yardTitle)); label.appendChild(el('p', 'yline', TEXT.yardLine)); label.appendChild(reserved);
        yard.appendChild(label);
        var tag = el('span', 'ytag', TEXT.yourAnts); yard.appendChild(tag);
        YARD_ANTS.forEach(function (team, i) {
            var spot = el('div', 'yant y' + (i + 1)), ant = el('span', 'ant' + (team ? ' ant-' + team : ''));
            spot.setAttribute('aria-hidden', 'true');
            spot.appendChild(ant); yard.appendChild(spot);
        });
        var opening = el('div', 'hopen', TEXT.opening); opening.setAttribute('role', 'status'); opening.setAttribute('aria-live', 'polite'); opening.hidden = true;
        host.appendChild(bar); host.appendChild(yard); host.appendChild(opening);

        // The Room: the slim bar before the page's own room card
        var rbar = el('header', 'rbar');
        var homeBtn = button('rhome', '', function () { show('home', 'push'); });
        homeBtn.appendChild(el('span', 'chev', '‹')); homeBtn.appendChild(el('span', '', TEXT.home));
        var leave = button('leave', TEXT.leave, function () { page.leave(); show('home', 'push'); });
        var menuRoom = menuButton();
        [homeBtn, logo(), el('span', 'grow'), leave, menuRoom].forEach(function (n) { rbar.appendChild(n); });
        env.lobby.parentNode.insertBefore(rbar, env.lobby);

        // ---- the Menu: one box for both bars --------------------------------------------------------------------------------------------------------------------------------------------------------
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
                else { item = el('button', 'mitem', m[0]); item.type = 'button'; item.addEventListener('click', function () { closeMenu(); page.how(); }); }
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
        function openMenu(btn) {
            if (!menuBox) buildMenu();
            menuFor = btn; menuOpen = true;
            var lines = facts();
            factsBox.textContent = lines.join('\n'); factsBox.hidden = !lines.length;
            var r = (btn === menuHome ? bar : rbar).getBoundingClientRect();         // (under the whole bar: on a phone the bar has three rows)
            menuBox.style.top = Math.round(r.bottom + 6) + 'px';
            menuBox.hidden = false;
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

        // ---- the screens and the address --------------------------------------------------------------------------------------------------------------------------------------------------------
        function path() { return win.location.pathname.replace(/^\/+/, '/'); }
        function stateOf(name) { var state = {}; state[STATE_KEY] = name; return state; }
        // A button makes a new history entry for the screen that it goes to ("/" for Home, "/#room" for the Room), so that Back comes back. The entry that the page opened on has no mark of ours (a link
        // opens the Room at "/?room=CODE"): it is marked with the screen it is the first time that we leave it, and its address stays. A browser that refuses (a sandboxed frame) keeps its address.
        function push(from, to) {
            try {
                var now = win.history.state;
                if (!(now && typeof now === 'object' && now[STATE_KEY])) win.history.replaceState(stateOf(from), '', path() + win.location.search + win.location.hash);
                win.history.pushState(stateOf(to), '', path() + (to === 'room' ? HASH : ''));
            } catch (e) { /* the address stays */ }
        }
        function fit() {                                    // what the Yard has: the page under the bar, as the picture prints it
            var r = host.getBoundingClientRect(), b = bar.getBoundingClientRect();
            if (r.width > 0 && r.height > 0) reserved.textContent = reservedText(r.width, r.height - b.height);
        }
        // how: 'push' (a button: a new entry), 'pop' (the browser moved: no entry), 'first' (the page's first look: none either)
        function show(name, how) {
            closeMenu();
            var from = screen;
            screen = name;
            doc.body.setAttribute('data-screen', name);
            if (how === 'push') push(from, name);
            sync();
            if (how !== 'first') {
                try { win.scrollTo(0, 0); } catch (e) { /* no scrolling here */ }
                var next = name === 'room' ? homeBtn : mine;
                if (next.focus) next.focus();
            }
            fit();
        }
        function openRoom() {                               // My room: the first press makes the room; it and every later press show it
            if (!page.info().client) page.make();
            show('room', 'push');
        }
        function sync() {
            var info = page.info(), n = info.client ? countOf(info.model) : null;
            minePill.textContent = n ? n.people + '/' + n.size : '';
            minePill.hidden = !n;
            mine.setAttribute('aria-label', n ? TEXT.mine + ', ' + n.people + ' of ' + n.size : TEXT.mine);
        }
        // The page decided what it connects to: link (a link, or a test room, opens the Room), seat (it holds a room of this tab). Called once, before the page connects.
        function start(link, seat) {
            show(firstScreen(!!link, !!seat, win.location.hash), 'first');
            win.addEventListener('popstate', function (e) { show(screenOf(e && e.state, win.location.hash, !!page.info().client), 'pop'); });
            win.addEventListener('resize', fit);
        }
        // START reached this page while Home was up: the page is on its way to the game page, and Home says so
        function say() { if (screen === 'home') opening.hidden = false; }
        function inert(on) { [bar, rbar].forEach(function (n) { if (on) n.setAttribute('inert', ''); else n.removeAttribute('inert'); }); }

        return { start: start, sync: sync, opening: say, show: show, screen: function () { return screen; }, inert: inert, menu: function () { return menuOpen; } };
    }

    return { HASH: HASH, TEXT: TEXT, MENU_MAIN: MENU_MAIN, MENU_SMALL: MENU_SMALL, countOf: countOf, screenOf: screenOf, firstScreen: firstScreen, reservedText: reservedText, mount: mount };
});
