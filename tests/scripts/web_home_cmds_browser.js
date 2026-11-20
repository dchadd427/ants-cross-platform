// OPT-IN, not part of ./run_tests.sh: the commands of the chat dock of Home (web/home/cmds.js and cmds.css, docs/HOME_CHAT.md "Roles and commands") in a REAL browser (Chromium through Playwright), on the real front page
// (web/lobby.html with web/home/) against the FAKE hub (tests/scripts/fake_hub.js, which shares the page's address as nginx's /hub/ws does) and a stand-in for the two calls of the admin page that the builder makes
// (GET /admin/api/bot-options and GET /admin/api/bot-matches/summary). It looks at what no fake DOM can: at 320, 390 and 1280 px, that the helper (the popover over the field), the /bots builder (a sheet from the bottom on a
// phone) and the admin page's list of maps over it lie wholly inside the window, that nothing scrolls sideways, that every target is 44 px, that a private block is drawn under the line that was typed, and that the files
// of the helper are asked for by an account that has commands and by nobody else (a guest, a reader).
//
//   node tests/scripts/web_home_cmds_browser.js [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). --shots DIR saves a picture of each state as cmds_<scene>_<size>.png in DIR.
// Exit status: 0 every check passed (or the check could not be made), 1 a check failed or the page broke.
'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');
const { startHub, line } = require('./fake_hub.js');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the commands of the chat were NOT run in a browser'); process.exit(0); }

const WEB = path.resolve(__dirname, '..', '..', 'web');
const shotsAt = process.argv.indexOf('--shots') !== -1 ? process.argv[process.argv.indexOf('--shots') + 1] : null;
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json' };
const SIZES = { small: [320, 640], phone: [390, 844], desktop: [1280, 800] };
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const map = (file, name, o) => Object.assign({ file, name, group: 'original', width: 31, height: 31, players: 4, minutes: 15, playable: true, places: 1, random: true, checked: true }, o);
const MAPS = [map('TINY.LVL', 'TINY'), map('SMALL.LVL', 'SMALL', { width: 45, height: 45 }), map('ISLANDS.LVL', 'ISLANDS'), map('Bombz Away.lvl', 'Bombz Away', { group: 'players' }), map('Evil.lvl', '<img src=x onerror=alert(1)> evil', { group: 'library' })];

function serve() {                                       // the front page and its files as the site's nginx gives them, the two admin calls of the builder, and nothing else
    const requests = [];
    const server = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        requests.push(url.pathname);
        const json = (o) => { res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(o)); };
        if (url.pathname === '/admin/api/bot-options') return json({ maps: MAPS, random_max_minutes: 30, checking: 0 });
        if (url.pathname === '/admin/api/bot-matches/summary') return json({ at_once: 80, running: 0, waiting: 0, busy: 0, queues: [] });
        const file = url.pathname === '/' ? 'lobby.html' : url.pathname.replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'application/json' }); res.end('{"error":"not here"}'); return; }
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
        res.end(fs.readFileSync(full));
    });
    return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({ server, requests, url: 'http://127.0.0.1:' + server.address().port })));
}

const HISTORY = [line(1, 'pebble', 'anyone up for Islands? two seats left'), line(2, 'clover', 'I have a room open, Treasure, one bot'), line(3, 'juniper', 'joining Clover!')];
const YOU = { admin: { name: 'basil', mod: true, role: 'admin', k: 'aaaaaaaaaaaa' }, mod: { name: 'sage', mod: true, role: 'mod', k: 'bbbbbbbbbbbb' }, player: { name: 'maple.leaf', mod: false, role: 'none', k: 'cccccccccccc' } };

async function main() {
    const site = await serve();
    const hub = await startHub({ server: site.server, login: true, you: YOU.admin, history: HISTORY, online: 9 });
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the commands of the chat were NOT run in a browser'); await hub.close(); site.server.close(); process.exit(0); }
    const errors = [];
    const shot = async (page, scene, size) => { if (shotsAt) { fs.mkdirSync(shotsAt, { recursive: true }); await page.screenshot({ path: path.join(shotsAt, 'cmds_' + scene + '_' + size + '.png') }); } };
    const rect = (page, selector) => page.evaluate((s) => { const e = document.querySelector(s); if (!e || !e.getClientRects().length) return null; const r = e.getBoundingClientRect(); return { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height), right: Math.round(r.right), bottom: Math.round(r.bottom) }; }, selector);
    const inside = (r, W, H) => !!r && r.x >= 0 && r.y >= 0 && r.right <= W && r.bottom <= H;
    const noSideways = (page) => page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth && document.body.scrollWidth <= window.innerWidth);
    const targets = (page, scope) => page.evaluate((s) => [...document.querySelectorAll(s + ' button, ' + s + ' input, ' + s + ' select')].filter((e) => e.getClientRects().length).map((e) => { const r = e.getBoundingClientRect(); return { name: (e.className || e.tagName) + ' ' + Math.round(r.width) + 'x' + Math.round(r.height), w: r.width, h: r.height }; }), scope);
    const small = (list) => list.filter((t) => t.h < 44 || t.w < 44).map((t) => t.name);
    async function open(role, size) {
        hub.set({ login: role !== null, you: role ? YOU[role] : null });
        hub.received.length = 0;
        const [W, H] = SIZES[size], mobile = W < 701;
        const context = await browser.newContext({ viewport: { width: W, height: H }, deviceScaleFactor: 1, isMobile: mobile, hasTouch: mobile, colorScheme: 'dark' });
        const page = await context.newPage();
        page.on('pageerror', (e) => errors.push(size + ' ' + role + ': ' + String(e && e.message || e)));
        await page.goto(site.url + '/', { waitUntil: 'load' });
        await page.waitForSelector('#gchat', { state: 'attached', timeout: 8000 });
        if (mobile) await page.click('.htabs button:has-text("Chat")');
        await page.waitForFunction(() => { const f = document.querySelector('#gchat .gc-form'); return !!f && !f.hidden; }, null, { timeout: 8000 }).catch(() => {});
        await sleep(400);
        return { page, context, W, H };
    }
    const field = '#gchat .gc-input';

    // ---- an administrator, at the three sizes ----
    for (const size of Object.keys(SIZES)) {
        site.requests.length = 0;
        const { page, context, W, H } = await open('admin', size);
        check(size + ': the page asks for the helper\'s two files after the hub sent the account its commands, and for nothing else of it', await page.waitForFunction(() => !!window.AntsHomeCmds, null, { timeout: 5000 }).then(() => true, () => false) && site.requests.indexOf('/home/cmds.js') !== -1 && site.requests.indexOf('/home/cmds.css') !== -1
              && site.requests.indexOf('/admin_map_pick.js') === -1 && site.requests.indexOf('/admin.css') === -1);
        await page.click(field);
        await page.keyboard.type('/');
        await sleep(150);
        const pop = await rect(page, '.gh-pop');
        check(size + ': a slash opens the helper, and it lies wholly inside the window (' + JSON.stringify(pop) + ')', inside(pop, W, H));
        const rows = await page.evaluate(() => [...document.querySelectorAll('.gh-row')].map((r) => r.querySelector('.gh-w').textContent));
        check(size + ': an administrator\'s list holds the commands of all three groups, /bots among them', rows.length === 14 && rows.indexOf('/bots') !== -1 && rows.indexOf('/help') === 0, rows.join());
        check(size + ': every row of the helper is at least 44 px high, and nothing scrolls sideways', (await page.evaluate(() => [...document.querySelectorAll('.gh-row')].every((r) => r.getBoundingClientRect().height >= 44))) && await noSideways(page));
        await shot(page, 'helper', size);
        await page.keyboard.press('Escape');
        check(size + ': Escape closes the helper and leaves the dock open', (await rect(page, '.gh-pop')) === null && (await page.getAttribute('#gchat', 'data-open')) === 'true');
        await page.fill(field, '');
        await page.keyboard.type('/bo');
        await sleep(100);
        check(size + ': typing narrows the list to /bots, whose usage shows', (await page.evaluate(() => [...document.querySelectorAll('.gh-row')].map((r) => r.querySelector('.gh-w').textContent))).join() === '/bots' && /COUNT/.test(await page.evaluate(() => document.querySelector('.gh-a').textContent)));
        await shot(page, 'helper_bo', size);
        await page.keyboard.press('Enter');
        await sleep(200);
        const box = await rect(page, '.gb');
        check(size + ': Enter on /bots opens the builder (nothing is sent), inside the window (' + JSON.stringify(box) + ')', inside(box, W, H) && (await page.inputValue(field)) === '/bots ' && hub.received.filter((m) => m.t === 'say').length === 0);
        check(size + ': every control of the builder is at least 44 px high and wide, and nothing scrolls sideways', small(await targets(page, '.gb')).length === 0 && await noSideways(page), small(await targets(page, '.gb')).join());
        await shot(page, 'builder', size);
        await page.fill('#gb-n', '1000');
        await page.click('.gb-seg button:has-text("187")');
        await page.click('.gb-presets button:has-text("100")');
        await page.selectOption('#gb-levels', 'hard');
        await sleep(100);
        check(size + ': the plain line and the command follow the choices', /1,000|100 /.test(await page.innerText('.gb-plan')) && (await page.innerText('.gb-cmd')) === '/bots 100 rules 187 levels hard', await page.innerText('.gb-cmd'));
        await page.click('.gb-map');
        await page.waitForSelector('.dlg.mp', { timeout: 5000 });
        await sleep(200);
        const mp = await rect(page, '.dlg.mp');
        check(size + ': Map opens the admin page\'s list of maps over the builder, inside the window, with the page\'s two files asked for now (' + JSON.stringify(mp) + ')', inside(mp, W, H) && site.requests.indexOf('/admin_map_pick.js') !== -1 && site.requests.indexOf('/admin.css') !== -1);
        check(size + ': a map whose name is markup is shown as text', (await page.evaluate(() => document.querySelectorAll('.dlg.mp img').length)) === 0);
        await shot(page, 'picker', size);
        const before = errors.length;
        await page.keyboard.press('Tab');
        await page.keyboard.press('Tab');
        await sleep(100);
        check(size + ': Tab goes round inside the list of maps (the keys are heard on the page: a key that throws is a trap that does not close)', errors.length === before && (await page.evaluate(() => !!document.activeElement && !!document.activeElement.closest('.dlg.mp'))), errors.slice(before).join(' | '));
        await page.fill('.mp-q', 'bombz');
        await page.click('.mrow:has-text("Bombz")');
        await sleep(200);
        check(size + ': a map that is chosen is the builder\'s map, and the list is gone', (await rect(page, '.dlg.mp')) === null && /Bombz/.test(await page.innerText('.gb-map')) && (await page.innerText('.gb-cmd')) === '/bots 100 rules 187 levels hard map Bombz Away.lvl');
        await page.click('.gb-btns .btn:has-text("Write")');
        await sleep(150);
        check(size + ': Write the command closes the builder and puts the command in the field (nothing is sent)', (await rect(page, '.gb')) === null && (await page.inputValue(field)) === '/bots 100 rules 187 levels hard map Bombz Away.lvl' && hub.received.filter((m) => m.t === 'say').length === 0);
        await shot(page, 'written', size);
        await page.keyboard.press('Enter');
        await page.waitForFunction(() => document.querySelector('#gchat .gc-private'), null, { timeout: 4000 }).catch(() => {});
        await sleep(300);
        const text = await page.evaluate(() => { const p = [...document.querySelectorAll('#gchat .gc-private')].pop(); return p ? p.innerText : ''; });
        check(size + ': Enter sends the command and the answer is a private block under the line that was typed (' + text.replace(/\n/g, ' | ') + ')', /\/bots 100 rules 187/.test(text) && /Started 100 matches/.test(text) && hub.received.filter((m) => m.t === 'say').length === 1);
        check(size + ': nothing scrolls sideways with the answer on the screen, and the dock is inside the window', await noSideways(page) && inside(await rect(page, '#gchat'), W, H));
        await shot(page, 'reply', size);
        hub.received.length = 0;
        await page.fill(field, '/zzz');
        await page.keyboard.press('Enter');
        await sleep(400);
        check(size + ': a word that is no command is answered privately, and posted to nobody', /That command is not one that you have/.test(await page.evaluate(() => [...document.querySelectorAll('#gchat .gc-private')].pop().innerText)) && (await page.evaluate(() => document.body.innerText.indexOf('/zzz')) >= 0));
        await shot(page, 'unknown', size);
        hub.set({ botsReply: (text) => (text === '/bots status' ? { t: 'reply', c: 'bots', r: 'status', at_once: 80, running: 80, waiting: 648, busy: 31, queues: [{ total: 1000, remaining: 648, finished: 312, running: 80, kept: 30, every_s: 0, next_in: -1, held: '', by: 'basil' }, { total: 5, remaining: 4, finished: 0, running: 1, kept: 0, every_s: 180, next_in: 100, held: 'cap', by: '<img src=x onerror=alert(1)>' }] } : null) });
        await page.fill(field, '/bots status');
        await page.keyboard.press('Enter');
        await sleep(500);
        const card = await page.evaluate(() => { const p = [...document.querySelectorAll('#gchat .gc-status')].pop(); return p ? { text: p.innerText, imgs: p.querySelectorAll('img').length, bars: p.querySelectorAll('.gc-bar').length } : null; });
        check(size + ': the status of two batches is one private block: a bar and the numbers for each, and a name that is markup stays text (' + JSON.stringify(card && card.text.replace(/\n/g, ' | ')) + ')', !!card && card.bars === 2 && card.imgs === 0 && /Batch of 1,000 matches/.test(card.text) && /312 done|312\s+done/.test(card.text) && /<img src=x onerror=alert\(1\)>/.test(card.text));
        check(size + ': nothing scrolls sideways with the card on the screen, and the dock is inside the window', await noSideways(page) && inside(await rect(page, '#gchat'), W, H));
        await shot(page, 'status', size);
        hub.set({ botsReply: null });
        await context.close();
    }

    // ---- a player: the three commands of everybody and no /bots; a guest: nothing is asked for ----
    for (const size of ['small', 'desktop']) {
        site.requests.length = 0;
        const p = await open('player', size);
        await p.page.click(field);
        await p.page.keyboard.type('/');
        await sleep(200);
        const rows = await p.page.evaluate(() => [...document.querySelectorAll('.gh-row')].map((r) => r.querySelector('.gh-w').textContent));
        check(size + ': a player\'s helper holds the commands of everybody and none of the staff\'s', rows.join() === '/help,/ignore,/unignore', rows.join());
        await shot(p.page, 'player', size);
        await p.context.close();
        site.requests.length = 0;
        const g = await open(null, size);
        await sleep(500);
        check(size + ': a guest downloads nothing of it, and a slash in the page does nothing', site.requests.filter((u) => /cmds|admin/.test(u)).length === 0 && (await g.page.evaluate(() => typeof window.AntsHomeCmds)) === 'undefined');
        await shot(g.page, 'guest', size);
        await g.context.close();
    }
    check('the page threw nothing', errors.length === 0, errors.join(' | '));
    await browser.close();
    await hub.close();
    site.server.close();
    console.log('commands of the chat in a browser: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}
main().catch((e) => { console.error(e); process.exit(1); });
