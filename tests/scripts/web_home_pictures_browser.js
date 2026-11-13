// OPT-IN, not part of ./run_tests.sh: the pictures in the round circles of Home in a REAL browser (Chromium through Playwright), against the fake hub of tests/scripts/fake_hub.js behind a small stand-in for the
// site that serves the files of web/ and the pictures at /hub/pic/<name>.png (test pictures drawn here, each with a mode: ok, slow, 404, 429). What no fake DOM can show (docs/HOME_SCREEN.md, "The login",
// "The players list"):
//   1. nothing moves when a picture loads: the rows of the list, the list and the account chip have the same boxes before and after (the pictures are held back, then let through), and the page reports no layout shift;
//   2. the ring: the circle's own border (red for an administrator, yellow for a moderator, 2 px) is still there around a picture, the picture is inside it, and the status dot is on top of the picture;
//   3. the letter: shown while the picture is on its way, hidden once it has loaded; a picture that answers 404 or 429, a request that is blocked, are the letter exactly as without a picture (no broken image, same boxes);
//   4. the list is drawn again when one row changes: no circle that shows a picture shows its letter, or nothing, for even one frame;
//   5. the switch by keyboard: Tab / Shift+Tab reach the box, Space presses it, the hub is told, the box waits and then follows the hub; a refusal and silence put it back; the row is 46 px tall or more;
//   6. phone width: the chip is the circle alone and its picture is the chip; the Players view shows the pictures.
//   --shots DIR takes the pictures of the built page (device scale factor 2), with a README.md table, for people to look at beside the approved mock-ups; --mockups DIR names the folder of those (the
//   side-by-side files are made when both are given).
//
//   node tests/scripts/web_home_pictures_browser.js [--web DIR] [--shots DIR] [--mockups DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). All names and pictures here are made up. Exit status: 0 every check passed (or none could be
// made), 1 a check failed or the page broke.
'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');
const zlib = require('zlib');
const crypto = require('crypto');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the pictures were NOT run in a real browser'); process.exit(0); }
const ROOT = path.resolve(__dirname, '..', '..');
const arg = (name) => { const i = process.argv.indexOf(name); return i === -1 ? null : process.argv[i + 1]; };
const WEB = path.resolve(arg('--web') || path.join(ROOT, 'web'));
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const MOCKUPS = arg('--mockups') ? path.resolve(arg('--mockups')) : null;
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml' };
const { startHub, line } = require('./fake_hub.js');
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// ---- made-up pictures: a small PNG of one color with a lighter band and a dark square in the middle ---------------------------------------------------------------------------------------------------------
const CRC = (() => { const t = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
const crc32 = (buf) => { let c = 0xffffffff; for (const b of buf) c = CRC[(c ^ b) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
function chunk(type, data) { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const body = Buffer.concat([Buffer.from(type), data]); const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(body)); return Buffer.concat([len, body, crc]); }
function drawn(size, rgb, shape) {
    const raw = Buffer.alloc((size * 3 + 1) * size);
    for (let y = 0; y < size; y++) {
        raw[y * (size * 3 + 1)] = 0;
        for (let x = 0; x < size; x++) {
            let c = rgb;
            if (Math.abs(x - y) < size / 8) c = rgb.map((v) => Math.min(255, v + 70));
            const mid = Math.abs(x - size / 2) < size / 5 && Math.abs(y - size / 2) < size / 5;
            if (mid && shape === 0) c = [20, 24, 40];
            if (shape === 1 && ((x - size / 2) ** 2 + (y - size / 2) ** 2) < (size / 4) ** 2) c = [250, 240, 200];
            raw.set(c, y * (size * 3 + 1) + 1 + x * 3);
        }
    }
    const head = Buffer.alloc(13); head.writeUInt32BE(size, 0); head.writeUInt32BE(size, 4); head[8] = 8; head[9] = 2;
    return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', head), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}
const hex = (word) => crypto.createHash('md5').update('test-picture-' + word).digest('hex');       // (a made-up name of 32 lower-case hex characters)
const N = { me: hex('me'), ada: hex('ada'), moss: hex('moss'), juniper: hex('juniper'), pebble: hex('pebble'), gone: hex('gone'), busy: hex('busy'), blocked: hex('blocked'), slow: hex('slow'), fern: hex('fern') };
const BODY = { me: drawn(64, [210, 90, 60], 1), ada: drawn(64, [60, 130, 200], 0), moss: drawn(64, [90, 170, 90], 1), juniper: drawn(64, [170, 90, 190], 0), pebble: drawn(64, [200, 170, 60], 1), slow: drawn(64, [80, 160, 160], 0), fern: drawn(64, [160, 60, 100], 0) };
const REVERSE = {}; Object.keys(N).forEach((k) => { REVERSE[N[k]] = k; });

const LOBBIES = { format: 1, count: 2, lobbies: [
    { code: 'c1v3rr', host: 'Clover', map: 'TREASURE.LVL', mode: 'highest-score', people: 2, bots: 1, free: 1, size: 4, age: 200 },
    { code: 'p4n8dz', host: 'Pebble', map: 'ISLANDS.LVL', mode: 'highest-score', people: 3, bots: 0, free: 1, size: 4, age: 312 }] };
function serve() {                                       // the front page and its files, and the pictures with their modes; nothing else
    const state = { requests: [], mode: { gone: '404', busy: '429' }, gate: null, hold: new Set() };
    const server = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        state.requests.push(url.pathname);
        if (url.pathname === '/lobbies') { res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(LOBBIES)); return; }
        if (url.pathname === '/stats' || url.pathname === '/busy') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end('{}'); return; }
        const pic = /^\/hub\/pic\/([0-9a-f]{32})\.png$/.exec(url.pathname);
        if (pic) {
            const who = REVERSE[pic[1]];
            const done = () => {
                const mode = state.mode[who];
                if (mode === '404' || !BODY[who] && mode !== '429') { res.writeHead(404, { 'Content-Type': 'text/plain' }); res.end('no'); return; }
                if (mode === '429') { res.writeHead(429, { 'Content-Type': 'text/plain', 'Retry-After': '30' }); res.end('slow down'); return; }
                res.writeHead(200, { 'Content-Type': 'image/png', 'Cache-Control': 'no-store' });
                res.end(BODY[who]);
            };
            if (state.gate && (state.hold.size === 0 || state.hold.has(who))) state.gate.then(done); else done();
            return;
        }
        const file = url.pathname === '/' ? 'lobby.html' : url.pathname.replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'application/json' }); res.end('{"error":"not here"}'); return; }
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
        res.end(fs.readFileSync(full));
    });
    state.server = server;
    state.close = () => new Promise((resolve) => server.close(() => resolve()));
    state.hold_ = (names) => { let open; state.gate = new Promise((r) => { open = r; }); state.hold = new Set(names || []); state.release = () => { state.gate = null; open(); }; };
    return new Promise((resolve) => server.listen(0, '127.0.0.1', () => { state.url = 'http://127.0.0.1:' + server.address().port; resolve(state); }));
}

const ME = { name: 'maple.leaf', mod: false, role: 'none', pics: 'on', pic: N.me };
const row = (i, name, at, more) => Object.assign({ i, name, at: at || 'look' }, more || {});
const ROWS = [row(2, 'ada', 'look', { mod: true, role: 'admin', pic: N.ada }), row(3, 'moss', 'look', { mod: true, role: 'mod', pic: N.moss }), row(4, 'tess', 'look', { mod: true, role: 'mod' }),
    row(5, 'juniper', 'look', { pic: N.juniper }), row(6, 'pebble', 'wait', { pic: N.pebble }), row(7, 'clover', 'play')];
const WHO = (rows) => ({ t: 'who', full: true, n: rows.length + 2, g: 2, more: 0, rows });
const HISTORY = [line(1, 'pebble', 'anyone up for Islands?'), line(2, 'clover', 'I have a room open'), line(3, 'juniper', 'joining!')];
const R = (selector) => (page) => page.evaluate((s) => [...document.querySelectorAll(s)].map((e) => { const r = e.getBoundingClientRect(); return [Math.round(r.left * 100) / 100, Math.round(r.top * 100) / 100, Math.round(r.width * 100) / 100, Math.round(r.height * 100) / 100]; }), selector);
const boxes = async (page) => ({ cards: await R('#players .pl-card')(page), list: await R('#players')(page), cards2: await R('#players .pl-av')(page), chip: await R('.hbar .hlogin, .hbar .gc-av')(page), top: await R('.hbar')(page) });
const shift = (a, b) => { let worst = 0; for (const k of Object.keys(a)) { if (a[k].length !== b[k].length) return Infinity; a[k].forEach((r, i) => r.forEach((v, j) => { worst = Math.max(worst, Math.abs(v - b[k][i][j])); })); } return worst; };
const circle = (page, name) => page.evaluate((n) => { const c = [...document.querySelectorAll('#players .pl-card')].find((x) => x.querySelector('.pl-nm').textContent === n); if (!c) return null; const av = c.querySelector('.pl-av'), s = getComputedStyle(av), i = av.querySelector('img'), r = av.getBoundingClientRect(), ir = i && i.getBoundingClientRect(); return { img: !!i, loaded: av.getAttribute('data-pic') === 'on', color: s.color, border: s.borderTopColor, bw: s.borderTopWidth, w: r.width, h: r.height, inset: ir ? [Math.round((ir.left - r.left) * 10) / 10, Math.round((ir.top - r.top) * 10) / 10, Math.round((r.right - ir.right) * 10) / 10] : null, radius: i ? getComputedStyle(i).borderTopLeftRadius : null, cx: r.left, cy: r.top }; }, name);
const letterShown = (c) => c && c.color !== 'rgba(0, 0, 0, 0)' && c.color !== 'transparent';
const loadedAll = (page, n) => page.waitForFunction((k) => document.querySelectorAll('#players .pl-av[data-pic="on"]').length >= k, n, { timeout: 8000 }).then(() => true, () => false);
const settled = (page) => page.evaluate(() => new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(resolve, 0)))));

async function main() {
    const site = await serve();
    const hub = await startHub({ server: site.server, login: true, you: ME, history: HISTORY, online: 9, welcomeExtra: { presence: true, me: 1 }, who: WHO(ROWS) });
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the pictures were NOT run in a browser'); await hub.close(); await site.close(); process.exit(0); }
    const errors = [];
    let shotsList = [];
    const shot = async (page, name, what, o) => { if (!SHOTS) return; fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot(Object.assign({ path: path.join(SHOTS, name) }, o || {})); shotsList.push([name, what]); };
    async function open(width, height, o) {
        o = o || {};
        const context = await browser.newContext({ viewport: { width, height }, deviceScaleFactor: SHOTS ? 2 : 1 });
        const page = await context.newPage();
        await page.addInitScript(() => { window.__cls = 0; try { new PerformanceObserver((l) => { l.getEntries().forEach((e) => { if (!e.hadRecentInput) window.__cls += e.value; }); }).observe({ type: 'layout-shift', buffered: true }); } catch (e) { /* no observer */ } });
        page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
        if (o.block) await page.route('**/hub/pic/' + o.block + '.png', (r) => r.abort());
        await page.goto(site.url + '/', { waitUntil: 'load' });
        await page.waitForSelector('#gchat', { timeout: 5000 }).catch(() => {});
        await page.waitForSelector('#players .pl-card', { timeout: 8000 }).catch(() => {});
        return { page, context };
    }
    const showPlayers = async (page) => { if (await page.evaluate(() => innerWidth <= 700)) { await page.click('.htab.t-players'); await sleep(150); } };
    const openMenu = async (page) => { await page.click('.hbar .hlogin'); await page.waitForSelector('#gchat-login-slot:not([hidden])', { timeout: 3000 }).catch(() => {}); await settled(page); };

    // ---- 1 to 4: desktop, the pictures held back and then let through -----------------------------------------------------------------------------------------------------------------------------------------------
    {
        hub.set({ you: ME, who: WHO(ROWS) });
        site.hold_(['me', 'ada', 'moss', 'juniper', 'pebble']);
        const { page, context } = await open(1280, 800);
        await sleep(300);
        const before = await boxes(page), cls0 = await page.evaluate(() => window.__cls);
        const ada0 = await circle(page, 'ada');
        check('while the pictures are on their way every circle is its letter and nothing is hidden', ada0 && ada0.img === true && !ada0.loaded && letterShown(ada0) && letterShown(await circle(page, 'tess')), JSON.stringify(ada0));
        check('the chip shows the letter too while its picture is on the way', await page.evaluate(() => { const a = document.querySelector('.hbar .gc-av'); return !!a && getComputedStyle(a).color !== 'rgba(0, 0, 0, 0)' && !a.getAttribute('data-pic'); }));
        await shot(page, 'built_desktop_letters_first.png', 'the front page while the pictures are still on their way: the letters');
        site.release();
        check('the pictures arrive: five circles (four rows and the visitor\'s own card) hide their letters', await loadedAll(page, 5));
        await settled(page);
        const after = await boxes(page);
        const moved = shift(before, after);
        const cls = (await page.evaluate(() => window.__cls)) - cls0;
        console.log('layout shift of the rows, the list and the chip when the pictures load: ' + moved + ' px (layout-shift score ' + cls + ')');
        check('nothing moves when the pictures load: every row, the list, the circles and the chip have the same boxes before and after', moved === 0, String(moved));
        check('the page reports no layout shift from the pictures', cls === 0, String(cls));
        const ada = await circle(page, 'ada'), moss = await circle(page, 'moss'), tess = await circle(page, 'tess'), juni = await circle(page, 'juniper');
        check('the ring stays: an administrator\'s circle is red and 2 px wide with its picture, a moderator\'s is yellow, the picture is inside it (inset 2 px) and round', ada.border === 'rgb(255, 95, 102)' && ada.bw === '2px' && JSON.stringify(ada.inset) === '[2,2,2]' && ada.radius === '50%' && moss.bw === '2px' && moss.border !== juni.border && JSON.stringify(moss.inset) === '[2,2,2]', JSON.stringify([ada, moss]));
        check('a person with a picture has the letter hidden and the circle is still 32 x 32; one with none keeps the letter', ada.w === 32 && ada.h === 32 && !letterShown(ada) && letterShown(tess) && !tess.img, JSON.stringify([ada, tess]));
        const dot = await page.evaluate((c) => { const e = document.elementFromPoint(c.cx + 25, c.cy + 25); return e ? e.tagName.toLowerCase() : null; }, await circle(page, 'ada'));
        check('the status dot is on top of the picture (the point on the dot is the circle, not the picture)', dot !== 'img', String(dot));
        await shot(page, 'built_players_desktop.png', 'the front page: the Players list with an administrator, a moderator and four players, pictures in the circles');
        // 4. a change to one row draws the list again: no circle with a picture shows its letter, or nothing, even for one frame
        await page.evaluate(() => {
            window.__flicker = []; window.__watch = true;
            const names = ['ada', 'moss', 'juniper', 'pebble'];
            const tick = () => {
                if (!window.__watch) return;
                names.forEach((n) => {
                    const c = [...document.querySelectorAll('#players .pl-card')].find((x) => x.querySelector('.pl-nm').textContent === n), av = c && c.querySelector('.pl-av');
                    if (!av) { window.__flicker.push(n + ': no circle'); return; }
                    if (getComputedStyle(av).color !== 'rgba(0, 0, 0, 0)') window.__flicker.push(n + ': the letter shows');
                });
                requestAnimationFrame(tick);
            };
            requestAnimationFrame(tick);
        });
        for (let i = 0; i < 4; i++) {
            hub.broadcast({ t: 'who', full: false, n: 8, g: 2, more: 0, rows: [row(7, 'clover', i % 2 ? 'play' : 'look')], gone: [] });
            await sleep(450);
        }
        await sleep(300);
        const flicker = await page.evaluate(() => { window.__watch = false; return window.__flicker; });
        console.log('frames in which a circle that had a picture showed its letter while a row changed: ' + flicker.length);
        check('the list is drawn again four times when one row changes: no circle with a picture shows its letter or goes missing for even one frame', flicker.length === 0, flicker.slice(0, 3).join('; '));
        check('... and the pictures are asked for from the page, not again and again (no more than a few requests each)', site.requests.filter((u) => u === '/hub/pic/' + N.ada + '.png').length <= 6, String(site.requests.filter((u) => u === '/hub/pic/' + N.ada + '.png').length));
        await context.close();
    }
    if (SHOTS) {                                         // the same list, tall enough that all of it shows (the real window scrolls the list inside its panel)
        hub.set({ you: ME, who: WHO(ROWS) }); site.hold_([]); site.release();
        const t = await open(1280, 1000);
        await loadedAll(t.page, 5); await settled(t.page);
        await shot(t.page, 'built_players_desktop_tall.png', 'the same page 1000 px high, so the whole list shows');
        await t.context.close();
        const u = await open(390, 1100);
        await showPlayers(u.page); await loadedAll(u.page, 5); await settled(u.page);
        await shot(u.page, 'built_players_phone_tall.png', 'a phone, 1100 px high so the whole Players view shows');
        await u.context.close();
    }

    // ---- 3. the fallbacks: a 404, a 429, a blocked request, a slow answer -------------------------------------------------------------------------------------------------------------------------------------------
    {
        const fallbacks = [row(2, 'ada', 'look', { mod: true, role: 'admin', pic: N.ada }), row(3, 'gone', 'look', { pic: N.gone }), row(4, 'busy', 'look', { pic: N.busy }), row(5, 'blocked', 'look', { pic: N.blocked }), row(6, 'slow', 'look', { pic: N.slow }), row(7, 'plain', 'look')];
        hub.set({ you: Object.assign({}, ME, { pic: N.gone }), who: WHO(fallbacks) });
        site.hold_(['slow']);
        const { page, context } = await open(1280, 800, { block: N.blocked });
        await sleep(700);
        const beforeBoxes = await boxes(page);
        const c = {}; for (const n of ['gone', 'busy', 'blocked', 'slow', 'plain', 'ada']) c[n] = await circle(page, n);
        check('a picture that answers 404, one that answers 429 and one whose request is blocked are the letter exactly as without a picture (no img left, letter shown, same 32 x 32 circle)', ['gone', 'busy', 'blocked'].every((n) => c[n] && !c[n].img && !c[n].loaded && letterShown(c[n]) && c[n].w === 32 && c[n].h === 32), JSON.stringify(c));
        check('a slow answer: the letter shows while it waits, and the picture is there when it comes', c.slow.img && !c.slow.loaded && letterShown(c.slow));
        check('the chip with a picture that answers 404 is the letter, no broken image', await page.evaluate(() => { const a = document.querySelector('.hbar .gc-av'); return !!a && !a.querySelector('img') && !a.getAttribute('data-pic') && getComputedStyle(a).color !== 'rgba(0, 0, 0, 0)'; }));
        await shot(page, 'built_desktop_failed_picture.png', 'pictures that fail (404, too many requests, blocked): the letters show, as for people with no picture; the slow one is still on its way');
        site.release();
        await sleep(500);
        const slow = await circle(page, 'slow');
        check('... the slow one arrives and hides its letter', slow.loaded && !letterShown(slow), JSON.stringify(slow));
        await settled(page);
        const afterBoxes = await boxes(page);
        check('none of the four paths moved a row or the list', shift(beforeBoxes, afterBoxes) === 0, String(shift(beforeBoxes, afterBoxes)));
        check('an img that failed is not asked for again by the page (404 and 429: once each, from the list; the chip asks once more for the one name it shares)', site.requests.filter((u) => u === '/hub/pic/' + N.busy + '.png').length <= 2, String(site.requests.filter((u) => u === '/hub/pic/' + N.busy + '.png').length));
        await context.close();
    }

    // ---- 5. the switch by keyboard, and its four states ------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        hub.set({ you: Object.assign({}, ME, { pics: 'off' }), who: WHO(ROWS), picsMode: undefined });
        delete hub.options.you.pic;
        site.hold_([]); site.release();
        const { page, context } = await open(1280, 800);
        await page.focus('.hbar .hlogin');
        await page.keyboard.press('Enter');
        await page.waitForSelector('#gchat-login-slot:not([hidden])', { timeout: 3000 });
        await settled(page);
        check('the chip opens the dropdown from the keyboard and the focus is in it', await page.evaluate(() => document.getElementById('gchat-login-slot').contains(document.activeElement)));
        await page.keyboard.press('Shift+Tab');
        const onBox = await page.evaluate(() => { const a = document.activeElement; return { box: a.classList.contains('gc-swb'), type: a.type, visible: a.matches(':focus-visible') }; });
        check('Shift+Tab from Log out reaches the checkbox, and it shows the focus', onBox.box && onBox.type === 'checkbox' && onBox.visible, JSON.stringify(onBox));
        const tall = await page.evaluate(() => document.querySelector('.gc-swl').getBoundingClientRect().height);
        check('the row of the box is 46 px tall or more', tall >= 46, String(tall));
        await shot(page, 'built_menu_off_desktop.png', 'the account menu, switch off (the box is not checked)', { clip: { x: 760, y: 0, width: 520, height: 520 } });
        await page.keyboard.press('Space');
        await sleep(300);
        const got = hub.received.filter((m) => m.t === 'pics');
        check('Space presses it: the hub is told {"t":"pics","on":true} once', JSON.stringify(got) === JSON.stringify([{ t: 'pics', on: true }]), JSON.stringify(got));
        check('the hub answers with the whole `you`: the box is on and enabled, and still has the focus', await page.evaluate(() => { const b = document.querySelector('.gc-swb'); return b.checked && !b.disabled && document.activeElement === b; }));
        check('the dropdown says that the picture is loaded when the person logs in (the hub sent none yet), with a Log in again button', await page.evaluate(() => document.querySelector('.gc-swn').textContent === 'Your picture is loaded when you log in.' && document.querySelector('.gc-again').getClientRects().length > 0));
        await shot(page, 'built_menu_none_desktop.png', 'the account menu, switch on and no picture yet (Log in again)', { clip: { x: 760, y: 0, width: 520, height: 520 } });
        hub.broadcast({ t: 'you', you: Object.assign({}, ME, { pics: 'on' }) });
        await sleep(250);
        await shot(page, 'built_menu_on_desktop.png', 'the account menu, switch on with a picture', { clip: { x: 760, y: 0, width: 520, height: 520 } });
        check('a `you` with a picture draws it on the chip at once', await page.evaluate(() => !!document.querySelector('.hbar .gc-av img')));
        // a refusal, and silence
        hub.set({ picsMode: 'refuse' });
        await page.keyboard.press('Space');
        await sleep(300);
        check('a refusal puts the box back (checked, enabled) and says "That did not work, try again"', await page.evaluate(() => { const b = document.querySelector('.gc-swb'); return b.checked && !b.disabled && document.querySelector('.gc-swn').textContent === 'That did not work, try again'; }));
        hub.set({ picsMode: 'silent' });
        await page.keyboard.press('Space');
        await sleep(300);
        check('while the hub is silent the box waits (disabled)', await page.evaluate(() => document.querySelector('.gc-swb').disabled));
        await sleep(8200);
        check('after 8 seconds of silence the box is back, enabled, with the same words', await page.evaluate(() => { const b = document.querySelector('.gc-swb'); return b.checked && !b.disabled && document.querySelector('.gc-swn').textContent === 'That did not work, try again'; }));
        hub.set({ picsMode: undefined });
        hub.broadcast({ t: 'you', you: Object.assign({}, ME, { pics: 'blocked' }) });
        await sleep(250);
        check('blocked: the box is off and disabled, and the line says it is hidden on this site', await page.evaluate(() => { const b = document.querySelector('.gc-swb'); return !b.checked && b.disabled && document.querySelector('.gc-swn').textContent === 'Your picture is hidden on this site.'; }));
        await shot(page, 'built_menu_blocked_desktop.png', 'the account menu, blocked (the box is off and cannot be pressed)', { clip: { x: 760, y: 0, width: 520, height: 520 } });
        await context.close();
    }

    // ---- 6. phone ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        hub.set({ you: ME, who: WHO(ROWS) });
        site.hold_(['me', 'ada', 'moss', 'juniper', 'pebble']);
        const { page, context } = await open(390, 844);
        await sleep(300);
        await showPlayers(page);
        const before = await boxes(page);
        const chipBefore = await page.evaluate(() => { const c = document.querySelector('.hbar .hlogin'), r = c.getBoundingClientRect(); return [r.width, r.height, getComputedStyle(document.querySelector('.hbar .gc-cn')).display]; });
        site.release();
        await sleep(600);
        const after = await boxes(page);
        const chipAfter = await page.evaluate(() => { const c = document.querySelector('.hbar .hlogin'), r = c.getBoundingClientRect(); return [r.width, r.height, getComputedStyle(document.querySelector('.hbar .gc-cn')).display]; });
        check('phone: nothing moves when the pictures load (rows, list, chip), and the chip is the same size', shift(before, after) === 0 && JSON.stringify(chipBefore) === JSON.stringify(chipAfter), String(shift(before, after)) + ' ' + JSON.stringify([chipBefore, chipAfter]));
        check('phone: the Players view shows the pictures, and the visitor\'s own card has one', await page.evaluate(() => document.querySelectorAll('#players .pl-av[data-pic="on"]').length >= 5));
        check('phone: the chip\'s picture is loaded and is the whole chip circle (28 px)', await page.evaluate(() => { const a = document.querySelector('.hbar .gc-av'), r = a.getBoundingClientRect(), i = a.querySelector('img'); return a.getAttribute('data-pic') === 'on' && r.width === 28 && r.height === 28 && !!i; }));
        await shot(page, 'built_players_phone.png', 'a phone, the Players view with pictures in the circles');
        await shot(page, 'built_chip_phone.png', 'a phone: the chip with a picture (the circle alone)', { clip: { x: 0, y: 0, width: 390, height: 120 } });
        await openMenu(page);
        await shot(page, 'built_menu_on_phone.png', 'a phone: the account menu, switch on with a picture');
        for (const [state, name, what] of [['off', 'off', 'switch off'], ['none', 'none', 'switch on, no picture yet'], ['blocked', 'blocked', 'blocked']]) {
            const you = state === 'none' ? Object.assign({}, ME, { pics: 'on', pic: undefined }) : Object.assign({}, ME, { pics: state, pic: undefined });
            hub.broadcast({ t: 'you', you });
            await sleep(250);
            await shot(page, 'built_menu_' + name + '_phone.png', 'a phone: the account menu, ' + what);
        }
        await context.close();
        // the chip at desktop width: the picture, closed menu
        hub.set({ you: ME });
        const d = await open(1280, 800);
        await sleep(500);
        await shot(d.page, 'built_chip_desktop.png', 'the chip with a picture and the name, in the bar', { clip: { x: 760, y: 0, width: 520, height: 90 } });
        await d.context.close();
    }

    check('the page threw nothing', errors.length === 0, errors.join(' | '));
    if (SHOTS) {
        const table = ['# Pictures in the circles: the built page', '', 'Taken from the built pages (web/lobby.html with web/home/) in Chromium against a stand-in hub, device scale factor 2. Names and pictures are made up and drawn by the check; nothing here is a real person.', '',
            '| File | What it shows |', '|---|---|'].concat(shotsList.map(([f, w]) => '| ' + f + ' | ' + w + ' |'));
        fs.writeFileSync(path.join(SHOTS, 'README.md'), table.join('\n') + '\n');
    }
    await browser.close(); await hub.close(); await site.close();
    console.log('pictures in a browser: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
}
main().catch((e) => { console.log('FAIL ' + (e && e.stack || e)); process.exit(1); });
