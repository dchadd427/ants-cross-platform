// Keeps the screen awake while a match is played and while a recording plays (docs/PLAY_IN_BROWSER.md, "The screen stays on"): the browser's Screen Wake Lock (navigator.wakeLock), the way a video page holds
// it, so that a phone does not turn its screen off in the middle of a match or a replay. Nothing is drawn and nothing is said. ES5, no module system: a page gets AntsWakeLock, node gets module.exports
// (tests/scripts/web_wake_lock_check.js runs it with fakes).
//   * Wanted while: a match that this page plays, from the countdown card to the results (the game's ants_match_running), or a recording that plays (the replay states Playing and Jumping: not Paused, not
//     Ended, not Cut short, not a file that cannot be shown; a live match that is watched is a recording that plays). The page's state is read every 500 ms, and again when the page becomes visible.
//   * What it does: asks for the lock when it is wanted and not held; lets it go when it is not wanted any more and when the page is left (pagehide); starts again when the page is shown again (pageshow, or
//     the tab becoming visible: a page that was left may live on). The browser drops the lock itself when the tab is hidden: the page asks again when it is visible and the lock is still wanted.
//   * What it never does: show an error. A browser without the API (iOS before 16.4 and any other that lacks it) gets no timer, no listener and no stand-in. A request that is refused (a low battery, a policy),
//     that throws, or that is not answered in 10 seconds is dropped quietly and asked again after 15 seconds, or at once when the tab becomes visible. The game never waits for any of it.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsWakeLock = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var POLL_MS = 500;                                               // how often the game's state is looked at
    var RETRY_MS = 15000;                                            // a refused request is asked again no sooner than this (a page that becomes visible asks at once)
    var ASK_MS = 10000;                                              // a request that is not answered in this long counts as refused
    var STATE = { playing: 2, jumping: 4 };                          // include/ants_app/replay_view.hpp ReplayState: the states of a recording that holds the screen (a jump is playing up to a turn, without a picture)

    // True while the page wants the screen awake. page: { ready(), replay (true for a recording or a live match), matchRunning() (ants_match_running: 1 while a match is played), replayState() (ReplayValue::State) }
    function wantedOf(page) {
        if (!page.ready()) return false;
        if (page.replay) {
            var state = page.replayState();
            return state === STATE.playing || state === STATE.jumping;
        }
        return page.matchRunning() === 1;
    }

    // forPage({ win, doc, nav, replay, ready, matchRunning, replayState, every, clear, now }) returns { sync, state, stop }, or null where the browser has no wake lock (and nothing is made)
    function forPage(page) {
        var nav = page.nav, api = nav && nav.wakeLock;
        if (!api || typeof api.request !== 'function') return null;
        var win = page.win, doc = page.doc;
        var every = page.every || function (fn, ms) { return win.setInterval(fn, ms); };
        var clear = page.clear || function (id) { win.clearInterval(id); };
        var clock = page.now || function () { return Date.now(); };
        var held = null, asking = false, asks = 0, askedAt = 0, away = false, blockedUntil = 0, timer = 0;

        function wanted() {
            try { return !away && !!wantedOf(page); } catch (e) { return false; }            // (a game that does not answer yet wants nothing)
        }
        function visible() { return !doc || doc.visibilityState !== 'hidden'; }
        function quiet() { /* a lock that cannot be asked for or dropped is nobody's business */ }
        function letGo(sentinel) {
            try {
                var done = sentinel.release();
                if (done && typeof done.then === 'function') done.then(quiet, quiet);
            } catch (e) { quiet(); }
        }
        function refused() { asking = false; blockedUntil = clock() + RETRY_MS; }
        function abandon() { asks++; asking = false; }                                       // (what the request that was waited for says later is no longer asked for)
        function ask() {
            var mine = ++asks, request;
            asking = true;
            askedAt = clock();
            try { request = api.request('screen'); } catch (e) { refused(); return; }
            if (!request || typeof request.then !== 'function') { refused(); return; }
            request.then(function (sentinel) {
                if (mine === asks) asking = false;
                if (!sentinel) return;
                if (held || !wanted()) { letGo(sentinel); return; }                          // (it is not wanted any more, the match is over or the page has gone; or another request was answered first)
                held = sentinel;
                try { sentinel.addEventListener('release', function () { if (held === sentinel) held = null; }); } catch (e) { quiet(); }
            }, function () { if (mine === asks) refused(); });                               // (a refusal of a request that was given up on adds no wait)
        }
        function sync() {
            if (asking && clock() - askedAt >= ASK_MS) { abandon(); refused(); }             // (a request that is never answered must not block the next)
            if (held && held.released) held = null;                                          // (the browser let it go)
            if (!wanted()) {
                var gone = held;
                held = null;
                if (gone) letGo(gone);
                return;
            }
            if (held || asking || !visible() || clock() < blockedUntil) return;
            ask();
        }
        function start() { if (!timer) timer = every(sync, POLL_MS); }
        function back() { if (away) { away = false; start(); } }                           // (the page is shown again: it was left, and it lives on)
        function stop() {
            away = true;
            if (timer) { clear(timer); timer = 0; }
            sync();
        }

        if (doc && doc.addEventListener) doc.addEventListener('visibilitychange', function () {
            if (visible()) { back(); if (asking) abandon(); }                               // (shown: a request that was made while the tab was hidden is not waited for)
            blockedUntil = 0;
            sync();
        });
        if (win && win.addEventListener) {
            win.addEventListener('pagehide', stop);
            win.addEventListener('pageshow', function () { back(); sync(); });
        }
        start();
        sync();
        return { sync: sync, stop: stop, state: function () { return { held: !!held, asking: asking, away: away, blockedUntil: blockedUntil, polling: !!timer }; } };
    }

    return { forPage: forPage, wantedOf: wantedOf, POLL_MS: POLL_MS, RETRY_MS: RETRY_MS, STATE: STATE };
});
