// Opening a replay FILE of your own on the website, in a REAL browser (Chromium through Playwright; opt-in, see tests/scripts/test_web_replay_open.sh and docs/REPLAYS.md "A file of your own").
// No Docker image, no game server, no game: this serves the pages of web/ itself (the Matches page, the game page as play.html, their scripts and pictures) behind a small stand-in for the site (GET /history,
// /live, /maps/all, /maps/files/<name>, and nothing else: POST and PUT are refused and counted) and puts a stand-in for the game's program (tests/scripts/web_live_stub.js: Module._ants_replay_get /
// _ants_replay_do and the report that the game makes) in the place of index.js, so what it shows is the pages' own part: the strip, the drop, the cards, the hand-off and the player's header. It does not show
// that the real game plays a file (the C++ tests and the part REAL below do). Every request is counted: none carries a body, none asks the site for the file, none asks for its stats.
// With --web URL (or ANTS_WEB_URL: a running site, the web image or tools/web_without_docker.py) the part REAL runs the same pages with the REAL game against that site (no stand-in: the game says what a file is).
// Parts (--only): strip (the Matches page: the strip, the chooser, a dropped file, a refused file and its card, the page with the site down, a phone), hand (the hand-off: the bytes that the game gets are the
// file's, a reload, a record that is gone, the header of the player, a library map), cards (a file of an older or newer version, a map that the site does not have, an unreadable file, and the way out of each), long (text that cannot wrap and a file name that reads right to left, on a phone and a computer).
// usage: node web_replay_open_browser.js [--only strip|hand|cards|long|real] [--shots DIR] [--web URL] [--require]     (exit 0: every check holds, or the browser is not there; every failure is printed;
// with --require a check that could not be made, for want of Playwright, a browser or a site, is a failure: the CI asks for it)
'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');
const crypto = require('crypto');
const tools = require('./replay_file_tools.js');

const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i === -1 ? null : args[i + 1]; };
const only = opt('--only'), shotsDir = opt('--shots'), webUrl = opt('--web') || process.env.ANTS_WEB_URL || '', required = args.indexOf('--require') !== -1;
const PARTS = ['strip', 'hand', 'cards', 'long', 'real'];
if (only && PARTS.indexOf(only) === -1) { console.log('--only is one of: ' + PARTS.join(', ')); process.exit(2); }
let playwright;
for (const name of ['playwright', 'playwright-core']) { try { playwright = require(name); break; } catch (e) { /* the next one */ } }
if (!playwright) { console.log((required ? 'FAIL' : 'SKIP') + ': Playwright is not installed: the real browser was NOT run (tests/scripts/web_replay_open_browser.js)'); process.exit(required ? 1 : 0); }
const WEB = path.resolve(__dirname, '..', '..', 'web');
const FIXTURE = fs.readFileSync(path.resolve(__dirname, '..', 'data', 'replay_bots_tiny.antsrep'));
const TINY_MAP = fs.readFileSync(path.resolve(__dirname, '..', '..', 'Original-Ants', 'Maps', 'TINY.LVL'));
const FILLED = { '@@SITE_TITLE@@': '', '@@SITE_FOOTER@@': '', '@@GAME_VERSION@@': 'v0.0.0', '@@BUILD_ID@@': '20260101-0000', '@@BUILD_DATE@@': '2026-01-01 00:00 PT', '@@BUILD_TIMESTAMP@@': '0', '@@DATA_SIZE@@': '2000' };

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
        await sleep(50);
    }
}

// ---- the stand-in for the site
const MAPS = ['TINY', 'SMALL', 'MEDIUM', 'GAUNTLET', 'TREASURE', 'ISLANDS'];
function seat(n, color, name, bot, o) { return Object.assign({ seat: n, color, name, bot, score: 0, killed: 0, lost: 0, hatched: 0, bombs_planted: 0, bombs_defused: 0, fires_lit: 0, left: false, winner: false }, o || {}); }
function record(id, ended, map, format, seats, rows, o) {
    return Object.assign({ v: 1, id, ended, map, game: 'v0.0.0', turns: 14400, seconds: 720, finished: true, quitter: null, mode: 'highest-score', format, draw: false, result: 'decided', computers_only: false,
        seats, rows, recording: 'watch', file: id + '.antsrep', keep_days: 30 }, o || {});
}
const T = (d, h, m) => Date.UTC(2026, 9, d, h, m, 0) / 1000;                       // (Oct d, h:m UTC)
function records() {
    const out = [
        record('ants-TREASURE-20261015-173700Z', T(15, 17, 37), 'TREASURE.LVL', '2v2', [seat(0, 'Green', 'Mika', '', { score: 140 }), seat(1, 'Red', 'Bea', '', { score: 1010 }), seat(2, 'Blue', 'Pip', 'Hard', { score: 2040, winner: true }), seat(3, 'Black', 'Rex', 'Hard', { score: 70 })],
               [{ seats: [2], score: 2040, killed: 3, lost: 4, hatched: 0, winner: true }, { seats: [1], score: 1010, killed: 7, lost: 5, hatched: 0, winner: false }, { seats: [0, 3], score: 210, killed: 9, lost: 10, hatched: 1, winner: false }]),
        record('ants-TINY-20261015-172700Z', T(15, 17, 27), 'TINY.LVL', '1v1', [seat(0, 'Green', 'Ann', '', { score: 900, winner: true }), seat(2, 'Blue', 'Cy', 'Easy', { score: 300 })],
               [{ seats: [0], score: 900, killed: 2, lost: 1, hatched: 0, winner: true }, { seats: [2], score: 300, killed: 1, lost: 2, hatched: 0, winner: false }]),
        record('ants-Huge-20261015-172600Z', T(15, 17, 26), 'Huge.lvl', 'ffa4', [0, 1, 2, 3].map((k) => seat(k, ['Green', 'Red', 'Blue', 'Black'][k], 'Bot' + k, 'Hard', { score: 1000 + k, winner: k === 3 })),
               [3, 2, 1, 0].map((k) => ({ seats: [k], score: 1000 + k, killed: k, lost: 5, hatched: 0, winner: k === 3 }))),
    ];
    for (let i = 0; i < 59; i++) {
        const m = MAPS[i % 6], at = T(15, 17, 20) - 600 * (i + 1);
        const seats = [0, 1].map((k) => seat(k, ['Green', 'Red'][k], 'Bot' + k, 'Medium', { score: 1000 + 50 * ((i * 7 + k * 3) % 20), winner: k === 0 }));
        out.push(record('ants-' + m + '-' + new Date(at * 1000).toISOString().replace(/[-:]/g, '').slice(0, 8) + '-' + String(100000 + i) + 'Z', at, m + '.LVL', '1v1', seats,
                        seats.map((s) => ({ seats: [s.seat], score: s.score, killed: 0, lost: 0, hatched: 0, winner: s.winner }))));
    }
    return out;
}
const LIVE = [{ id: 'TREASURE-20261104-190000Z', map: 'TREASURE.LVL', started: 1793818800, turns: 4000, seconds: 200, players: ['Green (Mika)', 'Red (Bot (Medium))'] },
              { id: 'TINY-20261104-190500Z', map: 'TINY.LVL', started: 1793819100, turns: 2000, seconds: 100, players: ['Green (Ann)', 'Blue (Bot (Easy))'] }];
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png', '.jpg': 'image/jpeg', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.lvl': 'application/octet-stream' };

function makeSite(o) {
    o = o || {};
    const site = { hits: [], mode: o.down ? 'down' : 'up', maps: o.maps || {}, history: o.history === undefined ? records() : o.history };
    site.server = http.createServer((req, res) => {
        const chunks = [];
        req.on('data', (c) => chunks.push(c));
        req.on('end', () => {
            const url = new URL(req.url, 'http://x');
            const hit = { method: req.method, path: url.pathname, query: url.search, bodyLength: Buffer.concat(chunks).length, contentLength: req.headers['content-length'] || '' };
            site.hits.push(hit);
            const send = (code, body, type) => { const b = Buffer.isBuffer(body) ? body : Buffer.from(body); res.writeHead(code, { 'Content-Type': type || 'text/plain', 'Content-Length': b.length, 'Cache-Control': 'no-store' }); res.end(b); };
            if (req.method !== 'GET' && req.method !== 'HEAD') return send(405, 'no');
            const p = url.pathname;
            if (site.mode === 'down' && (p === '/history' || p === '/live' || p.startsWith('/maps/') || p.startsWith('/replays'))) return send(502, 'bad gateway');
            if (p === '/history') return send(200, JSON.stringify({ history: site.history, count: site.history.length, total: site.history.length }), 'application/json');
            if (p === '/live') return send(200, JSON.stringify({ live: o.live === undefined ? LIVE : o.live, count: 2, sim_rules: 1 }), 'application/json');
            if (p === '/maps/all') return send(200, JSON.stringify({ maps: [] }), 'application/json');
            if (p.startsWith('/maps/files/')) {
                const name = decodeURIComponent(p.slice('/maps/files/'.length));
                return site.maps[name] ? send(200, site.maps[name], 'application/octet-stream') : send(404, 'no such map');
            }
            if (p === '/index.js') return send(200, fs.readFileSync(path.join(__dirname, 'web_live_stub.js')), 'text/javascript');
            if (p === '/index.data') return send(200, Buffer.alloc(2000), 'application/octet-stream');
            if (p === '/index.wasm') return send(200, Buffer.from([0, 0x61, 0x73, 0x6d]), 'application/wasm');
            const CLEAN = { '/play': 'shell.html', '/matches': 'matches.html', '/match': 'match.html', '/live-now': 'live.html', '/legal': 'legal.html', '/changelog': 'changelog.html', '/mapmaker': 'mapmaker.html' };       // (the clean addresses of docker/nginx.conf)
            const rel = CLEAN[p] || p.replace(/^\/+/, '');
            const file = path.resolve(WEB, rel);
            if (file.startsWith(WEB + path.sep) && fs.existsSync(file) && fs.statSync(file).isFile()) {
                let body = fs.readFileSync(file);
                const type = TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream';
                if (path.extname(file) === '.html') { let text = body.toString('utf8'); Object.keys(FILLED).forEach((k) => { text = text.split(k).join(FILLED[k]); }); body = Buffer.from(text); }
                return send(200, body, type);
            }
            return send(404, 'not found');
        });
    });
    return new Promise((resolve) => site.server.listen(0, '127.0.0.1', () => { site.port = site.server.address().port; site.url = (p) => 'http://127.0.0.1:' + site.port + p; resolve(site); }));
}

async function main() {
    const launch = { args: ['--no-sandbox'] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log((required ? 'FAIL' : 'SKIP') + ': no browser could be started (' + String(e.message).split('\n')[0] + '): the pages were NOT run in a browser'); if (required) failures++; return; }
    const shot = async (page, name) => { if (shotsDir) { fs.mkdirSync(shotsDir, { recursive: true }); await page.screenshot({ path: path.join(shotsDir, name + '.png') }); } };
    try {
        const parts = { strip: partStrip, hand: partHand, cards: partCards, long: partLong, real: partReal };
        for (const name of PARTS) {
            if (name === 'real' && !webUrl) { if (only === 'real') { console.log((required ? 'FAIL' : 'SKIP') + ': no site to run the real game against (--web URL or ANTS_WEB_URL): the real game was NOT run (tests/scripts/web_replay_open_browser.js, part real)'); if (required) failures++; } continue; }
            if (!only || only === name) await parts[name](browser, shot);
        }
    } finally { await browser.close(); }
}

// the sizes of the approved pictures: a computer's window and a phone (390 x 844 at twice the pixels)
const DESKTOP = { viewport: { width: 1280, height: 800 }, deviceScaleFactor: 1, timezoneId: 'America/Los_Angeles', locale: 'en-US' };
const PHONE = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true, timezoneId: 'America/Los_Angeles', locale: 'en-US' };

// a drag of a file over the page, as the browser makes it: the events carry a DataTransfer that lists Files; the answer is whether the page took the event over (preventDefault)
const drag = (page, type, names, selector) => page.evaluate(({ type, names, selector }) => {
    const dt = new DataTransfer();
    (names || []).forEach((n) => { if (n === 'Files') dt.items.add(new File(['x'], 'x.antsrep')); else if (n.indexOf('/') !== -1) dt.setData(n, 'x'); else dt.items.add(new File(['x'], n)); });
    const ev = new DragEvent(type, { dataTransfer: dt, bubbles: true, cancelable: true });
    (selector ? document.querySelector(selector) : document.body).dispatchEvent(ev);
    return ev.defaultPrevented;
}, { type, names, selector });

async function partStrip(browser, shot) {
    const site = await makeSite();
    const open = async (context, s) => { const page = await context.newPage(); await page.goto((s || site).url('/matches')); await page.waitForSelector('#body .note, #body .list'); await sleep(250); return page; };
    for (const [kind, context] of [['desktop', DESKTOP], ['phone', PHONE]]) {
        const ctx = await browser.newContext(context);
        const page = await open(ctx);
        await shot(page, 'built_b_' + kind + '_list');
        if (kind === 'desktop') {
            check('the veil is not there before a drag', !(await page.isVisible('#dropveil')));
            const entered = await drag(page, 'dragenter', ['Files']);
            const moved = await drag(page, 'dragover', ['Files']);
            await sleep(150);
            check('a file dragged over the page: the page takes the drag over, the veil shows its card and the strip lights up', entered && moved && await page.isVisible('#dropveil') && /Drop the replay file to watch it/.test(await text(page, '#dropveil .big'))
                  && (await page.getAttribute('#opener', 'class')).split(' ').indexOf('drag') !== -1);
            check('the veil lets the pointer through (a click on the page goes to what is under it)', await page.evaluate(() => getComputedStyle(document.getElementById('dropveil')).pointerEvents === 'none'));
            await shot(page, 'built_b_desktop_hover');
            await drag(page, 'dragleave', ['Files']);
            check('the drag leaves: the veil and the light go', !(await page.isVisible('#dropveil')) && (await page.getAttribute('#opener', 'class')).split(' ').indexOf('drag') === -1);
            const text1 = await drag(page, 'dragenter', ['text/plain']);
            const text2 = await drag(page, 'dragover', ['text/plain']);
            const text3 = await drag(page, 'drop', ['text/plain']);
            check('a drag of text is not taken over and shows no veil', !text1 && !text2 && !text3 && !(await page.isVisible('#dropveil')));
        }
        // a file that is refused: a video (too big by its size) and a damaged replay
        const chooser = async (file) => { const [fc] = await Promise.all([page.waitForEvent('filechooser'), page.click('#open-btn')]); await fc.setFiles(file); };
        await chooser({ name: 'holiday-video.mp4', mimeType: 'video/mp4', buffer: Buffer.alloc(14900000) });
        await page.waitForSelector('#open-note .card');
        await shot(page, 'built_c_' + kind + '_toobig');
        const damaged = Buffer.from(FIXTURE); damaged[60] ^= 255;
        await chooser({ name: 'ants-TREASURE-20261006-143209.antsrep', mimeType: 'application/octet-stream', buffer: damaged });
        await page.waitForFunction(() => /not a readable/.test(document.querySelector('#open-note h3') ? document.querySelector('#open-note h3').textContent : ''));
        await shot(page, 'built_c_' + kind + '_damaged');
        await ctx.close();
        const empty = await makeSite({ history: [] });
        const c2 = await browser.newContext(context);
        const p2 = await open(c2, empty);
        await shot(p2, 'built_b_' + kind + '_empty');
        await c2.close(); empty.server.close();
    }
    site.server.close();
}


// ---- what the checks of the next two parts share
const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');
const asFile = (name, buffer) => ({ name, mimeType: 'application/octet-stream', buffer: Buffer.from(buffer) });
const NAME = 'ants-TREASURE-20261006-143209.antsrep';                      // (a name that the desktop game gives: the local time, no Z)
// the game is up (the stand-in has read the file) and has reported it: the tag over the picture is shown
const stubStarted = (page) => page.waitForFunction(() => window.__stub && window.__stub.total > 0 && !document.getElementById('rtag').hidden, null, { timeout: 15000, polling: 100 });
const stubSha = (page, file) => page.evaluate(async (p) => {
    const b = window.__stub && window.__stub.files[p];
    if (!b) return null;
    const d = await crypto.subtle.digest('SHA-256', b);
    return Array.from(new Uint8Array(d)).map((x) => x.toString(16).padStart(2, '0')).join('');
}, file);
async function chooseFile(page, trigger, file) {
    const [chooser] = await Promise.all([page.waitForEvent('filechooser'), page.click(trigger)]);
    await chooser.setFiles(file);
}
const text = (page, selector) => page.evaluate((sel) => { const e = document.querySelector(sel); return e ? e.textContent : null; }, selector);
const LOCAL_URL = /\/play\?local=[0-9a-f]{32}$/;
const bodiless = (hits) => hits.every((h) => h.method === 'GET' && h.bodyLength === 0 && h.contentLength === '');
const askedFor = (hits, re) => hits.filter((h) => re.test(h.path));
// a page of the site in a fresh context, with what the stand-in game says (window.__stubReport) set before the page's own scripts run
async function newPage(browser, context, report, extra) {
    const ctx = await browser.newContext(context);
    await ctx.addInitScript((r) => { if (r) window.__stubReport = r; }, report || null);
    if (extra) await ctx.addInitScript(extra);
    return { ctx, page: await ctx.newPage() };
}
async function openMatches(site, page) {
    await page.goto(site.url('/matches'));
    await page.waitForSelector('#opener:not([hidden])');
}
// the card of the player: its title, its buttons (the words), and whether it offers the file for download
async function cardOf(page) {
    await page.waitForSelector('#rm:not([hidden])', { timeout: 15000 });
    return page.evaluate(() => ({
        title: document.getElementById('rm-title').textContent,
        body: document.getElementById('rm-body').innerText,
        buttons: [].map.call(document.querySelectorAll('#rm-body .rm-btns .btn'), (b) => ({ text: b.textContent, tag: b.tagName, href: b.getAttribute('href'), download: b.hasAttribute('download') })),
        downloads: document.querySelectorAll('#rm-body a[download], #rm-body a[href^="/replays/"]').length,
    }));
}

// ---- part hand: the file goes from the Matches page to the player, on this device and nowhere else
async function partHand(browser, shot) {
    const site = await makeSite();
    const report = { map: 'TREASURE.LVL', turns: 20060 };
    // 1. a computer: chosen with the button
    {
        const { ctx, page } = await newPage(browser, DESKTOP, report);
        await openMatches(site, page);
        await page.waitForSelector('#body .list');
        await chooseFile(page, '#open-btn', asFile(NAME, FIXTURE));
        await page.waitForURL(LOCAL_URL);
        await stubStarted(page);
        let url = page.url();
        check('the player gets exactly the bytes of the file (SHA-256)', (await stubSha(page, '/replay.antsrep')) === sha256(FIXTURE));
        const args = await page.evaluate(() => window.__stub.args);
        check('the game is started with --replay and the file, and not as a live match', args.join(' ').indexOf('--replay /replay.antsrep') !== -1 && args.indexOf('--replay-live') === -1, args.join(' '));
        const head = await text(page, '#r-title');
        check('the header says the map, Your file, the name of the file and its length', /^Treasure/.test(head) && /Your file/.test(head) && head.indexOf(NAME) !== -1 && /16:43/.test(head), head);
        check('the header holds no date (a desktop file name has the local time: nothing is guessed from it)', !/Today|Yesterday|Oct \d|\d+:\d\d (AM|PM)/.test(head), head);
        check('the chips are the players of the report', (await page.$$eval('#r-meta .chip .nm', (n) => n.map((x) => x.textContent))).join('|') === 'Mika|Bot (Medium)|Bot (Medium)|Bot (Medium)');
        check('the buttons: Matches and Fullscreen, no Copy link, no Stats, no Download', await page.isVisible('#all-btn') && (await text(page, '#all-btn')).trim() === '‹ Matches' && await page.isVisible('#fullscreen-btn-r')
              && !(await page.isVisible('#copy-btn')) && !(await page.isVisible('#stats-btn')) && (await page.$$eval('[data-dl]', (n) => n.every((x) => x.getClientRects().length === 0))));
        check('the body says replay and ownfile, not live', await page.evaluate(() => document.body.classList.contains('replay') && document.body.classList.contains('ownfile') && !document.body.classList.contains('live')));
        await sleep(300);
        await shot(page, 'built_d_desktop_player');
        const all = site.hits.slice();
        check('no request of the whole visit carries a body, none is a POST, none asks the site for a recording or its stats', bodiless(all) && askedFor(all, /^\/(replays|history\/|stats)/).length === 0, JSON.stringify(askedFor(all, /^\/(replays|history\/|stats)/)));
        check('no request names the file or its bytes', all.every((h) => (h.path + h.query).indexOf(NAME) === -1));
        // 1b. a drop of the file anywhere on the Matches page (a computer): the same bytes arrive
        await page.goto(site.url('/matches'));
        await page.waitForSelector('#opener:not([hidden])');
        const dropped = await page.evaluate(({ name, bytes }) => {
            const first = new File([new Uint8Array(bytes)], name), second = new File([new Uint8Array(bytes.slice(0, 500))], 'second.antsrep');
            const dt = new DataTransfer();
            dt.items.add(first); dt.items.add(second);
            const ev = new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true });
            document.getElementById('q').dispatchEvent(ev);                      // (on the search box: the drop is the page's wherever it lands)
            return ev.defaultPrevented;
        }, { name: NAME, bytes: Array.from(FIXTURE) });
        await page.waitForURL(LOCAL_URL);
        await stubStarted(page);
        check('a dropped file (two were dropped: the first is taken) is taken over by the page and the game gets exactly its bytes', dropped === true && (await stubSha(page, '/replay.antsrep')) === sha256(FIXTURE) && (await text(page, '#r-title')).indexOf(NAME) !== -1);
        check('the drop made no request with a body either', bodiless(site.hits.slice()) && askedFor(site.hits, /^\/(replays|history\/|stats)/).length === 0);
        url = page.url();                                                        // (the record of the drop is the one that a reload must find)
        // 2. the player is reloaded: the record is still there
        await page.reload();
        await stubStarted(page);
        check('a reload of the player plays the same file again (the record stays)', page.url() === url && (await stubSha(page, '/replay.antsrep')) === sha256(FIXTURE));
        // 3. a second file takes the place of the first: the address of the first finds nothing, and does not start the game
        await page.goto(site.url('/matches'));
        await page.waitForSelector('#opener:not([hidden])');
        await chooseFile(page, '#open-btn', asFile('second.antsrep', FIXTURE));
        await page.waitForURL(LOCAL_URL);
        check('a second file has an address of its own', page.url() !== url);
        await page.goto(url);
        const gone = await cardOf(page);
        check('the address of the first file, after a second was opened, is the card "Choose the replay file again" and no game', gone.title === 'Choose the replay file again' && (await page.evaluate(() => typeof window.__stub)) === 'undefined', gone.title);
        check('... which has a button for a file and a link to all matches, and nothing to download', gone.buttons.length === 2 && gone.buttons[0].text === 'Open a replay file' && gone.buttons[1].text === '‹ All matches' && gone.buttons[1].href === '/matches' && gone.downloads === 0, JSON.stringify(gone.buttons));
        await shot(page, 'built_e_desktop_again');
        await chooseFile(page, '#rm-body .rm-btns button', asFile(NAME, FIXTURE));
        await page.waitForURL(LOCAL_URL);
        await stubStarted(page);
        check('a file chosen on the card plays', (await stubSha(page, '/replay.antsrep')) === sha256(FIXTURE));
        await ctx.close();
    }
    // 4. an address with no record, or one that is no id, starts nothing
    {
        const { ctx, page } = await newPage(browser, DESKTOP, report);
        const before = site.hits.length;
        for (const bad of ['/play?local=', '/play?local=xyz', '/play?local=' + '0'.repeat(32), '/play?local=' + 'A'.repeat(32), '/play?local=../../etc/passwd']) {
            await page.goto(site.url(bad));
            const c = await cardOf(page);
            check('no record behind ' + bad + ': the card, and no game', c.title === 'Choose the replay file again' && (await page.evaluate(() => typeof window.__stub)) === 'undefined', c.title);
        }
        check('none of them asked for a recording', askedFor(site.hits.slice(before), /^\/replays/).length === 0);
        await ctx.close();
    }
    // 5. a record older than a day is thrown away by either page
    {
        const { ctx, page } = await newPage(browser, DESKTOP, report);
        await openMatches(site, page);
        await chooseFile(page, '#open-btn', asFile(NAME, FIXTURE));
        await page.waitForURL(LOCAL_URL);
        await stubStarted(page);
        const age = (days) => page.evaluate((d) => new Promise((resolve, reject) => {
            const open = indexedDB.open('ants-own-replay', 1);
            open.onerror = () => reject(open.error);
            open.onsuccess = () => {
                const tx = open.result.transaction('slot', 'readwrite'), store = tx.objectStore('slot'), get = store.get('file');
                get.onsuccess = () => { const rec = get.result; rec.at = Date.now() - d * 24 * 3600 * 1000 - 1000; store.put(rec, 'file'); };
                tx.oncomplete = () => resolve(true);
            };
        }), days);
        const slot = () => page.evaluate(() => new Promise((resolve) => { const o = indexedDB.open('ants-own-replay', 1); o.onsuccess = () => { const g = o.result.transaction('slot').objectStore('slot').get('file'); g.onsuccess = () => resolve(g.result ? g.result.id : null); }; }));
        const url = page.url();
        await age(0.5);
        await page.goto(url); await stubStarted(page);
        check('a record of half a day is still played', (await stubSha(page, '/replay.antsrep')) === sha256(FIXTURE));
        await age(1);
        await page.goto(url);
        check('a record of a day and a second is gone when the player opens: the card', (await cardOf(page)).title === 'Choose the replay file again' && (await slot()) === null);
        await page.goto(site.url('/matches')); await page.waitForSelector('#opener:not([hidden])');
        await chooseFile(page, '#open-btn', asFile(NAME, FIXTURE));
        await page.waitForURL(LOCAL_URL); await stubStarted(page);
        await age(2);
        check('before the Matches page opens the old record is there', (await slot()) !== null);
        await page.goto(site.url('/matches')); await page.waitForSelector('#opener:not([hidden])');
        await until('the Matches page throws the old record away', async () => (await slot()) === null, 5000);
        check('and the Matches page threw it away', (await slot()) === null);
        await ctx.close();
    }
    // 6. a file that is refused leaves the record that was there
    {
        const { ctx, page } = await newPage(browser, DESKTOP, report);
        await openMatches(site, page);
        await chooseFile(page, '#open-btn', asFile(NAME, FIXTURE));
        await page.waitForURL(LOCAL_URL); await stubStarted(page);
        const url = page.url();
        await page.goto(site.url('/matches')); await page.waitForSelector('#opener:not([hidden])');
        await chooseFile(page, '#open-btn', asFile('notes.txt', Buffer.from('this is a text file and no replay at all, long enough to hold a head if it were one')));
        await page.waitForSelector('#open-note .card');
        check('a file that is no replay is refused with its card', /not a readable replay/.test(await text(page, '#open-note h3')) && page.url().indexOf('/matches') !== -1);
        await page.goto(url); await stubStarted(page);
        check('and the record that was there still plays', (await stubSha(page, '/replay.antsrep')) === sha256(FIXTURE));
        await ctx.close();
    }
    // 7. a phone: the same, and More has a way to open another file
    {
        const { ctx, page } = await newPage(browser, PHONE, report);
        await openMatches(site, page);
        await page.waitForSelector('#body .list');
        await chooseFile(page, '#open-btn', asFile(NAME, FIXTURE));
        await page.waitForURL(LOCAL_URL); await stubStarted(page); await sleep(300);
        await shot(page, 'built_d_phone_player');
        check('a phone plays the same bytes', (await stubSha(page, '/replay.antsrep')) === sha256(FIXTURE));
        await page.click('.header-actions.rep-only summary');
        const more = await page.$$eval('.header-actions.rep-only .more-list > *', (n) => n.filter((x) => x.getClientRects().length > 0).map((x) => x.textContent));
        check('More holds one thing on a phone: Open another file', more.length === 1 && more[0] === 'Open another file', JSON.stringify(more));
        const url = page.url();
        await chooseFile(page, '#own-more', asFile('other.antsrep', FIXTURE));
        await page.waitForFunction((u) => location.href !== u, url);
        check('another file from More goes to the player at its own address', LOCAL_URL.test(page.url()));
        await ctx.close();
    }
    // 8. the site is down: the strip and the player do not need it
    {
        const down = await makeSite({ down: true });
        const { ctx, page } = await newPage(browser, DESKTOP, report);
        await page.goto(down.url('/matches'));
        await page.waitForSelector('#opener:not([hidden])');
        await page.waitForSelector('#body .note');
        check('with the server down the list says so and the strip is there', await page.isVisible('#open-btn') && /Try again|could not|Could not|not answer|isn/.test(await text(page, '#body .note') || ''), await text(page, '#body .note'));
        await chooseFile(page, '#open-btn', asFile(NAME, FIXTURE));
        await page.waitForURL(LOCAL_URL); await stubStarted(page);
        check('and the file plays', (await stubSha(page, '/replay.antsrep')) === sha256(FIXTURE));
        await ctx.close();
        down.server.close();
    }
    // 9. a browser that keeps no site data: the card that says so
    for (const [kindName, context] of [['desktop', DESKTOP], ['phone', PHONE]]) {
        const { ctx, page } = await newPage(browser, context, report, () => { Object.defineProperty(window, 'indexedDB', { value: undefined, configurable: true }); });
        await openMatches(site, page);
        await chooseFile(page, '#open-btn', asFile(NAME, FIXTURE));
        await page.waitForSelector('#open-note .card');
        check('no IndexedDB (' + kindName + '): the card says that the browser will not keep the file, the page stays', /will not keep the file/.test(await text(page, '#open-note h3')) && page.url().indexOf('/matches') !== -1);
        await sleep(200);
        await shot(page, 'built_c_' + kindName + '_nostorage');
        await ctx.close();
    }
    {   // 10. the card that asks for the file again, on a phone (a second file took the place of the first)
        const { ctx, page } = await newPage(browser, PHONE, report);
        await openMatches(site, page);
        await chooseFile(page, '#open-btn', asFile(NAME, FIXTURE));
        await page.waitForURL(LOCAL_URL);
        const first = page.url();
        await page.goto(site.url('/matches'));
        await page.waitForSelector('#opener:not([hidden])');
        await chooseFile(page, '#open-btn', asFile('second.antsrep', FIXTURE));
        await page.waitForURL(LOCAL_URL);
        await page.goto(first);
        const gone = await cardOf(page);
        check('on a phone the address of the first file, after a second was opened, is the card "Choose the replay file again"', gone.title === 'Choose the replay file again');
        await sleep(200);
        await shot(page, 'built_e_phone_again');
        await ctx.close();
    }
    site.server.close();
}

// ---- part cards: what the game says of a file, the maps, and the way out of every card
async function partCards(browser, shot) {
    const site = await makeSite();
    const open = async (page, file) => {
        await openMatches(site, page);
        await chooseFile(page, '#open-btn', file);
        await page.waitForURL(LOCAL_URL);
    };
    // 1. the game refuses the file for its version (the game decides: the stand-in answers as the game does)
    for (const [kind, report, title, needs] of [['older', { failure: 2, game: 'v0.11.0', map: 'TREASURE.LVL', turns: 20060 }, 'Recorded by an older version', /played on v0\.11\.0\. The rules of the game have changed since.*this version \(v0\.0\.0\) can’t show it/],
                                                 ['newer', { failure: 3, game: 'v0.14.2', map: 'TREASURE.LVL', turns: 20060 }, 'Recorded by a newer version', /played on v0\.14\.2, and this page is v0\.0\.0\. Load the page again to get the newer version/]]) {
        for (const [kindName, context] of [['desktop', DESKTOP], ['phone', PHONE]]) {
            const { ctx, page } = await newPage(browser, context, report);
            await open(page, asFile(NAME, FIXTURE));
            const card = await cardOf(page);
            check('a file of ' + kind + ' version (' + kindName + '): the card "' + title + '" with the approved sentence', card.title === title && needs.test(card.body.replace(/\s+/g, ' ')), card.title + ' / ' + card.body);
            const want = kind === 'newer' ? ['Open another file', 'Load the page again', '‹ All matches'] : ['Open another file', '‹ All matches'];
            check('... its buttons are ' + want.join(', ') + ' and nothing to download', JSON.stringify(card.buttons.map((b) => b.text)) === JSON.stringify(want) && card.downloads === 0 && !/download/i.test(card.body), JSON.stringify(card.buttons));
            check('... the header still says whose file it is', /Your file/.test(await text(page, '#r-title')) && (await page.$$eval('#r-meta .chip', (n) => n.length)) === 4);
            await sleep(200);
            await shot(page, 'built_c_' + kindName + '_otherversion_' + kind);
            if (kindName === 'desktop') {
                const url = page.url();
                await chooseFile(page, '#rm-body .rm-btns button', asFile('again.antsrep', FIXTURE));
                await page.waitForFunction((u) => location.href !== u, url);
                check('Open another file opens a chooser and a file from it goes to its own address', LOCAL_URL.test(page.url()));
                const url2 = page.url();
                await cardOf(page);
                await chooseFile(page, '#rm-body .rm-btns button', asFile('notes.txt', Buffer.from('not a replay, but long enough to look at, as a head would need it to be')));
                await page.waitForSelector('#toast:not([hidden])');
                check('a file that is refused from the card is a note on the page, and the page stays', /not a readable replay/.test(await text(page, '#toast')) && page.url() === url2, await text(page, '#toast'));
                if (kind === 'newer') {
                    await page.evaluate(() => { window.__reloaded = 0; });
                    await Promise.all([page.waitForNavigation(), page.click('#rm-body .rm-btns a[href="#"]')]);
                    check('Load the page again reloads the page (the record stays: the same card)', (await cardOf(page)).title === title && page.url() === url2);
                }
            }
            await ctx.close();
        }
    }
    // 2. the game cannot read the file, or has not the map: the generic card, without a download
    {
        const { ctx, page } = await newPage(browser, DESKTOP, { failure: 1, text: 'chunk CMDS at byte 900: the checksum is wrong (the file is damaged)', map: 'TREASURE.LVL', turns: 20060 });
        await open(page, asFile(NAME, FIXTURE));
        const card = await cardOf(page);
        check('a file that the game cannot read: "This replay can’t be played here", the game\'s reason, Open another file and All matches, no download', card.title === 'This replay can’t be played here' && /the checksum is wrong/.test(card.body)
              && JSON.stringify(card.buttons.map((b) => b.text)) === JSON.stringify(['Open another file', '‹ All matches']) && card.downloads === 0, JSON.stringify(card));
        await ctx.close();
    }
    {
        const { ctx, page } = await newPage(browser, DESKTOP, { failure: 4, text: 'the map Dune.lvl is not in Original-Ants/Maps (the replay needs it; --maps-dir says where the maps are)', map: 'Dune.lvl', turns: 20060 });
        await open(page, asFile(NAME, FIXTURE));
        const card = await cardOf(page);
        check('the game has not the map (its own words for a folder that lacks it): "This site doesn’t have the map" with the map\'s name', card.title === 'This site doesn’t have the map' && /played on the map Dune\.lvl/.test(card.body) && card.downloads === 0, JSON.stringify(card));
        await ctx.close();
    }
    // 2b. the game refuses a map that the FILE carries: its reason is the card's, and the words for a map that the site has not are not said of a map that the file brought
    for (const [label, bytes, why, wanted] of [
        ['markers outside its grid', tools.withMap(FIXTURE, 'Dune.lvl', tools.mapWithGrid(TINY_MAP, 1, 1), true), 'the map cannot be played by the seats of this match: team green: 3 start marker(s) lie outside the 1 x 1 grid', /Team green: 3 start marker\(s\) lie outside the 1 x 1 grid|team green: 3 start marker\(s\) lie outside the 1 x 1 grid/],
        ['a rules word that no reader knows', tools.withMap(FIXTURE, 'Dune.lvl', tools.mapAsVersion9(TINY_MAP, 2), true), 'the map in the replay cannot be loaded: the rules word is 2: only 0 (the original’s rules) and 1 (extended rules) are known, the file is not read', /The map in the replay cannot be loaded: the rules word is 2/],
    ]) {
        for (const [kindName, context] of [['desktop', DESKTOP], ['phone', PHONE]]) {
            const { ctx, page } = await newPage(browser, context, { failure: 4, text: why, map: 'Dune.lvl', turns: 20060, over: true });          // (over: the game knows the whole file even when it refuses its map)
            const before = site.hits.length;
            await open(page, asFile('carried.antsrep', bytes));
            const card = await cardOf(page);
            check('a map that the file carries and the game refuses (' + label + ', ' + kindName + '): "This replay can’t be played here" with the game\'s reason, not the words for a missing map', card.title === 'This replay can’t be played here' && wanted.test(card.body) && !/doesn’t have the map|no longer has it/.test(card.title + card.body) && card.downloads === 0, JSON.stringify(card));
            check('... the site was not asked for the map (the file brought it), and the buttons are Open another file and All matches', askedFor(site.hits.slice(before), /^\/maps\/files\//).length === 0 && JSON.stringify(card.buttons.map((b) => b.text)) === JSON.stringify(['Open another file', '‹ All matches']));
            if (label.indexOf('grid') !== -1) { await sleep(200); await shot(page, 'built_c_' + kindName + '_refusedmap'); }
            await ctx.close();
        }
    }
    // 2c. a map that the file carries and that starts more ants than a replay is set up with: the page says so before the game starts (the game is never asked), at once, with the count
    for (const [kindName, context] of [['desktop', DESKTOP], ['phone', PHONE]]) {
        const { ctx, page } = await newPage(browser, context, null);
        await open(page, asFile('crowd.antsrep', tools.withMap(FIXTURE, 'Crowd.lvl', tools.mapWithAnts(TINY_MAP, 65535), true)));
        const started = Date.now();
        const card = await cardOf(page);
        check('a map of 65,535 start markers carried by the file (' + kindName + '): the card "This replay can’t be played here" with the count, in no time', card.title === 'This replay can’t be played here' && /The map in the file starts 65,535 ants \(the start markers of the four colors\), more than the 8,000 that a replay is set up with\./.test(card.body) && Date.now() - started < 5000, JSON.stringify(card));
        check('... the game was never started and nothing was fetched for the map', (await page.evaluate(() => typeof window.__stub)) === 'undefined' && askedFor(site.hits, /^\/maps\/files\/Crowd/).length === 0);
        if (kindName === 'phone') { await sleep(200); await shot(page, 'built_c_phone_crowd'); } else await shot(page, 'built_c_desktop_crowd');
        await ctx.close();
    }
    for (const [kindName, context] of [['desktop', DESKTOP], ['phone', PHONE]]) {   // 65,400 plants and 65,535 waypoints (the engine looks up the waypoint of every plant among all of them): the page says so at once, with the numbers
        const { ctx, page } = await newPage(browser, context, null);
        await open(page, asFile('plants.antsrep', tools.withMap(FIXTURE, 'Plants.lvl', tools.mapWithPlantsAndWaypoints(TINY_MAP, 65400, 65535), true)));
        const started = Date.now();
        const card = await cardOf(page);
        check('a map of 65,400 plants and 65,535 waypoints carried by the file (' + kindName + '): the card "This replay can’t be played here" with the numbers, in no time', card.title === 'This replay can’t be played here'
              && /The map in the file has 65,400 plants and 65,535 waypoints, so setting it up looks at 4,285,989,000 pairs of them, more than the 1,000,000 that a replay is set up with\./.test(card.body) && Date.now() - started < 5000, JSON.stringify(card));
        check('... the game was never started', (await page.evaluate(() => typeof window.__stub)) === 'undefined');
        if (kindName === 'phone') { await sleep(200); await shot(page, 'built_c_phone_pairs'); } else await shot(page, 'built_c_desktop_pairs');
        await ctx.close();
    }
    {   // exactly 1,000,000 pairs go to the game
        const { ctx, page } = await newPage(browser, DESKTOP, { map: 'Dune.lvl', turns: 20060 });
        await open(page, asFile('plants.antsrep', tools.withMap(FIXTURE, 'Plants.lvl', tools.mapWithPlantsAndWaypoints(TINY_MAP, 1000, 1000), true)));
        await stubStarted(page);
        check('a map of exactly 1,000,000 pairs (1,000 plants and 1,000 waypoints) is handed to the game', !(await page.isVisible('#rm')));
        await ctx.close();
    }
    {   // 8,000 is the limit: that many go to the game
        const { ctx, page } = await newPage(browser, DESKTOP, { map: 'Dune.lvl', turns: 20060 });
        await open(page, asFile('crowd.antsrep', tools.withMap(FIXTURE, 'Crowd.lvl', tools.mapWithAnts(TINY_MAP, 8000), true)));
        await stubStarted(page);
        check('a map of exactly 8,000 start markers is handed to the game', (await page.evaluate(() => window.__stub.args.join(' '))).indexOf('--replay /replay.antsrep') !== -1 && !(await page.isVisible('#rm')));
        await ctx.close();
    }
    // 3. the maps of a file: an original is in the game; a map of the library is fetched by its name and checked; a map that the file carries is used without asking; an odd name asks for nothing
    const DUNE = TINY_MAP;
    const dune = (embed) => tools.withMap(FIXTURE, 'Dune.lvl', DUNE, embed);
    {
        const { ctx, page } = await newPage(browser, DESKTOP, null);
        const before = site.hits.length;
        await open(page, asFile('dune.antsrep', dune(false)));
        const card = await cardOf(page);
        const maps = askedFor(site.hits.slice(before), /^\/maps\/files\//);
        check('a map of the library that the site has not: "This site doesn’t have the map", the map\'s name, "no longer has it"', card.title === 'This site doesn’t have the map' && /map Dune\.lvl/.test(card.body) && /no longer has it/.test(card.body), JSON.stringify(card));
        check('... the game is not started, and the site was asked once for that map by its name (a GET with no body)', (await page.evaluate(() => typeof window.__stub)) === 'undefined' && maps.length === 1 && maps[0].path === '/maps/files/Dune.lvl' && bodiless(maps), JSON.stringify(maps));
        check('... the header says the map and the players of the file\'s head, though the game never read it', /^Dune/.test(await text(page, '#r-title')) && (await page.$$eval('#r-meta .chip .nm', (n) => n.length)) === 4);
        await sleep(200);
        await shot(page, 'built_c_desktop_mapmissing');
        await ctx.close();
        const { ctx: c2, page: p2 } = await newPage(browser, PHONE, null);
        await open(p2, asFile('dune.antsrep', dune(false)));
        await cardOf(p2);
        await sleep(200);
        await shot(p2, 'built_c_phone_mapmissing');
        await c2.close();
    }
    {
        site.maps['Dune.lvl'] = Buffer.concat([DUNE, Buffer.from('x')]);
        const { ctx, page } = await newPage(browser, DESKTOP, null);
        await open(page, asFile('dune.antsrep', dune(false)));
        const card = await cardOf(page);
        check('a map of that name that is another file: the card says it is not the same file, the game is not started', card.title === 'This site doesn’t have the map' && /has a map of that name, but it is not the same file/.test(card.body) && !/host/.test(card.body)
              && (await page.evaluate(() => typeof window.__stub)) === 'undefined', JSON.stringify(card));
        await ctx.close();
    }
    {
        site.maps['Dune.lvl'] = DUNE;
        const { ctx, page } = await newPage(browser, DESKTOP, null);
        const before = site.hits.length;
        await open(page, asFile('dune.antsrep', dune(false)));
        await stubStarted(page);
        const maps = askedFor(site.hits.slice(before), /^\/maps\/files\//);
        check('a map of the library that the site has: fetched by its name, checked against the head\'s hash and handed to the game', maps.length === 1 && maps[0].path === '/maps/files/Dune.lvl' && (await stubSha(page, 'Original-Ants/Maps/Dune.lvl')) === sha256(DUNE));
        check('... the whole visit carries no body and asks for no recording', bodiless(site.hits.slice(before)) && askedFor(site.hits.slice(before), /^\/(replays|history\/)/).length === 0);
        await ctx.close();
    }
    {
        delete site.maps['Dune.lvl'];
        const { ctx, page } = await newPage(browser, DESKTOP, null);
        const before = site.hits.length;
        await open(page, asFile('dune.antsrep', dune(true)));
        await stubStarted(page);
        check('a file that carries its map plays with nothing fetched: the map the game gets is the file\'s', askedFor(site.hits.slice(before), /^\/maps\/files\//).length === 0 && (await stubSha(page, 'Original-Ants/Maps/Dune.lvl')) === sha256(DUNE));
        await ctx.close();
        const { ctx: c2, page: p2 } = await newPage(browser, DESKTOP, null);
        const before2 = site.hits.length;
        const bad = tools.withMap(FIXTURE, 'Dune.lvl', DUNE, true);
        const spoilt = Uint8Array.from(bad); spoilt[tools.chunks(bad)[1].at + 8 + 5] ^= 1;       // (a byte of the carried map: its checksum and its hash are wrong now)
        await open(p2, asFile('spoilt.antsrep', spoilt));
        const card = await cardOf(p2).catch(() => null);
        check('a carried map that is not the one the head names is not used: the page asks the site for the map by its name instead (and the card for a map that the site has not)', askedFor(site.hits.slice(before2), /^\/maps\/files\/Dune\.lvl$/).length === 1 && !!card, JSON.stringify(card));
        await c2.close();
    }
    {
        for (const [label, name, expectAsked] of [['a map name with a path in it', '../../etc/passwd.lvl', 0], ['a map name with a slash', 'a/b.lvl', 0], ['a map name that is not printable', 'a\u0001b.lvl', 0], ['a map name that is percent codes', 'x%2e%2e%2fy.lvl', 1]]) {
            const { ctx, page } = await newPage(browser, DESKTOP, null);
            const before = site.hits.length;
            const file = tools.withHead(FIXTURE, (fields) => { tools.setField(fields, 3, tools.text(name)); tools.setField(fields, 4, tools.hashBytes('0123456789abcdef')); });
            await open(page, asFile('damaged.antsrep', file));
            const card = await cardOf(page);
            const maps = askedFor(site.hits.slice(before), /^\/maps\/files\//);
            check(label + ': ' + (expectAsked ? 'the name is percent-encoded, one request, no game' : 'the site is not asked at all, no game'), maps.length === expectAsked && (!expectAsked || maps[0].path === '/maps/files/x%252e%252e%252fy.lvl') && (await page.evaluate(() => typeof window.__stub)) === 'undefined', JSON.stringify(maps));
            check(label + ': a card with no way to download (' + card.title + ')', card.downloads === 0 && !!card.title);
            await ctx.close();
        }
    }
    // 4. everything that a file says is text: names of players, the map, the name of the file
    {
        const evil = '<img src=x onerror=window.__pwn=1>';
        const file = tools.withHead(FIXTURE, (fields) => { tools.setField(fields, 8, tools.text(evil)); tools.setField(fields, 9, tools.text('"><script>window.__pwn=1</script>')); });
        const { ctx, page } = await newPage(browser, DESKTOP, { map: 'TINY.LVL', names: [evil, '"><script>window.__pwn=1</script>', 'Bot (Medium)', 'Bot (Medium)'] });
        await open(page, asFile('"><img src=x onerror=window.__pwn=1>.antsrep', file));
        await stubStarted(page);
        await sleep(300);
        check('a name of a player, the name of the file: text, never markup (nothing ran, no element was made)', (await page.evaluate(() => window.__pwn)) === undefined && (await page.$$eval('#r-title img, #r-meta img, #r-title script, #r-meta script', (n) => n.length)) === 0);
        check('... the chip shows the name as it is, and the header the name of the file as it is', (await page.$$eval('#r-meta .chip .nm', (n) => n.map((x) => x.textContent)))[0] === evil && /"><img src=x onerror=window\.__pwn=1>\.antsrep/.test(await text(page, '#r-title')));
        await ctx.close();
    }
    site.server.close();
}

// ---- part long: text that cannot wrap (a file name of 100 characters, a map name of 59, four player names of 64) on a phone: the page does not scroll sideways; and a file name that reads right to left keeps the header's order
async function partLong(browser, shot) {
    const site = await makeSite();
    const widths = (page) => page.evaluate(() => ({ root: document.documentElement.scrollWidth, body: document.body.scrollWidth, window: window.innerWidth }));
    const fits = (w) => w.root <= w.window && w.body <= w.window;
    const cases = [
        ['a file name of 100 characters', 'ants-' + 'x'.repeat(91) + '.antsrep', FIXTURE, {}, 'file'],
        ['a map name of 59 characters (the header, and the card of a map that the site does not have)', NAME, tools.withMap(FIXTURE, 'Q'.repeat(55) + '.lvl', TINY_MAP, false), {}, 'card'],
        ['four player names of 64 characters', NAME, FIXTURE, { names: ['a', 'b', 'c', 'd'].map((c) => c.repeat(64)), roster: 15 }, 'players'],
    ];
    for (const [label, name, bytes, report, key] of cases) {
        const { ctx, page } = await newPage(browser, PHONE, Object.assign({ map: 'TREASURE.LVL', turns: 20060 }, report));
        await openMatches(site, page);
        await chooseFile(page, '#open-btn', asFile(name, bytes));
        await page.waitForURL(LOCAL_URL);
        if (key === 'card') await cardOf(page); else await stubStarted(page);
        await sleep(400);
        const w = await widths(page);
        check('on a phone, ' + label + ' do not make the page scroll sideways', fits(w), JSON.stringify(w));
        await shot(page, 'built_f_phone_long_' + key);
        await ctx.close();
    }
    {   // the three at once, and the header on a computer too
        for (const [kind, context] of [['phone', PHONE], ['desktop', DESKTOP]]) {
            const { ctx, page } = await newPage(browser, context, { map: 'TREASURE.LVL', turns: 20060, names: ['a', 'b', 'c', 'd'].map((c) => c.repeat(64)), roster: 15 });
            await openMatches(site, page);
            await chooseFile(page, '#open-btn', asFile('ants-' + 'x'.repeat(91) + '.antsrep', tools.withMap(FIXTURE, 'Q'.repeat(55) + '.lvl', TINY_MAP, false)));
            await page.waitForURL(LOCAL_URL);
            await cardOf(page);
            await sleep(400);
            const w = await widths(page);
            check('a ' + kind + ': the long file name, map name and player names together do not make the page scroll sideways', fits(w), JSON.stringify(w));
            await ctx.close();
        }
    }
    {   // a file name that reads right to left: the name is a unit, so the length that follows it stays after it (a plain run of text puts the digits in the middle of the Arabic)
        const arabic = '\u0645\u0644\u0641_\u0625\u0639\u0627\u062f\u0629_\u0627\u0644\u0645\u0628\u0627\u0631\u0627\u0629.antsrep';
        for (const [kind, context] of [['phone', PHONE], ['desktop', DESKTOP]]) {
            const { ctx, page } = await newPage(browser, context, { map: 'TREASURE.LVL', turns: 20060 });
            await openMatches(site, page);
            await chooseFile(page, '#open-btn', asFile(arabic, FIXTURE));
            await page.waitForURL(LOCAL_URL);
            await stubStarted(page);
            await sleep(300);
            const g = await page.evaluate((expect) => {
                const small = document.querySelector('#r-title small');
                const bdi = small && small.querySelector('bdi');
                let length = null;
                if (small) {
                    const walker = document.createTreeWalker(small, NodeFilter.SHOW_TEXT);
                    for (let n = walker.nextNode(); n; n = walker.nextNode()) {
                        const at = n.data.indexOf('16:43');
                        if (at !== -1) { const r = document.createRange(); r.setStart(n, at); r.setEnd(n, at + 5); length = r.getBoundingClientRect().toJSON(); }
                    }
                }
                const pill = small && small.querySelector('.pill.own');
                return { bdi: bdi ? bdi.textContent : null, same: bdi ? bdi.textContent === expect : false, name: bdi ? bdi.getBoundingClientRect().toJSON() : null, length, pill: pill ? pill.getBoundingClientRect().toJSON() : null };
            }, arabic);
            check('a ' + kind + ': the file name that reads right to left is one unit (a bdi) with the text as it is', g.same, JSON.stringify(g.bdi));
            check('a ' + kind + ': the header reads Your file, the name, then the length, from the left (the length is not in the middle of the name)', !!g.name && !!g.length && !!g.pill && g.pill.right <= g.name.left + 1 && g.length.left >= g.name.right - 1, JSON.stringify(g));
            if (kind === 'phone') await shot(page, 'built_f_phone_long_rtl');
            await ctx.close();
        }
    }
    site.server.close();
}

// ---- part real: the same pages and the REAL game, against a running site (the web image, or tools/web_without_docker.py): the game, not a stand-in, says what each file is
async function partReal(browser, shot) {
    const base = webUrl.replace(/\/+$/, '');
    const probe = await fetch(base + '/index.wasm', { method: 'HEAD' }).then((r) => r.ok, () => false);
    if (!probe) { console.log((required ? 'FAIL' : 'SKIP') + ': the site at ' + base + ' has no game (index.wasm): the real game was NOT run'); if (required) failures++; return; }
    // (the game's exports are stubs that replace themselves at the first call: one made before the program runs leaves Module._ants_replay_get undefined for good. So nothing here asks before the page says that the
    // game is ready (isReadyToPlay), as the page's own glue does; and every look is in a try)
    const state = (page) => page.evaluate(() => { try { return typeof isReadyToPlay !== 'undefined' && isReadyToPlay && Module._ants_replay_get ? { state: Module._ants_replay_get(0), turn: Module._ants_replay_get(1), total: Module._ants_replay_get(2), failure: Module._ants_replay_get(5) } : null; } catch (e) { return null; } });
    // the game is up and holds a match: playing (2), paused (3), jumping (4), ended (5) or cut short (6) with its turns; a game that failed to read the file (7) is a card and not this
    const up = async (page) => { await page.waitForFunction(() => { try { return typeof isReadyToPlay !== 'undefined' && isReadyToPlay && !!Module._ants_replay_get && Module._ants_replay_get(0) >= 2 && Module._ants_replay_get(0) <= 6 && Module._ants_replay_get(2) > 0; } catch (e) { return false; } }, null, { timeout: 120000, polling: 250 }); return state(page); };
    const openOn = async (page, file) => {
        await page.goto(base + '/matches');
        await page.waitForSelector('#opener:not([hidden])');
        await chooseFile(page, '#open-btn', file);
        await page.waitForURL(LOCAL_URL);
    };
    const anyCard = (page) => page.waitForSelector('#rm:not([hidden])', { timeout: 120000 }).then(() => cardOf(page));
    const ctxOf = () => browser.newContext(Object.assign({}, DESKTOP, { baseURL: base }));
    // 1. a real recording: the game starts, plays and answers the bar
    {
        const ctx = await ctxOf(), page = await ctx.newPage();
        const hits = [];
        page.on('request', (r) => hits.push({ method: r.method(), url: new URL(r.url()).pathname, body: r.postData() }));
        await openOn(page, asFile(NAME, FIXTURE));
        const s1 = await up(page);
        check('the real game plays the file: its state is playing, and it holds the 7,205 turns of the match', (s1.state === 2 || s1.state === 3) && s1.total === 7205, JSON.stringify(s1));
        await page.waitForSelector('#rtag:not([hidden])', { timeout: 60000 });
        check('the header says what the game says of the file: Tiny, Your file, the name, 6:00, the four players', /^Tiny/.test(await text(page, '#r-title')) && /Your file/.test(await text(page, '#r-title')) && /6:00/.test(await text(page, '#r-title'))
              && (await page.$$eval('#r-meta .chip .nm', (n) => n.map((x) => x.textContent))).join('|') === 'Bot (Medium)|Bot (Medium)|Bot (Medium)|Bot (Medium)', await text(page, '#r-title'));
        check('no card is up', !(await page.isVisible('#rm')));
        await sleep(2500);
        const s2 = await state(page);
        check('the match runs (the game\'s turn goes on)', s2.turn > s1.turn, JSON.stringify([s1, s2]));
        const rail = await page.$('#rail'), box = await rail.boundingBox();
        await page.mouse.click(box.x + box.width * 0.5, box.y + box.height / 2);
        await page.waitForFunction(() => { try { return Module._ants_replay_get(1) > 3000 && Module._ants_replay_get(0) !== 4; } catch (e) { return false; } }, null, { timeout: 60000, polling: 250 });
        check('a click on the middle of the timeline jumps there (the game plays the match again up to it)', (await state(page)).turn > 3000);
        await shot(page, 'built_real_player');
        check('no request of the visit carries a body, none is a POST, none asked the site for a recording, its stats or the file', hits.every((h) => h.method === 'GET' && !h.body) && !hits.some((h) => /^\/(replays|history\/|stats)/.test(h.url)));
        await page.reload();
        const s3 = await up(page);
        check('a reload plays it again (the record stays)', s3.total === 7205 && (s3.state === 2 || s3.state === 3));
        await ctx.close();
    }
    // 2. the game decides the version: a rules number of an older or a newer game
    for (const [rules, title, words] of [[4, 'Recorded by an older version', /played on v0\.13\.1\. The rules of the game have changed since/], [99, 'Recorded by a newer version', /played on v0\.13\.1, and this page is /]]) {
        const ctx = await ctxOf(), page = await ctx.newPage();
        await openOn(page, asFile(NAME, tools.withRules(FIXTURE, rules)));
        const card = await anyCard(page);
        check('rules number ' + rules + ': the real game says "' + title + '" and the card has no download', card.title === title && words.test(card.body.replace(/\s+/g, ' ')) && card.downloads === 0 && card.buttons[0].text === 'Open another file', JSON.stringify(card));
        await sleep(400);
        await shot(page, 'built_real_otherversion_' + (rules === 4 ? 'older' : 'newer'));
        await ctx.close();
    }
    // 3. maps: one that the site has not; one that the file carries; one that the file carries and that is damaged in ways that a stranger could choose
    const DUNE = TINY_MAP;
    {
        // (a site with its game server answers 404 for a map that it has not; nginx alone, with no server behind it as in tools/web_without_docker.py, answers 502, and then the 404 is made here)
        const answer = await fetch(base + '/maps/files/Dune.lvl').then((r) => r.status, () => 0);
        const ctx = await ctxOf(), page = await ctx.newPage();
        if (answer !== 404) await page.route('**/maps/files/Dune.lvl', (route) => route.fulfill({ status: 404, body: 'not found' }));
        await openOn(page, asFile('dune.antsrep', tools.withMap(FIXTURE, 'Dune.lvl', DUNE, false)));
        const card = await anyCard(page);
        check('a map of the library that the site has not: the card, from ' + (answer === 404 ? 'the real site\'s 404' : 'a 404 made here (the site answered ' + answer + ')'), card.title === 'This site doesn’t have the map' && /Dune\.lvl/.test(card.body), JSON.stringify(card));
        await sleep(400);
        await shot(page, 'built_real_mapmissing');
        await ctx.close();
    }
    {
        const ctx = await ctxOf(), page = await ctx.newPage();
        await openOn(page, asFile('dune.antsrep', tools.withMap(FIXTURE, 'Dune.lvl', DUNE, true)));
        const s1 = await up(page);
        check('a file that carries its map: the real game reads it and plays the match (nothing fetched for it)', (s1.state === 2 || s1.state === 3) && s1.total === 7205 && !(await page.isVisible('#rm')), JSON.stringify(s1));
        await ctx.close();
    }
    const flip = (bytes, at, v) => { const b = Uint8Array.from(bytes); b[at] = v; return b; };
    const set32 = (bytes, at, v) => { const b = Uint8Array.from(bytes); for (let i = 0; i < 4; i++) b[at + i] = (v >>> (8 * i)) & 255; return b; };
    const untrusted = [['a map of nothing but zeros', new Uint8Array(1000)], ['a map cut after 100 bytes', DUNE.slice(0, 100)], ['a map cut in its middle', DUNE.slice(0, DUNE.length >> 1)], ['a map of random bytes of the longest size', Uint8Array.from({ length: 1 << 20 }, (_, i) => (i * 2654435761 >>> 7) & 255)],
                     ['a map whose header says the grid is huge', set32(DUNE, 8, 0xffffffff)], ['a map with a changed first byte', flip(DUNE, 0, DUNE[0] ^ 255)], ['a map with a changed byte in its tiles', flip(DUNE, 5000, DUNE[5000] ^ 255)],
                     ['a map with 65,535 in its second word', set32(DUNE, 4, 0xffff)], ['a map with its middle zeroed', (() => { const b = Uint8Array.from(DUNE); b.fill(0, 2000, 9000); return b; })()]];
    // 3b. a crowd: a map that starts tens of thousands of ants, or has tens of thousands of plants and of waypoints (the engine looks the waypoint of every plant up among all of them), held the tab for seconds to minutes
    //     (the set-up grows with the square). The page counts first and says so at once; where the page does not (a map named like one of the six originals is never taken by the page: the game gets the file) the game
    //     refuses it in the same breath, with the count
    const crowd = (ants) => tools.mapWithAnts(TINY_MAP, ants), plantsAndWaypoints = (plants, waypoints) => tools.mapWithPlantsAndWaypoints(TINY_MAP, plants, waypoints);
    for (const [label, name, map, words, picture] of [
        ['65,535 ants carried under a name of its own: the page refuses before the game starts', 'Crowd.lvl', crowd(65535), /The map in the file starts 65,535 ants \(the start markers of the four colors\), more than the 8,000 that a replay is set up with\./, 'built_real_crowd'],
        ['65,535 ants carried under the name of an original (the page leaves it to the game): the game refuses it', 'TINY.LVL', crowd(65535), /The map in the replay starts 65,535 ants \(the start markers of the four colors\), more than the 8,000 that a replay is set up with/, 'built_real_crowd_game'],
        ['8,001 ants under the name of an original: the game refuses it too', 'TINY.LVL', crowd(8001), /The map in the replay starts 8,001 ants/, ''],
        ['65,400 plants and 65,535 waypoints (a file of 936 KB) under a name of its own: the page refuses', 'Plants.lvl', plantsAndWaypoints(65400, 65535), /The map in the file has 65,400 plants and 65,535 waypoints, so setting it up looks at 4,285,989,000 pairs of them, more than the 1,000,000 that a replay is set up with\./, 'built_real_pairs'],
        ['65,400 plants and 65,535 waypoints under the name of an original: the game refuses it', 'TINY.LVL', plantsAndWaypoints(65400, 65535), /The map in the replay has 65,400 plants and 65,535 waypoints, so setting it up looks at 4,285,989,000 pairs of them, more than the 1,000,000 that a replay is set up with/, 'built_real_pairs_game'],
        ['30,000 plants and 30,000 waypoints under the name of an original: the game refuses it', 'TINY.LVL', plantsAndWaypoints(30000, 30000), /The map in the replay has 30,000 plants and 30,000 waypoints, so setting it up looks at 900,000,000 pairs/, ''],
    ]) {
        const ctx = await ctxOf(), page = await ctx.newPage();
        const hits = [];
        page.on('request', (r) => hits.push({ method: r.method(), url: new URL(r.url()).pathname, body: r.postData() }));
        const started = Date.now();
        await openOn(page, asFile('crowd.antsrep', tools.withMap(FIXTURE, name, map, true)));
        const card = await anyCard(page);
        const took = Date.now() - started;
        check(label + ': the card "This replay can’t be played here" with the count, no download, within 60 s (the page was held for seconds to minutes before)', card.title === 'This replay can’t be played here' && words.test(card.body.replace(/\s+/g, ' ')) && card.downloads === 0 && took < 60000, took + ' ms ' + JSON.stringify(card));
        check('... the page still answers (its buttons are there and the way to the list works)', await page.isVisible('#all-btn') && (await page.evaluate(() => new Promise((resolve) => { const t0 = performance.now(); setTimeout(() => resolve(performance.now() - t0), 50); }))) < 2000);
        check('... no request of the visit carries a body or asks the site for the map', hits.every((h) => h.method === 'GET' && !h.body) && !hits.some((h) => /^\/maps\/files\//.test(h.url)));
        if (picture) { await sleep(400); await shot(page, picture); }
        await ctx.close();
    }
    // 3b'. the same files for a page that does not count (an older page, a cached one): the page hands the game the map by writing it into the maps folder under its name, and the game, reading THAT copy, counts it too
    const pageCountsNothing = () => {
        let kept;
        Object.defineProperty(window, 'AntsMapFiles', { configurable: true, get() { return kept; }, set(value) { if (value && typeof value.carriedVerdict === 'function') value.carriedVerdict = (carried) => ({ ok: true, bytes: carried.bytes }); kept = value; } });
    };
    for (const [label, name, map, words] of [['30,000 ants', 'Crowd30000.lvl', crowd(30000), /The map in the replay starts 30,000 ants \(the start markers of the four colors\), more than the 8,000 that a replay is set up with/],
                                             ['30,000 plants and 30,000 waypoints', 'Plants30000.lvl', plantsAndWaypoints(30000, 30000), /The map in the replay has 30,000 plants and 30,000 waypoints, so setting it up looks at 900,000,000 pairs of them/]]) {
        const ctx = await ctxOf();
        await ctx.addInitScript(pageCountsNothing);
        const page = await ctx.newPage();
        const started = Date.now();
        await openOn(page, asFile('crowd.antsrep', tools.withMap(FIXTURE, name, map, true)));
        const card = await anyCard(page);
        const took = Date.now() - started;
        check('with the page\'s own count off, ' + label + ' under a name of their own: the REAL game ran (the page handed it the map) and refused it from the maps folder\'s copy, with the count', card.title === 'This replay can’t be played here' && words.test(card.body.replace(/\s+/g, ' ')) && await page.evaluate(() => typeof isReadyToPlay !== 'undefined' && isReadyToPlay === true) && took < 60000, took + ' ms ' + JSON.stringify(card));
        await ctx.close();
    }
    // 3c. a map that the game refuses although the file carries it: the game's reason, in the card, and never the words for a map that the site has not
    for (const [label, bytes, words] of [['markers outside its grid', tools.mapWithGrid(TINY_MAP, 1, 1), /start marker\(s\) lie outside the 1 x 1 grid/],
                                         ['a rules word that no reader knows', tools.mapAsVersion9(TINY_MAP, 2), /the rules word is 2/i]]) {
        const ctx = await ctxOf(), page = await ctx.newPage();
        await openOn(page, asFile('refused.antsrep', tools.withMap(FIXTURE, 'Dune.lvl', bytes, true)));
        const card = await anyCard(page);
        check('a map that the file carries and that the real game refuses (' + label + '): "This replay can’t be played here" with the game\'s own reason, not "This site doesn’t have the map"', card.title === 'This replay can’t be played here' && words.test(card.body) && !/doesn’t have the map|no longer has it/.test(card.title + card.body) && card.downloads === 0, JSON.stringify(card));
        if (label.indexOf('grid') !== -1) { await sleep(400); await shot(page, 'built_real_refusedmap'); }
        await ctx.close();
    }
    for (const [label, map] of untrusted) {
        const ctx = await ctxOf(), page = await ctx.newPage();
        const errors = [];
        page.on('pageerror', (e) => errors.push(String(e.message).slice(0, 100)));
        await openOn(page, asFile('damaged.antsrep', tools.withMap(FIXTURE, 'Dune.lvl', map, true)));
        const verdict = await Promise.race([up(page).then((s1) => 'plays ' + s1.state), anyCard(page).then((c) => 'card: ' + c.title), sleep(110000).then(() => 'neither')]);
        check(label + ': the real game plays it or the page says it cannot, within the start watchdog (' + verdict + ')', verdict !== 'neither');
        check(label + ': the page is still there (the way to the list answers)', await page.isVisible('#all-btn') || await page.isVisible('#rm'));
        await ctx.close();
    }
    // 4. the real game on a phone: the player, and the cards that the real game and the page say
    {
        const pctx = () => browser.newContext(Object.assign({}, PHONE, { baseURL: base }));
        {
            const ctx = await pctx(), page = await ctx.newPage();
            await openOn(page, asFile(NAME, FIXTURE));
            await up(page);
            await page.waitForSelector('#rtag:not([hidden])', { timeout: 60000 });
            await sleep(3000);
            check('on a phone the real game plays the file, and the page does not scroll sideways', (await state(page)).total === 7205 && await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth));
            await shot(page, 'built_real_phone_player');
            await ctx.close();
        }
        for (const [name, file, route, kind] of [['otherversion_older', asFile(NAME, tools.withRules(FIXTURE, 4)), false, null], ['otherversion_newer', asFile(NAME, tools.withRules(FIXTURE, 99)), false, null],
                                                 ['mapmissing', asFile('dune.antsrep', tools.withMap(FIXTURE, 'Dune.lvl', DUNE, false)), true, null], ['crowd', asFile('crowd.antsrep', tools.withMap(FIXTURE, 'Crowd.lvl', tools.mapWithAnts(TINY_MAP, 65535), true)), false, null],
                                                 ['pairs', asFile('plants.antsrep', tools.withMap(FIXTURE, 'Plants.lvl', plantsAndWaypoints(65400, 65535), true)), false, null],
                                                 ['refusedmap', asFile('refused.antsrep', tools.withMap(FIXTURE, 'Dune.lvl', tools.mapWithGrid(TINY_MAP, 1, 1), true)), false, null]]) {
            const ctx = await pctx(), page = await ctx.newPage();
            if (route && (await fetch(base + '/maps/files/Dune.lvl').then((r) => r.status, () => 0)) !== 404) await page.route('**/maps/files/Dune.lvl', (r) => r.fulfill({ status: 404, body: 'not found' }));
            await openOn(page, file);
            const card = await anyCard(page);
            check('on a phone the real game and the page say the card "' + card.title + '" (' + name + ') and the page does not scroll sideways', card.title.length > 0 && await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), card.title);
            await sleep(400);
            await shot(page, 'built_real_phone_' + name);
            await ctx.close();
        }
    }
}

main().then(() => {
    console.log('web_replay_open_browser: ' + checks + ' checks, ' + failures + ' failures');
    process.exit(failures ? 1 : 0);
}, (e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
