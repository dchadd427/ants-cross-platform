/* The update notice and the "What's new" window (docs/SITE_NAV.md "The update notice"; update_notice.css has the look). One script for every page that is stamped with the site's version: it learns the version that is
   live from /whats_new.json (made when the site is built, from CHANGELOG.md: only the lines written for players), and
     - the strip: when that version is newer than this page's, a strip at the top says so, with Reload and Later; Later hides it in this tab until a newer version comes;
     - the window: on a page that is loaded again on a newer version than the one this browser last saw, a window lists what changed since then (newest five; the newest open, the others folded; All changes opens
       the changelog); a first visit shows nothing. The version that was seen is kept in this browser's own storage and goes nowhere.
   The page may tell the script when it must show NOTHING (a match is loaded or played): AntsUpdate.page({ quiet: fn, moved: fn }); the strip waits until quiet() says no, and goes while a match runs. moved() is
   called when the strip comes or goes (the game page lays its picture out again). A page with data-window="no" on the script tag never opens the window (the game page, the admin page).
   One small request, with revalidation: 3 seconds after the page opens (at once when the window may come), then every 5 minutes while the tab is in front, and when it comes back after a minute away; none while it is
   hidden; a failed request is silent. Nothing is shown in a frame. Text only (textContent), no inline style: the admin page allows none. Plain ES5. */
(function () {
    'use strict';
    var w = window, d = document, me = d.currentScript;
    try { if (w.top !== w.self) return; } catch (e) { return; }
    if (typeof w.fetch !== 'function') return;
    var FILE = '/whats_new.json', FIRST_MS = 3000, SOON_MS = 600, EVERY_MS = 300000, BACK_MS = 60000, HOLD_MS = 1000, SHOWN = 5;
    var SEEN = 'ants.seenVersion', LATER = 'ants.updateLater', MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    var canWindow = !(me && me.getAttribute('data-window') === 'no');
    var own = '', newer = '', later = '', strip = null, timer = 0, holding = 0, last = 0, tried = false, quiets = [], movers = [];

    // ---- versions and storage ----------------------------------------------------------------------------------------
    function parts(v) { var m = /^v?(\d+)\.(\d+)\.(\d+)$/.exec(String(v === undefined || v === null ? '' : v).replace(/^\s+|\s+$/g, '')); return m ? [+m[1], +m[2], +m[3]] : null; }
    function cmp(a, b) { var x = parts(a), y = parts(b), i; if (!x || !y) return 0; for (i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1; return 0; }
    function ownVersion() {         // the bar's stamp, or the front page's footer; a page that nobody stamped has none (the placeholder of a source page is no version)
        var bar = d.querySelector('header.sb[data-version]'), foot = d.getElementById('game-version'), v = bar ? bar.getAttribute('data-version') : foot ? foot.textContent : '';
        return parts(v) ? String(v).replace(/^\s*v?|\s+$/g, '') : '';
    }
    function area(kind) { try { return w[kind + 'Storage']; } catch (e) { return null; } }
    function get(kind, k) { try { return area(kind).getItem(k); } catch (e) { return null; } }
    function put(kind, k, v) { try { area(kind).setItem(k, v); } catch (e) { /* kept nowhere: the window does not come either */ } }
    function now() { return (w.Date || Date).now(); }

    // ---- the file ----------------------------------------------------------------------------------------------------
    function valid(x) { return !!x && typeof x === 'object' && !!parts(x.version) && Object.prototype.toString.call(x.releases) === '[object Array]'; }
    function load(then) {
        var done = false;
        function end(x) { if (!done) { done = true; then(valid(x) ? x : null); } }
        try {
            w.fetch(FILE, { cache: 'no-cache', credentials: 'omit' }).then(function (r) { return r && r.ok ? r.text() : ''; }).then(function (t) {
                var x = null;
                try { x = JSON.parse(t); } catch (e) { /* a page of HTML, or nothing: not ours */ }
                end(x);
            }, function () { end(null); });
        } catch (e) { end(null); }
    }

    function el(tag, cls, text) { var e = d.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
    function button(text, cls, fn) { var b = el('button', 'u-b' + (cls ? ' ' + cls : ''), text); b.type = 'button'; b.addEventListener('click', fn); return b; }
    function each(list, fn) { for (var i = 0; i < list.length; i++) { try { fn(list[i]); } catch (e) { /* a page's hook that fails does not stop the notice */ } } }

    // ---- the strip -----------------------------------------------------------------------------------------------------
    function quiet() { var out = false; each(quiets, function (q) { if (q()) out = true; }); return out; }
    function mount(node) {              // the front page's strips (its own messages are there), else right under the bar
        var strips = d.querySelector('.strips'), bar = d.querySelector('header.sb');
        if (strips) strips.insertBefore(node, strips.firstChild);
        else if (bar && bar.parentNode) bar.parentNode.insertBefore(node, bar.nextSibling);
        else return false;
        return true;
    }
    function build() {
        var box = el('div', 'upd upd-strip'), tx = el('div', 'u-tx'), ac = el('div', 'u-ac');
        box.setAttribute('role', 'status');
        tx.appendChild(el('p', 'u-tt', 'A new version of Ants is out.'));
        tx.appendChild(el('p', 'u-nn u-room', 'Reloading keeps your seat in this room.'));          // (shown by the stylesheet in a room only)
        ac.appendChild(button('Reload', 'u-go', function () { try { w.location.reload(); } catch (e) { /* none */ } }));
        ac.appendChild(button('Later', '', function () { later = newer; put('session', LATER, newer); render(); }));
        box.appendChild(tx);
        box.appendChild(ac);
        return box;
    }
    function render() {
        var show = !!newer && later !== newer && get('session', LATER) !== newer && !quiet(), changed = false, node;
        if (show && !strip) { node = build(); if (mount(node)) { strip = node; changed = true; } }
        else if (!show && strip) { if (strip.parentNode) strip.parentNode.removeChild(strip); strip = null; changed = true; }
        if (changed) each(movers, function (m) { m(); });
        if (newer && quiets.length && !holding) holding = w.setInterval(render, HOLD_MS);            // (while a match runs the strip is held back; it comes when the page says that the match is over)
        else if ((!newer || !quiets.length) && holding) { w.clearInterval(holding); holding = 0; }
    }

    // ---- the window ----------------------------------------------------------------------------------------------------
    function dateText(s) { var m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(s || ''); return m && +m[2] >= 1 && +m[2] <= 12 ? MONTHS[+m[2] - 1] + ' ' + (+m[3]) : ''; }
    function fill(li, segs) {
        for (var i = 0; i < segs.length; i++) {
            if (typeof array(segs[i])[1] !== 'string') continue;
            li.appendChild(segs[i][0] === 'b' ? el('b', '', segs[i][1]) : d.createTextNode(segs[i][1]));
        }
    }
    function array(x) { return Object.prototype.toString.call(x) === '[object Array]' ? x : []; }
    function lines(rel) { return array(rel.items).concat(array(rel.fixes)); }
    function lead(rel) {                // a folded release says what its first line says, and how many more it has
        var all = lines(rel), s = array(all[0]), t = '', i;
        for (i = 0; i < s.length; i++) if (array(s[i])[0] === 'b' && typeof s[i][1] === 'string') { t = s[i][1].replace(/[.:]\s*$/, ''); break; }
        if (!t && s.length && typeof array(s[0])[1] === 'string') t = s[0][1].slice(0, 60);
        return t + (all.length > 1 ? ' + ' + (all.length - 1) + ' more' : '');
    }
    function bullets(box, items) {
        var ul = el('ul'), i, li;
        for (i = 0; i < items.length; i++) { li = el('li'); fill(li, array(items[i])); ul.appendChild(li); }
        box.appendChild(ul);
    }
    function release(rel, open) {
        var sec = el(open ? 'section' : 'details', 'u-rel' + (open ? '' : ' u-fold')), head = el(open ? 'div' : 'summary', 'u-rh');
        head.appendChild(el('span', 'u-rv', 'v' + rel.version));
        if (!open) head.appendChild(el('span', 'u-rt', lead(rel)));
        if (dateText(rel.date)) head.appendChild(el('span', 'u-rd', dateText(rel.date)));
        sec.appendChild(head);
        if (array(rel.items).length) bullets(sec, rel.items);
        if (array(rel.fixes).length) { sec.appendChild(el('p', 'u-fx', 'Fixes')); bullets(sec, rel.fixes); }
        return sec;
    }
    function focusables(box) { return [].slice.call(box.querySelectorAll('summary, a, button, .u-wb')).filter(function (n) { return n.offsetParent !== null; }); }
    function openWindow(shown, since, more) {
        var wrap = el('div', 'upd upd-win'), box = el('div', 'upd-box'), head = el('div', 'u-wh'), body = el('div', 'u-wb'), foot = el('div', 'u-wf'), h = el('h2', '', 'What’s new in Ants'), back = d.activeElement, close, down = false, i, a;
        h.id = 'upd-win-title';
        box.setAttribute('role', 'dialog'); box.setAttribute('aria-modal', 'true'); box.setAttribute('aria-labelledby', h.id);
        head.appendChild(h);
        head.appendChild(el('p', 'u-nn', 'Changes since v' + since + (more ? ', newest ' + shown.length + ' shown' : '')));
        body.setAttribute('tabindex', '0');
        for (i = 0; i < shown.length; i++) body.appendChild(release(shown[i], i === 0));
        function shut() {
            d.removeEventListener('keydown', onKey, true);
            if (wrap.parentNode) wrap.parentNode.removeChild(wrap);
            try { if (back && back.focus && d.body.contains(back)) back.focus(); } catch (e) { /* none */ }
        }
        function onKey(e) {
            var all, at;
            if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); shut(); }
            else if (e.key === 'Tab') {
                all = focusables(box); at = all.indexOf(d.activeElement);
                if (!all.length) return;
                if (e.shiftKey && at <= 0) { e.preventDefault(); all[all.length - 1].focus(); } else if (!e.shiftKey && at === all.length - 1) { e.preventDefault(); all[0].focus(); }
            }
        }
        if (more) { a = el('a', '', 'All changes'); a.setAttribute('href', '/changelog'); a.setAttribute('target', '_blank'); a.setAttribute('rel', 'noopener noreferrer'); foot.appendChild(a); }
        close = button('Close', 'u-go', shut);
        foot.appendChild(close);
        box.appendChild(head); box.appendChild(body); box.appendChild(foot); wrap.appendChild(box);
        wrap.addEventListener('pointerdown', function (e) { down = e.target === wrap; });          // (a press that starts and ends on the dimmed page around the box closes it, as every window of the site does)
        wrap.addEventListener('click', function (e) { var hit = down && e.target === wrap; down = false; if (hit) shut(); });
        d.addEventListener('keydown', onKey, true);
        d.body.appendChild(wrap);
        close.focus();
    }
    function whatsNew(data) {           // once per page: what changed since the version that this browser saw, written down at once so that no page shows it twice
        var seen = get('local', SEEN), news = [], i, more;
        if (!canWindow) return;
        if (!parts(seen)) { put('local', SEEN, own); return; }                       // a first visit: nothing to say
        if (cmp(own, seen) <= 0) return;                                           // the same version, or an older page in another tab
        put('local', SEEN, own);
        if (get('local', SEEN) !== own) return;                                    // storage that keeps nothing: the window would come at every load
        for (i = 0; i < data.releases.length; i++) { var r = data.releases[i]; if (r && parts(r.version) && cmp(r.version, seen) > 0 && cmp(r.version, own) <= 0 && lines(r).length) news.push(r); }
        news.sort(function (a, b) { return cmp(b.version, a.version); });
        if (!news.length) return;
        more = news.length > SHOWN;
        if (more) news = news.slice(0, SHOWN);
        else { var oldest = data.releases[data.releases.length - 1]; more = !!oldest && parts(oldest.version) && cmp(oldest.version, seen) > 0; }      // (the file reaches back less far than the version that was seen: the rest is in the changelog)
        try { openWindow(news, String(seen).replace(/^\s*v?|\s+$/g, ''), more); } catch (e) { /* a window that cannot be made is not worth a message */ }
    }

    // ---- the checks ----------------------------------------------------------------------------------------------------
    function visible() { return d.visibilityState !== 'hidden'; }
    function check() {
        last = now();
        load(function (data) {
            if (!data) return;
            newer = cmp(data.version, own) > 0 ? String(data.version).replace(/^v/, '') : '';
            render();
            if (!tried) { tried = true; whatsNew(data); }
        });
    }
    function schedule(ms) { w.clearTimeout(timer); timer = 0; if (visible()) timer = w.setTimeout(run, ms); }
    function run() { timer = 0; if (!visible()) return; check(); schedule(EVERY_MS); }
    own = ownVersion();
    if (!own) return;
    w.AntsUpdate = { page: function (hooks) { if (hooks && typeof hooks.quiet === 'function') quiets.push(hooks.quiet); if (hooks && typeof hooks.moved === 'function') movers.push(hooks.moved); render(); } };
    d.addEventListener('visibilitychange', function () {
        if (!visible()) { w.clearTimeout(timer); timer = 0; } else if (!last || now() - last >= BACK_MS) run(); else schedule(Math.max(0, EVERY_MS - (now() - last)));
    });
    schedule(canWindow && parts(get('local', SEEN)) && cmp(own, get('local', SEEN)) > 0 ? SOON_MS : FIRST_MS);
}());
