/* The Menu and the live count of the bar at the top of every page (web/site_nav.css; the same links, in the same order, as the Menu of the front page, web/home/home.js: a test keeps the two lists
   the same). The bar's markup is in each page; this file builds the Menu from the bar's own <nav> and the small links below, and asks /live for the count of live matches: when the page opens
   (data-live="now"), or when the Menu is first opened (data-live="menu"), or never (the live page itself). The last line of the Menu is the stamped version and build
   of the site (data-version, data-build, data-built on the bar, text only; the build stamps them in; a page nobody stamped shows no site line) and, on the map maker, its own version (data-maker). */
(function () {
    'use strict';
    var d = document, bar = d.querySelector('.sb'), btn = bar && bar.querySelector('.sbtn');
    if (!btn) return;
    var SMALL = [['Sprites and sounds', '/asset_catalog/'], ['Changelog', '/changelog'], ['GitHub', 'https://github.com/dchadd427/ants-cross-platform'], ['Feedback', 'https://github.com/dchadd427/ants-cross-platform/issues'], ['Legal', '/legal']];
    var box = null, count = 0, asked = false;
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
        var n = count > 0 ? String(count) : '', lv = bar.querySelector('.sblv'), top = bar.querySelector('.sblive'), inbox = box && box.querySelector('.sbn');
        if (lv) { lv.querySelector('.sbn').textContent = n; lv.classList.toggle('on', count > 0); }
        if (top) { top.textContent = n + ' live'; top.hidden = count < 1; }
        if (inbox) inbox.textContent = n;
    }
    function look() {
        if (asked || typeof fetch !== 'function') return;
        asked = true;
        fetch('/live', { cache: 'no-store', credentials: 'omit' }).then(function (r) { return r.ok ? r.json() : null; }).then(function (data) {
            count = data && Array.isArray(data.live) ? Math.min(data.live.length, 99) : 0;
            paint();
        }).catch(function () { /* no count */ });
    }
    function build() {
        box = make('nav', 'sbm'); box.id = 'sbm'; box.setAttribute('aria-label', 'Menu'); box.hidden = true;
        [].forEach.call(bar.querySelectorAll('nav a'), function (a) {
            var t = a.getAttribute('aria-label') || (a.querySelector('span') || a).textContent, m = item(t, a.getAttribute('href'), a.hasAttribute('aria-current'));
            if (a.classList.contains('sblv')) m.appendChild(make('span', 'sbn'));
            box.appendChild(m);
        });
        var small = make('p', 'sbsmall');
        SMALL.forEach(function (s) { small.appendChild(item(s[0], s[1], d.location.pathname.indexOf(s[1]) === 0)); });
        box.appendChild(small);
        if (stamp()) box.appendChild(make('p', 'sbver', stamp()));
        d.body.appendChild(box);
        paint();
    }
    function open() {
        if (!box) build();
        if (bar.getAttribute('data-live') === 'menu') look();
        var top = Math.round(bar.getBoundingClientRect().bottom + 6);
        box.style.top = top + 'px';
        box.style.maxHeight = Math.max(160, (window.innerHeight || 0) - top - 10) + 'px';
        box.hidden = false;
        btn.setAttribute('aria-expanded', 'true');
        box.firstChild.focus();
    }
    function close(back) {
        if (!box || box.hidden) return;
        box.hidden = true;
        btn.setAttribute('aria-expanded', 'false');
        if (back) btn.focus();
    }
    btn.addEventListener('click', function () { if (box && !box.hidden) close(true); else open(); });
    d.addEventListener('keydown', function (e) { if (e.key === 'Escape') close(true); });
    d.addEventListener('click', function (e) { if (box && !box.hidden && !box.contains(e.target) && !btn.contains(e.target)) close(false); });
    window.addEventListener('resize', function () { close(false); });
    box = null;
    if (bar.getAttribute('data-live') === 'now') look();
})();
