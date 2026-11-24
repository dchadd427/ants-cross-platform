// The private messages of Home (docs/HUB_MESSAGES.md): the Messages button in the chat's head with the count of what is not read, the panel that it opens (a sheet on a phone), the list, one conversation and the report card. Loaded by
// web/home/home.js after the chat dock, with web/home/messages.css; it speaks through the dock's socket and keeps nothing in the browser. The hub decides who may write and what is kept and counted. Down: `msgs`, `msg` (the echo of a message
// that this person sent is what draws it), `thread`, `dmx` (a refusal), `dmreported`. Up: dmopen, dm, dmread, dmdel, dmreport and the commands `/ignore h:HANDLE` and `/unignore h:HANDLE` (Block): a person is named by a handle that the hub gave.
//   * What the hub sends is text from strangers: cleaned and cut as the chat's text is, numbers and handles checked, a frame that is wrong ignored, 50 conversations and 100 messages of one at the most, drawn as text, never as markup.
//   * Nothing is drawn for a guest or for a hub that is not there. A conversation that is on the screen says `dmread` for each message that comes in.
// ES5, no module system: a page gets AntsHomeMessages, node gets module.exports (tests/scripts/web_home_messages_check.js).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeMessages = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var HANDLE = /^[0-9a-f]{12}$/, WHY = ['abuse', 'spam', 'other'], SVG = 'http://www.w3.org/2000/svg', PHONE = '(max-width: 700px), (max-height: 500px)';
    var LIMIT = { convs: 50, rows: 100, threads: 40, name: 32, text: 500, at: 4e9, wait: 8000 };
    var TEXT = {
        button: 'Messages', title: 'Private messages', close: 'Close', back: 'Back to the list', more: 'More', tag: 'Private', unread: ' unread', loading: 'Loading\u2026', empty: 'Nothing here yet. Say hello.', today: 'Today', you: 'You: ',
        none: 'No private messages yet', noneLine: 'Open a person\u2019s menu and press Message, or type /msg NAME.', followed: 'You follow this person', report: 'Report', markup: 'Messages cannot hold < or >', offline: 'Chat is offline',
        keep: 'Messages are kept for 7 days. Anyone can write to you; block a person to stop their messages.', notePre: 'Only you and ', notePost: ' can read this. Admins see a message only if one of you reports it. Kept for 7 days.',
        toPre: 'Private message to ', toPost: '. This is not the public chat.', field: 'Private message\u2026', send: 'Send', block: 'Block ', unblock: 'Unblock ', del: 'Delete this conversation', delAgain: 'Press again to delete it',
        blocked: ' is blocked: you get no more messages from them.', unblocked: ' is not blocked any more.', staff: 'You cannot block a moderator.', cardTitle: 'Report this message', cancel: 'Cancel', sendReport: 'Send report',
        why: ['Harassment or threats', 'Spam or links', 'Something else'], thanks: 'Thanks: the admins will look at it.', nothing: ' has not sent you a private message, so there is nothing to report.',
        finePre: 'The admins will see this message and the 5 before it in this conversation, with both names. Nothing else of your messages. ', finePost: ' is not told who reported.'
    };
    var DMX = { empty: 'Write something first', long: 'That message is too long', bad: 'That message could not be sent', banned: 'You cannot send messages now', muted: 'You cannot send messages now', young: 'Your account is too new to send messages yet',
        who: 'That person is not available', dup: 'You just sent that', rate: 'Slow down a little', quota: 'You have sent a lot of messages today', 'new': 'You have started a lot of conversations: try again later',
        full: 'Messages are full for now: try again later', busy: 'That did not go through, try again', gone: 'That message is gone' };
    var PATHS = { lock: ['M7 11V8a5 5 0 0 1 10 0v3', 'M6 11h12v9H6z'], back: ['M15 5l-7 7 7 7'], x: ['M6 6l12 12', 'M18 6L6 18'], more: ['M5 12h.01', 'M12 12h.01', 'M19 12h.01'], star: ['M12 3.5l2.6 5.4 5.9.8-4.3 4.1 1 5.9L12 17l-5.2 2.7 1-5.9L3.5 9.7l5.9-.8z'] };

    // ---- the pure parts ----
    function whole(v, low, high) { return typeof v === 'number' && v === Math.floor(v) && v >= low && v <= high; }
    function clean(Chat, s, max, strict) { return typeof s === 'string' ? Chat.clip(Chat.plain(s.slice(0, max * 4), strict), max) : ''; }
    function initial(name, Chat) { var first = Chat.clip(name, 1); return first.length > 1 && first.charAt(first.length - 1) === '\u2026' ? first.slice(0, -1) : first; }
    function countText(n) { return n > 99 ? '99+' : String(n); }
    function handleOf(v) { return typeof v === 'string' && HANDLE.test(v); }
    // One conversation of the list, cleaned: { h, name, at, text, me (the newest is the visitor's own), u (not read), f (followed), b (blocked) }, or null
    function convOf(raw, Chat) {
        if (!raw || typeof raw !== 'object' || !handleOf(raw.h) || !whole(raw.at, 0, LIMIT.at) || !whole(raw.u, 0, 1e9)) return null;
        var name = clean(Chat, raw.name, LIMIT.name, true);
        return name ? { h: raw.h, name: name, at: raw.at, text: clean(Chat, raw.text, LIMIT.text, false), me: raw.me === 1, u: raw.u, f: raw.f === 1, b: raw.b === 1 } : null;
    }
    // One message, cleaned: { id, at, text, me }, or null
    function msgOf(raw, Chat) {
        if (!raw || typeof raw !== 'object' || !whole(raw.id, 0, 1e15) || !whole(raw.at, 0, LIMIT.at)) return null;
        var text = clean(Chat, raw.text, LIMIT.text, false);
        return text ? { id: raw.id, at: raw.at, text: text, me: raw.me === 1 } : null;
    }
    function listOf(m, Chat) {                                          // the list that a `msgs` frame makes, or null for a frame that is not one
        if (!m || typeof m !== 'object' || !Array.isArray(m.rows) || m.rows.length > LIMIT.convs) return null;
        return m.rows.map(function (raw) { return convOf(raw, Chat); }).filter(Boolean);
    }
    function threadOf(m, Chat) {                                        // the conversation that a `thread` frame makes: { h, name, f, b, rows, loaded }, or null
        if (!m || typeof m !== 'object' || !handleOf(m.h) || !Array.isArray(m.rows) || m.rows.length > LIMIT.rows) return null;
        var name = clean(Chat, m.name, LIMIT.name, true);
        return name ? { h: m.h, name: name, f: m.f === 1, b: m.b === 1, rows: m.rows.map(function (raw) { return msgOf(raw, Chat); }).filter(Boolean), loaded: true } : null;
    }

    // mount({ doc, win (with AntsHomeChat), dock: { tap, send, call, root, when, note }, say(words, tone), now() (ms), error }) -> { open(h, name), openList(), close(), report(h, name), isOpen(h), count(), state(), button() }
    function mount(env) {
        var doc = env.doc, win = env.win, dock = env.dock, Chat = win.AntsHomeChat;
        if (!dock || typeof dock.tap !== 'function' || !Chat || !doc.createElement) return null;
        var now = env.now || function () { return Date.now(); };
        var you = null, live = false, convs = [], threads = {}, cur = '', view = 'list', shade = null, panel = null, opener = null, listBox = null, logBox = null, field = null, sendBtn = null, status = null, moreBtn = null;
        var menuOn = false, armed = false, timers = {}, sending = null, asking = null, card = null, cardShade = null, cardOpener = null, btn = null, badge = null;

        function el(tag, cls, text) { var e = doc.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
        function put(parent, kids) { kids.forEach(function (k) { if (k) parent.appendChild(k); }); return parent; }
        function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }
        function guard(fn) { return function () { try { return fn.apply(this, arguments); } catch (e) { if (env.error) env.error(e); return undefined; } }; }
        function stop(name) { if (timers[name]) { clearTimeout(timers[name]); timers[name] = 0; } }
        function later(name, ms, fn) { stop(name); timers[name] = setTimeout(guard(function () { timers[name] = 0; fn(); }), ms); }
        function icon(name, cls) {                                      // a small picture drawn with the DOM; a browser that cannot draw it shows none
            try {
                var svg = doc.createElementNS(SVG, 'svg');
                svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('class', 'pm-ico ' + (cls || '')); svg.setAttribute('aria-hidden', 'true'); svg.setAttribute('focusable', 'false');
                PATHS[name].forEach(function (d) { var p = doc.createElementNS(SVG, 'path'); p.setAttribute('d', d); svg.appendChild(p); });
                return svg;
            } catch (e) { return null; }
        }
        function button(cls, text, press, label) {
            var b = el('button', cls, text);
            b.type = 'button'; if (label) b.setAttribute('aria-label', label);
            b.addEventListener('click', guard(press));
            return b;
        }
        function iconButton(cls, label, name, press) { var b = button('pm-ib ' + cls, undefined, press, label), i = icon(name); b.setAttribute('title', label); if (i) b.appendChild(i); else b.textContent = label.charAt(0); return b; }
        function say(words, tone) {                                     // the page's toast (the chat's own line where the page has none); under a phone's sheet the toast is not seen: the line under the field says it
            if (shade && status && view === 'thread' && phone()) { status.textContent = words; return; }
            try { if (typeof env.say === 'function') env.say(words, tone); else if (typeof dock.note === 'function') dock.note(words); } catch (e) { /* the page's */ }
        }
        function phone() { try { return !!win.matchMedia && win.matchMedia(PHONE).matches; } catch (e) { return false; } }
        function count() { var n = 0; convs.forEach(function (c) { n += c.u; }); return n; }
        function conv(h) { for (var i = 0; i < convs.length; i++) if (convs[i].h === h) return convs[i]; return null; }
        function isOpen(h) { return !!shade && view === 'thread' && !!cur && cur === h && !doc.hidden; }
        function when(at) {                                             // the clock of the day, the day's word, and whether it is today (the dock's own words for a time; a dock that has none gives "N min ago")
            var sec = Math.floor(now() / 1000), w = typeof dock.when === 'function' ? dock.when(at) : null, t = w ? dock.when(sec) : null;
            return w && t ? { time: w.time, day: w.day, today: w.key === t.key } : { time: '', day: Chat.agoOf(at, sec), today: false };
        }
        function read(h) { dock.send({ t: 'dmread', h: h }); }

        // ---- the button in the chat's head ----
        function paintButton() {
            if (!btn) return;
            var n = count();
            btn.hidden = !(you && live);
            btn.setAttribute('aria-label', TEXT.button + (n ? ', ' + n + TEXT.unread : '')); btn.setAttribute('aria-expanded', shade ? 'true' : 'false');
            badge.hidden = !n; badge.textContent = countText(n);
        }
        function makeButton() {
            var box = dock.root, head = box && box.querySelector ? box.querySelector('.gc-head') : null, here;
            if (!head) return;
            btn = button('btn pm-btn', undefined, function (e) { if (shade) closePanel(); else openPanel(btn); });
            btn.setAttribute('aria-haspopup', 'dialog'); btn.hidden = true;
            badge = el('span', 'pm-n'); badge.hidden = true; badge.setAttribute('aria-hidden', 'true');
            put(btn, [icon('lock'), el('span', 'pm-bt', TEXT.button), badge]);
            here = head.querySelector('.gc-online');
            head.insertBefore(btn, here && here.parentNode === head ? here : null);
            if (head.classList) head.classList.add('pm-has');
        }

        // ---- the list and one conversation ----
        function row(c) {
            var b = button('pm-row' + (c.u ? ' unread' : ''), undefined, function () { openThread(c.h, c.name); }, c.name + (c.u ? ', ' + c.u + TEXT.unread : '') + ', ' + (c.me ? TEXT.you : '') + c.text), line = el('span', 'pm-ln'), w = when(c.at), star = c.f ? icon('star', 'pm-star') : null;
            if (star) { star.removeAttribute('aria-hidden'); star.setAttribute('role', 'img'); star.setAttribute('aria-label', TEXT.followed); }
            put(line, [el('bdi', 'pm-nm', c.name), star]);
            return put(b, [el('span', 'pm-av', initial(c.name, Chat)), put(el('span', 'pm-tx'), [line, el('bdi', 'pm-pv', (c.me ? TEXT.you : '') + c.text)]), put(el('span', 'pm-meta'), [el('span', 'pm-when', w.today ? w.time : w.day), c.u ? el('span', 'pm-u', countText(c.u)) : null])]);
        }
        function fillList() {
            if (!listBox) return;
            clear(listBox);
            if (convs.length) convs.forEach(function (c) { listBox.appendChild(row(c)); });
            else listBox.appendChild(put(el('div', 'pm-empty'), [icon('lock', 'big'), el('p', 'pm-et', TEXT.none), el('p', 'pm-es', TEXT.noneLine)]));
        }
        function fillLog() {
            if (!logBox) return;
            var t = threads[cur], stick = !logBox.scrollHeight || logBox.scrollHeight - logBox.scrollTop - logBox.clientHeight < 80, day = null;
            clear(logBox);
            if (!t || !t.loaded || !t.rows.length) logBox.appendChild(el('p', 'pm-empty pm-es', t && t.loaded ? TEXT.empty : TEXT.loading));
            else t.rows.forEach(function (g) {
                var w = when(g.at), meta = put(el('span', 'pm-mm'), [el('span', 'pm-tm', w.time)]);
                if (w.day !== day) { day = w.day; logBox.appendChild(el('div', 'pm-day', w.today ? TEXT.today : w.day)); }
                if (!g.me) meta.appendChild(button('pm-rep', TEXT.report, function () { openCard({ h: t.h, id: g.id, text: g.text, name: t.name }); }, TEXT.report + ': ' + g.text.slice(0, 40)));
                logBox.appendChild(put(el('div', 'pm-msg ' + (g.me ? 'me' : 'them')), [el('bdi', 'pm-mt', g.text), meta]));
            });
            if (stick) logBox.scrollTop = logBox.scrollHeight;
        }
        function paintMenu() {
            if (!panel) return;
            var old = panel.querySelector('.pm-menu'), t = threads[cur], items;
            if (old) panel.removeChild(old);
            if (moreBtn) moreBtn.setAttribute('aria-expanded', menuOn ? 'true' : 'false');
            if (!menuOn || !t) return;
            items = [button('pm-mi', (t.b ? TEXT.unblock : TEXT.block) + t.name, toggleBlock), button('pm-mi pm-danger', armed ? TEXT.delAgain : TEXT.del, remove)];
            items.forEach(function (b) { b.setAttribute('role', 'menuitem'); });
            old = put(el('div', 'pm-menu'), items); old.setAttribute('role', 'menu');
            panel.appendChild(old);
        }
        function setStatus(words) { if (status) status.textContent = words || ''; else if (words) say(words, 'warn'); }
        function toggleBlock() {                                        // Block and Unblock are the hub's own commands: `/ignore` and `/unignore`
            var t = threads[cur], h = cur;
            menuOn = false; paintMenu();
            if (!t) return;
            if (typeof dock.call !== 'function') { say(TEXT.offline, 'warn'); return; }
            dock.call((t.b ? '/unignore h:' : '/ignore h:') + h, guard(function (code, m) {
                var r = code === 'reply' && m ? m.r : code, was = t.b, c = conv(h);
                if (r !== 'ok' && !(was && r === 'not')) { say(r === 'staff' ? TEXT.staff : code === 'offline' ? TEXT.offline : DMX.busy, 'warn'); return; }
                t.b = !was; if (c) c.b = !was;
                say(t.name + (was ? TEXT.unblocked : TEXT.blocked));
                paintMenu();
            }));
        }
        function disarm() { stop('arm'); armed = false; if (menuOn) paintMenu(); }
        function remove() {                                             // Delete asks once more: the second press does it
            if (!armed) { armed = true; paintMenu(); later('arm', 4000, disarm); return; }
            var h = cur;
            disarm();
            if (!dock.send({ t: 'dmdel', h: h })) { say(TEXT.offline, 'warn'); return; }
            convs = convs.filter(function (c) { return c.h !== h; }); delete threads[h];
            menuOn = false; view = 'list'; cur = '';
            paint(); paintButton();
        }
        function endSend() { stop('send'); sending = null; if (sendBtn) { sendBtn.removeAttribute('aria-busy'); sendBtn.removeAttribute('aria-disabled'); } }
        function submit() {
            if (!field || sending) return;
            var words = Chat.plain(field.value);
            if (!words) { setStatus(DMX.empty); return; }
            if (/[<>]/.test(words)) { setStatus(TEXT.markup); return; }
            if (Chat.points(words) > LIMIT.text) { setStatus(DMX.long); return; }
            if (!dock.send({ t: 'dm', h: cur, text: words })) { setStatus(TEXT.offline); return; }
            setStatus(''); sending = { h: cur, text: words };
            sendBtn.setAttribute('aria-busy', 'true'); sendBtn.setAttribute('aria-disabled', 'true');
            later('send', LIMIT.wait, function () { endSend(); setStatus(DMX.busy); });
        }

        // ---- the panel ----
        function paint() {                                              // the panel again, for a view
            if (!panel) return;
            var thread = view === 'thread', t = threads[cur] || null, name = t ? t.name : '', top = el('div', 'pm-top'), form, tag;
            clear(panel); listBox = logBox = field = sendBtn = status = moreBtn = null;
            panel.setAttribute('data-view', view); panel.setAttribute('aria-label', TEXT.title + (thread ? ': ' + name : ''));
            panel.appendChild(el('div', 'pm-grab'));
            if (!thread) {
                listBox = el('div', 'pm-list');
                put(panel, [put(top, [el('h2', 'pm-title', TEXT.title), iconButton('pm-x', TEXT.close, 'x', closePanel)]), listBox, el('p', 'pm-keep', TEXT.keep)]);
                fillList();
                return;
            }
            tag = put(el('span', 'pm-tag'), [icon('lock'), el('span', '', TEXT.tag)]);
            moreBtn = iconButton('pm-more', TEXT.more, 'more', function () { menuOn = !menuOn; if (!menuOn) disarm(); paintMenu(); });
            moreBtn.setAttribute('aria-haspopup', 'menu'); moreBtn.setAttribute('aria-expanded', 'false');
            put(top, [iconButton('pm-back', TEXT.back, 'back', function () { view = 'list'; cur = ''; menuOn = false; disarm(); paint(); focusFirst(); }), el('span', 'pm-av', initial(name, Chat)),
                put(el('span', 'pm-who'), [el('bdi', 'pm-nm', name), tag]), moreBtn, iconButton('pm-x', TEXT.close, 'x', closePanel)]);
            logBox = el('div', 'pm-log'); logBox.setAttribute('role', 'log'); logBox.setAttribute('aria-live', 'polite'); logBox.setAttribute('aria-label', TEXT.title); logBox.setAttribute('tabindex', '0');
            status = el('p', 'pm-status'); status.setAttribute('role', 'status');
            form = el('form', 'pm-form'); field = el('input', 'pm-input'); field.type = 'text';
            ['maxlength:1000', 'autocomplete:off', 'enterkeyhint:send', 'placeholder:' + TEXT.field, 'aria-label:' + TEXT.toPre + name].forEach(function (a) { field.setAttribute(a.slice(0, a.indexOf(':')), a.slice(a.indexOf(':') + 1)); });
            sendBtn = button('pm-send', undefined, submit); put(sendBtn, [icon('lock'), el('span', '', TEXT.send)]);
            field.addEventListener('keydown', guard(function (e) { if (e && e.key === 'Enter') { if (e.preventDefault) e.preventDefault(); submit(); } }));
            field.addEventListener('input', guard(function () { status.textContent = ''; }));
            form.addEventListener('submit', guard(function (e) { if (e && e.preventDefault) e.preventDefault(); submit(); }));       // (a keyboard that submits the form is a send, never a new page)
            put(form, [field, sendBtn]);
            put(panel, [top, el('p', 'pm-note', TEXT.notePre + name + TEXT.notePost), logBox, status, el('p', 'pm-to', TEXT.toPre + name + TEXT.toPost), form]);
            fillLog(); paintMenu();
        }
        function focusFirst() {
            var q = function (s) { return shade && shade.querySelector ? shade.querySelector(s) : null; }, b = view === 'thread' ? (phone() ? q('.pm-back') : null) || q('.pm-input') : q('.pm-row') || q('.pm-x');
            if (b && b.focus) b.focus();
        }
        function place() {                                              // a wide window: under the bar, down to the chat; a phone's sheet is the style's
            if (!panel) return;
            if (phone()) { panel.style.top = ''; panel.style.height = ''; return; }
            var bar = doc.querySelector ? doc.querySelector('.hbar') : null, top = 76, high = win.innerHeight || 600, floor = high - 12, r;
            try {
                if (bar && bar.getBoundingClientRect) top = Math.round(bar.getBoundingClientRect().bottom) + 6;
                if (dock.root && dock.root.getBoundingClientRect) { r = dock.root.getBoundingClientRect(); if (r.top > top + 200) floor = Math.round(r.top) - 8; }
            } catch (e) { /* the style's place */ }
            panel.style.top = top + 'px'; panel.style.height = Math.max(260, Math.min(high - top - 12, floor - top)) + 'px';
        }
        function openPanel(back) {
            if (shade || !you || !live) return false;
            opener = back || btn || null;
            shade = el('div', 'pm-shade' + (phone() ? ' phone' : '')); panel = el('section', 'pm-panel'); panel.setAttribute('role', 'dialog'); panel.setAttribute('aria-modal', 'true');
            shade.appendChild(panel);
            shade.addEventListener('click', function (e) { if (e.target === shade) closePanel(); });       // (a press on the page outside)
            paint();
            doc.body.appendChild(shade); place(); focusFirst(); paintButton();
            return true;
        }
        function closePanel() {
            if (!shade) return;
            var s = shade, back = opener;
            if (card) closeCard();
            shade = panel = listBox = logBox = field = sendBtn = status = moreBtn = opener = null; menuOn = false; view = 'list'; cur = ''; disarm(); endSend();
            if (s.parentNode && s.parentNode.removeChild) s.parentNode.removeChild(s);
            paintButton();
            if (back && back.focus) back.focus();
        }
        function openThread(h, name) {                                  // the conversation with a person: the hub is asked for it (and reads it); what is known is shown until it comes
            if (!you || !live || !handleOf(h)) return false;
            var t = threads[h], c = conv(h), keys;
            if (!t) {
                t = threads[h] = { h: h, name: clean(Chat, name, LIMIT.name, true) || (c ? c.name : '?'), f: !!(c && c.f), b: !!(c && c.b), rows: [], loaded: false };
                keys = Object.keys(threads); if (keys.length > LIMIT.threads) delete threads[keys[0]];
            }
            cur = h; view = 'thread'; menuOn = false; disarm();
            if (shade) { paint(); focusFirst(); } else openPanel(btn);
            if (c && c.u) { c.u = 0; paintButton(); }
            dock.send({ t: 'dmopen', h: h });
            return true;
        }

        // ---- the report ----
        function closeCard() {
            if (!card) return;
            var s = cardShade, back = cardOpener;
            stop('rep'); card = cardShade = cardOpener = null;
            if (s && s.parentNode && s.parentNode.removeChild) s.parentNode.removeChild(s);
            if (back && back.focus) back.focus();
        }
        function openCard(o) {                                          // o: { h, id, text, name }: the message that is reported, as it stands on the screen
            if (card) return;
            cardOpener = doc.activeElement || null; card = { h: o.h, id: o.id, busy: false };
            var c = el('div', 'pm-card'), title = el('h2', 'pm-ct', TEXT.cardTitle), group = el('div', 'pm-opts'), radios = [], go;
            c.setAttribute('role', 'dialog'); c.setAttribute('aria-modal', 'true'); title.id = 'pm-card-title'; c.setAttribute('aria-labelledby', title.id);
            group.setAttribute('role', 'radiogroup'); group.setAttribute('aria-label', TEXT.cardTitle);
            WHY.forEach(function (why, n) {
                var r = el('input');
                r.setAttribute('type', 'radio'); r.setAttribute('name', 'pm-why'); r.setAttribute('value', why); r.checked = n === 0; radios.push(r);
                group.appendChild(put(el('label', 'pm-opt'), [r, el('span', '', TEXT.why[n])]));
            });
            go = button('btn pm-go', TEXT.sendReport, function () {
                if (card.busy) return;
                var why = 'abuse';
                radios.forEach(function (r) { if (r.checked) why = r.getAttribute('value'); });
                if (!dock.send({ t: 'dmreport', h: card.h, id: card.id, why: why })) { say(TEXT.offline, 'warn'); return; }
                card.busy = true; go.setAttribute('aria-busy', 'true');
                later('rep', LIMIT.wait, function () { closeCard(); say(DMX.busy, 'warn'); });
            });
            put(c, [title, el('p', 'pm-quote', o.text), group, el('p', 'pm-fine', TEXT.finePre + o.name + TEXT.finePost), put(el('div', 'pm-cb'), [button('btn pm-no', TEXT.cancel, closeCard), go])]);
            cardShade = put(el('div', 'pm-card-shade'), [c]); doc.body.appendChild(cardShade);
            radios[0].focus();
        }
        function endAsk() { stop('ask'); asking = null; }
        function report(h) {                                      // the sheet's Report: the conversation is asked for, and the newest message of the person is what the card shows
            if (!you || !live || !handleOf(h)) return false;
            asking = { h: h };
            if (!dock.send({ t: 'dmopen', h: h })) { asking = null; say(TEXT.offline, 'warn'); return false; }
            later('ask', LIMIT.wait, function () { asking = null; say(DMX.busy, 'warn'); });
            return true;
        }

        // ---- what the hub says ----
        function onList(m) {
            var next = listOf(m, Chat), open;
            if (!next) return;
            convs = next;
            convs.forEach(function (c) { if (threads[c.h]) { threads[c.h].b = c.b; threads[c.h].f = c.f; } });
            open = conv(cur);
            if (isOpen(cur) && open && open.u) { open.u = 0; read(cur); }
            if (shade && view === 'list') fillList(); else if (shade && menuOn) paintMenu();
        }
        function onMsg(m) {
            var g = msgOf(m, Chat), old = m && handleOf(m.h) ? conv(m.h) : null, c = g ? convOf({ h: m.h, name: m.name, at: m.at, text: m.text, me: m.me, u: m.u, f: m.f, b: old && old.b ? 1 : 0 }, Chat) : null, t, open;
            if (!c) return;
            open = isOpen(c.h);
            if (c.me || open) c.u = 0;
            convs = [c].concat(convs.filter(function (x) { return x.h !== c.h; })).slice(0, LIMIT.convs);
            t = threads[c.h];
            if (t && t.loaded && !t.rows.some(function (r) { return r.id === g.id; })) { t.rows.push(g); if (t.rows.length > LIMIT.rows) t.rows.shift(); }
            if (!c.me && open) read(c.h);
            if (c.me && sending && sending.h === c.h) { if (field && cur === c.h && Chat.plain(field.value) === sending.text) field.value = ''; endSend(); }       // (only the hub's echo shows a message as sent)
            if (shade && view === 'list') fillList(); else if (shade && cur === c.h) fillLog();
        }
        function onThread(m) {
            var t = threadOf(m, Chat), c, last = null, i;
            if (!t) return;
            threads[t.h] = t; c = conv(t.h);
            if (c) { c.u = 0; c.f = t.f; c.b = t.b; }
            if (shade && view === 'thread' && cur === t.h) { fillLog(); paintMenu(); }
            if (asking && asking.h === t.h) {
                for (i = t.rows.length - 1; i >= 0 && !last; i--) if (!t.rows[i].me) last = t.rows[i];
                endAsk();
                if (last) openCard({ h: t.h, id: last.id, text: last.text, name: t.name }); else say(t.name + TEXT.nothing, 'warn');
            }
        }
        function onDmx(m) {
            if (!m || typeof m.h !== 'string' || typeof m.why !== 'string') return;
            var words = Object.prototype.hasOwnProperty.call(DMX, m.why) ? DMX[m.why] : DMX.busy;
            if (sending && sending.h === m.h) endSend();
            if (card && card.busy && card.h === m.h) { closeCard(); say(words, 'warn'); }
            else if (asking && asking.h === m.h) { endAsk(); say(words, 'warn'); }
            else if (shade && view === 'thread' && cur === m.h) setStatus(words);
            else say(words, 'warn');
        }
        function tap(kind, m) {
            var y;
            if (kind === 'live') {                                      // a new socket: what was known is gone, and the conversation on the screen is asked for again
                var shown = view === 'thread' && threads[cur] ? threads[cur].name : '?', again = !!shade && view === 'thread' && !!cur;
                y = Chat.youOf(m && m.you);
                you = y ? { name: y.name, role: y.role } : null; live = true; convs = []; threads = {}; endSend(); endAsk(); closeCard();
                if (!you) closePanel();
                else if (again) { threads[cur] = { h: cur, name: shown, f: false, b: false, rows: [], loaded: false }; paint(); dock.send({ t: 'dmopen', h: cur }); } else if (shade) paint();
            } else if (kind === 'lost') { live = false; endSend(); endAsk(); closeCard(); }
            else if (kind === 'you') { y = Chat.youOf(m && m.you); if (y && you) you.role = y.role; }
            else if (!you || !live) return;
            else if (kind === 'msgs') onList(m);
            else if (kind === 'msg') onMsg(m);
            else if (kind === 'thread') onThread(m);
            else if (kind === 'dmx') onDmx(m);
            else if (kind === 'dmreported' && m && card && card.h === m.h && card.id === m.id) { closeCard(); say(TEXT.thanks); }
            else if (kind === 'open' && m && typeof m.h === 'string' && HANDLE.test(m.h)) openThread(m.h, typeof m.to === 'string' ? m.to : '');       // (a typed /msg NAME: the hub's answer names the conversation)
            paintButton();
        }

        // ---- the keys, the window ----
        function trap(root, e) {                                        // the focus stays inside the panel (or the card)
            var all = [].slice.call(root.querySelectorAll('button, input')).filter(function (c) { return !c.disabled && !c.hidden; }), i = all.indexOf(doc.activeElement);
            if (!all.length || !((e.shiftKey && i <= 0) || (!e.shiftKey && (i === -1 || i === all.length - 1)))) return;
            if (e.preventDefault) e.preventDefault();
            all[e.shiftKey ? all.length - 1 : 0].focus();
        }
        doc.addEventListener('keydown', guard(function (e) {
            if (!e || (!card && !shade)) return;
            if (e.key === 'Tab') { trap(card ? cardShade : shade, e); return; }
            if (e.key !== 'Escape') return;
            if (e.preventDefault) e.preventDefault();
            if (card) closeCard();                                      // (the card first, then the menu, then the panel)
            else if (menuOn) { menuOn = false; disarm(); paintMenu(); if (moreBtn && moreBtn.focus) moreBtn.focus(); } else closePanel();
        }));
        doc.addEventListener('visibilitychange', guard(function () { var c = conv(cur); if (!doc.hidden && isOpen(cur) && c && c.u) { c.u = 0; read(cur); paintButton(); } }));
        try { win.addEventListener('resize', guard(function () { if (shade) { shade.className = 'pm-shade' + (phone() ? ' phone' : ''); place(); } })); } catch (e) { /* the panel keeps its place */ }

        makeButton();
        dock.tap(guard(tap));
        return {
            open: guard(openThread), openList: guard(function () { if (!you || !live) return false; view = 'list'; cur = ''; if (!shade) return openPanel(btn); paint(); focusFirst(); return true; }), close: guard(function () { closeCard(); closePanel(); }),
            report: guard(report), isOpen: isOpen, count: count, button: function () { return btn; }, state: function () { return !you || !live ? 'off' : !shade ? 'closed' : view; }
        };
    }

    return { LIMIT: LIMIT, TEXT: TEXT, DMX: DMX, convOf: convOf, msgOf: msgOf, listOf: listOf, threadOf: threadOf, countText: countText, initial: initial, mount: mount };
});
