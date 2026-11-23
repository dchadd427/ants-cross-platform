// Runs the commands of Home's chat (web/home/cmds.js, docs/HOME_CHAT.md "Roles and commands") with the REAL chat dock (web/home/chat.js) on a fake hub's socket, a fake clock and the fake browser of
// tests/scripts/web_page_harness.js (its DOM). The dock hands the helper its field, its log and the hub's `cmds` and `reply` frames, as home.js does (env.load). What it checks:
//   1. the pure parts: the rows of a `cmds` frame (cleaned, at most 24, one for each word), the narrowing, the bounds of a count and of the spacing, the exact command that the builder writes for each choice (the grammar of
//      docs/HUB_COMMANDS.md), the plain line of what will happen, the words of every `reply` code (and that a word that is no command is answered with the same words whoever asks);
//   2. who is offered what: the rows of each role are what the hub sent and nothing more (nothing of a higher role is ever drawn, and a role that goes takes the rows away at once), a guest, a reader and an empty list
//      download nothing, the files are asked for once, the keys of the helper (Up, Down, Tab, Enter, Escape, a tap), the slash that opens it and the text that closes it, /help and //;
//   3. the builder: the choices and the command each writes into the field (it sends nothing), the bounds of the count and of the spacing in the page, the split that needs three or four bots, the map (the admin page's
//      list, loaded on the first press; a catalog that does not come leaves the rotation; a name that is markup stays text), the number that run at once from the game server;
//   4. the private blocks: under the line that was typed, made of text only (a map name, a person's name and the typed words that are markup stay text), the status card's numbers, a reply that comes before the helper is there,
//      garbage that does not throw.
// tests/scripts/test_web_home_cmds.py runs this with node (the quick tier). usage: node web_home_cmds_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');

const lobbyPath = process.argv[2];
if (!lobbyPath) { console.log('usage: web_home_cmds_check.js lobby.html'); process.exit(2); }
const lobbyDir = path.dirname(path.resolve(lobbyPath));
const chatSource = fs.readFileSync(path.join(lobbyDir, 'home', 'chat.js'), 'utf8');
const Cmds = require(path.join(lobbyDir, 'home', 'cmds.js'));
const H = require('./web_page_harness.js')(lobbyPath);
const { cmdRows } = require('./fake_hub.js');
const { El } = H;

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
const byId = (root, id) => root.findAll((e) => e.id === id)[0] || null;
const button = (root, text) => root.findAll((e) => e.tagName === 'button' && e.textContent === text)[0] || null;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the fakes: a clock, a hub's socket, the page that the dock and the helper are mounted into
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
const QUIET = { DateTimeFormat: function () { return { format: () => '10:54' }; } };
const ME = { name: 'maple.leaf', k: 'aaaaaaaaaaaa' };
const asRole = (role) => Object.assign({}, ME, { role, mod: role !== 'none' });
const W = (o) => Object.assign({ t: 'welcome', v: 1, you: null, login: true, online: 7, history: [] }, o);
const CMDS = (role) => ({ t: 'cmds', rows: cmdRows(role) });

// one dock on a fake page, the helper loaded the way home.js loads it (o.noLoad: no loader at all; o.loadFails: the files never come); o.fetch(url) answers the builder's two calls: { status, json } or throws
function rig(o) {
    o = o || {};
    const env = { elements: {}, innerHTMLWrites: [] };
    const clock = makeClock();
    const chatRoot = {};
    new Function('self', 'setTimeout', 'clearTimeout', chatSource)(chatRoot, clock.setTimeout, clock.clearTimeout);
    const doc = {
        hidden: false, listeners: {}, capturing: {}, activeElement: null, head: new El(env, 'head', ''), body: new El(env, 'body', ''), createElement: (t) => new El(env, t), createTextNode: (t) => ({ isText: true, textContent: String(t), parent: null }),
        addEventListener(t, fn, capture) { const book = capture ? doc.capturing : doc.listeners; (book[t] = book[t] || []).push(fn); }, removeEventListener() {}
    };
    env.doc = doc;
    const fetches = [];
    const sync = (v) => ({ then(ok) { return ok(v); } });
    const win = {
        innerWidth: o.width || 1280, innerHeight: 800, location: { protocol: 'https:', host: 'play.test' }, listeners: {}, Intl: QUIET, addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); },
        fetch: o.fetch ? (url, init) => {
            fetches.push({ url, init });
            let answer;
            try { answer = o.fetch(url); } catch (e) { return { then(ok, bad) { return bad(e); } }; }
            return sync({ status: answer.status, json: () => (answer.notJson ? { then(ok, bad) { return bad(new Error('not json')); } } : sync(answer.json)) });
        } : undefined
    };
    const sockets = [], loads = [];
    class HubSocket {
        constructor(url) { this.url = url; this.sent = []; this.readyState = 0; sockets.push(this); }
        send(text) { if (this.readyState !== 1) throw new Error('send on a socket that is not open'); this.sent.push(text); if (text === '{"t":"ping"}') this.recv({ t: 'online', n: 7 }); }
        close() { this.readyState = 3; }
        open() { this.readyState = 1; if (this.onopen) this.onopen({}); return this; }
        recv(x) { if (this.onmessage) this.onmessage({ data: typeof x === 'string' ? x : JSON.stringify(x) }); return this; }
        fail() { this.readyState = 3; if (this.onerror) this.onerror({}); if (this.onclose) this.onclose({ code: 1006 }); return this; }
    }
    const host = new El(env, 'div', 'home');
    const errors = [];
    const load = o.noLoad ? undefined : (base, done) => { loads.push(base); if (o.loadFails) return; win.AntsHomeCmds = Cmds; clock.setTimeout(done, 0); };
    const dock = chatRoot.AntsHomeChat.mount({ doc, win, host, screen: undefined, now: clock.now, random: () => 0, Intl: QUIET, WebSocket: HubSocket, error: (e) => errors.push(e), load });
    dock.shown(true);
    const r = { env, clock, doc, win, host, sockets, dock, errors, loads, fetches, root: dock.root, HubSocket };
    r.s = () => sockets[sockets.length - 1];
    r.live = (welcome, rows) => { r.s().open().recv(welcome || W({ you: asRole('none') })); if (rows !== false) r.s().recv(rows || CMDS(welcome && welcome.you ? welcome.you.role : 'none')); clock.advance(1); return r.s(); };
    r.field = () => one(dock.root, 'gc-input');
    r.log = () => one(dock.root, 'gc-log');
    r.pop = () => one(dock.root, 'gh-pop');
    r.popRows = () => (r.pop() ? byClass(r.pop(), 'gh-row').map((x) => one(x, 'gh-w').textContent) : null);
    r.selected = () => (r.pop() ? byClass(r.pop(), 'gh-row').filter((x) => x.getAttribute('aria-selected') === 'true').map((x) => one(x, 'gh-w').textContent) : null);
    r.rows = () => r.log().children.slice(1).filter((x) => cls(x).indexOf('gc-day') === -1);
    r.blocks = () => r.rows().filter((x) => cls(x).indexOf('gc-private') !== -1);
    r.answers = () => r.blocks().map((x) => x.children.slice(1).map((c) => c.textContent).join('\n'));
    r.said = () => sockets.reduce((all, s) => all.concat(s.sent.map((x) => JSON.parse(x)).filter((m) => m.t === 'say')), []);
    r.type = (text) => { r.field().value = text; r.field().fire('input', {}); };
    r.key = (k, extra) => { const ev = Object.assign({ key: k, prevented: false, stopped: false, preventDefault() { this.prevented = true; }, stopPropagation() { this.stopped = true; } }, extra); r.field().fire('keydown', ev); return ev; };
    r.submit = (text) => { r.field().value = text; one(dock.root, 'gc-form').fire('submit', { preventDefault() {} }); };
    r.builder = () => byClass(doc.body, 'gb')[0] || null;
    r.at = (id) => byId(r.builder(), id);
    return r;
}
const FETCH = (o) => (url) => {
    o = o || {};
    if (url === '/admin/api/bot-matches/summary') return o.summary || { status: 200, json: { at_once: 80, running: 0, waiting: 0, busy: 0, queues: [] } };
    if (url === '/admin/api/bot-options') return o.options || { status: 200, json: { maps: [{ file: 'TINY.LVL', name: 'TINY' }, { file: 'Bombz Away.lvl', name: 'Bombz Away' }, { file: '<b>x</b>.lvl', name: '<b>x</b>' }], random_max_minutes: 30 } };
    throw new Error('a call that the builder must not make: ' + url);
};
const ALLOWED_TAGS = ['div', 'p', 'span', 'bdi', 'b', 'i', 'mark', 'kbd', 'button', 'code', 'input', 'select', 'option', 'label', 'h3', 'section', 'form', 'textarea', 'a', 'h2'];

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The pure parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const P = Cmds;
    const bad = [{ w: '/Bad', a: '', d: 'x', g: 'g' }, { w: 'help', a: '', d: 'x' }, { w: '/x y', a: '', d: 'x' }, { w: '/ban', a: 'NAME [purge]', d: '<b>x</b> \u202e ban', g: 'Administrators' }, 5, null, 'x', { w: '/help', a: '', d: 'again', g: 'Everyone' },
        { w: '/long', a: 'a'.repeat(500), d: 'd'.repeat(500) }, { w: '/nog', a: 5, d: null, g: '' }, { w: '/help', a: '', d: 'list your commands', g: 'Everyone' }, { w: '/' + 'a'.repeat(40) }];
    const rows = P.rowsOf(bad);
    same('rowsOf: a word is a slash and lower-case letters or digits (at most 16), once; markup and bidirectional controls in the texts are only text; a long argument or description is cut; a row with no group is "Commands"; what is not a row is dropped',
        rows.map((r) => [r.w, r.a.length, r.d.length, r.g, r.d === '<b>x</b> ban' ? 'text' : '']), [['/ban', 12, 12, 'Administrators', 'text'], ['/help', 0, 5, 'Everyone', ''], ['/long', 161, 61, 'Commands', ''], ['/nog', 0, 0, 'Commands', '']]);
    same('rowsOf: not a list is no rows, and a list is read up to 24 rows', [P.rowsOf(null), P.rowsOf('x'), P.rowsOf({}), P.rowsOf(new Array(40).fill(0).map((x, i) => ({ w: '/c' + i, a: '', d: 'd' }))).length], [[], [], [], 24]);
    const all = P.rowsOf(cmdRows('admin'));
    same('narrow: a slash is every row; letters keep the words that start with them, in any case; nothing that matches is no row', [P.narrow(all, '/').length, P.narrow(all, '/b').map((r) => r.w), P.narrow(all, '/BO').map((r) => r.w), P.narrow(all, '/ban').map((r) => r.w), P.narrow(all, '/zzz').length, P.narrow(all, '/o')], [15, ['/ban', '/bans', '/bots'], ['/bots'], ['/ban', '/bans'], 0, []]);
    same('narrow: a word starts with what is typed; a letter inside a word does not count', P.narrow(all, '/o').concat(P.narrow(all, '/ic'), P.narrow(all, '/ots')), []);
    same('countOf: whole numbers from 1 to 10,000, digits only (blanks and commas are dropped); 0, 10,001, six digits, a point, a sign, an exponent, other digits and nothing are refused', ['1', '10000', '10001', '0', '', 'abc', '1e3', '1.5', '-1', '1,000', ' 12 ', '\u0663', '00005', '123456', '\uff11'].map(P.countOf), [1, 10000, -1, -1, -1, -1, -1, -1, -1, 1000, 12, -1, 5, -1, -1]);
    same('everyOf: nothing or 0 is no spacing; whole or half minutes up to 120; 120.5, 121, a quarter, a sign and words are refused; a comma is a point',
        ['', '0', '0.5', '1', '120', '120.5', '121', '-1', '1.25', '1,5', 'abc', ' 3 ', '3.50', '0.05'].map(P.everyOf), [0, 0, 0.5, 1, 120, -1, -1, -1, -1, 1.5, -1, 3, 3.5, -1]);
    const S = (o) => Object.assign(P.startState(), o);
    const cases = [
        [S({ count: 5, every: 3 }), '/bots 5 every 3'], [S({ count: 1, every: 3 }), '/bots 1'], [S({}), '/bots 1'],
        [S({ count: 1000, rules: '187', bots: '4', levels: 'hard', teams: 'free', map: { file: 'Bombz Away.lvl' } }), '/bots 1000 rules 187 bots 4 levels hard teams free map Bombz Away.lvl'],
        [S({ count: 10000, every: 0.5, rules: 'highest', bots: '3', levels: 'any', teams: '1-23' }), '/bots 10000 every 0.5 rules highest bots 3 levels any teams 1-23'],
        [S({ count: 2, bots: 'random', teams: '12-34' }), '/bots 2'], [S({ count: 2, bots: '3', teams: '12-34' }), '/bots 2 bots 3'], [S({ count: 2, bots: '4', teams: '14-23' }), '/bots 2 bots 4 teams 14-23'], [S({ count: 2, bots: '2', teams: 'free' }), '/bots 2 bots 2 teams free'],
        [S({ count: 7, levels: 'mixed', rules: 'random', teams: 'random' }), '/bots 7'], [S({ count: 7, levels: 'easy' }), '/bots 7 levels easy'], [S({ count: 7, levels: 'medium' }), '/bots 7 levels medium'], [S({ count: 3, map: { file: 'Two   Words .lvl' } }), '/bots 3 map Two Words .lvl'],
        [S({ count: 3, map: { file: 'a'.repeat(97) } }), '/bots 3'], [S({ count: 3, map: { file: 'a<b' } }), '/bots 3'], [S({ count: 3, map: { file: 'a'.repeat(96) } }), '/bots 3 map ' + 'a'.repeat(96)]];
    cases.forEach(([s, want]) => same('commandOf ' + JSON.stringify(s), P.commandOf(s), want));
    same('stateOk: a count from 1 to 10,000, a spacing of whole or half minutes from 0 to 120 and a map that can be typed', [S({ count: 0 }), S({ count: 10001 }), S({ count: 10000 }), S({ count: -1 }), S({ count: 5, every: 120.5 }), S({ count: 5, every: 120 }), S({ count: 5, every: -1 }), S({ count: 5, every: 0.25 }), S({ count: 5, map: { file: 'a'.repeat(97) } }), S({ count: 5, map: { file: 'ok.lvl' } })].map(P.stateOk),
        [false, false, true, false, false, true, false, false, false, true]);
    const flat = (lines) => lines.map((l) => (l.bad ? '!' : '') + l.parts.map((p) => (typeof p === 'string' ? p : '<' + p.b + '>')).join(''));
    same('planOf: one match; spaced (the last start, the limit); everything fits at once; too many for the places (the numbers that run and wait, people first); no number from the server; the replay line from four matches on',
        [flat(P.planOf(S({ count: 1 }), 80)), flat(P.planOf(S({ count: 5, every: 3 }), 80)), flat(P.planOf(S({ count: 50 }), 80)), flat(P.planOf(S({ count: 1000 }), 80)), flat(P.planOf(S({ count: 1000 }), 0)), flat(P.planOf(S({ count: 3 }), 80))],
        [['One match starts now.'], ['One starts now, then one every 3 minutes: the last of 5 matches starts in about 12 min.', 'No more than 80 run at once; a start that comes while the server is full waits for room.', 'The server keeps 30 of them an hour as replays; the rest are played but not kept (watch them live while they run).'],
            ['All 50 matches start now, one a second.', 'The server keeps 30 of them an hour as replays; the rest are played but not kept (watch them live while they run).'],
            ['<80> run at once, <920> wait. A new one starts as each one ends, until all 1,000 have played or you type /bots stop.', 'People\u2019s matches always come first: the bots give way when the server is busy.', 'The server keeps 30 of them an hour as replays; the rest are played but not kept (watch them live while they run).'],
            ['The server runs as many as it has room for at once; the rest wait, and a new one starts as each one ends, until all 1,000 have played or you type /bots stop.', 'The server keeps 30 of them an hour as replays; the rest are played but not kept (watch them live while they run).'], ['All 3 matches start now, one a second.']]);
    same('planOf: a count or a spacing that is not allowed is one red line and nothing else', [flat(P.planOf(S({ count: -1 }), 80)), flat(P.planOf(S({ count: 10001 }), 80)), flat(P.planOf(S({ count: 5, every: -1 }), 80)), flat(P.planOf(S({ count: 5, map: { file: '<' } }), 80))],
        [['!Matches: use a number from 1 to 10,000.'], ['!Matches: use a number from 1 to 10,000.'], ['!One every: 0 to 120 minutes, in whole or half minutes.'], ['!That map\u2019s name cannot be typed into the chat (it is too long, or holds a < or >). Choose another map.']]);
    const at = Date.UTC(2026, 10, 20, 18, 42), clk = (d) => d.toISOString().slice(11, 16);
    const R = (m, typed) => P.replyOf(Object.assign({ t: 'reply' }, m), typed || '', at, clk);
    const text = (r) => r.lines.join(' | ');
    same('replyOf started: all of them go; a batch that waits (the first ones go, the rest start as each ends); spaced (the time of the last one); nothing could start yet (why); one match',
        [text(R({ c: 'bots', r: 'started', asked: 5, started: 5, queued: 0, at_once: 80 })), text(R({ c: 'bots', r: 'started', asked: 1000, started: 80, queued: 920, at_once: 80 })), text(R({ c: 'bots', r: 'started', asked: 5, started: 1, queued: 4, every_s: 180, at_once: 80 })),
            text(R({ c: 'bots', r: 'started', asked: 3, started: 0, queued: 3, held: 'cap', at_once: 80 })), text(R({ c: 'bots', r: 'started', asked: 1, started: 1, queued: 0 })), text(R({ c: 'bots', r: 'started', asked: 2, started: 1, queued: 1, every_s: 30 }))],
        ['Started 5 matches.', 'Started 1,000 matches. | The first 80 are going; 920 wait and start as each one ends. | Type /bots status to see how far it is, /bots stop to end it.', 'Started 5 matches. | One is going now; the next four start every 3 minutes (the last one at 18:54).',
            'Queued 3 matches. None has started yet: the limit of 80 bot matches at once is reached. They start as soon as there is room.', 'Started 1 match.', 'Started 2 matches. | One is going now; the other one starts in 30 s (at 18:42).']);
    same('replyOf held: the four words of a limit', ['cap', 'places', 'people', 'busy', 'other'].map((w) => text(R({ c: 'bots', r: 'started', asked: 1, started: 0, queued: 1, held: w, at_once: 40 }))), [
        'Queued 1 match. None has started yet: the limit of 40 bot matches at once is reached. They start as soon as there is room.', 'Queued 1 match. None has started yet: the server has no places left for more bot matches. They start as soon as there is room.',
        'Queued 1 match. None has started yet: people\u2019s matches are using the server. They start as soon as there is room.', 'Queued 1 match. None has started yet: the server is busy. They start as soon as there is room.', 'Queued 1 match. None has started yet: there is no room yet. They start as soon as there is room.']);
    same('replyOf stopped: the matches ended and the starts cancelled, in the words of each; nothing', [R({ c: 'bots', r: 'stopped', stopped: 80, dropped: 920 }), R({ c: 'bots', r: 'stopped', stopped: 1, dropped: 0 }), R({ c: 'bots', r: 'stopped', stopped: 0, dropped: 1 }), R({ c: 'bots', r: 'stopped', stopped: 0, dropped: 0 })].map(text),
        ['Stopped: 80 matches ended and 920 waiting starts cancelled.', 'Stopped: 1 match ended.', 'Stopped: 1 waiting start cancelled.', 'Nothing was running or waiting.']);
    const errs = { usage: 'That is not how /bots is typed. Choose it in the list of commands (type /) to open the box that writes it for you.', bad_count: 'Matches: use a number from 1 to 10,000.', bad_every: 'One every: 0 to 120 minutes, in whole or half minutes.',
        no_map: 'That map cannot be played now. Choose another.', map_187: 'ISLANDS is not played in 187 (nobody could be struck there). Choose another map or the rules Highest score.', no_replays: 'This server keeps no replays, so it plays no bot matches.',
        door: 'The game server did not answer, so nothing was started. Try again in a moment.', off: 'Bot matches are not set up on this chat.', role: 'Could not confirm your role, try again in a moment' };
    Object.keys(errs).forEach((code) => same('replyOf bots ' + code + ' is a refusal in plain words', [text(R({ c: 'bots', r: code })), R({ c: 'bots', r: code }).cls], [errs[code], 'gc-err']));
    same('replyOf bad_map: the name that was typed and up to three close names, as text; with none close, only the name; with no name typed, no name', [text(R({ c: 'bots', r: 'bad_map', close: ['TREASURE.LVL', 'TREASURE 2.lvl', 'Treasure Hunt.lvl', 'Fourth.lvl'] }, '/bots 5 map treasur')), text(R({ c: 'bots', r: 'bad_map', close: [] }, '/bots 5 map treasur')),
        text(R({ c: 'bots', r: 'bad_map', close: ['A.lvl'] }, '/bots 5')), text(R({ c: 'bots', r: 'bad_map', close: ['A.lvl', 'B.lvl'] }, '/bots 5 map <b>x</b>'))],
        ['No map called \u201ctreasur\u201d. Did you mean TREASURE.LVL, TREASURE 2.lvl or Treasure Hunt.lvl?', 'No map called \u201ctreasur\u201d.', 'No map has that name. Did you mean A.lvl?', 'No map called \u201c<b>x</b>\u201d. Did you mean A.lvl or B.lvl?']);
    same('replyOf queue_full and rate: the numbers of the hub, a wait of at least a second', [text(R({ c: 'bots', r: 'queue_full', waiting: 9990, asked: 100, max: 10000 })), text(R({ c: 'bots', r: 'rate', wait: 7 })), text(R({ c: 'bots', r: 'rate', wait: 1 })), text(R({ c: 'ignore', r: 'rate', wait: 0 })), text(R({ c: 'unknown', r: 'rate', wait: 3 })), text(R({ c: 'bots', r: 'rate', wait: 99999 }))],
        ['Not started: 100 more would wait with the 9,990 that wait already, and the server holds 10,000 at the most. Type /bots stop to clear them, or ask for fewer.', 'Too fast: wait 7 seconds before the next /bots.', 'Too fast: wait 1 second before the next /bots.', 'Too fast: wait 1 second before the next /ignore.',
            'Too fast: wait 3 seconds before the next command.', 'Too fast: wait 3600 seconds before the next /bots.']);
    same('replyOf ignore and unignore: the words of each code', ['ok', 'usage', 'nobody', 'ambiguous', 'self', 'staff', 'full', 'not', 'weird'].map((c) => text(R({ c: 'ignore', r: c, n: 3 }))).concat([text(R({ c: 'unignore', r: 'ok', n: 0 })), text(R({ c: 'unignore', r: 'usage' }))]),
        ['Done. You will not see that person\u2019s lines any more. You ignore 3 people now.', 'Type /ignore NAME, for example /ignore pebble.', 'Nobody in the chat has that name.', 'Several people show that name, so it cannot be told which one you mean.', 'You cannot ignore yourself.', 'A moderator or an administrator cannot be ignored.',
            'Your list is full (50 people). Type /unignore NAME to make room.', 'That person is not on your list.', 'That did not work, try again', 'Done. You see that person\u2019s lines again. You ignore nobody now.', 'Type /unignore NAME, for example /unignore pebble.']);
    const same1 = ['bots', 'ignore', 'unignore', 'mute', 'unknown', 'zzz', 'help'].map((c) => text(R({ c, r: 'unknown' })));
    same('replyOf unknown: the same words and the same refusal for every command word and so for every role (a word that is no command and a command of a higher role are answered alike); /help is the helper\'s, a word that is not a reply is nothing',
        [same1.every((t) => t === 'That command is not one that you have. Type /help to see the commands.') && ['bots', 'mute', 'zzz', 'unknown'].every((c) => R({ c, r: 'unknown' }).cls === 'gc-err'), R({ c: 'help', r: 'list' }), P.replyOf({ t: 'reply', c: 'bots', r: 'status', queues: 5 }, '', at, clk).batches.length, R({ c: 'mute', r: 'what' }).cls], [true, { help: true }, 0, 'gc-err']);
    const st = R({ c: 'bots', r: 'status', at_once: 80, running: 80, waiting: 648, busy: 31, queues: [{ total: 1000, remaining: 648, finished: 312, running: 80, kept: 30, every_s: 0, next_in: -1, held: false, by: 'basil' }] });
    same('replyOf status, one batch: the bar (done, running), the numbers, the lines under it', [st.cls, st.lines, st.batches.map((b) => [b.title, b.by, b.done, b.running, b.nums, b.fine])],
        ['gc-status', [], [['Batch of 1,000 matches', 'basil', 31.2, 8, [['312', 'done'], ['80', 'running'], ['648', 'waiting']], ['The next one starts as soon as one ends.', '30 of the 312 that ended are kept as replays (30 an hour); the rest could only be watched live.']]]]);
    const sp = R({ c: 'bots', r: 'status', at_once: 80, running: 1, waiting: 4, busy: 5, queues: [{ total: 5, remaining: 4, finished: 0, running: 1, kept: 0, every_s: 180, next_in: 100, held: false, by: 'basil' }, { total: 20, remaining: 20, finished: 0, running: 0, kept: 0, every_s: 0, next_in: -1, held: 'busy', by: 'sage' }] });
    same('replyOf status, two batches: a first line with the totals; a spaced batch says when the next and the last start; a held batch says why (and the busy percent)', [sp.lines, sp.batches[0].nums.slice(3), sp.batches[0].fine, sp.batches[1].fine],
        [['Bot matches: 1 running, 4 waiting (up to 80 at once).'], [['2 min', 'to the next start'], ['about 11 min', 'to the last start']], [], ['Waiting for room: the server is busy (5%).']]);
    same('replyOf status: the bar never goes past the whole (a batch that says it did more than it holds)', [R({ c: 'bots', r: 'status', running: 1, queues: [{ total: 10, remaining: 0, finished: 30, running: 5 }] }).batches.map((b) => [b.done, b.running])], [[[100, 0]]]);
    same('replyOf status: nothing; running and none waiting; at most five batches; a damaged batch is empty numbers', [text(R({ c: 'bots', r: 'status', running: 0, waiting: 0, queues: [] })), text(R({ c: 'bots', r: 'status', running: 3, waiting: 0, queues: [] })), text(R({ c: 'bots', r: 'status', running: 1, queues: [{}, {}, {}, {}, {}, {}, {}] })).length > 0,
        R({ c: 'bots', r: 'status', running: 1, queues: [{}, {}, {}, {}, {}, {}, {}] }).batches.length, R({ c: 'bots', r: 'status', running: 1, queues: [null, 5, 'x'] }).batches.map((b) => b.nums[0][0])],
        ['No bot match is running and no batch is waiting.', '3 matches are running; no batch is waiting.', true, 5, ['0', '0', '0']]);
    same('replyOf: numbers that are no numbers are nothing (a string, a negative, a fraction, a huge one, NaN)', [text(R({ c: 'bots', r: 'started', asked: '9', started: -1, queued: 1.5, at_once: 1e30 })), text(R({ c: 'bots', r: 'stopped', stopped: NaN, dropped: Infinity }))], ['Started 0 matches.', 'Nothing was running or waiting.']);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. Who is offered what, the keys, the slash
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{   // the files: asked for once, and only for an account that has a command
    const guest = rig(); guest.live(W({ you: null, login: true }), false);
    const reader = rig(); reader.live(W({ you: null, login: false }), false);
    const empty = rig(); empty.live(W({ you: asRole('none') }), { t: 'cmds', rows: [] });
    const junk = rig(); junk.live(W({ you: asRole('none') }), { t: 'cmds', rows: 'x' });
    same('a guest, a reader, an account that is sent no row and one that is sent garbage: the helper is never asked for (nothing is downloaded)', [guest.loads, reader.loads, empty.loads, junk.loads], [[], [], [], []]);
    guest.s().recv(CMDS('none'));
    same('a `cmds` frame to a socket that names nobody (the hub never sends one) is not believed: nothing is asked for and nothing is drawn, the field is off', [guest.loads, guest.field().disabled, guest.pop() === null], [[], true, true]);
    const r = rig(); r.live(W({ you: asRole('none') }));
    same('an account with commands asks for the helper once, by the dock, and a second `cmds` frame (a role that changed) does not ask again', [r.loads, (r.s().recv(CMDS('mod')), r.s().recv(CMDS('none')), r.clock.advance(1), r.loads)], [['home/cmds'], ['home/cmds']]);
    const twice = rig(); twice.live(W({ you: asRole('none') }), false); twice.s().recv(CMDS('none')); twice.s().recv(CMDS('none')); twice.clock.advance(1);
    same('... not even a second frame that comes before the files have', twice.loads, ['home/cmds']);
    const none = rig({ noLoad: true }); none.live(W({ you: asRole('none') }));
    same('a page that has no loader (a dock that is not Home\'s) never has the helper: a slash is only a slash', [none.loads, (none.type('/'), none.pop())], [[], null]);
    const slow = rig({ loadFails: true }); slow.live(W({ you: asRole('none') }));
    slow.type('/'); slow.s().recv(CMDS('none'));
    same('files that never come change nothing: the field works, a command is sent as typed, no error', [slow.pop(), (slow.submit('/ignore pebble'), slow.said().map((m) => m.text)), slow.errors.length], [null, ['/ignore pebble'], 0]);
}
{   // each role's list: what the hub sent, nothing more, and a role that goes takes the rows away
    const ROLES = { none: ['/help', '/invite', '/ignore', '/unignore'], mod: ['/help', '/invite', '/ignore', '/unignore', '/mute', '/unmute', '/del', '/kick', '/mutes'] };
    Object.keys(ROLES).forEach((role) => {
        const r = rig(); r.live(W({ you: asRole(role) })); r.type('/');
        same(role + ': a slash opens the list: every row that the hub sent and no other, in its order, the first one chosen', [r.popRows(), r.selected()], [ROLES[role], ['/help']]);
    });
    const admin = rig(); admin.live(W({ you: asRole('admin') })); admin.type('/');
    same('admin: the groups are the hub\'s own words, one caption each, in order; the count says how many', [byClass(admin.pop(), 'gh-cap').map((c) => c.textContent), one(admin.pop(), 'gh-top').textContent, admin.popRows().length], [['Everyone', 'Moderators', 'Administrators'], 'Commands you can use15 in all', 15]);
    const none = rig(); none.live(W({ you: asRole('none') }));
    ['/b', '/bo', '/ban', '/bots', '/mute', '/del', '/BOTS', '/b '].forEach((t) => { none.type(t); });
    none.type('/bo');
    same('a player is never offered a command of a higher role, whatever is typed: /bo finds nothing, and the list says so', [none.popRows(), one(none.pop(), 'gh-none').textContent], [[], 'No command of yours starts with /bo. Type /help to see them.']);
    none.type('/i');
    same('... and /i finds the player\'s own three', none.popRows(), ['/invite', '/ignore', '/unignore'].filter((w) => w.indexOf('/i') === 0));
    // a role that goes: the hub's `you` and the new `cmds`
    const demoted = rig(); demoted.live(W({ you: asRole('admin') })); demoted.type('/b');
    same('before the demotion the list holds /ban, /bans and /bots', demoted.popRows(), ['/ban', '/bans', '/bots']);
    demoted.s().recv({ t: 'you', you: asRole('none') }); demoted.s().recv(CMDS('none'));
    same('the `cmds` frame of the new role changes the open list at once: nothing of the higher role stays on the screen, and typing on offers none of it', [demoted.popRows(), (demoted.type('/ba'), demoted.popRows())], [[], []]);
    const gone = rig(); gone.live(W({ you: asRole('admin') })); gone.type('/'); gone.s().recv({ t: 'cmds', rows: [] });
    same('a `cmds` frame with no row closes the list', [gone.pop()], [null]);
    const out = rig(); out.live(W({ you: asRole('admin') })); out.type('/');
    out.s().fail(); out.clock.advance(2000); out.s().open().recv(W({ you: null, login: true }));
    same('a new link whose welcome names nobody (a login that ended): the list is gone and no field is on', [out.pop(), out.field().disabled], [null, true]);
}
{   // the keys: each case on a dock of its own
    const open = (role, text) => { const r = rig(); r.live(W({ you: asRole(role) })); r.type(text); return r; };
    const r = open('admin', '/');
    const down = r.key('ArrowDown');
    same('Down chooses the next row (the key is the list\'s: not passed on), Up the one before, never past the ends', [down.prevented, r.selected(), (r.key('ArrowDown'), r.selected()), (r.key('ArrowUp'), r.key('ArrowUp'), r.key('ArrowUp'), r.selected()), r.key('ArrowUp').prevented, (() => { for (let i = 0; i < 40; i++) r.key('ArrowDown'); return r.selected(); })()],
        [true, ['/invite'], ['/ignore'], ['/help'], true, ['/bots']]);
    const t = open('admin', '/'); t.key('ArrowDown'); t.key('ArrowDown');
    const tab = t.key('Tab');
    same('Tab completes the chosen row into the field (a command that takes names gets its space) and the list closes (a field with a space is no slash word)', [tab.prevented, t.field().value, t.pop()], [true, '/ignore ', null]);
    const nm = open('admin', '/b'); nm.key('ArrowDown'); nm.key('Tab');
    same('Tab on a command that takes nothing more writes the word alone, and the list shows it again with its usage chosen', [nm.field().value, nm.popRows(), nm.selected()], ['/bans', ['/bans'], ['/bans']]);
    const e = nm.key('Enter');
    same('Enter on a whole word of a command that takes nothing more is not the list\'s (it chooses nothing): the dock sends the line, once, and the field is empty', [nm.said().map((m) => m.text), nm.field().value, nm.pop()], [['/bans'], '', null]);
    const mu = open('mod', '/mu');
    const enter = mu.key('Enter');
    same('Enter on letters that are the start of a command writes the chosen word, with its space, and sends nothing', [enter.prevented, mu.field().value, mu.said().length, mu.pop()], [true, '/mute ', 0, null]);
    const sel = open('mod', '/m'); sel.key('ArrowDown'); sel.key('ArrowDown');
    same('Down in a narrowed list stays inside it, and Enter takes the chosen one', [sel.popRows(), sel.selected(), (sel.key('Enter'), sel.field().value)], [['/mute', '/mutes'], ['/mutes'], '/mutes']);
    const none = open('mod', '/zzz');
    same('a list with no row has no key of its own except Escape: Tab goes on, and Enter sends the word (a word that is no command is sent, and the hub says so)', [none.popRows(), none.key('Tab').prevented, (none.key('Enter'), none.said().map((m) => m.text))], [[], false, ['/zzz']]);
    const esc = open('admin', '/b');
    const ev = esc.key('Escape');
    same('Escape closes the list and is the list\'s alone (the panel stays open, not folded: the event goes no further); the same text does not open it again', [ev.prevented, ev.stopped, esc.pop(), (esc.type('/b'), esc.pop())], [true, true, null, null]);
    esc.type('/bo');
    same('... another text opens it again, and the next Escape (no list open) is the dock\'s own', [esc.popRows(), (esc.key('Escape'), esc.pop()), esc.key('Escape').stopped], [['/bots'], null, false]);
    const bl = open('admin', '/bo'); bl.field().blur();
    same('the field losing the focus closes the list', [bl.pop()], [null]);
    const tp = open('admin', '/bo'); byClass(tp.pop(), 'gh-row')[0].click();
    same('a tap on a row is a choice: /bots writes its word and opens the builder', [tp.field().value, tp.builder() !== null, tp.pop()], ['/bots ', true, null]);
    const tm = open('mod', '/'); byClass(tm.pop(), 'gh-row')[4].click();
    same('a tap on /mute writes it with a space, and the focus is on the field', [tm.field().value, tm.doc.activeElement === tm.field(), tm.pop()], ['/mute ', true, null]);
    const kb = open('admin', '/'); kb.key('ArrowDown');
    same('the field says which row is chosen (aria-activedescendant) and what it controls; both are gone when the list closes', [kb.field().getAttribute('aria-activedescendant'), kb.field().getAttribute('aria-controls'), (kb.key('Escape'), [kb.field().getAttribute('aria-activedescendant'), kb.field().getAttribute('aria-controls')])],
        ['gchat-cmds-1', 'gchat-cmds', [null, null]]);
    const pd = open('admin', '/'); const press = (el) => { const p = { prevented: false, preventDefault() { this.prevented = true; } }; el.fire('pointerdown', p); return p.prevented; };
    same('a press on the list does not take the focus from the field (a phone\'s keyboard stays up)', press(pd.pop()), true);
}
{   // the slash that opens it, and the text that does not
    const r = rig(); r.live(W({ you: asRole('admin') }));
    ['/', '/b', '/B', '/bots'].forEach((t) => { r.type(t); });
    same('a slash and letters open the list, in any case', (r.type('/B'), r.popRows()), ['/ban', '/bans', '/bots']);
    same('anything else does not: a space after the word, a second slash (a line that starts with a slash), a digit, a letter before the slash, accents, nothing',
        ['/bots 5', '//', '//shrug', '/5', 'x/', 'hello /bo', '/b\u00e9', ''].map((t) => (r.type(t), r.pop() === null)), [true, true, true, true, true, true, true, true]);
    r.type('/b'); r.type('');
    same('the list follows the text: the field emptied closes it', r.pop(), null);
    const ban = rig(); ban.live(W({ you: asRole('none') }));
    ban.s().recv({ t: 'bye', why: 'banned' }); ban.type('/');
    same('a banned person\'s field is off: no list', [ban.field().disabled, ban.pop() === null], [true, true]);
}
{   // /help and //
    const r = rig(); r.live(W({ you: asRole('mod') }));
    r.submit('/help');
    same('/help typed and sent is never sent: the field keeps a slash and the list opens (the same list as the helper)', [r.said().length, r.field().value, r.popRows()], [0, '/', ['/help', '/invite', '/ignore', '/unignore', '/mute', '/unmute', '/del', '/kick', '/mutes']]);
    r.clock.advance(2600); r.type('/HELP me'); r.submit('/HELP me');
    same('in any case, and with words after it', [r.said().length, r.field().value], [0, '/']);
    r.clock.advance(2600); r.submit('//shrug');
    same('a line with two slashes is sent as typed and is not a command: the field is empty, no list', [r.said().map((m) => m.text), r.field().value, r.pop()], [['//shrug'], '', null]);
    r.s().recv({ t: 'reply', c: 'help', r: 'list' });
    same('a hub that answers a /help anyway (`reply help list`) opens the list: the page never prints a list in the log', [r.popRows().length, r.field().value, r.blocks().length], [9, '/', 0]);
    const q = rig(); q.live(W({ you: asRole('none') })); q.clock.advance(2600); q.submit('/ignore pebble');
    same('another command goes to the hub as typed, once, and the field is empty (no list)', [q.said().map((m) => m.text), q.field().value, q.pop()], [['/ignore pebble'], '', null]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. The builder
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function builderOf(o) {
    o = o || {};
    const r = rig({ fetch: o.fetch || FETCH(), width: o.width });
    r.live(W({ you: asRole('admin') }));
    r.type('/bo'); r.key('Enter');
    return r;
}
const num = (r, v) => { const i = r.at('gb-n'); i.value = String(v); i.fire('input', {}); };
const every = (r, v) => { const i = r.at('gb-e'); i.value = String(v); i.fire('input', {}); };
const seg = (r, label, to) => { const b = r.builder().findAll((e) => e.tagName === 'button' && e.parent && cls(e.parent).indexOf('gb-seg') !== -1 && e.textContent === to)[0]; b.click(); };
const pick = (r, id, v) => { const s = r.at(id); s.value = v; s.fire('change', {}); };
const cmd = (r) => one(r.builder(), 'gb-cmd').textContent;
const plan = (r) => byClass(r.builder(), 'gb-plan')[0].children.map((p) => p.textContent);
const write = (r) => button(r.builder(), 'Write the command');
{   // the way in
    const r = builderOf();
    same('Enter on /bo (the whole list narrowed to /bots) opens the builder and writes /bots with a space; nothing is sent; the number that run at once is asked of the game server, and not the list of maps', [r.builder() !== null, r.field().value, r.said().length, r.fetches.map((f) => f.url)], [true, '/bots ', 0, ['/admin/api/bot-matches/summary']]);
    same('the builder is a dialog with a name, on the page (not in the dock), with the first choices of the picture: one match, no spacing, the rotation, a coin, 2 to 4 bots, Medium or Hard, a draw', [r.builder().getAttribute('role'), r.builder().getAttribute('aria-modal'), cmd(r), one(r.builder(), 'gb-map').children[0].textContent,
        byClass(r.builder(), 'gb-seg').map((s) => s.children.filter((b) => b.getAttribute('aria-pressed') === 'true').map((b) => b.textContent).join()), r.at('gb-levels').value, r.at('gb-teams').value, r.at('gb-n').value, r.at('gb-e').value],
        ['dialog', 'true', '/bots 1', 'The rotation', ['A coin', '2 to 4'], 'mixed', 'random', '1', '0']);
    same('the number that run at once is the game server\'s (the first line says it when many are asked for)', (num(r, 1000), plan(r)[0]), '80 run at once, 920 wait. A new one starts as each one ends, until all 1,000 have played or you type /bots stop.');
    r.doc.capturing.keydown.forEach((fn) => fn({ key: 'Escape' }));
    same('Escape closes the builder, and the field keeps what it held', [r.builder(), r.field().value], [null, '/bots ']);
    const c = builderOf();
    button(c.builder(), 'Cancel').click();
    same('Cancel closes it too; so does the cross, and the dark part', [c.builder(), (c.type('/bo'), c.key('Enter'), c.builder() !== null), (c.builder().findAll((e) => e.tagName === 'button' && e.getAttribute('aria-label') === 'Close')[0].click(), c.builder()), (c.type('/bo'), c.key('Enter'), byClass(c.doc.body, 'gb-back')[0].click(), c.builder())], [null, true, null, null]);
    const down = builderOf({ fetch: () => FETCH({ summary: { status: 503, json: {} } })('/admin/api/bot-matches/summary') });
    same('with no number from the game server the plan still speaks (without one), and the builder still writes', [(num(down, 1000), plan(down)[0]).slice(0, 40), cmd(down)], ['The server runs as many as it has room f', '/bots 1000']);
}
{   // each choice and the command it writes; the field is written, nothing is sent
    const r = builderOf();
    num(r, 1000); seg(r, 'Rules', '187'); seg(r, 'Bots', '4'); pick(r, 'gb-levels', 'hard'); pick(r, 'gb-teams', 'free');
    same('1,000 matches, rules 187, four bots, Hard, free for all: the exact line (the same words as the approved picture, without a map)', cmd(r), '/bots 1000 rules 187 bots 4 levels hard teams free');
    write(r).click();
    same('Write the command puts that line in the field, closes the builder, and sends nothing (Enter sends)', [r.field().value, r.builder(), r.said().length, r.doc.activeElement === r.field()], ['/bots 1000 rules 187 bots 4 levels hard teams free', null, 0, true]);
    r.submit(r.field().value);
    same('Enter sends it as typed', r.said().map((m) => m.text), ['/bots 1000 rules 187 bots 4 levels hard teams free']);
    const p = builderOf();
    every(p, '3'); num(p, 5);
    same('5 matches every 3 minutes', cmd(p), '/bots 5 every 3');
    every(p, '0.5'); same('half a minute', cmd(p), '/bots 5 every 0.5');
    num(p, 1); same('one match has no spacing to write', cmd(p), '/bots 1');
    num(p, 10); [1, 10, 100, 1000].forEach((n, i) => { byClass(p.builder(), 'gb-presets')[0].children[i].click(); });
    same('the four presets set the count (the last one is 1,000, shown as pressed, and the box says it)', [p.at('gb-n').value, byClass(p.builder(), 'gb-presets')[0].children.map((b) => b.getAttribute('aria-pressed'))], ['1000', ['false', 'false', 'false', 'true']]);
    pick(p, 'gb-levels', 'easy'); pick(p, 'gb-levels', 'any'); pick(p, 'gb-levels', 'medium'); pick(p, 'gb-levels', 'mixed');
    seg(p, 'Rules', 'Highest score'); seg(p, 'Rules', 'A coin');
    same('a choice that is put back to the open one leaves the line as it was', cmd(p), '/bots 1000 every 0.5');
}
{   // the split needs three or four bots
    const r = builderOf();
    num(r, 4);
    same('with 2 to 4 bots the teams offered are the draw and free for all', r.at('gb-teams').children.map((o) => o.value), ['random', 'free']);
    seg(r, 'Bots', '4');
    same('with four bots: the draw, free for all and the three splits of four', r.at('gb-teams').children.map((o) => o.value), ['random', 'free', '12-34', '13-24', '14-23']);
    pick(r, 'gb-teams', '13-24');
    same('a split of four, with four bots', cmd(r), '/bots 4 bots 4 teams 13-24');
    seg(r, 'Bots', '3');
    same('three bots: the splits of three, and the split of four is the draw again (the line has none)', [r.at('gb-teams').children.map((o) => o.value), cmd(r)], [['random', 'free', '1-23', '2-13', '3-12'], '/bots 4 bots 3']);
    pick(r, 'gb-teams', '2-13'); seg(r, 'Bots', '2');
    same('two bots: no split, free for all stays possible', [r.at('gb-teams').children.map((o) => o.value), cmd(r)], [['random', 'free'], '/bots 4 bots 2']);
    pick(r, 'gb-teams', 'free'); seg(r, 'Bots', '4');
    same('free for all stays free for all when the bots change', cmd(r), '/bots 4 bots 4 teams free');
}
{   // the bounds in the page
    const r = builderOf();
    const state = (v) => { num(r, v); return [cmd(r), plan(r).length === 1 && cls(byClass(r.builder(), 'gb-plan')[0].children[0]).indexOf('gb-bad') !== -1, write(r).disabled === true]; };
    same('a count of 0, 10,001, six digits, text, a fraction and nothing: a red line, the Write button is off', ['0', '10001', '100000', 'many', '1.5', '', '-3', '1e3'].map((v) => { const s = state(v); return [s[1], s[2]]; }), new Array(8).fill([true, true]));
    same('1 and 10,000 are fine, and so are blanks and a comma ("1,000")', ['1', '10000', ' 1,000 '].map((v) => { const s = state(v); return [s[1], s[2]]; }), new Array(3).fill([false, false]));
    num(r, 5);
    same('a spacing of 120.5, 121, a quarter, a sign or text: the red line of the spacing, no Write; 0 to 120 in halves is fine', ['120.5', '121', '2.25', '-1', 'x', '0', '120', '0.5'].map((v) => { every(r, v); return [byClass(r.builder(), 'gb-bad').length === 1, write(r).disabled === true]; }), [[true, true], [true, true], [true, true], [true, true], [true, true], [false, false], [false, false], [false, false]]);
    every(r, '121'); write(r).click();
    same('a Write button that is off writes nothing (even when pressed)', [r.builder() !== null, r.field().value], [true, '/bots ']);
    num(r, 10001); every(r, '1');
    same('the count is the first line that is told when both are wrong', plan(r), ['Matches: use a number from 1 to 10,000.']);
    num(r, 10000);
    same('the largest count the page writes is 10,000', (every(r, '0'), cmd(r)), '/bots 10000');
}
{   // the map: the admin page's list, loaded on the first press
    const opened = [];
    const r = rig({ fetch: FETCH() }); r.win.AntsAdminMapPick = { make: (h) => { opened.push(['make', Object.keys(h).sort().join()]); return { open: (o) => { opened.push(['open', o]); }, close() { opened.push(['close']); } }; } };
    r.live(W({ you: asRole('admin') })); r.type('/bo'); r.key('Enter');
    one(r.builder(), 'gb-map').click();
    const o = opened.filter((x) => x[0] === 'open')[0][1];
    same('Map asks the game server for its maps (a GET with the cookie, no body) and opens the admin page\'s list over the page, to choose one map, with the one random pick that a command can say', [r.fetches.map((f) => f.url), r.fetches.map((f) => [f.init.credentials, f.init.method === undefined, f.init.body === undefined]), o.mode, o.title, o.maps().map((m) => m.file), o.randoms().map((x) => x[0]), o.current],
        [['/admin/api/bot-matches/summary', '/admin/api/bot-options'], [['same-origin', true, true], ['same-origin', true, true]], 'choose', 'Choose a map', ['TINY.LVL', 'Bombz Away.lvl', '<b>x</b>.lvl'], ['random:rotation'], 'random:rotation']);
    check('the list is given the helpers that the admin page has (a maker of elements, a button, text from outside, the box of a question and the inert page behind it)', opened[0][0] === 'make' && opened[0][1] === 'add,button,clearInert,doc,el,inertOthers,questionBox,typed', opened[0][1]);
    o.onPick({ file: 'Bombz Away.lvl', name: 'Bombz Away', random: true });
    same('a map that is chosen is the batch\'s map (its file name is what is typed; a map that may be drawn at random is still a named one), and the list closes', [one(r.builder(), 'gb-map').children[0].textContent, cmd(r), opened.filter((x) => x[0] === 'close').length], ['Bombz Away', '/bots 1 map Bombz Away.lvl', 1]);
    o.onPick({ file: 'random:rotation', random: true, name: 'x' });
    same('the random pick of the rotation leaves the rotation (no map in the line)', [one(r.builder(), 'gb-map').children[0].textContent, cmd(r)], ['The rotation', '/bots 1']);
    o.onPick({ file: '<b>x</b>.lvl', name: '<b>x</b>' });
    same('a map whose name is markup stays text: it is shown as it is, it cannot be typed (a < in a line is refused by the chat), so the line has no map and says why, and Write is off', [one(r.builder(), 'gb-map').children[0].textContent, cmd(r), plan(r), write(r).disabled, r.env.innerHTMLWrites.length],
        ['<b>x</b>', '/bots 1', ['That map\u2019s name cannot be typed into the chat (it is too long, or holds a < or >). Choose another map.'], true, 0]);
    o.onClose && o.onClose();
    const fail = rig({ fetch: FETCH({ options: { status: 403, json: { error: 'not_allowed' } } }) }); fail.win.AntsAdminMapPick = { make: () => ({ open() { throw new Error('the list must not open'); }, close() {} }) };
    fail.live(W({ you: asRole('admin') })); fail.type('/bo'); fail.key('Enter'); one(fail.builder(), 'gb-map').click();
    same('a catalog that does not come (a refusal, garbage, a network that fails) leaves the rotation and says so; the list is not opened', [one(fail.builder(), 'gb-hint') ? 1 : 0, fail.builder().textContent.indexOf('The list of maps did not come') !== -1, cmd(fail), fail.errors.length], [1, true, '/bots 1', 0]);
    const bad = rig({ fetch: FETCH({ options: { status: 200, notJson: true } }) }); bad.live(W({ you: asRole('admin') })); bad.type('/bo'); bad.key('Enter'); one(bad.builder(), 'gb-map').click();
    const down = rig({ fetch: (u) => { if (u.indexOf('options') !== -1) throw new Error('down'); return FETCH()(u); } }); down.live(W({ you: asRole('admin') })); down.type('/bo'); down.key('Enter'); one(down.builder(), 'gb-map').click();
    same('... so for a body that is not JSON and a network that is down', [bad.builder().textContent.indexOf('The list of maps did not come') !== -1, down.builder().textContent.indexOf('The list of maps did not come') !== -1, bad.errors.length + down.errors.length], [true, true, 0]);
    const lazy = rig({ fetch: FETCH() }); lazy.live(W({ you: asRole('admin') })); lazy.type('/bo'); lazy.key('Enter'); one(lazy.builder(), 'gb-map').click();
    const heads = lazy.doc.head.children.map((e) => [e.tagName, e.src || e.href, e.rel || '']);
    same('the admin page\'s list is loaded on the first press of Map and not before (its script and its style, from the site\'s own root)', [lazy.loads, heads], [['home/cmds'], [['link', '/admin.css', 'stylesheet'], ['script', '/admin_map_pick.js', '']]]);
    let made = 0; lazy.win.AntsAdminMapPick = { make: () => { made++; return { open() { made += 10; }, close() {} }; } };
    lazy.doc.head.children.filter((e) => e.tagName === 'script')[0].onload();
    same('when the script has come the list opens; a script that does not come says that the list did not come', [made, (() => { const x = rig({ fetch: FETCH() }); x.live(W({ you: asRole('admin') })); x.type('/bo'); x.key('Enter'); one(x.builder(), 'gb-map').click(); x.doc.head.children.filter((e) => e.tagName === 'script')[0].onerror(); return x.builder().textContent.indexOf('The list of maps did not come') !== -1; })()], [11, true]);
    const twice = rig({ fetch: FETCH() }); twice.win.AntsAdminMapPick = { make: () => ({ open() {}, close() {} }) };
    twice.live(W({ you: asRole('admin') })); twice.type('/bo'); twice.key('Enter');
    one(twice.builder(), 'gb-map').click(); one(twice.builder(), 'gb-map').click();
    same('a second press of Map while the list is open (or on its way) is the same press: the catalog is asked for once', twice.fetches.filter((f) => f.url === '/admin/api/bot-options').length, 1);
    twice.doc.capturing.keydown.forEach((fn) => fn({ key: 'Escape' }));
    same('while the admin page\'s list is open the keys are the list\'s: Escape does not close the builder under it', twice.builder() !== null, true);
    const again = builderOf(); num(again, 'abc'); again.doc.capturing.keydown.forEach((fn) => fn({ key: 'Escape' })); again.type('/'); again.type('/bo'); again.key('Enter');
    same('a builder that is opened again after a count that was no number holds one match, not the refusal', [again.at('gb-n').value, cmd(again)], ['1', '/bots 1']);
    const player = rig({ fetch: FETCH() }); player.live(W({ you: asRole('mod') })); player.type('/b');
    same('only an account whose rows hold /bots reaches the builder: a moderator\'s list has no /bots, and nothing opens it or asks the game server anything', [player.popRows(), player.builder(), player.fetches.length], [[], null, 0]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. The private blocks
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const send = (r, text, frame) => { r.clock.advance(2600); r.submit(text); r.s().recv(frame); return r; };
    const r = rig(); r.live(W({ you: asRole('admin') }));
    send(r, '/bots 1000', { t: 'reply', c: 'bots', r: 'started', asked: 1000, started: 80, queued: 920, at_once: 80 });
    same('the answer is a private block of the log ("Only you") under the line that was typed, in its own words; nothing is posted and the command is answered once', [r.answers(), one(r.blocks()[0], 'gc-tag').textContent, r.said().length, r.rows().length],
        [['/bots 1000\nStarted 1,000 matches.\nThe first 80 are going; 920 wait and start as each one ends.\nType /bots status to see how far it is, /bots stop to end it.'], 'Only you', 1, 1]);
    r.s().recv({ t: 'reply', c: 'bots', r: 'bad_count' });
    same('a reply that no command waits for (the wait is over, or another tab asked) is shown without the echo', r.answers().slice(1), ['Matches: use a number from 1 to 10,000.']);
    same('a refusal has its own class (a red edge); an answer has none', [cls(r.blocks()[1]).indexOf('gc-err') !== -1, cls(r.blocks()[0]).indexOf('gc-err') !== -1], [true, false]);
    send(r, '/bots status', { t: 'reply', c: 'bots', r: 'status', at_once: 80, running: 80, waiting: 648, busy: 31, queues: [{ total: 1000, remaining: 648, finished: 312, running: 80, kept: 30, every_s: 0, next_in: -1, held: false, by: 'basil' }] });
    const card = r.blocks()[2];
    same('the status card: the batch\'s name (no echo of the command: the card says what it is), the bar with the numbers of the picture, three numbers and the lines', [byClass(card, 'gc-echo').length, cls(card).indexOf('gc-status') !== -1, byClass(card, 'gc-pl').map((x) => x.textContent), one(card, 'gc-bar').children.map((i) => [i.style.width, i.style.left || '']),
        one(card, 'gc-bar').getAttribute('role'), one(card, 'gc-bar').getAttribute('aria-valuenow'), byClass(card, 'gc-nums')[0].children.map((x) => x.textContent), byClass(card, 'gc-fine').map((x) => x.textContent)],
        [0, true, ['Batch of 1,000 matches by basil'], [['31.2%', ''], ['8%', '31.2%']], 'progressbar', '31', ['312 done', '80 running', '648 waiting'], ['The next one starts as soon as one ends.', '30 of the 312 that ended are kept as replays (30 an hour); the rest could only be watched live.']]);
    send(r, '/bots stop', { t: 'reply', c: 'bots', r: 'stopped', stopped: 80, dropped: 568 });
    same('stop: what ended and what was cancelled', r.answers()[3], '/bots stop\nStopped: 80 matches ended and 568 waiting starts cancelled.');
    send(r, '/bots 5 map treasur', { t: 'reply', c: 'bots', r: 'bad_map', close: ['TREASURE.LVL', '<img src=x onerror=alert(1)>'] });
    same('an unknown map: the typed name and the close names, as text', r.answers()[4], '/bots 5 map treasur\nNo map called \u201ctreasur\u201d. Did you mean TREASURE.LVL or <img src=x onerror=alert(1)>?');
    send(r, '/bots 5 <b>x</b>', { t: 'reply', c: 'bots', r: 'usage' });
    send(r, '/bots 5 map ' + 'x'.repeat(300), { t: 'reply', c: 'bots', r: 'no_map' });
    same('a typed line that is very long is echoed cut (200 characters and a mark), as text', [one(r.blocks()[r.blocks().length - 1], 'gc-echo').textContent.length, one(r.blocks()[r.blocks().length - 1], 'gc-echo').textContent.slice(-1)], [201, '\u2026']);
    check('nothing in any block is made from markup: no element but the plain ones, and not one write of innerHTML, whatever the names and the typed words hold', r.env.innerHTMLWrites.length === 0 && r.log().all().every((e) => ALLOWED_TAGS.indexOf(e.tagName) !== -1 || e.isText), r.env.innerHTMLWrites.join('|'));
    check('the echo and the names are in a <bdi> (a name may run the other way)', byClass(r.blocks()[4], 'gc-echo').every((e) => e.children.length === 1 && e.children[0].tagName === 'bdi') && one(card, 'gc-pl').textContent !== '' && byClass(card, 'gc-pl')[0].children.some((c) => c.tagName === 'bdi'));
    same('the count of blocks is the dock\'s: they are lines of the log like the others (100 at most)', (() => { const x = rig(); x.live(W({ you: asRole('admin') })); for (let i = 0; i < 130; i++) { x.clock.advance(2600); x.submit('/bots 1'); x.s().recv({ t: 'reply', c: 'bots', r: 'started', asked: 1, started: 1, queued: 0 }); } return [x.rows().length, x.errors.length]; })(), [100, 0]);
    const unk = (role) => { const x = rig(); x.live(W({ you: asRole(role) })); send(x, '/bots 5', { t: 'reply', c: 'unknown', r: 'unknown' }); return x.answers()[0]; };
    same('a command that the account has not (a higher role\'s, or a word that is none) is answered with the same words and the same echo for every role', ['none', 'mod', 'admin'].map(unk), new Array(3).fill('/bots 5\nThat command is not one that you have. Type /help to see the commands.'));
    const held = rig(); held.live(W({ you: asRole('none') }), false);
    held.clock.advance(2600); held.submit('/ignore pebble'); held.s().recv({ t: 'reply', c: 'ignore', r: 'ok', n: 1 });
    same('a reply that comes before the helper is there is kept and drawn when it has come (the line that was typed is its echo)', [held.answers(), (held.s().recv(CMDS('none')), held.clock.advance(1), held.answers())], [[], ['/ignore pebble\nDone. You will not see that person\u2019s lines any more. You ignore 1 person now.']]);
    const junk = rig(); junk.live(W({ you: asRole('admin') }));
    [{ t: 'reply' }, { t: 'reply', c: 5, r: {} }, { t: 'reply', c: 'bots', r: 'status', queues: 'x' }, { t: 'reply', c: 'bots', r: 'started', asked: 'x' }, { t: 'reply', c: 'bots', r: 'bad_map', close: 'x' }, { t: 'reply', c: 'bots', r: 'status', queues: [{ by: { x: 1 }, held: 5, total: 'x' }] },
        { t: 'cmds' }, { t: 'cmds', rows: [null, 5, { w: 5 }] }, { t: 'cmds', rows: { length: 5 } }].forEach((f) => { junk.s().recv(f); });
    check('garbage frames of the two new kinds throw nothing and leave the dock live', junk.errors.length === 0 && junk.dock.state() === 'live', junk.errors.map((e) => e.stack).join('|'));
    const sheet = rig(); sheet.live(W({ you: asRole('mod') }));
    const got = []; sheet.dock.command('/mute #5 10', (code) => got.push(code)); sheet.s().recv({ t: 'reply', c: 'unknown', r: 'unknown' });
    sheet.clock.advance(2600); sheet.dock.command('/mute #5 10', (code) => got.push(code)); sheet.s().recv({ t: 'reply', c: 'mute', r: 'rate', wait: 3 });
    same('a command of the moderation sheet that the hub answers with a reply is told to the sheet in its own codes (not a command of the account: power; too fast: rate) and the dock adds nothing to the log', [got, sheet.answers()], [['power', 'rate'], []]);
}
{   // the words of a role in the dock's header: the command row for /bots has the hub's usage, shown only when chosen
    const r = rig(); r.live(W({ you: asRole('admin') })); r.type('/bo');
    same('the chosen row shows how the command is typed (the hub\'s own text), only on the chosen row', [byClass(r.pop(), 'gh-a').map((x) => x.textContent), r.selected()], [['/bots COUNT [every MINUTES] [map NAME] | status | stop'], ['/bots']]);
    r.type('/b');
    same('the typed letters are marked in each word', byClass(r.pop(), 'gh-w').map((w) => w.children[0] ? [w.children[0].tagName, w.children[0].textContent, w.children[1] ? w.children[1].textContent : w.textContent] : [w.textContent]), [['mark', '/b', 'an'], ['mark', '/b', 'ans'], ['mark', '/b', 'ots']]);
}

// ---- 5. people: the names after `/invite ` and after an `@`, and the words of an invitation's answer ----
{
    const P = Cmds;
    const PEOPLE = [{ id: 1, name: 'juniper', at: 'look' }, { id: 5, name: 'Wren', at: 'wait' }, { id: 2, name: 'maple.leaf', at: 'look' }, { id: 3, name: 'tess', at: 'look' }, { id: 4, name: 'Clover', at: 'wait' }, { id: 6, name: 'moss', at: 'play' }, { id: 9, name: 'Me', at: 'look', me: true }, { id: 10, name: 'rowan', at: 'away' }];
    const WHY = { Wren: 'In your room', moss: 'Playing a match' }, T0 = 1700000000;
    const NOWS = Math.floor(Date.now() / 1000);       // (the helper says how long ago with the real clock)
    const HIST = [{ id: 1, at: NOWS - 3600, who: 'marigold', text: 'b', k: 'kmari' }, { id: 2, at: NOWS - 1200, who: 'birch', text: 'a', k: 'kbirch' }, { id: 3, at: NOWS - 60, who: 'juniper', text: 'c', k: 'kjun' }, { id: 4, at: NOWS - 30, who: 'maple', text: 'd', k: 'kme' }, { id: 5, at: NOWS - 90, who: 'rowan', text: 'e', k: 'krow' }];
    function withPeople(o) {
        const r = rig(); r.live(W({ you: Object.assign(asRole('none'), { k: 'kme' }), history: HIST.map((h) => Object.assign({}, h)) }));
        r.invited = [];
        r.field().setSelectionRange = (a, b) => { r.field().selectionStart = a; r.field().selectionEnd = b; };       // (the fake text area keeps no caret of its own: a typed text puts it at the end, as a keyboard does)
        const typed = r.type; r.type = (text) => { r.field().selectionStart = r.field().selectionEnd = text.length; typed(text); };
        r.dock.directory(() => ({ rows: PEOPLE, why: (row) => WHY[row.name] || '', invited: (name) => r.invited.push(name) }));
        if (o && o.none) r.dock.directory(null);
        return r;
    }
    const heads = (r) => [one(r.pop(), 'gh-top').children.map((c) => c.textContent), byClass(r.pop(), 'gh-cap').map((c) => c.textContent)];
    const row = (r, name) => byClass(r.pop(), 'gh-row').filter((x) => one(x, 'gh-w').textContent === name)[0];
    const sub = (r, name) => one(row(r, name), 'gh-d').textContent;
    let r = withPeople();
    r.type('/inv');
    same('the command narrows as any command does, and a Tab writes it with the space where a name follows', [r.popRows(), (r.key('Tab'), r.field().value), r.popRows()[0]], [['/invite'], '/invite ', 'juniper']);
    same('after `/invite ` the people who are here: the ones who can be invited first, then the dim ones with the reason; not the visitor, not a person who is away', [r.popRows(), heads(r)[0], heads(r)[1]], [['juniper', 'maple.leaf', 'tess', 'Clover', 'Wren', 'moss'], ['People here, to invite', '6 online'], []]);
    same('the first is chosen and shows the command that it makes; a dim row is dim, with its reason where the place was', [r.selected(), one(row(r, 'juniper'), 'gh-a').textContent, cls(row(r, 'Wren')).indexOf('gh-dim') !== -1, sub(r, 'Wren'), sub(r, 'moss'), sub(r, 'tess'), cls(row(r, 'tess')).indexOf('gh-dim') !== -1], [['juniper'], '/invite juniper', true, 'In your room', 'Playing a match', 'Looking', false]);
    r.type('/invite m');
    same('letters narrow it (the names that start with them, in any case), the matching letters marked, the ones who cannot be invited after', [r.popRows(), heads(r)[0], byClass(row(r, 'maple.leaf'), 'gh-w')[0].children[0].textContent], [['maple.leaf', 'moss'], ['People here', '2 matches'], 'm']);
    r.type('/invite M'); check('in any case', r.popRows().join() === 'maple.leaf,moss');
    r.type('/invite zzz');
    same('a name that nobody has: the list says so, and a Tab chooses nothing', [byClass(r.pop(), 'gh-none')[0].textContent, r.key('Tab').prevented, r.field().value], ['Nobody here by that name.', false, '/invite zzz']);
    r.type('/invite m'); r.key('Tab');
    same('Tab writes the chosen name after the command, and the list stays with its one row', [r.field().value, r.popRows(), r.said().length], ['/invite maple.leaf', ['maple.leaf'], 0]);
    r.key('Enter');
    same('and Enter now sends it, as typed and once: a whole name is not chosen again', r.said().map((m) => m.text), ['/invite maple.leaf']);
    r.type('/invite '); r.key('Escape');
    same('Escape closes the list', [r.pop(), r.field().value], [null, '/invite ']);
    r.type('/invitation');
    same('a word that only starts with the command is no command and no names: the list of commands says so', byClass(r.pop(), 'gh-none')[0].textContent.slice(0, 31), 'No command of yours starts with');
    r.type('/invite2');
    check('the command with something glued to it that is no letter is no command and has no names', r.pop() === null);
    r.type('/invite e');
    same('a name is matched from its start: a letter inside a name is none', byClass(r.pop(), 'gh-none')[0].textContent, 'Nobody here by that name.');
    r.type('/invite'); check('the command alone, with no space, is the command\'s row, not the names', r.popRows().join() === '/invite');
    const dim = withPeople(); dim.type('/invite '); dim.key('ArrowDown'); dim.key('ArrowDown'); dim.key('ArrowDown'); dim.key('ArrowDown'); dim.key('Tab');
    check('a dim row can be chosen too: the hub answers what the reason says', dim.field().value === '/invite Wren');
    // an @ in the line
    r = withPeople();
    r.type('@');
    same('an @ opens the people to mention: those here (with where they are), then those who wrote lately and are not here, newest first', [r.popRows(), heads(r)[0], heads(r)[1], sub(r, 'juniper'), sub(r, 'Clover'), sub(r, 'moss'), sub(r, 'birch'), sub(r, 'marigold')], [
        ['juniper', 'Wren', 'maple.leaf', 'tess', 'Clover', 'moss', 'rowan', 'birch', 'marigold'], ['People to mention', '9 people'], ['Here now', 'Wrote lately'], 'Looking', 'In a room', 'Playing a match', 'Wrote 20 min ago · not here', 'Wrote 1 hour ago · not here']);
    same('the first is chosen and shows what it writes; a person who is here and wrote is only here; the visitor\'s own name (by its key) is not among the ones who wrote lately; a person who is away and wrote is', [r.selected(), one(row(r, 'juniper'), 'gh-a').textContent, r.popRows().filter((n) => n === 'juniper').length, r.popRows().indexOf('maple'), sub(r, 'rowan')], [['juniper'], '@juniper', 1, -1, 'Wrote 1 min ago \u00b7 not here']);
    r.type('hey @m');
    same('in the middle of a line, narrowing as it is typed', [r.popRows(), heads(r)[0], heads(r)[1]], [['maple.leaf', 'moss', 'marigold'], ['People to mention', '3 matches'], ['Here now', 'Wrote lately']]);
    r.key('Tab');
    same('Tab writes the name in place of the @ and the letters, with a space after it, and closes the list', [r.field().value, r.pop(), r.field().selectionStart], ['hey @maple.leaf ', null, 16]);
    r.type('hi @juniper, and @t'); r.key('Enter');
    same('Enter picks while the list is open (it does not send)', [r.field().value, r.said().length], ['hi @juniper, and @tess ', 0]);
    r.type('@ma'); r.key('ArrowDown'); r.key('Tab');
    check('the arrows choose before Tab', r.field().value === '@marigold ' || r.field().value === '@maple.leaf ' || r.field().value === '@marigold ');
    r.type('mail me at a@b'); check('an @ inside a word is no mention: no list', r.pop() === null);
    r.type('a @'); check('an @ after a space is', r.pop() !== null);
    r.type('@zzz'); check('letters that match nobody: the @ and the letters are words', r.pop() === null);
    r.type('/ignore @t'); check('a command line has no mentions', r.pop() === null);
    r.type('@juniper '); check('a name that is finished with its space: no list', r.pop() === null);
    r.type('@'); r.key('Escape'); check('Escape closes it', r.pop() === null);
    const none = withPeople({ none: true }); none.type('@');
    same('the players list is not there: only the people who wrote lately are offered', [none.popRows(), heads(none)[1]], [['rowan', 'juniper', 'birch', 'marigold'], ['Wrote lately']]);
    const lonely = rig(); lonely.live(W({ you: asRole('none') })); lonely.dock.directory(() => ({ rows: [], why: () => '' })); lonely.type('@');
    check('nobody is here and nobody wrote: nothing to show', lonely.pop() === null);
    const caret = withPeople(); caret.field().value = 'x @ju here'; caret.field().selectionStart = caret.field().selectionEnd = 5; caret.field().fire('input', {});
    same('the caret decides: the letters before it are the ones that count, and the words after it stay', [caret.popRows(), (caret.key('Tab'), caret.field().value)], [['juniper'], 'x @juniper here']);
    // the words of the answer of /invite
    const reply = (m, typed) => P.replyOf(Object.assign({ t: 'reply', c: 'invite' }, m), typed || '/invite pat', 1700000000000, null);
    same('the words of every answer', [reply({ r: 'ok', to: 'juniper' }).lines, reply({ r: 'usage' }).lines, reply({ r: 'ambiguous' }).lines, reply({ r: 'self' }).lines, reply({ r: 'noroom' }).lines, reply({ r: 'full', n: 4, of: 4 }).lines,
        reply({ r: 'busy', to: 'moss' }).lines, reply({ r: 'person', to: 'juniper', wait: 45 }).lines, reply({ r: 'person', to: 'juniper', wait: 1 }).lines, reply({ r: 'total', wait: 30 }).lines, reply({ r: 'rate', wait: 3 }).lines, reply({ r: 'what' }).lines], [
        ['Invitation sent to juniper.'], ['Type /invite NAME, for example /invite pebble.'], ['Several people show that name, so it cannot be told which one you mean.'], ['You cannot invite yourself.'], ['You are not in a room. Use Quick host to open one, then invite.'], ['Your room is full (4 of 4).'],
        ['moss is playing a match.'], ['You invited juniper a moment ago. Try again in 45 seconds.'], ['You invited juniper a moment ago. Try again in 1 second.'], ['Too many invitations. Wait a minute, then try again.'], ['Too fast: wait 3 seconds before the next /invite.'],
        ['That did not work, try again']]);
    same('a person nobody has: the name that was typed is in the words, as text, and a row or a line number has the plain words', [reply({ r: 'nobody' }, '/invite zed').lines, reply({ r: 'nobody' }, '/invite row:5').lines, reply({ r: 'nobody' }, '/invite #3').lines, reply({ r: 'nobody' }, '/invite <b>x</b>').lines], [
        ['Nobody called “zed” is here. Type /invite and a space to see who is.'], ['Nobody here by that name. Type /invite and a space to see who is.'], ['Nobody here by that name. Type /invite and a space to see who is.'], ['Nobody called “<b>x</b>” is here. Type /invite and a space to see who is.']]);
    same('a refusal has the red edge, the answer that is ok has none; the words are never markup, a name is cut at 32', [[reply({ r: 'noroom' }).cls, reply({ r: 'usage' }).cls, reply({ r: 'total' }).cls].join(), reply({ r: 'ok', to: 'x' }).cls, reply({ r: 'ok', to: 'n'.repeat(60) }).lines[0].length, reply({ r: 'ok', to: '<img src=x>' }).lines[0]], ['gc-err,gc-err,gc-err', '', 'Invitation sent to '.length + 33 + 1, 'Invitation sent to <img src=x>.']);
    const q = withPeople(); q.submit('/invite juniper'); q.s().recv({ t: 'reply', c: 'invite', r: 'ok', to: 'juniper' });
    same('the answer is a private block with the typed command above it, and the list greys the person at once', [q.answers(), q.invited], [['/invite juniper\nInvitation sent to juniper.'], ['juniper']]);
    q.clock.advance(2600); q.submit('/invite tess'); q.s().recv({ t: 'reply', c: 'invite', r: 'person', to: 'tess', wait: 20 });
    same('a refusal is a block with the red edge and greys nobody', [cls(q.blocks()[1]).indexOf('gc-err') !== -1, q.invited], [true, ['juniper']]);
}

console.log('home chat commands: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
