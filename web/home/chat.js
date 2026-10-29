// The global chat dock of Home (docs/HOME_SCREEN.md, "The chat dock"): a panel in the lower-left corner (a strip along the bottom on a phone) that reads and writes the site's chat hub, one WebSocket, JSON, version 1
// (docs/HUB.md: up {"t":"say","text"} and {"t":"ping"}; down welcome, line, del, online, err). Loaded by web/home/home.js after the first paint, with web/home/chat.css, and only by the front page: a plain visit of any
// other page opens no hub socket. ES5, no module system: a page gets AntsHomeChat, node gets module.exports (tests/scripts/web_home_chat_check.js).
//   * Everything the hub sends is text from strangers: it becomes DOM text with createElement and textContent only (never markup, never a link); names and lines are cut for display, the bidirectional and zero-width
//     controls are removed, and each name and line sits in a <bdi>; a frame that is too big, is not JSON or is of a kind that is not known is ignored; at most 100 lines are kept (in memory and on the screen).
//   * The chat never blocks Home, the Room or START: a hub that is not there, slow or garbage is "Chat is offline" and a retry (1 s, doubling to 60 s, plus jitter; a hub that opens and closes again at once does not
//     reset it); every handler is guarded; nothing is kept in the browser (no storage), the state is in memory.
//   * It reads for everybody; the field and Send are there only when the welcome names the visitor (`you`); the login chip's place (#gchat-login-slot) stays empty and hidden until the login, which is a later change.
//   * While the tab is hidden the socket stays and the model follows, but nothing is painted (the screen is made again when the tab is back); a line that arrives while the dock is folded, on the Room or in a hidden tab is unread.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeChat = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var PATH = '/hub/ws', VERSION = 1;
    var LIMIT = { name: 32, line: 200, feed: 100, history: 50, frame: 120000, bad: 30, field: 400, count: 1e9, here: 500, said: 2 };       // (the hub's own: names 32, lines 200 code points, history 50; the rest is this page's)
    var MS = { ping: 25000, answer: 30000, connect: 15000, stable: 30000, notice: 6000, regain: 1000, flood: 3000 };
    var BACKOFF = { min: 1000, max: 60000, low: 0.5, high: 1.5 };      // the wait is spread from half to one and a half times the step (never under a second or over a minute): many pages that lost one hub do not come back together
    var STRIP = 62, BELOW = 12;                                        // px: the strip's height, and the room under the dock (chat.css)
    var PACE = { burst: 2, every: 2500, same: 31000 };                  // this page's own limit, below the hub's (3 at once, one every 2 s, the same text twice within 30 s)
    var TEXT = {
        title: 'Global chat', short: 'Chat', welcome: 'Welcome to the front page. Everyone here can read what you write.', online: ' here now', connecting: 'connecting…', reconnecting: 'reconnecting…',
        offline: 'Chat is offline', readonly: 'Chat is read-only for now', guest: 'You can read as a guest. Playing and watching stay anonymous.', field: 'Say something to everyone', send: 'Send',
        fold: 'Fold chat', open: 'Open chat', mod: 'mod', log: 'Chat messages', unread: ' unread', generic: 'That did not go through', more: ' more messages', one: 'One more message', many: '500+'
    };
    var ERR = {                                                          // the words of the hub's err codes (docs/HUB.md: "the page owns the words") and of this page's own refusals
        rate: 'Slow down a little', login: 'Log in with Discord to chat', muted: 'You cannot chat for a while', banned: 'You cannot chat here', long: 'That line is too long', empty: 'Write something first',
        young: 'Your account is too new to chat yet', bad: 'That line could not be sent', markup: 'Lines cannot hold < or >', offline: TEXT.offline, readonly: TEXT.readonly
    };

    // ---- the pure parts -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // The invisible characters that the hub removes (docs/HUB.md "Text rules": the format characters Cf, the fillers that draw as blanks); tests/scripts/test_web_home_chat.py keeps this list in step with hub/text.py
    var HIDDEN = /[\u00ad\u034f\u0600-\u0605\u061c\u06dd\u070f\u0890\u0891\u08e2\u115f\u1160\u17b4\u17b5\u180e\u200b-\u200f\u202a-\u202e\u2060-\u206f\u2800\u3164\ufeff\uffa0\ufff9-\ufffc]|\ud804[\udcbd\udccd]|\ud80d[\udc30-\udc3f]|\ud82f[\udca0-\udca3]|\ud834[\udd59\udd73-\udd7a]|\udb40[\udc00-\udc7f]/g;
    var SELECTORS = /[\u180b-\u180d\u180f\ufe00-\ufe0f]|\udb40[\udd00-\uddef]/g;       // (variation selectors: a name is made of none, a line keeps them: they make an emoji of a heart)
    function pairs(s) {                                                  // only the surrogates that make a pair stay (a run of lone ones leaves nothing behind)
        var out = '';
        for (var i = 0; i < s.length; i++) {
            var c = s.charCodeAt(i), d = s.charCodeAt(i + 1);
            if (c >= 0xd800 && c <= 0xdbff) { if (d >= 0xdc00 && d <= 0xdfff) { out += s.charAt(i) + s.charAt(i + 1); i++; } }
            else if (!(c >= 0xdc00 && c <= 0xdfff)) out += s.charAt(i);
        }
        return out;
    }
    // text from the hub for display: line ends and tabs are blanks, the other controls and the invisible characters go, blanks become one space, lone surrogates go; strict (a name) takes the variation selectors out too
    function plain(s, strict) {
        s = pairs(String(s).replace(/[\t-\r\u0085\u2028\u2029]/g, ' ')).replace(/[\u0000-\u0008\u000e-\u001f\u007f-\u009f]/g, '').replace(HIDDEN, '');
        return (strict ? s.replace(SELECTORS, '') : s).replace(/\s+/g, ' ').replace(/^ | $/g, '');
    }
    function shows(s) { return /[^\s\u0300-\u036f\u1ab0-\u1aff\u1dc0-\u1dff\u20d0-\u20ff\ufe00-\ufe0f\ufe20-\ufe2f]/.test(s); }       // (marks alone show nothing)
    function points(s) { var n = 0; for (var i = 0; i < s.length; i++) { var c = s.charCodeAt(i); if (!(c >= 0xdc00 && c <= 0xdfff)) n++; } return n; }
    function clip(s, max) {                                              // at most `max` code points, then "…" (a pair of surrogates is never cut)
        var n = 0, i = 0;
        for (; i < s.length; n++) {
            if (n === max) return s.slice(0, i) + '…';
            var c = s.charCodeAt(i);
            i += c >= 0xd800 && c <= 0xdbff ? 2 : 1;
        }
        return s;
    }
    function parse(data) {                                               // a frame of the hub: an object with a "t", else null
        if (typeof data !== 'string' || data.length > LIMIT.frame) return null;
        var m;
        try { m = JSON.parse(data); } catch (e) { return null; }
        return m && typeof m === 'object' && typeof m.t === 'string' ? m : null;
    }
    function count(n) { return typeof n === 'number' && n >= 0 && n <= LIMIT.count && n === Math.floor(n) ? n : null; }
    function lineOf(m) {                                                 // one line of the hub (history or live), cleaned: { id, who, text, mod }, or null when it is not a line
        if (!m || typeof m !== 'object' || typeof m.who !== 'string' || typeof m.text !== 'string' || count(m.id) === null) return null;
        var text = clip(plain(m.text.slice(0, LIMIT.line * 4)), LIMIT.line);
        return shows(text) ? { id: m.id, who: clip(plain(m.who.slice(0, LIMIT.name * 4), true), LIMIT.name) || '?', text: text, mod: m.mod === true } : null;
    }
    function wordOf(code, login) {                                       // the words of an err code; "log in" is asked only where the hub has a login
        if (code === 'login' && !login) return ERR.readonly;
        return typeof code === 'string' && Object.prototype.hasOwnProperty.call(ERR, code) ? ERR[code] : TEXT.generic;
    }
    function delayOf(attempt, random) {                                  // the wait before retry number `attempt` (0 is the first): the step is 1 s, doubling to 60 s; the wait is anywhere from half to one and a half steps, within 1 to 60 s
        var base = Math.min(BACKOFF.max, BACKOFF.min * Math.pow(2, Math.min(attempt, 12)));
        var low = Math.max(BACKOFF.min, base * BACKOFF.low), high = Math.min(BACKOFF.max, base * BACKOFF.high);
        return Math.round(low + (high - low) * (random >= 0 && random <= 1 ? random : 0.5));
    }
    function addressOf(loc) { return (loc.protocol === 'https:' ? 'wss://' : 'ws://') + loc.host + PATH; }

    // ---- the dock ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // mount({ doc, win, host (Home's div), now, random, WebSocket, error }) returns { root, slot, field, shown(isHome), open(on), say(text), state() }
    function mount(env) {
        var doc = env.doc, win = env.win, host = env.host;
        var now = env.now || function () { return Date.now(); }, rnd = env.random || Math.random, Socket = env.WebSocket || win.WebSocket;
        var feed = [], you = null, login = false, people = null, state = 'connecting', welcomed = false, ever = false, lastId = -1, bad = 0, attempt = 0, liveSince = null, lastTry = 0;
        var sock = null, stopped = false, isHome = false, stale = true, notice = '', draft = '', stickTimer = 0, stuck = true, timers = { retry: 0, connect: 0, ping: 0, answer: 0, notice: 0, quiet: 0, flood: 0 };
        var tokens = PACE.burst, tokensAt = now(), said = Object.create(null), heard = 0, skipped = 0, muted = { repaint: false, flood: false };       // (said: the texts that were sent in the last 31 s, as the hub remembers them)
        var open = (win.innerWidth || 0) >= 721 && (win.innerHeight || 0) >= 501;       // a desktop opens with the panel out, a phone or a short window with the strip (home.css has the same breakpoints)
        var tight = 0;                                                           // what Home leaves under its bar: 0 the room for the panel, 1 too little for the open panel (the strip shows), 2 too little for the strip (nothing shows)

        function el(tag, cls, text) {
            var e = doc.createElement(tag);
            if (cls) e.className = cls;
            if (text !== undefined) e.textContent = text;
            return e;
        }
        function put(parent, kids) { kids.forEach(function (k) { parent.appendChild(k); }); return parent; }
        function guard(fn) { return function () { try { return fn.apply(this, arguments); } catch (e) { if (env.error) env.error(e); return undefined; } }; }       // (nothing that goes wrong in here reaches the page)
        function stop(name) { if (timers[name]) { clearTimeout(timers[name]); timers[name] = 0; } }

        // The markup: a header (the title, the unread count, the last line for the strip, who is here, the fold button) and a body (the log, a note, the form, the login chip's place)
        var box = el('section', 'gc'); box.id = 'gchat'; box.setAttribute('aria-labelledby', 'gchat-title');
        var head = el('div', 'gc-head'), title = el('h2', 'gc-title'); title.id = 'gchat-title';
        var shortTitle = el('span', 'gc-short', TEXT.short); shortTitle.setAttribute('aria-hidden', 'true');
        put(title, [el('span', 'gc-long', TEXT.title), shortTitle]);
        var pill = el('span', 'gc-unread'); pill.setAttribute('aria-hidden', 'true'); pill.hidden = true;
        var peek = el('span', 'gc-peek'), peekWho = el('bdi', 'gc-pw'), peekText = el('bdi', 'gc-pt'), here = el('span', 'gc-online'), hereText = el('span', 'gc-on');
        put(peek, [peekWho, peekText]);
        put(here, [el('i', 'gc-dot'), hereText]);
        var fold = el('button', 'btn gc-fold'); fold.type = 'button'; fold.setAttribute('aria-controls', 'gchat-body'); fold.appendChild(el('i', 'gc-mark'));
        put(head, [title, pill, peek, here, fold]);
        var body = el('div', 'gc-body'); body.id = 'gchat-body';
        var log = el('div', 'gc-log'); log.setAttribute('role', 'log'); log.setAttribute('aria-live', 'polite'); log.setAttribute('aria-label', TEXT.log); log.setAttribute('tabindex', '0');
        log.appendChild(el('p', 'gc-sys', TEXT.welcome));
        var note = el('p', 'gc-note'); note.setAttribute('role', 'status'); note.setAttribute('aria-live', 'polite');
        var news = el('p', 'gc-news'); news.setAttribute('role', 'status'); news.setAttribute('aria-live', 'polite');       // (what a screen reader is told when lines come faster than it can read them: one summary)
        var form = el('form', 'gc-form'), field = el('input', 'gc-input'), send = el('button', 'btn gc-send', TEXT.send);
        field.type = 'text'; field.setAttribute('aria-label', TEXT.field); field.setAttribute('placeholder', TEXT.field); field.setAttribute('autocomplete', 'off'); field.setAttribute('enterkeyhint', 'send');
        field.setAttribute('maxlength', String(LIMIT.field)); send.type = 'submit';
        put(form, [field, send]); form.hidden = true;
        var guest = el('p', 'gc-guest', TEXT.guest); guest.hidden = true;
        var slot = el('div', 'gc-slot'); slot.id = 'gchat-login-slot'; slot.setAttribute('data-slot', 'login'); slot.hidden = true;      // the login chip's place: the login (a later change) fills it and shows it
        put(body, [log, note, news, form, guest, slot]);
        put(box, [head, body]);
        host.appendChild(box);

        // ---- what is seen -----------------------------------------------------------------------------------------------------------------------------------------------------------------------
        function visible() { return !doc.hidden; }
        function fit() {                                                         // the panel stays under Home's bar: notices above Home (or a small phone) take the room, and then the strip shows instead of the panel, or nothing when not even the strip fits
            var bar = host.querySelector ? host.querySelector('.hbar') : null, least = 0, room, was = tight;
            try { least = parseFloat(win.getComputedStyle(box).getPropertyValue('--gc-min')) || 0; } catch (e) { least = 0; }       // (the least height of the open panel is the style's: chat.css)
            if (least && typeof host.clientHeight === 'number' && bar && typeof bar.offsetHeight === 'number') {
                room = host.clientHeight - bar.offsetHeight;
                tight = room >= least + BELOW ? 0 : room >= STRIP + BELOW ? 1 : 2;
            } else tight = 0;
            return tight !== was;
        }
        function seen() { return isHome && open && !tight && visible(); }                 // the log is on the screen and somebody can read it
        function unread() { var n = 0; for (var i = 0; i < feed.length; i++) if (feed[i].fresh) n++; return n; }
        function row(e) {
            var r = el('div', 'gc-line' + (e.mod ? ' gc-mod' : '') + (e.own ? ' gc-me' : ''));
            if (e.mod) r.appendChild(el('span', 'gc-badge', TEXT.mod + ' '));
            put(r, [el('bdi', 'gc-who', e.who), el('span', 'gc-sep', ': '), el('bdi', 'gc-text', e.text)]);
            return r;
        }
        function atBottom() { var h = log.scrollHeight; return typeof h !== 'number' || h - log.scrollTop - log.clientHeight < 48; }
        function settle(force) {                                                 // one scroll to the newest line for a whole burst, and only when the reader was at the bottom (a log that is made again is read from the bottom)
            if (!stickTimer) {
                stuck = atBottom();
                stickTimer = setTimeout(guard(function () { stickTimer = 0; if (stuck) log.scrollTop = log.scrollHeight; }), 0);
            }
            if (force) stuck = true;
        }
        function live() { log.setAttribute('aria-live', muted.repaint || muted.flood ? 'off' : 'polite'); }
        function flow() {                                                        // lines that are read out one by one, two in 3 s: the rest of a flood (the hub lets 5 a second through) is one line, "9 more messages"
            if (!timers.flood) {
                heard = 0; skipped = 0; news.textContent = '';                  // (the summary of the window before goes: the same words twice in a row are not told twice)
                timers.flood = setTimeout(guard(function () {
                    timers.flood = 0;
                    if (skipped) news.textContent = skipped === 1 ? TEXT.one : skipped + TEXT.more;
                    muted.flood = false; live();
                }), MS.flood);
            }
            if (++heard > LIMIT.said) { skipped++; if (!muted.flood) { muted.flood = true; live(); } }
        }
        function repaint() {                                                     // the log is made again from the model (a welcome, the tab or the dock coming back); a screen reader is not told all of it again
            muted.repaint = true; live(); stop('quiet');
            while (log.children.length > 1) log.removeChild(log.children[log.children.length - 1]);
            feed.forEach(function (e) { e.el = row(e); log.appendChild(e.el); });
            stale = false;
            settle(true);
            timers.quiet = setTimeout(guard(function () { timers.quiet = 0; muted.repaint = false; live(); }), 300);
        }
        function paint() {                                                       // everything that is not a line, from the model
            if (seen()) feed.forEach(function (e) { e.fresh = false; });
            var n = unread(), last = feed.length ? feed[feed.length - 1] : null, dead = typeof Socket !== 'function';
            var out = open && !tight;                                                // (the panel that shows: a window with no room for it shows the strip, and the visitor's wish stays for when the room is back)
            box.setAttribute('data-state', state); box.setAttribute('data-open', out ? 'true' : 'false'); box.setAttribute('data-you', you ? 'yes' : 'no'); box.hidden = tight === 2;
            body.hidden = !out;
            fold.setAttribute('aria-expanded', out ? 'true' : 'false');
            fold.setAttribute('aria-label', out ? TEXT.fold : TEXT.open + (n ? ', ' + n + TEXT.unread : ''));
            pill.textContent = n > 99 ? '99+' : String(n); pill.hidden = !n;
            peekWho.textContent = state !== 'offline' && last ? last.who + ': ' : '';
            peekText.textContent = state === 'offline' ? TEXT.offline : last ? last.text : state === 'connecting' ? TEXT.connecting : you ? '' : ERR.readonly;
            hereText.textContent = state === 'live' ? (people !== null ? (people > LIMIT.here ? TEXT.many : people) + TEXT.online : '') : state === 'connecting' ? TEXT.connecting : TEXT.reconnecting;
            here.hidden = dead || !hereText.textContent;
            form.hidden = !(state === 'live' && you);
            guest.hidden = !(state === 'live' && !you);
            note.textContent = notice || (state === 'offline' ? TEXT.offline : state === 'live' && !you ? (login ? ERR.login : ERR.readonly) : '');
            if (stale && seen()) repaint();
        }
        function render() { if (visible()) paint(); else stale = true; }       // (a hidden tab is not painted; the model goes on, and the tab's return paints it all)
        function tell(code) {                                                    // a word for the note, for a few seconds
            notice = wordOf(code, login);
            stop('notice');
            timers.notice = setTimeout(guard(function () { timers.notice = 0; notice = ''; render(); }), MS.notice);
            render();
        }

        // ---- the link -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        function lost() {                                                        // the socket is gone (closed, failed, silent, or it sent what no hub sends): say so, and try again later
            var s = sock;
            sock = null;
            ['connect', 'ping', 'answer', 'retry', 'notice'].forEach(stop); notice = '';
            if (s) { try { s.onopen = s.onmessage = s.onerror = s.onclose = null; s.close(); } catch (e) { /* it is gone */ } }
            if (liveSince !== null && now() - liveSince >= MS.stable) attempt = 0;       // (a link that held for a while starts over at 1 s; one that is closed at once keeps growing)
            liveSince = null; welcomed = false; state = 'offline';
            if (!stopped) { timers.retry = setTimeout(guard(go), delayOf(attempt, rnd())); attempt++; }
            render();
        }
        function ping() {
            timers.ping = setTimeout(guard(function () {
                timers.ping = 0;
                if (!sock || sock.readyState !== 1) return;
                try { sock.send('{"t":"ping"}'); } catch (e) { lost(); return; }
                timers.answer = setTimeout(guard(lost), MS.answer);             // (the answer to a ping is the online count: a hub that says nothing for this long is gone)
                ping();
            }), MS.ping);
        }
        function go() {
            timers.retry = 0;
            if (sock || stopped) return;
            if (typeof Socket !== 'function') { state = 'offline'; render(); return; }       // (a browser without WebSocket: offline for good)
            lastTry = now(); bad = 0; welcomed = false;
            if (!ever) state = 'connecting';
            var s;
            try { s = new Socket(addressOf(win.location)); } catch (e) { lost(); return; }
            sock = s;
            s.onmessage = guard(function (ev) { if (s === sock) take(ev && ev.data); });
            s.onerror = s.onclose = guard(function () { if (s === sock) lost(); });
            timers.connect = setTimeout(guard(function () { timers.connect = 0; if (s === sock && !welcomed) lost(); }), MS.connect);
            render();
        }
        function nudge() {                                                       // the tab or the network is back: do not wait for the timer, unless the last try was a moment ago
            if (state === 'offline' && !sock && !stopped && now() - lastTry >= MS.regain) { stop('retry'); go(); }
        }

        // ---- what the hub says --------------------------------------------------------------------------------------------------------------------------------------------------------------------
        function known(id) { for (var i = 0; i < feed.length; i++) if (feed[i].id === id) return true; return false; }
        function push(e) {
            feed.push(e);
            if (e.id > lastId) lastId = e.id;
            while (feed.length > LIMIT.feed) { var old = feed.shift(); if (seen() && !stale && old.el && old.el.parentNode) old.el.parentNode.removeChild(old.el); else stale = true; }       // (a screen that is not painted is made again when it comes back)
        }
        function onWelcome(m) {
            if (welcomed) return;                                                // (one welcome for each socket)
            if (m.v !== VERSION || (m.history !== undefined && !Array.isArray(m.history))) { lost(); return; }       // (a hub that speaks another version is no hub for this page)
            var name = m.you && typeof m.you === 'object' && typeof m.you.name === 'string' ? clip(plain(m.you.name.slice(0, LIMIT.name * 4), true), LIMIT.name) : '';
            you = name ? { name: name, mod: m.you.mod === true } : null;
            login = m.login === true; people = count(m.online);
            var before = lastId;
            feed.length = 0;
            (m.history || []).slice(-LIMIT.history).forEach(function (raw) {
                var e = lineOf(raw);
                if (!e || known(e.id)) return;
                e.own = !!you && e.who === you.name;
                e.fresh = ever && e.id > before && !e.own && !seen();             // (the first welcome is the past; after a lost link, the lines that came meanwhile are news)
                push(e);
            });
            stop('connect'); stop('notice'); notice = ''; stale = true; welcomed = ever = true; state = 'live'; liveSince = now();
            ping();
            render();
        }
        function onLine(m) {
            var e = lineOf(m);
            if (!e || known(e.id)) return;
            e.own = !!you && e.who === you.name; e.fresh = !e.own && !seen();
            if (e.own && e.text === draft) draft = '';
            push(e);
            if (seen() && !stale) { settle(); flow(); e.el = row(e); log.appendChild(e.el); } else stale = true;
            render();
        }
        function onDel(m) {
            for (var i = 0; i < feed.length; i++) {
                if (feed[i].id !== m.id) continue;
                var gone = feed.splice(i, 1)[0];
                if (seen() && !stale && gone.el && gone.el.parentNode) gone.el.parentNode.removeChild(gone.el); else stale = true;
                break;
            }
            render();
        }
        function take(data) {
            var m = parse(data);
            if (!m) { if (++bad >= LIMIT.bad) lost(); return; }                  // (a hub that sends nothing but noise is dropped; a frame of noise is ignored)
            if (!welcomed && m.t !== 'welcome') return;
            stop('answer');
            if (m.t === 'welcome') onWelcome(m);
            else if (m.t === 'line') onLine(m);
            else if (m.t === 'del') onDel(m);
            else if (m.t === 'online') { people = count(m.n); render(); }
            else if (m.t === 'err') { tell(m.code); if (draft && !field.value) field.value = draft; delete said[draft]; draft = ''; }       // (a refused line is not one that the hub remembers: it may be sent again)
        }                                                                        // (any other kind is ignored)

        // ---- what the visitor does ------------------------------------------------------------------------------------------------------------------------------------------------------------
        function allowed(text) {                                                // this page's own pace, below the hub's; a text that was sent in the last 31 s is not sent again (the hub remembers every one of them, not only the last)
            var t = now(), old;
            tokens = Math.min(PACE.burst, tokens + Math.max(0, t - tokensAt) / PACE.every); tokensAt = t;
            for (old in said) if (t - said[old] >= PACE.same) delete said[old];
            if (tokens < 1 || said[text] !== undefined) return false;
            tokens -= 1; said[text] = t;
            return true;
        }
        function sendLine(raw) {                                                // '' when the line went out, else the code that says why not; never while the link is down
            var text = plain(String(raw));
            var why = !sock || sock.readyState !== 1 || state !== 'live' ? 'offline' : !you ? 'readonly' : !text ? 'empty' : points(text) > LIMIT.line ? 'long' : /[<>]/.test(text) ? 'markup' : !allowed(text) ? 'rate' : '';
            if (why) { tell(why); return why; }
            try { sock.send(JSON.stringify({ t: 'say', text: text })); } catch (e) { lost(); return 'offline'; }
            draft = text;
            return '';
        }
        function setOpen(on) { open = !!on; if (seen()) stale = true; render(); }
        form.addEventListener('submit', guard(function (e) {
            if (e && e.preventDefault) e.preventDefault();
            if (sendLine(field.value) === '') field.value = '';
        }));
        fold.addEventListener('click', guard(function () { setOpen(tight ? true : !open); if (fold.focus) fold.focus(); }));       // (a window with no room for the panel keeps the wish to open it, and shows the strip)
        head.addEventListener('click', guard(function (e) { if (!open && !(e && e.target && fold.contains && fold.contains(e.target))) setOpen(true); }));       // (a folded strip opens wherever it is pressed)
        box.addEventListener('keydown', guard(function (e) { if (e && e.key === 'Escape' && open) { setOpen(false); if (fold.focus) fold.focus(); } }));
        doc.addEventListener('visibilitychange', guard(function () { if (visible()) { stale = true; render(); nudge(); } }));
        win.addEventListener('online', guard(nudge));
        win.addEventListener('pagehide', guard(function () { stopped = true; lost(); }));       // (a page that goes away, or into the back-forward cache, lets the hub go)
        win.addEventListener('pageshow', guard(function (e) { if (e && e.persisted && stopped) { stopped = false; attempt = 0; go(); } }));

        paint();
        go();
        win.addEventListener('resize', guard(function () { if (fit()) render(); }));
        try { if (typeof win.ResizeObserver === 'function') new win.ResizeObserver(guard(function () { if (fit()) render(); })).observe(host); } catch (e) { /* the window's own resize is enough */ }       // (a notice that comes or goes above Home changes Home's height, not the window's)
        return {
            root: box, slot: slot, field: field, say: sendLine, state: function () { return state; },
            shown: guard(function (on) { isHome = !!on; fit(); if (seen()) stale = true; render(); }), open: guard(setOpen)
        };
    }

    return { PATH: PATH, LIMIT: LIMIT, MS: MS, PACE: PACE, TEXT: TEXT, ERR: ERR, plain: plain, points: points, clip: clip, parse: parse, count: count, lineOf: lineOf, wordOf: wordOf, delayOf: delayOf, addressOf: addressOf, mount: mount };
});
