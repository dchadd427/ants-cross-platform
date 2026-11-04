// The players list of Home (docs/HOME_SCREEN.md, "The players list"): who has the front page open right now. A panel at the right of the Yard on a wide window; on a phone a button, "Players (N)", that opens
// the same panel as a sheet (a drawer on the right where the bar has no room). It speaks through the chat dock's socket (docs/HUB.md, "Presence": up {"t":"who"} and {"t":"here","at",...}, down {"t":"who",...}),
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
    var WIDE = '(min-width: 1180px) and (min-height: 501px)';             // the panel is docked at the right of the Yard (players.css has the same breakpoint); a narrower window has the button and the sheet
    var ROOM = /^[a-z0-9_-]{1,32}$/;
    var STATUS = ['look', 'wait', 'play'];
    var GROUPS = [['mod', 'Moderators'], ['look', 'Looking'], ['wait', 'Waiting'], ['play', 'Playing']];
    var WORD = { look: 'Looking', wait: 'Waiting', play: 'Playing' };
    var TEXT = {
        title: 'Players', here: ' here now', you: 'you', mod: 'mod', privateRoom: 'Private room', privateGame: 'Private game', publicRoom: 'Public room', inGame: 'In a game', join: 'Join', gone: 'That game is not open any more.',
        guest: ' guest', guests: ' guests', notIn: 'not logged in', more: ' more', and: 'and ', many: '500+', player: ' player', players: ' players', of: ' of ',
        empty: 'Just you here. Host a game!', emptyNote: 'People who log in with Discord show up here by name.', host: '+ Host a game',
        off: 'Players list is off right now', offNote: 'The chat hub is not answering. Games, rooms and the Yard work as always; this list comes back by itself.',
        fold: 'Fold the list', unfold: 'Open the list', close: 'Close the list', button: 'Players'
    };

    // ---- the pure parts ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    function whole(v, low, high) { return typeof v === 'number' && v === Math.floor(v) && v >= low && v <= high; }
    function possessive(name) { return name + (/s$/i.test(name) ? '’' : '’s'); }
    function initial(name, Chat) {                                       // the first code point of a name, for the circle
        var first = Chat.clip(name, 1);
        return first.length > 1 && first.charAt(first.length - 1) === '…' ? first.slice(0, -1) : first;
    }
    function fresh() { return { rows: Object.create(null), n: 0, g: 0, more: 0 }; }
    // One row of the hub's list, cleaned: { id, name, at, mod, pub, map, mapName, n, of, code }, or null when it is not a row. A room's details are kept for a public room only, a code for a room that waits only.
    function rowOf(raw, Chat, rules) {
        if (!raw || typeof raw !== 'object' || !whole(raw.i, 0, LIMIT.ids) || typeof raw.name !== 'string' || STATUS.indexOf(raw.at) === -1) return null;
        var name = Chat.clip(Chat.plain(raw.name.slice(0, LIMIT.name * 4), true), LIMIT.name);
        if (!name) return null;
        var row = { id: raw.i, name: name, at: raw.at, mod: raw.mod === true, pub: false, map: '', mapName: '', n: -1, of: -1, code: '' };
        if (raw.pub === true) {
            row.pub = true;
            if (typeof raw.map === 'string' && rules.validMapFile(raw.map)) {
                var known = rules.mapOfFile(raw.map);
                row.map = raw.map; row.mapName = known ? known.name : rules.communityName(raw.map).slice(0, LIMIT.map);
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
    // The rows in their groups, each by name; the visitor's own row is shown apart and is not among them
    function listOf(model, you) {
        var out = { mod: [], look: [], wait: [], play: [] };
        for (var id in model.rows) {
            var row = model.rows[id];
            if (!(you && row.name === you.name)) out[row.mod ? 'mod' : row.at].push(row);
        }
        GROUPS.forEach(function (g) { out[g[0]].sort(function (a, b) { return a.name < b.name ? -1 : a.name > b.name ? 1 : a.id - b.id; }); });
        return out;
    }
    function seatsText(row) {                                            // "Treasure . 2 of 4": the map, and the colors that play of those in the match
        var parts = [];
        if (row.mapName) parts.push(row.mapName);
        if (row.n >= 0 && row.of >= 1) parts.push(row.n + TEXT.of + row.of);
        return parts.join(' · ');
    }
    // The line under a name: where the person is. A person who looks has none (the group says it), except where `label` is set (the visitor's own row, a moderator's: no group says it there)
    function metaOf(row, label) {
        if (row.at === 'look') return label ? WORD.look : '';
        if (row.at === 'wait') return row.pub ? seatsText(row) || TEXT.publicRoom : TEXT.privateRoom;
        if (!row.pub) return TEXT.privateGame;
        return [row.mapName, row.n >= 0 ? row.n + (row.n === 1 ? TEXT.player : TEXT.players) : ''].filter(Boolean).join(' · ') || TEXT.inGame;
    }
    // What this page tells the hub it is doing, from its own room (client: the page's LobbyClient or null; count: Home's { people, size } of the room or null; handing: the page is on its way to a game).
    // Looking: no room. Waiting: a room that has not started. Playing: it has (or the page is on its way to it). A public room adds its map, seats and code; a private one adds nothing.
    function announceOf(client, count, handing) {
        var room = client && client.room;
        if (!room) return { at: 'look' };
        var say = { at: room.starting || handing || !room.lobby ? 'play' : 'wait' };
        if (room.isPublic === true) {
            say.pub = true;
            if (typeof room.map === 'string' && room.map) say.map = room.map;
            if (count && count.size >= 1 && count.people <= count.size) { say.n = count.people; say.of = count.size; }
            if (say.at === 'wait' && typeof client.code === 'string' && ROOM.test(client.code)) say.code = client.code;
        }
        return say;
    }

    // ---- the list ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // mount({ doc, win, host (Home's div), yard, bar (the bar's right side), quickHost and games (the bar's buttons), dock (the chat dock: tap(fn), send(obj)), page (the bridge: client(), info(), handing()), now, error }) returns { root, state, model, open, sync }, or null
    function mount(env) {
        var doc = env.doc, win = env.win, host = env.host, dock = env.dock, page = env.page || {};
        var Chat = win.AntsHomeChat, rules = win.AntsLobbyRules, Home = win.AntsHome;
        if (!dock || typeof dock.tap !== 'function' || !Chat || !rules || !doc.createElement) return null;
        var now = env.now || function () { return Date.now(); };
        var yard = env.yard || host;
        var phase = 'none', supported = null, live = false, asked = false, you = null, model = fresh();      // phase: 'none' (nothing shows), 'off' (the hub is not there), 'on'
        var open = false, opener = null, doing = null, sentSig = null, lastSent = null, painted = '', joins = [], timers = { poll: 0, note: 0 };

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
        function docked() { try { return !!win.matchMedia && win.matchMedia(WIDE).matches; } catch (e) { return false; } }
        function countText(n) { return n > LIMIT.here ? TEXT.many : String(n); }
        function find(selector) { try { return host.querySelector ? host.querySelector(selector) : null; } catch (e) { return null; } }
        function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }

        // The markup. The panel, the corner button and the scrim stand in the Yard (one box, `root`, that Home can make inert); the bar's own button stands at the right of the bar (a phone's first row).
        var panel = el('section', 'pl'); panel.id = 'players'; panel.setAttribute('aria-labelledby', 'players-title');
        panel.setAttribute('data-state', 'off'); panel.setAttribute('data-folded', 'false'); panel.setAttribute('data-sheet', 'false');
        var head = el('div', 'pl-head'), title = el('h2', 'pl-title', TEXT.title); title.id = 'players-title';
        var total = el('span', 'pill pl-count'), dot = el('i', 'pl-dot'), fold = button('pl-fold'), close = button('pl-fold pl-close');
        dot.setAttribute('aria-hidden', 'true'); total.hidden = true; dot.hidden = true;
        fold.appendChild(el('i', 'pl-mark')); fold.setAttribute('aria-controls', 'players-body');
        close.appendChild(el('i', 'pl-x')); close.setAttribute('aria-label', TEXT.close);
        put(head, [title, total, dot, fold, close]);
        var body = el('div', 'pl-body'), youBox = el('div', 'pl-you'), youList = el('ul', 'pl-cards'), rule = el('hr', 'pl-div'), status = el('p', 'pl-status'), list = el('div', 'pl-list'), offBox = el('div', 'pl-note');
        body.id = 'players-body'; youBox.appendChild(youList);
        status.setAttribute('role', 'status'); status.setAttribute('aria-live', 'polite');
        list.setAttribute('tabindex', '0'); list.setAttribute('role', 'region'); list.setAttribute('aria-label', TEXT.title);
        put(panel, [head, body]);
        var corner = button('pl-btn'), barBtn = button('pl-bar'), scrim = el('div', 'pl-scrim'), cornerPill = el('span', 'pill'), barPill = el('span', 'pill');
        [[corner, cornerPill], [barBtn, barPill]].forEach(function (p) {
            var icon = el('i', 'pl-ppl'), word = el('span', 'pl-word', TEXT.button);
            icon.setAttribute('aria-hidden', 'true'); p[1].hidden = true;
            put(p[0], [icon, word, p[1]]);
            p[0].setAttribute('aria-haspopup', 'dialog'); p[0].setAttribute('aria-expanded', 'false'); p[0].setAttribute('aria-label', TEXT.title);
            p[0].addEventListener('click', guard(function () { show(p[0]); }));
        });
        scrim.hidden = true;
        var root = el('div', 'pl-root'); root.hidden = true;
        put(root, [panel, corner, scrim]);
        yard.appendChild(root);
        barBtn.hidden = true;
        if (env.bar) env.bar.insertBefore(barBtn, env.bar.firstChild || null);

        // ---- what is drawn ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        function ownCode() { var c = page.client ? page.client() : null; return c && c.code ? c.code : ''; }
        function quick() { return env.quickHost; }
        function card(row, o) {                                          // o: { you: the visitor's own row, label: say where the person is under the name, join: a Join button (a public room that waits) }
            var li = el('li', 'pl-card'), who = el('span', 'pl-who'), name = el('span', 'pl-name'), av = el('span', 'pl-av', initial(row.name, Chat)), meta = metaOf(row, o.label);
            li.setAttribute('data-s', row.at);
            av.setAttribute('aria-hidden', 'true');
            if (row.mod) name.appendChild(el('span', 'pl-badge', TEXT.mod));
            name.appendChild(el('bdi', 'pl-nm', row.name));
            if (o.you) name.appendChild(el('span', 'pl-tag', TEXT.you));
            who.appendChild(name);
            if (meta) {
                var line = el('span', 'pl-meta');
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
            rows.forEach(function (r) { ul.appendChild(card(r, { label: key === 'mod', join: r.code !== '' && r.code !== own })); });
            return put(s, [h, ul]);
        }
        function guests(n, cls) {                                        // "+ 4 guests, not logged in": counted, never named (in the list on a short window, under it otherwise)
            var g = el('div', 'pl-guests ' + cls), av = el('span', 'pl-gav', '…'), text = el('span', 'pl-gt');
            av.setAttribute('aria-hidden', 'true');
            put(text, [el('span', '', '+ ' + n + (n === 1 ? TEXT.guest : TEXT.guests)), el('span', 'pl-gsub', TEXT.notIn)]);
            return put(g, [av, text]);
        }
        function paintBody() {                                           // the part under the header: made again only when what it shows changed (the scroll and the focus stay where they were)
            var q = quick(), own = ownCode(), shown = listOf(model, you), people = 0;
            GROUPS.forEach(function (x) { people += shown[x[0]].length; });
            var empty = model.n <= 1 && model.more === 0 && people === 0, mine = you ? { name: you.name, mod: you.mod, doing: doing } : null;
            var sig = JSON.stringify([phase, mine, own, !!(q && !q.disabled), model.n, model.g, model.more, GROUPS.map(function (x) { return shown[x[0]].map(function (r) { return [r.id, r.name, r.at, r.mod, r.pub, r.map, r.n, r.of, r.code]; }); })]);
            if (sig === painted) return;
            var active = doc.activeElement, focused = active && active.getAttribute ? active.getAttribute('data-code') : null, scroll = list.scrollTop;
            painted = sig;
            clear(body); clear(list); clear(youList); clear(offBox); joins = [];
            if (phase === 'off') { put(offBox, [el('p', '', TEXT.off), el('p', 'small', TEXT.offNote)]); body.appendChild(offBox); return; }
            if (you) {                                                   // the visitor, first, as the page knows it (not as the hub last said)
                var row = rowOf({ i: 0, name: you.name, at: doing ? doing.at : 'look', mod: you.mod, pub: !!(doing && doing.pub), map: doing && doing.map, n: doing && doing.n, of: doing && doing.of }, Chat, rules);
                if (row) { youList.appendChild(card(row, { you: true, label: true, join: false })); put(body, [youBox, rule]); }
            }
            body.appendChild(status);
            if (empty) {
                var n = el('div', 'pl-note'), hostBtn = button('pl-host', TEXT.host, function () { var b = quick(); hide(false); if (b && !b.disabled) b.click(); });
                hostBtn.disabled = !(q && !q.disabled);
                list.appendChild(put(n, [el('p', '', TEXT.empty), el('p', 'small', TEXT.emptyNote), hostBtn]));
            } else {
                GROUPS.forEach(function (x) { if (shown[x[0]].length) list.appendChild(group(x[0], x[1], shown[x[0]], own)); });
                if (model.more > 0) list.appendChild(el('p', 'pl-more', TEXT.and + model.more + TEXT.more));
                if (model.g > 0) list.appendChild(guests(model.g, 'in-list'));
            }
            body.appendChild(list);
            if (!empty && model.g > 0) body.appendChild(guests(model.g, 'pinned'));
            list.scrollTop = scroll;
            if (focused) joins.forEach(function (b) { if (b.getAttribute('data-code') === focused && b.focus) b.focus(); });
        }
        function paint() {
            var shown = phase !== 'none', on = phase === 'on';
            root.hidden = !shown; barBtn.hidden = !shown;
            try { if (host.classList) { if (shown) host.classList.add('pl-on'); else host.classList.remove('pl-on'); } } catch (e) { /* the style needs it */ }
            panel.setAttribute('data-state', on ? 'on' : 'off');
            var counted = on && model.n > 1;                             // (just the visitor is no count)
            total.hidden = !counted; dot.hidden = on; total.textContent = countText(model.n); total.setAttribute('aria-label', countText(model.n) + TEXT.here);
            [[cornerPill, corner], [barPill, barBtn]].forEach(function (p) {
                p[0].hidden = !counted; p[0].textContent = countText(model.n);
                p[1].setAttribute('aria-label', counted ? TEXT.title + ' (' + countText(model.n) + ')' : TEXT.title);
            });
            if (shown) paintBody();
        }

        // ---- what the visitor does ----------------------------------------------------------------------------------------------------------------------------------------------------------------
        function tabbable() {                                            // the buttons and the list that the sheet shows, in order
            var all = panel.querySelectorAll ? panel.querySelectorAll('button, [tabindex="0"]') : [], out = [];
            for (var i = 0; i < all.length; i++) if (!all[i].disabled && all[i].getClientRects && all[i].getClientRects().length) out.push(all[i]);
            return out;
        }
        function show(from) {                                            // the panel as a sheet (a phone) or a drawer (a window that is not wide), over a scrim: its button, the scrim and Escape close it
            if (open || docked()) return;
            open = true; opener = from || null;
            panel.setAttribute('data-sheet', 'true'); scrim.hidden = false;
            [corner, barBtn].forEach(function (b) { b.setAttribute('aria-expanded', 'true'); });
            panel.setAttribute('role', 'dialog'); panel.setAttribute('aria-modal', 'true');
            if (close.focus) close.focus();
        }
        function hide(back) {                                          // back: the focus goes back to the button that opened it
            if (!open) return;
            open = false;
            panel.setAttribute('data-sheet', 'false'); scrim.hidden = true;
            [corner, barBtn].forEach(function (b) { b.setAttribute('aria-expanded', 'false'); });
            panel.removeAttribute('role'); panel.removeAttribute('aria-modal');
            var to = opener; opener = null;
            if (back && to && to.focus) to.focus();
        }
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
            hide(false);
            if (b) b.click(); else say(TEXT.gone);
        }
        function setFold(on) { panel.setAttribute('data-folded', on ? 'true' : 'false'); fold.setAttribute('aria-expanded', on ? 'false' : 'true'); fold.setAttribute('aria-label', on ? TEXT.unfold : TEXT.fold); }
        setFold(false);
        fold.addEventListener('click', guard(function () { setFold(panel.getAttribute('data-folded') !== 'true'); }));
        close.addEventListener('click', guard(function () { hide(true); }));
        scrim.addEventListener('click', guard(function () { hide(true); }));
        panel.addEventListener('keydown', guard(function (e) {
            if (!e) return;
            if (e.key === 'Escape') { if (open) hide(true); else if (docked()) { setFold(true); if (fold.focus) fold.focus(); } return; }
            if (e.key !== 'Tab' || !open) return;                        // (a sheet is a dialog: the focus stays in it)
            var items = tabbable(), first = items[0], last = items[items.length - 1], active = doc.activeElement;
            if (e.shiftKey && active === first && last) { if (e.preventDefault) e.preventDefault(); last.focus(); }
            else if (!e.shiftKey && active === last && first) { if (e.preventDefault) e.preventDefault(); first.focus(); }
        }));
        doc.addEventListener('keydown', guard(function (e) { if (e && e.key === 'Escape' && open) hide(true); }));
        win.addEventListener('resize', guard(function () { if (open && docked()) hide(false); }));

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
        function ask() { if (live && supported && !asked && dock.send({ t: 'who' })) asked = true; }
        function sync() {                                                // once a second: the visitor's own row follows the page's room at once, the hub is told, the Host button follows Quick host
            doing = look();
            tell(); ask();
            if (phase !== 'none') paint();
        }
        function clock() { timers.poll = setTimeout(guard(function () { timers.poll = 0; sync(); clock(); }), MS.poll); }
        dock.tap(guard(function (kind, m) {
            if (kind === 'live') {
                var name = m && m.you && typeof m.you === 'object' && typeof m.you.name === 'string' ? Chat.clip(Chat.plain(m.you.name.slice(0, LIMIT.name * 4), true), LIMIT.name) : '';
                supported = !!m && m.presence === true; live = true; asked = false; sentSig = null; lastSent = null; model = fresh();
                you = name ? { name: name, mod: m.you.mod === true } : null;
                if (!supported) phase = 'none'; else { doing = look(); tell(); ask(); }
            } else if (kind === 'who') {
                var next = live && supported ? apply(model, m, Chat, rules) : null;
                if (!next) return;
                model = next; phase = 'on';
            } else if (kind === 'lost') {
                live = false; asked = false; model = fresh();
                phase = supported === false ? 'none' : 'off';
            } else if (kind === 'shown') { if (!m) hide(false); return; }          // (the Room shows: a sheet that was open is closed)
            else return;
            paint();
        }));
        doing = look();
        clock();
        return { root: root, state: function () { return phase; }, model: function () { return model; }, open: function (on) { if (on) show(null); else hide(false); }, sheet: function () { return open; }, sync: guard(sync) };
    }

    return { LIMIT: LIMIT, MS: MS, TEXT: TEXT, GROUPS: GROUPS, WIDE: WIDE, rowOf: rowOf, apply: apply, listOf: listOf, metaOf: metaOf, seatsText: seatsText, announceOf: announceOf, possessive: possessive, mount: mount };
});
