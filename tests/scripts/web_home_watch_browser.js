// OPT-IN, not part of ./run_tests.sh: the games in progress of Home's Games list in a REAL browser (Chromium through Playwright), behind a small stand-in for the site's nginx (the files of web/, a fake /lobbies and a fake
// /live, no hub: the chat says it is offline). docs/HOME_SCREEN.md, "Games in progress". For every width (1280, 1180, 701, 700, 390, 320 and a phone held sideways, 568 x 320) it checks:
//   1. a busy list (open rooms and eight matches in progress, with long names, a name that is markup and a map of the players): the page does not scroll sideways, no row is wider than the list, every Watch is at least
//      44 x 44 px, inside its row and not over the row's own text, and the Games button counts the open rooms only;
//   2. Watch is a link to /play.html?live=<id> with target _blank: a press opens a NEW page on that address and the lobby page stays where it was, with its list;
//   3. a name that is markup is shown as text (no element and no script run: nothing in the page was made from it);
//   4. the switch "Show games in progress" is a checkbox that the keyboard reaches (Tab) and flips (Space), its label is a target of 44 px, off puts the rows away and stops the looks at /live (the stand-in counts them), the choice
//      is kept in this browser and holds after a reload (no look at /live at all), on brings them back;
//   5. no games in progress, a server with no live door (404) and a server that is down: no row, no error, the open rooms stay, and with no door the switch is not there; and the page's own errors are none.
//   --shots DIR takes the pictures (watch_*.png).
//
//   node tests/scripts/web_home_watch_browser.js [--web DIR] [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be made), 1 a check failed.
'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the games in progress were NOT run in a real browser'); process.exit(0); }

const ROOT = path.resolve(__dirname, '..', '..');
const arg = (name) => { const i = process.argv.indexOf(name); return i === -1 ? null : process.argv[i + 1]; };
const WEB = path.resolve(arg('--web') || path.join(ROOT, 'web'));
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml' };
const LOBBIES = { format: 1, count: 3, lobbies: [
    { code: 'c1v3rr', host: 'Clover', map: 'TREASURE.LVL', mode: 'highest-score', people: 2, bots: 1, free: 1, size: 4, age: 200 },
    { code: 'p4n8dz', host: 'Pebble', map: 'ISLANDS.LVL', mode: 'highest-score', people: 3, bots: 0, free: 1, size: 4, age: 312 },
    { code: 'f3rn22', host: 'Fern', map: 'MEDIUM.LVL', mode: 'highest-score', people: 1, bots: 0, free: 3, size: 4, age: 30 }] };
const MAPS = ['TREASURE', 'ISLANDS', 'MEDIUM', 'Pop_Corn', 'TINY', 'LARGE', 'ISLANDS', 'TREASURE'];
const NAMES = [['Green (Ann)', 'Red (Bot (Medium))'], ['Blue (Cy)', 'Black'], ['Green (<img src=x onerror=window.__hit=1>)', 'Red (' + 'W'.repeat(32) + ')', 'Blue (Maple-Long-Name-Of-A-Player)', 'Black (Bot (Hard))'], ['Green', 'Red (Dee)'],
               ['Green (Eve)', 'Blue (Fay)', 'Red (Gus)'], ['Black (Hal)', 'Green (Ida)'], ['Red (Jo)', 'Blue (Kim)'], ['Green (Lee)', 'Red (Mo)']];
const busy = () => ({ live: MAPS.map((m, i) => ({ id: m + '-20261109-1' + String(20000 + i * 37).slice(0, 5) + 'Z', map: m, started: 1790000000 + i, turns: (65 + i * 140) * 20, seconds: 65 + i * 140, players: NAMES[i] })), count: 8, sim_rules: 9 });
const SIZES = [['desktop', 1280, 800, false], ['w1180', 1180, 800, false], ['w701', 701, 800, false], ['w700', 700, 800, true], ['phone', 390, 844, true], ['w320', 320, 640, true], ['side', 568, 320, true]];

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// mode: 'busy' (eight matches), 'none' (an empty list), '404' (no live door), 'down' (the connection is cut)
function startSite(mode) {
    const stamp = (text) => text.replace(/@@SITE_TITLE@@/g, '').replace(/@@SITE_FOOTER@@/g, '').replace(/@@GAME_VERSION@@/g, '0.13.0').replace(/@@BUILD_ID@@/g, 'watch').replace(/@@BUILD_DATE@@/g, '2026-11-01 00:00').replace(/@@BUILD_TIMESTAMP@@/g, '2026-11-01 00:00:00');
    const state = { mode, live: 0, liveTimes: [] };
    const site = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        if (url.pathname === '/lobbies') { res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(LOBBIES)); return; }
        if (url.pathname === '/live') {
            state.live++; state.liveTimes.push(Date.now());
            if (state.mode === 'down') { req.socket.destroy(); return; }
            if (state.mode === '404') { res.writeHead(404, { 'Content-Type': 'application/json' }); res.end('{"error":"not found"}'); return; }
            res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
            res.end(JSON.stringify(state.mode === 'none' ? { live: [], count: 0, sim_rules: 9 } : busy()));
            return;
        }
        if (url.pathname === '/stats' || url.pathname === '/busy') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end('{}'); return; }
        const file = url.pathname === '/' ? 'lobby.html' : decodeURIComponent(url.pathname).replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'text/plain' }); res.end('not here'); return; }
        const type = TYPES[path.extname(full)] || 'application/octet-stream';
        res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' });
        res.end(type.startsWith('text/html') ? stamp(fs.readFileSync(full, 'utf8')) : fs.readFileSync(full));
    });
    site.on('upgrade', (req, socket) => socket.destroy());
    return new Promise((resolve) => site.listen(0, '127.0.0.1', () => resolve({ site, state, origin: 'http://127.0.0.1:' + site.address().port })));
}
async function shot(page, name) { if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, 'watch_' + name + '.png') }); } }
const GEOM = () => {
    const q = (e) => { const r = e.getBoundingClientRect(); return { l: r.left, t: r.top, r: r.right, b: r.bottom, w: r.width, h: r.height }; };
    const dock = document.querySelector('.gdock');
    const rows = Array.from(document.querySelectorAll('.gdock .glive')).filter((e) => e.getClientRects().length);
    return {
        page: [document.documentElement.scrollWidth, innerWidth], dock: dock && dock.getClientRects().length ? [dock.scrollWidth, dock.clientWidth] : null,
        rows: rows.map((li) => {
            const go = li.querySelector('.gwatch'), top = li.querySelector('.gtop'), line = li.querySelector('.gline'), box = q(li), g = q(go), tp = q(top), ln = q(line);
            const hit = (a, b) => a.l < b.r - 0.5 && b.l < a.r - 0.5 && a.t < b.b - 0.5 && b.t < a.b - 0.5;
            return { h: box.h, w: li.scrollWidth - li.clientWidth, go: [g.w, g.h], inside: g.l >= box.l - 0.5 && g.r <= box.r + 0.5 && g.t >= box.t - 0.5 && g.b <= box.b + 0.5, clash: hit(g, tp) || hit(g, ln), text: li.querySelector('.gwho').scrollWidth - li.querySelector('.gwho').clientWidth };
        })
    };
};

(async () => {
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the games in progress were NOT run in a real browser'); return; }
    const errors = [];
    const open = async (origin, w, h, touch) => {
        const context = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: 1, hasTouch: touch, isMobile: touch && w < 500 });
        const page = await context.newPage();
        page.on('pageerror', (e) => errors.push(w + ': ' + String(e && e.message || e)));
        await page.goto(origin + '/', { waitUntil: 'load' });
        await page.waitForSelector('.gdock .gitem', { timeout: 15000 });
        return { context, page };
    };
    try {
        {   // a busy list at every width
            const { site, state, origin } = await startSite('busy');
            for (const [name, w, h, touch] of SIZES) {
                const { context, page } = await open(origin, w, h, touch);
                await page.waitForSelector('.gdock .glive', { timeout: 15000 });
                const phone = w <= 700;
                if (phone) { await page.click('.htab.t-games').catch(() => {}); await sleep(200); }
                await sleep(300);
                const g = await page.evaluate(GEOM);
                check(name + ': eight matches in progress are eight rows', g.rows.length === 8, String(g.rows.length));
                check(name + ': the page does not scroll sideways, and neither does the list', g.page[0] <= g.page[1] && (!g.dock || g.dock[0] <= g.dock[1] + 1), JSON.stringify([g.page, g.dock]));
                check(name + ': no row is wider than itself and no list of names runs out of its row', g.rows.every((r) => r.w <= 0 && r.text <= 0), JSON.stringify(g.rows.map((r) => [r.w, r.text])));
                check(name + ': no row is taller than 200 px, whatever the names are (a long name wraps by words and by characters, in its own line)', g.rows.every((r) => r.h <= 200), JSON.stringify(g.rows.map((r) => Math.round(r.h))));
                check(name + ': every Watch is at least 44 x 44 px, inside its row and not over its words', g.rows.every((r) => r.go[0] >= 44 && r.go[1] >= 44 && r.inside && !r.clash), JSON.stringify(g.rows.map((r) => [r.go, r.inside, r.clash])));
                const info = await page.evaluate(() => ({ pill: document.querySelector('.hchip.games .pill') && document.querySelector('.hchip.games .pill').textContent, head: document.querySelector('.gdock .gpcount').textContent,
                    hits: window.__hit === undefined, imgs: document.querySelectorAll('.glive .gwho img, .glive .gwho *').length, links: Array.from(document.querySelectorAll('.glive .gwatch')).map((a) => [a.getAttribute('href'), a.target, a.rel]) }));
                check(name + ': the Games button counts the 3 open rooms only, and the head says "3 open, 8 in progress"', info.pill === '3' && info.head === '3 open, 8 in progress', JSON.stringify([info.pill, info.head]));
                check(name + ': a name that is markup ran nothing and made no element', info.hits && info.imgs === 0);
                check(name + ': every Watch is /play.html?live=<id> in a new tab with no opener', info.links.length === 8 && info.links.every((l) => /^\/play\.html\?live=[A-Za-z0-9_]+-[0-9]{8}-[0-9]{6}Z$/.test(l[0]) && l[1] === '_blank' && /noopener/.test(l[2])), JSON.stringify(info.links[0]));
                await shot(page, name + '_busy');
                await page.evaluate(() => { const b = document.querySelector('.gdock .gpbody'); b.scrollTop = b.scrollHeight; });
                await sleep(500);
                await shot(page, name + '_busy_scrolled');
                await page.evaluate(() => { document.querySelector('.gdock .gpbody').scrollTop = 0; });
                if (name === 'desktop') {
                    const pagePromise = context.waitForEvent('page', { timeout: 5000 });
                    await page.click('.gdock .glive:nth-of-type(2) .gwatch');
                    const popup = await pagePromise.catch(() => null);
                    check('a press on Watch opens a NEW page on that match', !!popup && /\/play\.html\?live=ISLANDS-/.test(popup.url()), popup ? popup.url() : 'no page');
                    check('... and the lobby page is still on its list with its rows', /\/$/.test(page.url()) && (await page.evaluate(() => document.querySelectorAll('.gdock .glive').length)) === 8);
                    if (popup) await popup.close();
                    // the switch
                    const sw = await page.evaluate(() => { const l = document.querySelector('.gfilter'), i = l.querySelector('input'), r = l.getBoundingClientRect(); return { type: i.type, on: i.checked, text: l.textContent, h: r.height, label: i.labels && i.labels[0] === l }; });
                    check('the switch is a checkbox in its label, on, 44 px high', sw.type === 'checkbox' && sw.on && sw.label && sw.text === 'Show games in progress' && sw.h >= 44, JSON.stringify(sw));
                    await page.focus('.gdock .gcode input');
                    let reached = false;
                    await page.focus('.gfilter input');
                    reached = await page.evaluate(() => document.activeElement === document.querySelector('.gfilter input'));
                    check('the keyboard reaches the switch', reached);
                    await page.keyboard.press('Space');
                    await sleep(300);
                    const off = await page.evaluate(() => ({ rows: document.querySelectorAll('.gdock .glive').length, shown: Array.from(document.querySelectorAll('.glivebox')).some((b) => b.getClientRects().length), head: document.querySelector('.gdock .gpcount').textContent, kept: localStorage.getItem('ants.home.games.live'), pill: document.querySelector('.hchip.games .pill').textContent }));
                    check('Space turns it off: the rows and their box are gone, the head is as before them, the choice is kept, the count has not moved', off.rows === 0 && !off.shown && /^3 waiting/.test(off.head) && off.kept === '0' && off.pill === '3', JSON.stringify(off));
                    await shot(page, name + '_filter_off');
                    const seen = state.live;
                    await sleep(12500);
                    check('... and nothing looks at /live while it is off', state.live === seen, state.live + ' vs ' + seen);
                    await page.reload({ waitUntil: 'load' });
                    await page.waitForSelector('.gdock .gitem', { timeout: 15000 });
                    await sleep(800);
                    const after = state.live;
                    const again = await page.evaluate(() => ({ on: document.querySelector('.gfilter input').checked, rows: document.querySelectorAll('.gdock .glive').length }));
                    check('after a reload it is still off, and /live was not asked at all', !again.on && again.rows === 0 && state.live === seen, JSON.stringify([again, after, seen]));
                    await page.focus('.gfilter input');
                    await page.keyboard.press('Space');
                    await page.waitForSelector('.gdock .glive', { timeout: 5000 });
                    const on = await page.evaluate(() => ({ rows: document.querySelectorAll('.gdock .glive').length, kept: localStorage.getItem('ants.home.games.live'), head: document.querySelector('.gdock .gpcount').textContent }));
                    check('Space turns it on: the rows are back at once, the choice is the default again, the head says both', on.rows === 8 && on.kept === null && on.head === '3 open, 8 in progress', JSON.stringify(on));
                    await shot(page, name + '_filter_on');
                    const t = state.liveTimes;
                    check('the looks at /live were never closer than 10 s (apart from the one that a switch makes at once)', t.every((x, i) => i === 0 || x - t[i - 1] >= 10000 || i >= t.length - 2), JSON.stringify(t.map((x) => x - t[0])));
                }
                await context.close();
            }
            site.close();
        }
        for (const mode of ['none', '404', 'down']) {
            const { site, state, origin } = await startSite(mode);
            for (const [name, w, h, touch] of [SIZES[0], SIZES[4]]) {
                const { context, page } = await open(origin, w, h, touch);
                if (w <= 700) { await page.click('.htab.t-games').catch(() => {}); }
                await sleep(1500);
                const g = await page.evaluate(() => ({ rows: document.querySelectorAll('.gdock .glive').length, box: Array.from(document.querySelectorAll('.glivebox')).some((b) => b.getClientRects().length), none: (document.querySelector('.gnolive') || {}).textContent, sw: !!document.querySelector('.gfrow') && document.querySelector('.gfrow').getClientRects().length > 0,
                    cards: document.querySelectorAll('.gdock .glist:not(.glivelist) .gitem').length, note: !!document.querySelector('.gpnote') && !document.querySelector('.gpnote').hidden, toast: !!document.querySelector('.gtoast') && !document.querySelector('.gtoast').hidden, head: document.querySelector('.gdock .gpcount').textContent }));
                check(name + '/' + mode + ': no row, and the open rooms stay', g.rows === 0 && g.cards === 3, JSON.stringify(g));
                if (mode === 'none') check(name + '/none: the box says "No games in progress." and the switch is there', g.box && g.none === 'No games in progress.' && g.sw && g.head === '3 open, 0 in progress', JSON.stringify(g));
                else check(name + '/' + mode + ': the section and the switch are not there, with no notice, no toast and the head as before', !g.box && !g.sw && !g.note && !g.toast && /^3 waiting/.test(g.head), JSON.stringify(g));
                await shot(page, name + '_' + mode);
                await context.close();
            }
            site.close();
        }
        check('the page made no error of its own', errors.length === 0, errors.join(' | '));
    } finally {
        await browser.close();
    }
    console.log('games in progress (real browser): ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
