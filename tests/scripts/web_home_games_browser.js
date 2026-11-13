// The front page's PUBLIC ROOM and GAMES LIST in a REAL browser (Chromium through Playwright) against a REAL ants_server, behind a small stand-in for the site's nginx: the files of web/ (stamped as the Dockerfile
// stamps them), /ws to the server's WebSocket door, /lobbies, /stats and /busy to its public door and its status, and nothing else (docs/HOME_SCREEN.md, docs/PUBLIC_ROOMS.md). Opt-in, like the other browser checks: it needs
// Playwright (`require('playwright')`), a Chromium that it can start and a built ants_server.
//   1. two browser contexts: the first makes its room public with Quick host (a real room, a real Visibility), the second opens the Games list and sees that room in the answer of the real /lobbies door (the host's name,
//      the map, the seats), joins it from the list (a second client that only joins; the page's own room goes when the new one welcomes it), and the first sees the guest in its room;
//   2. what a screen shows: with --shots DIR, pictures of Home (the count on the Games button), the Games list (rows and the empty state), the Room (public and private, a guest's chip), the card of the question and the notices,
//      at 390 x 844, 844 x 390 and 1280 x 800 (the sizes of the approved pictures), named games4_*.png, and a few measurements (every target at least 44 px, nothing wider than the window).
//   0c. the pictures of the rows: a map of the players shows the library's (the picture is there, loaded), a map that has none leaves the plain square with no broken image, an original shows its own, and no list of maps is fetched for it.
//   0. Home's body: the Games list is docked in #hmain (a region with a card for each room and a Join on each), the chat dock lies along the bottom under it, a window up to 700 px wide has the strip (Games, Players, Chat) and one view at a
//      time (the chat view is open and fills the page), there is no Yard and no stand-in, and a build with the site label "staging" has the same Home; at 1280 x 800, 1366 x 640, 900 x 700, 800 x 600, 701 and 700 px wide, 390 x 844, 844 x 390 and 568 x 320.
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
const PREVIEWS = path.resolve(WEB, '..', 'Community-Maps', 'previews');       // (the site's nginx serves them at /maps/previews/: docker/nginx.conf)

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
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.webp': 'image/webp', '.ttf': 'font/ttf', '.svg': 'image/svg+xml', '.json': 'application/json', '.wasm': 'application/wasm' };
function standIn(server, o) {
    o = o || {};
    const label = o.label || '';       // (o.label: the build argument ANTS_SITE_LABEL, put into the title and the footer as the Dockerfile's step does; none is the production site)
    const stamp = (text) => text.replace(/@@SITE_TITLE@@/g, label ? ' (' + label + ')' : '').replace(/@@SITE_FOOTER@@/g, label ? '<strong id="site-label">' + label + '</strong>&#8197;&bull;&#8197;' : '').replace(/@@GAME_VERSION@@/g, '0.13.0').replace(/@@BUILD_ID@@/g, 'standin').replace(/@@BUILD_DATE@@/g, '2026-01-01 00:00').replace(/@@BUILD_TIMESTAMP@@/g, '2026-01-01');
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
        if (url.pathname.startsWith('/maps/previews/')) {            // the library's pictures, as nginx serves them: a name of printable ASCII (no / or \\), .webp, a file that is there; anything else is a 404
            let name = '';
            try { name = decodeURIComponent(url.pathname.slice('/maps/previews/'.length)); } catch (e) { name = ''; }
            const full = path.join(PREVIEWS, name);
            if (!/^[ -.0-\[\]-~]{1,80}\.webp$/.test(name) || !full.startsWith(PREVIEWS + path.sep) || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'content-type': 'text/plain' }); res.end('not found'); return; }
            res.writeHead(200, { 'content-type': 'image/webp', 'cache-control': 'public, max-age=86400' }); res.end(fs.readFileSync(full)); return;
        }
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
        // ---- 0. Home's body: the Games list is the panel at the left, the chat dock is the panel along the bottom, and a phone (up to 700 px wide) has one view at a time with a strip to pick it; a site with the label "staging" has the same Home --------------------------
        {
            const staging = await standIn(server, { label: 'staging', lobbies: FIVE });
            const prod = await standIn(server, { lobbies: FIVE });
            const look = (page) => page.evaluate(() => {
                const r = (e) => { if (!e) return null; const b = e.getBoundingClientRect(); return [Math.round(b.left), Math.round(b.top), Math.round(b.width), Math.round(b.height)]; };
                const main = document.getElementById('hmain'), bar = document.querySelector('.hbar'), pop = document.querySelector('.gpop'), chat = document.querySelector('.gc'), tabs = document.querySelector('.htabs'), shown = (e) => !!e && e.getClientRects().length > 0;
                const covered = [...document.querySelectorAll('.gpop .gjoin, .gpop .gcodelink')].filter((b) => {          // (a button that shows in the list's own window, and is under something else)
                    const q = b.getBoundingClientRect(), p = document.querySelector('.gpbody').getBoundingClientRect(); if (q.top < p.top || q.bottom > p.bottom || !shown(pop)) return false;
                    const top = document.elementFromPoint(q.left + q.width / 2, q.top + q.height / 2); return !(top === b || b.contains(top));
                }).length;
                return { title: document.title, label: !!document.getElementById('site-label'), main: r(main), bar: r(bar), host: r(document.getElementById('home')), pop: r(pop), chat: r(chat), tabs: r(tabs), tabsShown: shown(tabs), popShown: shown(pop), chatShown: shown(chat), role: pop && pop.getAttribute('role'),
                    yard: !!document.getElementById('yard') || !!document.querySelector('.standin, .ylabel, .yant'), rows: document.querySelectorAll('.gpop .gitem').length, joins: document.querySelectorAll('.gpop .gjoin').length, covered, tab: document.getElementById('home').getAttribute('data-tab'),
                    hasPopup: document.querySelector('.hchip.games').getAttribute('aria-haspopup'), wide: document.documentElement.scrollWidth <= window.innerWidth, mapSize: (document.querySelector('.gpop .gmap') || { getBoundingClientRect: () => ({ width: 0 }) }).getBoundingClientRect().width };
            });
            for (const size of [['desktop', 1280, 800], ['laptop', 1366, 640], ['tablet', 900, 700], ['small window', 800, 600], ['701 wide', 701, 800], ['700 wide', 700, 800], ['phone', 390, 844], ['side', 844, 390], ['small side', 568, 320]]) {
                const plain = await newPage(browser, prod, 'Maple', size, '/', true), test = await newPage(browser, staging, 'Maple', size, '/', true);
                await plain.waitForSelector('.gpop .gitem'); await plain.waitForSelector('.gc', { state: 'attached' }); await test.waitForSelector('.gpop .gitem'); await test.waitForSelector('.gc', { state: 'attached' }); await sleep(300);
                const a = await look(plain), b = await look(test), at = size[0] + ' (' + size[1] + ' x ' + size[2] + ')', phone = size[1] <= 700;
                check('Home, ' + at + ': the Games list is docked in the body (#hmain: a region, five cards with a Join each), there is no Yard and no stand-in, Games names no popup, nothing scrolls sideways', a.popShown && a.role === 'region' && a.rows === 5 && a.joins === 5 && !a.yard && a.hasPopup === null && a.wide
                    && a.main && a.pop[0] >= a.main[0] && a.pop[1] >= a.main[1] && a.pop[0] + a.pop[2] <= a.main[0] + a.main[2] + 1 && a.pop[1] + a.pop[3] <= a.main[1] + a.main[3] + 1, JSON.stringify(a));
                if (!phone) check('... a wide window: the chat panel lies under the list along the bottom of Home, as wide as the body (no strip: the games, the players and the chat are all there), and no Join or code field of the list is under anything else ' + at,
                    a.chatShown && !a.tabsShown && a.chat[1] >= a.pop[1] + a.pop[3] && a.chat[1] + a.chat[3] <= a.host[1] + a.host[3] + 1 && a.chat[0] <= a.pop[0] && a.covered === 0, JSON.stringify(a));
                else check('... a phone\'s width: the strip is at the bottom (Games, Players, Chat), the Games view shows the list and not the chat, one card to a row, and no Join is under anything else ' + at,
                    a.tabsShown && a.tab === 'games' && a.popShown && !a.chatShown && a.tabs[1] >= a.main[1] + a.main[3] - 1 && a.tabs[1] + a.tabs[3] <= a.host[1] + a.host[3] + 1 && a.covered === 0 && a.mapSize === 56, JSON.stringify(a));
                check('the test site, ' + at + ': the label "staging" is in the title and the footer and Home is the same: no stand-in, the same boxes (the bar, the body, Home)', b.label && b.title.indexOf('(staging)') !== -1 && !b.yard && b.role === 'region' && JSON.stringify([a.bar, a.main, a.host]) === JSON.stringify([b.bar, b.main, b.host]), JSON.stringify([a.bar, a.main, a.host]) + ' / ' + JSON.stringify([b.bar, b.main, b.host]));
                if (phone) {
                    await plain.click('.htab.t-chat'); await sleep(200);
                    const c = await look(plain);
                    check('a press on Chat in the strip shows the chat view (open, filling the page between the bar and the strip) and puts the list away, and Games brings the list back ' + at,
                        c.tab === 'chat' && c.chatShown && !c.popShown && c.chat[1] >= c.bar[1] + c.bar[3] - 1 && c.chat[1] + c.chat[3] <= c.tabs[1] + 1 && await plain.evaluate(() => document.querySelector('.gc').getAttribute('data-open') === 'true')
                        && (await plain.click('.htab.t-games'), await sleep(150), await plain.evaluate(() => document.querySelector('.gpop').getClientRects().length > 0 && document.querySelector('.gc').getClientRects().length === 0)), JSON.stringify(c));
                } else {
                    await plain.click('.hchip.games'); await sleep(200);
                    check('Games takes the focus into the list and the list stays ' + at, await plain.evaluate(() => !!document.activeElement.closest('.gpop')) && (await look(plain)).popShown);
                }
                check('... and neither page said anything wrong (no error of the page)', plain.errors.length === 0 && test.errors.length === 0, plain.errors.concat(test.errors).join('; '));
                await plain.context().close(); await test.context().close();
            }
            {   // an empty list and a long one: the empty state with Quick host and Quick join, and a list that scrolls inside itself with the Join by code box at its foot
                const none = await standIn(server, { lobbies: () => ({ format: 1, lobbies: [], count: 0 }) });
                const many = await standIn(server, { lobbies: () => ({ format: 1, count: 12, lobbies: Array.from({ length: 12 }, (_, i) => ({ code: 'm' + (i + 2) + 'x2q' + i, host: 'Host' + i, map: 'TREASURE.LVL', mode: 'highest-score', people: 1, bots: 0, free: 3, size: 4, age: 10 + i })) }) });
                for (const size of [['desktop', 1280, 800], ['phone', 390, 844], ['side', 844, 390]]) {
                    const empty = await newPage(browser, none, 'Maple', size, '/', true), long = await newPage(browser, many, 'Maple', size, '/', true);
                    await empty.waitForSelector('.gpop .gempty:not([hidden])'); await long.waitForSelector('.gpop .gitem'); await long.waitForSelector('.gc', { state: 'attached' });
                    check('' + size[0] + ': no open games shows the empty state (words only, no Quick host or Quick join: the bar has them) and Have a code? in the docked list', await empty.evaluate(() => { const p = document.querySelector('.gpop'), b = [...p.querySelectorAll('.gempty button')]; return p.textContent.indexOf('No open games right now.') !== -1 && !!p.querySelector('.gcodelink') && /none waiting/.test(p.textContent) && b.length === 0; }));
                    const m = await long.evaluate(async () => {
                        const p = document.querySelector('.gpop'), body = p.querySelector('.gpbody'), q = p.getBoundingClientRect(), y = document.getElementById('hmain').getBoundingClientRect(); const scrolls = body.scrollHeight > body.clientHeight + 4;
                        body.scrollTop = body.scrollHeight; await new Promise((r) => setTimeout(r, 50)); const c = p.querySelector('.gcodelink'), cr = c.getBoundingClientRect(), top = document.elementFromPoint(cr.left + cr.width / 2, cr.top + cr.height / 2), last = [...p.querySelectorAll('.gjoin')].pop(), lr = last.getBoundingClientRect(), lt = document.elementFromPoint(lr.left + lr.width / 2, lr.top + lr.height / 2);
                        return { scrolls, bottom: q.bottom <= y.bottom + 1, codeReachable: top === c, lastReachable: lt === last || last.contains(lt), count: document.querySelectorAll('.gpop .gitem').length, wide: document.documentElement.scrollWidth <= window.innerWidth };
                    });
                    check('' + size[0] + ': twelve games scroll inside the list (it stays in the body), the last Join can be reached, Have a code? stays in the heading, nothing sideways', m.scrolls && m.bottom && m.codeReachable && m.lastReachable && m.count === 12 && m.wide, JSON.stringify(m));
                    await empty.context().close(); await long.context().close();
                }
                none.close(); many.close();
            }
            staging.close(); prod.close();
        }

        // ---- 0c. the pictures of the rows of the docked list ------------------------------------------------------------------------------
        {
            const rows = () => ({ format: 1, count: 3, lobbies: [
                { code: 'k7m2xq', host: 'dAwGy', map: 'POPcOrN.lvl', mode: 'highest-score', people: 1, bots: 0, free: 3, size: 4, age: 90 },
                { code: 'p4n8dz', host: 'Pebble', map: 'No Picture Yet.lvl', mode: 'highest-score', people: 2, bots: 0, free: 2, size: 4, age: 60 },
                { code: 'c1v3rr', host: 'Clover', map: 'TREASURE.LVL', mode: 'highest-score', people: 2, bots: 1, free: 1, size: 4, age: 30 }] });
            const site = await standIn(server, { lobbies: rows });
            const page = await newPage(browser, site, 'Maple', SIZES[2], '/', true);
            await page.waitForSelector('.gpop .gitem');
            await page.waitForFunction(() => [...document.querySelectorAll('.gpop .gmap')].every((q) => { const i = q.querySelector('img'); return !i || i.complete; }), null, { timeout: 10000 });
            await sleep(300);
            const pics = await page.evaluate(() => [...document.querySelectorAll('.gpop .gitem')].map((li) => {
                const q = li.querySelector('.gmap'), i = q.querySelector('img'), b = q.getBoundingClientRect();
                return { host: li.querySelector('.gname').textContent, img: !!i, loaded: !!i && i.complete && i.naturalWidth > 0, src: i ? i.getAttribute('src') : null, w: Math.round(b.width), h: Math.round(b.height), bg: getComputedStyle(q).backgroundColor };
            }));
            const by = (host) => pics.find((x) => x.host === host) || {};
            check('a map of the players shows the library\'s picture in its row: the picture is there and loaded (POPcOrN, at /maps/previews/POPcOrN.webp), 72 px square (56 on a phone)', by('dAwGy').loaded === true && by('dAwGy').src === '/maps/previews/POPcOrN.webp' && by('dAwGy').w === 72 && by('dAwGy').h === 72, JSON.stringify(pics));
            check('a map that the library has no picture of (a 404) leaves the plain dark square: no image at all, so no broken image, and the square is still 72 px', by('Pebble').img === false && by('Pebble').w === 72 && by('Pebble').h === 72 && by('Pebble').bg === 'rgb(20, 32, 27)', JSON.stringify(by('Pebble')));
            check('an original shows its own picture, as before (front/preview_treasure.png, loaded)', by('Clover').loaded === true && by('Clover').src === 'front/preview_treasure.png', JSON.stringify(by('Clover')));
            check('the page asked the site for the two pictures by their percent-encoded names and for no list of maps (the 96 KB /maps/all or /maps)', site.hits.indexOf('/maps/previews/POPcOrN.webp') !== -1 && site.hits.indexOf('/maps/previews/No%20Picture%20Yet.webp') !== -1 && !site.hits.some((h) => /^\/maps(\/all)?(\?|$)/.test(h)), JSON.stringify(site.hits.filter((h) => /maps/.test(h))));
            check('(no script error in the page)', page.errors.length === 0, page.errors.join(' | '));
            await page.context().close();
            site.close();
        }

        // ---- 1. two contexts against the real server --------------------------------------------------------------------------------------------------------------------------------------------
        const maple = await newPage(browser, live, 'Maple', SIZES[2]);
        await lit(maple);
        check('the page opens on Home and loads the Games list after its first paint (Quick host, Quick join and Games are lit)', await screenOf(maple) === 'home' && await maple.evaluate(() => document.querySelector('.quick.join').disabled === false));
        await maple.click('.quick.host'); await maple.click('.gconfirm .gyes');       // (Quick host with no room asks Public or Private first: Public room)
        await until('the room is public', async () => (await maple.evaluate(() => { const b = document.querySelector('.vbtn[aria-pressed="true"] b'); return b && b.textContent; })) === 'Public', 10000);
        check('Quick host in a real browser: the Room shows, the switch says Public (the real room said so) and the notice is there', await screenOf(maple) === 'room' && (await textOf(maple, '.gtoast')).indexOf(NOTICES.public) !== -1);
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
        check('Join in a real browser: the guest shows the Room of the host, the chip says "Public room" and there is no switch, and the notice says it', (await textOf(sage, '.vistag')) === 'Public room' && (await sage.evaluate(() => document.querySelector('.vis').hidden)) === true && (await textOf(sage, '.gtoast')).indexOf('Joined') === 0);
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
            await a.evaluate(() => document.querySelector('.hchip.mine').click());
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
            await a.click('.gcodelink'); await a.fill('.gcode input', 'zzzzzz');
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
            await host.click('.quick.host'); await host.click('.gconfirm .gyes');       // (Quick host with no room asks Public or Private first: Public room)
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
            await host.click('.quick.host'); await host.click('.gconfirm .gyes');       // (Quick host with no room asks Public or Private first: Public room)
            await until('the room is public', async () => (await host.evaluate(() => { const b = document.querySelector('.vbtn[aria-pressed="true"] b'); return b && b.textContent; })) === 'Public', 10000);
            const code = await host.evaluate(() => new URL(document.getElementById('link').value).searchParams.get('room'));
            const guest = await newPage(browser, live, 'Sage', size); await lit(guest);
            await until('the real list holds the room', async () => (await (await guest.request.get(live.url + '/lobbies')).json()).lobbies.some((x) => x.code === code), 10000);
            await guest.dblclick('.quick.join');
            await until('the guest is in the Room of the host', async () => (await screenOf(guest)) === 'room' && (await textOf(guest, '#room-h')) === 'Maple’s room', 15000).catch(() => null);
            check(label + ': Quick join (a real double click on it) takes the page to the best open room, once: the host\'s Room, as a guest, with no toast and no card left', (await screenOf(guest)) === 'room' && (await textOf(guest, '#room-h')) === 'Maple’s room' && (await guest.evaluate(() => document.querySelectorAll('.gconfirm').length)) === 0 && (await guest.evaluate(() => document.querySelector('.gtoast').hidden)));
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
            await rae.evaluate(() => document.querySelector('.hchip.mine').click());
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

        // ---- 1e. a match of this browser that is still running (the page's Rejoin offer is up): Quick join, Join and Join by code open nothing and say so, with Rejoin (real clicks, no socket) ------------------------------------
        {
            const ctx = await browser.newContext({ viewport: { width: SIZES[2][1], height: SIZES[2][2] }, deviceScaleFactor: 1 });
            await ctx.addInitScript((entry) => { try { window.localStorage.setItem('ants.name', 'Maya'); window.localStorage.setItem('ants.rejoin.k7m2xq.1', entry); } catch (e) { /* none */ } }, JSON.stringify({ k: '00112233445566778899aabbccddeeff', s: fake.url.replace(/^http/, 'ws') + '/ws', t: Date.now() }));
            const page = await ctx.newPage();
            const sockets = [];
            page.errors = [];
            page.on('pageerror', (e) => page.errors.push(String(e)));
            page.on('websocket', (ws) => { if (!/\/hub\/ws$/.test(ws.url())) sockets.push(ws.url()); });       // (the game server's door: the chat dock's own socket is another)
            await page.goto(fake.url + '/');
            await lit(page);
            check('(a match of this browser is still running: the page offers to rejoin it)', await page.evaluate(() => !document.getElementById('rejoin').hidden));
            const said = () => page.evaluate(() => { const s = document.querySelector('.gtoast'); return s.hidden ? null : s.textContent; });
            await page.click('.quick.join');
            check('Quick join with the Rejoin offer up (a real click): "Your match is running." with Rejoin, no card, no socket', (await said()) === 'Your match is running.RejoinOK' && (await page.evaluate(() => document.querySelectorAll('.gconfirm').length)) === 0 && sockets.length === 0);
            await page.click('.gtoast .btn:last-child');
            await page.click('.hchip.games'); await page.waitForSelector('.glist .gitem');
            await page.click('.gjoin');
            check('Join in the list with the Rejoin offer up: the same words and the same Rejoin, the list stays where it is (docked in the body), no socket', (await said()) === 'Your match is running.RejoinOK' && (await page.evaluate(() => { const p = document.querySelector('.gpop'); return p.classList.contains('gdock') ? !p.hidden : p.hidden; })) && sockets.length === 0);
            await page.click('.gtoast .btn:last-child');
            await page.click('.hchip.games'); await page.waitForSelector('.gcodelink'); await page.click('.gcodelink'); await page.waitForSelector('.gcode input');
            await page.fill('.gcode input', 'abc234');
            await page.click('.gcgo');
            check('Join by code with the Rejoin offer up: the same words, no name card, no socket', (await said()) === 'Your match is running.RejoinOK' && (await page.evaluate(() => document.getElementById('name-step').hidden)) && sockets.length === 0);
            check('(no script error in this page)', page.errors.length === 0, page.errors.join(' | '));
            await ctx.close();
        }

        // ---- 1f. the cards in short and narrow windows (docs/HOME_SCREEN.md): at 568 x 320 and at 195 x 422 (a 390 x 844 phone zoomed to 200 %: the same CSS pixels) both buttons of the empty card and of the question stand inside the card, the card inside the window and nothing scrolls
        // inside it; at 390 x 844 and 1280 x 800 the cards are not compact (the shade's 16 px, the title's 26 px) --------------------------------------------------------------------------------------------
        for (const [label, w, h, compact, narrow] of [['568 x 320', 568, 320, true, false], ['195 x 422 (200 % zoom)', 195, 422, true, true], ['390 x 844', 390, 844, false, false], ['1280 x 800', 1280, 800, false, false]]) {
            const size = ['x', w, h];
            const look = (page) => page.evaluate(() => {
                const box = document.querySelector('.gconfirm'), shade = document.querySelector('.gshade'), r = box.getBoundingClientRect(), cs = getComputedStyle(shade);
                const btns = Array.from(box.querySelectorAll('button')).map((b) => b.getBoundingClientRect());
                return { inWindow: r.top >= 0 && r.left >= 0 && r.bottom <= innerHeight && r.right <= innerWidth, buttonsIn: btns.length >= 1 && btns.every((q) => q.top >= r.top && q.bottom <= r.bottom && q.left >= r.left && q.right <= r.right), noScroll: box.scrollHeight <= box.clientHeight + 1,
                         pad: cs.paddingTop, title: getComputedStyle(box.querySelector('.gctitle')).fontSize, buttons: btns.length };
            });
            const verdict = (what, v) => {
                check(label + ': ' + what + ' card inside the window, both buttons inside the card, nothing scrolls inside it', v.inWindow && v.buttonsIn && v.noScroll, JSON.stringify(v));
                check(label + ': ' + what + ' card is ' + (compact ? 'compact (shade 8 px' + (narrow ? ', title 20 px' : '') + ')' : 'not compact (shade 16 px, title at least 22 px)'),
                      compact ? v.pad === '8px' && (!narrow || v.title === '20px') : v.pad === '16px' && parseFloat(v.title) >= 22, JSON.stringify(v));
            };
            const none = await newPage(browser, emptySite, 'Maya', size); await lit(none);
            await none.click('.quick.join'); await none.waitForSelector('.gconfirm .gcbtns'); await none.waitForTimeout(300);
            const nv = await look(none);
            check(label + ': the empty card has its two buttons', nv.buttons === 2, JSON.stringify(nv));
            verdict('the empty', nv);
            await none.context().close();
            const rae = await newPage(browser, fake, 'Rae', size); await lit(rae);
            await rae.evaluate(() => document.querySelector('.hchip.mine').click()); await rae.waitForSelector('#slots li');
            const room = await rae.evaluate(() => new URL(document.getElementById('link').value).searchParams.get('room'));
            const tess = await newPage(browser, fake, 'Tess', size, '/?room=' + room);
            await tess.click('#name-step-go');
            await until('Tess is in', async () => (await rae.evaluate(() => document.getElementById('slots').textContent.indexOf('Tess') !== -1)), 10000);
            await rae.click('.rhome'); await rae.click('.quick.join'); await rae.waitForSelector('.gconfirm .gcbtns'); await rae.waitForTimeout(300);
            const cv = await look(rae);
            check(label + ': the question has its two buttons', cv.buttons === 2, JSON.stringify(cv));
            verdict('the question', cv);
            await rae.context().close(); await tess.context().close();
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
                check(label + ': the empty list says it, with no button of its own', (await p.evaluate(() => !!document.querySelector('.gempty') && document.querySelectorAll('.gempty button').length === 0)));
                await p.context().close();
                p = await newPage(browser, offSite, 'Maple', size); await lit(p);
                await p.click('.hchip.games'); await p.waitForSelector('.goff:not([hidden])');
                await shot(p, 'games_off', size);
                await p.context().close();
                // the room: Maple, Sage and a computer player; private; then Quick host from Home; Sage's own view
                const a = await newPage(browser, fake, 'Maple', size); await lit(a);
                await a.click('.hmenu', { trial: true }).catch(() => {});
                await a.evaluate(() => document.querySelector('.hchip.mine').click());
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
                await host.click('.quick.host'); await host.click('.gconfirm .gyes');       // (Quick host with no room asks Public or Private first: Public room)
                await host.waitForSelector('.vbtn[aria-pressed="true"] >> text=Public');
                const guest = await newPage(browser, live, 'Sage', size); await lit(guest);
                await guest.waitForFunction(() => { const p = document.querySelector('.hchip.games .pill'); return p && p.textContent === '1'; }, null, { timeout: 12000 });
                await guest.click('.hchip.games'); await guest.waitForSelector('.glist .gitem');
                await guest.click('.gjoin');
                await guest.waitForSelector('.vistag');
                await shot(guest, 'joined', size);
                check(label + ': a Join from the real list brings the guest to the host\'s Room, with the notice', (await textOf(guest, '.gtoast')).indexOf('Joined') === 0 && (await textOf(guest, '.vistag')) === 'Public room');
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
                await qh.click('.quick.host'); await qh.click('.gconfirm .gyes');       // (Quick host with no room asks Public or Private first: Public room)
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
                await qa.evaluate(() => document.querySelector('.hchip.mine').click());
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
        // ---- 1g. the page's own public room is the first row of its Games list, tagged Yours, with no Join; the count and the heading include it; BEST and Quick join are for the other rooms (real clicks, real rooms) ---------
        for (const size of [SIZES[2], SIZES[0]]) {
            const label = size[0] + ' ' + size[1] + 'x' + size[2];
            const rowsOf = (p) => p.evaluate(() => Array.from(document.querySelectorAll('.glist .gitem')).map((li) => ({ name: li.querySelector('.gname').textContent, yours: !!li.querySelector('.gyours') && li.querySelector('.gyours').textContent, best: !!li.querySelector('.gbest'), buttons: li.querySelectorAll('button').length, join: !!li.querySelector('.gjoin') })));
            const hostPublic = async (name) => {
                const p = await newPage(browser, live, name, size); await lit(p);
                await p.click('.quick.host'); await p.click('.gconfirm .gyes');       // (Quick host with no room asks Public or Private first: Public room)
                await until(name + ' room is public', async () => (await p.evaluate(() => { const b = document.querySelector('.vbtn[aria-pressed="true"] b'); return b && b.textContent; })) === 'Public', 10000);
                return p;
            };
            const openGames = async (p) => { if (!(await p.evaluate(() => document.querySelector('.gpop') && !document.querySelector('.gpop').hidden))) await p.click('.hchip.games'); };
            const maple = await hostPublic('Maple');
            await maple.click('.rhome');
            await openGames(maple);
            await until('the own room is the one row', async () => (await rowsOf(maple)).length === 1, 12000);
            let rows = await rowsOf(maple);
            check(label + ': only the own public room is open: one row, Maple, tagged Yours, with no Join button and no button at all, no BEST', rows[0].name === 'Maple' && rows[0].yours === 'Yours' && !rows[0].join && rows[0].buttons === 0 && !rows[0].best);
            check(label + ': ... the button counts it (1), the heading says "1 waiting · updates live", and "No open games right now." is not shown', (await textOf(maple, '.hchip.games .pill')) === '1' && (await textOf(maple, '.gpcount')) === '1 waiting · updates live' && (await maple.evaluate(() => document.querySelector('.gempty').hidden && !document.querySelector('.glist').hidden)));
            await maple.click('.glist .gitem');
            check(label + ': ... a press on the own row changes nothing: Home, the list still open, the room still Maple\'s', (await screenOf(maple)) === 'home' && (await maple.evaluate(() => !document.querySelector('.gpop').hidden && document.querySelectorAll('.glist .gitem').length === 1)));
            const sage = await hostPublic('Sage'), tess = await hostPublic('Tess');
            await until('the list holds three rows', async () => (await rowsOf(maple)).length === 3, 14000);
            rows = await rowsOf(maple);
            check(label + ': own room and two others: Yours is the first row, BEST is on the second and on no other, only the other two have a Join', rows[0].name === 'Maple' && rows[0].yours === 'Yours' && !rows[0].best && !rows[0].join && !rows[1].yours && rows[1].best && rows[1].join && !rows[2].yours && !rows[2].best && rows[2].join, JSON.stringify(rows));
            check(label + ': ... the button says 3 and the heading "3 waiting · updates live"', (await textOf(maple, '.hchip.games .pill')) === '3' && (await textOf(maple, '.gpcount')) === '3 waiting · updates live');
            const bestHost = rows[1].name;
            // the own room goes private: its row is gone at the next look
            await maple.keyboard.press('Escape');
            await maple.evaluate(() => document.querySelector('.hchip.mine').click());
            await maple.click('.vbtn[aria-pressed="false"]');
            await until('the room is private', async () => (await maple.evaluate(() => document.querySelector('.vbtn[aria-pressed="true"] b').textContent)) === 'Private', 10000);
            await maple.click('.rhome');
            await openGames(maple);
            await until('the own row is gone', async () => (await rowsOf(maple)).length === 2, 14000);
            rows = await rowsOf(maple);
            check(label + ': the own room goes private: its row disappears at the next look, the other two stay (BEST on the first), the button says 2', rows.every((r) => r.name !== 'Maple' && !r.yours && r.join) && rows[0].best && !rows[1].best && (await textOf(maple, '.hchip.games .pill')) === '2' && (await textOf(maple, '.gpcount')) === '2 waiting · updates live');
            // public again, and Quick join: the own room is skipped by its code, the page joins the best other room
            await maple.keyboard.press('Escape');
            await maple.evaluate(() => document.querySelector('.hchip.mine').click());
            await maple.click('.vbtn[aria-pressed="false"]');
            await until('the room is public again', async () => (await maple.evaluate(() => document.querySelector('.vbtn[aria-pressed="true"] b').textContent)) === 'Public', 10000);
            await maple.click('.rhome');
            await openGames(maple);
            await until('the own row is back, first', async () => { const r = await rowsOf(maple); return r.length === 3 && r[0].yours === 'Yours'; }, 14000);
            rows = await rowsOf(maple);
            check(label + ': public again: Yours is first again and BEST is still on the first other row (' + rows[1].name + ')', rows[1].best && !rows[2].best && !rows[0].best);
            await maple.keyboard.press('Escape');
            await maple.click('.quick.join');
            await until('Maple is in another room', async () => (await screenOf(maple)) === 'room' && (await textOf(maple, '#room-h')) !== 'Maple’s room', 15000).catch(() => null);
            const wentTo = await textOf(maple, '#room-h');
            check(label + ': Quick join skips the own room (by its code) and joins the best other room: ' + wentTo, (await screenOf(maple)) === 'room' && /^(Sage|Tess)’s room$/.test(wentTo) && wentTo === bestHost + '’s room', wentTo + ' / ' + bestHost);
            for (const p of [maple, sage, tess]) check(label + ': no script error in the page', p.errors.length === 0, p.errors.join(' | '));
            for (const p of [maple, sage, tess]) await p.context().close();
        }

        // ---- 1h. the bars at many widths (docs/HOME_SCREEN.md, "The top bar"): in Home's bar and the Room's bar every button is inside the window and inside the bar's own padding (left >= 0, right <= the window's width), and the page has no sideways scroll, at the sizes ----
        // of the approved pictures, in the band of narrow desktop windows (900 to 1000 px, where the bar was once wider than the window, so that the Menu button was cut off), on every tenth width from 320 to 1300 at five heights and on both sides of every
        // limit of the bar's steps; Home with its widest state (the counts "4/4" and "50+" on My room and Games, the login chip with a long name), one window that only changes its size (the media queries answer at once)
        {
            const WIDEST = () => {
                const set = (q, text) => { const e = document.querySelector(q); if (e) { e.hidden = false; e.textContent = text; } };
                (document.querySelector('.mine') || {}).hidden = false; set('.mine .pill', '4/4'); set('.games .pill', '50+');
                const chip = document.querySelector('.hlogin');
                if (chip) { chip.hidden = false; chip.textContent = ''; for (const [c, t] of [['gc-av', 'M'], ['gc-cn', 'Maple_Leaf_Longname_88']]) { const i = document.createElement('span'); i.className = c; i.textContent = t; chip.appendChild(i); } }
            };
            const OUT = () => {                                                      // what of the visible bar is out of the window, and whether the page scrolls sideways
                const w = document.documentElement.clientWidth, home = document.body.getAttribute('data-screen') === 'home', bar = document.querySelector(home ? '.hbar' : '.rbar'), out = [];
                const box = bar.getBoundingClientRect(), css = window.getComputedStyle(bar), left = Math.max(0, box.left + parseFloat(css.paddingLeft)), right = Math.min(w, box.right - parseFloat(css.paddingRight));       // (the bar's own padding stays free: the window's edge is not the limit)
                bar.querySelectorAll('button, a, img, input, .hbrand').forEach((e) => { const r = e.getBoundingClientRect(); if (r.width < 3 || r.height < 3 || e.closest('[hidden]')) return; if (r.right > right + 0.5 || r.left < left - 0.5) out.push((e.className || e.tagName) + ' ' + Math.round(r.left) + '..' + Math.round(r.right) + ' (' + Math.round(left) + '..' + Math.round(right) + ')'); });
                if (bar.scrollWidth > bar.clientWidth + 0.5) out.push('the bar is wider than itself: ' + bar.scrollWidth + ' > ' + bar.clientWidth);
                if (document.documentElement.scrollWidth > w) out.push('the page scrolls sideways: ' + document.documentElement.scrollWidth + ' > ' + w);
                return out;
            };
            const sizes = [[900, 700], [920, 700], [960, 700], [1000, 700], [844, 390], [390, 844], [360, 640], [320, 568], [1280, 800], [1920, 1080]];
            const SIDEWAYS = [[568, 320], [600, 300], [630, 360], [480, 320], [360, 240], [361, 240], [420, 240], [479, 300], [359, 240], [639, 500], [600, 500], [600, 501], [639, 501], [640, 360], [844, 390], [360, 480], [500, 500]];       // (a phone held sideways, and both sides of the limits of the two compact rows: 360 and 639 px wide, 500 px high, and upright: 360 x 480 and 500 x 500 keep the three rows)
            for (const h of [240, 300, 360, 500, 501, 700, 1000]) for (let w = 320; w <= 1300; w += 10) sizes.push([w, h]);
            for (const edge of [1280, 1077, 892, 775, 721, 640, 390, 389, 360]) for (const h of [240, 360, 700]) sizes.push([edge, h], [edge - 1, h]);
            sizes.push(...SIDEWAYS);
            const bar = await newPage(browser, live, 'Maple', SIZES[2]); await lit(bar);
            await bar.evaluate(WIDEST);
            const badHome = [], badRoom = [];
            for (const [w, h] of sizes) { await bar.setViewportSize({ width: w, height: h }); await bar.evaluate(WIDEST); const o = await bar.evaluate(OUT); if (o.length) badHome.push(w + 'x' + h + ': ' + o.join(', ')); }
            check('Home\'s bar at ' + sizes.length + ' sizes (900 to 1000 px wide, 844 x 390, 390 x 844, 360 x 640, every tenth width from 320 to 1300 px, and both sides of every limit) with the widest counts and a long name on the login chip: every button inside the window and no sideways scroll', badHome.length === 0, badHome.slice(0, 6).join(' | ') + (badHome.length > 6 ? ' ... ' + badHome.length + ' sizes' : ''));
            // a phone held sideways (docs/HOME_SCREEN.md, "The top bar"): 360 to 639 px wide and at most 500 px high, landscape, Home's bar is two compact rows (at most 110 px, the body keeps the rest: 110 px or more at 568 x 320, beside the strip), every target at
            // least 44 px high and wide and no two of them over each other; one pixel narrower or one pixel higher, a window wider than high keeps the three rows (150 px or more), and a phone upright has two rows of 44 px (110 px); from 640 px wide it is the one low row (at most 70 px)
            const BARS = () => {
                const box = (e) => e.getBoundingClientRect(), bar = document.querySelector('.hbar'), yard = document.querySelector('#hmain'), small = [], over = [], cut = [], list = [];
                bar.querySelectorAll('button').forEach((e) => { const r = box(e); if (r.width >= 3 && !e.closest('[hidden]')) list.push([e, r]); });
                bar.querySelectorAll('button, button *').forEach((e) => { if (e.clientWidth && e.scrollWidth > e.clientWidth + 0.5 && !e.closest('[hidden]')) cut.push((e.className || e.tagName) + ' ' + (e.scrollWidth - e.clientWidth)); });       // (a word or a count that is wider than its box)
                list.forEach(([e, r], i) => { if (r.height < 43.5 || r.width < 43.5) small.push(e.className + ' ' + Math.round(r.width) + 'x' + Math.round(r.height)); for (const [f, q] of list.slice(i + 1)) if (Math.min(r.right, q.right) - Math.max(r.left, q.left) > 1 && Math.min(r.bottom, q.bottom) - Math.max(r.top, q.top) > 1) over.push(e.className + ' on ' + f.className); });
                return { bar: Math.round(box(bar).height), yard: Math.round(box(yard).height), small, over, cut };
            };
            const badRows = [];
            for (const [w, h] of SIDEWAYS) {
                await bar.setViewportSize({ width: w, height: h }); await bar.evaluate(WIDEST); const m = await bar.evaluate(BARS), two = w >= 360 && w <= 639 && h <= 500 && w > h;
                if (two && (m.bar > 110 || m.small.length || m.over.length || m.cut.length)) badRows.push(w + 'x' + h + ': two rows expected, the bar is ' + m.bar + ' px high' + (m.small.length ? ', small: ' + m.small.join(', ') : '') + (m.over.length ? ', over: ' + m.over.join(', ') : '') + (m.cut.length ? ', cut: ' + m.cut.join(', ') : ''));
                const upright = h >= w && w <= 720;       // (a phone upright: two rows of 44 px, 110 px high)
                if (upright && (m.bar > 112 || m.small.length || m.over.length || m.cut.filter((c) => !/^gc-cn /.test(c)).length)) badRows.push(w + 'x' + h + ': the upright phone\'s two rows expected, the bar is ' + m.bar + ' px high' + (m.small.length ? ', small: ' + m.small.join(', ') : '') + (m.over.length ? ', over: ' + m.over.join(', ') : '') + (m.cut.length ? ', cut: ' + m.cut.join(', ') : ''));
                if (!two && !upright && w <= 639 && m.bar < 150) badRows.push(w + 'x' + h + ': the three rows expected, the bar is ' + m.bar + ' px high');
                if (w >= 640 && h <= 500 && m.bar > 70) badRows.push(w + 'x' + h + ': one low row expected, the bar is ' + m.bar + ' px high');
                if (w === 568 && h === 320 && m.yard < 110) badRows.push('568x320: the body is only ' + m.yard + ' px high');
            }
            check('a phone held sideways (568 x 320, 600 x 300, 630 x 360, 480 x 320, 360 x 240, 639 x 500) shows two compact rows: the bar at most 110 px high, the body 110 px or more at 568 x 320, every button 44 px or more, none over another and no word or count wider than its button; and the three rows on the other side of each limit (359 x 240, 639 x 501, 600 x 501), the two rows of a phone upright (360 x 480 and 500 x 500: 110 px) and the low row from 640 px wide', badRows.length === 0, badRows.join(' | '));
            // the chat dock's open panel stays under the bar (its top is at or below the bar's bottom), also under the two compact rows (106 px): chat.css
            const badPanel = [];
            for (const [w, h] of [[568, 320], [480, 300], [600, 400], [630, 360], [640, 360], [844, 390]]) {
                await bar.setViewportSize({ width: w, height: h });
                await bar.evaluate((phone) => { const g = document.querySelector('.gc'); if (phone) document.querySelector('.htab.t-chat').click(); else if (g && g.getAttribute('data-open') !== 'true') document.querySelector('.gc-fold').click(); }, w <= 700);
                await sleep(100);
                const m = await bar.evaluate(() => { const g = document.querySelector('.gc'), b = document.querySelector('.hbar').getBoundingClientRect(); return g ? { open: g.getAttribute('data-open'), hidden: g.hidden, top: Math.round(g.getBoundingClientRect().top * 10) / 10, bar: Math.round(b.bottom * 10) / 10 } : null; });
                if (!m || m.hidden || m.open !== 'true' || m.top < m.bar - 0.5) badPanel.push(w + 'x' + h + ': ' + JSON.stringify(m));
            }
            check('the chat dock\'s open panel starts at or below the bottom of the bar at 568 x 320, 480 x 300, 600 x 400, 630 x 360, 640 x 360 and 844 x 390 (the two compact rows are 106 px, the panel used to start at 64 px)', badPanel.length === 0, badPanel.join(' | '));
            await bar.setViewportSize({ width: SIZES[2][1], height: SIZES[2][2] });
            await bar.evaluate(() => document.querySelector('.hchip.mine').click());
            await until('the page shows the Room', async () => (await screenOf(bar)) === 'room', 10000);
            for (const [w, h] of sizes) { await bar.setViewportSize({ width: w, height: h }); const o = await bar.evaluate(OUT); if (o.length) badRoom.push(w + 'x' + h + ': ' + o.join(', ')); }
            check('the Room\'s slim bar at the same ' + sizes.length + ' sizes: every button inside the window and no sideways scroll', badRoom.length === 0, badRoom.slice(0, 6).join(' | ') + (badRoom.length > 6 ? ' ... ' + badRoom.length + ' sizes' : ''));
            check('(no script error in the page)', bar.errors.length === 0, bar.errors.join(' | '));
            await bar.context().close();
        }

        // ---- 1i. a phone whose page reaches under the system bar (viewport-fit=cover): the layout window is taller than what shows, and the browser says by how much (env(safe-area-inset-bottom), here the DevTools override of it). Home, its body and the
        // chat dock (folded, and the panel open; a phone's Games and Chat views) end at or above the visible bottom (the window's height less the inset), at 448 x 998 and 390 x 880 (a 36 px bar), 844 x 390 (24 px), 1280 x 800 (none); Home still fills the window when there is no inset (docs/HOME_SCREEN.md, "The page's height") ----
        {
            const bad = [];
            for (const [w, h, inset] of [[448, 998, 36], [390, 880, 36], [390, 844, 0], [844, 390, 24], [1280, 800, 0]]) {
                const page = await newPage(browser, live, 'Maple', [0, w, h]); await lit(page);
                await (await page.context().newCDPSession(page)).send('Emulation.setSafeAreaInsetsOverride', { insets: { top: 0, left: 0, bottom: inset, right: 0 } });
                await page.waitForSelector('.gc', { state: 'attached' });
                for (const open of [false, true]) {
                    await page.evaluate(([o, phone]) => { const g = document.querySelector('.gc'); if (phone) document.querySelector(o ? '.htab.t-chat' : '.htab.t-games').click(); else if ((g.getAttribute('data-open') === 'true') !== o) document.querySelector('.gc-fold').click(); }, [open, w <= 700]);
                    await sleep(150);
                    const m = await page.evaluate(() => { const b = (q) => Math.round(document.querySelector(q).getBoundingClientRect().bottom * 10) / 10; return { home: b('#home'), yard: b('#hmain'), chat: b('.gc'), open: document.querySelector('.gc').getAttribute('data-open'), end: window.innerHeight }; });
                    const limit = h - inset;
                    if (m.open !== String(open) || m.home > limit + 0.5 || m.yard > limit + 0.5 || m.chat > limit + 0.5 || (inset === 0 && m.home < limit - 0.5)) bad.push(w + 'x' + h + ' inset ' + inset + ' ' + (open ? 'open' : 'strip') + ': ' + JSON.stringify(m));
                }
                await page.context().close();
            }
            check('with the system bar over the last 36 px (448 x 998, 390 x 880), 24 px (844 x 390) or nothing (390 x 844, 1280 x 800): Home, the body and the chat dock (folded and open) end at or above the visible bottom (the window less the inset), and Home fills the window when there is no inset', bad.length === 0, bad.join(' | '));
        }
        // ---- 1j. the Room's Public | Private switch: the label sits in the middle of its button (docs/HOME_SCREEN.md, "The Room's switch"). The two words (the big "Private" / "Public" and the small "link only" / "listed") share a baseline; they were a flex row
        // with align-items: baseline, which hangs at the TOP of a 44 px button (the capitals' middle was 7 px above the button's middle, more empty space below the text than above). The middle of the big word's capitals (its baseline less half
        // the cap height of its font) is within 1 px of the middle of the button, with either button selected, at a phone's 448 x 962, 390 x 844, 360 x 640, 320 x 568 (a pixel ratio of 2.6), 844 x 390 and 1280 x 800; the button is 44 px high and the words are not cut.
        // At 320 px (and 300 to 346) "link only" spilled past the button's right border (the label did not fit the box): at every width from 300 to 400 px, and at a few above, the small word is inside its button (not past the border, one line) ----
        {
            const SWITCH = () => {
                const br0 = (el) => el.getBoundingClientRect();
                const cv = document.createElement('canvas').getContext('2d'), out = [];
                const baseline = (el) => { const m = document.createElement('span'); m.style.cssText = 'display:inline-block;width:0;height:0;vertical-align:baseline'; el.insertBefore(m, el.firstChild); const y = m.getBoundingClientRect().bottom; m.remove(); return y; };
                for (const btn of document.querySelectorAll('.vbtn')) {
                    const r = btn.getBoundingClientRect(), big = btn.querySelector('b'), small = btn.querySelector('small'), cs = getComputedStyle(big);
                    cv.font = cs.fontWeight + ' ' + cs.fontSize + ' ' + cs.fontFamily;
                    const base = baseline(big), cap = cv.measureText('P').actualBoundingBoxAscent;
                    const sr = small.getBoundingClientRect();
                    out.push({ word: big.textContent, pressed: btn.getAttribute('aria-pressed'), h: r.height, gap: r.right - sr.right, lgap: br0(big).left - r.left, lines: sr.height, off: (base - cap / 2) - (r.top + r.bottom) / 2, sub: Math.abs(baseline(small) - base), cut: btn.scrollWidth - btn.clientWidth, cap, fonts: document.fonts.check(cs.fontSize + ' "Libre Franklin"') });
                }
                return out;
            };
            const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 }, deviceScaleFactor: 2.6 });
            await ctx.addInitScript(() => { try { window.localStorage.setItem('ants.name', 'Maple'); } catch (e) { /* none */ } });
            const sw = await ctx.newPage(); sw.errors = [];
            sw.on('pageerror', (e) => sw.errors.push(String(e)));
            await sw.goto(live.url + '/'); await lit(sw);
            await sw.evaluate(() => document.querySelector('.hchip.mine').click()); await sw.waitForSelector('#slots li'); await sw.waitForSelector('.vis:not([hidden])');
            const bad = [], seen = [];
            for (const state of ['Private', 'Public']) {
                if (state === 'Public') {
                    await sw.click('.vbtn[aria-pressed="false"]');
                    await until('the room is public', async () => (await sw.evaluate(() => document.querySelector('.vbtn[aria-pressed="true"] b').textContent)) === 'Public', 10000);
                }
                for (const [w, h] of [[448, 962], [390, 844], [360, 640], [320, 568], [844, 390], [1280, 800]]) {
                    await sw.setViewportSize({ width: w, height: h }); await sw.evaluate(() => document.fonts.ready); await sleep(120);
                    const m = await sw.evaluate(SWITCH);
                    seen.push(m.length);
                    for (const b of m) if (m.length !== 2 || !b.fonts || b.cap < 8 || Math.abs(b.off) > 1 || Math.abs(b.h - 44) > 0.5 || b.sub > 0.6 || b.cut > 0.5 || b.gap < 0 || b.lgap < 0 || b.lines > 20 || (b.pressed === 'true') !== (b.word === state)) bad.push(state + ' selected, ' + w + 'x' + h + ', ' + b.word + (b.pressed === 'true' ? ' (selected)' : '') + ': the capitals\' middle is ' + b.off.toFixed(2) + ' px from the button\'s middle, ' + b.h + ' px high, the small word ' + b.sub.toFixed(2) + ' px off the baseline, cut ' + b.cut + ', ' + b.gap.toFixed(2) + ' px from the small word to the right border, cap ' + b.cap.toFixed(1));
                }
            }
            check('the Room\'s Public | Private switch (448 x 962, 390 x 844, 360 x 640, 320 x 568 at a pixel ratio of 2.6, 844 x 390, 1280 x 800; Private selected, then Public selected): in each of the two buttons the capitals of the big word are within 1 px of the middle of the button (the label was 7 px too high), the button is 44 px high, the small word shares the baseline, is inside the button (at 320 px "link only" was 3.6 px past the border) and nothing is cut', bad.length === 0 && seen.length === 12, bad.slice(0, 4).join(' | ') + (bad.length > 4 ? ' ... ' + bad.length + ' more' : ''));
            // every width from 300 to 400 px (a narrow phone, or a window): the small word of either button, selected or not, is inside the button and on one line, the capitals stay in the middle, the button stays 44 px high
            const scan = [], wide = [];
            for (let w = 300; w <= 400; w++) scan.push(w);
            scan.push(420, 448, 520, 720, 721, 900, 1400);
            let scanned = 0;
            for (const state of ['Public', 'Private']) {                      // (Public is selected now; the second pass selects Private)
                if (state === 'Private') {
                    await sw.click('.vbtn[aria-pressed="false"]');
                    await until('the room is private', async () => (await sw.evaluate(() => document.querySelector('.vbtn[aria-pressed="true"] b').textContent)) === 'Private', 10000);
                }
                for (const w of scan) {
                    await sw.setViewportSize({ width: w, height: w < 500 ? 640 : 800 }); await sleep(15);
                    const m = await sw.evaluate(SWITCH);
                    scanned += m.length;
                    for (const b of m) if (m.length !== 2 || b.gap < 0 || b.lgap < 0 || b.lines > 20 || b.cut > 0.5 || Math.abs(b.off) > 1 || Math.abs(b.h - 44) > 0.5) wide.push(state + ' selected, ' + w + ' px, ' + b.word + ': ' + b.gap.toFixed(2) + ' px from the small word to the right border, ' + b.lgap.toFixed(2) + ' px from the big word to the left, cut ' + b.cut + ', off ' + b.off.toFixed(2) + ', ' + b.h + ' px high');
                }
            }
            check('the Room\'s switch at every width from 300 to 400 px and at 420, 448, 520, 720, 721, 900 and 1400 (either button selected): both words of each button are inside it (at least 0 px to the borders), on one line, nothing is cut, the capitals are within 1 px of the middle and the button is 44 px high (the small word spilled past the border from 300 to 327 px)', wide.length === 0 && scanned === 2 * 2 * scan.length, wide.slice(0, 4).join(' | ') + (wide.length > 4 ? ' ... ' + wide.length + ' more' : ''));
            check('(no script error in the page)', sw.errors.length === 0, sw.errors.join(' | '));
            await ctx.close();
        }
    } finally {
        for (const s of [live, fake, emptySite, offSite]) s.close();
        await browser.close();
        server.stop();
    }
}

flows().then(() => { console.log('public room and games list in a real browser: ' + checks + ' checks, ' + failures + ' failed'); process.exit(failures === 0 ? 0 : 1); }, (e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
