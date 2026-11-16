// The moderation sheet of Home's chat (docs/HOME_SCREEN.md, "Roles and commands"): staff (the hub's `you.role` is mod or admin) act on a person's line without typing: a right click on the line, a long press on it (a touch
// screen), or the keyboard (Up and Down choose a line of the log, Enter, the menu key or Shift and F10 open it) opens a sheet with what the viewer may do to that line's author: Delete the line, Mute (four times and a
// custom number of minutes), Kick, Ban (administrators; also with the person's lines of the last hour taken off every screen). Delete, Kick and Ban ask once more in a confirm card whose safe answer has the focus. ES5,
// no module system: a page gets AntsHomeMod, node gets module.exports (tests/scripts/web_home_check/web_home_mod_check.js).
//   * Loaded by web/home/home.js, with web/home/mod.css, only after the chat dock names a visitor with a role, and mounted into that dock. A person with no role never downloads it and sees no change: no line is marked, a
//     right click and a long press are the browser's own. A `you` frame that takes the role away closes the sheet and takes the marks off at once.
//   * It sends nothing by itself: it types the hub's own commands through the dock (`dock.command`), by the NUMBER of the line (`/mute #148 60`, `/kick #148`, `/ban #148`, `/ban #148 purge`, `/del 148`), so there is no new power:
//     the hub's table of the powers and its fresh check of the roles decide, and what the page offers is only its guess. A line is never found by a name that was typed or guessed.
//   * Only lines that the viewer has an action on are touched: the browser's context menu and its text selection are held back there and nowhere else (the selection only on a touch screen, in the style sheet); a link keeps
//     the browser's own menu. One command at a time: the buttons take no press while it waits; the answer is a toast at the top (and the same words as a private line of the chat), never a sound or a focus.
//   * Everything that is said (names, lines) becomes DOM text with createElement and textContent only, never markup.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeMod = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var LIMIT = { hold: 500, show: 150, move: 10, arm: 400, stay: 6000, stayBad: 12000, minutes: 10080, quote: 240, top: 76 };       // (ms and px; minutes: the hub's own longest mute, a week)
    var TIMES = [[10, '10 min'], [60, '1 hour'], [1440, '1 day'], [10080, '1 week']];       // the four buttons: minutes and words
    var TEXT = {
        rowHint: 'Moderation: press Enter, the menu key or Shift and F10 on this line for its actions.', logHint: 'For moderators: Up and Down choose a line, Enter opens its actions.',
        sheet: 'Actions', del: 'Delete this line', delSmall: 'Gone from every screen', mute: 'Mute ', muteSmall: 'Cannot write for a while', kick: 'Kick ', kickSmall: 'Logged out; can log in again',
        ban: 'Ban ', banSmall: 'Can read, cannot write', cancel: 'Cancel', back: 'Back', muteFor: ' \u2014 for how long?', custom: 'Custom', minutes: 'minutes', go: 'Mute', customName: 'Custom mute, in minutes',
        hint: 'Or type 1 to 10,080 minutes (1 week).', tooLong: 'The most is 10,080 minutes (1 week).', delTitle: 'Delete this line?', delText: 'It goes from every screen and from the history. This cannot be undone.',
        kickText: ' is logged out of the chat and can log in again at once. Everyone sees ', banText: ' can still read the chat but cannot write until an administrator lifts the ban. Everyone sees ',
        banNote: 'The second red button also removes everything that person wrote in the hour before the ban, from every screen. That cannot be undone.', banPurge: 'Ban and delete their lines from the last hour',
        ok: 'OK', example: 'for example 45', admin: 'administrator', mod: 'moderator', you: 'you', line: 'line #'
    };
    var NOTE = {                                                          // the words of an answer (the toast and the private line of the chat are these, and nothing else)
        power: 'You cannot do that to ', role: 'Could not confirm your role with Discord, try again in a moment.', rate: 'Too fast: wait a moment and try again.', offline: 'Chat is offline: nothing was done.',
        silent: 'Nothing came back: check the chat before trying again.', generic: 'That did not work, try again.', gone: 'That line is gone already.', mine: 'You cannot do that now.',
        updating: 'That did not work: the chat server may be updating, try again in a minute.'
    };
    var ICONS = {                                                         // [element, 'attribute:value;...'] (the same 24 x 24 outline pictures as the chat's other buttons)
        del: [['polyline', 'points:3 6 5 6 21 6'], ['path', 'd:M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6'], ['path', 'd:M10 11v6M14 11v6'], ['path', 'd:M9 6V4a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v2']],
        mute: [['path', 'd:M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z'], ['line', 'x1:3;y1:3;x2:21;y2:21']],
        kick: [['path', 'd:M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4'], ['polyline', 'points:16 17 21 12 16 7'], ['line', 'x1:21;y1:12;x2:9;y2:12']],
        ban: [['circle', 'cx:12;cy:12;r:10'], ['line', 'x1:4.93;y1:4.93;x2:19.07;y2:19.07']],
        chev: [['polyline', 'points:9 18 15 12 9 6']],
        check: [['path', 'd:M22 11.08V12a10 10 0 1 1-5.93-9.14'], ['polyline', 'points:22 4 12 14.01 9 11.01']],
        warn: [['path', 'd:M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z'], ['line', 'x1:12;y1:9;x2:12;y2:13'], ['line', 'x1:12;y1:17;x2:12.01;y2:17']]
    };

    // ---- the pure parts ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    function actionsOf(viewer, line) {                                    // what a viewer ('mod' or 'admin') is offered on a person's line: the page's guess; the hub decides all the same (a role that changed since the line was written is refused `power`)
        if (!line || line.sys || (viewer !== 'mod' && viewer !== 'admin')) return [];
        if (line.own) return ['del'];                                     // (anybody with a role may delete a line of their own: that is no power over a person)
        if (line.role === 'admin') return [];                             // (nobody acts on an administrator here)
        if (line.role === 'mod' && viewer !== 'admin') return [];         // (only an administrator acts on a moderator)
        return viewer === 'admin' ? ['del', 'mute', 'kick', 'ban'] : ['del', 'mute', 'kick'];
    }
    function commandOf(kind, line, minutes) {                             // the hub's command for an action: by the number of the line, never by a name
        var n = line.id;
        if (kind === 'del') return '/del ' + n;
        if (kind === 'mute') return '/mute #' + n + ' ' + minutes;
        if (kind === 'kick') return '/kick #' + n;
        if (kind === 'ban') return '/ban #' + n;
        if (kind === 'banpurge') return '/ban #' + n + ' purge';
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
        return 'Done: ' + who + (purge ? ' is banned and the lines of the last hour are gone.' : ' is banned.');
    }
    function refusedWords(code, kind, who, role) {                        // what an answer that is not `ok` says (the hub's code, or offline and silent, this page's own)
        if (code === 'power') return NOTE.power + who + (role === 'admin' ? ' (an administrator).' : role === 'mod' ? ' (a moderator).' : '.');
        if (code === 'role') return NOTE.role;
        if (code === 'who') return kind === 'kick' ? who + ' is not on the front page now, so there is nobody to kick. Mute or ban instead.' : 'That person is not here any more.';
        if (code === 'bad') return kind === 'ban' ? who + ' is banned already.' : NOTE.gone;
        if (code === 'rate') return NOTE.rate;
        if (code === 'offline') return NOTE.offline;
        if (code === 'silent') return NOTE.silent;
        if (code === 'muted' || code === 'banned') return NOTE.mine;
        return NOTE.generic;
    }
    function cut(text, n) { text = String(text); return text.length > n ? text.slice(0, n - 1).replace(/[\ud800-\udbff]$/, '') + '\u2026' : text; }
    function firstOf(name) { var c = String(name).charCodeAt(0); return (c >= 0xd800 && c <= 0xdbff ? String(name).slice(0, 2) : String(name).charAt(0)).toUpperCase(); }

    // ---- the sheet ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // mount({ doc, win, dock }) returns { close, open(id), state() }
    function mount(env) {
        var doc = env.doc, win = env.win, dock = env.dock, log = dock.log, now = env.now || function () { return Date.now(); };
        var sheet = null, cur = null, press = null, toastBox = null, toastTimer = 0, hintBox = null, statusBox = null, finger = false, quiet = 0;

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
        function idOf(row) { var n = row && row.getAttribute ? row.getAttribute('data-n') : null; return n !== null && /^[0-9]{1,9}$/.test(n) ? Number(n) : null; }
        function actionable(id) { var l = id === null ? null : dock.line(id); return l && actionsOf(viewer(), l).length ? l : null; }
        function rowOf(node) {                                          // the line of the log that a node is in, or null
            var r = node && node.closest ? node.closest('.gc-line') : null;
            return r && log.contains(r) ? r : null;
        }
        function inLink(node) { var a = node && node.closest ? node.closest('a') : null; return !!a && log.contains(a); }

        // ---- the marks on the log: the gold bar of the line that is pressed, chosen with the keys, or has its sheet open; and what a screen reader is told of an actionable line ----
        function subjects() { return [cur, press && press.shown ? press.id : null, sheet ? sheet.line.id : null]; }
        function decorate(row, id) {
            var on = !!viewer() && !!actionable(id);
            if (on) { row.setAttribute('data-act', 'true'); row.setAttribute('aria-description', TEXT.rowHint); } else { row.removeAttribute('data-act'); row.removeAttribute('aria-description'); }
            if (row.classList) row.classList.toggle('gm-on', subjects().indexOf(id) !== -1);
        }
        function refresh() {                                            // every line again (the role changed, or the page is mounted)
            var i, c, id;
            for (i = 0; i < log.children.length; i++) { c = log.children[i]; id = idOf(c); if (id !== null) decorate(c, id); }
            hintBox.hidden = !viewer(); if (!viewer()) statusBox.textContent = '';       // (the words for a screen reader go with the role)
            if (viewer()) { log.setAttribute('aria-describedby', hintBox.id); } else { log.removeAttribute('aria-describedby'); cur = null; }
        }
        function paint() { var s = subjects(), i, c, id; for (i = 0; i < log.children.length; i++) { c = log.children[i]; id = idOf(c); if (id !== null && c.classList) c.classList.toggle('gm-on', s.indexOf(id) !== -1); } }
        function rowById(id) { for (var i = 0; i < log.children.length; i++) if (idOf(log.children[i]) === id) return log.children[i]; return null; }
        function ids() {                                                // the lines that the viewer may act on, oldest first
            var out = [], i, id;
            for (i = 0; i < log.children.length; i++) { id = idOf(log.children[i]); if (id !== null && actionable(id)) out.push(id); }
            return out;
        }

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
        function head(line, name, plain) {
            var who = el('div', 'gm-who'), sub = (line.u ? '@' + line.u + ' \u00b7 ' : '') + (line.role === 'admin' ? TEXT.admin + ' \u00b7 ' : line.role === 'mod' ? TEXT.mod + ' \u00b7 ' : '') + TEXT.line + line.id + (line.own ? ' \u00b7 ' + TEXT.you : '');
            put(who, [title(name, plain ? 'none' : line.role), el('p', 'gm-sub', sub)]);
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
        function view(name, arg) {
            var s = sheet, box = s.box, l = s.line, who = l.who, acts = actionsOf(viewer(), l), first = null, list, quote, ask = function (kind) { return function () { view('confirm', kind); }; };
            box.textContent = ''; box.className = 'gm-box'; s.ctl = []; s.view = name; s.armed = false;
            box.removeAttribute('aria-describedby');
            box.appendChild(el('div', 'gm-grab'));
            if (name === 'line') {
                quote = el('p', 'gm-quote', cut(l.text, LIMIT.quote)); quote.id = 'gm-quote'; box.setAttribute('aria-describedby', quote.id);
                list = el('div', 'gm-acts');
                var del = acts.indexOf('del') !== -1 ? act('del', TEXT.del, TEXT.delSmall, true, false, ask('del')) : null, mute = acts.indexOf('mute') !== -1 ? act('mute', TEXT.mute + who + '\u2026', TEXT.muteSmall, false, true, function () { view('mute'); }) : null;
                var kick = acts.indexOf('kick') !== -1 ? act('kick', TEXT.kick + who, TEXT.kickSmall, true, false, ask('kick')) : null, ban = acts.indexOf('ban') !== -1 ? act('ban', TEXT.ban + who, TEXT.banSmall, true, false, ask('ban')) : null;
                put(list, [del, mute, kick, ban]);
                put(box, [head(l, l.who), quote, list, button('btn gm-cancel', TEXT.cancel, function () { close(true); })]);
                first = arg === 'mute' && mute ? mute : del || mute;
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
                put(box, [head(l, TEXT.mute + who + TEXT.muteFor, true), grid, el('p', 'gm-cap', TEXT.custom), custom, hint, button('btn gm-cancel', TEXT.back, function () { view('line', 'mute'); })]);
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
        function open(id) {
            var l = actionable(id);
            if (!l || sheet) return false;
            var shade = el('div', 'gm-shade'), box = el('div', 'gm-box'), quiet = [], kids, i;
            clearToast();                                                // (the last answer is read: it does not stay over the next sheet; the private line of the chat keeps it)
            kids = doc.body.children || [];
            box.setAttribute('role', 'dialog'); box.setAttribute('aria-modal', 'true'); box.setAttribute('aria-labelledby', 'gm-title'); box.setAttribute('tabindex', '-1');
            shade.appendChild(box);
            for (i = 0; i < kids.length; i++) if (!(kids[i].hasAttribute && kids[i].hasAttribute('inert'))) { kids[i].setAttribute('inert', ''); quiet.push(kids[i]); }       // (what stands behind takes no key or tap; what was inert before stays so)
            doc.body.appendChild(shade);
            sheet = { shade: shade, box: box, line: l, view: '', ctl: [], busy: false, armed: false, timer: 0, quiet: quiet, at: now() };
            shade.addEventListener('keydown', guard(onSheetKey));
            shade.addEventListener('click', guard(function (e) { if (e && e.target === shade && sheet && !sheet.busy && !held() && now() - sheet.at >= LIMIT.arm) close(true); }));       // (a press on the dark part is Cancel; not in the moment after a long press that opened it)
            shade.addEventListener('contextmenu', guard(stay));          // (the menu key's event comes when the key is let go, to what has the focus now: the sheet; the browser's own menu is not meant over it)
            shade.addEventListener('mousedown', guard(function (e) { if (held()) stay(e); }));       // (the mouse press that the browser makes of the lifting finger moves no focus)
            shade.addEventListener('pointerdown', guard(function () { finger = false; quiet = 0; }));       // (a new touch is a press)
            view('line');
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
            if (back && log.focus) log.focus({ preventScroll: true });
        }
        function onSheetKey(e) {
            var s = sheet, i, list, at;
            if (!s || !e) return;
            if (e.key === 'Escape') { stay(e); if (e.stopPropagation) e.stopPropagation(); if (!s.busy) { if (s.view === 'mute') view('line', 'mute'); else close(true); } return; }
            if (e.key !== 'Tab') return;
            list = s.ctl.filter(function (c) { return !c.hidden && !c.disabled; });
            if (!list.length) { stay(e); return; }
            at = list.indexOf(doc.activeElement);
            stay(e);
            i = at === -1 ? 0 : (at + (e.shiftKey ? list.length - 1 : 1)) % list.length;
            list[i].focus();
        }
        function send(kind, minutes, purge, b) {                        // one command at a time: the buttons take no press until the hub has answered (the dock says when, at most 8 s)
            var s = sheet, l, role;
            if (!s || s.busy) return;
            l = s.line; role = l.role; s.busy = true;
            s.ctl.forEach(function (c) { c.setAttribute('aria-disabled', 'true'); });
            if (b) { b.setAttribute('aria-busy', 'true'); b.classList.add('gm-wait'); }
            dock.command(commandOf(purge ? 'banpurge' : kind, l, minutes), function (code) {
                var words = code === 'ok' ? doneWords(kind, l.who, minutes, purge) : refusedWords(code, kind, l.who, role);
                if (sheet === s) close(true);
                toast(words, code !== 'ok');
                dock.note(words);
            });
        }

        // ---- the ways in: a right click, a long press, the keys ----
        function cancelPress() { if (!press) return; clearTimeout(press.mark); clearTimeout(press.hold); var was = press.shown; press = null; if (was) paint(); }
        function buzz() { try { if (win.navigator && typeof win.navigator.vibrate === 'function') win.navigator.vibrate(10); } catch (e) { /* no motor */ } }
        log.addEventListener('contextmenu', guard(function (e) {
            if (!viewer()) return;
            var t = e && e.target, id;
            if (inLink(t)) return;                                     // (a link keeps the browser's own menu: copy the address, open in a new tab)
            id = idOf(rowOf(t));
            if (actionable(id)) { var touching = !!press; stay(e); cancelPress(); if (open(id) && touching) finger = true; return; }       // (Android sends this event itself after a long press: the finger is still down then)
            if (t === log && !sheet && cur !== null && actionable(cur)) { stay(e); open(cur); }       // (the menu key: the log has the focus)
        }));
        log.addEventListener('pointerdown', guard(function (e) {
            var id;
            if (cur !== null && !sheet) { cur = null; paint(); }          // (a pointer is used now: the line that the keys had chosen is let go, so that two lines are not marked)
            if (!viewer() || sheet || !e || (e.pointerType !== 'touch' && e.pointerType !== 'pen') || e.isPrimary === false) { cancelPress(); return; }
            if (inLink(e.target)) return;
            id = idOf(rowOf(e.target));
            if (!actionable(id)) return;
            cancelPress();
            var p = { id: id, x: e.clientX, y: e.clientY, shown: false };
            p.mark = setTimeout(guard(function () { if (press === p) { p.shown = true; paint(); } }), LIMIT.show);
            p.hold = setTimeout(guard(function () { if (press !== p) return; cancelPress(); buzz(); finger = open(id); }), LIMIT.hold);
            press = p;
        }));
        log.addEventListener('pointermove', guard(function (e) { if (press && e && (Math.abs(e.clientX - press.x) > LIMIT.move || Math.abs(e.clientY - press.y) > LIMIT.move)) cancelPress(); }));
        ['pointerup', 'pointercancel', 'scroll'].forEach(function (t) { log.addEventListener(t, guard(function () { if (finger && t !== 'scroll') { finger = false; quiet = now() + LIMIT.arm; } cancelPress(); })); });
        log.addEventListener('blur', guard(function () { if (!sheet && cur !== null) { cur = null; paint(); } }));
        log.addEventListener('keydown', guard(function (e) {
            var list, at, id;
            if (!e || !viewer() || sheet || e.ctrlKey || e.altKey || e.metaKey) return;
            if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
                list = ids();
                if (!list.length) return;
                at = list.indexOf(cur);
                id = at === -1 ? list[list.length - 1] : list[Math.max(0, Math.min(list.length - 1, at + (e.key === 'ArrowUp' ? -1 : 1)))];
                stay(e); cur = id; paint();
                var r = rowById(id), l = dock.line(id);
                if (r && r.scrollIntoView) r.scrollIntoView({ block: 'nearest' });
                if (l) statusBox.textContent = l.who + ': ' + cut(l.text, 80) + '. ' + TEXT.rowHint;
                return;
            }
            if (e.key === 'Enter' || e.key === 'ContextMenu' || (e.key === 'F10' && e.shiftKey)) {
                id = cur !== null && actionable(cur) ? cur : null;
                if (id === null && e.key !== 'Enter') { list = ids(); id = list.length ? list[list.length - 1] : null; }
                if (id !== null) { stay(e); cur = id; open(id); }
                return;
            }
            if (e.key === 'Escape' && cur !== null) { cur = null; paint(); if (e.stopPropagation) e.stopPropagation(); }       // (the dock's own Escape, which folds the chat, is not also meant)
        }));

        // ---- the dock's news: a role that changes, a line that goes ----
        hintBox = el('p', 'gc-news', TEXT.logHint); hintBox.id = 'gchat-mod-hint';
        statusBox = el('p', 'gc-news'); statusBox.setAttribute('role', 'status'); statusBox.setAttribute('aria-live', 'polite');
        dock.root.appendChild(hintBox); dock.root.appendChild(statusBox);
        dock.rows(function (id, row) { decorate(row, id); });
        dock.tap(guard(function (kind, m) {
            if (kind === 'you' || kind === 'live') { if (sheet && kind === 'you') close(true); if (!viewer()) { cancelPress(); cur = null; } refresh(); }
            else if (kind === 'del' && m && sheet && !sheet.busy && m.id === sheet.line.id) { close(true); toast(NOTE.gone, true); }
        }));
        refresh();
        return { close: close, open: open, state: function () { return sheet ? sheet.view : ''; } };
    }

    return { LIMIT: LIMIT, TEXT: TEXT, TIMES: TIMES, actionsOf: actionsOf, commandOf: commandOf, minutesOf: minutesOf, lengthOf: lengthOf, doneWords: doneWords, refusedWords: refusedWords, mount: mount };
});
