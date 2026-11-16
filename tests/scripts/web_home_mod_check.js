// Runs the moderation sheet of Home's chat (web/home/mod.js, docs/HOME_SCREEN.md "Roles and commands") against the REAL chat dock (web/home/chat.js, mounted on a fake hub socket) in the fake browser of
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
// o.role ('admin', 'mod', 'none'), o.history, o.size, o.noMod (the sheet is not mounted), o.vibrate (a function, or false for a browser with none)
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
    const dock = ChatApi.mount({ doc, win, host, chip: null, screen: () => 'home', fetch: undefined, now: () => clock.t, random: () => 0, WebSocket: HubSocket, error: (e) => { throw e; } });
    dock.shown(true);
    const mine = (l) => (l.who === 'Me' ? Object.assign({}, l, { role: role === 'none' ? undefined : role, mod: role !== 'none' }) : l);       // (the viewer's own line says the viewer's own role)
    sockets[0].open().recv({ t: 'welcome', v: 1, you: { name: 'Me', mod: role !== 'none', role, k: 'k-me' }, login: false, online: 3, history: (o.history || HISTORY).map(mine) });
    const Mod = loadScript(modSource, 'AntsHomeMod', clock);
    const r = { env, doc, win, body, host, dock, sockets, clock, buzzes, Mod, HubSocket };
    r.sock = () => sockets[sockets.length - 1];
    r.log = dock.log;
    r.rows = () => r.log.children.filter((c) => c.getAttribute && c.getAttribute('data-n') !== null);
    r.row = (id) => r.rows().find((c) => c.getAttribute('data-n') === String(id));
    r.acts = () => r.rows().filter((c) => c.getAttribute('data-act') === 'true').map((c) => Number(c.getAttribute('data-n')));
    r.marked = () => r.rows().filter((c) => cls(c).indexOf('gm-on') !== -1).map((c) => Number(c.getAttribute('data-n')));
    r.mount = () => { r.mod = Mod.mount({ doc, win, dock, now: () => clock.t, error: (e) => { throw e; } }); return r.mod; };
    if (!o.noMod) r.mount();
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
        [{ id: 148, who: 'Clover', text: 'FREE ant gold!!! see my page', role: 'none', sys: false, u: 'clover.4782', own: false }, true, true, null, null]);
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
    same('rows(fn): not told anything for a visitor with no role', none, []);
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
    same('an administrator on a person\'s line: Delete, Mute, Kick, Ban and Cancel, each named by its verb and the person', r.names(), ['Delete this line', 'Mute Clover\u2026', 'Kick Clover', 'Ban Clover', 'Cancel']);
    same('...each action says what it does in a few words (the description)', r.buttons().slice(0, 4).map((b) => r.env.elements && b.getAttribute('aria-describedby')).map((id) => id && one(box, 'gm-act') && box.findAll((e) => e.id === id)[0].textContent), ['Gone from every screen', 'Cannot write for a while', 'Logged out; can log in again', 'Can read, cannot write']);
    check('it is a dialog, modal, labelled by its title and described by the line', box.getAttribute('role') === 'dialog' && box.getAttribute('aria-modal') === 'true' && box.getAttribute('aria-labelledby') === 'gm-title' && box.findAll((e) => e.id === 'gm-title')[0].textContent === 'Clover' && box.getAttribute('aria-describedby') === 'gm-quote' && box.findAll((e) => e.id === 'gm-quote')[0].textContent === 'FREE ant gold!!! see my page');
    same('the header: the author, then the username where the hub sent one and the number of the line', one(box, 'gm-sub').textContent, '@clover.4782 \u00b7 line #148');
    check('the first action has the focus', r.doc.activeElement === r.buttons()[0]);
    check('Mute is the only one with a chevron (it goes on to a second step)', box.findAll((e) => e.tagName === 'svg' && cls(e).indexOf('gm-chev') !== -1).length === 1 && r.buttons()[1].findAll((e) => e.tagName === 'svg' && cls(e).indexOf('gm-chev') !== -1).length === 1);
    check('the line is marked while its sheet is open, and what stands behind is inert (the page, not the sheet)', r.marked().join() === '148' && r.host.hasAttribute('inert') && !r.shade().hasAttribute('inert'));
    r.press('Cancel');
    check('Cancel: the sheet and the mark and the inert are gone, and nothing was sent', r.shade() === null && r.marked().length === 0 && !r.host.hasAttribute('inert') && r.said().length === 0 && r.privates().length === 0);
    r.open(143);
    same('an administrator on a moderator\'s line: all four, and the header says what the author is', [r.names().length, one(r.box(), 'gm-sub').textContent, cls(one(r.box(), 'gm-name')).indexOf('gc-r-mod') !== -1], [5, '@sage.moss \u00b7 moderator \u00b7 line #143', true]);
    r.press('Cancel');
    r.open(150);
    same('on the viewer\'s own line: Delete and Cancel, and the header says "you"', [r.names(), one(r.box(), 'gm-sub').textContent], [['Delete this line', 'Cancel'], '@me.r \u00b7 administrator \u00b7 line #150 \u00b7 you']);
    r.press('Cancel');
    r.open(141);
    check('a line whose author has no username in the frame: no @ in the header', one(r.box(), 'gm-sub').textContent === 'line #141');
    r.press('Cancel');
    const m = rig({ role: 'mod' });
    m.open(148);
    same('a moderator on a person\'s line: no Ban', m.names(), ['Delete this line', 'Mute Clover\u2026', 'Kick Clover', 'Cancel']);
    const l = rig({ history: [line(148, 'Clover', 'x'.repeat(2000), { u: 'clover.4782' })] });
    l.open(148);
    check('a very long line is cut for the quote (the style sheet keeps three lines of it)', one(l.box(), 'gm-quote').textContent.length <= Pure.LIMIT.quote);
    const w = rig({ history: [line(148, '<b>Clover</b>', '<i>hi</i> & <script>x</script>', { u: 'clover.4782' })] });
    w.open(148); w.press('Delete this line');
    check('names and words with markup characters in them are text (nothing is made of them but text)', w.env.innerHTMLWrites.length === 0 && one(w.box(), 'gm-qt').textContent === '<i>hi</i> & <script>x</script>' && w.box().findAll((e) => e.tagName === 'b' || e.tagName === 'i' || e.tagName === 'script').filter((e) => e.textContent.indexOf('Clover') === 0 && e.tagName === 'b' && cls(e.parent).indexOf('gm-at') === -1).length === 0);
}
{   // focus, Escape, Tab, the dark part
    const r = rig();
    r.open(148);
    const first = r.buttons()[0], last = r.buttons()[4];
    check('Tab goes to the next control, and from the last to the first (the sheet keeps the focus)', (() => { first.focus(); const stopped = r.event(r.shade(), 'keydown', { key: 'Tab', shiftKey: false }); const second = r.doc.activeElement === r.buttons()[1]; last.focus(); r.event(r.shade(), 'keydown', { key: 'Tab' }); return stopped && second && r.doc.activeElement === first; })());
    check('Shift and Tab goes back, and from the first to the last', (() => { first.focus(); r.event(r.shade(), 'keydown', { key: 'Tab', shiftKey: true }); return r.doc.activeElement === last; })());
    r.event(r.shade(), 'keydown', { key: 'Escape' });
    check('Escape closes the line sheet, with the focus back on the log', r.shade() === null && r.doc.activeElement === r.log);
    r.open(148); r.press('Mute Clover\u2026');
    check('Mute\u2026 is a second step on the same sheet (the line sheet is replaced, not stacked)', r.mod.state() === 'mute' && r.body.children.filter((c) => cls(c).indexOf('gm-shade') !== -1).length === 1);
    r.event(r.shade(), 'keydown', { key: 'Escape' });
    check('Escape in the second step is Back: the line sheet again, with the focus on Mute\u2026', r.mod.state() === 'line' && r.doc.activeElement === r.buttons()[1] && r.shade() !== null);
    r.press('Mute Clover\u2026'); r.press('Back');
    check('the Back button does the same', r.mod.state() === 'line' && r.doc.activeElement === r.buttons()[1]);
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
    check('a sheet that throws when it mounts: Home goes on (the dock is there, My room still makes the room)', env.$('home').children.find((c) => c.id === 'gchat') !== undefined && (env.home('mine').click(), env.screen() === 'room'));
    const none = runLobby('', { 'ants.name': 'Maya' }, { stayHome: true });
    const s2 = hubFor(none); none.advance(0); bring(none, 'chat', 'AntsHomeChat'); none.advance(0); welcomeWith(s2[0], 'mod'); none.advance(0);
    filesOf(none, 'mod').forEach((n) => { if (n.onload) n.onload(); });
    check('a script that is not there when the files "load": nothing mounts and nothing breaks', none.$('home').children.find((c) => c.id === 'gchat') !== undefined);
}

console.log(failures === 0 ? 'OK: ' + checks + ' checks' : failures + ' of ' + checks + ' checks FAILED');
process.exit(failures === 0 ? 0 : 1);
