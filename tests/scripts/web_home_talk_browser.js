// OPT-IN, not part of ./run_tests.sh: the emoji picker and the voice button of the chat (web/home/talk.js, talk.css, docs/HOME_SCREEN.md "Emoji and voice") in a REAL browser (Chromium through Playwright), on the real front
// page (web/lobby.html with web/home/) and the FAKE hub (tests/scripts/fake_hub.js) that shares the page's address. The speech recognition is a FAKE (an init script puts a class in its place: no microphone, no sound).
// For every size (1280 x 800, 701 x 800, a phone 390 x 844, 320 x 640 and a phone held sideways 568 x 320) it checks, with a writer in the chat:
//   1. the buttons stand in the field's row beside Send, each at least 44 x 44 px, inside the window, the page does not scroll sideways, and the field keeps room to type (at least 90 px);
//   2. the picker opens above the field inside the dock, never over the field or Send, inside the window, with a scroll of its own; a pick goes in at the caret; the dock is still open; Escape closes it and the focus
//      is back in the field;
//   3. the voice: a press starts the (fake) recognition, the words come into the field, the button looks pressed, a second press stops it, an error is a plain line, and nothing is sent until Send is pressed;
//   4. the keyboard: Tab goes field, emoji, microphone, Send; Enter opens the picker and puts the focus on an emoji;
//   5. a line with emoji goes out through Send as one say frame with the same text, and the hub's rules would keep it as it is (the check in node does that); the page's own errors are none;
//   6. a browser with no speech recognition: no microphone button, the emoji button stays; a nameless visitor (read only) has neither.
//   --shots DIR takes the pictures (talk_<scene>_<size>.png: idle, picker, listening, error, unsupported); with TALK_SCALE=3 in the environment the page is drawn at that scale and a close-up of the two buttons is taken too (talk_close_*).
//
//   node tests/scripts/web_home_talk_browser.js [--web DIR] [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be made), 1 a check failed.
'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');
const { startHub, line } = require('./fake_hub.js');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the emoji picker and the voice button were NOT run in a real browser'); process.exit(0); }

const ROOT = path.resolve(__dirname, '..', '..');
const arg = (name) => { const i = process.argv.indexOf(name); return i === -1 ? null : process.argv[i + 1]; };
const WEB = path.resolve(arg('--web') || path.join(ROOT, 'web'));
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml' };
const SIZES = [['desktop', 1280, 800, false], ['w701', 701, 800, false], ['w900', 900, 800, false], ['w1000', 1000, 800, false], ['w1100', 1100, 800, false], ['phone', 390, 844, true], ['w320', 320, 640, true], ['side', 568, 320, true]];
const HISTORY = [line(1, 'pebble', 'anyone up for Islands?'), line(2, 'clover', 'I have a room open'), line(3, 'juniper', 'joining!')];
const GRIN = String.fromCodePoint(0x1F600);

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
        const text = TYPES[path.extname(full)] && /html/.test(TYPES[path.extname(full)]) ? fs.readFileSync(full, 'utf8').replace(/@@[A-Z_]+@@/g, '') : null;
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
        res.end(text !== null ? text : fs.readFileSync(full));
    });
    return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({ server, requests, url: 'http://127.0.0.1:' + server.address().port })));
}

const FAKE = () => {           // (the browser's recognizer, in a page that has none of its own to speak of: a class that the test drives)
    window.__recs = [];
    class FakeRec { constructor() { window.__recs.push(this); this.started = 0; this.stopped = 0; this.aborted = 0; } start() { this.started++; } stop() { this.stopped++; } abort() { this.aborted++; } }
    Object.defineProperty(window, 'SpeechRecognition', { value: FakeRec, configurable: true });
    Object.defineProperty(window, 'webkitSpeechRecognition', { value: FakeRec, configurable: true });
};
const NONE = () => {
    Object.defineProperty(window, 'SpeechRecognition', { value: undefined, configurable: true });
    Object.defineProperty(window, 'webkitSpeechRecognition', { value: undefined, configurable: true });
};

async function main() {
    const site = await serve();
    const hub = await startHub({ server: site.server, login: true, you: { name: 'maple.leaf', mod: false }, history: HISTORY, online: 7 });
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the emoji picker and the voice button were NOT run in a real browser'); await hub.close(); site.server.close(); process.exit(0); }
    const errors = [];
    const shot = async (page, scene, size) => { if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, 'talk_' + scene + '_' + size + '.png') }); if (process.env.TALK_SCALE) { const t = await page.$('.gt-tools'); if (t) await t.screenshot({ path: path.join(SHOTS, 'talk_close_' + scene + '_' + size + '.png') }); } } };
    const rect = (page, selector) => page.evaluate((s) => { const e = document.querySelector(s); if (!e || !e.getClientRects().length) return null; const r = e.getBoundingClientRect(); return { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height), right: Math.round(r.right), bottom: Math.round(r.bottom) }; }, selector);
    const field = (page) => page.evaluate(() => document.querySelector('.gc-input').value);
    const active = (page) => page.evaluate(() => { const a = document.activeElement; return a ? (a.className || a.tagName) : ''; });
    async function open(w, h, phone, o) {
        o = o || {};
        const context = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: process.env.TALK_SCALE ? Number(process.env.TALK_SCALE) : 1 });
        const page = await context.newPage();
        await page.addInitScript(o.none ? NONE : FAKE);
        page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
        await page.goto(site.url + '/', { waitUntil: 'load' });
        await page.waitForSelector('#gchat', { timeout: 5000 }).catch(() => {});
        if (phone) { await page.click('.htab.t-chat').catch(() => {}); await sleep(250); }
        await page.waitForFunction(() => { const f = document.querySelector('#gchat .gc-form'); return !!f && !f.hidden; }, null, { timeout: 8000 }).catch(() => {});
        await page.waitForSelector('.gt-emoji', { state: 'visible', timeout: 6000 }).catch(() => {});
        await sleep(150);
        return { page, context };
    }

    for (const [size, w, h, phone] of SIZES) {
        const { page, context } = await open(w, h, phone);
        const name = size + ': ';
        check(name + 'the page asked for the two files of the buttons, after the dock', site.requests.indexOf('/home/talk.js') !== -1 && site.requests.indexOf('/home/talk.css') !== -1 && site.requests.indexOf('/home/talk.js') > site.requests.indexOf('/home/chat.js'));
        const emoji = await rect(page, '.gt-emoji'), mic = await rect(page, '.gt-mic'), input = await rect(page, '.gc-input'), send = await rect(page, '.gc-form .gc-send:not(.gc-login)');
        check(name + 'both buttons and Send are shown', !!emoji && !!mic && !!send && !!input, JSON.stringify([emoji, mic, send, input]));
        if (!emoji || !mic || !send || !input) { await context.close(); continue; }
        check(name + 'each button is at least 44 x 44 and inside the window', [emoji, mic, send].every((r) => r.w >= 44 && r.h >= 44 && r.x >= 0 && r.right <= w && r.y >= 0 && r.bottom <= h), JSON.stringify([emoji, mic, send]));
        const under = emoji.y >= input.bottom - 2;       // (a narrow form: the field and Send share the first row and the two buttons have the row under it)
        const dockBox = await rect(page, '#gchat');
        check(name + 'the buttons are ' + (under ? 'in a row under the field and Send' : 'between the field and Send') + ', in order, without overlap, and inside the dock', (under ? emoji.x >= input.x - 1 && emoji.right <= mic.x + 1 && mic.right <= send.right + 1 && Math.abs((input.y + input.h / 2) - (send.y + send.h / 2)) <= 12
            : input.right <= emoji.x + 1 && emoji.right <= mic.x + 1 && mic.right <= send.x + 1 && Math.abs((emoji.y + emoji.h / 2) - (input.y + input.h / 2)) <= 12) && emoji.bottom <= dockBox.bottom + 1 && mic.bottom <= dockBox.bottom + 1 && send.bottom <= dockBox.bottom + 1, JSON.stringify([input, emoji, mic, send, dockBox]));
        const logBox = await rect(page, '.gc-log');
        console.log('   ' + size + ': log ' + (logBox && logBox.h) + ' px, dock ' + (dockBox && dockBox.h) + ' px');
        check(name + 'the log keeps room to be read (at least 60 px high, 40 px in a window under 400 px high)', !!logBox && logBox.h >= (h < 400 ? 40 : 60), JSON.stringify(logBox));
        console.log('   ' + size + ': ' + (under ? 'row under' : 'beside') + ', field ' + input.w + ' px');
        check(name + 'the field keeps room to type (90 px at least)', input.w >= 90, input.w);
        check(name + 'the page does not scroll sideways', await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth && document.body.scrollWidth <= window.innerWidth));
        const labels = await page.evaluate(() => [...document.querySelectorAll('.gt-btn')].map((b) => [b.getAttribute('aria-label'), b.title]));
        check(name + 'the labels and tooltips', JSON.stringify(labels) === JSON.stringify([['Emoji', 'Emoji'], ['Voice to text: your browser\'s speech service hears you', 'Voice to text: your browser\'s speech service hears you']]), JSON.stringify(labels));
        await shot(page, 'idle', size);

        // the picker
        await page.fill('.gc-input', 'hi ');
        await page.click('.gt-emoji');
        await sleep(100);
        const pick = await rect(page, '.gt-pick'), inp = await rect(page, '.gc-input'), snd = await rect(page, '.gc-form .gc-send:not(.gc-login)'), dock = await rect(page, '#gchat');
        check(name + 'the picker is open and expanded; its buttons are at least 44 px', !!pick && (await page.getAttribute('.gt-emoji', 'aria-expanded')) === 'true' && (await page.evaluate(() => [...document.querySelectorAll('.gt-e')].slice(0, 5).every((b) => b.getBoundingClientRect().width >= 44 && b.getBoundingClientRect().height >= 44))));
        if (pick && inp && dock) {
            check(name + 'the picker is inside the dock and the window, and above the field (nothing covers the field or Send)', pick.x >= 0 && pick.right <= w && pick.y >= 0 && pick.bottom <= inp.y + 1 && pick.bottom <= snd.y + snd.h && inp.bottom <= h && snd.bottom <= h && pick.right <= dock.right + 1 && pick.x >= dock.x - 1, JSON.stringify({ pick, inp, snd, dock }));
            check(name + 'the picker is not taller than 180 px and has a scroll of its own', pick.h <= 180 && (await page.evaluate(() => { const p = document.querySelector('.gt-pick'); return p.scrollHeight > p.clientHeight && getComputedStyle(p).overflowY === 'auto'; })));
        }
        check(name + 'the focus is on an emoji in the picker', (await active(page)) === 'gt-e');
        await shot(page, 'picker', size);
        await page.click('.gt-e >> nth=0');
        check(name + 'a pick goes in at the end of the field (the caret was there) and the picker stays', (await field(page)) === 'hi ' + GRIN && (await rect(page, '.gt-pick')) !== null);
        await page.keyboard.press('ArrowRight');
        check(name + 'the arrow key moves to the next emoji', (await active(page)) === 'gt-e' && (await page.evaluate(() => document.activeElement.textContent !== document.querySelector('.gt-e').textContent)));
        await page.keyboard.press('Escape');
        await sleep(100);
        check(name + 'Escape closes the picker, the focus is in the field, and the chat is still open', (await rect(page, '.gt-pick')) === null && (await active(page)) === 'gc-input' && (await rect(page, '.gc-input')) !== null && (await page.getAttribute('.gt-emoji', 'aria-expanded')) === 'false');

        // the voice
        await page.fill('.gc-input', '');
        await page.click('.gt-mic');
        await sleep(50);
        await page.evaluate(() => { const r = window.__recs[window.__recs.length - 1]; r.onstart({}); r.onresult({ results: [[{ transcript: 'hello there' }]] }); });
        await sleep(100);
        const heard = await page.evaluate(() => ({ n: window.__recs.length, started: window.__recs[0].started, lang: window.__recs[0].lang, interim: window.__recs[0].interimResults, value: document.querySelector('.gc-input').value, pressed: document.querySelector('.gt-mic').getAttribute('aria-pressed'), say: document.querySelector('.gt-say').textContent, sayShown: document.querySelector('.gt-say').getClientRects().length > 0, hot: document.querySelector('.gt-mic').classList.contains('gt-hot') }));
        check(name + 'a press starts one recognizer in the page\'s language with interim words; the words are in the field; the button looks pressed; the line says it listens', heard.n === 1 && heard.started === 1 && heard.lang === 'en-US' && heard.interim === true && heard.value === 'hello there' && heard.pressed === 'true' && heard.hot && heard.sayShown && /Listening/.test(heard.say), JSON.stringify(heard));
        check(name + 'nothing was sent by the voice', hub.received.filter((m) => m && m.t === 'say').length === 0);
        const inp2 = await rect(page, '.gc-input'), say = await rect(page, '.gt-say');
        check(name + 'the line of words is above the field and inside the window; the field is still in the window', !!say && say.bottom <= inp2.y + 1 && say.right <= w && inp2.bottom <= h, JSON.stringify([say, inp2]));
        await shot(page, 'listening', size);
        await page.click('.gt-mic');
        check(name + 'a second press stops it (stop, not abort); the button is not pressed any more', await page.evaluate(() => window.__recs[0].stopped === 1 && window.__recs[0].aborted === 0 && document.querySelector('.gt-mic').getAttribute('aria-pressed') === 'false'));
        await page.evaluate(() => window.__recs[0].onend({}));
        await page.click('.gt-mic');
        await page.evaluate(() => { const r = window.__recs[1]; r.onstart({}); r.onerror({ error: 'not-allowed' }); r.onend({}); });
        await sleep(100);
        const err = await page.evaluate(() => ({ say: document.querySelector('.gt-say').textContent, shown: document.querySelector('.gt-say').getClientRects().length > 0, pressed: document.querySelector('.gt-mic').getAttribute('aria-pressed') }));
        check(name + 'a refused microphone is a plain line, and the button is idle', err.shown && err.say === 'The microphone is blocked for this page.' && err.pressed === 'false', JSON.stringify(err));
        await shot(page, 'error', size);

        // the keyboard
        await page.fill('.gc-input', 'a');
        await page.focus('.gc-input');
        const order = [];
        for (let i = 0; i < 3; i++) { await page.keyboard.press('Tab'); order.push(await active(page)); }
        check(name + 'Tab goes from the field to Emoji, Voice and Send', /gt-emoji/.test(order[0]) && /gt-mic/.test(order[1]) && /gc-send/.test(order[2]), JSON.stringify(order));
        await page.focus('.gt-emoji');
        await page.keyboard.press('Enter');
        await sleep(100);
        check(name + 'Enter on the emoji button opens the picker and puts the focus on an emoji', (await active(page)) === 'gt-e' && (await rect(page, '.gt-pick')) !== null);
        await page.keyboard.press('Enter');
        check(name + 'Enter on an emoji adds it (at the caret: after the "a")', (await field(page)).length === 3 && (await field(page)).startsWith('a'), JSON.stringify(await field(page)));
        await page.keyboard.press('Escape');

        // Send
        await page.fill('.gc-input', 'ants ' + GRIN);
        await page.click('.gc-form .gc-send:not(.gc-login)');
        await sleep(300);
        const said = hub.received.filter((m) => m && m.t === 'say');
        check(name + 'Send puts one say frame out with the same text, emoji and all', said.length >= 1 && said[said.length - 1].text === 'ants ' + GRIN, JSON.stringify(said.slice(-1)));
        hub.received.length = 0;
        await context.close();
    }

    // no speech recognition: no microphone; a visitor who may not write: nothing
    for (const [size, w, h, phone] of SIZES.filter((x) => x[0] === 'desktop' || x[0] === 'phone')) {
        const { page, context } = await open(w, h, phone, { none: true });
        check(size + ' (no speech recognition): the emoji button is there and the microphone is not', (await rect(page, '.gt-emoji')) !== null && (await page.$('.gt-mic')) === null);
        const input = await rect(page, '.gc-input'), emoji = await rect(page, '.gt-emoji'), send = await rect(page, '.gc-form .gc-send:not(.gc-login)');
        check(size + ' (no speech recognition): the field has more room', !!input && !!emoji && !!send && emoji.right <= send.x + 1 && input.w >= 90);
        await shot(page, 'unsupported', size);
        await context.close();
    }
    hub.set({ you: null, login: false });
    {
        const context = await browser.newContext({ viewport: { width: 1280, height: 800 } });
        const page = await context.newPage();
        await page.addInitScript(FAKE);
        page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
        await page.goto(site.url + '/', { waitUntil: 'load' });
        await sleep(1500);
        check('a chat that is read only: neither button is shown', (await rect(page, '.gt-emoji')) === null && (await rect(page, '.gt-mic')) === null);
        await context.close();
    }
    check('the page\'s own errors: none', errors.length === 0, errors.join(' | '));
    await browser.close();
    await hub.close();
    site.server.close();
    console.log(failures === 0 ? 'OK: ' + checks + ' checks' : failures + ' of ' + checks + ' checks FAILED');
    process.exit(failures === 0 ? 0 : 1);
}
main().catch((e) => { console.log('FAIL: the script broke: ' + (e && e.stack || e)); process.exit(1); });
