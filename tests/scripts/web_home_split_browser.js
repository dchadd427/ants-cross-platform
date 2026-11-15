// OPT-IN, not part of ./run_tests.sh: the two drag handles of Home (web/home/split.js, docs/HOME_SCREEN.md "The splitters") in a REAL browser (Chromium through Playwright) over the REAL hub, as the players list's
// browser check does. It drags the handle between the Games list and the players list and the one on top of the chat dock with a real mouse, uses the keyboard on them, reloads (the sizes are kept), resizes the window
// (they are clamped), and goes to a phone's width (no handle). --shots DIR takes the pictures (split_*.png).
//   node tests/scripts/web_home_split_browser.js [--web DIR] [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
// Needs `playwright`, a Chromium and python3 with the hub's folder; without them it says so and exits 0. Exit status: 0 every check passed (or none could be made), 1 a check failed or the page broke.
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
    const hub = spawn('python3', ['-m', 'hub'], { cwd: ROOT, env: Object.assign({}, process.env, { ANTS_HUB_PORT: String(hubPort), ANTS_HUB_HOST: '127.0.0.1', ANTS_HUB_DB: path.join(dir, 'hub.db'), ANTS_HUB_PUBLIC_URL: origin, ANTS_HUB_OWNER: '9001', ANTS_HUB_TEST_LOGIN: '1' }), stdio: ['ignore', 'pipe', 'pipe'] });
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
    await page.waitForSelector('#gchat', { state: 'attached', timeout: 15000 });
    if (!o.noGames) await page.waitForSelector('.gpop .gitem', { timeout: 15000 });
    return page;
}
const listShows = (page, text) => until('the list shows ' + text, () => page.evaluate((t) => { const p = document.getElementById('players'); return !!p && p.textContent.indexOf(t) !== -1 && !p.closest('.pl-root').hidden; }, text), 15000);
async function shot(page, name) { if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, 'built_' + name + '.png') }); } }
const PHONE_W = 700;                                       // (a window up to this wide has the strip and one view at a time)
const showPlayers = async (page) => {                       // the Players view of a phone's width; in a wider window the list is on the page already
    if (await page.evaluate(() => innerWidth <= 700)) { await page.click('.htab.t-players'); await sleep(150); }
};

// what a page must be at any size and with any crowd (the checks of docs/HOME_SCREEN.md that the list must not break)
const LAYOUT = () => {
    const R = (e) => { if (!e || !e.getClientRects().length) return null; const q = e.getBoundingClientRect(); return { l: q.left, t: q.top, r: q.right, b: q.bottom }; };
    const meet = (a, b) => a && b && a.l < b.r - 0.5 && b.l < a.r - 0.5 && a.t < b.b - 0.5 && b.t < a.b - 0.5;
    const panel = document.getElementById('players'), phone = innerWidth <= 700;
    const pl = R(panel), dock = R(document.querySelector('.gdock')), chat = R(document.getElementById('gchat')), main = R(document.getElementById('hmain')), tabs = R(document.querySelector('.htabs'));
    const iw = innerWidth, ih = innerHeight, out = [];
    [['panel', pl], ['games', dock], ['chat', chat], ['strip', tabs]].forEach(([n, q]) => { if (q && (q.l < -0.5 || q.t < -0.5 || q.r > iw + 0.5 || q.b > ih + 0.5)) out.push(n + ' outside the window ' + JSON.stringify(q)); });
    if (document.documentElement.scrollWidth > iw) out.push('sideways scroll');
    if (!phone) {
        if (pl && meet(pl, dock)) out.push('the panel is over the Games list');
        if (pl && meet(pl, chat)) out.push('the panel is over the chat dock');
        if (pl && dock && !(pl.l >= dock.r)) out.push('the panel is not right of the Games list');
        if (pl && dock && Math.abs(pl.t - dock.t) > 1) out.push('the panel and the Games list do not start at the same line');
        if (pl && chat && !(chat.t >= pl.b - 0.5)) out.push('the chat dock is not under the panel');
        if (pl && (pl.r - pl.l < 219.5 || pl.r - pl.l > 280.5)) out.push('the panel is ' + Math.round(pl.r - pl.l) + ' px wide (220 to 280)');
        if (tabs) out.push('the phone\'s strip shows in a wide window');
    } else {
        if (!tabs) out.push('no strip on a phone');
        if (chat && (dock || pl)) out.push('the chat shows with another view');
    }
    if (pl && main && (pl.t < main.t - 0.5 || pl.b > main.b + 0.5)) out.push('the panel is not inside the body');
    const covered = [...document.querySelectorAll('.gjoin, .pl-join, .gcodelink')].filter((e) => {
        if (!e.getClientRects().length) return false;
        const q = e.getBoundingClientRect(), box = e.closest('.gpbody, .pl-list, .pl-body'), bq = box && box.getBoundingClientRect();
        if (bq && (q.top < bq.top - 0.5 || q.bottom > bq.bottom + 0.5)) return false;                  // (scrolled out of its own box: not on the screen)
        if (q.top < 0 || q.bottom > ih || q.left < 0 || q.right > iw) return false;
        const top = document.elementFromPoint(q.left + q.width / 2, q.top + q.height / 2);
        return !(top === e || e.contains(top));
    }).length;
    if (covered) out.push(covered + ' Join or field under something else');
    return { out, hasPanel: !!pl, barHeight: document.querySelector('.hbar').getBoundingClientRect().height };
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
    const head = document.querySelector('.pl-count'), button = [...document.querySelectorAll('.htab.t-players .pill')].find((e) => e.getClientRects().length);
    return { chat: chat ? chat[1] : null, head: head && !head.hidden ? head.textContent : null, button: button && !button.hidden ? button.textContent : null };
});
const phoneChat = async (page, w, h) => {                  // a page that opened at this size has the dock as a fresh page has it: open on a desktop, a strip on a phone or in a short window
    const want = w >= 701 && h >= 601 ? 'true' : 'false';
    if ((await page.getAttribute('#gchat', 'data-open')) !== want) { await page.evaluate(() => document.querySelector('#gchat .gc-fold').click()); await sleep(100); }
};

const box = (page, sel) => page.evaluate((s) => { const e = document.querySelector(s); if (!e || !e.getClientRects().length) return null; const q = e.getBoundingClientRect(); return { l: q.left, t: q.top, r: q.right, b: q.bottom, w: q.width, h: q.height }; }, sel);
const mid = (q) => ({ x: q.l + q.w / 2, y: q.t + q.h / 2 });

async function main() {
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + ')'); return; }
    const r = await startRig({});
    try {
        await crowd(r, PICTURE, 4);
        for (const size of [['desktop', 1280, 800, false], ['wide', 1920, 1080, false]]) {
            const [name, w, h] = size;
            const page = await openPage(r, size, '1000_maple');
            await listShows(page, 'sage');
            await phoneChat(page, w, h);
            await page.evaluate(() => { try { localStorage.removeItem('ants.home.split.v1'); } catch (e) { /* none */ } });
            await sleep(200);
            const vx = await box(page, '.sp-x'), vy = await box(page, '.sp-y');
            check(name + ': both handles show', !!vx && !!vy);
            check(name + ': they are separators with the values', await page.evaluate(() => [...document.querySelectorAll('.sp')].every((e) => e.getAttribute('role') === 'separator' && +e.getAttribute('aria-valuenow') >= +e.getAttribute('aria-valuemin') && +e.getAttribute('aria-valuenow') <= +e.getAttribute('aria-valuemax'))));
            check(name + ': the handle between is in the gap of the lists', await page.evaluate(() => { const h = document.querySelector('.sp-x').getBoundingClientRect(), g = document.querySelector('.gdock').getBoundingClientRect(), p = document.querySelector('.pl').getBoundingClientRect(); return h.left >= g.right - 1 && h.right <= p.left + 1; }));
            await shot(page, 'split_' + name + '_rest');
            // hover
            let m = mid(vx); await page.mouse.move(m.x, m.y); await sleep(250); await shot(page, 'split_' + name + '_hover_between');
            const before = { pl: (await box(page, '.pl')).w, gc: (await box(page, '#gchat')).h };
            // drag the players wider (handle left)
            await page.mouse.down(); await page.mouse.move(m.x - 60, m.y, { steps: 6 }); await shot(page, 'split_' + name + '_drag_players_wider');
            await page.mouse.move(m.x - 120, m.y, { steps: 6 }); await page.mouse.up();
            const pw = (await box(page, '.pl')).w;
            check(name + ': dragging left makes the players wider (' + before.pl + ' to ' + pw + ')', pw > before.pl + 100 && pw <= before.pl + 121, pw);
            check(name + ': no text got selected', await page.evaluate(() => String(getSelection()) === ''));
            // chat taller
            m = mid(await box(page, '.sp-y')); await page.mouse.move(m.x, m.y); await sleep(250); await shot(page, 'split_' + name + '_hover_chat');
            await page.mouse.down(); await page.mouse.move(m.x, m.y - 50, { steps: 5 }); await shot(page, 'split_' + name + '_drag_chat_taller');
            await page.mouse.move(m.x, m.y - 100, { steps: 5 }); await page.mouse.up();
            const gh = (await box(page, '#gchat')).h;
            check(name + ': dragging up makes the chat taller (' + before.gc + ' to ' + gh + ')', gh > before.gc + 80 && gh <= before.gc + 101, gh);
            const lay = await page.evaluate(LAYOUT.toString().replace(/^\(\)\s*=>/, '(') + ')()').catch(() => null);
            const kept = await page.evaluate(() => localStorage.getItem('ants.home.split.v1'));
            check(name + ': the sizes are kept', /"p":\d+/.test(kept || '') && /"c":\d+/.test(kept || ''), kept);
            // limits
            m = mid(await box(page, '.sp-y')); await page.mouse.move(m.x, m.y); await page.mouse.down(); await page.mouse.move(m.x, 2, { steps: 8 }); await page.mouse.up();
            const bodyH = await page.evaluate(() => document.getElementById('home').clientHeight - document.querySelector('.hbar').offsetHeight);
            const top = (await box(page, '#gchat')).h;
            check(name + ': the chat stops at 70% of the body (' + top + ' of ' + bodyH + ')', top <= bodyH * 0.7 + 1 && top >= bodyH * 0.7 - 2, top);
            m = mid(await box(page, '.sp-x')); await page.mouse.move(m.x, m.y); await page.mouse.down(); await page.mouse.move(2, m.y, { steps: 8 }); await page.mouse.up();
            const gw = (await box(page, '.gdock')).w, pw2 = (await box(page, '.pl')).w;
            check(name + ': the players stop where the Games list is 240 wide (' + gw + ')', gw >= 239 && gw <= 245 && pw2 > 400, gw + ' ' + pw2);
            await shot(page, 'split_' + name + '_limits');
            // keyboard
            await page.focus('.sp-x'); await shot(page, 'split_' + name + '_focus_between');
            await page.keyboard.press('Home'); const a = (await box(page, '.pl')).w;
            check(name + ': Home gives the least', Math.abs(a - 240) <= 1, a);
            await page.keyboard.press('ArrowLeft'); const b = (await box(page, '.pl')).w;
            await page.keyboard.press('Shift+ArrowLeft'); const c = (await box(page, '.pl')).w;
            check(name + ': ArrowLeft 16 px, Shift 64 px', Math.abs(b - a - 16) <= 1 && Math.abs(c - b - 64) <= 1, [a, b, c]);
            await page.keyboard.press('Enter'); const d = (await box(page, '.pl')).w;
            check(name + ': Enter resets', Math.abs(d - before.pl) <= 1, d);
            await page.focus('.sp-y'); await shot(page, 'split_' + name + '_focus_chat');
            await page.keyboard.press('End'); const e1 = (await box(page, '#gchat')).h; await page.keyboard.press('ArrowDown'); const e2 = (await box(page, '#gchat')).h;
            check(name + ': End gives the most, ArrowDown 16 px less', Math.abs(e1 - e2 - 16) <= 1, [e1, e2]);
            await page.dblclick('.sp-y');
            const e3 = (await box(page, '#gchat')).h;
            check(name + ': a double click resets the chat', Math.abs(e3 - before.gc) <= 1, e3);
            // reload keeps
            await page.focus('.sp-x'); for (let i = 0; i < 3; i++) await page.keyboard.press('Shift+ArrowLeft');
            const want = (await box(page, '.pl')).w;
            await page.reload({ waitUntil: 'load' }); await page.waitForSelector('.pl', { timeout: 15000 }); await sleep(300);
            check(name + ': a reload keeps the width', Math.abs((await box(page, '.pl')).w - want) <= 1, want);
            // the window gets smaller: clamped
            await page.setViewportSize({ width: 760, height: 560 }); await sleep(300);
            const small = await page.evaluate(() => { const g = document.querySelector('.gdock').getBoundingClientRect(), p = document.querySelector('.pl').getBoundingClientRect(); return { g: g.width, p: p.width, over: document.documentElement.scrollWidth > innerWidth }; });
            check(name + ': a narrow window clamps (games ' + Math.round(small.g) + ', players ' + Math.round(small.p) + ')', small.g >= 239 && !small.over, JSON.stringify(small));
            await page.setViewportSize({ width: 390, height: 844 }); await sleep(300);
            check(name + ': a phone has no handle and no kept size', await page.evaluate(() => [...document.querySelectorAll('.sp')].every((e) => !e.getClientRects().length) && localStorage.getItem('ants.home.split.v1') === null));
            await page.context().close();
        }
        // the Room: whether it has side-by-side or stacked panels is a matter for the report; a picture
        const page = await openPage(r, ['desktop', 1280, 800, false], '1000_maple');
        await page.evaluate(() => document.querySelector('.hmain') && 0);
        await page.click('.quick.host, .hbar .quick').catch(() => {});
        await sleep(600);
        await page.getByRole('button', { name: 'Private room' }).click().catch(() => {});
        await page.waitForFunction(() => document.body.getAttribute('data-screen') === 'room', null, { timeout: 8000 }).catch(() => {});
        await sleep(800);
        await shot(page, 'split_room_probe');
        await shot(page, 'split_room_probe');
        await page.context().close();
        check('no script error', errors.length === 0, errors.join(' | '));
    } finally { r.stop(); await browser.close(); }
}

main().then(() => {
    console.log('splitters in a real browser: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
}).catch((e) => { console.log('FAILED: ' + (e && e.stack || e)); process.exit(1); });
