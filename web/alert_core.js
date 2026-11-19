// The alerts that need no permission (docs/HOME_SCREEN.md, "Alerts"): what the front page (web/home/alerts.js) and the game page (web/shell.html) share. Nothing here is sent anywhere and nothing is asked of the browser.
//   * settings(win): the person's switches, kept in this browser only (localStorage `ants.alerts.v1`, every read and write guarded: a browser that refuses keeps them for the visit). Every switch is on until it is turned off.
//   * flasher({ win, doc }): the tab alert. While the page is in the background (hidden, or not the window that has the focus) the tab's title alternates once a second with a line of its own and the tab's icon gets a red
//     dot; it stops, and the title and the icon are put back, as soon as the page is shown and has the focus again (or goes away).
//   * watchMatch(env): for the game page: when the countdown card of a match comes up and the page is in the background, the tab alert starts ("Your match is starting!"), and when the card goes it says "Your match has
//     started!" until the page is looked at. The game says whether a card is up (env.ask(60): the seconds left, -1 for none), the page says whether it is ready.
//   * sounds: SOUNDS below is the table of the sound files. NO SOUND IS ASSIGNED: the maintainer hears and approves each one first. A sound exists for an event only when its path is written there; until then no sound
//     plays, no switch for it is drawn, and nothing else changes.
// ES5 and no module system, as the site's other scripts: a page gets the global AntsAlertCore, node gets module.exports (tests/scripts/web_home_alerts_check.js runs it with fakes).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsAlertCore = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var KEY = 'ants.alerts.v1';
    var BLINK_MS = 1000;                                   // the title changes this often
    var LOOK_MS = 500;                                     // the game page looks for the countdown card this often (when it has no timer that a hidden page does not slow)
    var SOUND_GAP_MS = 2000;                               // at most one sound in this time, whatever happens
    var SOUND_VOLUME = 0.5;
    var NAMES = ['join', 'start', 'soundJoin', 'soundStart'];    // the switches: an alert when a person joins my room, the tab alert of my match, and (only where a sound is wired) a sound for each
    var SOUND_SWITCH = { join: 'soundJoin', start: 'soundStart' };
    // THE SOUNDS: the path of the file of each event, '' for none. To assign an approved sound, write its path here (the file goes in web/home/, which the image copies whole), for example join: '/home/alert_join.mp3';
    // nothing else changes: the switch for it appears in the sheet and the sound plays with the alert.
    var SOUNDS = { join: '', start: '' };
    var TEXT = { starting: '\u25B6 Your match is starting!', started: '\u25B6 Your match has started!', sheet: 'Alerts', sheetSounds: 'Alerts and sounds' };

    // ---- the switches ----
    function read(win) {
        try {
            var text = win.localStorage.getItem(KEY), v = text === null ? null : JSON.parse(text);
            return v && typeof v === 'object' && !Array.isArray(v) ? v : {};
        } catch (e) { return {}; }
    }
    // on(name): the switch as it is now (on unless it was turned off); set(name, on): turn it, and keep it where the browser lets us
    function settings(win) {
        var visit = {};                                    // (what was set in this visit where the storage refused: the storage is what counts, so that a change in another tab is seen)
        return {
            on: function (name) {
                if (NAMES.indexOf(name) === -1) return false;
                if (typeof visit[name] === 'boolean') return visit[name];
                var v = read(win)[name];
                return typeof v === 'boolean' ? v : true;
            },
            set: function (name, on) {
                if (NAMES.indexOf(name) === -1) return false;
                var kept = read(win), out = {}, i;
                kept[name] = !!on;
                for (i = 0; i < NAMES.length; i++) if (typeof kept[NAMES[i]] === 'boolean') out[NAMES[i]] = kept[NAMES[i]];
                try { win.localStorage.setItem(KEY, JSON.stringify(out)); delete visit[name]; return true; } catch (e) { visit[name] = !!on; return false; }
            }
        };
    }

    // ---- the sounds ----
    function wired(table) { return Object.keys(SOUND_SWITCH).filter(function (n) { return !!(table || SOUNDS)[n]; }); }
    function title(table) { return wired(table).length ? TEXT.sheetSounds : TEXT.sheet; }
    // play(name): the file of the event, when there is one and its switch is on; never twice within SOUND_GAP_MS; never throws. Says whether it started one.
    function sounds(env) {
        var win = env.win, table = env.table || SOUNDS, set = env.settings, last = null;
        return {
            play: function (name, nowMs) {
                var path = table[name], now = typeof nowMs === 'number' ? nowMs : Date.now();
                if (!path || !SOUND_SWITCH[name] || !set.on(SOUND_SWITCH[name])) return false;
                if (last !== null && now - last >= 0 && now - last < SOUND_GAP_MS) return false;
                last = now;
                try {
                    var a = new win.Audio(path), p;
                    a.volume = SOUND_VOLUME;
                    p = a.play();
                    if (p && p.catch) p.catch(function () { /* no gesture yet, or the browser refuses: no sound, no error */ });
                } catch (e) { /* no sound here */ }
                return true;
            }
        };
    }

    // ---- the tab alert ----
    // env: { win, doc }. start(line) says whether it started (it does not while the page is in front); relabel(line) changes the line of a running one; stop(); active(); pulse(nowMs) is the clock's tick (the game
    // page's timer that a hidden page does not slow calls it too).
    function flasher(env) {
        var win = env.win, doc = env.doc, timer = null, original = null, line = '', shown = false, last = 0;
        var link = null, plain = '', dot = '', loading = false, seen = false;       // seen: somebody pressed or typed on the page while it was shown, so a focus that is not reported is not the page's being away
        function away() {
            var unfocused = false;
            if (!seen) { try { unfocused = typeof doc.hasFocus === 'function' && !doc.hasFocus(); } catch (e) { unfocused = false; } }
            return !!doc.hidden || unfocused;
        }
        function find() {
            try { link = doc.querySelector ? doc.querySelector('link[rel~="icon"]') : null; } catch (e) { link = null; }
            plain = link ? String(link.href || '') : '';
        }
        function prepare() {                               // the icon with a red dot, drawn once from the page's own icon (a data address: the page's policy allows it)
            if (dot || loading || !link || !plain || !win.Image || !doc.createElement) return;
            loading = true;
            try {
                var img = new win.Image();
                img.onload = function () {
                    try {
                        var c = doc.createElement('canvas'), g;
                        c.width = c.height = 32; g = c.getContext('2d');
                        g.drawImage(img, 0, 0, 32, 32);
                        g.beginPath(); g.arc(24, 8, 8, 0, 6.2832); g.fillStyle = '#ffffff'; g.fill();
                        g.beginPath(); g.arc(24, 8, 6, 0, 6.2832); g.fillStyle = '#e8342b'; g.fill();
                        dot = c.toDataURL('image/png');
                        if (timer !== null && shown) paint();
                    } catch (e) { dot = ''; }
                };
                img.src = plain;
            } catch (e) { loading = false; }
        }
        function paint() {
            try { doc.title = shown ? line : original; } catch (e) { /* the title stays */ }
            if (link && plain) { try { link.href = shown && dot ? dot : plain; } catch (e) { /* the icon stays */ } }
        }
        function see() { var now = doc.title; if (now !== line) original = now; }       // (a title that the page itself changed while the alert ran is the page's own now: it is what comes back)
        function tick() { timer = setTimeout(tick, BLINK_MS); pulse(Date.now()); }
        function pulse(now) {
            if (timer === null || now - last < BLINK_MS - 50) return;
            see(); last = now; shown = !shown; paint();
        }
        function stop() {
            if (timer === null) return;
            see(); clearTimeout(timer); timer = null; shown = false;
            paint();
        }
        function start(text) {
            if (!away()) return false;
            if (timer === null) {
                line = String(text);
                original = doc.title; find(); prepare();
                shown = true; last = Date.now(); paint();
                timer = setTimeout(tick, BLINK_MS);
            } else { see(); line = String(text); if (shown) paint(); }
            return true;
        }
        function back() { if (doc.hidden) seen = false; if (timer !== null && !away()) stop(); }
        function present() { if (doc.hidden) return; seen = true; if (timer !== null) stop(); }       // (a press or a key on a page that is shown: somebody is there, whatever the focus says, until the window loses the focus or the page is hidden)
        try {
            doc.addEventListener('visibilitychange', back);
            doc.addEventListener('pointerdown', present, true);
            doc.addEventListener('keydown', present, true);
            doc.addEventListener('touchstart', present, true);
            win.addEventListener('focus', back);
            win.addEventListener('blur', function () { seen = false; });
            win.addEventListener('pagehide', stop);
        } catch (e) { /* no events here: the alert stops when it is told to */ }
        return { start: start, stop: stop, pulse: pulse, away: away, active: function () { return timer !== null; }, relabel: function (text) { if (timer === null) return; see(); line = String(text); if (shown) paint(); } };
    }

    // ---- the game page: my match starts ----
    // env: { win, doc, ask(what), ready(), every(fn) (optional: a timer that a hidden page does not slow; it calls fn often, and may return a function that stops it), settings (optional), table (optional) }
    function watchMatch(env) {
        var win = env.win, set = env.settings || settings(win), flash = flasher(env), snd = sounds({ win: win, settings: set, table: env.table }), up = false, timer = null;
        function look() {
            var left = -1;
            try { left = env.ready() ? env.ask(60) : -1; } catch (e) { left = -1; }
            var card = typeof left === 'number' && left >= 0, now = Date.now();
            flash.pulse(now);
            if (card && !flash.active() && set.on('start') && flash.away()) { if (flash.start(TEXT.starting)) snd.play('start', now); }
            else if (!card && up && flash.active()) flash.relabel(TEXT.started);
            up = card;
        }
        function loop() { timer = setTimeout(loop, LOOK_MS); look(); }
        if (typeof env.every === 'function') { try { env.every(look); } catch (e) { loop(); } } else loop();
        try { win.addEventListener('pagehide', function () { if (timer !== null) clearTimeout(timer); timer = null; }); } catch (e) { /* none */ }
        return { look: look, flash: flash };
    }

    return { KEY: KEY, NAMES: NAMES, SOUNDS: SOUNDS, TEXT: TEXT, BLINK_MS: BLINK_MS, SOUND_GAP_MS: SOUND_GAP_MS, settings: settings, sounds: sounds, wired: wired, title: title, flasher: flasher, watchMatch: watchMatch };
});
