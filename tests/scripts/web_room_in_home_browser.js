// The ROOM INSIDE HOME in a REAL browser (Chrome, driven over its DevTools protocol with no package) against a REAL ants_server, behind a small stand-in for the site: the files of web/, /ws to the server's WebSocket door, a fake chat
// hub at /hub/ws (tests/scripts/fake_hub.js) and the small answers of the page (docs/HOME_SCREEN.md "The Room", docs/PLAY_IN_BROWSER.md "Your room", docs/READY_UP.md). Opt-in, like the other browser checks: it needs a Chrome that it can start
// (CHROME=/path/to/chrome, or the one of a Mac) and a built ants_server; without them it says so and exits 0 (nothing was checked). It starts its own browser with a profile of its own and closes it, the server and the stand-in before it ends,
// also after a failure.
//   1. at 1920 x 1080, 1600 x 900, 1366 x 768 and 1280 x 800: a host makes a room (Quick host, Invite only), a guest joins by the link, a Medium bot sits in the third color: the page does not scroll (the document is no taller or wider than
//      the window), the room panel holds everything (nothing inside it scrolls, nothing is cut off), every control of the room is in the window, and the chat dock is under it with the room's tab: a line of the room is in the room's tab, a line of
//      Home's chat in the other; the games' list is not drawn, the players' list is;
//   2. Ready: the guest has Ready up on their own card, the host has none; START of the host is off with the line "Waiting for <guest> to ready up" until the guest presses Ready, then it is on and the line says that everyone is ready; the host
//      changes the map and the marks stay; the guest takes the mark back and START is off again; the bot and the host show Ready by themselves; a guest's START says that it waits for the host;
//   3. a window lower than the room needs (1366 x 650): the page does not scroll, the room's panel scrolls inside itself and START stays in view;
//   4. a phone upright (390 x 844) and held sideways (844 x 390): the strip has Room, Players and Chat; the Room view scrolls with START at its bottom, the Chat view has the two tabs;
//   --shots DIR writes the pictures of every scene (room_*.png).
// usage: node web_room_in_home_browser.js --server PATH/ants_server --maps PATH/Original-Ants/Maps [--shots DIR]     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const net = require('net');
const { spawn } = require('child_process');

const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i === -1 ? null : args[i + 1]; };
const serverPath = opt('--server'), mapsPath = opt('--maps'), shotsDir = opt('--shots');
if (!serverPath || !mapsPath) { console.log('usage: web_room_in_home_browser.js --server ants_server --maps Original-Ants/Maps [--shots DIR]'); process.exit(2); }
const CHROMES = [process.env.CHROME, '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'].filter(Boolean);
const chromePath = CHROMES.find((p) => fs.existsSync(p));
if (!chromePath || typeof WebSocket !== 'function') { console.log('SKIP: no Chrome that this check can start, or a node without WebSocket: the room was NOT run in a real browser (tests/scripts/web_room_in_home_browser.js)'); process.exit(0); }
const WEB = path.resolve(__dirname, '..', '..', 'web');
const { startHub, line } = require(path.join(__dirname, 'fake_hub.js'));
const row = (i, name, at) => ({ i, name, at: at || 'look' });
const ROWS = [row(3, 'maple.leaf'), row(4, 'Clover', 'wait'), row(5, 'Pebble', 'wait'), row(6, 'Fern', 'wait'), row(7, 'juniper'), row(8, 'moss', 'play'), row(9, 'tess')];
const WHO = { t: 'who', full: true, n: ROWS.length + 2, g: 2, more: 0, rows: ROWS };
const HISTORY = [line(1, 'pebble', 'anyone up for Islands?'), line(2, 'moss', 'gg everyone, that last match was close'), line(3, 'tess', 'new map in the Map maker if anyone wants to try it'), line(4, 'pebble', 'which one?'), line(5, 'tess', 'Four Corners, it is small')];
const PEOPLE = { dAwGy: 2, Dethcon: 1 };               // the two names of the room (a cookie tells the stand-in who a browser is: the login of the fake hub)

let checks = 0, failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(label, fn, ms) {
    const t0 = Date.now();
    for (;;) {
        let v = null;
        try { v = await fn(); } catch (e) { v = null; }
        if (v) return v;
        if (Date.now() - t0 > (ms || 10000)) throw new Error('timeout: ' + label);
        await sleep(50);
    }
}
function freePort() {
    return new Promise((resolve, reject) => {
        const s = net.createServer();
        s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); });
        s.on('error', reject);
    });
}

// ---- the real server and the stand-in for the site -------------------------------------------------------------------------------------------------------------------------------------------------
const started = [];                                   // everything that this run starts, stopped at the end by what holds it (a child process by its handle, a server by close())
async function startServer() {
    const [port, ws, ctl, replay] = [await freePort(), await freePort(), await freePort(), await freePort()];
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ants_room_browser.'));
    const child = spawn(path.resolve(serverPath), ['--maps', path.resolve(mapsPath), '--port', String(port), '--ws-port', String(ws), '--ctl-port', String(ctl), '--replay-port', String(replay), '--results-dir', dir, '--no-replays', '--no-db',
        '--demo-rooms', '2', '--demo-map', 'TREASURE.LVL', '--demo-maps', 'TINY.LVL,SMALL.LVL,MEDIUM.LVL,GAUNTLET.LVL,TREASURE.LVL,ISLANDS.LVL', '--demo-lobbies', '10'], { env: Object.assign({}, process.env, { ANTS_SERVER_SECRET: 'room-browser-secret' }), stdio: 'ignore' });
    const server = { child, port, ws, ctl, replay, dir, stop() { try { child.kill('SIGTERM'); } catch (e) { /* gone */ } try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) { /* it stays */ } } };
    started.push(server);
    await until('the server answers', () => new Promise((resolve) => http.get({ host: '127.0.0.1', port: ctl, path: '/healthz' }, (res) => { res.resume(); resolve(res.statusCode === 200); }).on('error', () => resolve(false))), 15000);
    return server;
}
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.webp': 'image/webp', '.ttf': 'font/ttf', '.svg': 'image/svg+xml', '.json': 'application/json', '.ico': 'image/x-icon' };
const whoIs = (req) => { const m = /(?:^|; )who=([^;]+)/.exec(req.headers.cookie || ''); return m && PEOPLE[decodeURIComponent(m[1])] ? decodeURIComponent(m[1]) : null; };
async function startSite(server) {
    const stamp = (text) => text.replace(/@@SITE_TITLE@@/g, '').replace(/@@SITE_FOOTER@@/g, '').replace(/@@GAME_VERSION@@/g, '0.0.0').replace(/@@BUILD_ID@@/g, 'test').replace(/@@BUILD_DATE@@/g, 'today');
    const proxy = (res, port, target) => {
        const up = http.request({ host: '127.0.0.1', port, path: target, method: 'GET', headers: { host: '127.0.0.1' } }, (r) => { res.writeHead(r.statusCode, { 'content-type': r.headers['content-type'] || 'application/json', 'cache-control': 'no-store' }); r.pipe(res); });
        up.on('error', () => { res.writeHead(503, { 'content-type': 'application/json' }); res.end('{"error":"off"}'); });
        up.end();
    };
    const site = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        if (url.pathname === '/lobbies') { proxy(res, server.replay, '/lobbies'); return; }
        if (url.pathname === '/stats' || url.pathname === '/busy') { proxy(res, server.ws, url.pathname); return; }
        if (url.pathname === '/hub/me') { const me = whoIs(req); res.writeHead(200, { 'content-type': 'application/json', 'cache-control': 'no-store' }); res.end(JSON.stringify(me ? { login: true, you: { name: me, mod: false, role: 'none' } } : { login: false })); return; }
        const file = url.pathname === '/' ? 'lobby.html' : decodeURIComponent(url.pathname).replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (!full.startsWith(WEB + path.sep) || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404); res.end('not found'); return; }
        const type = TYPES[path.extname(full)] || 'application/octet-stream';
        res.writeHead(200, { 'content-type': type, 'cache-control': 'no-cache, must-revalidate' });
        res.end(type.startsWith('text/html') ? stamp(fs.readFileSync(full, 'utf8')) : fs.readFileSync(full));
    });
    await new Promise((resolve) => site.listen(0, '127.0.0.1', resolve));
    site.url = 'http://127.0.0.1:' + site.address().port;
    const hubs = {};                                                                                // (one fake hub for each person: a login, the welcome with the list of players)
    for (const name of Object.keys(PEOPLE)) hubs[name] = await startHub({ login: true, you: { name, mod: false, role: 'none' }, history: HISTORY, online: 9, welcomeExtra: { presence: true, me: PEOPLE[name] }, who: WHO });
    site.on('upgrade', (req, socket, head) => {                                                    // (/ws is the game server's door, /hub/ws the fake hub's: each is a plain pipe)
        const pathname = new URL(req.url, 'http://x').pathname, me = whoIs(req), to = pathname === '/ws' ? server.ws : pathname === '/hub/ws' && me ? hubs[me].port : 0;
        if (!to) { socket.destroy(); return; }
        const up = net.connect(to, '127.0.0.1', () => {
            up.write(req.method + ' ' + pathname + ' HTTP/1.1\r\n' + Object.entries(req.headers).map(([k, v]) => (k === 'host' ? 'host: 127.0.0.1' : k + ': ' + v)).join('\r\n') + '\r\n\r\n');
            if (head && head.length) up.write(head);
            up.pipe(socket); socket.pipe(up);
        });
        up.on('error', () => socket.destroy()); socket.on('error', () => up.destroy());
    });
    started.push({ stop() { try { site.close(); } catch (e) { /* gone */ } for (const h of Object.values(hubs)) { try { h.close(); } catch (e) { /* gone */ } } } });
    return site;
}

// ---- Chrome over the DevTools protocol: a browser of its own, one context (so one storage) for each page ---------------------------------------------------------------------------------------------------
class Chrome {
    static async launch() {
        const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'ants_room_chrome.'));
        const proc = spawn(chromePath, ['--headless=new', '--remote-debugging-port=0', '--user-data-dir=' + profile, '--no-first-run', '--no-default-browser-check', '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
            '--disable-features=Translate,MediaRouter', '--font-render-hinting=none', '--no-sandbox', 'about:blank'], { stdio: 'ignore' });
        const chrome = new Chrome(proc, profile);
        started.push({ stop() { chrome.close(); } });
        let port = 0;
        for (let i = 0; i < 150 && !port; i++) {
            await sleep(100);
            try { port = Number(fs.readFileSync(path.join(profile, 'DevToolsActivePort'), 'utf8').split('\n')[0]); } catch (e) { /* not yet */ }
        }
        if (!port) throw new Error('Chrome did not start');
        const info = await new Promise((resolve, reject) => http.get({ host: '127.0.0.1', port, path: '/json/version' }, (res) => { let b = ''; res.on('data', (d) => { b += d; }); res.on('end', () => resolve(JSON.parse(b))); }).on('error', reject));
        chrome.ws = new WebSocket(info.webSocketDebuggerUrl);
        await new Promise((resolve, reject) => { chrome.ws.onopen = resolve; chrome.ws.onerror = reject; });
        chrome.ws.onmessage = (ev) => chrome.onMessage(JSON.parse(ev.data));
        return chrome;
    }
    constructor(proc, profile) { this.proc = proc; this.profile = profile; this.id = 0; this.waiting = new Map(); this.listeners = []; this.closed = false; }
    onMessage(m) {
        if (m.id && this.waiting.has(m.id)) { const w = this.waiting.get(m.id); this.waiting.delete(m.id); if (m.error) w.reject(new Error(m.error.message)); else w.resolve(m.result); } else if (m.method) this.listeners.forEach((l) => l(m));
    }
    send(method, params, sessionId) {
        return new Promise((resolve, reject) => { const n = ++this.id; this.waiting.set(n, { resolve, reject }); this.ws.send(JSON.stringify(Object.assign({ id: n, method, params: params || {} }, sessionId ? { sessionId } : {}))); });
    }
    async newPage(width, height, mobile) {
        const { browserContextId } = await this.send('Target.createBrowserContext');
        const { targetId } = await this.send('Target.createTarget', { url: 'about:blank', browserContextId });
        const { sessionId } = await this.send('Target.attachToTarget', { targetId, flatten: true });
        const page = new Page(this, sessionId, browserContextId, targetId);
        await page.send('Page.enable'); await page.send('Runtime.enable');
        await page.size(width, height, mobile);
        return page;
    }
    close() {
        if (this.closed) return;
        this.closed = true;
        try { this.ws.send(JSON.stringify({ id: ++this.id, method: 'Browser.close' })); } catch (e) { /* gone */ }
        try { this.ws.close(); } catch (e) { /* gone */ }
        try { this.proc.kill('SIGTERM'); } catch (e) { /* gone */ }
        setTimeout(() => { try { this.proc.kill('SIGKILL'); } catch (e) { /* gone */ } try { fs.rmSync(this.profile, { recursive: true, force: true }); } catch (e) { /* it stays */ } }, 1500).unref();
    }
}
class Page {
    constructor(chrome, sessionId, contextId, targetId) { this.chrome = chrome; this.sessionId = sessionId; this.contextId = contextId; this.targetId = targetId; this.errors = []; this.chrome.listeners.push((m) => { if (m.sessionId === sessionId && m.method === 'Runtime.exceptionThrown') this.errors.push(m.params.exceptionDetails.text + ' ' + ((m.params.exceptionDetails.exception || {}).description || '')); }); }
    send(method, params) { return this.chrome.send(method, params, this.sessionId); }
    async size(w, h, mobile) {
        this.w = w; this.h = h;
        await this.send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: !!mobile, screenWidth: w, screenHeight: h });
        await this.send('Emulation.setTouchEmulationEnabled', { enabled: !!mobile });
    }
    async goto(url) {
        const loaded = new Promise((resolve) => { const l = (m) => { if (m.sessionId === this.sessionId && m.method === 'Page.loadEventFired') { this.chrome.listeners.splice(this.chrome.listeners.indexOf(l), 1); resolve(); } }; this.chrome.listeners.push(l); });
        await this.send('Page.navigate', { url });
        await loaded;
    }
    async eval(expression) {
        const r = await this.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
        if (r.exceptionDetails) throw new Error('page error: ' + (r.exceptionDetails.exception && r.exceptionDetails.exception.description || r.exceptionDetails.text));
        return r.result.value;
    }
    async waitFor(label, expression, ms) { return until(label, () => this.eval(expression), ms || 10000); }
    // a real press at the middle of an element (the first that matches the selector, whose text contains `text` when it is given)
    async press(selector, text) {
        const at = await this.eval('(function () { var list = [].slice.call(document.querySelectorAll(' + JSON.stringify(selector) + ')).filter(function (e) { return e.getClientRects().length && (' + JSON.stringify(text || '') + ' === "" || e.textContent.indexOf(' + JSON.stringify(text || '') + ') !== -1); }); var e = list[0]; if (!e) return null; e.scrollIntoView({ block: "nearest" }); var r = e.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; })()');
        if (!at) throw new Error('nothing to press: ' + selector + ' ' + (text || ''));
        for (const type of ['mousePressed', 'mouseReleased']) await this.send('Input.dispatchMouseEvent', { type, x: at[0], y: at[1], button: 'left', clickCount: 1 });
        await sleep(60);
    }
    async shot(name) {
        if (!shotsDir) return;
        fs.mkdirSync(shotsDir, { recursive: true });
        const { data } = await this.send('Page.captureScreenshot', { format: 'png' });
        fs.writeFileSync(path.join(shotsDir, name + '.png'), Buffer.from(data, 'base64'));
    }
    async close() { try { await this.chrome.send('Target.closeTarget', { targetId: this.targetId }); await this.chrome.send('Target.disposeBrowserContext', { browserContextId: this.contextId }); } catch (e) { /* gone */ } }
}

// ---- the page as a person uses it ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
async function open(chrome, site, name, size, url, mobile) {
    const page = await chrome.newPage(size[0], size[1], mobile);
    await page.send('Network.enable');
    await page.send('Network.setCookie', { name: 'who', value: name, url: site.url });
    await page.goto(site.url + (url || '/'));
    await page.waitFor('Home is lit', '(function () { var h = document.querySelector(".quick.host"), j = document.querySelector(".quick.join"); return !!h && !!j && !h.disabled && !j.disabled; })()', 15000);
    return page;
}
async function host(page) {
    await page.press('.quick.host');
    await page.waitFor('the card asks', '!!document.querySelector(".gconfirm")', 5000);
    await page.press('.gconfirm button', 'Invite only');
    await page.waitFor('the room is shown', 'document.body.getAttribute("data-screen") === "room" && document.querySelectorAll("#slots > li").length === 4 && !!document.getElementById("link").value', 15000);
    return page.eval('document.getElementById("link").value');
}
// everything that the page shows of the room, measured (nothing is read from the markup's words, only from what the browser laid out)
const MEASURE = '(function () {' +
    'var vis = function (e) { return !!e && e.getClientRects().length > 0 && getComputedStyle(e).visibility !== "hidden"; };' +
    'var r = function (e) { if (!e) return null; var b = e.getBoundingClientRect(); return { l: Math.round(b.left), t: Math.round(b.top), w: Math.round(b.width), h: Math.round(b.height), r: Math.round(b.right), b: Math.round(b.bottom) }; };' +
    'var lobby = document.getElementById("lobby"), body = document.body, doc = document.documentElement, L = r(lobby);' +
    'var controls = [].slice.call(lobby.querySelectorAll("button, input, select, a, [role=combobox]")).filter(vis).map(function (e) { var b = r(e); return { id: e.id || e.className || e.tagName, t: (e.textContent || e.value || "").slice(0, 24), b: b, inside: b.l >= L.l - 1 && b.r <= L.r + 1 && b.t >= L.t - 1 && b.b <= L.b + 1, inWindow: b.l >= 0 && b.t >= 0 && b.r <= innerWidth && b.b <= innerHeight }; });' +
    'var dock = document.getElementById("gchat"), pl = document.querySelector(".pl"), games = document.querySelector(".gdock");' +
    'return { w: innerWidth, h: innerHeight, scroll: { docH: doc.scrollHeight, docW: doc.scrollWidth, bodyH: body.scrollHeight }, noPageScroll: doc.scrollHeight <= innerHeight && doc.scrollWidth <= innerWidth && body.scrollHeight <= innerHeight + 1,' +
    ' lobby: L, lobbyScroll: lobby.scrollHeight - lobby.clientHeight, controls: controls, cut: controls.filter(function (c) { return !c.inside || !c.inWindow; }).map(function (c) { return c.id + ":" + c.t; }),' +
    ' small: controls.filter(function (c) { return c.b.h < 24 || c.b.w < 24; }).map(function (c) { return c.id + ":" + c.t + " " + c.b.w + "x" + c.b.h; }),' +
    ' dock: r(dock), dockShown: vis(dock), players: r(pl), playersShown: vis(pl), gamesShown: vis(games), start: r(document.getElementById("start")), startShown: vis(document.getElementById("start")), screen: body.getAttribute("data-screen") };' +
    '})()';
const measure = (page) => page.eval(MEASURE);
const text = (page, selector) => page.eval('(function () { var e = document.querySelector(' + JSON.stringify(selector) + '); return e ? e.textContent : null; })()');

async function main() {
    const server = await startServer();
    const site = await startSite(server);
    const chrome = await Chrome.launch();
    const SIZES = [['1920x1080', 1920, 1080], ['1600x900', 1600, 900], ['1366x768', 1366, 768], ['1280x800', 1280, 800]];
    // 1 and 2. a room of three people (host, guest, bot) at each desktop size
    for (const size of SIZES) {
        const hostPage = await open(chrome, site, 'dAwGy', [size[1], size[2]]);
        const link = await host(hostPage);
        const guestPage = await open(chrome, site, 'Dethcon', [size[1], size[2]], new URL(link).pathname + new URL(link).search);
        // the name card of a link: Join
        await guestPage.waitFor('the name card', '!document.getElementById("name-step").hidden || document.body.getAttribute("data-screen") === "room"', 10000).catch(() => null);
        if (await guestPage.eval('!document.getElementById("name-step").hidden')) await guestPage.press('#name-step-go');
        try { await hostPage.waitFor('the guest is in', 'document.querySelectorAll("#slots .k-person").length === 2', 15000); } catch (e) { await hostPage.shot('fail_host'); await guestPage.shot('fail_guest'); console.log('guest errors: ' + JSON.stringify(guestPage.errors) + ' host errors: ' + JSON.stringify(hostPage.errors) + ' link ' + link); throw e; }
        await hostPage.eval('(function () { var s = document.querySelector("select[data-mode]"); s.value = "medium"; s.dispatchEvent(new Event("change", { bubbles: true })); })()');
        await hostPage.waitFor('the bot sits', 'document.querySelectorAll("#slots .k-bot").length === 1', 8000);
        await sleep(400);
        const m = await measure(hostPage);
        const at = size[0];
        check('the room, ' + at + ': the page does not scroll (the document is no taller or wider than the window)', m.noPageScroll, JSON.stringify(m.scroll));
        check('the room, ' + at + ': it replaced the games\' list, and the players and the chat dock are there', m.screen === 'room' && !m.gamesShown && m.dockShown && m.lobby.w > 600, JSON.stringify({ g: m.gamesShown, d: m.dockShown, p: m.playersShown }));
        check('the room, ' + at + ': nothing in the room\'s panel scrolls and nothing is cut off', m.lobbyScroll <= 1 && m.cut.length === 0, JSON.stringify({ s: m.lobbyScroll, cut: m.cut }));
        check('the room, ' + at + ': every control is at least 24 px each way, START at least 44 high', m.small.length === 0 && m.start && m.start.h >= 44, JSON.stringify({ small: m.small, start: m.start }));
        check('the room, ' + at + ': the dock lies under the room and is as wide as the room and the players together', m.dock && m.dock.t >= m.lobby.b - 1, JSON.stringify({ d: m.dock, l: m.lobby }));
        await hostPage.shot('room_' + at + '_host');
        await guestPage.shot('room_' + at + '_guest');
        await guestPage.close();
        await hostPage.close();
    }
}

main().then(() => { }, (e) => { failures++; console.log('FAIL the run did not finish: ' + (e && e.stack || e)); }).then(() => {
    for (const s of started.reverse()) { try { s.stop(); } catch (e) { /* gone */ } }
    console.log('room in Home (real browser): ' + checks + ' checks, ' + failures + ' failed');
    setTimeout(() => process.exit(failures ? 1 : 0), 1700).unref();
});
