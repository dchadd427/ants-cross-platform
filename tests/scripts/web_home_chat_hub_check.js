// Runs the chat dock of Home (web/home/chat.js) against a FAKE hub on a REAL socket (tests/scripts/fake_hub.js: the hub's protocol, docs/HUB.md, over TCP), with the real clock: the welcome and its history, lines, del,
// the visitor's own line and the hub's pace, a welcome that comes in pieces, a frame of megabytes, noise, a hub that is cut and comes back (the first retry is about a second later), a site that has no such door
// (404 on the upgrade), a hub that is not there (503 with the JSON of nginx), a hub that says nothing. The fake-clock cases of every other kind are tests/scripts/web_home_chat_check.js; this one proves that the
// script works over a socket that is not a fake (the client here is tests/scripts/mini_websocket.js, which has the browser's WebSocket for text messages). Run by tests/scripts/test_web_home_chat.py (a few seconds).
// usage: node web_home_chat_hub_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
const H = require('./web_page_harness.js')(process.argv[2] || path.join(__dirname, '..', '..', 'web', 'lobby.html'));
const { startHub, line } = require('./fake_hub.js');
const MiniWebSocket = require('./mini_websocket.js');
const { El } = H;

const chatSource = fs.readFileSync(path.join(H.lobbyDir, 'home', 'chat.js'), 'utf8');
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(test, ms) { const end = Date.now() + (ms || 3000); while (Date.now() < end) { if (test()) return true; await sleep(10); } return test(); }
const cls = (e) => String(e.className).split(/\s+/);
const one = (root, c) => root.findAll((e) => cls(e).indexOf(c) !== -1)[0] || null;

function dockFor(hub, o) {                               // the real script, real timers, a real socket to the fake hub
    o = o || {};
    const root = {};
    new Function('self', 'setTimeout', 'clearTimeout', chatSource)(root, (fn, ms) => setTimeout(fn, ms), (id) => clearTimeout(id));
    const env = { elements: {}, innerHTMLWrites: [] };
    const doc = { hidden: false, listeners: {}, createElement: (t) => new El(env, t), addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); } };
    env.doc = doc;
    const win = { innerWidth: o.width || 1280, innerHeight: 800, location: { protocol: 'http:', host: hub.http.replace('http://', '') }, listeners: {}, addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); } };
    const errors = [];
    const sockets = [];
    class Counted extends MiniWebSocket { constructor(url) { super(url); sockets.push(this); } }
    const host = new El(env, 'div', 'home');
    const dock = root.AntsHomeChat.mount({ doc, win, host, WebSocket: Counted, error: (e) => errors.push(e) });
    dock.shown(true);
    const view = { dock, env, errors, sockets, log: one(dock.root, 'gc-log') };
    view.rows = () => view.log.children.slice(1).map((r) => r.textContent);
    view.note = () => one(dock.root, 'gc-note').textContent;
    view.on = () => one(dock.root, 'gc-on').textContent;
    view.pill = () => { const p = one(dock.root, 'gc-unread'); return p.hidden ? '' : p.textContent; };
    view.form = () => one(dock.root, 'gc-form');
    return view;
}
const HISTORY = [line(1, 'pebble', 'anyone up for Islands?'), line(2, 'clover', 'I have a room open', true), line(3, 'juniper', 'joining!')];

async function main() {
    {   // read only, history, lines, del, count
        const hub = await startHub({ login: false, you: null, history: HISTORY, online: 7 });
        const v = dockFor(hub);
        check('the dock opens one socket to /hub/ws of the page\'s own host', await until(() => hub.upgrades === 1 && v.dock.state() === 'live'));
        check('the welcome is shown: the history as "name: text", the mod marked, the count, the read-only words, and no field', JSON.stringify(v.rows()) === JSON.stringify(['pebble: anyone up for Islands?', 'modclover: I have a room open', 'juniper: joining!']) && v.on() === '7 here now'
              && v.note() === 'Chat is read-only for now' && v.form().hidden === true && hub.received.length === 0);
        hub.say('pebble', 'a new line'); hub.say('x'.repeat(40), 'long name');
        check('a line that the hub sends is a line, a long name is cut', await until(() => v.rows().length === 5) && v.rows()[3] === 'pebble: a new line' && v.rows()[4] === 'x'.repeat(32) + '…: long name');
        hub.del(100);
        check('del takes it away', await until(() => v.rows().length === 4) && v.rows().indexOf('pebble: a new line') === -1);
        hub.setOnline(12);
        check('the online count follows', await until(() => v.on() === '12 here now'));
        check('and the read-only dock says nothing to the hub, whatever is pressed', v.dock.say('hello') === 'readonly' && hub.received.length === 0);
        await hub.close();
        check('the hub going away is "Chat is offline"', await until(() => v.dock.state() === 'offline') && v.note() === 'Chat is offline' && v.errors.length === 0, v.dock.state() + '|' + v.note() + '|' + v.errors.map((e) => e.stack));
    }
    {   // a writer, the hub's pace
        const hub = await startHub({ login: true, you: { name: 'maple.leaf', mod: false }, history: HISTORY, online: 2 });
        const v = dockFor(hub);
        await until(() => v.dock.state() === 'live');
        check('a named visitor has the field and Send', v.form().hidden === false && v.note() === '');
        const answers = ['one', 'two', 'three', 'four', 'five'].map((t) => v.dock.say(t));
        check('five lines in a row: two go (the page\'s burst), the others wait, and the hub never has to say "rate" (the page paces below it)', JSON.stringify(answers) === JSON.stringify(['', '', 'rate', 'rate', 'rate']) && await until(() => v.rows().length === 5) && (hub.rateRefusals || 0) === 0 && hub.received.filter((m) => m.t === 'say').length === 2);
        check('the visitor\'s own lines come back from the hub as lines, in the visitor\'s mark', v.rows().slice(3).join('|') === 'maple.leaf: one|maple.leaf: two' && v.log.children[4].className.indexOf('gc-me') !== -1);
        hub.send(hub.peers[0], { t: 'err', code: 'muted' });
        check('an err of the hub is words in the note', await until(() => v.note() === 'You cannot chat for a while'));
        await sleep(2600);
        check('after 2.5 s the page lets one more through', v.dock.say('six') === '' && await until(() => v.rows().length === 6));
        await hub.close();
    }
    {   // a hostile hub on a real socket: markup, bidi, a frame of megabytes, noise, a welcome in pieces
        const hub = await startHub({ login: true, you: null, history: [], online: 1, mode: 'pieces' });
        hub.set({ history: Array.from({ length: 50 }, (_, i) => line(i + 1, 'user' + i, 'a line of the history, number ' + i + ' ' + 'y'.repeat(150))) });
        const v = dockFor(hub);
        check('a welcome that comes in pieces (50 lines in frames of 700 bytes) is read whole', await until(() => v.dock.state() === 'live', 5000) && v.rows().length === 50);
        hub.say('<img src=x onerror=alert(1)>', '<script>alert(1)</script> ‮ evil ​ text http://x.example');
        check('markup and bidi from the hub are text and nothing else', await until(() => v.rows().length === 51) && v.rows()[50] === '<img src=x onerror=alert(1)>: <script>alert(1)</script> evil text http://x.example' && v.log.all().every((e) => ['div', 'p', 'span', 'bdi'].indexOf(e.tagName) !== -1) && v.env.innerHTMLWrites.length === 0);
        hub.raw(hub.peers[0], require('./fake_hub.js').frame(1, Buffer.from('{"t":"line","id":900,"who":"big","text":"' + 'z'.repeat(3 * 1024 * 1024) + '"}')));
        for (let i = 0; i < 5; i++) hub.send(hub.peers[0], 'not json ' + i);
        hub.say('after', 'the noise');
        check('a frame of 3 MB and a little noise are ignored; the next good line is read; the link is the same one', await until(() => v.rows().pop() === 'after: the noise') && hub.upgrades === 1 && v.errors.length === 0);
        for (let i = 0; i < 40; i++) hub.send(hub.peers[0] || { open: false }, '{{{' + i);
        check('a hub that sends nothing but noise is let go, and the dock comes back to it (a new socket, a new welcome)', await until(() => hub.upgrades === 2, 5000) && await until(() => v.dock.state() === 'live', 5000));
        await hub.close();
    }
    {   // a hub that is cut, and comes back
        const hub = await startHub({ login: true, you: { name: 'maple.leaf', mod: false }, history: HISTORY, online: 2 });
        const v = dockFor(hub);
        await until(() => v.dock.state() === 'live');
        v.dock.open(false);
        hub.drop();
        const cut = Date.now();
        check('a cut socket is "Chat is offline" and "reconnecting…" at once', await until(() => v.dock.state() === 'offline') && v.note() === 'Chat is offline' && v.on() === 'reconnecting…' && v.dock.say('hi') === 'offline');
        hub.set({ history: HISTORY.concat([line(4, 'pebble', 'while you were away')]) });
        check('the first retry is 1 to 1.25 s after the cut (the backoff starts at one second, with jitter), and the welcome after it replaces the log', await until(() => v.dock.state() === 'live', 3000) && Date.now() - cut >= 900 && Date.now() - cut < 2200 && hub.upgrades === 2);
        check('the line that came while the link was down is unread (the dock is folded), the history is not doubled', v.pill() === '1' && v.errors.length === 0);
        v.dock.open(true);
        check('... and opening the dock shows it once', v.rows().length === 4 && v.pill() === '');
        await hub.close();
    }
    {   // a site with no such door (404), a hub that is not there (503 with the JSON of nginx), a hub that says nothing
        const nodoor = await startHub({ path: '/elsewhere' });
        const a = dockFor(nodoor);
        check('a site that has no /hub/ws (404 on the upgrade) is "Chat is offline", and it is tried again later, not at once', await until(() => nodoor.upgrades === 1 && a.dock.state() === 'offline') && a.note() === 'Chat is offline' && (await sleep(500), nodoor.upgrades === 1));
        const off = await startHub({ mode: 'off' });
        const b = dockFor(off);
        check('a hub that nginx answers 503 {"error":"hub_off"} for is "Chat is offline" too', await until(() => off.upgrades === 1 && b.dock.state() === 'offline') && b.note() === 'Chat is offline' && b.errors.length === 0);
        const silent = await startHub({ mode: 'silent' });
        const c = dockFor(silent);
        check('a hub that opens the socket and says nothing: still "connecting…", nothing sent, nothing shown (the dock waits 15 s, which is the fake clock\'s case)', await until(() => silent.upgrades === 1) && (await sleep(300), c.dock.state() === 'connecting') && silent.received.length === 0 && c.rows().length === 0);
        await Promise.all([nodoor.close(), off.close(), silent.close()]);
        const gone = await startHub({});
        const dead = { http: gone.http };
        await gone.close();
        const d = dockFor(dead);
        check('a port that nobody listens on: "Chat is offline", no throw', await until(() => d.dock.state() === 'offline') && d.errors.length === 0 && d.note() === 'Chat is offline');
    }
    console.log('home chat on a real socket: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}
main().catch((e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
