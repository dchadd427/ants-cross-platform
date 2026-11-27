// The name that a person plays under on the front page and on the game page (web/lobby.html, web/shell.html): nobody types a name in the game; the name comes from the site (tests/scripts/web_player_name_check.js).
//   * A person who is logged in with Discord (docs/HUB_LOGIN.md "Login") plays under the name that the hub says (docs/HUB_NICKNAMES.md): "[TAG] Nickname", the nickname, or, for a person who chose none, the name of the
//     login that fits the game. The page asks the site's own `GET /hub/me` (same origin; the cookie is HttpOnly) when it opens, and again at every seat (a login, a guest of a site that has a login, a failed answer), so
//     that a log in or a log out in the chat is known at the next room. `play` is the name and its PASS, which a server that has the hub's key asks for (docs/NETWORK_PORT.md "Protocol 29"): the page tells the pass in its Hello.
//     A hub that does not say `play` is read the old way: `you.name`, then `you.next` and `you.other`, as far as the game's font draws them; the username always fits.
//   * Everybody else (not logged in, no hub or no login, an answer that is late or wrong) plays as a guest: the name that this browser keeps and "(Guest)" after it, or, when none was typed, "Guest" and six digits, made
//     once and kept in this browser (localStorage, GUEST_KEY and NAME_KEY; a browser that refuses its storage keeps them for the page). A login and a log out never change them. A guest's name has no pass: the server makes
//     every name that has none a guest's, "Name (Guest)" (a typed name is checked by the hub's `name` frame before it is kept: docs/HUB_NICKNAMES.md).
//   * ready(fn) calls fn(name, kind, canSwitch, how, pass) once: at once when the answer cannot change (a site with no login), else when the question is answered, failed or given up (ASK_MS). how: 'other' (the
//     shown name did not fit: the other one is used), 'nick' (the person has a nickname), 'login' (a guest whom the hub could log in) or ''. A question on its way is shared by all who wait; a failed one is asked again at the next ready().
//   * ES5, no module system: a page gets the global AntsPlayerName, node gets module.exports. A name from the hub is text from strangers: cut before any work, and only the letters of a name are kept (name_letters.js).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsPlayerName = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var NL = (typeof self !== 'undefined' && self.AntsNameLetters) || (typeof globalThis !== 'undefined' && globalThis.AntsNameLetters) || null;       // (front/name_letters.js: a script tag before this one; the node checks set the global first)
    var ME = '/hub/me';                  // the hub's page for "who am I" (it answers only the site's own page)
    var GUEST_KEY = 'ants.guest';        // localStorage: the six digits of this browser's guest name
    var NAME_KEY = 'ants.guestname';     // localStorage: the name that a guest typed (docs/HUB_NICKNAMES.md); none: the guest name is the six digits
    var GUEST_MIN = 3, GUEST_MAX = 16;   // the length of a typed guest's name (hub/nickrules.py)
    var SUFFIX = ' (Guest)';             // what follows a typed guest's name wherever it is shown, and in the game
    var ASK_MS = 2000;                   // the longest that a seat waits: then the person goes on as a guest
    var CUT = 256;                       // a name from the hub is cut to this many characters before any work
    var MIN = 2, MAX = 32;               // the length of a name that fits (the game itself takes 1 to 32)

    // ---- the pure parts ----
    // The text made to fit the game's name rules, or '' when it cannot: NFC, blanks collapsed and cut at both ends, 2 to 32 characters of what the wire takes (printable ASCII and the letters of the game's font:
    // name_letters.js; a Greek or a CJK name is refused as a whole, so that the next name gets its turn), and `check` (the page's nameCheck) takes it.
    function fit(text, check) {
        if (typeof text !== 'string') return '';
        var s = text.slice(0, CUT);
        try { s = s.normalize('NFC'); } catch (e) { /* a browser without normalize: the text as it is */ }
        s = s.replace(/[\t\r\n]/g, ' ').replace(/ +/g, ' ').replace(/^ | $/g, '');
        if (s.length < MIN || !NL.isWireName(s, MAX)) return '';
        var checked = typeof check === 'function' ? check(s) : { ok: true, name: s };
        return checked && checked.ok === true && typeof checked.name === 'string' && checked.name.length >= MIN ? checked.name : '';
    }
    // The pass that goes with a name (net::valid_pass_shape): digits, a dot, 32 lower-case hex digits; anything else is none
    function passOf(text) { return typeof text === 'string' && /^[0-9]{1,12}\.[0-9a-f]{32}$/.test(text) ? text : ''; }
    // What the answer of /hub/me says: { kind, name, canSwitch }.
    //   kind 'login'  the hub names the visitor (`you`): name is `play.name`, or, from a hub that does not say it, the first of you.name, you.next and you.other that fits ('' when neither does: the guest name is
    //                 used then); pass: the hub's pass for it ('' for none); canSwitch: the hub says an other name (a switch between the display name and the username exists); viaOther: the name of the
    //                 login did not fit and another one is used
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
        var play = body.play && typeof body.play === 'object' && !Array.isArray(body.play) ? body.play : null;
        var shown = fit(you.name, check), name = play ? fit(play.name, check) : shown || fit(you.next, check) || fit(you.other, check);
        var answer = { kind: 'login', name: name, pass: play && name !== '' && name === play.name ? passOf(play.pass) : '', canSwitch: typeof you.other === 'string' && you.other !== '', viaOther: !shown && name !== '' && !(typeof you.nick === 'string' && you.nick !== '') };
        if (typeof you.nick === 'string' && you.nick !== '' && name !== '') answer.nick = true;       // (the person has a nickname: the card of a link says "your nickname", not "your Discord name")
        return answer;
    }
    // Six digits that make a guest name: 100000 to 999999 from `random` (a number from 0 up to, not including, 1; anything else is the first)
    function guestDigits(random) {
        var r = typeof random === 'number' && random >= 0 && random < 1 ? random : 0;
        return String(100000 + Math.floor(r * 900000));
    }
    function validDigits(text) { return typeof text === 'string' && /^[1-9][0-9]{5}$/.test(text); }
    function guestName(digits) { return 'Guest ' + digits; }
    // A name that a guest typed and the hub took (hub/nicknames.py guest_check): 3 to 16 characters of what the wire takes
    function validTyped(text) { return typeof text === 'string' && text.length >= GUEST_MIN && text === text.replace(/^ +| +$/g, '') && NL.isWireName(text, GUEST_MAX); }

    // ---- the person ----
    // create(env): env.fetch(url, init) a promise of a response (null: no fetch), env.later(fn, ms) and env.cancel(handle) the clock, env.storage() the localStorage (it may throw or be none), env.random() a
    // number from 0 up to 1, env.Abort the browser's AbortController (or none), env.check the page's nameCheck. Returns { ask, ready, known, typed, setTyped, guestShown }.
    function create(env) {
        var later = env.later, cancel = env.cancel, random = env.random;       // (plain calls: a browser's setTimeout and clearTimeout refuse to be a method of another object)
        var kept = '';                    // the guest digits of this page
        var typedName = null;             // the name that a guest typed (null: not read yet, '': none)
        var last = null;                  // the last question that ended: { kind, name, canSwitch }
        var waiting = [];                 // what waits for the question that is on its way
        var round = 0, flying = false, timer = null;

        function guest() {
            if (kept) return kept;
            var store = null, value = null;
            try { store = env.storage ? env.storage() : null; value = store ? store.getItem(GUEST_KEY) : null; } catch (e) { value = null; }
            if (!validDigits(value)) {
                value = guestDigits(random ? random() : Math.random());
                try { if (store) store.setItem(GUEST_KEY, value); } catch (e) { /* a browser that keeps nothing: the name lasts for this page */ }
            }
            kept = value;
            return kept;
        }
        // The name that this browser's guest typed, or '' (a stored text that is no name is none)
        function typed() {
            if (typedName !== null) return typedName;
            var store = null, value = '';
            try { store = env.storage ? env.storage() : null; value = store ? store.getItem(NAME_KEY) : ''; } catch (e) { value = ''; }
            typedName = validTyped(value) ? value : '';
            return typedName;
        }
        // Keeps the name that a guest typed (the hub has checked it), or lets it go ('' ): false for a text that is no such name; a browser that keeps nothing keeps it for the page
        function setTyped(text) {
            if (text !== '' && !validTyped(text)) return false;
            var store = null;
            try { store = env.storage ? env.storage() : null; } catch (e) { store = null; }
            try { if (store) { if (text === '') store.removeItem(NAME_KEY); else store.setItem(NAME_KEY, text); } } catch (e) { /* a browser that keeps nothing: the name lasts for this page */ }
            typedName = text;
            return true;
        }
        // The guest's name as the game and the screens show it: the typed name and " (Guest)", else the number
        function guestShown() { var t = typed(); return t ? t + SUFFIX : guestName(guest()); }
        // A login's answer can change (a log out, a log in in the chat without a reload) and a failed one may be better next time: a login, a guest of a site that has a login and a failed one are all asked again at
        // every seat; only a site without a login stays
        function again(result) { return result.kind !== 'none'; }
        function answer(result) {
            return result.kind === 'login' && result.name ? { name: result.name, kind: 'login', canSwitch: result.canSwitch, how: result.viaOther ? 'other' : result.nick === true ? 'nick' : '', pass: result.pass || '' } : { name: guestShown(), pass: '', kind: 'guest', canSwitch: false, how: result.kind === 'guest' ? 'login' : '' };
        }
        function tell(fn, result) {
            var a = answer(result);
            try { fn(a.name, a.kind, a.canSwitch, a.how, a.pass); } catch (e) { if (typeof console !== 'undefined' && console.error) console.error(e); }       // (the others that wait still hear)
        }
        function settle(result) {
            if (timer !== null) { cancel(timer); timer = null; }
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
            timer = later(function () {
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
            // The question, at once (the page asks it when it opens, so that a button pressed soon after finds it answered)
            ask: function () { if (!last || again(last)) ask(); },
            // fn(name, kind, canSwitch, how): kind 'login' or 'guest'; at once for an answer that cannot change, else when the question ends (ASK_MS at the latest)
            ready: function (fn) {
                if (last && !again(last)) { tell(fn, last); return; }
                waiting.push(fn);
                ask();
            },
            // The name that can be used without asking (a site with no login: a guest's), else ''
            known: function () { return last && !again(last) ? guestShown() : ''; },
            // The name that a guest typed ('' for none), keeping a new one (false: it is no such name), and the guest's name as it is shown and played ("Wren (Guest)", or "Guest 482913")
            typed: typed, setTyped: setTyped, guestShown: guestShown
        };
    }
    // The person of a page: its window's fetch, localStorage and AbortController, its nameCheck and, in `tools`, its own clock and generator (setTimeout, clearTimeout and Math.random; the global ones when none)
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

    return { ME: ME, GUEST_KEY: GUEST_KEY, NAME_KEY: NAME_KEY, SUFFIX: SUFFIX, ASK_MS: ASK_MS, fit: fit, validTyped: validTyped, fromMe: fromMe, guestDigits: guestDigits, validDigits: validDigits, guestName: guestName, create: create, forWindow: forWindow };
});
