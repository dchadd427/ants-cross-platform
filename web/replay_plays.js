/* The play counter of the replay page (docs/REPLAYS.md "Watching a replay", the numbers and the privacy note in docs/REPLAY_COUNTS.md): a recording of the server's own store is one play when this page has played it for
   10 seconds. Only the game page's replay mode loads this file, and only for such a recording (never a match that is being played, never a file of your own: web/shell.html "PLAYS_BEGIN").
   * What counts: time in which the game says "playing" (the replay state, not paused, not jumping, not ended) and the tab is shown. The page looks a few times a second; between two looks at most one second counts,
     and only when both looks found the game playing and the tab shown, so a pause, a jump, a hidden tab and a computer that slept add nothing.
   * Once a day: the id of a counted recording is remembered in this browser (localStorage "ants.plays", { id: milliseconds }, a day long, 200 at the most), BEFORE the play is sent, so that two tabs do not both send it.
     A browser that refuses its storage counts once for each page view.
   * What is sent: POST /counts/play/<id>, no body, no cookies (credentials "omit"); an answer, or none, is ignored and nothing is sent again. Nothing is shown.
   ES5, no module system: the page gets AntsPlays, node gets module.exports (tests/scripts/web_plays_check.js runs it with fakes). */
(function (root, factory) {
    'use strict';
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsPlays = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var NEED_MS = 10000;                             // playing time that makes a play
    var STEP_MS = 250;                               // how often the page looks
    var GAP_MS = 1000;                               // the most that one gap between two looks adds
    var DAY_MS = 24 * 60 * 60 * 1000;                // how long a counted recording is remembered
    var KEEP = 200;                                  // how many are remembered
    var KEY = 'ants.plays';
    var ID = /^[A-Za-z0-9_-]{1,64}$/;                // (the page passes an id that passed the stricter shape of web/history_page.js: this one keeps odd text out of an address)

    function clock(win) { return (win.Date || Date).now(); }
    function storeOf(win) { try { return win.localStorage || null; } catch (e) { return null; } }

    // What this browser counted in the last day, as { id: ms }. Nothing in the store is trusted: a text that is not JSON, an entry that is not a number, one from the future or one a day old is nothing.
    function recall(store, t) {
        var seen = Object.create(null), data = null;
        try { var raw = store ? store.getItem(KEY) : null; data = raw ? JSON.parse(raw) : null; } catch (e) { data = null; }
        if (data && typeof data === 'object' && !Array.isArray(data)) {
            Object.keys(data).forEach(function (k) { var v = data[k]; if (typeof v === 'number' && isFinite(v) && v <= t && t - v < DAY_MS) seen[k] = v; });
        }
        return seen;
    }
    function remember(store, seen) {
        var out = Object.create(null);
        Object.keys(seen).sort(function (a, b) { return seen[b] - seen[a]; }).slice(0, KEEP).forEach(function (k) { out[k] = seen[k]; });
        try { if (store) store.setItem(KEY, JSON.stringify(out)); } catch (e) { /* nothing is kept: this page view counts once all the same */ }
    }
    // True the first time in a day that this browser has counted the recording; it is written down at once
    function first(win, id) {
        var store = storeOf(win), t = clock(win), seen = recall(store, t);
        if (seen[id] !== undefined) return false;
        seen[id] = t;
        remember(store, seen);
        return true;
    }
    function send(win, id) {
        try {
            var sent = win.fetch('/counts/play/' + id, { method: 'POST', credentials: 'omit' });
            if (sent && typeof sent.then === 'function') sent.then(null, function () { /* a play that did not arrive is not counted: nothing is sent again */ });
        } catch (e) { /* the same */ }
    }

    // counter(win, id, playing): { sample, done }. `playing()` is true while the game says "playing". Each sample is one look; the page calls it a few times a second.
    function counter(win, id, playing) {
        var doc = win.document, last = clock(win), lastOn = false, total = 0, over = false;
        function on() { try { return !!playing() && !(doc && doc.visibilityState === 'hidden'); } catch (e) { return false; } }
        return {
            sample: function () {
                if (over) return;
                var t = clock(win), now = on();
                if (lastOn && now) total += Math.min(GAP_MS, Math.max(0, t - last));
                last = t; lastOn = now;
                if (total >= NEED_MS) { over = true; if (first(win, id)) send(win, id); }
            },
            done: function () { return over; }
        };
    }
    // start(win, id, playing): the counter on a timer that stops itself once the play is counted; null when there is nothing to start (no window, no timer, an id that is not one)
    function start(win, id, playing) {
        if (!win || typeof win.setInterval !== 'function' || typeof id !== 'string' || !ID.test(id)) return null;
        var c = counter(win, id, playing), timer = win.setInterval(function () { c.sample(); if (c.done()) win.clearInterval(timer); }, STEP_MS);
        c.sample();
        return c;
    }

    return { start: start, counter: counter, NEED_MS: NEED_MS, STEP_MS: STEP_MS, GAP_MS: GAP_MS, KEEP: KEEP, KEY: KEY };
});
