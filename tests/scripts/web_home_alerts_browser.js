// OPT-IN, not part of ./run_tests.sh: the alerts of the front page (web/alert_core.js, web/home/alerts.js; docs/HOME_SCREEN.md "Alerts") in a REAL browser (Chromium through Playwright), behind a small stand-in for the
// site (the files of web/ stamped as the Dockerfile stamps them, a fake /lobbies, and a fake game server on /ws that welcomes the page into a room and lets this script seat a person or a computer player). No hub:
// the chat says that it is offline, which is a guest's page. At a desktop window (1000 x 720) and a phone (390 x 844, pixel ratio 2) it checks:
//   1. the Menu has the row Alerts; it opens the sheet; the sheet lies inside the window (a phone scrolls inside it), every row is a target of 44 px at least, nothing is wider than the window, there is no row for a
//      sound and no part for notifications; a press turns a switch, it stays turned after a reload (kept in this browser); Escape and the dimmed page close it and the focus goes back to the Menu's button;
//   2. a person who joins the room while Home shows: the toast is up, inside the window, not over the chat's field or the phone's strip of views, with Open, which shows the Room; the page's own toast is not up;
//      a computer player makes none; with the switch off there is none;
//   3. the background (the page is made to say that it is hidden): the title alternates with the line and the icon is a real picture with a red dot; shown again, the title and the icon are the page's own;
//   4. the page never asks the browser for anything: no permission is requested, no service worker is registered, the permission is still "default".
//   5. the bell in the top bar (a site with a fake chat hub, so that the visitor is logged in or not): for a logged in visitor the dot (not set up), the plain bell (on), the struck bell (blocked by the browser),
//      the arrow of an iPhone that has not installed the app, none where the browser has no notifications; the plain bell for a visitor who is not logged in; at 320, 390, 721, 1000 and 1280 px wide, with and without
//      a room held: the bell and Quick host, Quick join, My room, the account button and the Menu's button lie inside the window and none is over another, every target is 44 px or more, a phone held upright with a
//      room has the dot on the Menu's button and no bell; a press opens the sheet, and no press and no load asks the browser for anything.
//   --shots DIR takes the pictures of the build (alerts_*.png).
//   node tests/scripts/web_home_alerts_browser.js [--web DIR] [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be made), 1 a check failed.
'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');
const crypto = require('crypto');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the alerts were NOT run in a real browser'); process.exit(0); }

const ROOT = path.resolve(__dirname, '..', '..');
const arg = (name) => { const i = process.argv.indexOf(name); return i === -1 ? null : process.argv[i + 1]; };
const WEB = path.resolve(arg('--web') || path.join(ROOT, 'web'));
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const H = require('./web_page_harness.js')(path.join(WEB, 'lobby.html'));
const { frame, startHub } = require('./fake_hub.js');
const EventEmitter = require('events');
const { roomMessage, welcome, E, C, KEY, NET0 } = H;
const BOT = NET0.SLOT.Bot;
const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml', '.webmanifest': 'application/manifest+json' };
const LOBBIES = { format: 1, count: 2, lobbies: [
    { code: 'c1v3rr', host: 'Clover', map: 'TREASURE.LVL', mode: 'highest-score', people: 2, bots: 1, free: 1, size: 4, age: 200 },
    { code: 'p4n8dz', host: 'Pebble', map: 'ISLANDS.LVL', mode: 'highest-score', people: 3, bots: 0, free: 1, size: 4, age: 312 }] };
const SIZES = [['desktop', 1000, 720, 1, false], ['phone', 390, 844, 2, true]];

let checks = 0;
let failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
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

// ---- the stand-in: the files of web/, /lobbies, and a game server at /ws that seats a person or a computer player when the script says so ----
function startSite(hubYou) {                                                       // hubYou: undefined: no hub (the chat is offline); null: a hub with nobody logged in; an object: a hub and who is logged in
    const stamp = (text) => text.replace(/@@SITE_TITLE@@/g, '').replace(/@@SITE_FOOTER@@/g, '').replace(/@@GAME_VERSION@@/g, '0.0.0').replace(/@@BUILD_ID@@/g, 'alerts').replace(/@@BUILD_DATE@@/g, '2026-11-01 00:00');
    const state = { sockets: [], seats: [[C, 'Maya'], [E, ''], [E, ''], [E, '']] }, emitter = new EventEmitter(); let hub = null;
    const room = () => frame(2, Buffer.from(roomMessage(state.seats, { you: 0, leader: 0 })));
    const site = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        const send = (code, type, body) => { res.writeHead(code, { 'Content-Type': type, 'Cache-Control': 'no-store' }); res.end(body); };
        if (url.pathname === '/lobbies') return send(200, 'application/json', JSON.stringify(LOBBIES));
        if (url.pathname === '/stats' || url.pathname === '/busy') return send(200, 'application/json', '{}');
        if (url.pathname === '/hub/me' && hubYou !== undefined) return send(200, 'application/json', JSON.stringify({ login: true, you: hubYou }));
        if (url.pathname === '/__seat') {                                           // ?seat=1&kind=client|bot|empty&name=Sam: the room changes, every socket is told
            const kind = url.searchParams.get('kind');
            state.seats[Number(url.searchParams.get('seat'))] = [kind === 'client' ? C : kind === 'bot' ? BOT : E, kind === 'empty' ? '' : url.searchParams.get('name')];
            state.sockets.forEach((s) => s.write(room()));
            return send(200, 'text/plain', 'ok');
        }
        if (url.pathname === '/__reset') { state.seats = [[C, 'Maya'], [E, ''], [E, ''], [E, '']]; return send(200, 'text/plain', 'ok'); }
        const file = url.pathname === '/' ? 'lobby.html' : decodeURIComponent(url.pathname).replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) return send(404, 'text/plain', 'not here');
        const type = TYPES[path.extname(full)] || 'application/octet-stream';
        send(200, type, type.startsWith('text/html') ? stamp(fs.readFileSync(full, 'utf8')) : fs.readFileSync(full));
    });
    site.on('upgrade', (req, socket) => {
        if (req.url === '/hub/ws' && hub) { emitter.emit('upgrade', req, socket); return; }
        if (req.url !== '/ws') { socket.destroy(); return; }
        socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + crypto.createHash('sha1').update(req.headers['sec-websocket-key'] + GUID).digest('base64') + '\r\n\r\n');
        state.sockets.push(socket);
        let greeted = false;
        socket.on('data', () => { if (greeted) return; greeted = true; socket.write(frame(2, Buffer.from(welcome(0, KEY, 2)))); socket.write(room()); });       // (the first bytes are the page's Hello: the answer does not depend on them)
        socket.on('close', () => { state.sockets = state.sockets.filter((s) => s !== socket); });
        socket.on('error', () => {});
    });
    return new Promise((resolve) => site.listen(0, '127.0.0.1', async () => {
        emitter.address = () => site.address();
        if (hubYou !== undefined) hub = await startHub({ server: emitter, login: true, you: hubYou, history: [], online: 3 });
        resolve({ site, state, origin: 'http://127.0.0.1:' + site.address().port, close: () => { state.sockets.forEach((s) => s.destroy()); if (hub) hub.drop(); site.close(); } });
    }));
}

async function shot(page, name) { if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, 'alerts_' + name + '.png') }); } }
const box = (page, selector) => page.evaluate((s) => { const e = document.querySelector(s); if (!e || !e.getClientRects().length) return null; const q = e.getBoundingClientRect(); return { l: q.left, t: q.top, r: q.right, b: q.bottom, w: q.width, h: q.height }; }, selector);
const over = (a, b) => a && b && a.l < b.r - 0.5 && b.l < a.r - 0.5 && a.t < b.b - 0.5 && b.t < a.b - 0.5;

(async () => {
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the alerts were NOT run in a real browser'); return; }
    const server = await startSite();
    const { origin, state } = server;
    const errors = [];
    try {
        for (const [name, w, h, ratio, touch] of SIZES) {
            const context = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: ratio, hasTouch: touch, isMobile: touch });
            const page = await context.newPage();
            page.on('pageerror', (e) => errors.push(name + ': ' + String(e && e.message || e)));
            await context.addInitScript(() => {
                window.__asked = 0;
                try { if (window.Notification) { const real = Notification.requestPermission; Notification.requestPermission = function () { window.__asked++; return real.apply(this, arguments); }; } } catch (e) { /* none */ }
            });
            await fetch(origin + '/__reset');
            await page.goto(origin + '/', { waitUntil: 'load' });
            const phone = name === 'phone';
            const label = (s) => name + ': ' + s;
            await until('the alerts part is loaded', () => page.evaluate(() => !!window.AntsHomeAlerts && !!document.querySelector('.hbar .hmenu')));

            // ---- 1. the Menu and the sheet ----
            await page.click('.hbar .hmenu');
            const row = await box(page, '.hmenubox .al-menu');
            check(label('the Menu has the row Alerts, a target of 44 px at least, inside the Menu'), !!row && row.h >= 44 && await page.evaluate(() => /Alerts/.test(document.querySelector('.hmenubox .al-menu').textContent) && !/sounds/.test(document.querySelector('.hmenubox .al-menu').textContent)));
            await shot(page, name + '_menu');
            await page.click('.hmenubox .al-menu');
            await until('the sheet', () => box(page, '.al-card'));
            const card = await box(page, '.al-card');
            check(label('the sheet lies inside the window and nothing is wider than it'), card.l >= 0 && card.r <= w && card.t >= 0 && card.b <= h && await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), JSON.stringify(card));
            const lines = await page.evaluate(() => Array.from(document.querySelectorAll('.al-line')).map((e) => ({ sw: e.getAttribute('data-switch'), h: e.getBoundingClientRect().height, on: e.getAttribute('aria-checked') })));
            check(label('two rows, each 44 px high at least and on: no row for a sound'), lines.length === 2 && lines.every((l) => l.h >= 44 && l.on === 'true') && lines.map((l) => l.sw).join() === 'join,start', JSON.stringify(lines));
            const text = await page.evaluate(() => document.querySelector('.al-card').textContent);
            check(label('the words of the sheet, and nothing about notifications'), /On this page/.test(text) && /Alert when someone joins my room/.test(text) && /Flash the tab when my match starts/.test(text) && !/When Ants is closed|notification|Log in/i.test(text));
            check(label('the focus is on the first row'), await page.evaluate(() => document.activeElement && document.activeElement.getAttribute('data-switch') === 'join'));
            await shot(page, name + '_sheet');
            await page.click('.al-line[data-switch="join"]');
            check(label('a press turns the switch off'), await page.evaluate(() => document.querySelector('.al-line[data-switch="join"]').getAttribute('aria-checked') === 'false' && document.querySelector('.al-line[data-switch="start"]').getAttribute('aria-checked') === 'true'));
            await shot(page, name + '_sheet_join_off');
            await page.keyboard.press('Escape');
            check(label('Escape closes it and the focus is on the Menu\'s button'), !(await box(page, '.al-card')) && await page.evaluate(() => document.activeElement === document.querySelector('.hbar .hmenu')));
            await page.reload({ waitUntil: 'load' });
            await until('the alerts part is loaded again', () => page.evaluate(() => !!window.AntsHomeAlerts));
            await page.click('.hbar .hmenu'); await page.click('.hmenubox .al-menu');
            check(label('after a reload the switch is still off (kept in this browser)'), await page.evaluate(() => document.querySelector('.al-line[data-switch="join"]').getAttribute('aria-checked') === 'false' && JSON.parse(localStorage.getItem('ants.alerts.v1')).join === false));
            const dim = await box(page, '.al-shade');
            await page.mouse.click(Math.min(dim.r - 3, 3), Math.min(dim.b - 3, 3));
            check(label('a press on the dimmed page closes it'), !(await box(page, '.al-card')));
            await page.click('.hbar .hmenu'); await page.click('.hmenubox .al-menu'); await page.click('.al-line[data-switch="join"]'); await page.keyboard.press('Escape');       // (the switch is on again)

            // ---- 2. a person joins while Home shows ----
            await page.click('.hbar .quick.host');
            await until('the host card', () => page.evaluate(() => !!document.querySelector('.gshade .gyes')));
            await page.evaluate(() => document.querySelector('.gshade .gyes').click());
            await until('the Room', () => page.evaluate(() => document.body.dataset.screen === 'room' && document.getElementById('code') && document.getElementById('code').textContent.length > 3));
            await page.click('.rbar .rhome');
            await until('Home', () => page.evaluate(() => document.body.dataset.screen === 'home'));
            await sleep(300);
            await fetch(origin + '/__seat?seat=1&kind=bot&name=' + encodeURIComponent('Pip (bot, Easy)'));
            await sleep(500);
            check(label('a computer player sits down: no alert, and the page\'s own toast is not up either'), !(await box(page, '.al-toast')) && await page.evaluate(() => document.getElementById('toast').hidden === true));
            await fetch(origin + '/__seat?seat=2&kind=client&name=Sam');
            await until('the toast', () => box(page, '.al-toast'));
            const t = await box(page, '.al-toast');
            const parts = phone ? ['.htabs', '.hbar'] : ['#gchat .gc-form', '.hbar'];
            let bad = [];
            for (const sel of parts) { const b = await box(page, sel); if (over(t, b)) bad.push(sel); }
            check(label('the toast is up, inside the window and not over ' + parts.join(' or ')), t.l >= 0 && t.r <= w && t.t >= 0 && t.b <= h && bad.length === 0, JSON.stringify(t) + ' ' + bad.join());
            check(label('it says who and where, with Open, and the page\'s own toast is not up'), await page.evaluate(() => /Sam joined as Blue\./.test(document.querySelector('.al-toast').textContent) && /Open/.test(document.querySelector('.al-toast').textContent) && document.getElementById('toast').hidden === true));
            await sleep(400);                                                          // (the toast comes in over a fifth of a second)
            await shot(page, name + '_join');
            await page.click('.al-toast .btn');
            check(label('Open shows the Room'), await page.evaluate(() => document.body.dataset.screen === 'room') && !(await box(page, '.al-toast')));
            await page.click('.rbar .rhome');
            await fetch(origin + '/__seat?seat=2&kind=empty');
            await sleep(300);
            await page.evaluate(() => { document.querySelectorAll('.al-toast, #toast').forEach((e) => { e.hidden = true; }); });       // (the page's own "Sam left." is up for a moment)
            // the switch off: none
            await page.evaluate(() => localStorage.setItem('ants.alerts.v1', '{"join":false}'));
            await fetch(origin + '/__seat?seat=2&kind=client&name=Eve');
            await sleep(600);
            const offState = await page.evaluate(() => ({ own: document.getElementById('toast').hidden ? '' : document.getElementById('toast').textContent, mine: Array.from(document.querySelectorAll('.al-toast')).filter((e) => !e.hidden).map((e) => e.textContent), stored: localStorage.getItem('ants.alerts.v1'), screen: document.body.dataset.screen }));
            check(label('the switch off: no alert on Home, and none of the page\'s own'), offState.own === '' && offState.mine.length === 0, JSON.stringify(offState));
            await page.evaluate(() => localStorage.removeItem('ants.alerts.v1'));
            await fetch(origin + '/__seat?seat=2&kind=empty'); await sleep(300);

            // ---- 3. the background ----
            const original = await page.evaluate(() => document.title);
            await page.evaluate(() => {
                let hidden = true;
                Object.defineProperty(document, 'hidden', { configurable: true, get: () => hidden });
                Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => (hidden ? 'hidden' : 'visible') });
                document.hasFocus = () => !hidden;
                window.__setHidden = (on) => { hidden = on; document.dispatchEvent(new Event('visibilitychange')); if (!on) window.dispatchEvent(new Event('focus')); };
                window.__icons = [];
            });
            await fetch(origin + '/__seat?seat=2&kind=client&name=Ivy');
            await until('the title says it', () => page.evaluate(() => document.title === '● Ivy joined your room'));
            const seen = new Set();
            for (let i = 0; i < 30; i++) { const s = await page.evaluate(() => [document.title, document.querySelector('link[rel~="icon"]').href]); seen.add(s[0] === original ? 'plain' : 'line'); seen.add(s[1].indexOf('data:image/png') === 0 ? 'dot' : 'icon'); if (seen.size === 4) break; await sleep(100); }
            check(label('in the background the title blinks once a second and the icon has a dot'), seen.has('plain') && seen.has('line') && seen.has('dot') && seen.has('icon'), Array.from(seen).join());
            const pixel = await page.evaluate(() => new Promise((resolve) => { const link = document.querySelector('link[rel~="icon"]'); const keep = link.href; const find = () => { const href = link.href; if (href.indexOf('data:image/png') !== 0) return setTimeout(find, 30); const img = new Image(); img.onload = () => { const c = document.createElement('canvas'); c.width = c.height = 32; const g = c.getContext('2d'); g.drawImage(img, 0, 0); resolve(Array.from(g.getImageData(24, 8, 1, 1).data)); }; img.src = href; }; find(); }));
            check(label('the dot is a real picture: red on the top right of the icon'), pixel[0] > 200 && pixel[1] < 90 && pixel[2] < 90, JSON.stringify(pixel));
            await page.evaluate(() => window.__setHidden(false));
            await sleep(1300);
            check(label('shown again: the title and the icon are the page\'s own, and stay so'), await page.evaluate((o) => document.title === o && /\/favicon\.png/.test(document.querySelector('link[rel~="icon"]').href), original));
            await fetch(origin + '/__seat?seat=2&kind=empty'); await sleep(300);

            // ---- 4. the page asks nothing ----
            const asked = await page.evaluate(async () => ({ asked: window.__asked, permission: window.Notification ? Notification.permission : 'none', workers: navigator.serviceWorker ? (await navigator.serviceWorker.getRegistrations()).length : 0 }));
            check(label('no permission was asked, none is given, no service worker'), asked.asked === 0 && asked.permission !== 'granted' && asked.workers === 0, JSON.stringify(asked));
            await context.close();
        }
        // ---- 5. the bell ----
        const signed = await startSite({ name: 'Maya', mod: false, role: 'none' }), guest = await startSite(null);
        const IPHONE = 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_4 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Mobile/15E148 Safari/604.1';
        async function bellPage(site, w, h, ratio, touch, scenario) {
            const context = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: ratio, hasTouch: touch, isMobile: touch, userAgent: scenario.ua });
            const page = await context.newPage();
            page.on('pageerror', (e) => errors.push('bell ' + w + ': ' + String(e && e.message || e)));
            await context.addInitScript((sc) => {
                window.__asked = 0;
                try {
                    if (sc.none) { delete window.Notification; }
                    else if (window.Notification) { Object.defineProperty(Notification, 'permission', { configurable: true, get: () => sc.permission }); Notification.requestPermission = function () { window.__asked++; return Promise.resolve(sc.permission); }; }
                } catch (e) { /* none */ }
            }, scenario);
            await fetch(site.origin + '/__reset');
            await page.goto(site.origin + '/', { waitUntil: 'load' });
            await until('the alerts part', () => page.evaluate(() => !!window.AntsHomeAlerts && !!document.querySelector('.hbar .hmenu')));
            return { context, page };
        }
        const controls = (page) => page.evaluate(() => {
            const out = {};
            [['host', '.hbar .quick.host'], ['join', '.hbar .quick.join'], ['mine', '.hbar .hmine .mine'], ['bell', '.hbar .al-bell'], ['chip', '.hbar .hlogin'], ['menu', '.hbar .hmenu']].forEach(([k, sel]) => { const e = document.querySelector(sel); if (e && e.getClientRects().length && getComputedStyle(e).visibility !== 'hidden') { const q = e.getBoundingClientRect(); out[k] = { l: q.left, t: q.top, r: q.right, b: q.bottom, w: q.width, h: q.height }; } });
            return out;
        });
        const clash = (c) => { const names = Object.keys(c), bad = []; for (let i = 0; i < names.length; i++) for (let j = i + 1; j < names.length; j++) if (over(c[names[i]], c[names[j]])) bad.push(names[i] + '/' + names[j]); return bad; };
        const bellState = (page) => page.evaluate(() => { const b = document.querySelector('.hbar .al-bell'); return b ? { hidden: b.hidden || !b.getClientRects().length, state: b.getAttribute('data-state'), label: b.getAttribute('aria-label'), marks: b.querySelectorAll('.al-mark').length, dot: document.querySelector('.hbar .hmenu').classList.contains('al-dot'), after: getComputedStyle(document.querySelector('.hbar .hmenu'), '::after').content } : null; });
        for (const [name, w, h, ratio, touch] of [['w320', 320, 640, 2, true], ['phone', 390, 844, 2, true], ['w721', 721, 800, 1, false], ['desktop', 1000, 720, 1, false], ['wide', 1280, 800, 1, false]]) {
            const upright = touch && h > w;
            const { context, page } = await bellPage(signed, w, h, ratio, touch, { permission: 'default' });
            await until('the dock knows the visitor', () => page.evaluate(() => { const b = document.querySelector('.hbar .al-bell'); return b && b.getAttribute('data-state') === 'off'; }));
            let c = await controls(page), st = await bellState(page);
            const label = (x) => 'bell ' + name + ': ' + x;
            check(label('a logged in visitor who has not set notifications up: the bell with a dot, named for a reader'), !!c.bell && st.state === 'off' && st.marks === 1 && st.label === 'Alerts: not set up on this device' && c.bell.w >= 44 && c.bell.h >= 44, JSON.stringify(st) + JSON.stringify(c.bell));
            check(label('Quick host, Quick join, the account button, the Menu and the bell are inside the window and none is over another'), c.host && c.join && c.menu && c.chip && [c.host, c.join, c.menu, c.chip, c.bell].every((r) => r.l >= -0.5 && r.r <= w + 0.5 && r.t >= -0.5 && r.b <= h + 0.5) && clash(c).length === 0, clash(c).join() + ' ' + JSON.stringify(c));
            const wide = w >= 721, last = (k) => (wide && k.bell ? k.bell : k.menu);          // a wide window: the bell is the last of the group, after the Menu; a phone: in front of the account button
            check(label('the group of the account button, the Menu and the bell is at the right edge of the bar'), last(c).r >= w - 36 && last(c).r <= w, JSON.stringify(last(c)));
            check(label(wide ? 'the bell stands after the Menu, on its line' : 'the bell stands next to the account button, in front of it'), (wide ? c.bell.l >= c.menu.r - 0.5 && c.chip.r <= c.menu.l + 0.5 : c.bell.r <= c.chip.l + 0.5) && Math.abs((c.bell.t + c.bell.b) / 2 - (c.chip.t + c.chip.b) / 2) < 6, JSON.stringify([c.bell, c.chip, c.menu]));
            check(label('the Menu\'s button has no dot while the bell is there'), st.dot === true && (st.after === 'none' || st.after === 'normal'), JSON.stringify(st));
            await shot(page, 'bell_' + name + '_off');
            // a room held: My room joins the row
            await page.click('.hbar .quick.host');
            await until('the host card', () => page.evaluate(() => !!document.querySelector('.gshade .gyes')));
            await page.evaluate(() => document.querySelector('.gshade .gyes').click());
            await until('the Room', () => page.evaluate(() => document.body.dataset.screen === 'room' && document.getElementById('code') && document.getElementById('code').textContent.length > 3));
            await page.click('.rbar .rhome');
            await until('Home', () => page.evaluate(() => document.body.dataset.screen === 'home' && !!document.querySelector('.hbar .hmine .mine') && document.querySelector('.hbar .hmine .mine').getClientRects().length > 0));
            await sleep(300);
            c = await controls(page); st = await bellState(page);
            check(label('with a room held: My room, Quick host, Quick join, the account button and the Menu are inside the window and none is over another (or over the bell)'), !!c.mine && [c.host, c.join, c.menu, c.chip, c.mine].every((r) => r && r.l >= -0.5 && r.r <= w + 0.5 && r.t >= -0.5 && r.b <= h + 0.5) && clash(c).length === 0, clash(c).join() + ' ' + JSON.stringify(c));
            check(label('with a room held the last button of the group is still at the right edge'), last(c).r >= w - 36 && last(c).r <= w, JSON.stringify(last(c)));
            if (upright) {
                check(label('a phone held upright with a room has no room for the bell: it is not drawn, and the Menu\'s button has the dot'), !c.bell && st.hidden === true && st.dot === true && st.after !== 'none' && st.after !== 'normal', JSON.stringify(st));
                check(label('the Menu\'s button is still a target of 44 px'), c.menu.w >= 44 && c.menu.h >= 44);
            } else check(label('a room held at this width: the bell keeps its place'), !!c.bell && c.bell.w >= 44 && c.bell.h >= 44, JSON.stringify(c.bell));
            await shot(page, 'bell_' + name + '_room');
            await page.click('.hbar .hmenu'); await sleep(150);
            check(label('the Menu\'s button opens the Menu as before'), await page.evaluate(() => !!document.querySelector('.hmenubox') && !document.querySelector('.hmenubox').hidden && !!document.querySelector('.hmenubox .al-menu')));
            await page.keyboard.press('Escape');
            if (c.bell) {
                await page.click('.hbar .al-bell');
                check(label('a press on the bell opens the sheet, and the browser was not asked'), !!(await box(page, '.al-card')) && await page.evaluate(() => window.__asked === 0 && Notification.permission === 'default'));
                await page.keyboard.press('Escape');
                check(label('Escape closes it and the focus is on the bell'), await page.evaluate(() => document.activeElement === document.querySelector('.hbar .al-bell')));
            }
            check(label('the browser was never asked: not on load, not by a press'), await page.evaluate(() => window.__asked === 0));
            await context.close();
        }
        // the states, on a phone and on a wide window
        for (const [name, w, h, ratio, touch] of [['phone', 390, 844, 2, true], ['wide', 1280, 800, 1, false]]) {
            for (const [label, site, scenario, want] of [
                ['on', signed, { permission: 'granted' }, { state: 'on', marks: 0, dot: false, text: 'Alerts: on for this device' }],
                ['blocked', signed, { permission: 'denied' }, { state: 'blocked', marks: 0, dot: false, text: 'Alerts: blocked in this browser' }],
                ['an iPhone that has not installed the app', signed, { none: true, ua: IPHONE }, { state: 'needs', marks: 1, dot: false, text: 'Alerts: need the app on the Home Screen' }],
                ['a visitor who is not logged in', guest, { permission: 'default' }, { state: 'guest', marks: 0, dot: false, text: 'Alerts' }],
            ]) {
                const { context, page } = await bellPage(site, w, h, ratio, touch, scenario);
                await until('the bell is in its state', () => page.evaluate((s) => { const b = document.querySelector('.hbar .al-bell'); return b && b.getAttribute('data-state') === s; }, want.state));
                const st = await bellState(page), c = await controls(page);
                check('bell ' + name + ', ' + label + ': the state, its name and its marks', st.state === want.state && st.label === want.text && st.marks === want.marks && st.dot === want.dot && !st.hidden, JSON.stringify(st));
                check('bell ' + name + ', ' + label + ': lies inside the window, over no other button, 44 px or more', c.bell && c.bell.w >= 44 && c.bell.h >= 44 && c.bell.l >= 0 && c.bell.r <= w && clash(c).length === 0, clash(c).join() + JSON.stringify(c.bell));
                await shot(page, 'bell_' + name + '_' + want.state);
                await page.click('.hbar .al-bell');
                check('bell ' + name + ', ' + label + ': a press opens the sheet, and the browser is not asked', !!(await box(page, '.al-card')) && await page.evaluate(() => window.__asked === 0));
                await context.close();
            }
            const none = await bellPage(signed, w, h, ratio, touch, { none: true });
            await sleep(600);
            check('bell ' + name + ', a browser with no notifications: no bell, and the Menu still has the row', !(await box(none.page, '.hbar .al-bell')) && await none.page.evaluate(() => { document.querySelector('.hbar .hmenu').click(); return !!document.querySelector('.hmenubox .al-menu'); }));
            await none.context.close();
        }
        signed.close(); guest.close();
        check('no error on any page', errors.length === 0, errors.join(' | '));
    } finally { await browser.close(); server.close(); }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
})().catch((e) => { console.log('FAIL ' + (e && e.stack || e)); process.exit(1); });
