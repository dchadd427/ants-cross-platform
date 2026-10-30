// The front page's PUBLIC ROOM and GAMES LIST in a REAL browser (Chromium through Playwright) against a REAL ants_server, behind a small stand-in for the site's nginx: the files of web/ (stamped as the Dockerfile
// stamps them), /ws to the server's WebSocket door, /lobbies, /stats and /busy to its public door and its status, and nothing else (docs/HOME_SCREEN.md, docs/PUBLIC_ROOMS.md). Opt-in, like the other browser checks: it needs
// Playwright (`require('playwright')`), a Chromium that it can start and a built ants_server.
//   1. two browser contexts: the first makes its room public with Quick host (a real room, a real Visibility), the second opens the Games list and sees that room in the answer of the real /lobbies door (the host's name,
//      the map, the seats), joins it from the list (a second client that only joins; the page's own room goes when the new one welcomes it), and the first sees the guest in its room;
//   2. what a screen shows: with --shots DIR, pictures of Home (the count on the Games button), the Games list (rows and the empty state), the Room (public and private, a guest's chip), the card of the question and the notices,
//      at 390 x 844, 844 x 390 and 1280 x 800 (the sizes of the approved pictures), named games4_*.png, and a few measurements (every target at least 44 px, nothing wider than the window).
// usage: node web_home_games_browser.js --server PATH/ants_server --maps PATH/Original-Ants/Maps [--shots DIR]     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');
const net = require('net');
const { spawn } = require('child_process');

const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i === -1 ? null : args[i + 1]; };
const serverPath = opt('--server'), mapsPath = opt('--maps'), shotsDir = opt('--shots');
if (!serverPath || !mapsPath) { console.log('usage: web_home_games_browser.js --server ants_server --maps Original-Ants/Maps [--shots DIR]'); process.exit(2); }
let playwright;
try { playwright = require('playwright'); } catch (e) { console.log('SKIP: Playwright is not installed: the real browser was NOT run (tests/scripts/web_home_games_browser.js)'); process.exit(0); }
const WEB = path.resolve(__dirname, '..', '..', 'web');

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
        await sleep(60);
    }
}
function freePort() {
    return new Promise((resolve, reject) => {
        const s = net.createServer();
        s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); });
        s.on('error', reject);
    });
}

// ---- the real server --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
async function startServer() {
    const [port, ws, ctl, replay] = [await freePort(), await freePort(), await freePort(), await freePort()];
    const dir = fs.mkdtempSync(path.join(require('os').tmpdir(), 'ants_games_browser.'));
    const child = spawn(path.resolve(serverPath), ['--maps', path.resolve(mapsPath), '--port', String(port), '--ws-port', String(ws), '--ctl-port', String(ctl), '--replay-port', String(replay), '--results-dir', dir, '--no-replays', '--no-db',
        '--demo-rooms', '2', '--demo-map', 'TINY.LVL', '--demo-maps', 'TINY.LVL,SMALL.LVL,MEDIUM.LVL,GAUNTLET.LVL,TREASURE.LVL,ISLANDS.LVL', '--demo-lobbies', '40'], { env: Object.assign({}, process.env, { ANTS_SERVER_SECRET: 'browser-check-secret' }), stdio: 'ignore' });
    await until('the server answers', () => new Promise((resolve) => http.get({ host: '127.0.0.1', port: ctl, path: '/healthz' }, (res) => { res.resume(); resolve(res.statusCode === 200); }).on('error', () => resolve(false))), 15000);
    return { child, port, ws, ctl, replay, dir, stop() { child.kill('SIGTERM'); try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) { /* it stays */ } } };
}

// ---- the stand-in for the site: the files of web/, /ws, /lobbies, /stats, /busy ----------------------------------------------------------------------------------------------------------------------------
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.svg': 'image/svg+xml', '.json': 'application/json', '.wasm': 'application/wasm' };
function standIn(server, o) {
    o = o || {};
    const stamp = (text) => text.replace(/@@SITE_TITLE@@/g, '').replace(/@@SITE_FOOTER@@/g, '').replace(/@@GAME_VERSION@@/g, '0.13.0').replace(/@@BUILD_ID@@/g, 'standin').replace(/@@BUILD_DATE@@/g, '2026-01-01 00:00').replace(/@@BUILD_TIMESTAMP@@/g, '2026-01-01');
    const proxy = (req, res, port, target) => {
        const up = http.request({ host: '127.0.0.1', port, path: target, method: req.method, headers: { host: '127.0.0.1' } }, (r) => { res.writeHead(r.statusCode, { 'content-type': r.headers['content-type'] || 'application/json', 'cache-control': 'no-store' }); r.pipe(res); });
        up.on('error', () => { res.writeHead(503, { 'content-type': 'application/json' }); res.end('{"error":"lobbies_off"}'); });
        up.end();
    };
    const site = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        site.hits.push(url.pathname + url.search);
        if (url.pathname === '/lobbies') {
            if (o.lobbies) { const answer = o.lobbies(); res.writeHead(answer ? 200 : 404, { 'content-type': 'application/json', 'cache-control': 'no-store' }); res.end(JSON.stringify(answer || { error: 'lobbies_off' })); return; }
            if (url.search) { res.writeHead(404, { 'content-type': 'application/json' }); res.end('{"error":"lobbies_off"}'); return; }
            proxy(req, res, server.replay, '/lobbies'); return;
        }
        if (url.pathname === '/stats' || url.pathname === '/busy') { proxy(req, res, server.ws, url.pathname); return; }
        let file = url.pathname === '/' ? 'lobby.html' : decodeURIComponent(url.pathname).replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (!full.startsWith(WEB + path.sep) || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404); res.end('not found'); return; }
        const type = TYPES[path.extname(full)] || 'application/octet-stream';
        res.writeHead(200, { 'content-type': type, 'cache-control': 'no-cache, must-revalidate' });
        res.end(type.startsWith('text/html') ? stamp(fs.readFileSync(full, 'utf8')) : fs.readFileSync(full));
    });
    site.hits = [];
    site.on('upgrade', (req, socket, head) => {
        if (new URL(req.url, 'http://x').pathname !== '/ws') { socket.destroy(); return; }
        const up = net.connect(server.ws, '127.0.0.1', () => {
            up.write(req.method + ' /ws HTTP/1.1\r\n' + Object.entries(req.headers).map(([k, v]) => (k === 'host' ? 'host: 127.0.0.1' : k + ': ' + v)).join('\r\n') + '\r\n\r\n');
            if (head && head.length) up.write(head);
            up.pipe(socket); socket.pipe(up);
        });
        up.on('error', () => socket.destroy()); socket.on('error', () => up.destroy());
    });
    return new Promise((resolve) => site.listen(0, '127.0.0.1', () => { site.url = 'http://127.0.0.1:' + site.address().port; resolve(site); }));
}

const FIVE = () => ({ format: 1, count: 5, lobbies: [
    { code: 'p4n8dz', host: 'Pebble', map: 'ISLANDS.LVL', mode: 'highest-score', people: 3, bots: 0, free: 1, size: 4, age: 312 },
    { code: 'c1v3rr', host: 'Clover', map: 'TREASURE.LVL', mode: 'highest-score', people: 2, bots: 1, free: 1, size: 4, age: 200 },
    { code: 'j7n1pr', host: 'Juniper', map: 'GAUNTLET.LVL', mode: 'highest-score', people: 1, bots: 1, free: 2, size: 4, age: 120 },
    { code: 'b4s1l2', host: 'Basil', map: 'SMALL.LVL', mode: 'highest-score', people: 2, bots: 0, free: 2, size: 4, age: 90 },
    { code: 'f3rn22', host: 'Fern', map: 'MEDIUM.LVL', mode: 'highest-score', people: 1, bots: 0, free: 3, size: 4, age: 30 }] });

const SIZES = [['phone', 390, 844], ['side', 844, 390], ['desktop', 1280, 800]];
const NOTICES = { public: 'Your room is public now', joined: 'Joined' };

async function newPage(browser, site, name, size, search, silent) {
    const ctx = await browser.newContext({ viewport: { width: size[1], height: size[2] }, deviceScaleFactor: 1 });
    if (silent) await ctx.addInitScript(() => { const Real = window.WebSocket; window.WebSocket = function (url, p) { return /\/ws$/.test(String(url)) && !/\/hub\/ws$/.test(String(url)) ? { close() {}, send() {}, readyState: 0 } : new Real(url, p); }; });       // (a game server that says nothing: a try that waits)
    await ctx.addInitScript((n) => { try { if (n && !window.localStorage.getItem('ants.name')) window.localStorage.setItem('ants.name', n); } catch (e) { /* none */ } }, name);
    const page = await ctx.newPage();
    page.errors = [];
    page.on('pageerror', (e) => page.errors.push(String(e)));
    await page.goto(site.url + (search || '/'));
    return page;
}
const lit = (page) => page.waitForFunction(() => { const g = document.querySelector('.hchip.games'), q = document.querySelector('.quick.host'); return !!g && !!q && !g.disabled && !q.disabled; }, null, { timeout: 10000 });
const screenOf = (page) => page.evaluate(() => document.body.getAttribute('data-screen'));
const textOf = (page, selector) => page.evaluate((q) => { const e = document.querySelector(q); return e ? e.textContent : null; }, selector);

async function flows() {
    const server = await startServer();
    const browser = await playwright.chromium.launch({ args: ['--no-sandbox'] });
    const live = await standIn(server);
    const fake = await standIn(server, { lobbies: FIVE });
    const emptySite = await standIn(server, { lobbies: () => ({ format: 1, lobbies: [], count: 0 }) });
    const offSite = await standIn(server, { lobbies: () => null });
    try {
        // ---- 1. two contexts against the real server --------------------------------------------------------------------------------------------------------------------------------------------
        const maple = await newPage(browser, live, 'Maple', SIZES[2]);
        await lit(maple);
        check('the page opens on Home and loads the Games list after its first paint (Quick host, Quick join and Games are lit)', await screenOf(maple) === 'home' && await maple.evaluate(() => document.querySelector('.quick.join').disabled === false));
        await maple.click('.quick.host');
        await until('the room is public', async () => (await maple.evaluate(() => { const b = document.querySelector('.vbtn[aria-pressed="true"] b'); return b && b.textContent; })) === 'Public', 10000);
        check('Quick host in a real browser: the Room shows, the switch says Public (the real room said so) and the notice is there', await screenOf(maple) === 'room' && (await textOf(maple, '.gstrip')).indexOf(NOTICES.public) !== -1);
        const code = await maple.evaluate(() => new URL(document.getElementById('link').value).searchParams.get('room'));
        const sage = await newPage(browser, live, 'Sage', SIZES[2]);
        await lit(sage);
        const list = await until('the real list holds the room', async () => { const r = await (await sage.request.get(live.url + '/lobbies')).json(); return r.lobbies.some((x) => x.code === code) ? r : null; }, 10000);
        const mine = list.lobbies.find((x) => x.code === code);
        check('the real /lobbies door lists the public room: its code, the host\'s name, the map (the server\'s default) and the seats (one person, three free colors)', mine.host === 'Maple' && /\.LVL$/.test(mine.map) && mine.people === 1 && mine.free === 3 && mine.size === 4);
        await sage.click('.hchip.games');
        await until('the row is in the popover', async () => (await textOf(sage, '.glist .gname')) === 'Maple', 12000);
        check('a second browser sees the room in its Games list (the count on the button too) and joins from it', (await textOf(sage, '.hchip.games .pill')) === '1' && (await textOf(sage, '.glist .gcount')) === '1/4');
        await sage.click('.gjoin');
        await until('the guest is in the Room', async () => (await screenOf(sage)) === 'room' && (await textOf(sage, '#room-h')) === 'Maple’s room', 12000);
        check('Join in a real browser: the guest shows the Room of the host, the chip says "Public room" and there is no switch, and the notice says it', (await textOf(sage, '.vistag')) === 'Public room' && (await sage.evaluate(() => document.querySelector('.vis').hidden)) === true && (await textOf(sage, '.gstrip')).indexOf('Joined') === 0);
        await until('the host sees the guest', async () => (await maple.evaluate(() => document.getElementById('slots').textContent.indexOf('Sage') !== -1)), 10000);
        check('... and the host sees the guest in the room (the real server\'s Room message)', true);
        await maple.click('.vbtn[aria-pressed="false"]');
        await until('both see private', async () => (await textOf(sage, '.vistag')) === 'Private room' && (await maple.evaluate(() => document.querySelector('.vbtn[aria-pressed="true"] b').textContent)) === 'Private', 10000);
        const after = await (await sage.request.get(live.url + '/lobbies')).json();
        check('the host flips the room to Private: the guest\'s chip follows, and the real list no longer holds it', !after.lobbies.some((x) => x.code === code));
        for (const p of [maple, sage]) check('no script error in the page', p.errors.length === 0, p.errors.join(' | '));
        for (const p of [maple, sage]) await p.context().close();

        // ---- 1b. a press on the page's own cards (the question, the name) is not a press elsewhere: the list stays open, with the focus where it was, and shows why a Join did not go (real mouse clicks) ----------------------
        {
            const a = await newPage(browser, fake, 'Maple', SIZES[2]); await lit(a);
            await a.click('.hchip.mine');
            await a.waitForSelector('#slots li');
            const room = await a.evaluate(() => new URL(document.getElementById('link').value).searchParams.get('room'));
            const b = await newPage(browser, fake, 'Sage', SIZES[2], '/?room=' + room);
            await b.click('#name-step-go');
            await until('Sage is in the room of Maple', async () => (await a.evaluate(() => document.getElementById('slots').textContent.indexOf('Sage') !== -1)), 10000);
            await a.click('.rhome');
            await a.click('.hchip.games'); await a.waitForSelector('.glist .gitem');
            await a.click('.gjoin');
            await a.waitForSelector('.gconfirm');
            await a.click('.gstay');
            check('"Stay here" with the mouse keeps the list open, and the focus is on the Join button that asked', await a.evaluate(() => !document.querySelector('.gpop').hidden && !document.querySelector('.gconfirm') && !!document.activeElement && document.activeElement.classList.contains('gjoin')));
            await a.click('.gjoin');
            await a.waitForSelector('.gconfirm');
            await a.click('.gyes');
            await a.waitForSelector('.gpnote:not([hidden])');
            check('"Leave and join" into a game that is gone: the list is open and says "That game is not open any more." (and the room that the page is in is kept)', !(await a.evaluate(() => document.querySelector('.gpop').hidden)) && (await textOf(a, '.gpnote')) === 'That game is not open any more.' && await screenOf(a) === 'home' && (await a.evaluate(() => !!document.getElementById('code').textContent)));
            await a.fill('.gcode input', 'zzzzzz');
            await a.click('.gcgo');
            await a.waitForSelector('.gconfirm');
            await a.click('.gyes');
            await a.waitForSelector('#name-step:not([hidden])');
            await a.click('#name-step-go');
            await a.waitForFunction(() => { const h = document.querySelector('.gcode .hint'); return !!h && h.textContent.length > 0; }, null, { timeout: 12000 });
            check('Join by code with a code that leads nowhere: the name card is answered, the list is still open and the words are under the field (not in a list that went away)', await a.evaluate(() => !document.querySelector('.gpop').hidden && /no room/i.test(document.querySelector('.gcode .hint').textContent)), await textOf(a, '.gcode .hint'));
            await a.click('.gjoin');
            await a.waitForSelector('.gconfirm');
            await a.click('.gyes');
            await a.waitForSelector('.gjoining:not([hidden]), .gpnote:not([hidden])');
            check('(no script error in these pages)', a.errors.length === 0 && b.errors.length === 0, a.errors.concat(b.errors).join(' | '));
            await a.context().close(); await b.context().close();
        }

        // ---- 1c. a double click on Join is one Join: the line of a Join that waits is put above the rows, and its Cancel stands where the Join button stood, so that the second click of the double click lands on it (a real double click) ----
        for (const size of [SIZES[2], SIZES[0]]) {
            const host = await newPage(browser, live, 'Maple', size); await lit(host);
            await host.click('.quick.host');
            await until('the room is public', async () => (await host.evaluate(() => { const b = document.querySelector('.vbtn[aria-pressed="true"] b'); return b && b.textContent; })) === 'Public', 10000);
            const code = await host.evaluate(() => new URL(document.getElementById('link').value).searchParams.get('room'));
            const guest = await newPage(browser, live, 'Sage', size); await lit(guest);
            await until('the real list holds the room', async () => (await (await guest.request.get(live.url + '/lobbies')).json()).lobbies.some((x) => x.code === code), 10000);
            await guest.click('.hchip.games');
            await guest.waitForSelector('.gjoin[data-code="' + code + '"]', { timeout: 15000 });
            await guest.dblclick('.gjoin[data-code="' + code + '"]');
            await until('the guest is in the Room of the host', async () => (await screenOf(guest)) === 'room' && (await textOf(guest, '#room-h')) === 'Maple’s room', 12000).catch(() => null);
            check(size[0] + ' ' + size[1] + 'x' + size[2] + ': a double click on Join is one Join: the second click does not land on the Cancel of the line that appears (a real double click): the guest is in the host\'s Room', (await screenOf(guest)) === 'room' && (await textOf(guest, '#room-h')) === 'Maple’s room');
            check('... and the host sees the guest, once', (await host.evaluate(() => document.getElementById('slots').textContent.split('Sage').length - 1)) === 1);
            check('(no script error in these pages)', host.errors.length === 0 && guest.errors.length === 0, host.errors.concat(guest.errors).join(' | '));
            await host.context().close(); await guest.context().close();
        }

        // ---- 1d. Quick join and the question before Leave room, with real clicks against the real server, at 1280 and 390 px ---------------------------------------------------------------------------------------------
        for (const size of [SIZES[2], SIZES[0]]) {
            const label = size[0] + ' ' + size[1] + 'x' + size[2];
            const host = await newPage(browser, live, 'Maple', size); await lit(host);
            await host.click('.quick.host');
            await until('the room is public', async () => (await host.evaluate(() => { const b = document.querySelector('.vbtn[aria-pressed="true"] b'); return b && b.textContent; })) === 'Public', 10000);
            const code = await host.evaluate(() => new URL(document.getElementById('link').value).searchParams.get('room'));
            const guest = await newPage(browser, live, 'Sage', size); await lit(guest);
            await until('the real list holds the room', async () => (await (await guest.request.get(live.url + '/lobbies')).json()).lobbies.some((x) => x.code === code), 10000);
            await guest.dblclick('.quick.join');
            await until('the guest is in the Room of the host', async () => (await screenOf(guest)) === 'room' && (await textOf(guest, '#room-h')) === 'Maple’s room', 15000).catch(() => null);
            check(label + ': Quick join (a real double click on it) takes the page to the best open room, once: the host\'s Room, as a guest, with no strip and no card left', (await screenOf(guest)) === 'room' && (await textOf(guest, '#room-h')) === 'Maple’s room' && (await guest.evaluate(() => document.querySelectorAll('.gconfirm').length)) === 0 && (await guest.evaluate(() => document.querySelector('.gstrip').hidden)));
            check('... and the host sees the guest, once', (await host.evaluate(() => document.getElementById('slots').textContent.split('Sage').length - 1)) === 1);
            // Leave room asks when there are other people in the room: a real click on Stay here, and then on Leave room
            await guest.click('.rbar .leave');
            await guest.waitForSelector('.gconfirm');
            check(label + ': Leave room in a room with other people asks first ("Leave Maple’s room?", "Leave room" and "Stay here", every target 44 px high)', (await textOf(guest, '.gctitle')) === 'Leave Maple’s room?' && (await guest.evaluate(() => Array.from(document.querySelectorAll('.gconfirm .btn')).every((b) => b.getBoundingClientRect().height >= 44))));
            await guest.click('.gstay');
            check('"Stay here" (a real click) leaves the Room as it was, with the focus on Leave room', (await screenOf(guest)) === 'room' && (await guest.evaluate(() => !document.querySelector('.gconfirm') && document.activeElement.classList.contains('leave'))));
            await guest.click('.rbar .leave');
            await guest.waitForSelector('.gconfirm');
            await guest.click('.gyes');
            await until('the guest is Home', async () => (await screenOf(guest)) === 'home', 8000);
            check('"Leave room" (a real click) leaves it: Home shows and the page holds no room (My room has no count)', (await screenOf(guest)) === 'home' && (await guest.evaluate(() => document.querySelector('.hchip.mine .pill').hidden)));
            await until('the host is alone', async () => (await host.evaluate(() => document.getElementById('slots').textContent.indexOf('Sage') === -1)), 10000);
            check('... and the host\'s Room no longer shows Sage', await host.evaluate(() => document.getElementById('slots').textContent.indexOf('Sage') === -1));
            await host.click('.rbar .leave');
            await until('the host is Home at once (alone in the room: no question)', async () => (await screenOf(host)) === 'home', 5000).catch(() => null);
            check(label + ': Leave room when alone leaves at once (no card)', (await screenOf(host)) === 'home' && (await host.evaluate(() => document.querySelectorAll('.gconfirm').length)) === 0);
            // a page with another person in its own room: Quick join asks, with the best room in the card
            const rae = await newPage(browser, fake, 'Rae', size); await lit(rae);
            await rae.click('.hchip.mine');
            await rae.waitForSelector('#slots li');
            const raeRoom = await rae.evaluate(() => new URL(document.getElementById('link').value).searchParams.get('room'));
            const tess = await newPage(browser, fake, 'Tess', size, '/?room=' + raeRoom);
            await tess.click('#name-step-go');
            await until('Tess is in', async () => (await rae.evaluate(() => document.getElementById('slots').textContent.indexOf('Tess') !== -1)), 10000);
            await rae.click('.rhome');
            await rae.click('.quick.join');
            await rae.waitForSelector('.gconfirm');
            check(label + ': Quick join with another person in the own room asks first ("Leave your room?", "Tess is in your room…", the best room with its BEST tag, "Leave and join" and "Stay here")', (await textOf(rae, '.gctitle')) === 'Leave your room?' && /Tess is in your room/.test(await textOf(rae, '.gcline')) && (await textOf(rae, '.gconfirm .gbest')) === 'BEST' && (await textOf(rae, '.gconfirm .gname')) === 'Pebble');
            await rae.click('.gstay');
            check('"Stay here" (a real click) takes the card away, changes nothing, and the focus is on Quick join', (await rae.evaluate(() => !document.querySelector('.gconfirm') && document.activeElement.classList.contains('join'))) && (await screenOf(rae)) === 'home');
            await host.context().close(); await guest.context().close(); await rae.context().close(); await tess.context().close();
        }

        // ---- 2. the pictures: the scenes of the approved pictures, at their three sizes ----------------------------------------------------------------------------------------------------
        if (shotsDir) {
            fs.mkdirSync(shotsDir, { recursive: true });
            const shot = async (page, scene, size) => { await page.waitForSelector('#toast', { state: 'hidden', timeout: 6000 }).catch(() => {}); await page.waitForTimeout(250); await page.screenshot({ path: path.join(shotsDir, (scene.indexOf('qj_') === 0 ? 'quickjoin5_' + scene.slice(3) : 'games4_' + scene) + '_' + size[0] + '.png') }); };
            const reach = async (page) => { const bad = await page.evaluate(() => { const w = document.documentElement.clientWidth, out = []; document.querySelectorAll('button, input, select, a').forEach((e) => { const r = e.getBoundingClientRect(); if (!r.width || !r.height || e.closest('[hidden]')) return; if (r.right > w + 1 || r.left < -1) out.push('off the window: ' + (e.className || e.tagName)); }); return out; }); return bad; };
            const small = (page, root) => page.evaluate((q) => Array.from(document.querySelectorAll(q + ' button, ' + q + ' input')).filter((e) => { const r = e.getBoundingClientRect(); return r.width > 0 && r.height > 0 && !e.closest('[hidden]') && (r.height < 43.5 || r.width < 43.5); }).map((e) => (e.className || e.tagName) + ' ' + Math.round(e.getBoundingClientRect().width) + 'x' + Math.round(e.getBoundingClientRect().height)), root);
            for (const size of SIZES) {
                const label = size[0] + ' ' + size[1] + 'x' + size[2];
                // Home with the count, the list, the empty list, the list that is off (a fake list on the real site)
                let p = await newPage(browser, fake, 'Maple', size); await lit(p);
                await p.waitForFunction(() => document.querySelector('.hchip.games .pill') && !document.querySelector('.hchip.games .pill').hidden);
                await shot(p, 'home', size);
                check(label + ': Home has no sideways scroll and every control is inside the window', (await p.evaluate(() => document.documentElement.scrollWidth <= document.documentElement.clientWidth)) && (await reach(p)).length === 0, JSON.stringify(await reach(p)));
                await p.click('.hchip.games'); await p.waitForSelector('.glist .gitem');
                await shot(p, 'games', size);
                const best = await p.evaluate(() => { const r = document.querySelector('.glist .gitem'), n = r.querySelector('.gname').getBoundingClientRect(), m = r.querySelector('.gmapname').getBoundingClientRect(), b = r.querySelector('.gbest').getBoundingClientRect(); return { nameTop: Math.round(n.top), nameBottom: Math.round(n.bottom), mapRight: Math.round(m.right), bestTop: Math.round(b.top), bestBottom: Math.round(b.bottom), bestLeft: Math.round(b.left) }; });
                check(label + ': the BEST tag ' + (size[0] === 'phone' ? 'wraps under the name (the row is too narrow for it, as the phone picture draws it)' : 'stands beside the name and the map\'s name, on their line (the picture draws it so)'), size[0] === 'phone' ? best.bestTop >= best.nameBottom - 1 : best.bestTop >= best.nameTop - 1 && best.bestBottom <= best.nameBottom + 1 && best.bestLeft >= best.mapRight, JSON.stringify(best));
                check(label + ': the Games list shows five rows, and every target in it is at least 44 px high and wide', (await p.evaluate(() => document.querySelectorAll('.glist .gitem').length)) === 5 && (await small(p, '.gpop')).length === 0, JSON.stringify(await small(p, '.gpop')));
                check(label + ': the list is inside the window and scrolls inside itself (it is no taller than what is left under the bar)', await p.evaluate(() => { const r = document.querySelector('.gpop').getBoundingClientRect(); return r.bottom <= window.innerHeight + 1 && r.right <= window.innerWidth + 1 && r.left >= 0 && r.top > 0; }));
                await p.evaluate(() => { const j = document.querySelector('.gjoining'); j.hidden = false; j.firstChild.textContent = 'Joining Pebble’s room…'; });       // (the line of a Join that waits: shown by hand, the real one lasts a moment)
                await shot(p, 'games_joining', size);
                check(label + ': the line that says that a Join is on its way is inside the list, and its Cancel is at least 44 px high and wide', await p.evaluate(() => { const j = document.querySelector('.gjoining'), b = j.querySelector('.btn'), r = j.getBoundingClientRect(), q = document.querySelector('.gpop').getBoundingClientRect(), br = b.getBoundingClientRect(); return br.height >= 44 && br.width >= 44 && r.left >= q.left && r.right <= q.right + 1 && r.height > 0; }));
                await p.evaluate(() => { document.querySelector('.gjoining').hidden = true; });
                await p.keyboard.press('Escape');
                await p.context().close();
                p = await newPage(browser, emptySite, 'Maple', size); await lit(p);
                await p.click('.hchip.games'); await p.waitForSelector('.gempty:not([hidden])');
                await shot(p, 'games_empty', size);
                check(label + ': the empty list says it and offers Host one! (44 px)', (await p.evaluate(() => document.querySelector('.gempty .btn').getBoundingClientRect().height)) >= 44);
                await p.context().close();
                p = await newPage(browser, offSite, 'Maple', size); await lit(p);
                await p.click('.hchip.games'); await p.waitForSelector('.goff:not([hidden])');
                await shot(p, 'games_off', size);
                await p.context().close();
                // the room: Maple, Sage and a computer player; private; then Quick host from Home; Sage's own view
                const a = await newPage(browser, fake, 'Maple', size); await lit(a);
                await a.click('.hmenu', { trial: true }).catch(() => {});
                await a.click('.hchip.mine');
                await a.waitForSelector('#slots li');
                const room = await a.evaluate(() => new URL(document.getElementById('link').value).searchParams.get('room'));
                const b = await newPage(browser, fake, 'Sage', size, '/?room=' + room);
                await b.click('#name-step-go');
                await until('Sage is in', async () => (await a.evaluate(() => document.getElementById('slots').textContent.indexOf('Sage') !== -1)), 10000);
                await a.selectOption('#slots select[data-mode="2"]', 'medium');
                await a.waitForFunction(() => document.getElementById('slots').textContent.indexOf('Pip') !== -1 || document.getElementById('slots').textContent.indexOf('bot') !== -1);
                await shot(a, 'room_private', size);
                check(label + ': the Room card has the switch (private), 44 px high, inside the window, and the page does not scroll sideways', (await small(a, '.vis')).length === 0 && (await a.evaluate(() => document.documentElement.scrollWidth <= document.documentElement.clientWidth)), JSON.stringify(await small(a, '.vis')));
                await a.click('.rhome'); await a.click('.quick.host');
                await a.waitForSelector('.gconfirm');
                await shot(a, 'confirm_public', size);
                check(label + ': Quick host with people in the room asks first ("Make your room public?"), with its two answers at least 44 px high', (await textOf(a, '.gctitle')) === 'Make your room public?' && await a.evaluate(() => Array.from(document.querySelectorAll('.gconfirm .btn')).every((b) => b.getBoundingClientRect().height >= 44)));
                await a.click('.gyes');
                await a.waitForSelector('.vbtn[aria-pressed="true"] >> text=Public');
                await shot(a, 'room_public', size);
                await shot(b, 'room_guest', size);
                await a.click('.rhome');
                await a.click('.hchip.games'); await a.waitForSelector('.glist .gitem');
                await a.click('.gjoin');
                await a.waitForSelector('.gconfirm');
                await shot(a, 'confirm', size);
                check(label + ': the question card is inside the window with its two answers at least 44 px high', await a.evaluate(() => { const r = document.querySelector('.gconfirm').getBoundingClientRect(), ok = Array.from(document.querySelectorAll('.gconfirm button')).every((x) => x.getBoundingClientRect().height >= 44); return ok && r.bottom <= window.innerHeight + 1 && r.right <= window.innerWidth + 1 && r.left >= 0; }));
                await a.keyboard.press('Escape');
                await b.click('.rhome'); await b.click('.quick.host');
                await b.waitForSelector('.gconfirm');
                await shot(b, 'confirm_host', size);
                await a.context().close(); await b.context().close();
                // a refusal in plain words (a game of the fake list that the real server does not have), and a Join that works (the real list)
                const r = await newPage(browser, fake, 'Sage', size); await lit(r);
                await r.click('.hchip.games'); await r.waitForSelector('.glist .gitem');
                await r.click('.gjoin');
                await r.waitForSelector('.gpnote:not([hidden])');
                await shot(r, 'join_refused', size);
                check(label + ': a game that is gone is said in plain words in the list, and the buttons are back', (await textOf(r, '.gpnote')) === 'That game is not open any more.' && await r.evaluate(() => !document.querySelector('.gjoin').disabled));
                await r.context().close();
                const host = await newPage(browser, live, 'Maple', size); await lit(host);
                await host.click('.quick.host');
                await host.waitForSelector('.vbtn[aria-pressed="true"] >> text=Public');
                const guest = await newPage(browser, live, 'Sage', size); await lit(guest);
                await guest.waitForFunction(() => { const p = document.querySelector('.hchip.games .pill'); return p && p.textContent === '1'; }, null, { timeout: 12000 });
                await guest.click('.hchip.games'); await guest.waitForSelector('.glist .gitem');
                await guest.click('.gjoin');
                await guest.waitForSelector('.vistag');
                await shot(guest, 'joined', size);
                check(label + ': a Join from the real list brings the guest to the host\'s Room, with the notice', (await textOf(guest, '.gstrip')).indexOf('Joined') === 0 && (await textOf(guest, '.vistag')) === 'Public room');
                await host.context().close(); await guest.context().close();
                // Quick join: the cards of the approved pictures (finding: a game server that says nothing; joined: the real one; none: an empty list; confirm: another person in the own room), and the question before Leave room
                const qf = await newPage(browser, fake, 'Maple', size, undefined, true); await lit(qf);
                await qf.waitForFunction(() => { const p = document.querySelector('.hchip.games .pill'); return p && !p.hidden; });
                await qf.click('.quick.join');
                await qf.waitForSelector('.gconfirm .gbar');
                await shot(qf, 'qj_finding', size);
                check(label + ': the card "Finding a game…" has its four seats, its line, its bar and a Cancel of 44 px, inside the window', (await textOf(qf, '.gctitle')) === 'Finding a game…' && (await qf.evaluate(() => { const r = document.querySelector('.gconfirm').getBoundingClientRect(), b = document.querySelector('.gconfirm .btn').getBoundingClientRect(); return r.left >= 0 && r.right <= innerWidth && r.top >= 0 && r.bottom <= innerHeight && b.height >= 44 && document.querySelectorAll('.gconfirm .gdot').length === 4; })));
                await qf.context().close();
                const qh = await newPage(browser, live, 'Maple', size); await lit(qh);
                await qh.click('.quick.host');
                await qh.waitForSelector('.vbtn[aria-pressed="true"] >> text=Public');
                const qg = await newPage(browser, live, 'Sage', size); await lit(qg);
                await qg.waitForFunction(() => { const p = document.querySelector('.hchip.games .pill'); return p && p.textContent === '1'; }, null, { timeout: 12000 });
                await qg.click('.quick.join');
                await qg.waitForSelector('.gmark.check');
                await shot(qg, 'qj_joined', size);
                check(label + ': the card "Joined Maple’s room" shows the room, its bar and "Go to the room" (44 px), and the Room opens by itself', (await textOf(qg, '.gctitle')) === 'Joined Maple’s room' && (await textOf(qg, '.gconfirm .gyes')) === 'Go to the room');
                await qg.waitForFunction(() => document.body.getAttribute('data-screen') === 'room', null, { timeout: 5000 });
                await qh.context().close(); await qg.context().close();
                const qn = await newPage(browser, emptySite, 'Maple', size); await lit(qn);
                await qn.click('.quick.join');
                await qn.waitForSelector('.gconfirm .ant');
                await shot(qn, 'qj_none', size);
                check(label + ': the card "No open games right now." offers Host one! and Not now, every target 44 px high, inside the window', (await textOf(qn, '.gctitle')) === 'No open games right now.' && (await qn.evaluate(() => { const r = document.querySelector('.gconfirm').getBoundingClientRect(); return r.left >= 0 && r.right <= innerWidth && r.bottom <= innerHeight && Array.from(document.querySelectorAll('.gconfirm .btn')).every((b) => b.getBoundingClientRect().height >= 44); })));
                await qn.context().close();
                const qa = await newPage(browser, fake, 'Maple', size); await lit(qa);
                await qa.click('.hchip.mine');
                await qa.waitForSelector('#slots li');
                const qRoom = await qa.evaluate(() => new URL(document.getElementById('link').value).searchParams.get('room'));
                const qb = await newPage(browser, fake, 'Sage', size, '/?room=' + qRoom);
                await qb.click('#name-step-go');
                await until('Sage is in', async () => (await qa.evaluate(() => document.getElementById('slots').textContent.indexOf('Sage') !== -1)), 10000);
                await qa.waitForFunction(() => { const p = document.querySelector('.hchip.games .pill'); return p && !p.hidden; }).catch(() => null);
                await qa.click('.rbar .leave');
                await qa.waitForSelector('.gconfirm');
                await shot(qa, 'qj_leave', size);
                check(label + ': the question before Leave room is the card of the other questions, inside the window', (await textOf(qa, '.gctitle')) === 'Leave your room?' && (await qa.evaluate(() => { const r = document.querySelector('.gconfirm').getBoundingClientRect(); return r.left >= 0 && r.right <= innerWidth && r.bottom <= innerHeight; })));
                await qa.click('.gstay');
                await qa.click('.rhome');
                await qa.click('.quick.join');
                await qa.waitForSelector('.gconfirm .gbest');
                await shot(qa, 'qj_confirm', size);
                check(label + ': the question of Quick join shows the best room with its BEST tag, inside the window', (await textOf(qa, '.gctitle')) === 'Leave your room?' && (await qa.evaluate(() => { const r = document.querySelector('.gconfirm').getBoundingClientRect(); return r.left >= 0 && r.right <= innerWidth && r.bottom <= innerHeight; })));
                await qa.context().close(); await qb.context().close();
            }
        }
    } finally {
        for (const s of [live, fake, emptySite, offSite]) s.close();
        await browser.close();
        server.stop();
    }
}

flows().then(() => { console.log('public room and games list in a real browser: ' + checks + ' checks, ' + failures + ' failed'); process.exit(failures === 0 ? 0 : 1); }, (e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
