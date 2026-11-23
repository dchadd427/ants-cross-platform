// Runs the moderation sheet of Home's chat (web/home/mod.js, docs/HOME_CHAT.md "Roles and commands") against the REAL chat dock (web/home/chat.js, mounted on a fake hub socket) in the fake browser of
// tests/scripts/web_page_harness.js, with a fake clock. What it checks:
//   1. the pure parts: what a viewer is offered on a line (every viewer x author), the hub's command for each action (by the number of the line, never by a name), the minutes that a typed text means, the words of an
//      answer (every code of the hub), the source of the script (ASCII, no markup);
//   2. the dock's side: `command` (one at a time, answered once: ok, a code of the hub, offline, silent; nothing added to the log by the dock), `rows`, `line`, `you`, `note`;
//   3. who gets it: the marks (`data-act`, the description for a screen reader) on the lines that the viewer may act on and no others, a person with no role and a role that goes or comes while the page is open;
//   4. the ways in: a right click (and where the browser's own menu stays), a long press (500 ms; moving, scrolling, lifting, a second finger, a mouse and a link do nothing), the keys (Up, Down, Enter, the menu key,
//      Shift and F10, Escape);
//   5. the sheet: the actions of each role, Cancel, Escape, Back, the focus (first action, Cancel on a card, back to the log), Tab kept inside, what stands behind is inert, the dark part is Cancel (not at once), nothing is
//      sent before the red button (and not in its first 400 ms), the exact commands, one at a time, the answers (toast and private line: the same words), the codes in words, 8 s of silence, a socket that goes,
//      a line that goes, a role that goes meanwhile, text that is never markup;
//   5b. the recent messages: a row on the sheet with the count of the person's lines that are in the page's log (matched by the hub's key, never a name; not on a line of one's own; not for a visitor with no role),
//      the list (newest first, the log's own times and day words, text only, the line that was opened marked, one stop for Tab, Up, Down, Home and End walk it, Escape and Back), the sheet of a line of the list,
//      Delete from it (back to the list, the focus on the next line, else the one before, else Back), and the live follow (a new line, a deleted line, the log's 100 lines, a role that goes);
//   5c. the person sheet of the players list (the real web/home/players.js on the same dock): the rows that carry a number and a mark, the same ways in (right click, long press, keys; a press that starts on a button of a
//      row is that button's), the sheet (where the person is, Mute, Kick, Ban; no line to delete, no recent messages), `/mutes` asked when it opens (Mute is not offered until the answer; Unmute and "Muted until"; a
//      failure and a retry), the exact commands by the number of the ROW (`row:4`, never a name), a sheet that does not outlive the socket or the person, and the toast;
//   6. the page (web/lobby.html with web/home/home.js): the two files come once, after the dock names a visitor with a role, and never for a person with none; a broken or missing one changes nothing.
// tests/scripts/test_web_home_mod.py runs this with node (the quick tier). usage: node web_home_mod_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
globalThis.AntsShapes = require(path.resolve(__dirname, '..', '..', 'web', 'shapes.js'));

const lobbyPath = process.argv[2];
if (!lobbyPath) { console.log('usage: web_home_mod_check.js lobby.html'); process.exit(2); }
const lobbyDir = path.dirname(path.resolve(lobbyPath));
const modPath = path.join(lobbyDir, 'home', 'mod.js');
const modSource = fs.readFileSync(modPath, 'utf8');
const Pure = require(modPath);
const Chat = require(path.join(lobbyDir, 'home', 'chat.js'));
const playersSource = fs.readFileSync(path.join(lobbyDir, 'home', 'players.js'), 'utf8');
const H = require('./web_page_harness.js')(lobbyPath);
const { El, runLobby } = H;

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

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The pure parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Pure;
    const L = (o) => Object.assign({ id: 148, who: 'Clover', text: 'hi', role: 'none', sys: false, own: false }, o);
    same('an administrator is offered everything on a person with no role', P.actionsOf('admin', L({})), ['del', 'mute', 'kick', 'ban']);
    same('a moderator is offered no Ban (the hub keeps it for administrators)', P.actionsOf('mod', L({})), ['del', 'mute', 'kick']);
    same('an administrator is offered everything on a moderator\'s line (the hub lets an administrator act on a moderator)', P.actionsOf('admin', L({ role: 'mod' })), ['del', 'mute', 'kick', 'ban']);
    same('a moderator is offered nothing on a moderator\'s line, and nobody anything on an administrator\'s', [P.actionsOf('mod', L({ role: 'mod' })), P.actionsOf('mod', L({ role: 'admin' })), P.actionsOf('admin', L({ role: 'admin' }))], [[], [], []]);
    same('on a line of one\'s own: Delete only, for both roles and whatever role the line says', [P.actionsOf('admin', L({ own: true, role: 'admin' })), P.actionsOf('mod', L({ own: true, role: 'mod' })), P.actionsOf('mod', L({ own: true }))], [['del'], ['del'], ['del']]);
    same('on a line of the hub\'s own: nothing, and for no viewer, a viewer who is nobody, or no line', [P.actionsOf('admin', L({ sys: true })), P.actionsOf('none', L({})), P.actionsOf('', L({})), P.actionsOf('admin', null), P.actionsOf('root', L({}))], [[], [], [], [], []]);
    const named = L({ who: 'Clover', u: 'clover.4782' });
    same('the commands: by the number of the line (the hub\'s own forms)', [P.commandOf('del', named), P.commandOf('mute', named, 60), P.commandOf('kick', named), P.commandOf('ban', named), P.commandOf('banpurge', named), P.commandOf('nothing', named)],
        ['/del 148', '/mute #148 60', '/kick #148', '/ban #148', '/ban #148 purge', '']);
    check('...never a name or a username, whatever the line says of its author', ['del', 'mute', 'kick', 'ban', 'banpurge'].every((k) => !/clover/i.test(P.commandOf(k, named, 10))));
    same('the four times', P.TIMES, [[10, '10 min'], [60, '1 hour'], [1440, '1 day'], [10080, '1 week']]);
    same('the hub\'s longest mute is the page\'s', [P.LIMIT.minutes, Chat.COMMANDS === undefined], [10080, true]);
    same('minutesOf: digits only, 1 to 10,080, blanks around are nothing', [P.minutesOf('1'), P.minutesOf('45'), P.minutesOf(' 45 '), P.minutesOf('10080'), P.minutesOf('007'), P.minutesOf('10000')], [1, 45, 45, 10080, 7, 10000]);
    same('minutesOf: no number, zero, too many, a sign, a point, an exponent, blanks inside, a letter, other digits, five digits over the limit, six digits', [P.minutesOf(''), P.minutesOf('   '), P.minutesOf('0'), P.minutesOf('10081'), P.minutesOf('-5'), P.minutesOf('1.5'), P.minutesOf('1e3'), P.minutesOf('4 5'), P.minutesOf('ten'), P.minutesOf('\u0663'), P.minutesOf('99999'), P.minutesOf('100000')], [null, null, null, null, null, null, null, null, null, null, null, null]);
    same('lengthOf: a minute, minutes, an hour, hours and minutes, a day, a week', [P.lengthOf(1), P.lengthOf(10), P.lengthOf(60), P.lengthOf(61), P.lengthOf(90), P.lengthOf(120), P.lengthOf(1440), P.lengthOf(1500), P.lengthOf(2880 + 60 + 1), P.lengthOf(10080)],
        ['1 minute', '10 minutes', '1 hour', '1 hour 1 minute', '1 hour 30 minutes', '2 hours', '1 day', '1 day 1 hour', '2 days 1 hour 1 minute', '1 week']);
    same('the words of a done command', [P.doneWords('del', 'Clover'), P.doneWords('mute', 'Clover', 60), P.doneWords('mute', 'Clover', 90), P.doneWords('kick', 'Clover'), P.doneWords('ban', 'Clover', 0, false), P.doneWords('ban', 'Clover', 0, true)],
        ['Done: the line is gone.', 'Done: Clover is muted for 1 hour.', 'Done: Clover is muted for 1 hour 30 minutes.', 'Done: Clover was kicked.', 'Done: Clover is banned.', 'Done: Clover is banned and the lines of the last hour are gone.']);
    same('the words of a refusal: power says who and what they are, a role that Discord could not confirm, a person who is gone (a kick says so), a line or a ban that is there already',
        [P.refusedWords('power', 'ban', 'Sage', 'mod'), P.refusedWords('power', 'mute', 'Tess', 'admin'), P.refusedWords('power', 'mute', 'Clover', 'none'), P.refusedWords('role', 'mute', 'Clover', 'none'), P.refusedWords('who', 'kick', 'Pebble', 'none'),
         P.refusedWords('who', 'mute', 'Pebble', 'none'), P.refusedWords('bad', 'ban', 'Clover', 'none'), P.refusedWords('bad', 'del', 'Clover', 'none'), P.refusedWords('bad', 'mute', 'Clover', 'none')],
        ['You cannot do that to Sage (a moderator).', 'You cannot do that to Tess (an administrator).', 'You cannot do that to Clover.', 'Could not confirm your role with Discord, try again in a moment.', 'Pebble is not on the front page now, so there is nobody to kick. Mute or ban instead.',
         'That person is not here any more.', 'Clover is banned already.', 'That line is gone already.', 'That line is gone already.']);
    same('...too fast, offline, no answer, a staff member who is muted or banned, and any other word', [P.refusedWords('rate', 'del', 'x', 'none'), P.refusedWords('offline', 'del', 'x', 'none'), P.refusedWords('silent', 'del', 'x', 'none'), P.refusedWords('muted', 'del', 'x', 'none'), P.refusedWords('banned', 'del', 'x', 'none'), P.refusedWords('zzz', 'del', 'x', 'none')],
        ['Too fast: wait a moment and try again.', 'Chat is offline: nothing was done.', 'Nothing came back: check the chat before trying again.', 'You cannot do that now.', 'You cannot do that now.', 'That did not work, try again.']);
    check('mod.js is ASCII only (the dashes and quotes are escapes)', /^[\x00-\x7f]*$/.test(modSource));
    check('...and makes no markup, no link and no network call', !/innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function|\.href\b|\.src\b|XMLHttpRequest|fetch\(|WebSocket|localStorage|sessionStorage/.test(modSource.replace(/\/\/.*/g, '')));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the fakes: the real dock on a fake hub, the sheet on it, a fake clock
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function clockOf() {
    const c = { t: 0, queue: [], seq: 0 };
    c.setTimeout = (fn, ms) => { const id = ++c.seq; c.queue.push({ id, fn, at: c.t + (ms || 0) }); return id; };
    c.clearTimeout = (id) => { c.queue = c.queue.filter((x) => x.id !== id); };
    c.advance = (ms) => {
        const end = c.t + ms;
        for (;;) {
            c.queue.sort((a, b) => a.at - b.at || a.id - b.id);
            const next = c.queue[0];
            if (!next || next.at > end) break;
            c.queue.shift(); c.t = Math.max(c.t, next.at); next.fn();
        }
        c.t = end;
    };
    return c;
}
function loadScript(source, exportName, clock) {
    const root = {};
    new Function('self', 'setTimeout', 'clearTimeout', source)(root, clock.setTimeout, clock.clearTimeout);
    return root[exportName];
}
const T0 = 1700000000;
const line = (id, who, text, extra) => Object.assign({ id, at: T0 + id, who, text, mod: !!(extra && extra.role), k: 'k-' + who.toLowerCase() }, extra);
const HISTORY = [
    line(141, 'Pebble', 'anyone up for Islands?'),
    line(143, 'Sage', 'Please be kind.', { role: 'mod', u: 'sage.moss' }),
    line(146, 'Tess', 'Matches start at the top of the hour.', { role: 'admin', u: 'tess.ward' }),
    line(147, 'system', 'Dash was kicked.', { sys: true, k: '' }),
    line(148, 'Clover', 'FREE ant gold!!! see my page', { u: 'clover.4782' }),
    line(149, 'Juniper', 'that looks like spam, see https://example.org/x now', { u: 'juniper.berry' }),
    line(150, 'Me', 'looking at it', { u: 'me.r' }),
];
// o.role ('admin', 'mod', 'none'), o.history, o.size, o.noMod (the sheet is not mounted), o.vibrate (a function, or false for a browser with none), o.people (the players list is there too: the visitor's own row is number 3; the sheet is told of it unless o.noLink)
function rig(o) {
    o = o || {};
    const role = o.role || 'admin';
    const env = { elements: {}, innerHTMLWrites: [] };
    const clock = clockOf();
    const ChatApi = loadScript(fs.readFileSync(path.join(lobbyDir, 'home', 'chat.js'), 'utf8'), 'AntsHomeChat', clock);
    const html = new El(env, 'html', '');
    html.style.setProperty = (k, v) => { html.style[k] = v; }; html.style.removeProperty = (k) => { delete html.style[k]; };
    const body = new El(env, 'body', '');
    const doc = { hidden: false, listeners: {}, capturing: {}, documentElement: html, body, createElement: (t) => new El(env, t), addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); }, activeElement: null };
    env.doc = doc; env.root = body;
    const buzzes = [];
    const win = { innerWidth: o.size ? o.size[0] : 1280, innerHeight: o.size ? o.size[1] : 800, location: { protocol: 'https:', host: 'play.test' }, listeners: {}, addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); }, navigator: { language: 'en-US' } };
    if (o.vibrate !== false) win.navigator.vibrate = (ms) => { buzzes.push(ms); return true; };
    const sockets = [];
    class HubSocket {
        constructor(url) { this.url = url; this.sent = []; this.readyState = 0; sockets.push(this); }
        send(text) { if (this.readyState !== 1) throw new Error('send on a socket that is not open'); this.sent.push(text); }
        close() { this.readyState = 3; }
        open() { this.readyState = 1; if (this.onopen) this.onopen({}); return this; }
        recv(x) { if (this.onmessage) this.onmessage({ data: JSON.stringify(x) }); return this; }
    }
    const host = new El(env, 'div', 'home');
    body.appendChild(host);
    const CmdsApi = o.room ? loadScript(fs.readFileSync(path.join(lobbyDir, 'home', 'cmds.js'), 'utf8'), 'AntsHomeCmds', clock) : null;       // (a room to invite to: the helper is on the dock as home.js loads it, and says the words of an invitation's answer)
    const dock = ChatApi.mount({ doc, win, host, chip: null, screen: () => 'home', fetch: undefined, now: () => clock.t, random: () => 0, WebSocket: HubSocket, error: (e) => { throw e; }, load: CmdsApi ? (base, done) => { win.AntsHomeCmds = CmdsApi; clock.setTimeout(done, 0); } : undefined });
    dock.shown(true);
    const mine = (l) => (l.who === 'Me' ? Object.assign({}, l, { role: role === 'none' ? undefined : role, mod: role !== 'none' }) : l);       // (the viewer's own line says the viewer's own role)
    win.AntsHomeChat = ChatApi; win.AntsLobbyRules = H.RULES0;
    sockets[0].open().recv(Object.assign({ t: 'welcome', v: 1, you: o.guest ? null : { name: 'Me', mod: role !== 'none', role, k: 'k-me' }, login: !!o.guest, online: 3, history: (o.history || HISTORY).map(mine) }, o.people ? { presence: true, me: 3 } : {}));
    if (CmdsApi && !o.guest) { sockets[0].recv({ t: 'cmds', rows: [{ w: '/invite', a: 'NAME', d: 'invite someone to your room', g: 'Everyone' }] }); clock.advance(1); }
    const Mod = loadScript(modSource, 'AntsHomeMod', clock);
    const r = { env, doc, win, body, host, dock, sockets, clock, buzzes, Mod, HubSocket };
    r.sock = () => sockets[sockets.length - 1];
    r.log = dock.log;
    r.rows = () => r.log.children.filter((c) => c.getAttribute && c.getAttribute('data-n') !== null);
    r.row = (id) => r.rows().find((c) => c.getAttribute('data-n') === String(id));
    r.acts = () => r.rows().filter((c) => c.getAttribute('data-act') === 'true').map((c) => Number(c.getAttribute('data-n')));
    r.marked = () => r.rows().filter((c) => cls(c).indexOf('gm-on') !== -1).map((c) => Number(c.getAttribute('data-n')));
    // the room that the page holds (o.room: { people, size, names, code, guest, map }), or none: the page's client as the list reads it, and its model of four colors
    r.room = o.room || null;
    r.client = () => (r.room ? { room: { lobby: true, starting: false, isPublic: false, map: r.room.map || 'TREASURE.LVL', you: 0, leader: r.room.guest ? 1 : 0, slots: [0, 1, 2, 3].map((i) => ({ state: i < r.room.names.length ? 1 : 0, name: r.room.names[i] || '' })) }, code: r.room.code || 'c1v3rr', isLeader: () => !r.room.guest } : null);
    r.model = () => (r.room ? { slots: [0, 1, 2, 3].map((i) => ({ kind: i < r.room.people ? 'person' : i < r.room.size ? 'open' : 'nobody' })) } : null);
    const taps = [], spyDock = Object.assign({}, dock, { tap: (fn) => { taps.push(fn); return dock.tap(fn); } });       // (the sheet's own taps, so that a test can tell it what the dock would)
    r.tell = (kind, m) => taps.forEach((fn) => fn(kind, m));
    r.mount = () => { r.mod = Mod.mount({ doc, win, dock: spyDock, say: o.say, now: () => clock.t, error: (e) => { throw e; } }); return r.mod; };
    if (!o.noMod && !o.modLast) r.mount();
    if (o.people) {
        const yard = new El(env, 'main'), quick = new El(env, 'button'), games = new El(env, 'button');
        host.appendChild(yard);
        const PlayersApi = loadScript(playersSource, 'AntsHomePlayers', clock);
        r.players = PlayersApi.mount({ doc, win, host, yard, quickHost: quick, games, dock, page: { client: () => r.client(), info: () => ({ client: !!r.client(), model: r.model() }), handing: () => false }, now: () => clock.t, error: (e) => { throw e; } });
        dock.directory(r.players.directory);       // (what home.js does: the list lends the dock who is here and the room)
        if (o.modLast) r.mount();       // (the list came first: its taps are told of a socket that goes before the sheet's are)
        if (r.mod && !o.noLink) r.mod.people(r.players);
    }
    r.said = () => r.sockets.reduce((all, s) => all.concat(s.sent.map((x) => JSON.parse(x)).filter((m) => m.t === 'say').map((m) => m.text)), []);
    r.shade = () => body.children.find((c) => cls(c).indexOf('gm-shade') !== -1) || null;
    r.box = () => { const s = r.shade(); return s ? one(s, 'gm-box') : null; };
    r.buttons = () => { const b = r.box(); return b ? b.findAll((e) => e.tagName === 'button') : []; };
    r.named = (text) => r.buttons().find((b) => (b.getAttribute('aria-label') || b.textContent) === text || b.textContent === text) || null;
    r.names = () => r.buttons().map((b) => b.getAttribute('aria-label') || b.textContent);
    r.toast = () => body.children.find((c) => cls(c).indexOf('gm-toast') !== -1) || null;
    r.toastText = () => { const t = r.toast(); return t ? one(t, 'gm-tt').textContent : null; };
    r.privates = () => r.log.children.filter((c) => cls(c).indexOf('gc-private') !== -1).map((c) => c.textContent.replace(/^Only you/, ''));
    r.event = (target, type, ev) => { let stopped = false; target.fire(type, Object.assign({ target, preventDefault() { stopped = true; } }, ev)); return stopped; };
    r.menu = (id) => r.event(r.log, 'contextmenu', { target: typeof id === 'object' ? id : r.row(id) });
    r.key = (k, extra) => r.event(r.log, 'keydown', Object.assign({ key: k }, extra));
    r.down = (id, ev) => r.event(r.log, 'pointerdown', Object.assign({ target: typeof id === 'object' ? id : r.row(id), pointerType: 'touch', isPrimary: true, clientX: 100, clientY: 100 }, ev));
    r.move = (dx, dy) => r.event(r.log, 'pointermove', { clientX: 100 + dx, clientY: 100 + dy });
    r.up = () => r.event(r.log, 'pointerup', {});
    r.recv = (x) => r.sock().recv(x);
    r.press = (name) => { const b = r.named(name); if (!b) throw new Error('no button ' + name + ' in ' + r.names().join('|')); b.click(); };
    r.you = (role2) => r.recv({ t: 'you', you: { name: 'Me', mod: role2 !== 'none', role: role2, k: 'k-me' } });
    r.open = (id) => { r.menu(id); return r.shade() !== null; };
    // the players list: its rows (the number of a row is the hub's own), and the ways in on it
    r.who = (rows, extra) => r.recv(Object.assign({ t: 'who', full: true, n: rows.length, g: 0, more: 0, rows }, extra));
    r.cards = () => r.players ? r.players.list.querySelectorAll('.pl-card[data-i]') : [];
    r.card = (i) => r.cards().find((c) => c.getAttribute('data-i') === String(i));
    r.pacts = () => r.cards().filter((c) => c.getAttribute('data-act') === 'true').map((c) => Number(c.getAttribute('data-i')));
    r.pmarked = () => r.cards().filter((c) => cls(c).indexOf('gm-on') !== -1).map((c) => Number(c.getAttribute('data-i')));
    r.pmenu = (i) => r.event(r.players.list, 'contextmenu', { target: typeof i === 'object' ? i : r.card(i) });
    r.pkey = (k, extra) => r.event(r.players.list, 'keydown', Object.assign({ key: k, target: r.players.list }, extra));
    r.pdown = (i, ev) => r.event(r.players.list, 'pointerdown', Object.assign({ target: typeof i === 'object' ? i : r.card(i), pointerType: 'touch', isPrimary: true, clientX: 100, clientY: 100 }, ev));
    r.pmove = (dx, dy) => r.event(r.players.list, 'pointermove', { clientX: 100 + dx, clientY: 100 + dy });
    r.pup = () => r.event(r.players.list, 'pointerup', {});
    r.popen = (i) => { r.pmenu(i); return r.shade() !== null; };
    r.mutes = (rows) => r.recv({ t: 'mutes', rows });
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. The dock's side
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({ noMod: true });
    const got = [];
    r.dock.command('/del 148', (code) => got.push(code));
    same('a command goes out as a line of the chat, as typed', r.said(), ['/del 148']);
    same('...and waits: nothing is said yet', got, []);
    r.dock.command('/del 149', (code) => got.push('second:' + code));
    same('one command at a time: the second is `rate` at once and is not sent', [got, r.said()], [['second:rate'], ['/del 148']]);
    r.recv({ t: 'ok', c: 'del' });
    same('the hub\'s ok answers it, once', got, ['second:rate', 'ok']);
    check('the dock adds nothing to the log of its own for a command of the sheet (the sheet says the words)', r.privates().length === 0);
    r.recv({ t: 'ok', c: 'del' });
    same('a second ok is nobody\'s', got.length, 2);
    for (const code of ['power', 'role', 'who', 'rate', 'bad', 'muted', 'banned']) {
        const q = rig({ noMod: true }), seen = [];
        q.dock.command('/mute #148 5', (c) => seen.push(c));
        q.recv({ t: 'err', code });
        same('the hub\'s `' + code + '` is the answer, once, and no private line is made of it by the dock', [seen, q.privates()], [[code], []]);
    }
    const w = rig({ noMod: true }), seen = [];
    w.dock.command('/kick #148', (c) => seen.push(c));
    w.clock.advance(7999);
    same('no answer for 7.999 s: still waiting', seen, []);
    w.clock.advance(2);
    same('no answer for 8 s: `silent`', seen, ['silent']);
    w.recv({ t: 'ok', c: 'kick' });
    same('...and a late ok is told to nobody (it is not a second answer)', seen, ['silent']);
    w.dock.command('/kick #149', (c) => seen.push(c));
    same('after the 8 s the next command goes out', w.said(), ['/kick #148', '/kick #149']);
    const x = rig({ noMod: true }), saw = [];
    x.dock.command('/ban #148', (c) => saw.push(c));
    x.sock().readyState = 3; x.sock().onclose({});
    same('a socket that goes while a command waits: `offline`', saw, ['offline']);
    x.dock.command('/ban #148', (c) => saw.push(c));
    same('a command with no socket: `offline`, nothing sent', [saw[1], x.said()], ['offline', ['/ban #148']]);
    const p = rig({ role: 'none', noMod: true }), q2 = [];
    p.dock.command('/del 148', (c) => q2.push(c));
    same('a visitor with no role: `power`, and nothing is sent (the hub would refuse it and post no line)', [q2, p.said()], [['power'], []]);
    const n = rig({ noMod: true }), typed = [];
    n.dock.say('/mutes'); n.dock.command('/del 148', (c) => typed.push(c));
    same('a command that was typed and waits is a command that waits: `rate`', typed, ['rate']);
    const u = rig({ noMod: true });
    same('you(): the name and the role (nothing else)', u.dock.you(), { name: 'Me', role: 'admin' });
    same('line(id): a copy of a person\'s line, with the role it says and whether it is one\'s own (no account key)', [u.dock.line(148), u.dock.line(150).own, u.dock.line(147).sys, u.dock.line(999), u.dock.line(null)],
        [{ id: 148, who: 'Clover', text: 'FREE ant gold!!! see my page', role: 'none', sys: false, u: 'clover.4782', img: false, own: false }, true, true, null, null]);
    check('line(id) is a copy: changing it changes nothing', (() => { const l = u.dock.line(148); l.text = 'x'; return u.dock.line(148).text === 'FREE ant gold!!! see my page'; })());
    u.dock.note('Done: a thing.');
    same('note(text): one private line, in the log, for this visitor', u.privates(), ['Done: a thing.']);
    const seenRows = [];
    const v = rig({ noMod: true });
    v.dock.rows((id, el) => seenRows.push(id + ':' + (el.getAttribute('data-n'))));
    v.recv({ t: 'line', id: 151, at: T0 + 151, who: 'Fern', text: 'hello', mod: false });
    same('rows(fn): told of a new person\'s line with its element, and not of a private answer or a line of the hub\'s own', seenRows, ['151:151']);
    const z = rig({ role: 'none', noMod: true }), none = [];
    z.dock.rows((id) => none.push(id)); z.recv({ t: 'line', id: 151, at: T0 + 151, who: 'Fern', text: 'hello', mod: false });
    same('rows(fn): told of a line for a visitor with no role too (the sheet marks the name of a line for anybody who is in a room), and of nothing else', none, [151]);
    const b = rig({ noMod: true }), by = b.dock.by(148);
    same('by(id): for staff, the lines of the log that the author of line `id` wrote, newest first, as { id, text, at, img } and nothing else (no key, no name)', [by, b.dock.by(150).length, b.dock.by(147), b.dock.by(999), b.dock.by(null), b.dock.by('148')],
        [[{ id: 148, text: 'FREE ant gold!!! see my page', at: T0 + 148, img: false }], 1, [], [], [], []]);
    const bb = rig({ noMod: true, history: [line(1, 'Clover', 'one', { k: 'k-c' }), line(2, 'Pebble', 'two', { k: 'k-p' }), line(3, 'Clover2', 'three', { k: 'k-c' }), line(4, 'Clover', 'four', { k: 'k-other' }), line(5, 'Fern', 'five', { k: '' }), line(6, 'Fern', 'six', { k: '' }), line(7, 'system', 'Dash was kicked.', { k: 'k-c', sys: true })] });
    same('...matched by the hub\'s key of the account: a changed name is the same person, the same name with another key is not, and a line with no key has no one to match', [bb.dock.by(1).map((x) => x.id), bb.dock.by(3).map((x) => x.id), bb.dock.by(4).map((x) => x.id), bb.dock.by(5).map((x) => x.id)], [[3, 1], [3, 1], [4], []]);
    same('...the hub\'s own line is nobody\'s, whatever key it carries, and it is never counted for anybody', [bb.dock.by(7), bb.dock.by(1).map((x) => x.id)], [[], [3, 1]]);
    bb.dock.note('a private answer');
    same('...never a private answer', bb.dock.by(1).length, 2);
    const nobody = rig({ role: 'none', noMod: true });
    same('...and nothing for a visitor with no role', [nobody.dock.by(148), nobody.dock.by(141)], [[], []]);
    nobody.you('mod');
    same('...until the hub gives the role', nobody.dock.by(148).length, 1);
    const wh = b.dock.when(T0 + 148), wn = b.dock.when(undefined), wd = b.dock.when(T0 + 148 - 3 * 86400);
    check('when(at): the log\'s own words for a time (the clock, the full date, the divider\'s word, a number per local day); none for a line with no time', wn === null && typeof wh.time === 'string' && wh.time.length > 0 && wh.full.indexOf(wh.time) !== -1 && typeof wh.day === 'string' && wh.day.length > 0 && wh.key !== wd.key && wh.key === b.dock.when(T0 + 150).key);
    const told = [], ar = rig({ noMod: true });
    ar.dock.arrived(() => told.push('line'));
    ar.recv({ t: 'line', id: 151, at: T0 + 151, who: 'Fern', text: 'hello', mod: false, k: 'k-fern' });
    ar.recv({ t: 'line', id: 151, at: T0 + 151, who: 'Fern', text: 'hello', mod: false, k: 'k-fern' });
    ar.dock.note('a private answer');
    ar.recv({ t: 'del', id: 151 });
    same('arrived(fn): told once for each new line of the log, not for the same line twice, a private answer or a deleted line', told, ['line']);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. Who gets it
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const a = rig({ role: 'admin' });
    same('an administrator: every line but the administrator\'s own colleague\'s and the hub\'s (own line too, for Delete)', a.acts(), [141, 143, 148, 149, 150]);
    const m = rig({ role: 'mod' });
    same('a moderator: not the moderator\'s line (Sage), not the administrator\'s, not the hub\'s', m.acts(), [141, 148, 149, 150]);
    check('a line that is marked carries the words for a screen reader, and a line that is not carries none', a.row(141).getAttribute('aria-description') === Pure.TEXT.rowHint && a.row(146).getAttribute('aria-description') === null && a.row(147).getAttribute('aria-description') === null);
    check('the log is described by a hidden line that says how (the keys), and a hidden status line is there for what the keys choose', a.log.getAttribute('aria-describedby') === 'gchat-mod-hint' && a.dock.root.findAll((e) => e.id === 'gchat-mod-hint').length === 1 && a.dock.root.findAll((e) => e.getAttribute && e.getAttribute('role') === 'status' && cls(e).indexOf('gc-news') !== -1).length >= 2);
    a.recv({ t: 'line', id: 160, at: T0 + 160, who: 'Fern', text: 'new line', mod: false });
    check('a line that comes later is marked as it is drawn', a.row(160).getAttribute('data-act') === 'true');
    a.recv({ t: 'line', id: 161, at: T0 + 161, who: 'Aster', text: 'a colleague', mod: true, role: 'admin' });
    check('...and one that the viewer may not act on is not', a.row(161).getAttribute('data-act') === null);
    a.you('mod');
    same('promoted or demoted: the marks follow at once (an administrator who becomes a moderator loses Sage and Aster)', a.acts(), [141, 148, 149, 150, 160]);
    a.you('admin');
    same('...and come back', a.acts(), [141, 143, 148, 149, 150, 160]);
    a.you('none');
    check('the role goes: no line is marked, nothing describes the log, and a right click is the browser\'s', a.acts().length === 0 && a.row(141).getAttribute('aria-description') === null && a.log.getAttribute('aria-describedby') === null && a.menu(148) === false && a.shade() === null);
    check('...and the hidden words that tell a screen reader of the keys are gone too (they come back with the role)', (() => { const h = a.dock.root.findAll((e) => e.id === 'gchat-mod-hint'); return h.length === 1 && h[0].hidden === true; })());
    a.you('mod');
    check('...and the words are there again', (() => { const h = a.dock.root.findAll((e) => e.id === 'gchat-mod-hint'); return h.length === 1 && h[0].hidden === false; })());
    check('...and a role that comes back marks the lines again', a.acts().length === 5 && a.log.getAttribute('aria-describedby') === 'gchat-mod-hint');
    const p = rig({ role: 'none', noMod: false });
    same('a visitor with no role who somehow has the script: nothing is marked, and every way in is the browser\'s own', [p.acts(), p.menu(148), p.down(148), p.key('ArrowUp'), p.key('Enter'), p.shade()], [[], false, false, false, false, null]);
    p.clock.advance(1000);
    check('...also after a long press', p.shade() === null && p.marked().length === 0 && p.buzzes.length === 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. The ways in
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{   // a right click
    const r = rig();
    check('a right click on a line that has actions: the browser\'s menu is held back and the sheet opens on that line', r.menu(148) === true && r.shade() !== null && r.mod.state() === 'line' && r.marked().join() === '148');
    r.press('Cancel');
    check('...and it goes with Cancel', r.shade() === null && r.marked().length === 0);
    check('a right click on the administrator\'s line (no action for a moderator): the browser\'s own menu', (() => { const m = rig({ role: 'mod' }); return m.menu(146) === false && m.menu(143) === false && m.shade() === null; })());
    check('...on a line of the hub\'s own: the browser\'s', r.menu(147) === false && r.shade() === null);
    check('...on a link in a line that has actions: the browser\'s (copy the address, open in a new tab)', (() => { const a = r.row(149).findAll((e) => e.tagName === 'a')[0]; return !!a && r.event(r.log, 'contextmenu', { target: a }) === false && r.shade() === null; })());
    check('...on the log\'s own empty place, or on the day divider: the browser\'s', r.menu(r.log) === false && r.menu(r.log.children[0]) === false && r.shade() === null);
    check('...on a private answer of the chat: the browser\'s', (() => { r.dock.note('x'); const p = r.log.children.find((c) => cls(c).indexOf('gc-private') !== -1); return r.menu(p) === false && r.shade() === null; })());
    r.menu(150);
    same('on a line of one\'s own the sheet opens with Delete only', r.names(), ['Delete this line. Gone from every screen', 'Cancel'].map((x, i) => i === 0 ? 'Delete this line' : x));
    check('a second right click while the sheet is open is held back and opens no second sheet', (() => { r.menu(148); return r.body.children.filter((c) => cls(c).indexOf('gm-shade') !== -1).length === 1; })());
}
{   // a long press
    const r = rig();
    check('a finger down on a line: nothing yet', r.down(148) === false && r.shade() === null && r.marked().length === 0);
    r.clock.advance(149);
    check('before 150 ms the line is not marked (a swipe to scroll does not flash)', r.marked().length === 0);
    r.clock.advance(2);
    check('after 150 ms the line is marked (the gold bar) while the finger is down', r.marked().join() === '148' && r.shade() === null);
    r.clock.advance(340);
    check('just before 500 ms: still no sheet', r.shade() === null);
    r.clock.advance(20);
    check('at 500 ms: the sheet opens on that line and the phone buzzes once, lightly', r.shade() !== null && r.mod.state() === 'line' && r.buzzes.length === 1 && r.buzzes[0] <= 20 && r.marked().join() === '148');
    r.up();
    check('the finger lifting afterwards changes nothing', r.shade() !== null);
    r.clock.advance(1000);
    check('...and nothing opens again', r.body.children.filter((c) => cls(c).indexOf('gm-shade') !== -1).length === 1 && r.buzzes.length === 1);
    r.press('Cancel');
    const t = rig();
    t.down(148); t.clock.advance(300); t.up(); t.clock.advance(1000);
    check('a short tap opens nothing and leaves no mark', t.shade() === null && t.marked().length === 0 && t.buzzes.length === 0);
    const mv = rig();
    mv.down(148); mv.clock.advance(200); mv.move(11, 0); mv.clock.advance(1000);
    check('a finger that moves 11 px (a scroll) cancels it: no sheet, no mark', mv.shade() === null && mv.marked().length === 0);
    const sm = rig();
    sm.down(148); sm.clock.advance(200); sm.move(6, 6); sm.move(-8, 3); sm.clock.advance(400);
    check('...and one that moves 10 px or less (a hand is not still) does not', sm.shade() !== null);
    const vm = rig();
    vm.down(148); vm.clock.advance(100); vm.move(0, -11); vm.clock.advance(1000);
    check('a move along the other axis counts too', vm.shade() === null);
    const pc = rig();
    pc.down(148); pc.clock.advance(300); pc.event(pc.log, 'pointercancel', {}); pc.clock.advance(1000);
    check('the browser taking the touch for a scroll (pointercancel) cancels it', pc.shade() === null && pc.marked().length === 0);
    const sc = rig();
    sc.down(148); sc.clock.advance(300); sc.event(sc.log, 'scroll', {}); sc.clock.advance(1000);
    check('a scroll of the log cancels it', sc.shade() === null && sc.marked().length === 0);
    const ms = rig();
    ms.down(148, { pointerType: 'mouse' }); ms.clock.advance(1000);
    check('a mouse button held down is not a long press (a right click is how a mouse does it)', ms.shade() === null && ms.marked().length === 0);
    const pen = rig();
    pen.down(148, { pointerType: 'pen' }); pen.clock.advance(600);
    check('a pen is a touch', pen.shade() !== null);
    const sec = rig();
    sec.down(148); sec.clock.advance(300); sec.down(141, { isPrimary: false }); sec.clock.advance(1000);
    check('a second finger (a pinch) cancels it', sec.shade() === null && sec.marked().length === 0);
    const lk = rig();
    const anchor = lk.row(149).findAll((e) => e.tagName === 'a')[0];
    check('(the line of the link has a link in it)', !!anchor);
    lk.down(anchor); lk.clock.advance(1000);
    check('a long press on a link is the browser\'s own (its preview)', lk.shade() === null && lk.marked().length === 0 && lk.buzzes.length === 0);
    const no = rig({ role: 'mod' });
    no.down(146); no.clock.advance(1000); no.down(147); no.clock.advance(1000);
    check('a long press on a line without an action, or a line of the hub\'s own: nothing', no.shade() === null && no.marked().length === 0 && no.buzzes.length === 0);
    const al = rig();
    al.down(148); al.clock.advance(300);
    check('a long press and then the browser\'s own contextmenu event (Android sends one): it is held back and opens one sheet only', al.menu(148) === true && al.body.children.filter((c) => cls(c).indexOf('gm-shade') !== -1).length === 1);
    al.clock.advance(1000);
    check('...and the press timer does not open another', al.body.children.filter((c) => cls(c).indexOf('gm-shade') !== -1).length === 1 && al.buzzes.length === 0);
    const nb = rig({ vibrate: false });
    nb.down(148); nb.clock.advance(600);
    check('a browser with no vibration (iOS): the sheet opens all the same', nb.shade() !== null);
    const fg = rig();
    fg.down(148); fg.clock.advance(600);
    fg.press('Mute Clover\u2026');
    check('a long press opened the sheet and the finger is still down: a click that the browser sends to what stands under it is no press', fg.mod.state() === 'line');
    fg.clock.advance(3000); fg.press('Kick Clover');
    check('...however long the finger stays down', fg.mod.state() === 'line');
    fg.up(); fg.press('Kick Clover'); fg.event(fg.shade(), 'click', { target: fg.shade() });
    check('...and in the moment after it lifts (the click of the lifting finger): neither a button nor the dark part', fg.mod.state() === 'line' && fg.shade() !== null);
    check('...a mouse press that the browser makes of it moves no focus', fg.event(fg.shade(), 'mousedown', {}) === true);
    fg.clock.advance(399); fg.press('Kick Clover');
    check('...up to 399 ms after', fg.mod.state() === 'line');
    fg.clock.advance(2);
    check('...and later the sheet takes presses as any other', fg.event(fg.shade(), 'mousedown', {}) === false && (fg.press('Mute Clover\u2026'), fg.mod.state() === 'mute'));
    const fk = rig();
    fk.down(148); fk.clock.advance(600); fk.event(fk.shade(), 'pointerdown', { target: fk.shade() });
    fk.press('Mute Clover\u2026');
    check('a new touch on the sheet is a press, even while the first finger is still counted as down', fk.mod.state() === 'mute');
    const fm = rig();
    fm.menu(148); fm.press('Mute Clover\u2026');
    check('a right click opens the sheet with no finger down: its buttons take presses at once', fm.mod.state() === 'mute');
    const ac = rig();
    ac.down(148); ac.clock.advance(300);
    check('Android\'s own contextmenu event comes first, with the finger down: it opens the sheet', ac.menu(148) === true && ac.shade() !== null);
    ac.press('Mute Clover\u2026'); ac.clock.advance(1000); ac.press('Mute Clover\u2026');
    check('...and the finger is counted: no press while it is down', ac.mod.state() === 'line');
    ac.up(); ac.press('Mute Clover\u2026');
    check('...nor in the moment after it lifts', ac.mod.state() === 'line');
    ac.clock.advance(401); ac.press('Mute Clover\u2026');
    check('...then it takes presses', ac.mod.state() === 'mute');
    const cm = rig();
    cm.open(148);
    check('the browser\'s own menu is held back over the sheet too (the menu key sends its event when it is let go)', cm.event(cm.shade(), 'contextmenu', { target: cm.box() }) === true);
    const kp = rig();
    kp.key('ArrowUp');
    check('(a line is chosen with the keys)', kp.marked().join() === '150');
    kp.down(148); kp.clock.advance(200);
    check('a finger down on another line lets the keys\' line go: one line is marked, the pressed one', kp.marked().join() === '148');
    kp.up(); kp.clock.advance(1000);
    check('...and none after it lifts', kp.marked().length === 0);
    const km = rig();
    km.key('ArrowUp'); km.down(148, { pointerType: 'mouse' });
    check('a mouse press does the same', km.marked().length === 0);
    const rl = rig();
    rl.down(148); rl.clock.advance(300); rl.you('none'); rl.clock.advance(1000);
    check('the role going during a press: no sheet, no mark', rl.shade() === null && rl.marked().length === 0);
}
{   // the keys
    const r = rig();
    check('Up with no line chosen: the newest line that has actions is chosen, the key is ours, and the line is marked', r.key('ArrowUp') === true && r.marked().join() === '150');
    const status = r.dock.root.findAll((e) => e.getAttribute && e.getAttribute('role') === 'status' && cls(e).indexOf('gc-news') !== -1 && e.textContent.indexOf('looking at it') !== -1);
    check('...and a status line says which one, and how to open it', status.length === 1 && status[0].textContent.indexOf('Me: looking at it.') === 0 && status[0].textContent.indexOf('Enter') !== -1);
    r.key('ArrowUp'); r.key('ArrowUp');
    same('Up again: the older lines, and the line of the hub\'s own and the administrator\'s are skipped', r.marked(), [148]);
    r.key('ArrowUp'); r.key('ArrowUp'); r.key('ArrowUp'); r.key('ArrowUp');
    same('...to the oldest, and no further', r.marked(), [141]);
    r.key('ArrowDown');
    same('Down: the next newer line that has actions', r.marked(), [143]);
    for (let i = 0; i < 9; i++) r.key('ArrowDown');
    same('...to the newest, and no further', r.marked(), [150]);
    check('Control and Alt with the arrows are not ours', r.key('ArrowUp', { ctrlKey: true }) === false && r.key('ArrowUp', { altKey: true }) === false && r.marked().join() === '150');
    check('PageUp, Home and the other keys are the browser\'s (they scroll the log)', ['PageUp', 'PageDown', 'Home', 'End', 'a'].every((k) => r.key(k) === false));
    r.key('ArrowUp');
    check('Enter opens the sheet of the chosen line', r.key('Enter') === true && r.shade() !== null && r.mod.state() === 'line');
    r.press('Cancel');
    check('...and Cancel gives the focus back to the log (and keeps the line chosen, for the next key)', r.doc.activeElement === r.log && r.marked().join() === '149');
    check('the menu key opens it too', r.key('ContextMenu') === true && r.shade() !== null);
    r.press('Cancel');
    check('...and so does Shift and F10', r.key('F10', { shiftKey: true }) === true && r.shade() !== null);
    r.press('Cancel');
    check('F10 alone is not ours', r.key('F10') === false);
    let stopped = false;
    r.key('Escape', { stopPropagation() { stopped = true; } });
    check('Escape lets go of the chosen line, and is not also the dock\'s own Escape (which folds the chat)', r.marked().length === 0 && stopped);
    stopped = false; r.key('Escape', { stopPropagation() { stopped = true; } });
    check('...Escape with no line chosen is left to the dock', !stopped);
    check('Enter with no line chosen does nothing (it is the log\'s)', r.key('Enter') === false && r.shade() === null);
    check('the menu key with no line chosen opens the newest line that has actions', r.key('ContextMenu') === true && r.shade() !== null && r.marked().join() === '150');
    r.press('Cancel');
    r.log.fire('blur', {});
    check('the log losing the focus lets go of the chosen line', r.marked().length === 0);
    const k = rig({ role: 'mod', history: [line(143, 'Sage', 'a', { role: 'mod' }), line(146, 'Tess', 'b', { role: 'admin' })] });
    check('no line with an action: the arrows are the browser\'s (the log scrolls)', k.key('ArrowUp') === false && k.key('ArrowDown') === false && k.key('Enter') === false);
    const menukey = rig();
    menukey.key('ArrowUp');
    check('the contextmenu event that a menu key makes on the focused log (target: the log itself) opens the chosen line', menukey.menu(menukey.log) === true && menukey.shade() !== null && menukey.marked().join() === '150');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. The sheet
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{   // what it shows
    const r = rig();
    r.open(148);
    const box = r.box();
    same('an administrator on a person\'s line: the recent messages row (with the count of their lines in the log), Delete, Mute, Kick, Ban and Cancel, each named by its verb and the person', r.names(), ['Recent messages from Clover (1)', 'Delete this line', 'Mute Clover\u2026', 'Kick Clover', 'Ban Clover', 'Cancel']);
    same('...each action says what it does in a few words (the description)', r.buttons().slice(1, 5).map((b) => r.env.elements && b.getAttribute('aria-describedby')).map((id) => id && one(box, 'gm-act') && box.findAll((e) => e.id === id)[0].textContent), ['Gone from every screen', 'Cannot write for a while', 'Logged out; can log in again', 'Can read, cannot write']);
    check('it is a dialog, modal, labelled by its title and described by the line', box.getAttribute('role') === 'dialog' && box.getAttribute('aria-modal') === 'true' && box.getAttribute('aria-labelledby') === 'gm-title' && box.findAll((e) => e.id === 'gm-title')[0].textContent === 'Clover' && box.getAttribute('aria-describedby') === 'gm-quote' && box.findAll((e) => e.id === 'gm-quote')[0].textContent === 'FREE ant gold!!! see my page');
    same('the header: the author, then the username where the hub sent one and the number of the line', one(box, 'gm-sub').textContent, '@clover.4782 \u00b7 line #148');
    check('the first action (Delete, not the recent messages row) has the focus', r.doc.activeElement === r.buttons()[1]);
    check('the recent messages row and Mute are the only ones with a chevron (each goes on to a second step)', box.findAll((e) => e.tagName === 'svg' && cls(e).indexOf('gm-chev') !== -1).length === 2 && [0, 2].every((i) => r.buttons()[i].findAll((e) => e.tagName === 'svg' && cls(e).indexOf('gm-chev') !== -1).length === 1));
    check('the line is marked while its sheet is open, and what stands behind is inert (the page, not the sheet)', r.marked().join() === '148' && r.host.hasAttribute('inert') && !r.shade().hasAttribute('inert'));
    r.press('Cancel');
    check('Cancel: the sheet and the mark and the inert are gone, and nothing was sent', r.shade() === null && r.marked().length === 0 && !r.host.hasAttribute('inert') && r.said().length === 0 && r.privates().length === 0);
    r.open(143);
    same('an administrator on a moderator\'s line: all four, and the header says what the author is', [r.names().length, one(r.box(), 'gm-sub').textContent, cls(one(r.box(), 'gm-name')).indexOf('gc-r-mod') !== -1], [6, '@sage.moss \u00b7 moderator \u00b7 line #143', true]);
    r.press('Cancel');
    r.open(150);
    same('on the viewer\'s own line: Delete and Cancel, and the header says "you"', [r.names(), one(r.box(), 'gm-sub').textContent], [['Delete this line', 'Cancel'], '@me.r \u00b7 administrator \u00b7 line #150 \u00b7 you']);
    r.press('Cancel');
    r.open(141);
    check('a line whose author has no username in the frame: no @ in the header', one(r.box(), 'gm-sub').textContent === 'line #141');
    r.press('Cancel');
    const m = rig({ role: 'mod' });
    m.open(148);
    same('a moderator on a person\'s line: no Ban', m.names(), ['Recent messages from Clover (1)', 'Delete this line', 'Mute Clover\u2026', 'Kick Clover', 'Cancel']);
    const l = rig({ history: [line(148, 'Clover', 'x'.repeat(2000), { u: 'clover.4782' })] });
    l.open(148);
    check('a very long line is cut for the quote (the style sheet keeps three lines of it)', one(l.box(), 'gm-quote').textContent.length <= Pure.LIMIT.quote);
    const p = rig({ history: [line(148, 'Clover', '', { u: 'clover.4782', img: { n: 'a'.repeat(32), x: 'jpg' } })] });
    p.open(148);
    const pq = one(p.box(), 'gm-quote');
    check('a line that is only an image: the quote says so instead of an empty box', pq !== null && pq.textContent.trim() !== '' && p.box().getAttribute('aria-describedby') === 'gm-quote');
    p.press('Delete this line');
    check('... and so does the question before it is deleted', one(p.box(), 'gm-qt') !== null && one(p.box(), 'gm-qt').textContent === pq.textContent);
    const w = rig({ history: [line(148, '<b>Clover</b>', '<i>hi</i> & <script>x</script>', { u: 'clover.4782' })] });
    w.open(148); w.press('Delete this line');
    check('names and words with markup characters in them are text (nothing is made of them but text)', w.env.innerHTMLWrites.length === 0 && one(w.box(), 'gm-qt').textContent === '<i>hi</i> & <script>x</script>' && w.box().findAll((e) => e.tagName === 'b' || e.tagName === 'i' || e.tagName === 'script').filter((e) => e.textContent.indexOf('Clover') === 0 && e.tagName === 'b' && cls(e.parent).indexOf('gm-at') === -1).length === 0);
}
{   // focus, Escape, Tab, the dark part
    const r = rig();
    r.open(148);
    const first = r.buttons()[0], last = r.buttons()[5];
    check('Tab goes to the next control, and from the last to the first (the sheet keeps the focus)', (() => { first.focus(); const stopped = r.event(r.shade(), 'keydown', { key: 'Tab', shiftKey: false }); const second = r.doc.activeElement === r.buttons()[1]; last.focus(); r.event(r.shade(), 'keydown', { key: 'Tab' }); return stopped && second && r.doc.activeElement === first; })());
    check('Shift and Tab goes back, and from the first to the last', (() => { first.focus(); r.event(r.shade(), 'keydown', { key: 'Tab', shiftKey: true }); return r.doc.activeElement === last; })());
    r.event(r.shade(), 'keydown', { key: 'Escape' });
    check('Escape closes the line sheet, with the focus back on the log', r.shade() === null && r.doc.activeElement === r.log);
    r.open(148); r.press('Mute Clover\u2026');
    check('Mute\u2026 is a second step on the same sheet (the line sheet is replaced, not stacked)', r.mod.state() === 'mute' && r.body.children.filter((c) => cls(c).indexOf('gm-shade') !== -1).length === 1);
    r.event(r.shade(), 'keydown', { key: 'Escape' });
    check('Escape in the second step is Back: the line sheet again, with the focus on Mute\u2026', r.mod.state() === 'line' && r.doc.activeElement === r.buttons()[2] && r.shade() !== null);
    r.press('Mute Clover\u2026'); r.press('Back');
    check('the Back button does the same', r.mod.state() === 'line' && r.doc.activeElement === r.buttons()[2]);
    r.press('Cancel');
    r.open(148);
    const shade = r.shade();
    r.event(shade, 'click', { target: shade });
    check('a press on the dark part in the first 400 ms (the finger of a long press lifting) is nothing', r.shade() !== null);
    r.clock.advance(401);
    r.event(shade, 'click', { target: r.box() });
    check('a press on the sheet itself is nothing', r.shade() !== null);
    r.event(shade, 'click', { target: shade });
    check('a press on the dark part later is Cancel', r.shade() === null && r.said().length === 0 && r.doc.activeElement === r.log);
}
{   // Delete, Kick, Ban: the confirm card
    for (const [verb, cmd, title, words] of [['Delete this line', '/del 148', 'Delete this line?', 'Done: the line is gone.'], ['Kick Clover', '/kick #148', 'Kick Clover?', 'Done: Clover was kicked.'], ['Ban Clover', '/ban #148', 'Ban Clover?', 'Done: Clover is banned.']]) {
        const r = rig();
        r.open(148); r.press(verb);
        check(verb + ': a card, not the sheet (nothing sent yet), with the question and Cancel focused', r.mod.state() === 'confirm' && r.shade().className.indexOf('gm-card') !== -1 && r.said().length === 0 && r.doc.activeElement === r.named('Cancel') && one(r.box(), 'gm-title').textContent === title && r.box().getAttribute('aria-describedby') === 'gm-text');
        r.press(verb === 'Delete this line' ? 'Delete' : verb.split(' ')[0]);
        check('...the red button is dead for 400 ms (a double press is not an answer)', r.said().length === 0 && r.box() !== null);
        r.clock.advance(401);
        r.press(verb === 'Delete this line' ? 'Delete' : verb.split(' ')[0]);
        same('...then it sends the command, exactly once', r.said(), [cmd]);
        r.press(verb === 'Delete this line' ? 'Delete' : verb.split(' ')[0]); r.press('Cancel');
        same('...and while the answer is awaited every control takes no press', [r.said().length, r.box() !== null, r.buttons().every((b) => b.getAttribute('aria-disabled') === 'true')], [1, true, true]);
        check('...the pressed button says it is busy', r.buttons().filter((b) => b.getAttribute('aria-busy') === 'true').length === 1 && cls(r.buttons().find((b) => b.getAttribute('aria-busy') === 'true')).indexOf('gm-wait') !== -1);
        r.event(r.shade(), 'keydown', { key: 'Escape' });
        r.event(r.shade(), 'click', { target: r.shade() });
        check('...and neither Escape nor the dark part closes it', r.box() !== null);
        r.recv({ t: 'ok', c: verb.split(' ')[0].toLowerCase() === 'delete' ? 'del' : verb.split(' ')[0].toLowerCase() });
        check('the hub\'s ok: the sheet closes, the focus is back on the log, the inert is gone', r.shade() === null && r.doc.activeElement === r.log && !r.host.hasAttribute('inert'));
        check('...a toast at the top says it, as a polite status that takes no focus, and the log has the same words as a private line', r.toastText() === words && r.toast().getAttribute('role') === 'status' && cls(r.toast()).indexOf('gm-bad') === -1 && r.doc.activeElement === r.log && r.privates().join() === words, r.toastText());
        check('...the toast has no button and goes after 6 s', one(r.toast(), 'gm-tok') === null && (r.clock.advance(5990), r.toast() !== null) && (r.clock.advance(20), r.toast() === null));
        check('...the field was not touched', r.dock.field.value === '');
    }
    const r = rig();
    r.open(148); r.press('Ban Clover');
    check('Ban has a second red button (and the words about the hour), Cancel has the focus', r.names().join('|') === 'Cancel|Ban|Ban and delete their lines from the last hour' && r.doc.activeElement === r.named('Cancel') && one(r.box(), 'gm-note').textContent.indexOf('hour before the ban') !== -1);
    r.press('Ban and delete their lines from the last hour');
    check('...the second red button is dead for 400 ms too', r.said().length === 0 && r.box() !== null);
    r.clock.advance(401); r.press('Ban and delete their lines from the last hour');
    same('...then it sends /ban with purge', r.said(), ['/ban #148 purge']);
    r.recv({ t: 'ok', c: 'ban' });
    same('...and says so', [r.toastText(), r.privates()], ['Done: Clover is banned and the lines of the last hour are gone.', ['Done: Clover is banned and the lines of the last hour are gone.']]);
    const k = rig();
    k.open(148); k.press('Kick Clover');
    check('Kick has no second button', k.names().join('|') === 'Cancel|Kick');
    check('the card names the person and what happens, and what everybody will read', one(k.box(), 'gm-text').textContent === 'Clover is logged out of the chat and can log in again at once. Everyone sees \u201cClover was kicked.\u201d' && one(k.box(), 'gm-cn').textContent === 'Clover (@clover.4782)');
    k.press('Cancel');
    check('Cancel on a card: closed, focus on the log, nothing sent', k.shade() === null && k.said().length === 0 && k.doc.activeElement === k.log);
    k.open(148); k.press('Ban Clover');
    check('the Ban card says that an administrator lifts the ban and what everybody will read', one(k.box(), 'gm-text').textContent === 'Clover can still read the chat but cannot write until an administrator lifts the ban. Everyone sees \u201cClover was banned.\u201d');
    k.event(k.shade(), 'keydown', { key: 'Escape' });
    check('Escape on a card is Cancel', k.shade() === null && k.said().length === 0);
    k.open(148); k.press('Delete this line');
    check('the Delete card quotes the line, and says that it cannot be undone', one(k.box(), 'gm-qt').textContent === 'FREE ant gold!!! see my page' && one(k.box(), 'gm-text').textContent === 'It goes from every screen and from the history. This cannot be undone.' && k.named('Delete') !== null);
}
{   // Mute: four times and a custom number
    for (const [name, minutes] of [['10 min', 10], ['1 hour', 60], ['1 day', 1440], ['1 week', 10080]]) {
        const r = rig();
        r.open(148); r.press('Mute Clover\u2026');
        if (minutes === 10) {
            same('the second step: the title, the four times, a custom field and its button, Back', [one(r.box(), 'gm-name').textContent, r.names()], ['Mute Clover \u2014 for how long?', ['10 min', '1 hour', '1 day', '1 week', 'Mute', 'Back']]);
            check('...the first time has the focus, and no confirm card stands in the way (a mute is short, and Unmute is one tap)', r.doc.activeElement === r.buttons()[0] && r.said().length === 0);
        }
        r.press(name);
        same(name + ': a tap sends it at once, by the number of the line', r.said(), ['/mute #148 ' + minutes]);
        r.recv({ t: 'ok', c: 'mute' });
        same('...and the toast and the private line say the same', [r.toastText(), r.privates()], ['Done: Clover is muted for ' + Pure.lengthOf(minutes) + '.', ['Done: Clover is muted for ' + Pure.lengthOf(minutes) + '.']]);
    }
    const r = rig();
    r.open(148); r.press('Mute Clover\u2026');
    const input = r.box().findAll((e) => e.tagName === 'input')[0], go = r.named('Mute');
    check('the custom field: numbers, not more than five characters, a label, the button is off until a good number is typed', input.getAttribute('inputmode') === 'numeric' && input.getAttribute('maxlength') === '5' && input.getAttribute('aria-label') === 'Custom mute, in minutes' && go.disabled === true);
    const type = (v) => { input.value = v; input.fire('input', {}); return { go: !go.disabled, bad: input.getAttribute('aria-invalid'), hint: one(r.box(), 'gm-hint').textContent, err: cls(one(r.box(), 'gm-hint')).indexOf('gm-err') !== -1 }; };
    same('empty: the hint says the range', type(''), { go: false, bad: 'false', hint: 'Or type 1 to 10,080 minutes (1 week).', err: false });
    same('45: on, and the hint says it', type('45'), { go: true, bad: 'false', hint: '45 minutes', err: false });
    same('90: on, and the hint says it in hours', type('90'), { go: true, bad: 'false', hint: '90 minutes = 1 hour 30 minutes', err: false });
    same('1: one minute, singular', type('1'), { go: true, bad: 'false', hint: '1 minute', err: false });
    same('10080: the longest, a week', type('10080'), { go: true, bad: 'false', hint: '10080 minutes = 1 week', err: false });
    same('20000: off, the field is wrong, the hint says the most', type('20000'), { go: false, bad: 'true', hint: 'The most is 10,080 minutes (1 week).', err: true });
    same('0: off and wrong', [type('0').go, type('0').bad], [false, 'true']);
    same('a letter: off and wrong', [type('4a').go, type('4a').bad], [false, 'true']);
    same('blanks only: off, and not called wrong', [type('  ').go, type('  ').bad], [false, 'false']);
    type('90');
    check('Enter in the field with a good number sends it, and nothing else does', (() => { const stopped = r.event(input, 'keydown', { key: 'Enter' }); return stopped && r.said().join() === '/mute #148 90'; })());
    r.recv({ t: 'ok', c: 'mute' });
    check('...and says it', r.toastText() === 'Done: Clover is muted for 1 hour 30 minutes.');
    const s = rig();
    s.open(148); s.press('Mute Clover\u2026');
    const si = s.box().findAll((e) => e.tagName === 'input')[0];
    si.value = '20000'; si.fire('input', {}); s.event(si, 'keydown', { key: 'Enter' });
    check('Enter with a wrong number sends nothing', s.said().length === 0);
    si.value = '75'; si.fire('input', {}); s.named('Mute').click();
    same('the Mute button sends the custom number', s.said(), ['/mute #148 75']);
    const t = rig();
    t.open(148); t.press('Mute Clover\u2026');
    const tin = t.box().findAll((e) => e.tagName === 'input')[0], names = t.names();
    check('the Tab order is the times, the field, its button, Back', names.join('|') === '10 min|1 hour|1 day|1 week|Mute|Back' && (() => { tin.focus(); t.event(t.shade(), 'keydown', { key: 'Tab' }); return t.doc.activeElement === t.named('Back') || t.doc.activeElement === t.named('Mute'); })());
    check('...and a button that is off is skipped by Tab', (() => { const m = t.named('Mute'); tin.focus(); t.event(t.shade(), 'keydown', { key: 'Tab' }); return m.disabled === true && t.doc.activeElement === t.named('Back'); })());
}
{   // the answers
    const cases = [
        ['power', 'Ban Clover', 'You cannot do that to Clover.'], ['role', 'Kick Clover', 'Could not confirm your role with Discord, try again in a moment.'], ['who', 'Kick Clover', 'Clover is not on the front page now, so there is nobody to kick. Mute or ban instead.'],
        ['who', 'Ban Clover', 'That person is not here any more.'], ['bad', 'Ban Clover', 'Clover is banned already.'], ['bad', 'Delete this line', 'That line is gone already.'], ['rate', 'Kick Clover', 'Too fast: wait a moment and try again.'],
        ['quota', 'Kick Clover', 'That did not work, try again.'], ['muted', 'Kick Clover', 'You cannot do that now.']];
    for (const [code, verb, words] of cases) {
        const r = rig();
        r.open(148); r.press(verb); r.clock.advance(401);
        r.press(verb === 'Delete this line' ? 'Delete' : verb.split(' ')[0]);
        r.recv({ t: 'err', code });
        const t = r.toast();
        check('the hub\'s `' + code + '` to ' + verb + ': the sheet closes and the toast says "' + words + '"', r.shade() === null && r.toastText() === words && cls(t).indexOf('gm-bad') !== -1 && r.privates().join() === words, r.toastText());
        check('...a refusal has an OK button, stays 12 s (not 6), and takes no focus', !!one(t, 'gm-tok') && (r.clock.advance(11990), r.toast() !== null) && r.doc.activeElement === r.log);
        one(t, 'gm-tok').click();
        check('...and OK takes it away at once', r.toast() === null);
    }
    const r = rig();
    r.open(143); r.press('Ban Sage'); r.clock.advance(401); r.press('Ban');
    r.recv({ t: 'err', code: 'power' });
    check('power on a moderator\'s line says what they are', r.toastText() === 'You cannot do that to Sage (a moderator).');
    const s = rig();
    s.open(148); s.press('Kick Clover'); s.clock.advance(401); s.press('Kick');
    s.clock.advance(8001);
    check('no answer in 8 s: the sheet closes and the toast says to check the chat', s.shade() === null && s.toastText() === 'Nothing came back: check the chat before trying again.' && cls(s.toast()).indexOf('gm-bad') !== -1);
    const t = rig();
    t.open(148); t.press('Kick Clover'); t.clock.advance(401); t.press('Kick');
    t.sock().readyState = 3; t.sock().onclose({});
    check('the socket going while the command waits: "Chat is offline: nothing was done." with OK', t.shade() === null && t.toastText() === 'Chat is offline: nothing was done.' && !!one(t.toast(), 'gm-tok'));
    const u = rig();
    u.open(148); u.press('Kick Clover'); u.clock.advance(401);
    u.sock().readyState = 3; u.sock().onclose({});
    u.press('Kick');
    check('a press with the socket already gone: nothing is sent, offline', u.said().length === 0 && u.toastText() === 'Chat is offline: nothing was done.');
    const v = rig();
    v.open(148); v.press('Delete this line'); v.clock.advance(401); v.press('Delete');
    v.recv({ t: 'del', id: 148 });
    check('the hub\'s del frame, with our command in flight: the sheet stays and waits for the answer, and says nothing yet', v.shade() !== null && v.toast() === null);
    v.recv({ t: 'ok', c: 'del' });
    check('the hub\'s del frame comes before its ok: that is not "gone already" (our own command is in flight)', v.toastText() === 'Done: the line is gone.' && v.shade() === null && v.privates().join() === 'Done: the line is gone.');
    const w = rig();
    w.open(148);
    w.recv({ t: 'del', id: 148 });
    check('somebody else deletes the line while its sheet is open: the sheet closes and the toast says so', w.shade() === null && w.toastText() === 'That line is gone already.' && cls(w.toast()).indexOf('gm-bad') !== -1);
    const x = rig();
    x.open(148);
    x.recv({ t: 'del', id: 141 });
    check('another line going changes nothing', x.shade() !== null);
    const y = rig();
    y.open(148); y.you('none');
    check('the role going while a sheet is open closes it at once, with nothing sent and no mark', y.shade() === null && y.said().length === 0 && y.acts().length === 0 && !y.host.hasAttribute('inert'));
    const z = rig();
    z.open(148); z.press('Kick Clover'); z.clock.advance(401); z.press('Kick');
    z.you('none'); z.recv({ t: 'ok', c: 'kick' });
    check('the role going while a command waits: the sheet closes, and the answer is still told (the command was sent)', z.shade() === null && z.toastText() === 'Done: Clover was kicked.');
    const zz = rig();
    zz.open(148); zz.press('Kick Clover'); zz.clock.advance(401); zz.press('Kick');
    zz.you('none'); zz.you('admin'); zz.open(141); zz.press('Kick Pebble'); zz.clock.advance(401); zz.press('Kick');
    check('a second command while the first still waits (its sheet had gone with the role): `rate`, a toast with OK', zz.toastText() === 'Too fast: wait a moment and try again.' && zz.shade() === null);
    zz.recv({ t: 'ok', c: 'kick' });
    check('...and the first one\'s answer comes: the toast is replaced, never two at once', zz.body.children.filter((c) => cls(c).indexOf('gm-toast') !== -1).length === 1 && zz.toastText() === 'Done: Clover was kicked.');
    const q = rig();
    q.open(148); q.press('Kick Clover'); q.clock.advance(401); q.press('Kick');
    q.recv({ t: 'ok', c: 'kick' });
    q.open(141); q.press('Kick Pebble'); q.clock.advance(401); q.press('Kick');
    same('a second action after the first: its own command, and the first toast goes when the next sheet opens (the private line keeps it)', [q.said(), q.toastText()], [['/kick #148', '/kick #141'], null]);
    q.recv({ t: 'ok', c: 'kick' });
    check('...and the first toast was replaced, not stacked', q.body.children.filter((c) => cls(c).indexOf('gm-toast') !== -1).length === 1 && q.toastText() === 'Done: Pebble was kicked.');
    const inertBefore = rig();
    inertBefore.host.setAttribute('inert', '');
    inertBefore.open(148); inertBefore.press('Cancel');
    check('what was inert before the sheet stays inert after it', inertBefore.host.hasAttribute('inert'));
}
{   // the bar is where the toast goes
    const r = rig();
    r.open(148); r.press('Delete this line'); r.clock.advance(401); r.press('Delete'); r.recv({ t: 'ok', c: 'del' });
    check('the toast stands at the top (a number of px under the bar), not at the bottom, and is placed with style, not markup', /^\d+px$/.test(r.toast().style.top) && r.toast().style.bottom === undefined);
    const b = rig();
    b.doc.querySelector = (s) => (s === '.hbar' ? { getBoundingClientRect: () => ({ bottom: 64 }) } : null);
    b.open(148); b.press('Delete this line'); b.clock.advance(401); b.press('Delete'); b.recv({ t: 'ok', c: 'del' });
    check('...10 px under the bar where the page has one', b.toast().style.top === '74px');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5b. The recent messages
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const CL = { u: 'clover.4782' };
    const hist = () => [
        line(139, 'Clover', 'old line from three days ago', Object.assign({ at: T0 - 3 * 86400 }, CL)),
        line(141, 'Pebble', 'anyone up for Islands?'),
        line(143, 'Sage', 'Please be kind.', { role: 'mod', u: 'sage.moss' }),
        line(144, 'Clover', 'FREE ant gold!!! see my page', CL),
        line(145, 'Juniper', 'looks like spam', { u: 'juniper.berry' }),
        line(147, 'system', 'Dash was kicked.', { sys: true, k: '' }),
        line(148, 'Clover', 'second: <b>free</b> & https://example.org/x', CL),
        line(150, 'Me', 'looking at it', { u: 'me.r' }),
        line(151, 'Clover', 'third line', CL),
    ];
    const items = (r) => r.box().findAll((e) => cls(e).indexOf('gm-ri') !== -1);
    const ids = (r) => items(r).map((e) => Number(e.getAttribute('data-n')));
    const rowName = (r) => { const b = r.buttons()[0]; return b ? b.getAttribute('aria-label') : null; };
    const said = (r, name) => r.box().findAll((e) => e.getAttribute && e.getAttribute('data-n') === String(name))[0];
    const keyOn = (r, k, extra) => r.event(r.shade(), 'keydown', Object.assign({ key: k }, extra));
    const lineOf = (id, who, text, extra) => ({ t: 'line', id, at: T0 + id, who, text, mod: false, k: 'k-' + who.toLowerCase(), u: 'clover.4782', ...(extra || {}) });
    const red = (r, name) => { r.press(name); r.clock.advance(401); r.press('Delete'); };

    // the row
    const r = rig({ history: hist() });
    r.open(148);
    check('the sheet of a person\'s line has the recent messages row first (under the quoted line), with the count of their lines in the log, a chevron and its description', rowName(r) === 'Recent messages from Clover (4)' && r.buttons()[0].findAll((e) => e.tagName === 'svg' && cls(e).indexOf('gm-chev') !== -1).length === 1 && r.box().findAll((e) => e.id === 'gm-d-recent')[0].textContent === 'Their lines in this chat, newest first' && r.buttons()[0].getAttribute('aria-describedby') === 'gm-d-recent');
    check('...the first action is still Delete (the row is not the first stop of the focus)', r.doc.activeElement === r.buttons()[1] && r.names()[1] === 'Delete this line');
    check('...the hub\'s own line is not counted, nor are the lines of other people', rowName(r).indexOf('(4)') !== -1 && r.dock.by(148).every((x) => x.id !== 147));
    r.press('Cancel');
    r.open(141);
    check('a person with one line in the log: the row stays, with (1)', rowName(r) === 'Recent messages from Pebble (1)');
    r.press('Cancel');
    r.open(143);
    check('an administrator on a moderator\'s line: the row too', rowName(r) === 'Recent messages from Sage (1)');
    r.press('Cancel');
    r.open(150);
    check('a line of one\'s own: Delete only, no row (that sheet stays what it was)', r.names().join('|') === 'Delete this line|Cancel');
    r.press('Cancel');
    const m = rig({ role: 'mod', history: hist() });
    m.open(148);
    check('a moderator on a person\'s line: the row, and no Ban', rowName(m) === 'Recent messages from Clover (4)' && m.names().indexOf('Ban Clover') === -1);
    m.press('Cancel');
    check('a moderator on a moderator\'s line: no sheet at all (so no row)', m.menu(143) === false && m.shade() === null);
    const nk = rig({ history: [line(1, 'Fern', 'a line from a hub that sent no key', { k: '' }), line(2, 'Fern', 'and another', { k: '' })] });
    nk.open(1);
    check('a line that carries no key: no row (nobody to match), the sheet is the old one', nk.names().join('|') === 'Delete this line|Mute Fern…|Kick Fern|Ban Fern|Cancel');
    const pl = rig({ role: 'none', history: hist() });
    check('a person with no role never sees it: no sheet, no marks, the browser\'s own menu, nothing of the recent messages is asked of the dock', pl.menu(148) === false && pl.shade() === null && pl.acts().length === 0 && pl.dock.by(148).length === 0);
    pl.you('mod');
    pl.menu(148);
    check('...a role that comes gives it', rowName(pl) === 'Recent messages from Clover (4)');
    pl.you('none');
    check('...and a role that goes closes the sheet at once, with the row', pl.shade() === null);

    // the list
    const q = rig({ history: hist() });
    q.open(148); q.press('Recent messages from Clover (4)');
    check('the row opens a second step on the same sheet: titled by the person, described by its keys, named group of lines', q.mod.state() === 'recent' && q.body.children.filter((c) => cls(c).indexOf('gm-shade') !== -1).length === 1 && q.box().findAll((e) => e.id === 'gm-title')[0].textContent === 'Recent messages from Clover' && q.box().getAttribute('aria-labelledby') === 'gm-title' && one(q.box(), 'gm-rl').getAttribute('role') === 'group' && one(q.box(), 'gm-rl').getAttribute('aria-label') === 'Recent messages, newest first' && /Up and Down/.test(one(q.box(), 'gm-rl').getAttribute('aria-description')));
    same('...the lines of that person, newest first, and nobody else\'s', ids(q), [151, 148, 144, 139]);
    same('...the header says who and how many', one(q.box(), 'gm-sub').textContent, '@clover.4782 · 4 messages in this chat');
    check('...every line has its time in the log\'s own words, and the full date as its title', ids(q).every((id) => { const tt = q.row(id).findAll((e) => cls(e).indexOf('gc-t') !== -1)[0], mine = said(q, id); return tt && one(mine, 'gm-rt').textContent === tt.textContent && mine.getAttribute('title') === tt.getAttribute('title').replace(/^#\d+ /, ''); }), ids(q).map((id) => [one(said(q, id), 'gm-rt').textContent, q.row(id).findAll((e) => cls(e).indexOf('gc-t') !== -1)[0].textContent].join('=')).join());
    check('...words are text: markup characters and an address stay as typed, no link, no element made from them', one(said(q, 148), 'gm-rq').textContent === 'second: <b>free</b> & https://example.org/x' && q.box().findAll((e) => e.tagName === 'a' || e.tagName === 'b' || e.tagName === 'script').filter((e) => e.textContent.indexOf('free') !== -1 && cls(e.parent).indexOf('gm-at') === -1 && e.tagName !== 'b').length === 0 && q.env.innerHTMLWrites.length === 0);
    check('...the line that was opened is marked "this line" and nothing else is', one(said(q, 148), 'gm-this').textContent === 'this line' && byClass(q.box(), 'gm-this').length === 1 && cls(said(q, 148)).indexOf('gm-here') !== -1 && byClass(q.box(), 'gm-here').length === 1);
    const qi = rig({ history: [line(147, 'Clover', '', { u: 'clover.4782', img: { n: 'b'.repeat(32), x: 'png' } }), line(148, 'Clover', 'words', { u: 'clover.4782' })] });
    qi.open(148); qi.press('Recent messages from Clover (2)');
    const qr = said(qi, 147), qt = qr && one(qr, 'gm-rq');
    check('...a line that is only an image is listed as one, in its text and in its name for a screen reader', !!qt && qt.textContent.trim() !== '' && (qr.getAttribute('aria-label') || '').indexOf(qt.textContent) !== -1);
    const days = byClass(q.box(), 'gm-day').map((e) => e.textContent), logDays = byClass(q.log, 'gc-day').map((e) => e.textContent);
    check('...day headings only because the lines are on two days: the log\'s own words for them, the newest day first', days.length === 2 && days[0] === logDays[logDays.length - 1] && days[1] === logDays[0] && days[0] !== days[1], JSON.stringify([days, logDays]));
    same('...the order of the group: heading, three lines of the day, heading, the old line', one(q.box(), 'gm-rl').children.map((c) => (cls(c).indexOf('gm-day') !== -1 ? 'day' : c.getAttribute('data-n'))), ['day', '151', '148', '144', 'day', '139']);
    check('...a line is a button named by its day word (only when there are two days), time and words', said(q, 148).getAttribute('aria-label') === days[0] + ' ' + one(said(q, 148), 'gm-rt').textContent + '. second: <b>free</b> & https://example.org/x. this line' && said(q, 151).tagName === 'button' && q.names().length === 5 && q.names()[4] === 'Back');
    check('...the first (newest) line has the focus, every line is at least a button with a chevron', q.doc.activeElement === said(q, 151) && items(q).every((e) => e.findAll((x) => x.tagName === 'svg' && cls(x).indexOf('gm-chev') !== -1).length === 1));
    const one1 = rig({ history: [line(1, 'Fern', 'only line', { k: 'k-f' })] });
    one1.open(1); one1.press('Recent messages from Fern (1)');
    check('a person with one line: the list is that line, "1 message", no day heading, and it is marked as the line that was opened', ids(one1).join() === '1' && one(one1.box(), 'gm-sub').textContent === '1 message in this chat' && byClass(one1.box(), 'gm-day').length === 0 && byClass(one1.box(), 'gm-this').length === 1 && one1.names().join('|') === one1.names()[0] + '|Back' && one1.buttons()[0].getAttribute('aria-label').indexOf('Today') === -1);

    // the keys in the list
    const stopped = (k, extra) => keyOn(q, k, extra);
    check('Down walks to the next line (and stops the page from scrolling), Up comes back', stopped('ArrowDown') === true && q.doc.activeElement === said(q, 148) && stopped('ArrowDown') && q.doc.activeElement === said(q, 144) && stopped('ArrowUp') && q.doc.activeElement === said(q, 148));
    check('...Home and End go to the first and the last, and the ends do not wrap', stopped('End') && q.doc.activeElement === said(q, 139) && stopped('ArrowDown') && q.doc.activeElement === said(q, 139) && stopped('Home') && q.doc.activeElement === said(q, 151) && stopped('ArrowUp') && q.doc.activeElement === said(q, 151));
    check('...a key with Shift, Ctrl, Alt or Meta is not the list\'s', ['shiftKey', 'ctrlKey', 'altKey', 'metaKey'].every((mod) => { q.doc.activeElement = said(q, 151); const s = keyOn(q, 'ArrowDown', { [mod]: true }); return s === false && q.doc.activeElement === said(q, 151); }));
    stopped('ArrowDown'); stopped('ArrowDown');
    check('the list is one stop for Tab: from a line to Back, from Back to the line that was last on, Shift and Tab the other way', (() => { const there = q.doc.activeElement; keyOn(q, 'Tab'); const onBack = q.doc.activeElement === q.named('Back'); keyOn(q, 'Tab'); const again = q.doc.activeElement === there; keyOn(q, 'Tab', { shiftKey: true }); const back2 = q.doc.activeElement === q.named('Back'); keyOn(q, 'Tab', { shiftKey: true }); return onBack && again && back2 && q.doc.activeElement === there && there === said(q, 144); })());
    check('...from Back, Up goes to the last line of the list', (() => { q.named('Back').focus(); const s = keyOn(q, 'ArrowUp'); return s === true && q.doc.activeElement === said(q, 139); })());
    check('...Enter is the button\'s own (a line of the list opens its sheet; nothing of the list takes it)', keyOn(q, 'Enter') === false);
    q.named('Back').focus();
    keyOn(q, 'Escape');
    check('Escape in the list is Back: the line sheet again, with the focus on the recent messages row', q.mod.state() === 'line' && q.doc.activeElement === q.buttons()[0] && rowName(q) === 'Recent messages from Clover (4)' && q.shade() !== null);
    q.press('Recent messages from Clover (4)'); q.press('Back');
    check('the Back button does the same', q.mod.state() === 'line' && q.doc.activeElement === q.buttons()[0]);
    q.press('Recent messages from Clover (4)');
    q.event(q.shade(), 'click', { target: q.shade() });
    q.clock.advance(401);
    q.event(q.shade(), 'click', { target: q.shade() });
    check('the dark part (after the first 400 ms) is Cancel from the list too, with nothing sent', q.shade() === null && q.said().length === 0 && q.doc.activeElement === q.log);

    // a line of the list: its own sheet
    const s = rig({ history: hist() });
    s.open(148); s.press('Recent messages from Clover (4)'); said(s, 144).click();
    check('a line of the list opens the sheet of that line: its words, its number, the line marked in the log, Delete has the focus, and the row is there with the same count', s.mod.state() === 'line' && s.box().findAll((e) => e.id === 'gm-title')[0].textContent === 'Clover' && one(s.box(), 'gm-quote').textContent === 'FREE ant gold!!! see my page' && one(s.box(), 'gm-sub').textContent === '@clover.4782 · line #144' && s.marked().join() === '144' && s.doc.activeElement === s.buttons()[1] && rowName(s) === 'Recent messages from Clover (4)');
    s.press('Kick Clover'); s.clock.advance(401); s.press('Kick');
    same('...its actions are the sheet\'s own: Kick sends the command of THAT line by its number, and the sheet closes after the answer (only a Delete returns to the list)', [s.said(), s.shade() === null || (s.recv({ t: 'ok', c: 'kick' }), s.shade() === null)], [['/kick #144'], true]);

    // Delete from the list: back to the list, the focus on the next line
    for (const [open, target, focusAfter, order, early] of [[148, 148, 144, [151, 144, 139], true], [148, 148, 144, [151, 144, 139], false], [148, 139, 144, [151, 148, 144], true], [148, 151, 148, [148, 144, 139], false]]) {
        const d = rig({ history: hist() });
        d.open(open); d.press('Recent messages from Clover (4)');
        const order0 = ids(d);
        same('(setup) the list before: ' + order0, order0, [151, 148, 144, 139]);
        said(d, target).click();
        red(d, 'Delete this line');
        same('Delete of line ' + target + ' from the list: `/del ' + target + '` is sent', d.said(), ['/del ' + target]);
        if (early) d.recv({ t: 'del', id: target });
        d.recv({ t: 'ok', c: 'del' });
        if (!early) d.recv({ t: 'del', id: target });
        check('...after the answer (' + (early ? 'the line goes first' : 'the ok comes first') + ') the sheet is the list again, without that line, and the focus is on the next line (' + focusAfter + ')', d.mod.state() === 'recent' && ids(d).join() === order.join() && d.doc.activeElement === said(d, focusAfter) && d.shade() !== null, JSON.stringify([d.mod.state(), ids(d), d.doc.activeElement && d.doc.activeElement.getAttribute && d.doc.activeElement.getAttribute('data-n')]));
        check('...the toast and the private line say "Done: the line is gone."', d.toastText() === 'Done: the line is gone.' && d.privates().join() === 'Done: the line is gone.');
        check('...nothing is waiting: the lines take a press again', d.buttons().every((b) => b.getAttribute('aria-disabled') !== 'true'));
        d.press('Back');
        check('...Back goes to the sheet of a line that is still there', d.mod.state() === 'line' && d.shade() !== null && d.toast() !== null && d.toastText() === 'Done: the line is gone.');
    }
    {   // the last line goes: the focus is on Back, and Back closes without a word about a line that is gone
        const d = rig({ history: [line(1, 'Fern', 'only line', { k: 'k-f' })] });
        d.open(1); d.press('Recent messages from Fern (1)'); said(d, 1).click(); red(d, 'Delete this line');
        d.recv({ t: 'del', id: 1 }); d.recv({ t: 'ok', c: 'del' });
        check('the only line is deleted from the list: the list says none is left and the focus is on Back', d.mod.state() === 'recent' && ids(d).length === 0 && /None of their lines/.test(one(d.box(), 'gm-none').textContent) && d.doc.activeElement === d.named('Back') && one(d.box(), 'gm-sub').textContent === '0 messages in this chat');
        d.press('Back');
        check('...Back closes the sheet, and the toast stays the done one (not "that line is gone already")', d.shade() === null && d.toastText() === 'Done: the line is gone.');
    }
    {   // a refusal from the hub: also back to the list, the toast stays with OK
        const d = rig({ history: hist() });
        d.open(148); d.press('Recent messages from Clover (4)'); said(d, 144).click(); red(d, 'Delete this line');
        d.recv({ t: 'err', code: 'power' });
        check('a refused Delete from the list: back to the list with every line still there, and the refusal stays with OK', d.mod.state() === 'recent' && ids(d).join() === '151,148,144,139' && /You cannot do that/.test(d.toastText()) && one(d.toast(), 'gm-tok') !== null);
    }
    {   // Delete of the line that the sheet was OPENED on (not from the list) closes the sheet, as before
        const d = rig({ history: hist() });
        d.open(148); d.press('Recent messages from Clover (4)'); d.press('Back'); red(d, 'Delete this line');
        d.recv({ t: 'ok', c: 'del' });
        check('Delete of the line that was opened (the list only looked at) closes the sheet as it always did', d.shade() === null);
    }

    // live: the count on the row, the list, the log's 100 lines, a role that goes
    const v = rig({ history: hist() });
    v.open(148);
    v.recv(lineOf(152, 'Clover', 'a new line while the sheet is open'));
    check('a new line of the person: the count on the row goes up at once (and its name for a screen reader)', rowName(v) === 'Recent messages from Clover (5)' && v.buttons()[0].textContent.indexOf('(5)') !== -1);
    v.recv(lineOf(153, 'Pebble', 'someone else', { u: undefined }));
    check('...a line of somebody else changes nothing', rowName(v) === 'Recent messages from Clover (5)');
    v.recv({ t: 'del', id: 144 });
    check('a deleted line of the person: the count goes down at once', rowName(v) === 'Recent messages from Clover (4)');
    v.recv({ t: 'del', id: 141 });
    check('...a deleted line of somebody else changes nothing', rowName(v) === 'Recent messages from Clover (4)');
    v.press('Recent messages from Clover (4)');
    same('the list follows: the new line is at the top, the deleted one is gone', ids(v), [152, 151, 148, 139]);
    v.doc.activeElement = said(v, 148);
    v.recv(lineOf(154, 'Clover', 'and one more while the list is open'));
    check('a line that comes while the list is open joins it at the top, the count in the header follows, the focus stays where it was', ids(v).join() === '154,152,151,148,139' && one(v.box(), 'gm-sub').textContent === '@clover.4782 · 5 messages in this chat' && v.doc.activeElement === said(v, 148) && v.mod.state() === 'recent');
    v.recv({ t: 'del', id: 148 });
    check('the line under the focus is deleted by somebody: the focus goes to the next line, and the line that the sheet was opened on is no longer marked', ids(v).join() === '154,152,151,139' && v.doc.activeElement === said(v, 139) && byClass(v.box(), 'gm-this').length === 0 && v.mod.state() === 'recent', JSON.stringify([ids(v), v.doc.activeElement && v.doc.activeElement.getAttribute && v.doc.activeElement.getAttribute('data-n')]));
    v.recv({ t: 'del', id: 139 });
    check('...the last line of the list goes: the focus goes to the one before it', v.doc.activeElement === said(v, 151) && ids(v).join() === '154,152,151');
    v.press('Back');
    check('Back when the line that the sheet was opened on went meanwhile: the sheet closes and says so (nothing to act on)', v.shade() === null && v.toastText() === 'That line is gone already.' && one(v.toast(), 'gm-tok') !== null);
    const w = rig({ history: hist() });
    w.open(148); w.press('Recent messages from Clover (4)');
    for (const id of [151, 148, 144, 139]) w.recv({ t: 'del', id });
    check('all their lines go while the list is open: the list says so, the focus is on Back (the sheet stays until the viewer leaves it)', w.mod.state() === 'recent' && ids(w).length === 0 && w.shade() !== null && w.doc.activeElement !== null && /None of their lines/.test(one(w.box(), 'gm-none').textContent));
    const x = rig({ history: hist() });
    x.open(148);
    for (const id of [151, 144, 139]) x.recv({ t: 'del', id });
    check('the other lines go while the line sheet is open: the count follows down to (1)', rowName(x) === 'Recent messages from Clover (1)');
    const t = rig({ history: hist() });
    t.open(148);
    for (let i = 0; i < 96; i++) t.recv(lineOf(300 + i, 'Pebble', 'filler ' + i));       // (9 + 96 lines: the four oldest, 139, 141, 143 and 144, go)
    check('the log keeps 100 lines: lines of the person that fall out of it fall out of the count (139 and 144 here: the log holds the last 100)', rowName(t) === 'Recent messages from Clover (2)', rowName(t));
    const u = rig({ history: hist() });
    u.open(148); u.press('Recent messages from Clover (4)');
    u.you('none');
    check('a role that goes while the list is open: the sheet closes at once', u.shade() === null && u.marked().length === 0);
    const c = rig({ role: 'mod', history: [line(1, 'Clover', 'was a moderator once', { k: 'k-c', role: 'mod', mod: true }), line(2, 'Clover', 'now a person', { k: 'k-c' })] });
    c.open(2); c.press('Recent messages from Clover (2)');
    check('a line of the person that the viewer may not act on (written when they were a moderator, and the viewer is a moderator): shown, as text, not a button', ids(c).join() === '2,1' && said(c, 1).tagName === 'div' && said(c, 2).tagName === 'button' && c.names().join('|') === c.names()[0] + '|Back');
    c.recv({ t: 'del', id: 2 });
    check('...and a list with only such lines has Back as its first stop', c.doc.activeElement !== null && ids(c).join() === '1');
    const rw = rig({ history: hist() });
    rw.open(148);
    rw.sock().readyState = 3; rw.sock().onclose({}); rw.clock.advance(60000);
    rw.sock().open().recv({ t: 'welcome', v: 1, you: { name: 'Me', mod: true, role: 'admin', k: 'k-me' }, login: false, online: 3, history: hist().filter((h) => h.id !== 144 && h.id !== 139) });
    check('a new welcome (the page was reconnected) with another history: the count follows it', rowName(rw) === 'Recent messages from Clover (2)' && rw.shade() !== null, rowName(rw));
    {   // a reconnect while a sheet is open: it goes when it would show what is not so any more (the line is not in the log, the viewer is not staff or is offered something else)
        const back = (r, role, history) => { r.sock().readyState = 3; r.sock().onclose({}); r.clock.advance(60000); r.sock().open().recv({ t: 'welcome', v: 1, you: { name: 'Me', mod: role !== 'none', role, k: 'k-me' }, login: false, online: 3, history }); };
        const gone = (r) => r.shade() === null && r.marked().length === 0 && !r.host.hasAttribute('inert') && r.doc.activeElement === r.log;
        const a = rig({ history: hist() });
        a.open(148); back(a, 'admin', hist().filter((h) => h.id !== 148));
        check('a reconnect whose history has not the line of an open sheet: the sheet goes (mark and inert too, the focus on the log), and nothing was sent', gone(a) && a.said().length === 0);
        const b = rig({ history: hist() });
        b.open(148); back(b, 'none', hist());
        check('a reconnect that says the viewer has no role now: the sheet goes, and the marks with it', gone(b) && b.acts().length === 0);
        const c = rig({ history: hist() });
        c.open(148);
        check('(setup) an administrator\'s sheet offers Ban', c.names().indexOf('Ban Clover') !== -1);
        back(c, 'mod', hist());
        check('a reconnect that makes the administrator a moderator: the sheet that offers Ban goes, the line is still there', gone(c) && c.row(148) !== undefined);
        c.open(148);
        check('...and the next sheet offers a moderator\'s three actions', c.names().indexOf('Ban Clover') === -1 && c.names().indexOf('Kick Clover') !== -1);
        const d = rig({ history: hist() });
        d.open(148); d.press('Recent messages from Clover (4)');
        back(d, 'admin', hist().filter((h) => h.id !== 148));
        check('the same in the list of recent messages (the line it was opened on is not in the new history)', gone(d));
        const e = rig({ history: hist() });
        e.open(148); back(e, 'admin', hist());
        check('a reconnect that changes nothing for the sheet (same role, the line is there): the sheet stays, with its buttons, and the focus is not moved', e.shade() !== null && e.mod.state() === 'line' && e.names().indexOf('Ban Clover') !== -1 && e.marked().join() === '148');
        const f = rig({ history: hist() });
        f.open(148); f.press('Recent messages from Clover (4)');
        back(f, 'admin', hist().filter((h) => h.id !== 144));
        check('...in the list too (another of the person\'s lines went: the list follows, the sheet stays)', f.shade() !== null && f.mod.state() === 'recent' && ids(f).join() === '151,148,139');
    }
    const busy = rig({ history: hist() });
    busy.open(148); busy.press('Delete this line'); busy.clock.advance(401); busy.press('Delete');
    busy.recv(lineOf(160, 'Clover', 'comes while a command waits'));
    check('a line that comes while a command waits changes nothing on the card (the buttons stay off until the answer)', busy.buttons().every((b) => b.getAttribute('aria-disabled') === 'true') && busy.mod.state() === 'confirm');
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5c. The person sheet of the players list
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const ROWS = [
    { i: 1, name: 'Tess', at: 'look', mod: true, role: 'admin' }, { i: 2, name: 'Sage', at: 'look', mod: true, role: 'mod' }, { i: 3, name: 'Me', at: 'look', mod: true, role: 'admin' },
    { i: 4, name: 'Clover', at: 'wait', pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr' }, { i: 5, name: 'Juniper', at: 'look' }, { i: 6, name: 'Hazel', at: 'play', priv: true },
    { i: 7, name: 'Rowan', at: 'away' }, { i: 9, name: 'Clover', at: 'look' }];       // (two people called Clover: a name is not one person)
const withPeople = (role, o) => { const r = rig(Object.assign({ role, people: true }, o || {})); r.who(ROWS); return r; };
const sortedIds = (list) => list.slice().sort((a, b) => a - b).join();
const statusIn = (root) => root.findAll((e) => cls(e).indexOf('gc-news') !== -1 && e.getAttribute('role') === 'status').pop().textContent;
const statusOf = (r) => statusIn(r.players.root);       // (the list's own words for a screen reader: the chat dock is not shown on a phone's Players view)
const answer = (r, code) => (code === 'ok' ? r.recv({ t: 'ok', c: 'x' }) : r.recv({ t: 'err', code }));
{   // the pure parts
    const P = Pure;
    const person = (o) => Object.assign({ id: 4, who: 'Clover', role: 'none', person: true, where: 'Waiting', own: false, sys: false, text: '' }, o);
    same('a person of the list: an administrator is offered Mute, Kick and Ban (never Delete: there is no line), a moderator no Ban', [P.actionsOf('admin', person({})), P.actionsOf('mod', person({}))], [['mute', 'kick', 'ban'], ['mute', 'kick']]);
    same('...on a moderator: an administrator all three, a moderator nothing; on an administrator: nobody anything', [P.actionsOf('admin', person({ role: 'mod' })), P.actionsOf('mod', person({ role: 'mod' })), P.actionsOf('admin', person({ role: 'admin' })), P.actionsOf('mod', person({ role: 'admin' }))], [['mute', 'kick', 'ban'], [], [], []]);
    same('the commands name the ROW, `row:4`, never the name', [P.commandOf('mute', person({}), 60), P.commandOf('unmute', person({})), P.commandOf('kick', person({})), P.commandOf('ban', person({})), P.commandOf('banpurge', person({}))], ['/mute row:4 60', '/unmute row:4', '/kick row:4', '/ban row:4', '/ban row:4 purge']);
    same('...a person has no line to delete, and the sheet of a line never unmutes', [P.commandOf('del', person({})), P.commandOf('unmute', { id: 148, who: 'Clover' })], ['', '']);
    check('...whatever the person is called', ['mute', 'unmute', 'kick', 'ban', 'banpurge'].every((k) => !/clover/i.test(P.commandOf(k, person({ who: 'Clover' }), 5))));
    same('the words of Unmute: done; a person who is not muted any more; a person who has gone (for every action but a kick)', [P.doneWords('unmute', 'Clover'), P.refusedWords('bad', 'unmute', 'Clover', 'none'), P.refusedWords('who', 'unmute', 'Clover', 'none'), P.refusedWords('who', 'mute', 'Clover', 'none')],
        ['Done: Clover can write again.', 'Clover is not muted any more.', 'That person is not here any more.', 'That person is not here any more.']);
}
{   // the dock's `/mutes`
    const r = rig({ noMod: true }), got = [];
    r.dock.mutes((code, rows) => got.push([code, rows]));
    same('the dock asks `/mutes` as a line of the chat, as a staff member would type it', r.said(), ['/mutes']);
    r.mutes([{ u: 'clover.4782', until: 1700000900, i: 4 }, { u: 'old.name', until: 1700000800 }]);
    same('the hub\'s list answers it: the end of each mute and the row of the person, never a name', got, [['ok', [{ until: 1700000900, i: 4 }, { until: 1700000800, i: -1 }]]]);
    r.mutes([{ until: 5, i: 1 }]);
    check('...once, and the dock adds nothing to the log', got.length === 1 && r.privates().length === 0);
    r.dock.say('/mutes'); r.mutes([{ u: 'clover.4782', until: 1700000900, i: 4 }]);
    check('a `/mutes` that a person typed has nothing to show here: no line, nobody waiting', r.privates().length === 0 && got.length === 1);
    const wrong = [{ t: 'mutes' }, { rows: 'x' }, { rows: new Array(51).fill({ until: 5 }) }, { rows: [null] }, { rows: [{ until: 'soon' }] }, { rows: [{ until: -5 }] }, { rows: [{ until: 5, i: -1 }] }, { rows: [{ until: 5, i: 1.5 }] }, { rows: [{ until: 5, i: 'x' }] }, { rows: [{ until: 1e15 }] }];
    wrong.forEach((m, n) => {
        const w = rig({ noMod: true }), seen = [];
        w.dock.mutes((c) => seen.push(c)); w.recv(Object.assign({ t: 'mutes' }, m));
        const before = seen.length; w.clock.advance(8001);
        same('a list that is wrong in any part is not an answer (' + n + '): nobody is told it, and the wait ends as any other', [before, seen], [0, ['silent']]);
    });
    for (const code of ['power', 'role', 'rate']) { const q = rig({ noMod: true }), seen = []; q.dock.mutes((c) => seen.push(c)); q.recv({ t: 'err', code }); same('the hub\'s `' + code + '` answers it', seen, [code]); }
    const n = rig({ noMod: true, role: 'none' }), nope = []; n.dock.mutes((c) => nope.push(c));
    same('a visitor with no role gets `power` at once, and nothing is sent', [nope, n.said()], [['power'], []]);
    const o = rig({ noMod: true }), off = []; o.sock().readyState = 3; o.sock().onclose({}); o.dock.mutes((c) => off.push(c));
    same('a socket that is down: offline, at once', off, ['offline']);
    const b = rig({ noMod: true }), two = []; b.dock.command('/del 148', () => {}); b.dock.mutes((c) => two.push(c));
    same('one command at a time: `/mutes` while another waits is `rate`', [two, b.said()], [['rate'], ['/del 148']]);
}
{   // the rows, and who gets a mark
    const r = withPeople('admin');
    same('every person of the list but the visitor carries the hub\'s number of the row', sortedIds(r.cards().map((c) => Number(c.getAttribute('data-i')))), '1,2,4,5,6,7,9');
    same('an administrator: a mark on everyone but another administrator (a moderator, a person who looks, waits or plays) and a person who is away (the hub takes a row only for an open socket: every command would be refused)', sortedIds(r.pacts()), '2,4,5,6,9');
    check('...an Away row has a number and no mark; a right click on it is the browser\'s own, and nothing opens', r.card(7) !== undefined && r.card(7).getAttribute('data-act') === null && r.pmenu(7) === false && r.shade() === null && !r.popen(7));
    check('...each marked row has the description for a screen reader, the others none, and the list is described by its hint (one stop, not one per row)', r.card(4).getAttribute('aria-description') !== null && r.card(1).getAttribute('aria-description') === null && r.players.list.getAttribute('aria-describedby') === 'gchat-mod-list-hint' && r.players.list.getAttribute('tabindex') === '0');
    same('a moderator: no mark on a moderator or an administrator', sortedIds(withPeople('mod').pacts()), '4,5,6,9');
    const n = withPeople('none');
    check('a visitor with no role: no mark, no description, the browser\'s own menu, no press, nothing', n.pacts().length === 0 && n.players.list.getAttribute('aria-describedby') === null && !n.pmenu(4) && n.shade() === null && (n.pdown(4), n.clock.advance(700), n.shade() === null && n.pmarked().length === 0));
    r.pkey('ArrowDown');
    check('the keys say which row is chosen', statusOf(r) !== '' && r.players.list.findAll((e) => e.id === 'gchat-mod-list-hint')[0] === undefined && r.players.root.findAll((e) => e.id === 'gchat-mod-list-hint')[0].hidden === false);
    r.you('none');
    check('a `you` that takes the role away: the marks and the description go at once, the words of the keys with them', r.pacts().length === 0 && r.players.list.getAttribute('aria-describedby') === null && statusOf(r) === '' && r.players.root.findAll((e) => e.id === 'gchat-mod-list-hint')[0].hidden === true);
    r.you('mod');
    same('...and one that gives a role brings them back (for that role)', sortedIds(r.pacts()), '4,5,6,9');
    r.who(ROWS.concat([{ i: 11, name: 'Fern', at: 'look' }]));
    check('a list that is drawn again is marked again', sortedIds(r.pacts()) === '4,5,6,9,11');
    const s = withPeople('admin');
    s.mod.people(s.players);
    check('the sheet is told of the list once (a second time changes nothing)', s.dock.root.findAll((e) => e.id === 'gchat-mod-list-hint').length === 0 && s.players.root.findAll((e) => e.id === 'gchat-mod-list-hint').length === 1);
    const q = withPeople('admin', { noLink: true });
    check('until the sheet is told of the list nothing is marked and nothing opens', q.pacts().length === 0 && !q.pmenu(4) && q.shade() === null);
}
{   // the ways in
    const r = withPeople('admin');
    check('a right click on a row that has an action opens the sheet, holds the browser\'s menu back, and marks the row', r.pmenu(4) === true && r.shade() !== null && r.pmarked().join() === '4' && r.marked().length === 0 && r.mod.state() === 'line');
    r.event(r.shade(), 'keydown', { key: 'Escape' });
    check('Escape: the sheet and the mark are gone, and the focus is on the list', r.shade() === null && r.pmarked().length === 0 && r.doc.activeElement === r.players.list);
    check('a right click on an administrator\'s row, or on the visitor\'s own place in the list, is the browser\'s own', r.pmenu(1) === false && r.shade() === null);
    const b = new El(r.env, 'button'); r.card(4).appendChild(b);
    check('a right click on a button of the row (its Join) is the button\'s: the browser\'s menu, no sheet', r.pmenu(b) === false && r.shade() === null);
    r.pdown(b); r.clock.advance(700);
    check('a long press that starts on a button of the row is the button\'s: no mark, no sheet', r.pmarked().length === 0 && r.shade() === null);
    r.pup();
    r.pdown(4); r.clock.advance(200);
    check('a touch held on a row marks it from 150 ms on, and opens nothing yet', r.pmarked().join() === '4' && r.shade() === null);
    r.clock.advance(400);
    check('...at 500 ms the sheet opens, the phone buzzes, the mark stays for the sheet', r.shade() !== null && r.pmarked().join() === '4' && r.buzzes.join() === '10');
    r.press('Cancel');
    check('the finger is still down: the sheet takes no press', r.shade() !== null);
    r.pup(); r.press('Cancel');
    check('...nor for 400 ms after it lifts (the browser\'s click is not an answer)', r.shade() !== null);
    r.clock.advance(401); r.press('Cancel');
    check('...then it does', r.shade() === null && r.pmarked().length === 0);
    r.pdown(4); r.pmove(20, 0); r.clock.advance(700);
    check('a finger that moves more than 10 px is a scroll: no sheet, no mark', r.shade() === null && r.pmarked().length === 0);
    r.pdown(4); r.event(r.players.list, 'scroll', {}); r.clock.advance(700);
    check('...and so is a scroll of the list', r.shade() === null && r.pmarked().length === 0);
    r.pdown(4, { pointerType: 'mouse' }); r.clock.advance(700);
    check('a mouse button held is no long press', r.shade() === null && r.pmarked().length === 0);
    r.pdown(1); r.clock.advance(700);
    check('a long press on an administrator\'s row (no action) does nothing', r.shade() === null && r.pmarked().length === 0);
    r.pkey('ArrowDown');
    same('the keys: Down from nothing chooses the first row that has an action (the list is read from the top)', r.pmarked(), [2]);
    check('...and says so for a screen reader, with how to open it, in the list\'s own words (not in the chat dock, which is hidden on a phone\'s Players view)', /^Sage: Looking\. /.test(statusOf(r)) && /Enter/.test(statusOf(r)) && statusIn(r.dock.root) === '');
    r.pkey('ArrowDown'); r.pkey('ArrowDown'); r.pkey('ArrowUp');
    same('Down and Up walk the rows that have an action, one at a time', r.pmarked(), [9]);
    for (let i = 0; i < 9; i++) r.pkey('ArrowDown');
    same('...and stop at the last (not the Away row: Hazel is the last row that has an action)', r.pmarked(), [6]);
    const chosen = r.pmarked().join();
    check('Up and Down on a button of a row are the button\'s (the arrows scroll the list): not held back, no row is chosen by them', r.event(r.players.list, 'keydown', { key: 'ArrowDown', target: b }) === false && r.event(r.players.list, 'keydown', { key: 'ArrowUp', target: b }) === false && r.pmarked().join() === chosen);
    check('Enter on a button of a row is the button\'s (Join joins): no sheet, the key is not held back', r.event(r.players.list, 'keydown', { key: 'Enter', target: b }) === false && r.shade() === null);
    r.pkey('Enter');
    check('Enter opens the sheet of the chosen row', r.shade() !== null && one(r.box(), 'gm-name').textContent === 'Hazel' && r.pmarked().join() === '6');
    r.event(r.shade(), 'keydown', { key: 'Escape' });
    check('...and when it closes the row stays the chosen one (the keys go on from there)', r.pmarked().join() === '6');
    r.pkey('Escape');
    r.pkey('ArrowUp');
    same('Up from nothing chooses the last row that has an action', r.pmarked(), [6]);
    r.pkey('Escape');
    check('Escape lets the chosen row go', r.pmarked().length === 0);
    r.pkey('ContextMenu');
    check('the menu key with no row chosen opens the first row that has an action', r.shade() !== null && one(r.box(), 'gm-name').textContent === 'Sage');
    r.event(r.shade(), 'keydown', { key: 'Escape' });
    r.pkey('F10', { shiftKey: true });
    check('Shift and F10 does too', r.shade() !== null);
    r.event(r.shade(), 'keydown', { key: 'Escape' });
    r.pkey('ArrowDown'); r.pdown(4);
    check('a press on the list lets the row that the keys chose go (one row is marked at a time)', r.pmarked().length === 0);
}
{   // a link in a line of the log keeps its keys too
    const r = rig(), link = r.row(149).findAll((e) => e.tagName === 'a')[0];
    check('Up and Down on a link of a line are the link\'s: not held back, no line is chosen', !!link && r.event(r.log, 'keydown', { key: 'ArrowUp', target: link }) === false && r.marked().length === 0 && r.event(r.log, 'keydown', { key: 'Enter', target: link }) === false && r.shade() === null);
}
{   // the sheet
    const r = withPeople('admin');
    r.popen(4);
    const box = r.box();
    same('a person who waits: Mute, Kick, Ban, Cancel (no line to delete, no recent messages: a name is not one person), each named by its verb and the person', r.names(), ['Mute Clover…', 'Kick Clover', 'Ban Clover', 'Cancel']);
    same('the header: the person, and where they are in the list\'s own words', [one(box, 'gm-name').textContent, one(box, 'gm-sub').textContent], ['Clover', 'Waiting · Treasure · 2 of 4']);
    check('it is a dialog, modal, labelled by the title and described by where the person is; no quote, no username, no line number', box.getAttribute('role') === 'dialog' && box.getAttribute('aria-modal') === 'true' && box.getAttribute('aria-labelledby') === 'gm-title' && box.getAttribute('aria-describedby') === 'gm-sub' && one(box, 'gm-quote') === null && !/line #|@/.test(box.textContent));
    check('the row is marked while its sheet is open, what stands behind is inert, and the focus is on the first action', r.pmarked().join() === '4' && r.host.hasAttribute('inert') && r.doc.activeElement === r.buttons()[0]);
    same('`/mutes` is asked when it opens, once', r.said(), ['/mutes']);
    const wait = r.buttons()[0];
    same('...until the hub answers, Mute is not offered: the row is off, busy, and says why', [wait.getAttribute('aria-disabled'), wait.getAttribute('aria-busy'), wait.textContent.indexOf('Checking whether Clover is muted') !== -1], ['true', 'true', true]);
    wait.click();
    check('...a press on it does nothing: no second step, nothing sent', r.mod.state() === 'line' && r.said().length === 1);
    r.mutes([{ u: 'juniper.berry', until: 5000, i: 5 }]);
    const free = r.buttons()[0];
    check('the answer (this person is not muted): Mute is there, with its chevron, and the focus stayed on it', free.getAttribute('aria-disabled') === null && free.textContent.indexOf('Cannot write for a while') !== -1 && r.doc.activeElement === free && free.findAll((e) => e.tagName === 'svg' && cls(e).indexOf('gm-chev') !== -1).length === 1 && r.names()[0] === 'Mute Clover…');
    r.press('Mute Clover…'); r.press('1 hour');
    same('Mute, 1 hour: `/mute row:4 60` (the row, not the name), after the `/mutes`', r.said(), ['/mutes', '/mute row:4 60']);
    r.recv({ t: 'ok', c: 'mute' });
    check('the answer: the sheet is gone, the focus is on the list, the toast and the private line say the same words', r.shade() === null && r.doc.activeElement === r.players.list && r.toastText() === 'Done: Clover is muted for 1 hour.' && r.privates().join() === 'Done: Clover is muted for 1 hour.');
    const m = withPeople('admin');
    m.popen(4); m.mutes([{ u: 'clover.4782', until: 5000, i: 4 }]);
    same('a person who is muted: Unmute in place of Mute, with the end of the mute in the log\'s own words', [m.names(), m.buttons()[0].textContent.indexOf('Muted until ' + m.dock.when(5000).time + ' today') !== -1], [['Unmute Clover', 'Kick Clover', 'Ban Clover', 'Cancel'], true]);
    m.press('Unmute Clover');
    same('Unmute asks nothing more (it only gives back): `/unmute row:4`', m.said(), ['/mutes', '/unmute row:4']);
    m.recv({ t: 'ok', c: 'unmute' });
    check('...and the toast says so', m.shade() === null && m.toastText() === 'Done: Clover can write again.');
    const d = withPeople('admin');
    d.popen(4); d.mutes([{ until: 1700000500, i: 4 }]);
    check('a mute that ends on another day says the day', d.buttons()[0].textContent.indexOf('Muted until ' + d.dock.when(1700000500).time + ', ' + d.dock.when(1700000500).day) !== -1);
    const t = withPeople('admin');
    t.mutes([{ until: 5000, i: 9 }]); t.popen(4); t.mutes([{ until: 5000, i: 9 }]);
    check('the mute is matched by the number of the row, not by the name: the other Clover is the one who is muted', t.names()[0] === 'Mute Clover…' && t.buttons()[0].getAttribute('aria-disabled') === null);
    t.press('Cancel'); t.popen(9); t.mutes([{ until: 5000, i: 9 }]);
    check('...and on the other row it is Unmute', t.names()[0] === 'Unmute Clover');
    const fifty = (extra) => Array.from({ length: 50 }, (_, n) => ({ until: 5000 + n, i: 100 + n })).slice(extra ? 1 : 0).concat(extra ? [{ until: 6000, i: 4 }] : []);
    const full = withPeople('admin');
    full.popen(4); full.mutes(fifty(false));
    same('a full list (the hub lists 50 mutes at most, the ones that end soonest) that does not name the person tells nothing about them: "Check again", never Mute', [full.names()[0], full.buttons()[0].getAttribute('aria-disabled')], ['Check again', null]);
    const named = withPeople('admin');
    named.popen(4); named.mutes(fifty(true));
    check('...a full list that names the person is Unmute, as any other', named.names()[0] === 'Unmute Clover');
    const short = withPeople('admin');
    short.popen(4); short.mutes(fifty(false).slice(1));
    check('...49 mutes that do not name the person is Mute (nothing is cut off)', short.names()[0] === 'Mute Clover\u2026' && short.buttons()[0].getAttribute('aria-disabled') === null);
    const f = withPeople('admin');
    f.popen(4); f.recv({ t: 'err', code: 'role' });
    same('a `/mutes` that fails: a row that says so, and asks again when pressed (never Mute before the state is known)', [f.names()[0], f.buttons()[0].textContent.indexOf('Could not tell whether Clover is muted') !== -1, f.buttons()[0].getAttribute('aria-disabled')], ['Check again', true, null]);
    f.press('Check again');
    same('...the second time is a new `/mutes`, and the row waits again', [f.said(), f.names()[0], f.buttons()[0].getAttribute('aria-disabled')], [['/mutes', '/mutes'], 'Mute Clover…', 'true']);
    f.mutes([]);
    check('...and its answer gives Mute', f.buttons()[0].getAttribute('aria-disabled') === null && f.names()[0] === 'Mute Clover…');
    const q = withPeople('admin');
    q.popen(4); q.clock.advance(8001);
    check('no answer in 8 s: the same, "Check again"', q.names()[0] === 'Check again');
    const k = withPeople('admin');
    k.popen(4); k.mutes([]); k.press('Kick Clover');
    check('Kick asks once more: the card names the person (no username: the list has none), and nothing is sent', k.mod.state() === 'confirm' && one(one(k.box(), 'gm-chip'), 'gm-cn').textContent === 'Clover' && k.said().length === 1);
    k.press('Kick');
    check('...the red button takes no press for 400 ms', k.said().length === 1);
    k.clock.advance(401); k.press('Kick');
    same('...then it sends `/kick row:4`', k.said(), ['/mutes', '/kick row:4']);
    k.recv({ t: 'ok', c: 'kick' });
    check('...and the toast says it', k.toastText() === 'Done: Clover was kicked.' && k.shade() === null);
    const a = withPeople('admin');
    a.popen(9); a.mutes([]); a.press('Ban Clover'); a.clock.advance(401); a.press('Ban and delete their lines from the last hour');
    same('Ban and delete their lines: `/ban row:9 purge`, by the number of THAT row (the other Clover keeps their place)', a.said(), ['/mutes', '/ban row:9 purge']);
    const e = withPeople('admin');
    e.popen(4); e.mutes([]); e.press('Kick Clover'); e.clock.advance(401); e.press('Kick'); e.recv({ t: 'err', code: 'who' });
    check('a person who is gone: the hub says `who`, and a kick says what to do instead', e.toastText() === 'Clover is not on the front page now, so there is nobody to kick. Mute or ban instead.' && e.shade() === null);
    const u = withPeople('admin');
    u.popen(4); u.mutes([{ until: 5000, i: 4 }]); u.press('Unmute Clover'); u.recv({ t: 'err', code: 'bad' });
    check('an Unmute of a person who is not muted any more says so', u.toastText() === 'Clover is not muted any more.');
    const w = withPeople('mod');
    w.popen(4);
    same('a moderator: no Ban', w.names(), ['Mute Clover…', 'Kick Clover', 'Cancel']);
    const s = withPeople('admin');
    s.popen(2);
    same('an administrator on a moderator: all three, the header says what the person is, the name has the role\'s color', [s.names().slice(1), one(s.box(), 'gm-sub').textContent, cls(one(s.box(), 'gm-name')).indexOf('gc-r-mod') !== -1], [['Kick Sage', 'Ban Sage', 'Cancel'], 'moderator · Looking', true]);
    const h = withPeople('admin');
    h.popen(6);
    same('a person in a private game: the list\'s own words', one(h.box(), 'gm-sub').textContent, 'Playing · Private game');
}
{   // the sheet of a person does not outlive what it was opened on
    const r = withPeople('admin');
    r.popen(4); r.mutes([]);
    r.who(ROWS.filter((x) => x.i !== 4));
    check('a person who leaves the list while the sheet is open: the sheet goes, the toast says so', r.shade() === null && r.toastText() === 'That person is not here any more.' && r.pmarked().length === 0);
    const b = withPeople('admin');
    b.popen(4); b.mutes([]); b.press('Kick Clover'); b.clock.advance(401); b.press('Kick');
    b.who(ROWS.filter((x) => x.i !== 4));
    check('...not while a command waits (the hub answers it)', b.shade() !== null);
    const g = withPeople('admin');
    g.popen(4); g.mutes([]);
    g.recv({ t: 'who', n: 8, g: 0, more: 0, rows: [], gone: [4] });
    check('...also when only that row is taken off the list', g.shade() === null);
    const l = withPeople('admin', { modLast: true });
    l.popen(4); l.mutes([]);
    l.sock().readyState = 3; l.sock().onclose({});
    check('the numbers of the rows are the hub\'s for one run: a socket that goes closes the sheet of a person, without a word (it is not that the person has gone)', l.shade() === null && l.toastText() === null);
    const c = withPeople('admin');
    c.popen(4); c.mutes([]);
    c.sock().readyState = 3; c.sock().onclose({}); c.clock.advance(60000);
    c.sock().open().recv({ t: 'welcome', v: 1, you: { name: 'Me', mod: true, role: 'admin', k: 'k-me' }, login: false, online: 3, history: HISTORY, presence: true, me: 3 });
    check('...and the welcome that follows never finds it open (the same number may be another person then)', c.shade() === null);
    const rr = withPeople('admin');
    rr.who(ROWS.concat([{ i: 148, name: 'Clover', at: 'look' }])); rr.popen(148);
    check('a row whose number is the number of a line of the log has no recent messages: the lines are found by the key of an account, and a row has none', rr.mod.state() === 'line' && rr.names().every((n) => !/^Recent/.test(n)));
    const tl = withPeople('admin');
    tl.popen(4); tl.mutes([]); tl.tell('live', {});
    check('a welcome tapped to the sheet closes the sheet of a person without a word (a row number is the hub\'s for one run, and a line of that number is no judge of it)', tl.shade() === null && tl.toastText() === null);
    const y = withPeople('admin');
    y.popen(4); y.mutes([]); y.you('none');
    check('a `you` that takes the role away closes it, and the focus is not sent anywhere the viewer should not be', y.shade() === null && y.pacts().length === 0);
    const x = withPeople('admin');
    x.popen(4); x.mutes([]); x.recv({ t: 'del', id: 4 });
    check('a line that goes whose number is the person\'s row number is not the person\'s', x.shade() !== null);
    const v = withPeople('admin');
    v.popen(4); v.mutes([]);
    v.press('Mute Clover…'); v.press('10 min');
    v.press('Back');
    check('while the mute waits for the hub the sheet takes no other press', v.shade() !== null && v.mod.state() === 'mute' && v.said().length === 2);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5d. The invitation: the first row of a person's sheet, for anybody who is in a room (docs/HUB_PEOPLE.md)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Pure;
    same('actionsOf with a room to invite to: the invitation first for anybody, then what the role gives; never on one\'s own line or on the hub\'s', [
        P.actionsOf('none', { id: 4, who: 'Clover', role: 'none' }, true), P.actionsOf('', { id: 4, who: 'Clover', role: 'none' }, false), P.actionsOf('mod', { id: 4, who: 'Clover', role: 'none', person: true }, true), P.actionsOf('admin', { id: 148, who: 'Clover', role: 'none' }, true),
        P.actionsOf('none', { id: 4, who: 'Me', own: true }, true), P.actionsOf('admin', { id: 4, who: 'Me', own: true }, true), P.actionsOf('none', { id: 4, who: 'system', sys: true }, true), P.actionsOf('mod', { id: 4, who: 'Tess', role: 'admin' }, true), P.actionsOf('mod', { id: 4, who: 'Sage', role: 'mod' }, true)], [
        ['invite'], [], ['invite', 'mute', 'kick'], ['invite', 'del', 'mute', 'kick', 'ban'], [], ['del'], [], ['invite'], ['invite']]);
    same('the command names the row of the list or the number of a line, never a name', [P.inviteCommand({ id: 4, who: 'Clover', person: true }), P.inviteCommand({ id: 148, who: 'Clover' })], ['/invite row:4', '/invite #148']);
}
const ROOM2 = { people: 2, size: 4, names: ['Me', 'Wren'], code: 'c1v3rr' };
const ROWS5 = [{ i: 1, name: 'Tess', at: 'look', mod: true, role: 'admin' }, { i: 3, name: 'Me', at: 'look' }, { i: 4, name: 'Wren', at: 'wait' }, { i: 5, name: 'Juniper', at: 'look' }, { i: 6, name: 'Moss', at: 'play' }, { i: 7, name: 'Pebble', at: 'look' }].concat([0, 1, 2, 3, 4, 5, 6].map((n) => ({ i: 20 + n, name: 'Pal' + n, at: 'look' })));
const withRoom = (role, o) => { const r = rig(Object.assign({ role, people: true, room: ROOM2, history: [] }, o || {})); r.who(ROWS5); return r; };
const titles = (r) => r.buttons().map((b) => b.getAttribute('aria-label') || b.textContent);
const chatLine = (r, id, who, text, extra) => r.recv(Object.assign({ t: 'line', id, at: T0 + id, who, text, mod: false, k: 'k-' + who.toLowerCase() }, extra));
{   // a person with no role: the sheet is for the room that is held, on the list and on the name of a line
    const said = [], r = withRoom('none', { say: (words, tone) => { said.push([words, tone]); return true; } });
    chatLine(r, 200, 'Juniper', 'hello there'); chatLine(r, 201, 'Pebble', 'and me'); chatLine(r, 202, 'Ghost', 'gone now');
    check('a person with no role, in a room: the rows of the list can be pressed, and the lines in the log can be pressed at the name only', sortedIds(r.pacts()) === '4,5,6,7,20,21,22,23,24,25,26,1'.split(',').sort((x, y) => x - y).join() && one(r.row(200), 'gc-who').getAttribute('data-act') === 'true' && r.row(200).getAttribute('data-act') === null);
    same('a right click on a row of the list opens the sheet', [r.pmenu(5), r.shade() !== null], [true, true]);
    same('it has the person\'s name, where they are, the one row with the room under it, and Cancel; nothing of moderation', [titles(r), one(r.box(), 'gm-name').textContent, one(r.box(), 'gm-sub').textContent, r.box().findAll((e) => e.tagName === 'small').map((e) => e.textContent)], [
        ['Invite to my room', 'Cancel'], 'Juniper', 'Looking', ['Treasure · 2 of 4 · 2 seats free']]);
    r.press('Invite to my room');
    same('the press sends the hub\'s command by the number of the row, and waits for the answer (the buttons are off)', [r.said(), r.buttons().every((b) => b.getAttribute('aria-disabled') === 'true')], [['/invite row:5'], true]);
    r.recv({ t: 'reply', c: 'invite', r: 'ok', to: 'Juniper' });
    same('the answer: the sheet goes, the page says it in a toast, and the chat has a private line (the same sent words are not a second command)', [r.shade(), said, r.privates()], [null, [['Invitation sent to Juniper.', '']], ['You invited Juniper.']]);
    r.pmenu(5);
    same('the person is dim for the minute that the hub holds a second one back: the row is there and says why, and does nothing', [titles(r)[0], /^Invited \d+ s ago\. Again in \d+ s\.$/.test(r.box().findAll((e) => e.tagName === 'small')[0].textContent), r.buttons()[0].getAttribute('aria-disabled')], ['Invite to my room', true, 'true']);
    r.press('Invite to my room'); r.key('Escape');
    check('a press on it sends nothing', r.said().length === 1);
    r.shade() && r.press('Cancel');
    r.pmenu(4);
    same('a person who is in the room: dim, "In your room"', r.box().findAll((e) => e.tagName === 'small')[0].textContent, 'In your room');
    r.press('Cancel'); r.pmenu(6);
    same('a person in a match: dim, "Playing a match"', r.box().findAll((e) => e.tagName === 'small')[0].textContent, 'Playing a match');
    r.press('Cancel');
    const hint = (x) => x.players.root.findAll((e) => e.id === 'gchat-mod-list-hint')[0];
    same('the words for a screen reader are the invitation\'s for a person with no role, and the moderators\' for staff', [hint(r).textContent, hint(withRoom('mod')).textContent, hint(withRoom('none')).hidden], [Pure.TEXT.inviteList, Pure.TEXT.listHint, false]);
    {   // the room goes to the hub in front of the invitation, as it is now (not as it was a few seconds ago)
        const q = withRoom('none', { room: Object.assign({}, ROOM2) });
        q.room.people = 3; q.players.sync();
        const frames = () => q.sock().sent.map((x) => JSON.parse(x));
        q.pmenu(5); q.press('Invite to my room');
        const all = frames(), at = all.findIndex((m) => m.t === 'say' && m.text === '/invite row:5'), here = all.map((m, i) => (m.t === 'here' && m.inv && m.inv.n === 3 ? i : -1)).filter((i) => i >= 0)[0];
        check('the room as it is now is told to the hub first, then the invitation asks', at > 0 && here !== undefined && here < at, 'here ' + here + ' say ' + at);
    }
    // the answers of the hub that are refusals: the words of the command, in a warning toast, and in the chat
    [[{ r: 'person', to: 'Pebble', wait: 45 }, 'You invited Pebble a moment ago. Try again in 45 seconds.'], [{ r: 'total', wait: 30 }, 'Too many invitations. Wait a minute, then try again.'], [{ r: 'full', n: 4, of: 4 }, 'Your room is full (4 of 4).'],
        [{ r: 'noroom' }, 'You are not in a room. Use Quick host to open one, then invite.'], [{ r: 'nobody' }, 'Nobody here by that name. Type /invite and a space to see who is.'], [{ r: 'busy', to: 'Pebble' }, 'Pebble is playing a match.'], [{ r: 'inroom', to: 'Pebble' }, 'Pebble is in your room already.']].forEach(([reply, words], n) => {
        said.length = 0; r.pmenu(20 + n); r.press('Invite to my room'); r.recv(Object.assign({ t: 'reply', c: 'invite' }, reply));
        same('the hub says ' + reply.r + ': a gold toast with the words, the same words in the chat', [said, r.privates().pop()], [[[words, 'warn']], words]);
    });
    said.length = 0; r.pmenu(7); r.press('Invite to my room'); r.clock.advance(8100);
    same('a hub that says nothing for eight seconds: the words of the sheet\'s own "silent", in a gold toast', said.map((x) => x[1]), ['warn']);
    // a line of the log: the name only
    check('a right click on the text of a line is the browser\'s (Copy stays); on the name it opens the sheet', r.menu(one(r.row(200), 'gc-text')) === false && r.shade() === null && r.menu(one(r.row(200), 'gc-who')) === true && r.shade() !== null);
    same('the sheet of a line: the name, where the person is (from the list, by the name), the invitation, Cancel, and no quote of the line', [titles(r), one(r.box(), 'gm-sub').textContent, byClass(r.box(), 'gm-quote').length], [['Invite to my room', 'Cancel'], 'Looking', 0]);
    r.press('Cancel');
    r.menu(one(r.row(201), 'gc-who')); r.press('Invite to my room');
    same('it asks for the line\'s number, never a name', r.said().pop(), '/invite #201');
    r.recv({ t: 'reply', c: 'invite', r: 'ok', to: 'Pebble' });
    r.menu(one(r.row(202), 'gc-who'));
    check('a person who is not in the list at all (not here now) has the row, dim, with the words', /Ghost is not here now\./.test(r.box().textContent) && r.buttons()[0].getAttribute('aria-disabled') === 'true');
    r.press('Cancel');
    check('the keys: the arrows keep scrolling the list for a person with no role, and the menu key opens the first person', r.pkey('ArrowDown') === false && r.shade() === null && r.pkey('ContextMenu') === true && r.shade() !== null);
    r.press('Cancel');
    // the room goes: the sheet and the marks go with it
    r.who(ROWS5); r.pmenu(5);
    r.room = null; r.players.sync();
    check('the room is left while the sheet is open: it closes, and no row of the list is marked any more', r.shade() === null && r.pacts().length === 0 && one(r.row(200), 'gc-who').getAttribute('data-act') === null && r.pmenu(5) === false && r.shade() === null);
    // a room that is full
    const f = withRoom('none', { room: { people: 4, size: 4, names: ['Me', 'Wren', 'Moss', 'Tam'] } });
    f.pmenu(5);
    same('a room that is full: the row is dim, with how full', f.box().findAll((e) => e.tagName === 'small')[0].textContent, 'Your room is full (4 of 4).');
    // a guest in somebody\'s room
    const g = withRoom('none', { room: { people: 2, size: 4, names: ['Me', 'Wren'], guest: true } });
    g.pmenu(5);
    same('a guest in another\'s room: the host\'s room is named', g.box().findAll((e) => e.tagName === 'small')[0].textContent, 'Wren’s room · Treasure · 2 of 4');
    // a visitor who is not logged in
    const v = withRoom('none', { guest: true });
    check('a visitor who is not logged in has the sheet in a room, the row is dim with the way out, and a press sends nothing', v.pmenu(5) === true && v.box().findAll((e) => e.tagName === 'small')[0].textContent === 'Log in to invite people. Share sends your link.' && (v.press('Invite to my room'), v.said().length === 0 && v.shade() !== null));
    // no room: no sheet at all
    const n = withRoom('none', { room: null });
    check('no room: the browser\'s own menu, no mark, no description', n.pmenu(5) === false && n.shade() === null && n.pacts().length === 0 && n.players.list.getAttribute('aria-describedby') === null);
    // the visitor's own row and own line
    const own = withRoom('none', { history: [] }); chatLine(own, 300, 'Me', 'my own line', { k: 'k-me' });
    check('the visitor\'s own line has no sheet', own.menu(one(own.row(300), 'gc-who')) === false && own.shade() === null);
}
{   // staff: the invitation is the first row, and the moderation is what it was
    const a = withRoom('admin', { history: [line(148, 'Clover', 'FREE ant gold!!! see my page', { u: 'clover.4782' })] });
    a.pmenu(5);
    same('an administrator on a person of the list: the invitation, then Mute, Kick and Ban', titles(a).filter((x) => x !== 'Cancel').map((x) => x.replace(/ Juniper.*/, '')), ['Invite to my room', 'Mute', 'Kick', 'Ban']);
    a.press('Cancel'); a.menu(148);
    same('on a line: the invitation first of the actions, before Delete, Mute, Kick and Ban, with the quote and the number of the line', [titles(a).slice(1, 3), byClass(a.box(), 'gm-quote').length, one(a.box(), 'gm-sub').textContent.indexOf('line #148') !== -1], [['Invite to my room', 'Delete this line'], 1, true]);
    a.press('Cancel');
    const no = withPeople('admin');
    no.pmenu(5);
    check('with no room the sheet is what it was: no invitation', titles(no).indexOf('Invite to my room') === -1);
    const m = withRoom('mod'); m.pmenu(1);
    same('on an administrator, a moderator has only the invitation', titles(m), ['Invite to my room', 'Cancel']);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 6. The page
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const filesOf = (env, name) => env.body.children.filter((c) => (c.tagName === 'script' || c.tagName === 'link') && new RegExp('^home/' + name + '\\.(js|css)$').test(String(c.src || c.href || '')));
function hubFor(env) {
    const sockets = [];
    env.win.WebSocket = function (url) { this.url = url; this.sent = []; this.readyState = 0; this.close = () => { this.readyState = 3; }; this.send = (x) => { if (this.readyState !== 1) throw new Error('not open'); this.sent.push(x); }; sockets.push(this); };
    return sockets;
}
function bring(env, file, name) {
    env.win[name] = H.loadFile('home/' + file + '.js', env.scope)[name];
    filesOf(env, file).forEach((n) => { if (n.onload) n.onload(); });
}
function welcomeWith(sock, role) {
    sock.readyState = 1; if (sock.onopen) sock.onopen({});
    sock.onmessage({ data: JSON.stringify({ t: 'welcome', v: 1, you: role ? { name: 'Maya', mod: role !== 'none', role, k: 'k-maya' } : null, login: !role, online: 2, history: [] }) });
}
{
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    const socks = hubFor(env);
    env.advance(0);
    check('before the dock is there the page asks for nothing of the sheet', filesOf(env, 'mod').length === 0);
    bring(env, 'chat', 'AntsHomeChat');
    env.advance(0);
    check('the dock is there but nobody has said who the visitor is: still nothing', filesOf(env, 'mod').length === 0);
    welcomeWith(socks[0], 'none');
    env.advance(10);
    check('a visitor with no role: the page never asks for the sheet', filesOf(env, 'mod').length === 0);
    socks[0].onmessage({ data: JSON.stringify({ t: 'you', you: { name: 'Maya', mod: true, role: 'mod', k: 'k-maya' } }) });
    env.advance(0);
    same('...a `you` that gives the role: the style and the script of the sheet are asked for, once, async', filesOf(env, 'mod').map((n) => n.tagName + ':' + (n.href || n.src) + ':' + (n.rel || '') + ':' + (n.async === true ? 'async' : '')), ['link:home/mod.css:stylesheet:', 'script:home/mod.js::async']);
    socks[0].onmessage({ data: JSON.stringify({ t: 'you', you: { name: 'Maya', mod: true, role: 'admin', k: 'k-maya' } }) });
    env.advance(10);
    check('...and a second `you` asks for nothing more', filesOf(env, 'mod').length === 2);
    filesOf(env, 'mod')[1].onload();
    check('it mounts only when BOTH files are there (no unstyled sheet)', env.$('home').children.find((c) => c.id === 'gchat').findAll((e) => e.id === 'gchat-mod-hint').length === 0);
    env.win.AntsHomeMod = H.loadFile('home/mod.js', env.scope).AntsHomeMod;
    filesOf(env, 'mod')[0].onload();
    check('...then the hint for the keys is in the dock', env.$('home').children.find((c) => c.id === 'gchat').findAll((e) => e.id === 'gchat-mod-hint').length === 1);
}
{
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    const socks = hubFor(env);
    env.advance(0); bring(env, 'chat', 'AntsHomeChat'); env.advance(0);
    welcomeWith(socks[0], 'admin');
    env.advance(0);
    check('a visitor who is an administrator from the welcome: the page asks for the two files at once', filesOf(env, 'mod').length === 2);
    env.win.AntsHomeMod = { mount() { throw new Error('a sheet that breaks'); } };
    filesOf(env, 'mod').forEach((n) => { if (n.onload) n.onload(); });
    check('a sheet that throws when it mounts: Home goes on (the dock is there, the page still makes a room)', env.$('home').children.find((c) => c.id === 'gchat') !== undefined && (env.makeRoom(), env.screen() === 'room'));
    const none = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    const s2 = hubFor(none); none.advance(0); bring(none, 'chat', 'AntsHomeChat'); none.advance(0); welcomeWith(s2[0], 'mod'); none.advance(0);
    filesOf(none, 'mod').forEach((n) => { if (n.onload) n.onload(); });
    check('a script that is not there when the files "load": nothing mounts and nothing breaks', none.$('home').children.find((c) => c.id === 'gchat') !== undefined);
}

{   // a visitor with no role who holds a room gets the sheet too (it is the invitation's), and nobody else does
    const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    const socks = hubFor(env);
    env.advance(0); bring(env, 'chat', 'AntsHomeChat'); env.advance(0);
    env.win.AntsHomePlayers = { mount() { return { root: new El(env, 'div'), list: new El(env, 'div'), person: () => null, drawn() {}, directory: () => ({}) }; } };
    welcomeWith(socks[0], 'none'); env.advance(0);
    filesOf(env, 'players').forEach((n) => { if (n.onload) n.onload(); });
    env.advance(0);
    check('a visitor with no role who holds no room: no sheet', filesOf(env, 'mod').length === 0);
    env.makeRoom();
    env.arrive({ seat: 0, key: H.KEY, seats: [[H.C, 'Maya'], [H.E, ''], [H.E, ''], [H.E, '']], room: { you: 0, leader: 0 } });
    env.advance(0);
    same('the page holds a room: the style and the script of the sheet are asked for, once', filesOf(env, 'mod').map((n) => n.tagName + ':' + (n.href || n.src)), ['link:home/mod.css', 'script:home/mod.js']);
    env.advance(10); check('...and not again', filesOf(env, 'mod').length === 2);
    const guest = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    const gs = hubFor(guest);
    guest.advance(0); bring(guest, 'chat', 'AntsHomeChat'); guest.advance(0);
    guest.win.AntsHomePlayers = { mount() { return { root: new El(guest, 'div'), list: new El(guest, 'div'), person: () => null, drawn() {}, directory: () => ({}) }; } };
    welcomeWith(gs[0], null); guest.advance(0);
    filesOf(guest, 'players').forEach((n) => { if (n.onload) n.onload(); });
    guest.makeRoom();
    guest.arrive({ seat: 0, key: H.KEY, seats: [[H.C, 'Maya'], [H.E, ''], [H.E, ''], [H.E, '']], room: { you: 0, leader: 0 } });
    guest.advance(0);
    check('a visitor who is not logged in and holds a room has the sheet as well: its row is dim, with the way out', filesOf(guest, 'mod').length === 2);
}
{   // the sheet and the players list find each other, whichever of them comes second
    for (const first of ['the sheet', 'the list', 'a sheet that cannot take the list']) {
        const env = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
        const socks = hubFor(env), told = [];
        env.advance(0); bring(env, 'chat', 'AntsHomeChat'); env.advance(0);
        const list = { root: new El(env, 'div'), list: new El(env, 'div'), person: () => null, drawn() {} };
        env.win.AntsHomeMod = { mount() { return first === 'a sheet that cannot take the list' ? {} : { people: (p) => told.push(p) }; } };
        env.win.AntsHomePlayers = { mount() { return list; } };
        const loaded = (name) => filesOf(env, name).forEach((n) => { if (n.onload) n.onload(); });
        if (first !== 'the sheet') loaded('players');
        welcomeWith(socks[0], 'admin'); env.advance(0);
        loaded('mod');
        if (first === 'the sheet') loaded('players');
        env.advance(0);
        same('the sheet is told of the list once, when ' + first + ' came first (and a sheet that cannot take it changes nothing)', told.length, first === 'a sheet that cannot take the list' ? 0 : 1);
        check('...and it is that list', told.length === 0 || told[0] === list);
    }
    const none = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    const s3 = hubFor(none), told = [];
    none.advance(0); bring(none, 'chat', 'AntsHomeChat'); none.advance(0);
    none.win.AntsHomePlayers = { mount() { return { root: new El(none, 'div'), list: new El(none, 'div'), person: () => null, drawn() {} }; } };
    welcomeWith(s3[0], 'none'); none.advance(0);
    filesOf(none, 'players').forEach((n) => { if (n.onload) n.onload(); });
    check('a visitor with no role: the list comes, no sheet is asked for, nobody is told of anything', filesOf(none, 'mod').length === 0 && told.length === 0);
}

console.log(failures === 0 ? 'OK: ' + checks + ' checks' : failures + ' of ' + checks + ' checks FAILED');
process.exit(failures === 0 ? 0 : 1);
