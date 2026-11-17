// The players list of Home (docs/HOME_SCREEN.md, "The players list"): who has the front page open right now. A panel: the narrow column at the right of the Games list on Home, or the Players view of a phone. It speaks through the chat dock's socket (docs/HUB.md, "Presence": up {"t":"who"} and {"t":"here","at",...}, down {"t":"who",...}),
// so a page has one hub socket; web/home/home.js loads it after the chat dock, with web/home/players.css. ES5, no module system: a page gets AntsHomePlayers, node gets module.exports
// (tests/scripts/web_home_players_check.js).
//   * Everything the hub sends is text from strangers: names are cleaned and cut as the chat's are, a map is a file name that the page's own rule accepts, every number is checked, a frame that is wrong in any
//     part is ignored, at most 200 rows are kept, and all of it is drawn with createElement and textContent: never markup, never a link.
//   * The page tells the hub what its visitor is doing (looking, waiting in a room that has not started, in a match) from what it already knows (its room), at most every 3 seconds; a public room says its map,
//     seats and code, a private one says nothing. Nothing is kept in the browser.
//   * Home never waits for it: a hub that is not there is "Players list is off right now" (it comes back by itself), a hub that has no list (an older one) shows nothing, and what goes wrong in here stays here.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomePlayers = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var LIMIT = { rows: 200, name: 32, seats: 4, ids: 1e9, here: 500, map: 28 };       // (the hub's own: 200 rows, names of 32, four colors; a map's name is cut for the line as the Games list cuts it)
    var MS = { poll: 1000, tell: 3000, note: 4000 };                      // (poll: the page looks at its own room this often; tell: the hub is told what changed at most this often)
    var ROOM = /^[a-z0-9_-]{1,32}$/;
    var STATUS = ['look', 'wait', 'play'];
    var SHOWN = STATUS.concat('away');       // (what a row may say: a page says the first three; `away` is the hub's own word for a person whose last socket closed a short while ago)
    var GROUPS = [['mod', 'Staff'], ['look', 'Looking'], ['wait', 'Waiting'], ['play', 'Playing'], ['away', 'Away']];
    var WORD = { look: 'Looking', wait: 'Waiting', play: 'Playing', away: 'Away' };
    var TEXT = {
        title: 'Players', here: ' here now', you: 'you', moderator: 'moderator', administrator: 'administrator', privateRoom: 'Private room', privateGame: 'Private game', vsComputer: 'vs computer', publicRoom: 'Public room', inRoom: 'In a room', inGame: 'In a game', join: 'Join', gone: 'That game is not open any more.',
        guest: ' guest', guests: ' guests', notIn: 'not logged in', more: ' more', and: 'and ', many: '500+', player: ' player', players: ' players', of: ' of ',
        off: 'Players list is off right now', offNote: 'The chat hub is not answering. Games and rooms work as always; this list comes back by itself.'
    };

    // ---- the pure parts ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    function whole(v, low, high) { return typeof v === 'number' && v === Math.floor(v) && v >= low && v <= high; }
    function possessive(name) { return name + (/s$/i.test(name) ? '’' : '’s'); }
    function initial(name, Chat) {                                       // the first code point of a name, for the circle
        var first = Chat.clip(name, 1);
        return first.length > 1 && first.charAt(first.length - 1) === '…' ? first.slice(0, -1) : first;
    }
    function fresh() { return { rows: Object.create(null), n: 0, g: 0, more: 0 }; }
    function roleOf(raw, mod) { return mod !== true ? 'none' : raw === 'admin' ? 'admin' : 'mod'; }       // (the hub's `mod` is true for both roles; `role` says which, where the hub says it: nothing else makes an admin)
    // One row of the hub's list, cleaned: { id, name, at, mod, role, pic (a checked picture name or ''), pub, priv (a private room or match), solo (a game against the computer; its map is an original's name only), map, mapName, guess, n, of, code }, or null when it is not a row. A room's details are kept for a public room only, a code for a room that waits only.
    // mapName is an original's name (the page's own table); guess is what a community map would be called, which is drawn only where the Games list shows the same room under the same name (the hub's words are not drawn).
    function rowOf(raw, Chat, rules) {
        if (!raw || typeof raw !== 'object' || !whole(raw.i, 0, LIMIT.ids) || typeof raw.name !== 'string' || SHOWN.indexOf(raw.at) === -1) return null;
        var name = Chat.clip(Chat.plain(raw.name.slice(0, LIMIT.name * 4), true), LIMIT.name);
        if (!name) return null;
        var row = { id: raw.i, name: name, at: raw.at, mod: raw.mod === true, role: roleOf(raw.role, raw.mod), pic: Chat.picOf(raw.pic), pub: false, priv: false, solo: false, map: '', mapName: '', guess: '', n: -1, of: -1, code: '' };
        if (raw.at === 'away') return row;                                 // (an Away row is a name and the word: whatever else a frame holds is not drawn)
        if (raw.pub !== true && raw.at !== 'look') {                       // a private room or match is a fact (no map, seats or code are kept); a game against the computer is a match with a map at most
            if (raw.priv === true) row.priv = true;
            else if (raw.solo === true && raw.at === 'play') {
                row.solo = true;
                if (typeof raw.map === 'string' && rules.validMapFile(raw.map)) { var own = rules.mapOfFile(raw.map); row.map = raw.map; row.mapName = own ? own.name : ''; }
            }
        }
        if (raw.pub === true) {
            row.pub = true;
            if (typeof raw.map === 'string' && rules.validMapFile(raw.map)) {
                var known = rules.mapOfFile(raw.map);
                row.map = raw.map; row.mapName = known ? known.name : ''; row.guess = known ? '' : rules.communityName(raw.map).slice(0, LIMIT.map);
            }
            if (whole(raw.n, 0, LIMIT.seats)) row.n = raw.n;
            if (whole(raw.of, 1, LIMIT.seats) && raw.of >= row.n) row.of = raw.of;
            if (raw.at === 'wait' && typeof raw.code === 'string' && ROOM.test(raw.code)) row.code = raw.code;
        }
        return row;
    }
    // The list that a frame makes of the one that a page has ({ rows: {id: row}, n, g, more }): the whole list replaces it, a change upserts its rows and drops the numbers that are gone. null: not a frame of
    // the list (a part that is wrong, more rows than the hub sends): the page keeps what it has.
    function apply(model, m, Chat, rules) {
        if (!m || typeof m !== 'object' || Chat.count(m.n) === null || Chat.count(m.g) === null || Chat.count(m.more) === null || !Array.isArray(m.rows) || m.rows.length > LIMIT.rows) return null;
        if (m.gone !== undefined && (!Array.isArray(m.gone) || m.gone.length > LIMIT.rows)) return null;
        var rows = Object.create(null), id, kept = 0;
        if (m.full !== true && model) for (id in model.rows) rows[id] = model.rows[id];
        m.rows.forEach(function (raw) { var row = rowOf(raw, Chat, rules); if (row) rows[row.id] = row; });
        (m.gone || []).forEach(function (gone) { if (whole(gone, 0, LIMIT.ids)) delete rows[gone]; });
        for (id in rows) kept++;
        return kept > LIMIT.rows ? null : { rows: rows, n: m.n, g: m.g, more: m.more };
    }
    // The rows in their groups, each by name; the visitor's own row (its number, `me`, comes with the hub's welcome) is shown apart and is not among them
    function listOf(model, me) {
        var out = { mod: [], look: [], wait: [], play: [], away: [] };
        for (var id in model.rows) {
            var row = model.rows[id];
            if (row.id !== me) out[row.mod ? 'mod' : row.at].push(row);
        }
        GROUPS.forEach(function (g) { out[g[0]].sort(function (a, b) { return (b.role === 'admin') - (a.role === 'admin') || (a.name < b.name ? -1 : a.name > b.name ? 1 : a.id - b.id); }); });       // (the admins first, then by name)
        return out;
    }
    function seatsText(row) {                                            // "Treasure . 2 of 4": the map, and the colors that play of those in the match
        var parts = [];
        if (row.mapName) parts.push(row.mapName);
        if (row.n >= 0 && row.of >= 1) parts.push(row.n + TEXT.of + row.of);
        return parts.join(' · ');
    }
    // The line under a name: where the person is. A person who looks has none (the group says it), except where `label` is set (the visitor's own row, a moderator's: no group says it there). A room without details
    // is "In a room": the hub keeps none of an account that may not chat either (a private room or match is still a fact for an account that may not chat); the visitor's own row (`own`) knows.
    function metaOf(row, label, own) {
        if (row.at === 'look' || row.at === 'away') return label ? WORD[row.at] : '';       // (the group says it, except where there is no group for it: the visitor's own row, a moderator's)
        if (row.at === 'wait') return row.pub ? seatsText(row) || TEXT.publicRoom : row.priv || own ? TEXT.privateRoom : TEXT.inRoom;
        if (row.priv) return TEXT.privateGame;
        if (row.solo) return [row.mapName, TEXT.vsComputer].filter(Boolean).join(' · ');
        if (!row.pub) return own ? TEXT.privateGame : TEXT.inGame;
        return [row.mapName, row.n >= 0 ? row.n + (row.n === 1 ? TEXT.player : TEXT.players) : ''].filter(Boolean).join(' · ') || TEXT.inGame;
    }
    // Where a person is, in words, for the moderation sheet (web/home/mod.js): the group's word, then what the row says under the name ("Waiting · Treasure · 2 of 4", "Playing · Private game", "Looking")
    function whereOf(row) {
        var meta = row.at === 'look' || row.at === 'away' ? '' : metaOf(row, false, false);
        return meta ? WORD[row.at] + ' · ' + meta : WORD[row.at];
    }
    // What this page tells the hub it is doing, from its own room (client: the page's LobbyClient or null; count: Home's { people, size } of the room or null; handing: the page is on its way to a game).
    // Looking: no room. Waiting: a room that has not started. Playing: it has (or the page is on its way to it). A public room adds its map and seats, and its code if the visitor is its host; a private one says `pub`:false and nothing more.
    function announceOf(client, count, handing) {
        var room = client && client.room;
        if (!room) return { at: 'look' };
        var say = { at: room.starting || handing || !room.lobby ? 'play' : 'wait' };
        if (room.isPublic === true) {
            say.pub = true;
            if (typeof room.map === 'string' && room.map) say.map = room.map;
            if (count && count.size >= 1 && count.people <= count.size) { say.n = count.people; say.of = count.size; }
            if (say.at === 'wait' && typeof client.code === 'string' && ROOM.test(client.code) && client.isLeader && client.isLeader()) say.code = client.code;      // (the host's row has the Join, not each guest's)
        } else say.pub = false;                                          // (a private room says only that: the hub keeps a mark, never a map, seats or a code)
        return say;
    }

    // The note that the game page reads when this page hands the visitor over (web/hub_here.js; sessionStorage `ants.here`, this tab only): { v: 1, named, r, pub, map, n, of } and nothing else: `named` is 1 for a person who is logged in,
    // `r` eight hex digits of a plain hash of the room's code (it only tells the game page that the note is this room's: the code itself is never kept here), a private room says `pub`:false and no more. null where there is no room.
    function hashOf(code) {
        var h = 0x811c9dc5, i;
        for (i = 0; i < code.length; i++) { h ^= code.charCodeAt(i) & 255; h = (h + ((h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24))) >>> 0; }
        return ('00000000' + h.toString(16)).slice(-8);
    }
    function noteOf(said, named, code) {
        if (!said || (said.at !== 'wait' && said.at !== 'play') || typeof code !== 'string' || !ROOM.test(code)) return null;
        var note = { v: 1, named: named ? 1 : 0, r: hashOf(code), pub: said.pub === true };
        if (note.pub) { if (said.map) note.map = said.map; if (said.n !== undefined) { note.n = said.n; note.of = said.of; } }
        return note;
    }

    // ---- the list ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // mount({ doc, win, host (Home's div), yard (the box that it mounts into), games (the bar's button), dock (the chat dock: tap(fn), send(obj)), page (the bridge: client(), info(), handing()), now, error }) returns { root, list (the scrolling list: the moderation sheet's way in), state, model, person(i), drawn(fn), sync }, or null
    function mount(env) {
        var doc = env.doc, win = env.win, host = env.host, dock = env.dock, page = env.page || {};
        var Chat = win.AntsHomeChat, rules = win.AntsLobbyRules, Home = win.AntsHome;
        if (!dock || typeof dock.tap !== 'function' || !Chat || !rules || !doc.createElement) return null;
        var now = env.now || function () { return Date.now(); };
        var yard = env.yard || host;
        var phase = 'none', supported = null, live = false, asked = false, you = null, model = fresh();      // phase: 'none' (nothing shows), 'off' (the hub is not there), 'on'
        var me = null, doing = null, sentSig = null, lastSent = null, painted = '', joins = [], kept = '', timers = { poll: 0, note: 0 }, drawnFns = [];

        function el(tag, cls, text) {
            var e = doc.createElement(tag);
            if (cls) e.className = cls;
            if (text !== undefined) e.textContent = text;
            return e;
        }
        function put(parent, kids) { kids.forEach(function (k) { parent.appendChild(k); }); return parent; }
        function guard(fn) { return function () { try { return fn.apply(this, arguments); } catch (e) { if (env.error) env.error(e); return undefined; } }; }
        function button(cls, text, onclick) { var b = el('button', 'btn ' + cls, text); b.type = 'button'; if (onclick) b.addEventListener('click', guard(onclick)); return b; }
        function stop(name) { if (timers[name]) { clearTimeout(timers[name]); timers[name] = 0; } }
        function countText(n) { return n > LIMIT.here ? TEXT.many : String(n); }
        function find(selector) { try { return host.querySelector ? host.querySelector(selector) : null; } catch (e) { return null; } }
        function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }

        // The markup: one box, `root`, that Home can make inert, with the panel in it
        var panel = el('section', 'pl'); panel.id = 'players'; panel.setAttribute('aria-labelledby', 'players-title');
        panel.setAttribute('data-state', 'off');
        var head = el('div', 'pl-head'), title = el('h2', 'pl-title', TEXT.title); title.id = 'players-title';
        var total = el('span', 'pill pl-count'), dot = el('i', 'pl-dot');
        dot.setAttribute('aria-hidden', 'true'); total.hidden = true; dot.hidden = true;
        put(head, [title, total, dot]);
        var body = el('div', 'pl-body'), youBox = el('div', 'pl-you'), youList = el('ul', 'pl-cards'), rule = el('hr', 'pl-div'), status = el('p', 'pl-status'), list = el('div', 'pl-list'), offBox = el('div', 'pl-note');
        body.id = 'players-body'; youBox.appendChild(youList);
        status.setAttribute('role', 'status'); status.setAttribute('aria-live', 'polite');
        list.setAttribute('tabindex', '0'); list.setAttribute('role', 'region'); list.setAttribute('aria-label', TEXT.title);
        put(panel, [head, body]);
        var root = el('div', 'pl-root'); root.hidden = true;
        root.appendChild(panel);
        yard.appendChild(root);

        // ---- what is drawn ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        function ownCode() { var c = page.client ? page.client() : null; return c && c.code ? c.code : ''; }
        function gameOf(code) {                                          // the Games list's own row of a room (the game server's public list): its Join button, and the map's name that it shows
            var b = find('.gjoin[data-code="' + code + '"]'), item = b && b.parentNode, name = item && item.querySelector ? item.querySelector('.gmapname') : null;
            return b ? { name: name ? name.textContent : '' } : null;
        }
        function named(row) {                                            // the row as it is drawn: `listed` when the Games list has that room (a Join is offered only then), and its map's name only if that row shows the same
            var game = row.code ? gameOf(row.code) : null, copy = {};
            for (var k in row) copy[k] = row[k];
            copy.listed = !!game;
            if (game) copy.mapName = game.name === (row.mapName || row.guess) ? game.name : '';
            return copy;
        }
        function card(row, o) {                                          // o: { you: the visitor's own row, label: say where the person is under the name, join: a Join button (a public room that waits) }
            var li = el('li', 'pl-card'), who = el('span', 'pl-who'), name = el('span', 'pl-name'), av = el('span', 'pl-av', initial(row.name, Chat)), meta = metaOf(row, o.label, o.you);
            li.setAttribute('data-s', row.at);
            if (!o.you) li.setAttribute('data-i', String(row.id));       // (the hub's number of the row: the moderation sheet names the person by it, `row:<n>`, never by the name)
            av.setAttribute('aria-hidden', 'true'); Chat.picture(el, av, row.pic, 32);
            var nm = el('bdi', 'pl-nm', row.name), word = row.role === 'admin' ? TEXT.administrator : TEXT.moderator;
            if (row.mod) { li.className += row.role === 'admin' ? ' r-admin' : ' r-mod'; nm.setAttribute('title', word); name.appendChild(el('span', 'pl-sr', word + ' ')); }       // (the name is red or yellow: the word is in the accessible name and the title as well, so that color is never the only sign)
            name.appendChild(nm);
            if (o.you) name.appendChild(el('span', 'pl-tag', TEXT.you));
            who.appendChild(name);
            if (meta) {
                var line = el('span', 'pl-meta');
                if (row.priv) { var lock = el('i', 'pl-lock'); lock.setAttribute('aria-hidden', 'true'); line.appendChild(lock); }       // (a small lock: drawn by the style, the words say it too)
                if (o.label) line.appendChild(el('span', 'pl-sr', WORD[row.at] + ': '));
                line.appendChild(el('span', '', meta));
                who.appendChild(line); li.className += ' has-sub';
            }
            put(li, [av, who]);
            if (o.join) {
                var b = button('sm pl-join', TEXT.join, function () { press(row.code); });
                b.setAttribute('aria-label', 'Join ' + possessive(row.name) + ' room, ' + meta); b.setAttribute('data-code', row.code);
                joins.push(b); li.appendChild(b);
            }
            return li;
        }
        function group(key, label, rows, own) {
            var s = el('section', 'pl-group'), h = el('h3', 'pl-gh'), mark = el('i', 'pl-gd'), ul = el('ul', 'pl-cards');
            s.setAttribute('data-g', key); mark.setAttribute('aria-hidden', 'true');
            put(h, [mark, el('span', '', label), el('span', 'pl-gn', String(rows.length))]);
            rows.forEach(function (r) { ul.appendChild(card(r, { label: key === 'mod', join: r.listed && r.code !== own })); });
            return put(s, [h, ul]);
        }
        function guests(n, cls) {                                        // "+ 4 guests, not logged in": counted, never named (in the list on a short window, under it otherwise)
            var g = el('div', 'pl-guests ' + cls), av = el('span', 'pl-gav', '…'), text = el('span', 'pl-gt');
            av.setAttribute('aria-hidden', 'true');
            put(text, [el('span', '', '+ ' + n + (n === 1 ? TEXT.guest : TEXT.guests)), el('span', 'pl-gsub', TEXT.notIn)]);
            return put(g, [av, text]);
        }
        function playingElsewhere() {                                    // the hub's row of this account says Playing (another tab is in a match) while this page is not: the visitor's card says Playing too
            var mine = me !== null ? model.rows[me] : null;
            return mine && mine.at === 'play' && !(doing && doing.at === 'play') ? mine : null;
        }
        function paintBody() {                                           // the part under the header: made again only when what it shows changed (the scroll and the focus stay where they were)
            var own = ownCode(), shown = listOf(model, me), people = 0;
            GROUPS.forEach(function (x) { shown[x[0]] = shown[x[0]].map(named); people += shown[x[0]].length; });
            var empty = model.n <= 1 && model.more === 0 && people === 0, mine = you ? { name: you.name, mod: you.mod, role: you.role, pic: you.pic, doing: doing, elsewhere: playingElsewhere() } : null;
            var sig = JSON.stringify([phase, mine, own, model.n, model.g, model.more, GROUPS.map(function (x) { return shown[x[0]].map(function (r) { return [r.id, r.name, r.at, r.mod, r.role, r.pic, r.pub, r.priv, r.solo, r.mapName, r.n, r.of, r.code, r.listed]; }); })]);
            if (sig === painted) return;
            var active = doc.activeElement, focused = active && active.getAttribute ? active.getAttribute('data-code') : null, scroll = list.scrollTop;
            painted = sig;
            clear(body); clear(list); clear(youList); clear(offBox); joins = [];
            if (phase === 'off') { put(offBox, [el('p', '', TEXT.off), el('p', 'small', TEXT.offNote)]); body.appendChild(offBox); told(); return; }
            if (you) {                                                   // the visitor, first, as the page knows it (not as the hub last said)
                var away = playingElsewhere(), row = rowOf(away ? { i: 0, name: you.name, at: 'play', mod: you.mod, role: you.role, pic: you.pic, pub: away.pub, priv: away.priv, solo: away.solo, map: away.map, n: away.n, of: away.of } : { i: 0, name: you.name, at: doing ? doing.at : 'look', mod: you.mod, role: you.role, pic: you.pic, pub: !!(doing && doing.pub), map: doing && doing.map, n: doing && doing.n, of: doing && doing.of }, Chat, rules);
                if (row) { row.mapName = row.mapName || row.guess; youList.appendChild(card(row, { you: true, label: true, join: false })); put(body, [youBox, rule]); }       // (the page's own room: its map's name is its own word)
            }
            body.appendChild(status);
            if (!empty) {                                                // (alone: the list stays blank under the visitor's own row)
                GROUPS.forEach(function (x) { if (shown[x[0]].length) list.appendChild(group(x[0], x[1], shown[x[0]], own)); });
                if (model.more > 0) list.appendChild(el('p', 'pl-more', TEXT.and + model.more + TEXT.more));
                if (model.g > 0) list.appendChild(guests(model.g, 'in-list'));
            }
            body.appendChild(list);
            if (!empty && model.g > 0) body.appendChild(guests(model.g, 'pinned'));
            list.scrollTop = scroll;
            if (focused) joins.forEach(function (b) { if (b.getAttribute('data-code') === focused && b.focus) b.focus(); });
            told();
        }
        function told() { drawnFns.forEach(function (fn) { try { fn(); } catch (e) { if (env.error) env.error(e); } }); }       // (the list was made again: whoever marks rows marks them again)
        function person(i) {                                             // a row of the list for the moderation sheet: { i, name, role, at, where }, or null (no such row now, the visitor's own, a person who is away: the hub takes a row only for an open socket, or the hub is not there)
            var row = whole(i, 0, LIMIT.ids) && live && supported ? model.rows[i] : null;
            return row && i !== me && row.at !== 'away' ? { i: row.id, name: row.name, role: row.role, at: row.at, where: whereOf(named(row)) } : null;
        }
        function paint() {
            var shown = phase !== 'none', on = phase === 'on';
            root.hidden = !shown;
            try { if (host.classList) { if (shown) host.classList.add('pl-on'); else host.classList.remove('pl-on'); } } catch (e) { /* the style needs it */ }
            panel.setAttribute('data-state', on ? 'on' : 'off');
            var counted = on && model.n > 1;                             // (just the visitor is no count)
            total.hidden = !counted; dot.hidden = on; total.textContent = countText(model.n); total.setAttribute('aria-label', countText(model.n) + TEXT.here);
            if (shown) paintBody();
        }

        // ---- what the visitor does ----------------------------------------------------------------------------------------------------------------------------------------------------------------
        function say(words) {                                            // a few words in place of a Join that finds no game
            status.textContent = words; stop('note');
            timers.note = setTimeout(guard(function () { timers.note = 0; status.textContent = ''; }), MS.note);
        }
        function press(code) {                                           // Join: the Games list's own button for that room does it (its questions, its notices); a list that is a popover is opened for it
            var sel = '.gjoin[data-code="' + code + '"]', b = find(sel), games;
            if (!b) {
                games = env.games;
                if (games && games.getAttribute('aria-haspopup') === 'dialog' && games.getAttribute('aria-expanded') !== 'true') { games.click(); b = find(sel); }
            }
            if (b) b.click(); else say(TEXT.gone);
        }

        // ---- what the page says, and what the hub says ----------------------------------------------------------------------------------------------------------------------------------------------
        function look() {
            var client = page.client ? page.client() : null, info = client && page.info ? page.info() : {};
            return announceOf(client, client && Home && Home.countOf ? Home.countOf(info.model) : null, !!(page.handing && page.handing()));
        }
        function tell() {                                                // the hub is told what the page does, when it changed and not oftener than every 3 seconds
            if (!live || !supported || !you) return;
            var said = look(), sig = JSON.stringify(said), wait = lastSent === null ? 0 : lastSent + MS.tell - now();
            if (sig === sentSig) return;
            if (wait > 0) return;                                        // (the next look, a second later, says it when the 3 seconds are over)
            var frame = { t: 'here' };
            for (var k in said) frame[k] = said[k];
            if (dock.send(frame)) { sentSig = sig; lastSent = now(); }
        }
        function keep() {                                                // the note for the game page: what this page says of its room, kept in this tab while it waits or plays (a page that goes to the game cannot write it at that moment), gone when there is no room
            if (!live || !supported) return;
            var note = doing ? noteOf(doing, !!you, ownCode()) : null, text = note ? JSON.stringify(note) : '', store = env.session;
            if (text === kept) return;
            try {
                store = store || win.sessionStorage;
                if (text) store.setItem('ants.here', text); else store.removeItem('ants.here');
                kept = text;
            } catch (e) { /* no storage: the game page asks the public list instead */ }
        }
        function ask() { if (live && supported && !asked && dock.send({ t: 'who' })) asked = true; }
        function sync() {                                                // once a second: the visitor's own row follows the page's room at once, the hub is told
            doing = look();
            keep(); tell(); ask();
            if (phase !== 'none') paint();
        }
        function clock() { timers.poll = setTimeout(guard(function () { timers.poll = 0; if (doc.hidden) return; sync(); clock(); }), MS.poll); }       // (a hidden page runs no loop: it starts again when the page is shown)
        if (doc.addEventListener) doc.addEventListener('visibilitychange', guard(function () { if (!doc.hidden && !timers.poll) { sync(); clock(); } }));
        dock.tap(guard(function (kind, m) {
            if (kind === 'live') {
                supported = !!m && m.presence === true; live = true; asked = false; sentSig = null; lastSent = null; model = fresh();
                me = whole(m && m.me, 0, LIMIT.ids) ? m.me : null;
                var yo = Chat.youOf(m && m.you);                   // (the dock's one rule for who the visitor is and what role they hold)
                you = yo ? { name: yo.name, mod: yo.mod, role: yo.role, pic: yo.pic } : null;
                if (!supported) phase = 'none'; else { doing = look(); keep(); tell(); ask(); }
            } else if (kind === 'you') {                                   // the role changed (the chat dock's `you` frame): the visitor's own row follows at once
                var y = m && m.you && typeof m.you === 'object' ? m.you : null;
                if (!you || !y) return;
                var yn = Chat.youOf(y);
                if (!yn) return;
                you.role = yn.role; you.mod = yn.mod; you.pic = yn.pic;
            } else if (kind === 'who') {
                var next = live && supported ? apply(model, m, Chat, rules) : null;
                if (!next) return;
                model = next; phase = 'on';
            } else if (kind === 'lost') {
                live = false; asked = false; model = fresh(); me = null;
                phase = supported === false ? 'none' : 'off';
            } else return;
            paint();
        }));
        doing = look();
        clock();
        return { root: root, list: list, state: function () { return phase; }, model: function () { return model; }, person: person, drawn: function (fn) { drawnFns.push(fn); }, sync: guard(sync) };
    }

    return { LIMIT: LIMIT, MS: MS, TEXT: TEXT, GROUPS: GROUPS, rowOf: rowOf, apply: apply, listOf: listOf, metaOf: metaOf, whereOf: whereOf, seatsText: seatsText, announceOf: announceOf, noteOf: noteOf, hashOf: hashOf, possessive: possessive, roleOf: roleOf, mount: mount };
});
