// The global chat dock of Home (docs/HOME_SCREEN.md, "The chat dock"): a panel along the bottom of Home (the Chat view on a phone) that reads and writes the site's chat hub, one WebSocket, JSON, version 1
// (docs/HUB.md: up {"t":"say","text"} and {"t":"ping"}; down welcome, line, del, online, err, and for a moderator you, ok, bans, bye). Loaded by web/home/home.js after the first paint, with web/home/chat.css, and only by the front page: a plain visit of any
// other page opens no hub socket. ES5, no module system: a page gets AntsHomeChat, node gets module.exports (tests/scripts/web_home_chat_check.js).
//   * Everything the hub sends is text from strangers: it becomes DOM text with createElement and textContent only (never markup, never a link); names and lines are cut for display, the bidirectional and zero-width
//     controls are removed, and each name and line sits in a <bdi>; a frame that is too big, is not JSON or is of a kind that is not known is ignored; at most 100 lines are kept (in memory and on the screen).
//   * The chat never blocks Home, the Room or START: a hub that is not there, slow or garbage is "Chat is offline" and a retry (1 s, doubling to 60 s, plus jitter; a hub that opens and closes again at once does not
//     reset it); every handler is guarded; nothing is kept in the browser (no storage), the state is in memory.
//   * It reads for everybody; the field and Send are there only when the welcome names the visitor (`you`). The login (docs/HUB.md, "Login") is the hub's: where the welcome says login:true and names nobody, the field is off and a
//     Log in button takes its place, here and in the bar's chip (`env.chip`), and it is a full navigation to /hub/login (Discord, then back to this screen); where it names the visitor, the chip is a button that opens #gchat-login-slot (a dropdown under the chip): "Chatting as <name>",
//     the choice of the name that the chat shows, Log out and Forget me, which are POSTs of this page to the hub (same origin, with the cookie, which this script never sees: it is HttpOnly). Nothing of the login is read, kept or sent by this script.
//   * Roles (docs/HUB.md, "Roles", "Moderation"): `you.role` is none, mod or admin and a `you` frame changes it live. Only the flags `mod`, `role` and `sys` of the hub's frames decide a mark or the hub's own line, never
//     a person's text. /help is this page's own (never sent) and lists the commands of the role; each reply of the hub (`ok`, `bans`, the codes power, role, who, rate, bad) is told in plain words as a private line of this
//     screen. A `bye` is a plain notice, the field off where the hub's state needs it, and the usual reconnect with its backoff (so the person reads on).
//   * While the tab is hidden the socket stays and the model follows, but nothing is painted (the screen is made again when the tab is back); a line that arrives while the dock is folded, on the Room or in a hidden tab is unread.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeChat = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var PATH = '/hub/ws', VERSION = 1;
    var LOGIN = '/hub/login?next=', LOGOUT = '/hub/logout', FORGET = '/hub/forget';      // the hub's pages (nginx passes them; the cookie goes along with a request of this page only)
    var LIMIT = { name: 32, line: 200, feed: 100, history: 50, frame: 120000, bad: 30, field: 400, count: 1e9, here: 500, said: 2 };       // (the hub's own: names 32, lines 200 code points, history 50; the rest is this page's)
    var MS = { ping: 25000, answer: 30000, connect: 15000, stable: 30000, notice: 6000, regain: 1000, flood: 3000, pending: 8000 };       // (pending: the hub answers a line at once: one that has no answer after this long is not waited for)
    var BACKOFF = { min: 1000, max: 60000, low: 0.5, high: 1.5 };      // the wait is spread from half to one and a half times the step (never under a second or over a minute): many pages that lost one hub do not come back together
    var PACE = { burst: 2, every: 2500, same: 31000 };                  // this page's own limit, below the hub's (3 at once, one every 2 s, the same text twice within 30 s)
    var TEXT = {
        title: 'Global chat', welcome: 'Welcome to the front page. Everyone here can read what you write.', online: ' here now', connecting: 'connecting…', reconnecting: 'reconnecting…',
        offline: 'Chat is offline', readonly: 'Chat is read-only for now', guest: 'You can read as a guest. Playing and watching stay anonymous.', field: 'Say something to everyone', send: 'Send',
        fold: 'Fold chat', open: 'Open chat', log: 'Chat messages', unread: ' unread', generic: 'That did not go through', more: ' more messages', one: 'One more message', many: '500+',
        login: 'Log in', loginWith: 'Log in with Discord', chatting: 'Chatting as ', logout: 'Log out', forget: 'Forget me', sure: 'Are you sure? Press again', forgetNote: 'Deletes your lines in the chat and logs you out everywhere. A ban or a mute stays.',
        shows: 'Chat shows your ', names: ['display name', 'username'], use: 'Show my ',
        administrator: 'administrator', moderator: 'moderator', only: 'Only you', youAre: { mod: 'You are a moderator', admin: 'You are an administrator' }, number: 'line number',
        helpHead: 'Commands you can use. Only you can see this.', helpNone: 'There are no chat commands for you.', cmdBad: 'That command is not right. Type /help to see the commands.', cmdRate: 'You are going too fast, try again in a moment', bansHead: 'People who are banned', bansNone: 'Nobody is banned.', noName: '(no name)',
        kicked: 'A moderator removed you from the chat. You can log in again.', banned: 'You are banned from the chat. You can still read it.', banField: 'You are banned from the chat'
    };
    var RANK = { none: 0, mod: 1, admin: 2 };                            // (the hub's own table: docs/HUB.md, "Moderation")
    var COMMANDS = [                                                     // [the word, the rank that may use it, how it is typed, what it does]: what /help lists; the hub decides what is done
        ['/mute', 1, '/mute name minutes', 'stop a person writing for 1 to 10080 minutes'], ['/unmute', 1, '/unmute name', 'allow a muted person to write again'],
        ['/del', 1, '/del number', 'take one line off the screen of everybody (use the number beside the line)'], ['/kick', 1, '/kick name', 'remove a person from the chat (they can log in again)'],
        ['/ban', 2, '/ban name', 'stop a person writing until the ban is lifted (they can still read)'], ['/unban', 2, '/unban name', 'lift a ban'], ['/bans', 2, '/bans', 'list the people who are banned']
    ];
    var DONE = { mute: 'Done: the person is muted.', unmute: 'Done: the person can write again.', del: 'Done: the line is gone.', kick: 'Done: the person was removed.', ban: 'Done: the person is banned.', unban: 'Done: the ban is lifted.' };
    var MAXBANS = 50;                                                    // (the hub sends the newest 50)
    var ABOUT = ['power', 'role', 'who'];                                // the codes that are only for a command (a line is never refused with them)
    var SAID = ['rate', 'login', 'muted', 'banned', 'long', 'empty', 'young', 'quota', 'bad'];       // the codes that the hub answers a line with: an err of any other code (a refused `here` has its own) is not about a line
    var ERR = {                                                          // the words of the hub's err codes (docs/HUB.md: "the page owns the words") and of this page's own refusals
        rate: 'Slow down a little', login: 'Log in with Discord to chat', muted: 'You cannot chat for a while', banned: 'You cannot chat here', long: 'That line is too long', empty: 'Write something first',
        young: 'Your account is too new to chat yet', quota: 'You have chatted a lot, try again later', bad: 'That line could not be sent', markup: 'Lines cannot hold < or >', power: 'You are not allowed to do that', role: 'Could not confirm your role, try again in a moment', who: 'Nobody by that name is here', offline: TEXT.offline, readonly: TEXT.readonly, failed: 'That did not work, try again'
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
    function nfc(s) { try { return s.normalize ? s.normalize('NFC') : s; } catch (e) { return s; } }
    function within(a, b) { for (var i = 0, j = 0; i < a.length; i++, j++) { j = b.indexOf(a.charAt(i), j); if (j < 0) return false; } return true; }       // (a is b with characters taken out)
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
    function nameOf(s) { return typeof s === 'string' ? clip(plain(s.slice(0, LIMIT.name * 4), true), LIMIT.name) : ''; }       // (a name of the hub, or its key of an account)
    function roleOf(role, mod) {                                         // the role that the hub's flags say: `admin` or `mod` only where the hub says moderator (`mod`: true for both roles, which is all that an older hub says), else `none`; nothing else is a role
        return mod !== true ? 'none' : role === 'admin' ? 'admin' : 'mod';
    }
    function youOf(y) {                                                  // the visitor, as the hub's `you` says (a welcome, or the frame that changes it), or null when it names nobody: { name, mod, role, k, pick, other }
        if (!y || typeof y !== 'object') return null;
        var name = nameOf(y.name), role = y.role === 'admin' || y.role === 'mod' || y.role === 'none' ? y.role : y.mod === true ? 'mod' : 'none';       // (`role` where the hub says it; an older hub says `mod` only)
        return name ? { name: name, mod: role !== 'none', role: role, k: nameOf(y.k), pick: y.pick, other: y.pick === 'display' || y.pick === 'username' ? nameOf(y.other) : '' } : null;
    }
    function lineOf(m) {                                                 // one line of the hub (history or live), cleaned: { id, who, text, mod, role, sys (a line of the hub's own: only its flag says so), k (its author's key), u (its author's username: only a moderator's copy has it; drawn for a moderator only) }, or null when it is not a line
        if (!m || typeof m !== 'object' || typeof m.who !== 'string' || typeof m.text !== 'string' || count(m.id) === null) return null;
        var text = clip(plain(m.text.slice(0, LIMIT.line * 4)), LIMIT.line), sys = m.sys === true;
        return shows(text) ? { id: m.id, who: nameOf(m.who) || '?', text: text, mod: !sys && m.mod === true, role: sys ? 'none' : roleOf(m.role, m.mod), sys: sys, k: nameOf(m.k), u: nameOf(m.u) } : null;
    }
    function wordOfRole(role) { return role === 'admin' ? TEXT.administrator : TEXT.moderator; }
    function roleClass(role) { return role === 'admin' ? ' gc-r-admin' : role === 'mod' ? ' gc-r-mod' : ''; }       // (the name in red or yellow: chat.css)
    function wordOfCommand(text) {                                       // the command word that a text starts with, in lower case ('/mute'), or '': the hub reads them in any case
        var m = /^(\/[a-z]+)(?: |$)/i.exec(text);
        return m ? m[1].toLowerCase() : '';
    }
    function helpOf(role) {                                              // what /help says to a role: [the lines], each a text or [how it is typed, what it does]
        var rank = RANK[role] || 0, out = [];
        COMMANDS.forEach(function (c) { if (c[1] <= rank) out.push([c[2], c[3]]); });
        return out.length ? [TEXT.helpHead].concat(out) : [TEXT.helpNone];
    }
    function dayOf(at) {                                                 // the day of a time in seconds since 1970 (UTC, as 2026-11-12), or ''
        if (typeof at !== 'number' || at !== Math.floor(at) || at < 0 || at > 4e9) return '';
        return new Date(at * 1000).toISOString().slice(0, 10);
    }
    function bansOf(m) {                                                // the hub's private list of bans { rows: [{ u, at }] }, as the lines to show: [the heading, a line for each person], or null when it is not that
        if (!m || !Array.isArray(m.rows) || m.rows.length > MAXBANS) return null;
        var out = [m.rows.length ? TEXT.bansHead : TEXT.bansNone];
        m.rows.forEach(function (r) { if (r && typeof r === 'object') out.push([nameOf(r.u) || TEXT.noName, dayOf(r.at)]); });
        return out;
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
    function initial(name) {                                             // the first code point of a name, for the circle in the bar
        var first = clip(name, 1);
        return first.length > 1 && first.charAt(first.length - 1) === '…' ? first.slice(0, -1) : first;
    }
    function addressOf(loc) { return (loc.protocol === 'https:' ? 'wss://' : 'ws://') + loc.host + PATH; }

    // ---- the dock ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // mount({ doc, win, host (Home's div), chip (the bar's login button), screen() ('home' or 'room'), go(url), fetch, now, random, WebSocket, error }) returns { root, slot, field, shown(isHome), open(on), say(text), state() }
    function mount(env) {
        var doc = env.doc, win = env.win, host = env.host, chip = env.chip || null;
        var visit = env.go || function (url) { win.location.assign(url); };
        var now = env.now || function () { return Date.now(); }, rnd = env.random || Math.random, Socket = env.WebSocket || win.WebSocket;
        var feed = [], you = null, login = false, people = null, state = 'connecting', welcomed = false, ever = false, lastId = -1, bad = 0, attempt = 0, liveSince = null, lastTry = 0;
        var sock = null, stopped = false, isHome = false, stale = true, notice = '', unanswered = [], stickTimer = 0, stuck = true, timers = { retry: 0, connect: 0, ping: 0, answer: 0, notice: 0, quiet: 0, flood: 0 };       // (unanswered: lines and commands sent and not answered yet, oldest first)
        var busy = false, menuOn = false, forgetSure = false, refocus = false, forgetAt = 0;                                       // (a POST of the login is on its way; the chip's dropdown of the account is open)
        var tokens = PACE.burst, tokensAt = now(), said = Object.create(null), heard = 0, skipped = 0, muted = { repaint: false, flood: false };       // (said: the texts that were sent in the last 31 s, as the hub remembers them)
        var open = (win.innerWidth || 0) >= 701 && (win.innerHeight || 0) >= 601;       // a window of 701 x 601 or more opens with the panel out, a smaller one with the folded header (home.css and home.js have the same limits)
        var hold = false;                                                        // the next repaint is for a change of the role: the reader keeps their place
        var removed = '';                                                        // 'kicked' or 'banned' after the hub said `bye` (or `banned` to a line): a notice stays on the screen, and a banned person's field stays off
        var taps = [], hello = null;                                             // the players list listens through this socket: fn(kind, frame), kind 'live' (the welcome), 'who', 'lost' or 'shown' (Home shows or not); hello: the open socket's welcome

        function el(tag, cls, text) {
            var e = doc.createElement(tag);
            if (cls) e.className = cls;
            if (text !== undefined) e.textContent = text;
            return e;
        }
        function put(parent, kids) { kids.forEach(function (k) { parent.appendChild(k); }); return parent; }
        function guard(fn) { return function () { try { return fn.apply(this, arguments); } catch (e) { if (env.error) env.error(e); return undefined; } }; }       // (nothing that goes wrong in here reaches the page)
        function stop(name) { if (timers[name]) { clearTimeout(timers[name]); timers[name] = 0; } }
        function tell1(fn, kind, m) { try { fn(kind, m); } catch (e) { if (env.error) env.error(e); } }
        function tapped(kind, m) { taps.forEach(function (fn) { tell1(fn, kind, m); }); }

        // The markup: a header (the title, the unread count, the last line for the strip, who is here, the fold button) and a body (the log, a note, the form, the login chip's place)
        var box = el('section', 'gc'); box.id = 'gchat'; box.setAttribute('aria-labelledby', 'gchat-title');
        var head = el('div', 'gc-head'), title = el('h2', 'gc-title'); title.id = 'gchat-title';
        title.textContent = TEXT.title;
        var pill = el('span', 'gc-unread'); pill.setAttribute('aria-hidden', 'true'); pill.hidden = true;
        var here = el('span', 'gc-online'), hereText = el('span', 'gc-on');
        put(here, [el('i', 'gc-dot'), hereText]);
        var fold = el('button', 'btn gc-fold'); fold.type = 'button'; fold.setAttribute('aria-controls', 'gchat-body'); fold.appendChild(el('i', 'gc-mark'));
        put(head, [title, pill, here, fold]);
        var body = el('div', 'gc-body'); body.id = 'gchat-body';
        var log = el('div', 'gc-log'); log.setAttribute('role', 'log'); log.setAttribute('aria-live', 'polite'); log.setAttribute('aria-label', TEXT.log); log.setAttribute('tabindex', '0');
        log.appendChild(el('p', 'gc-sys', TEXT.welcome));
        var note = el('p', 'gc-note'); note.setAttribute('role', 'status'); note.setAttribute('aria-live', 'polite');
        var news = el('p', 'gc-news'); news.setAttribute('role', 'status'); news.setAttribute('aria-live', 'polite');       // (what a screen reader is told when lines come faster than it can read them: one summary)
        var form = el('form', 'gc-form'), field = el('input', 'gc-input'), send = el('button', 'btn gc-send', TEXT.send), enter = el('button', 'btn gc-send gc-login');
        field.type = 'text'; field.setAttribute('aria-label', TEXT.field); field.setAttribute('placeholder', TEXT.field); field.setAttribute('autocomplete', 'off'); field.setAttribute('enterkeyhint', 'send');
        field.setAttribute('maxlength', String(LIMIT.field)); send.type = 'submit';
        enter.type = 'button'; enter.setAttribute('aria-label', TEXT.loginWith); enter.hidden = true; put(enter, [el('i', 'gc-dc'), el('span', '', TEXT.login)]);      // (where a login is offered and nobody is named: the field is off and this is in Send's place)
        put(form, [field, send, enter]); form.hidden = true;
        var guest = el('p', 'gc-guest', TEXT.guest); guest.hidden = true;
        // the account's dropdown (the chip opens it): who the visitor is (the hub's welcome says), the choice of the name, Log out, and Forget me. It stands next to the chip in the bar, not in the panel (which a phone hides on its Games and Players views)
        var slot = el('div', 'gc-slot'); slot.id = 'gchat-login-slot'; slot.setAttribute('data-slot', 'login'); slot.hidden = true;
        var acct = el('div', 'gc-acct'), acctName = el('bdi', 'gc-an'), outBtn = el('button', 'btn gc-out', TEXT.logout), forgetBtn = el('button', 'gc-forget', TEXT.forget), pickNote = el('p', 'gc-pn'), pickBtn = el('button', 'btn gc-pick');
        outBtn.type = 'button'; forgetBtn.type = 'button'; pickBtn.type = 'button';
        put(acct, [el('i', 'gc-dc'), el('span', 'gc-as', TEXT.chatting), acctName]);
        var yr = el('p', 'gc-yr'); yr.hidden = true;       // (the role, in words, in the dropdown of the account)
        put(slot, [acct, yr, pickNote, pickBtn, outBtn, el('p', 'gc-mnote', TEXT.forgetNote), forgetBtn]);
        var chipMark = null, chipAv = null, chipName = null;                   // the bar's chip (home.js makes the button and hides it): Log in, or who the visitor is
        if (chip) { chipMark = el('i', 'gc-dc'); chipAv = el('span', 'gc-av'); chipName = el('span', 'gc-cn'); chip.textContent = ''; put(chip, [chipMark, chipAv, chipName]); }
        put(body, [log, note, news, form, guest]);
        put(box, [head, body]);
        host.appendChild(box); if (chip && chip.parentNode && chip.parentNode.insertBefore) chip.parentNode.insertBefore(slot, chip.nextSibling); else (doc.body || host).appendChild(slot);       // (right after the chip: the Tab order and the bar's inert follow)

        // ---- what is seen -----------------------------------------------------------------------------------------------------------------------------------------------------------------------
        function visible() { return !doc.hidden; }
        function seen() { return isHome && open && visible(); }                 // the log is on the screen and somebody can read it
        function unread() { var n = 0; for (var i = 0; i < feed.length; i++) if (feed[i].fresh) n++; return n; }
        function row(e) {
            var r, admin = e.role === 'admin';
            if (e.priv) {                                                        // an answer to this visitor alone (never sent, never kept by the hub): a tag, then its lines
                r = el('div', 'gc-line gc-private'); r.appendChild(el('span', 'gc-tag', TEXT.only));
                e.lines.forEach(function (l) { r.appendChild(typeof l === 'string' ? el('div', 'gc-pl', l) : put(el('div', 'gc-pl'), [el('bdi', 'gc-cmd', l[0]), el('span', 'gc-pw', l[1] ? ' ' + l[1] : '')])); });
                return r;
            }
            if (e.sys) return put(el('div', 'gc-line gc-event'), [el('bdi', 'gc-text', e.text)]);       // (a line of the hub's own, drawn quietly; it names the person in its words, and has no author)
            r = el('div', 'gc-line' + (e.mod ? ' gc-mod' : '') + (admin ? ' gc-admin' : '') + (e.own ? ' gc-me' : ''));
            var name = el('bdi', 'gc-who', e.who);
            if (e.mod) { r.appendChild(el('span', 'gc-sr', wordOfRole(e.role) + ' ')); name.setAttribute('title', wordOfRole(e.role)); }       // (the name is red or yellow: the word is in the accessible name too, so that color is never the only sign)
            put(r, [name]);
            if (tools() && e.u) put(r, [el('bdi', 'gc-u', ' (' + e.u + ')')]);       // (a moderator is shown the username where it differs from the name; nobody else ever is)
            put(r, [el('span', 'gc-sep', ': '), el('bdi', 'gc-text', e.text)]);
            if (tools()) { var no = el('span', 'gc-no', ' #' + e.id); no.setAttribute('title', TEXT.number); r.appendChild(no); }       // (the number that /del takes: a moderator's only)
            return r;
        }
        function tools() { return !!you && you.role !== 'none'; }                // (a moderator or an administrator: the line numbers and the usernames are for them)
        function mine(e) { return !!you && (you.k ? e.k === you.k : e.who === you.name); }       // (by the hub's key; a hub that sends none: by the name)
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
            var keep = hold, top = log.scrollTop, bottom = atBottom(); hold = false;
            muted.repaint = true; live(); stop('quiet');
            while (log.children.length > 1) log.removeChild(log.children[log.children.length - 1]);
            feed.forEach(function (e) { e.el = row(e); log.appendChild(e.el); });
            stale = false;
            if (keep && !bottom && typeof top === 'number') log.scrollTop = top; else settle(true);       // (a role that changes keeps the place of a reader who is up in the log; a welcome and a tab that comes back read from the bottom)
            timers.quiet = setTimeout(guard(function () { timers.quiet = 0; muted.repaint = false; live(); }), 300);
        }
        function paint() {                                                       // everything that is not a line, from the model
            if (seen()) feed.forEach(function (e) { e.fresh = false; });
            var n = unread(), dead = typeof Socket !== 'function';
            var out = open;
            box.setAttribute('data-state', state); box.setAttribute('data-open', out ? 'true' : 'false'); box.setAttribute('data-you', you ? 'yes' : 'no');
            box.setAttribute('data-role', you ? you.role : 'none');
            body.hidden = !out;
            fold.setAttribute('aria-expanded', out ? 'true' : 'false');
            fold.setAttribute('aria-label', out ? TEXT.fold : TEXT.open + (n ? ', ' + n + TEXT.unread : ''));
            pill.textContent = n > 99 ? '99+' : String(n); pill.hidden = !n;
            hereText.textContent = state === 'live' ? (people !== null ? (people > LIMIT.here ? TEXT.many : people) + TEXT.online : '') : state === 'connecting' ? TEXT.connecting : TEXT.reconnecting;
            here.hidden = dead || !hereText.textContent;
            var live = state === 'live';
            form.hidden = !(live && (you || login));
            var ask = live && !you && login;                                    // (a login is offered and nobody is named: the field says so, and is off)
            var barred = removed === 'banned' && !!you, said = ask ? ERR.login : barred ? TEXT.banField : TEXT.field;       // (a banned person reads on: the field is off, and says why)
            field.disabled = !you || barred; field.setAttribute('placeholder', said); field.setAttribute('aria-label', said);
            send.hidden = !you || barred; enter.hidden = !!you;
            guest.hidden = !(live && !you);
            note.textContent = notice || (removed ? TEXT[removed] : state === 'offline' ? TEXT.offline : live && !you && !login ? ERR.readonly : '');       // (the notice of a removal stays until the person is somebody else)
            note.setAttribute('data-removed', removed && !notice ? removed : '');
            slot.hidden = !(live && you && login && menuOn); acctName.textContent = you ? you.name : '';
            acctName.className = 'gc-an' + roleClass(you ? you.role : 'none'); if (you && you.role !== 'none') acctName.setAttribute('title', wordOfRole(you.role)); else acctName.removeAttribute('title');
            yr.hidden = !(you && you.role !== 'none'); yr.textContent = yr.hidden ? '' : TEXT.youAre[you.role];       // (the role in words, as well as in the color of the name)
            if (slot.hidden && chip && slot.contains && slot.contains(doc.activeElement)) { if (live && login) chip.focus(); else refocus = true; }       // (the dropdown goes under the focus: a Log out, a Forget me: the focus goes to the chip, not to nowhere; a name that is picked closes the socket and hides the chip too, so the focus waits for the dropdown to come back)
            outBtn.disabled = forgetBtn.disabled = busy;
            pickNote.hidden = pickBtn.hidden = !(you && you.other);
            if (you && you.other) { var a = you.pick === 'display' ? 0 : 1; pickNote.textContent = TEXT.shows + TEXT.names[a] + '.'; pickBtn.textContent = TEXT.use + TEXT.names[1 - a] + ': ' + you.other; }
            if (!slot.hidden) place();
            if (!slot.hidden && refocus) { refocus = false; var back = pickBtn.hidden ? outBtn : pickBtn; if (back.focus) back.focus(); }       // (it is back: the focus is where it was, on the button of the name)
            if (!menuOn) refocus = false;
            if (slot.hidden && forgetSure) sure(false);
            if (chip) {                                                         // (the bar: Log in where a login is offered, the visitor's name where they are named; nothing while the hub is not there)
                chip.hidden = !(live && login);
                chipAv.hidden = !you; chipMark.hidden = !!you;
                chipAv.textContent = you ? initial(you.name) : ''; chipName.textContent = you ? you.name : TEXT.login;
                var has = !!you && you.role !== 'none', rc = roleClass(you ? you.role : 'none');
                chipName.className = 'gc-cn' + rc; chipAv.className = 'gc-av' + rc; if (has) chipName.setAttribute('title', wordOfRole(you.role)); else chipName.removeAttribute('title');       // (the name is red or yellow; the word is in the label and the title too)
                chip.setAttribute('aria-label', you ? TEXT.chatting + you.name + (has ? ', ' + wordOfRole(you.role) : '') : TEXT.loginWith);
                if (you) { chip.setAttribute('aria-haspopup', 'true'); chip.setAttribute('aria-controls', slot.id); chip.setAttribute('aria-expanded', menuOn ? 'true' : 'false'); } else { chip.removeAttribute('aria-haspopup'); chip.removeAttribute('aria-controls'); chip.removeAttribute('aria-expanded'); }
            }
            if (stale && seen()) repaint();
        }
        function place() {                                                       // the dropdown is under the chip, to its right edge, and no taller than what is left of the window (the rest scrolls inside it)
            if (!chip || !chip.getBoundingClientRect) return;
            var r = chip.getBoundingClientRect(), high = win.innerHeight || 0, top = Math.max(6, Math.min(Math.round(r.bottom + 6), high - 130));       // (a window that is very low moves it up over the bar rather than past the window's edge)
            slot.style.top = top + 'px'; slot.style.maxHeight = Math.max(0, high - top - 10) + 'px';
        }
        function render() { if (visible()) paint(); else stale = true; }       // (a hidden tab is not painted; the model goes on, and the tab's return paints it all)
        function tell(code) {                                                    // a word for the note, for a few seconds
            notice = wordOf(code, login);
            stop('notice');
            timers.notice = setTimeout(guard(function () { timers.notice = 0; notice = ''; render(); }), MS.notice);
            render();
        }

        // ---- the link -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        function drop() {                                                        // let go of the socket: its timers stop, its handlers are cleared, it is closed
            var s = sock;
            sock = null;
            ['connect', 'ping', 'answer', 'retry', 'notice'].forEach(stop); notice = ''; unanswered = [];
            if (s) { try { s.onopen = s.onmessage = s.onerror = s.onclose = null; s.close(); } catch (e) { /* it is gone */ } }
        }
        function lost() {                                                        // the socket is gone (closed, failed, silent, or it sent what no hub sends): say so, and try again later
            drop();
            if (liveSince !== null && now() - liveSince >= MS.stable) attempt = 0;       // (a link that held for a while starts over at 1 s; one that is closed at once keeps growing)
            liveSince = null; welcomed = false; state = 'offline'; hello = null;
            if (!stopped) { timers.retry = setTimeout(guard(go), delayOf(attempt, rnd())); attempt++; }
            tapped('lost');
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
            if (e.id !== null && e.id > lastId) lastId = e.id;
            while (feed.length > LIMIT.feed) { var old = feed.shift(); if (seen() && !stale && old.el && old.el.parentNode) old.el.parentNode.removeChild(old.el); else stale = true; }       // (a screen that is not painted is made again when it comes back)
        }
        function answer(lines) {                                                // a private answer: a line of this screen, with its lines, for this visitor alone (nothing is sent, the hub keeps nothing)
            var e = { id: null, priv: true, lines: lines, mod: false, role: 'none', own: false, fresh: false };
            push(e);
            if (seen() && !stale) { settle(true); e.el = row(e); log.appendChild(e.el); reveal(e); } else stale = true;
            render();
        }
        function reveal(e) {                                                     // an answer taller than the log is read from its first line, not from its last (a list of commands)
            setTimeout(guard(function () { if (e.el && e.el.getBoundingClientRect && log.getBoundingClientRect && e.el.offsetHeight > log.clientHeight) log.scrollTop += e.el.getBoundingClientRect().top - log.getBoundingClientRect().top; }), 1);
        }
        function onWelcome(m) {
            if (welcomed) return;                                                // (one welcome for each socket)
            if (m.v !== VERSION || (m.history !== undefined && !Array.isArray(m.history))) { lost(); return; }       // (a hub that speaks another version is no hub for this page)
            you = youOf(m.you);
            if ((removed === 'kicked' && you) || (removed === 'banned' && !you)) removed = '';       // (a person who is somebody else now, or nobody, is not the one that was removed)
            login = m.login === true; people = count(m.online);
            var before = lastId;
            feed.length = 0;
            (m.history || []).slice(-LIMIT.history).forEach(function (raw) {
                var e = lineOf(raw);
                if (!e || known(e.id)) return;
                e.own = !e.sys && mine(e);
                e.fresh = ever && e.id > before && !e.own && !seen();             // (the first welcome is the past; after a lost link, the lines that came meanwhile are news)
                push(e);
            });
            stop('connect'); stop('notice'); notice = ''; stale = true; welcomed = ever = true; state = 'live'; liveSince = now(); hello = m;
            ping();
            render();
            tapped('live', m);
        }
        function onLine(m) {
            var e = lineOf(m);
            if (!e || known(e.id)) return;
            e.own = !e.sys && mine(e); e.fresh = !e.own && !seen();
            if (e.own) { var key = nfc(e.text), at = -1, old; waiting(); for (var j = 0; j < unanswered.length && at < 0; j++) if (unanswered[j].key === key) at = j; old = oldest(false); if (at < 0 && old >= 0 && points(key) * 2 >= points(unanswered[old].key) && within(key, unanswered[old].key)) at = old; if (at >= 0) unanswered.splice(at, 1); }       // (the echo of a line: composed, or with characters taken out)
            push(e);
            if (seen() && !stale) { settle(); flow(); e.el = row(e); log.appendChild(e.el); } else stale = true;
            render();
        }
        function onDel(m) {
            if (count(m.id) === null) return;                                    // (only a number names a line: a private answer has none)
            for (var i = 0; i < feed.length; i++) {
                if (feed[i].id !== m.id) continue;
                var gone = feed.splice(i, 1)[0];
                if (seen() && !stale && gone.el && gone.el.parentNode) gone.el.parentNode.removeChild(gone.el); else stale = true;
                break;
            }
            render();
        }
        function onYou(m) {                                                      // the role changed (a promotion, a removal, a role that ran out): the marks and the commands follow at once; who the visitor is does not change on a socket
            var next = you ? youOf(m.you) : null;
            if (!next) return;
            var was = tools();
            you.role = next.role; you.mod = next.mod;
            if (was !== tools()) { stale = true; hold = seen(); }                                   // (the numbers and the usernames come or go with the role: the log is made again)
            render();
            tapped('you', m);
        }
        function onBye(m) {                                                      // the hub removed this person: a plain notice stays; the hub closes the socket, and the page connects again as it does after any close (with its backoff: no storm)
            removed = m.why === 'kicked' ? 'kicked' : m.why === 'banned' ? 'banned' : removed;
            render();
        }
        function onBans(m) {
            var lines = bansOf(m), at;
            if (!lines) return;
            for (var i = 0; i < unanswered.length && at === undefined; i++) if (unanswered[i].cmd && wordOfCommand(unanswered[i].text) === '/bans') at = i;
            if (at !== undefined) unanswered.splice(at, 1);
            answer(lines);
        }
        function onOk(m) {                                                       // a command was done: the oldest command that waits is answered
            var at = oldest(true), c = typeof m.c === 'string' && Object.prototype.hasOwnProperty.call(DONE, m.c) ? m.c : '';
            if (at >= 0) unanswered.splice(at, 1);
            if (c) answer([DONE[c]]);
        }
        function take(data) {
            var m = parse(data);
            if (!m) { if (++bad >= LIMIT.bad) lost(); return; }                  // (a hub that sends nothing but noise is dropped; a frame of noise is ignored)
            if (!welcomed && m.t !== 'welcome') return;
            stop('answer');
            if (m.t === 'welcome') onWelcome(m);
            else if (m.t === 'line') onLine(m);
            else if (m.t === 'del') onDel(m);
            else if (m.t === 'you') onYou(m);
            else if (m.t === 'ok') onOk(m);
            else if (m.t === 'bans') onBans(m);
            else if (m.t === 'bye') onBye(m);
            else if (m.t === 'online') { people = count(m.n); render(); }
            else if (m.t === 'who') tapped('who', m);
            else if (m.t === 'err' && (SAID.indexOf(m.code) !== -1 || ABOUT.indexOf(m.code) !== -1)) refuse(m.code);
            else if (m.t === 'err' && m.code === 'pick') tell('failed');
        }                                                                        // (any other kind is ignored)

        // ---- what the visitor does ------------------------------------------------------------------------------------------------------------------------------------------------------------
        function waiting() { var t = now(); unanswered = unanswered.filter(function (o) { return t - o.at < MS.pending; }); }       // (a send with no answer after 8 s is not waited for)
        function oldest(cmd) { for (var i = 0; i < unanswered.length; i++) if (!unanswered[i].cmd === !cmd) return i; return -1; }       // (the oldest line that waits, or with `cmd` the oldest command)
        function refuse(code) {                                                 // the hub refused a send, in order: `bad` is what it says to a wrong command, any other code is for a line; nothing of ours waiting, no refusal of ours
            waiting();
            var about = ABOUT.indexOf(code) !== -1, at = oldest(about || code === 'bad'), refused = at < 0 && about ? { cmd: true } : unanswered.splice(at < 0 ? 0 : at, 1)[0];       // (power, role and who are a command's, even when its wait is over)
            if (!refused) return;
            if (code === 'banned') removed = 'banned';                           // (a banned person who reloads the page is told by the hub's refusal: the notice and the field off)
            if (refused.cmd) answer([code === 'bad' ? TEXT.cmdBad : code === 'rate' ? TEXT.cmdRate : wordOf(code, login)]); else tell(code);
            if (refused.text === undefined) return;
            delete said[refused.text];
            if (!refused.cmd && !field.value) field.value = refused.text;      // (a command is not put back: Enter would issue it again)
        }
        function allowed(text) {                                                // this page's own pace, below the hub's; a text that was sent in the last 31 s is not sent again (the hub remembers every one of them, not only the last)
            var t = now(), old;
            tokens = Math.min(PACE.burst, tokens + Math.max(0, t - tokensAt) / PACE.every); tokensAt = t;
            for (old in said) if (t - said[old] >= PACE.same) delete said[old];
            if (tokens < 1 || said[text] !== undefined) return false;
            tokens -= 1; said[text] = t;
            return true;
        }
        function sendLine(raw) {                                                // '' when the line went out, else the code that says why not; never while the link is down
            var text = plain(String(raw)), word = you ? wordOfCommand(text) : '';
            if (word === '/help') { answer(helpOf(you.role)); return ''; }       // (this page's own: it is never sent, and it never is a line)
            if (word && you.role === 'none' && COMMANDS.some(function (c) { return c[0] === word; })) { answer([ERR.power]); return ''; }       // (a command word from a person who holds no role is not a line either: the hub would refuse it and never post it)
            var why = !sock || sock.readyState !== 1 || state !== 'live' ? 'offline' : !you ? 'readonly' : removed === 'banned' ? 'banned' : !text ? 'empty' : points(text) > LIMIT.line ? 'long' : /[<>]/.test(text) ? 'markup' : !allowed(text) ? 'rate' : '';
            if (why) { tell(why); return why; }
            try { sock.send(JSON.stringify({ t: 'say', text: text })); } catch (e) { lost(); return 'offline'; }
            var cmd = you.mod && text.charAt(0) === '/';
            waiting(); unanswered.push({ text: text, key: nfc(text), at: now(), cmd: cmd });       // (a command has no answer when the hub takes it, and `bad` when it is wrong)
            return '';
        }
        function setOpen(on) { open = !!on; if (seen()) stale = true; render(); }
        function setMenu(on) { menuOn = !!on; render(); }
        function startLogin() {                                                 // a full navigation: Discord, then the hub's callback, then this page again, on the screen that the visitor was on
            visit(LOGIN + (env.screen && env.screen() === 'room' ? 'room' : 'home'));
        }
        function relink() {                                                     // the login changed (the hub closes the sockets of the old one): this page connects again at once, and is told who it is now
            drop();
            liveSince = null; welcomed = false; attempt = 0; you = null; menuOn = false; hello = null; removed = '';
            for (var i = feed.length - 1; i >= 0; i--) if (feed[i].priv) feed.splice(i, 1);       // (the private answers go too: a list of bans is not for the next person)
            stale = true;                                                        // (the log is made again at once, without the usernames and numbers of a moderator: nobody sees them after a Log out)
            go();
            render();
        }
        function leave(path) {                                                  // Log out or Forget me: a POST of this page, with the cookie (it is the browser's: the script never sees it)
            var f = env.fetch || win.fetch;
            if (busy) return;
            if (typeof f !== 'function') { tell('failed'); return; }
            busy = true; render();
            var done = guard(function (ok) { busy = false; if (ok) relink(); else { tell('failed'); } render(); });
            try { f.call(win, path, { method: 'POST', credentials: 'same-origin', cache: 'no-store', redirect: 'error' }).then(function (r) { done(!!r && r.ok === true); }, function () { done(false); }); } catch (e) { done(false); }
        }
        function out(obj) { if (!sock || sock.readyState !== 1 || !welcomed) return false; try { sock.send(JSON.stringify(obj)); return true; } catch (e) { lost(); return false; } }
        enter.addEventListener('click', guard(startLogin));
        pickBtn.addEventListener('click', guard(function () { if (you && you.other && !out({ t: 'pick', pick: you.pick === 'display' ? 'username' : 'display' })) tell('failed'); }));       // (the hub closes the account's sockets; the page connects again and reads the new name)
        outBtn.addEventListener('click', guard(function () { leave(LOGOUT); }));
        function sure(on) { forgetSure = !!on; forgetBtn.textContent = on ? TEXT.sure : TEXT.forget; forgetBtn.setAttribute('data-sure', on ? 'true' : 'false'); }       // (Forget me asks once more: the second press does it; another place, Escape or the dropdown going away puts the question back)
        forgetBtn.addEventListener('click', guard(function () { if (!forgetSure) { forgetAt = now(); sure(true); return; } if (now() - forgetAt < 400) return; sure(false); leave(FORGET); }));       // (a double press is one press: the answer to the question comes after a moment)
        forgetBtn.addEventListener('blur', guard(function () { sure(false); }));
        if (chip) chip.addEventListener('click', guard(function () { if (!you) { startLogin(); return; } setMenu(!menuOn); if (menuOn && outBtn.focus) outBtn.focus(); }));       // (the chip opens the dropdown and puts the focus on Log out; a second press closes it)
        doc.addEventListener('click', guard(function (e) { var t = e && e.target; if (menuOn && !(t && ((slot.contains && slot.contains(t)) || (chip && chip.contains && chip.contains(t))))) setMenu(false); }));       // (a press anywhere else puts it away)
        win.addEventListener('resize', guard(function () { if (menuOn) render(); }));
        doc.addEventListener('keydown', guard(function (e) { if (e && e.key === 'Escape' && menuOn) { setMenu(false); if (chip && chip.focus) chip.focus(); } }));       // (Escape closes it, wherever the focus is, and the focus goes back to the chip)
        form.addEventListener('submit', guard(function (e) {
            if (e && e.preventDefault) e.preventDefault();
            if (sendLine(field.value) === '') field.value = '';
        }));
        fold.addEventListener('click', guard(function () { setOpen(!open); if (fold.focus) fold.focus(); }));
        head.addEventListener('click', guard(function (e) { if (!open && !(e && e.target && fold.contains && fold.contains(e.target))) setOpen(true); }));       // (a folded header opens wherever it is pressed)
        box.addEventListener('keydown', guard(function (e) {
            if (e && e.key === 'Escape' && open && !menuOn) { setOpen(false); if (fold.focus) fold.focus(); }       // (the dropdown first, then the panel)
        }));
        doc.addEventListener('visibilitychange', guard(function () { if (visible()) { stale = true; render(); nudge(); } }));
        win.addEventListener('online', guard(nudge));
        win.addEventListener('pagehide', guard(function () { stopped = true; lost(); }));       // (a page that goes away, or into the back-forward cache, lets the hub go)
        win.addEventListener('pageshow', guard(function (e) { if (e && e.persisted && stopped) { stopped = false; attempt = 0; go(); } }));

        paint();
        go();
        return {
            root: box, slot: slot, field: field, say: sendLine, state: function () { return state; },
            tap: function (fn) { taps.push(fn); if (hello) tell1(fn, 'live', hello); else if (state === 'offline') tell1(fn, 'lost'); },
            send: out,
            shown: guard(function (on) { isHome = !!on; if (!on) menuOn = false; if (seen()) stale = true; render(); tapped('shown', !!on); }), open: guard(setOpen)
        };
    }

    return { PATH: PATH, LIMIT: LIMIT, MS: MS, PACE: PACE, TEXT: TEXT, ERR: ERR, plain: plain, points: points, clip: clip, parse: parse, count: count, lineOf: lineOf, youOf: youOf, roleOf: roleOf, wordOfRole: wordOfRole, helpOf: helpOf, bansOf: bansOf, wordOfCommand: wordOfCommand, wordOf: wordOf, delayOf: delayOf, addressOf: addressOf, mount: mount };
});
