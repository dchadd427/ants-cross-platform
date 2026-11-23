// Runs the images of Home's chat THROUGH THE REAL DOCK (web/home/chat.js with web/home/pics.js and web/home/post.js, docs/HUB_IMAGES.md "The page") against a fake hub: a fake WebSocket that plays the hub's protocol, a fake
// clock, and the fake browser's DOM of tests/scripts/web_page_harness.js. The parts are loaded by the dock itself, as home.js would load them (env.load), and meet the dock's own submit, paste, drop and answers.
// tests/scripts/web_home_images_check.js runs the two parts alone on a fake page of their own; this file is what only the real dock can show:
//   1. who gets what: the thumbnails come with the first line that has an image (a line with no words too), the button and the poster only for `you.img` = `on`, once, and a paste or a drop for a person who may not post
//      gets the few words of the reason and the page is not taken away;
//   2. the line: the id of an upload goes out as one `say` frame with the caption, the hub's echo of the line ends the wait, a refusal (`img`, `imgid`, a word of the hub's own) is told and given back, a banned or offline
//      dock sends nothing, a caption over the limit or with markup is refused before it goes;
//   3. the deletes: `delimg` for the author, answered by the hub's `del` of the line; a second one while one waits is `rate`; the staff's /del goes through the dock's command;
//   4. Send with an image waiting is the poster's: the dock's own submit does not see it (the box hears the submit first).
// tests/scripts/test_web_home_images.py runs this with node (the quick tier). usage: node web_home_images_dock_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
globalThis.AntsShapes = require(path.resolve(__dirname, '..', '..', 'web', 'shapes.js'));

const lobbyPath = process.argv[2];
if (!lobbyPath) { console.log('usage: web_home_images_dock_check.js lobby.html'); process.exit(2); }
const home = path.join(path.dirname(path.resolve(lobbyPath)), 'home');
const chatSource = fs.readFileSync(path.join(home, 'chat.js'), 'utf8');
const Pics = require(path.join(home, 'pics.js')), Post = require(path.join(home, 'post.js'));
const H = require('./web_page_harness.js')(lobbyPath);
const { El } = H;

let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }
const NAME = '0123456789abcdef0123456789abcdef', OTHER = 'fedcba9876543210fedcba9876543210';
const IMG = (o) => Object.assign({ n: NAME, x: 'png', w: 1280, h: 720, tw: 360, th: 203, a: false }, o || {});
const cls = (e) => String(e.className).split(/\s+/);
const byClass = (root, c) => root.findAll((e) => cls(e).indexOf(c) !== -1);
const one = (root, c) => byClass(root, c)[0] || null;
const shows = (e) => { for (let x = e; x; x = x.parent) if (x.hidden) return false; return true; };

function makeClock() {
    const c = { t: 0, timers: [], next: 1 };
    c.setTimeout = (fn, ms) => { const id = c.next++; c.timers.push({ id, at: c.t + (ms || 0), fn }); return id; };
    c.clearTimeout = (id) => { c.timers = c.timers.filter((x) => x.id !== id); };
    c.advance = (ms) => {
        const until = c.t + ms;
        for (let turns = 0; turns < 20000; turns++) {
            const due = c.timers.filter((x) => x.at <= until).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            c.timers = c.timers.filter((x) => x !== due); c.t = Math.max(c.t, due.at); due.fn();
        }
        c.t = until;
    };
    return c;
}
const ALL = [];
function rig(o) {
    o = o || {};
    const env = { elements: {}, innerHTMLWrites: [] }, clock = makeClock(), root = {};
    new Function('self', 'setTimeout', 'clearTimeout', chatSource)(root, clock.setTimeout, clock.clearTimeout);
    const Chat = root.AntsHomeChat;
    const doc = { hidden: false, listeners: {}, createElement: (t) => new El(env, t), createElementNS: (ns, t) => new El(env, t), createTextNode: (t) => { const e = new El(env, '#text'); e.textContent = t; return e; },
        addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); }, removeEventListener(t, fn) { doc.listeners[t] = (doc.listeners[t] || []).filter((f) => f !== fn); }, getElementById: () => null };
    env.doc = doc;
    const requests = [];
    class XHR { constructor() { this.headers = {}; this.upload = {}; requests.push(this); } open(m, u) { this.method = m; this.url = u; } setRequestHeader(k, v) { this.headers[k] = v; } send(b) { this.body = b; } abort() {} }
    const win = { innerWidth: 1280, innerHeight: 800, location: { protocol: 'https:', host: 'play.test' }, listeners: {}, XMLHttpRequest: XHR, URL: { createObjectURL: () => 'blob:made', revokeObjectURL() {} }, setTimeout: clock.setTimeout, clearTimeout: clock.clearTimeout,
        addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); } };
    const sockets = [];
    class HubSocket {
        constructor(url) { this.url = url; this.sent = []; this.readyState = 0; sockets.push(this); }
        send(text) { if (this.readyState !== 1) throw new Error('send on a socket that is not open'); this.sent.push(text); if (text === '{"t":"ping"}') this.recv({ t: 'online', n: 7 }); }
        close() { this.readyState = 3; }
        open() { this.readyState = 1; if (this.onopen) this.onopen({}); return this; }
        recv(x) { if (this.onmessage) this.onmessage({ data: typeof x === 'string' ? x : JSON.stringify(x) }); return this; }
    }
    const host = new El(env, 'div', 'home'), loads = [], posters = [], errors = [];
    const load = (name, cb) => {
        loads.push(name);
        if (name === 'home/pics') win.AntsHomePics = Pics;
        if (name === 'home/post') win.AntsHomePost = { mount(e) { const api = Post.mount(e); posters.push(api); return api; } };
        clock.setTimeout(cb, 0);                                // (a script arrives after the page's own code has finished: never inside the paint that asked for it)
    };
    const Intl = { DateTimeFormat: function () { return { format: () => '' }; } };
    const dock = Chat.mount({ doc, win, host, fetch: undefined, now: clock.now = () => clock.t, random: () => 0, Intl, WebSocket: HubSocket, error: (e) => errors.push(e), load: o.noLoad ? undefined : load });
    dock.shown(true);
    const r = { env, clock, doc, win, sockets, dock, errors, loads, posters, requests, root: dock.root };
    ALL.push(r);
    r.s = () => sockets[sockets.length - 1];
    r.form = () => one(dock.root, 'gc-form'); r.field = () => one(dock.root, 'gc-input'); r.log = () => one(dock.root, 'gc-log');
    r.live = (welcome) => { r.s().open().recv(Object.assign({ t: 'welcome', v: 1, you: null, login: true, online: 7, history: [] }, welcome)); clock.advance(0); return r.s(); };
    r.recv = (m) => { r.s().recv(m); clock.advance(0); };
    r.sent = () => r.s().sent.map((x) => JSON.parse(x)).filter((m) => m.t !== 'ping');
    r.submit = () => { const ev = { preventDefault() { this.defaultPrevented = true; }, stopImmediatePropagation() { this.stopped = true; }, target: r.form() }; r.root.fire('submit', ev); if (!ev.stopped) r.form().fire('submit', ev); return ev; };
    r.rows = () => r.log().children.filter((x) => cls(x).indexOf('gc-line') !== -1);
    // the helper buttons that talk.js would have put in the form: the poster stands beside them
    r.talk = () => { const tools = new El(env, 'span'); tools.className = 'gt-tools'; const mic = new El(env, 'button'); mic.className = 'gt-mic'; tools.appendChild(mic); const say = new El(env, 'span'); say.className = 'gt-say'; say.hidden = true; r.form().appendChild(tools); r.form().appendChild(say); return { tools, say }; };
    return r;
}
const YOU = (img, extra) => Object.assign({ name: 'maple.leaf', k: 'kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk', role: 'none', img }, extra || {});
const LINE = (id, who, text, img, k) => ({ id, at: 1700000000 + id, who, text, img, k });
const file = (type, size, name) => ({ type, size, name: name || 'shot.png', slice() { return this; } });
function upload(r, id) { const q = r.requests[r.requests.length - 1]; q.status = 200; q.responseText = JSON.stringify({ id: id || NAME }); q.onload(); }

// ---- 1. who gets what -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig();
    r.live({ you: YOU('role'), history: [LINE(1, 'pebble', 'anyone up for Islands?'), LINE(2, 'clover', 'a map', IMG()), LINE(3, 'juniper', '', IMG({ n: OTHER, x: 'gif', a: true }))] });
    check('the reader\'s part is asked for with the first line that has an image, once, and nothing else is loaded for a person who may not post', r.loads.join() === 'home/pics');
    const thumbs = byClass(r.log(), 'gc-thumb');
    check('...the log shows a thumbnail for each line that has an image (a line with no words too) and none for the line without one', thumbs.length === 2 && byClass(r.rows()[0], 'gc-thumb').length === 0 && byClass(r.rows()[2], 'gc-thumb').length === 1);
    check('...the thumbnail comes after the words and is made of text and properties only', one(r.rows()[1], 'gc-pic') === r.rows()[1].children[r.rows()[1].children.length - 1] && r.env.innerHTMLWrites.length === 0);
    r.recv({ t: 'line', id: 4, at: 1700000004, who: 'pebble', text: 'look', img: IMG({ n: OTHER, x: 'jpg' }) });
    check('a line that comes later has its thumbnail at once', byClass(r.log(), 'gc-thumb').length === 3 && r.loads.length === 1);
    r.recv({ t: 'line', id: 5, at: 1700000005, who: 'pebble', text: 'bad', img: { n: '../x', x: 'png', tw: 1, th: 1 } });
    r.recv({ t: 'line', id: 6, at: 1700000006, who: 'pebble', text: 'bad too', img: 'x' });
    check('an image that is not a good one is not drawn (and the line keeps its words)', byClass(r.log(), 'gc-thumb').length === 3 && r.rows().length === 6);
    check('a person who may not post gets no button, no poster, no loader for it, and an errored loader does not break the dock', r.posters.length === 0 && r.errors.length === 0);
    const pl = rig({ noLoad: true }); pl.live({ you: YOU('on'), history: [LINE(1, 'a', 'x', IMG())] });
    check('a page that cannot load its parts shows the line with its words and no image, and sends nothing', pl.rows().length === 1 && byClass(pl.log(), 'gc-thumb').length === 0 && pl.errors.length === 0);
}
for (const [mode, loaded] of [['on', true], ['role', false], ['off', false], ['wait', false], [undefined, false]]) {
    const r = rig(); r.live({ you: YOU(mode) });
    check('you.img = ' + mode + (loaded ? ': the poster is loaded (once, though the hub says it again)' : ': the poster is not loaded'), (loaded ? r.loads.join() === 'home/post' : r.loads.length === 0) && (r.recv({ t: 'you', you: YOU(mode) }), r.loads.length === (loaded ? 1 : 0)));
}
{
    const r = rig(); r.live({ you: YOU('role') });
    r.recv({ t: 'you', you: YOU('on') });
    check('a `you` that comes later and says `on` loads it (the role was given while the page was open)', r.loads.join() === 'home/post');
    const r2 = rig(); r2.live({ you: YOU('on') }); r2.talk(); r2.recv({ t: 'you', you: YOU('off') });
    const gone = r2.posters[0]; const f = file('image/png', 1000);
    check('...and when the right is taken away later the poster that was loaded is refused by the dock before the line goes out (the hub would refuse it too)', (gone.add(f), gone.submit(), r2.requests.length === 1) && (upload(r2), r2.sent().filter((m) => m.t === 'say').length === 0));
}
for (const [mode, words] of [['role', /Active Anters/], ['off', /turned off for your account/], ['wait', /Could not check your Discord role/]]) {
    const r = rig(); r.live({ you: YOU(mode) }); const t = r.talk();
    const ev = { clipboardData: { types: ['Files'], files: [file('image/png', 5)] }, type: 'paste', defaultPrevented: false, preventDefault() { this.defaultPrevented = true; } };
    r.field().fire('paste', ev);
    check('a paste of a file by a person with img=' + mode + ': the page is not taken away, a few words say why, in the form\'s own note', ev.defaultPrevented && words.test(t.say.textContent) && t.say.hidden === false && r.requests.length === 0);
    r.clock.advance(10000);
    check('...and the words go after a while', t.say.hidden === true);
    const drop = { dataTransfer: { types: ['Files'], files: [file('image/png', 5)] }, type: 'drop', defaultPrevented: false, preventDefault() { this.defaultPrevented = true; } };
    r.root.fire('drop', drop);
    check('...a drop is told the same (a drop would open the file in the tab otherwise)', drop.defaultPrevented && words.test(t.say.textContent));
    const text = { clipboardData: { types: ['text/plain'] }, type: 'paste', defaultPrevented: false, preventDefault() { this.defaultPrevented = true; } };
    r.field().fire('paste', text);
    check('...and a paste of words is left alone', !text.defaultPrevented);
}
{
    const r = rig(); r.live({ you: YOU('on') }); const t = r.talk();
    const ev = { clipboardData: { types: ['Files'], files: [file('image/png', 5)] }, type: 'paste', defaultPrevented: false, preventDefault() { this.defaultPrevented = true; } };
    r.field().fire('paste', ev);
    check('a person who may post is not told off for a paste (the poster takes it)', t.say.hidden === true && r.requests.length === 0);
    const none = rig(); none.live({ you: YOU(undefined) }); const u = none.talk();
    const again = { clipboardData: { types: ['Files'], files: [file('image/png', 5)] }, type: 'paste', defaultPrevented: false, preventDefault() { this.defaultPrevented = true; } };
    none.field().fire('paste', again);
    check('a hub that has no images says nothing of them: a paste of a file is left to the browser, no words, no poster', !again.defaultPrevented && u.say.hidden === true && none.loads.length === 0);
}

// ---- 2. the line ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig(); r.live({ you: YOU('on') }); r.talk();
    const post = r.posters[0];
    post.add(file('image/png', 5000)); r.field().value = 'the best spot';
    const ev = r.submit();
    check('Send with an image waiting is the poster\'s: the dock\'s own submit does not see it, and one upload goes out, to the hub\'s door', ev.defaultPrevented && ev.stopped && r.requests.length === 1 && r.requests[0].url === '/hub/img/up' && r.sent().length === 0);
    upload(r);
    same('the id of the upload goes out as one `say` with the caption, and nothing more', r.sent(), [{ t: 'say', text: 'the best spot', img: NAME }]);
    check('...the field keeps its caption until the line comes back', r.field().value === 'the best spot' || r.field().value === '');
    r.recv({ t: 'line', id: 9, at: 1700000009, who: 'maple.leaf', text: 'the best spot', img: IMG(), k: 'kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk' });
    check('the hub\'s echo of the line ends the wait: the card is gone, the field is empty, the line is in the log with its thumbnail, and the dock waits for nothing', r.field().value === '' && byClass(r.form(), 'gp-card').length === 0 && byClass(r.log(), 'gc-thumb').length === 1 && r.errors.length === 0);
    post.add(file('image/png', 5000)); r.submit(); upload(r, OTHER);
    r.recv({ t: 'err', code: 'imgid' });
    check('a refusal of the line (`imgid`: the upload is not there any more) is told in words, the card says that the chat did not take it, and the image waits so that it can be sent again',
        /not there any more/.test(one(r.root, 'gc-note').textContent) && /did not take it/.test(byClass(r.form(), 'gp-card')[0].textContent) && post.state().waiting);
    r.submit(); upload(r);
    r.recv({ t: 'err', code: 'img' });
    check('`img` (the right was taken away): told in words, and the image waits', /cannot post images/.test(one(r.root, 'gc-note').textContent) && post.state().waiting);
    r.submit(); upload(r);
    r.recv({ t: 'err', code: 'rate' });
    check('`rate` for an image line is the usual "Slow down" and the image waits', /Slow down/.test(one(r.root, 'gc-note').textContent) && post.state().waiting);
    post.clear();
    const before = r.sent().length;
    post.add(file('image/png', 5000)); r.field().value = 'a < b'; r.submit(); upload(r);
    check('a caption with markup is refused before it goes out: nothing is sent, the image waits', r.sent().length === before && post.state().waiting && /did not take it/.test(byClass(r.form(), 'gp-card')[0].textContent));
    r.field().value = 'x'.repeat(1001); r.submit(); upload(r);
    check('...and so is one over the limit, with the dock\'s words', r.sent().length === before && post.state().waiting && /too long/.test(one(r.root, 'gc-note').textContent), one(r.root, 'gc-note').textContent);
    post.clear();
    post.add(file('image/png', 5000)); r.field().value = ''; r.submit(); upload(r);
    check('a caption may be empty: the line goes out with `text` empty', r.sent().slice(-1)[0].text === '' && r.sent().slice(-1)[0].img === NAME);
}
{
    const r = rig(); r.live({ you: YOU('on') }); r.talk();
    r.field().value = 'a line'; r.submit(); r.recv({ t: 'err', code: 'banned' });
    r.posters[0].add(file('image/png', 5000)); r.submit(); upload(r);
    check('after the hub has said that the person is banned, an image line is not sent (the card keeps the image)', r.sent().filter((m) => m.img).length === 0 && r.posters[0].state().waiting);
}
{
    const r = rig(); r.live({ you: YOU('on') }); r.talk();
    r.s().readyState = 3;
    r.posters[0].add(file('image/png', 5000)); r.submit(); upload(r);
    check('a dock that is offline sends nothing and the image waits', r.sent().length === 0 && r.posters[0].state().waiting && !r.posters[0].state().busy);
}
{
    const r = rig(); r.live({ you: YOU('on') }); r.talk();
    r.posters[0].add(file('image/png', 5000)); r.submit(); upload(r);
    r.clock.advance(9000);
    check('a line that is not answered in 8 seconds gives the image back to be sent again (not lost)', r.posters[0].state().waiting && !r.posters[0].state().busy);
}

// ---- 3. the deletes ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig();
    r.live({ you: YOU('on'), history: [LINE(1, 'maple.leaf', 'mine', IMG(), 'kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk'), LINE(2, 'maple.leaf', 'mine too', IMG({ n: OTHER }), 'kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk')] });
    const viewerOf = () => one(r.doc.body || r.root, 'gv-scrim') || one(r.root, 'gv-scrim');
    const delete1 = (i) => { byClass(r.rows()[i], 'gc-thumb')[0].fire('click', {}); const v = viewerOf(); byClass(v, 'btn').filter((b) => b.textContent === 'Delete my image')[0].fire('click', {}); byClass(v, 'gv-del').slice(-1)[0].fire('click', {}); return v; };
    delete1(0);
    const second = delete1(1);
    check('a delete asked while another waits is `rate` (one at a time): no second frame goes out, and the viewer says so', r.sent().length === 1 && /Too fast/.test(second.textContent));
    r.recv({ t: 'err', code: 'bad' });
    check('a refusal of the hub (the line is not there, not the person\'s) is told in the viewer, and the viewer stays', !!viewerOf());
    r.clock.advance(9000);
    check('a delete that the hub does not answer in 8 seconds is told too (`silent`: try again), and nothing is deleted on the screen', r.rows().length === 2 && !!viewerOf());
}
{
    const r = rig();
    r.live({ you: YOU('on', { role: 'mod' }), history: [LINE(1, 'pebble', 'theirs', IMG({ n: OTHER }), 'oooooooooooooooooooooooooooooooo')] });
    byClass(r.rows()[0], 'gc-thumb')[0].fire('click', {});
    const scrim = one(r.doc.body || r.root, 'gv-scrim') || one(r.root, 'gv-scrim');
    byClass(scrim, 'btn').filter((b) => b.textContent === 'Delete this image')[0].fire('click', {}); byClass(scrim, 'gv-del').slice(-1)[0].fire('click', {});
    same('a moderator\'s delete goes through the command (/del and the number of the line: the hub\'s log has it)', r.sent(), [{ t: 'say', text: '/del 1' }]);
}

{   // the two commands that an administrator types: the hub's `ok` is answered in the log in words, as the other commands are
    const r = rig(); r.live({ you: YOU('on', { role: 'admin' }) });
    for (const [text, c, words] of [['/noimages pebble', 'noimages', /cannot post images/], ['/images pebble', 'images', /can post images again/]]) {
        r.field().value = text; r.submit();
        check(text + ' goes out as the line that it is', r.sent().slice(-1)[0].text === text && !r.sent().slice(-1)[0].img);
        r.recv({ t: 'ok', c });
        check('...and the hub\'s `ok` is answered in the log: ' + words, byClass(r.log(), 'gc-private').some((row) => words.test(row.textContent)), byClass(r.log(), 'gc-private').map((x) => x.textContent).join('|'));
    }
}

check('no part raised an error in any of the docks', ALL.every((r) => r.errors.length === 0), ALL.map((r) => r.errors.map(String)).join('|'));
console.log('home chat images in the dock: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
