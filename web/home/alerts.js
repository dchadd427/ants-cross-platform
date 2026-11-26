// The alerts of the front page (docs/HOME_SCREEN.md, "Alerts"): the bell in the top bar with the number of alerts that have not been read, the list of the person's recent alerts that it opens (newest first; the settings behind a small gear in the same
// panel), and the toasts of what just happened. Loaded by web/home/home.js after the first paint, with web/home/alerts.css; the shared part (the switches, the tab alert, the sound table) is web/alert_core.js, which the page loads. Nothing is asked of the
// browser: the switches are kept in this browser only, and the list is the account's own on the chat hub (docs/HUB_PEOPLE.md), or a visitor's own in this browser.
//   * The list. A person who is logged in has it from the hub: `alerts` (the whole list, after the welcome and after every change) and `alert` (a new one), through the chat dock's socket; "seen" (everything up to a number is read), "clearalerts" and "note" (a
//     person joined my room: the lobby's own news) go up. A visitor has the alerts of this browser (a person joined the room: `ants.alerts.list.v1`, the last 30 of 7 days, every read and write guarded) and no invitations or mentions (they have no name).
//     Opening the list shows the unread ones with a gold bar; the number on the bell goes when the list is closed (a press on Join or Open counts as read).
//   * An invitation says who, where and how many are in (Join: the Games list's own Join, which asks first when the page holds a room), and once its room is gone it is dim, with no button. A mention says who and the start of the line (Open: the chat). A person
//     who joined my room says who and as which color (Open: the Room).
//   * The toast says what just happened where the person is looking: an invitation or a mention is a line of the chat when the chat is on the screen (the chat dock draws it), else a toast over the page (10 and 6 seconds); a switch turned off leaves the entry in the
//     list and shows nothing and blinks nothing. A person joins the room that this page holds: a toast on Home (the Room keeps its own), and the tab's title blinks while the page is in the background.
//   * Everything that is said (names, lines) becomes DOM text with createElement and textContent only, never markup.
// ES5, no module system: a page gets AntsHomeAlerts, node gets module.exports (tests/scripts/web_home_alerts_check.js). It uses the class .gshade of web/home/games.css, and looks out for the .gtoast that the Games list shows.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeAlerts = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var TOAST_MS = 6000, INVITE_MS = 10000;              // a toast goes after this long (not under the pointer or the focus); an invitation offers an action, so it stays longer
    var NAME_MAX = 40;                                   // a name in the tab's title is cut to this
    var KEEP = 30, DAYS = 7, LIST_KEY = 'ants.alerts.list.v1';
    var SVG = 'http://www.w3.org/2000/svg';
    var MATCH_ID = /^ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?$/;       // a match's id: its replay's file name without .antsrep (web/history_page.js, web/front/rematch.js)
    var PHONE = '(max-width: 700px), (max-height: 500px)';
    var KINDS = ['invite', 'mention', 'join', 'over', 'room', 'msg'];       // the kinds of row that the hub sends (docs/HUB_PEOPLE.md)
    var TEXT = {
        title: 'Alerts', tabAlerts: 'Alerts', tabMessages: 'Messages', msgNone: 'No private messages yet', msgNoneLine: 'Open a person\u2019s menu and press Message, or type /msg NAME.', msgKeep: 'Messages are kept for 7 days. Anyone can write to you; block a person to stop their messages.',
        you: 'You: ', followed: 'You follow this person', settings: 'Alert settings', group: 'On this page', lead: 'Nothing to allow. These work while Ants is open, also in a background tab.', close: 'Close', back: 'Back', gear: 'Alert settings', open: 'Open', doJoin: 'Join',
        joined: ' joined your room', invited: ' invited you', mentioned: ' mentioned you', opened: ' opened a room', messaged: ' sent you a private message', inChat: 'In the chat', closed: 'The room has closed.', as: 'as ', in: ' in', of: ' of ', now: 'just now', yesterday: 'Yesterday',
        none: 'No alerts yet', noneLine: 'Invitations, people who join your room and your mentions in the chat show up here.', noneGuest: 'People who join your room show up here.',
        keptAccount: 'Kept on your account for 7 days.', keptBrowser: 'Kept in this browser for 7 days.', loginNote: 'Log in to get invitations from other players.', list: 'My list', clear: 'Clear my alerts',
        clearAccount: 'Kept on your account for 7 days, the last 30.', clearBrowser: 'Kept in this browser for 7 days, the last 30.', unread: ' unread',
        toastInvite: ' invited you to ', toastMention: ' mentioned you in the chat.', toastRoom: ' opened a room: ', toastMessage: ' sent you a private message.', lineRoom: ' opened a room.', youInvited: 'You invited ', sent: 'Invitation sent to ', gone: 'That room is not open any more.',
        over: 'Your match is over', view: 'View', places: ['1st place', '2nd place', '3rd place', '4th place'],
        invite: ['Alert when someone invites me', 'From a player who is logged in.'], mention: ['Alert when someone mentions me', 'Someone writes @ and my name in the chat.'],
        room: ['Alert when someone I follow opens a room', 'A public room, never an invite-only one.'],
        join: ['Alert when someone joins my room', 'A person, not a computer player.'], start: ['Flash the tab when my match starts', 'The tab title blinks while this page is in the background.'],
        soundJoin: ['Sound when someone joins my room', 'A person, not a computer player.'], soundStart: ['Sound when my match starts', 'Plays while this page is in the background.']
    };
    var ROWS = [['invite', null, true], ['mention', null, true], ['room', null, true], ['join', null, false], ['start', null, false], ['soundJoin', 'join', false], ['soundStart', 'start', false]];      // [the switch, the sound that the row is for (null: none), whether only a person who is logged in has it]
    var PATHS = {
        bell: ['M6 17h12l-1.6-2.4V10a4.4 4.4 0 0 0-8.8 0v4.6L6 17z', 'M10 20a2 2 0 0 0 4 0'],
        user: ['M12 12.2a3.6 3.6 0 1 0 0-7.2 3.6 3.6 0 0 0 0 7.2z', 'M5 20c.8-3.6 3.6-5.5 7-5.5s6.2 1.9 7 5.5'],
        invite: ['M10 11.2a3.4 3.4 0 1 0 0-6.8 3.4 3.4 0 0 0 0 6.8z', 'M3.5 19.5c.7-3.4 3.2-5.2 6.5-5.2s5.8 1.8 6.5 5.2', 'M19 8v6', 'M16 11h6'],
        chat: ['M5 5h14a1.5 1.5 0 0 1 1.5 1.5v8A1.5 1.5 0 0 1 19 16h-8l-4.5 3.5V16H5a1.5 1.5 0 0 1-1.5-1.5v-8A1.5 1.5 0 0 1 5 5z'],
        gear: ['M12 15.2a3.2 3.2 0 1 0 0-6.4 3.2 3.2 0 0 0 0 6.4z', 'M19.4 13.5a7.6 7.6 0 0 0 0-3l1.9-1.5-1.9-3.3-2.3.9a7.5 7.5 0 0 0-2.6-1.5l-.4-2.4h-3.8l-.4 2.4a7.5 7.5 0 0 0-2.6 1.5l-2.3-.9-1.9 3.3 1.9 1.5a7.6 7.6 0 0 0 0 3l-1.9 1.5 1.9 3.3 2.3-.9a7.5 7.5 0 0 0 2.6 1.5l.4 2.4h3.8l.4-2.4a7.5 7.5 0 0 0 2.6-1.5l2.3.9 1.9-3.3z'],
        flag: ['M6 21V4', 'M6 5h11l-2.2 4 2.2 4H6'],
        home: ['M4 11.5L12 4l8 7.5', 'M6 10v9h12v-9', 'M10 19v-5h4v5'], lock: ['M7 11h10v8H7z', 'M9 11V8.5a3 3 0 0 1 6 0V11'],
        x: ['M6 6l12 12', 'M18 6L6 18'], back: ['M15 5l-7 7 7 7'], check: ['M5 12.5l4.5 4.5L19 7'], warn: ['M12 4l9 16H3z', 'M12 10v4', 'M12 17.2v.1']
    };

    function shorten(name) { name = String(name); return name.length > NAME_MAX ? name.slice(0, NAME_MAX - 1) + '\u2026' : name; }

    // ---- the pure parts ----
    function whole(v, low, high) { return typeof v === 'number' && v === Math.floor(v) && v >= low && v <= high; }
    function possessive(name) { return name + (/s$/i.test(name) ? '\u2019' : '\u2019s'); }
    // One row of a list, cleaned (a hub's, or this browser's own): { id, at, k, u (unread), and by kind: invite { from, map (a file name or ''), n, of, open, code }, mention { from, text }, join { name, seat }, over { map, place (0 when not said), match (an id, or '') } }, or null when it is no row of ours
    function rowOf(raw, Chat, rules) {
        if (!raw || typeof raw !== 'object' || !whole(raw.id, 0, 1e15) || !whole(raw.at, 0, 4e9) || KINDS.indexOf(raw.k) === -1) return null;
        var plain = function (s, max, strict) { return typeof s === 'string' ? Chat.clip(Chat.plain(s.slice(0, max * 4), strict), max) : ''; };
        var row = { id: raw.id, at: raw.at, k: raw.k, u: raw.u === 1 };
        if (raw.k === 'invite' || raw.k === 'room') {
            row.from = plain(raw.from, 32, true);
            if (!row.from || !whole(raw.n, 0, 4) || !whole(raw.of, 1, 4) || raw.n > raw.of) return null;
            row.n = raw.n; row.of = raw.of;
            row.map = typeof raw.map === 'string' && rules && rules.validMapFile(raw.map) ? raw.map : '';
            row.code = raw.o === 1 && typeof raw.code === 'string' && /^[a-z0-9_-]{1,32}$/.test(raw.code) ? raw.code : '';
            row.open = row.code !== '';
        } else if (raw.k === 'msg') {
            row.from = plain(raw.from, 32, true); row.h = typeof raw.h === 'string' && /^[0-9a-f]{12}$/.test(raw.h) ? raw.h : '';
            if (!row.from || !row.h) return null;
        } else if (raw.k === 'mention') {
            row.from = plain(raw.from, 32, true); row.text = plain(raw.text, 80, false);
            if (!row.from) return null;
        } else if (raw.k === 'over') {
            if (typeof raw.map !== 'string' || !rules || !rules.validMapFile(raw.map)) return null;
            row.map = raw.map; row.place = whole(raw.place, 1, 4) ? raw.place : 0; row.match = typeof raw.match === 'string' && MATCH_ID.test(raw.match) ? raw.match : '';
        } else {
            row.name = plain(raw.name, 32, false); row.seat = typeof raw.seat === 'string' && /^(Green|Red|Blue|Black)$/.test(raw.seat) ? raw.seat : '';
            if (!row.name || !row.seat) return null;
        }
        return row;
    }
    // The map's name of an invitation, in the page's words (an original's name; a community map's guess; none)
    function mapNameOf(file, rules) {
        if (!file || !rules) return '';
        var own = rules.mapOfFile(file);
        return own ? own.name : String(rules.communityName(file)).slice(0, 28);
    }
    // The words of a row: { title, sub, action ('Join', 'Open' or ''), dim } for the list (`now`: seconds since 1970; `agoOf` the chat dock's way to say how long ago)
    function wordsOf(row, rules, now, agoOf) {
        var ago = agoOf(row.at, now), map, parts;
        if (row.k === 'invite' || row.k === 'room') {
            var said = row.k === 'room' ? TEXT.opened : TEXT.invited;
            if (!row.open) return { title: row.from + said, sub: TEXT.closed + ' \u00b7 ' + ago, action: '', dim: true };
            map = mapNameOf(row.map, rules);
            parts = [map, row.n + TEXT.of + row.of + TEXT.in, ago].filter(Boolean);
            return { title: row.from + said, sub: parts.join(' \u00b7 '), action: TEXT.doJoin, dim: false };
        }
        if (row.k === 'over') return { title: TEXT.over, sub: [mapNameOf(row.map, rules), row.place ? TEXT.places[row.place - 1] : '', ago].filter(Boolean).join(' \u00b7 '), action: TEXT.view, dim: false };
        if (row.k === 'mention') return { title: row.from + TEXT.mentioned, sub: (row.text ? '\u201c' + row.text + '\u201d' : TEXT.inChat) + ' \u00b7 ' + ago, action: TEXT.open, dim: false };
        return { title: row.name + TEXT.joined, sub: TEXT.as + row.seat + ' \u00b7 ' + ago, action: TEXT.open, dim: false };
    }
    function countOf(rows) { var n = 0; rows.forEach(function (r) { if (r.u) n++; }); return n; }
    function countText(n) { return n > 9 ? '9+' : String(n); }
    function initial(name, Chat) { var first = Chat.clip(name, 1); return first.length > 1 && first.charAt(first.length - 1) === '\u2026' ? first.slice(0, -1) : first; }          // (the first code point of a name, without the dots that a cut adds)
    function countBig(n) { return n > 99 ? '99+' : String(n); }          // (the count of a tab and of a conversation: the bell's is 9+)
    // The list that a visitor keeps in this browser: the rows of the last 7 days, the newest 30, newest first
    function trim(rows, now) { return rows.filter(function (r) { return now - r.at < DAYS * 86400; }).sort(function (a, b) { return b.id - a.id; }).slice(0, KEEP); }

    // mount({ doc, win, core (AntsAlertCore), ctl: { screen() -> 'home'|'room', show(name, how), join(row, opener) (join the room of an invitation), chat() (show the chat) }, bar (the right end of Home's bar), menuBtn (Home's Menu button, a child of bar: the bell goes in front of it),
    //         signedIn() (whether the visitor is logged in), dock() (the chat dock, when it is there: tap(fn), send(obj), onScreen(), block(draw, cls), note(text)), now (seconds since 1970), table (optional: the sound table) })
    //   -> { open(opener), close(), alert(kind, data), say(words, tone), rows(), count(), refresh(), flash, title, wired }
    function mount(env) {
        var doc = env.doc, win = env.win, core = env.core, ctl = env.ctl || {};
        if (!core || !core.settings) return null;
        var set = core.settings(win), flash = core.flasher({ win: win, doc: doc }), snd = core.sounds({ win: win, settings: set, table: env.table });
        var now = env.now || function () { return Math.floor(Date.now() / 1000); };
        var shade = null, panel = null, opener = null, view = 'list', tab = 'alerts', unreadAtOpen = {}, toast = null, toastText = null, toastBtns = null, told = null, toastTimer = null, bell = null, mark = null;
        var hub = [], local = null, drawn = '', invitations = {};

        function Chat() { return win.AntsHomeChat; }
        function rules() { return win.AntsLobbyRules; }
        function el(tag, cls, text) { var e = doc.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
        function icon(name, cls) {                         // a small picture, drawn with the DOM and never with markup; a browser that cannot draw it shows none
            try {
                if (!doc.createElementNS) return null;
                var svg = doc.createElementNS(SVG, 'svg'), i;
                svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('class', cls || 'al-ico'); svg.setAttribute('aria-hidden', 'true'); svg.setAttribute('focusable', 'false');
                for (i = 0; i < PATHS[name].length; i++) { var p = doc.createElementNS(SVG, 'path'); p.setAttribute('d', PATHS[name][i]); svg.appendChild(p); }
                return svg;
            } catch (e) { return null; }
        }
        function signedIn() { return !!(env.signedIn && env.signedIn()); }
        function dock() { return env.dock ? env.dock() : null; }
        function phone() { try { return !!win.matchMedia && win.matchMedia(PHONE).matches; } catch (e) { return false; } }
        function wired() { var out = []; ROWS.forEach(function (r) { if ((!r[1] || (env.table || core.SOUNDS)[r[1]]) && (!r[2] || signedIn())) out.push(r[0]); }); return out; }
        function title() { return core.title(env.table); }

        // ---- the list: the account's (the hub's) or this browser's ----
        function loadLocal() {                             // this browser's list, or null while the chat's script (its text rules) is not here yet
            var C = Chat(), v, out = [];
            if (!C) return null;
            try { v = JSON.parse(win.localStorage.getItem(LIST_KEY) || '[]'); } catch (e) { v = []; }
            (Array.isArray(v) ? v : []).forEach(function (raw) { var r = rowOf(raw, C, rules()); if (r && (r.k === 'join' || r.k === 'over')) out.push(r); });
            return trim(out, now());
        }
        function mine() { if (local === null) local = loadLocal(); return local || []; }
        function saveLocal() { try { win.localStorage.setItem(LIST_KEY, JSON.stringify(mine().map(function (r) { return { id: r.id, at: r.at, k: r.k, u: r.u ? 1 : 0, name: r.name, seat: r.seat, map: r.map, place: r.place || undefined, match: r.match || undefined }; }))); } catch (e) { /* the visit keeps it */ } }
        function rows() { return (signedIn() ? hub : mine()).filter(function (r) { return r.k !== 'msg'; }); }       // (a private message is a conversation, with its own tab and count: its row of the hub's list is the toast's and nothing else)
        function convs() { try { return signedIn() && ctl.convs ? ctl.convs() : []; } catch (e) { return []; } }
        function hasMessages() { try { return signedIn() && !!ctl.convs && !!ctl.messagesReady && !!ctl.messagesReady(); } catch (e) { return false; } }       // (the Messages part is there, and the hub has said who the visitor is)
        function messageCount() { var n = 0; if (hasMessages()) convs().forEach(function (c) { n += c.u; }); return n; }
        function count() { return countOf(rows()) + messageCount(); }
        function newest() { var m = 0; rows().forEach(function (r) { if (r.id > m) m = r.id; }); return m; }
        function readAll() {
            if (!countOf(rows())) return;
            if (signedIn()) { var d = dock(); if (d && d.send) d.send({ t: 'seen', upto: newest() }); hub.forEach(function (r) { r.u = false; }); }
            else { mine().forEach(function (r) { r.u = false; }); saveLocal(); }
        }
        function addLocal(raw) {                           // a row of this browser's own list: { k: 'join', name, seat } (unread) or { k: 'over', map, place, match } (read: the page showed it already)
            var C = Chat(), id = 1, row;
            mine().forEach(function (r) { if (r.id >= id) id = r.id + 1; });
            raw.id = id; raw.at = now(); raw.u = raw.k === 'join' ? 1 : 0;
            row = C ? rowOf(raw, C, rules()) : null;
            if (!row) return;
            local = trim([row].concat(mine()), now()); saveLocal();
        }
        function fromHub(list) {
            var C = Chat(), out = [];
            if (!C || !Array.isArray(list) || list.length > 100) return null;
            list.forEach(function (raw) { var r = rowOf(raw, C, rules()); if (r) out.push(r); });
            return out;
        }

        // ---- the sheet: the list, and behind the gear the settings ----
        function paintSwitch(line, name) { var on = set.on(name); line.setAttribute('aria-checked', on ? 'true' : 'false'); line.className = 'al-line' + (on ? ' on' : ''); }
        function line(name) {
            var b = el('button', 'al-line'), words = TEXT[name], tx = el('span', 'al-tx'), sw = el('i', 'al-sw');
            b.type = 'button'; b.setAttribute('role', 'switch'); b.setAttribute('data-switch', name);
            tx.appendChild(el('span', 'al-t', words[0])); tx.appendChild(el('span', 'al-s', words[1]));
            b.appendChild(tx); b.appendChild(sw); paintSwitch(b, name);
            b.addEventListener('click', function () { set.set(name, !set.on(name)); paintSwitch(b, name); });
            return b;
        }
        function iconButton(cls, label, name, press) {
            var b = el('button', 'btn ' + cls), i = icon(name);
            b.type = 'button'; b.setAttribute('aria-label', label); b.setAttribute('title', label);
            if (i) b.appendChild(i); else b.textContent = label.charAt(0);
            b.addEventListener('click', press);
            return b;
        }
        function act(row) {                                // what the button of a row does: it counts as read (the list closes)
            var back = opener;
            close();
            if (row.k === 'invite' || row.k === 'room') { if (ctl.join) ctl.join(row, back); }
            else if (row.k === 'mention') { if (ctl.chat) ctl.chat(); }
            else if (row.k === 'over') { try { win.open(row.match ? '/match?id=' + encodeURIComponent(row.match) : '/matches', '_blank', 'noopener,noreferrer'); } catch (e) { /* the page that shows the match is another tab: a browser that refuses it shows none */ } }
            else if (ctl.show) ctl.show('room', 'push');
        }
        function item(row) {
            var w = wordsOf(row, rules(), now(), Chat().agoOf), li = el('div', 'al-item' + (unreadAtOpen[row.id] ? ' unread' : '') + (w.dim ? ' dim' : '')), ic = el('span', 'al-ic'), tx = el('span', 'al-tx'), i = icon(row.k === 'invite' ? 'invite' : row.k === 'mention' ? 'chat' : row.k === 'over' ? 'flag' : row.k === 'room' ? 'home' : 'user');
            li.setAttribute('data-id', String(row.id)); li.setAttribute('data-k', row.k);
            if (i) ic.appendChild(i);
            tx.appendChild(el('span', 'al-t', w.title)); tx.appendChild(el('span', 'al-s', w.sub));
            li.appendChild(ic); li.appendChild(tx);
            if (w.action) { var b = el('button', 'btn sm al-act', w.action); b.type = 'button'; b.setAttribute('aria-label', w.action + ': ' + w.title); b.addEventListener('click', function () { act(row); }); li.appendChild(b); }
            return li;
        }
        function listBody(card) {
            var rs = rows(), box = el('div', 'al-list');
            box.setAttribute('role', 'list');
            if (!rs.length) {
                var e = el('div', 'al-empty'), bi = icon('bell');
                if (bi) { bi.setAttribute('class', 'al-ico big'); e.appendChild(bi); }
                e.appendChild(el('p', 'al-et', TEXT.none)); e.appendChild(el('p', 'al-es', signedIn() ? TEXT.noneLine : TEXT.noneGuest));
                box.appendChild(e);
            } else rs.forEach(function (r) { box.appendChild(item(r)); });
            card.appendChild(box);
            if (!signedIn()) card.appendChild(el('p', 'al-note', TEXT.loginNote));
            card.appendChild(el('p', 'al-keep', signedIn() ? TEXT.keptAccount : TEXT.keptBrowser));
        }
        function convItem(c) {                              // a conversation: the person's circle, the name (and the gold star of a person that is followed), the start of the newest message, the time and the count of what is not read
            var C = Chat(), b = el('button', 'al-item al-conv' + (c.u ? ' unread' : '')), ic = el('span', 'al-ic', initial(c.name, C)), tx = el('span', 'al-tx'), t = el('span', 'al-t'), sub = (c.me ? TEXT.you : '') + c.text, meta = el('span', 'al-meta'), star;
            b.type = 'button'; b.setAttribute('data-h', c.h); b.setAttribute('aria-label', c.name + (c.u ? ', ' + c.u + TEXT.unread : '') + ', ' + sub);
            t.appendChild(el('bdi', '', c.name));
            if (c.f) { star = el('span', 'al-star'); star.setAttribute('role', 'img'); star.setAttribute('aria-label', TEXT.followed); t.appendChild(star); }
            tx.appendChild(t); tx.appendChild(el('bdi', 'al-s', sub));
            meta.appendChild(el('span', 'al-when', C.agoOf(c.at, now())));
            if (c.u) meta.appendChild(el('span', 'al-u', countBig(c.u)));
            b.appendChild(ic); b.appendChild(tx); b.appendChild(meta);
            b.addEventListener('click', function () { var back = opener; close(); if (ctl.messages) ctl.messages(c.h, c.name, back); });          // (the list closes, the conversation opens; its X leaves the page where it was)
            return b;
        }
        function messagesBody(card) {
            var cs = convs(), box = el('div', 'al-list');
            box.setAttribute('role', 'list');
            if (!cs.length) {
                var e = el('div', 'al-empty'), li = icon('lock');
                if (li) { li.setAttribute('class', 'al-ico big'); e.appendChild(li); }
                e.appendChild(el('p', 'al-et', TEXT.msgNone)); e.appendChild(el('p', 'al-es', TEXT.msgNoneLine));
                box.appendChild(e);
            } else cs.forEach(function (c) { box.appendChild(convItem(c)); });
            card.appendChild(box);
            card.appendChild(el('p', 'al-keep', TEXT.msgKeep));
        }
        function tabsBar() {
            var bar = el('div', 'al-tabs'), unread = 0, id;
            bar.setAttribute('role', 'tablist');
            rows().forEach(function (r) { if (unreadAtOpen[r.id]) unread++; });
            [['alerts', TEXT.tabAlerts, unread], ['messages', TEXT.tabMessages, messageCount()]].forEach(function (t) {
                var b = el('button', 'al-tab');
                b.type = 'button'; b.setAttribute('role', 'tab'); b.setAttribute('data-tab', t[0]); b.setAttribute('aria-selected', tab === t[0] ? 'true' : 'false'); b.setAttribute('data-on', tab === t[0] ? 'true' : 'false');
                b.appendChild(el('span', 'al-tl', t[1]));
                if (t[2]) { var n = el('span', 'al-tn', countBig(t[2])); n.setAttribute('aria-label', t[2] + TEXT.unread); b.appendChild(n); }
                b.addEventListener('click', function () { if (tab !== t[0]) { tab = t[0]; paint(); var again = shade && shade.querySelector ? shade.querySelector('.al-tab[data-tab="' + t[0] + '"]') : null; if (again && again.focus) again.focus(); } });
                bar.appendChild(b);
            });
            return bar;
        }
        function settingsBody(card) {
            card.appendChild(el('div', 'al-h', TEXT.group)); card.appendChild(el('p', 'al-lead', TEXT.lead));
            wired().forEach(function (name) { card.appendChild(line(name)); });
            card.appendChild(el('div', 'al-h', TEXT.list));
            var b = el('button', 'al-clear'), t = el('span', 'al-t', TEXT.clear), s = el('span', 'al-s', signedIn() ? TEXT.clearAccount : TEXT.clearBrowser);
            b.type = 'button'; b.appendChild(t); b.appendChild(s); b.setAttribute('data-clear', 'yes');
            b.addEventListener('click', function () {
                if (signedIn()) { var d = dock(); if (d && d.send) d.send({ t: 'clearalerts' }); hub = []; } else { local = []; saveLocal(); }
                unreadAtOpen = {}; view = 'list'; paint(); refresh();
            });
            card.appendChild(b);
        }
        function controls() { var out = [], all = shade && shade.querySelectorAll ? shade.querySelectorAll('button') : []; for (var i = 0; i < all.length; i++) out.push(all[i]); return out; }
        function paint() {                                 // the panel's content again (the list changed, or the view)
            if (!panel) return;
            var tabbed = view === 'list' && hasMessages(), card = panel, top = el('div', 'al-top'), h, first;
            if (!tabbed) tab = 'alerts';
            h = el('h2', 'al-title', view === 'list' ? (tab === 'messages' ? TEXT.tabMessages : title()) : TEXT.settings);
            while (card.firstChild) card.removeChild(card.firstChild);
            card.setAttribute('aria-label', h.textContent); card.setAttribute('data-view', view); card.setAttribute('data-tab', tab);
            card.appendChild(el('div', 'al-grab'));
            if (view === 'settings') top.appendChild(iconButton('al-back', TEXT.back, 'back', function () { view = 'list'; paint(); focusFirst(); }));
            top.appendChild(h);
            if (view === 'list' && tab === 'alerts') top.appendChild(iconButton('al-gear', TEXT.gear, 'gear', function () { view = 'settings'; paint(); focusFirst(); }));
            top.appendChild(iconButton('al-x', TEXT.close, 'x', function () { close(); }));
            card.appendChild(top);
            if (tabbed) card.appendChild(tabsBar());
            if (view !== 'list') settingsBody(card); else if (tab === 'messages') messagesBody(card); else listBody(card);
            first = null;
        }
        function focusFirst() { var b = shade && shade.querySelector ? (shade.querySelector('.al-conv') || shade.querySelector('.al-act') || shade.querySelector('.al-line') || shade.querySelector('.al-x')) : null; if (b && b.focus) b.focus(); }
        function place() {                                 // a wide window: under the bell, its right edge at the bar's, and no taller than what is left of the window; a phone's sheet is the style's
            if (!panel || phone()) { if (panel) { panel.style.top = ''; panel.style.right = ''; panel.style.maxHeight = ''; } return; }
            var bar = env.bar && env.bar.parentNode && env.bar.parentNode.getBoundingClientRect ? env.bar.parentNode : null, r = bar ? bar.getBoundingClientRect() : { bottom: 70 }, top = Math.round(r.bottom + 6), high = win.innerHeight || 600;
            panel.style.top = top + 'px'; panel.style.maxHeight = Math.max(160, high - top - 12) + 'px';
        }
        function open(back, which) {                       // which: 'messages' opens on the Messages tab (a conversation's arrow comes back to it)
            if (shade) { if (which === 'messages' && hasMessages() && (tab !== 'messages' || view !== 'list')) { tab = 'messages'; view = 'list'; paint(); focusFirst(); } return; }
            opener = back || bell || null; view = 'list'; tab = which === 'messages' && hasMessages() ? 'messages' : 'alerts'; unreadAtOpen = {};
            rows().forEach(function (r) { if (r.u) unreadAtOpen[r.id] = true; });
            shade = el('div', 'gshade al-shade' + (phone() ? ' phone' : ''));
            panel = el('section', 'al-panel'); panel.setAttribute('role', 'dialog'); panel.setAttribute('aria-modal', 'true');
            shade.appendChild(panel);
            var down = false, hit = false;                 // a press on the page outside closes it: it must start and end outside the panel
            shade.addEventListener('pointerdown', function (e) { down = e.target === shade; hit = false; });
            shade.addEventListener('pointerup', function (e) { hit = down && e.target === shade; down = false; });
            shade.addEventListener('pointercancel', function () { down = hit = false; });
            shade.addEventListener('click', function (e) { var was = hit; hit = false; if (was && e.target === shade) close(); });
            paint();
            doc.body.appendChild(shade); place();
            focusFirst();
            if (bell) bell.setAttribute('aria-expanded', 'true');
        }
        function close() {
            if (!shade) return;
            var s = shade, back = opener;
            shade = panel = null; opener = null; unreadAtOpen = {};
            if (s.parentNode && s.parentNode.removeChild) s.parentNode.removeChild(s);
            if (bell) bell.setAttribute('aria-expanded', 'false');
            readAll(); refresh();                          // the list has been looked at: the bell's number goes
            if (back && back.focus) back.focus();
        }
        doc.addEventListener('keydown', function (e) {
            if (!shade || !e) return;
            if (e.key === 'Escape') { if (e.preventDefault) e.preventDefault(); if (view === 'settings') { view = 'list'; paint(); focusFirst(); } else close(); return; }
            if (e.key !== 'Tab') return;                   // the focus stays inside the panel
            var all = controls(), i = all.indexOf(doc.activeElement);
            if (!all.length) return;
            if (e.shiftKey && i <= 0) { if (e.preventDefault) e.preventDefault(); all[all.length - 1].focus(); }
            else if (!e.shiftKey && (i === -1 || i === all.length - 1)) { if (e.preventDefault) e.preventDefault(); all[0].focus(); }
        });
        try { win.addEventListener('resize', function () { if (shade) { shade.className = 'gshade al-shade' + (phone() ? ' phone' : ''); place(); } }); } catch (e) { /* the panel keeps its place */ }

        // ---- the toast: over the page, just above the chat (above the strip of views on a phone) and above another toast that is up ----
        function placeToast() {
            try {
                var h = win.innerHeight || 0, top = h, strip = doc.querySelector ? doc.querySelector('.htabs') : null, dk = doc.getElementById('gchat'), others = doc.querySelectorAll ? doc.querySelectorAll('.gtoast') : [], up, i;
                if (!h) return;
                if (strip && strip.offsetHeight) top = strip.getBoundingClientRect().top;
                else if (dk && dk.offsetHeight) top = dk.getBoundingClientRect().top;
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
            toast = el('div', 'al-toast'); toastText = el('span'); told = el('p', 'al-sr'); toastBtns = el('div', 'al-btns');
            told.setAttribute('role', 'status'); told.setAttribute('aria-live', 'polite'); toast.hidden = true;
            toast.appendChild(toastText); toast.appendChild(toastBtns); doc.body.appendChild(told); doc.body.appendChild(toast);
        }
        // up(text, o): o = { icon ('user', 'invite', 'chat', 'check', 'warn'), tone ('' or 'warn'), action: { label, run }, ms }
        function up(text, o) {
            o = o || {};
            var i = icon(o.icon || 'user');
            while (toast.firstChild) toast.removeChild(toast.firstChild);
            while (toastBtns.firstChild) toastBtns.removeChild(toastBtns.firstChild);
            toastText = el('span', '', text);
            if (i) toast.appendChild(i);
            toast.appendChild(toastText); toast.appendChild(toastBtns);
            if (o.action) {
                var go = el('button', 'btn sm', o.action.label);
                go.type = 'button';
                go.addEventListener('click', function () { toast.hidden = true; try { o.action.run(); } catch (e) { /* the part that runs it is its own */ } });
                toastBtns.appendChild(go);
            }
            toast.className = 'al-toast' + (o.tone === 'warn' ? ' warn' : '');
            told.textContent = text; toast.hidden = false; placeToast(); arm(o.ms || TOAST_MS);
        }
        makeToast();

        // ---- what happens: a person joins my room (the lobby's news), an invitation or a mention comes (the hub's) ----
        // alert('join', { name, text, seat }): a person joined the room. Says whether Home's own display took the place of the page's toast (it did when Home shows, with the switch on or off: off shows nothing); the Room screen keeps the page's own toast.
        // alert('over', { map, place, match }): the account's own match is over (the page that came back from it says so, once the stats of the match are looked for): an entry of the list, kept read. It is told to the hub when the visitor is logged
        // in (the account's list), kept in this browser when not; and held back until the chat dock has said which of the two the visitor is.
        var liveKnown = false, waiting = [];
        function deliverOver(data) {
            var C = Chat(), row = C ? rowOf({ id: 1, at: 1, k: 'over', map: data.map, place: data.place, match: data.match }, C, rules()) : null, d = dock(), note;
            if (!row) return;
            if (signedIn()) {
                if (!d || !d.send) return;
                note = { t: 'note', k: 'over', map: row.map };
                if (row.place) note.place = row.place;
                if (row.match) note.match = row.match;
                d.send(note);
            } else if (!mine().some(function (r) { return r.k === 'over' && r.match && r.match === row.match; })) { addLocal({ k: 'over', map: row.map, place: row.place, match: row.match }); refresh(); }
        }
        function flushOver() { var q = waiting; waiting = []; liveKnown = true; q.forEach(deliverOver); }
        function alert(kind, data) {
            if (kind === 'over') {
                if (!data || typeof data.map !== 'string') return false;
                if (liveKnown) deliverOver(data); else waiting.push(data);
                return true;
            }
            if (kind !== 'join' || !data) return false;
            var home = !!ctl.screen && ctl.screen() === 'home', seat = typeof data.seat === 'string' ? data.seat : '', d = dock();
            if (/^(Green|Red|Blue|Black)$/.test(seat)) {
                if (signedIn() && d && d.send) d.send({ t: 'note', k: 'join', name: String(data.name || ''), seat: seat });      // (the hub keeps it in the account's list and sends it back to every page of the account)
                else if (!signedIn()) { addLocal({ k: 'join', name: String(data.name || ''), seat: seat }); refresh(); }
            }
            if (set.on('join')) {
                flash.start('\u25cf ' + shorten(data.name || '') + ' joined your room');
                snd.play('join');                          // (once a sound is approved: it plays in front as well as in the background, a person has joined)
                if (home) up(String(data.text || ''), { icon: 'user', action: { label: TEXT.open, run: function () { if (ctl.show) ctl.show('room', 'push'); } } });
            }
            return home;
        }
        function onChat() { var d = dock(); try { return !!(d && d.onScreen && d.onScreen()); } catch (e) { return false; } }
        function news(row) {                               // a new alert of the hub: where the person is looking decides how it is said
            if (row.k === 'join' || row.k === 'over') return;       // (the page that holds the room said it already: alert('join'); the page that came back from the match showed it: alert('over'))
            var chat = onChat(), d = dock();
            if (row.k === 'invite' || row.k === 'room') {
                var room = row.k === 'room';
                if (!set.on(room ? 'room' : 'invite')) return;
                flash.start('\u25cf ' + shorten(row.from) + (room ? TEXT.opened : TEXT.invited));
                if (chat && d && d.block) { invitation(row); return; }
                up(row.from + (room ? TEXT.toastRoom : TEXT.toastInvite) + (mapNameOf(row.map, rules()) || (room ? 'a public room' : 'a room')) + ' (' + row.n + TEXT.of + row.of + ').', { icon: room ? 'home' : 'invite', ms: INVITE_MS, action: { label: TEXT.doJoin, run: function () { if (ctl.join) ctl.join(row, null); } } });
            } else if (row.k === 'msg') {
                if (ctl.seeing && ctl.seeing(row.h)) return;        // (the conversation is on the screen: the Messages part reads it, and there is nothing to tell)
                flash.start('\u25cf ' + shorten(row.from) + TEXT.messaged);
                up(row.from + TEXT.toastMessage, { icon: 'lock', ms: INVITE_MS, action: { label: TEXT.open, run: function () { if (ctl.messages) ctl.messages(row.h, row.from); } } });
            } else {
                if (!set.on('mention')) return;
                flash.start('\u25cf ' + shorten(row.from) + TEXT.mentioned);
                if (!chat) up(row.from + TEXT.toastMention, { icon: 'chat', action: { label: TEXT.open, run: function () { if (ctl.chat) ctl.chat(); } } });
            }
        }
        function invitation(row) {                         // the line of the chat that only this person sees: who, where and how many are in, and Join; it is dim when the room is gone
            var d = dock(), state = { row: row }, entry;
            function draw(box) {
                var cur = state.row, body = el('div', 'al-cl'), tx = el('div', 'al-ct'), who = el('div', 'gc-pl');
                who.appendChild(el('bdi', 'gc-cmd', cur.from)); who.appendChild(el('span', 'gc-pw', cur.k === 'room' ? TEXT.lineRoom : ' invited you to their room.'));
                tx.appendChild(who);
                tx.appendChild(el('div', 'gc-pl al-cs', cur.open ? [mapNameOf(cur.map, rules()), cur.n + TEXT.of + cur.of + TEXT.in].filter(Boolean).join(' \u00b7 ') : TEXT.closed));
                body.appendChild(tx);
                if (cur.open) { var b = el('button', 'btn sm', TEXT.doJoin); b.type = 'button'; b.addEventListener('click', function () { if (ctl.join) ctl.join(cur, null); }); body.appendChild(b); } else box.className += ' dim';
                box.appendChild(body);
            }
            entry = d.block(draw, 'al-chatline');
            invitations[row.id] = { state: state, entry: entry };
        }
        function reflect() {                               // the hub sent the whole list again: an invitation whose room is gone is dim in the chat too
            Object.keys(invitations).forEach(function (id) {
                var inv = invitations[id], cur = null, was = inv.state.row;
                hub.forEach(function (r) { if (String(r.id) === id) cur = r; });
                if (!cur || !cur.open) { inv.state.row = cur || { id: was.id, at: was.at, k: was.k, from: was.from, n: was.n, of: was.of, open: false, code: '', map: was.map, u: false }; if (inv.entry && inv.entry.redraw) inv.entry.redraw(); delete invitations[id]; }
            });
        }
        function tap(kind, m) {                            // the chat dock's news: who the visitor is, the list, a new alert
            if (kind === 'live') { hub = []; refresh(); flushOver(); }
            else if (kind === 'alerts') { var next = signedIn() ? fromHub(m && m.rows) : null; if (next) { hub = next; reflect(); if (shade) paint(); refresh(); } }
            else if (kind === 'alert') {
                var C = Chat(), row = C && signedIn() && m ? rowOf(m.row, C, rules()) : null;
                if (!row || hub.some(function (r) { return r.id === row.id; })) return;
                hub = [row].concat(hub).slice(0, KEEP);
                if (shade) { unreadAtOpen[row.id] = true; paint(); }
                refresh(); news(row);
            } else if (kind === 'lost' || kind === 'you') { refresh(); if (kind === 'lost') flushOver(); }
        }

        // ---- the bell ----
        function refresh() {
            if (!bell) return;
            var n = count(), key = n + ':' + (signedIn() ? 1 : 0);
            if (key === drawn) return;
            drawn = key;
            bell.setAttribute('aria-label', TEXT.title + (n ? ', ' + n + TEXT.unread : '')); bell.setAttribute('title', TEXT.title); bell.setAttribute('data-count', String(n));
            if (mark && mark.parentNode) mark.parentNode.removeChild(mark);
            mark = null;
            if (n) { mark = el('span', 'al-mark', countText(n)); mark.setAttribute('aria-hidden', 'true'); bell.appendChild(mark); }
        }
        function makeBell() {
            if (!env.bar || !env.bar.insertBefore) return;
            bell = el('button', 'btn al-bell'); bell.type = 'button'; bell.setAttribute('aria-haspopup', 'dialog'); bell.setAttribute('aria-expanded', 'false');
            var ic = icon('bell'); if (ic) bell.appendChild(ic);
            bell.addEventListener('click', function () { if (shade) close(); else open(bell); });         // (a press opens the list and nothing else: the browser is never asked)
            env.bar.insertBefore(bell, env.menuBtn && env.menuBtn.parentNode === env.bar ? env.menuBtn : null);          // (right before the Menu's button: its right edge is one gap from the Menu's, whoever else is in the bar)
            refresh();
        }
        var hooked = false, tries = 0;
        function hook() {                                  // the chat dock comes after this part, whichever is first: the list follows the dock's news
            var dk = dock();
            if (hooked) return;
            if (dk && dk.tap) { hooked = true; dk.tap(tap); refresh(); return; }
            if (++tries < 40) setTimeout(hook, 500);
            else flushOver();                              // (no chat on this page: the visitor keeps its list in this browser)
        }
        makeBell();
        hook();

        function messagesChanged() {                       // the Messages part says that the conversations or the count of what is not read changed: the bell's number, and the list if it is on the screen
            if (shade && view === 'list') { if (tab === 'messages' || hasMessages()) paint(); }
            refresh();
        }

        // say(words, tone): a toast of the page's own for another part (the invitation that was sent, the one that was refused); tone 'warn' is the gold one
        function say(words, tone) { up(String(words), { icon: tone === 'warn' ? 'warn' : 'check', tone: tone === 'warn' ? 'warn' : '' }); return true; }
        return { open: open, close: close, alert: alert, say: say, rows: rows, count: count, refresh: refresh, title: title, flash: flash, wired: wired, tap: tap, messagesChanged: messagesChanged };
    }

    return { TEXT: TEXT, TOAST_MS: TOAST_MS, INVITE_MS: INVITE_MS, KEEP: KEEP, DAYS: DAYS, LIST_KEY: LIST_KEY, rowOf: rowOf, wordsOf: wordsOf, countOf: countOf, countText: countText, trim: trim, possessive: possessive, mount: mount };
});
