// The alerts of the front page (docs/HOME_SCREEN.md, "Alerts"): the "Alerts" sheet that the Menu opens, and the alert for a person who joins the visitor's room. Loaded by web/home/home.js after the first paint, with
// web/home/alerts.css; the shared part (the switches, the tab alert, the sound table) is web/alert_core.js, which the page loads. Nothing is asked of the browser and nothing is sent anywhere: the switches are kept in this browser only.
//   * The sheet has the part of its design that works today, "On this page": an alert for a person who joins my room, and the tab alert for my match. A row for a sound is drawn only for an event whose sound file
//     is written in alert_core.js (none is: the maintainer approves each sound first), so no switch is ever dead. The same sheet for a guest and for a person who is logged in: it needs no login.
//   * A person (never a computer player: the lobby's `joined` event is for people) joins the room that this page holds: on Home a toast over the page says who and where, with Open (the Room's own toast stays for the Room);
//     while the page is in the background the tab's title blinks. The switch for it turns both off. The sound, once one is approved, plays with it.
//   * Everything that is said (names) becomes DOM text with createElement and textContent only, never markup.
//   * The bell in the top bar (next to the account button) shows where the alerts stand on this device: a dot when notifications are not set up (the browser's permission is still "default"), plain when they are on
//     ("granted"), struck through when the browser blocks them ("denied"), an arrow on an iPhone that has not installed the app (it has no notifications before that), and plain for a visitor who is not logged in. It
//     READS the permission (Notification.permission) and never asks for it: no call of requestPermission anywhere, on load or on a press. A press opens the sheet. A browser with no notifications at all has no bell
//     (the Menu's row stays). On a phone held upright while the page holds a room the bell has no room beside My room: it is not drawn and the Menu's button has the dot.
// ES5, no module system: a page gets AntsHomeAlerts, node gets module.exports (tests/scripts/web_home_alerts_check.js). It uses the class .gshade of web/home/games.css, and looks out for the .gtoast that the Games list shows.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeAlerts = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var TOAST_MS = 6000;                                 // a toast goes after this long (not under the pointer or the focus)
    var NAME_MAX = 40;                                   // a name in the tab's title is cut to this
    var SVG = 'http://www.w3.org/2000/svg';
    var TEXT = {
        group: 'On this page', lead: 'Nothing to allow. These work while Ants is open, also in a background tab.', close: 'Close', open: 'Open', joined: ' joined your room',
        join: ['Alert when someone joins my room', 'A person, not a computer player.'], start: ['Flash the tab when my match starts', 'The tab title blinks while this page is in the background.'],
        soundJoin: ['Sound when someone joins my room', 'A person, not a computer player.'], soundStart: ['Sound when my match starts', 'Plays while this page is in the background.']
    };
    var ROWS = [['join', null], ['start', null], ['soundJoin', 'join'], ['soundStart', 'start']];      // [the switch, the sound that the row is for: null for an alert that needs none]
    var BELL = { off: 'Alerts: not set up on this device', on: 'Alerts: on for this device', blocked: 'Alerts: blocked in this browser', needs: 'Alerts: need the app on the Home Screen' };
    var PATHS = { bell: ['M6 17h12l-1.6-2.4V10a4.4 4.4 0 0 0-8.8 0v4.6L6 17z', 'M10 20a2 2 0 0 0 4 0'], bellx: ['M6 17h12l-1.6-2.4V10a4.4 4.4 0 0 0-8.8 0v4.6L6 17z', 'M10 20a2 2 0 0 0 4 0', 'M4 4l16 16'], down: ['M12 5v9', 'M8 11l4 4 4-4'], user: ['M12 12.2a3.6 3.6 0 1 0 0-7.2 3.6 3.6 0 0 0 0 7.2z', 'M5 20c.8-3.6 3.6-5.5 7-5.5s6.2 1.9 7 5.5'], x: ['M6 6l12 12', 'M18 6L6 18'] };

    function shorten(name) { name = String(name); return name.length > NAME_MAX ? name.slice(0, NAME_MAX - 1) + '\u2026' : name; }

    // mount({ doc, win, core (AntsAlertCore), ctl: { screen() -> 'home'|'room', show(name, how) }, bar (the right end of Home's bar: the bell goes in front of its first button), menuBtn (Home's Menu button: it gets the dot),
    //         signedIn() (whether the visitor is logged in), dock() (the chat dock, when it is there: its tap(fn) says when the visitor changes), table (optional: the sound table) }) -> { row(onPress), open(opener), close(), alert(kind, data), refresh(), flash }
    function mount(env) {
        var doc = env.doc, win = env.win, core = env.core, ctl = env.ctl;
        if (!core || !core.settings) return null;
        var set = core.settings(win), seen = core.looked(win), flash = core.flasher({ win: win, doc: doc }), snd = core.sounds({ win: win, settings: set, table: env.table });
        var shade = null, opener = null, toast = null, toastText = null, told = null, toastTimer = null;

        function el(tag, cls, text) { var e = doc.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
        function icon(name) {                              // a small picture, drawn with the DOM and never with markup; a browser that cannot draw it shows none
            try {
                if (!doc.createElementNS) return null;
                var svg = doc.createElementNS(SVG, 'svg'), i;
                svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('class', 'al-ico'); svg.setAttribute('aria-hidden', 'true'); svg.setAttribute('focusable', 'false');
                for (i = 0; i < PATHS[name].length; i++) { var p = doc.createElementNS(SVG, 'path'); p.setAttribute('d', PATHS[name][i]); svg.appendChild(p); }
                return svg;
            } catch (e) { return null; }
        }
        function wired() { var out = []; ROWS.forEach(function (r) { if (!r[1] || (env.table || core.SOUNDS)[r[1]]) out.push(r[0]); }); return out; }
        function title() { return core.title(env.table); }

        // ---- the sheet ----
        function paint(line, name) { var on = set.on(name); line.setAttribute('aria-checked', on ? 'true' : 'false'); line.className = 'al-line' + (on ? ' on' : ''); }
        function line(name) {
            var b = el('button', 'al-line'), words = TEXT[name], tx = el('span', 'al-tx'), sw = el('i', 'al-sw');
            b.type = 'button'; b.setAttribute('role', 'switch'); b.setAttribute('data-switch', name);
            tx.appendChild(el('span', 'al-t', words[0])); tx.appendChild(el('span', 'al-s', words[1]));
            b.appendChild(tx); b.appendChild(sw); paint(b, name);
            b.addEventListener('click', function () { set.set(name, !set.on(name)); paint(b, name); });
            return b;
        }
        function controls() { var out = [], all = shade && shade.querySelectorAll ? shade.querySelectorAll('button') : []; for (var i = 0; i < all.length; i++) out.push(all[i]); return out; }
        function open(back) {
            if (shade) return;
            opener = back || null;
            shade = el('div', 'gshade al-shade');
            var card = el('section', 'al-card'), top = el('div', 'al-top'), h = el('h2', 'al-title', title()), x = el('button', 'btn al-x'), g = el('div', 'al-h', TEXT.group);
            card.setAttribute('role', 'dialog'); card.setAttribute('aria-modal', 'true'); card.setAttribute('aria-label', title());
            x.type = 'button'; x.setAttribute('aria-label', TEXT.close);
            var xi = icon('x'); if (xi) x.appendChild(xi); else x.textContent = '\u00D7';
            top.appendChild(h); top.appendChild(x); card.appendChild(top); card.appendChild(g); card.appendChild(el('p', 'al-lead', TEXT.lead));
            wired().forEach(function (name) { card.appendChild(line(name)); });
            shade.appendChild(card);
            x.addEventListener('click', close);
            var down = false, hit = false;                 // a press on the dimmed page closes it, as the Cancel of any card does: it must start and end on the dimmed page
            shade.addEventListener('pointerdown', function (e) { down = e.target === shade; hit = false; });
            shade.addEventListener('pointerup', function (e) { hit = down && e.target === shade; down = false; });
            shade.addEventListener('pointercancel', function () { down = hit = false; });
            shade.addEventListener('click', function (e) { var was = hit; hit = false; if (was && e.target === shade) close(); });
            doc.body.appendChild(shade);
            var first = card.querySelector ? card.querySelector('.al-line') : null;
            if (first && first.focus) first.focus(); else if (x.focus) x.focus();
            seen.mark(); refresh();                        // the sheet has been looked at: the bell's dot goes for good (in step 1 the dot only means "not looked yet"; with push it becomes the permission's state)
        }
        function close() {
            if (!shade) return;
            var s = shade, back = opener;
            shade = null; opener = null;
            if (s.parentNode && s.parentNode.removeChild) s.parentNode.removeChild(s);
            if (back && back.focus) back.focus();
        }
        doc.addEventListener('keydown', function (e) {
            if (!shade || !e) return;
            if (e.key === 'Escape') { if (e.preventDefault) e.preventDefault(); close(); return; }
            if (e.key !== 'Tab') return;                   // the focus stays inside the sheet
            var all = controls(), i = all.indexOf(doc.activeElement);
            if (!all.length) return;
            if (e.shiftKey && i <= 0) { if (e.preventDefault) e.preventDefault(); all[all.length - 1].focus(); }
            else if (!e.shiftKey && (i === -1 || i === all.length - 1)) { if (e.preventDefault) e.preventDefault(); all[0].focus(); }
        });

        // ---- the Menu's row ----
        function row(onPress) {                            // onPress() closes the Menu and says what had the focus
            var b = el('button', 'mitem al-menu'), i = icon('bell');
            b.type = 'button';
            if (i) b.appendChild(i);
            b.appendChild(doc.createTextNode(title()));
            b.addEventListener('click', function () { var back = null; try { back = onPress ? onPress() : null; } catch (e) { back = null; } open(back); });
            return b;
        }

        // ---- the join alert ----
        function place() {                                 // over the page, just above the chat (above the strip of views on a phone) and above another toast that is up
            try {
                var h = win.innerHeight || 0, top = h, strip = doc.querySelector ? doc.querySelector('.htabs') : null, dock = doc.getElementById('gchat'), others = doc.querySelectorAll ? doc.querySelectorAll('.gtoast') : [], up, i;
                if (!h) return;
                if (strip && strip.offsetHeight) top = strip.getBoundingClientRect().top;
                else if (dock && dock.offsetHeight) top = dock.getBoundingClientRect().top;
                up = h - top + 10;
                for (i = 0; i < others.length; i++) if (others[i] !== toast && !others[i].hidden && others[i].offsetHeight) up = Math.max(up, h - others[i].getBoundingClientRect().top + 8);
                toast.style.bottom = Math.round(up) + 'px';
            } catch (e) { /* the toast keeps its own place */ }
        }
        function arm(ms) {
            if (toastTimer !== null) clearTimeout(toastTimer);
            toastTimer = setTimeout(function () {
                toastTimer = null;
                var held = false;
                try { held = !!(toast.matches && toast.matches(':hover, :focus-within')); } catch (e) { held = false; }
                if (held) arm(1500); else toast.hidden = true;
            }, ms);
        }
        function makeToast() {                            // made at the mount, hidden, with a status line that is in the page for good: a line that is there before its words come is the one that is read out
            toast = el('div', 'al-toast'); toastText = el('span'); told = el('p', 'al-sr'); var btns = el('div', 'al-btns'), go = el('button', 'btn sm', TEXT.open), i = icon('user');
            told.setAttribute('role', 'status'); told.setAttribute('aria-live', 'polite'); toast.hidden = true; go.type = 'button';
            go.addEventListener('click', function () { toast.hidden = true; if (ctl && ctl.show) ctl.show('room', 'push'); });
            if (i) toast.appendChild(i);
            toast.appendChild(toastText); btns.appendChild(go); toast.appendChild(btns); doc.body.appendChild(told); doc.body.appendChild(toast);
        }
        function toastUp(text) { toastText.textContent = text; told.textContent = text; toast.hidden = false; place(); arm(TOAST_MS); }
        makeToast();
        // alert('join', { name, text }): a person joined the room. Says whether Home's own display took the place of the page's toast (it did when Home shows, with the switch on or off: off shows nothing); the Room screen keeps the page's own toast.
        function alert(kind, data) {
            if (kind !== 'join' || !data) return false;
            var home = !!ctl && ctl.screen() === 'home';
            if (set.on('join')) {
                flash.start('\u25CF ' + shorten(data.name || '') + TEXT.joined);
                snd.play('join');                          // (once a sound is approved: it plays in front as well as in the background, a person has joined)
                if (home) toastUp(String(data.text || ''));
            }
            return home;
        }

        // ---- the bell ----
        function iphone() { var nav = win.navigator || {}; return /iPhone|iPad|iPod/.test(String(nav.userAgent || '')) || (nav.platform === 'MacIntel' && nav.maxTouchPoints > 1); }
        function installed() {
            var nav = win.navigator || {};
            if (nav.standalone === true) return true;
            try { return !!(win.matchMedia && win.matchMedia('(display-mode: standalone)').matches); } catch (e) { return false; }
        }
        function permission() { try { return win.Notification && typeof win.Notification.permission === 'string' ? win.Notification.permission : ''; } catch (e) { return ''; } }       // (read, never asked)
        function stateNow() {                              // 'off', 'on', 'blocked', 'needs', 'guest', or null: no bell
            var p = permission(), phone = iphone();
            if (!p && !phone) return null;                 // a browser with no notifications at all
            if (!(env.signedIn && env.signedIn())) return 'guest';
            if (phone && !installed()) return 'needs';
            if (!p) return null;                           // an iPhone app of an iOS that has none
            return p === 'granted' ? 'on' : p === 'denied' ? 'blocked' : 'off';
        }
        var bell = null;
        function mark(state) {
            var m = null;
            if (state === 'off' && !seen.is()) m = el('span', 'al-mark al-dotmark');
            else if (state === 'needs') { m = el('span', 'al-mark'); var d = icon('down'); if (d) m.appendChild(d); }
            return m;
        }
        var drawn = '';
        function refresh() {
            if (!bell) return;
            var state = stateNow(), dot = state === 'off' && !seen.is();      // (the dot: not set up, and the sheet not looked at yet)
            bell.hidden = !state;
            if (env.menuBtn) env.menuBtn.className = String(env.menuBtn.className).split(/\s+/).filter(function (c) { return c && c !== 'al-dot'; }).concat(dot ? ['al-dot'] : []).join(' ');
            if (!state || state + dot === drawn) return;
            drawn = state + dot;
            var words = state === 'guest' ? title() : BELL[state], ic = icon(state === 'blocked' ? 'bellx' : 'bell'), m = mark(state);
            while (bell.firstChild) bell.removeChild(bell.firstChild);
            bell.setAttribute('data-state', state); bell.setAttribute('aria-label', words); bell.setAttribute('title', words);
            if (ic) bell.appendChild(ic);
            if (m) bell.appendChild(m);
        }
        function makeBell() {
            if (!env.bar || !env.bar.insertBefore) return;
            bell = el('button', 'btn al-bell'); bell.type = 'button'; bell.hidden = true;
            bell.addEventListener('click', function () { open(bell); });         // (a press opens the sheet and nothing else: the browser is never asked)
            env.bar.insertBefore(bell, env.bar.firstChild);
            refresh();
            try { win.addEventListener('focus', refresh); doc.addEventListener('visibilitychange', refresh); } catch (e) { /* the bell is read again when the page asks */ }
            try {                                          // the browser says when its permission changes (reading it never asks)
                var q = win.navigator && win.navigator.permissions && win.navigator.permissions.query ? win.navigator.permissions.query({ name: 'notifications' }) : null;
                if (q && q.then) q.then(function (status) { if (status) status.onchange = refresh; }, function () { /* no such permission here */ });
            } catch (e) { /* no permission list here */ }
        }
        var hooked = false, tries = 0;
        function hook() {                                  // the chat dock comes after this part, whichever is first: the bell is read again whenever the dock says who the visitor is
            var dk = env.dock && env.dock();
            if (hooked || !bell) return;
            if (dk && dk.tap) { hooked = true; dk.tap(function (kind) { if (kind === 'live' || kind === 'you' || kind === 'lost') refresh(); }); refresh(); return; }
            if (++tries < 40) setTimeout(hook, 500);
        }
        makeBell();
        hook();

        return { row: row, open: open, close: close, alert: alert, refresh: refresh, title: title, flash: flash, wired: wired };
    }

    return { TEXT: TEXT, TOAST_MS: TOAST_MS, mount: mount };
});
