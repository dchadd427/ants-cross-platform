// Runs the players list of Home (web/home/players.js, docs/HOME_SCREEN.md "The players list") against a FAKE chat dock (the hub's frames come from the test; what the page sends goes into a list) and a fake
// clock, in the fake browser of tests/scripts/web_page_harness.js (its DOM). The page itself (web/lobby.html with web/home/home.js and the real chat dock) is section 7. What it checks:
//   1. the pure parts: a row of the hub's list cleaned (hostile names, a map that the page's own rule accepts, numbers, a code only for a room that waits), the list that a frame makes, the groups, the words
//      under a name, and what the page tells the hub it is doing (looking, waiting, playing; a public room's details and never a private room's);
//   2. the phases: nothing until the hub has said the list, nothing for a hub that has none, "off" for a hub that is gone, back by itself, the count (not for one person alone), the class that Home carries;
//   3. what the page tells the hub: `here` (a named visitor) and `who` once for each socket, when the room changes and not oftener than every 3 seconds, again after a lost link, nothing for a guest;
//   4. what is drawn: the groups in order with their counts, the visitor first and as the page knows it, the moderator mark, the guests, the empty note and its Host button, a Join where a public room waits
//      (the Games list's own Join does it, a list that is a popover is opened for it, a game that is gone is said), "and N more", every line as text;
//   5. the sheet: opened by the bar's button or the corner's, closed by its button, the scrim and Escape with the focus back where it was, closed when the Room shows or the window grows wide; the fold;
//   6. what the hub may send: unknown kinds, a wrong frame, a hostile name, a flood of rows: nothing breaks, nothing becomes markup;
//   7. the page (web/lobby.html with web/home/home.js): the list is loaded once, after the dock, a missing or broken one changes nothing, the hand-off makes it inert;
//   8. the source of the script: no markup, no storage, no link, no address.
// tests/scripts/test_web_home_players.py runs this with node (the quick tier). usage: node web_home_players_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
globalThis.AntsShapes = require(path.resolve(__dirname, '..', '..', 'web', 'shapes.js'));

const lobbyPath = process.argv[2];
if (!lobbyPath) { console.log('usage: web_home_players_check.js lobby.html'); process.exit(2); }
const lobbyDir = path.dirname(path.resolve(lobbyPath));
const playersPath = path.join(lobbyDir, 'home', 'players.js');
const playersSource = fs.readFileSync(playersPath, 'utf8');
const Pure = require(playersPath);
const Chat = require(path.join(lobbyDir, 'home', 'chat.js'));
const Home = require(path.join(lobbyDir, 'home', 'home.js'));
const H = require('./web_page_harness.js')(lobbyPath);
const { El, runLobby, KEY, E, C, roomMessage } = H;
const Rules = H.RULES0;

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function same(label, got, want) {
    const a = JSON.stringify(got);
    const b = JSON.stringify(want);
    check(label, a === b, 'got ' + a + ', wanted ' + b);
}
const cls = (e) => String(e.className).split(/\s+/);
const byClass = (root, c) => root.findAll((e) => cls(e).indexOf(c) !== -1);
const one = (root, c) => byClass(root, c)[0] || null;
const texts = (root, c) => byClass(root, c).map((e) => e.textContent);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the fakes: a clock, the chat dock (its taps and the frames that the list sends), the page
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function makeClock() {
    const c = { t: 0, timers: [], next: 1 };
    c.setTimeout = (fn, ms) => { const id = c.next++; c.timers.push({ id, at: c.t + (ms || 0), fn }); return id; };
    c.clearTimeout = (id) => { c.timers = c.timers.filter((x) => x.id !== id); };
    c.advance = (ms) => {
        const until = c.t + ms;
        for (let turns = 0; ; turns++) {
            if (turns > 20000) throw new Error('a storm of timers');
            const due = c.timers.filter((x) => x.at <= until).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            c.timers = c.timers.filter((x) => x !== due);
            c.t = Math.max(c.t, due.at);
            due.fn();
        }
        c.t = until;
    };
    c.now = () => c.t;
    return c;
}
function loadPlayers(clock) {
    const root = {};
    new Function('self', 'setTimeout', 'clearTimeout', playersSource)(root, clock.setTimeout, clock.clearTimeout);
    return root.AntsHomePlayers;
}
const W = (o) => Object.assign({ t: 'welcome', v: 1, you: { name: 'maple', mod: false }, login: true, presence: true, me: 1, online: 2, history: [] }, o);
const row = (i, name, at, more) => Object.assign({ i, name, at: at || 'look' }, more || {});
const FULL = (rows, o) => Object.assign({ t: 'who', full: true, n: rows.length + 1, g: 0, more: 0, rows }, o);
const MAP_OF = { 'TREASURE.LVL': 'Treasure' };
// the page's room, as the page's LobbyClient has it (client.room: a Room message of the server, decoded), and Home's count of it
const ROOM = (o) => Object.assign({ lobby: true, starting: false, isPublic: false, map: 'TREASURE.LVL', slots: [] }, o);
const CLIENT = (room, code, guest) => ({ room, code: code || 'c1v3rr', isLeader: () => !guest });

// one list on a fake page: o.width and o.height (the window), o.wide (the window is a wide one: the panel is docked), o.client, o.count (Home's count of the room), o.handing, o.noMedia, o.noQuick, o.popover
function rig(o) {
    o = o || {};
    const env = { elements: {}, innerHTMLWrites: [] };
    const clock = makeClock();
    const Players = loadPlayers(clock);
    const doc = { hidden: false, listeners: {}, activeElement: null, createElement: (t) => new El(env, t), getElementById: () => null, addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); } };
    env.doc = doc;
    const state = { wide: !!o.wide, client: o.client || null, count: o.count || null, handing: !!o.handing };
    const win = { innerWidth: o.width || 1280, innerHeight: o.height || 800, listeners: {}, addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); }, AntsHomeChat: Chat, AntsLobbyRules: Rules, AntsHome: Object.assign({}, Home, { countOf: () => state.count }) };
    if (!o.noMedia) win.matchMedia = (q) => ({ matches: q === Players.WIDE ? state.wide : false });
    const host = new El(env, 'div', 'home'), yard = new El(env, 'main', 'yard'), right = new El(env, 'div'), quick = new El(env, 'button'), gamesButton = new El(env, 'button');
    right.className = 'hright'; right.appendChild(new El(env, 'button')); quick.disabled = !!o.noQuick;
    host.appendChild(yard);
    const pressed = [], found = {};
    host.querySelector = (sel) => found[sel] || null;
    quick.click = () => pressed.push('quick host');
    const sent = [], taps = [], errors = [], link = { open: true };
    const dock = { tap(fn) { taps.push(fn); }, send(obj) { if (!link.open) return false; sent.push(JSON.parse(JSON.stringify(obj))); return true; } };
    const page = { client: () => state.client, info: () => ({ client: !!state.client, model: null }), handing: () => state.handing };
    const list = Players.mount({ doc, win, host, dock, page, yard, bar: right, quickHost: quick, games: gamesButton, now: clock.now, error: (e) => errors.push(e) });
    const r = { env, clock, Players, doc, win, host, yard, right, quick, games: gamesButton, list, dock, sent, taps, errors, state, pressed, found, link };
    r.emit = (kind, m) => taps.forEach((fn) => fn(kind, m));
    r.live = (welcome) => { r.link.open = true; r.emit('live', welcome || W({})); return r; };
    r.who = (frame) => { r.emit('who', frame); return r; };
    r.lost = () => { r.emit('lost'); return r; };
    r.root = list && list.root;
    r.panel = () => one(r.root, 'pl');
    r.hereFrames = () => sent.filter((m) => m.t === 'here');
    r.whoFrames = () => sent.filter((m) => m.t === 'who');
    r.title = () => one(r.panel(), 'pl-title').textContent;
    r.count = () => { const p = one(r.panel(), 'pl-count'); return p.hidden ? '' : p.textContent; };
    r.groups = () => byClass(r.panel(), 'pl-group').map((g) => ({ g: g.getAttribute('data-g'), n: one(g, 'pl-gn').textContent, names: texts(g, 'pl-nm') }));
    r.names = () => byClass(r.panel(), 'pl-card').map((c) => one(c, 'pl-nm').textContent);
    r.metaOf = (name) => { const c = byClass(r.panel(), 'pl-card').find((x) => one(x, 'pl-nm').textContent === name); const m = c && one(c, 'pl-meta'); return m ? m.textContent : null; };
    r.joins = () => byClass(r.panel(), 'pl-join');
    r.corner = () => one(r.root, 'pl-btn');
    r.barButton = () => one(r.right, 'pl-bar');
    r.sheetOpen = () => r.panel().getAttribute('data-sheet') === 'true';
    r.tick = (ms) => r.clock.advance(ms === undefined ? 1000 : ms);
    r.listed = (code, name) => {                                     // the Games list has a row for that room: its Join button (the list's own) and the map's name that it shows
        const item = new El(env, 'li'), top = new El(env, 'span'), label = new El(env, 'span'), b = new El(env, 'button');
        item.className = 'gitem'; label.className = 'gmapname'; label.textContent = name; b.className = 'gjoin'; b.setAttribute('data-code', code);
        top.appendChild(label); item.appendChild(top); item.appendChild(b);
        found['.gjoin[data-code="' + code + '"]'] = b;
        return b;
    };
    r.unlisted = (code) => { delete found['.gjoin[data-code="' + code + '"]']; };
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The pure parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Pure;
    const rowOf = (raw) => P.rowOf(raw, Chat, Rules);
    same('a plain row: the name, where, no mark, no details', rowOf({ i: 4, name: 'sage', at: 'look' }), { id: 4, name: 'sage', at: 'look', mod: false, pub: false, map: '', mapName: '', guess: '', n: -1, of: -1, code: '' });
    same('the mark of a moderator is exactly true (a string or a number is none)', [rowOf(row(1, 'a1', 'look', { mod: true })).mod, rowOf(row(1, 'a1', 'look', { mod: 'true' })).mod, rowOf(row(1, 'a1', 'look', { mod: 1 })).mod], [true, false, false]);
    same('a name loses the invisible and bidirectional characters and is cut at 32 code points', [rowOf(row(1, '‮alice​⁦')).name, rowOf(row(1, 'x'.repeat(40))).name], ['alice', 'x'.repeat(32) + '…']);
    same('markup in a name stays text', rowOf(row(1, '<img src=x onerror=1>')).name, '<img src=x onerror=1>');
    same('a name that is nothing after the cleaning, or no name, is no row', [rowOf(row(1, '​‮')), rowOf(row(1, '')), rowOf({ i: 1, at: 'look' }), rowOf({ i: 1, name: 7, at: 'look' })], [null, null, null, null]);
    same('a status that is not one of the three is no row (also one that is a name of Object)', [rowOf(row(1, 'ab', 'away')), rowOf(row(1, 'ab', 'constructor')), rowOf(row(1, 'ab', '__proto__')), rowOf({ i: 1, name: 'ab', at: ['wait'] }), rowOf({ i: 1, name: 'ab' })], [null, null, null, null, null]);
    same('a number that is not a whole number from 0 is no row', [rowOf(row(-1, 'ab')), rowOf(row(1.5, 'ab')), rowOf(row('1', 'ab')), rowOf(row(1e12, 'ab')), rowOf(row(NaN, 'ab')), rowOf(null), rowOf('row'), rowOf([])], [null, null, null, null, null, null, null, null]);
    const pub = rowOf(row(5, 'clover', 'wait', { pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr' }));
    same('a public room that waits: the map (by the page\'s own name for it: an original\'s), the seats and the code', [pub.pub, pub.map, pub.mapName, pub.guess, pub.n, pub.of, pub.code], [true, 'TREASURE.LVL', 'Treasure', '', 2, 4, 'c1v3rr']);
    same('a map of the players has no name of its own: the name that it would have (its file name without the ending, cut at 28) is a guess, drawn only where the Games list says the same', [rowOf(row(5, 'ab', 'wait', { pub: true, map: 'My Map.LVL' })).mapName, rowOf(row(5, 'ab', 'wait', { pub: true, map: 'My Map.LVL' })).guess, rowOf(row(5, 'ab', 'wait', { pub: true, map: 'A'.repeat(40) + '.LVL' })).guess.length], ['', 'My Map', 28]);
    same('a map with two spaces in its name is a map (the game takes it)', [rowOf(row(5, 'ab', 'wait', { pub: true, map: 'Bomber Match  by PRo.lvl' })).map, rowOf(row(5, 'ab', 'wait', { pub: true, map: 'Bomber Match  by PRo.lvl' })).guess], ['Bomber Match  by PRo.lvl', 'Bomber Match  by PRo']);
    same('a map that the page\'s rule refuses is none, whatever it is (a path, no ending, markup, a control character, too long)', ['../x.LVL', 'TREASURE', '<b>.LVL', 'a\u0007b.LVL', 'x'.repeat(70) + '.LVL', '.hidden.LVL', 7, null].map((m) => rowOf(row(5, 'ab', 'wait', { pub: true, map: m })).map), ['', '', '', '', '', '', '', '']);
    same('seats: 0 to 4 taken of 1 to 4, and of is not less than n; anything else is none', [rowOf(row(5, 'ab', 'wait', { pub: true, n: 5 })).n, rowOf(row(5, 'ab', 'wait', { pub: true, n: -1 })).n, rowOf(row(5, 'ab', 'wait', { pub: true, n: '2' })).n, rowOf(row(5, 'ab', 'wait', { pub: true, of: 0 })).of, rowOf(row(5, 'ab', 'wait', { pub: true, n: 3, of: 2 })).of, rowOf(row(5, 'ab', 'wait', { pub: true, n: 0, of: 4 })).n], [-1, -1, -1, -1, -1, 0]);
    same('a code is kept for a room that waits and has the shape of a code (lower case, - and _, at most 32)', ['c1v3rr', 'a-b_c', 'ABC', '', 'x'.repeat(33), 'a b', 'a/b', 5].map((c) => rowOf(row(5, 'ab', 'wait', { pub: true, code: c })).code), ['c1v3rr', 'a-b_c', '', '', '', '', '', '']);
    same('no code for a match or for a visitor who looks; no details at all for a room that is not public', [rowOf(row(5, 'ab', 'play', { pub: true, code: 'c1v3rr', map: 'TREASURE.LVL', n: 2 })).code, rowOf(row(5, 'ab', 'look', { pub: true, code: 'c1v3rr' })).code, rowOf(row(5, 'ab', 'wait', { code: 'c1v3rr', map: 'TREASURE.LVL', n: 2, of: 4 })).code, rowOf(row(5, 'ab', 'wait', { pub: false, code: 'c1v3rr', map: 'TREASURE.LVL' })).map, rowOf(row(5, 'ab', 'wait', { pub: 'yes', code: 'c1v3rr' })).code], ['', '', '', '', '']);

    const apply = (m, model) => P.apply(model || null, m, Chat, Rules);
    const rowsOf = (model) => Object.keys(model.rows).map((k) => model.rows[k].name);
    const first = apply(FULL([row(1, 'a1'), row(2, 'b2')], { n: 3, g: 1 }));
    same('a whole list: its rows and its numbers', [rowsOf(first), first.n, first.g, first.more], [['a1', 'b2'], 3, 1, 0]);
    const second = apply({ t: 'who', full: false, n: 3, g: 1, more: 0, rows: [row(2, 'b2', 'wait'), row(3, 'c3')], gone: [1] }, first);
    same('a change upserts its rows and drops the numbers that are gone (and the old list is not changed)', [rowsOf(second), second.rows[2].at, rowsOf(first)], [['b2', 'c3'], 'wait', ['a1', 'b2']]);
    same('a change that names a number that is not here, or is no number, drops nothing', [rowsOf(apply({ t: 'who', n: 2, g: 0, more: 0, rows: [], gone: [99, 'x', -1, 1.5, null] }, first))], [['a1', 'b2']]);
    same('a whole list replaces what the page has; so does a frame that does not say full: it is a change', [rowsOf(apply(FULL([row(7, 'z7')]), first)), rowsOf(apply({ t: 'who', full: 'yes', n: 2, g: 0, more: 0, rows: [row(7, 'z7')] }, first))], [['z7'], ['a1', 'b2', 'z7']]);
    same('a frame with a wrong part is no frame: the count (negative, fractional, text, huge, missing), the rows (not a list), the gone (not a list), more than 200 rows', [
        apply({ t: 'who', full: true, n: -1, g: 0, more: 0, rows: [] }), apply({ t: 'who', full: true, n: 1.5, g: 0, more: 0, rows: [] }), apply({ t: 'who', full: true, n: '2', g: 0, more: 0, rows: [] }), apply({ t: 'who', full: true, n: 1e12, g: 0, more: 0, rows: [] }),
        apply({ t: 'who', full: true, g: 0, more: 0, rows: [] }), apply({ t: 'who', full: true, n: 1, g: 0, more: 0, rows: 'a1' }), apply({ t: 'who', full: false, n: 1, g: 0, more: 0, rows: [], gone: 'x' }),
        apply(FULL(Array.from({ length: 201 }, (_, i) => row(i, 'p' + i)))), apply(null), apply('who'), apply([])], Array(11).fill(null));
    check('a frame of more rows than the hub sends is no frame, whatever the rows are (250 of them that are none)', apply(FULL(Array.from({ length: 250 }, () => null))) === null && apply(FULL(Array.from({ length: 200 }, () => null))) !== null);
    check('a frame with a count of zero is a frame (guests 0 and more 0 are numbers)', apply(FULL([], { n: 0, g: 0, more: 0 })) !== null);
    check('a frame whose change would make more than 200 rows is no frame', apply({ t: 'who', n: 1, g: 0, more: 0, rows: Array.from({ length: 5 }, (_, i) => row(1000 + i, 'q' + i)) }, apply(FULL(Array.from({ length: 198 }, (_, i) => row(i, 'p' + i))))) === null);
    same('rows that are not rows are left out of a frame, the others stay', rowsOf(apply(FULL([row(1, 'a1'), { i: 2 }, null, 'x', row(3, 'c3', 'away'), row(4, 'd4')]))), ['a1', 'd4']);

    const model = apply(FULL([row(1, 'zed', 'wait'), row(2, 'amy'), row(3, 'moddy', 'play', { mod: true }), row(4, 'bob', 'play'), row(5, 'maple'), row(6, 'cat')]));
    const groups = P.listOf(model, 5);
    same('the groups: moderators apart whatever they do, the rest by status, each by name, the visitor\'s own row (by its number) not among them', ['mod', 'look', 'wait', 'play'].map((g) => groups[g].map((r) => r.name)), [['moddy'], ['amy', 'cat'], ['zed'], ['bob']]);
    same('with no row that is the visitor\'s every row is listed (a visitor that the list does not hold, a hub that did not say)', ['mod', 'look', 'wait', 'play'].map((g) => P.listOf(model, null)[g].length), [1, 3, 1, 1]);
    const twins = apply(FULL([row(1, 'maple'), row(2, 'maple'), row(3, 'sage')]));
    same('a person with the visitor\'s name is not the visitor: only the row that is the visitor\'s number is left out', [P.listOf(twins, 2).look.map((r) => r.id), P.listOf(twins, 1).look.map((r) => r.id)], [[1, 3], [2, 3]]);

    const m = (o) => Object.assign({ at: 'look', pub: false, mapName: '', n: -1, of: -1 }, o);
    same('the line under a name: nothing for one who looks (the visitor and a moderator are told where), a room by its map and seats; "In a room" where the hub says nothing of the room (it keeps no details of a private room, nor of the room of an account that may not chat: it is not called private), and "Private room" only on the visitor\'s own row', [
        P.metaOf(m({}), false), P.metaOf(m({}), true), P.metaOf(m({ at: 'wait' }), false), P.metaOf(m({ at: 'wait' }), true), P.metaOf(m({ at: 'wait' }), true, true), P.metaOf(m({ at: 'wait', pub: true }), false), P.metaOf(m({ at: 'wait', pub: true, mapName: 'Treasure', n: 2, of: 4 }), false), P.metaOf(m({ at: 'wait', pub: true, mapName: 'Treasure' }), false), P.metaOf(m({ at: 'wait', pub: true, n: 1, of: 2 }), false), P.metaOf(m({ at: 'wait', pub: true }), true, true)],
        ['', 'Looking', 'In a room', 'In a room', 'Private room', 'Public room', 'Treasure · 2 of 4', 'Treasure', '1 of 2', 'Public room']);
    same('... a match by its map and its players, "In a game" when nothing is known or the hub says nothing of it, and "Private game" only on the visitor\'s own row', [P.metaOf(m({ at: 'play', pub: true, mapName: 'Islands', n: 3 }), false), P.metaOf(m({ at: 'play', pub: true, mapName: 'Islands', n: 1 }), false), P.metaOf(m({ at: 'play', pub: true }), false), P.metaOf(m({ at: 'play' }), false), P.metaOf(m({ at: 'play' }), true), P.metaOf(m({ at: 'play' }), true, true), P.metaOf(m({ at: 'play', pub: true, n: 0 }), false)], ['Islands · 3 players', 'Islands · 1 player', 'In a game', 'In a game', 'In a game', 'Private game', '0 players']);
    same('a name in the possessive: "Sage’s", "Tess’" after an s', [P.possessive('sage'), P.possessive('tess'), P.possessive('Tess')], ['sage’s', 'tess’', 'Tess’']);

    const say = P.announceOf;
    same('no room: looking', [say(null, null, false), say({}, null, false), say({ room: null }, null, false)], [{ at: 'look' }, { at: 'look' }, { at: 'look' }]);
    same('a private room that waits says only that: no map, no seats, no code', say(CLIENT(ROOM({ isPublic: false })), { people: 2, size: 4 }, false), { at: 'wait' });
    same('a public room that waits says its map, seats and code', say(CLIENT(ROOM({ isPublic: true })), { people: 2, size: 4 }, false), { at: 'wait', pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr' });
    same('a guest of a public room says its map and seats and not the code (the host\'s row has the Join)', say(CLIENT(ROOM({ isPublic: true }), 'c1v3rr', true), { people: 2, size: 4 }, false), { at: 'wait', pub: true, map: 'TREASURE.LVL', n: 2, of: 4 });
    same('a client that cannot say whether it leads says no code', say({ room: ROOM({ isPublic: true }), code: 'c1v3rr' }, null, false), { at: 'wait', pub: true, map: 'TREASURE.LVL' });
    same('a public room whose seats are not known yet (or are more than the room) says the rest', [say(CLIENT(ROOM({ isPublic: true })), null, false), say(CLIENT(ROOM({ isPublic: true })), { people: 5, size: 4 }, false)], [{ at: 'wait', pub: true, map: 'TREASURE.LVL', code: 'c1v3rr' }, { at: 'wait', pub: true, map: 'TREASURE.LVL', code: 'c1v3rr' }]);
    same('a match that is starting, a page on its way to a game, a room that a game made: playing; a match has no code, a private one says nothing', [
        say(CLIENT(ROOM({ isPublic: true, starting: true })), { people: 3, size: 4 }, false), say(CLIENT(ROOM({ isPublic: true })), { people: 3, size: 4 }, true), say(CLIENT(ROOM({ lobby: false })), null, false), say(CLIENT(ROOM({ starting: true })), { people: 3, size: 4 }, false)],
        [{ at: 'play', pub: true, map: 'TREASURE.LVL', n: 3, of: 4 }, { at: 'play', pub: true, map: 'TREASURE.LVL', n: 3, of: 4 }, { at: 'play' }, { at: 'play' }]);
    same('a code that is not the shape of one is not said', say({ room: ROOM({ isPublic: true }), code: 'ABC DEF', isLeader: () => true }, null, false), { at: 'wait', pub: true, map: 'TREASURE.LVL' });
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. The phases: nothing, the list, off, back
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({ wide: true });
    check('mounted: the list taps the dock once and shows nothing (no panel, no button, no class on Home) until the hub has said the list', r.taps.length === 1 && r.list.state() === 'none' && r.root.hidden === true && r.barButton().hidden === true && cls(r.host).indexOf('pl-on') === -1);
    check('... the box that Home can make inert is in the Yard, the bar\'s button is first in the bar\'s right side', r.root.parent === r.yard && r.right.children[0] === r.barButton() && r.right.children.length === 2);
    r.live();
    check('the welcome alone shows nothing (the list is not here yet: no flash of "just you")', r.list.state() === 'none' && r.root.hidden === true);
    r.who(FULL([row(1, 'maple'), row(2, 'sage')], { n: 3, g: 1 }));
    check('the first list shows it: the panel, the buttons, the class on Home', r.list.state() === 'on' && r.root.hidden === false && r.barButton().hidden === false && cls(r.host).indexOf('pl-on') !== -1 && r.panel().getAttribute('data-state') === 'on');
    same('the title, the count (everybody, the visitor and the guests too: the chat says the same) and the buttons\' names', [r.title(), r.count(), r.corner().getAttribute('aria-label'), r.barButton().getAttribute('aria-label'), one(r.panel(), 'pl-count').getAttribute('aria-label')], ['Players', '3', 'Players (3)', 'Players (3)', '3 here now']);
    same('both buttons say what they open and that it is closed', [r.corner().getAttribute('aria-haspopup'), r.corner().getAttribute('aria-expanded'), r.barButton().getAttribute('aria-expanded')], ['dialog', 'false', 'false']);
    r.who(FULL([row(1, 'maple')], { n: 1, g: 0 }));
    check('one person alone is no count (no number in the header or on the buttons)', r.count() === '' && one(r.corner(), 'pill').hidden === true && one(r.barButton(), 'pill').hidden === true && r.corner().getAttribute('aria-label') === 'Players');
    r.who(FULL([row(1, 'maple')], { n: 600, g: 599 }));
    r.who(FULL([row(1, 'maple')], { n: 99, g: 98 }));
    same('up to 99 the header and the buttons say the number', [r.count(), one(r.corner(), 'pill').textContent, one(r.barButton(), 'pill').textContent], ['99', '99', '99']);
    r.who(FULL([row(1, 'maple')], { n: 100, g: 99 }));
    same('over 99 the buttons say "99+" (they are small: the bar of a phone has no room for more) and their names say the number; the header says it too', [one(r.corner(), 'pill').textContent, one(r.barButton(), 'pill').textContent, r.corner().getAttribute('aria-label'), r.barButton().getAttribute('aria-label'), r.count(), one(r.panel(), 'pl-count').getAttribute('aria-label')],
        ['99+', '99+', 'Players (100)', 'Players (100)', '100', '100 here now']);
    r.who(FULL([row(1, 'maple')], { n: 600, g: 599 }));
    same('over 500 people the header says "500+" and the buttons "99+"', [r.count(), one(r.corner(), 'pill').textContent, one(r.barButton(), 'pill').textContent], ['500+', '99+', '99+']);
    r.who(FULL([row(1, 'maple')], { n: 99999, g: 99998 }));
    same('a count that no hub has: the same words', [r.count(), one(r.barButton(), 'pill').textContent], ['500+', '99+']);
    r.lost();
    check('the hub is gone: "off" (the panel stays, the count goes, a grey dot and the words come), the rows are not kept', r.list.state() === 'off' && r.panel().getAttribute('data-state') === 'off' && r.count() === '' && one(r.panel(), 'pl-dot').hidden === false && texts(r.panel(), 'pl-note')[0].indexOf('Players list is off right now') === 0
        && r.names().length === 0 && Object.keys(r.list.model().rows).length === 0 && r.root.hidden === false);
    check('... the note says that the rest works and that the list comes back by itself', /Games, rooms and the Yard work as always; this list comes back by itself\./.test(texts(r.panel(), 'pl-note')[0]));
    r.live(); r.who(FULL([row(1, 'maple'), row(3, 'fern')], { n: 2 }));
    check('the hub is back: the list is on again, with the new rows', r.list.state() === 'on' && r.names().join() === 'maple,fern' && r.count() === '2' && one(r.panel(), 'pl-dot').hidden === true);
    r.live();
    check('a new welcome (a new socket) keeps nothing of the old list: it waits for the new one', r.list.state() === 'on' && Object.keys(r.list.model().rows).length === 0);
}
{   // a hub that has no list (an older one): nothing, whatever happens
    const r = rig({ wide: true });
    r.live(W({ presence: undefined }));
    r.who(FULL([row(1, 'maple')]));
    check('a welcome without presence: nothing is shown, a list that comes all the same is not used, and nothing is sent', r.list.state() === 'none' && r.root.hidden === true && r.sent.length === 0);
    r.state.client = CLIENT(ROOM({ isPublic: true })); r.tick(10000);
    check('... not later either, whatever the page does (an older hub answers a frame that it does not know with an error)', r.sent.length === 0);
    r.lost();
    check('... and a lost link of such a hub is not "off" (the hub never offered a list)', r.list.state() === 'none' && r.root.hidden === true && cls(r.host).indexOf('pl-on') === -1);
    r.live(W({ presence: 'yes' }));
    check('presence has to be exactly true', r.list.state() === 'none' && r.sent.length === 0);
}
{   // a hub that is gone from the first moment: "off" at once
    const r = rig({ wide: true });
    r.lost();
    check('a hub that was never there: "off" (the chat says "Chat is offline" beside it)', r.list.state() === 'off' && r.root.hidden === false);
    const late = rig({ wide: true });
    late.dock.tap = null;
    check('a dock that cannot be tapped (no list)', late.Players.mount({ doc: late.doc, win: late.win, host: late.host, dock: {}, page: {}, yard: late.yard }) === null && late.Players.mount({ doc: late.doc, win: { AntsLobbyRules: Rules }, host: late.host, dock: { tap() {} }, page: {}, yard: late.yard }) === null);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. What the page tells the hub
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({ wide: true });
    const to = (t) => r.clock.advance(t - r.clock.t);
    r.live();
    same('a named visitor tells the hub where it is, and then asks for the list (in that order, so that the list has the visitor in it)', r.sent, [{ t: 'here', at: 'look' }, { t: 'who' }]);
    r.who(FULL([row(1, 'maple')]));
    r.state.client = CLIENT(ROOM({ isPublic: true }), 'c1v3rr'); r.state.count = { people: 1, size: 4 };
    to(1000);
    check('the room changed less than 3 seconds after the last word: not said yet', r.hereFrames().length === 1);
    to(2999);
    check('... still not, a moment before the 3 seconds are over', r.hereFrames().length === 1);
    to(4000);
    same('... then said once, with the room\'s details (at the next look after the 3 seconds)', r.hereFrames().slice(1), [{ t: 'here', at: 'wait', pub: true, map: 'TREASURE.LVL', n: 1, of: 4, code: 'c1v3rr' }]);
    r.state.count = { people: 2, size: 4 }; to(4000); r.state.count = { people: 3, size: 4 }; to(5000); r.state.count = { people: 2, size: 4 }; to(5900);
    check('a room that changes again and again inside the 3 seconds is not told at each change', r.hereFrames().length === 2);
    to(6000);
    same('... it is told once, as it is when they are over', r.hereFrames().slice(2), [{ t: 'here', at: 'wait', pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr' }]);
    r.state.count = { people: 3, size: 4 }; to(7000); r.state.count = { people: 4, size: 4 }; to(8000); r.state.count = { people: 3, size: 4 }; to(8500); r.state.count = { people: 4, size: 4 }; to(8999);
    check('a flapping room: nothing before the 3 seconds are over', r.hereFrames().length === 3);
    to(9000);
    same('... then the last state, once', r.hereFrames().slice(3), [{ t: 'here', at: 'wait', pub: true, map: 'TREASURE.LVL', n: 4, of: 4, code: 'c1v3rr' }]);
    r.state.count = { people: 4, size: 4 }; to(10000);
    check('a room that went and came back to what the hub has is told nothing', r.hereFrames().length === 4);
    r.state.client = CLIENT(ROOM({ isPublic: true, starting: true }), 'c1v3rr'); to(12000);
    same('a match: playing, and no code', r.hereFrames().slice(4), [{ t: 'here', at: 'play', pub: true, map: 'TREASURE.LVL', n: 4, of: 4 }]);
    r.state.client = null; to(15000);
    same('no room again: looking', r.hereFrames().slice(5), [{ t: 'here', at: 'look' }]);
    const n = r.sent.length;
    to(60000);
    check('nothing is said while nothing changes', r.sent.length === n);
    r.lost(); r.state.client = CLIENT(ROOM({ isPublic: false }), 'k7m2xq');
    r.live();
    same('a new socket is told everything again at once (what the visitor is doing now, and the ask), without waiting', r.sent.slice(n), [{ t: 'here', at: 'wait' }, { t: 'who' }]);
    r.state.client = null; to(61000); to(64100);
    const m = r.sent.length; to(90000);
    check('(and the next change is told as always, then nothing more)', r.hereFrames().slice(-1)[0].at === 'look' && r.sent.length === m);
    r.lost(); r.live();
    same('a new socket is told again what the visitor does even when that is what the old socket was told (the hub knows nothing of the old one)', r.sent.slice(m), [{ t: 'here', at: 'look' }, { t: 'who' }]);
}
{   // a guest asks for the list and says nothing
    const r = rig({ wide: true });
    r.live(W({ you: null }));
    same('a guest (the welcome names nobody) only asks', r.sent, [{ t: 'who' }]);
    r.who(FULL([row(2, 'sage')], { n: 3, g: 2 }));
    r.state.client = CLIENT(ROOM({ isPublic: true }));
    r.tick(10000);
    check('... and still says nothing when it has a room', r.hereFrames().length === 0 && r.whoFrames().length === 1);
    check('no row of its own is shown (the page does not know who it is), the guests are counted, the count is everybody', one(r.panel(), 'pl-you') === null && r.names().join() === 'sage' && r.count() === '3' && byClass(r.panel(), 'pl-card').length === 1 && texts(r.panel(), 'pl-guests').some((t) => /\+ 2 guests/.test(t)));
}
{   // a link that cannot send: the ask is tried again
    const r = rig({ wide: true });
    r.link.open = false; r.emit('live', W({}));
    check('a dock that cannot send (the socket is not open yet) has said nothing', r.sent.length === 0);
    r.link.open = true; r.tick(1000);
    same('... the next look says it, once: where the visitor is, and the ask', r.sent, [{ t: 'here', at: 'look' }, { t: 'who' }]);
    r.tick(20000);
    check('... and not again', r.sent.length === 2);
}
{   // a visitor whose name is cleaned away is a guest; a moderator is known by the welcome
    const r = rig({ wide: true });
    r.live(W({ you: { name: '​', mod: false } }));
    check('a name that is nothing after the cleaning is nobody', r.sent.length === 1 && r.sent[0].t === 'who');
    const mod = rig({ wide: true });
    mod.live(W({ you: { name: 'moddy', mod: true } })); mod.who(FULL([row(1, 'moddy', 'look', { mod: true })], { n: 1 }));
    check('a moderator sees the mark on its own row', texts(mod.panel(), 'pl-badge').join() === 'mod' && texts(mod.panel(), 'pl-tag').join() === 'you');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. What is drawn
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const CROWD = [row(1, 'maple'), row(2, 'tess', 'look', { mod: true }), row(3, 'sage'), row(4, 'fern'), row(5, 'basil'),
    row(6, 'clover', 'wait', { pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr' }), row(7, 'juniper', 'wait'), row(8, 'pebble', 'play', { pub: true, map: 'ISLANDS.LVL', n: 3 }), row(9, 'hazel', 'play', { pub: true, map: 'GAUNTLET.LVL', n: 2 })];
{
    const r = rig({ wide: true });
    r.listed('c1v3rr', 'Treasure');
    r.live(); r.who(FULL(CROWD, { n: 13, g: 4 }));
    same('the groups in their order with their counts, each by name; the visitor is not among them', r.groups(), [
        { g: 'mod', n: '1', names: ['tess'] }, { g: 'look', n: '3', names: ['basil', 'fern', 'sage'] }, { g: 'wait', n: '2', names: ['clover', 'juniper'] }, { g: 'play', n: '2', names: ['hazel', 'pebble'] }]);
    same('the headings of the groups', texts(r.panel(), 'pl-gh').map((t) => t.replace(/\d+$/, '')), ['Moderators', 'Looking', 'Waiting', 'Playing']);
    check('the visitor is first, apart, as "you" with the line that says where', one(r.panel(), 'pl-you') && texts(one(r.panel(), 'pl-you'), 'pl-nm').join() === 'maple' && texts(one(r.panel(), 'pl-you'), 'pl-tag').join() === 'you' && texts(one(r.panel(), 'pl-you'), 'pl-meta').join().indexOf('Looking') !== -1);
    same('the lines under the names: a moderator is told where, those who look are not, a room by its map and seats, "In a room" for a room that the hub says nothing of, a match by its map and players', ['tess', 'sage', 'clover', 'juniper', 'pebble', 'hazel'].map((n) => r.metaOf(n)), ['Looking: Looking', null, 'Treasure · 2 of 4', 'In a room', 'Islands · 3 players', 'Gauntlet · 2 players']);
    same('the mark of a moderator is before the name, and only there', [texts(r.panel(), 'pl-badge'), one(byClass(r.panel(), 'pl-card').find((c) => one(c, 'pl-nm').textContent === 'tess'), 'pl-badge') !== null], [['mod'], true]);
    same('"+ 4 guests, not logged in", twice (in the list for a short window, under it otherwise: the style shows one)', texts(r.panel(), 'pl-guests'), ['…+ 4 guestsnot logged in', '…+ 4 guestsnot logged in']);
    same('each card says its status for the style (the dot of its circle)', byClass(r.panel(), 'pl-card').map((c) => c.getAttribute('data-s')), ['look', 'look', 'look', 'look', 'look', 'wait', 'wait', 'play', 'play'].slice(0, 0).concat(['look', 'look', 'look', 'look', 'look', 'wait', 'wait', 'play', 'play']));
    same('a Join on the row of a public room that waits (and no other), named for the room', [r.joins().length, r.joins()[0].getAttribute('aria-label'), r.joins()[0].getAttribute('data-code'), r.joins()[0].textContent], [1, 'Join clover’s room, Treasure · 2 of 4', 'c1v3rr', 'Join']);
    const keep = byClass(r.panel(), 'pl-group')[0];
    r.who({ t: 'who', full: false, n: 13, g: 4, more: 0, rows: [], gone: [] });
    check('a change that changes nothing draws nothing again (the same elements)', byClass(r.panel(), 'pl-group')[0] === keep);
    const scroller = one(r.panel(), 'pl-list'), takeOut = scroller.removeChild.bind(scroller);
    scroller.removeChild = (c) => { const gone = takeOut(c); if (scroller.children.length === 0) scroller.scrollTop = 0; return gone; };       // (a browser's list that is emptied is at the top again)
    scroller.scrollTop = 120;
    r.joins()[0].focus();
    r.who({ t: 'who', full: false, n: 14, g: 5, more: 0, rows: [row(10, 'ash', 'wait', { pub: true, map: 'TINY.LVL', n: 1, of: 2, code: 'a5h555' })], gone: [3] });
    check('a change that does change something is drawn again with the list scrolled where it was, and a Join that had the focus has it again', byClass(r.panel(), 'pl-group')[0] !== keep && one(r.panel(), 'pl-list').scrollTop === 120
        && !!r.env.focused && r.env.focused.getAttribute('data-code') === 'c1v3rr' && r.joins().indexOf(r.env.focused) !== -1);
    same('a change: a row comes, one goes, the numbers follow', [r.groups().map((g) => g.n), r.count(), texts(r.panel(), 'pl-guests')[1]], [['1', '2', '3', '2'], '14', '…+ 5 guestsnot logged in']);
    r.listed('a5h555', 'Tiny');
    r.tick(1000);
    same('the Games list gets the room: a Join comes with it (the rows are by name: ash, clover)', r.joins().map((b) => b.getAttribute('data-code')), ['a5h555', 'c1v3rr']);
    r.state.client = CLIENT(ROOM({ isPublic: true }), 'a5h555'); r.tick(1000);
    same('a Join is not offered on a room that the page is in', r.joins().map((b) => b.getAttribute('data-code')), ['c1v3rr']);
}
{   // the visitor's own row follows the page's room at once, not the hub's word
    const r = rig({ wide: true });
    r.live(); r.who(FULL([row(1, 'maple'), row(2, 'sage')], { n: 3, g: 1 }));
    check('looking: "Looking" under the visitor', texts(one(r.panel(), 'pl-you'), 'pl-meta').join().indexOf('Looking') !== -1);
    r.state.client = CLIENT(ROOM({ isPublic: false }), 'k7m2xq'); r.tick(1000);
    check('a private room: "Private room", at once (the hub is told 3 seconds later)', texts(one(r.panel(), 'pl-you'), 'pl-meta').join().indexOf('Private room') !== -1 && one(r.panel(), 'pl-you').findAll((e) => e.getAttribute && e.getAttribute('data-s') === 'wait').length === 1);
    r.state.client = CLIENT(ROOM({ isPublic: true }), 'k7m2xq'); r.state.count = { people: 1, size: 4 }; r.tick(1000);
    check('a public room: its map and seats', texts(one(r.panel(), 'pl-you'), 'pl-meta').join().indexOf('Treasure · 1 of 4') !== -1 && r.joins().length === 0);
    r.state.handing = true; r.tick(1000);
    check('on its way to the game: playing', one(r.panel(), 'pl-you').findAll((e) => e.getAttribute && e.getAttribute('data-s') === 'play').length === 1);
}
{   // empty: just the visitor
    const r = rig({ wide: true });
    r.live(); r.who(FULL([row(1, 'maple')], { n: 1 }));
    same('just the visitor: the note and the button, and the visitor first', [texts(r.panel(), 'pl-note').join('|'), r.names(), r.groups().length], ['Just you here. Host a game!People who log in with Discord show up here by name.+ Host a game', ['maple'], 0]);
    const host = one(r.panel(), 'pl-host');
    check('... Host a game is Quick host\'s button pressed', host.textContent === '+ Host a game' && host.disabled === false);
    host.click();
    same('... and a press does what Quick host does', r.pressed, ['quick host']);
    const off = rig({ wide: true, noQuick: true });
    off.live(); off.who(FULL([row(1, 'maple')], { n: 1 }));
    check('Quick host is not lit (the Games list has not come): the button is off, and a press does nothing', one(off.panel(), 'pl-host').disabled === true);
    off.quick.disabled = false; off.tick(1000);
    check('... and it is lit when Quick host is', one(off.panel(), 'pl-host').disabled === false);
    const guest = rig({ wide: true });
    guest.live(W({ you: null })); guest.who(FULL([], { n: 1, g: 1 }));
    check('a guest alone: the note too, no row, no guests line', texts(guest.panel(), 'pl-note')[0].indexOf('Just you here') === 0 && byClass(guest.panel(), 'pl-card').length === 0 && byClass(guest.panel(), 'pl-guests').length === 0);
}
{   // more people than rows, and a crowd
    const r = rig({ wide: true });
    r.live(); r.who(FULL([row(1, 'maple')].concat(Array.from({ length: 199 }, (_, i) => row(100 + i, 'p' + String(i).padStart(3, '0')))), { n: 230, g: 20, more: 10 }));
    check('200 rows: all drawn, "and 10 more" at the end of the list, the count is the hub\'s (everybody)', byClass(r.panel(), 'pl-card').length === 200 && texts(r.panel(), 'pl-more').join() === 'and 10 more' && r.count() === '230');
    r.who(FULL([row(1, 'maple'), row(2, 'sage')], { n: 3, g: 1, more: 0 }));
    check('... and the line goes with them', byClass(r.panel(), 'pl-more').length === 0);
    const joins = Array.from({ length: 50 }, (_, i) => row(300 + i, 'w' + i, 'wait', { pub: true, map: 'TREASURE.LVL', n: 1, of: 4, code: 'r' + i }));
    r.who(FULL([row(1, 'maple')].concat(joins), { n: 51 }));
    check('fifty rooms that wait and that the Games list has not: no Join (what a person says is not enough for a Join)', r.joins().length === 0);
    for (let i = 0; i < 50; i++) r.listed('r' + i, 'Treasure');
    r.tick(1000);
    check('... the Games list gets them: fifty Joins, each for its own room', r.joins().length === 50 && new Set(r.joins().map((b) => b.getAttribute('data-code'))).size === 50);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4b. Join: the Games list's own button does it
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({ wide: false });
    const clicks = [];
    Object.assign(r.listed('c1v3rr', 'Treasure'), { click() { clicks.push('c1v3rr'); } });
    r.live(); r.who(FULL(CROWD, { n: 13, g: 4 }));
    r.corner().click();
    check('(the sheet is open for the next check)', r.sheetOpen());
    r.joins()[0].click();
    same('Join presses the Games list\'s own Join of that room, and the sheet is put away', [clicks, r.sheetOpen()], [['c1v3rr'], false]);
    // a row of the Games list that is gone between the drawing and the press, in a list that is a popover (the test site): Games is pressed to open it, and its Join is there again if the room is
    const p = rig({ wide: true });
    p.listed('c1v3rr', 'Treasure');
    p.live(); p.who(FULL(CROWD, { n: 13, g: 4 }));
    p.unlisted('c1v3rr');
    p.games.click = () => { p.games.attributes['aria-expanded'] = 'true'; p.found['.gjoin[data-code="c1v3rr"]'] = Object.assign(new El(p.env, 'button'), { click() { clicks.push('popover'); } }); };
    p.games.setAttribute('aria-haspopup', 'dialog'); p.games.setAttribute('aria-expanded', 'false');
    p.joins()[0].click();
    same('a Games list that is a popover (the test site) is opened, and then its Join is pressed', clicks.slice(1), ['popover']);
    // a game that is not there any more
    const q = rig({ wide: true });
    q.listed('c1v3rr', 'Treasure');
    q.live(); q.who(FULL(CROWD, { n: 13, g: 4 }));
    q.unlisted('c1v3rr');
    q.joins()[0].click();
    check('a game that the list has not got any more says so (a few seconds), and presses nothing', texts(q.panel(), 'pl-status').join() === 'That game is not open any more.');
    q.tick(4100);
    check('... and the words go by themselves', texts(q.panel(), 'pl-status').join() === '');
    const docked = rig({ wide: true });
    docked.listed('c1v3rr', 'Treasure');
    docked.live(); docked.who(FULL(CROWD, { n: 13, g: 4 }));
    docked.unlisted('c1v3rr');
    docked.games.click = () => clicks.push('wrong');
    docked.joins()[0].click();
    check('a list that is docked has no popup: Games is not pressed for a game that is not in it', clicks.indexOf('wrong') === -1);
    // no row in the Games list, no Join (and a Join comes by itself when the list gets the room)
    const n = rig({ wide: true });
    n.live(); n.who(FULL(CROWD, { n: 13, g: 4 }));
    check('a room that waits and that the Games list does not have: no Join', n.joins().length === 0);
    n.listed('c1v3rr', 'Treasure'); n.tick(1000);
    check('... the Games list gets it: the Join comes at the next look of the page', n.joins().length === 1);
    n.unlisted('c1v3rr'); n.tick(1000);
    check('... and goes when the list loses it', n.joins().length === 0);
}
{   // a map of the players: its name is drawn only where the Games list shows that room under the same name; the hub\'s words are never drawn
    const comm = (name, code) => row(6, 'clover', 'wait', { pub: true, map: name + '.lvl', n: 2, of: 4, code: code || 'c0mm00' });
    const r = rig({ wide: true });
    r.live(); r.who(FULL([row(1, 'maple'), comm('Bomber Match  by PRo')], { n: 3 }));
    same('a map of the players that the Games list does not have: no name, only the seats', r.metaOf('clover'), '2 of 4');
    r.listed('c0mm00', 'Bomber Match  by PRo'); r.tick(1000);
    same('... the Games list has the room under that name: the name is drawn', r.metaOf('clover'), 'Bomber Match  by PRo · 2 of 4');
    r.unlisted('c0mm00'); r.listed('c0mm00', 'Another Map'); r.tick(1000);
    same('... the Games list has the room under another name: the hub\'s word is not drawn, and the list\'s is not either', [r.metaOf('clover'), r.joins().length], ['2 of 4', 1]);
    const lie = rig({ wide: true });
    lie.live(); lie.who(FULL([row(1, 'maple'), row(6, 'mallory', 'wait', { pub: true, map: 'Free nitro at discord.gg-abc.LVL', n: 1, of: 4, code: 'f4k3f4' })], { n: 3 }));
    same('a made-up map and code: no name, no Join', [lie.metaOf('mallory'), lie.joins().length, JSON.stringify(texts(lie.panel(), 'pl-card')).indexOf('nitro')], ['1 of 4', 0, -1]);
    lie.listed('f4k3f4', 'Treasure'); lie.tick(1000);
    same('a code of a room that the Games list has, with a map that is not that room\'s: the name is not drawn (a Join to that room is what the list offers)', [lie.metaOf('mallory'), JSON.stringify(texts(lie.panel(), 'pl-card')).indexOf('nitro')], ['1 of 4', -1]);
    const play = rig({ wide: true });
    play.live(); play.who(FULL([row(1, 'maple'), row(7, 'pat', 'play', { pub: true, map: 'Bomber Match  by PRo.lvl', n: 3 }), row(8, 'orig', 'play', { pub: true, map: 'ISLANDS.LVL', n: 2 })], { n: 3 }));
    same('a match: an original\'s name, never a map of the players\' (a match has no code to look up)', [play.metaOf('pat'), play.metaOf('orig')], ['3 players', 'Islands · 2 players']);
    const own = rig({ wide: true });
    own.state.client = CLIENT(ROOM({ isPublic: true, map: 'Bomber Match  by PRo.lvl' }), 'c0mm00'); own.state.count = { people: 1, size: 4 };
    own.live(); own.who(FULL([row(1, 'maple')], { n: 1 }));
    check('the visitor\'s own room: its map\'s name is the page\'s own word, drawn', texts(one(own.panel(), 'pl-you'), 'pl-meta').join().indexOf('Bomber Match  by PRo · 1 of 4') !== -1);
    const mine = rig({ wide: true });
    mine.live(); mine.who(FULL([row(1, 'maple'), row(2, 'maple', 'wait', { pub: true, map: 'TREASURE.LVL', n: 1, of: 4, code: 'c1v3rr' }), row(3, 'sage')], { n: 4 }));
    same('a person who has the visitor\'s name is not hidden (only the row that is the visitor\'s number is)', mine.names().sort(), ['maple', 'maple', 'sage'].sort());
}
{   // an original's name: the page's own table's (a closed set of six), and for a room that the Games list has, only if the list shows the same name
    const NAMES = { 'TINY.LVL': 'Tiny', 'SMALL.LVL': 'Small', 'MEDIUM.LVL': 'Medium', 'GAUNTLET.LVL': 'Gauntlet', 'TREASURE.LVL': 'Treasure', 'ISLANDS.LVL': 'Islands' };
    const waits = (map, code) => row(6, 'mallory', 'wait', { pub: true, map, n: 4, of: 4, code: code || 'f4k3f4' });
    const liar = rig({ wide: true });
    liar.live(); liar.who(FULL([row(1, 'maple'), waits('GAUNTLET.LVL')], { n: 3 }));
    liar.listed('f4k3f4', 'Islands'); liar.tick(1000);
    same('an original\'s name that the Games list does not confirm is not drawn: the list has the room under that code as Islands, the hub says Gauntlet (the line has the seats, and the Join is the list\'s own)', [liar.metaOf('mallory'), liar.joins().length], ['4 of 4', 1]);
    liar.unlisted('f4k3f4'); liar.listed('f4k3f4', 'Gauntlet'); liar.tick(1000);
    same('... the list has it under that name: drawn', liar.metaOf('mallory'), 'Gauntlet · 4 of 4');
    liar.unlisted('f4k3f4'); liar.listed('f4k3f4', 'Free nitro at discord.gg-abc'); liar.tick(1000);
    same('... the list has it under words that are not the original\'s: not drawn (and the list\'s words are not drawn by this panel either)', [liar.metaOf('mallory'), JSON.stringify(texts(liar.panel(), 'pl-card')).indexOf('nitro')], ['4 of 4', -1]);
    liar.unlisted('f4k3f4'); liar.tick(1000);
    same('... the list has no such room: an original\'s name is the page\'s own table\'s, as the hub says it', liar.metaOf('mallory'), 'Gauntlet · 4 of 4');
    const cases = [];
    Object.keys(NAMES).forEach((file) => [file, file.toLowerCase(), file.slice(0, -4) + '.lvl', file.charAt(0) + file.slice(1, -4).toLowerCase() + '.LVL'].forEach((spelling) => cases.push([spelling, NAMES[file]])));
    const spelled = rig({ wide: true });
    spelled.live(); spelled.who(FULL([row(1, 'maple')].concat(cases.map((c, i) => row(10 + i, 'p' + String(i).padStart(2, '0'), 'wait', { pub: true, map: c[0], n: 1, of: 4 }))), { n: cases.length + 1 }));
    same('the six originals, in every spelling that the page\'s rule takes of the file name, are drawn by the table\'s name and nothing else', cases.map((c, i) => spelled.metaOf('p' + String(i).padStart(2, '0'))), cases.map((c) => c[1] + ' · 1 of 4'));
    // a run of file names of any shape: what is drawn is never a word of the hub's, but the table's six names (the closed set) or, where the list confirms it, the list's own name
    let seed = 20261108;
    const chance = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
    const chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-!@#$%^&()+=,;\'[]{}~';
    const maps = [];
    for (let i = 0; i < 160; i++) { let n = 1 + Math.floor(chance() * 40), t = ''; while (n--) t += chars.charAt(Math.floor(chance() * chars.length)); maps.push(t + (chance() < 0.5 ? '.lvl' : '.LVL')); }
    const wild = rig({ wide: true });
    const rows = maps.map((map, i) => (i % 2 ? row(10 + i, 'w' + i, 'wait', { pub: true, map, n: 1 + (i % 4), of: 4, code: 'c' + i }) : row(10 + i, 'w' + i, 'play', { pub: true, map, n: 1 + (i % 4) })));
    wild.live(); wild.who(FULL([row(1, 'maple')].concat(rows), { n: rows.length + 1 }));
    const SHAPES = /^(?:(?:Tiny|Small|Medium|Gauntlet|Treasure|Islands) · )?(?:\d of \d|\d players?)$/;
    check('file names of any shape, in rooms that the Games list does not have: the line is the seats or the players, and a name before them is one of the six', rows.every((r) => SHAPES.test(wild.metaOf(r.name))), rows.map((r) => wild.metaOf(r.name)).filter((t) => !SHAPES.test(t)).join(' | '));
    rows.forEach((r, i) => { if (i % 2) wild.listed('c' + i, i % 6 === 1 ? 'Treasure' : 'Another map'); });
    wild.tick(1000);
    check('... and in rooms that it has, under a name that is not what the hub\'s file is called: the seats only (no word of the hub\'s and none of the list\'s)', rows.every((r, i) => !(i % 2) || /^\d of \d$/.test(wild.metaOf(r.name))), rows.map((r, i) => (i % 2 ? wild.metaOf(r.name) : '')).filter((t) => t && !/^\d of \d$/.test(t)).join(' | '));
    check('... a page that says a room of the players is the one that the list shows under the same name has that name drawn (by a map of that name, in a room of its own)', (() => { const q = rig({ wide: true }); q.live(); q.who(FULL([row(1, 'maple'), waits('Same Name.lvl', 'c0mm00')], { n: 3 })); q.listed('c0mm00', 'Same Name'); q.tick(1000); return q.metaOf('mallory') === 'Same Name · 4 of 4'; })());
}
{   // a room that the hub says nothing of is not called private: the hub keeps no details of a private room nor of the room of an account that may not chat
    const r = rig({ wide: true });
    r.live(); r.who(FULL([row(1, 'maple'), row(2, 'pat', 'wait'), row(3, 'quinn', 'play'), row(4, 'rae', 'wait', { pub: true, code: 'c0mm00' })], { n: 4 }));
    same('in the lines under the names of others: "In a room" and "In a game", and no word "Private"', [r.metaOf('pat'), r.metaOf('quinn'), r.metaOf('rae'), JSON.stringify(texts(r.panel(), 'pl-card')).indexOf('Private')], ['In a room', 'In a game', 'Public room', -1]);
}
{   // the visitor's own row is the one that the hub's welcome names, not a row with the visitor's name
    const youAnd = (r) => r.names().filter((n) => n === 'maple').length;
    const twins = [row(1, 'maple'), row(2, 'maple', 'wait'), row(3, 'sage')];
    const r = rig({ wide: true });
    r.live(W({ me: 2 })); r.who(FULL(twins, { n: 4 }));
    same('the welcome names the row (2): that one is left out of the groups, the other person called maple is not', [youAnd(r), r.groups().map((g) => g.g + ':' + g.names.join())], [2, ['look:maple,sage']]);
    r.live(W({ me: 1 })); r.who(FULL(twins, { n: 4 }));
    same('a new welcome (a new socket) names its own row: now it is the first that is left out', [youAnd(r), r.groups().map((g) => g.g + ':' + g.names.join())], [2, ['look:sage', 'wait:maple']]);
    ['2', -1, 1.5, 1e12, null, undefined, {}, [1], true].forEach((me) => {
        const q = rig({ wide: true });
        q.live(W({ me })); q.who(FULL([row(1, 'maple'), row(3, 'sage')], { n: 3 }));
        check('a welcome whose `me` is ' + JSON.stringify(me) + ' hides no row (nothing is guessed from a name)', youAnd(q) === 2);
    });
    const none = rig({ wide: true });
    none.live(W({ me: undefined })); none.who(FULL([row(1, 'maple')], { n: 2 }));
    check('a welcome with no `me` (a reader\'s, or a hub that does not say) hides no row', youAnd(none) === 2);
    r.lost(); r.live(W({ me: 7 })); r.who(FULL([row(1, 'maple'), row(7, 'maple', 'wait')], { n: 3 }));
    same('a link that was lost and comes back: the number is the new welcome\'s', [youAnd(r), r.groups().map((g) => g.g + ':' + g.names.join())], [2, ['look:maple']]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. The sheet and the fold
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({ wide: false });
    r.live(); r.who(FULL(CROWD, { n: 13, g: 4 }));
    check('closed: the panel is not open as a sheet, no dialog, no scrim', r.sheetOpen() === false && one(r.root, 'pl-scrim').hidden === true && r.panel().getAttribute('role') === null);
    r.corner().focus(); r.corner().click();
    check('the corner button opens it: a modal dialog over a scrim, the button says it is open, the focus is on Close', r.sheetOpen() && one(r.root, 'pl-scrim').hidden === false && r.panel().getAttribute('role') === 'dialog' && r.panel().getAttribute('aria-modal') === 'true'
        && r.corner().getAttribute('aria-expanded') === 'true' && r.env.focused === one(r.panel(), 'pl-close'));
    r.panel().fire('keydown', { key: 'Escape', preventDefault() {} });
    check('Escape closes it and the focus goes back to the button that opened it', !r.sheetOpen() && one(r.root, 'pl-scrim').hidden === true && r.panel().getAttribute('role') === null && r.env.focused === r.corner() && r.corner().getAttribute('aria-expanded') === 'false');
    r.barButton().focus(); r.barButton().click();
    check('the bar\'s button opens the same panel', r.sheetOpen() && r.env.focused === one(r.panel(), 'pl-close'));
    one(r.panel(), 'pl-close').click();
    check('Close closes it, and the focus goes back to the bar\'s button', !r.sheetOpen() && r.env.focused === r.barButton());
    r.corner().click(); one(r.root, 'pl-scrim').click();
    check('a press on the scrim closes it too', !r.sheetOpen());
    r.corner().click(); r.doc.listeners.keydown.forEach((fn) => fn({ key: 'Escape' }));
    check('Escape anywhere in the page closes it', !r.sheetOpen());
    r.corner().click(); r.list.open(false);
    check('(and the page can close it)', !r.sheetOpen());
    r.corner().click(); r.emit('shown', false);
    check('when the Room shows, a sheet that was open is closed', !r.sheetOpen());
    r.emit('shown', true);
    r.corner().click(); r.state.wide = true; (r.win.listeners.resize || []).forEach((fn) => fn({}));
    check('a window that grows wide has the panel docked: the sheet is closed', !r.sheetOpen());
    r.corner().click();
    check('... and a wide window\'s panel is never a sheet (no scrim, no dialog)', !r.sheetOpen() && one(r.root, 'pl-scrim').hidden === true);
    r.state.wide = false;
    r.corner().click(); r.lost();
    check('a hub that goes while the sheet is open: the sheet stays, and says "off"', r.sheetOpen() && r.list.state() === 'off');
    r.live(W({ presence: undefined }));
    check('... and a hub that comes back with no list takes the sheet away with the rest', !r.sheetOpen() && r.list.state() === 'none' && r.root.hidden === true);
}
{
    const r = rig({ wide: true });
    r.live(); r.who(FULL(CROWD, { n: 13, g: 4 }));
    const fold = byClass(r.panel(), 'pl-fold').find((b) => cls(b).indexOf('pl-close') === -1);
    check('the fold: open, labelled for what it does', r.panel().getAttribute('data-folded') === 'false' && fold.getAttribute('aria-label') === 'Fold the list' && fold.getAttribute('aria-expanded') === 'true');
    fold.click();
    check('pressed: folded to its header, the button says it opens', r.panel().getAttribute('data-folded') === 'true' && fold.getAttribute('aria-label') === 'Open the list' && fold.getAttribute('aria-expanded') === 'false');
    fold.click();
    check('pressed again: open', r.panel().getAttribute('data-folded') === 'false');
    r.panel().fire('keydown', { key: 'Escape', preventDefault() {} });
    check('Escape in a docked panel folds it and the focus is on the fold button', r.panel().getAttribute('data-folded') === 'true' && r.env.focused === fold);
    r.who(FULL(CROWD.concat([row(20, 'newcomer')]), { n: 14, g: 4 }));
    check('a fold stays folded when the list changes', r.panel().getAttribute('data-folded') === 'true');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 6. What the hub may send
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({ wide: true });
    r.live(); r.who(FULL([row(1, 'maple'), row(2, 'sage')], { n: 3, g: 1 }));
    const bad = [null, 'who', 7, [], {}, { t: 'who' }, { t: 'who', full: true, n: 'x', g: 0, more: 0, rows: [] }, { t: 'who', full: true, n: 3, g: -1, more: 0, rows: [] }, { t: 'who', full: true, n: 3, g: 0, more: 0, rows: { 0: row(1, 'zz') } },
        { t: 'who', full: true, n: 3, g: 0, more: 0, rows: Array.from({ length: 300 }, (_, i) => row(i, 'p' + i)) }, { t: 'who', full: false, n: 3, g: 0, more: 0, rows: [], gone: { 0: 1 } }];
    bad.forEach((frame) => r.who(frame));
    check('a frame that is wrong in any part is ignored: the list is what it was', r.names().join() === 'maple,sage' && r.count() === '3' && r.errors.length === 0);
    r.emit('nonsense', {}); r.emit('who'); r.emit('who', undefined);
    check('an unknown kind, a frame that is not there: nothing', r.names().join() === 'maple,sage' && r.errors.length === 0);
    r.who(FULL([row(1, 'maple'), row(2, '<img src=x onerror=alert(1)>'), row(3, '‮gnilleps'), row(4, 'long'.repeat(30)), row(5, 'z', 'wait', { pub: true, map: '<script>.LVL', code: '"><b>' })], { n: 6 }));
    check('hostile text is text: no markup was made, no element but the list\'s own kinds, the name cut and clean, a map and a code that are not what they should be are none', r.env.innerHTMLWrites.length === 0
        && r.names().join('|') === 'maple|<img src=x onerror=alert(1)>|gnilleps|' + 'long'.repeat(8) + '…|z'
        && r.panel().findAll((e) => ['img', 'script', 'a', 'iframe', 'style', 'link'].indexOf(e.tagName) !== -1).length === 0 && r.metaOf('z') === 'Public room' && r.joins().length === 0);
    // a thousand frames of garbage and a thousand of rows
    for (let i = 0; i < 1000; i++) r.who({ t: 'who', full: false, n: 3, g: 0, more: 0, rows: [row(i % 150, 'q' + (i % 150), i % 3 ? 'look' : 'wait')], gone: [(i + 75) % 150] });
    check('a thousand changes: the rows stay under 200, nothing throws', Object.keys(r.list.model().rows).length <= 200 && r.errors.length === 0);
    // a dock that throws in its tap, or a page that throws when asked
    const t = rig({ wide: true });
    t.state.client = { get room() { throw new Error('a page that breaks'); } };
    t.live();
    check('a page that throws when asked about its room: the list reports it and its other parts go on (the sheet and the fold still work)', t.errors.length >= 1 && (t.corner().click(), true));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 7. The page: the list is loaded after the dock, once; a missing or broken one changes nothing; the hand-off makes it inert
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const filesOf = (env, name) => env.body.children.filter((c) => (c.tagName === 'script' || c.tagName === 'link') && new RegExp('^home/' + name + '\\.(js|css)$').test(String(c.src || c.href || '')));
function hubFor(env) {
    const sockets = [];
    env.win.WebSocket = function (url) { this.url = url; this.sent = []; this.readyState = 0; this.close = () => { this.readyState = 3; }; this.send = (x) => { if (this.readyState !== 1) throw new Error('not open'); this.sent.push(x); }; sockets.push(this); };
    return sockets;
}
function bringChat(env) {
    env.win.AntsHomeChat = H.loadFile('home/chat.js', env.scope).AntsHomeChat;
    filesOf(env, 'chat').forEach((n) => { if (n.onload) n.onload(); });
    env.advance(0);                                          // (the page asks for the list's files after the next frame)
}
function bringPlayers(env) {
    env.win.AntsHomePlayers = H.loadFile('home/players.js', env.scope).AntsHomePlayers;
    filesOf(env, 'players').forEach((n) => { if (n.onload) n.onload(); });
}
{
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    const hub = hubFor(env);
    env.advance(0);
    check('before the dock is there the page asks for nothing of the list', filesOf(env, 'players').length === 0);
    bringChat(env);
    same('when the dock is mounted the page asks for the style and the script of the list, once, async', filesOf(env, 'players').map((n) => n.tagName + ':' + (n.href || n.src) + ':' + (n.rel || '') + ':' + (n.async === true ? 'async' : '')), ['link:home/players.css:stylesheet:', 'script:home/players.js::async']);
    const home = env.$('home'), kids = home.children.length;
    filesOf(env, 'players')[1].onload();
    check('it mounts only when BOTH files are there (no unstyled list)', home.children.length === kids);
    env.win.AntsHomePlayers = H.loadFile('home/players.js', env.scope).AntsHomePlayers;
    filesOf(env, 'players')[0].onload();
    const yard = home.children.find((c) => c.id === 'yard');
    check('... then its box is in the Yard, and Home\'s own parts are where they were', yard.children.some((c) => cls(c).indexOf('pl-root') !== -1) && home.children.slice(0, 3).map((c) => c.tagName).join() === 'header,main,div' && home.children[3].id === 'gchat');
    hub[0].readyState = 1; hub[0].onmessage({ data: JSON.stringify(W({ you: { name: 'maya', mod: false }, online: 2 })) });
    check('the dock\'s socket carries the list\'s words: the visitor says where it is and asks for the list, on the same socket (no second socket)', hub.length === 1 && hub[0].sent.map((x) => JSON.parse(x)).filter((m) => m.t === 'here' || m.t === 'who').map((m) => m.t).join() === 'here,who');
    hub[0].onmessage({ data: JSON.stringify(FULL([row(1, 'maya'), row(2, 'sage')], { n: 2 })) });
    const box = yard.children.find((c) => cls(c).indexOf('pl-root') !== -1);
    check('a list from the hub shows (the chat dock\'s count says the same number, 2 here now)', box.hidden === false && one(box, 'pl-count').textContent === '2' && one(home.children[3], 'gc-on').textContent === '2 here now');
    env.advance(10);
    check('the page does not ask for the list again (the same two files)', filesOf(env, 'players').length === 2);
    env.home('mine').click();
    env.roomBar('rhome').click();
    env.advance(10);
    check('Home shown again asks for nothing more', filesOf(env, 'players').length === 2 && filesOf(env, 'chat').length === 2);
}
{   // a dock that never comes: no list; a list script that is missing or breaks: Home is whole
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    hubFor(env); env.advance(0);
    check('no dock, no list: the page asked for the dock only', filesOf(env, 'players').length === 0 && filesOf(env, 'chat').length === 2);
    const broken = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    hubFor(broken); broken.advance(0); bringChat(broken);
    broken.win.AntsHomePlayers = { mount() { throw new Error('a list that breaks'); } };
    filesOf(broken, 'players').forEach((n) => { if (n.onload) n.onload(); });
    check('a list that throws when it mounts: Home goes on (My room still makes the room, the dock is there)', broken.$('home').children[3].id === 'gchat' && (broken.home('mine').click(), broken.sockets.length === 1) && broken.screen() === 'room');
    const none = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    hubFor(none); none.advance(0); bringChat(none);
    filesOf(none, 'players').forEach((n) => { if (n.onload) n.onload(); });
    check('a script that did not define the list (no AntsHomePlayers) mounts nothing', none.win.AntsHomePlayers === undefined && none.$('home').children.length === 4);
    const gone = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    hubFor(gone); gone.advance(0); bringChat(gone);
    check('(a style that never comes: nothing is mounted, as for the others)', filesOf(gone, 'players').length === 2 && gone.$('home').children.length === 4);
}
{   // the hand-off to the game: the list is inert with the bars and the dock; a list mounted late is inert from the start
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true, picker: true });
    hubFor(env); env.advance(0); bringChat(env); bringPlayers(env);
    const yard = env.$('home').children.find((c) => c.id === 'yard');
    const box = yard.children.find((c) => cls(c).indexOf('pl-root') !== -1);
    check('(the list is there and takes presses)', !!box && !box.hasAttribute('inert'));
    env.home('mine').click();
    env.arrive({ key: KEY, seats: [[C, 'Maya'], [E, ''], [E, ''], [E, '']] });
    env.$('browse').click();
    check('a dialog over the page makes the list inert with the bars and the dock', box.hasAttribute('inert') && env.home('hbar').hasAttribute('inert'));
    const late = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    hubFor(late); late.advance(0); bringChat(late);
    late.home('mine').click();
    const sock = late.arrive({ key: KEY, seats: [[C, 'Sam'], [E, ''], [E, ''], [E, '']] });
    sock.receive(roomMessage([[C, 'Sam'], [C, 'Maya'], [E, ''], [E, '']], { you: 0, leader: 0, flags: 7 }));
    bringPlayers(late);
    const lateYard = late.$('home').children.find((c) => c.id === 'yard');
    const lateBox = lateYard.children.find((c) => cls(c).indexOf('pl-root') !== -1);
    check('a list that is mounted while the page is on its way to the game is inert from the start', late.assigned.length === 1 && !!lateBox && lateBox.hasAttribute('inert'));
    late.advance(15000);
    check('... and 15 s later, when Home is free, so is the list', !lateBox.hasAttribute('inert'));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 8. The source of the script
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const code = playersSource.replace(/\/\/.*/g, '');
    check('no markup is made from text, nothing is evaluated', !/innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function|srcdoc|createContextualFragment|DOMParser|setTimeout\(\s*['"]/.test(code));
    check('no link, no picture, no address: the list sets no href or src', !/\.href\b|\.src\b|setAttribute\(\s*['"](href|src|style|on\w+|srcdoc)/.test(code));
    check('nothing is kept in the browser and no socket is opened by the list itself', !/localStorage|sessionStorage|indexedDB|document\.cookie|new WebSocket|WebSocket\(|XMLHttpRequest|fetch\(|navigator\.sendBeacon/.test(code));
    check('it never names the hub\'s door (the dock\'s script alone knows it)', !/\/hub\b/.test(code));
    same('it makes only these kinds of element', [...new Set([...code.matchAll(/\bel\('(\w+)'/g)].map((m) => m[1]).concat([...code.matchAll(/\bbutton\('/g)].length ? ['button'] : []))].sort(), ['bdi', 'button', 'div', 'h2', 'h3', 'hr', 'i', 'li', 'p', 'section', 'span', 'ul']);
}

console.log('players list: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
