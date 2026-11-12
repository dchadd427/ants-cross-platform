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
        check('the welcome is shown: the history as "name: text", the mod marked, the count, the read-only words, and no field', JSON.stringify(v.rows()) === JSON.stringify(['pebble: anyone up for Islands?', 'mod clover: I have a room open', 'juniper: joining!']) && v.on() === '7 here now'
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
        await until(() => v.note() === '', 8000);                      // (the page's own "Slow down a little" for the three that waited goes by itself)
        hub.send(hub.peers[0], { t: 'err', code: 'rate' });
        await sleep(150);
        check('an err of the hub when no line of the page is in flight is nobody\'s: no words in the note', v.note() === '');
        await sleep(2500);
        check('after 2.5 s the page lets one more through', v.dock.say('six') === '');
        hub.send(hub.peers[0], { t: 'err', code: 'muted' });
        check('an err of the hub for a line that is in flight is words in the note', await until(() => v.note() === 'You cannot chat for a while') && await until(() => v.rows().length === 6));
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
        check('the first retry is 1 to 1.5 s after the cut (the backoff starts at one second, with jitter), and the welcome after it replaces the log', await until(() => v.dock.state() === 'live', 3000) && Date.now() - cut >= 900 && Date.now() - cut < 2500 && hub.upgrades === 2);
        check('the line that came while the link was down is unread (the dock is folded), the history is not doubled', v.pill() === '1' && v.errors.length === 0);
        v.dock.open(true);
        check('... and opening the dock shows it once', v.rows().length === 4 && v.pill() === '');
        await hub.close();
    }
    {   // the two names: the switch on a real socket, the fake hub closes the account's sockets as the real one does, the page reads the new name after its first retry
        const hub = await startHub({ login: true, you: { name: 'Maple Leaf', mod: false, k: 'aaaaaaaaaaaa', pick: 'display', other: 'maple.leaf' }, history: [line(1, 'Maple Leaf', 'under the display name', false, { k: 'aaaaaaaaaaaa' }), line(2, 'Maple Leaf', 'another account that shows the same name', false, { k: 'bbbbbbbbbbbb' })], online: 2 });
        const v = dockFor(hub);
        await until(() => v.dock.state() === 'live');
        const mineRows = () => v.log.children.slice(1).map((r) => cls(r).indexOf('gc-me') !== -1);
        (v.chip && v.chip.click());
        check('a hub that says that the account has both names: the history has the visitor\'s line marked by the key and not the other account\'s of the same name; the menu offers the other name', JSON.stringify(mineRows()) === '[true,false]' && one(v.dock.slot, 'gc-pick').textContent === 'Show my username: maple.leaf');
        one(v.dock.slot, 'gc-pick').click();
        check('a press sends one pick frame with the other name', await until(() => hub.received.filter((m) => m.t === 'pick').length === 1) && hub.received.filter((m) => m.t === 'pick')[0].pick === 'username');
        const cut = Date.now();
        check('the hub closes the socket, the dock is offline for a moment and then connects again (a second socket, a second welcome, after about a second)', await until(() => hub.upgrades === 2 && v.dock.state() === 'live', 4000) && Date.now() - cut < 3000);
        check('the new welcome names the account by its username, and the menu offers the display name; the old line is still the visitor\'s (by its key)', one(v.dock.slot, 'gc-an').textContent === 'maple.leaf' && one(v.dock.slot, 'gc-pick').textContent === 'Show my display name: Maple Leaf' && JSON.stringify(mineRows()) === '[true,false]' && v.errors.length === 0);
        v.dock.say('and now by the username');
        check('a new line is by the new name, and comes back as the visitor\'s own', await until(() => v.rows().length === 3) && v.rows()[2] === 'maple.leaf: and now by the username' && cls(v.log.children[3]).indexOf('gc-me') !== -1);
        await hub.close();
        const plain = await startHub({ login: true, you: { name: 'maple.leaf', mod: false }, history: [], online: 1 });
        const w = dockFor(plain);
        await until(() => w.dock.state() === 'live');
        (w.chip && w.chip.click());
        check('an account that cannot choose has no button, and a pick that is made anyway is refused by the hub without closing anything (the hub says err pick; the dock says that it did not work)', one(w.dock.slot, 'gc-pick').hidden === true
              && (plain.send(plain.peers[0], { t: 'err', code: 'pick' }), await until(() => w.note() === 'That did not work, try again')) && plain.upgrades === 1 && w.dock.state() === 'live');
        await plain.close();
    }
    {   // the roles on a real socket: the hub's commands, its private answers, its own line, a live change of the role, and being removed
        const ME = { name: 'maple.leaf', k: 'aaaaaaaaaaaa' };
        const hub = await startHub({ login: true, you: Object.assign({ mod: true, role: 'mod' }, ME), history: HISTORY, people: { bob: 'none', modo: 'mod', boss: 'admin' }, bans: [{ u: 'carol', at: 1700000000 }] });
        const v = dockFor(hub);
        const answers = () => v.log.children.slice(1).filter((r) => cls(r).indexOf('gc-private') !== -1).map((r) => r.children.slice(1).map((c) => c.textContent).join('\n'));
        const say = (text) => { one(v.dock.root, 'gc-input').value = text; one(v.dock.root, 'gc-form').fire('submit', { preventDefault() {} }); };
        check('a moderator\'s welcome: the field is on', await until(() => v.dock.state() === 'live') && one(v.dock.root, 'gc-input').disabled === false && v.dock.root.getAttribute('data-role') === 'mod');
        say('/help');
        check('/help is answered by the page and the hub is told nothing', answers().length === 1 && hub.received.filter((m) => m.t === 'say').length === 0);
        say('/kick bob');
        check('a good /kick: ok, and everybody is shown the hub\'s own line, as a quiet line with no author', await until(() => answers().length === 2) && answers()[1] === 'Done: the person was removed.' && await until(() => v.rows().indexOf('bob was kicked.') !== -1) && cls(v.log.children[v.log.children.length - 1]).indexOf('gc-event') !== -1);
        say('/ban bob');
        check('a moderator\'s /ban: the hub says power, and the page says it in words, privately', await until(() => answers().length === 3) && answers()[2] === 'You are not allowed to do that');
        const before = v.rows().length;
        await sleep(2600); say('/kick nobody');
        await sleep(2600); say('/kick boss');
        await sleep(2600); say('/kick');
        check('a name that is nobody\'s (who), an admin (power) and a command that is not right (bad): each in its words; nothing is posted for anybody', await until(() => answers().length === 6, 6000) && answers().slice(3).join('|') === 'Nobody by that name is here|You are not allowed to do that|That command is not right. Type /help to see the commands.' && v.rows().length === before + 3 && hub.commands.length === 5);
        hub.you(Object.assign({ mod: true, role: 'admin' }, ME));
        check('the hub makes the visitor an admin (a `you` frame): the data-role follows, no reload, the socket stays', await until(() => v.dock.root.getAttribute('data-role') === 'admin') && hub.upgrades === 1);
        await sleep(2600); say('/bans');
        check('/bans, an admin\'s: the private list', await until(() => answers().length === 7) && answers()[6] === 'People who are banned\ncarol 2023-11-14');
        hub.set({ roleDown: true }); await sleep(2600); say('/mute bob 5');
        check('a role that Discord could not confirm: the hub says role, the page says so', await until(() => answers().length === 8) && answers()[7] === 'Could not confirm your role, try again in a moment');
        hub.you(Object.assign({ mod: false, role: 'none' }, ME));
        check('the hub takes the role away: the commands go (a command word is not sent, "you are not allowed")', await until(() => v.dock.root.getAttribute('data-role') === 'none') && (await sleep(2600), say('/mute bob 5'), answers()[8] === 'You are not allowed to do that') && hub.commands.length === 7);
        check('and the page threw nothing', v.errors.length === 0);
        await hub.close();
    }
    {   // removed: a kick ends the login (the next welcome names nobody), a ban does not (the visitor reads on); the page connects again once, after its wait
        const ME = { name: 'maple.leaf', k: 'aaaaaaaaaaaa', mod: false, role: 'none' };
        const hub = await startHub({ login: true, you: ME, history: HISTORY });
        const v = dockFor(hub);
        check('a visitor', await until(() => v.dock.state() === 'live') && one(v.dock.root, 'gc-input').disabled === false);
        hub.bye('kicked');
        check('kicked: the plain notice, the field off, the Log in button, and the page reads again (one more socket, after its wait of about a second, no loop)', await until(() => v.note() === 'A moderator removed you from the chat. You can log in again.' && v.dock.state() === 'offline' || v.dock.state() === 'live')
              && await until(() => hub.upgrades === 2 && v.dock.state() === 'live', 4000) && one(v.dock.root, 'gc-input').disabled === true && one(v.dock.root, 'gc-login').hidden === false && v.note() === 'A moderator removed you from the chat. You can log in again.');
        await sleep(1500);
        check('... and it stays there: no third socket', hub.upgrades === 2 && v.errors.length === 0);
        hub.set({ you: ME });
        hub.bye('banned');
        check('banned: the notice says that the person can still read; the page connects again, the history is shown, the field is off and says why, and Send and Log in are gone', await until(() => hub.upgrades === 3 && v.dock.state() === 'live', 5000) && v.note() === 'You are banned from the chat. You can still read it.' && one(v.dock.root, 'gc-input').disabled === true
              && one(v.dock.root, 'gc-input').getAttribute('placeholder') === 'You are banned from the chat' && one(v.dock.root, 'gc-send').hidden === true && one(v.dock.root, 'gc-login').hidden === true && v.rows().length === 3);
        await sleep(1500);
        check('... and no more sockets', hub.upgrades === 3 && v.errors.length === 0);
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
