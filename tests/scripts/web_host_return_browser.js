// OPT-IN, not part of ./run_tests.sh: the host of a room who goes to another page of the site (the list of matches, a replay) and comes back, in a REAL browser (Chromium through Playwright), against a real
// native ants_server and a small stand-in for the site that serves the files of web/, leads /ws to the server's WebSocket door and passes the public lists (/lobbies) on. A guest is a second browser of its own.
// What no fake DOM can show (docs/PLAY_IN_BROWSER.md, "Going away and coming back"):
//   0. while the page holds a room, its links to the matches open in a NEW tab (the maintainer's decision of 2026-11-17), and the room's tab stays connected for as long as the replay is watched (66 s of it: the
//      guest sees the host lead all the time, the room has two people); with no room the same links are plain links of the same tab;
//   1. the host leaves the front page for the list of matches (the same tab: the browser's own Back, a typed address) and is back within the minute that the room holds a seat: the same seat and the same lead, no strip, nobody joined or left;
//   2. the host leaves the front page for the list of matches and the person opens the site in a NEW tab within the minute: the new tab takes the same seat back (My room, the lead, no Join in the list for a room
//      that is the person's own, "Yours"), and the room still has two people, not three;
//   3. the host is away for more than the minute: the guest leads, and the page that comes back says that the seat ended and names the host, is a guest in that room and has no claim to lead it
//      (this part and the 66 s of part 0 take about 150 seconds together: the room's hold is a minute and is not a setting).
//
//   node tests/scripts/web_host_return_browser.js [--web DIR] [--server PATH] [--quick]     (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox; ANTS_SERVER_BIN or --server names the
//                                                                                          native ants_server, default build/src/ants_server/ants_server; --quick leaves out the parts that wait a minute: the 66 s of part 0, and part 3)
//
// Needs `playwright` (npm) and a Chromium that it can start, and a built ants_server; without them it says so and exits 0 (nothing was checked). All names here are the guest names that the pages make for
// themselves. Exit status: 0 every check passed (or none could be made), 1 a check failed or the page broke.
'use strict';
const fs = require('fs');
const http = require('http');
const net = require('net');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the host that comes back was NOT run in a real browser'); process.exit(0); }
const ROOT = path.resolve(__dirname, '..', '..');
const arg = (name) => { const i = process.argv.indexOf(name); return i === -1 ? null : process.argv[i + 1]; };
const WEB = path.resolve(arg('--web') || path.join(ROOT, 'web'));
const SERVER = path.resolve(arg('--server') || process.env.ANTS_SERVER_BIN || path.join(ROOT, 'build', 'src', 'ants_server', 'ants_server'));
const QUICK = process.argv.indexOf('--quick') !== -1;
if (!fs.existsSync(SERVER)) { console.log('SKIP: the game server program is not there (build the target ants_server, or give --server): the host that comes back was NOT run in a real browser'); process.exit(0); }
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.webp': 'image/webp', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml', '.webmanifest': 'application/manifest+json' };
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
function freePort() {
    return new Promise((resolve, reject) => { const s = net.createServer(); s.on('error', reject); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); }); });
}
function get(port, pathname) {
    return new Promise((resolve, reject) => {
        http.get({ host: '127.0.0.1', port, path: pathname }, (res) => { let body = ''; res.on('data', (d) => { body += d; }); res.on('end', () => resolve({ status: res.statusCode, body })); }).on('error', reject);
    });
}

// The game server of this tree with the lobby rooms and the public door, in a folder of its own (nothing else is touched)
async function startServer() {
    const ports = { tcp: await freePort(), ws: await freePort(), ctl: await freePort(), door: await freePort() };
    const results = fs.mkdtempSync(path.join(os.tmpdir(), 'ants_host_return.'));
    const child = spawn(SERVER, ['--maps', path.join(ROOT, 'Original-Ants', 'Maps'), '--port', String(ports.tcp), '--ws-port', String(ports.ws), '--ctl-port', String(ports.ctl), '--replay-port', String(ports.door),
        '--results-dir', results, '--demo-rooms', '5', '--demo-map', 'TINY.LVL', '--demo-maps', 'TINY.LVL,MEDIUM.LVL', '--demo-lobbies', '12'], { env: Object.assign({}, process.env, { ANTS_SERVER_SECRET: 'host-return-check-secret' }), stdio: 'ignore' });
    let ended = false;
    child.on('exit', () => { ended = true; });
    for (let i = 0; i < 100; i++) {
        if (ended) throw new Error('the game server stopped at startup');
        try { if ((await get(ports.ctl, '/healthz')).status === 200) return { ports, stop: () => { child.kill(); fs.rmSync(results, { recursive: true, force: true }); } }; } catch (e) { /* not listening yet */ }
        await sleep(100);
    }
    child.kill();
    throw new Error('the game server did not answer /healthz');
}

// The site: the files of web/, /ws to the server's WebSocket door, /lobbies and the other public lists to its public door
function serve(server) {
    const door = ['/lobbies', '/live', '/replays', '/history', '/maps', '/maps/all'];
    const through = (req, res, port) => {
        const up = http.request({ host: '127.0.0.1', port, path: req.url, method: req.method, headers: { host: '127.0.0.1' } }, (r) => { res.writeHead(r.statusCode, r.headers); r.pipe(res); });
        up.on('error', () => { res.writeHead(502); res.end('{}'); });
        up.end();
    };
    const site = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        if (door.some((p) => url.pathname === p || url.pathname.indexOf(p + '/') === 0)) return through(req, res, server.ports.door);
        if (url.pathname === '/busy') return through(req, res, server.ports.ws);
        if (url.pathname === '/stats') { res.writeHead(404, { 'Content-Type': 'application/json' }); return res.end('{}'); }
        const alias = { '/': 'lobby.html', '/matches': 'matches.html', '/match': 'match.html', '/play': 'shell.html' };
        const file = alias[url.pathname] || url.pathname.replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'application/json' }); return res.end('{"error":"not here"}'); }
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
        res.end(fs.readFileSync(full));
    });
    site.on('upgrade', (req, socket, head) => {                                 // (the page's WebSocket: the bytes go through as they are)
        if (req.url.split('?')[0] !== '/ws') { socket.destroy(); return; }
        const up = net.connect(server.ports.ws, '127.0.0.1', () => {
            let raw = req.method + ' ' + req.url + ' HTTP/1.1\r\n';
            for (let i = 0; i < req.rawHeaders.length; i += 2) raw += req.rawHeaders[i] + ': ' + (req.rawHeaders[i].toLowerCase() === 'host' ? '127.0.0.1' : req.rawHeaders[i + 1]) + '\r\n';
            up.write(raw + '\r\n');
            if (head && head.length) up.write(head);
            socket.pipe(up); up.pipe(socket);
        });
        up.on('error', () => socket.destroy()); socket.on('error', () => up.destroy()); socket.on('close', () => up.destroy()); up.on('close', () => socket.destroy());
    });
    return new Promise((resolve) => site.listen(0, '127.0.0.1', () => resolve({ url: 'http://127.0.0.1:' + site.address().port, port: site.address().port, close: () => new Promise((r) => { site.closeAllConnections && site.closeAllConnections(); site.close(() => r()); }) })));
}

const listed = async (server) => JSON.parse((await get(server.ports.door, '/lobbies')).body).lobbies;
// What a front page shows of its room: the screen, My room, the heading, the strip, the cards' labels, the tab's saved room and the Games list's own words
const snap = (page) => page.evaluate(() => {
    const mine = document.querySelector('.hbar .mine');
    const session = (() => { try { return JSON.parse(sessionStorage.getItem('ants.lobby')); } catch (e) { return null; } })();
    return {
        screen: document.body.getAttribute('data-screen'), mine: mine && !mine.hidden ? mine.textContent : null, heading: document.getElementById('room-h').textContent,
        banner: document.getElementById('banner').hidden ? '' : document.getElementById('banner-text').textContent, slots: Array.from(document.querySelectorAll('#slots li')).map((li) => li.getAttribute('aria-label')),
        code: session && session.c, own: session && session.o, join: document.querySelectorAll('.gjoin').length, yours: /Yours/.test((document.querySelector('#hmain') || { textContent: '' }).textContent)
    };
});
const people = (s) => (s.slots || []).filter((l) => l && !/: (open|Open)$/i.test(l)).length;

// A host: the page makes a public room with Quick host, and says its code
async function host(context, site) {
    const page = await context.newPage();
    await page.goto(site.url + '/', { waitUntil: 'load' });
    await page.waitForSelector('.hbar', { timeout: 8000 });
    await sleep(600);
    await page.click('button:has-text("Quick host")');
    await sleep(300);
    await page.click('button:has-text("Public room")');
    await page.waitForFunction(() => document.body.getAttribute('data-screen') === 'room' && /\S/.test(document.getElementById('code').textContent), null, { timeout: 8000 });
    const code = (await page.evaluate(() => document.getElementById('code').textContent)).replace(/\s/g, '');
    await sleep(500);
    return { page, code };
}
// A guest: the link of the room and the name card
async function guest(context, site, code) {
    const page = await context.newPage();
    await page.goto(site.url + '/?room=' + code, { waitUntil: 'load' });
    await page.waitForSelector('#name-step-go', { timeout: 8000 });
    await page.click('#name-step-go');
    await page.waitForFunction(() => document.querySelectorAll('#slots li.k-person').length >= 2, null, { timeout: 8000 });
    await sleep(300);
    return page;
}
const settle = async (page) => { await page.waitForSelector('.hbar', { timeout: 8000 }); await sleep(2500); };

async function main() {
    const server = await startServer();
    const site = await serve(server);
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the host that comes back was NOT run in a real browser'); await site.close(); server.stop(); return; }
    const errors = [];
    const watch = (page) => page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
    try {
        // ---- 0. the links to the matches open in a new tab while the page holds a room: the room's tab stays where it is ---------------------------------------------------------------------------------------------
        {
            const hostCtx = await browser.newContext({ viewport: { width: 1280, height: 800 } }), guestCtx = await browser.newContext({ viewport: { width: 1280, height: 800 } });
            const plain = await hostCtx.newPage(); watch(plain);
            await plain.goto(site.url + '/', { waitUntil: 'load' });
            await plain.waitForSelector('.hbar', { timeout: 8000 });
            const attrs = (page) => page.evaluate(() => ['watch-top', 'watch-link'].map((id) => { const a = document.getElementById(id); return [a.getAttribute('href'), a.getAttribute('target'), a.getAttribute('rel')]; }));
            check('0. with no room (a plain visit is Home) the links to the matches are plain links: no target', JSON.stringify(await attrs(plain)) === JSON.stringify([['/matches', null, null], ['/matches', null, null]]), JSON.stringify(await attrs(plain)));
            await plain.close();
            const A = await host(hostCtx, site); watch(A.page);
            const B = await guest(guestCtx, site, A.code); watch(B);
            const guestBefore = await snap(B);
            check('0. a page that holds a room: the links to the matches open in a new tab (_blank, noopener noreferrer)', JSON.stringify(await attrs(A.page)) === JSON.stringify([['/matches', '_blank', 'noopener noreferrer'], ['/matches', '_blank', 'noopener noreferrer']]), JSON.stringify(await attrs(A.page)));
            await A.page.click('.rbar .hmenu');                                              // (the masthead's and the footer's links are not drawn on the Room: the Menu's Matches is the way)
            const item = A.page.locator('.hmenubox a.mitem', { hasText: 'Matches' });
            check('0. the Menu\'s Matches item opens in a new tab too', (await item.getAttribute('target')) === '_blank' && (await item.getAttribute('rel')) === 'noopener noreferrer');
            const [tab] = await Promise.all([hostCtx.waitForEvent('page'), item.click()]);
            watch(tab);
            await tab.waitForLoadState('load');
            check('0. the press opens the list of matches in another tab, and the room\'s tab is still on the front page with its room', new URL(tab.url()).pathname === '/matches' && new URL(A.page.url()).pathname === '/' && (await snap(A.page)).heading === 'Your room');
            if (!QUICK) {
                await sleep(66000);                                                           // (longer than the room holds a seat: a tab that had left would have lost the lead by now)
                const seen = await snap(B), mine = await snap(A.page);
                check('0. after 66 seconds of a replay the room\'s tab is connected still: the host leads, there is no strip, the guest saw nobody leave, and the room has two people', mine.heading === 'Your room' && mine.banner === '' && mine.slots.some((l) => /you, host$/.test(l)) && JSON.stringify(seen.slots) === JSON.stringify(guestBefore.slots) && seen.banner === '' && (await listed(server)).every((x) => x.code !== A.code || x.people === 2), JSON.stringify([mine, seen]));
            }
            await hostCtx.close(); await guestCtx.close();
        }
        // ---- 1. back within the minute, in the same tab: the same seat and the same lead -------------------------------------------------------------------------------------------------------------------
        {
            const hostCtx = await browser.newContext({ viewport: { width: 1280, height: 800 } }), guestCtx = await browser.newContext({ viewport: { width: 1280, height: 800 } });
            const A = await host(hostCtx, site); watch(A.page);
            const B = await guest(guestCtx, site, A.code); watch(B);
            const before = await snap(A.page);
            check('(the host leads a room of two: "Your room", the host\'s card says "you, host")', before.heading === 'Your room' && people(before) === 2 && before.slots.some((l) => /you, host$/.test(l)), JSON.stringify(before));
            const guestBefore = await snap(B);
            await A.page.goto(site.url + '/matches', { waitUntil: 'load' });
            await sleep(5000);
            await A.page.goto(site.url + '/', { waitUntil: 'load' });
            await settle(A.page);
            const back = await snap(A.page), seen = await snap(B);
            check('1. the host comes back to the front page after 5 seconds: My room is there with its two colors, the room is "Your room", the host leads again', back.mine !== null && back.heading === 'Your room' && back.slots.some((l) => /you, host$/.test(l)), JSON.stringify(back));
            check('1. ... and says nothing about a seat that ended (no strip), and the room has two people', back.banner === '' && people(back) === 2 && (await listed(server)).every((x) => x.code !== A.code || x.people === 2), JSON.stringify(back));
            check('1. ... the guest sees the same room as before: the same host, the same two people, no strip', JSON.stringify(seen.slots) === JSON.stringify(guestBefore.slots) && seen.banner === '' && seen.heading === guestBefore.heading, JSON.stringify(seen));
            await hostCtx.close(); await guestCtx.close();
        }
        // ---- 2. a NEW tab within the minute: the seat that the first page left ------------------------------------------------------------------------------------------------------------------------------
        {
            const hostCtx = await browser.newContext({ viewport: { width: 1280, height: 800 } }), guestCtx = await browser.newContext({ viewport: { width: 1280, height: 800 } });
            const A = await host(hostCtx, site); watch(A.page);
            const B = await guest(guestCtx, site, A.code); watch(B);
            const guestBefore = await snap(B);
            await A.page.goto(site.url + '/matches', { waitUntil: 'load' });                // (the first tab leaves the front page: its seat is held, and it left a note)
            const fresh = await hostCtx.newPage(); watch(fresh);                             // (a new tab: it has no session of its own)
            await fresh.goto(site.url + '/', { waitUntil: 'load' });
            await settle(fresh);
            const back = await snap(fresh), seen = await snap(B), list = await listed(server);
            check('2. the new tab takes the seat that the first page left: My room is there, the room is "Your room", the host leads', back.mine !== null && back.heading === 'Your room' && back.slots.some((l) => /you, host$/.test(l)) && back.code === A.code, JSON.stringify(back));
            check('2. ... it does not offer to Join the room (the room is the person\'s own, "Yours"), and there is no strip', back.join === 0 && back.yours === true && back.banner === '', JSON.stringify(back));
            check('2. ... the room has two people, not three: the real list says so, and the guest saw nobody join or leave', list.filter((x) => x.code === A.code).every((x) => x.people === 2) && people(back) === 2 && JSON.stringify(seen.slots) === JSON.stringify(guestBefore.slots), JSON.stringify(list));
            await hostCtx.close(); await guestCtx.close();
        }
        // ---- 3. away for more than the minute: the seat ended -------------------------------------------------------------------------------------------------------------------------------------------------
        if (!QUICK) {
            const hostCtx = await browser.newContext({ viewport: { width: 1280, height: 800 } }), guestCtx = await browser.newContext({ viewport: { width: 1280, height: 800 } });
            const A = await host(hostCtx, site); watch(A.page);
            const B = await guest(guestCtx, site, A.code); watch(B);
            const guestName = (await snap(B)).slots.filter((l) => /, you$/.test(l))[0].replace(/^[A-Za-z]+: /, '').replace(/, you$/, '');
            await A.page.goto(site.url + '/matches', { waitUntil: 'load' });
            await sleep(66000);                                                               // (the room holds the seat for 60 seconds)
            const lead = await snap(B);
            check('3. the guest leads the room when the minute is over, and the host\'s color is open', /you, host$/.test(lead.slots.filter((l) => /, you/.test(l))[0] || '') && lead.slots.some((l) => /^Green: open$/i.test(l) || /: open$/i.test(l)) && /left, so you are the host now/.test(lead.banner), JSON.stringify(lead));
            await A.page.goto(site.url + '/', { waitUntil: 'load' });
            await settle(A.page);
            const back = await snap(A.page);
            check('3. the page that comes back says that the seat ended and who the host is: ' + guestName, back.banner === 'Your seat in this room ended while you were away, so you joined again. ' + guestName + ' is the host.', back.banner);
            check('3. ... it is a guest in the guest\'s room (not the host, not a second seat: two people in all), and the saved room says that this page did not make it', back.heading === guestName + '’s room' && !back.slots.some((l) => /you, host$/.test(l)) && back.slots.some((l) => /, you$/.test(l)) && people(back) === 2 && back.own === 0, JSON.stringify(back));
            await hostCtx.close(); await guestCtx.close();
        }
        check('no page error', errors.length === 0, errors.join(' | '));
    } finally {
        await browser.close(); await site.close(); server.stop();
    }
}
main().then(() => { console.log('the host that comes back, in a browser: ' + checks + ' checks, ' + failures + ' failed'); process.exit(failures === 0 ? 0 : 1); }, (e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
