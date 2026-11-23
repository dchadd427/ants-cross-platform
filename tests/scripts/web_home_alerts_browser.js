// OPT-IN, not part of ./run_tests.sh: the alerts of the front page (web/alert_core.js, web/home/alerts.js; docs/HOME_SCREEN.md "Alerts"), the invitations and the mentions (docs/HOME_CHAT.md "Invitations and mentions") in a REAL browser
// (Chromium through Playwright), behind a small stand-in for the site (the files of web/ stamped as the Dockerfile stamps them, a fake /lobbies, a fake game server on /ws that welcomes the page into a room and lets this script seat a person
// or a computer player, and, for a logged in visitor, the fake chat hub of tests/scripts/fake_hub.js with a players list). At 1920 x 1080, 1366 x 768, 1280 x 800 and a phone (390 x 844, pixel ratio 2) it checks:
//   1. a visitor (no hub: the chat says that it is offline): the bell in the bar opens the list (a panel under the bell, a sheet from the bottom on a phone), inside the window, nothing wider than the window, no dimming of a wide page;
//      the gear shows the switches (one row each, 44 px at least, none for a sound, no part for notifications); a press turns a switch, it stays after a reload (kept in this browser); Escape goes back, then closes, and the focus goes back to
//      the bell; a press on the page outside the panel closes it;
//   2. a person who joins the room while the Room shows: the page's own toast says who and where, Home's alert is not drawn; a computer player makes none;
//   3. the background (the page is made to say that it is hidden): the title alternates with the line and the icon is a real picture with a red dot; shown again, the title and the icon are the page's own;
//   4. the page never asks the browser for anything: no permission is requested, no service worker is registered, the permission is still "default";
//   5. the bell in the bar, for a visitor who is logged in or not and for the longest name, with and without a room held, at 320, 390, 721, 1000 and 1280 px wide and a phone held sideways: the bell and the other buttons lie inside the
//      window and none is over another, the bell is a target of 44 px or more and stands right beside the Menu's button (one gap to its left, the same top and height), the account button before it;
//   6. a logged in visitor with the hub's alerts: the number on the bell, the list (three unread of five entries, the match that is over read), the invitation as a toast (the chat folded), the sheet of a person to invite (a room held), the two helper lists (`/invite ` and `@`): each lies
//      inside the window, every target is 44 px or more, no word is cut, the page does not scroll sideways.
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
const SIZES = [['1920', 1920, 1080, 1, false], ['1366', 1366, 768, 1, false], ['1280', 1280, 800, 1, false], ['phone', 390, 844, 2, true]];
const NOW = () => Math.floor(Date.now() / 1000);
const WHO = { t: 'who', full: true, n: 6, g: 2, more: 0, rows: [{ i: 2, name: 'Wren', at: 'wait' }, { i: 3, name: 'maple.leaf', at: 'look' }, { i: 4, name: 'Clover', at: 'wait' }, { i: 7, name: 'juniper', at: 'look' }, { i: 9, name: 'tess', at: 'look' }] };
const invite = (id, from, o) => Object.assign({ id, at: NOW() - 40, k: 'invite', u: 1, from, map: 'TREASURE.LVL', n: 3, of: 4, o: 1, code: 'abc234' }, o || {});
const LIST = () => [invite(7, 'Clover'), { id: 6, at: NOW() - 840, k: 'join', u: 1, name: 'juniper', seat: 'Red' }, { id: 5, at: NOW() - 1920, k: 'mention', u: 1, from: 'tess', text: 'which map are we on, @Maya? This one is a long line of words that goes on and on to see that it wraps' }, { id: 4, at: NOW() - 3600, k: 'over', map: 'ISLANDS.LVL', place: 2, match: 'ants-ISLANDS-20261123-091500Z' }, invite(3, 'Pebble', { u: 0, o: undefined, code: undefined, at: NOW() - 26 * 3600 })];

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
        if (hubYou !== undefined) hub = await startHub({ server: emitter, login: true, you: hubYou, history: [], online: 9, welcomeExtra: { presence: true, me: 1 }, who: WHO });
        resolve({ site, state, hub: () => hub, alerts: (rows) => hub.peers.forEach((q) => hub.send(q, { t: 'alerts', rows })), alert: (row) => hub.peers.forEach((q) => hub.send(q, { t: 'alert', row })), origin: 'http://127.0.0.1:' + site.address().port, close: () => { state.sockets.forEach((s) => s.destroy()); if (hub) hub.drop(); site.close(); } });
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
            await until('the alerts part is loaded', () => page.evaluate(() => !!window.AntsHomeAlerts && !!document.querySelector('.hbar .al-bell')));

            // ---- 1. the bell, the list and the switches ----
            const bellBox = await box(page, '.hbar .al-bell');
            check(label('the bell is in the bar, a target of 44 px or more, with no number (nothing is unread)'), !!bellBox && bellBox.w >= 44 && bellBox.h >= 44 && await page.evaluate(() => !document.querySelector('.hbar .al-bell .al-mark')), JSON.stringify(bellBox));
            await page.click('.hbar .al-bell');
            await until('the panel', () => box(page, '.al-panel'));
            const panel = await box(page, '.al-panel');
            check(label('the list lies inside the window and nothing is wider than it'), panel.l >= 0 && panel.r <= w && panel.t >= 0 && panel.b <= h && await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), JSON.stringify(panel));
            check(label('nothing yet: the words of an empty list, where it is kept, and the line that says to log in'), await page.evaluate(() => { const t = document.querySelector('.al-panel').textContent; return /No alerts yet/.test(t) && /Kept in this browser/.test(t) && /Log in to get invitations/.test(t); }));
            check(label(phone ? 'a phone: the list is a sheet from the bottom over a dimmed page' : 'a wide window: the page behind the panel is not dimmed'), await page.evaluate((ph) => { const e = document.querySelector('.al-shade'), dim = !/rgba\(0, 0, 0, 0\)|transparent/.test(getComputedStyle(e).backgroundColor); return ph ? e.classList.contains('phone') && dim : !e.classList.contains('phone') && !dim; }, phone));
            await shot(page, name + '_list');
            await page.click('.al-panel .al-gear');
            const lines = await page.evaluate(() => Array.from(document.querySelectorAll('.al-panel .al-line')).map((e) => ({ sw: e.getAttribute('data-switch'), h: e.getBoundingClientRect().height, on: e.getAttribute('aria-checked') })));
            check(label('the settings: two rows for a visitor, each 44 px high at least and on, none for a sound'), lines.length === 2 && lines.every((l) => l.h >= 44 && l.on === 'true') && lines.map((l) => l.sw).join() === 'join,start', JSON.stringify(lines));
            check(label('and nothing about notifications'), await page.evaluate(() => !/notification|When Ants is closed|sounds/i.test(document.querySelector('.al-panel').textContent)));
            await shot(page, name + '_settings');
            await page.click('.al-panel .al-line[data-switch="join"]');
            check(label('a press turns the switch off'), await page.evaluate(() => document.querySelector('.al-line[data-switch="join"]').getAttribute('aria-checked') === 'false' && document.querySelector('.al-line[data-switch="start"]').getAttribute('aria-checked') === 'true'));
            await page.keyboard.press('Escape');
            check(label('Escape goes back from the settings to the list'), !!(await box(page, '.al-panel')) && await page.evaluate(() => !document.querySelector('.al-line')));
            await page.keyboard.press('Escape');
            check(label('Escape closes the list and the focus is on the bell'), !(await box(page, '.al-panel')) && await page.evaluate(() => document.activeElement === document.querySelector('.hbar .al-bell')));
            await page.reload({ waitUntil: 'load' });
            await until('the alerts part is loaded again', () => page.evaluate(() => !!window.AntsHomeAlerts && !!document.querySelector('.hbar .al-bell')));
            await page.click('.hbar .al-bell'); await page.click('.al-panel .al-gear');
            check(label('after a reload the switch is still off (kept in this browser)'), await page.evaluate(() => document.querySelector('.al-line[data-switch="join"]').getAttribute('aria-checked') === 'false' && JSON.parse(localStorage.getItem('ants.alerts.v1')).join === false));
            await page.click('.al-panel .al-line[data-switch="join"]');                 // (the switch is on again)
            await page.keyboard.press('Escape'); await page.keyboard.press('Escape');
            await page.click('.hbar .al-bell'); await until('the panel', () => box(page, '.al-panel'));
            await page.mouse.click(5, 5);
            check(label('a press on the page outside the panel closes it'), !(await box(page, '.al-panel')));

            // ---- 2. a person joins while the Room shows (the page never shows Home while it holds a room): the page's own toast says it, and Home's own alert is not drawn ----
            await page.click('.hbar .quick.host');
            await until('the host card', () => page.evaluate(() => !!document.querySelector('.gshade .gyes')));
            await page.evaluate(() => document.querySelector('.gshade .gyes').click());
            await until('the Room', () => page.evaluate(() => document.body.dataset.screen === 'room' && document.getElementById('code') && document.getElementById('code').textContent.length > 3));
            await sleep(300);
            await fetch(origin + '/__seat?seat=1&kind=bot&name=' + encodeURIComponent('Pip (bot, Easy)'));
            await sleep(500);
            check(label('a computer player sits down: no alert of Home, and the page\'s own toast says nothing of it either'), !(await box(page, '.al-toast')) && await page.evaluate(() => document.getElementById('toast').hidden === true));
            await fetch(origin + '/__seat?seat=2&kind=client&name=Sam');
            await sleep(600);
            check(label('a person sits down while the Room shows: the page\'s own toast says who and where, and Home\'s alert is not drawn'), await page.evaluate(() => /Sam joined as Blue\./.test(document.getElementById('toast').textContent) && !document.getElementById('toast').hidden && !Array.from(document.querySelectorAll('.al-toast')).some((e) => !e.hidden)));
            await shot(page, name + '_join');
            await fetch(origin + '/__seat?seat=2&kind=empty');
            await sleep(300);
            await page.evaluate(() => { document.querySelectorAll('.al-toast, #toast').forEach((e) => { e.hidden = true; }); });       // (the page's own "Sam left." is up for a moment)
            await page.evaluate(() => localStorage.removeItem('ants.alerts.v1'));

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
        // ---- 5. the bell's place ----
        const signed = await startSite({ name: 'Maya', mod: false, role: 'none', k: 'k_maya' }), guest = await startSite(null), longName = await startSite({ name: 'LongestNameOfThirtyTwoCharacters', mod: false, role: 'none', k: 'k_long' });
        async function bellPage(site, w, h, ratio, touch) {
            const context = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: ratio, hasTouch: touch, isMobile: touch });
            const page = await context.newPage();
            page.on('pageerror', (e) => errors.push('bell ' + w + ': ' + String(e && e.message || e)));
            await context.addInitScript(() => { window.__asked = 0; try { if (window.Notification) { const real = Notification.requestPermission; Notification.requestPermission = function () { window.__asked++; return real.apply(this, arguments); }; } } catch (e) { /* none */ } });
            await fetch(site.origin + '/__reset');
            await page.goto(site.origin + '/', { waitUntil: 'load' });
            await until('the bell is drawn', () => page.evaluate(() => { const b = document.querySelector('.hbar .al-bell'); return !!b && b.getClientRects().length > 0; }));
            await sleep(300);
            return { context, page };
        }
        const controls = (page) => page.evaluate(() => {
            const out = {};
            [['host', '.hbar .quick.host'], ['join', '.hbar .quick.join'], ['hist', '.hbar .quick.hist'], ['bell', '.hbar .al-bell'], ['chip', '.hbar .hlogin'], ['menu', '.hbar .hmenu']].forEach(([k, sel]) => { const e = document.querySelector(sel); if (e && e.getClientRects().length && getComputedStyle(e).visibility !== 'hidden') { const q = e.getBoundingClientRect(); out[k] = { l: q.left, t: q.top, r: q.right, b: q.bottom, w: q.width, h: q.height }; } });
            return out;
        });
        const clash = (c) => { const names = Object.keys(c), bad = []; for (let i = 0; i < names.length; i++) for (let j = i + 1; j < names.length; j++) if (over(c[names[i]], c[names[j]])) bad.push(names[i] + '/' + names[j]); return bad; };
        // the gap between neighbors in the bar: the column gap of the bar's own row (a phone's bar, where the right-hand group is `display: contents`) or of the group (a wide bar)
        const gapOf = (page) => page.evaluate(() => { const g = document.querySelector('.hbar .hright'), bar = document.querySelector('.hbar'); return parseFloat(getComputedStyle(getComputedStyle(g).display === 'contents' ? bar : g).columnGap) || 0; });
        const BESIDE = (c, gap) => !!c.bell && !!c.menu && Math.abs(c.menu.l - c.bell.r - gap) < 0.75 && Math.abs(c.bell.t - c.menu.t) < 0.5 && Math.abs(c.bell.h - c.menu.h) < 0.5 && (!c.chip || c.chip.r <= c.bell.l + 0.5);
        const holdRoom = async (page) => {
            await page.click('.hbar .quick.host');
            await until('the host card', () => page.evaluate(() => !!document.querySelector('.gshade .gyes')));
            await page.evaluate(() => document.querySelector('.gshade .gyes').click());
            await until('the Room', () => page.evaluate(() => document.body.dataset.screen === 'room' && document.getElementById('code') && document.getElementById('code').textContent.length > 3));
            await sleep(300);
        };
        // the bell's place: right beside the Menu's button for a visitor who is logged in or not, with or without a room, at five widths and a phone held sideways (two compact rows at 600 x 360, one low row at 844 x 390)
        for (const [name, w, h, ratio, touch] of [['w320', 320, 640, 2, true], ['phone', 390, 844, 2, true], ['w721', 721, 800, 1, false], ['desktop', 1000, 720, 1, false], ['wide', 1280, 800, 1, false], ['sideways600', 600, 360, 2, true], ['sideways844', 844, 390, 2, true]]) {
            for (const [who, site] of [['logged in', signed], ['not logged in', guest], ['logged in with the longest name', longName]]) {
                const { context, page } = await bellPage(site, w, h, ratio, touch);
                let c = await controls(page);
                const place = 'bell place ' + name + ', ' + who;
                check(place + ': the bell is 44 px or more, inside the window, over no other button, right beside the Menu\'s button (one gap to its left, the same top and height)' + (who !== 'not logged in' ? ', the account button before it' : ''), !!c.bell && c.bell.w >= 44 && c.bell.h >= 44 && c.bell.l >= 0 && c.bell.r <= w && clash(c).length === 0 && BESIDE(c, await gapOf(page)) && (who === 'not logged in' || !!c.chip), clash(c).join() + JSON.stringify(c));
                await holdRoom(page);
                c = await controls(page);
                check(place + ', a room held: the same, with Match history in the row (a phone has no Quick host and Quick join in its bar then)', !!c.hist && !!c.bell && c.bell.w >= 44 && Object.keys(c).every((k) => c[k].l >= -0.5 && c[k].r <= w + 0.5 && c[k].t >= -0.5 && c[k].b <= h + 0.5) && clash(c).length === 0 && BESIDE(c, await gapOf(page)), clash(c).join() + JSON.stringify(c));
                check(place + ': the browser was never asked for anything', await page.evaluate(() => window.__asked === 0));
                await context.close();
            }
        }

        // ---- 6. the hub's alerts, the invitation, the sheet of a person and the helper, at the four sizes ----
        async function measure(page, selector, label, w, h, o) {
            const r = await page.evaluate(([sel, targets]) => {
                const root = document.querySelector(sel);
                if (!root) return null;
                const q = root.getBoundingClientRect(), out = { box: [q.left, q.top, q.right, q.bottom], small: [], clipped: [], page: document.documentElement.scrollWidth > window.innerWidth + 1 };
                root.querySelectorAll('button, [role="switch"], a[href], input').forEach((e) => { const b = e.getBoundingClientRect(); if (targets && b.width && b.height && (b.height < 43.5 || b.width < 43.5) && getComputedStyle(e).visibility !== 'hidden') out.small.push((e.className || e.tagName) + ' ' + Math.round(b.width) + 'x' + Math.round(b.height)); });
                root.querySelectorAll('*').forEach((e) => { if (e.clientWidth && e.scrollWidth > e.clientWidth + 1 && getComputedStyle(e).overflowX === 'visible' && !/^(svg|path|circle|line)$/i.test(e.tagName)) out.clipped.push(String(e.className && e.className.baseVal === undefined ? e.className : e.tagName) + ' ' + e.scrollWidth + '>' + e.clientWidth); });
                return out;
            }, [selector, !o || o.targets !== false]);
            check(label + ': it is there', !!r);
            if (!r) return;
            check(label + ': inside the window, targets of 44 px or more, no word cut, no sideways scroll of the page', r.box[0] >= -0.5 && r.box[1] >= -0.5 && r.box[2] <= w + 0.5 && r.box[3] <= h + 0.5 && r.small.length === 0 && r.clipped.length === 0 && !r.page, JSON.stringify(r));
        }
        for (const [name, w, h, ratio, touch] of SIZES) {
            const label = (x) => name + ': ' + x;
            async function hubPage(o) {
                signed.state.seats = [[C, 'Maya'], [C, 'Wren'], [E, ''], [E, '']];
                const context = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: ratio, hasTouch: touch, isMobile: touch });
                const page = await context.newPage();
                page.on('pageerror', (e) => errors.push('hub ' + name + ': ' + String(e && e.message || e)));
                await context.addInitScript(() => { try { localStorage.setItem('ants.name', 'Maya'); } catch (e) { /* none */ } });
                await page.goto(signed.origin + '/', { waitUntil: 'load' });
                await until('the bell is drawn', () => page.evaluate(() => { const b = document.querySelector('.hbar .al-bell'); return !!b && b.getClientRects().length > 0; }));
                await sleep(800);
                if (o && o.host) { await holdRoom(page); await sleep(800); }
                if (o && o.tab) await page.evaluate((t) => { const b = document.querySelector('.htab.t-' + t); if (b) b.click(); }, o.tab);
                return { context, page };
            }
            let { context, page } = await hubPage();
            signed.alerts(LIST());
            await until('the number is on the bell', () => page.evaluate(() => { const m = document.querySelector('.hbar .al-bell .al-mark'); return !!m && m.textContent === '3'; }));
            const mark = await box(page, '.hbar .al-bell .al-mark');
            check(label('the hub\'s list: three unread, the number on the bell, inside the window'), mark.l >= 0 && mark.t >= 0 && mark.r <= w, JSON.stringify(mark));
            await page.click('.hbar .al-bell'); await until('the panel', () => box(page, '.al-panel')); await sleep(200);
            await measure(page, '.al-panel', label('the list'), w, h);
            check(label('the list: an invitation with Join, a person who joined and a mention with Open, a match that is over with View, an old invitation dim, with no button'), await page.evaluate(() => { const it = Array.from(document.querySelectorAll('.al-panel .al-item')); return it.length === 5 && /Join/.test(it[0].textContent) && /Open/.test(it[1].textContent) && /Open/.test(it[2].textContent) && /Your match is over/.test(it[3].textContent) && /2nd place/.test(it[3].textContent) && /View/.test(it[3].textContent) && /dim/.test(it[4].className) && !it[4].querySelector('button'); }));
            await shot(page, name + '_hub_list');
            await page.click('.al-panel .al-gear'); await sleep(200);
            await measure(page, '.al-panel', label('the settings of a logged in visitor'), w, h);
            check(label('the settings: four rows (an invitation, a mention, a person who joined, my match) and the button that empties the list'), await page.evaluate(() => Array.from(document.querySelectorAll('.al-panel .al-line')).map((e) => e.getAttribute('data-switch')).join() === 'invite,mention,join,start' && !!document.querySelector('.al-panel [data-clear]')));
            await shot(page, name + '_hub_settings');
            await page.keyboard.press('Escape'); await page.keyboard.press('Escape'); await sleep(200);
            const fold = await page.$('.gc-fold[aria-expanded="true"]'); if (fold) { await fold.click(); await sleep(300); }
            signed.alert(invite(9, 'Pebble'));
            await until('the toast', () => box(page, '.al-toast:not([hidden])'));
            await measure(page, '.al-toast:not([hidden])', label('the invitation as a toast'), w, h);
            check(label('the toast says who invited you and to what, with Join'), await page.evaluate(() => /Pebble invited you to Treasure \(3 of 4\)\./.test(document.querySelector('.al-toast:not([hidden])').textContent) && /Join/.test(document.querySelector('.al-toast:not([hidden])').textContent)));
            await shot(page, name + '_invite_toast');
            await context.close();
            // the sheet of a person to invite (a room held), on a phone in the Players view
            ({ context, page } = await hubPage({ host: true, tab: touch ? 'players' : null }));
            await page.locator('.pl-card', { has: page.locator('.pl-name', { hasText: /^juniper$/ }) }).last().click({ button: 'right' });
            await until('the sheet', () => box(page, '.gm-box')); await sleep(250);
            await measure(page, '.gm-box', label('the sheet of a person'), w, h);
            check(label('the sheet offers Invite to my room, with the room and the free seats'), await page.evaluate(() => { const b = document.querySelector('.gm-box button[aria-describedby="gm-d-invite"]'); return !!b && /Invite to my room/.test(b.textContent) && /Treasure . 2 of 4 . 2 seats free/.test(b.textContent) && b.getAttribute('aria-disabled') !== 'true'; }));
            await shot(page, name + '_person_sheet');
            await context.close();
            // the helper lists in the chat box: the names of /invite, the people of @
            ({ context, page } = await hubPage({ host: true, tab: touch ? 'chat' : null }));
            await page.click('.gc-input'); await page.keyboard.type('/invite '); await sleep(450);
            await measure(page, '.gh-pop', label('the names of /invite'), w, h, { targets: false });
            check(label('/invite: the people to invite, the ones who cannot be invited now dim with the reason'), await page.evaluate(() => /People here, to invite/.test(document.querySelector('.gh-pop').textContent) && /juniper/.test(document.querySelector('.gh-pop').textContent) && /In your room/.test(document.querySelector('.gh-pop').textContent)));
            await shot(page, name + '_invite_names');
            await page.keyboard.press('Escape'); await page.fill('.gc-input', ''); await page.keyboard.type('hey @'); await sleep(450);
            await measure(page, '.gh-pop', label('the people of @'), w, h, { targets: false });
            check(label('@: the people to mention'), await page.evaluate(() => /People to mention/.test(document.querySelector('.gh-pop').textContent)));
            await shot(page, name + '_mention_names');
            await context.close();
        }
        signed.close(); guest.close(); longName.close();
        check('no error on any page', errors.length === 0, errors.join(' | '));
    } finally { await browser.close(); server.close(); }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
})().catch((e) => { console.log('FAIL ' + (e && e.stack || e)); process.exit(1); });
