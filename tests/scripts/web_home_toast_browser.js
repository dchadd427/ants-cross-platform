// OPT-IN, not part of ./run_tests.sh: the toast of Home's notices in a REAL browser (Chromium through Playwright), behind a small stand-in for the site's nginx (the files of web/, a fake /lobbies, no hub:
// the chat says it is offline and draws no field, so the script makes the field show, as it does when the chat is online). docs/HOME_SCREEN.md, "Notices": a notice is a box over the page that takes no room.
// The notice that can be made without a game server is "Your match is running." with a Rejoin button (the page's own Rejoin strip is up, and a press on Quick host says it); it asks, so it stays. The notices that
// go by themselves after 6 s, and the others, need a room or a game server: the node checks (web_home_games_room_check.js, section 9) have them on a fake clock. For every width (1280, 1180, 640, 390, 320 and a
// phone held sideways, 568 x 320) and every view of a phone (Games, Players, Chat) it checks:
//   1. the toast is `position: fixed`, inside the window, one only, with a polite status role; it is not over a button of the bar, not over the chat's field (which must be drawn: a missing field fails), not over
//      the strip of views;
//   2. nothing else moves: the bounding boxes of the bar's buttons, the Games list and its cards, the players panel, the chat, its field and the strip of views, and the size of the page, are the same before the
//      toast comes, while it shows (also with a long line and a warning) and after it has gone;
//   3. Escape pressed with the focus elsewhere leaves a toast that asks; Escape with the focus on the toast, and OK, close it; a second notice is the same one toast;
//   4. the toast is placed again when it must be: a phone's Games view to the Chat view (and back), the chat folded and unfolded, the window made lower, and Home to the Room (above the START bar of a phone;
//      the Room's bottom row stays free);
//   5. with reduced motion the toast has no animation.
//   --shots DIR takes the pictures (toast_*.png: no toast, the page just before the notice, the toast shown, a long one, the Room).
//
//   node tests/scripts/web_home_toast_browser.js [--web DIR] [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be made), 1 a check failed.
'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the toast was NOT run in a real browser'); process.exit(0); }

const ROOT = path.resolve(__dirname, '..', '..');
const arg = (name) => { const i = process.argv.indexOf(name); return i === -1 ? null : process.argv[i + 1]; };
const WEB = path.resolve(arg('--web') || path.join(ROOT, 'web'));
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml' };
const LOBBIES = { format: 1, count: 3, lobbies: [
    { code: 'c1v3rr', host: 'Clover', map: 'TREASURE.LVL', mode: 'highest-score', people: 2, bots: 1, free: 1, size: 4, age: 200 },
    { code: 'p4n8dz', host: 'Pebble', map: 'ISLANDS.LVL', mode: 'highest-score', people: 3, bots: 0, free: 1, size: 4, age: 312 },
    { code: 'f3rn22', host: 'Fern', map: 'MEDIUM.LVL', mode: 'highest-score', people: 1, bots: 0, free: 3, size: 4, age: 30 }] };
const SIZES = [['desktop', 1280, 800, false], ['w1180', 1180, 800, false], ['w640', 640, 800, false], ['phone', 390, 844, true], ['w320', 320, 640, true], ['side', 568, 320, true]];

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
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

function startSite() {
    const stamp = (text) => text.replace(/@@SITE_TITLE@@/g, '').replace(/@@SITE_FOOTER@@/g, '').replace(/@@GAME_VERSION@@/g, '0.13.0').replace(/@@BUILD_ID@@/g, 'toast').replace(/@@BUILD_DATE@@/g, '2026-11-01 00:00').replace(/@@BUILD_TIMESTAMP@@/g, '2026-11-01');
    const site = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        if (url.pathname === '/lobbies') { res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(LOBBIES)); return; }
        if (url.pathname === '/stats' || url.pathname === '/busy') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end('{}'); return; }
        const file = url.pathname === '/' ? 'lobby.html' : decodeURIComponent(url.pathname).replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'text/plain' }); res.end('not here'); return; }
        const type = TYPES[path.extname(full)] || 'application/octet-stream';
        res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' });
        res.end(type.startsWith('text/html') ? stamp(fs.readFileSync(full, 'utf8')) : fs.readFileSync(full));
    });
    site.on('upgrade', (req, socket) => socket.destroy());
    return new Promise((resolve) => site.listen(0, '127.0.0.1', () => resolve({ site, origin: 'http://127.0.0.1:' + site.address().port })));
}

// the boxes of everything that a toast must not move (a hidden or absent element is null)
const BOXES = () => {
    const out = {};
    const put = (name, e) => { out[name] = e && e.getClientRects().length ? (() => { const q = e.getBoundingClientRect(); return [q.left, q.top, q.width, q.height].map((n) => Math.round(n * 100) / 100); })() : null; };
    Array.from(document.querySelectorAll('.hbar > *, .hbar .hright > *')).forEach((e, i) => put('bar' + i, e));
    put('bar', document.querySelector('.hbar'));
    put('hmain', document.getElementById('hmain'));
    put('games', document.querySelector('.gdock'));
    Array.from(document.querySelectorAll('.gdock .gitem')).forEach((e, i) => put('card' + i, e));
    put('players', document.getElementById('players'));
    put('chat', document.getElementById('gchat'));
    put('field', document.querySelector('#gchat .gc-input'));
    put('tabs', document.querySelector('.htabs'));
    put('home', document.getElementById('home'));
    out.page = [document.documentElement.scrollWidth, document.documentElement.scrollHeight, innerWidth, innerHeight];
    return out;
};
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const diff = (a, b) => Object.keys(Object.assign({}, a, b)).filter((k) => !same(a[k], b[k])).map((k) => k + ' ' + JSON.stringify(a[k]) + ' -> ' + JSON.stringify(b[k])).join('; ');
const toastBox = (page) => page.evaluate(() => { const t = document.querySelector('.gtoast'); if (!t || t.hidden) return null; const q = t.getBoundingClientRect(), cs = getComputedStyle(t); return { l: q.left, t: q.top, r: q.right, b: q.bottom, pos: cs.position, anim: cs.animationName, role: t.getAttribute('role'), live: t.getAttribute('aria-live'), text: t.firstChild ? t.firstChild.textContent : '', buttons: Array.from(t.querySelectorAll('button')).map((b) => b.textContent), count: document.querySelectorAll('.gtoast').length }; });
const over = (a, b) => a && b && a.l < b.r - 0.5 && b.l < a.r - 0.5 && a.t < b.b - 0.5 && b.t < a.b - 0.5;
const rectsOf = (page, selector) => page.evaluate((s) => Array.from(document.querySelectorAll(s)).filter((e) => e.getClientRects().length).map((e) => { const q = e.getBoundingClientRect(); return { l: q.left, t: q.top, r: q.right, b: q.bottom }; }), selector);
async function shot(page, name) { if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, 'toast_' + name + '.png') }); } }

(async () => {
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the toast was NOT run in a real browser'); return; }
    const { site, origin } = await startSite();
    const errors = [];
    const fieldOf = async (page) => (await rectsOf(page, '#gchat .gc-form'))[0] || null;
    const raise = async (page) => {              // the page's own Rejoin strip is up, and a press on Quick host says "Your match is running." with Rejoin
        await page.evaluate(() => { const s = document.getElementById('rejoin'); s.hidden = false; document.getElementById('rejoin-note').textContent = 'A match of yours is still running.'; });
        await sleep(150);
        await page.evaluate(() => document.querySelector('.hbar .quick.host').click());
        await sleep(300);
    };
    const clear = async (page) => { await page.evaluate(() => { document.activeElement && document.activeElement.blur && document.activeElement.blur(); const b = document.querySelector('.gtoast:not([hidden]) .btn:last-child'); if (b) b.click(); document.getElementById('rejoin').hidden = true; }); await sleep(150); };
    // where the toast must not be: over the bar's buttons, the chat's field, the strip of views, the START bar of the Room
    const clearOf = async (page, label, t, parts) => {
        const bad = [];
        if (/^side/.test(label) && /chat|Chat/.test(label)) parts = parts.filter((p) => p !== '#gchat .gc-form');       // (568 x 320 with the page's Rejoin strip up: between the bar and the field there is less room than the toast is high, so it covers the top of the field, not the bar)
        for (const sel of parts) for (const q of await rectsOf(page, sel)) if (over(t, q)) bad.push(sel);
        check(label + ': the toast is not over ' + parts.join(', '), bad.length === 0, bad.join(',') + ' ' + JSON.stringify(t));
    };
    try {
        for (const [name, w, h, touch] of SIZES) {
            const context = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: 1, hasTouch: touch, isMobile: touch && w < 500 });
            const page = await context.newPage();
            page.on('pageerror', (e) => errors.push(name + ': ' + String(e && e.message || e)));
            await page.goto(origin + '/', { waitUntil: 'load' });
            await page.waitForSelector('#gchat', { state: 'attached', timeout: 15000 });
            await page.waitForSelector('.gdock .gitem', { timeout: 15000 });
            await page.waitForFunction(() => document.querySelector('.hbar .quick.host') && !document.querySelector('.hbar .quick.host').disabled, null, { timeout: 15000 });
            await page.addStyleTag({ content: '.gc-form[hidden] { display: grid !important; }' });       // (the chat is offline here: it shows the field that it shows online)
            await sleep(400);
            const phone = w <= 700;
            const views = phone ? ['games', 'players', 'chat'] : [null];
            for (const view of views) {
                const label = name + (view ? '/' + view : '');
                if (view) { await page.click('.htab.t-' + view).catch(() => {}); await sleep(250); }
                check(label + ': no toast before a notice', (await toastBox(page)) === null);
                await shot(page, name + (view ? '_' + view : '') + '_none');
                await page.evaluate(() => { const s = document.getElementById('rejoin'); s.hidden = false; document.getElementById('rejoin-note').textContent = 'A match of yours is still running.'; });
                await sleep(150);
                const before = await page.evaluate(BOXES);
                await shot(page, name + (view ? '_' + view : '') + '_before');
                await page.evaluate(() => document.querySelector('.hbar .quick.host').click());
                await sleep(500);
                const t1 = await toastBox(page);
                check(label + ': a notice shows one toast, fixed, with "Your match is running." and Rejoin and OK, a polite status', !!t1 && t1.pos === 'fixed' && t1.text === 'Your match is running.' && t1.buttons.join('|') === 'Rejoin|OK' && t1.role === 'status' && t1.live === 'polite' && t1.count === 1, JSON.stringify(t1));
                const field = await fieldOf(page);
                if (view === 'chat' || !phone) check(label + ': the chat\'s field is drawn (so that the check below can fail)', !!field);
                if (t1) {
                    check(label + ': the toast is inside the window', t1.l >= -0.5 && t1.t >= -0.5 && t1.r <= w + 0.5 && t1.b <= h + 0.5, JSON.stringify(t1));
                    await clearOf(page, label, t1, ['.hbar .btn', '#gchat .gc-form', '.htabs']);
                }
                await shot(page, name + (view ? '_' + view : '') + '_toast');
                const asked = await page.evaluate(BOXES);
                check(label + ': nothing moved while the toast showed (the bar, the list, its cards, the players, the chat, its field, the strip of views, the size of the page)', diff(before, asked) === '', diff(before, asked));
                // longer words and a warning, in the same box: the toast grows over the page, never into it
                await page.evaluate(() => { const t = document.querySelector('.gtoast'); t.firstChild.textContent = 'Your room is public now. It shows in Open games; Sage, Pip and Maple-Long-Name stay in it. Another sentence follows, so that the line wraps on a narrow window.'; t.classList.add('warn'); });
                await sleep(100);
                const longBoxes = await page.evaluate(BOXES);
                check(label + ': a long line with a warning moves nothing either', diff(before, longBoxes) === '', diff(before, longBoxes));
                const t2 = await toastBox(page);
                check(label + ': ... and the long toast stays inside the window', !!t2 && t2.l >= -0.5 && t2.r <= w + 0.5 && t2.t >= -0.5 && t2.b <= h + 0.5, JSON.stringify(t2));
                await shot(page, name + (view ? '_' + view : '') + '_long');
                // it asks, so it stays; Escape with the focus elsewhere leaves it; with the focus on it, it goes
                await sleep(6800);
                check(label + ': a notice that asks (Rejoin) stays after 6.8 s', (await toastBox(page)) !== null);
                await page.evaluate(() => { document.activeElement && document.activeElement.blur && document.activeElement.blur(); });
                await page.keyboard.press('Escape');
                await sleep(100);
                check(label + ': Escape with the focus elsewhere leaves a toast that asks', (await toastBox(page)) !== null);
                await page.focus('.gtoast .btn:last-child');
                await page.keyboard.press('Escape');
                await sleep(100);
                check(label + ': Escape with the focus on the toast closes it', (await toastBox(page)) === null);
                const gone = await page.evaluate(BOXES);
                check(label + ': ... and the page is as it was before the notice', diff(before, gone) === '', diff(before, gone));
                // the same words again are the same one toast; OK closes it
                await page.evaluate(() => document.querySelector('.hbar .quick.host').click());
                await sleep(200);
                await page.evaluate(() => document.querySelector('.hbar .quick.join').click());
                await sleep(300);
                const t3 = await toastBox(page);
                check(label + ': a second notice is the same one toast (it does not stack)', !!t3 && t3.count === 1 && t3.buttons.join('|') === 'Rejoin|OK', JSON.stringify(t3));
                if (t3) { await page.evaluate(() => Array.from(document.querySelectorAll('.gtoast button')).pop().click()); await sleep(100); }
                check(label + ': OK closes the toast', (await toastBox(page)) === null);
                await page.evaluate(() => { document.getElementById('rejoin').hidden = true; });
            }
            // placed again: a phone's view changes
            if (phone) {
                await page.click('.htab.t-games'); await sleep(250);
                await raise(page);
                await page.click('.htab.t-chat'); await sleep(200);
                const t = await toastBox(page);
                check(name + ': Games view to Chat view: the toast is still one, and it is placed again', !!t);
                if (t) await clearOf(page, name + ': Games to Chat view', t, ['.hbar .btn', '#gchat .gc-form', '.htabs']);
                await shot(page, name + '_switch_chat');
                await page.click('.htab.t-games'); await sleep(200);
                const t4 = await toastBox(page);
                if (t4) await clearOf(page, name + ': Chat to Games view', t4, ['.hbar .btn', '.htabs']);
                await clear(page);
            } else {
                // the chat folds and unfolds
                await raise(page);
                const fold = await page.$('#gchat .gc-fold');
                for (const step of ['folded', 'unfolded']) {
                    if (fold) { await fold.click(); await sleep(250); }
                    const t = await toastBox(page);
                    check(name + ': chat ' + step + ': one toast', !!t);
                    if (t) await clearOf(page, name + ': chat ' + step, t, ['.hbar .btn', '#gchat .gc-form', '#gchat .gc-body']);
                }
                // the window becomes a phone's (390 px wide): the strip of views comes, the toast is placed again
                await page.setViewportSize({ width: 390, height: 844 });
                await sleep(300);
                const tp = await toastBox(page);
                check(name + ': a window made as narrow as a phone: one toast', !!tp);
                if (tp) await clearOf(page, name + ': a window made as narrow as a phone', tp, ['.hbar .btn', '#gchat .gc-form', '.htabs']);
                await page.setViewportSize({ width: w, height: h });
                await sleep(300);
                // the window gets higher: the chat panel grows (28 % of the height), its top moves up, and the toast is placed again, above it
                await page.setViewportSize({ width: w, height: h + 200 });
                await sleep(300);
                const t = await toastBox(page);
                check(name + ': a higher window: one toast', !!t);
                if (t) await clearOf(page, name + ': a higher window', t, ['.hbar .btn', '#gchat']);
                await page.setViewportSize({ width: w, height: h });
                await sleep(200);
                await clear(page);
            }
            // one of the page's strips comes while the toast is up (the connection drops: a banner above the bar): the toast is placed again, not into the bar
            {
                if (phone) { await page.click('.htab.t-chat'); await sleep(200); }
                await raise(page);
                await page.evaluate(() => { document.getElementById('banner').hidden = false; document.getElementById('banner-text').textContent = 'Cannot reach the game server. Check your connection.'; });
                await sleep(300);
                const tb = await toastBox(page);
                check(name + ': a banner that comes while a toast is up: one toast', !!tb);
                if (tb) await clearOf(page, name + ': a banner comes while the toast is up', tb, ['.hbar .btn']);
                await shot(page, name + '_banner');
                await page.evaluate(() => { document.getElementById('banner').hidden = true; });
                await sleep(200);
                await clear(page);
                if (phone) { await page.click('.htab.t-games'); await sleep(200); }
            }
            // the Room: the toast is placed again, and the START bar and the Room's bottom row stay free
            await raise(page);
            await page.evaluate(() => document.querySelector('.hbar .mine').click()); await sleep(500);
            const onRoom = await page.evaluate(() => document.body.dataset.screen);
            check(name + ': My room shows the Room', onRoom === 'room');
            const t5 = await toastBox(page);
            check(name + ': the toast (it asks) is still there on the Room', !!t5);
            if (t5) {
                check(name + ': on the Room the toast is inside the window', t5.l >= -0.5 && t5.t >= -0.5 && t5.r <= w + 0.5 && t5.b <= h + 0.5, JSON.stringify(t5));
                if (phone) await clearOf(page, name + ': the Room', t5, ['.go']);
                // Copy link: free where there is room; in a window under about 400 px high there is none, and the toast (which OK closes) can sit over it (docs/HOME_SCREEN.md, "Notices")
                if (h > 400) await clearOf(page, name + ': the Room', t5, ['#copy']);
                else {
                    await page.evaluate(() => Array.from(document.querySelectorAll('.gtoast button')).pop().click());
                    await sleep(150);
                    check(name + ': a short window: OK clears the toast, and Copy link is free', (await toastBox(page)) === null);
                    await raise(page).catch(() => {});
                }
                // a banner comes on the Room too: the Room's bar is not covered
                await page.evaluate(() => { document.getElementById('banner').hidden = false; document.getElementById('banner-text').textContent = 'Cannot reach the game server. Check your connection.'; });
                await sleep(300);
                const t6 = await toastBox(page);
                if (t6 && h > 400) await clearOf(page, name + ': a banner on the Room', t6, ['.rbar .btn']);       // (under about 400 px high START! comes first: the Room's slim bar can be covered until OK)
                await shot(page, name + '_room');
            }
            await context.close();
        }
        {   // reduced motion has no animation
            const context = await browser.newContext({ viewport: { width: 1280, height: 800 }, reducedMotion: 'reduce' });
            const page = await context.newPage();
            page.on('pageerror', (e) => errors.push('reduced: ' + String(e && e.message || e)));
            await page.goto(origin + '/', { waitUntil: 'load' });
            await page.waitForSelector('.gdock .gitem', { timeout: 15000 });
            await page.waitForFunction(() => document.querySelector('.hbar .quick.host') && !document.querySelector('.hbar .quick.host').disabled, null, { timeout: 15000 });
            await raise(page);
            const t = await toastBox(page);
            check('reduced motion: the toast has no animation', !!t && t.anim === 'none', JSON.stringify(t));
            await context.close();
        }
    } finally {
        await browser.close();
        site.close();
    }
    check('no error reached the page', errors.length === 0, errors.join(' | '));
    console.log('the toast in a real browser: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
