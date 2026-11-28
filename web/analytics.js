/* The counter of the site's visits (docs/ANALYTICS.md): the page tells the hub a few small things with navigator.sendBeacon, and the hub counts them. No cookie, no local or session storage, no id of the visitor: the
   hub makes a code from the address and the browser that is good for one day (hub/analytics.py). What this script sends, all of it, to POST /hub/a:
     view    the page's path, the name of the site that sent the visitor (its host only), the width of the window and the browser's language
     leave   the page's path, when the page is hidden or left
     perf    how long the page took to load, and how long the game took to start (AntsAnalytics.game), with the site's version
     error   the class of an error, its message, and the site's own file and line (at most five for a page, each once)
   A browser that asked not to be tracked (Do Not Track or Global Privacy Control) sends one `visit` with nothing in it: the hub counts the visit and its country. Nothing is counted in a frame, on the admin page
   or in a browser with no sendBeacon. A failure is silent: counting never disturbs the page. Plain ES5. */
(function () {
    'use strict';
    var w = window, d = document, n = navigator;
    try { if (w.top !== w.self) return; } catch (e) { return; }
    if (typeof n.sendBeacon !== 'function' || typeof w.Blob !== 'function' || typeof w.JSON !== 'object') return;
    var loc = w.location, path = loc.pathname || '/', DOOR = '/hub/a', MAX_ERRORS = 5, MAX_PERF_MS = 600000;
    if (/^\/(?:admin|hub)(?:\/|$)/.test(path)) return;

    function untracked() {
        return n.doNotTrack === '1' || n.doNotTrack === 'yes' || w.doNotTrack === '1' || n.msDoNotTrack === '1' || n.globalPrivacyControl === true;
    }
    function send(event) {
        try { n.sendBeacon(DOOR, new w.Blob([JSON.stringify(event)], { type: 'text/plain;charset=UTF-8' })); } catch (e) { /* not counted */ }
    }
    if (untracked()) { send({ t: 'visit' }); return; }

    function hostOf(url) {                // the host that a referrer names, or '' (this site itself is no referrer)
        var m = /^https?:\/\/([^\/?#:]+)/i.exec(String(url || ''));
        return m && m[1].toLowerCase() !== String(loc.hostname).toLowerCase() ? m[1].toLowerCase() : '';
    }
    function ownVersion() {               // the bar's stamp, or the front page's footer: the page's own version (a source page that nobody stamped has none)
        var bar = d.querySelector('header.sb[data-version]'), foot = d.getElementById('game-version'), v = bar ? bar.getAttribute('data-version') : foot ? foot.textContent : '';
        return /^\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(String(v || '').replace(/^\s*v?|\s+$/g, '')) ? String(v).replace(/^\s*v?|\s+$/g, '') : '';
    }
    function perf(name, ms) {
        ms = Math.round(ms);
        if (ms >= 1 && ms <= MAX_PERF_MS) send({ t: 'perf', n: name, ms: ms, v: ownVersion() });
    }

    send({ t: 'view', p: path, r: hostOf(d.referrer), w: w.innerWidth, l: n.language || '' });

    // how long the page took: from the start of the navigation to the end of the load event
    function loaded() {
        var e = w.performance && w.performance.getEntriesByType ? w.performance.getEntriesByType('navigation')[0] : null;
        if (e && e.type === 'back_forward') return;                  // (a page that came from the browser's memory was not loaded)
        perf('load', e ? e.loadEventEnd : w.performance && w.performance.timing ? w.performance.timing.loadEventEnd - w.performance.timing.navigationStart : 0);
    }
    if (d.readyState === 'complete') w.setTimeout(loaded, 0);
    else w.addEventListener('load', function () { w.setTimeout(loaded, 0); });

    // the page was hidden or left (the last one of a visit ends it)
    function leave() { if (d.visibilityState !== 'visible') send({ t: 'leave', p: path }); }
    d.addEventListener('visibilitychange', leave);
    w.addEventListener('pagehide', function () { send({ t: 'leave', p: path }); });

    // errors: the class, the message and the site's own file and line
    var errors = 0, told = {};
    function ownFile(file) {
        var prefix = loc.origin ? loc.origin + '/' : '';
        return prefix && String(file || '').indexOf(prefix) === 0 ? String(file).slice(prefix.length).split('?')[0].split('#')[0] : '';
    }
    function error(name, message, file, line) {
        var src = ownFile(file), key = name + '|' + message + '|' + src + '|' + line;
        if (errors >= MAX_ERRORS || told[key]) return;
        errors++;
        told[key] = true;
        send({ t: 'error', k: name === 'RuntimeError' || /(?:^|\/)index\.(?:js|wasm)$/.test(src) ? 'Game' : 'Page', n: name, m: String(message || '').slice(0, 300), s: src && line ? src + ':' + line : '' });
    }
    w.addEventListener('error', function (e) {
        if (!e || (!e.filename && !e.lineno && !e.error)) return;      // ("Script error." from another site says nothing)
        var name = e.error && typeof e.error.name === 'string' ? e.error.name : 'Error', message = String(e.message || '').replace(/^Uncaught (?:[A-Za-z]+: )?/, '');
        error(name, message, e.filename, e.lineno);
    });
    w.addEventListener('unhandledrejection', function (e) {
        var r = e && e.reason;
        error(r && typeof r.name === 'string' ? r.name : 'Error', r && r.message !== undefined ? r.message : typeof r === 'string' ? r : '', '', 0);
    });

    // the game page tells how long the game took to start, once
    var toldGame = false;
    w.AntsAnalytics = { game: function (ms) { if (!toldGame) { toldGame = true; perf('game', ms); } } };
})();
