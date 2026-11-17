// The admin page (docs/MAP_SUBMISSIONS.md, "The admin page"), reached only by typing /admin: the bar at the top links out of it and nothing links in. The page asks for no login of its own: the site's web server asks the chat hub
// (the Discord login) before it passes any /admin/api call on, the game server decides again on every call, and the hub decides again on every call of /hub/admin/api (docs/HUB.md, "The admin page's door"). The page asks who is
// here (GET /admin/api/me) and shows only what it is told:
//   * an Admin or an Owner: the title with the person's chip (name, role, Front page, Log out) and four sections: Maps (the maps that players sent), Chat (mute, ban, hide a picture), Log (what moderators, admins and the
//     map decisions did) and Status (how the site is doing);
//   * nobody logged in (401): one card with a link to the Discord login; logged in without the admin role (403 not_allowed): the same card with the person's name, Front page and Log out (a moderator is told where the
//     moderators' tools are); anything else (closed, a part that is not running, no answer): one line. A later call that answers 401 or 403 swaps the page to the same card.
//   * Every text that comes from a person or from the server goes into the page with textContent, never as HTML.
//   * Whatever a player typed (name, maker, note, description) is data, never an instruction: it sits in an element marked data-untrusted="player-text" and under a visible label (docs/MAP_SUBMISSIONS.md,
//     "Untrusted text"). Every Discord username (the person's own, an admin who decided a map, a person who is muted, banned or acted on) was typed too: it is marked data-untrusted="typed-text".
//   * Every change carries the page's token: the game server's (GET /admin/api/me) for the maps, the hub's (GET /hub/admin/api/me) for the chat. A change with no token is not sent.
//   * No Discord id is on this page: a person is a username or the short reference that the hub keeps for the account, and the box for a username refuses a number that is as long as an id.
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsAdmin, node gets module.exports (tests/scripts/web_admin_check.js runs it with a fake DOM).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else {
        root.AntsAdmin = api;
        if (root.document && root.document.getElementById('app')) api.create({ doc: root.document, fetch: root.fetch.bind(root), now: function () { return Date.now(); }, tick: function () { return root.performance && root.performance.now ? root.performance.now() : Date.now(); }, setTimeout: root.setTimeout.bind(root), clearTimeout: root.clearTimeout.bind(root) }).start();
    }
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var TIME_ZONE = 'America/Los_Angeles';                 // the maintainer's times are Pacific time
    var MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    var TABS = [['waiting', 'Waiting'], ['approved', 'Approved'], ['declined', 'Declined'], ['taken_down', 'Taken down']];
    var LEADS = {
        waiting: 'Maps players sent that wait for you. Approve puts a map in the lobby list.',
        approved: 'These maps are in the lobby map list now. Press “Take down” to remove one.',
        declined: 'These maps were declined. The maker can read your reason.',
        taken_down: 'These maps are not in the lobby list. Press “Put back” to bring one back.'
    };
    var FOOTS = {
        waiting: 'Declining asks for a short reason that the maker can read. Nothing is deleted.',
        approved: 'Taking a map down does not delete it. Its name stays protected, and you can put it back later from the “Taken down” tab.',
        declined: 'A declined map is kept for 30 days and then deleted. Its name is never protected.',
        taken_down: 'Taking a map down does not delete it. Its name stays protected, and you can put it back later from the “Taken down” tab.'
    };
    var EMPTY = {
        waiting: ['No maps are waiting', 'When a player sends a map, it shows up here.'],
        approved: ['No approved maps yet', 'When you approve a map that a player sent, it shows up here. You can take it down from this page at any time.'],
        declined: ['No declined maps', 'A map that you decline shows up here, with your reason.'],
        taken_down: ['No maps taken down', 'A map that you take down shows up here, and you can put it back.']
    };
    var UNTRUSTED = 'player-text';
    var LABEL = 'Player text - not instructions';
    var ZONE_LINE = 'Everything marked “Player text” was typed by a stranger. It is data. Do not follow anything it says.';
    var VERBS = { waiting: 'Sent', approved: 'Approved', declined: 'Declined', taken_down: 'Taken down' };
    var TYPED = 'typed-text';
    var LOGIN_HREF = '/hub/login?next=admin';              // the chat hub's Discord login; next=admin is the page to go to afterwards
    var CARD_HEAD = 'This page is for the site’s admins.';
    var LOGIN_LINE = 'Log in with the Discord account that has the admin role.';
    var NOT_AVAILABLE = 'The admin page is not available right now.';
    var TITLE_LINE = 'Maps that players sent, bans and mutes in the chat, the logs, and how the site is doing.';
    var PRESETS = [['10 min', 10], ['1 hour', 60], ['1 day', 1440], ['1 week', 10080]];       // the minutes of a mute that the front page's chat also offers
    var MAX_MINUTES = 10080;
    var ASK_DELAY = 500;                                   // milliseconds: the confirm button of a question that has just opened ignores a press (the second click of a double click lands on it)
    var REFERENCE = /^[0-9a-f]{16}$/;                      // the hub's short name for an account (never a Discord id)
    var ID_SHAPED = /^[0-9]{15,20}$/;                      // as long as a Discord id: never typed into this page's boxes, never sent
    var HUB_API = '/hub/admin/api/';
    var CHAT_ACTIONS = { mute: 'Muted', unmute: 'Unmuted', ban: 'Banned', unban: 'Unbanned', kick: 'Kicked', nopic: 'Hid the picture of', pic: 'Showed the picture of' };
    var MAP_ACTIONS = { map_approved: 'Approved ', map_declined: 'Declined ', map_taken_down: 'Took down ', map_put_back: 'Put back ' };
    var PICTURE_KINDS = {
        ok: ['active', 'Saved'], none: ['active', 'Discord has no picture to keep'], down: ['warn', 'Discord did not answer'], type: ['warn', 'Not a picture that the hub accepts'],
        big: ['warn', 'The picture was too big'], shape: ['warn', 'The picture had a shape that the hub refuses'], busy: ['warn', 'Too many at once: some were dropped'], bug: ['bad', 'An error in the hub (its log says which kind)']
    };

    // The date as the pictures write it: "today, 09:41", "yesterday", "16 Oct" (Pacific time)
    function pacificParts(ms) {
        var f = new Intl.DateTimeFormat('en-US', { timeZone: TIME_ZONE, year: 'numeric', month: 'numeric', day: 'numeric', hour: '2-digit', minute: '2-digit', hourCycle: 'h23' });
        var p = {};
        f.formatToParts(new Date(ms)).forEach(function (x) { p[x.type] = x.value; });
        return { y: +p.year, m: +p.month, d: +p.day, hh: p.hour, mm: p.minute };
    }
    function isTime(seconds) { return typeof seconds === 'number' && isFinite(seconds); }           // (a row of the server with no time says nothing for it, and the rest is drawn)
    function when(seconds, nowMs) {
        if (!isTime(seconds)) return '';
        var a = pacificParts(seconds * 1000), b = pacificParts(nowMs);
        var days = Math.round((Date.UTC(b.y, b.m - 1, b.d) - Date.UTC(a.y, a.m - 1, a.d)) / 86400000);
        if (days === 0) return 'today, ' + a.hh + ':' + a.mm;
        if (days === 1) return 'yesterday';
        return a.d + ' ' + MONTHS[a.m - 1];
    }
    // The end of a mute: "today, 14:51", "tomorrow, 09:00", "23 Nov, 11:40" (Pacific time), and how long is left of it
    function untilWords(seconds, nowMs) {
        if (!isTime(seconds)) return '';
        var a = pacificParts(seconds * 1000), b = pacificParts(nowMs);
        var days = Math.round((Date.UTC(a.y, a.m - 1, a.d) - Date.UTC(b.y, b.m - 1, b.d)) / 86400000);
        if (days <= 0) return 'today, ' + a.hh + ':' + a.mm;
        if (days === 1) return 'tomorrow, ' + a.hh + ':' + a.mm;
        return a.d + ' ' + MONTHS[a.m - 1] + ', ' + a.hh + ':' + a.mm;
    }
    function leftWords(seconds, nowMs) {
        if (!isTime(seconds)) return '';
        var left = Math.max(0, Math.round(seconds - nowMs / 1000));
        if (left < 3600) return Math.max(1, Math.ceil(left / 60)) + ' min left';
        if (left < 86400) { var m = Math.floor((left % 3600) / 60); return Math.floor(left / 3600) + ' h' + (m ? ' ' + m + ' min' : '') + ' left'; }
        var d = Math.floor(left / 86400);
        return d + (d === 1 ? ' day' : ' days') + ' left';
    }
    function ago(seconds) {
        if (seconds < 60) return 'just now';
        if (seconds < 3600) { var m = Math.floor(seconds / 60); return m + (m === 1 ? ' minute ago' : ' minutes ago'); }
        if (seconds < 86400) { var h = Math.floor(seconds / 3600); return h + (h === 1 ? ' hour ago' : ' hours ago'); }
        var d = Math.floor(seconds / 86400);
        return d + (d === 1 ? ' day ago' : ' days ago');
    }
    function megabytes(bytes, whole) { var v = bytes / 1048576; return whole && v === Math.round(v) ? String(Math.round(v)) : v.toFixed(1); }
    function minutesWords(n) { return n + (n === 1 ? ' minute' : ' minutes'); }

    // The description as the admin sees it: one line, no control or invisible characters, at most 100 characters (the file keeps its own bytes; the server cleans it too)
    function cleanDescription(text) {
        var t = String(text === undefined || text === null ? '' : text).replace(/[\u0000-\u001f\u007f-\u009f]/g, ' ');
        t = t.replace(/[\u00ad\u200b-\u200f\u202a-\u202e\u2060-\u206f\ufeff\ufff9-\ufffb]|\udb40[\udc00-\udc7f]/g, '').replace(/\s+/g, ' ').replace(/^ | $/g, '');
        var a = Array.from(t);
        return a.length > 100 ? a.slice(0, 100).join('') : t;
    }

    // What a username box holds, as the hub is asked for it: {account} or {why}. A number as long as an id is refused here (an id is like a password: it is never typed into this page, shown by it or sent);
    // a shorter number is a username made of digits, which the hub wants written with an @ in front.
    function accountOf(text) {
        var t = String(text === undefined || text === null ? '' : text).replace(/^\s+|\s+$/g, '');
        if (!t) return { why: 'Type a username first.' };
        if (ID_SHAPED.test(t)) return { why: 'That looks like an account number. Type the username instead.' };
        if (!/^[!-~]{1,40}$/.test(t)) return { why: 'A username has no spaces and is at most 40 characters.' };
        return { account: /^[0-9]+$/.test(t) ? '@' + t : t, shown: t };
    }

    function create(env) {
        var doc = env.doc;
        var app = doc.getElementById('app');
        var me = null;                                      // GET /admin/api/me: { name, ref, role, csrf }
        var hub = null;                                     // GET /hub/admin/api/me: { csrf } (asked once, when the Chat section needs the token of a change)
        var hubAsk = null;                                  // the call for it, while it is on its way
        var current = null;                                 // the section that is shown
        var host = null;                                    // where the section is drawn
        var tabsBox = null, select = null, waiting = 0;     // the tabs, the select of a phone, and the number of maps that wait (the badge)
        var cleanup = null;                                 // what a section must undo when another screen takes its place (the key listener of a question)
        var toastTimer = null;
        function tick() { return (env.tick || env.now)(); }                      // a clock that only goes forward, in milliseconds (the page's other clock, now, says the time of day and can be frozen)
        var inerted = [];                                   // the nodes that a question made inert
        var menu = null;                                    // the person's menu while it is open: { node, off }
        var cardSeq = 0;                                    // a card that waits for the name of the person is not drawn over a page that came after it

        function el(tag, cls, text) {
            var n = doc.createElement(tag);
            if (cls) n.className = cls;
            if (text !== undefined && text !== null) n.textContent = text;
            return n;
        }
        // A value that a player typed: its own element, marked as data (the mark does not depend on the words around it)
        function player(tag, cls, text) {
            var n = el(tag, cls, String(text === undefined || text === null ? '' : text).replace(/[\u061c\u200e\u200f\u202a-\u202e\u2066-\u2069]/g, ''));        // (bidirectional controls are dropped: an unclosed one would scramble the words around the value)
            n.setAttribute('data-untrusted', UNTRUSTED);
            return n;
        }
        function typed(tag, cls, text) {                    // a name that a person typed (a Discord username, an admin who decided a map): data too, never an instruction
            var n = el(tag, cls, String(text === undefined || text === null ? '' : text).replace(/[\u061c\u200e\u200f\u202a-\u202e\u2066-\u2069]/g, ''));
            n.setAttribute('data-untrusted', TYPED);
            return n;
        }
        function ref(text) { return typed('code', 'refn', text); }          // the short reference of an account that has no username left
        function ptag() { return el('span', 'ptag', 'Player text'); }
        function add(parent) { for (var i = 1; i < arguments.length; i++) if (arguments[i]) parent.appendChild(arguments[i]); return parent; }
        function button(label, cls, onclick) {
            var b = el('button', 'btn ' + cls, label);
            b.type = 'button';
            if (onclick) b.addEventListener('click', onclick);
            return b;
        }

        function isHub(path) { return path.indexOf(HUB_API) === 0; }
        // One call. A change carries the token of the part that it goes to (the hub's for /hub/admin/api, the game server's for /admin/api); with no token nothing is sent.
        function call(method, path, body) {
            var init = { method: method, credentials: 'same-origin', headers: {} };
            if (method === 'POST') {
                var token = isHub(path) ? hub && hub.csrf : me && me.csrf;
                if (!token) return Promise.resolve({ status: 0, json: {} });
                init.headers['Content-Type'] = 'application/json';
                init.headers['X-Ants-Csrf'] = token;
                init.body = JSON.stringify(body || {});
            }
            return env.fetch(path, init).then(function (r) {
                return r.json().then(function (j) { return { status: r.status, json: j }; }, function () { return { status: r.status, json: {} }; });
            });
        }
        // The hub's token, asked for once (a failure is forgotten, so the next change asks again)
        function hubToken() {
            if (hub) return Promise.resolve(hub);
            if (!hubAsk) {
                hubAsk = call('GET', HUB_API + 'me').then(function (r) {
                    hubAsk = null;
                    if (r.status === 200 && r.json && typeof r.json.csrf === 'string' && r.json.csrf !== '') hub = { csrf: r.json.csrf };
                    else refused(r);
                    return hub;
                }, function () { hubAsk = null; return null; });
            }
            return hubAsk;
        }

        function toast(text) {
            var old = app.querySelector('.toast');
            if (old) old.parentNode.removeChild(old);
            var t = el('div', 'toast', text);
            t.setAttribute('role', 'alert');
            app.appendChild(t);
            if (env.setTimeout) {
                if (toastTimer) env.clearTimeout(toastTimer);
                toastTimer = env.setTimeout(function () { if (t.parentNode) t.parentNode.removeChild(t); }, 6000);
            }
        }
        function message(text, cls) {                       // the page's own line: an error (an alert) or a done (a status)
            var m = el('div', 'msg ' + (cls || 'bad'));
            if (text !== null && text !== undefined) m.textContent = text;
            m.setAttribute('role', cls === 'ok' ? 'status' : 'alert');
            return m;
        }

        // ---- the page behind a question is inert: the keyboard cannot walk into it and a screen reader does not read it ----
        function inertOthers(keep) {
            clearInert();
            for (var node = keep; node && node !== doc.body && node.parentNode; node = node.parentNode) {
                Array.prototype.slice.call(node.parentNode.children).forEach(function (n) { if (n !== node) { n.setAttribute('inert', ''); inerted.push(n); } });
            }
        }
        function clearInert() {
            inerted.forEach(function (n) { n.removeAttribute('inert'); });
            inerted = [];
        }

        // ---- the person's chip and its menu ----
        function closeMenu(focusBack) {
            if (!menu) return;
            menu.off();
            if (menu.node.parentNode) menu.node.parentNode.removeChild(menu.node);
            menu.chip.className = 'acct';
            menu.chip.setAttribute('aria-expanded', 'false');
            var back = menu.chip;
            menu = null;
            if (focusBack) back.focus();
        }
        function logOut() {
            closeMenu(false);
            return env.fetch('/hub/logout', { method: 'POST', credentials: 'same-origin' }).then(function (r) {
                if (r.status !== 200) { toast('That did not work. Reload the page.'); return null; }
                return start();
            }, function () { toast('The server did not answer. Try again.'); });
        }
        function openMenu(chip, wrap) {
            var node = el('div', 'amenu');
            node.setAttribute('role', 'menu');
            add(node, add(el('div', 'mtop'), typed('b', null, me.name), el('small', null, String(me.role))));
            var home = el('a', null, 'Front page');
            home.setAttribute('href', '/');
            home.setAttribute('role', 'menuitem');
            var out = el('button', null, 'Log out');
            out.type = 'button';
            out.setAttribute('role', 'menuitem');
            out.addEventListener('click', logOut);
            add(node, home, el('hr'), out);
            add(wrap, node);
            chip.className = 'acct open';
            chip.setAttribute('aria-expanded', 'true');
            function onKey(e) { if (e.key === 'Escape') closeMenu(true); }
            function onPress(e) { if (!wrap.contains(e.target)) closeMenu(false); }
            doc.addEventListener('keydown', onKey, true);
            doc.addEventListener('click', onPress, true);
            menu = { node: node, chip: chip, off: function () { doc.removeEventListener('keydown', onKey, true); doc.removeEventListener('click', onPress, true); } };
            home.focus();
        }
        function whoChip() {
            var wrap = el('div', 'acctw');
            var name = typed('b', null, me.name);
            var chip = el('button', 'acct');
            chip.type = 'button';
            chip.setAttribute('aria-haspopup', 'menu');
            chip.setAttribute('aria-expanded', 'false');
            add(chip, el('span', 'av', Array.from(name.textContent)[0] || '?'), add(el('span', 'wn'), name, el('small', me.role === 'owner' ? 'own' : null, String(me.role))), el('span', 'car', '\u25be'));
            chip.addEventListener('click', function () { if (menu) closeMenu(false); else openMenu(chip, wrap); });
            return add(wrap, chip);
        }

        // ---- a visitor the door does not let in: one card in the middle of the page ----
        function leave() {                                  // another screen takes the place of a section: what it listens to goes, and so does the dimming
            if (cleanup) { cleanup(); cleanup = null; }
            clearInert();
        }
        function card(inner) {
            leave();
            closeMenu(false);
            app.textContent = '';
            return add(app, add(el('div', 'alg'), inner));
        }
        function discordMark() {                            // the mark of the bar's Discord button, as it is there
            var src = doc.querySelector ? doc.querySelector('.sb svg.dc') : null;
            if (!src || !src.cloneNode) return null;
            var c = src.cloneNode(true);
            c.setAttribute('width', '24');
            c.setAttribute('height', '24');
            return c;
        }
        function cardOf(kind, name) {                       // kind: out (nobody), none (a person with no role), mod (a moderator), bare (a person whom nobody has named yet), line (no way in)
            var c = el('div', 'lcard');
            if (kind === 'line') return add(c, message(NOT_AVAILABLE));
            add(c, el('h1', null, CARD_HEAD));
            if (kind === 'out') {                           // nobody is logged in: a plain link to the Discord login
                var go = el('a', 'btn big', null);
                go.setAttribute('href', LOGIN_HREF);
                add(go, discordMark(), doc.createTextNode('Log in with Discord'));
                return add(c, el('div', 'l2', LOGIN_LINE), add(el('div', 'btnrow'), go));
            }
            if (kind === 'bare') return c;
            var l2 = el('div', 'l2');
            add(l2, doc.createTextNode('You are logged in as '), typed('b', null, name), doc.createTextNode(kind === 'mod' ? ', a moderator. The tools for moderators are on the front page.' : '.'));
            var home = el('a', 'btn big', 'Front page');
            home.setAttribute('href', '/');
            add(c, l2, add(el('div', 'btnrow'), home, button('Log out', 'big ghost', logOut)));
            if (kind === 'none') add(c, el('p', 'small', 'Made an admin just now? Wait a minute and reload this page.'));
            return c;
        }
        // The card that /admin/api/me and every later refusal give. A person who is logged in and not allowed is named by the hub's own answer (GET /hub/me), which comes a moment after the heading.
        function showRefusal(status, json) {
            me = null;
            hub = null;
            current = null;
            var e = json && json.error;
            var seq = ++cardSeq;
            if (status === 401) { card(cardOf('out')); return Promise.resolve(); }
            if (!(status === 403 && (e === 'not_allowed' || e === 'admin'))) { card(cardOf('line')); return Promise.resolve(); }         // closed, a part that is not running, no answer, anything else
            card(cardOf('bare'));
            env.fetch('/hub/me', { credentials: 'same-origin' }).then(function (r) { return r.status === 200 ? r.json() : null; }).then(function (j) {
                if (seq !== cardSeq) return;
                var you = j && j.you;
                if (j && j.login === true && !you) { card(cardOf('out')); return; }                      // (the login ended in the meantime)
                if (you && typeof you.name === 'string') card(cardOf(you.role === 'mod' ? 'mod' : 'none', you.name));
            }, function () {});
            return Promise.resolve();
        }
        // A call that the door refuses (the login ended, the role was taken away, the door closed): the page swaps to the card that GET /admin/api/me would give. True when it did.
        function refused(r) {
            var e = r.json && r.json.error;
            if (r.status === 401 || (r.status === 403 && (e === 'not_allowed' || e === 'closed' || e === 'admin'))) { showRefusal(r.status, r.json); return true; }
            return false;
        }

        function onBackdrop(node, close) {         // a press that starts and ends on the backdrop itself closes the box as its Cancel does (a drag from inside the box, a text selection or a scroll does not)
            var down = false, hit = false;
            node.addEventListener('pointerdown', function (e) { down = e.target === node; hit = false; });
            node.addEventListener('pointerup', function (e) { hit = down && e.target === node; down = false; });
            node.addEventListener('pointercancel', function () { down = hit = false; });
            node.addEventListener('click', function (e) { var h = hit; hit = false; if (h && e.target === node) close(); });
        }
        // The box of a question: the dimmed page, the box in it, and the keys (Escape is Cancel; the focus stays inside the box: Tab from the last button goes to the first, Shift+Tab the other way)
        function questionBox(dlg, cancel, go, closeDialog) {
            var scrim = el('div', 'scrim');
            // the keys are heard on the document (a click on plain text moves the focus to the body, and Escape must still close the box)
            function onKey(e) {
                if (e.key === 'Escape') { closeDialog(); return; }
                if (e.key !== 'Tab') return;
                var now = doc.activeElement;
                if (!dlg.contains(now) || (e.shiftKey && now === cancel)) { e.preventDefault(); (e.shiftKey ? go : cancel).focus(); }
                else if (!e.shiftKey && now === go) { e.preventDefault(); cancel.focus(); }
            }
            add(scrim, dlg);
            onBackdrop(scrim, closeDialog);
            return { node: scrim, focus: cancel, onKey: onKey };
        }

        // ---- the admin's shell: the title and the chip, the sections as tabs (a select on a phone), and the section that is shown ----
        var SECTIONS = [
            { id: 'maps', title: 'Maps', render: renderMaps },
            { id: 'chat', title: 'Chat', render: renderChat },
            { id: 'log', title: 'Log', render: renderLog },
            { id: 'status', title: 'Status', render: renderStatus }
        ];

        function paintTabs() {
            Array.prototype.slice.call(tabsBox.children).forEach(function (b, i) {
                var s = SECTIONS[i];
                b.textContent = '';
                b.setAttribute('aria-selected', s === current ? 'true' : 'false');
                add(b, el('span', null, s.title));
                if (s.id === 'maps' && waiting > 0) add(b, el('span', 'cnt', String(waiting)));
            });
            Array.prototype.slice.call(select.children).forEach(function (o, i) {
                var s = SECTIONS[i];
                o.textContent = s.title + (s.id === 'maps' && waiting > 0 ? ' (' + waiting + ' waiting)' : '');
                o.selected = s === current;
            });
            select.value = current.id;
        }
        function show(section) {
            if (section === current && host.children.length) return;
            leave();
            current = section;
            paintTabs();
            host.textContent = '';
            section.render(host);
        }
        function sectionBox() {
            var wrap = el('div');
            tabsBox = el('div', 'atabs');
            tabsBox.setAttribute('role', 'tablist');
            tabsBox.setAttribute('aria-label', 'Sections');
            select = el('select');
            select.id = 'asec';
            SECTIONS.forEach(function (s, i) {
                var b = el('button', 'atab');
                b.type = 'button';
                b.id = 'tab-' + s.id;
                b.setAttribute('role', 'tab');
                b.setAttribute('aria-controls', 'secpanel');
                b.addEventListener('click', function () { show(s); });
                b.addEventListener('keydown', function (e) {                    // (the arrow keys move between the tabs, as a tab list does)
                    var to = e.key === 'ArrowRight' ? i + 1 : e.key === 'ArrowLeft' ? i - 1 : -1;
                    if (to < 0 || to >= SECTIONS.length) return;
                    e.preventDefault();
                    show(SECTIONS[to]);
                    tabsBox.children[to].focus();
                });
                add(tabsBox, b);
                var o = el('option');
                o.value = s.id;
                add(select, o);
            });
            select.addEventListener('change', function () { SECTIONS.forEach(function (s) { if (s.id === select.value) show(s); }); });
            var label = el('label', null, 'Section');
            label.setAttribute('for', 'asec');
            add(wrap, tabsBox, add(el('div', 'asel'), label, select));
            return wrap;
        }
        function showShell() {
            leave();
            closeMenu(false);
            cardSeq++;
            app.textContent = '';
            var pt = el('div', 'pt apt');
            add(pt, el('h1', null, 'Admin'), el('p', null, TITLE_LINE), whoChip());
            host = el('div');
            host.id = 'secpanel';
            host.setAttribute('role', 'tabpanel');
            add(app, pt, sectionBox(), host);
            current = null;
            waiting = 0;
            show(SECTIONS[0]);
        }

        // ---- the section "Maps" (the maps that players sent) ----
        function renderMaps(host) {
            var st = { tab: 'waiting', data: null, open: null, busy: false, dialog: null, dialogAt: null, dialogKind: null, dialogBody: null, keyOff: null, focusBack: null, dead: false };
            cleanup = function () { st.dead = true; if (st.keyOff) { st.keyOff(); st.keyOff = null; } };          // (another screen took the page: nothing of this one is drawn or heard any more)

            function load(tab) {
                st.tab = tab || st.tab;
                return call('GET', '/admin/api/maps?state=' + encodeURIComponent(st.tab)).then(function (r) {
                    if (refused(r)) return null;
                    if (r.status !== 200) { toast('The list could not be loaded. Reload the page.'); return null; }
                    if (st.dead) return null;
                    st.data = r.json;
                    waiting = (r.json.counts && r.json.counts.waiting) || 0;
                    paintTabs();
                    st.open = null;
                    draw();
                    return null;
                }, function () { toast('The server did not answer. Reload the page.'); });
            }

            function act(id, action, body, done) {
                if (st.busy) return;
                st.busy = true;
                function release() { st.busy = false; }
                call('POST', '/admin/api/maps/' + id + '/' + action, body).then(function (r) {
                    if (r.status === 200) {
                        st.focusBack = st.dialog ? st.dialog.id : null;
                        st.dialog = null;
                        Promise.resolve(load()).then(release, release);          // (the buttons stay locked until the list is drawn again: a late second tap cannot land on the map that moved into the place of this one)
                        return;
                    }
                    release();
                    if (refused(r)) return;
                    if (done && done(r)) return;
                    toast(r.json && r.json.message ? r.json.message : 'That did not work. Reload the page.');
                }, function () { release(); toast('The server did not answer. Try again.'); });
            }

            function tags(m) {
                var t = el('span', 'tags');
                if (m.kind === 'extended') add(t, el('span', 'tag tag-ext', 'Extended map'), el('span', 'badge-nv', 'New version only'));
                else add(t, el('span', 'tag tag-classic', 'Classic map'));
                return t;
            }
            function picture(m) {
                var img = el('img');
                img.src = '/admin/api/maps/' + m.id + '/picture';
                img.alt = '';
                img.loading = 'lazy';                         // (a long list asks for the pictures that are on the screen)
                img.width = 76;
                img.height = 76;
                return img;
            }
            function shown(m) { return m.state === 'approved' || m.state === 'taken_down' ? m.approved_name : m.name; }
            function meta(m) {
                var stamp = m.state === 'waiting' ? m.sent_at : m.decided_at;
                var line = el('span');
                add(line, doc.createTextNode('By '), player('span', 'who', m.maker), doc.createTextNode(' '), ptag(), doc.createTextNode(' · ' + m.width + ' x ' + m.height + ' · ' + VERBS[st.tab] + ' ' + when(stamp, env.now())));
                if (st.tab !== 'waiting' && m.decided_by) add(line, doc.createTextNode(' by '), typed('span', 'who', m.decided_by));          // (the admin who decided, by the name of the Discord login, or the words "earlier admin": a name that was typed, so it is marked)
                return line;
            }

            function renameBox(m) {
                var box = el('div', 'rn');
                if (m.name_taken) add(box, add(el('div', 'rn1'), doc.createTextNode('The name “'), player('span', null, m.name), doc.createTextNode('” is already used by another map.')));
                var input = el('input');
                input.type = 'text';
                input.id = 'rename-' + m.id;
                input.maxLength = 60;
                input.value = st.open.typed || st.open.suggestion || m.name;
                var label = el('label', null, 'Name for this map');
                label.setAttribute('for', input.id);
                var fld = add(el('span', 'fld'), label, input);
                var why = el('div', 'why');
                why.hidden = !st.open.why;
                if (st.open.why) why.textContent = st.open.why;
                function nextName() {
                    call('GET', '/admin/api/next-name?name=' + encodeURIComponent(input.value)).then(function (r) {
                        if (refused(r)) return;
                        if (r.status === 200) { input.value = r.json.name; why.hidden = true; }
                        else { why.textContent = r.json && r.json.message ? r.json.message : 'No free name was found.'; why.hidden = false; }
                    });
                }
                add(box, add(el('div', 'rn2'), fld, button('Use next name', 'sm', nextName)));
                add(box, el('div', 'rn3', '“Use next name” puts the next free name in the box' + (st.open.suggestion ? ' (here: ' + st.open.suggestion + ')' : '') + '. You can also type your own. A name that is already taken is never replaced.'), why);
                var save = button('Save name and approve', 'ok', function () { st.open.typed = input.value; st.dialog = m; st.dialogKind = 'approve'; st.dialogBody = { name: input.value }; draw(); });
                add(box, add(el('div', 'rn4'), button('Cancel', 'ghost', function () { st.open = null; draw(); }), save));
                return box;
            }

            function declineBox(m) {
                var box = el('div', 'rn');
                var area = el('textarea');
                area.id = 'decline-' + m.id;
                area.maxLength = 200;
                var label = el('label', null, 'Why? The maker can read this (a short sentence)');
                label.setAttribute('for', area.id);
                var why = el('div', 'why');
                why.hidden = true;
                add(box, add(el('div', 'fld'), label, area), why);
                var go = button('Decline', 'bad', function () {
                    if (!area.value.replace(/\s+/g, '')) { why.textContent = 'Please write a short reason.'; why.hidden = false; area.className = 'err'; return; }
                    act(m.id, 'decline', { reason: area.value }, function (r) {
                        if (r.json && r.json.error === 'bad_reason') { why.textContent = r.json.message; why.hidden = false; return true; }
                        return false;
                    });
                });
                add(box, add(el('div', 'rn4'), button('Cancel', 'ghost', function () { st.open = null; draw(); }), go));
                return box;
            }

            function row(m) {
                var group = el('div', 'wgrp');
                var r = el('div', 'arow');
                var info = el('div');
                add(info, add(el('div', 't1'), player('span', null, shown(m)), tags(m), ptag()), add(el('div', 'am'), meta(m)));
                if (st.tab === 'waiting' && m.note) add(info, add(el('div', 'qbox'), el('div', 'qhead', LABEL), player('div', 'quote', m.note)));
                if (st.tab === 'declined' && m.message) add(info, el('div', 'quote', 'Reason: ' + m.message));
                var acts = el('div', 'ab');
                if (st.tab === 'waiting') {
                    var ap = button('Approve', 'big ok', function () { st.dialog = m; st.dialogKind = 'approve'; st.dialogBody = null; draw(); });
                    ap.setAttribute('data-approve', m.id);
                    add(acts, ap);
                    add(acts, button('Rename and approve', 'big', function () {
                        if (m.name_taken) {
                            call('GET', '/admin/api/next-name?name=' + encodeURIComponent(m.name)).then(function (res) {
                                if (refused(res)) return;
                                st.open = { id: m.id, kind: 'rename', suggestion: res.status === 200 ? res.json.name : '' };
                                draw();
                            });
                        } else { st.open = { id: m.id, kind: 'rename', suggestion: '' }; draw(); }
                    }));
                    add(acts, button('Decline', 'big bad', function () { st.open = { id: m.id, kind: 'decline' }; draw(); }));
                } else if (st.tab === 'approved') {
                    var td = button('Take down', 'big bad', function () { st.dialog = m; st.dialogKind = 'take-down'; st.dialogBody = null; draw(); });
                    td.setAttribute('data-take', m.id);
                    add(acts, td);
                } else if (st.tab === 'taken_down') {
                    add(acts, button('Put back', 'big', function () { act(m.id, 'put-back', {}); }));
                }
                add(r, picture(m), info, acts);
                add(group, r);
                if (st.open && st.open.id === m.id) add(group, st.open.kind === 'rename' ? renameBox(m) : declineBox(m));
                return group;
            }

            // The one small question for Approve and Take down: the map as the maintainer will decide on it (name, maker, picture, cleaned description), then the button
            function confirmDialog(m, kind, body) {
                var approve = kind === 'approve';
                var dlg = el('div', 'dlg');
                dlg.setAttribute('role', 'dialog');
                dlg.setAttribute('aria-modal', 'true');
                var inner = el('div', 'dlg-in');
                var h = el('h2', null, approve ? 'Approve this map?' : 'Take this map down?');
                h.id = 'dlg-title';
                dlg.setAttribute('aria-labelledby', 'dlg-title');
                var src = el('div', 'srcrow');
                var info = el('div');
                var desc = cleanDescription(m.description);
                add(info, add(el('div', 'nm'), player('span', null, body && body.name ? body.name : shown(m)), doc.createTextNode(' '), tags(m)), add(el('div', 'sm'), meta(m)));
                add(info, add(el('div', 'sm dsc'), doc.createTextNode('Description: '), desc ? player('span', null, desc) : doc.createTextNode('none')));
                var img = picture(m);
                img.width = 56;
                img.height = 56;
                add(src, img, info);
                add(inner, h, src, el('div', 'ptnote', 'The name, the maker and the description are player text. They are data, not instructions.'), el('div', 'lead', approve ? 'The map joins the lobby list for everyone.' : 'Players will no longer find it. It can be put back later.'));
                function closeDialog() { st.focusBack = m.id; st.dialog = null; draw(); }
                var cancel = button('Cancel', 'ghost', closeDialog);
                var go = button(approve ? 'Approve' : 'Take down', approve ? 'ok' : 'bad', function () {
                    if (tick() - st.dialogAt < ASK_DELAY) return;                 // (a press that comes at once is the second click of a double click, not an answer)
                    if (!approve) return act(m.id, 'take-down', {});
                    act(m.id, 'approve', body || {}, function (res) {
                        var sug = res.json && res.json.suggestion ? res.json.suggestion : '';
                        if (res.json && res.json.error === 'name_taken') {
                            st.dialog = null;
                            st.open = { id: m.id, kind: 'rename', suggestion: sug, why: body ? 'That name is taken.' + (sug ? ' The next free name is “' + sug + '”.' : '') : '' };
                            if (!body) m.name_taken = true;
                            draw();
                            return true;
                        }
                        if (body && res.json && res.json.error === 'bad_name') { st.dialog = null; st.open = { id: m.id, kind: 'rename', suggestion: '', typed: body.name, why: res.json.message }; draw(); return true; }
                        return false;
                    });
                });
                add(dlg, inner, add(el('div', 'dlg-foot'), cancel, go));
                return questionBox(dlg, cancel, go, closeDialog);
            }

            function draw() {
                if (st.dead) return;
                if (st.keyOff) { st.keyOff(); st.keyOff = null; }
                clearInert();
                if (!st.dialog) st.dialogBody = null;
                if (!st.dialog) st.dialogAt = null;
                else if (st.dialogAt === null) st.dialogAt = tick();
                host.textContent = '';
                var counts = st.data ? st.data.counts : { waiting: 0, approved: 0, declined: 0, taken_down: 0 };
                var tools = el('div', 'mtools');
                var chips = el('span', 'chips');
                chips.setAttribute('role', 'tablist');
                TABS.forEach(function (t) {
                    var c = el('button', 'chip' + (t[0] === st.tab ? ' on' : ''), t[1] + ' (' + (counts[t[0]] || 0) + ')');
                    c.type = 'button';
                    c.setAttribute('role', 'tab');
                    c.setAttribute('aria-selected', t[0] === st.tab ? 'true' : 'false');
                    c.addEventListener('click', function () { if (t[0] !== st.tab) { st.data = null; st.open = null; load(t[0]); } });
                    add(chips, c);
                });
                add(host, add(tools, chips));
                if (!st.data) return;
                if (st.tab === 'waiting') add(host, el('div', 'zone', ZONE_LINE));
                var maps = st.data.maps || [];
                if (!maps.length) {
                    var empty = el('div', 'empty');
                    add(empty, el('div', 'e1', EMPTY[st.tab][0]), el('div', 'e2', EMPTY[st.tab][1]));
                    if (st.tab === 'approved' && counts.waiting > 0) add(empty, button('Go to maps waiting (' + counts.waiting + ')', 'sm', function () { st.data = null; load('waiting'); }));
                    add(host, empty);
                } else {
                    var list = el('div', 'alist');
                    maps.forEach(function (m) { add(list, row(m)); });
                    add(host, el('div', 'alead', LEADS[st.tab]), list);
                }
                add(host, el('div', 'qfoot', FOOTS[st.tab]));
                if (st.dialog) {
                    var d = confirmDialog(st.dialog, st.dialogKind, st.dialogBody);
                    add(host, d.node);
                    inertOthers(d.node);
                    doc.addEventListener('keydown', d.onKey, true);
                    st.keyOff = function () { doc.removeEventListener('keydown', d.onKey, true); };
                    d.focus.focus();
                } else if (st.focusBack) {                                       // (the box is gone: the focus goes back to the button that opened it, else to the chosen tab)
                    var back = host.querySelector('[data-take="' + st.focusBack + '"]') || host.querySelector('[data-approve="' + st.focusBack + '"]') || host.querySelector('.chip.on');
                    st.focusBack = null;
                    if (back) back.focus();
                }
            }

            draw();
            return load('waiting');
        }

        // ---- the section "Chat": mute or ban someone, the lists of the banned and the muted, and the pictures that are hidden ----
        function panel(title, hint) {
            var p = el('section', 'panel');
            var h = el('h2', null, title);
            add(p, h);
            if (hint) add(p, el('p', 'ph', hint));
            return p;
        }
        function field(label, id, placeholder, after) {                                      // a box to type in under its label; `after` (the presets of the minutes) sits beside it
            var f = el('div', 'f');
            var l = el('label', null, label);
            l.setAttribute('for', id);
            var inw = el('div', 'inw');
            var i = el('input', 'plain');
            i.type = 'text';
            i.id = id;
            if (placeholder) i.setAttribute('placeholder', placeholder);
            i.setAttribute('autocomplete', 'off');
            i.setAttribute('autocapitalize', 'off');
            i.setAttribute('spellcheck', 'false');
            add(inw, i);
            add(f, l, after ? add(el('div', 'minrow'), inw, after) : inw);
            return { box: f, input: i };
        }
        function personName(x) { return x.name ? typed('div', 'pn', x.name) : add(el('div', 'pn'), ref(x.ref)); }
        // who did it, as a small line says it: the username that the hub knows, else the short reference; the command line is the terminal
        function byNode(x) {
            if (x.by === 'command line') return doc.createTextNode('terminal');
            return x.by_name ? typed('span', null, x.by_name) : REFERENCE.test(x.by || '') ? ref(x.by) : doc.createTextNode('someone');
        }
        function personRows(list, label, line, onPress) {
            var box = el('div', 'plist');
            list.forEach(function (x) {
                var r = el('div', 'prow');
                add(r, add(el('div'), personName(x), line(x)), button(label, '', function () { onPress(x); }));
                add(box, r);
            });
            return box;
        }

        function renderChat(host) {
            var st = { busy: false, keyOff: null };
            var msgBox = el('div'), bannedBox = el('div'), mutedBox = el('div'), hiddenBox = el('div');
            cleanup = function () { if (st.keyOff) { st.keyOff(); st.keyOff = null; } };

            // the form that mutes or bans
            var presets = el('span', 'chips');
            var user = field('Username', 'cm-user', 'for example clover.4782');
            var minutes = field('Minutes (a mute only)', 'cm-min', null, presets);
            minutes.input.value = '60';
            minutes.input.setAttribute('inputmode', 'numeric');
            PRESETS.forEach(function (p) {
                var c = el('button', 'chip', p[0]);
                c.type = 'button';
                c.addEventListener('click', function () { minutes.input.value = String(p[1]); });
                add(presets, c);
            });
            add(minutes.box, el('div', 'fh', '1 to ' + MAX_MINUTES + ' (one week). A mute ends by itself.'));
            var banButton = button('Ban', 'big bad', function () { askBan(); });
            var form = panel('Mute or ban someone');
            form.className += ' p-form';
            add(form, msgBox, user.box, minutes.box, add(el('div', 'acts'), button('Mute', 'big', function () { mute(); }), banButton));

            // the lists, and the box that hides a picture
            var banned = panel('Banned', 'They can read the chat but cannot write.'), muted = panel('Muted', 'A mute ends by itself at the time shown.'), pics = panel('Hide a picture', 'Hides a person’s Discord picture in the chat; their letter shows instead.');
            banned.className += ' p-banned';
            muted.className += ' p-muted';
            pics.className += ' p-pics';
            var picUser = field('Username', 'hp-user', 'for example marlow.old');
            var hiddenHead = el('h2', 'sub', 'Hidden now');
            add(banned, bannedBox);
            add(muted, mutedBox);
            add(pics, picUser.box, add(el('div', 'acts'), button('Hide picture', 'big', function () { hidePicture(); })), hiddenHead, hiddenBox, el('p', 'ph', 'After “Show again” the picture stays off until the person turns it on themselves.'));
            var left = el('div', 'cc'), right = el('div', 'cc');
            add(left, form, pics);
            add(right, banned, muted);
            add(host, add(el('div', 'cgrid'), left, right));

            function say(parts, cls) {                                                                      // the line in the form: an error or a done (words and names)
                msgBox.textContent = '';
                if (!parts) return;
                var m = message(null, cls);
                parts.forEach(function (n) { add(m, typeof n === 'string' ? doc.createTextNode(n) : n); });
                add(msgBox, m);
            }
            function paint(box, head, title, rows, none, build) {                                           // a list: its count in the heading, its rows or the words for none, or that it could not be had
                box.textContent = '';
                var old = head.querySelector('.n');
                if (old) head.removeChild(old);
                if (rows === null) { add(box, message('The list could not be loaded.')); return; }
                add(head, el('span', 'n', '(' + rows.length + ')'));
                add(box, rows.length ? build(rows) : el('p', 'none', none));
            }
            function loadLists() {
                return Promise.all([call('GET', HUB_API + 'bans'), call('GET', HUB_API + 'mutes'), call('GET', HUB_API + 'hidden')]).then(function (rs) {
                    for (var i = 0; i < rs.length; i++) if (refused(rs[i])) return;
                    var bans = rs[0].status === 200 ? rs[0].json.bans || [] : null, mutes = rs[1].status === 200 ? rs[1].json.mutes || [] : null, hidden = rs[2].status === 200 ? rs[2].json.hidden || [] : null;
                    var now = env.now();
                    paint(bannedBox, banned.children[0], 'Banned', bans, 'Nobody is banned.', function (rows) {
                        return personRows(rows, 'Unban', function (x) { return add(el('div', 'ps'), doc.createTextNode('banned ' + when(x.at, now) + ' · by '), byNode(x)); }, function (x) { rowChange('unban', x, 'is unbanned'); });
                    });
                    paint(mutedBox, muted.children[0], 'Muted', mutes, 'Nobody is muted.', function (rows) {
                        return personRows(rows, 'Unmute', function (x) { return add(el('div', 'ps'), doc.createTextNode('until ' + untilWords(x.until, now) + ' (' + leftWords(x.until, now) + ') · by '), byNode(x)); }, function (x) { rowChange('unmute', x, 'is unmuted'); });
                    });
                    paint(hiddenBox, hiddenHead, 'Hidden now', hidden, 'No picture is hidden.', function (rows) {
                        return personRows(rows, 'Show again', function () { return el('div', 'ps', 'picture hidden'); }, function (x) { rowChange('pic', x, 'has the picture shown again'); });
                    });
                }, function () {
                    paint(bannedBox, banned.children[0], 'Banned', null);
                    paint(mutedBox, muted.children[0], 'Muted', null);
                    paint(hiddenBox, hiddenHead, 'Hidden now', null);
                });
            }

            // One change: the words for what the hub answers, and the lists asked again afterwards
            function change(name, body, who, doneWords, box) {
                if (st.busy) return;
                st.busy = true;
                function release() { st.busy = false; }
                hubToken().then(function () {
                    return call('POST', HUB_API + name, body).then(function (r) {                  // (with no token of the hub this sends nothing and answers with status 0)
                        if (r.status === 200) {
                            if (box) box.value = '';
                            say(['Done: ', who, ' ' + doneWords + '.'], 'ok');
                            Promise.resolve(loadLists()).then(release, release);
                            return;
                        }
                        release();
                        if (refused(r)) return;
                        say(failure(name, r, who), 'bad');
                    }, function () { release(); say(['The server did not answer. Try again.'], 'bad'); });
                });
            }
            function failure(name, r, who) {
                var e = r.json && r.json.error;
                if (r.status === 404 && e === 'who') return ['No account has the username “', who, '”. Check the spelling in the lists below.'];
                if (r.status === 403 && e === 'power') return ['You may not do that to this person.'];
                if (r.status === 409 && e === 'bad') {
                    var words = { ban: ' is banned already.', unban: ' is not banned.', unmute: ' is not muted.', nopic: ': the picture is hidden already.', pic: ': the picture is not hidden.' };
                    return [who, words[name] || ': nothing changed.'];
                }
                if (r.status === 503 && e === 'role') return ['Discord did not confirm the roles just now, so nothing was done. Try again in a minute.'];
                if (r.status === 403 && (e === 'csrf' || e === 'origin')) return ['The page’s token is old. Reload the page.'];
                if (r.status === 429) return ['Too many requests. Wait a few seconds and try again.'];
                return ['That did not work. Reload the page.'];
            }
            function rowChange(name, x, words) {                                                            // a button of a list: the account is its reference, never a name that could mean another person
                change(name, { account: x.ref }, x.name ? typed('span', null, x.name) : ref(x.ref), words);
            }
            function named(input) {                                                                          // the username of a box, or the line that says what is wrong with it
                var a = accountOf(input.value);
                input.className = a.why ? 'plain err' : 'plain';
                if (a.why) say([a.why], 'bad');
                return a.why ? null : a;
            }
            function mute() {
                var a = named(user.input);
                if (!a) return;
                var text = String(minutes.input.value).replace(/^\s+|\s+$/g, '');
                var n = /^[0-9]{1,5}$/.test(text) ? parseInt(text, 10) : 0;
                minutes.input.className = n >= 1 && n <= MAX_MINUTES ? 'plain' : 'plain err';
                if (n < 1 || n > MAX_MINUTES) { say(['Minutes: a whole number from 1 to ' + MAX_MINUTES + '.'], 'bad'); return; }
                change('mute', { account: a.account, minutes: n }, typed('span', null, a.shown), 'is muted for ' + minutesWords(n), user.input);
            }
            function hidePicture() {
                var a = named(picUser.input);
                if (!a) return;
                change('nopic', { account: a.account }, typed('span', null, a.shown), 'has the picture hidden', picUser.input);
            }
            // The question before a ban: the name as it was typed, then Cancel or the red Ban
            function askBan() {
                var a = named(user.input);
                if (!a || st.keyOff) return;
                var dlg = el('div', 'dlg');
                dlg.setAttribute('role', 'dialog');
                dlg.setAttribute('aria-modal', 'true');
                dlg.setAttribute('aria-labelledby', 'dlg-title');
                var h = el('h2', null);
                h.id = 'dlg-title';
                add(h, doc.createTextNode('Ban '), typed('span', null, a.shown), doc.createTextNode('?'));
                var src = el('div', 'srcrow');
                add(src, add(el('div'), typed('div', 'nm', a.shown), el('div', 'sm', 'as typed in the Username box')));
                add(dlg, add(el('div', 'dlg-in'), h, src, el('div', 'lead', 'They can still read the chat but cannot write until an admin lifts the ban.')));
                var shell, opened = tick();
                function closeAsk() {
                    if (st.keyOff) { st.keyOff(); st.keyOff = null; }
                    if (shell.node.parentNode) shell.node.parentNode.removeChild(shell.node);
                    clearInert();
                    banButton.focus();
                }
                var cancel = button('Cancel', 'ghost', closeAsk);
                var go = button('Ban', 'bad', function () { if (tick() - opened < ASK_DELAY) return; closeAsk(); change('ban', { account: a.account }, typed('span', null, a.shown), 'is banned', user.input); });
                add(dlg, add(el('div', 'dlg-foot'), cancel, go));
                shell = questionBox(dlg, cancel, go, closeAsk);
                add(host, shell.node);
                inertOthers(shell.node);
                doc.addEventListener('keydown', shell.onKey, true);
                st.keyOff = function () { doc.removeEventListener('keydown', shell.onKey, true); };
                shell.focus.focus();
            }

            return hubToken().then(function () { return loadLists(); });
        }

        // ---- the section "Log": what moderators and admins did in the chat, and the decisions on maps, newest first ----
        function renderLog(host) {
            var st = { mode: 'all', chat: null, maps: null };
            var tools = el('div', 'mtools lhead'), lead = el('p', 'alead'), body = el('div');
            add(host, tools, lead, body);

            function who(actor, name) {                                                                 // a person is a username, else the short code that the hub keeps; the command line is the terminal
                var w = el('span', 'wh');
                if (actor === 'terminal') return add(w, doc.createTextNode('terminal'));
                if (name) return add(w, typed('span', null, name));
                return add(w, REFERENCE.test(actor || '') ? ref(actor) : doc.createTextNode('someone'));
            }
            function tag(src, tagged) { return tagged ? el('span', 'src ' + src, src === 'chat' ? 'Chat' : 'Map') : null; }         // (the filter All says which list a row came from)
            function chatText(r, tagged) {
                var wt = add(el('span', 'wt'), tag('chat', tagged));
                var target = r.target ? (r.target_name ? typed('span', null, r.target_name) : ref(r.target)) : null;
                if (r.action === 'del') {
                    add(wt, doc.createTextNode(r.detail === 'purge' ? 'Deleted the recent lines of ' : 'Deleted line ' + r.detail + (target ? ' by ' : '')), target);
                } else if (r.action === 'mute') {
                    add(wt, doc.createTextNode('Muted '), target, doc.createTextNode(' for ' + minutesWords(parseInt(r.detail, 10) || 0)));
                } else if (CHAT_ACTIONS[r.action]) {
                    add(wt, doc.createTextNode(CHAT_ACTIONS[r.action] + ' '), target);
                } else {
                    add(wt, typed('span', null, r.action), doc.createTextNode(' '), target);
                }
                return wt;
            }
            function mapText(r, tagged) {
                var wt = add(el('span', 'wt'), tag('map', tagged));
                add(wt, doc.createTextNode(MAP_ACTIONS[r.action] || ''), typed('em', null, r.target));
                if (r.detail) add(wt, doc.createTextNode(' '), r.action === 'map_declined' ? add(el('span', 'why', 'Reason: '), typed('span', null, r.detail)) : typed('span', 'why', r.detail));
                if (!MAP_ACTIONS[r.action]) add(wt, doc.createTextNode(' '), typed('span', null, r.action));
                return wt;
            }
            function draw() {
                var chat = st.chat || [], maps = st.maps || [], now = env.now();
                tools.textContent = '';
                var chips = el('span', 'chips');
                chips.setAttribute('role', 'tablist');
                [['all', 'All (' + (chat.length + maps.length) + ')'], ['chat', 'Chat actions (' + chat.length + ')'], ['maps', 'Map decisions (' + maps.length + ')']].forEach(function (c) {
                    var b = el('button', 'chip' + (c[0] === st.mode ? ' on' : ''), c[1]);
                    b.type = 'button';
                    b.setAttribute('role', 'tab');
                    b.setAttribute('aria-selected', c[0] === st.mode ? 'true' : 'false');
                    b.addEventListener('click', function () { if (c[0] !== st.mode) { st.mode = c[0]; draw(); } });
                    add(chips, b);
                });
                add(tools, chips);
                lead.textContent = st.mode === 'maps' ? 'What was decided about the maps that players sent.' : st.mode === 'chat' ? 'What moderators and admins did in the chat, the last 30 days.' : 'Chat actions (the last 30 days) and map decisions, newest first.';
                body.textContent = '';
                if (st.chat === null) add(body, message('The chat’s log could not be loaded.'));
                if (st.maps === null) add(body, message('The map decisions could not be loaded.'));
                var rows = [];
                if (st.mode !== 'maps') chat.forEach(function (r) { rows.push({ at: r.at, src: 'chat', who: who(r.actor, r.actor_name), what: chatText(r, st.mode === 'all') }); });
                if (st.mode !== 'chat') maps.forEach(function (r) { rows.push({ at: r.at, src: 'map', who: who(r.ref, r.who), what: mapText(r, st.mode === 'all') }); });
                rows.sort(function (a, b) { return b.at - a.at; });                                        // (newest first; a stable sort keeps the order of the log for one second)
                var hd = el('div', 'ahd');
                add(hd, el('span', null, 'When'), el('span', null, 'Who'), el('span', null, 'What'));
                var list = el('div', 'alist2');
                rows.forEach(function (r) {
                    add(list, add(el('div', 'arow2'), el('span', 'tm', when(r.at, now)), r.who, r.what));
                });
                if (rows.length) add(body, hd, list);
                else if (st.chat !== null && st.maps !== null) add(body, el('p', 'none', 'Nothing was done in this time.'));
                add(body, el('div', 'logfoot', 'People are shown by their Discord username. A person with no profile left is shown by the short code that the hub keeps for them.'));
            }
            draw();
            return Promise.all([call('GET', HUB_API + 'modlog'), call('GET', '/admin/api/audit')]).then(function (rs) {
                for (var i = 0; i < rs.length; i++) if (refused(rs[i])) return;
                st.chat = rs[0].status === 200 ? rs[0].json.modlog || [] : null;
                st.maps = rs[1].status === 200 ? rs[1].json.entries || [] : null;
                draw();
            }, function () { st.chat = st.maps = null; draw(); });
        }

        // ---- the section "Status": matches and players, the role checks, the pictures, the chat's store and the versions ----
        function renderStatus(host) {
            var top = el('div', 'stop'), upd = el('span', 'upd'), grid = el('div', 'sgrid'), refresh = button('Refresh', 'sm', function () { load(); });
            add(top, upd, refresh);
            add(host, top, grid);

            function kv(k, v, small, cls) {
                var r = el('div', 'kv' + (cls ? ' ' + cls : ''));
                var vv = el('span', 'v', v);
                if (small) add(vv, doc.createTextNode(' '), el('small', null, small));
                return add(r, el('span', 'k', k), vv);
            }
            function stamp() {                                                                           // the site's own version, from the stamps of the bar (the build wrote them in; a page that nobody stamped has none)
                var bar = doc.querySelector ? doc.querySelector('.sb') : null;
                function got(k) { var v = bar && bar.getAttribute(k) || ''; return v && v.indexOf('@') < 0 ? v : ''; }
                return { version: got('data-version'), build: got('data-build'), built: got('data-built') };
            }
            function state(cls, word, line) { return add(el('div', 'state'), el('span', 'tg ' + cls + ' big', word), doc.createTextNode(line)); }
            function rightNow(busy) {
                var p = panel('Right now');
                if (!busy) return add(p, message('The game server did not answer.'));
                return add(p, kv('Matches running', String(busy.matches)), kv('Players on the game server', String(busy.players), 'in matches and lobbies'));
            }
            function roles(s) {
                var p = panel('Role checks', 'The chat asks Discord who has the admin and moderator roles, about every 3 minutes.');
                var h = s.health;
                if (h.roles === 'ok') add(p, state('active', 'Working', 'Roles known for ' + h.roles_known + ' accounts'), el('div', 'note', 'If this says Refused or Not answering, the page stays shut for everyone but the owner’s account.'));
                else if (h.roles === 'refused') {
                    var minutes = Math.max(1, Math.ceil((h.retry || 0) / 60));
                    add(p, state('bad', 'Refused', 'Discord said no (' + (h.kind || 'unknown') + ', status ' + h.status + ')'), el('div', 'note bad', 'Nobody holds a role while Discord refuses. This page and the moderators’ tools stay shut, except for the owner’s account. The hub tries again in about ' + minutes + (minutes === 1 ? ' minute.' : ' minutes.')));
                } else if (h.roles === 'down') add(p, state('warn', 'Not answering', 'Discord did not answer the last check'), el('div', 'note', 'Roles that are not a few minutes old are not believed: the page stays shut for everyone but the owner’s account until Discord answers.'));
                else add(p, state('warn', 'Off', 'No role is asked of Discord'), el('div', 'note', 'Nobody holds a role: only the owner’s account gets in.'));
                return p;
            }
            function pictures(s) {
                var p = panel('Pictures from Discord', 'The last time the hub fetched a person’s picture.');
                var l = s.last_picture, k = l && PICTURE_KINDS[l.kind];
                if (!l) return add(p, state('', 'none yet', 'No picture was fetched since the hub started'));
                return add(p, state(k ? k[0] : 'warn', String(k ? l.kind : 'unknown'), (k ? k[1] : 'Something that this page does not know') + ' · ' + ago(l.age)));
            }
            function store(s) {
                var p = panel('Chat store', 'The hub’s own database: the lines, bans, mutes and the log.');
                var t = s.store, pct = t.limit ? Math.floor(t.bytes * 100 / t.limit) : 0;
                var meter = el('div', 'meter'), fill = el('i', pct >= 90 ? 'b' : pct >= 70 ? 'w' : '');
                fill.style.width = Math.min(100, pct) + '%';
                add(meter, fill);
                add(p, kv('Database', megabytes(t.bytes) + ' of ' + megabytes(t.limit, true) + ' MB', pct + '%'), meter,
                    kv('Log file', megabytes(t.log) + ' MB'), kv('Lines kept', String(t.lines), t.oldest ? 'the oldest from ' + when(t.oldest, env.now()) : null),
                    kv('Pictures kept', String(t.pictures), megabytes(t.picture_bytes) + ' of ' + megabytes(t.picture_bytes_max, true) + ' MB'), kv('Switched off / blocked', t.off + ' / ' + t.blocked));
                return p;
            }
            function versions(s) {
                var p = panel('Versions'), site = stamp();
                if (site.version) add(p, kv('Site', site.version, site.build ? 'build ' + site.build + (site.built ? ' · built ' + site.built : '') : null, 'stack'));
                var v = s.version;
                if (v) add(p, kv('Chat hub', 'code ' + v.code, 'running since ' + when(v.started, env.now()), 'stack'));
                return p;
            }
            function draw(busy, hubStatus) {
                grid.textContent = '';
                upd.textContent = 'Checked ' + when(env.now() / 1000, env.now()) + ' (Pacific time)';
                var c1 = el('div', 'cc'), c2 = el('div', 'cc');
                add(c1, rightNow(busy));
                if (hubStatus) { add(c1, roles(hubStatus), pictures(hubStatus)); add(c2, store(hubStatus), versions(hubStatus)); }
                else { var p = panel('The chat hub'); add(c2, add(p, message('The chat hub did not answer.'))); }
                add(grid, c1, c2);
            }
            function load() {
                refresh.disabled = true;
                return Promise.all([env.fetch('/busy', { credentials: 'omit' }).then(function (r) { return r.status === 200 ? r.json() : null; }).catch(function () { return null; }), call('GET', HUB_API + 'status')]).then(function (rs) {
                    refresh.disabled = false;
                    if (refused(rs[1])) return;
                    var busy = rs[0] && typeof rs[0].matches === 'number' && typeof rs[0].players === 'number' ? rs[0] : null;
                    draw(busy, rs[1].status === 200 && rs[1].json && rs[1].json.health && rs[1].json.store ? rs[1].json : null);
                }, function () { refresh.disabled = false; draw(null, null); });
            }
            return load();
        }

        function start() {
            return call('GET', '/admin/api/me').then(function (r) {
                if (r.status === 200 && r.json && typeof r.json.csrf === 'string' && r.json.csrf !== '') { me = r.json; hub = null; return showShell(); }
                return showRefusal(r.status, r.json);
            }, function () { return showRefusal(0, {}); });
        }

        return { start: start, when: when, state: function () { return { me: me }; }, sections: function () { return SECTIONS.map(function (x) { return x.id; }); } };
    }

    return { create: create, when: when, cleanDescription: cleanDescription, untilWords: untilWords, leftWords: leftWords, accountOf: accountOf };
});
