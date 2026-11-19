/* The Menu and the live count of the bar at the top of every page (web/site_nav.css; the same links, in the same order, as the Menu of the front page, web/home/home.js: a test keeps the two lists
   the same). The bar's markup is in each page; this file builds the Menu from the bar's own <nav> and the small links below, and asks /live for the count of live matches: when the page opens
   (data-live="now"), or when the Menu is first opened (data-live="menu"), or never, and again when the tab comes back to the front. The last line of the Menu is the stamped version and build
   of the site (data-version, data-build, data-built on the bar, text only; the build stamps them in; a page nobody stamped shows no site line) and, on the map maker, its own version (data-maker). */
(function () {
    'use strict';
    var d = document, bar = d.querySelector('.sb'), btn = bar && bar.querySelector('.sbtn');
    if (!btn) return;
    var SMALL = [['Sprites and sounds', '/asset_catalog/'], ['Changelog', '/changelog'], ['GitHub', 'https://github.com/dchadd427/ants-cross-platform'], ['Feedback', 'https://github.com/dchadd427/ants-cross-platform/issues'], ['Legal', '/legal']];
    var box = null, count = 0, asked = false, openWidth = 0;
    function make(tag, cls, text) { var n = d.createElement(tag); if (cls) n.className = cls; if (text) n.textContent = text; return n; }
    function item(text, href, cur) {
        var a = make('a', '', text);
        a.href = href;
        if (cur) a.setAttribute('aria-current', 'page');
        if (/^https?:/.test(href)) { a.target = '_blank'; a.rel = 'noopener noreferrer'; }
        return a;
    }
    function stamp() {
        function got(k) { var v = bar.getAttribute(k) || ''; return v && v.indexOf('@') < 0 ? v : ''; }
        var v = got('data-version'), b = got('data-build'), t = got('data-built'), m = got('data-maker'), out = [];
        if (v) out.push('Version ' + v + (b ? ' - build ' + b + (t ? ' (' + t + ')' : '') : ''));
        if (m) out.push('Map maker ' + m);
        return out.join(' - ');
    }
    function paint() {
        var top = bar.querySelector('.sblive');
        if (!top) return;
        top.textContent = count + ' live';
        top.setAttribute('aria-label', count + ' live: see them on the front page');
        top.setAttribute('title', count + (count === 1 ? ' match is' : ' matches are') + ' being played - see them on the front page');
        top.hidden = count < 1;
    }
    function look(again) {
        if ((asked && !again) || typeof fetch !== 'function') return;
        asked = true;
        fetch('/live', { cache: 'no-store', credentials: 'omit' }).then(function (r) { return r.ok ? r.json() : null; }).then(function (data) {
            count = data && Array.isArray(data.live) ? Math.min(data.live.length, 99) : 0;
            paint();
        }).catch(function () { if (again) { count = 0; paint(); } });
    }
    function build() {
        box = make('nav', 'sbm'); box.id = 'sbm'; box.setAttribute('aria-label', 'Menu'); box.hidden = true;
        [].forEach.call(bar.querySelectorAll('nav a'), function (a) {
            var t = a.getAttribute('aria-label') || (a.querySelector('span') || a).textContent, m = item(t, a.getAttribute('href'), a.hasAttribute('aria-current'));
            box.appendChild(m);
        });
        var small = make('p', 'sbsmall');
        SMALL.forEach(function (s) { small.appendChild(item(s[0], s[1], d.location.pathname.indexOf(s[1]) === 0)); });
        box.appendChild(small);
        if (stamp()) box.appendChild(make('p', 'sbver', stamp()));
        d.body.appendChild(box);
        paint();
    }
    function fit() {
        var top = Math.round(bar.getBoundingClientRect().bottom + 6);
        box.style.top = top + 'px';
        box.style.maxHeight = Math.max(160, (window.innerHeight || 0) - top - 10) + 'px';
    }
    function open() {
        if (!box) build();
        if (bar.getAttribute('data-live') === 'menu') look();
        fit();
        box.hidden = false;
        openWidth = window.innerWidth;
        btn.setAttribute('aria-expanded', 'true');
        box.firstChild.focus();
    }
    function close(back) {
        if (!box || box.hidden) return;
        box.hidden = true;
        btn.setAttribute('aria-expanded', 'false');
        if (back) btn.focus();
    }
    // A tab that the front page opened only because its person holds a room (Watch, Matches; docs/SITE_NAV.md): its Home closes the tab and leaves the front page's tab as it was. The page asks on the channel when it
    // opens (heard) and again at the press of Home; a tab keeps its key in the session storage, so that the pages that its links lead to ask too.
    var CHANNEL = 'ants-watch', SIDE = 'ants.side', ASK_MS = 300, CLOSE_MS = 400, talk = null, token = '', then = null, heard = false, waiting = false;
    function kept(set) { try { if (set === undefined) return window.sessionStorage.getItem(SIDE) || ''; if (set) window.sessionStorage.setItem(SIDE, set); else window.sessionStorage.removeItem(SIDE); } catch (e) { /* none */ } return ''; }
    function sideKey() {
        var at = window.location || {}, live = /(?:^|[?&])live=([^&#]{1,80})/.exec(at.search || ''), own = at.pathname === '/matches' ? 'matches' : at.pathname === '/play' && live ? live[1] : '';
        return kept() || (window.history && window.history.length === 1 ? own : '');       // (a page is such a tab by its own address only in a tab that began on it: a page that a tab went on to is told by the key that the tab keeps)
    }
    function ask(on) { token = String(Math.random()).slice(2); then = on; try { talk.postMessage({ t: 'back', side: sideKey(), token: token }); return true; } catch (e) { return false; } }
    if (typeof window.BroadcastChannel === 'function' && sideKey()) {
        try {
            talk = new window.BroadcastChannel(CHANNEL);
            talk.onmessage = function (m) { var x = m && m.data, on = then; if (on && x && x.t === 'here' && x.token === token) { then = null; on(); } };
            ask(function () { heard = true; kept(sideKey()); });
        } catch (e) { talk = null; }
    }
    function homeBack() {                      // true: the press is taken
        if (!talk || !heard) return false;
        if (waiting) return true;
        var timer, plain = function () { waiting = false; then = null; kept(''); window.location.assign('/'); };
        waiting = true;
        if (!ask(function () { clearTimeout(timer); try { window.close(); } catch (e) { /* it goes on as a link */ } setTimeout(plain, CLOSE_MS); })) plain();
        else timer = setTimeout(plain, ASK_MS);
        return true;
    }
    d.addEventListener('click', function (e) {
        var a = e.target && e.target.closest ? e.target.closest('a') : null;
        if (a && a.getAttribute('href') === '/' && (bar.contains(a) || (box && box.contains(a))) && !e.ctrlKey && !e.metaKey && !e.shiftKey && !e.altKey && !e.button && homeBack()) e.preventDefault();
    }, true);
    btn.addEventListener('click', function () { if (box && !box.hidden) close(true); else open(); });
    d.addEventListener('keydown', function (e) { if (e.key === 'Escape') close(true); });
    d.addEventListener('click', function (e) { if (box && !box.hidden && !box.contains(e.target) && !btn.contains(e.target)) close(false); });
    window.addEventListener('resize', function () {      // (a new width or a turn closes it; a change of height alone, as a phone's address bar makes, or the game page's own resize, only fits it to the window again)
        if (box && !box.hidden) { if (window.innerWidth !== openWidth) close(false); else fit(); }
    });
    d.addEventListener('visibilitychange', function () { if (asked && d.visibilityState === 'visible') look(true); });
    box = null;
    if (bar.getAttribute('data-live') === 'now') look();
})();
