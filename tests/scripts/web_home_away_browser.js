// OPT-IN, not part of ./run_tests.sh: the Away rows of the players list in a REAL browser (Chromium through Playwright), against the fake hub of tests/scripts/fake_hub.js behind a small stand-in for the site
// that serves the files of web/ and a fake /lobbies. What no fake DOM can show (docs/HUB.md "Away", docs/HOME_SCREEN.md "The players list"):
//   1. a mix of rows (Looking, Waiting, Playing, Away, a moderator who is Away): the groups in their order with the Away group last, an Away card is dimmed (its box has an opacity under 1, a live card has 1), the word is
//      there for a reader (the group's heading, and "Away" under a moderator's name), no Join on an Away row, no sideways scroll at 1280 x 800 and at 390 x 844 (the Players view);
//   2. a row that comes back (the hub says it Looking again) and one that goes (`gone`).
//   --shots DIR takes the pictures (1280 x 800 and 390 x 844) with a README.md.
//
//   node tests/scripts/web_home_away_browser.js [--web DIR] [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). All names here are made up. Exit status: 0 every check passed (or none could be made), 1 a check
// failed or the page broke.
'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the Away rows were NOT run in a real browser'); process.exit(0); }
const ROOT = path.resolve(__dirname, '..', '..');
const arg = (name) => { const i = process.argv.indexOf(name); return i === -1 ? null : process.argv[i + 1]; };
const WEB = path.resolve(arg('--web') || path.join(ROOT, 'web'));
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml' };
const { startHub, line } = require('./fake_hub.js');
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const LOBBIES = { format: 1, count: 1, lobbies: [{ code: 'c1v3rr', host: 'Clover', map: 'TREASURE.LVL', mode: 'highest-score', people: 2, bots: 1, free: 1, size: 4, age: 200 }] };
function serve() {
    const server = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        if (url.pathname === '/lobbies') { res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(LOBBIES)); return; }
        if (url.pathname === '/stats' || url.pathname === '/busy') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end('{}'); return; }
        const file = url.pathname === '/' ? 'lobby.html' : url.pathname.replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'application/json' }); res.end('{"error":"not here"}'); return; }
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
        res.end(fs.readFileSync(full));
    });
    return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({ server, url: 'http://127.0.0.1:' + server.address().port, close: () => new Promise((r) => server.close(() => r())) })));
}
const ME = { name: 'maple.leaf', mod: false, role: 'none' };
const row = (i, name, at, more) => Object.assign({ i, name, at: at || 'look' }, more || {});
const ROWS = [row(2, 'ada', 'look', { mod: true, role: 'admin' }), row(3, 'moss', 'away', { mod: true, role: 'mod' }), row(4, 'juniper', 'look'), row(5, 'pebble', 'wait'), row(6, 'clover', 'wait', { pub: true, map: 'TREASURE.LVL', n: 2, of: 4, code: 'c1v3rr' }),
    row(7, 'hazel', 'play', { pub: true, map: 'ISLANDS.LVL', n: 3 }), row(8, 'fern', 'away'), row(9, 'basil', 'away')];
const WHO = (rows, more) => Object.assign({ t: 'who', full: true, n: 6, g: 2, more: 0, rows }, more || {});
const groups = (page) => page.evaluate(() => [...document.querySelectorAll('#players .pl-group')].map((g) => g.getAttribute('data-g') + ':' + [...g.querySelectorAll('.pl-nm')].map((n) => n.textContent).join('+')));
const settled = (page) => page.evaluate(() => new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(resolve, 0)))));

async function main() {
    const site = await serve();
    const hub = await startHub({ server: site.server, login: true, you: ME, history: [line(1, 'pebble', 'anyone up for Islands?'), line(2, 'clover', 'I have a room open')], online: 6, welcomeExtra: { presence: true, me: 1 }, who: WHO(ROWS) });
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the Away rows were NOT run in a browser'); await hub.close(); await site.close(); process.exit(0); }
    const errors = [], taken = [];
    try {
        for (const [width, height, name] of [[1280, 800, 'away_1280x800.png'], [390, 844, 'away_390x844.png']]) {
            hub.set({ who: WHO(ROWS) });
            const context = await browser.newContext({ viewport: { width, height }, deviceScaleFactor: SHOTS ? 2 : 1 });
            const page = await context.newPage();
            page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
            await page.goto(site.url + '/', { waitUntil: 'load' });
            await page.waitForSelector('#players .pl-card', { state: 'attached', timeout: 8000 });
            if (width <= 700 || height <= 500) { await page.click('.htab.t-players'); await sleep(200); }
            await settled(page);
            const size = width + ' x ' + height;
            check(size + ': the groups in their order, a moderator who is away stays with the moderators, the Away group is last', JSON.stringify(await groups(page)) === JSON.stringify(['mod:ada+moss', 'look:juniper', 'wait:clover+pebble', 'play:hazel', 'away:basil+fern']), JSON.stringify(await groups(page)));
            const opacity = await page.evaluate(() => [...document.querySelectorAll('#players .pl-card')].map((c) => [c.querySelector('.pl-nm').textContent, c.getAttribute('data-s'), Number(getComputedStyle(c).opacity)]));
            check(size + ': an Away card is dimmed and a live card is not', opacity.filter((x) => x[1] === 'away').length === 3 && opacity.every((x) => (x[1] === 'away') === (x[2] < 1)), JSON.stringify(opacity));
            check(size + ': the word is there for a reader (the heading, and under a moderator\'s name)', await page.evaluate(() => /Away/.test(document.querySelector('#players .pl-group[data-g="away"] .pl-gh').textContent) && /Away/.test([...document.querySelectorAll('#players .pl-card')].find((c) => c.querySelector('.pl-nm').textContent === 'moss').querySelector('.pl-meta').textContent)));
            check(size + ': no Join on an Away row (and the Join of the room that waits is still there)', await page.evaluate(() => [...document.querySelectorAll('#players .pl-card')].every((c) => !c.querySelector('.pl-join') || c.getAttribute('data-s') !== 'away') && document.querySelectorAll('#players .pl-join').length === 1));
            check(size + ': no sideways scroll', await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1));
            if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, name) }); taken.push([name, size]);
                await page.evaluate(() => { const l = document.querySelector('#players .pl-list'); l.scrollTop = l.scrollHeight; });
                await sleep(150);
                const down = name.replace('.png', '_scrolled.png');
                await page.screenshot({ path: path.join(SHOTS, down) }); taken.push([down, size + ', the list scrolled to its end (the Away group)']);
                await page.evaluate(() => { document.querySelector('#players .pl-list').scrollTop = 0; });
            }
            if (width > 700 && height > 500) {
                hub.set({ who: WHO([row(8, 'fern')], { full: false, gone: [9] }) });
                hub.broadcast(WHO([row(8, 'fern')], { full: false, gone: [9] }));
                await sleep(300);
                const after = JSON.stringify(await groups(page));
                check('a row that is back is Looking again and one that is gone is gone', after.indexOf('look:fern+juniper') !== -1 && after.indexOf('basil') === -1 && after.indexOf('"away:') === -1, after);
            }
            await context.close();
        }
        if (SHOTS) fs.writeFileSync(path.join(SHOTS, 'README.md'), ['# Away rows of the players list (built page)', '', 'A mix of rows: a moderator who is Away (with the moderators, dimmed, "Away" under the name), people who look, wait and play, and an Away group at the bottom.', '', '| File | Window |', '|---|---|'].concat(taken.map((t) => '| ' + t[0] + ' | ' + t[1] + ' |')).join('\n') + '\n');
        check('no page error', errors.length === 0, errors.join(' | '));
    } finally {
        await browser.close(); await hub.close(); await site.close();
    }
}
main().then(() => { console.log('away rows in a browser: ' + checks + ' checks, ' + failures + ' failed'); process.exit(failures === 0 ? 0 : 1); }, (e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
