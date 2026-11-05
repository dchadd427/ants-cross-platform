// OPT-IN, not part of ./run_tests.sh: the players list of Home in a REAL browser (Chromium through Playwright) against the REAL hub (`python3 -m hub`, with its test login), behind a small stand-in for the site's nginx that
// serves the files of web/ (stamped as the Dockerfile stamps them), a fake /lobbies (the Games list that the live site docks in the Yard: it has rooms that the list's Join can press) and /hub/ws (to the hub, with the
// account that a cookie names). The other people are raw sockets of the hub (their own masked frames, each from an address of its own, as nginx would tell the hub). It looks at what only a browser shows
// (docs/HOME_SCREEN.md, "The players list"):
//   1. the crowd of the approved pictures (a moderator, three who look, two who wait, two who play, four guests) at 1280 x 800, 900 x 700, 390 x 844, 844 x 390 and 568 x 320, and with none, 3, 12 and 200 others: the
//      panel, the button or the sheet is inside the window, the page does not scroll sideways, the panel and the corner button are clear of the Games list and the chat dock, no Join (the list's or the panel's) and no
//      field is under anything else, the bar of a phone keeps its three rows and the logo is not covered, and the count of the list is the count of the chat;
//   2. the sheet: opened by the bar's button or the corner's, a dialog with the focus on Close, over the Menu, Escape and the scrim close it and the focus goes back, Tab stays inside it, it clears a phone's system bar;
//   3. the states: nobody else (the note and the Host button), the hub gone ("Players list is off right now", the chat says it too) and back by itself, a person who comes, goes and changes what they do;
//   4. Join: the Games list's own Join of that room is pressed (also from the sheet and the drawer);
//   5. a hub that has no list (an older one): nothing shows, and Home is the same as with the list's two files not there at all, but for a few pixels at the edges of rounded buttons that Chromium draws either way (five sizes);
//   6. the Room: the list is not on it, and it is back on Home;
//   7. the bar of a phone at any count (13, 99, 100, 226, 480 people on the real hub, 9999 from a fake one) at 320, 360, 370, 375, 380, 390 and 412 px: nothing is cut off, nothing covers the logo or another
//      button, the Menu keeps room at the edge, the button says 99+ over 99 and the sheet shows the whole number;
//   8. the Games list that the live site docks in the Yard, at 1280 x 800, 1366 x 768, 1536 x 864, 1920 x 1080 and 2560 x 1440 with the list absent, open and folded and the chat dock open and folded: the
//      Games list is centered in the window (to 1 px), clear of both panels, and no Join is under anything else.
//   --shots DIR takes the pictures of the built page (built_*.png), the ones to put beside the approved pictures.
//
//   node tests/scripts/web_home_players_browser.js [--web DIR] [--shots DIR] [--only 1,3]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm), a Chromium that it can start and python3 with the hub's own folder (hub/); without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be
// made), 1 a check failed or the page broke.
'use strict';
const fs = require('fs');
const os = require('os');
const net = require('net');
const http = require('http');
const path = require('path');
const crypto = require('crypto');
const { spawn, spawnSync } = require('child_process');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the players list was NOT run in a real browser'); process.exit(0); }
const python = spawnSync('python3', ['-c', 'import hub'], { cwd: path.resolve(__dirname, '..', '..') });
if (python.status !== 0) { console.log('SKIP: python3 cannot import the hub: the players list was NOT run against the real hub'); process.exit(0); }

const ROOT = path.resolve(__dirname, '..', '..');
const arg = (name) => { const i = process.argv.indexOf(name); return i === -1 ? null : process.argv[i + 1]; };
const WEB = path.resolve(arg('--web') || path.join(ROOT, 'web'));
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const ONLY = arg('--only') ? arg('--only').split(',') : null;
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml' };
const { startHub: startFakeHub } = require('./fake_hub.js');
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const freePort = () => new Promise((resolve, reject) => { const s = net.createServer(); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); }); s.on('error', reject); });
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

const LOBBIES = { format: 1, count: 3, lobbies: [
    { code: 'c1v3rr', host: 'Clover', map: 'TREASURE.LVL', mode: 'highest-score', people: 2, bots: 1, free: 1, size: 4, age: 200 },
    { code: 'p4n8dz', host: 'Pebble', map: 'ISLANDS.LVL', mode: 'highest-score', people: 3, bots: 0, free: 1, size: 4, age: 312 },
    { code: 'f3rn22', host: 'Fern', map: 'MEDIUM.LVL', mode: 'highest-score', people: 1, bots: 0, free: 3, size: 4, age: 30 }] };
const SIZES = [['desktop', 1280, 800, false], ['tablet', 900, 700, false], ['phone', 390, 844, true], ['side', 844, 390, true], ['small side', 568, 320, true]];
const NOISE = 12;       // pixels that two loads of the same page may draw differently, by more than 24 levels (the edges of rounded buttons: also with the list's files not there at all)
const PICTURE = [   // the crowd of the approved pictures: [login, what it says]
    ['9001:tess', { at: 'look' }], ['2001:sage', { at: 'look' }], ['2002:fern', { at: 'look' }], ['2003:basil', { at: 'look' }],
    ['2004:clover', { at: 'wait', pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr' }], ['2005:juniper', { at: 'wait' }],
    ['2006:pebble', { at: 'play', pub: true, map: 'ISLANDS.LVL', n: 3 }], ['2007:hazel', { at: 'play', pub: true, map: 'GAUNTLET.LVL', n: 2 }]];

// ---- the real hub behind the stand-in site -----------------------------------------------------------------------------------------------------------------------------------------------------------------
async function startRig(o) {
    o = o || {};
    const [hubPort, sitePort] = [await freePort(), await freePort()];
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ants_players_browser.'));
    const origin = 'http://127.0.0.1:' + sitePort;
    const hub = spawn('python3', ['-m', 'hub'], { cwd: ROOT, env: Object.assign({}, process.env, { ANTS_HUB_PORT: String(hubPort), ANTS_HUB_HOST: '127.0.0.1', ANTS_HUB_DB: path.join(dir, 'hub.db'), ANTS_HUB_PUBLIC_URL: origin, ANTS_HUB_MODS: '9001', ANTS_HUB_TEST_LOGIN: '1' }), stdio: ['ignore', 'pipe', 'pipe'] });
    let log = '';
    hub.stdout.on('data', (d) => { log += d; });
    hub.stderr.on('data', (d) => { log += d; });
    await until('the hub answers', () => new Promise((resolve) => http.get({ host: '127.0.0.1', port: hubPort, path: '/hub/healthz' }, (res) => { res.resume(); resolve(res.statusCode === 200); }).on('error', () => resolve(false))), 15000).catch((e) => { console.log(log); throw e; });
    const open = new Set();
    const r = { hub, hubPort, origin, dir, on: true, blocked: false, lobbies: LOBBIES, log: () => log, clients: [], cut() { r.on = false; open.forEach((x) => x.destroy()); open.clear(); }, restore() { r.on = true; } };
    const label = o.label || '';
    const stamp = (text) => text.replace(/@@SITE_TITLE@@/g, label ? ' (' + label + ')' : '').replace(/@@SITE_FOOTER@@/g, label ? '<strong id="site-label">' + label + '</strong>&#8197;&bull;&#8197;' : '').replace(/@@GAME_VERSION@@/g, '0.13.0').replace(/@@BUILD_ID@@/g, 'players').replace(/@@BUILD_DATE@@/g, '2026-11-01 00:00').replace(/@@BUILD_TIMESTAMP@@/g, '2026-11-01');
    const site = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        if (url.pathname === '/lobbies') { res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(r.lobbies)); return; }
        if (url.pathname === '/stats' || url.pathname === '/busy') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end('{}'); return; }
        const file = url.pathname === '/' ? 'lobby.html' : decodeURIComponent(url.pathname).replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile() || (r.blocked && /^home\/players\./.test(file))) { res.writeHead(404, { 'Content-Type': 'text/plain' }); res.end('not here'); return; }
        const type = TYPES[path.extname(full)] || 'application/octet-stream';
        res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' });
        res.end(type.startsWith('text/html') ? stamp(fs.readFileSync(full, 'utf8')) : fs.readFileSync(full));
    });
    site.on('upgrade', (req, socket, head) => {             // /hub/ws to the hub, with the account that the cookie names (or a 503 as nginx says it when the hub is not there)
        if (new URL(req.url, 'http://x').pathname !== '/hub/ws') { socket.destroy(); return; }
        if (!r.on) { socket.end('HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{"error":"hub_off"}'); return; }
        const who = /(?:^|;\s*)acct=([0-9]+_[a-z0-9_.]+)/.exec(req.headers.cookie || '');
        const headers = Object.assign({}, req.headers);
        delete headers.cookie;
        if (who) { const at = who[1].indexOf('_'); headers['x-hub-test-login'] = who[1].slice(0, at) + ':' + who[1].slice(at + 1); }
        const up = net.connect(hubPort, '127.0.0.1', () => {
            up.write('GET /hub/ws HTTP/1.1\r\n' + Object.entries(headers).map(([k, v]) => k + ': ' + v).join('\r\n') + '\r\n\r\n');
            if (head && head.length) up.write(head);
            up.pipe(socket); socket.pipe(up);
            open.add(socket); socket.on('close', () => open.delete(socket));
        });
        up.on('error', () => socket.destroy()); socket.on('error', () => up.destroy());
    });
    await new Promise((resolve) => site.listen(sitePort, '127.0.0.1', resolve));
    r.site = site;
    r.stop = () => { r.clients.forEach((c) => c.close()); hub.kill('SIGTERM'); site.close(); try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) { /* it stays */ } };
    return r;
}
let addresses = 0;
function person(r, login, says) {                           // a raw socket of the hub: a person (login 'id:name') or a guest (none); says: what its page says it does
    return new Promise((resolve, reject) => {
        const headers = { Connection: 'Upgrade', Upgrade: 'websocket', 'Sec-WebSocket-Key': crypto.randomBytes(16).toString('base64'), 'Sec-WebSocket-Version': '13', Origin: r.origin, 'X-Client-Address': '10.2.' + (addresses >> 8) + '.' + (addresses++ & 255) };
        if (login) headers['X-Hub-Test-Login'] = login;
        const req = http.request({ host: '127.0.0.1', port: r.hubPort, path: '/hub/ws', headers });
        req.on('response', (res) => reject(new Error('refused ' + res.statusCode)));
        req.on('upgrade', (res, socket) => {
            const send = (obj) => {
                const p = Buffer.from(JSON.stringify(obj)), m = crypto.randomBytes(4), body = Buffer.from(p);
                for (let i = 0; i < body.length; i++) body[i] ^= m[i & 3];
                const head = p.length < 126 ? Buffer.from([0x81, 0x80 | p.length]) : Buffer.from([0x81, 0x80 | 126, p.length >> 8, p.length & 255]);
                socket.write(Buffer.concat([head, m, body]));
            };
            socket.on('data', () => {}); socket.on('error', () => {});
            if (says) send(Object.assign({ t: 'here' }, says));
            const client = { send, close: () => socket.destroy() };
            r.clients.push(client);
            resolve(client);
        });
        req.end();
    });
}
async function crowd(r, people, guests) {                    // replaces the other people on the hub: people: [login, says], guests: how many
    r.clients.splice(0).forEach((c) => c.close());
    await sleep(150);
    for (const [login, says] of people) await person(r, login, says);
    for (let i = 0; i < guests; i++) await person(r, null, null);
}
const many = (n) => Array.from({ length: n }, (_, i) => ['' + (4000 + i) + ':p' + String(i).padStart(3, '0'), i % 7 === 0 ? { at: 'wait', pub: true, map: 'TREASURE.LVL', n: 1 + (i % 3), of: 4, code: 'r' + i } : i % 11 === 0 ? { at: 'play', pub: true, map: 'ISLANDS.LVL', n: 2 } : { at: 'look' }]);

// ---- a page -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
let browser = null;
const errors = [];
async function openPage(r, size, account, o) {
    o = o || {};
    const [, w, h, touch] = size;
    const context = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: 1, hasTouch: !!touch, isMobile: !!touch && w < 500 });
    if (account) await context.addCookies([{ name: 'acct', value: account, url: r.origin }]);
    const page = await context.newPage();
    page.on('pageerror', (e) => errors.push(size[0] + ': ' + String(e && e.message || e)));
    await page.goto(r.origin + '/', { waitUntil: 'load' });
    await page.waitForSelector('#gchat', { timeout: 15000 });
    if (!o.noGames) await page.waitForSelector('.gpop .gitem', { timeout: 15000 });
    return page;
}
const listShows = (page, text) => until('the list shows ' + text, () => page.evaluate((t) => { const p = document.getElementById('players'); return !!p && p.textContent.indexOf(t) !== -1 && !p.closest('.pl-root').hidden; }, text), 15000);
async function shot(page, name) { if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, 'built_' + name + '.png') }); } }
const openList = async (page) => {                         // the button that opens the list in this window (none in a wide one)
    const kind = await page.evaluate(() => { const v = (e) => !!e && e.getClientRects().length > 0; return v(document.querySelector('.pl-bar')) ? '.pl-bar' : v(document.querySelector('.pl-btn')) ? '.pl-btn' : ''; });
    if (kind) await page.click(kind);
    return kind;
};

// what a page must be at any size and with any crowd (the checks of docs/HOME_SCREEN.md that the list must not break)
const LAYOUT = () => {
    const R = (e) => { if (!e || !e.getClientRects().length) return null; const q = e.getBoundingClientRect(); return { l: q.left, t: q.top, r: q.right, b: q.bottom }; };
    const meet = (a, b) => a && b && a.l < b.r - 0.5 && b.l < a.r - 0.5 && a.t < b.b - 0.5 && b.t < a.b - 0.5;
    const panel = document.getElementById('players'), open = !!panel && panel.getAttribute('data-sheet') === 'true';
    const pl = R(panel), btn = R(document.querySelector('.pl-btn')), bar = R(document.querySelector('.pl-bar')), dock = R(document.querySelector('.gdock')), chat = R(document.getElementById('gchat')), logo = R(document.querySelector('.hbar .hlogo'));
    const iw = innerWidth, ih = innerHeight, out = [];
    [['panel', pl], ['corner button', btn], ['bar button', bar]].forEach(([n, q]) => { if (q && (q.l < -0.5 || q.t < -0.5 || q.r > iw + 0.5 || q.b > ih + 0.5)) out.push(n + ' outside the window ' + JSON.stringify(q)); });
    if (document.documentElement.scrollWidth > iw) out.push('sideways scroll');
    if (pl && !open && meet(pl, dock)) out.push('the docked panel is over the Games list');
    if (pl && !open && meet(pl, chat)) out.push('the docked panel is over the chat dock');
    if (btn && meet(btn, dock)) out.push('the corner button is over the Games list');
    if (btn && meet(btn, chat)) out.push('the corner button is over the chat dock');
    if (bar && logo && logo.r > bar.l - 4) out.push('the bar\'s button covers the logo');
    const kids = [...document.querySelectorAll('.hbar .hright > *')].filter((e) => e.getClientRects().length).map((e) => e.getBoundingClientRect());
    if (kids.some((k) => k.left < 0 || k.right > iw + 0.5)) out.push('a button of the bar is cut off');
    for (let i = 0; i < kids.length; i++) for (let j = i + 1; j < kids.length; j++) if (kids[i].left < kids[j].right - 0.5 && kids[j].left < kids[i].right - 0.5 && kids[i].top < kids[j].bottom - 0.5 && kids[j].top < kids[i].bottom - 0.5) out.push('two buttons of the bar overlap');
    const covered = [...document.querySelectorAll('.gjoin, .pl-join, .gcode input, .pl-host')].filter((e) => {
        if (!e.getClientRects().length) return false;
        const q = e.getBoundingClientRect(), box = e.closest('.gpop, .pl-list, .pl-body'), bq = box && box.getBoundingClientRect();
        if (bq && (q.top < bq.top - 0.5 || q.bottom > bq.bottom + 0.5)) return false;                  // (scrolled out of its own box: not on the screen)
        if (q.top < 0 || q.bottom > ih || q.left < 0 || q.right > iw) return false;
        const top = document.elementFromPoint(q.left + q.width / 2, q.top + q.height / 2);
        return !(top === e || e.contains(top));
    }).length;
    if (covered) out.push(covered + ' Join or field under something else');
    return { out, hasPanel: !!pl, hasCorner: !!btn, hasBar: !!bar, barHeight: document.querySelector('.hbar').getBoundingClientRect().height };
};
// two pictures of a page, compared in a browser's canvas: how many pixels differ by more than a few levels of any color (the grain of the bar and the edges of the shadows are not drawn twice to the last level)
async function pixelsThatDiffer(a, b) {
    const context = await browser.newContext();
    try {
        const page = await context.newPage();
        return await page.evaluate(async ([x, y]) => {
            const load = (s) => new Promise((resolve, reject) => { const i = new Image(); i.onload = () => resolve(i); i.onerror = reject; i.src = 'data:image/png;base64,' + s; });
            const [ia, ib] = [await load(x), await load(y)];
            if (ia.width !== ib.width || ia.height !== ib.height) return { size: false, n: -1, worst: 0 };
            const data = (img) => { const c = document.createElement('canvas'); c.width = img.width; c.height = img.height; const g = c.getContext('2d'); g.drawImage(img, 0, 0); return g.getImageData(0, 0, img.width, img.height).data; };
            const da = data(ia), db = data(ib);
            let n = 0, worst = 0;
            for (let i = 0; i < da.length; i += 4) { const d = Math.max(Math.abs(da[i] - db[i]), Math.abs(da[i + 1] - db[i + 1]), Math.abs(da[i + 2] - db[i + 2])); if (d > worst) worst = d; if (d > 24) n++; }
            return { size: true, n, worst };
        }, [a.toString('base64'), b.toString('base64')]);
    } finally { await context.close(); }
}
const countsAgree = (page) => page.evaluate(() => {
    const chat = /^(\d+|500\+) here now$/.exec((document.querySelector('.gc-on') || {}).textContent || '');
    const head = document.querySelector('.pl-count'), button = [...document.querySelectorAll('.pl-btn .pill, .pl-bar .pill')].find((e) => e.getClientRects().length);
    return { chat: chat ? chat[1] : null, head: head && !head.hidden ? head.textContent : null, button: button && !button.hidden ? button.textContent : null };
});
const phoneChat = async (page, w, h) => {                  // a page that opened at this size has the dock as a fresh page has it: open on a desktop, a strip on a phone or in a short window
    const want = w >= 721 && h >= 501 ? 'true' : 'false';
    if ((await page.getAttribute('#gchat', 'data-open')) !== want) { await page.evaluate(() => document.querySelector('#gchat .gc-fold').click()); await sleep(100); }
};

async function main() {
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the players list was NOT run in a real browser'); return; }
    const want = (n) => !ONLY || ONLY.indexOf(String(n)) !== -1;
    const r = await startRig();
    try {
        // ---- 1. the crowd of the pictures, and none, 3, 12 and 200 others, at the five sizes -------------------------------------------------------------------------------------------------------------------
        if (want(1)) {
            const bare = {};
            r.blocked = true;
            for (const size of SIZES) { const page = await openPage(r, size, '1000_maple'); await phoneChat(page, size[1], size[2]); bare[size[0]] = (await page.evaluate(LAYOUT)).barHeight; await page.context().close(); }
            r.blocked = false;
            const crowds = [['none', [], 0], ['3 others', PICTURE.slice(0, 3), 1], ['12 others (the pictures\')', PICTURE, 4], ['200 others', many(205), 20]];
            for (const [what, people, guests] of crowds) {
                await crowd(r, people, guests);
                for (const size of SIZES) {
                    const page = await openPage(r, size, '1000_maple');
                    await phoneChat(page, size[1], size[2]);
                    const others = people.length + guests;
                    if (others) await listShows(page, people.length ? people[0][0].split(':')[1] : 'guest'); else await listShows(page, 'Just you here');
                    await sleep(400);
                    const m = await page.evaluate(LAYOUT);
                    check(what + ', ' + size[0] + ' ' + size[1] + ' x ' + size[2] + ': the list is in the window, there is no sideways scroll, nothing of the Games list, the chat dock or a Join is covered, the logo is free', m.out.length === 0 && (m.hasPanel || m.hasCorner || m.hasBar), JSON.stringify(m));
                    check('... the bar is as high as it is without the list (' + bare[size[0]] + ' px)', Math.abs(m.barHeight - bare[size[0]]) < 0.5, m.barHeight + ' / ' + bare[size[0]]);
                    const c = await countsAgree(page);
                    const total = people.length + guests + 1, shown = total > 500 ? '500+' : String(total);
                    check('... the count of the list is the count of the chat (' + total + ' people: the others and the visitor)' + (total > 1 ? '' : ' (one alone: no number)'), c.chat === String(total) && (total > 1 ? (c.head === shown || c.button === shown) : (c.head === null && c.button === null)), JSON.stringify(c));
                    await shot(page, what.replace(/[^a-z0-9]+/gi, '_').replace(/_$/, '') + '_' + size[1] + 'x' + size[2]);
                    const kind = await openList(page);
                    if (kind) {
                        await until('the sheet is open', () => page.evaluate(() => document.getElementById('players').getAttribute('data-sheet') === 'true'), 3000);
                        await sleep(150);
                        const o = await page.evaluate(LAYOUT);
                        const inside = await page.evaluate(() => { const q = document.getElementById('players').getBoundingClientRect(); return q.left >= -0.5 && q.top >= -0.5 && q.right <= innerWidth + 0.5 && q.bottom <= innerHeight + 0.5 && q.width > 150 && q.height > 100; });
                        check(what + ', ' + size[0] + ': the open sheet is inside the window, nothing sideways', o.out.filter((x) => x.indexOf('under something') === -1).length === 0 && inside, JSON.stringify(o));
                        const scrolls = await page.evaluate(() => { const l = document.querySelector('.pl-list'); return { more: l.scrollHeight > l.clientHeight + 2, rows: document.querySelectorAll('.pl-card').length }; });
                        check('... the list scrolls inside the sheet when it is long (200 others: it is, 3: it need not be)', what !== '200 others' || (scrolls.more && scrolls.rows >= 150), JSON.stringify(scrolls));
                        await shot(page, what.replace(/[^a-z0-9]+/gi, '_').replace(/_$/, '') + '_' + size[1] + 'x' + size[2] + '_open');
                        await page.keyboard.press('Escape');
                        await sleep(100);
                    }
                    await page.context().close();
                }
            }
        }

        // ---- 2. the sheet: a dialog over the Menu, Escape and the scrim, the focus, Tab, the system bar ----------------------------------------------------------------------------------------------------
        if (want(2)) {
            await crowd(r, PICTURE, 4);
            for (const size of [SIZES[1], SIZES[2], SIZES[3]]) {
                const page = await openPage(r, size, '1000_maple');
                await phoneChat(page, size[1], size[2]);
                await listShows(page, 'clover');
                const kind = await page.evaluate(() => (document.querySelector('.pl-bar') && document.querySelector('.pl-bar').getClientRects().length ? '.pl-bar' : '.pl-btn'));
                check(size[0] + ': the button says what it opens and that it is closed', await page.evaluate((k) => { const b = document.querySelector(k); return b.getAttribute('aria-haspopup') === 'dialog' && b.getAttribute('aria-expanded') === 'false' && /^Players \(\d+\)$/.test(b.getAttribute('aria-label')); }, kind));
                await page.focus(kind); await page.keyboard.press('Enter');
                await until('open', () => page.evaluate(() => document.getElementById('players').getAttribute('data-sheet') === 'true'), 3000);
                const o = await page.evaluate((k) => { const p = document.getElementById('players'), a = document.activeElement; return { role: p.getAttribute('role'), modal: p.getAttribute('aria-modal'), name: p.getAttribute('aria-labelledby'), title: document.getElementById('players-title').textContent, focus: a && a.className, expanded: document.querySelector(k).getAttribute('aria-expanded'), scrim: !!document.querySelector('.pl-scrim') && document.querySelector('.pl-scrim').getClientRects().length > 0 }; }, kind);
                check(size[0] + ': the keyboard opens it: a modal dialog named "Players", over a scrim, the focus on Close', o.role === 'dialog' && o.modal === 'true' && o.name === 'players-title' && o.title === 'Players' && /pl-close/.test(o.focus || '') && o.expanded === 'true' && o.scrim, JSON.stringify(o));
                const order = [];
                for (let i = 0; i < 40; i++) {
                    await page.keyboard.press('Tab');
                    const at = await page.evaluate(() => { const a = document.activeElement; return { in: !!a.closest('#players'), what: a.className.split(' ').slice(-1)[0] || a.tagName }; });
                    order.push(at);
                    if (!at.in) break;
                }
                check(size[0] + ': Tab stays inside the sheet (Close, the list, the Join of the room that waits, and round again): ' + order.slice(0, 5).map((x) => x.what).join(' > '), order.every((x) => x.in) && order.length === 40 && order.slice(0, 4).map((x) => x.what).indexOf('pl-join') !== -1, JSON.stringify(order.slice(0, 8)));
                await page.keyboard.press('Shift+Tab');
                check(size[0] + ': Shift+Tab goes back inside it too', await page.evaluate(() => !!document.activeElement.closest('#players')));
                await page.keyboard.press('Escape');
                await sleep(100);
                check(size[0] + ': Escape closes it and the focus is on the button that opened it', await page.evaluate((k) => document.getElementById('players').getAttribute('data-sheet') === 'false' && document.activeElement === document.querySelector(k), kind));
                await page.click(kind);
                await sleep(100);
                await page.mouse.click(size[1] / 2, 4);
                await sleep(100);
                check(size[0] + ': a press on the scrim closes it', await page.evaluate(() => document.getElementById('players').getAttribute('data-sheet') === 'false'));
                // the Menu is under the sheet (where the Menu leaves the button free to press: a phone's bar; the Menu of the other windows covers the corner button, as a popup covers what is under it)
                if (kind === '.pl-bar') {
                    await page.click('.hmenu'); await page.waitForSelector('.hmenubox:not([hidden])');
                    await page.click(kind);
                    await sleep(100);
                    const top = await page.evaluate(() => { const q = document.getElementById('players').getBoundingClientRect(), a = document.elementFromPoint(q.left + q.width / 2, q.top + 30); return { sheet: !!a && !!a.closest('#players'), menu: !document.querySelector('.hmenubox').hidden }; });
                    check(size[0] + ': the sheet is over the Menu (a press on the button put the Menu away)', top.sheet && !top.menu, JSON.stringify(top));
                    await page.keyboard.press('Escape');
                }
                await page.context().close();
            }
            // a phone whose page reaches under the system bar: the sheet clears it
            for (const [w, h, inset] of [[390, 880, 36], [448, 998, 36]]) {
                const page = await openPage(r, ['phone', w, h, true], '1000_maple');
                await (await page.context().newCDPSession(page)).send('Emulation.setSafeAreaInsetsOverride', { insets: { top: 0, left: 0, bottom: inset, right: 0 } });
                await phoneChat(page, w, h); await listShows(page, 'clover');
                await page.click('.pl-bar'); await sleep(200);
                const b = await page.evaluate(() => { const q = document.getElementById('players').getBoundingClientRect(); return { bottom: q.bottom, end: innerHeight }; });
                check('a phone with the system bar over the last ' + inset + ' px (' + w + ' x ' + h + '): the sheet ends above it, 12 px over', b.bottom <= h - inset - 11.5 && b.bottom >= h - inset - 13, JSON.stringify(b));
                await page.context().close();
            }
        }

        // ---- 3. the states: alone, a person who comes and goes and changes, the hub gone and back -------------------------------------------------------------------------------------------------------
        if (want(3)) {
            await crowd(r, [], 0);
            const page = await openPage(r, SIZES[0], '1000_maple');
            await listShows(page, 'Just you here');
            check('alone: the note, the visitor first (as "you", looking), no count, and Host a game that is lit once Quick host is', await page.evaluate(() => { const p = document.getElementById('players'); const c = document.querySelector('.pl-count'); const h = p.querySelector('.pl-host'); return p.querySelector('.pl-you .pl-tag').textContent === 'you' && /Looking/.test(p.querySelector('.pl-you').textContent) && c.hidden && !!h && !h.disabled; }));
            const joiner = await person(r, '5001:willow', { at: 'look' });
            await listShows(page, 'willow');
            check('a person comes: the row, the group "Looking 1", the count (2 here: the chat says it too) within the pace of the hub', await page.evaluate(() => { const g = [...document.querySelectorAll('.pl-group')].map((e) => e.getAttribute('data-g') + ':' + e.querySelector('.pl-gn').textContent); return g.join() === 'look:1' && document.querySelector('.pl-count').textContent === '2'; }));
            await until('the chat says 2 too', async () => (await countsAgree(page)).chat === '2', 8000);
            joiner.send({ t: 'here', at: 'wait', pub: true, map: 'TINY.LVL', n: 1, of: 2, code: 'w1ll0w' });
            await until('willow waits', () => page.evaluate(() => document.querySelector('.pl-group[data-g="wait"]') && document.querySelector('.pl-group[data-g="wait"]').textContent.indexOf('Tiny · 1 of 2') !== -1), 8000);
            check('... who waits in a public room: the group "Waiting", the room by its map and seats, and no Join while the Games list has not got the room', await page.evaluate(() => !document.querySelector('.pl-group[data-g="look"]') && !!document.querySelector('.pl-group[data-g="wait"]') && !document.querySelector('.pl-join')));
            const rooms = r.lobbies;
            r.lobbies = { format: 1, count: rooms.lobbies.length + 1, lobbies: rooms.lobbies.concat([{ code: 'w1ll0w', host: 'Willow', map: 'TINY.LVL', mode: 'highest-score', people: 1, bots: 0, free: 1, size: 2, age: 5 }]) };
            await until('the Games list has the room', () => page.evaluate(() => !!document.querySelector('.gdock .gjoin[data-code="w1ll0w"]')), 20000);
            await until('the list has a Join for it', () => page.evaluate(() => !!document.querySelector('.pl-group[data-g="wait"] .pl-join[data-code="w1ll0w"]')), 5000);
            r.lobbies = rooms;
            check('... the Games list gets the room: the Join comes, and it is the one of the room', await page.evaluate(() => document.querySelectorAll('.pl-join').length === 1 && !!document.querySelector('.pl-group[data-g="wait"] .pl-join[data-code="w1ll0w"]')));
            joiner.send({ t: 'here', at: 'play', pub: true, map: 'TINY.LVL', n: 2 });
            await until('willow plays', () => page.evaluate(() => !!document.querySelector('.pl-group[data-g="play"]')), 8000);
            check('... who plays: "Playing", the map and the players, no Join', await page.evaluate(() => /Tiny · 2 players/.test(document.querySelector('.pl-group[data-g="play"]').textContent) && !document.querySelector('.pl-join')));
            joiner.close();
            await until('willow is gone', () => page.evaluate(() => document.getElementById('players').textContent.indexOf('willow') === -1 && /Just you here/.test(document.getElementById('players').textContent)), 8000);
            check('... who goes: the row is gone and so is the count (nothing of them stays: just you here)', await page.evaluate(() => document.getElementById('players').textContent.indexOf('willow') === -1 && document.querySelector('.pl-count').hidden));
            // the hub goes and comes back by itself
            await crowd(r, PICTURE, 2);
            await listShows(page, 'clover');
            r.cut();
            await until('the list says that it is off', () => page.evaluate(() => document.getElementById('players').getAttribute('data-state') === 'off' && /Players list is off right now/.test(document.getElementById('players').textContent)), 8000);
            const off = await page.evaluate(() => ({ chat: (document.querySelector('.gc-note') || {}).textContent, rows: document.querySelectorAll('.pl-card').length, count: document.querySelector('.pl-count').hidden, dot: !document.querySelector('.pl-dot').hidden, panel: document.getElementById('players').getClientRects().length > 0 }));
            check('the hub is gone: the list says "Players list is off right now" (the chat dock says "Chat is offline"), no rows, no count, a grey dot, and the panel stays', off.chat === 'Chat is offline' && off.rows === 0 && off.count && off.dot && off.panel, JSON.stringify(off));
            await shot(page, 'off_1280x800');
            const keep = await page.evaluate(LAYOUT);
            check('... Home is whole (nothing covered, nothing sideways)', keep.out.length === 0, JSON.stringify(keep));
            r.restore();
            await until('the list is back', () => page.evaluate(() => document.getElementById('players').getAttribute('data-state') === 'on' && document.getElementById('players').textContent.indexOf('clover') !== -1), 15000);
            check('the hub is back: the list is back by itself, with the people that are there', await page.evaluate(() => document.querySelectorAll('.pl-group').length === 4));
            await page.context().close();
        }

        // ---- 4. Join: the Games list's own Join of that room is pressed (the docked list, from the panel, the drawer and the sheet) -----------------------------------------------------------------------
        if (want(4)) {
            await crowd(r, PICTURE, 4);
            for (const size of [SIZES[0], SIZES[1], SIZES[2]]) {
                const page = await openPage(r, size, '1000_maple');
                await phoneChat(page, size[1], size[2]);
                await listShows(page, 'clover');
                await openList(page);
                await sleep(200);
                await page.evaluate(() => { window.__pressed = []; document.querySelector('.gdock').addEventListener('click', (e) => { const b = e.target.closest && e.target.closest('.gjoin'); if (b) window.__pressed.push(b.getAttribute('data-code')); }, true); });
                await page.click('.pl-join[data-code="c1v3rr"]');
                await sleep(300);
                const o = await page.evaluate(() => ({ pressed: window.__pressed, sheet: document.getElementById('players').getAttribute('data-sheet'), joining: !!document.querySelector('.gjoining:not([hidden])') || !!document.querySelector('.gpnote:not([hidden])') || !!document.querySelector('.gconfirm') || document.body.getAttribute('data-screen') === 'room' }));
                check(size[0] + ': Join in the list presses the Games list\'s own Join of that room (c1v3rr), the sheet is put away, and the list\'s own flow goes on', o.pressed.join() === 'c1v3rr' && (size[0] === 'desktop' || o.sheet === 'false') && o.joining, JSON.stringify(o));
                await page.context().close();
            }
            // a list that is not in the Yard (the test site: a popover that is closed draws no rows): the panel has no Join for a room that the Games list does not hold, and has it once the list was opened
            const label = await startRig({ label: 'staging' });
            try {
                await person(label, '2004:clover', { at: 'wait', pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr' });
                const page = await openPage(label, SIZES[0], '1000_maple', { noGames: true });
                await listShows(page, 'clover');
                await sleep(1500);
                check('the test site (the Games list is a popover that is closed and has drawn nothing): the room that waits has no Join in the list (what a person says is not enough for a Join)', await page.evaluate(() => document.querySelectorAll('.pl-join').length === 0 && !document.querySelector('.gjoin')));
                await page.click('.games');
                await until('the Games list draws the room', () => page.evaluate(() => !!document.querySelector('.gpop .gjoin[data-code="c1v3rr"]')), 10000);
                await page.click('.games');
                await until('the list has the Join now', () => page.evaluate(() => !!document.querySelector('.pl-join[data-code="c1v3rr"]')), 5000);
                await page.evaluate(() => { window.__pressed = []; document.querySelector('.gpop').addEventListener('click', (e) => { const b = e.target.closest && e.target.closest('.gjoin'); if (b) window.__pressed.push(b.getAttribute('data-code')); }, true); });
                await page.click('.pl-join[data-code="c1v3rr"]');
                await sleep(500);
                const after = await page.evaluate(() => ({ pressed: window.__pressed, joining: !!document.querySelector('.gjoining:not([hidden])') || !!document.querySelector('.gpnote:not([hidden])') || !!document.querySelector('.gconfirm') }));
                check('... the Games list has drawn the room: the Join presses the list\'s own Join of it, and the list\'s own flow goes on', after.pressed.join() === 'c1v3rr' && after.joining, JSON.stringify(after));
                await shot(page, 'test_site_join_1280x800');
                await page.context().close();
            } finally { label.stop(); }
        }

        // ---- 6. the Room: not on it; Home again: back --------------------------------------------------------------------------------------------------------------------------------------------------------------
        if (want(6)) {
            await crowd(r, PICTURE, 4);
            const page = await openPage(r, SIZES[0], '1000_maple');
            await listShows(page, 'clover');
            await page.click('.hchip.mine');
            await sleep(300);
            check('the Room shows: the list is not on it (Home is hidden with it)', await page.evaluate(() => document.body.getAttribute('data-screen') === 'room' && !document.getElementById('players').getClientRects().length));
            await page.click('.rhome');
            await sleep(300);
            check('Home again: the list is there, the same one', await page.evaluate(() => document.body.getAttribute('data-screen') === 'home' && document.getElementById('players').getClientRects().length > 0 && document.getElementById('players').textContent.indexOf('clover') !== -1));
            await page.context().close();
        }

        // ---- 5. a hub that has no list: nothing shows, and Home is the same pixel for pixel as with the list's files not there --------------------------------------------------------------------------------
        if (want(5)) {
            const fake = await startFakeSite();
            try {
                for (const size of SIZES) {
                    const shots = [];
                    for (const blocked of [false, true]) {
                        fake.blocked = blocked;
                        const context = await browser.newContext({ viewport: { width: size[1], height: size[2] }, deviceScaleFactor: 1, hasTouch: !!size[3], isMobile: !!size[3] && size[1] < 500 });
                        const page = await context.newPage();
                        page.on('pageerror', (e) => errors.push(size[0] + ': ' + String(e && e.message || e)));
                        await page.goto(fake.url + '/', { waitUntil: 'load' });
                        await page.waitForSelector('#gchat'); await page.waitForSelector('.gpop .gitem');
                        await until('the dock is live', () => page.evaluate(() => document.getElementById('gchat').getAttribute('data-state') === 'live'), 8000);
                        await sleep(1500);
                        await phoneChat(page, size[1], size[2]);
                        await sleep(300);
                        const shown = await page.evaluate(() => { const p = document.getElementById('players'); return { panel: !!p && !!p.closest('.pl-root') && !p.closest('.pl-root').hidden, cls: document.getElementById('home').className, anyButton: [...document.querySelectorAll('.pl-btn, .pl-bar')].some((e) => e.getClientRects().length) }; });
                        let png = await page.screenshot({ animations: 'disabled', caret: 'hide' });
                        for (let again = 0; again < 6; again++) { await sleep(250); const next = await page.screenshot({ animations: 'disabled', caret: 'hide' }); if (next.equals(png)) break; png = next; }          // (a picture that is the same twice in a row: the page has settled)
                        shots.push({ png, shown });
                        await context.close();
                    }
                    check('a hub that has no list (' + size[0] + ' ' + size[1] + ' x ' + size[2] + '): nothing of the list shows (no panel, no button, no class on Home)', !shots[0].shown.panel && !shots[0].shown.anyButton && shots[0].shown.cls.indexOf('pl-on') === -1, JSON.stringify(shots[0].shown));
                    const diff = await pixelsThatDiffer(shots[0].png, shots[1].png);
                    check('... and Home is the same as with the list\'s two files not there at all (at most ' + NOISE + ' pixels differ by more than 24 levels: the edge of a rounded button is drawn either way from one load to the next, with the files or without; a panel or a button is thousands)', diff.size && diff.n <= NOISE, JSON.stringify(diff));
                    if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); fs.writeFileSync(path.join(SHOTS, 'built_panel_off_' + size[1] + 'x' + size[2] + '.png'), shots[0].png); }
                }
            } finally { fake.close(); }
        }
        // ---- 7. the bar of a phone at any count: nothing is cut off from 320 to 639 px, whatever the count ------------------------------------------------------------------------------------------------------
        if (want(7)) {
            const WIDTHS = [320, 360, 370, 375, 380, 390, 412];
            const pillOf = (n) => (n > 99 ? '99+' : String(n));
            const atWidths = async (page, count, who) => {
                for (const w of WIDTHS) {
                    await page.setViewportSize({ width: w, height: 740 });
                    await sleep(120);
                    const m = await page.evaluate(LAYOUT);
                    const bar = await page.evaluate(() => { const b = document.querySelector('.pl-bar'), menu = document.querySelector('.hbar .hright > :last-child'), pill = b && b.querySelector('.pill'); return { visible: !!b && b.getClientRects().length > 0, text: pill && !pill.hidden ? pill.textContent : null, slack: menu ? innerWidth - menu.getBoundingClientRect().right : null, high: document.querySelector('.hbar').getBoundingClientRect().height }; });
                    check(who + ', ' + count + ' people at ' + w + ' px: nothing of the bar is cut off, nothing covers the logo or another button, no sideways scroll', m.out.length === 0, JSON.stringify(m.out));
                    check('... the button says ' + pillOf(count) + ', the Menu keeps 8 px at the edge, and the bar keeps its three rows (184 px)', bar.visible && bar.text === pillOf(count) && bar.slack >= 8 && Math.abs(bar.high - 184) < 0.5, JSON.stringify(bar));
                }
            };
            const sheetAt = async (page, count, w) => {
                await page.setViewportSize({ width: w, height: 740 });
                await sleep(150);
                await page.click('.pl-bar');
                await until('the sheet is open', () => page.evaluate(() => document.getElementById('players').getAttribute('data-sheet') === 'true'), 3000);
                await sleep(150);
                const o = await page.evaluate(LAYOUT);
                const head = await page.evaluate(() => { const c = document.querySelector('.pl-count'), q = document.querySelector('.pl-head').getBoundingClientRect(), x = document.querySelector('.pl-close').getBoundingClientRect(), t = c && c.getBoundingClientRect(); return { text: c && !c.hidden ? c.textContent : null, inside: !!t && t.left >= q.left && t.right <= x.left + 0.5 }; });
                check('the sheet at ' + w + ' px with ' + count + ' people: inside the window, the header says the whole number (500+ over 500) and it stays clear of the Close button', o.out.filter((x) => x.indexOf('under something') === -1).length === 0 && head.text === (count > 500 ? '500+' : String(count)) && head.inside, JSON.stringify([o.out, head]));
                await shot(page, 'bar_' + count + '_sheet_' + w);
                await page.keyboard.press('Escape');
                await sleep(100);
            };
            for (const count of [13, 99, 100, 226, 480]) {
                await crowd(r, [], count - 1);
                const page = await openPage(r, ['bar', 390, 740, true], '1000_maple', { noGames: true });
                await until('the list says ' + count, () => page.evaluate((n) => { const b = document.querySelector('.pl-bar .pill'); return !!b && !b.hidden && b.textContent === (n > 99 ? '99+' : String(n)); }, count), 15000);
                check('(the chat says the same number: ' + count + ')', (await countsAgree(page)).chat === String(count));
                await atWidths(page, count, 'the hub');
                if (count === 226) for (const w of [320, 375, 390, 412]) { await page.setViewportSize({ width: w, height: 740 }); await sleep(150); await shot(page, 'bar_226_' + w); }
                if (count === 226 || count === 480) { await sheetAt(page, count, 320); await sheetAt(page, count, 375); }
                await page.context().close();
            }
            await crowd(r, [], 0);
            const many = await startFakeSite({ mode: 'custom' });                // a count that no hub has (it lets 500 sockets in): the page takes what a hub says
            try {
                const page = await openPage(many, ['bar', 390, 740, true], '1000_maple', { noGames: true });
                await until('the fake hub has the page', () => many.hub.peers.length > 0, 10000);
                const peer = many.hub.peers[0];
                many.hub.send(peer, { t: 'welcome', v: 1, you: { name: 'maple', mod: false }, login: true, presence: true, me: 1, online: 9999, history: [] });
                many.hub.send(peer, { t: 'who', full: true, n: 9999, g: 9998, more: 0, rows: [{ i: 1, name: 'maple', at: 'look' }] });
                await until('the list says 99+', () => page.evaluate(() => { const b = document.querySelector('.pl-bar .pill'); return !!b && !b.hidden && b.textContent === '99+'; }), 10000);
                await atWidths(page, 9999, 'a fake hub');
                await sheetAt(page, 9999, 320);
                await page.context().close();
            } finally { many.close(); }
        }

        // ---- 8. the Games list that the live site docks in the Yard: centered in the window and clear of both panels, from 1280 px ----------------------------------------------------------------------------
        if (want(8)) {
            const BIG = [['w1280', 1280, 800], ['w1366', 1366, 768], ['w1536', 1536, 864], ['w1920', 1920, 1080], ['w2560', 2560, 1440]];
            const where = () => {
                const R = (e) => { if (!e || !e.getClientRects().length) return null; const q = e.getBoundingClientRect(); return { l: q.left, r: q.right, t: q.top, b: q.bottom }; };
                const list = R(document.querySelector('.gdock')), panel = R(document.getElementById('players')), chat = R(document.getElementById('gchat'));
                const meet = (a, c) => !!a && !!c && a.l < c.r - 0.5 && c.l < a.r - 0.5 && a.t < c.b - 0.5 && c.t < a.b - 0.5;
                return { mid: list ? (list.l + list.r) / 2 : null, win: innerWidth / 2, list, overPanel: meet(list, panel), overChat: meet(list, chat), gap: list && panel ? panel.l - list.r : null };
            };
            for (const [what, people, guests] of [['3 others', PICTURE.slice(0, 3), 1], ['12 others', PICTURE, 4]]) {
                for (const state of ['absent', 'open', 'folded']) {
                    r.blocked = state === 'absent';
                    await crowd(r, people, guests);
                    for (const size of BIG) {
                        const page = await openPage(r, [size[0], size[1], size[2], false], '1000_maple');
                        if (state !== 'absent') await listShows(page, people[0][0].split(':')[1]);
                        if (state === 'folded') { await page.click('.pl-fold:not(.pl-close)'); await sleep(150); }
                        for (const chat of ['open', 'folded']) {
                            if (chat === 'folded') { await page.evaluate(() => document.querySelector('#gchat .gc-fold').click()); await sleep(200); }
                            await sleep(250);
                            const m = await page.evaluate(where), lay = await page.evaluate(LAYOUT);
                            const label = what + ', the list ' + state + ', the chat ' + chat + ', ' + size[1] + ' x ' + size[2];
                            check(label + ': the Games list is centered in the window (to 1 px)', !!m.list && Math.abs(m.mid - m.win) <= 1, JSON.stringify(m));
                            check('... and clear of the players panel and the chat dock, no Join or field under anything else, nothing sideways', !m.overPanel && !m.overChat && lay.out.length === 0 && (state === 'absent' || m.gap === null || m.gap >= 100), JSON.stringify([m, lay.out]));
                            if (what === '12 others') await shot(page, 'center_' + size[1] + 'x' + size[2] + '_' + state + '_chat_' + chat);
                        }
                        await page.context().close();
                    }
                }
            }
            r.blocked = false;
        }
        check('no script error in any page', errors.length === 0, errors.join(' | '));
    } finally {
        await browser.close();
        r.stop();
    }
}

// a site that has the old kind of hub (no list): tests/scripts/fake_hub.js behind the same files
async function startFakeSite(hubOptions) {
    const site = { blocked: false };
    const server = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        if (url.pathname === '/lobbies') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(LOBBIES)); return; }
        if (url.pathname === '/stats' || url.pathname === '/busy') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end('{}'); return; }
        const file = url.pathname === '/' ? 'lobby.html' : decodeURIComponent(url.pathname).replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile() || (site.blocked && /^home\/players\./.test(file))) { res.writeHead(404); res.end('not here'); return; }
        const type = TYPES[path.extname(full)] || 'application/octet-stream';
        res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' });
        res.end(type.startsWith('text/html') ? fs.readFileSync(full, 'utf8').replace(/@@SITE_TITLE@@/g, '').replace(/@@SITE_FOOTER@@/g, '').replace(/@@GAME_VERSION@@/g, '0.13.0').replace(/@@BUILD_ID@@/g, 'players').replace(/@@BUILD_DATE@@/g, '2026-11-01 00:00').replace(/@@BUILD_TIMESTAMP@@/g, '2026-11-01') : fs.readFileSync(full));
    });
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const hub = await startFakeHub(Object.assign({ server, login: true, you: { name: 'maple', mod: false }, online: 7, history: [{ id: 1, at: 1700000001, who: 'pebble', text: 'anyone up for Islands?', mod: false }] }, hubOptions));
    site.hub = hub;
    site.url = 'http://127.0.0.1:' + server.address().port;
    site.origin = site.url;
    site.close = () => { hub.drop(); server.close(); };
    return site;
}

main().then(() => {
    console.log('players list in a real browser: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
}).catch((e) => { console.log('FAILED: ' + (e && e.stack || e)); process.exit(1); });
