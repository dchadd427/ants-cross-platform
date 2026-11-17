// The moderation sheet of Home's chat (docs/HOME_CHAT.md, "Roles and commands"): staff (the hub's `you.role` is mod or admin) act on a person's line without typing: a right click on the line, a long press on it (a touch
// screen), or the keyboard (Up and Down choose a line of the log, Enter, the menu key or Shift and F10 open it) opens a sheet with what the viewer may do to that line's author: Delete the line, Mute (four times and a
// custom number of minutes), Kick, Ban (administrators; also with the person's lines of the last hour taken off every screen). Delete, Kick and Ban ask once more in a confirm card whose safe answer has the focus. On another person's line a first row, Recent messages from the person (n), opens the lines of that person that are in
// the page's log (newest first; the dock finds them by the hub's key, the sheet only holds the number of one of them); a line of the list opens the sheet of that line. ES5,
// no module system: a page gets AntsHomeMod, node gets module.exports (tests/scripts/web_home_mod_check.js).
//   * Loaded by web/home/home.js, with web/home/mod.css, only after the chat dock names a visitor with a role, and mounted into that dock. A person with no role never downloads it and sees no change: no line is marked, a
//     right click and a long press are the browser's own. A `you` frame that takes the role away closes the sheet and takes the marks off at once.
//   * It sends nothing by itself: it types the hub's own commands through the dock (`dock.command`), by the NUMBER of the line (`/mute #148 60`, `/kick #148`, `/ban #148`, `/ban #148 purge`, `/del 148`), so there is no new power:
//     the hub's table of the powers and its fresh check of the roles decide, and what the page offers is only its guess. A line is never found by a name that was typed or guessed.
//   * The same sheet opens from a person of the players list (the list's own script is handed to `people(list)` by home.js): the same ways in on a row, and the person is named to the hub by the NUMBER of the row, `row:<n>`,
//     never by a name. Mute waits for the hub's `/mutes` (the dock's `mutes(done)`), so that it is never offered for a person who is muted already (Unmute is, and asks nothing more); a sheet of a person closes with the socket.
//   * Only lines and rows that the viewer has an action on are touched: the browser's context menu and its text selection are held back there and nowhere else (the selection only on a touch screen, in the style sheet); a link
//     or a button (a row's Join) keeps its own press and menu. One command at a time: the buttons take no press while it waits; the answer is a toast at the top (and the same words as a private line of the chat), never a sound or a focus.
//   * Everything that is said (names, lines) becomes DOM text with createElement and textContent only, never markup.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeMod = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var LIMIT = { hold: 500, show: 150, move: 10, arm: 400, stay: 6000, stayBad: 12000, minutes: 10080, quote: 240, top: 76, listed: 50 };       // (ms and px; minutes: the hub's own longest mute, a week)
    var TIMES = [[10, '10 min'], [60, '1 hour'], [1440, '1 day'], [10080, '1 week']];       // the four buttons: minutes and words
    var TEXT = {
        rowHint: 'Moderation: press Enter, the menu key or Shift and F10 on this line for its actions.', logHint: 'For moderators: Up and Down choose a line, Enter opens its actions.',
        sheet: 'Actions', del: 'Delete this line', delSmall: 'Gone from every screen', mute: 'Mute ', muteSmall: 'Cannot write for a while', kick: 'Kick ', kickSmall: 'Logged out; can log in again',
        ban: 'Ban ', banSmall: 'Can read, cannot write', cancel: 'Cancel', back: 'Back', muteFor: ' \u2014 for how long?', custom: 'Custom', minutes: 'minutes', go: 'Mute', customName: 'Custom mute, in minutes',
        hint: 'Or type 1 to 10,080 minutes (1 week).', tooLong: 'The most is 10,080 minutes (1 week).', delTitle: 'Delete this line?', delText: 'It goes from every screen and from the history. This cannot be undone.',
        kickText: ' is logged out of the chat and can log in again at once. Everyone sees ', banText: ' can still read the chat but cannot write until an administrator lifts the ban. Everyone sees ',
        banNote: 'The second red button also removes everything that person wrote in the hour before the ban, from every screen. That cannot be undone.', banPurge: 'Ban and delete their lines from the last hour',
        recent: 'Recent messages from ', recentSmall: 'Their lines in this chat, newest first', recentSub: ' in this chat', one: ' message', more: ' messages', thisLine: 'this line', recentNone: 'None of their lines is in the chat now.', recentName: 'Recent messages, newest first', recentKeys: 'Up and Down move between the lines, Home and End go to the first and the last.',
        ok: 'OK', example: 'for example 45', admin: 'administrator', mod: 'moderator', you: 'you', line: 'line #',
        unmute: 'Unmute ', until: 'Muted until ', today: ' today', checking: 'Checking whether ', checkingEnd: ' is muted', again: 'Check again', againSmall: 'Could not tell whether ',
        listHint: 'For moderators: Up and Down choose a person, Enter opens the actions.', personHint: 'Moderation: press Enter, the menu key or Shift and F10 on this person for the actions.'
    };
    var NOTE = {                                                          // the words of an answer (the toast and the private line of the chat are these, and nothing else)
        power: 'You cannot do that to ', role: 'Could not confirm your role with Discord, try again in a moment.', rate: 'Too fast: wait a moment and try again.', offline: 'Chat is offline: nothing was done.',
        silent: 'Nothing came back: check the chat before trying again.', generic: 'That did not work, try again.', gone: 'That line is gone already.', mine: 'You cannot do that now.',
        updating: 'That did not work: the chat server may be updating, try again in a minute.', away: 'That person is not here any more.'
    };
    var ICONS = {                                                         // [element, 'attribute:value;...'] (the same 24 x 24 outline pictures as the chat's other buttons)
        del: [['polyline', 'points:3 6 5 6 21 6'], ['path', 'd:M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6'], ['path', 'd:M10 11v6M14 11v6'], ['path', 'd:M9 6V4a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v2']],
        mute: [['path', 'd:M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z'], ['line', 'x1:3;y1:3;x2:21;y2:21']],
        unmute: [['path', 'd:M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z']],
        kick: [['path', 'd:M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4'], ['polyline', 'points:16 17 21 12 16 7'], ['line', 'x1:21;y1:12;x2:9;y2:12']],
        ban: [['circle', 'cx:12;cy:12;r:10'], ['line', 'x1:4.93;y1:4.93;x2:19.07;y2:19.07']],
        recent: [['line', 'x1:8;y1:6;x2:21;y2:6'], ['line', 'x1:8;y1:12;x2:21;y2:12'], ['line', 'x1:8;y1:18;x2:21;y2:18'], ['line', 'x1:3;y1:6;x2:3.01;y2:6'], ['line', 'x1:3;y1:12;x2:3.01;y2:12'], ['line', 'x1:3;y1:18;x2:3.01;y2:18']],
        chev: [['polyline', 'points:9 18 15 12 9 6']],
        check: [['path', 'd:M22 11.08V12a10 10 0 1 1-5.93-9.14'], ['polyline', 'points:22 4 12 14.01 9 11.01']],
        warn: [['path', 'd:M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z'], ['line', 'x1:12;y1:9;x2:12;y2:13'], ['line', 'x1:12;y1:17;x2:12.01;y2:17']]
    };

    // ---- the pure parts ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    function actionsOf(viewer, line) {                                    // what a viewer ('mod' or 'admin') is offered on a person's line, or on a person of the players list (`line.person`): the page's guess; the hub decides all the same (a role that changed since the line was written is refused `power`)
        if (!line || line.sys || (viewer !== 'mod' && viewer !== 'admin')) return [];
        if (line.own) return ['del'];                                     // (anybody with a role may delete a line of their own: that is no power over a person)
        if (line.role === 'admin') return [];                             // (nobody acts on an administrator here)
        if (line.role === 'mod' && viewer !== 'admin') return [];         // (only an administrator acts on a moderator)
        var all = viewer === 'admin' ? ['del', 'mute', 'kick', 'ban'] : ['del', 'mute', 'kick'];
        return line.person ? all.slice(1) : all;                          // (a person of the players list has no line to delete)
    }
    function commandOf(kind, line, minutes) {                             // the hub's command for an action: by the number of the line (`#148`) or of the row of the players list (`row:4`), never by a name
        var n = line.id, who = (line.person ? 'row:' : '#') + n;
        if (kind === 'del') return line.person ? '' : '/del ' + n;
        if (kind === 'mute') return '/mute ' + who + ' ' + minutes;
        if (kind === 'unmute') return line.person ? '/unmute ' + who : '';
        if (kind === 'kick') return '/kick ' + who;
        if (kind === 'ban') return '/ban ' + who;
        if (kind === 'banpurge') return '/ban ' + who + ' purge';
        return '';
    }
    function minutesOf(text) {                                            // the minutes that a typed text means (digits only, 1 to 10,080), or null
        var t = String(text).replace(/^\s+|\s+$/g, '');
        if (!/^[0-9]{1,5}$/.test(t)) return null;
        var n = Number(t);
        return n >= 1 && n <= LIMIT.minutes ? n : null;
    }
    function lengthOf(n) {                                                // "10 minutes", "1 hour", "1 day", "1 week", "1 hour 30 minutes"
        var out = [], d = Math.floor(n / 1440), h = Math.floor(n % 1440 / 60), m = n % 60;
        if (n % LIMIT.minutes === 0) return n / LIMIT.minutes === 1 ? '1 week' : n / LIMIT.minutes + ' weeks';
        if (d) out.push(d + (d === 1 ? ' day' : ' days'));
        if (h) out.push(h + (h === 1 ? ' hour' : ' hours'));
        if (m) out.push(m + (m === 1 ? ' minute' : ' minutes'));
        return out.join(' ');
    }
    function doneWords(kind, who, minutes, purge) {                       // what an answer `ok` says
        if (kind === 'del') return 'Done: the line is gone.';
        if (kind === 'mute') return 'Done: ' + who + ' is muted for ' + lengthOf(minutes) + '.';
        if (kind === 'kick') return 'Done: ' + who + ' was kicked.';
        if (kind === 'unmute') return 'Done: ' + who + ' can write again.';
        return 'Done: ' + who + (purge ? ' is banned and the lines of the last hour are gone.' : ' is banned.');
    }
    function refusedWords(code, kind, who, role) {                        // what an answer that is not `ok` says (the hub's code, or offline and silent, this page's own)
        if (code === 'power') return NOTE.power + who + (role === 'admin' ? ' (an administrator).' : role === 'mod' ? ' (a moderator).' : '.');
        if (code === 'role') return NOTE.role;
        if (code === 'who') return kind === 'kick' ? who + ' is not on the front page now, so there is nobody to kick. Mute or ban instead.' : NOTE.away;
        if (code === 'bad') return kind === 'ban' ? who + ' is banned already.' : kind === 'unmute' ? who + ' is not muted any more.' : NOTE.gone;
        if (code === 'rate') return NOTE.rate;
        if (code === 'offline') return NOTE.offline;
        if (code === 'silent') return NOTE.silent;
        if (code === 'muted' || code === 'banned') return NOTE.mine;
        return NOTE.generic;
    }
    function cut(text, n) { text = String(text); return text.length > n ? text.slice(0, n - 1).replace(/[\ud800-\udbff]$/, '') + '\u2026' : text; }
    function firstOf(name) { var c = String(name).charCodeAt(0); return (c >= 0xd800 && c <= 0xdbff ? String(name).slice(0, 2) : String(name).charAt(0)).toUpperCase(); }

    // ---- the sheet ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // mount({ doc, win, dock }) returns { close, open(id) (a line), openPerson(i) (a row of the players list), people(list) (the list, once it is there), state() }
    function mount(env) {
        var doc = env.doc, win = env.win, dock = env.dock, log = dock.log, now = env.now || function () { return Date.now(); };
        var sheet = null, toastBox = null, toastTimer = 0, hintBox = null, statusBox = null, finger = false, quiet = 0, people = null, lines = null, persons = null;

        function el(tag, cls, text) { var e = doc.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
        function put(parent, kids) { kids.forEach(function (k) { if (k) parent.appendChild(k); }); return parent; }
        function guard(fn) { return function () { try { return fn.apply(this, arguments); } catch (e) { if (env.error) env.error(e); return undefined; } }; }
        function stay(e) { if (e && e.preventDefault) e.preventDefault(); }
        function held() { return finger || now() < quiet; }               // the finger that opened the sheet by a long press is still down, or has just lifted: the browser may send its click to what stands under it now, and that is no press
        var NS = ['http' + ':', '', 'www.w3.org', '2000', 'svg'].join('/');           // (the SVG namespace: a name, not an address that is asked for)
        function icon(kind, cls) {
            var svg = doc.createElementNS ? doc.createElementNS(NS, 'svg') : doc.createElement('svg');
            svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('class', cls || 'gm-ico'); svg.setAttribute('aria-hidden', 'true'); svg.setAttribute('focusable', 'false');
            ICONS[kind].forEach(function (p) { var n = doc.createElementNS ? doc.createElementNS(NS, p[0]) : doc.createElement(p[0]); p[1].split(';').forEach(function (a) { n.setAttribute(a.split(':')[0], a.split(':')[1]); }); svg.appendChild(n); });
            return svg;
        }
        function viewer() { var y = dock.you(); return y && (y.role === 'mod' || y.role === 'admin') ? y.role : ''; }
        function personOf(id) {                                         // a row of the players list as the sheet holds it (a person, not a line: no text, no username, `where` instead)
            var p = people ? people.person(id) : null;
            return p ? { id: p.i, who: p.name, role: p.role, where: p.where, person: true, own: false, sys: false, text: '' } : null;
        }
        function actionable(id) { return lines.target(id); }
        // ---- the two places that open the sheet: the lines of the log and the rows of the players list (the same marks, the same ways in); o: { kind, el (the scrolling box), row (a row's selector), attr (its number), keep (what keeps its own press: a link, a button), find(id), say(target) (the words for a screen reader), rowHint, hint (the box that describes the box) }
        function surface(o) {
            var s = { kind: o.kind, el: o.el, cur: null, press: null, hint: o.hint, status: o.status, cancel: null, say: o.say, top: !!o.top };
            s.idOf = function (row) { var n = row && row.getAttribute ? row.getAttribute(o.attr) : null; return n !== null && /^[0-9]{1,9}$/.test(n) ? Number(n) : null; };
            s.rowOf = function (node) { var r = node && node.closest ? node.closest(o.row) : null; return r && s.el.contains(r) ? r : null; };
            s.keeps = function (node) { var a = node && node.closest ? node.closest(o.keep) : null; return !!a && s.el.contains(a); };       // (a link or a button of the row is its own: it keeps the browser's menu and its press)
            s.rows = function () { return [].slice.call(s.el.querySelectorAll(o.row)); };
            s.target = function (id) { var t = id === null ? null : o.find(id); return t && actionsOf(viewer(), t).length ? t : null; };       // (the line or the person of a number that the viewer has an action on)
            s.subjects = function () { return [s.cur, s.press && s.press.shown ? s.press.id : null, sheet && !!sheet.line.person === (s.kind === 'person') ? sheet.line.id : null]; };
            s.decorate = function (row, id) {
                var on = !!viewer() && !!s.target(id);
                if (on) { row.setAttribute('data-act', 'true'); row.setAttribute('aria-description', o.rowHint); } else { row.removeAttribute('data-act'); row.removeAttribute('aria-description'); }
                if (row.classList) row.classList.toggle('gm-on', s.subjects().indexOf(id) !== -1);
            };
            s.refresh = function () {                                   // every row again (the role changed, or the list was drawn again)
                s.rows().forEach(function (r) { var id = s.idOf(r); if (id !== null) s.decorate(r, id); });
                s.hint.hidden = !viewer();
                if (!viewer()) s.status.textContent = '';
                if (viewer()) s.el.setAttribute('aria-describedby', s.hint.id); else { s.el.removeAttribute('aria-describedby'); s.cur = null; }       // (the words for a screen reader go with the role)
            };
            s.paint = function () { var on = s.subjects(); s.rows().forEach(function (r) { var id = s.idOf(r); if (id !== null && r.classList) r.classList.toggle('gm-on', on.indexOf(id) !== -1); }); };
            s.rowById = function (id) { return s.rows().filter(function (r) { return s.idOf(r) === id; })[0] || null; };
            s.ids = function () { return s.rows().map(s.idOf).filter(function (id) { return id !== null && !!s.target(id); }); };       // (the rows that the viewer may act on, in the order of the screen)
            return s;
        }
        function refresh() { lines.refresh(); if (persons) persons.refresh(); }
        function paint() { lines.paint(); if (persons) persons.paint(); }

        // ---- the toast: at the top, under the bar; the same words are a private line of the chat ----
        function clearToast() { if (toastTimer) { clearTimeout(toastTimer); toastTimer = 0; } if (toastBox && toastBox.parentNode) toastBox.parentNode.removeChild(toastBox); toastBox = null; }
        function toast(words, bad) {
            clearToast();
            var t = el('div', 'gm-toast' + (bad ? ' gm-bad' : '')), ok, bar = doc.querySelector ? doc.querySelector('.hbar') : null, top = LIMIT.top;
            t.setAttribute('role', 'status');
            put(t, [icon(bad ? 'warn' : 'check', 'gm-ico gm-ti'), el('span', 'gm-tt', words)]);
            if (bad) { ok = el('button', 'btn gm-tok', TEXT.ok); ok.type = 'button'; ok.addEventListener('click', guard(clearToast)); t.appendChild(ok); }
            try { if (bar && bar.getBoundingClientRect) top = Math.round(bar.getBoundingClientRect().bottom) + 10; } catch (e) { /* the usual place */ }
            t.style.top = top + 'px';
            doc.body.appendChild(t); toastBox = t;
            toastTimer = setTimeout(guard(function () { toastTimer = 0; clearToast(); }), bad ? LIMIT.stayBad : LIMIT.stay);
        }

        // ---- the sheet's views ----
        function circle(line) { return el('span', 'gc-av' + (line.role === 'admin' ? ' gc-r-admin' : line.role === 'mod' ? ' gc-r-mod' : ''), firstOf(line.who)); }
        function button(cls, text, run) {                               // a control of the sheet: it takes no press while a command waits
            var b = el('button', cls, text);
            b.type = 'button';
            b.addEventListener('click', guard(function () { if (sheet && !sheet.busy && !held() && (b.getAttribute('aria-disabled') !== 'true')) run(b); }));
            sheet.ctl.push(b);
            return b;
        }
        function title(text, role) { var h = el('h2', 'gm-name' + (role === 'admin' ? ' gc-r-admin' : role === 'mod' ? ' gc-r-mod' : ''), text); h.id = 'gm-title'; return h; }
        function head(line, name, plain, words) {
            var who = el('div', 'gm-who'), sub = words !== undefined ? words : (line.u ? '@' + line.u + ' \u00b7 ' : '') + (line.role === 'admin' ? TEXT.admin + ' \u00b7 ' : line.role === 'mod' ? TEXT.mod + ' \u00b7 ' : '') + TEXT.line + line.id + (line.own ? ' \u00b7 ' + TEXT.you : '');
            var subline = el('p', 'gm-sub', sub);
            subline.id = 'gm-sub';
            put(who, [title(name, plain ? 'none' : line.role), subline]);
            return put(el('div', 'gm-head'), [circle(line), who]);
        }
        function act(kind, verb, small, red, more, run) {
            var b = button('gm-act' + (red ? ' gm-red' : ''), '', run), t = el('span', 'gm-at');
            var d = el('small', '', small); d.id = 'gm-d-' + kind;
            put(t, [el('b', '', verb), d]);
            put(b, [icon(kind), t, more ? icon('chev', 'gm-ico gm-chev') : null]);
            b.setAttribute('aria-label', verb); b.setAttribute('aria-describedby', d.id);       // (the name is the verb and the person; the small words are the description)
            return b;
        }
        // ---- recent messages: the person's lines that are in the page's log (the welcome's history and what came since), found by the hub's key of the account that the dock holds; the sheet only follows a number ----
        function recentOf(s) {                                          // newest first; when the line that the sheet follows is gone, it follows another line of the same person
            var list = dock.by(s.anchor), i;
            for (i = 0; !list.length && i < s.seen.length; i++) list = dock.by(s.seen[i]);
            if (list.length) { s.anchor = list[0].id; s.seen = list.map(function (e) { return e.id; }); }
            return list;
        }
        function countWords(n) { return n + (n === 1 ? TEXT.one : TEXT.more); }
        function recentVerb(l, n) { return TEXT.recent + l.who + ' (' + n + ')'; }
        function near(order, id, ok) {                                  // the line after `id` in `order` (newest first) that `ok` accepts, else the one before it, else null
            var at = order.indexOf(id), rest = at === -1 ? order : order.slice(at + 1).concat(order.slice(0, at).reverse()), i;
            for (i = 0; i < rest.length; i++) if (rest[i] !== id && ok(rest[i])) return rest[i];
            return null;
        }
        function leave(name) { var s = sheet; if (!s) return; if (!dock.line(s.line.id)) { close(true); if (!s.mine) toast(NOTE.gone, true); } else view('line', name); }       // (back from the list: if the line that the sheet was opened on went meanwhile there is nothing to act on, and the page says so unless the viewer deleted it)
        function jump(id) { var l = actionable(id); if (l && sheet && !sheet.busy) { sheet.line = l; sheet.anchor = id; sheet.fromList = true; sheet.mine = false; view('line'); } }       // the sheet of another line of the same person (Delete works on it)
        function again(s, gone) {                                       // a line that was deleted from the list: back to the list, the focus on the next line (else the one before it, else Back)
            var pick = near(s.seen.slice(), gone, function (n) { return n !== gone && !!actionable(n); }), l = pick === null ? null : actionable(pick);
            s.busy = false; s.mine = true;
            if (l) { s.line = l; s.anchor = pick; }
            view('recent', pick);
        }
        function itemOf(s, id) { var i; for (i = 0; i < s.items.length; i++) if (Number(s.items[i].getAttribute('data-n')) === id) return s.items[i]; return null; }
        function fill(s, keep) {                                        // the list of the recent step, made (again) from the dock
            var box = s.rl, before = s.seen.slice(), list = recentOf(s), at = keep ? box.scrollTop : 0, mark = null, rel = 0, had = keep && box.contains(doc.activeElement) ? doc.activeElement.getAttribute('data-n') : null, fresh = [], last = 0, days = {}, multi, i, c, found = null, nb;
            if (keep) for (i = 0; i < box.children.length; i++) { c = box.children[i]; if (c.getAttribute('data-n') !== null && c.offsetTop + c.offsetHeight > at) { mark = c.getAttribute('data-n'); rel = c.offsetTop - at; break; } }
            box.textContent = '';
            list.forEach(function (e) { var w = e.at === undefined ? null : dock.when(e.at); if (w) days[w.key] = 1; });
            multi = Object.keys(days).length > 1;
            list.forEach(function (e) {
                var w = e.at === undefined ? null : dock.when(e.at), l = dock.line(e.id), can = !!l && actionsOf(viewer(), l).length > 0, row, t = el('span', 'gm-rt', w ? w.time : ''), x = el('span', 'gm-rx');
                if (multi && w && w.key !== last) { box.appendChild(el('p', 'gm-day', w.day)); last = w.key; }
                x.appendChild(el('span', 'gm-rq', cut(e.text, LIMIT.quote)));
                if (e.id === s.line.id) x.appendChild(el('small', 'gm-this', TEXT.thisLine));
                if (can) { row = button('gm-ri', '', function () { jump(e.id); }); fresh.push(row); } else row = el('div', 'gm-ri');
                if (w) row.setAttribute('title', w.full);
                row.setAttribute('data-n', String(e.id));
                if (e.id === s.line.id) row.className += ' gm-here';
                put(row, [t, x, can ? icon('chev', 'gm-ico gm-chev') : null]);
                if (can) row.setAttribute('aria-label', (w ? (multi ? w.day + ' ' : '') + w.time + '. ' : '') + cut(e.text, LIMIT.quote) + (e.id === s.line.id ? '. ' + TEXT.thisLine : ''));
                box.appendChild(row);
            });
            if (!list.length) box.appendChild(el('p', 'gm-none', TEXT.recentNone));
            s.ctl = fresh.concat(s.ctl.filter(function (k) { return fresh.indexOf(k) === -1 && s.items.indexOf(k) === -1; }));
            s.items = fresh;
            if (s.rsub) s.rsub.textContent = (s.line.u ? '@' + s.line.u + ' \u00b7 ' : '') + countWords(list.length) + TEXT.recentSub;
            if (mark !== null) { for (i = 0; i < box.children.length; i++) if (box.children[i].getAttribute('data-n') === mark) { found = box.children[i]; break; } if (found && at > 0) box.scrollTop = found.offsetTop - rel; else box.scrollTop = at; }
            if (had !== null) {                                         // (the focus stays on its line; if that line went, on the next one, else the one before it, else Back)
                nb = itemOf(s, Number(had)) || itemOf(s, near(before, Number(had), function (n) { return !!itemOf(s, n); })) || s.back;
                if (nb && nb.focus) nb.focus({ preventScroll: true });       // (the list keeps the place that is being read, wherever the focus is)
            }
            return list;
        }
        function personSub(l) { return (l.role === 'admin' ? TEXT.admin + ' \u00b7 ' : l.role === 'mod' ? TEXT.mod + ' \u00b7 ' : '') + l.where; }       // "moderator . Looking", "Waiting . Treasure . 2 of 4"
        function untilWords(until) {                                    // "2:51 PM today", or the log's word for another day: the log's own clock
            var w = dock.when(until), t = dock.when(Math.floor(now() / 1000));
            return w ? w.time + (t && t.key === w.key ? TEXT.today : ', ' + w.day) : '';
        }
        // ---- the mute row of a person's sheet: the hub's `/mutes` says whether the person is muted, and until then Mute is not offered (a mute of ten minutes on a person who is muted for a week would shorten it) ----
        function muteRow(s) {
            var l = s.line, who = l.who, b;
            if (s.mutes === 'muted') b = act('unmute', TEXT.unmute + who, TEXT.until + untilWords(s.until), false, false, function (c) { send('unmute', 0, false, c); });
            else if (s.mutes === 'free') b = act('mute', TEXT.mute + who + '\u2026', TEXT.muteSmall, false, true, function () { view('mute'); });
            else if (s.mutes === 'fail') b = act('mute', TEXT.again, TEXT.againSmall + who + TEXT.checkingEnd, false, false, function () { s.mutes = 'wait'; swap(s); learn(s); });
            else { b = act('mute', TEXT.mute + who + '\u2026', TEXT.checking + who + TEXT.checkingEnd, false, false, function () { /* waits for the hub */ }); b.setAttribute('aria-disabled', 'true'); b.setAttribute('aria-busy', 'true'); b.classList.add('gm-wait'); }
            return b;
        }
        function swap(s) {                                              // the mute row again, in its place (the focus stays on it)
            var old = s.mute, at = s.ctl.indexOf(old), had = doc.activeElement === old, fresh;
            if (!old || !old.parentNode) return;
            fresh = muteRow(s); s.ctl.pop();
            old.parentNode.replaceChild(fresh, old);
            if (at >= 0) s.ctl[at] = fresh;
            s.mute = fresh;
            if (had && fresh.focus) fresh.focus();
        }
        function learn(s) {                                             // ask the hub who is muted; the answer is matched by the number of the person's row
            dock.mutes(function (code, rows) {
                var i;
                if (sheet !== s) return;
                s.mutes = code === 'ok' && rows.length < LIMIT.listed ? 'free' : 'fail';       // (the hub lists 50 mutes at most, the ones that end soonest: a full list that does not name the person tells nothing about them)
                for (i = 0; code === 'ok' && i < rows.length; i++) if (rows[i].i === s.line.id) { s.mutes = 'muted'; s.until = rows[i].until; }
                if (s.view === 'line') swap(s);
            });
        }
        function view(name, arg) {
            var s = sheet, box = s.box, l = s.line, who = l.who, acts = actionsOf(viewer(), l), first = null, list, quote, ask = function (kind) { return function () { view('confirm', kind); }; };
            box.textContent = ''; box.className = 'gm-box'; s.ctl = []; s.view = name; s.armed = false; s.offer = acts.join();
            box.removeAttribute('aria-describedby');
            box.appendChild(el('div', 'gm-grab'));
            if (name === 'line') {
                var person = !!l.person;
                quote = person ? null : el('p', 'gm-quote', cut(l.text, LIMIT.quote));
                if (quote) quote.id = 'gm-quote';
                box.setAttribute('aria-describedby', quote ? quote.id : 'gm-sub');          // (the dialog is described by the quoted line, or for a person by where they are)
                list = el('div', 'gm-acts');
                var nr = l.own || person ? 0 : recentOf(s).length, recent = nr ? act('recent', recentVerb(l, nr), TEXT.recentSmall, false, true, function () { view('recent'); }) : null;       // (not on a line of one's own: Delete only; not on a person of the list: the list has no key of the account, and a name is not one person)
                var del = acts.indexOf('del') !== -1 ? act('del', TEXT.del, TEXT.delSmall, true, false, ask('del')) : null, mute = acts.indexOf('mute') === -1 ? null : person ? muteRow(s) : act('mute', TEXT.mute + who + '\u2026', TEXT.muteSmall, false, true, function () { view('mute'); });
                var kick = acts.indexOf('kick') !== -1 ? act('kick', TEXT.kick + who, TEXT.kickSmall, true, false, ask('kick')) : null, ban = acts.indexOf('ban') !== -1 ? act('ban', TEXT.ban + who, TEXT.banSmall, true, false, ask('ban')) : null;
                s.recent = recent; s.mute = person ? mute : null;
                put(list, [del, mute, kick, ban]);
                put(box, [head(l, l.who, false, person ? personSub(l) : undefined), quote, recent ? put(el('div', 'gm-acts gm-nav'), [recent]) : null, list, button('btn gm-cancel', TEXT.cancel, function () { close(true); })]);
                first = arg === 'recent' && recent ? recent : arg === 'mute' && mute ? mute : del || mute;
            } else if (name === 'recent') {
                var hd = head(l, TEXT.recent + who, true, ''), back = button('btn gm-cancel', TEXT.back, function () { leave('recent'); }), rl = el('div', 'gm-rl');
                rl.setAttribute('role', 'group'); rl.setAttribute('aria-label', TEXT.recentName);
                box.className = 'gm-box gm-rbox'; s.rl = rl; s.items = []; s.back = back; s.rsub = hd.querySelector('.gm-sub');
                put(box, [hd, rl, back]);
                fill(s, false);
                rl.setAttribute('aria-description', TEXT.recentKeys);
                first = (typeof arg === 'number' ? itemOf(s, arg) : null) || (arg === null ? null : s.items[0]) || back;       // (after a delete from the list: `arg` is the line to focus, or null for Back)
            } else if (name === 'mute') {
                var custom = el('div', 'gm-custom'), wrap = el('div', 'gm-num'), input = el('input'), go, hint = el('p', 'gm-hint', TEXT.hint), grid = el('div', 'gm-times');
                TIMES.forEach(function (t) { var b = button('btn gm-time', t[1], function (c) { send('mute', t[0], false, c); }); if (!first) first = b; grid.appendChild(b); });
                input.type = 'text'; input.setAttribute('inputmode', 'numeric'); input.setAttribute('maxlength', '5'); input.setAttribute('autocomplete', 'off'); input.setAttribute('aria-label', TEXT.customName); input.setAttribute('placeholder', TEXT.example);
                hint.id = 'gm-hint'; hint.setAttribute('aria-live', 'polite'); input.setAttribute('aria-describedby', hint.id);
                s.ctl.push(input);                                      // (the field comes before its button in the Tab order)
                go = button('btn gm-go', TEXT.go, function (c) { var n = minutesOf(input.value); if (n !== null) send('mute', n, false, c); });
                go.disabled = true;
                var typed = function () {
                    var n = minutesOf(input.value), has = input.value.replace(/\s/g, '') !== '', wrong = has && n === null;
                    go.disabled = n === null; input.setAttribute('aria-invalid', wrong ? 'true' : 'false'); input.className = wrong ? 'gm-bad' : ''; hint.className = 'gm-hint' + (wrong ? ' gm-err' : '');
                    hint.textContent = wrong ? TEXT.tooLong : n !== null ? n + (n === 1 ? ' minute' : ' minutes') + (n >= 60 ? ' = ' + lengthOf(n) : '') : TEXT.hint;
                };
                input.addEventListener('input', guard(typed));
                input.addEventListener('keydown', guard(function (e) { if (e && e.key === 'Enter') { stay(e); var n = minutesOf(input.value); if (n !== null && !s.busy) send('mute', n, false, go); } }));
                input.addEventListener('focus', guard(function () { if (input.scrollIntoView) input.scrollIntoView({ block: 'center' }); }));       // (a phone's keyboard comes up: the field is brought into view)
                put(wrap, [input, el('span', 'gm-unit', TEXT.minutes)]);
                put(custom, [wrap, go]);
                put(box, [head(l, TEXT.mute + who + TEXT.muteFor, true, l.person ? personSub(l) : undefined), grid, el('p', 'gm-cap', TEXT.custom), custom, hint, button('btn gm-cancel', TEXT.back, function () { view('line', 'mute'); })]);
            } else {                                                    // the confirm card: a mark, the question, whom, what happens, Cancel (the focus) and the red button
                var kind = arg, mark = el('div', 'gm-mark'), chip, text, no, yes, btns = el('div', 'gm-btns'), h = el('h2', 'gm-title', kind === 'del' ? TEXT.delTitle : (kind === 'kick' ? 'Kick ' : 'Ban ') + who + '?'), named = who + (l.u ? ' (@' + l.u + ')' : '');
                h.id = 'gm-title';
                mark.appendChild(icon(kind === 'del' ? 'del' : kind === 'kick' ? 'kick' : 'ban', 'gm-ico gm-mi'));
                if (kind === 'del') { chip = el('div', 'gm-chip gm-q'); put(chip, [el('span', 'gm-qw', named + ' \u00b7 ' + TEXT.line + l.id), el('span', 'gm-qt', cut(l.text, LIMIT.quote))]); }
                else chip = put(el('div', 'gm-chip'), [circle(l), el('div', 'gm-cn', named)]);
                text = el('p', 'gm-text', kind === 'del' ? TEXT.delText : who + (kind === 'kick' ? TEXT.kickText + '\u201c' + who + ' was kicked.\u201d' : TEXT.banText + '\u201c' + who + ' was banned.\u201d')); text.id = 'gm-text';
                box.className = 'gm-box gm-conf'; box.setAttribute('aria-describedby', text.id);
                no = button('btn gm-stay', TEXT.cancel, function () { close(true); });
                yes = button('btn leave gm-yes', kind === 'del' ? 'Delete' : kind === 'kick' ? 'Kick' : 'Ban', function (c) { if (s.armed) send(kind, 0, false, c); });
                first = no;
                put(box, [mark, h, chip, text]);
                put(btns, [no, yes]);
                if (kind === 'ban') {
                    box.appendChild(el('p', 'gm-note', TEXT.banNote)); box.appendChild(btns);
                    box.appendChild(put(el('div', 'gm-btns gm-2'), [button('btn leave gm-yes gm-wide', TEXT.banPurge, function (c) { if (s.armed) send('ban', 0, true, c); })]));
                } else box.appendChild(btns);
                s.timer = setTimeout(guard(function () { s.timer = 0; s.armed = true; }), LIMIT.arm);       // (the red button takes no press for a moment: a double press is not an answer)
            }
            s.shade.className = 'gm-shade' + (name === 'confirm' ? ' gm-card' : '');
            if (first && first.focus) first.focus();
            paint();
        }
        function open(id) { var l = lines.target(id); return !!l && !sheet && begin(l); }
        function openPerson(id) { var t = persons ? persons.target(id) : null; return !!t && !sheet && begin(t); }
        function begin(l) {                                              // the sheet of a line, or of a person of the players list (`l.person`)
            var shade = el('div', 'gm-shade'), box = el('div', 'gm-box'), quiet = [], kids, i;
            clearToast();                                                // (the last answer is read: it does not stay over the next sheet; the private line of the chat keeps it)
            kids = doc.body.children || [];
            box.setAttribute('role', 'dialog'); box.setAttribute('aria-modal', 'true'); box.setAttribute('aria-labelledby', 'gm-title'); box.setAttribute('tabindex', '-1');
            shade.appendChild(box);
            for (i = 0; i < kids.length; i++) if (!(kids[i].hasAttribute && kids[i].hasAttribute('inert'))) { kids[i].setAttribute('inert', ''); quiet.push(kids[i]); }       // (what stands behind takes no key or tap; what was inert before stays so)
            doc.body.appendChild(shade);
            sheet = { shade: shade, box: box, line: l, view: '', ctl: [], busy: false, armed: false, timer: 0, quiet: quiet, at: now(), offer: '', anchor: l.id, seen: [l.id], items: [], rl: null, rsub: null, back: null, recent: null, cur: null, fromList: false, mine: false, mutes: l.person ? 'wait' : '', until: 0, mute: null };
            shade.addEventListener('keydown', guard(onSheetKey));
            shade.addEventListener('click', guard(function (e) { if (e && e.target === shade && sheet && !sheet.busy && !held() && now() - sheet.at >= LIMIT.arm) close(true); }));       // (a press on the dark part is Cancel; not in the moment after a long press that opened it)
            shade.addEventListener('contextmenu', guard(stay));          // (the menu key's event comes when the key is let go, to what has the focus now: the sheet; the browser's own menu is not meant over it)
            shade.addEventListener('mousedown', guard(function (e) { if (held()) stay(e); }));       // (the mouse press that the browser makes of the lifting finger moves no focus)
            shade.addEventListener('pointerdown', guard(function () { finger = false; quiet = 0; }));       // (a new touch is a press)
            view('line');
            if (l.person) learn(sheet);
            return true;
        }
        function close(back) {
            var s = sheet;
            if (!s) return;
            sheet = null; finger = false; quiet = 0;
            if (s.timer) clearTimeout(s.timer);
            if (s.shade.parentNode) s.shade.parentNode.removeChild(s.shade);
            s.quiet.forEach(function (k) { k.removeAttribute('inert'); });
            paint();
            if (back) { var home = s.line.person && people ? people.list : log; if (home.focus) home.focus({ preventScroll: true }); }
        }
        function onSheetKey(e) {
            var s = sheet, i, list, at, stop;
            if (!s || !e) return;
            if (e.key === 'Escape') { stay(e); if (e.stopPropagation) e.stopPropagation(); if (!s.busy) { if (s.view === 'recent') leave('recent'); else if (s.view === 'mute') view('line', 'mute'); else close(true); } return; }
            if (s.view === 'recent' && (e.key === 'ArrowUp' || e.key === 'ArrowDown' || e.key === 'Home' || e.key === 'End') && !e.altKey && !e.ctrlKey && !e.metaKey && !e.shiftKey) { walk(s, e); return; }
            if (e.key !== 'Tab') return;
            stop = s.view === 'recent' ? stopOf(s) : null;
            list = s.ctl.filter(function (c) { return !c.hidden && !c.disabled && (s.view !== 'recent' || s.items.indexOf(c) === -1 || c === stop); });       // (the list is one stop: Up and Down walk it)
            if (!list.length) { stay(e); return; }
            at = list.indexOf(doc.activeElement);
            stay(e);
            i = at === -1 ? 0 : (at + (e.shiftKey ? list.length - 1 : 1)) % list.length;
            list[i].focus();
        }
        function stopOf(s) { var a = doc.activeElement; if (s.items.indexOf(a) !== -1) { s.cur = Number(a.getAttribute('data-n')); return a; } return itemOf(s, s.cur) || s.items[0] || null; }       // (the line of the list that the focus is on, or was last on)
        function walk(s, e) {                                           // the list of the recent step: Up and Down, Home and End move the focus along the lines
            var n = s.items.length, at = s.items.indexOf(doc.activeElement), up = e.key === 'ArrowUp', to;
            if (!n) return;
            to = e.key === 'Home' ? 0 : e.key === 'End' ? n - 1 : at === -1 ? (up ? n - 1 : 0) : Math.max(0, Math.min(n - 1, at + (up ? -1 : 1)));
            stay(e); s.items[to].focus(); s.cur = Number(s.items[to].getAttribute('data-n'));
        }
        function send(kind, minutes, purge, b) {                        // one command at a time: the buttons take no press until the hub has answered (the dock says when, at most 8 s)
            var s = sheet, l, role;
            if (!s || s.busy) return;
            l = s.line; role = l.role; s.busy = true;
            s.ctl.forEach(function (c) { c.setAttribute('aria-disabled', 'true'); });
            if (b) { b.setAttribute('aria-busy', 'true'); b.classList.add('gm-wait'); }
            dock.command(commandOf(purge ? 'banpurge' : kind, l, minutes), function (code) {
                var words = code === 'ok' ? doneWords(kind, l.who, minutes, purge) : refusedWords(code, kind, l.who, role);
                if (sheet === s) { if (kind === 'del' && s.fromList) again(s, l.id); else close(true); }
                toast(words, code !== 'ok');
                dock.note(words);
            });
        }

        // ---- the ways in: a right click, a long press, the keys (on each of the two places) ----
        function buzz() { try { if (win.navigator && typeof win.navigator.vibrate === 'function') win.navigator.vibrate(10); } catch (e) { /* no motor */ } }
        function openFrom(s, id) { return s.kind === 'person' ? openPerson(id) : open(id); }
        function watch(s) {
            function cancelPress() { var p = s.press; if (!p) return; clearTimeout(p.mark); clearTimeout(p.hold); s.press = null; if (p.shown) s.paint(); }
            s.cancel = cancelPress;
            s.el.addEventListener('contextmenu', guard(function (e) {
                if (!viewer()) return;
                var t = e && e.target, id;
                if (s.keeps(t)) return;                                // (a link or a button keeps the browser's own menu: copy the address, open in a new tab)
                id = s.idOf(s.rowOf(t));
                if (s.target(id)) { var touching = !!s.press; stay(e); cancelPress(); if (openFrom(s, id) && touching) finger = true; return; }       // (Android sends this event itself after a long press: the finger is still down then)
                if (t === s.el && !sheet && s.cur !== null && s.target(s.cur)) { stay(e); openFrom(s, s.cur); }       // (the menu key: the box has the focus)
            }));
            s.el.addEventListener('pointerdown', guard(function (e) {
                var id, p;
                if (s.cur !== null && !sheet) { s.cur = null; s.paint(); }          // (a pointer is used now: the row that the keys had chosen is let go, so that two rows are not marked)
                if (!viewer() || sheet || !e || (e.pointerType !== 'touch' && e.pointerType !== 'pen') || e.isPrimary === false) { cancelPress(); return; }
                if (s.keeps(e.target)) return;                         // (a press that starts on a link or a button is that control's, never the sheet's)
                id = s.idOf(s.rowOf(e.target));
                if (!s.target(id)) return;
                cancelPress();
                p = { id: id, x: e.clientX, y: e.clientY, shown: false };
                p.mark = setTimeout(guard(function () { if (s.press === p) { p.shown = true; s.paint(); } }), LIMIT.show);
                p.hold = setTimeout(guard(function () { if (s.press !== p) return; cancelPress(); buzz(); finger = openFrom(s, id); }), LIMIT.hold);
                s.press = p;
            }));
            s.el.addEventListener('pointermove', guard(function (e) { if (s.press && e && (Math.abs(e.clientX - s.press.x) > LIMIT.move || Math.abs(e.clientY - s.press.y) > LIMIT.move)) cancelPress(); }));
            ['pointerup', 'pointercancel', 'scroll'].forEach(function (t) { s.el.addEventListener(t, guard(function () { if (finger && t !== 'scroll') { finger = false; quiet = now() + LIMIT.arm; } cancelPress(); })); });
            s.el.addEventListener('blur', guard(function () { if (!sheet && s.cur !== null) { s.cur = null; s.paint(); } }));
            s.el.addEventListener('keydown', guard(function (e) {
                var list, at, id, r, t;
                if (!e || !viewer() || sheet || e.ctrlKey || e.altKey || e.metaKey || s.keeps(e.target)) return;       // (a key that starts on a link or a button of a row is that control's: Join joins, the arrows scroll)
                if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
                    list = s.ids();
                    if (!list.length) return;
                    at = list.indexOf(s.cur);
                    id = at === -1 ? list[s.top && e.key === 'ArrowDown' ? 0 : list.length - 1] : list[Math.max(0, Math.min(list.length - 1, at + (e.key === 'ArrowUp' ? -1 : 1)))];       // (from no row: the log starts at its newest line, the list at its first row on Down)
                    stay(e); s.cur = id; s.paint();
                    r = s.rowById(id); t = s.target(id);
                    if (r && r.scrollIntoView) r.scrollIntoView({ block: 'nearest' });
                    if (t) s.status.textContent = s.say(t);
                    return;
                }
                if (e.key === 'Enter' || e.key === 'ContextMenu' || (e.key === 'F10' && e.shiftKey)) {
                    id = s.cur !== null && s.target(s.cur) ? s.cur : null;
                    if (id === null && e.key !== 'Enter') { list = s.ids(); id = list.length ? list[s.top ? 0 : list.length - 1] : null; }
                    if (id !== null) { stay(e); s.cur = id; openFrom(s, id); }
                    return;
                }
                if (e.key === 'Escape' && s.cur !== null) { s.cur = null; s.paint(); if (e.stopPropagation) e.stopPropagation(); }       // (the dock's own Escape, which folds the chat, is not also meant)
            }));
        }

        // ---- the dock's news: a role that changes, a line that goes ----
        hintBox = el('p', 'gc-news', TEXT.logHint); hintBox.id = 'gchat-mod-hint';
        statusBox = el('p', 'gc-news'); statusBox.setAttribute('role', 'status'); statusBox.setAttribute('aria-live', 'polite');
        dock.root.appendChild(hintBox); dock.root.appendChild(statusBox);
        lines = surface({ kind: 'line', el: log, row: '.gc-line[data-n]', attr: 'data-n', keep: 'a', rowHint: TEXT.rowHint, hint: hintBox, status: statusBox, find: function (id) { return dock.line(id); }, say: function (l) { return l.who + ': ' + cut(l.text, 80) + '. ' + TEXT.rowHint; } });
        watch(lines);
        dock.rows(function (id, row) { lines.decorate(row, id); });
        function attach(p) {                                           // the players list (its own script), once it is there: its rows open the sheet of a person in the same ways; the person is named to the hub by the NUMBER of the row (`row:<n>`), never by a name
            if (persons || !p || !p.list || !p.root || typeof p.person !== 'function' || typeof p.drawn !== 'function') return;
            var hint = el('p', 'gc-news', TEXT.listHint), status = el('p', 'gc-news');       // (its own words for a screen reader: the chat dock is not shown on a phone's Players view)
            hint.id = 'gchat-mod-list-hint'; status.setAttribute('role', 'status'); status.setAttribute('aria-live', 'polite');
            p.root.appendChild(hint); p.root.appendChild(status);
            people = p;
            persons = surface({ kind: 'person', el: p.list, top: true, row: '.pl-card[data-i]', attr: 'data-i', keep: 'a, button', rowHint: TEXT.personHint, hint: hint, status: status, find: personOf, say: function (t) { return t.who + ': ' + t.where + '. ' + TEXT.personHint; } });
            watch(persons);
            p.drawn(guard(function () { persons.refresh(); if (sheet && sheet.line.person && !sheet.busy && p.state() === 'on' && !personOf(sheet.line.id)) { close(true); toast(NOTE.away, true); } }));       // (the list was drawn again: the marks follow, and a person who has gone has no sheet; a hub that is gone is the socket's, not this)
            persons.refresh();
        }
        function stale(s) { return !!s.line.person || actionsOf(viewer(), dock.line(s.line.id)).join() !== s.offer; }       // (a new welcome: what the viewer is offered on that line is not what the sheet shows: the line is not in the log any more, or the role is another)
        function changed() {                                           // a line came or went: the count on the sheet and the list of the recent step follow
            var s = sheet, n;
            if (!s || s.busy) return;
            if (s.view === 'recent') fill(s, true);
            else if (s.view === 'line' && s.recent) {
                n = recentOf(s).length;
                s.recent.querySelector('b').textContent = recentVerb(s.line, n); s.recent.setAttribute('aria-label', recentVerb(s.line, n));
            }
        }
        dock.arrived(guard(changed));
        dock.tap(guard(function (kind, m) {
            var person = !!sheet && !!sheet.line.person;
            if (kind === 'you' || kind === 'live') { if (sheet && (kind === 'you' || stale(sheet))) close(true); if (!viewer()) { lines.cancel(); lines.cur = null; if (persons) { persons.cancel(); persons.cur = null; } } refresh(); if (kind === 'live') changed(); }
            else if (kind === 'lost' && person) close(true);           // (the row numbers are the hub's own for the length of one run: a sheet of a person does not outlive the socket it was opened on; a new welcome is always after a lost socket)
            else if (kind === 'del' && m && sheet && !person && !sheet.busy && m.id === sheet.line.id && sheet.view !== 'recent') { close(true); toast(NOTE.gone, true); }
            else if (kind === 'del') changed();
        }));
        refresh();
        return { close: close, open: open, people: attach, openPerson: openPerson, state: function () { return sheet ? sheet.view : ''; } };
    }

    return { LIMIT: LIMIT, TEXT: TEXT, TIMES: TIMES, actionsOf: actionsOf, commandOf: commandOf, minutesOf: minutesOf, lengthOf: lengthOf, doneWords: doneWords, refusedWords: refusedWords, mount: mount };
});
