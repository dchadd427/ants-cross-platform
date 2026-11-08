// The name that a person plays under on the front page and on the game page (web/lobby.html, web/shell.html): nobody types a name on the web any more (tests/scripts/web_player_name_check.js).
//   * A person who is logged in with Discord (the chat's login, docs/HUB.md "Login") plays under the login's name, made to fit the game's name rules. The page asks the site's own `GET /hub/me` (same origin,
//     with the cookie, which this script never sees: it is HttpOnly) when it opens, and again at every seat that it asks for while the answer was a login: a log out in the chat leaves the page where it is, and
//     the next room is then a guest's. The answer is read for `you.name` and, when the hub says one, `you.other` (the other of the display name and the username): the first that fits wins; the username always fits.
//   * Everybody else (not logged in, a site with no hub or no login, an answer that is late or is not what the hub says) plays under a guest name, "Guest" and six digits, made once and kept in this browser
//     (localStorage, GUEST_KEY; a browser that refuses its storage keeps it for the page). A login and a log out never change it. It is a random number and nothing else.
//   * ready(fn) calls fn(name, kind) once: at once when the answer cannot have changed (a guest's, or a site with no login), else when the question is answered, failed or given up (ASK_MS). A question that is
//     on its way is shared by everybody who waits for it. A failed question is asked again at the next ready(), never in a loop.
//   * ES5, no module system: a page gets the global AntsPlayerName, node gets module.exports. A name from the hub is text from strangers: it is cut before any work, and only printable ASCII is kept.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsPlayerName = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var ME = '/hub/me';                  // the hub's page for "who does the cookie say I am" (the hub answers only the site's own page)
    var GUEST_KEY = 'ants.guest';        // localStorage: the six digits of this browser's guest name
    var ASK_MS = 2000;                   // the longest that a seat waits for the question: then the person goes on as a guest
    var CUT = 256;                       // a name from the hub is cut to this many characters before it is looked at (far more than any name needs)
    var MIN = 2, MAX = 32;               // the length of a name that fits: a login's name has 2 to 32 characters (the game itself takes 1 to 32)

    // ---- the pure parts -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // The text made to fit the game's name rules, or '' when it cannot: NFKD (a fancy or full-width letter becomes the plain one, an accented letter a letter and a mark), then everything that is not
    // printable ASCII goes (the marks too, and emoji, other scripts, controls, direction marks), blanks are collapsed and cut off at both ends; 2 to 32 characters are left (a longer name does not fit:
    // it is not cut), and `check` (the page's nameCheck: { ok, name }) takes it. There is no table of look-alikes: what NFKD does not give is dropped.
    function fit(text, check) {
        if (typeof text !== 'string') return '';
        var s = text.slice(0, CUT);
        try { s = s.normalize('NFKD'); } catch (e) { /* a browser without normalize: the text as it is, and its accented letters go below */ }
        s = s.replace(/[^\x20-\x7e]/g, '').replace(/ +/g, ' ').replace(/^ | $/g, '');
        if (s.length < MIN || s.length > MAX) return '';
        var checked = typeof check === 'function' ? check(s) : { ok: true, name: s };
        return checked && checked.ok === true && typeof checked.name === 'string' && checked.name.length >= MIN ? checked.name : '';
    }
    // What the answer of /hub/me says: { kind, name, canSwitch }.
    //   kind 'login'  the hub names the visitor (`you`): name is the first of you.name and you.other that fits ('' when neither does: the guest name is used then); canSwitch: the hub says an other name (a
    //                 switch between the display name and the username exists)
    //   kind 'guest'  the hub has a login and the visitor is not logged in (`you` is null)
    //   kind 'none'   the hub says that it has no login
    //   kind 'failed' anything else
    function fromMe(body, check) {
        if (!body || typeof body !== 'object') return { kind: 'failed', name: '', canSwitch: false };
        if (body.login === false) return { kind: 'none', name: '', canSwitch: false };
        if (body.login !== true) return { kind: 'failed', name: '', canSwitch: false };
        var you = body.you;
        if (you === null) return { kind: 'guest', name: '', canSwitch: false };
        if (!you || typeof you !== 'object' || Array.isArray(you)) return { kind: 'failed', name: '', canSwitch: false };
        var name = fit(you.name, check) || fit(you.other, check);
        return { kind: 'login', name: name, canSwitch: typeof you.other === 'string' && you.other !== '' };
    }
    // Six digits that make a guest name: 100000 to 999999 from `random` (a number from 0 up to, not including, 1; anything else is the first)
    function guestDigits(random) {
        var r = typeof random === 'number' && random >= 0 && random < 1 ? random : 0;
        return String(100000 + Math.floor(r * 900000));
    }
    function validDigits(text) { return typeof text === 'string' && /^[1-9][0-9]{5}$/.test(text); }
    function guestName(digits) { return 'Guest ' + digits; }

    // ---- the person --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // create(env): env.fetch(url, init) is a promise of a response (null: a browser without fetch), env.later(fn, ms) and env.cancel(handle) the clock, env.storage() the browser's localStorage (it may
    // throw or be none), env.random() a number from 0 up to 1, env.Abort the AbortController of the browser (or none), env.check the page's nameCheck. Returns { ask, ready, known }.
    function create(env) {
        var kept = '';                    // the guest digits of this page
        var last = null;                  // the last question that ended: { kind, name, canSwitch }
        var waiting = [];                 // what waits for the question that is on its way
        var round = 0, flying = false, timer = null;

        function guest() {
            if (kept) return kept;
            var store = null, value = null;
            try { store = env.storage ? env.storage() : null; value = store ? store.getItem(GUEST_KEY) : null; } catch (e) { value = null; }
            if (!validDigits(value)) {
                value = guestDigits(env.random ? env.random() : Math.random());
                try { if (store) store.setItem(GUEST_KEY, value); } catch (e) { /* a browser that keeps nothing: the name lasts for this page */ }
            }
            kept = value;
            return kept;
        }
        // A login's answer can change (a log out) and a failed one may be better next time: both are asked again at the next ready(); a guest's and a site without a login stay as they are
        function again(result) { return result.kind === 'login' || result.kind === 'failed'; }
        function answer(result) {
            return result.kind === 'login' && result.name ? { name: result.name, kind: 'login', canSwitch: result.canSwitch } : { name: guestName(guest()), kind: 'guest', canSwitch: false };
        }
        function tell(fn, result) {
            var a = answer(result);
            try { fn(a.name, a.kind, a.canSwitch); } catch (e) { if (typeof console !== 'undefined' && console.error) console.error(e); }       // (the others that wait still hear)
        }
        function settle(result) {
            if (timer !== null) { env.cancel(timer); timer = null; }
            round++; flying = false;
            last = result;
            var run = waiting;
            waiting = [];
            for (var i = 0; i < run.length; i++) tell(run[i], result);
        }
        function ask() {
            if (flying) return;
            flying = true;
            var mine = ++round;
            var ctl = null;
            try { ctl = env.Abort ? new env.Abort() : null; } catch (e) { ctl = null; }
            timer = env.later(function () {
                timer = null;
                if (mine !== round) return;
                if (ctl) { try { ctl.abort(); } catch (e) { /* it is given up all the same */ } }
                settle({ kind: 'failed', name: '', canSwitch: false });
            }, ASK_MS);
            var p = null;
            try { p = env.fetch ? env.fetch(ME, { credentials: 'same-origin', cache: 'no-store', redirect: 'error', signal: ctl ? ctl.signal : undefined }) : null; } catch (e) { p = null; }
            function failed() { if (mine === round) settle({ kind: 'failed', name: '', canSwitch: false }); }
            if (!p || typeof p.then !== 'function') { failed(); return; }
            p.then(function (res) {
                if (mine !== round) return;
                if (res && res.status === 404) { settle({ kind: 'none', name: '', canSwitch: false }); return; }       // (a site that has no login has no such page)
                if (!res || res.ok !== true || typeof res.json !== 'function') { failed(); return; }
                var body = null;
                try { body = res.json(); } catch (e) { failed(); return; }
                if (!body || typeof body.then !== 'function') { failed(); return; }
                body.then(function (data) { if (mine === round) settle(fromMe(data, env.check)); }, failed);
            }, failed);
        }
        return {
            // The question, at once (the page asks it when it opens, so that a person who presses a button soon after finds it answered)
            ask: function () { if (!last || again(last)) ask(); },
            // fn(name, kind, canSwitch): kind 'login' or 'guest'. At once for an answer that cannot change, else when the question ends (at the latest ASK_MS later)
            ready: function (fn) {
                if (last && !again(last)) { tell(fn, last); return; }
                waiting.push(fn);
                ask();
            },
            // The name that can be used without asking (a guest's, or a site with no login), else ''
            known: function () { return last && !again(last) ? guestName(guest()) : ''; }
        };
    }
    // The person of a page: its window's fetch, localStorage and AbortController, its nameCheck and, in `tools`, the page's own clock and generator (later(fn, ms), cancel(handle) and random(): its setTimeout,
    // clearTimeout and Math.random; the global ones when it passes none)
    function forWindow(win, check, tools) {
        tools = tools || {};
        return create({
            fetch: typeof win.fetch === 'function' ? function (url, init) { return win.fetch(url, init); } : null,
            later: tools.later || function (fn, ms) { return setTimeout(fn, ms); }, cancel: tools.cancel || function (handle) { clearTimeout(handle); },
            storage: function () { return win.localStorage; },
            random: tools.random || function () { return Math.random(); },
            Abort: win.AbortController, check: check
        });
    }

    return { ME: ME, GUEST_KEY: GUEST_KEY, ASK_MS: ASK_MS, fit: fit, fromMe: fromMe, guestDigits: guestDigits, validDigits: validDigits, guestName: guestName, create: create, forWindow: forWindow };
});
