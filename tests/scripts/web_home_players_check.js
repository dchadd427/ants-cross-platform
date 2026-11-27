// Runs the players list of Home (web/home/players.js, docs/HOME_SCREEN.md "The players list") against a FAKE chat dock (the hub's frames come from the test; what the page sends goes into a list) and a fake
// clock, in the fake browser of tests/scripts/web_page_harness.js (its DOM). The page itself (web/lobby.html with web/home/home.js and the real chat dock) is section 7. What it checks:
//   1. the pure parts: a row of the hub's list cleaned (odd names, a map that the page's own rule accepts, numbers, a code only for a room that waits), the list that a frame makes, the groups, the words
//      under a name, and what the page tells the hub it is doing (looking, waiting, playing; a public room's details and never a private room's);
//   2. the phases: nothing until the hub has said the list, nothing for a hub that has none, "off" for a hub that is gone, back by itself, the count (not for one person alone), the class that Home carries;
//   3. what the page tells the hub: `here` (a named visitor) and `who` once for each socket, when the room changes and not oftener than every 3 seconds, again after a lost link, nothing for a guest;
//   4. what is drawn: the groups in order with their counts, the visitor first and as the page knows it, the moderator mark, the guests, nothing under the visitor when alone, a Join where a public room waits
//      (the Games list's own Join does it, a list that is a popover is opened for it, a game that is gone is said), "and N more", every line as text;
//   5. no fold: the panel has no minus button, no folded state, and Escape in it does nothing to it (it is always open);
//   6. what the hub may send: unknown kinds, a wrong frame, an odd name, a flood of rows: nothing breaks, nothing becomes markup;
//   7. the page (web/lobby.html with web/home/home.js): the list is loaded once, after the dock, a missing or broken one changes nothing, the hand-off makes it inert;
//   8. the source of the script: no markup, no storage, no link, no address;
//   10. what it gives the moderation sheet (web/home/mod.js): the number of each row but the visitor's own as `data-i`, `person(i)` (a row as { i, name, role, at, where }; none for the visitor's own row, a number that
//      is not a row, a hub that is gone), `where` in words ("Waiting · Treasure · 2 of 4"), the list's box, and `drawn(fn)` (told each time the list is made again, and only then).
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
// the page's room, as the page's LobbyClient has it (client.room: a Room message of the server, decoded), and the count of its seats
const ROOM = (o) => Object.assign({ lobby: true, starting: false, isPublic: false, map: 'TREASURE.LVL', slots: [] }, o);
const CLIENT = (room, code, guest) => ({ room, code: code || 'c1v3rr', isLeader: () => !guest });

// one list on a fake page: o.width and o.height (the window), o.client, o.count (the seats of the room: { people, size }, made into the Room's model that the page hands over), o.handing, o.noQuick, o.popover
function rig(o) {
    o = o || {};
    const env = { elements: {}, innerHTMLWrites: [] };
    const clock = makeClock();
    const Players = loadPlayers(clock);
    const doc = { hidden: false, listeners: {}, activeElement: null, createElement: (t) => new El(env, t), createTextNode: (t) => ({ isText: true, textContent: String(t), parent: null }), getElementById: () => null, addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); } };
    env.doc = doc;
    const state = { client: o.client || null, count: o.count || null, handing: !!o.handing };       // (count: { people, size } stands for the Room's model: that many persons, then open colors up to size, then Closed ones)
    const modelOf = (c) => (c ? { slots: [0, 1, 2, 3].map((i) => ({ kind: i < c.people ? 'person' : i < c.size ? 'open' : 'nobody' })) } : null);
    const win = { innerWidth: o.width || 1280, innerHeight: o.height || 800, listeners: {}, addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); }, AntsHomeChat: Chat, AntsLobbyRules: Rules };
    const store = { items: {}, getItem(k) { return Object.prototype.hasOwnProperty.call(store.items, k) ? store.items[k] : null; }, setItem(k, v) { store.items[k] = String(v); }, removeItem(k) { delete store.items[k]; } };
    win.sessionStorage = store;
    const host = new El(env, 'div', 'home'), yard = new El(env, 'main', 'yard'), right = new El(env, 'div'), quick = new El(env, 'button'), gamesButton = new El(env, 'button');
    right.className = 'hright'; right.appendChild(new El(env, 'button')); quick.disabled = !!o.noQuick;
    host.appendChild(yard);
    const pressed = [], found = {};
    host.querySelector = (sel) => found[sel] || null;
    quick.click = () => pressed.push('quick host');
    const sent = [], taps = [], errors = [], link = { open: true };
    const dock = { tap(fn) { taps.push(fn); }, send(obj) { if (!link.open) return false; sent.push(JSON.parse(JSON.stringify(obj))); return true; } };
    const page = { client: () => state.client, info: () => ({ client: !!state.client, model: modelOf(state.count) }), handing: () => state.handing };
    const list = Players.mount({ doc, win, host, dock, page, yard, quickHost: quick, games: gamesButton, now: clock.now, error: (e) => errors.push(e) });
    const r = { store, env, clock, Players, doc, win, host, yard, right, quick, games: gamesButton, list, dock, sent, taps, errors, state, pressed, found, link };
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
    same('a plain row: the name, where, no mark, no details', rowOf({ i: 4, name: 'sage', at: 'look' }), { id: 4, name: 'sage', tag: '', at: 'look', mod: false, role: 'none', pic: '', pub: false, priv: false, solo: false, map: '', mapName: '', guess: '', n: -1, of: -1, code: '' });
    same('the mark of a moderator is exactly true (a string or a number is none)', [rowOf(row(1, 'a1', 'look', { mod: true })).mod, rowOf(row(1, 'a1', 'look', { mod: 'true' })).mod, rowOf(row(1, 'a1', 'look', { mod: 1 })).mod], [true, false, false]);
    same('a name loses the invisible and bidirectional characters and is cut at 32 code points', [rowOf(row(1, '‮alice​⁦')).name, rowOf(row(1, 'x'.repeat(40))).name], ['alice', 'x'.repeat(32) + '…']);
    same('markup in a name stays text', rowOf(row(1, '<img src=x onerror=1>')).name, '<img src=x onerror=1>');
    same('a name that is nothing after the cleaning, or no name, is no row', [rowOf(row(1, '​‮')), rowOf(row(1, '')), rowOf({ i: 1, at: 'look' }), rowOf({ i: 1, name: 7, at: 'look' })], [null, null, null, null]);
    same('a status that is not one of the four is no row (also one that is a name of Object)', [rowOf(row(1, 'ab', 'asleep')), rowOf(row(1, 'ab', 'constructor')), rowOf(row(1, 'ab', '__proto__')), rowOf({ i: 1, name: 'ab', at: ['wait'] }), rowOf({ i: 1, name: 'ab' })], [null, null, null, null, null]);
    same('an Away row is the name and the word and nothing of a room, whatever the frame holds', (() => { const r = rowOf(row(1, 'ab', 'away', { pub: true, priv: true, solo: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr' })); return [r.at, r.pub, r.priv, r.solo, r.map, r.n, r.of, r.code]; })(), ['away', false, false, false, '', -1, -1, '']);
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
    same('rows that are not rows are left out of a frame, the others stay', rowsOf(apply(FULL([row(1, 'a1'), { i: 2 }, null, 'x', row(3, 'c3', 'asleep'), row(4, 'd4')]))), ['a1', 'd4']);

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

    const slots = (kinds) => ({ slots: kinds.map((kind) => ({ kind })) });
    same('the seats of a room: persons and computer players are people, a Closed color is no seat, no model of four colors is no count', [P.countOf(slots(['person', 'open', 'open', 'open'])), P.countOf(slots(['person', 'bot', 'open', 'nobody'])), P.countOf(slots(['person', 'person', 'bot', 'bot'])), P.countOf(null), P.countOf({}), P.countOf(slots(['person', 'open']))],
        [{ people: 1, size: 4 }, { people: 2, size: 3 }, { people: 4, size: 4 }, null, null, null]);
    const say = P.announceOf;
    same('no room: looking', [say(null, null, false), say({}, null, false), say({ room: null }, null, false)], [{ at: 'look' }, { at: 'look' }, { at: 'look' }]);
    same('a private room that waits says only that about itself (no map, no seats, no code in the row), and says its code and seats apart, for invitations', say(CLIENT(ROOM({ isPublic: false })), { people: 2, size: 4 }, false), { at: 'wait', pub: false, inv: { code: 'c1v3rr', n: 2, of: 4 } });
    same('a public room that waits says its map, seats and code', say(CLIENT(ROOM({ isPublic: true })), { people: 2, size: 4 }, false), { at: 'wait', pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr', inv: { code: 'c1v3rr', n: 2, of: 4 } });
    same('a guest of a public room says its map and seats and not the code (the host\'s row has the Join)', say(CLIENT(ROOM({ isPublic: true }), 'c1v3rr', true), { people: 2, size: 4 }, false), { at: 'wait', pub: true, map: 'TREASURE.LVL', n: 2, of: 4, inv: { code: 'c1v3rr', n: 2, of: 4 } });
    same('a client that cannot say whether it leads says no code', say({ room: ROOM({ isPublic: true }), code: 'c1v3rr' }, null, false), { at: 'wait', pub: true, map: 'TREASURE.LVL' });
    same('a public room whose seats are not known yet (or are more than the room) says the rest', [say(CLIENT(ROOM({ isPublic: true })), null, false), say(CLIENT(ROOM({ isPublic: true })), { people: 5, size: 4 }, false)], [{ at: 'wait', pub: true, map: 'TREASURE.LVL', code: 'c1v3rr' }, { at: 'wait', pub: true, map: 'TREASURE.LVL', code: 'c1v3rr' }]);
    same('a match that is starting, a page on its way to a game, a room that a game made: playing; a match has no code, a private one says nothing', [
        say(CLIENT(ROOM({ isPublic: true, starting: true })), { people: 3, size: 4 }, false), say(CLIENT(ROOM({ isPublic: true })), { people: 3, size: 4 }, true), say(CLIENT(ROOM({ lobby: false })), null, false), say(CLIENT(ROOM({ starting: true })), { people: 3, size: 4 }, false)],
        [{ at: 'play', pub: true, map: 'TREASURE.LVL', n: 3, of: 4 }, { at: 'play', pub: true, map: 'TREASURE.LVL', n: 3, of: 4 }, { at: 'play', pub: false }, { at: 'play', pub: false }]);
    same('a code that is not the shape of one is not said', say({ room: ROOM({ isPublic: true }), code: 'ABC DEF', isLeader: () => true }, null, false), { at: 'wait', pub: true, map: 'TREASURE.LVL' });
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. The phases: nothing, the list, off, back
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig();
    check('mounted: the list taps the dock once and shows nothing (no panel, no class on Home) until the hub has said the list', r.taps.length === 1 && r.list.state() === 'none' && r.root.hidden === true && cls(r.host).indexOf('pl-on') === -1);
    check('... the box that Home can make inert is in the body (the box that Home hands it)', r.root.parent === r.yard);
    r.live();
    check('the welcome alone shows nothing (the list is not here yet: no flash of "just you")', r.list.state() === 'none' && r.root.hidden === true);
    r.who(FULL([row(1, 'maple'), row(2, 'sage')], { n: 3, g: 1 }));
    check('the first list shows it: the panel and the class on Home', r.list.state() === 'on' && r.root.hidden === false && cls(r.host).indexOf('pl-on') !== -1 && r.panel().getAttribute('data-state') === 'on');
    same('the title and the count (everybody, the visitor and the guests too: the chat says the same)', [r.title(), r.count(), one(r.panel(), 'pl-count').getAttribute('aria-label')], ['Players', '3', '3 here now']);
    r.who(FULL([row(1, 'maple')], { n: 1, g: 0 }));
    check('one person alone is no count (no number in the header)', r.count() === '');
    r.who(FULL([row(1, 'maple')], { n: 600, g: 599 }));
    r.who(FULL([row(1, 'maple')], { n: 99, g: 98 }));
    same('up to 99 the header says the number', r.count(), '99');
    r.who(FULL([row(1, 'maple')], { n: 100, g: 99 }));
    same('over 99 the header says it, and its name says it too', [r.count(), one(r.panel(), 'pl-count').getAttribute('aria-label')], ['100', '100 here now']);
    r.who(FULL([row(1, 'maple')], { n: 600, g: 599 }));
    same('over 500 people the header says "500+"', r.count(), '500+');
    r.who(FULL([row(1, 'maple')], { n: 99999, g: 99998 }));
    same('a count that no hub has: the same words', r.count(), '500+');
    r.lost();
    check('the hub is gone: "off" (the panel stays, the count goes, a grey dot and the words come), the rows are not kept', r.list.state() === 'off' && r.panel().getAttribute('data-state') === 'off' && r.count() === '' && one(r.panel(), 'pl-dot').hidden === false && texts(r.panel(), 'pl-note')[0].indexOf('Players list is off right now') === 0
        && r.names().length === 0 && Object.keys(r.list.model().rows).length === 0 && r.root.hidden === false);
    check('... the note says that the rest works and that the list comes back by itself', /Games and rooms work as always; this list comes back by itself\./.test(texts(r.panel(), 'pl-note')[0]));
    r.live(); r.who(FULL([row(1, 'maple'), row(3, 'fern')], { n: 2 }));
    check('the hub is back: the list is on again, with the new rows', r.list.state() === 'on' && r.names().join() === 'maple,fern' && r.count() === '2' && one(r.panel(), 'pl-dot').hidden === true);
    r.live();
    check('a new welcome (a new socket) keeps nothing of the old list: it waits for the new one', r.list.state() === 'on' && Object.keys(r.list.model().rows).length === 0);
}
{   // a hidden page runs no poll loop; it starts again when the page is shown
    const r = rig();
    r.live(); r.who(FULL([row(1, 'maple')]));
    r.doc.hidden = true;
    r.tick(1000);
    const timers = r.clock.timers.length, sent = r.sent.length;
    r.tick(60000);
    check('hidden: the poll ran once and set no other timer (nothing is polled or sent while it is hidden)', r.clock.timers.length === timers && r.sent.length === sent);
    r.doc.hidden = false;
    (r.doc.listeners.visibilitychange || []).forEach((fn) => fn({}));
    check('shown: the loop is back (a timer is waiting again), and a second event does not start a second one', r.clock.timers.length === timers + 1 && ((r.doc.listeners.visibilitychange || []).forEach((fn) => fn({})), r.clock.timers.length === timers + 1));
}
{   // a hub that has no list (an older one): nothing, whatever happens
    const r = rig();
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
    const r = rig();
    r.lost();
    check('a hub that was never there: "off" (the chat says "Chat is offline" beside it)', r.list.state() === 'off' && r.root.hidden === false);
    const late = rig();
    late.dock.tap = null;
    check('a dock that cannot be tapped (no list)', late.Players.mount({ doc: late.doc, win: late.win, host: late.host, dock: {}, page: {}, yard: late.yard }) === null && late.Players.mount({ doc: late.doc, win: { AntsLobbyRules: Rules }, host: late.host, dock: { tap() {} }, page: {}, yard: late.yard }) === null);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. What the page tells the hub
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig();
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
    same('... then said once, with the room\'s details (at the next look after the 3 seconds)', r.hereFrames().slice(1), [{ t: 'here', at: 'wait', pub: true, map: 'TREASURE.LVL', n: 1, of: 4, code: 'c1v3rr', inv: { code: 'c1v3rr', n: 1, of: 4, map: 'TREASURE.LVL' } }]);
    r.state.count = { people: 2, size: 4 }; to(4000); r.state.count = { people: 3, size: 4 }; to(5000); r.state.count = { people: 2, size: 4 }; to(5900);
    check('a room that changes again and again inside the 3 seconds is not told at each change', r.hereFrames().length === 2);
    to(6000);
    same('... it is told once, as it is when they are over', r.hereFrames().slice(2), [{ t: 'here', at: 'wait', pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr', inv: { code: 'c1v3rr', n: 2, of: 4, map: 'TREASURE.LVL' } }]);
    r.state.count = { people: 3, size: 4 }; to(7000); r.state.count = { people: 4, size: 4 }; to(8000); r.state.count = { people: 3, size: 4 }; to(8500); r.state.count = { people: 4, size: 4 }; to(8999);
    check('a flapping room: nothing before the 3 seconds are over', r.hereFrames().length === 3);
    to(9000);
    same('... then the last state, once', r.hereFrames().slice(3), [{ t: 'here', at: 'wait', pub: true, map: 'TREASURE.LVL', n: 4, of: 4, code: 'c1v3rr', inv: { code: 'c1v3rr', n: 4, of: 4, map: 'TREASURE.LVL' } }]);
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
    same('a new socket is told everything again at once (what the visitor is doing now, and the ask), without waiting', r.sent.slice(n), [{ t: 'here', at: 'wait', pub: false, inv: { code: 'k7m2xq', n: 4, of: 4, map: 'TREASURE.LVL' } }, { t: 'who' }]);
    r.state.client = null; to(61000); to(64100);
    const m = r.sent.length; to(90000);
    check('(and the next change is told as always, then nothing more)', r.hereFrames().slice(-1)[0].at === 'look' && r.sent.length === m);
    r.lost(); r.live();
    same('a new socket is told again what the visitor does even when that is what the old socket was told (the hub knows nothing of the old one)', r.sent.slice(m), [{ t: 'here', at: 'look' }, { t: 'who' }]);
}
{   // a guest asks for the list and says nothing
    const r = rig();
    r.live(W({ you: null }));
    same('a guest (the welcome names nobody) only asks', r.sent, [{ t: 'who' }]);
    r.who(FULL([row(2, 'sage')], { n: 3, g: 2 }));
    r.state.client = CLIENT(ROOM({ isPublic: true }));
    r.tick(10000);
    check('... and still says nothing when it has a room', r.hereFrames().length === 0 && r.whoFrames().length === 1);
    check('no row of its own is shown (the page does not know who it is), the guests are counted, the count is everybody', one(r.panel(), 'pl-you') === null && r.names().join() === 'sage' && r.count() === '3' && byClass(r.panel(), 'pl-card').length === 1 && texts(r.panel(), 'pl-guests').some((t) => /\+ 2 guests/.test(t)));
}
{   // a link that cannot send: the ask is tried again
    const r = rig();
    r.link.open = false; r.emit('live', W({}));
    check('a dock that cannot send (the socket is not open yet) has said nothing', r.sent.length === 0);
    r.link.open = true; r.tick(1000);
    same('... the next look says it, once: where the visitor is, and the ask', r.sent, [{ t: 'here', at: 'look' }, { t: 'who' }]);
    r.tick(20000);
    check('... and not again', r.sent.length === 2);
}
{   // a visitor whose name is cleaned away is a guest; a moderator is known by the welcome
    const r = rig();
    r.live(W({ you: { name: '​', mod: false } }));
    check('a name that is nothing after the cleaning is nobody', r.sent.length === 1 && r.sent[0].t === 'who');
    const mod = rig();
    mod.live(W({ you: { name: 'moddy', mod: true } })); mod.who(FULL([row(1, 'moddy', 'look', { mod: true })], { n: 1 }));
    check('a moderator\'s own row has the role of a moderator (the class for yellow, the word in the accessible name and the title) and no tag but "you"', texts(mod.panel(), 'pl-badge').length === 0 && cls(one(mod.panel(), 'pl-card')).indexOf('r-mod') !== -1 && texts(one(one(mod.panel(), 'pl-card'), 'pl-name'), 'pl-sr').join() === 'moderator ' && one(one(mod.panel(), 'pl-card'), 'pl-nm').getAttribute('title') === 'moderator' && texts(mod.panel(), 'pl-tag').join() === 'you');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. What is drawn
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const CROWD = [row(1, 'maple'), row(2, 'tess', 'look', { mod: true }), row(3, 'sage'), row(4, 'fern'), row(5, 'basil'),
    row(6, 'clover', 'wait', { pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr' }), row(7, 'juniper', 'wait'), row(8, 'pebble', 'play', { pub: true, map: 'ISLANDS.LVL', n: 3 }), row(9, 'hazel', 'play', { pub: true, map: 'GAUNTLET.LVL', n: 2 })];
{
    const r = rig();
    r.listed('c1v3rr', 'Treasure');
    r.live(); r.who(FULL(CROWD, { n: 13, g: 4 }));
    same('the groups in their order with their counts, each by name; the visitor is not among them', r.groups(), [
        { g: 'mod', n: '1', names: ['tess'] }, { g: 'look', n: '3', names: ['basil', 'fern', 'sage'] }, { g: 'wait', n: '2', names: ['clover', 'juniper'] }, { g: 'play', n: '2', names: ['hazel', 'pebble'] }]);
    same('the headings of the groups', texts(r.panel(), 'pl-gh').map((t) => t.replace(/\d+$/, '')), ['Staff', 'Looking', 'Waiting', 'Playing']);
    check('the visitor is first, apart, as "you" with the line that says where', one(r.panel(), 'pl-you') && texts(one(r.panel(), 'pl-you'), 'pl-nm').join() === 'maple' && texts(one(r.panel(), 'pl-you'), 'pl-tag').join() === 'you' && texts(one(r.panel(), 'pl-you'), 'pl-meta').join().indexOf('Looking') !== -1);
    same('the lines under the names: a moderator is told where, those who look are not, a room by its map and seats, "In a room" for a room that the hub says nothing of, a match by its map and players', ['tess', 'sage', 'clover', 'juniper', 'pebble', 'hazel'].map((n) => r.metaOf(n)), ['Looking: Looking', null, 'Treasure · 2 of 4', 'In a room', 'Islands · 3 players', 'Gauntlet · 2 players']);
    same('the role of a moderator is on the name (class, word, title) of its card, and only there: nobody else has one, and no tag is drawn', [texts(r.panel(), 'pl-badge').length, (() => { const c = byClass(r.panel(), 'pl-card').find((x) => one(x, 'pl-nm').textContent === 'tess'); return [cls(c).indexOf('r-mod') !== -1, one(c, 'pl-sr').textContent, one(c, 'pl-nm').getAttribute('title')]; })(), byClass(r.panel(), 'pl-card').filter((c) => cls(c).indexOf('r-mod') !== -1).length], [0, [true, 'moderator ', 'moderator'], 1]);
    same('"+ 4 guests, not logged in", twice (in the list for a short window, under it otherwise: the style shows one)', texts(r.panel(), 'pl-guests'), ['…+ 4 guestsnot logged in', '…+ 4 guestsnot logged in']);
    same('each card says its status for the style (the dot of its circle)', byClass(r.panel(), 'pl-card').map((c) => c.getAttribute('data-s')), ['look', 'look', 'look', 'look', 'look', 'wait', 'wait', 'play', 'play'].slice(0, 0).concat(['look', 'look', 'look', 'look', 'look', 'wait', 'wait', 'play', 'play']));
    same('a Join on the row of a public room that waits (and no other), named for the room', [r.joins().length, r.joins()[0].getAttribute('aria-label'), r.joins()[0].getAttribute('data-code'), r.joins()[0].textContent], [1, 'Join clover’s room, Treasure · 2 of 4', 'c1v3rr', 'Join']);
    {
        // Away: the hub's own word for a person whose last socket closed a short while ago (docs/HUB.md "Away"): a name and the word, in a group of their own at the bottom, a moderator's row stays among the moderators, dimmed by the style; no Join, whatever else the frame holds
        const a = rig();
        a.listed('c1v3rr', 'Treasure');
        a.live();
        a.who(FULL([row(1, 'maple'), row(2, 'tess', 'look', { mod: true }), row(3, 'sage'), row(4, 'moss', 'away'), row(5, 'dawn', 'away', { pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr', priv: true }), row(6, 'staffer', 'away', { mod: true, role: 'mod' })], { n: 3, g: 0 }));
        same('the Away group is last, by name; a moderator who is away stays with the moderators', a.groups(), [{ g: 'mod', n: '2', names: ['staffer', 'tess'] }, { g: 'look', n: '1', names: ['sage'] }, { g: 'away', n: '2', names: ['dawn', 'moss'] }]);
        same('the card of an Away row says its status for the style, has the word where no group says it (a moderator) and nothing else, and no Join even where the frame names a room that the Games list has', [byClass(a.panel(), 'pl-card').filter((c) => c.getAttribute('data-s') === 'away').length, texts(byClass(a.panel(), 'pl-card').find((c) => one(c, 'pl-nm').textContent === 'staffer'), 'pl-meta').join(), a.joins().length, texts(byClass(a.panel(), 'pl-card').find((c) => one(c, 'pl-nm').textContent === 'dawn'), 'pl-meta').length], [3, 'Away: Away', 0, 0]);
        a.who({ t: 'who', full: false, n: 3, g: 0, more: 0, rows: [row(4, 'moss')], gone: [] });
        same('a row that is back is Looking again (a change of its word)', a.groups().map((g) => g.g + ':' + g.names.join('+')), ['mod:staffer+tess', 'look:moss+sage', 'away:dawn']);
    }
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
    const r = rig();
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
    const r = rig();
    r.live(); r.who(FULL([row(1, 'maple')], { n: 1 }));
    same('just the visitor: only the visitor\'s own row; no note, no button, no groups', [texts(r.panel(), 'pl-note').length, byClass(r.panel(), 'pl-host').length, r.panel().findAll((e) => e.tagName === 'button' && e.className !== 'pl-tab').length, r.names(), r.groups().length], [0, 0, 0, ['maple'], 0]);       // (the two tabs of a person who is logged in are buttons: none else)
    same('... and nothing in the list under the row says anything', texts(one(r.panel(), 'pl-list'), 'pl-note').length + byClass(one(r.panel(), 'pl-list'), 'pl-card').length, 0);
    const guest = rig();
    guest.live(W({ you: null })); guest.who(FULL([], { n: 1, g: 1 }));
    check('a guest alone: no note, no row, no guests line', texts(guest.panel(), 'pl-note').length === 0 && byClass(guest.panel(), 'pl-card').length === 0 && byClass(guest.panel(), 'pl-guests').length === 0);
}
{   // more people than rows, and a crowd
    const r = rig();
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
    const r = rig();
    const clicks = [];
    Object.assign(r.listed('c1v3rr', 'Treasure'), { click() { clicks.push('c1v3rr'); } });
    r.live(); r.who(FULL(CROWD, { n: 13, g: 4 }));
    r.joins()[0].click();
    same('Join presses the Games list\'s own Join of that room', clicks, ['c1v3rr']);
    // a row of the Games list that is gone between the drawing and the press, in a list that is a popover (the test site): Games is pressed to open it, and its Join is there again if the room is
    const p = rig();
    p.listed('c1v3rr', 'Treasure');
    p.live(); p.who(FULL(CROWD, { n: 13, g: 4 }));
    p.unlisted('c1v3rr');
    p.games.click = () => { p.games.attributes['aria-expanded'] = 'true'; p.found['.gjoin[data-code="c1v3rr"]'] = Object.assign(new El(p.env, 'button'), { click() { clicks.push('popover'); } }); };
    p.games.setAttribute('aria-haspopup', 'dialog'); p.games.setAttribute('aria-expanded', 'false');
    p.joins()[0].click();
    same('a Games list that is a popover (the test site) is opened, and then its Join is pressed', clicks.slice(1), ['popover']);
    // a game that is not there any more
    const q = rig();
    q.listed('c1v3rr', 'Treasure');
    q.live(); q.who(FULL(CROWD, { n: 13, g: 4 }));
    q.unlisted('c1v3rr');
    q.joins()[0].click();
    check('a game that the list has not got any more says so (a few seconds), and presses nothing', texts(q.panel(), 'pl-status').join() === 'That game is not open any more.');
    q.tick(4100);
    check('... and the words go by themselves', texts(q.panel(), 'pl-status').join() === '');
    const docked = rig();
    docked.listed('c1v3rr', 'Treasure');
    docked.live(); docked.who(FULL(CROWD, { n: 13, g: 4 }));
    docked.unlisted('c1v3rr');
    docked.games.click = () => clicks.push('wrong');
    docked.joins()[0].click();
    check('a list that is docked has no popup: Games is not pressed for a game that is not in it', clicks.indexOf('wrong') === -1);
    // no row in the Games list, no Join (and a Join comes by itself when the list gets the room)
    const n = rig();
    n.live(); n.who(FULL(CROWD, { n: 13, g: 4 }));
    check('a room that waits and that the Games list does not have: no Join', n.joins().length === 0);
    n.listed('c1v3rr', 'Treasure'); n.tick(1000);
    check('... the Games list gets it: the Join comes at the next look of the page', n.joins().length === 1);
    n.unlisted('c1v3rr'); n.tick(1000);
    check('... and goes when the list loses it', n.joins().length === 0);
}
{   // a map of the players: its name is drawn only where the Games list shows that room under the same name; the hub\'s words are never drawn
    const comm = (name, code) => row(6, 'clover', 'wait', { pub: true, map: name + '.lvl', n: 2, of: 4, code: code || 'c0mm00' });
    const r = rig();
    r.live(); r.who(FULL([row(1, 'maple'), comm('Bomber Match  by PRo')], { n: 3 }));
    same('a map of the players that the Games list does not have: no name, only the seats', r.metaOf('clover'), '2 of 4');
    r.listed('c0mm00', 'Bomber Match  by PRo'); r.tick(1000);
    same('... the Games list has the room under that name: the name is drawn', r.metaOf('clover'), 'Bomber Match  by PRo · 2 of 4');
    r.unlisted('c0mm00'); r.listed('c0mm00', 'Another Map'); r.tick(1000);
    same('... the Games list has the room under another name: the hub\'s word is not drawn, and the list\'s is not either', [r.metaOf('clover'), r.joins().length], ['2 of 4', 1]);
    const lie = rig();
    lie.live(); lie.who(FULL([row(1, 'maple'), row(6, 'mallory', 'wait', { pub: true, map: 'Free nitro at discord.gg-abc.LVL', n: 1, of: 4, code: 'f4k3f4' })], { n: 3 }));
    same('a made-up map and code: no name, no Join', [lie.metaOf('mallory'), lie.joins().length, JSON.stringify(texts(lie.panel(), 'pl-card')).indexOf('nitro')], ['1 of 4', 0, -1]);
    lie.listed('f4k3f4', 'Treasure'); lie.tick(1000);
    same('a code of a room that the Games list has, with a map that is not that room\'s: the name is not drawn (a Join to that room is what the list offers)', [lie.metaOf('mallory'), JSON.stringify(texts(lie.panel(), 'pl-card')).indexOf('nitro')], ['1 of 4', -1]);
    const play = rig();
    play.live(); play.who(FULL([row(1, 'maple'), row(7, 'pat', 'play', { pub: true, map: 'Bomber Match  by PRo.lvl', n: 3 }), row(8, 'orig', 'play', { pub: true, map: 'ISLANDS.LVL', n: 2 })], { n: 3 }));
    same('a match: an original\'s name, never a map of the players\' (a match has no code to look up)', [play.metaOf('pat'), play.metaOf('orig')], ['3 players', 'Islands · 2 players']);
    const own = rig();
    own.state.client = CLIENT(ROOM({ isPublic: true, map: 'Bomber Match  by PRo.lvl' }), 'c0mm00'); own.state.count = { people: 1, size: 4 };
    own.live(); own.who(FULL([row(1, 'maple')], { n: 1 }));
    check('the visitor\'s own room: its map\'s name is the page\'s own word, drawn', texts(one(own.panel(), 'pl-you'), 'pl-meta').join().indexOf('Bomber Match  by PRo · 1 of 4') !== -1);
    const mine = rig();
    mine.live(); mine.who(FULL([row(1, 'maple'), row(2, 'maple', 'wait', { pub: true, map: 'TREASURE.LVL', n: 1, of: 4, code: 'c1v3rr' }), row(3, 'sage')], { n: 4 }));
    same('a person who has the visitor\'s name is not hidden (only the row that is the visitor\'s number is)', mine.names().sort(), ['maple', 'maple', 'sage'].sort());
}
{   // an original's name: the page's own table's (a closed set of six), and for a room that the Games list has, only if the list shows the same name
    const NAMES = { 'TINY.LVL': 'Tiny', 'SMALL.LVL': 'Small', 'MEDIUM.LVL': 'Medium', 'GAUNTLET.LVL': 'Gauntlet', 'TREASURE.LVL': 'Treasure', 'ISLANDS.LVL': 'Islands' };
    const waits = (map, code) => row(6, 'mallory', 'wait', { pub: true, map, n: 4, of: 4, code: code || 'f4k3f4' });
    const liar = rig();
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
    const spelled = rig();
    spelled.live(); spelled.who(FULL([row(1, 'maple')].concat(cases.map((c, i) => row(10 + i, 'p' + String(i).padStart(2, '0'), 'wait', { pub: true, map: c[0], n: 1, of: 4 }))), { n: cases.length + 1 }));
    same('the six originals, in every spelling that the page\'s rule takes of the file name, are drawn by the table\'s name and nothing else', cases.map((c, i) => spelled.metaOf('p' + String(i).padStart(2, '0'))), cases.map((c) => c[1] + ' · 1 of 4'));
    // a run of file names of any shape: what is drawn is never a word of the hub's, but the table's six names (the closed set) or, where the list confirms it, the list's own name
    let seed = 20261108;
    const chance = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
    const chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-!@#$%^&()+=,;\'[]{}~';
    const maps = [];
    for (let i = 0; i < 160; i++) { let n = 1 + Math.floor(chance() * 40), t = ''; while (n--) t += chars.charAt(Math.floor(chance() * chars.length)); maps.push(t + (chance() < 0.5 ? '.lvl' : '.LVL')); }
    const wild = rig();
    const rows = maps.map((map, i) => (i % 2 ? row(10 + i, 'w' + i, 'wait', { pub: true, map, n: 1 + (i % 4), of: 4, code: 'c' + i }) : row(10 + i, 'w' + i, 'play', { pub: true, map, n: 1 + (i % 4) })));
    wild.live(); wild.who(FULL([row(1, 'maple')].concat(rows), { n: rows.length + 1 }));
    const SHAPES = /^(?:(?:Tiny|Small|Medium|Gauntlet|Treasure|Islands) · )?(?:\d of \d|\d players?)$/;
    check('file names of any shape, in rooms that the Games list does not have: the line is the seats or the players, and a name before them is one of the six', rows.every((r) => SHAPES.test(wild.metaOf(r.name))), rows.map((r) => wild.metaOf(r.name)).filter((t) => !SHAPES.test(t)).join(' | '));
    rows.forEach((r, i) => { if (i % 2) wild.listed('c' + i, i % 6 === 1 ? 'Treasure' : 'Another map'); });
    wild.tick(1000);
    check('... and in rooms that it has, under a name that is not what the hub\'s file is called: the seats only (no word of the hub\'s and none of the list\'s)', rows.every((r, i) => !(i % 2) || /^\d of \d$/.test(wild.metaOf(r.name))), rows.map((r, i) => (i % 2 ? wild.metaOf(r.name) : '')).filter((t) => t && !/^\d of \d$/.test(t)).join(' | '));
    check('... a page that says a room of the players is the one that the list shows under the same name has that name drawn (by a map of that name, in a room of its own)', (() => { const q = rig(); q.live(); q.who(FULL([row(1, 'maple'), waits('Same Name.lvl', 'c0mm00')], { n: 3 })); q.listed('c0mm00', 'Same Name'); q.tick(1000); return q.metaOf('mallory') === 'Same Name · 4 of 4'; })());
}
{   // a room that the hub says nothing of is not called private: the hub keeps no details of a private room nor of the room of an account that may not chat
    const r = rig();
    r.live(); r.who(FULL([row(1, 'maple'), row(2, 'pat', 'wait'), row(3, 'quinn', 'play'), row(4, 'rae', 'wait', { pub: true, code: 'c0mm00' })], { n: 4 }));
    same('in the lines under the names of others: "In a room" and "In a game", and no word "Private"', [r.metaOf('pat'), r.metaOf('quinn'), r.metaOf('rae'), JSON.stringify(texts(r.panel(), 'pl-card')).indexOf('Private')], ['In a room', 'In a game', 'Public room', -1]);
}
{   // the tag of a name (docs/HUB_NICKNAMES.md): a row carries the person's tag, drawn before the name in its own dimmer node, and the visitor's own row too
    const r = rig();
    r.live(W({ you: { name: 'Zo\u00eb', tag: '\u00c4NT', mod: false, role: 'none', nick: 'Zo\u00eb' }, me: 1 }));
    r.who(FULL([row(1, 'Zo\u00eb'), Object.assign(row(2, 'Wren (Guest)'), { tag: undefined }), Object.assign(row(3, 'Fern'), { tag: 'FERN' }), Object.assign(row(4, 'Eve'), { tag: '<b>' })], { n: 5 }));
    const names = byClass(r.panel(), 'pl-nm');
    same('every row has its name as text; a tag is its own node with its brackets, before the name; a tag that is no tag is none', names.map((n) => n.textContent), ['[\u00c4NT] Zo\u00eb', 'Eve', '[FERN] Fern', 'Wren (Guest)']);       // (the visitor first, then the people by name)
    same('...in a node of the class nm-tag, not in the name', names.map((n) => byClass(n, 'nm-tag').map((t) => t.textContent)), [['[\u00c4NT]'], [], ['[FERN]'], []]);
    const mine = byClass(r.panel(), 'pl-card')[0];
    check('the visitor\'s own row is the first and has the tag of the visitor (the page\'s word, not the hub\'s last)', !!mine && byClass(mine, 'pl-nm')[0].textContent === '[\u00c4NT] Zo\u00eb' && byClass(mine, 'pl-tag')[0].textContent === 'you');
    const plain = rig();
    plain.live(W({ you: { name: 'maple', mod: false, role: 'none' }, me: 1 }));
    plain.who(FULL([row(1, 'maple')], { n: 1 }));
    check('a visitor who has no tag has a row of the name alone', byClass(plain.panel(), 'pl-nm')[0].textContent === 'maple' && byClass(plain.panel(), 'nm-tag').length === 0);
}
{   // the visitor's own row is the one that the hub's welcome names, not a row with the visitor's name
    const youAnd = (r) => r.names().filter((n) => n === 'maple').length;
    const twins = [row(1, 'maple'), row(2, 'maple', 'wait'), row(3, 'sage')];
    const r = rig();
    r.live(W({ me: 2 })); r.who(FULL(twins, { n: 4 }));
    same('the welcome names the row (2): that one is left out of the groups, the other person called maple is not', [youAnd(r), r.groups().map((g) => g.g + ':' + g.names.join())], [2, ['look:maple,sage']]);
    r.live(W({ me: 1 })); r.who(FULL(twins, { n: 4 }));
    same('a new welcome (a new socket) names its own row: now it is the first that is left out', [youAnd(r), r.groups().map((g) => g.g + ':' + g.names.join())], [2, ['look:sage', 'wait:maple']]);
    ['2', -1, 1.5, 1e12, null, undefined, {}, [1], true].forEach((me) => {
        const q = rig();
        q.live(W({ me })); q.who(FULL([row(1, 'maple'), row(3, 'sage')], { n: 3 }));
        check('a welcome whose `me` is ' + JSON.stringify(me) + ' hides no row (nothing is guessed from a name)', youAnd(q) === 2);
    });
    const none = rig();
    none.live(W({ me: undefined })); none.who(FULL([row(1, 'maple')], { n: 2 }));
    check('a welcome with no `me` (a reader\'s, or a hub that does not say) hides no row', youAnd(none) === 2);
    r.lost(); r.live(W({ me: 7 })); r.who(FULL([row(1, 'maple'), row(7, 'maple', 'wait')], { n: 3 }));
    same('a link that was lost and comes back: the number is the new welcome\'s', [youAnd(r), r.groups().map((g) => g.g + ':' + g.names.join())], [2, ['look:maple']]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. No fold
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig();
    r.live(); r.who(FULL(CROWD, { n: 13, g: 4 }));
    check('the panel has no fold button and no folded state: it is always open', byClass(r.panel(), 'pl-fold').length === 0 && byClass(r.panel(), 'pl-mark').length === 0 && r.panel().getAttribute('data-folded') === null);
    const before = r.panel().getAttribute('data-state');
    r.panel().fire('keydown', { key: 'Escape', preventDefault() {} });
    check('Escape in the panel changes nothing (it used to fold it)', r.panel().getAttribute('data-folded') === null && r.panel().getAttribute('data-state') === before && byClass(r.panel(), 'pl-fold').length === 0);
    r.who(FULL(CROWD.concat([row(20, 'newcomer')]), { n: 14, g: 4 }));
    check('the list still changes while open: the rows of the list are drawn', byClass(r.panel(), 'pl-card').length > 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 6. What the hub may send
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig();
    r.live(); r.who(FULL([row(1, 'maple'), row(2, 'sage')], { n: 3, g: 1 }));
    const bad = [null, 'who', 7, [], {}, { t: 'who' }, { t: 'who', full: true, n: 'x', g: 0, more: 0, rows: [] }, { t: 'who', full: true, n: 3, g: -1, more: 0, rows: [] }, { t: 'who', full: true, n: 3, g: 0, more: 0, rows: { 0: row(1, 'zz') } },
        { t: 'who', full: true, n: 3, g: 0, more: 0, rows: Array.from({ length: 300 }, (_, i) => row(i, 'p' + i)) }, { t: 'who', full: false, n: 3, g: 0, more: 0, rows: [], gone: { 0: 1 } }];
    bad.forEach((frame) => r.who(frame));
    check('a frame that is wrong in any part is ignored: the list is what it was', r.names().join() === 'maple,sage' && r.count() === '3' && r.errors.length === 0);
    r.emit('nonsense', {}); r.emit('who'); r.emit('who', undefined);
    check('an unknown kind, a frame that is not there: nothing', r.names().join() === 'maple,sage' && r.errors.length === 0);
    r.who(FULL([row(1, 'maple'), row(2, '<img src=x onerror=alert(1)>'), row(3, '‮gnilleps'), row(4, 'long'.repeat(30)), row(5, 'z', 'wait', { pub: true, map: '<script>.LVL', code: '"><b>' })], { n: 6 }));
    check('untrusted text is text: no markup was made, no element but the list\'s own kinds, the name cut and clean, a map and a code that are not what they should be are none', r.env.innerHTMLWrites.length === 0
        && r.names().join('|') === 'maple|<img src=x onerror=alert(1)>|gnilleps|' + 'long'.repeat(8) + '…|z'
        && r.panel().findAll((e) => ['img', 'script', 'a', 'iframe', 'style', 'link'].indexOf(e.tagName) !== -1).length === 0 && r.metaOf('z') === 'Public room' && r.joins().length === 0);
    // a thousand frames of garbage and a thousand of rows
    for (let i = 0; i < 1000; i++) r.who({ t: 'who', full: false, n: 3, g: 0, more: 0, rows: [row(i % 150, 'q' + (i % 150), i % 3 ? 'look' : 'wait')], gone: [(i + 75) % 150] });
    check('a thousand changes: the rows stay under 200, nothing throws', Object.keys(r.list.model().rows).length <= 200 && r.errors.length === 0);
    // a dock that throws in its tap, or a page that throws when asked
    const t = rig();
    t.state.client = { get room() { throw new Error('a page that breaks'); } };
    t.live();
    check('a page that throws when asked about its room: the list reports it and its other parts go on', t.errors.length >= 1);
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
    const yard = home.children.find((c) => c.id === 'hmain');
    check('... then its box is in Home\'s body, and Home\'s own parts are where they were', yard.children.some((c) => cls(c).indexOf('pl-root') !== -1) && home.children.slice(0, 3).map((c) => c.tagName).join() === 'header,main,nav' && home.children[3].id === 'gchat');
    hub[0].readyState = 1; hub[0].onmessage({ data: JSON.stringify(W({ you: { name: 'maya', mod: false }, online: 2 })) });
    check('the dock\'s socket carries the list\'s words: the visitor says where it is and asks for the list, on the same socket (no second socket)', hub.length === 1 && hub[0].sent.map((x) => JSON.parse(x)).filter((m) => m.t === 'here' || m.t === 'who').map((m) => m.t).join() === 'here,who');
    hub[0].onmessage({ data: JSON.stringify(FULL([row(1, 'maya'), row(2, 'sage')], { n: 2 })) });
    const box = yard.children.find((c) => cls(c).indexOf('pl-root') !== -1);
    check('a list from the hub shows (the chat dock\'s count says the same number, 2 here now)', box.hidden === false && one(box, 'pl-count').textContent === '2' && one(home.children[3], 'gc-on').textContent === '2 here now');
    env.advance(10);
    check('the page does not ask for the list again (the same two files)', filesOf(env, 'players').length === 2);
    env.makeRoom();
    env.toggleRoom();
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
    check('a list that throws when it mounts: Home goes on (the page still makes a room, the dock is there)', broken.$('home').children[3].id === 'gchat' && (broken.makeRoom(), broken.sockets.length === 1) && broken.screen() === 'room');
    const seen = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    hubFor(seen); seen.advance(0); bringChat(seen);
    let given = null;
    seen.win.AntsHomePlayers = { mount(o) { given = o; return null; } };
    filesOf(seen, 'players').forEach((n) => { if (n.onload) n.onload(); });
    same('the list is given the document, the window, Home\'s box and body, the two buttons of the bar that it uses, the dock and the page\'s bridge, and nothing else', given && Object.keys(given).sort(), ['doc', 'dock', 'games', 'host', 'page', 'quickHost', 'win', 'yard']);
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
    const yard = env.$('home').children.find((c) => c.id === 'hmain');
    const box = yard.children.find((c) => cls(c).indexOf('pl-root') !== -1);
    check('(the list is there and takes presses)', !!box && !box.hasAttribute('inert'));
    env.makeRoom();
    env.arrive({ key: KEY, seats: [[C, 'Maya'], [E, ''], [E, ''], [E, '']] });
    env.$('change-map').click();
    check('a dialog over the page makes the list inert with the bars and the dock', box.hasAttribute('inert') && env.home('hbar').hasAttribute('inert'));
    const late = runLobby('', { 'ants.name': 'Sam' }, { stayHome: true });
    hubFor(late); late.advance(0); bringChat(late);
    late.makeRoom();
    const sock = late.arrive({ key: KEY, seats: [[C, 'Sam'], [E, ''], [E, ''], [E, '']] });
    sock.receive(roomMessage([[C, 'Sam'], [C, 'Maya'], [E, ''], [E, '']], { you: 0, leader: 0, flags: 7 }));
    bringPlayers(late);
    const lateYard = late.$('home').children.find((c) => c.id === 'hmain');
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
    check('the only thing kept in the browser is the hand-over note of this tab (ants.here), and no socket is opened by the list itself', (code.match(/win\.sessionStorage/g) || []).length === 1 && /'ants\.here'/.test(code) && !/localStorage|indexedDB|document\.cookie|new WebSocket|WebSocket\(|XMLHttpRequest|fetch\(|navigator\.sendBeacon/.test(code));
    check('it never names the hub\'s door (the dock\'s script alone knows it)', !/\/hub\b/.test(code));
    same('it makes only these kinds of element', [...new Set([...code.matchAll(/\bel\('(\w+)'/g)].map((m) => m[1]).concat([...code.matchAll(/\bbutton\('/g)].length ? ['button'] : []))].sort(), ['bdi', 'button', 'div', 'h2', 'h3', 'hr', 'i', 'li', 'p', 'section', 'span', 'ul']);
}


// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 9. The roles: the role of a name, red or yellow, with the word in the accessible name (the hub's `mod` is true for both; `role` says which), and the visitor's own row that follows a live change
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Pure;
    same('a row\'s role is the hub\'s `role` where `mod` is true: admin, mod; a row that says admin and not mod, a word that is neither, or no role is mod or none',
        [P.rowOf({ i: 1, name: 'a', at: 'look', mod: true, role: 'admin' }, Chat, Rules).role, P.rowOf({ i: 1, name: 'a', at: 'look', mod: true, role: 'mod' }, Chat, Rules).role, P.rowOf({ i: 1, name: 'a', at: 'look', mod: true }, Chat, Rules).role, P.rowOf({ i: 1, name: 'a', at: 'look', mod: true, role: 'root' }, Chat, Rules).role,
         P.rowOf({ i: 1, name: 'a', at: 'look', mod: false, role: 'admin' }, Chat, Rules).role, P.rowOf({ i: 1, name: 'a', at: 'look', mod: 'true', role: 'admin' }, Chat, Rules).role], ['admin', 'mod', 'mod', 'mod', 'none', 'none']);
    const r = rig();
    r.live(W({ you: { name: 'maple', mod: false, role: 'none' } }));
    r.who(FULL([row(2, 'zed', 'look', { mod: true, role: 'mod' }), row(3, 'boss', 'look', { mod: true, role: 'admin' }), row(4, 'amy', 'look', { mod: true }), row(5, 'sage')], { n: 5 }));
    const cardOf = (name) => byClass(r.panel(), 'pl-card').find((c) => one(c, 'pl-nm').textContent === name);
    const markOf = (card) => card ? [cls(card).filter((c) => c === 'r-admin' || c === 'r-mod').join(''), one(one(card, 'pl-name'), 'pl-sr') ? one(one(card, 'pl-name'), 'pl-sr').textContent : '', one(card, 'pl-nm').getAttribute('title')] : null;
    const badgeOf = (name) => markOf(cardOf(name));
    same('the Staff group has both, the admins first, then by name; each name has its role (red for the admin, yellow for the moderator, the word in the accessible name and the title), and a person with no role has none', [r.groups()[0].names, badgeOf('boss'), badgeOf('amy'), badgeOf('zed'), badgeOf('sage')],
        [['boss', 'amy', 'zed'], ['r-admin', 'administrator ', 'administrator'], ['r-mod', 'moderator ', 'moderator'], ['r-mod', 'moderator ', 'moderator'], ['', '', null]]);
    const heading = (q) => texts(q.panel(), 'pl-gh').map((t) => t.replace(/\d+$/, ''));
    const only = (rows) => { const q = rig(); q.live(W({ you: { name: 'maple', mod: false, role: 'none' } })); q.who(FULL(rows, { n: rows.length + 1 })); return q; };
    same('the group of the staff is titled "Staff" whoever is in it: both roles, only administrators, only moderators; a person with no role is never in it, and with none of the staff there is no such group', [heading(r)[0], heading(only([row(3, 'boss', 'look', { mod: true, role: 'admin' })]))[0], heading(only([row(2, 'zed', 'look', { mod: true, role: 'mod' })]))[0], heading(only([row(5, 'sage')])).join(), only([row(2, 'zed', 'look', { mod: true }), row(5, 'sage')]).groups().map((g) => g.g + ':' + g.names.join('+')).join()],
        ['Staff', 'Staff', 'Staff', 'Looking', 'mod:zed,look:sage']);
    check('... and no group says moderators any more (the title is the page\'s word, and the accessible name of the group is that title)', texts(r.panel(), 'pl-gh').every((t) => !/moderators/i.test(t)) && byClass(r.panel(), 'pl-group').every((g) => one(g, 'pl-gh') !== null));
    r.who({ t: 'who', full: false, n: 5, g: 0, more: 0, rows: [row(2, 'zed', 'look', { mod: true, role: 'admin' })], gone: [] });
    same('a change that makes a moderator an administrator is drawn at once (the role, the place in the group)', [r.groups()[0].names, badgeOf('zed')], [['boss', 'zed', 'amy'], ['r-admin', 'administrator ', 'administrator']]);
    const inPlace = rig(); inPlace.live(W({ you: { name: 'maple', mod: false, role: 'none' } }));
    inPlace.who(FULL([row(3, 'boss', 'look', { mod: true, role: 'admin' }), row(2, 'zed', 'look', { mod: true, role: 'mod' })], { n: 3 }));
    inPlace.who({ t: 'who', full: false, n: 3, g: 0, more: 0, rows: [row(2, 'zed', 'look', { mod: true, role: 'admin' })], gone: [] });
    const zed = byClass(inPlace.panel(), 'pl-card').find((c) => one(c, 'pl-nm').textContent === 'zed');
    same('... also where nothing moves (a moderator who is in place becomes an administrator: the class and the word change)', [inPlace.groups()[0].names, markOf(zed)], [['boss', 'zed'], ['r-admin', 'administrator ', 'administrator']]);
    r.emit('you', { t: 'you', you: { name: 'maple', mod: true, role: 'admin' } });
    const own = () => markOf(one(r.panel(), 'pl-you') && one(one(r.panel(), 'pl-you'), 'pl-card'));
    same('the visitor\'s own row follows a live `you` at once: red, then yellow, then nothing', [own(), (r.emit('you', { t: 'you', you: { name: 'maple', mod: true, role: 'mod' } }), own()), (r.emit('you', { t: 'you', you: { name: 'maple', mod: false, role: 'none' } }), own())],
        [['r-admin', 'administrator ', 'administrator'], ['r-mod', 'moderator ', 'moderator'], ['', '', null]]);
    r.emit('you', { t: 'you', you: { name: 'maple', mod: true, role: 'admin' } });
    r.emit('you', { t: 'you' }); r.emit('you', { t: 'you', you: 'admin' }); r.emit('you', null);
    check('a `you` that is not a visitor changes nothing and throws nothing', own()[0] === 'r-admin' && r.errors.length === 0);
    const reader = rig(); reader.live(W({ you: null })); reader.who(FULL([row(2, 'zed', 'look', { mod: true })], { n: 2 })); reader.emit('you', { t: 'you', you: { name: 'maple', mod: true, role: 'admin' } });
    check('a reader that is told a `you` is still a reader (no own row)', one(reader.panel(), 'pl-you') === null && reader.errors.length === 0);
    const old = rig(); old.live(W({ you: { name: 'moddy', mod: true } })); old.who(FULL([], { n: 1 }));
    check('a hub that says `mod` only: the visitor is a moderator (yellow)', markOf(one(old.panel(), 'pl-card'))[0] === 'r-mod');
    const flag = rig(); flag.live(W({ you: { name: 'odd', mod: true, role: 'none' } })); flag.who(FULL([], { n: 1 })); flag.emit('you', { t: 'you', you: { name: 'odd', mod: true, role: 'none' } });
    check('a `you` that says role none and mod true is nobody on the list as it is in the chat (one rule for both)', markOf(one(flag.panel(), 'pl-card'))[0] === '');
    const adm = rig(); adm.live(W({ you: { name: 'boss', mod: true, role: 'admin' } })); adm.who(FULL([], { n: 1 }));
    check('a welcome that says admin: the own row is red at once', markOf(one(adm.panel(), 'pl-card'))[0] === 'r-admin');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 10. The pictures: an img over the letter in the circle of a row and of the visitor's own card, made only from a checked 32-character name
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const NAME = 'a1b2c3d4e5f60718293a4b5c6d7e8f90', OTHER = '0123456789abcdef0123456789abcdef';
    const circleOf = (q, name) => { const c = byClass(q.panel(), 'pl-card').find((x) => one(x, 'pl-nm').textContent === name); return c ? one(c, 'pl-av') : null; };
    const imgsOf = (circle) => circle.children.filter((c) => c.tagName === 'img');
    const q = rig();
    q.live(W({ you: { name: 'maple', mod: false, role: 'none', pics: 'on', pic: OTHER } }));
    q.who(FULL([row(2, 'sage', 'look', { pic: NAME }), row(3, 'tess')], { n: 4 }));
    const sage = circleOf(q, 'sage'), img = imgsOf(sage)[0];
    same('a row with a valid picture name has one img in its circle, with the address made of the name and the attributes the page sets', [imgsOf(sage).length, img && img.attributes],
        [1, { src: '/hub/pic/' + NAME + '.png', alt: '', width: '32', height: '32', loading: 'lazy', decoding: 'async', draggable: 'false' }]);
    same('the letter is still in the circle, and a row with no picture has no img', [sage.textContent, imgsOf(circleOf(q, 'tess')).length], ['s', 0]);
    const tg = rig();
    tg.live(W({ you: { name: 'maple', mod: false, role: 'none' } }));
    tg.who(FULL([row(2, 'Fern', 'look', { tag: 'FERN' }), row(3, '[ANTS1] Wolfgang', 'wait')], { n: 3 }));
    same('the circle of a row with a tag holds the letter of the nickname (never the bracket), whether the tag comes in its own field or in the name', [circleOf(tg, '[FERN] Fern').textContent, circleOf(tg, '[ANTS1] Wolfgang').textContent], ['F', 'W']);
    check('the visitor\'s own card has the visitor\'s picture', imgsOf(one(one(q.panel(), 'pl-you'), 'pl-av')).length === 1 && imgsOf(one(one(q.panel(), 'pl-you'), 'pl-av'))[0].attributes.src === '/hub/pic/' + OTHER + '.png');
    check('the letter is not hidden before the picture has loaded', sage.getAttribute('data-pic') === null);
    img.fire('load');
    check('a picture that loaded hides the letter (the circle says so)', sage.getAttribute('data-pic') === 'on');
    const GONE = 'cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd', FRESH = '5656565656565656565656565656abab';
    q.who({ t: 'who', full: false, n: 4, g: 0, more: 0, rows: [row(3, 'tess', 'look', { pic: GONE })], gone: [] });
    const tess = circleOf(q, 'tess');
    check('a row that gains a picture is drawn again with it', imgsOf(tess).length === 1 && imgsOf(tess)[0].attributes.src === '/hub/pic/' + GONE + '.png');
    imgsOf(tess)[0].fire('error');
    same('a picture that fails to load is taken away and the letter is still there, not hidden', [imgsOf(tess).length, tess.textContent, tess.getAttribute('data-pic')], [0, 't', null]);
    // the list is made again at every change: a picture that failed is not asked for again, one that loaded and a new one are as before
    let failedAgain = 0, drawn = 0;
    for (let i = 0; i < 12; i++) {
        q.who({ t: 'who', full: false, n: 4, g: 0, more: 0, rows: [row(5, 'wade', i % 2 ? 'look' : 'wait', { pic: GONE })], gone: [] });
        failedAgain += q.panel().findAll((e) => e.tagName === 'img' && e.attributes.src === '/hub/pic/' + GONE + '.png').length;
        drawn += q.panel().findAll((e) => e.tagName === 'img' && e.attributes.src === '/hub/pic/' + NAME + '.png').length;
    }
    same('after a picture failed, 12 repaints of the list (the failed name in two rows) make no img for it; the one that loaded is made as before', [failedAgain, drawn], [0, 12]);
    q.who({ t: 'who', full: false, n: 4, g: 0, more: 0, rows: [row(6, 'xena', 'look', { pic: FRESH })], gone: [5] });
    same('a new name is asked for as before', imgsOf(circleOf(q, 'xena')).map((i) => i.attributes.src), ['/hub/pic/' + FRESH + '.png']);
    // a row that loses its picture is drawn again with the letter
    q.who({ t: 'who', full: false, n: 4, g: 0, more: 0, rows: [row(2, 'sage', 'look')], gone: [] });
    const lost = circleOf(q, 'sage');
    same('a row that loses `pic` is drawn again: no img, the letter, nothing hidden', [imgsOf(lost).length, lost.textContent, lost.getAttribute('data-pic')], [0, 's', null]);
    q.who({ t: 'who', full: false, n: 4, g: 0, more: 0, rows: [row(2, 'sage', 'look', { pic: OTHER })], gone: [] });
    same('a row that gains one is drawn again with it, and one that changes it has the new address', [imgsOf(circleOf(q, 'sage')).map((i) => i.attributes.src)], [['/hub/pic/' + OTHER + '.png']]);
    // the visitor's own picture follows a live `you`
    q.emit('you', { t: 'you', you: { name: 'maple', mod: false, role: 'none' } });
    check('a `you` without `pic` takes the own card\'s picture away', imgsOf(one(one(q.panel(), 'pl-you'), 'pl-av')).length === 0);
    q.emit('you', { t: 'you', you: { name: 'maple', mod: false, role: 'none', pics: 'on', pic: NAME } });
    check('a `you` with one puts it on', imgsOf(one(one(q.panel(), 'pl-you'), 'pl-av')).map((i) => i.attributes.src).join() === '/hub/pic/' + NAME + '.png');
    for (const pics of ['off', 'blocked', undefined, 'maybe']) {
        q.emit('you', { t: 'you', you: { name: 'maple', mod: false, role: 'none', pics, pic: NAME } });
        check('a `you` that carries a name but whose `pics` is ' + JSON.stringify(pics) + ' shows no picture on the own card (the picture is shown only where the switch says on)', imgsOf(one(one(q.panel(), 'pl-you'), 'pl-av')).length === 0);
    }
    q.emit('you', { t: 'you', you: { name: 'maple', mod: false, role: 'none', pics: 'on', pic: NAME } });
    q.lost(); q.live(W({ you: { name: 'maple', mod: false, role: 'none', pics: 'on' } })); q.who(FULL([row(2, 'sage')], { n: 3 }));
    check('after the link is closed and made again, the welcome says who the visitor is now: a welcome without `pic` leaves the own card with its letter', imgsOf(one(one(q.panel(), 'pl-you'), 'pl-av')).length === 0 && one(one(q.panel(), 'pl-you'), 'pl-av').textContent === 'm');
    q.lost(); q.live(W({ you: null })); q.who(FULL([row(2, 'sage')], { n: 2 }));
    check('... and a welcome for a guest has no own card at all', one(q.panel(), 'pl-you') === null || byClass(q.panel(), 'pl-you').length === 0 || one(one(q.panel(), 'pl-you'), 'pl-av') === null);
    // names that are not a picture's
    const bad = ['A1B2C3D4E5F60718293A4B5C6D7E8F90', NAME.slice(0, 31), NAME + '0', '../' + NAME.slice(3), 'https://x.example/' + NAME + '.png', '/hub/pic/' + NAME + '.png', 12345678901234567890123456789012, { toString: () => NAME }, [NAME], null, true, '', ' ' + NAME.slice(1), NAME + '\n', NAME.slice(0, 31) + 'g', NAME.slice(0, 31) + '\u0661'];
    const odd = rig();
    odd.live(W({ you: { name: 'maple', mod: false, role: 'none', pic: bad[0] } }));
    odd.who(FULL(bad.map((b, i) => row(10 + i, 'p' + i, 'look', { pic: b })), { n: bad.length + 1 }));
    check('every name that is not 32 lower-case hex characters makes no img (capitals, 31, 33, a path, an address, a number, an object, null, a line end), in rows and in the own card',
        odd.panel().findAll((e) => e.tagName === 'img').length === 0 && byClass(odd.panel(), 'pl-card').length === bad.length + 1 && odd.errors.length === 0 && odd.env.innerHTMLWrites.length === 0);
    const named = rig(); named.live(W({ you: { name: NAME, mod: false, role: 'none' } })); named.who(FULL([row(5, OTHER), row(6, 'x', 'look', { map: NAME, code: NAME })], { n: 3 }));
    check('a picture comes only from the `pic` field: a person whose name (or map, or code) looks like a picture name has none', named.panel().findAll((e) => e.tagName === 'img').length === 0 && byClass(named.panel(), 'pl-card').length === 3);
    same('the row keeps the name only when it is one', [Pure.rowOf({ i: 1, name: 'a', at: 'look', pic: NAME }, Chat, Rules).pic, Pure.rowOf({ i: 1, name: 'a', at: 'look', pic: NAME.toUpperCase() }, Chat, Rules).pic], [NAME, '']);
    check('no picture was written as markup', q.env.innerHTMLWrites.length === 0 && q.errors.length === 0);
}


// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 9. Who is playing: a private mark, a game against the computer, the visitor's own card, and the hand-over note
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Pure, rules = Rules;
    const got = (raw) => P.rowOf(raw, Chat, rules);
    const base = { i: 5, name: 'ivy' };
    check('a private room or match keeps the mark and nothing else', (() => {
        const a = got(Object.assign({ at: 'wait', priv: true, map: 'SECRET.LVL', n: 2, of: 4, code: 'c0d3' }, base)), b = got(Object.assign({ at: 'play', priv: true, map: 'TREASURE.LVL', n: 2 }, base));
        return a.priv && b.priv && !a.pub && !b.pub && a.map === '' && b.map === '' && a.code === '' && a.n === -1 && b.n === -1 && !a.solo && !b.solo;
    })());
    check('the mark is for a room or a match, never for a visitor who looks, and a public row is not private', !got(Object.assign({ at: 'look', priv: true }, base)).priv && !got(Object.assign({ at: 'play', priv: true, pub: true, map: 'ISLANDS.LVL' }, base)).priv && !got(Object.assign({ at: 'play', priv: 'yes' }, base)).priv);
    const solo = got(Object.assign({ at: 'play', solo: true, map: 'TREASURE.LVL', n: 3, code: 'x1' }, base));
    check('a game against the computer: solo, an original\'s name, no seats, no code', solo.solo && solo.mapName === 'Treasure' && solo.n === -1 && solo.code === '' && !solo.pub && !solo.priv);
    check('solo is for a match only, and an odd map is no name', !got(Object.assign({ at: 'wait', solo: true }, base)).solo && !got(Object.assign({ at: 'look', solo: true }, base)).solo && got(Object.assign({ at: 'play', solo: true, map: '../x.lvl' }, base)).map === '' && got(Object.assign({ at: 'play', solo: true, map: 'MY MAP.lvl' }, base)).mapName === '');
    same('the lines: a private room, a private game, a game against the computer by an original\'s name or by itself', [
        P.metaOf(got(Object.assign({ at: 'wait', priv: true }, base)), false), P.metaOf(got(Object.assign({ at: 'play', priv: true }, base)), false),
        P.metaOf(got(Object.assign({ at: 'play', solo: true, map: 'TREASURE.LVL' }, base)), false), P.metaOf(got(Object.assign({ at: 'play', solo: true }, base)), false), P.metaOf(got(Object.assign({ at: 'play', solo: true, map: 'MY MAP.lvl' }, base)), false)],
    ['Private room', 'Private game', 'Treasure · vs computer', 'vs computer', 'vs computer']);
    const r = rig();
    r.live(W({ me: 1 })).who(FULL([row(1, 'maple'), row(2, 'pat', 'wait', { priv: true }), row(3, 'quinn', 'play', { priv: true }), row(4, 'rex', 'play', { solo: true, map: 'TREASURE.LVL' }), row(5, 'sky', 'play', { pub: true, map: 'ISLANDS.LVL', n: 3 }), row(6, 'tom', 'play')]));
    same('the list: Waiting and Playing rows say what each is', ['pat', 'quinn', 'rex', 'sky', 'tom'].map((n) => r.metaOf(n)), ['Private room', 'Private game', 'Treasure · vs computer', 'Islands · 3 players', 'In a game']);
    const lockOf = (n) => { const c = byClass(r.panel(), 'pl-card').find((x) => one(x, 'pl-nm').textContent === n); return c ? byClass(c, 'pl-lock').length : -1; };
    same('a lock only on the private rows, and no Join or button anywhere on them', [lockOf('pat'), lockOf('quinn'), lockOf('rex'), lockOf('sky'), r.joins().length, r.panel().findAll((e) => e.tagName === 'button' && e.className !== 'pl-tab').length], [1, 1, 0, 0, 0, 0]);
    same('the hub is told a private room is private, and its code, seats and map apart, for invitations', (() => {
        const q = rig({ client: CLIENT(ROOM({ isPublic: false }), 'k7m2xq'), count: { people: 2, size: 4 } });
        q.live(W({})); q.tick(0);
        return q.hereFrames();
    })(), [{ t: 'here', at: 'wait', pub: false, inv: { code: 'k7m2xq', n: 2, of: 4, map: 'TREASURE.LVL' } }]);
}
{   // the visitor's own card: another tab in a match makes it Playing
    const r = rig();
    r.live(W({ me: 1 })).who(FULL([row(1, 'maple', 'play', { pub: true, map: 'ISLANDS.LVL', n: 3 })]));
    r.tick(1000);
    same('another tab plays: the visitor\'s card says Playing, with the match', [r.metaOf('maple')], ['Playing: Islands · 3 players']);
    r.who({ t: 'who', full: false, n: 2, g: 0, more: 0, rows: [row(1, 'maple', 'look')], gone: [] }); r.tick(1000);
    same('and says Looking again when the hub says so', [r.metaOf('maple')], ['Looking: Looking']);
    const q = rig({ client: CLIENT(ROOM({ isPublic: false }), 'k7m2xq'), count: { people: 2, size: 4 } });
    q.live(W({ me: 1 })).who(FULL([row(1, 'maple', 'play', { priv: true })])); q.tick(1000);
    same('a room of this page that waits is the visitor\'s own word, but a match in another tab wins', [q.metaOf('maple')], ['Playing: Private game']);
}
{   // the note for the game page
    const r = rig({ client: CLIENT(ROOM({ isPublic: true }), 'c1v3rr'), count: { people: 2, size: 4 } });
    r.live(W({})); r.tick(1000);
    const note = JSON.parse(r.store.getItem('ants.here'));
    same('a room that waits: the note is the details that the front page tells the hub, and no code, no name', note, { v: 1, named: 1, r: Pure.hashOf('c1v3rr'), pub: true, map: 'TREASURE.LVL', n: 2, of: 4 });
    check('no code, no name and no account in the note', JSON.stringify(note).indexOf('c1v3rr') === -1 && JSON.stringify(note).indexOf('maple') === -1);
    r.state.handing = true; r.state.count = { people: 3, size: 4 }; r.tick(1000);
    same('on the way to the game it is the latest details', JSON.parse(r.store.getItem('ants.here')).n, 3);
    r.state.client = null; r.state.handing = false; r.tick(1000);
    check('no room: no note', r.store.getItem('ants.here') === null);
    const p = rig({ client: CLIENT(ROOM({ isPublic: false, map: 'SECRET.LVL' }), 'zzz999'), count: { people: 2, size: 4 } });
    p.live(W({})); p.tick(1000);
    same('a private room: the note says private and nothing more', JSON.parse(p.store.getItem('ants.here')), { v: 1, named: 1, r: Pure.hashOf('zzz999'), pub: false });
    const g = rig({ client: CLIENT(ROOM({ isPublic: true }), 'c1v3rr'), count: { people: 1, size: 4 } });
    g.live(W({ you: null, me: undefined })); g.tick(1000);
    check('a guest: the note says so (the game page opens no socket)', JSON.parse(g.store.getItem('ants.here')).named === 0);
    const off = rig({ client: CLIENT(ROOM({ isPublic: true }), 'c1v3rr'), count: { people: 2, size: 4 } });
    off.tick(1000);
    check('no hub, no note', off.store.getItem('ants.here') === null);
}
same('a note of a private room is only that, whatever it is given; no room, or one that is not a code: none', [Pure.noteOf({ at: 'play', pub: false, map: 'SECRET.LVL', n: 2, of: 3 }, true, 'k7m2xq'), Pure.noteOf({ at: 'look' }, true, 'k7m2xq'), Pure.noteOf({ at: 'wait', pub: true }, true, 'A B'), Pure.noteOf(null, true, 'k7m2xq')], [{ v: 1, named: 1, r: Pure.hashOf('k7m2xq'), pub: false }, null, null, null]);
check('the hash of the note is the game page\'s own', ['c1v3rr', 'a', 'k7m2xq', 'zzz999'].every((c) => Pure.hashOf(c) === require(path.join(lobbyDir, 'hub_here.js')).hashOf(c)));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 10. What the list gives the moderation sheet
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Pure, m = (o) => Object.assign({ at: 'look', pub: false, priv: false, solo: false, mapName: '', n: -1, of: -1 }, o);
    same('where a person is, in words: the group, then what the row says under the name (nothing under it for a person who looks or is away)', [P.whereOf(m({})), P.whereOf(m({ at: 'away' })), P.whereOf(m({ at: 'wait', pub: true, mapName: 'Treasure', n: 2, of: 4 })), P.whereOf(m({ at: 'wait', priv: true })), P.whereOf(m({ at: 'wait' })), P.whereOf(m({ at: 'play', pub: true, mapName: 'Gauntlet', n: 2 })), P.whereOf(m({ at: 'play', priv: true })), P.whereOf(m({ at: 'play', solo: true, mapName: 'Treasure' })), P.whereOf(m({ at: 'play' }))],
        ['Looking', 'Away', 'Waiting \u00b7 Treasure \u00b7 2 of 4', 'Waiting \u00b7 Private room', 'Waiting \u00b7 In a room', 'Playing \u00b7 Gauntlet \u00b7 2 players', 'Playing \u00b7 Private game', 'Playing \u00b7 Treasure \u00b7 vs computer', 'Playing \u00b7 In a game']);
    const r = rig();
    const drawn = [];
    r.list.drawn(() => drawn.push(r.names().length));
    check('the list hands over its scrolling box (the sheet\'s way in), and nothing is drawn or said yet', r.list.list !== undefined && cls(r.list.list).indexOf('pl-list') !== -1 && r.list.list.getAttribute('tabindex') === '0' && drawn.length === 0 && r.list.person(4) === null);
    r.live(W({ me: 1, you: { name: 'maple', mod: true, role: 'admin' } }));
    r.who(FULL([row(1, 'maple', 'look', { mod: true, role: 'admin' }), row(2, 'zed', 'look', { mod: true, role: 'mod' }), row(4, 'clover', 'wait', { pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr' }), row(5, 'sage'), row(6, 'sage', 'away')], { n: 6 }));
    same('a row of the list carries the hub\'s number of the row as data-i; the visitor\'s own place does not (it has no number to act on)', [byClass(r.panel(), 'pl-card').map((c) => c.getAttribute('data-i')).filter((x) => x !== null).sort().join(), byClass(r.panel(), 'pl-card').filter((c) => c.getAttribute('data-i') === null).length], ['2,4,5,6', 1]);
    same('person(i): the number, the name, the role, what the person does and where, in words', [r.list.person(4), r.list.person(2), r.list.person(5)], [{ i: 4, name: 'clover', role: 'none', at: 'wait', where: 'Waiting \u00b7 Treasure \u00b7 2 of 4', word: 'row:4', following: false }, { i: 2, name: 'zed', role: 'mod', at: 'look', where: 'Looking', word: 'row:2', following: false }, { i: 5, name: 'sage', role: 'none', at: 'look', where: 'Looking', word: 'row:5', following: false }]);
    same('...a person who is Away has a row (and a number) and no sheet: the hub takes `row:` only for a person with an open socket, so every command would be refused', [byClass(r.panel(), 'pl-card').some((c) => c.getAttribute('data-i') === '6'), r.list.person(5).name, r.list.person(6)], [true, 'sage', null]);
    same('...the visitor\'s own row (number 1 here), a number that is not a row, and anything that is not a number are nobody', [r.list.person(1), r.list.person(99), r.list.person('4'), r.list.person(-1), r.list.person(1.5), r.list.person(null)], [null, null, null, null, null, null]);
    check('drawn(fn) is told when the list was made again (a change in it), with the list already drawn', drawn.length >= 1 && drawn[drawn.length - 1] === 5 && r.list.list.parent !== null);
    const before = drawn.length;
    r.tick(1000); r.tick(1000);
    check('...and not when nothing changed (the one-second look makes nothing again)', drawn.length === before);
    r.who({ t: 'who', full: false, n: 6, g: 0, more: 0, rows: [row(5, 'sage', 'wait')], gone: [] });
    check('...a change draws it again, and the person\'s place follows at once', drawn.length === before + 1 && r.list.person(5).where === 'Waiting \u00b7 In a room');
    r.who({ t: 'who', full: false, n: 5, g: 0, more: 0, rows: [], gone: [4] });
    check('a person who leaves has no row any more', r.list.person(4) === null && drawn.length === before + 2);
    r.lost();
    check('a hub that is gone: nobody (the numbers were the hub\'s for that run)', r.list.person(2) === null && r.list.person(5) === null);
    const broken = rig({});
    broken.list.drawn(() => { throw new Error('a sheet that breaks'); });
    broken.live(); broken.who(FULL(CROWD, { n: 13, g: 4 }));
    check('a hook that throws is the error handler\'s, and the list is drawn all the same', broken.errors.length >= 1 && byClass(broken.panel(), 'pl-card').length > 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 10. People (docs/HUB_PEOPLE.md): the room that the page waits in, for an invitation, and who can be invited now
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Pure, valid = Rules.validMapFile;
    const SLOTS = (names) => names.map((n, i) => ({ state: n === null ? 0 : 1, name: n || '' }));
    const MINE = (o) => ROOM(Object.assign({ isPublic: false, you: 0, leader: 0, slots: SLOTS(['Maple', 'Wren', null, null]) }, o || {}));
    same('invOf: the code, the seats and the map of a room that waits, whether it is public or not; none for a match, no room or no seats', [
        P.invOf(CLIENT(MINE()), { people: 2, size: 4 }, valid), P.invOf(CLIENT(MINE({ isPublic: true })), { people: 1, size: 3 }, valid), P.invOf(CLIENT(MINE()), { people: 2, size: 4 }), P.invOf(CLIENT(MINE({ starting: true })), { people: 2, size: 4 }, valid),
        P.invOf(CLIENT(MINE({ lobby: false })), { people: 2, size: 4 }, valid), P.invOf({ room: null, code: 'c1v3rr' }, { people: 2, size: 4 }, valid), P.invOf(CLIENT(MINE()), null, valid), P.invOf(CLIENT(MINE()), { people: 5, size: 4 }, valid),
        P.invOf(CLIENT(MINE(), 'NOT A CODE'), { people: 2, size: 4 }, valid), P.invOf(CLIENT(MINE({ map: '../x' })), { people: 2, size: 4 }, valid)], [
        { code: 'c1v3rr', n: 2, of: 4, map: 'TREASURE.LVL' }, { code: 'c1v3rr', n: 1, of: 3, map: 'TREASURE.LVL' }, { code: 'c1v3rr', n: 2, of: 4 }, null, null, null, null, null, null, { code: 'c1v3rr', n: 2, of: 4 }]);
    const mine = P.mineOf(CLIENT(MINE()), { people: 2, size: 4 }, false, Rules);
    same('mineOf: the room for the row of the sheet, its map\'s name, its free seats, the other people in it', [mine.code, mine.mapName, mine.n, mine.of, mine.free, mine.host, mine.names], ['c1v3rr', 'Treasure', 2, 4, 2, '', ['Wren']]);
    same('a guest in somebody\'s room has the host\'s name', P.mineOf(CLIENT(MINE({ you: 1, leader: 0 }), 'c1v3rr', true), { people: 2, size: 4 }, false, Rules).host, 'Maple');
    same('no room to invite to while the page is on its way to a match', P.mineOf(CLIENT(MINE()), { people: 2, size: 4 }, true, Rules), null);
    same('inviteLine: the map, the seats and the seats that are free; for a guest the host\'s room instead of the free seats', [P.inviteLine(mine), P.inviteLine(Object.assign({}, mine, { free: 1 })), P.inviteLine(Object.assign({}, mine, { host: 'Wren', free: 2 })), P.inviteLine(Object.assign({}, mine, { host: 'Wrénds', mapName: '' }))],
        ['Treasure · 2 of 4 · 2 seats free', 'Treasure · 2 of 4 · 1 seat free', 'Wren’s room · Treasure · 2 of 4', 'Wrénds’ room · 2 of 4']);
    const why = (o) => P.inviteWhy(Object.assign({ you: true, mine: mine, row: { name: 'juniper', at: 'look' }, last: 0, now: 100000 }, o));
    same('inviteWhy: ok where nothing is in the way (and where the page holds no room: the command says so)', [why({}), why({ mine: null })], ['', '']);
    same('... a visitor who is not logged in, a full room, a person in the room, a person who plays, a person invited fifteen seconds ago', [why({ you: false }), why({ mine: Object.assign({}, mine, { free: 0, n: 4 }) }), why({ row: { name: 'Wren', at: 'wait' } }), why({ row: { name: 'moss', at: 'play' } }), why({ last: 85000 }), why({ last: 40001 }), why({ last: 40000 })],
        ['Log in to invite people. Share sends your link.', 'Your room is full (4 of 4).', 'In your room', 'Playing a match', 'Invited 15 s ago. Again in 45 s.', 'Invited 59 s ago. Again in 1 s.', '']);
    // the list's own directory
    const r = rig({ client: CLIENT(MINE()), count: { people: 2, size: 4 } });
    r.state.client.room.slots = SLOTS(['Maple', 'Wren', null, null]);
    r.live(W({ me: 1 })); r.who(FULL([row(2, 'juniper'), row(3, 'Wren', 'wait'), row(4, 'moss', 'play'), row(5, 'tess', 'away'), row(1, 'maple')]));
    const d = r.list.directory();
    same('the directory: the people in the order of the list, the visitor\'s own row marked out, the room', [d.rows.map((x) => x.name + ':' + x.at), d.you, d.mine && d.mine.code], [['juniper:look', 'Wren:wait', 'moss:play', 'tess:away'], true, 'c1v3rr']);
    same('the row of the sheet for a person of the list (by its number) and for a person of the chat (by name)', [d.inviteOf({ name: 'juniper', row: 2 }), d.inviteOf({ name: 'juniper', row: null }).ok, d.inviteOf({ name: 'JUNIPER', row: null }).where], [
        { shown: true, ok: true, title: 'Invite to my room', sub: 'Treasure · 2 of 4 · 2 seats free', where: 'Looking' }, true, 'Looking']);
    same('a person in the room, a person who plays, a person who is not here, and nobody known by that name', [d.inviteOf({ name: 'Wren', row: 3 }).sub, d.inviteOf({ name: 'moss', row: 4 }).sub, d.inviteOf({ name: 'ghost', row: null }).sub, d.inviteOf({ name: 'ghost', row: null }).ok], ['In your room', 'Playing a match', 'ghost is not here now.', false]);
    d.invited('juniper');
    check('a person who was just invited is dim for the minute that the hub holds a second invitation back', /^Invited \d+ s ago\. Again in \d+ s\.$/.test(r.list.directory().inviteOf({ name: 'juniper', row: 2 }).sub) && r.list.directory().inviteOf({ name: 'juniper', row: 2 }).ok === false);
    r.tick(61000);
    check('and ready again after it', r.list.directory().inviteOf({ name: 'juniper', row: 2 }).ok === true);
    r.who(FULL([row(2, 'juniper'), row(3, 'Wren', 'wait'), row(4, 'moss', 'play'), row(1, 'maple'), row(7, 'twin'), row(8, 'Twin')]));
    same('a name that two people show is no one person: the row of the sheet for a name of the chat says so', [r.list.directory().inviteOf({ name: 'twin', row: null }).sub, r.list.directory().inviteOf({ name: 'twin', row: 7 }).ok], ['twin is not here now.', true]);
    const empty = rig({}); empty.live(W({ me: 1 })); empty.who(FULL([row(2, 'juniper')]));
    same('with no room: no row for the sheet at all', [empty.list.directory().mine, empty.list.directory().inviteOf({ name: 'juniper', row: 2 }).shown], [null, false]);
    const guest = rig({ client: CLIENT(MINE()), count: { people: 2, size: 4 } }); guest.state.client.room.slots = SLOTS(['Maple', 'Wren', null, null]); guest.live(W({ you: null, login: true, me: undefined })); guest.who(FULL([row(2, 'juniper')]));
    same('a visitor who is not logged in has the row, dim, with the way out', guest.list.directory().inviteOf({ name: 'juniper', row: 2 }), { shown: true, ok: false, title: 'Invite to my room', sub: 'Log in to invite people. Share sends your link.', where: 'Looking' });
    // the room goes to the hub at once when a command is about to ask about it
    const f = rig({ client: CLIENT(MINE()), count: { people: 2, size: 4 } }); f.live(W({ me: 1 })); f.tick(0);
    const before = f.hereFrames().length;
    f.state.count = { people: 3, size: 4 }; f.tick(500);
    check('a change of seats waits for the pace of one every three seconds ...', f.hereFrames().length === before);
    r.list.directory(); f.list.directory().flush();
    check('... but not when an invitation is about to ask: the hub is told now, the room as it is', f.hereFrames().length === before + 1 && f.hereFrames()[before].inv.n === 3);
    f.list.directory().flush();
    check('and once, not twice', f.hereFrames().length === before + 1);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The people that the visitor follows: the hub's `follows` frame, the two tabs, the star, the Following tab (docs/HUB_PEOPLE.md, "Follows")
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r0 = rig(), P0 = r0.Players, Chat0 = r0.win.AntsHomeChat, HH = (n) => ('a1b2c3d4e5f' + n).slice(0, 12);
    same('the follows of a frame: handle, name, the number of the row while the person is here, the key of their lines; text is cleaned like a name', P0.followsOf({ rows: [{ h: HH(1), name: 'Fern', i: 4, k: 'k1' }, { h: HH(2), name: 'Bo‏b', i: -1 }, { h: HH(3), name: 'birch', i: 'x', k: 7 }] }, Chat0),
        [{ h: HH(1), name: 'Fern', i: 4, k: 'k1' }, { h: HH(2), name: 'Bob', i: -1, k: '' }, { h: HH(3), name: 'birch', i: -1, k: '' }]);
    same('...a row that is wrong is left out, and a frame that is not one is none', [P0.followsOf({ rows: [{ h: 'nope', name: 'x' }, { h: HH(1) }, null, 5, { h: HH(1), name: '' }] }, Chat0), P0.followsOf({ rows: 'x' }, Chat0), P0.followsOf(null, Chat0), P0.followsOf({ rows: new Array(101).fill({ h: HH(1), name: 'x' }) }, Chat0)], [[], null, null, null]);
    const model = { rows: { 4: { id: 4, name: 'Fern', at: 'wait' }, 5: { id: 5, name: 'Pebble', at: 'play' }, 6: { id: 6, name: 'Wren', at: 'wait' }, 7: { id: 7, name: 'Moss', at: 'away' }, 9: { id: 9, name: 'Ash', at: 'look' }, 1: { id: 1, name: 'maple', at: 'look' } } };
    const f = (h, name, i) => ({ h: HH(h), name, i, k: '' });
    const list = P0.followedList(model, [f(1, 'Fern', 4), f(2, 'Pebble', 5), f(3, 'Wren', 6), f(4, 'Moss', 7), f(5, 'birch', -1), f(6, 'Ash', 9), f(7, 'ghost', 55), f(8, 'maple', 1)], 1);
    same('the Following tab\'s groups: by what they do now; a person who is away, who is not in the list at all or who is the visitor\'s own row is not here; each group by name', ['wait', 'play', 'look', 'gone'].map((g) => list[g].map((x) => x.follow.name)), [['Fern', 'Wren'], ['Pebble'], ['Ash'], ['birch', 'ghost', 'maple', 'Moss'].sort((a, b) => (a < b ? -1 : 1))]);
}
{
    const FR = (i, name, extra) => Object.assign({ h: ('c0ffee00000' + i).slice(0, 12), name, i: -1, k: '' }, extra);
    const r = rig();
    r.live(W({ me: 1 }));
    r.who(FULL([row(1, 'maple'), row(2, 'Fern', 'wait', { pub: true, map: 'TREASURE.LVL', n: 1, of: 4, code: 'c1v3rr' }), row(3, 'Wren', 'wait', { priv: true }), row(4, 'Pebble', 'play', { priv: true }), row(5, 'tess')], { n: 5 }));
    r.emit('follows', { rows: [FR(2, 'Fern', { i: 2 }), FR(3, 'Wren', { i: 3 }), FR(4, 'Pebble', { i: 4 }), FR(6, 'birch')] });
    const tabs = byClass(r.panel(), 'pl-tab');
    same('a person who is logged in has two tabs, Everyone (all of the list) and Following (the people they follow), Everyone first and selected', [tabs.map((t) => t.textContent.replace(/\s+/g, ' ').trim()), tabs.map((t) => t.getAttribute('aria-selected')), one(r.panel(), 'pl-tabs').getAttribute('role')], [['Everyone 5', 'Following 4'], ['true', 'false'], 'tablist']);
    same('Everyone: a star after the name of each person that is followed, with the words for a screen reader, and none after the others', [byClass(r.panel(), 'pl-card').map((c) => [one(c, 'pl-nm').textContent, byClass(c, 'pl-star').length]).filter((x) => x[1]).map((x) => x[0]).sort(), texts(r.panel(), 'pl-sr').filter((t) => /following/.test(t)).length], [['Fern', 'Pebble', 'Wren'], 3]);
    tabs[1].click();
    same('the Following tab: the groups by what the people do now, then the person who is not here; the visitor\'s own row is not on it and there are no guests', [r.groups().map((g) => [g.g, g.n, g.names]), byClass(r.panel(), 'pl-you').length === 0 || one(r.panel(), 'pl-you').children.length === 0 ? 'no own row' : 'own row', byClass(r.panel(), 'pl-guests').length], [[['wait', '2', ['Fern', 'Wren']], ['play', '1', ['Pebble']], ['away', '1', ['birch']]], 'no own row', 0]);
    check('...the two tabs are still there, Following is the selected one', byClass(r.panel(), 'pl-tab').map((t) => t.getAttribute('aria-selected')).join() === 'false,true');
    check('...it says that only the visitor sees the list and that nobody is told', texts(r.panel(), 'pl-keep').join() === 'Only you see this list. Nobody is told that you follow them.');
    check('...a person who is not here has a card with a number from 900,000,000, the hub\'s handle and no row of the list', (() => { const c = r.panel().findAll((e) => e.getAttribute && e.getAttribute('data-h') !== null)[0]; return !!c && Number(c.getAttribute('data-i')) >= r.Players.FAR && c.getAttribute('data-h') === FR(6, 'birch').h; })());
    same('...person(n) of that card says so for the sheet: the handle is the word, the person is followed and is not here', [r.list.person(r.Players.FAR + 3).word, r.list.person(r.Players.FAR + 3).following, r.list.person(r.Players.FAR + 3).far, r.list.person(r.Players.FAR + 3).where, r.list.person(r.Players.FAR + 4)], [FR(6, 'birch').h ? 'h:' + FR(6, 'birch').h : '', true, true, 'Not here', null]);
    same('a person of the list: person(i) is the row, and says whether they are followed', [r.list.person(2).word, r.list.person(2).following, r.list.person(5).following], ['row:2', true, false]);
    same('directory().following: by the number of the row, or by the key of the lines of the person', [r.list.directory().following({ row: 2 }), r.list.directory().following({ row: 5 }), (r.emit('follows', { rows: [FR(2, 'Fern', { i: 2, k: 'k-fern' })] }), r.list.directory().following({ k: 'k-fern' })), r.list.directory().following({ k: 'k-other' }), r.list.directory().following({ k: '' })], [true, false, true, false, false]);
    tabs.forEach((t) => t.getAttribute('data-tab') === 'all' && t.click());
    check('Everyone again: the whole list, with the visitor\'s own row', r.names().indexOf('maple') !== -1 && r.names().indexOf('tess') !== -1 && byClass(r.panel(), 'pl-group').length >= 1);
    r.emit('follows', { rows: [] });
    r.panel().findAll((e) => e.getAttribute && e.getAttribute('data-tab') === 'following')[0].click();
    same('nobody followed: the tab says so and how to follow, and the line about who sees the list is there', [texts(r.panel(), 'pl-nt'), texts(r.panel(), 'small').length, byClass(r.panel(), 'pl-card').length, texts(r.panel(), 'pl-keep').length], [['You follow nobody yet'], 1, 0, 1]);
    const bad = rig();
    bad.emit('follows', { rows: [FR(1, 'x', { k: 'kx' })] });
    check('a follows frame before the welcome is nothing (not kept for the welcome that comes)', bad.list.directory().following({ k: 'kx' }) === false);
    bad.live(W({})); bad.who(FULL([row(1, 'maple')], { n: 1 }));
    bad.emit('follows', { rows: 'x' }); bad.emit('follows', null); bad.emit('follows', { rows: new Array(101).fill(FR(1, 'x')) });
    check('a follows frame that comes before the welcome, or that is wrong, changes nothing', bad.list.model().n === 1 && bad.list.directory().following({ row: 1 }) === false && byClass(bad.panel(), 'pl-tab').map((t) => t.textContent.replace(/\s+/g, ' ').trim()).join() === 'Everyone 1,Following 0');
    const hostile = rig(); hostile.live(W({})); hostile.who(FULL([row(1, 'maple')], { n: 1 }));
    hostile.emit('follows', { rows: [FR(1, '<img src=x onerror=alert(1)>', {}), FR(2, 'a'.repeat(300))] });
    hostile.panel().findAll((e) => e.getAttribute && e.getAttribute('data-tab') === 'following')[0].click();
    check('names of the people that are followed are text: no markup is written, and a long one is cut', hostile.env.innerHTMLWrites.length === 0 && texts(hostile.panel(), 'pl-nm').every((t) => t.length <= 33) && texts(hostile.panel(), 'pl-nm').indexOf('<img src=x onerror=alert(1)>') !== -1);
    const guest = rig(); guest.live(W({ you: null })); guest.who(FULL([], { n: 1, g: 1 }));
    check('a guest has no tabs and nothing of this', byClass(guest.panel(), 'pl-tab').length === 0 && byClass(guest.panel(), 'pl-keep').length === 0);
    const again = rig(); again.live(W({})); again.who(FULL([row(1, 'maple'), row(2, 'Fern')], { n: 2 })); again.emit('follows', { rows: [FR(2, 'Fern', { i: 2, k: 'ky' })] }); again.live(W({}));
    check('a welcome forgets the follows even where no loss came first (the hub sends them again)', again.list.directory().following({ k: 'ky' }) === false && again.list.directory().following({ row: 2 }) === false);
    const gone = rig(); gone.live(W({})); gone.who(FULL([row(1, 'maple'), row(2, 'Fern')], { n: 2 })); gone.emit('follows', { rows: [FR(2, 'Fern', { i: 2 })] }); gone.lost(); gone.live(W({})); gone.who(FULL([row(1, 'maple'), row(2, 'Fern')], { n: 2 }));
    check('a new socket forgets the follows (the hub sends them again)', byClass(gone.panel(), 'pl-card').every((c) => byClass(c, 'pl-star').length === 0) && byClass(gone.panel(), 'pl-tab').map((t) => t.textContent.replace(/\s+/g, ' ').trim()).join() === 'Everyone 2,Following 0');
}

console.log('players list: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
