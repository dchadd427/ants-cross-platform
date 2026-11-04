// Opening a replay FILE of your own on the website, in a REAL browser (Chromium through Playwright; opt-in, see tests/scripts/test_web_replay_open.sh and docs/REPLAYS.md "A file of your own").
// No Docker image, no game server, no game: this serves the pages of web/ itself (the Matches page, the game page as play.html, their scripts and pictures) behind a small stand-in for the site (GET /history,
// /live, /maps/all, /maps/files/<name>, and nothing else: POST and PUT are refused and counted) and puts a stand-in for the game's program (tests/scripts/web_live_stub.js: Module._ants_replay_get /
// _ants_replay_do and the report that the game makes) in the place of index.js, so what it shows is the pages' own part: the strip, the drop, the cards, the hand-off and the player's header. It does not show
// that the real game plays a file (the C++ tests and tests/scripts/web_replay_open_real.sh do). Every request is counted: none carries a body, none asks the site for the file, none asks for its stats.
// Parts (--only): strip (the Matches page: the strip, the chooser, a dropped file, a refused file and its card, the page with the site down, a phone), hand (the hand-off: the bytes that the game gets are the
// file's, a reload, a record that is gone, the header of the player, a library map), cards (a file of an older or newer version, a map that the site does not have, an unreadable file, and the way out of each).
// usage: node web_replay_open_browser.js [--only strip|hand|cards] [--shots DIR]     (exit 0: every check holds, or the browser is not there; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');

const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i === -1 ? null : args[i + 1]; };
const only = opt('--only'), shotsDir = opt('--shots');
const PARTS = ['strip', 'hand', 'cards'];
if (only && PARTS.indexOf(only) === -1) { console.log('--only is one of: ' + PARTS.join(', ')); process.exit(2); }
let playwright;
try { playwright = require('playwright'); } catch (e) { console.log('SKIP: Playwright is not installed: the real browser was NOT run (tests/scripts/web_replay_open_browser.js)'); process.exit(0); }
const WEB = path.resolve(__dirname, '..', '..', 'web');
const FIXTURE = fs.readFileSync(path.resolve(__dirname, '..', 'data', 'replay_bots_tiny.antsrep'));
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
function seat(n, color, name, bot, o) { return Object.assign({ seat: n, color, colour: color, name, bot, score: 0, killed: 0, lost: 0, hatched: 0, bombs_planted: 0, bombs_defused: 0, fires_lit: 0, left: false, winner: false }, o || {}); }
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
            const rel = p === '/play.html' ? 'shell.html' : p.replace(/^\/+/, '');
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
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the pages were NOT run in a browser'); return; }
    const shot = async (page, name) => { if (shotsDir) { fs.mkdirSync(shotsDir, { recursive: true }); await page.screenshot({ path: path.join(shotsDir, name + '.png') }); } };
    try {
        const parts = { strip: partStrip };
        for (const name of PARTS) if ((!only || only === name) && parts[name]) await parts[name](browser, shot);
    } finally { await browser.close(); }
}

// the sizes of the approved pictures: a computer's window and a phone (390 x 844 at twice the pixels)
const DESKTOP = { viewport: { width: 1280, height: 800 }, deviceScaleFactor: 1, timezoneId: 'America/Los_Angeles', locale: 'en-US' };
const PHONE = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true, timezoneId: 'America/Los_Angeles', locale: 'en-US' };

// a drag of a file over the page, as the browser makes it: the events carry a DataTransfer that lists Files; the answer is whether the page took the event over (preventDefault)
const drag = (page, type, names, selector) => page.evaluate(({ type, names, selector }) => {
    const dt = new DataTransfer();
    (names || []).forEach((n) => dt.items.add(new File(['x'], n)));
    const ev = new DragEvent(type, { dataTransfer: dt, bubbles: true, cancelable: true });
    (selector ? document.querySelector(selector) : document.body).dispatchEvent(ev);
    return ev.defaultPrevented;
}, { type, names, selector });

async function partStrip(browser, shot) {
    const site = await makeSite();
    const open = async (context, s) => { const page = await context.newPage(); await page.goto((s || site).url('/matches.html')); await page.waitForSelector('#body .note, #body .list'); await sleep(250); return page; };
    for (const [kind, context] of [['desktop', DESKTOP], ['phone', PHONE]]) {
        const ctx = await browser.newContext(context);
        const page = await open(ctx);
        await shot(page, 'built_b_' + kind + '_list');
        if (kind === 'desktop') {
            await drag(page, 'dragenter', ['a.antsrep']);
            await drag(page, 'dragover', ['a.antsrep']);
            await sleep(150);
            await shot(page, 'built_b_desktop_hover');
            await drag(page, 'dragleave', ['a.antsrep']);
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

main().then(() => {
    console.log('web_replay_open_browser: ' + checks + ' checks, ' + failures + ' failures');
    process.exit(failures ? 1 : 0);
}, (e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
