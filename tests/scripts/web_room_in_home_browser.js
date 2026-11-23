// The ROOM INSIDE HOME in a REAL browser (Chrome, driven over its DevTools protocol with no package) against a REAL ants_server, behind a small stand-in for the site: the files of web/, /ws to the server's WebSocket door, a fake chat
// hub at /hub/ws (tests/scripts/fake_hub.js) and the small answers of the page (docs/HOME_SCREEN.md "The Room", docs/PLAY_IN_BROWSER.md "Your room", docs/READY_UP.md). Opt-in, like the other browser checks: it needs a Chrome that it can start
// (CHROME=/path/to/chrome, or the one of a Mac) and a built ants_server; without them it says so and exits 0 (nothing was checked). It starts its own browser with a profile of its own and closes it, the server and the stand-in before it ends,
// also after a failure.
//   1. at 1920 x 1080, 1600 x 900, 1366 x 768 and 1280 x 800: a host makes a room (Quick host, Invite only), a guest joins by the link, a Medium bot sits in the third color: the page does not scroll (the document is no taller or wider than
//      the window), the room panel holds everything (nothing inside it scrolls, nothing is cut off), every control of the room is in the window, and the chat dock is under it with the room's tab: a line of the room is in the room's tab, a line of
//      Home's chat in the other; the games' list is not drawn, the players' list is. The Rules sheet: for the host it is in the window with Done reachable, the page behind it takes no press and the switches work (who can join, Fog of War,
//      Direct connection); for the guest it shows what the host set and takes no press on the first two; Done gives the focus back to the Rules button. The room with Fog of War on fits too, and START is 44 px high;
//   2. Ready: the guest's big button is their READY, the host has START; START of the host is off with the line that names whose Ready it waits for until the guest presses READY, then it is on and the line says that everyone is ready; the host
//      changes the map and the marks stay; the guest takes the mark back and START is off again; the bot and the host show Ready by themselves; a guest's START says that it waits for the host;
//   3. a window lower than the room needs (1366 x 650): the page does not scroll, the room's panel scrolls inside itself and START stays in view;
//   4. the phone layout (up to 700 px wide or up to 500 px high): upright (390 x 844 and 390 x 664), held sideways (844 x 390, 932 x 430) and a wide, low window (1366 x 450): the strip has Room, Players and Chat; the Room view fits or scrolls
//      inside itself with START at its end above the strip; every button of the room is 44 px each way (36 held sideways); held sideways the whole row of an open color is the target of its menu, and the menu is in the window with five choices of
//      44 px; the Rules sheet is in the window with Done reachable; the Chat view has the two tabs and a field in the window, the Players view has the list, no handle of the splitters is drawn;
//   4b. a phone upright has tiles (docs/HOME_SCREEN.md "The Room"): the room fits with no scrolling at 844 and at 664 px high, START in the window; the bar is one row (no Quick host, no Quick join); the four colors are tiles two by two,
//      each 56 px or more and one target for the leader (a press at its middle reaches its button), with no dots, Remove, Team buttons or drop-down; a guest's tiles have no button and open nothing; the leader's sheet is in the window with Done
//      reachable and every target 44 px or more, and what it does reaches the real server: a team pair (the tiles show it on both screens), a color moved (the guest's screen follows), a computer player and Closed chosen, Remove asking first.
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
const PEOPLE = { Hazel: 2, Wren: 1 };               // the two names of the room (a cookie tells the stand-in who a browser is: the login of the fake hub)

let checks = 0, failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }
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
        '--demo-rooms', '2', '--demo-map', 'TREASURE.LVL', '--demo-maps', 'TINY.LVL,SMALL.LVL,MEDIUM.LVL,GAUNTLET.LVL,TREASURE.LVL,ISLANDS.LVL', '--demo-lobbies', '30'], { env: Object.assign({}, process.env, { ANTS_SERVER_SECRET: 'room-browser-secret' }), stdio: 'ignore' });
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
    await page.waitFor('the room is shown', 'document.body.getAttribute("data-screen") === "room" && document.querySelectorAll("#slots > li").length === 4 && document.getElementById("code").textContent.length > 0', 15000);
    return page.eval('location.origin + "/?room=" + document.getElementById("code").textContent.replace(/\\s/g, "")');       // (the link that Share gives: there is no field of it on the page)
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
const SHEET = '(function () { var b = document.getElementById("rules-box").getBoundingClientRect(), d = document.getElementById("rules-done").getBoundingClientRect(), shown = !document.getElementById("rules").hidden && b.width > 0;' +
    ' var sw = function (id) { var e = document.getElementById(id); return e ? { off: !!e.disabled, on: e.getAttribute("aria-checked") === "true", w: Math.round(e.getBoundingClientRect().width), h: Math.round(e.getBoundingClientRect().height) } : null; };' +
    ' return { shown: shown, fits: b.left >= 0 && b.right <= innerWidth + 0.5 && b.top >= 0 && b.bottom <= innerHeight + 0.5, done: d.width > 0 && d.bottom <= innerHeight && d.top >= 0, noScroll: document.documentElement.scrollWidth <= innerWidth, vis: [].map.call(document.querySelectorAll(".vbtn"), function (e) { return { text: e.textContent, off: !!e.disabled, on: e.getAttribute("aria-pressed") === "true" }; }), fog: sw("fg-switch"), direct: sw("dr-switch"), state: document.getElementById("rules-state").textContent, pageInert: document.getElementById("lobby").hasAttribute("inert") }; })()';
// the Rules sheet as the browser laid it out (it is a card over the page; a sheet from the bottom on a phone)
const text = (page, selector) => page.eval('(function () { var e = document.querySelector(' + JSON.stringify(selector) + '); return e ? e.textContent : null; })()');

// a room of a host (Hazel), a guest who joined by the link (Wren) and, when asked, a Medium computer player in the third color
async function makeRoom(chrome, site, w, h, bot, mobile) {
    const hostPage = await open(chrome, site, 'Hazel', [w, h], '/', mobile);
    const link = await host(hostPage);
    const guestPage = await open(chrome, site, 'Wren', [w, h], new URL(link).pathname + new URL(link).search, mobile);
    await guestPage.waitFor('the name card', '!document.getElementById("name-step").hidden || document.body.getAttribute("data-screen") === "room"', 10000).catch(() => null);
    if (await guestPage.eval('!document.getElementById("name-step").hidden')) await guestPage.press('#name-step-go');
    try { await hostPage.waitFor('the guest is in', 'document.querySelectorAll("#slots .k-person").length === 2', 15000); } catch (e) { await hostPage.shot('fail_host'); await guestPage.shot('fail_guest'); console.log('guest errors: ' + JSON.stringify(guestPage.errors)); throw e; }
    if (bot) {
        if (w <= 700 && h >= 501) {                                       // (a phone upright: the colors are tiles, and the leader's sheet has the choices)
            await hostPage.press('#slots .slot.k-open .tap');
            await hostPage.press('.ss-item', 'Medium bot');
            await hostPage.press('.ss-back .btn', 'Done');
        } else {
            await hostPage.press('.mode');
            await hostPage.press('.sm-item', 'Medium bot');
        }
        await hostPage.waitFor('the bot sits', 'document.querySelectorAll("#slots .k-bot").length === 1', 8000);
    }
    for (const page of [hostPage, guestPage]) await page.waitFor('the page\'s toast has gone (it is the picture\'s business: a toast hides the chat)', '(function () { return [".gtoast", "#toast"].every(function (q) { var t = document.querySelector(q); return !t || t.hidden; }); })()', 12000);
    await sleep(300);
    return { hostPage, guestPage, link };
}
const chips = (page) => page.eval('[].map.call(document.querySelectorAll("#slots .rdy-chip"), function (e) { return e.textContent + (e.className.indexOf(" bot") !== -1 ? " (bot)" : ""); }).sort()');
const readyLine = (page) => page.eval('document.getElementById("go-status").textContent');
const startState = (page) => page.eval('(function () { var e = document.getElementById("start"); return { off: !!e.disabled || e.getAttribute("aria-disabled") === "true", text: e.textContent }; })()');
const readySwitch = (page) => page.eval('(function () { var e = document.getElementById("start"); return e.getAttribute("aria-pressed") === null ? null : { on: e.getAttribute("aria-pressed") === "true", text: e.textContent }; })()');       // (a guest's big button is their Ready, a switch; the host's is START, no switch)

// a phone upright (docs/HOME_SCREEN.md "The Room"): the bar, the four tiles and what a press at the middle of each reaches, and the leader's sheet for a color, as the browser laid them out
const TILES = '(function () {' +
    'var box = function (e) { var b = e.getBoundingClientRect(); return { l: Math.round(b.left), t: Math.round(b.top), w: Math.round(b.width), h: Math.round(b.height) }; };' +
    'var vis = function (q) { var e = document.querySelector(q); return !!e && e.getClientRects().length > 0; };' +
    'var slots = [].slice.call(document.querySelectorAll("#slots .slot"));' +
    'return { bar: box(document.querySelector(".hbar")), quick: vis(".hbar .quick.host") || vis(".hbar .quick.join"), hist: vis(".hbar .quick.hist"),' +
    ' tiles: slots.map(function (li) { var b = box(li), e = document.elementFromPoint(b.l + b.w / 2, b.t + b.h / 2); return { b: b, tap: !!e && !!e.closest(".tap"), inside: !!e && !!e.closest(".slot") }; }),' +
    ' rows: document.querySelectorAll("#slots .grip, #slots .rm, #slots .teamset, #slots .mode, #slots .rdy-chip").length, taps: document.querySelectorAll("#slots .tap").length,' +
    ' start: box(document.getElementById("start")), strip: box(document.querySelector(".htabs")) };' +
    '})()';
const SEAT_SHEET = '(function () {' +
    'var back = document.querySelector(".ss-back"), box = document.querySelector(".ss");' +
    'if (!back || back.hidden || !box.getClientRects().length) return { shown: false };' +
    'var r = box.getBoundingClientRect(), done = [].filter.call(box.querySelectorAll(".btn"), function (e) { return e.textContent === "Done"; })[0], d = done.getBoundingClientRect();' +
    'var small = [].filter.call(box.querySelectorAll("button"), function (e) { var b = e.getBoundingClientRect(); return b.width > 0 && (b.width < 44 || b.height < 44); }).map(function (e) { var b = e.getBoundingClientRect(); return e.className + " " + Math.round(b.width) + "x" + Math.round(b.height); });' +
    'return { shown: true, fits: r.left >= 0 && r.right <= innerWidth + .5 && r.top >= 0 && r.bottom <= innerHeight + .5, done: d.top >= r.top && d.bottom <= r.bottom + .5, noScroll: document.documentElement.scrollWidth <= innerWidth && box.scrollHeight <= box.clientHeight + 1,' +
    ' inert: document.getElementById("lobby").hasAttribute("inert"), small: small, title: box.querySelector("h3").textContent, sub: box.querySelector(".ss-sub").textContent, keys: [].map.call(box.querySelectorAll("[data-key]"), function (e) { return e.getAttribute("data-key"); }) };' +
    '})()';
async function tilesBlock(page, guestPage, at, phone) {
    const L = 'phone ' + at + ': ';
    const sheet = () => page.eval(SEAT_SHEET);
    const seatSheetShown = async () => (await sheet()).shown;
    const m = await page.eval(TILES);
    check(L + 'while the room is held the bar is one row: no Quick host, no Quick join, Match history is there', !m.quick && m.hist && m.bar.h <= 80, JSON.stringify({ quick: m.quick, hist: m.hist, bar: m.bar }));
    check(L + 'four tiles, two by two (two columns and two rows), each 56 px high or more and wide enough for a name', m.tiles.length === 4 && new Set(m.tiles.map((t) => t.b.l)).size === 2 && new Set(m.tiles.map((t) => t.b.t)).size === 2 && m.tiles.every((t) => t.b.h >= 56 && t.b.w >= 140), JSON.stringify(m.tiles.map((t) => t.b)));
    check(L + 'a press at the middle of any tile of the leader reaches its button: the whole tile is the one target', m.tiles.every((t) => t.tap), JSON.stringify(m.tiles.map((t) => t.tap)));
    check(L + 'no dots, no Remove, no Team buttons, no drop-down and no Ready pill on a tile; one button for each tile', m.rows === 0 && m.taps === 4, JSON.stringify({ rows: m.rows, taps: m.taps }));
    check(L + 'START is in the window above the strip and 44 px high or more', m.start.t >= 0 && m.start.t + m.start.h <= m.strip.t + 1 && m.start.h >= 44, JSON.stringify({ start: m.start, strip: m.strip }));
    await page.shot('phone_' + at + '_tiles_leader');
    const g = await guestPage.eval(TILES);
    check(L + 'a guest\'s tiles are pictures: four of them, no button over them, nothing of the rows', g.taps === 0 && g.rows === 0 && g.tiles.length === 4 && g.tiles.every((t) => !t.tap && t.inside), JSON.stringify({ taps: g.taps, rows: g.rows }));
    await guestPage.press('#slots .slot.k-person');
    await sleep(200);
    check(L + 'a press on a guest\'s tile opens no sheet', !(await guestPage.eval(SEAT_SHEET)).shown);
    await guestPage.shot('phone_' + at + '_tiles_guest');
    // the sheet of a person (the guest, Wren, in Red: seat 1) is in the window with every target a finger's size
    await page.press('.tap[data-tap="1"]');
    await page.waitFor('the sheet opens', 'document.querySelector(".ss-back") && !document.querySelector(".ss-back").hidden', 3000);
    const ss = await sheet();
    check(L + 'the sheet of a person is in the window with Done reachable, nothing scrolls, the page behind it takes no press, and every target is 44 px or more', ss.shown && ss.fits && ss.done && ss.noScroll && ss.inert && ss.small.length === 0, JSON.stringify(ss));
    check(L + 'it names the person and the color, and has the team, the color and the Remove rows', ss.title === 'Wren' && /^Red/.test(ss.sub) && ['team:1', 'team:2', 'color:0', 'color:1', 'color:2', 'color:3', 'rm', 'done'].every((k) => ss.keys.indexOf(k) !== -1), JSON.stringify({ title: ss.title, sub: ss.sub, keys: ss.keys }));
    await page.shot('phone_' + at + '_sheet_player');
    // a team pair (Hazel and Wren on Team 1): the server holds it, and both screens show the tags
    await page.press('.ss-tb', 'Team 1');
    await sleep(150);
    await page.press('.ss-back .btn', 'Done');
    await page.waitFor('the sheet is away', '!document.querySelector(".ss-back") || document.querySelector(".ss-back").hidden', 3000);
    check(L + 'Done puts the sheet away, lets the page behind go, and the focus is on the tile of the color', await page.eval('!document.getElementById("lobby").hasAttribute("inert") && document.activeElement === document.querySelector(".tap[data-tap=\\"1\\"]")'));
    await page.press('.tap[data-tap="0"]');
    await page.press('.ss-tb', 'Team 1');
    await page.waitFor('the pair is in the room', 'document.querySelectorAll("#slots .tteam.t1").length === 2', 8000);
    await guestPage.waitFor('the guest sees the pair too', 'document.querySelectorAll("#slots .tteam.t1").length === 2', 8000);
    check(L + 'a team pair set in the sheets is the room\'s: both screens show Team 1 on the two colors, and the sheet shows its own lit', await page.eval('document.querySelector(".ss-tb[data-key=\\"team:1\\"]").getAttribute("aria-pressed") === "true"'));
    await page.shot('phone_' + at + '_tiles_teams');
    await page.press('.ss-back .btn', 'Done');
    // a color moved: Wren goes to Blue (seat 2), the guest's screen follows, and the sheet goes with Wren
    await page.press('.tap[data-tap="1"]');
    await page.press('.ss-cb[data-c="blue"]');
    await guestPage.waitFor('the guest is in Blue', 'document.querySelector("#slots .slot[data-c=blue] .you") !== null', 8000);
    await page.waitFor('the sheet went with Wren', '(function () { var h = document.querySelector(".ss h3"), s = document.querySelector(".ss-sub"); return !!h && h.textContent === "Wren" && /^Blue/.test(s.textContent); })()', 5000);
    check(L + 'a color moved in the sheet is moved in the room (the guest\'s screen is in Blue), and the sheet is still open, for Blue now', await seatSheetShown() && (await page.eval('document.querySelector(".ss-cb[aria-pressed=\\"true\\"]").getAttribute("data-c")')) === 'blue');
    await page.press('.ss-back .btn', 'Done');
    check(L + 'Done gives the focus to the tile of Blue, where Wren is now', await page.eval('document.activeElement === document.querySelector(".tap[data-tap=\\"2\\"]")'));
    // the color that Wren left is open: its sheet has the five choices (a computer player, Closed, Open again)
    await page.press('.tap[data-tap="1"]');
    const os = await sheet();
    check(L + 'the sheet of an open color is in the window with the five choices of 44 px or more, and no team, color or Remove row', os.shown && os.fits && os.done && os.small.length === 0 && os.keys.filter((k) => /^mode:/.test(k)).length === 5 && os.keys.indexOf('rm') === -1 && os.keys.indexOf('team:1') === -1, JSON.stringify(os));
    await page.shot('phone_' + at + '_sheet_open_color');
    await page.press('.ss-item', 'Easy bot');
    await page.waitFor('the bot sits in Red', 'document.querySelectorAll("#slots .k-bot").length === 2', 8000);
    await page.waitFor('the sheet says who it is', '/Easy/.test(document.querySelector(".ss h3").textContent)', 5000);
    await page.press('.ss-item', 'Closed');
    await page.waitFor('Red is closed', 'document.querySelectorAll("#slots .k-nobody").length === 1', 8000);
    check(L + 'Easy bot and Closed chosen in the sheet are the room\'s: the tiles show a second computer player and a closed color, and the sheet says Closed', await page.eval('document.querySelector(".ss h3").textContent === "Closed" && document.querySelector(".ss-item[data-word=\\"nobody\\"]").getAttribute("aria-checked") === "true"'));
    await page.press('.ss-item', 'Open');
    await page.waitFor('Red is open again', 'document.querySelectorAll("#slots .k-nobody").length === 0 && document.querySelectorAll("#slots .k-bot").length === 1', 8000);
    await page.press('.ss-back .btn', 'Done');
    // Remove asks first
    await page.press('.tap[data-tap="2"]');
    await page.press('.ss-remove');
    await page.waitFor('the question is asked', '!!document.querySelector(".ss-ask")', 3000);
    check(L + 'Remove asks first: the question names Wren, nobody has left, and the focus is on Keep', await page.eval('document.querySelector(".ss-ask p").textContent === "Remove Wren from the room?" && document.querySelectorAll("#slots .k-person").length === 2 && document.activeElement === document.querySelector(".ss-keep")'));
    await page.shot('phone_' + at + '_sheet_remove');
    await page.press('.ss-keep');
    await sleep(300);
    check(L + 'Keep puts the question away and Wren is still in the room', await page.eval('!document.querySelector(".ss-ask") && document.querySelectorAll("#slots .k-person").length === 2'));
    await page.press('.ss-remove');
    await page.press('.ss-yes');
    await page.waitFor('Wren is out of the room', 'document.querySelectorAll("#slots .k-person").length === 1', 8000);
    await page.waitFor('the sheet is away', '!document.querySelector(".ss-back") || document.querySelector(".ss-back").hidden', 3000);
    check(L + 'Remove, then Remove, takes the person out of the room, and the sheet goes with them (the page behind is let go)', !(await seatSheetShown()) && await page.eval('!document.getElementById("lobby").hasAttribute("inert")'));
}

async function main() {
    const server = await startServer();
    const site = await startSite(server);
    const chrome = await Chrome.launch();
    const SIZES = [['1920x1080', 1920, 1080], ['1600x900', 1600, 900], ['1366x768', 1366, 768], ['1280x800', 1280, 800]];
    // 1. a room of three people (host, guest, bot) at each desktop size
    for (const size of SIZES) {
        const { hostPage, guestPage } = await makeRoom(chrome, site, size[1], size[2], true);
        const m = await measure(hostPage);
        const at = size[0];
        check('the room, ' + at + ': the page does not scroll (the document is no taller or wider than the window)', m.noPageScroll, JSON.stringify(m.scroll));
        check('the room, ' + at + ': it replaced the games\' list, and the players and the chat dock are there', m.screen === 'room' && !m.gamesShown && m.dockShown && m.lobby.w > 600, JSON.stringify({ g: m.gamesShown, d: m.dockShown, p: m.playersShown }));
        check('the room, ' + at + ': nothing in the room\'s panel scrolls and nothing is cut off', m.lobbyScroll <= 1 && m.cut.length === 0, JSON.stringify({ s: m.lobbyScroll, cut: m.cut }));
        check('the room, ' + at + ': every control is at least 24 px each way, START at least 44 high', m.small.length === 0 && m.start && m.start.h >= 44, JSON.stringify({ small: m.small, start: m.start }));
        check('the room, ' + at + ': the dock lies under the room and is as wide as the room and the players together', m.dock && m.dock.t >= m.lobby.b - 1, JSON.stringify({ d: m.dock, l: m.lobby }));
        // the chat box has the two tabs; a line of the room is a dot on the Room tab, the line of Home's chat shows in its own tab
        const tabs = await hostPage.eval('[].map.call(document.querySelectorAll(".rc-tab"), function (e) { return e.textContent; })');
        check('the room, ' + at + ': the chat box has two tabs, Ants & Uncles and Room', JSON.stringify(tabs) === JSON.stringify(['Ants & Uncles', 'Room']), JSON.stringify(tabs));
        await hostPage.shot('room_' + at + '_host');
        await guestPage.shot('room_' + at + '_guest');
        // The Rules sheet (docs/PLAY_IN_BROWSER.md "Your room"): a card over the page that holds who can join, Fog of War and Direct connection. The host's controls work; a guest reads them, and Direct connection is theirs.
        await hostPage.press('#rules-open');
        await hostPage.waitFor('the sheet opens', '!document.getElementById("rules").hidden', 3000);
        const hs = await hostPage.eval(SHEET);
        check('the Rules sheet, ' + at + ', the host: it is in the window with Done reachable, the page behind it takes no press, the switches work (who can join, Fog of War, Direct connection)', hs.shown && hs.fits && hs.done && hs.noScroll && hs.pageInert && hs.vis.length === 2 && hs.vis.every((v) => !v.off) && hs.fog && !hs.fog.off && hs.direct && !hs.direct.off && hs.fog.w >= 40 && hs.fog.h >= 24, JSON.stringify(hs));
        check('the Rules sheet, ' + at + ': the host\'s room is invite only (the room was made so), and the button says so', hs.vis.find((v) => v.text === 'Invite only').on === true && /Invite only/.test(hs.state), JSON.stringify(hs.state));
        await hostPage.press('.vbtn', 'Public');
        await hostPage.waitFor('the room is public', '/^Public/.test(document.getElementById("rules-state").textContent)', 5000);
        check('the Rules sheet, ' + at + ': Public makes the room public (the Rules button says Public, Public is the lit button)', (await hostPage.eval(SHEET)).vis.find((v) => v.text === 'Public').on === true);
        await hostPage.shot('rules_' + at + '_host');
        await hostPage.press('#fg-switch');
        await hostPage.waitFor('the question is asked', '(function () { var t = document.querySelector(".gtoast"); return !!t && !t.hidden && /Turn fog on/.test(t.textContent); })()', 6000);
        await hostPage.eval('(function () { document.querySelector(".gtoast .btn").click(); })()');
        await hostPage.waitFor('fog is on and the bot is open', 'document.getElementById("fg-switch").getAttribute("aria-checked") === "true" && document.querySelectorAll("#slots .k-bot").length === 0', 8000);
        await guestPage.press('#rules-open');
        await guestPage.waitFor('the sheet opens', '!document.getElementById("rules").hidden', 3000);
        await guestPage.waitFor('the guest is told that fog is on', 'document.getElementById("fg-switch").getAttribute("aria-checked") === "true" && /Fog/.test(document.getElementById("rules-state").textContent)', 8000);
        const gs0 = await guestPage.eval(SHEET);
        check('the Rules sheet, ' + at + ', the guest: it reads the room (who can join and Fog of War show what the host set, and take no press) and Direct connection is the guest\'s own', gs0.shown && gs0.fits && gs0.done && gs0.vis.every((v) => v.off) && gs0.vis.find((v) => v.text === 'Public').on === true && gs0.fog.off && gs0.fog.on === true && !gs0.direct.off, JSON.stringify(gs0));
        await guestPage.shot('rules_' + at + '_guest');
        await hostPage.press('#rules-done');
        await guestPage.press('#rules-done');
        check('the Rules sheet, ' + at + ': Done closes it and gives the focus back to the Rules button', await hostPage.eval('document.getElementById("rules").hidden && document.activeElement === document.getElementById("rules-open") && !document.getElementById("lobby").hasAttribute("inert")'));
        for (const page of [hostPage, guestPage]) await page.waitFor('the toasts have gone', '(function () { return [".gtoast", "#toast"].every(function (q) { var t = document.querySelector(q); return !t || t.hidden; }); })()', 12000);
        await sleep(300);
        const fm = await measure(hostPage), gm = await measure(guestPage);
        for (const [who, mm2] of [['host', fm], ['guest', gm]]) {
            check('the room with Fog of War on, ' + at + ', the ' + who + ': the page does not scroll, nothing in the room scrolls, nothing is cut off', mm2.noPageScroll && mm2.lobbyScroll <= 1 && mm2.cut.length === 0, JSON.stringify({ scroll: mm2.scroll, s: mm2.lobbyScroll, cut: mm2.cut }));
        }
        check('the room with Fog of War on, ' + at + ': START is in the window and 44 px high', fm.start && fm.start.h >= 44 && gm.start && gm.start.h >= 44, JSON.stringify({ h: fm.start, g: gm.start }));
        await hostPage.shot('room_' + at + '_host_fog');
        await guestPage.shot('room_' + at + '_guest_fog');
        await hostPage.press('.rc-tab', 'Room');
        await hostPage.waitFor('the room chat shows', '!document.getElementById("chat").hidden && document.querySelector(".rc-tab[aria-selected=true]").textContent.indexOf("Room") === 0', 3000);
        const mm = await measure(hostPage);
        check('the room, ' + at + ': with the Room tab open the page still does not scroll and the room\'s chat is inside the window', mm.noPageScroll && await hostPage.eval('(function () { var r = document.getElementById("chat").getBoundingClientRect(); return r.bottom <= innerHeight && r.height > 40; })()'), JSON.stringify(mm.scroll));
        await hostPage.shot('room_' + at + '_roomtab');
        await guestPage.close();
        await hostPage.close();
    }

    // 2. Ready up (docs/READY_UP.md), at 1366 x 768
    {
        const { hostPage, guestPage } = await makeRoom(chrome, site, 1366, 768, true);
        check('ready: the host\'s big button is START (no switch), the guest\'s is their READY (a switch)', (await readySwitch(hostPage)) === null && (await readySwitch(guestPage)) !== null);
        same('ready: the marks on the host\'s screen (the host and the computer player are Ready by themselves)', await chips(hostPage), ['Not ready', 'Ready', 'Ready (bot)']);
        const s1 = await startState(hostPage);
        check('ready: START of the host is off, and the line says whose Ready it waits for', s1.off && (await readyLine(hostPage)) === 'Waiting for Wren to ready up', JSON.stringify({ s: s1, l: await readyLine(hostPage) }));
        const g1 = await startState(guestPage);
        check('ready: a guest\'s big button says READY and is on, and the line says that the host starts', !g1.off && g1.text === 'READY' && (await readyLine(guestPage)) === 'Hazel starts the match.', JSON.stringify({ s: g1, l: await readyLine(guestPage) }));
        await hostPage.shot('ready_host_waiting');
        await guestPage.shot('ready_guest_not_ready');
        // a START that the page sends too early is refused by the server all the same: the page's own button is off, so the check is in the C++ tests (tests/test_net/test_lobby_ready.cpp)
        await guestPage.press('#start');
        await hostPage.waitFor('the host sees everybody ready', 'document.getElementById("go-status").textContent === "Everyone is ready."', 5000);
        const s2 = await startState(hostPage);
        check('ready: when the guest has pressed Ready, START of the host is on and the line says so', !s2.off, JSON.stringify(s2));
        check('ready: the guest\'s READY is pressed', (await readySwitch(guestPage)).on === true);
        same('ready: the marks, all Ready', await chips(hostPage), ['Ready', 'Ready', 'Ready (bot)']);
        await hostPage.shot('ready_host_all_ready');
        await guestPage.shot('ready_guest_ready');
        // the host changes the game type: the marks stay (the maintainer's decision: people who readied stay ready)
        await hostPage.press('#gt-combo');
        await hostPage.press('.gt-opt', '187');
        await hostPage.waitFor('the game type changed', 'document.getElementById("gt-combo").textContent.indexOf("187") !== -1', 5000);
        await guestPage.waitFor('the guest sees the new game type', 'document.getElementById("gt-combo").textContent.indexOf("187") !== -1', 5000);
        await sleep(300);
        check('ready: a changed game type leaves the marks as they were', (await readySwitch(guestPage)).on === true && !(await startState(hostPage)).off && (await readyLine(hostPage)) === 'Everyone is ready.');
        // the guest takes the mark back
        await guestPage.press('#start');
        await hostPage.waitFor('START is off again', 'document.getElementById("start").disabled === true || document.getElementById("start").getAttribute("aria-disabled") === "true"', 5000);
        check('ready: when the guest takes the mark back, START is off again and the line names them', (await readyLine(hostPage)) === 'Waiting for Wren to ready up' && (await readySwitch(guestPage)).on === false);
        // the games' list and the room: while the page holds a room there is no games' list: the Menu's Home, Back and Quick host show the room; Leave asks, then brings the games' list back
        const visWas = await hostPage.eval('document.querySelector(".vbtn[aria-pressed=\\"true\\"]").textContent');
        await hostPage.press('.hmenu');
        await hostPage.press('.hmenubox .mitem', 'Home');
        await sleep(300);
        const stays = await measure(hostPage);
        check('games and room: the Menu\'s Home leaves the room up (there is no games\' list for a person who holds a room), and the page does not scroll', stays.noPageScroll && !stays.gamesShown && await hostPage.eval('document.body.getAttribute("data-screen") === "room" && document.getElementById("lobby").getClientRects().length > 0'));
        await hostPage.eval('history.back()');
        await sleep(400);
        check('... Back shows the room too, at /#room, and the room is still the host\'s', await hostPage.eval('document.body.getAttribute("data-screen") === "room" && location.hash === "#room"') && (await startState(hostPage)).text === 'START!');
        await hostPage.press('.quick.host');
        await sleep(300);
        check('... and so does Quick host: no question, who can join is as it was', await hostPage.eval('document.body.getAttribute("data-screen") === "room" && !document.querySelector(".gconfirm")') && (await hostPage.eval('document.querySelector(".vbtn[aria-pressed=\\"true\\"]").textContent')) === visWas);
        await hostPage.press('.leave');
        await hostPage.waitFor('the card asks', '!!document.querySelector(".gconfirm")', 3000);
        await hostPage.press('.gconfirm button', 'Leave room');
        await hostPage.waitFor('the games\' list is back', 'document.body.getAttribute("data-screen") === "home"', 5000);
        check('leaving the room (the question first, as with another person in it) shows the games\' list', (await measure(hostPage)).gamesShown);
        await guestPage.close();
        await hostPage.close();
    }

    // 3. a window lower than the room needs: the page does not move, the room's panel scrolls inside itself, START stays in view
    {
        const { hostPage, guestPage } = await makeRoom(chrome, site, 1366, 650, true);
        const m = await measure(hostPage);
        check('a low window (1366 x 650): the page does not scroll', m.noPageScroll, JSON.stringify(m.scroll));
        check('a low window: the room\'s panel scrolls inside itself, and START is in the window', m.lobbyScroll > 0 && m.startShown && m.start.b <= m.h && m.start.t >= 0, JSON.stringify({ s: m.lobbyScroll, start: m.start, h: m.h }));
        await hostPage.shot('room_1366x650_top');
        await hostPage.eval('document.getElementById("lobby").scrollTop = 100000');
        await sleep(200);
        const m2 = await measure(hostPage);
        check('a low window, the panel scrolled to its end: START is still in the window, nothing moved the page', m2.noPageScroll && m2.start.b <= m2.h && m2.start.t >= 0, JSON.stringify({ start: m2.start, h: m2.h, scroll: m2.scroll }));
        await hostPage.shot('room_1366x650_scrolled');
        await guestPage.close();
        await hostPage.close();
    }

    // 4. the phone layout (a window up to 700 px wide or up to 500 px high): upright (390 x 844), held sideways (844 x 390, 932 x 430) and a wide window that is only low (1366 x 450). Home is one of three views and the strip
    //    along the bottom picks it: Games (Room, while a room is held), Players, Chat. The room's panel scrolls inside itself with START in view at its end, the chat is a view of its own with the two tabs and a field that
    //    is in the window, the players' list has its view, and no handle of the splitters is there.
    const PHONES = [['390x844', 390, 844, true], ['390x664', 390, 664, true], ['844x390', 844, 390, true], ['932x430', 932, 430, true], ['1366x450', 1366, 450, false]];
    for (const phone of PHONES) {
        const at = phone[0];
        // the games' view, before there is a room: the strip is Games, Players, Chat; the list is shown and the chat and the splitters are not
        {
            const first = await open(chrome, site, 'Hazel', [phone[1], phone[2]], '/', phone[3]);
            await sleep(400);
            const home = await first.eval('(function () { var vis = function (q) { var e = document.querySelector(q); return !!e && e.getClientRects().length > 0; }, d = document.documentElement, t = document.querySelector(".htabs"), b = t ? t.getBoundingClientRect() : null;' +
                ' return { strip: [].map.call(document.querySelectorAll(".htab .name"), function (e) { return e.textContent; }), stripShown: !!b && b.height > 0, games: vis(".gdock"), chat: vis("#gchat"), players: vis(".pl"), handles: [].filter.call(document.querySelectorAll(".sp"), function (e) { return e.getClientRects().length > 0; }).length, page: d.scrollHeight <= innerHeight + 1 && d.scrollWidth <= innerWidth }; })()');
            check('phone ' + at + ': Home with no room is the Games view, with the strip Games, Players, Chat along the bottom, no chat, no players and no handle drawn', JSON.stringify(home.strip) === JSON.stringify(['Games', 'Players', 'Chat']) && home.stripShown && home.games && !home.chat && !home.players && home.handles === 0 && home.page, JSON.stringify(home));
            await first.shot('phone_' + at + '_games');
            await first.close();
        }
        // a room of three (the host, a guest who joined by the link, a Medium computer player): the cards are the fullest
        const { hostPage: page, guestPage } = await makeRoom(chrome, site, phone[1], phone[2], true, phone[3]);
        const tiles = await page.eval('document.body.classList.contains("tiles")');
        check('phone ' + at + ': ' + (phone[1] <= 700 && phone[2] >= 501 ? 'a phone upright draws the colors as tiles' : 'a window that is not a phone upright keeps the rows'), tiles === (phone[1] <= 700 && phone[2] >= 501), String(tiles));
        const view = await page.eval('(function () { var l = document.getElementById("lobby"), d = document.documentElement; return { page: d.scrollHeight <= innerHeight + 1 && d.scrollWidth <= innerWidth, scrolls: l.scrollHeight > l.clientHeight + 1 }; })()');
        check('phone ' + at + ': the page does not scroll (the room fits, or its panel scrolls inside itself)', view.page, JSON.stringify(view));
        if (tiles) check('phone ' + at + ': the tiles fit with no scrolling at all: the room\'s panel does not scroll', !view.scrolls, JSON.stringify(view));
        const finger = phone[2] >= 600 ? 44 : 36;       // (held upright every button is 44 px each way; held sideways the header's buttons are as drawn, 36 px, with the rows 40 and START 50)
        const fingers = await page.eval('[].filter.call(document.querySelectorAll("#lobby button:not(.mode), #lobby .rm"), function (e) { var b = e.getBoundingClientRect(); return b.width > 0 && getComputedStyle(e).visibility !== "hidden" && (b.height < (e.classList.contains("rm") ? 40 : ' + finger + ') - 0.5 || b.width < ' + finger + ' - 0.5); }).map(function (e) { var b = e.getBoundingClientRect(); return (e.className || e.tagName) + " " + Math.round(b.width) + "x" + Math.round(b.height); })');
        check('phone ' + at + ': every button of the room is ' + finger + ' px each way or more (Remove: 40 high)', fingers.length === 0, JSON.stringify(fingers));
        if (tiles) await tilesBlock(page, guestPage, at, phone);
        else {
            const rows = await page.eval('[].map.call(document.querySelectorAll("#slots .slot.k-open, #slots .slot.k-bot"), function (li) { var b = li.getBoundingClientRect(), e = document.elementFromPoint(b.left + b.width / 2, b.top + Math.min(b.height / 2, 22)); return !!e && !!e.closest(".mode") && b.height >= ' + (finger - 4) + '; })');
            check('phone ' + at + ': the whole row of an open color or a computer player is the target of its menu (a press at its middle reaches the button), and the row is ' + (finger - 4) + ' px high or more', rows.length >= 1 && rows.every(Boolean), JSON.stringify(rows));
            await page.eval('(function () { window.__log = []; ["click", "scroll", "resize", "pointerdown"].forEach(function (t) { window.addEventListener(t, function (e) { window.__log.push(t + ":" + (e.target && (e.target.className || e.target.id || e.target.nodeName))); }, true); }); })()');
            await page.press('#slots .slot.k-open');
            await page.waitFor('the color\'s menu opens', '(function () { var m = document.querySelector(".sm-menu"); return !!m && !m.hidden && m.getClientRects().length > 0; })()', 3000).catch(async (e) => { console.log('log: ' + JSON.stringify(await page.eval('window.__log'))); console.log('menu state: ' + JSON.stringify(await page.eval('(function () { var m = document.querySelector(".sm-menu"), li = document.querySelector("#slots .slot.k-open"), b = li.getBoundingClientRect(), at = document.elementFromPoint(b.left + b.width / 2, b.top + b.height / 2); return { menu: !!m, hidden: m && m.hidden, rects: m && m.getClientRects().length, row: [b.left, b.top, b.width, b.height], at: at && (at.className || at.tagName), vw: innerWidth, vh: innerHeight }; })()'))); throw e; });
            const menu = await page.eval('(function () { var b = document.querySelector(".sm-menu").getBoundingClientRect(), items = [].map.call(document.querySelectorAll(".sm-item"), function (e) { return Math.round(e.getBoundingClientRect().height); }); return { fits: b.left >= 0 && b.right <= innerWidth + 0.5 && b.top >= 0 && b.bottom <= innerHeight + 0.5, items: items }; })()');
            check('phone ' + at + ': the menu of a color is in the window with five choices of 44 px or more', menu.fits && menu.items.length === 5 && menu.items.every((h) => h >= 44), JSON.stringify(menu));
            await page.shot('phone_' + at + '_seat_menu');
            await page.press('.sm-item', 'Open');
            await sleep(150);
        }
        await page.press('#rules-open');
        await page.waitFor('the Rules sheet opens', '!document.getElementById("rules").hidden', 3000);
        const sh = await page.eval(SHEET);
        check('phone ' + at + ': the Rules sheet is in the window (nothing scrolls sideways), Done is reachable, the switches are 44 px high or more, and the page behind it takes no press', sh.shown && sh.fits && sh.done && sh.noScroll && sh.pageInert && sh.fog && sh.fog.h >= 36 && sh.direct && sh.direct.h >= 36, JSON.stringify(sh));
        await page.shot('phone_' + at + '_rules');
        await page.press('#rules-done');
        await page.waitFor('the sheet closes', 'document.getElementById("rules").hidden', 3000);
        await page.shot('phone_' + at + '_room_top');
        await page.eval('document.getElementById("lobby").scrollTop = 100000');
        await sleep(200);
        const end = await page.eval('(function () { var s = document.getElementById("start").getBoundingClientRect(), t = document.querySelector(".htabs"), tr = t && t.getClientRects().length ? t.getBoundingClientRect() : null; return { in: s.top >= 0 && s.bottom <= (tr ? tr.top : innerHeight) + 1 && s.height >= 44, start: [Math.round(s.top), Math.round(s.bottom)], strip: tr ? Math.round(tr.top) : null }; })()');
        check('phone ' + at + ': at the end of the room START is in the window above the strip', end.strip !== null && end.in, JSON.stringify(end));
        await page.shot('phone_' + at + '_room_end');
        const strip = await page.eval('[].map.call(document.querySelectorAll(".htab"), function (e) { var b = e.getBoundingClientRect(); return [e.querySelector(".name").textContent, Math.round(b.height), b.width > 0]; })');
        check('phone ' + at + ': the strip is Room, Players, Chat, each 44 px high at least', JSON.stringify(strip.map((t) => t[0])) === JSON.stringify(['Room', 'Players', 'Chat']) && strip.every((t) => t[2] && t[1] >= 44), JSON.stringify(strip));
        await page.press('.htab.t-chat');
        await sleep(300);
        const chat = await page.eval('(function () { var vis = function (q) { var e = document.querySelector(q); return !!e && e.getClientRects().length > 0; }, t = document.querySelector(".htabs").getBoundingClientRect(), f = document.querySelector("#gchat .gc-input, #gchat textarea"), b = f ? f.getBoundingClientRect() : null, send = document.querySelector("#gchat .gc-send"), sb = send ? send.getBoundingClientRect() : null;' +
            ' return { open: document.getElementById("gchat").getAttribute("data-open"), room: vis("#lobby"), field: b ? [Math.round(b.top), Math.round(b.bottom), Math.round(b.height)] : null, send: sb ? Math.round(sb.height) : null, strip: Math.round(t.top), tabs: [].map.call(document.querySelectorAll(".rc-tab"), function (e) { var r = e.getBoundingClientRect(), mid = r.left + r.width / 2, above = document.elementFromPoint(mid, r.top - 3), below = document.elementFromPoint(mid, r.bottom + 3); return [e.textContent, Math.round(r.height), r.width > 0, !!above && e.contains(above), !!below && e.contains(below)]; }) }; })()');       // (a tab\'s press area reaches 3 px past the drawn tab, each way)
        check('phone ' + at + ': the chat view is open with the room hidden, the field and Send (44 px or more) are in the window above the strip, and the two tabs are 44 px or more', chat.open === 'true' && !chat.room && chat.field && chat.field[0] >= 0 && chat.field[1] <= chat.strip && chat.field[2] >= 44 && chat.send >= 44 && chat.tabs.length === 2 && chat.tabs.every((t) => t[2] && t[1] >= 44), JSON.stringify(chat));
        await page.shot('phone_' + at + '_chat_open');
        await page.press('.rc-tab', 'Ants');
        await sleep(300);
        await page.shot('phone_' + at + '_chat_global');
        await page.press('.rc-tab', 'Room');
        await sleep(300);
        await page.shot('phone_' + at + '_chat_room');
        await page.press('.htab.t-players');
        await sleep(300);
        const players = await page.eval('(function () { var vis = function (q) { var e = document.querySelector(q); return !!e && e.getClientRects().length > 0; }; return { players: vis(".pl"), room: vis("#lobby"), chat: vis("#gchat") }; })()');
        check('phone ' + at + ': the players\' view shows the players and neither the room nor the chat', players.players && !players.room && !players.chat, JSON.stringify(players));
        await page.shot('phone_' + at + '_players');
        await page.press('.htab.t-games');
        await sleep(300);
        check('phone ' + at + ': the first tab brings the room back', await page.eval('document.getElementById("lobby").getClientRects().length > 0'));
        await guestPage.close();
        await page.close();
    }
}

main().then(() => { }, (e) => { failures++; console.log('FAIL the run did not finish: ' + (e && e.stack || e)); }).then(() => {
    for (const s of started.reverse()) { try { s.stop(); } catch (e) { /* gone */ } }
    console.log('room in Home (real browser): ' + checks + ' checks, ' + failures + ' failed');
    setTimeout(() => process.exit(failures ? 1 : 0), 1700).unref();
});
