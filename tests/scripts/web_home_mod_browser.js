// OPT-IN, not part of ./run_tests.sh: the moderation sheet of the chat (web/home/mod.js, mod.css, docs/HOME_SCREEN.md "Roles and commands") in a REAL browser (Chromium through Playwright), on the real front page
// (web/lobby.html with web/home/) and the FAKE hub (tests/scripts/fake_hub.js) that shares the page's address. Made-up names only. For each window size (a phone upright 390 x 844, a phone held sideways 844 x 390,
// a desktop 1280 x 800 with a mouse, the same with a touch screen, and a small phone 320 x 640) it checks:
//   1. an administrator: the part is asked for after the welcome, the lines that have an action carry the mark, and the lines that have none (an administrator's, the hub's own) do not;
//   2. a RIGHT CLICK (a mouse window) and a REAL LONG PRESS (a touch window: touch events sent through the browser's own input, held 600 ms) on a line open the sheet on it; the browser's menu is held back there;
//      a short tap, a finger that moves and scrolls the log, a right click on a line with no action and a right click on a link do not (the browser's own menu stays); the line is marked while it is pressed;
//   3. the sheet: a dialog, modal, named by the person, the first action has the focus, every control is at least 44 x 44 px, the box is inside the window, nothing scrolls sideways, Tab stays inside, what stands
//      behind is inert, Escape lets it go and the focus is back on the log; the mute step (four times, the custom number: Mute stays off for 0 and for 10,081, on for 90, Enter sends, the commands are `/mute #n m`);
//      the confirm cards (Cancel has the focus, nothing is sent before the red button, the red button takes no press for 400 ms, `/kick #n`, `/ban #n`, `/ban #n purge`, `/del n`); one command at a time;
//   4. the answer: a toast AT THE TOP (inside the window, under the bar, over no composer), the same words as a private line of the chat, a refusal that stays with OK;
//   5. the keyboard: the log has the focus, Up and Down choose a line, Enter, the menu key and Shift and F10 open it, Escape lets the line go;
//   6. a moderator (no Ban, nothing on an administrator's or a moderator's line) and a person with no role: nothing marked, the part is never asked for, a right click and a long press are the browser's own, and the lines
//      are as wide as for staff (no button takes room from them);
//   7. the page's own errors are none.
//   --shots DIR takes the pictures (<scene>_<size>.png, 2 x): the line sheet by right click and by long press, the pressed line, the keyboard's line, the mute step and its custom field, the confirm cards, the
//   toast (done and refused), a moderator's sheet, a person's log.
//
//   node tests/scripts/web_home_mod_browser.js [--web DIR] [--shots DIR] [--only phone|side|desktop|desktouch|w320]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be made), 1 a check failed.
'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');
const { startHub, line } = require('./fake_hub.js');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the moderation sheet was NOT run in a real browser'); process.exit(0); }

const ROOT = path.resolve(__dirname, '..', '..');
const arg = (name) => { const i = process.argv.indexOf(name); return i === -1 ? null : process.argv[i + 1]; };
const WEB = path.resolve(arg('--web') || path.join(ROOT, 'web'));
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml', '.webmanifest': 'application/manifest+json' };
const ONLY = arg('--only');       // (one size by its name, to look at one)
const SIZES = [['phone', 390, 844, true], ['side', 844, 390, true], ['desktop', 1280, 800, false], ['desktouch', 1280, 800, true], ['w320', 320, 640, true]].filter((s) => !ONLY || s[0] === ONLY);       // [name, width, height, touch]
const NOW = Math.floor(Date.now() / 1000);

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function serve() {
    const requests = [];
    const server = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        requests.push(url.pathname);
        const file = url.pathname === '/' ? 'lobby.html' : url.pathname.replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'application/json' }); res.end('{"error":"not here"}'); return; }
        const text = /html/.test(TYPES[path.extname(full)] || '') ? fs.readFileSync(full, 'utf8').replace(/@@[A-Z_]+@@/g, '') : null;
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
        res.end(text !== null ? text : fs.readFileSync(full));
    });
    return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({ server, requests, url: 'http://127.0.0.1:' + server.address().port })));
}

// made-up people only
const VIEWERS = { admin: { name: 'Basil', role: 'admin', k: 'k-basil' }, mod: { name: 'Marlow', role: 'mod', k: 'k-marlow' }, player: { name: 'Hazel', role: 'none', k: 'k-hazel' } };
function history(v) {
    let n = 0;
    const L = (id, who, text, extra) => line(id, who, text, !!(extra && extra.role), Object.assign({ at: NOW - (14 - ++n) * 41 - 20 }, extra));
    return [
        L(141, 'Pebble', 'anyone up for Islands? two seats left', { k: 'k-pebble' }),
        L(142, 'Juniper', 'I am in, give me a minute', { k: 'k-juniper', u: 'juniper.berry' }),
        L(143, 'Sage', 'Welcome everyone. Please be kind in here.', { k: 'k-sage', role: 'mod', u: 'sage.moss' }),
        L(144, 'Fern', 'does the beta keep the old Treasure maps?', { k: 'k-fern', u: 'fern_77' }),
        L(145, 'Pebble', 'yes, all of them', { k: 'k-pebble' }),
        L(146, 'Tess', 'Matches start at the top of the hour.', { k: 'k-tess', role: 'admin', u: 'tess.ward' }),
        L(147, 'system', 'Dash was kicked.', { k: '', sys: true }),
        L(148, 'Clover', 'FREE ant gold!!! get 500 coins at my page, dm me now, only for the first 20 players', { k: 'k-clover', u: 'clover.4782' }),
        L(149, 'Juniper', 'that one looks like spam to me, see https://example.org/spam for what it says', { k: 'k-juniper', u: 'juniper.berry' }),
        L(150, 'Fern', 'reported it, thanks Juniper', { k: 'k-fern', u: 'fern_77' }),
        L(151, 'Nobody', 'a line of somebody the hub does not know any more', { k: 'k-nobody', u: 'nobody.1' }),
        L(152, v.name, v.role === 'admin' ? 'I am looking at it now.' : v.role === 'mod' ? 'Thanks, I will check it.' : 'thanks for the heads up', Object.assign({ k: v.k }, v.role === 'none' ? {} : { role: v.role }, v.role === 'none' ? {} : { mod: true })),
    ];
}
async function startAll(kind) {
    const site = await serve();
    const v = VIEWERS[kind];
    const hub = await startHub({
        server: site.server, login: true, online: 12,
        you: v.role === 'none' ? { name: v.name, mod: false, k: v.k } : { name: v.name, mod: true, role: v.role, k: v.k },
        history: history(v),
        people: { clover: 'none', juniper: 'none', fern: 'none', pebble: 'none', sage: 'mod', tess: 'admin' },       // ('Nobody' is not here: a command that names that line is answered `who`)
    });
    return { site, hub };
}

async function main() {
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the moderation sheet was NOT run in a real browser'); process.exit(0); }
    const errors = [];
    const rigs = { admin: await startAll('admin'), mod: await startAll('mod'), player: await startAll('player') };
    const shot = async (page, scene, size, quick) => { if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); if (!quick) await sleep(250); await page.screenshot({ path: path.join(SHOTS, scene + '_' + size + '.png') }); } };
    const rect = (page, selector) => page.evaluate((s) => { const e = document.querySelector(s); if (!e || !e.getClientRects().length) return null; const r = e.getBoundingClientRect(); return { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height), right: Math.round(r.right), bottom: Math.round(r.bottom) }; }, selector);
    const row = (id) => '.gc-log .gc-line[data-n="' + id + '"]';
    const sheetUp = (page) => page.evaluate(() => !!document.querySelector('.gm-shade'));
    const toastWords = (page) => page.evaluate(() => { const t = document.querySelector('.gm-toast .gm-tt'); return t ? t.textContent : null; });
    const privates = (page) => page.evaluate(() => [...document.querySelectorAll('#gchat .gc-private')].map((p) => p.textContent.replace(/^Only you/, '')));
    const focused = (page) => page.evaluate(() => { const a = document.activeElement; return a ? (a.getAttribute('aria-label') || a.textContent || a.className || a.tagName).slice(0, 40) : ''; });
    const names = (page) => page.evaluate(() => [...document.querySelectorAll('.gm-box button')].map((b) => b.getAttribute('aria-label') || b.textContent));
    const pressBy = (page, label) => page.evaluate((l) => { const b = [...document.querySelectorAll('.gm-box button')].find((x) => (x.getAttribute('aria-label') || x.textContent) === l); if (!b) return false; b.click(); return true; }, label);
    const clickBy = async (page, label) => { const b = await page.locator('.gm-box button', { hasText: label }).first(); try { await b.click({ timeout: 4000 }); } catch (e) { console.log('   (no button ' + label + ': sheet ' + (await sheetUp(page)) + ', buttons ' + JSON.stringify(await names(page)) + ', lines ' + (await page.evaluate(() => [...document.querySelectorAll('#gchat .gc-line')].map((r) => r.getAttribute('data-n')).join())) + ')'); throw e; } };

    async function open(rig, size, w, h, touch, o) {
        o = o || {};
        const context = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: SHOTS ? 2 : 1, hasTouch: touch, isMobile: touch && w <= 700, forcedColors: o.forced ? 'active' : 'none' });
        if (w >= 1000) await context.addInitScript(() => { try { localStorage.setItem('ants.home.split.v1', JSON.stringify({ c: 400 })); } catch (e) { /* none */ } });
        await context.addInitScript(() => { window.__buzz = []; try { navigator.vibrate = function (ms) { window.__buzz.push(ms); return true; }; } catch (e) { /* none */ } window.__ctx = []; document.addEventListener('contextmenu', (e) => { window.__ctx.push(e.defaultPrevented); }); });
        const page = await context.newPage();
        page.on('pageerror', (e) => errors.push(size + ': ' + String(e && e.message || e)));
        rig.site.requests.length = 0;
        await page.goto(rig.site.url + '/', { waitUntil: 'load' });
        await page.waitForSelector('#gchat', { state: 'attached', timeout: 8000 });
        await page.waitForFunction(() => { const d = document.querySelector('#gchat'); return !!d && d.getAttribute('data-state') === 'live'; }, null, { timeout: 8000 });
        if (w <= 700) { const tab = page.locator('.htab.t-chat'); await tab.click().catch(() => {}); }
        if (w > 700 && h < 500 && (await page.getAttribute('#gchat', 'data-open')) === 'false') await page.click('#gchat .gc-head');
        await page.waitForFunction(() => document.querySelectorAll('#gchat .gc-line').length > 6, null, { timeout: 8000 });
        if (o.staff) await page.waitForFunction(() => document.querySelectorAll('#gchat .gc-line[data-act]').length > 0, null, { timeout: 8000 });
        else await sleep(500);
        await page.evaluate(() => { const log = document.querySelector('#gchat .gc-log'); log.scrollTop = log.scrollHeight; });
        await sleep(200);
        return { page, context };
    }
    // a touch that is held (a long press), tapped or dragged, through the browser's own input
    async function hold(ctx, page, id, ms) {
        const box = await page.locator(row(id)).boundingBox();
        const x = Math.round(box.x + Math.min(box.width - 6, 150)), y = Math.round(box.y + 8);
        const cdp = await ctx.newCDPSession(page);
        await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y }] });
        await sleep(ms);
        return { cdp, x, y, end: () => cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }), move: (dy) => cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x, y: y + dy }] }) };
    }
    async function pointOf(page, id) { const box = await page.locator(row(id)).boundingBox(); return { x: Math.round(box.x + Math.min(box.width - 6, 150)), y: Math.round(box.y + 8) }; }
    async function openByRightClick(page, id) { const p = await pointOf(page, id); await page.mouse.click(p.x, p.y, { button: 'right' }); await sleep(120); }
    async function openByLongPress(ctx, page, id) { const t = await hold(ctx, page, id, 650); await t.end(); await sleep(450); }       // (the sheet takes no press for 400 ms after the finger lifts)
    const way = (touch) => (touch ? 'a long press' : 'a right click');
    async function openIt(ctx, page, touch, id) { await page.locator(row(id)).scrollIntoViewIfNeeded(); if (touch) await openByLongPress(ctx, page, id); else await openByRightClick(page, id); }
    const closeIt = async (page) => { await page.keyboard.press('Escape'); await sleep(100); };

    // the lines of a window as a person sees them: the width of the line of a viewer's choice
    const widthOf = (page, id) => page.evaluate((s) => { const e = document.querySelector(s); return e ? Math.round(e.getBoundingClientRect().width) : -1; }, row(id));

    // ---- staff: an administrator ----
    const wide = {};
    for (const [size, w, h, touch] of SIZES) {
        const name = size + ': ';
        const { page, context } = await open(rigs.admin, size, w, h, touch, { staff: true });
        const hub = rigs.admin.hub, base = hub.commands.length, said = () => hub.commands.slice(base);       // (the hub keeps every command of every window: this window's are after `base`)
        check(name + 'the page asked for the two files of the sheet', rigs.admin.site.requests.indexOf('/home/mod.js') !== -1 && rigs.admin.site.requests.indexOf('/home/mod.css') !== -1);
        const marked = await page.evaluate(() => [...document.querySelectorAll('#gchat .gc-line[data-act]')].map((r) => Number(r.getAttribute('data-n'))));
        check(name + 'the lines with an action carry the mark (not an administrator\'s, not the hub\'s own); each has a description for a screen reader', marked.join() === '141,142,143,144,145,148,149,150,151,152' && await page.evaluate(() => [...document.querySelectorAll('#gchat .gc-line[data-act]')].every((r) => /Enter/.test(r.getAttribute('aria-description') || ''))), marked.join());
        check(name + 'the log is described to a screen reader (the keys)', await page.evaluate(() => { const l = document.querySelector('#gchat .gc-log'), d = document.getElementById(l.getAttribute('aria-describedby') || '-'); return !!d && /Up and Down/.test(d.textContent); }));
        wide[size] = await widthOf(page, 148);
        const user = await page.evaluate((s) => getComputedStyle(document.querySelector(s)).userSelect, row(148));
        check(name + 'the text of a line is held back from selection on a touch screen only (' + user + ')', touch ? user === 'none' : user !== 'none', user);

        // the way in
        await page.evaluate(() => { window.__ctx.length = 0; });
        await openIt(context, page, touch, 148);
        check(name + way(touch) + ' on Clover\'s line opens the sheet', await sheetUp(page));
        const info = await page.evaluate(() => {
            const b = document.querySelector('.gm-box'), t = document.getElementById('gm-title');
            return { role: b.getAttribute('role'), modal: b.getAttribute('aria-modal'), title: t ? t.textContent : '', labelled: b.getAttribute('aria-labelledby'), marked: [...document.querySelectorAll('.gc-line.gm-on')].map((r) => r.getAttribute('data-n')), prevented: window.__ctx.slice(), buzz: window.__buzz.length };
        });
        check(name + 'it is a modal dialog named by the person, on that line, and the line is marked', info.role === 'dialog' && info.modal === 'true' && info.title === 'Clover' && info.labelled === 'gm-title' && info.marked.join() === '148', JSON.stringify(info));
        if (!touch) check(name + 'the browser\'s own menu was held back', info.prevented.length === 1 && info.prevented[0] === true, JSON.stringify(info.prevented));
        else check(name + 'the phone buzzed once, lightly', info.buzz === 1, String(info.buzz));
        check(name + 'the first action has the focus', (await focused(page)) === 'Delete this line', await focused(page));
        check(name + 'the actions of an administrator on a person: Delete, Mute, Kick, Ban, Cancel', JSON.stringify(await names(page)) === JSON.stringify(['Delete this line', 'Mute Clover…', 'Kick Clover', 'Ban Clover', 'Cancel']), JSON.stringify(await names(page)));
        const small = await page.evaluate(() => [...document.querySelectorAll('.gm-box button, .gm-box input')].map((e) => { const r = e.getBoundingClientRect(); return [e.getAttribute('aria-label') || e.textContent, Math.round(r.width), Math.round(r.height)]; }).filter((a) => a[1] < 44 || a[2] < 44));
        check(name + 'every control of the sheet is at least 44 x 44', small.length === 0, JSON.stringify(small));
        const inside = await page.evaluate(() => { const r = document.querySelector('.gm-box').getBoundingClientRect(), b = document.documentElement; return { top: r.top, left: r.left, right: window.innerWidth - r.right, bottom: window.innerHeight - r.bottom, sideways: b.scrollWidth > window.innerWidth || document.body.scrollWidth > window.innerWidth, box: document.querySelector('.gm-box').scrollWidth > document.querySelector('.gm-box').clientWidth }; });
        check(name + 'the sheet is inside the window and nothing scrolls sideways', inside.top >= -0.5 && inside.left >= -0.5 && inside.right >= -0.5 && inside.bottom >= -0.5 && !inside.sideways && !inside.box, JSON.stringify(inside));
        const behind = await page.evaluate(() => [...document.body.children].filter((c) => !c.classList.contains('gm-shade') && !c.hasAttribute('inert')).map((c) => c.tagName + '.' + c.className));
        check(name + 'everything behind the sheet is inert', behind.length === 0, behind.join());
        check(name + 'a second ' + way(touch) + ' while the sheet is open opens no second sheet', await (async () => { if (touch) return true; await page.mouse.click(5, 5, { button: 'right' }); return (await page.evaluate(() => document.querySelectorAll('.gm-shade').length)) === 1; })());
        await shot(page, touch ? '2_line_sheet_long_press' : '2_line_sheet_right_click', size);

        // Tab stays inside, Escape lets go, the focus is back on the log
        let outside = 0;
        for (let i = 0; i < 7; i++) { await page.keyboard.press('Tab'); if (!(await page.evaluate(() => !!document.activeElement && !!document.activeElement.closest('.gm-box')))) outside++; }
        check(name + 'Tab stays inside the sheet', outside === 0);
        await page.keyboard.press('Escape'); await sleep(100);
        check(name + 'Escape lets the sheet go, with nothing sent, and the focus is back on the log', !(await sheetUp(page)) && said().length === 0 && await page.evaluate(() => document.activeElement === document.querySelector('#gchat .gc-log')));
        check(name + '...and nothing is inert any more, the line is not marked', await page.evaluate(() => !document.querySelector('[inert]') && !document.querySelector('.gc-line.gm-on')));

        // what does not open it
        await page.evaluate(() => { window.__ctx.length = 0; });
        const noAction = touch ? async (id) => { const t = await hold(context, page, id, 650); await t.end(); } : async (id) => openByRightClick(page, id);
        await page.locator(row(146)).scrollIntoViewIfNeeded();
        await noAction(146);
        check(name + way(touch) + ' on an administrator\'s line opens nothing (the browser\'s own menu is left alone)', !(await sheetUp(page)) && (touch || (await page.evaluate(() => window.__ctx.every((x) => x === false)))));
        await page.locator(row(147)).scrollIntoViewIfNeeded();
        await noAction(147);
        check(name + '...nor on the hub\'s own line', !(await sheetUp(page)));
        await page.evaluate(() => { window.__ctx.length = 0; });
        if (!touch) {
            const a = await page.locator(row(149) + ' a').first().boundingBox();
            await page.locator(row(149)).scrollIntoViewIfNeeded();
            const b = await page.locator(row(149) + ' a').first().boundingBox();
            await page.mouse.click(Math.round(b.x + b.width / 2), Math.round(b.y + b.height / 2), { button: 'right' }); await sleep(120);
            check(name + 'a right click on a link in a line keeps the browser\'s own menu', !(await sheetUp(page)) && (await page.evaluate(() => window.__ctx.every((x) => x === false))) && !!a);
        }
        if (touch) {
            // a short tap, a finger that scrolls, a long press on the link
            await page.locator(row(148)).scrollIntoViewIfNeeded();
            let t = await hold(context, page, 148, 120); await t.end(); await sleep(700);
            check(name + 'a short tap opens nothing and leaves no mark', !(await sheetUp(page)) && (await page.evaluate(() => document.querySelectorAll('.gc-line.gm-on').length)) === 0);
            await page.evaluate(() => { document.querySelector('#gchat .gc-log').scrollTop = 0; });
            const before = await page.evaluate(() => document.querySelector('#gchat .gc-log').scrollTop);
            t = await hold(context, page, 143, 60);
            for (let dy = -12; dy >= -90; dy -= 13) { await t.move(dy); await sleep(30); }
            await sleep(700); await t.end(); await sleep(150);
            const after = await page.evaluate(() => document.querySelector('#gchat .gc-log').scrollTop);
            check(name + 'a finger that moves and scrolls the log opens nothing and leaves no mark', !(await sheetUp(page)) && (await page.evaluate(() => document.querySelectorAll('.gc-line.gm-on').length)) === 0, 'scrollTop ' + before + ' to ' + after);
            await page.evaluate(() => { const log = document.querySelector('#gchat .gc-log'); log.scrollTop = log.scrollHeight; });
            await page.locator(row(149)).scrollIntoViewIfNeeded();
            const lb = await page.locator(row(149) + ' a').first().boundingBox();
            const cdp = await context.newCDPSession(page), lx = Math.round(lb.x + lb.width / 2), ly = Math.round(lb.y + lb.height / 2);
            await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: lx, y: ly }] }); await sleep(700); await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); await sleep(150);
            check(name + 'a long press on a link in a line opens nothing (the browser\'s own preview)', !(await sheetUp(page)));
            // the line is marked while it is pressed
            await page.locator(row(148)).scrollIntoViewIfNeeded();
            t = await hold(context, page, 148, 200);
            const pressed = await page.evaluate(() => [...document.querySelectorAll('.gc-line.gm-on')].map((r) => r.getAttribute('data-n')).join());
            const bar = await page.evaluate(() => { const s = getComputedStyle(document.querySelector('.gc-line.gm-on'), '::before'); return [s.content, s.backgroundColor, s.width]; });
            await shot(page, '2a_pressed_line', size, true);       // (the finger has been down for about 200 ms: the mark is there, the sheet comes at 500)
            await t.end(); await sleep(150);
            check(name + 'a line is marked (a gold bar) while the finger is on it, 200 ms in, with no sheet yet', pressed === '148' && !(await sheetUp(page)) && bar[0] !== 'none' && bar[2] === '4px', JSON.stringify([pressed, bar]));
            check(name + '...and the mark is gone when the finger lifts', (await page.evaluate(() => document.querySelectorAll('.gc-line.gm-on').length)) === 0);
        }

        // the keyboard
        await page.evaluate(() => { document.querySelector('#gchat .gc-log').focus(); });
        await page.keyboard.press('ArrowUp'); await sleep(80);
        const first = await page.evaluate(() => [...document.querySelectorAll('.gc-line.gm-on')].map((r) => r.getAttribute('data-n')).join());
        check(name + 'Up on the log chooses the newest line that has an action, and marks it', first === '152', first);
        await page.keyboard.press('ArrowUp'); await page.keyboard.press('ArrowUp'); await sleep(80);
        check(name + 'Up again moves up over the lines that have an action (151, 150), Down comes back', (await page.evaluate(() => document.querySelector('.gc-line.gm-on').getAttribute('data-n'))) === '150' && (await (async () => { await page.keyboard.press('ArrowDown'); return page.evaluate(() => document.querySelector('.gc-line.gm-on').getAttribute('data-n')); })()) === '151');
        for (let i = 0; i < 3; i++) await page.keyboard.press('ArrowUp');
        await sleep(80);
        check(name + '...the chosen line is scrolled into view (a low window may hold only a part of a line)', await page.evaluate(() => { const l = document.querySelector('#gchat .gc-log').getBoundingClientRect(), r = document.querySelector('.gc-line.gm-on').getBoundingClientRect(); return (r.top >= l.top - 1 && r.bottom <= l.bottom + 1) || (l.height < r.height + 4 && r.bottom > l.top && r.top < l.bottom); }));
        const status = await page.evaluate(() => [...document.querySelectorAll('#gchat [role="status"].gc-news')].map((e) => e.textContent).filter((t) => /Enter/.test(t)).join('|'));
        check(name + 'a hidden status line says which line is chosen and how to open it', /Enter|menu key/.test(status) && status.length > 20, status);
        await shot(page, '1_log_line_chosen_by_keys', size);
        await page.keyboard.press('Escape'); await sleep(80);
        check(name + 'Escape lets the chosen line go', (await page.evaluate(() => document.querySelectorAll('.gc-line.gm-on').length)) === 0);
        for (const [key, label] of [['Enter', 'Enter'], ['ContextMenu', 'the menu key'], ['Shift+F10', 'Shift and F10']]) {
            await page.evaluate(() => { document.querySelector('#gchat .gc-log').focus(); });
            if (process.env.MODDEBUG) console.log('   focus(): ' + JSON.stringify(await page.evaluate(() => { const l = document.querySelector('#gchat .gc-log'); l.focus(); return [document.activeElement === l, l.tabIndex, document.hasFocus(), !!l.closest('[inert]'), l.getBoundingClientRect().height, getComputedStyle(l).display, document.querySelectorAll('[inert]').length, !!document.querySelector('.gm-shade')]; })));
            if (process.env.MODDEBUG) console.log('   before ' + label + ': focus ' + (await page.evaluate(() => { const a = document.activeElement; return a.className || a.tagName; })));
            await page.keyboard.press('ArrowUp'); await sleep(60);
            if (process.env.MODDEBUG) console.log('   after Up: focus ' + (await page.evaluate(() => { const a = document.activeElement; return a.className || a.tagName; })) + ' chosen ' + (await page.evaluate(() => [...document.querySelectorAll('.gc-line.gm-on')].map((r) => r.getAttribute('data-n')).join())));
            await page.keyboard.press(key); await sleep(120);
            if (process.env.MODDEBUG) console.log('   ' + label + ': sheet ' + (await sheetUp(page)) + ', focus ' + (await focused(page)) + ', chosen ' + (await page.evaluate(() => [...document.querySelectorAll('.gc-line.gm-on')].map((r) => r.getAttribute('data-n')).join())));
            check(name + label + ' opens the sheet of the chosen line, which has the focus on its first action; Escape puts the focus back on the log', (await sheetUp(page)) && (await focused(page)) === 'Delete this line' && await (async () => { await closeIt(page); return page.evaluate(() => document.activeElement === document.querySelector('#gchat .gc-log')); })());
        }

        // the mute step: four times and the custom field
        await openIt(context, page, touch, 148);
        check(name + 'after the keys, a ' + (touch ? 'long press' : 'right click') + ' on another line marks that line alone (the keys\' line is let go)', (await page.evaluate(() => [...document.querySelectorAll('.gc-line.gm-on')].map((r) => r.getAttribute('data-n')).join())) === '148');
        await clickBy(page, 'Mute Clover'); await sleep(120);
        const mute = await page.evaluate(() => ({ title: document.getElementById('gm-title').textContent, buttons: [...document.querySelectorAll('.gm-box button')].map((b) => b.textContent), input: !!document.querySelector('.gm-box input'), go: document.querySelector('.gm-go').disabled, minHeight: Math.min(...[...document.querySelectorAll('.gm-time')].map((b) => b.getBoundingClientRect().height)) }));
        check(name + 'the mute step: "Mute Clover - for how long?", 10 min, 1 hour, 1 day, 1 week, the custom field, its Mute (off), Back', mute.title === 'Mute Clover — for how long?' && mute.buttons.join('|') === '10 min|1 hour|1 day|1 week|Mute|Back' && mute.input && mute.go, JSON.stringify(mute));
        check(name + 'the four times are at least 48 px high', mute.minHeight >= 48, String(mute.minHeight));
        check(name + 'the first time has the focus', (await focused(page)) === '10 min');
        await shot(page, '3_mute_time', size);
        await page.fill('.gm-box input', '90'); await sleep(80);
        const typed = await page.evaluate(() => ({ go: document.querySelector('.gm-go').disabled, hint: document.querySelector('.gm-hint').textContent, invalid: document.querySelector('.gm-box input').getAttribute('aria-invalid') }));
        check(name + 'a typed 90: Mute is on and the hint says "90 minutes = 1 hour 30 minutes"', typed.go === false && typed.hint === '90 minutes = 1 hour 30 minutes' && typed.invalid === 'false', JSON.stringify(typed));
        await shot(page, '3b_mute_custom_typed', size);
        for (const [text, good] of [['0', false], ['10081', false], ['20000', false], ['1', true], ['10080', true], ['ten', false], ['-5', false], ['1.5', false]]) {
            await page.fill('.gm-box input', text); await sleep(40);
            const s = await page.evaluate(() => ({ go: document.querySelector('.gm-go').disabled, hint: document.querySelector('.gm-hint').textContent }));
            check(name + 'typed "' + text + '": Mute is ' + (good ? 'on' : 'off') + (good ? '' : ' and the hint says the most'), s.go === !good && (good || /The most is 10,080 minutes/.test(s.hint)), JSON.stringify(s));
        }
        await page.fill('.gm-box input', '20000'); await sleep(60);
        await shot(page, '3c_mute_custom_too_long', size);
        await page.fill('.gm-box input', '');
        const before = said().length;
        await page.locator('.gm-go').click({ force: true, timeout: 800 }).catch(() => {});
        check(name + 'a press on the off Mute button sends nothing', said().length === before);
        await page.fill('.gm-box input', '45'); await page.keyboard.press('Enter'); await sleep(450);
        check(name + 'a typed 45 and Enter send `/mute #148 45`', said()[said().length - 1] === '/mute #148 45', JSON.stringify(said()));
        check(name + 'the sheet is gone, the answer is a toast AT THE TOP and a private line with the same words', !(await sheetUp(page)) && (await toastWords(page)) === 'Done: Clover is muted for 45 minutes.' && (await privates(page)).pop() === 'Done: Clover is muted for 45 minutes.', await toastWords(page));
        const toast = await page.evaluate(() => { const t = document.querySelector('.gm-toast').getBoundingClientRect(), bar = document.querySelector('.hbar'), b = bar ? bar.getBoundingClientRect() : null, f = document.querySelector('#gchat .gc-form'), fr = f && f.getClientRects().length ? f.getBoundingClientRect() : null; return { top: Math.round(t.top), bottom: Math.round(t.bottom), left: Math.round(t.left), right: Math.round(t.right), bar: b ? Math.round(b.bottom) : null, form: fr ? Math.round(fr.top) : null, role: document.querySelector('.gm-toast').getAttribute('role'), focus: document.activeElement.closest('.gm-toast') !== null, w: window.innerWidth, h: window.innerHeight }; });
        check(name + 'the toast is at the top, under the bar, inside the window, over no composer, a polite status that took no focus', toast.top >= 0 && toast.top <= 140 && (toast.bar === null || toast.top >= toast.bar) && toast.left >= 0 && toast.right <= toast.w && (toast.form === null || toast.bottom < toast.form) && toast.role === 'status' && !toast.focus, JSON.stringify(toast));
        await shot(page, '5_toast_done', size);
        await sleep(6300);
        check(name + 'the toast goes by itself after 6 seconds', (await toastWords(page)) === null);

        // the times: a tap acts at once; the confirm cards
        await openIt(context, page, touch, 149);
        await clickBy(page, 'Mute Juniper'); await clickBy(page, '1 hour'); await sleep(450);
        check(name + 'a tap on "1 hour" sends `/mute #149 60` at once and the toast says it', said()[said().length - 1] === '/mute #149 60' && (await toastWords(page)) === 'Done: Juniper is muted for 1 hour.', JSON.stringify(said().slice(-2)));
        for (const [verb, who, cmd, title, words, red, purge] of [['Kick Clover', 148, '/kick #148', 'Kick Clover?', 'Done: Clover was kicked.', 'Kick'], ['Ban Juniper', 142, '/ban #142', 'Ban Juniper?', 'Done: Juniper is banned.', 'Ban'],
            ['Ban Fern', 144, '/ban #144 purge', 'Ban Fern?', 'Done: Fern is banned and the lines of the last hour are gone.', 'Ban', true], ['Delete this line', 148, '/del 148', 'Delete this line?', 'Done: the line is gone.', 'Delete']]) {
            await openIt(context, page, touch, who);
            await clickBy(page, verb); await sleep(100);
            const card = await page.evaluate(() => ({ title: document.getElementById('gm-title').textContent, buttons: [...document.querySelectorAll('.gm-box button')].map((b) => b.textContent), box: document.querySelector('.gm-box').className }));
            check(name + verb + ' asks once more: "' + title + '", Cancel has the focus', card.title === title && /gm-conf/.test(card.box) && (await focused(page)) === 'Cancel', JSON.stringify(card));
            const sent = said().length;
            await pressBy(page, purge ? 'Ban and delete their lines from the last hour' : red); await sleep(100);
            check(name + 'the red button takes no press for the first 400 ms: nothing sent', said().length === sent && await sheetUp(page));
            if (verb === 'Kick Clover') await shot(page, '4a_confirm_kick', size);
            if (verb === 'Ban Juniper') {
                check(name + 'the Ban card has the second red button "Ban and delete their lines from the last hour"', card.buttons.join('|') === 'Cancel|Ban|Ban and delete their lines from the last hour', card.buttons.join('|'));
                await shot(page, '4b_confirm_ban', size);
            }
            if (verb === 'Delete this line') await shot(page, '4c_confirm_delete', size);
            await sleep(400);
            await pressBy(page, purge ? 'Ban and delete their lines from the last hour' : red); await sleep(450);
            check(name + 'then `' + cmd + '` goes, once, and the toast says so', said()[said().length - 1] === cmd && said().filter((c) => c === cmd).length === 1 && (await toastWords(page)) === words && !(await sheetUp(page)), JSON.stringify(said().slice(-2)) + ' / ' + (await toastWords(page)));
            if (verb === 'Delete this line') check(name + '...the line is gone from the log', (await page.locator(row(148)).count()) === 0);
            if (purge) check(name + '...the lines of that person are gone from the log (the hub took them off)', (await page.locator(row(144)).count()) === 0 && (await page.locator(row(150)).count()) === 0);
        }
        // Cancel sends nothing; a refusal stays with OK
        await openIt(context, page, touch, 142);
        await clickBy(page, 'Kick Juniper'); await sleep(100);
        const sentBefore = said().length;
        await clickBy(page, 'Cancel'); await sleep(100);
        check(name + 'Cancel on a card sends nothing and closes it', said().length === sentBefore && !(await sheetUp(page)));
        await openIt(context, page, touch, 151);
        await clickBy(page, 'Kick Nobody'); await sleep(450); await pressBy(page, 'Kick'); await sleep(450);
        const refused = await page.evaluate(() => { const t = document.querySelector('.gm-toast'); return t ? { words: t.querySelector('.gm-tt').textContent, ok: !!t.querySelector('button'), bad: t.classList.contains('gm-bad') } : null; });
        check(name + 'a refusal of the hub is a toast in words with an OK button, and the same words are a private line', !!refused && refused.words === 'Nobody is not on the front page now, so there is nobody to kick. Mute or ban instead.' && refused.ok && refused.bad && (await privates(page)).pop() === refused.words, JSON.stringify(refused));
        await shot(page, '5b_toast_refused', size);
        await sleep(6500);
        check(name + '...it stays after 6 seconds', (await toastWords(page)) !== null);
        await page.locator('.gm-toast button').click(); await sleep(80);
        check(name + '...and OK takes it away', (await toastWords(page)) === null);
        await context.close();
    }

    // ---- staff: a moderator ----
    for (const [size, w, h, touch] of SIZES) {
        const name = size + ' (a moderator): ';
        const { page, context } = await open(rigs.mod, size, w, h, touch, { staff: true });
        const marked = await page.evaluate(() => [...document.querySelectorAll('#gchat .gc-line[data-act]')].map((r) => Number(r.getAttribute('data-n'))));
        check(name + 'the lines with an action: not an administrator\'s, not a moderator\'s, not the hub\'s own', marked.join() === '141,142,144,145,148,149,150,151,152', marked.join());
        await openIt(context, page, touch, 148);
        check(name + 'the sheet offers Delete, Mute and Kick, no Ban', JSON.stringify(await names(page)) === JSON.stringify(['Delete this line', 'Mute Clover…', 'Kick Clover', 'Cancel']), JSON.stringify(await names(page)));
        await shot(page, '2b_line_sheet_moderator_no_ban', size);
        await closeIt(page);
        await openIt(context, page, touch, 152);
        check(name + 'on the line of one\'s own: Delete only', JSON.stringify(await names(page)) === JSON.stringify(['Delete this line', 'Cancel']), JSON.stringify(await names(page)));
        await shot(page, '2c_line_sheet_own_line_delete_only', size);
        await closeIt(page);
        await page.locator(row(143)).scrollIntoViewIfNeeded();
        await page.evaluate(() => { window.__ctx.length = 0; });
        if (touch) { const t = await hold(context, page, 143, 650); await t.end(); } else await openByRightClick(page, 143);
        check(name + way(touch) + ' on a moderator\'s line opens nothing', !(await sheetUp(page)) && (touch || (await page.evaluate(() => window.__ctx.every((x) => x === false)))));
        await context.close();
    }

    // ---- a person with no role ----
    for (const [size, w, h, touch] of SIZES) {
        const name = size + ' (no role): ';
        const { page, context } = await open(rigs.player, size, w, h, touch, { staff: false });
        const rq = rigs.player.site.requests;
        check(name + 'the sheet is never asked for', rq.indexOf('/home/mod.js') === -1 && rq.indexOf('/home/mod.css') === -1 && rq.indexOf('/home/chat.js') !== -1, rq.filter((r) => /home/.test(r)).join());
        check(name + 'no line is marked, nothing describes the log', await page.evaluate(() => !document.querySelector('[data-act]') && !document.querySelector('[aria-description]') && !document.querySelector('#gchat .gc-log').getAttribute('aria-describedby')));
        check(name + 'the lines are as wide as an administrator\'s (no button takes room from them)', (await widthOf(page, 145)) > 0 && (await widthOf(page, 148)) === wide[size], (await widthOf(page, 148)) + ' against ' + wide[size]);
        const user = await page.evaluate((s) => getComputedStyle(document.querySelector(s)).userSelect, row(145));
        check(name + 'the text of a line is selectable', user !== 'none', user);
        await page.evaluate(() => { window.__ctx.length = 0; });
        await page.locator(row(145)).scrollIntoViewIfNeeded();
        if (touch) { const t = await hold(context, page, 145, 700); await t.end(); await sleep(150); } else await openByRightClick(page, 145);
        check(name + (touch ? 'a long press' : 'a right click') + ' on a line is the browser\'s own: no sheet, nothing held back', !(await sheetUp(page)) && (await page.evaluate(() => window.__buzz.length === 0 && window.__ctx.every((x) => x === false))));
        await page.evaluate(() => { document.querySelector('#gchat .gc-log').focus(); });
        await page.keyboard.press('ArrowUp'); await page.keyboard.press('Enter'); await sleep(100);
        check(name + 'Up and Enter in the log do nothing', !(await sheetUp(page)) && (await page.evaluate(() => !document.querySelector('.gc-line.gm-on'))));
        await shot(page, '6_chat_log_player', size);
        await context.close();
    }

    // ---- forced colors: the marked line has an outline ----
    {
        const { page, context } = await open(rigs.admin, 'desktop', 1280, 800, false, { staff: true, forced: true });
        await page.evaluate(() => { document.querySelector('#gchat .gc-log').focus(); });
        await page.keyboard.press('ArrowUp'); await sleep(100);
        const outline = await page.evaluate(() => { const r = document.querySelector('.gc-line.gm-on'); const s = getComputedStyle(r); return [s.outlineStyle, s.outlineWidth]; });
        check('forced colors: the chosen line has a 2 px outline', outline[0] === 'solid' && outline[1] === '2px', JSON.stringify(outline));
        await openIt(context, page, false, 148);
        const edge = await page.evaluate(() => { const b = document.querySelector('.gm-act'); return getComputedStyle(b).borderTopColor; });
        check('forced colors: the sheet opens and its buttons keep a border', (await sheetUp(page)) && edge !== 'rgba(0, 0, 0, 0)', edge);
        await context.close();
    }

    check('the page\'s own errors are none', errors.length === 0, errors.join(' | '));
    await browser.close();
    for (const k of Object.keys(rigs)) { await rigs[k].hub.close(); rigs[k].site.server.close(); }
    console.log(failures ? 'FAILED: ' + failures + ' of ' + checks + ' checks' : 'OK: ' + checks + ' checks');
    process.exit(failures ? 1 : 0);
}
main().catch((e) => { console.log('FAIL ' + (e && e.stack || e)); process.exit(1); });
