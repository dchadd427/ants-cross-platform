// OPT-IN, not part of ./run_tests.sh: the chat dock of Home in a REAL browser (Chromium through Playwright), against the real front page (web/lobby.html with web/home/) and the FAKE hub (tests/scripts/fake_hub.js)
// that shares the page's address, as nginx's /hub/ws does. It looks at what no fake DOM can: the places and sizes of the dock at 390 x 844 (a phone), 844 x 390 (a phone held sideways) and 1280 x 800 (a desktop) next to
// the approved pictures, that nothing scrolls sideways, that every target is 44 px, that the dock covers none of the bar, that the dock comes after the first paint, that the page asks for its two files and for
// nothing else, that no other page of the site opens a hub socket, the real WebSocket against the fake hub (read only, a writer, the hub's pace, hostile text, a hub that goes and comes back, a hub that is not
// there, a site with no /hub/ws), and the Room bar's logo at 320 to 389 px.
//
//   node tests/scripts/web_home_chat_browser.js [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). --shots DIR saves a picture of each state as dock7_<scene>_<phone|side|desktop>.png in DIR.
// Exit status: 0 every check passed (or the check could not be made), 1 a check failed or the page broke.
'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');
const { startHub, line } = require('./fake_hub.js');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the chat dock was NOT run in a browser'); process.exit(0); }

const WEB = path.resolve(__dirname, '..', '..', 'web');
const shotsAt = process.argv.indexOf('--shots') !== -1 ? process.argv[process.argv.indexOf('--shots') + 1] : null;
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json' };
const SIZES = { phone: [390, 844], side: [844, 390], desktop: [1280, 800] };
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function serve() {                                       // the front page and its files, as the site's nginx gives them (/ is lobby.html), and nothing else; the hub's door is the fake hub's
    const requests = [];
    const server = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        requests.push(url.pathname);
        const file = url.pathname === '/' ? 'lobby.html' : url.pathname.replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'application/json' }); res.end('{"error":"not here"}'); return; }
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
        res.end(fs.readFileSync(full));
    });
    return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({ server, requests, url: 'http://127.0.0.1:' + server.address().port })));
}

const HISTORY = [line(1, 'pebble', 'anyone up for Islands? two seats left'), line(2, 'clover', 'I have a room open, Treasure, one bot'), line(3, 'juniper', 'joining Clover!')];
const rect = (page, selector) => page.evaluate((s) => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height), right: Math.round(r.right), bottom: Math.round(r.bottom) }; }, selector);
const shot = async (page, scene, size) => { if (shotsAt) { fs.mkdirSync(shotsAt, { recursive: true }); await page.screenshot({ path: path.join(shotsAt, 'dock7_' + scene + '_' + size + '.png') }); } };

async function main() {
    const site = await serve();
    const hub = await startHub({ server: site.server, login: false, you: null, history: HISTORY, online: 7 });
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the chat dock was NOT run in a browser'); await hub.close(); site.server.close(); process.exit(0); }
    const errors = [];
    async function open(size, query, o) {
        const context = await browser.newContext({ viewport: { width: SIZES[size] ? SIZES[size][0] : size[0], height: SIZES[size] ? SIZES[size][1] : size[1] }, deviceScaleFactor: 1 });
        const page = await context.newPage();
        await page.addInitScript(() => { window.__frame = 0; requestAnimationFrame(() => { window.__frame = performance.now(); }); });       // (when the page's first frame began: the chat's files are asked for after it)
        page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
        await page.goto(site.url + (query || '/'), { waitUntil: 'load' });
        if (!o || o.wait !== false) await page.waitForSelector('#gchat', { timeout: 5000 }).catch(() => {});
        return { page, context };
    }
    const dockText = (page, selector) => page.evaluate((s) => { const e = document.querySelector('#gchat ' + s); return e ? e.innerText : null; }, selector);
    const state = (page) => page.evaluate(() => { const d = document.getElementById('gchat'); return d ? d.getAttribute('data-state') + '/' + d.getAttribute('data-open') : null; });
    const waitState = async (page, want, ms) => { const end = Date.now() + (ms || 6000); while (Date.now() < end) { if ((await state(page)) === want) return true; await sleep(50); } return (await state(page)) === want; };
    const noSideways = (page) => page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth && document.body.scrollWidth <= window.innerWidth);
    const targets = (page) => page.evaluate(() => [...document.querySelectorAll('#gchat button, #gchat input, #gchat a, #gchat [tabindex]')].filter((e) => e.offsetParent !== null || e.getClientRects().length).map((e) => { const r = e.getBoundingClientRect(); return { tag: e.tagName.toLowerCase() + '.' + e.className, w: Math.round(r.width), h: Math.round(r.height) }; }));

    // ---- the three sizes: read only (the approved x_home / x_chat_out), a writer (x_chat_in), folded (x_collapsed), offline ----
    for (const size of Object.keys(SIZES)) {
        const [W, Hh] = SIZES[size];
        hub.set({ login: false, you: null, mode: 'normal', online: 7 });
        const { page, context } = await open(size);
        check(size + ': the dock is mounted on Home by the page itself, after its two files came (home/chat.js, home/chat.css)', (await state(page)) !== null && site.requests.indexOf('/home/chat.js') !== -1 && site.requests.indexOf('/home/chat.css') !== -1);
        check(size + ': it welcomes with the history and says that the chat is read-only (no field, no Send, no login button)', await waitState(page, size === 'desktop' ? 'live/true' : 'live/false')
              && (await page.evaluate(() => document.querySelector('#gchat .gc-form').hidden && document.querySelector('#gchat-login-slot').hidden && document.querySelectorAll('#gchat button').length === 2)));
        const d = await rect(page, '#gchat');
        if (size === 'desktop') {
            check('desktop: the panel is 380 x 380 in the lower-left corner (the approved picture: 24 px from the left, 22 px from the bottom)', d.w === 380 && d.h === 380 && d.x === 24 && Hh - d.bottom >= 18 && Hh - d.bottom <= 26, JSON.stringify(d));
            await shot(page, 'x_chat_out', size);
            await shot(page, 'x_home', size);
            hub.set({ you: { name: 'maple.leaf', mod: false }, login: true });
            hub.drop();
            check('desktop: a hub that now names the visitor: after the retry the field and Send are there', await waitState(page, 'live/true', 8000) && (await page.evaluate(() => !document.querySelector('#gchat .gc-form').hidden)));
            hub.say('maple.leaf', 'quick join works nice');
            await sleep(150);
            await shot(page, 'x_chat_in', size);
            await page.click('#gchat .gc-fold');
            const f = await rect(page, '#gchat');
            check('desktop: the minus button folds the panel to its header, 380 wide and about 58 high, bottom edge where the panel was', f.w === 380 && f.h >= 54 && f.h <= 68 && f.bottom === d.bottom, JSON.stringify(f));
            check('... the header says Global chat and how many are here, and the button says it opens', (await dockText(page, '.gc-title')).indexOf('Global chat') !== -1 && /here now/.test(await dockText(page, '.gc-online')) && (await page.getAttribute('#gchat .gc-fold', 'aria-label')) === 'Open chat');
            await shot(page, 'x_collapsed', size);
        } else {
            check(size + ': it starts as a strip along the bottom, 62 px high, 16 px from both sides (the approved strip)', d.h === 62 && d.x === 16 && W - d.right === 16 && Hh - d.bottom >= 8 && Hh - d.bottom <= 16, JSON.stringify(d));
            check(size + ': the strip says "Chat" (the long title is clipped away) and shows the last line, not the history as unread', (await dockText(page, '.gc-short')) === 'Chat' && (await rect(page, '.gc-long')).w <= 1 && /juniper: joining Clover!/.test(await dockText(page, '.gc-peek')) && (await page.evaluate(() => document.querySelector('#gchat .gc-unread').hidden)));
            await shot(page, 'x_collapsed', size);
            await page.click('#gchat .gc-head');
            check(size + ': a press on the strip opens the panel upward', await waitState(page, 'live/true'));
            const o = await rect(page, '#gchat');
            if (size === 'phone') check('phone: the open panel is 490 px high, between 10 and 16 px from the sides (the approved panel)', o.h === 490 && o.x >= 10 && o.x <= 16 && W - o.right >= 10 && W - o.right <= 16 && o.y >= 190, JSON.stringify(o));
            else check('side: the open panel is 420 x 286 at the left (the approved panel)', o.w === 420 && o.h === 286 && o.x >= 12 && o.x <= 16 && Hh - o.bottom >= 8 && Hh - o.bottom <= 16, JSON.stringify(o));
            await shot(page, 'x_chat_out', size);
            hub.set({ you: { name: 'maple.leaf', mod: false }, login: true });
            hub.drop();
            check(size + ': a hub that names the visitor after a retry: the field and Send are in the open panel', await waitState(page, 'live/true', 8000));
            hub.say('maple.leaf', 'quick join works nice');
            await sleep(150);
            await shot(page, 'x_chat_in', size);
            await page.focus('#gchat .gc-log');
            await page.keyboard.press('Escape');
            check(size + ': Escape inside the panel folds it to the strip and puts the focus on its button', (await state(page)) === 'live/false' && (await page.evaluate(() => document.activeElement && document.activeElement.className.indexOf('gc-fold') !== -1)));
        }
        check(size + ': nothing scrolls sideways', await noSideways(page));
        // the targets (an open dock for the phone and the side)
        if (size !== 'desktop') await page.click('#gchat .gc-head');
        else if ((await state(page)) === 'live/false') await page.click('#gchat .gc-fold');
        const t = await targets(page);
        check(size + ': every button and field of the dock is at least 44 px high and wide, the field and Send 48 (' + t.map((x) => x.tag.split(' ')[1] + ' ' + x.w + 'x' + x.h).join(', ') + ')', t.length >= 4 && t.every((x) => x.h >= 44 && (x.w >= 44 || /gc-log/.test(x.tag))) && t.filter((x) => /gc-(input|send)/.test(x.tag)).every((x) => x.h >= 48), JSON.stringify(t));
        // the dock covers none of the bar
        const covered = await page.evaluate(() => [...document.querySelectorAll('.hbar .btn, .hbar .hmenu')].filter((b) => b.getClientRects().length).filter((b) => { const r = b.getBoundingClientRect(); const e = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2); return !(e === b || b.contains(e)); }).length);
        check(size + ': no button of the top bar is under the dock', covered === 0);
        const inside = await rect(page, '#gchat');
        check(size + ': the dock is wholly inside the window', inside.x >= 0 && inside.y >= 0 && inside.right <= W && inside.bottom <= Hh, JSON.stringify(inside));
        await sleep(400);       // (the log is quiet for 300 ms after it is made again: a screen reader is not read all of it)
        check(size + ': one h1 on the screen (the logo), the dock\'s heading is an h2, the log is role=log aria-live=polite', (await page.evaluate(() => [...document.querySelectorAll('h1')].filter((h) => h.getClientRects().length).length)) === 1
              && (await page.evaluate(() => document.querySelector('#gchat h2').tagName === 'H2' && document.querySelector('#gchat .gc-log').getAttribute('role') === 'log' && document.querySelector('#gchat .gc-log').getAttribute('aria-live') === 'polite')));
        await context.close();
    }

    // ---- unread on the strip and on the header, a moderator's line, a name that asks for the login ----
    for (const size of Object.keys(SIZES)) {
        hub.set({ login: true, you: null, mode: 'normal', online: 12, history: HISTORY });
        const { page, context } = await open(size);
        await waitState(page, size === 'desktop' ? 'live/true' : 'live/false');
        check(size + ': a hub with a login and nobody named: "Log in with Discord to chat" (and still no button, no field)', (await page.evaluate(() => document.querySelector('#gchat .gc-form').hidden && document.querySelectorAll('#gchat button').length === 2)) && (size !== 'desktop' ? true : (await dockText(page, '.gc-note')) === 'Log in with Discord to chat'));
        if (size === 'desktop') await page.click('#gchat .gc-fold');
        hub.say('clover', 'new room: Treasure, 3 of 4', true); hub.say('pebble', 'joining!'); hub.say('juniper', 'me too');
        await sleep(250);
        check(size + ': three lines that came while the dock was folded are "3" on the ' + (size === 'desktop' ? 'header' : 'strip') + ' and in the button\'s name', (await dockText(page, '.gc-unread')) === '3' && (await page.getAttribute('#gchat .gc-fold', 'aria-label')) === 'Open chat, 3 unread');
        await shot(page, 'unread', size);
        await page.click(size === 'desktop' ? '#gchat .gc-fold' : '#gchat .gc-head');
        await sleep(150);
        check(size + ': opened, the count is gone and the moderator\'s line carries its mark', (await page.evaluate(() => document.querySelector('#gchat .gc-unread').hidden && !!document.querySelector('#gchat .gc-mod .gc-badge') && document.querySelector('#gchat .gc-mod .gc-badge').innerText.toLowerCase() === 'mod')));
        await shot(page, 'mod', size);
        await context.close();
    }

    // ---- both strips above Home (a notice and the Rejoin button): the dock keeps under Home's bar: the open panel when there is room for it (never a sliver), the strip when there is not, nothing when not even that fits; it covers no
    // button of the bar at any size, and the "Opening the game" card is above it ----
    for (const [W, Hh, name] of [[320, 568, 'phone_small'], [360, 640, 'phone_360'], [390, 844, 'phone'], [568, 320, 'side_small'], [667, 375, 'side_667'], [844, 390, 'side'], [1280, 800, 'desktop'], [320, 480, 'phone_tiny']]) {
        hub.set({ login: false, you: null, mode: 'normal', online: 7, history: HISTORY });
        const context = await browser.newContext({ viewport: { width: W, height: Hh }, deviceScaleFactor: 1 });
        await context.addInitScript((port) => { try { localStorage.setItem('ants.rejoin.abcdef.0', JSON.stringify({ k: '0f1e2d3c4b5a69788796a5b4c3d2e1f0', s: 'ws://127.0.0.1:' + port + '/ws', t: Date.now() })); } catch (e) { /* no storage */ } }, hub.port);
        const page = await context.newPage();
        page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
        await page.goto(site.url + '/', { waitUntil: 'load' });
        await page.waitForSelector('#gchat', { timeout: 5000 });
        await page.evaluate(() => document.querySelector('.hbar .mine').click());       // (My room: the game server is not there: the notice strip)
        await sleep(900);
        await page.evaluate(() => document.querySelector('.rbar .rhome').click());
        await sleep(200);
        if ((await state(page)) === 'live/false' || (await state(page)) === 'offline/false' || (await state(page)) === 'connecting/false') await page.evaluate(() => document.querySelector('#gchat .gc-head').click());
        await sleep(300);
        const m = await page.evaluate(() => {
            const r = (sel) => { const e = document.querySelector(sel); if (!e || !e.getClientRects().length) return null; const q = e.getBoundingClientRect(); return { left: Math.round(q.left), right: Math.round(q.right), top: Math.round(q.top), bottom: Math.round(q.bottom), height: Math.round(q.height) }; };
            const dock = r('#gchat'), buttons = [...document.querySelectorAll('.hbar button')].map((b) => b.getBoundingClientRect()).filter((q) => q.width > 0 && q.height > 0);
            const covered = dock ? buttons.filter((q) => q.left < dock.right && q.right > dock.left && q.top < dock.bottom && q.bottom > dock.top).length : 0;
            const d = document.getElementById('gchat');
            return { banner: r('#banner'), rejoin: r('#rejoin'), home: r('#home'), bar: r('.hbar'), dock, covered, open: d ? d.getAttribute('data-open') : null, hidden: d ? d.hidden : null, least: d ? parseFloat(getComputedStyle(d).getPropertyValue('--gc-min')) || 0 : 0 };
        });
        check(name + ' (' + W + ' x ' + Hh + ') with both strips above Home: the dock covers no button of the bar (' + (m.dock ? (m.open === 'true' ? 'the panel, ' : 'the strip, ') + m.dock.height + ' px high' : 'not shown') + ')', !!m.banner && !!m.rejoin && m.covered === 0, JSON.stringify(m));
        check(name + ': ... an open panel is never a sliver (at least ' + m.least + ' px), a window with no room for it shows the strip (62 px) and one with no room for that shows nothing', m.dock === null ? m.hidden === true : m.open === 'true' ? m.dock.height >= m.least : m.dock.height <= 62 + 2, JSON.stringify(m));
        if (name !== 'desktop') check(name + ': ... and the dock never starts above the end of the bar (top ' + (m.dock ? m.dock.top : '-') + ', the bar ends at ' + m.bar.bottom + ')', m.dock === null || m.dock.top >= m.bar.bottom - 1, JSON.stringify(m));
        if (name === 'phone_small' || name === 'side_small') check(name + ': a window with these two notices has no room for the open panel (it needs ' + m.least + ' px and 12 under them): the strip stands in, and the wish to open it is kept', m.dock !== null && m.open === 'false' && m.dock.height <= 64, JSON.stringify(m));
        if (name === 'phone_tiny') check(name + ': a window with nothing under the bar shows no dock at all', m.dock === null && m.hidden === true, JSON.stringify(m));
        if (name === 'phone' || name === 'side' || name === 'desktop') check(name + ': a window with room has the open panel', m.open === 'true' && m.dock !== null, JSON.stringify(m));
        check(name + ': nothing scrolls sideways, and the dock is inside the window', (await noSideways(page)) && (m.dock === null || m.dock.bottom <= Hh));
        if (name === 'phone_small') {                                  // the notices go (the page's own OK and the strip's): the panel that was wished for is back
            await page.evaluate(() => { document.getElementById('rejoin').hidden = true; document.getElementById('banner').hidden = true; });
            await sleep(300);
            const back = await page.evaluate(() => { const d = document.getElementById('gchat'), q = d.getBoundingClientRect(); return { open: d.getAttribute('data-open'), height: Math.round(q.height) }; });
            check(name + ': when the notices are gone the panel is back as the visitor left it, without a press (Home\'s height changed, not the window\'s)', back.open === 'true' && back.height >= 200, JSON.stringify(back));
        }
        await shot(page, 'strips', name);
        await context.close();
    }
    {
        const context = await browser.newContext({ viewport: { width: 390, height: 844 } });
        const page = await context.newPage();
        await page.goto(site.url + '/', { waitUntil: 'load' });
        await page.waitForSelector('#gchat');
        await page.evaluate(() => { const c = document.querySelector('.hopen'); c.hidden = false; document.querySelector('#gchat .gc-head').click(); });
        await sleep(200);
        const top = await page.evaluate(() => { const c = document.querySelector('.hopen').getBoundingClientRect(), d = document.querySelector('#gchat').getBoundingClientRect(); const x = Math.round(Math.max(c.left, d.left) + 20), y = Math.round(Math.max(c.top, d.top) + 10); const e = document.elementFromPoint(x, y); return { overlap: c.bottom > d.top && c.top < d.bottom, onTop: !!e && (e.closest('.hopen') !== null) }; });
        check('the card "Opening the game…" is above an open dock where they meet', !top.overlap || top.onTop, JSON.stringify(top));
        await context.close();
    }

    // ---- the page's first paint comes before the chat's files; the page asks for nothing else; nothing is kept in the browser ----
    {
        site.requests.length = 0;
        const { page, context } = await open('desktop');
        const timing = await page.evaluate(() => { const js = performance.getEntriesByType('resource').filter((r) => /home\/chat\.js/.test(r.name))[0]; return { frame: window.__frame, js: js ? js.startTime : null }; });
        check('the chat\'s script is asked for after the first frame of the page (the frame began at ' + Math.round(timing.frame) + ' ms, chat.js was asked for at ' + Math.round(timing.js) + ' ms)', timing.js !== null && timing.frame > 0 && timing.js >= timing.frame);
        check('the page\'s requests to the site that are the chat\'s: its script and style only (no other file, no other site)', site.requests.filter((r) => /^\/home\/chat/.test(r)).sort().join() === '/home/chat.css,/home/chat.js');
        const kept = await page.evaluate(() => JSON.stringify([Object.keys(localStorage), Object.keys(sessionStorage)]));
        check('nothing of the chat is in local or session storage (' + kept + ')', !/chat|hub/i.test(kept));
        check('no page error', errors.length === 0, errors.join(' | '));
        await context.close();
    }

    // ---- a writer, the hub's pace, hostile text, on the real socket in a real browser ----
    {
        hub.set({ you: { name: 'maple.leaf', mod: false }, login: true, mode: 'normal', history: HISTORY });
        const { page, context } = await open('desktop');
        await waitState(page, 'live/true');
        hub.received.length = 0;
        for (const text of ['one', 'two', 'three', 'four', 'five']) { await page.fill('#gchat .gc-input', text); await page.press('#gchat .gc-input', 'Enter'); }
        await sleep(300);
        check('five lines typed and sent at once with Enter: two reach the hub, the hub never says "rate", the others wait with "Slow down a little" and stay in the field', hub.received.filter((m) => m.t === 'say').length === 2 && (hub.rateRefusals || 0) === 0
              && (await dockText(page, '.gc-note')) === 'Slow down a little' && (await page.inputValue('#gchat .gc-input')) === 'five');
        check('the lines come back from the hub and are the visitor\'s own (gold name)', (await page.evaluate(() => document.querySelectorAll('#gchat .gc-me').length)) === 2);
        hub.say('<b>evil</b>', '<img src=x onerror="window.pwned=1"><script>window.pwned=1</script> ‮ rtl javascript:alert(1) http://evil.example');
        await sleep(200);
        const hostile = await page.evaluate(() => ({ pwned: window.pwned || 0, imgs: document.querySelectorAll('#gchat img, #gchat script, #gchat a, #gchat b, #gchat iframe').length, last: document.querySelector('#gchat .gc-log').lastElementChild.innerText }));
        check('hostile text in a real browser is text: no element made from it, nothing ran, no link', hostile.pwned === 0 && hostile.imgs === 0 && hostile.last.indexOf('<b>evil</b>: <img src=x') === 0, JSON.stringify(hostile));
        for (let i = 0; i < 300; i++) hub.say('u' + (i % 5), 'burst ' + i);
        await sleep(400);
        check('a burst of 300 lines leaves 100 in the page', (await page.evaluate(() => document.querySelectorAll('#gchat .gc-line').length)) === 100);
        await page.evaluate(() => { Object.defineProperty(document, 'hidden', { configurable: true, get: () => true }); document.dispatchEvent(new Event('visibilitychange')); });
        const before = await page.evaluate(() => document.querySelectorAll('#gchat .gc-line').length + '|' + document.querySelector('#gchat .gc-log').lastElementChild.innerText);
        hub.say('later', 'while the tab is hidden');
        await sleep(200);
        const during = await page.evaluate(() => document.querySelectorAll('#gchat .gc-line').length + '|' + document.querySelector('#gchat .gc-log').lastElementChild.innerText);
        await page.evaluate(() => { Object.defineProperty(document, 'hidden', { configurable: true, get: () => false }); document.dispatchEvent(new Event('visibilitychange')); });
        await sleep(100);
        const after = await page.evaluate(() => document.querySelector('#gchat .gc-log').lastElementChild.innerText);
        check('a hidden tab is not painted (the log is as it was), and the tab\'s return shows what came', before === during && after === 'later: while the tab is hidden', before + ' / ' + during + ' / ' + after);
        const upgrades = hub.upgrades;
        hub.set({ mode: 'off' });
        hub.drop();
        check('the hub goes away: "Chat is offline" and "reconnecting…"', (await waitState(page, 'offline/true')) && (await dockText(page, '.gc-note')) === 'Chat is offline' && /reconnecting/.test(await dockText(page, '.gc-online')));
        await sleep(1700);
        hub.set({ mode: 'normal' });
        check('... and when it is back (within the backoff) the chat is live again without a reload', await waitState(page, 'live/true', 9000) && hub.upgrades > upgrades);
        check('no page error through all of it', errors.length === 0, errors.join(' | '));
        await context.close();
    }

    // ---- a site with no hub: the page is whole, and says so ----
    {
        hub.set({ mode: 'off' });
        const { page, context } = await open('desktop');
        check('a site whose /hub/ws is answered 503: "Chat is offline", Home is whole (the bar, the Yard), no page error', (await waitState(page, 'offline/true')) && (await dockText(page, '.gc-note')) === 'Chat is offline' && (await page.evaluate(() => !!document.querySelector('.hbar .mine') && !!document.querySelector('#yard'))) && errors.length === 0);
        await shot(page, 'offline', 'desktop');
        await page.click('.hbar .mine');
        check('... and My room still shows the Room (the page\'s own socket is another one)', (await page.getAttribute('body', 'data-screen')) === 'room');
        await page.click('.rbar .rhome');
        check('... and Home again: the dock is still there', (await page.getAttribute('body', 'data-screen')) === 'home' && (await state(page)) !== null);
        await context.close();
        const phone = await open('phone');
        await shot(phone.page, 'offline', 'phone');
        await phone.context.close();
        const side = await open('side');
        await shot(side.page, 'offline', 'side');
        await side.context.close();
        hub.set({ mode: 'normal' });
    }

    // ---- no other page of the site opens a hub socket ----
    {
        const before = hub.upgrades;
        for (const name of ['matches.html', 'live.html', 'legal.html', 'mapmaker.html', 'match.html']) {
            const { page, context } = await open([800, 600], '/' + name, { wait: false });
            await sleep(700);
            check(name + ' opens no hub socket and loads no chat file', (await page.evaluate(() => !document.getElementById('gchat') && !performance.getEntriesByType('resource').some((r) => /chat\.js|hub/.test(r.name)))));
            await context.close();
        }
        check('the hub saw no upgrade from any of them', hub.upgrades === before);
        const room = await open('desktop', '/?room=abcdef', { wait: false });
        await sleep(700);
        check('a link to a room opens the Room: no chat file is asked for and no hub socket is opened until Home is shown', hub.upgrades === before && !(await room.page.evaluate(() => !!document.getElementById('gchat'))));
        await room.context.close();
    }

    // ---- the Room's bar at the narrow widths ----
    {
        for (const width of [320, 339, 340, 360, 389]) {
            const { page, context } = await open([width, 700], '/?room=abcdef', { wait: false });
            await sleep(300);
            await page.evaluate(() => { const b = document.getElementById('name-step-go'); if (b) b.click(); });
            await sleep(300);
            const logo = await page.evaluate(() => { const l = document.querySelector('.rbar .hlogo'); const h = document.querySelector('.rbar h1'); const r = l.getBoundingClientRect(); return { w: Math.round(r.width), alt: l.getAttribute('alt'), h1: h.tagName, sideways: document.documentElement.scrollWidth > window.innerWidth }; });
            check('the Room\'s bar at ' + width + ' px: ' + (width <= 339 ? 'the logo\'s picture is left out (1 px, clipped) and the h1 keeps its name' : 'the logo is drawn (at least 40 px wide)') + ', nothing scrolls sideways', logo.alt === 'Ants!' && logo.h1 === 'H1' && !logo.sideways && (width <= 339 ? logo.w <= 1 : logo.w >= 40), JSON.stringify(logo));
            if (width === 320 || width === 360) { if (shotsAt) { fs.mkdirSync(shotsAt, { recursive: true }); await page.screenshot({ path: path.join(shotsAt, 'dock7_room_bar_' + width + '.png') }); } }
            await context.close();
        }
    }

    await browser.close();
    await hub.close();
    site.server.close();
    check('no page error at all', errors.length === 0, errors.join(' | '));
    console.log('home chat in a browser: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}
main().catch((e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
