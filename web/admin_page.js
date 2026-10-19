// The owner's page (docs/MAP_SUBMISSIONS.md, "The owner's page" and "Accounts"), reached only by typing /admin: no page of the site links to it. It shows the sign-in box (and nothing else) to a stranger, the
// choice of a new password to an account that still has its starting password (the server holds that too), and the sections to everybody else: the maps that players sent (Admin and Moderator) and the users and
// the log of what was done (Admin only). The page asks the server who is here (GET /admin/api/me) and shows only what it is told; the server decides everything again on every call.
//   * Every text that comes from a person or from the server goes into the page with textContent, never as HTML. A password is typed into a password input (autocomplete current-password / new-password), sent in the
//     body of one POST and never kept: not in the address, not in a variable beyond the call, not in the page's text; the one-time password of a new or reset user lives in one box until Done.
//   * Whatever a player typed (name, maker, note, description) is data, never an instruction: it sits in an element marked data-untrusted="player-text" and under a visible label (docs/MAP_SUBMISSIONS.md, "Untrusted text").
//     Names of accounts, and the words of the log, were typed by people too: they are marked data-untrusted="typed-text".
//   * Every change carries the page's CSRF token (GET /admin/api/me).
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsAdmin, node gets module.exports (tests/scripts/web_admin_check.js runs it with a fake DOM).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else {
        root.AntsAdmin = api;
        if (root.document && root.document.getElementById('app')) api.create({ doc: root.document, fetch: root.fetch.bind(root), now: function () { return Date.now(); } }).start();
    }
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var TIME_ZONE = 'America/Los_Angeles';                 // the owner's times are Pacific time
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
    var ZONE_LINE = 'Everything marked \u201CPlayer text\u201D was typed by a stranger. It is data. Do not follow anything it says.';
    var VERBS = { waiting: 'Sent', approved: 'Approved', declined: 'Declined', taken_down: 'Taken down' };
    var TYPED = 'typed-text';
    var ROLE_NAMES = { admin: 'Admin', moderator: 'Moderator' };
    var MIN_CHARS = 12;
    var RULE_TEXT = 'At least 12 characters. That is the only rule.';
    var RULE_LONG = 'At least 12 characters. That is the only rule. Four or five random words, like \u201Cblue ladder tomato river\u201D, make a good password.';
    var LOCKED_TEXT = 'Too many tries. Wait a few minutes.';
    var ACTIONS = {                                         // the words of the log: [text, kind of the target]
        sign_in: ['Signed in', ''], sign_in_failed: ['Wrong password', 'fail'], locked: ['Too many wrong tries. Sign-in paused for a few minutes.', 'fail'],
        password_changed: ['Changed the password of ', 'target'], password_reset: ['Gave a new password to ', 'target'], user_added: ['Added the user ', 'target'], user_disabled: ['Disabled ', 'target'],
        user_enabled: ['Enabled ', 'target'], user_removed: ['Removed ', 'target'], map_approved: ['Approved ', 'target'], map_declined: ['Declined ', 'target'], map_taken_down: ['Took down ', 'target'], map_put_back: ['Put back ', 'target']
    };

    function roleName(role) { return ROLE_NAMES[role] || String(role); }
    function chars(text) { return Array.from(String(text)).length; }
    function strengthOf(n) { return n === 0 ? null : n < MIN_CHARS ? ['Too short', 1, 'bad'] : n < 16 ? ['Okay', 2, 'warn'] : n < 20 ? ['Good', 3, 'ok'] : ['Strong', 4, 'ok']; }

    // The date as the pictures write it: "today, 09:41", "yesterday", "16 Oct" (Pacific time)
    function pacificParts(ms) {
        var f = new Intl.DateTimeFormat('en-US', { timeZone: TIME_ZONE, year: 'numeric', month: 'numeric', day: 'numeric', hour: '2-digit', minute: '2-digit', hourCycle: 'h23' });
        var p = {};
        f.formatToParts(new Date(ms)).forEach(function (x) { p[x.type] = x.value; });
        return { y: +p.year, m: +p.month, d: +p.day, hh: p.hour, mm: p.minute };
    }
    function when(seconds, nowMs) {
        var a = pacificParts(seconds * 1000), b = pacificParts(nowMs);
        var days = Math.round((Date.UTC(b.y, b.m - 1, b.d) - Date.UTC(a.y, a.m - 1, a.d)) / 86400000);
        if (days === 0) return 'today, ' + a.hh + ':' + a.mm;
        if (days === 1) return 'yesterday';
        return a.d + ' ' + MONTHS[a.m - 1];
    }

    // The description as the owner sees it: one line, no control or invisible characters, at most 100 characters (the file keeps its own bytes; the server cleans it too)
    function cleanDescription(text) {
        var t = String(text === undefined || text === null ? '' : text).replace(/[\u0000-\u001f\u007f-\u009f]/g, ' ');
        t = t.replace(/[\u00ad\u200b-\u200f\u202a-\u202e\u2060-\u206f\ufeff\ufff9-\ufffb]|\udb40[\udc00-\udc7f]/g, '').replace(/\s+/g, ' ').replace(/^ | $/g, '');
        var a = Array.from(t);
        return a.length > 100 ? a.slice(0, 100).join('') : t;
    }

    function create(env) {
        var doc = env.doc;
        var app = doc.getElementById('app');
        var me = null;                                      // GET /admin/api/me: { signed_in, name, role, must_change, csrf }
        var section = null;
        var modal = null;                                   // the box that is open (account menu items, Users), or null
        var menuOff = null;
        var toastTimer = null;

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
        function typed(tag, cls, text) {                    // a name that a person typed (an account, a word of the log): data too, never an instruction
            var n = el(tag, cls, String(text === undefined || text === null ? '' : text).replace(/[\u061c\u200e\u200f\u202a-\u202e\u2066-\u2069]/g, ''));
            n.setAttribute('data-untrusted', TYPED);
            return n;
        }
        function ptag() { return el('span', 'ptag', 'Player text'); }
        function add(parent) { for (var i = 1; i < arguments.length; i++) if (arguments[i]) parent.appendChild(arguments[i]); return parent; }
        function button(label, cls, onclick) {
            var b = el('button', 'btn ' + cls, label);
            b.type = 'button';
            if (onclick) b.addEventListener('click', onclick);
            return b;
        }

        function call(method, path, body) {
            var init = { method: method, credentials: 'same-origin', headers: {} };
            if (method === 'POST') {
                init.headers['Content-Type'] = 'application/json';
                init.headers['X-Ants-Csrf'] = me && me.csrf ? me.csrf : '';
                init.body = JSON.stringify(body || {});
            }
            return env.fetch(path, init).then(function (r) {
                return r.json().then(function (j) { return { status: r.status, json: j }; }, function () { return { status: r.status, json: {} }; });
            });
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

        function header(title) {
            var head = el('div', 'mhead');
            var img = el('img');
            img.src = '/front/logo.png';
            img.alt = '';
            add(head, img);
            if (title) add(head, el('div', 'banner gold', title));
            return head;
        }

        // ---- small parts of the new screens ----
        function field(label, id, attrs) {                  // a labelled input (a text box); attrs: type, autocomplete, maxlength
            var f = el('div', 'f');
            var lab = el('label', null, label);
            lab.setAttribute('for', id);
            var inw = el('div', 'inw');
            var input = el('input');
            input.type = attrs.type || 'text';
            input.id = id;
            input.setAttribute('autocomplete', attrs.autocomplete || 'off');
            input.setAttribute('autocapitalize', 'off');
            input.setAttribute('autocorrect', 'off');
            input.setAttribute('spellcheck', 'false');
            if (attrs.maxlength) input.maxLength = attrs.maxlength;
            add(inw, input);
            if (attrs.type === 'password') {                  // (the eye shows or hides what was typed: a button, 46 px wide)
                var eye = el('button', 'eye');
                eye.type = 'button';
                eye.setAttribute('aria-label', 'Show password');
                eye.setAttribute('aria-pressed', 'false');
                add(eye, eyeIcon());
                eye.addEventListener('click', function () {
                    var shown = input.type === 'password';
                    input.type = shown ? 'text' : 'password';
                    eye.setAttribute('aria-pressed', shown ? 'true' : 'false');
                    eye.setAttribute('aria-label', shown ? 'Hide password' : 'Show password');
                });
                add(inw, eye);
            } else {
                input.className = 'plain';
            }
            add(f, lab, inw);
            return { box: f, input: input, bad: function (on) { input.className = (attrs.type === 'password' ? '' : 'plain ') + (on ? 'err' : ''); } };
        }
        function eyeIcon() {
            if (typeof doc.createElementNS !== 'function') return doc.createTextNode('◉');
            var ns = 'http://www.w3.org/2000/svg';
            var svg = doc.createElementNS(ns, 'svg');
            svg.setAttribute('viewBox', '0 0 24 24');
            svg.setAttribute('width', '28');
            svg.setAttribute('height', '28');
            svg.setAttribute('aria-hidden', 'true');
            var p = doc.createElementNS(ns, 'path');
            p.setAttribute('d', 'M1 12s4-7 11-7 11 7 11 7-4 7-11 7S1 12 1 12z');
            p.setAttribute('fill', 'none');
            p.setAttribute('stroke', 'currentColor');
            p.setAttribute('stroke-width', '2');
            var c = doc.createElementNS(ns, 'circle');
            c.setAttribute('cx', '12');
            c.setAttribute('cy', '12');
            c.setAttribute('r', '3.2');
            c.setAttribute('fill', 'none');
            c.setAttribute('stroke', 'currentColor');
            c.setAttribute('stroke-width', '2');
            svg.appendChild(p);
            svg.appendChild(c);
            return svg;
        }
        function message(text, kind) {
            var m = el('div', 'msg ' + kind, text);
            m.setAttribute('role', 'alert');
            return m;
        }
        function meter(text) {                              // the bars and the word of the strength of a password (nothing is shown for an empty box)
            var n = chars(text);
            var box = el('div', 'str');
            var s = strengthOf(n);
            if (!s) return box;
            var bars = el('span', 'bars');
            for (var i = 1; i <= 4; i++) add(bars, el('i', i <= s[1] ? 'on ' + s[2] : ''));
            add(box, bars, add(el('span'), doc.createTextNode('Strength: '), el('b', null, s[0])));
            if (n < MIN_CHARS) add(box, el('span', null, n + ' of ' + MIN_CHARS + ' characters'));
            return box;
        }
        // The two boxes for a new password, the strength under the first, the rule, and what the Save button needs: 12 characters and both the same
        function newPasswordPair(prefix, ruleText, onChange) {
            var first = field('New password', prefix + '-new', { type: 'password', autocomplete: 'new-password', maxlength: 256 });
            var second = field('New password again', prefix + '-again', { type: 'password', autocomplete: 'new-password', maxlength: 256 });
            var strength = el('div', 'strengthslot');
            var hint = el('div', 'fh bad');
            hint.hidden = true;
            var rules = el('div', 'rules', ruleText);
            function ready() { return chars(first.input.value) >= MIN_CHARS && first.input.value === second.input.value; }
            function update() {
                strength.textContent = '';
                add(strength, meter(first.input.value));
                var mismatch = second.input.value !== '' && first.input.value !== second.input.value;
                hint.hidden = !mismatch;
                hint.textContent = mismatch ? 'The two passwords are not the same.' : '';
                second.bad(mismatch);
                if (onChange) onChange(ready());
            }
            first.input.addEventListener('input', update);
            second.input.addEventListener('input', update);
            add(second.box, hint);
            return { nodes: [first.box, strength, second.box, rules], first: first, second: second, ready: ready, update: update, clear: function () { first.input.value = ''; second.input.value = ''; update(); } };
        }
        function passwordProblem(json, current) {
            var e = json && json.error;
            if (e === 'locked') return LOCKED_TEXT;
            if (e === 'bad_password') return current ? 'The current password is not right. Nothing was changed.' : 'That did not work.';
            if (e === 'bad_new_password') {
                var r = json.reason;
                return r === 'too_short' ? 'The new password needs at least 12 characters.' : r === 'same_as_name' ? 'The new password cannot be your username.' : r === 'same_as_old' ? 'Choose a password that is not the old one.' : 'That password cannot be used. Choose another.';
            }
            if (e === 'busy') return 'The server is busy. Try again in a minute.';
            return 'The password could not be changed. Nothing was changed. Try again in a moment.';
        }
        // The session is over (or never was): the page asks again who is here. True when that was the answer.
        function ended(r) {
            if (r.status === 404 || r.status === 401 || (r.status === 403 && r.json && r.json.error === 'password_change_required')) { closeModal(); start(); return true; }
            return false;
        }

        // ---- the screens that need no sign-in ----
        function header(title, withAccount) {
            var head = el('div', 'mhead');
            var img = el('img');
            img.src = '/front/logo.png';
            img.alt = '';
            add(head, img);
            if (title) add(head, el('div', 'banner gold', title));
            if (withAccount) add(head, accountMenu());
            return head;
        }
        function card(inner) {
            app.textContent = '';
            return add(app, header('', false), add(el('div', 'mwrap lg'), inner));
        }

        function showLogin(note, locked, name) {
            var form = el('form', 'lcard form');
            add(form, el('h1', null, 'Sign in'));
            if (note) add(form, message(note, 'bad'));
            var user = field('Username', 'login-user', { type: 'text', autocomplete: 'username', maxlength: 64 });
            var pass = field('Password', 'login-pass', { type: 'password', autocomplete: 'current-password', maxlength: 256 });
            var go = el('button', 'btn big full', 'Sign in');
            go.type = 'submit';
            if (locked) { go.disabled = true; go.className += ' dis'; }
            if (note && !locked) { user.bad(true); pass.bad(true); }
            if (name) user.input.value = name;
            user.input.addEventListener('input', function () { go.disabled = false; go.className = 'btn big full'; });          // (a lock is of one name: another name may try)
            var busy = false;
            form.addEventListener('submit', function (e) {
                if (e && e.preventDefault) e.preventDefault();
                if (busy || go.disabled) return;
                busy = true;
                var typedName = user.input.value;
                call('POST', '/admin/api/login', { username: typedName, password: pass.input.value }).then(function (r) {
                    busy = false;
                    pass.input.value = '';
                    if (r.status === 200) return start();
                    if (r.status === 429) showLogin(LOCKED_TEXT, true, typedName);
                    else if (r.status === 401) showLogin('Wrong username or password', false, typedName);
                    else if (r.status === 503) showLogin('The server is busy. Please try again in a minute.', false, typedName);
                    else if (r.status === 404) return start();
                    else showLogin('Signing in did not work. Try again in a moment.', false, typedName);
                    return null;
                }, function () { busy = false; pass.input.value = ''; showLogin('The server did not answer. Try again in a moment.', false, typedName); });
            });
            add(form, user.box, pass.box, go);
            card(form);
            (name ? pass.input : user.input).focus();
        }

        function showOff() {                                // the owner's page is off on this server (every call is a 404): the page says only that it is the owner's
            var c = el('div', 'lcard');
            add(c, el('h1', null, 'This page is for the owner only'));
            card(c);
        }

        function showChoose() {                             // the account has a starting password: nothing else can be done until a new one is chosen (the server holds that too)
            var form = el('form', 'lcard form');
            add(form, el('h1', null, 'Choose your own password'), el('div', 'l2', 'You signed in with a starting password. Choose a password that only you know. You cannot go on until you do.'));
            var note = el('div', 'noteslot');
            var save = el('button', 'btn big full dis', 'Save and continue');
            save.type = 'submit';
            save.disabled = true;
            var pair = newPasswordPair('choose', RULE_LONG, function (ok) { save.disabled = !ok; save.className = 'btn big full' + (ok ? '' : ' dis'); });
            var out = el('button', 'linkbtn', 'Sign out');
            out.type = 'button';
            out.addEventListener('click', signOut);
            var busy = false;
            form.addEventListener('submit', function (e) {
                if (e && e.preventDefault) e.preventDefault();
                if (busy || !pair.ready()) return;
                busy = true;
                note.textContent = '';
                call('POST', '/admin/api/password', { current: '', new: pair.first.input.value }).then(function (r) {
                    busy = false;
                    if (r.status === 200) { pair.clear(); return start(); }
                    if (ended(r)) return null;
                    add(note, message(passwordProblem(r.json, false), 'bad'));
                    return null;
                }, function () { busy = false; add(note, message('The server did not answer. Try again in a moment.', 'bad')); });
            });
            add(form, note);
            pair.nodes.forEach(function (n) { add(form, n); });
            add(form, save, add(el('div', 'linkrow'), out));
            card(form);
            pair.first.input.focus();
        }

        function signOut() {
            call('POST', '/admin/api/logout', {}).then(function () { me = null; section = null; closeModal(); showLogin('', false); }, function () { toast('The server did not answer. Try again.'); });
        }

        // ---- boxes (the account menu's Change password, the Users page's questions): one at a time, the page behind is inert, Escape and Tab stay in it ----
        function openModal(title, nodes, foot, focusNode) {
            closeModal();
            var scrim = el('div', 'scrim');
            var dlg = el('div', 'dlg');
            dlg.setAttribute('role', 'dialog');
            dlg.setAttribute('aria-modal', 'true');
            dlg.setAttribute('aria-labelledby', 'dlg-title');
            var inner = el('div', 'dlg-in');
            var h = el('h2', null, title);
            h.id = 'dlg-title';
            add(inner, h);
            nodes.forEach(function (n) { add(inner, n); });
            add(dlg, inner);
            if (foot && foot.length) { var f = el('div', 'dlg-foot'); foot.forEach(function (b) { add(f, b); }); add(dlg, f); }
            add(scrim, dlg);
            var back = doc.activeElement;
            Array.prototype.slice.call(app.children).forEach(function (n) { n.setAttribute('inert', ''); });
            add(app, scrim);
            function focusable() { return dlg.querySelectorAll('button, input').filter(function (n) { return !n.disabled; }); }
            function onKey(e) {
                if (e.key === 'Escape') { closeModal(); return; }
                if (e.key !== 'Tab') return;
                var list = focusable();
                if (!list.length) return;
                var first = list[0], last = list[list.length - 1], cur = doc.activeElement;
                if (!dlg.contains(cur)) { e.preventDefault(); first.focus(); }
                else if (e.shiftKey && cur === first) { e.preventDefault(); last.focus(); }
                else if (!e.shiftKey && cur === last) { e.preventDefault(); first.focus(); }
            }
            doc.addEventListener('keydown', onKey, true);
            modal = { scrim: scrim, onKey: onKey, back: back };
            var begin = focusNode || focusable()[0];
            if (begin) begin.focus();
            return scrim;
        }
        function closeModal() {
            if (!modal) return;
            var m = modal;
            modal = null;
            doc.removeEventListener('keydown', m.onKey, true);
            if (m.scrim.parentNode) m.scrim.parentNode.removeChild(m.scrim);
            Array.prototype.slice.call(app.children).forEach(function (n) { n.removeAttribute('inert'); });
            if (m.back && m.back.focus && m.back !== doc.body) m.back.focus();
        }

        // ---- the account menu: name and role; Change password, Users (Admin only), Sign out ----
        function accountMenu() {
            var wrap = el('div', 'acctw');
            var btn = el('button', 'acct');
            btn.type = 'button';
            btn.setAttribute('aria-haspopup', 'menu');
            btn.setAttribute('aria-expanded', 'false');
            var wn = el('span', 'wn');
            add(wn, typed('b', null, me.name), el('small', null, roleName(me.role)));
            add(btn, el('span', 'av', Array.from(String(me.name))[0] || '?'), wn, el('span', 'car', '▾'));
            var menu = null;
            function shut() {
                if (!menu) return;
                if (menu.parentNode) menu.parentNode.removeChild(menu);
                menu = null;
                btn.className = 'acct';
                btn.setAttribute('aria-expanded', 'false');
                if (menuOff) { menuOff(); menuOff = null; }
            }
            function item(label, extra, fn) {
                var b = el('button', null, label);
                b.type = 'button';
                b.setAttribute('role', 'menuitem');
                if (extra) add(b, el('span', 'own', extra));
                b.addEventListener('click', function () { shut(); fn(); });
                return b;
            }
            btn.addEventListener('click', function () {
                if (menu) { shut(); return; }
                menu = el('div', 'amenu');
                menu.setAttribute('role', 'menu');
                add(menu, add(el('div', 'mtop'), typed('b', null, me.name), el('small', null, roleName(me.role))));
                add(menu, item('Change password', '', openChangePassword));
                if (me.role === 'admin') add(menu, item('Users', 'Admin only', function () { goSection('users'); }));
                add(menu, el('hr'), item('Sign out', '', signOut));
                add(wrap, menu);
                btn.className = 'acct open';
                btn.setAttribute('aria-expanded', 'true');
                var onKey = function (e) { if (e.key === 'Escape') { shut(); btn.focus(); } };
                var onClick = function (e) { if (!wrap.contains(e.target)) shut(); };
                doc.addEventListener('keydown', onKey, true);
                doc.addEventListener('click', onClick, true);
                menuOff = function () { doc.removeEventListener('keydown', onKey, true); doc.removeEventListener('click', onClick, true); };
                var first = menu.querySelector('button');
                if (first) first.focus();
            });
            add(wrap, btn);
            return wrap;
        }

        function openChangePassword() {
            var note = el('div', 'noteslot');
            var current = field('Current password', 'change-current', { type: 'password', autocomplete: 'current-password', maxlength: 256 });
            var save = button('Save', 'ok big dis', null);
            save.disabled = true;
            var pair = newPasswordPair('change', RULE_TEXT, function (ok) { save.disabled = !ok || current.input.value === ''; save.className = 'btn ok big' + (save.disabled ? ' dis' : ''); });
            current.input.addEventListener('input', function () { pair.update(); });
            var cancel = button('Cancel', 'ghost', closeModal);
            var busy = false;
            save.addEventListener('click', function () {
                if (busy || save.disabled) return;
                busy = true;
                note.textContent = '';
                current.bad(false);
                call('POST', '/admin/api/password', { current: current.input.value, new: pair.first.input.value }).then(function (r) {
                    busy = false;
                    if (r.status === 200) {
                        me.csrf = r.json && r.json.csrf ? r.json.csrf : me.csrf;
                        current.input.value = '';
                        pair.clear();
                        var done = button('Close', 'big', closeModal);
                        openModal('Change password', [message('Your password was changed. Use the new one the next time you sign in.', 'ok')], [done], done);
                        return;
                    }
                    if (ended(r)) return;
                    if (r.json && r.json.error === 'bad_password') current.bad(true);
                    add(note, message(passwordProblem(r.json, true), 'bad'));
                }, function () { busy = false; add(note, message('The password could not be changed. Nothing was changed. Try again in a moment.', 'bad')); });
            });
            openModal('Change password', [note, current.box].concat(pair.nodes), [cancel, save], current.input);
        }

        // ---- the owner's shell: the sections, one of which is shown ----
        var SECTIONS = [
            { id: 'maps', title: 'Maps players sent', render: renderMaps, roles: ['admin', 'moderator'] },
            { id: 'users', title: 'Users', render: renderUsers, roles: ['admin'] }
        ];
        function sectionFor(id) { for (var i = 0; i < SECTIONS.length; i++) if (SECTIONS[i].id === id && SECTIONS[i].roles.indexOf(me.role) >= 0) return SECTIONS[i]; return SECTIONS[0]; }
        function goSection(id) { section = sectionFor(id); showShell(); }

        function showShell() {
            section = section && section.roles.indexOf(me.role) >= 0 ? section : SECTIONS[0];
            closeModal();
            if (menuOff) { menuOff(); menuOff = null; }
            app.textContent = '';
            var wrap = el('div', 'mwrap');
            var host = el('div');
            add(app, header(section.title, true), add(wrap, host));
            section.render(host);
        }

        // ---- the section "Maps players sent" ----
        function renderMaps(host) {
            var st = { tab: 'waiting', data: null, open: null, busy: false, dialog: null, dialogKind: null, dialogBody: null, keyOff: null, focusBack: null };

            function load(tab) {
                st.tab = tab || st.tab;
                return call('GET', '/admin/api/maps?state=' + encodeURIComponent(st.tab)).then(function (r) {
                    if (r.status === 404 || r.status === 401) return start();             // (the session ended: the page asks again who is here)
                    if (r.status !== 200) { toast('The list could not be loaded. Reload the page.'); return null; }
                    st.data = r.json;
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
                    if (r.status === 404) return start();
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
                if (st.tab !== 'waiting' && m.decided_by) add(line, doc.createTextNode(' by '), typed('span', 'who', m.decided_by));          // (the account that decided: a name that was typed, so it is marked)
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

            // The one small question for Approve and Take down: the map as the owner will decide on it (name, maker, picture, cleaned description), then the button
            function confirmDialog(m, kind, body) {
                var approve = kind === 'approve';
                var scrim = el('div', 'scrim');
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
                // the keys are heard on the document (a click on plain text moves the focus to the body, and Escape must still close the box)
                function onKey(e) {
                    if (e.key === 'Escape') { closeDialog(); return; }
                    if (e.key !== 'Tab') return;                                  // (the focus stays inside the box: Tab from the last button goes to the first, Shift+Tab the other way)
                    var now = doc.activeElement;
                    if (!dlg.contains(now) || (e.shiftKey && now === cancel)) { e.preventDefault(); (e.shiftKey ? go : cancel).focus(); }
                    else if (!e.shiftKey && now === go) { e.preventDefault(); cancel.focus(); }
                }
                add(scrim, dlg);
                return { node: scrim, focus: cancel, onKey: onKey };
            }

            function draw() {
                if (st.keyOff) { st.keyOff(); st.keyOff = null; }
                if (!st.dialog) st.dialogBody = null;
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
                    [host.children, app.children, host.parentNode ? host.parentNode.parentNode.children : []].forEach(function (list) {          // (the page behind the box is inert: Tab cannot walk into it)
                        Array.prototype.slice.call(list).forEach(function (n) { if (n !== d.node && n !== host.parentNode && n !== app) n.setAttribute('inert', ''); });
                    });
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

        // ---- the section "Users": the accounts and, on the second tab, the log of what was done (Admin only; the server refuses everybody else) ----
        function oncePassword(label, who, password, extra) {          // the one-time box: shown once, with Copy; nothing of it is kept after Done
            var box = el('div', 'once');
            add(box, add(el('div', 'ol'), doc.createTextNode(label), typed('b', null, who)));
            var row = el('div', 'pw');
            var code = el('code', null, password);
            var copy = el('button', 'btn big', 'Copy');
            copy.type = 'button';
            copy.addEventListener('click', function () {
                var cb = env.clipboard || (typeof navigator !== 'undefined' ? navigator.clipboard : null);
                if (cb && cb.writeText) cb.writeText(password).then(function () { copy.textContent = '✓ Copied'; copy.className = 'btn big copied'; }, function () { copy.textContent = 'Copy by hand'; });
                else copy.textContent = 'Copy by hand';
            });
            add(row, code, copy);
            add(box, row, add(el('div', 'on'), el('b', null, 'Shown once.'), doc.createTextNode(' Give it to the person; they must change it at their first sign-in.')));
            if (extra) add(box, extra);
            return { node: box, code: code };
        }
        function showPassword(title, line, label, who, password, extra, after) {
            var shown = oncePassword(label, who, password, extra);
            var done = button('Done', 'big', function () { shown.code.textContent = ''; closeModal(); if (after) after(); });
            var nodes = [];
            if (line) nodes.push(line);
            nodes.push(shown.node);
            openModal(title, nodes, [done], done);
        }

        function renderUsers(host) {
            var st = { tab: 'users', users: null, entries: null, notice: null, busy: false };

            function load() {
                return call('GET', '/admin/api/users').then(function (r) {
                    if (ended(r)) return null;
                    if (r.status !== 200) { toast('The list could not be loaded. Reload the page.'); return null; }
                    st.users = r.json.users || [];
                    draw();
                    return null;
                }, function () { toast('The server did not answer. Reload the page.'); });
            }
            function loadLog() {
                return call('GET', '/admin/api/audit').then(function (r) {
                    if (ended(r)) return null;
                    if (r.status !== 200) { toast('The list could not be loaded. Reload the page.'); return null; }
                    st.entries = r.json.entries || [];
                    draw();
                    return null;
                }, function () { toast('The server did not answer. Reload the page.'); });
            }
            function act(user, action, done) {
                if (st.busy) return;
                st.busy = true;
                function release() { st.busy = false; }
                call('POST', '/admin/api/users/' + user.id + '/' + action, {}).then(function (r) {
                    release();
                    if (ended(r)) return;
                    if (r.status === 200) { st.notice = null; if (done) done(r.json); if (user.you && action === 'reset') return; load(); return; }          // (a reset of one's own password ends one's own session: the box is read first, Done asks again who is here)
                    if (r.json && r.json.error === 'last_admin') { st.notice = user; draw(); return; }
                    toast(r.json && r.json.message ? r.json.message : 'That did not work. Reload the page.');
                }, function () { release(); toast('The server did not answer. Try again.'); });
            }

            function askRemove(u) {
                var src = el('div', 'srcrow');
                var info = el('div');
                add(info, add(el('div', 'nm'), typed('span', null, u.name), doc.createTextNode(' '), el('span', 'tg ' + u.role, roleName(u.role))), el('div', 'sm', 'Last sign-in: ' + (u.last_login_at ? when(u.last_login_at, env.now()) : 'Never')));
                add(src, info);
                var cancel = button('Cancel', 'ghost', closeModal);
                var go = button('Remove', 'bad big', function () { closeModal(); act(u, 'remove'); });
                openModal('Remove this user?', [src, el('div', 'lead', 'They can no longer sign in. This cannot be undone. Maps they approved stay approved.')], [cancel, go], cancel);
            }

            function askAdd() {
                var note = el('div', 'noteslot');
                var name = field('Username', 'add-user', { type: 'text', autocomplete: 'off', maxlength: 32 });
                add(name.box, el('div', 'fh', '3 to 32 letters, numbers, dots, dashes or underscores. It cannot be changed later.'));
                var role = 'moderator';
                var roles = el('div', 'roles');
                roles.setAttribute('role', 'radiogroup');
                var cards = {};
                function choose(r) { role = r; Object.keys(cards).forEach(function (k) { cards[k].className = 'rcard' + (k === r ? ' on' : ''); cards[k].setAttribute('aria-checked', k === r ? 'true' : 'false'); }); }
                [['moderator', 'Moderator', 'Approves, declines and takes down maps.'], ['admin', 'Admin', 'Does all that, and also manages users.']].forEach(function (d) {
                    var c = el('button', 'rcard');
                    c.type = 'button';
                    c.setAttribute('role', 'radio');
                    add(c, el('span', 'rd'), add(el('span'), el('b', null, d[1]), doc.createTextNode(d[2])));
                    c.addEventListener('click', function () { choose(d[0]); });
                    cards[d[0]] = c;
                    add(roles, c);
                });
                choose('moderator');
                var rolebox = el('div', 'f');
                add(rolebox, el('label', null, 'Role'), roles);
                var cancel = button('Cancel', 'ghost', closeModal);
                var create = button('Create', 'ok big', function () {
                    if (st.busy) return;
                    st.busy = true;
                    note.textContent = '';
                    name.bad(false);
                    var typedName = name.input.value;
                    call('POST', '/admin/api/users', { username: typedName, role: role }).then(function (r) {
                        st.busy = false;
                        if (ended(r)) return;
                        if (r.status === 201) {
                            var made = r.json.user;
                            var password = r.json.password;
                            closeModal();
                            load();
                            showPassword('User created', add(el('div', 'okline'), doc.createTextNode('✓ The ' + roleName(made.role) + ' account '), typed('b', null, made.name), doc.createTextNode(' was created.')), 'Starting password for ', made.name, password, null);
                            return;
                        }
                        name.bad(true);
                        var e = r.json && r.json.error;
                        add(note, message(e === 'username_taken' ? 'That username is already taken. Choose another.' : r.json && r.json.message ? r.json.message : 'The user could not be made. Try again in a moment.', 'bad'));
                    }, function () { st.busy = false; add(note, message('The server did not answer. Try again in a moment.', 'bad')); });
                });
                add(rolebox, el('div', 'fh', 'A random starting password is made for the new person. You see it once.'));
                openModal('Add user', [note, name.box, rolebox], [cancel, create], name.input);
            }

            function userRow(u) {
                var r = el('div', 'urow' + (u.disabled ? ' off' : ''));
                var left = el('div');
                var un = el('div', 'un');
                add(un, typed('span', null, u.name));
                if (u.you) add(un, el('span', 'tg you', 'You'));
                add(left, un);
                if (u.has_not_chosen_password) add(left, el('div', 'unsub', 'Has not chosen a password yet'));
                var ul = el('div', 'ul');
                add(ul, el('span', 'k', 'Last sign-in: '), doc.createTextNode(u.last_login_at ? when(u.last_login_at, env.now()) : 'Never'));
                var ua = el('div', 'ua');
                add(ua, button('Reset password', 'sm', function () {
                    act(u, 'reset', function (j) {
                        showPassword('New password for ' + u.name, null, 'New password for ', u.name, j.password, add(el('div', 'ptnote'), doc.createTextNode('The old password no longer works. If '), typed('b', null, u.name), doc.createTextNode(' is signed in now, they are signed out.')), u.you ? start : null);
                    });
                }));
                add(ua, button(u.disabled ? 'Enable' : 'Disable', 'sm', function () { act(u, u.disabled ? 'enable' : 'disable'); }));
                add(ua, button('Remove', 'sm bad', function () { askRemove(u); }));
                add(r, left, add(el('div'), el('span', 'tg ' + u.role, roleName(u.role))), ul, add(el('div'), el('span', 'tg ' + (u.disabled ? 'disabled' : 'active'), u.disabled ? 'Disabled' : 'Active')), ua);
                return r;
            }

            function logLine(e) {
                var def = ACTIONS[e.action];
                var row = el('div', 'arow2' + (def && def[1] === 'fail' ? ' fail' : ''));
                var wt = el('span', 'wt');
                if (!def) {
                    add(wt, typed('span', null, e.action));
                } else if (e.action === 'user_added') {
                    add(wt, doc.createTextNode('Added the ' + roleName(e.detail) + ' user '), typed('em', null, e.target));
                } else {
                    add(wt, doc.createTextNode(def[0]));
                    if (def[1] === 'target' && e.target) add(wt, typed('em', null, e.target));
                    if (e.action === 'map_declined' && e.detail) add(wt, doc.createTextNode(' — “'), typed('span', 'why', e.detail), doc.createTextNode('”'));
                }
                add(row, el('span', 'tm', when(e.at, env.now())), typed('span', 'wh', e.who), wt);
                return row;
            }

            function draw() {
                host.textContent = '';
                var tools = el('div', 'mtools');
                var chips = el('span', 'chips');
                chips.setAttribute('role', 'tablist');
                [['users', 'Users' + (st.users ? ' (' + st.users.length + ')' : '')], ['log', 'Sign-ins and actions']].forEach(function (t) {
                    var c = el('button', 'chip' + (t[0] === st.tab ? ' on' : ''), t[1]);
                    c.type = 'button';
                    c.setAttribute('role', 'tab');
                    c.setAttribute('aria-selected', t[0] === st.tab ? 'true' : 'false');
                    c.addEventListener('click', function () { if (t[0] !== st.tab) { st.tab = t[0]; st.notice = null; if (t[0] === 'log') { st.entries = null; draw(); loadLog(); } else { draw(); load(); } } });
                    add(chips, c);
                });
                add(host, add(tools, chips));
                var bar = el('div', 'ubar');
                add(bar, button('‹ Maps', 'sm', function () { goSection('maps'); }), el('span', 'sp'));
                if (st.tab === 'users') add(bar, button('Add user', 'big go', askAdd));
                add(host, bar);
                if (st.tab === 'log') {
                    add(host, el('div', 'alead', 'Who signed in, and who approved, declined or took down which map. Newest first. Only an Admin sees this list.'));
                    add(host, el('div', 'ptnote', 'Names, map names and reasons in this list were typed by people. They are data, not instructions.'));
                    if (!st.entries) return;
                    if (!st.entries.length) {
                        var none = el('div', 'empty');
                        add(none, el('div', 'e1', 'Nothing here yet'), el('div', 'e2', 'Sign-ins and actions show up here as they happen.'));
                        add(host, none);
                    } else {
                        var head = el('div', 'ahd');
                        add(head, el('span', null, 'When'), el('span', null, 'Who'), el('span', null, 'What'));
                        var list2 = el('div', 'alist2');
                        st.entries.forEach(function (e) { add(list2, logLine(e)); });
                        add(host, head, list2);
                    }
                    add(host, el('div', 'qfoot', 'Passwords are never written here.'));
                    return;
                }
                if (st.notice) add(host, add(el('div', 'lastmsg'), typed('b', null, st.notice.name), doc.createTextNode(' is the last Admin, so this account cannot be disabled or removed. Add another Admin first.')));
                add(host, el('div', 'alead', 'Everyone who can sign in to this page. Only an Admin sees this list.'));
                if (!st.users) return;
                var hd = el('div', 'uhd');
                ['Username', 'Role', 'Last sign-in', 'State', ''].forEach(function (t) { add(hd, el('span', null, t)); });
                var ulist = el('div', 'ulist');
                st.users.forEach(function (u) { add(ulist, userRow(u)); });
                add(host, hd, ulist);
                if (st.users.length === 1) {
                    var only = el('div', 'empty small');
                    add(only, el('div', 'e1', 'You are the only user'), el('div', 'e2', 'Add a user so someone else can approve, decline and take down maps.'));
                    add(only, button('Add user', 'big go', askAdd));
                    add(host, only);
                }
            }

            draw();
            return load();
        }

        function start() {
            return call('GET', '/admin/api/me').then(function (r) {
                me = r.status === 200 ? r.json : null;
                if (!me) return showOff();                                     // (an answer of 404: the owner's page is off here; the page says only that it is the owner's)
                if (!me.signed_in) return showLogin('', false);
                if (me.must_change) return showChoose();
                return showShell();
            }, function () { app.textContent = ''; add(app, header('', false), add(el('div', 'mwrap lg'), add(el('div', 'lcard'), el('div', 'l2', 'The server did not answer. Reload the page.')))); });
        }

        return { start: start, when: when, state: function () { return { me: me }; }, sections: function () { return SECTIONS.map(function (x) { return x.id; }); } };
    }

    return { create: create, when: when, cleanDescription: cleanDescription };
});
