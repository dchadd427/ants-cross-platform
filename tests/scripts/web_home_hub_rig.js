// A real hub (`python3 -m hub`, with its test login) behind a small stand-in for the site's nginx, for the opt-in checks that drive Home in a real browser (Playwright): tests/scripts/web_home_follows_dms_browser.js, web_home_dm_phone_browser.js.
// startStack(WEB) -> { origin, db, log(), stop() }: new hub, new database; the files of WEB as nginx gives them (/ is lobby.html), a fake /lobbies, /hub/ws to the hub with the account that the cookie `acct` names (ACCOUNTS: id, an underscore, the name).
'use strict';
const fs = require('fs');
const os = require('os');
const net = require('net');
const http = require('http');
const path = require('path');
const { spawn } = require('child_process');
const ROOT = path.resolve(__dirname, '..', '..');
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml', '.webmanifest': 'application/manifest+json' };
const ACCOUNTS = { hazel: '100_hazel', pebble: '200_pebble', fern: '300_fern', juniper: '400_juniper', birch: '500_birch' };         // (the cookie: the account's id, an underscore, its name)
const LOBBIES = { format: 1, count: 3, lobbies: [
    { code: 'c1v3rr', host: 'Clover', map: 'TREASURE.LVL', mode: 'highest-score', people: 2, bots: 1, free: 1, size: 4, age: 200 },
    { code: 'p4n8dz', host: 'Pebble', map: 'ISLANDS.LVL', mode: 'highest-score', people: 3, bots: 0, free: 1, size: 4, age: 312 },
    { code: 'f3rn22', host: 'Fern', map: 'TREASURE.LVL', mode: 'highest-score', people: 1, bots: 2, free: 1, size: 4, age: 20 }] };

const freePort = () => new Promise((resolve, reject) => { const s = net.createServer(); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); }); s.on('error', reject); });
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(label, fn, ms) {
    const end = Date.now() + (ms || 10000);
    for (;;) {
        let v = null;
        try { v = await fn(); } catch (e) { v = null; }
        if (v) return v;
        if (Date.now() > end) throw new Error('timeout: ' + label);
        await sleep(50);
    }
}

async function startStack(WEB) {                        // a new hub with a new database, and the site in front of it
    const [hubPort, sitePort] = [await freePort(), await freePort()];
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ants_follows_browser.'));
    const origin = 'http://127.0.0.1:' + sitePort, db = path.join(dir, 'hub.db');
    const hub = spawn('python3', ['-m', 'hub'], { cwd: ROOT, env: Object.assign({}, process.env, { ANTS_HUB_PORT: String(hubPort), ANTS_HUB_HOST: '127.0.0.1', ANTS_HUB_DB: db, ANTS_HUB_PUBLIC_URL: origin, ANTS_HUB_OWNER: '999', ANTS_HUB_TEST_LOGIN: '1' }), stdio: ['ignore', 'pipe', 'pipe'] });
    let hubLog = '';
    hub.stdout.on('data', (d) => { hubLog += d; });
    hub.stderr.on('data', (d) => { hubLog += d; });
    await until('the hub answers', () => new Promise((resolve) => http.get({ host: '127.0.0.1', port: hubPort, path: '/hub/healthz' }, (res) => { res.resume(); resolve(res.statusCode === 200); }).on('error', () => resolve(false))), 15000).catch((e) => { console.log('FAIL ' + e.message + '\n' + hubLog); process.exit(1); });

    const site = http.createServer((req, res) => {          // the files of web/, as nginx gives them (/ is lobby.html), a fake /lobbies
        const url = new URL(req.url, 'http://x');
        if (url.pathname === '/lobbies') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(LOBBIES)); return; }
        if (url.pathname === '/stats' || url.pathname === '/busy') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end('{}'); return; }
        const file = url.pathname === '/' ? 'lobby.html' : decodeURIComponent(url.pathname).replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'application/json' }); res.end('{"error":"not here"}'); return; }
        const type = TYPES[path.extname(full)] || 'application/octet-stream';
        const stamp = (t) => t.replace(/@@SITE_TITLE@@/g, '').replace(/@@SITE_FOOTER@@/g, '').replace(/@@GAME_VERSION@@/g, '0.0.0').replace(/@@BUILD_ID@@/g, 'check').replace(/@@BUILD_DATE@@/g, '20000101');
        res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' });
        res.end(type.startsWith('text/html') ? stamp(fs.readFileSync(full, 'utf8')) : fs.readFileSync(full));
    });
    site.on('upgrade', (req, socket, head) => {             // /hub/ws to the hub, with the account that the cookie names
        if (new URL(req.url, 'http://x').pathname !== '/hub/ws') { socket.destroy(); return; }
        const who = /(?:^|;\s*)acct=([0-9]+_[a-z0-9_.]+)/.exec(req.headers.cookie || '');
        const headers = Object.assign({}, req.headers);
        delete headers.cookie;
        if (who) { const at = who[1].indexOf('_'); headers['x-hub-test-login'] = who[1].slice(0, at) + ':' + who[1].slice(at + 1); }
        const up = net.connect(hubPort, '127.0.0.1', () => {
            up.write('GET /hub/ws HTTP/1.1\r\n' + Object.entries(headers).map(([k, v]) => k + ': ' + v).join('\r\n') + '\r\n\r\n');
            if (head && head.length) up.write(head);
            up.pipe(socket); socket.pipe(up);
        });
        up.on('error', () => socket.destroy()); socket.on('error', () => up.destroy());
    });
    await new Promise((resolve) => site.listen(sitePort, '127.0.0.1', resolve));
    return { origin, db, log: () => hubLog, stop() { hub.kill('SIGTERM'); site.close(); try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) { /* it stays */ } } };
}

// One page of an account on a stack, Home, the dock live; every WebSocket of the page is kept in window.__socks (so that a test can say what a page says to the hub). size: [name, width, height, pixel ratio, touch].
async function openPage(browser, stack, account, size, onError, label) {
    const context = await browser.newContext({ viewport: { width: size[1], height: size[2] }, deviceScaleFactor: size[3], hasTouch: !!size[4], isMobile: !!size[4] });
    await context.addCookies([{ name: 'acct', value: ACCOUNTS[account], url: stack.origin }]);
    await context.addInitScript((name) => {
        try { localStorage.setItem('ants.name', name); } catch (e) { /* none */ }
        window.__socks = [];
        const Native = window.WebSocket;
        window.WebSocket = function (url, protocols) { const socket = protocols === undefined ? new Native(url) : new Native(url, protocols); window.__socks.push(socket); return socket; };
        window.WebSocket.prototype = Native.prototype;
        ['CONNECTING', 'OPEN', 'CLOSING', 'CLOSED'].forEach((k) => { window.WebSocket[k] = Native[k]; });
    }, account);
    const page = await context.newPage();
    page.on('pageerror', (e) => onError(String(e && e.message || e)));
    await page.goto(stack.origin + '/', { waitUntil: 'load' });
    await until(label + 'the dock is live', () => page.evaluate(() => { const d = document.getElementById('gchat'); return !!d && d.getAttribute('data-state') === 'live'; }), 15000);
    await until(label + 'the bell and the Messages button are there', () => page.evaluate(() => !!document.querySelector('.hbar .al-bell') && !!document.querySelector('#gchat .pm-btn')), 15000);
    await sleep(400);
    return { context, page, phone: !!size[4], size, name: account };
}

module.exports = { ACCOUNTS, LOBBIES, startStack, openPage, until, sleep, freePort, ROOT };
