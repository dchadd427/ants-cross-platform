// OPT-IN, not part of ./run_tests.sh: the emoji picker and the voice button of the chat (web/home/talk.js, talk.css, docs/HOME_CHAT.md "Emoji and voice") in a REAL browser (Chromium through Playwright), on the real front
// page (web/lobby.html with web/home/) and the FAKE hub (tests/scripts/fake_hub.js) that shares the page's address. The speech recognition is a FAKE (an init script puts a class in its place: no microphone, no sound).
// For every size (1280 x 800, 701 x 800, a phone 390 x 844, 320 x 640 and a phone held sideways 568 x 320) it checks, with a writer in the chat:
//   1. the composer is one box in every window, ONE row when it is empty (the field, the buttons, Send), each at least 44 x 44 px; a long line makes two rows (the field on top over the whole width, the buttons and Send under it; a window under 501 px high stays one row), and Send or a deleted line brings the one row back; the dock is not taller for one row; inside the window, the page does not scroll sideways, and the field keeps room to type (at least 90 px);
//      a draft of 1,000 characters (one word) keeps the field at four lines at the most and scrolls inside itself, and a line from the hub that is one word of 1,000 characters wraps inside the log (the dock keeps its width, nothing scrolls sideways);
//   2. the picker (nine tabs, a search field, the recently used row, the emoji of a group, a name line): a box above the field at the right in a wide window, a row of the form's width over a low window, a sheet at the
//      bottom of a phone upright (52 percent of the height, the log shorter, the field and Send above it); never over the field or Send, inside the window, nothing scrolls sideways; its buttons are at least 44 px high; a
//      tab shows its group; the search finds the cat and Enter puts it in without sending; a pick goes in at the caret and is kept (it comes back after a reload); the arrow keys move by row; the dock is still open;
//      Escape closes it and the focus is back in the field;
//   3. the voice: a press starts the (fake) recognition, the words come into the field, the button looks pressed, a second press stops it, an error is a plain line, and nothing is sent until Send is pressed;
//   4. the keyboard: Tab goes field, emoji, microphone, Send; Enter opens the picker and puts the focus in the search field, the down arrow goes into the emoji;
//   5. a line with emoji goes out through Send as one say frame with the same text, and the hub's rules would keep it as it is (the check in node does that); the page's own errors are none;
//   6. a browser with no speech recognition: no microphone button, the emoji button stays; a nameless visitor (read only) has neither.
//   --shots DIR takes the pictures (talk_<scene>_<size>.png: idle, picker, tab, search, listening, error, unsupported); with TALK_SCALE=3 in the environment the page is drawn at that scale and a close-up of the two buttons is taken too (talk_close_*).
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
const SIZES = [['desktop', 1280, 800, false], ['w701', 701, 800, false], ['w900', 900, 800, false], ['w1000', 1000, 800, false], ['w1100', 1100, 800, false], ['phone', 390, 844, true], ['w320', 320, 640, true], ['side', 568, 320, true], ['land844', 844, 390, false], ['land932', 932, 430, false]];
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
        if (phone || w <= 700 || h <= 500) { await page.click('.htab.t-chat').catch(() => {}); await sleep(250); }       // (a low window is a phone's layout: the Chat view)
        await page.waitForFunction(() => { const f = document.querySelector('#gchat .gc-form'); return !!f && !f.hidden; }, null, { timeout: 8000 }).catch(() => {});
        await page.waitForSelector('.gt-emoji', { state: 'visible', timeout: 6000 }).catch(() => {});
        await sleep(150);
        return { page, context };
    }

    for (const [size, w, h, phone] of SIZES) {
        const { page, context } = await open(w, h, phone);
        const name = size + ': ';
        check(name + 'the page asked for the two files of the buttons', site.requests.indexOf('/home/talk.js') !== -1 && site.requests.indexOf('/home/talk.css') !== -1 && site.requests.indexOf('/home/chat.js') !== -1);
        const emoji = await rect(page, '.gt-emoji'), mic = await rect(page, '.gt-mic'), input = await rect(page, '.gc-input'), send = await rect(page, '.gc-form .gc-send:not(.gc-login)');
        check(name + 'both buttons and Send are shown', !!emoji && !!mic && !!send && !!input, JSON.stringify([emoji, mic, send, input]));
        if (!emoji || !mic || !send || !input) { await context.close(); continue; }
        check(name + 'each button is at least 44 x 44 and inside the window', [emoji, mic, send].every((r) => r.w >= 44 && r.h >= 44 && r.x >= 0 && r.right <= w && r.y >= 0 && r.bottom <= h), JSON.stringify([emoji, mic, send]));
        const dockBox = await rect(page, '#gchat');
        const formBox = await rect(page, '#gchat .gc-form');
        const oneRow = (e, m, i, sd) => i.right <= e.x + 1 && e.right <= m.x + 1 && m.right <= sd.x + 1 && [e, m, sd].every((q) => Math.abs((q.y + q.h / 2) - (i.y + i.h / 2)) <= 3);
        check(name + 'the box starts as ONE row, empty: the field, then the two buttons and Send at its right, the field one line high (44 px), without the class of two rows', oneRow(emoji, mic, input, send) && input.h === 44 && formBox.h <= 60 && !(await page.evaluate(() => document.querySelector('#gchat .gc-form').classList.contains('gt-tall'))), JSON.stringify([emoji, mic, input, send, formBox]));
        check(name + 'the buttons and Send are inside the dock', emoji.bottom <= dockBox.bottom + 1 && mic.bottom <= dockBox.bottom + 1 && send.bottom <= dockBox.bottom + 1, JSON.stringify([emoji, mic, send, dockBox]));
        const logBox = await rect(page, '.gc-log');
        console.log('   ' + size + ': log ' + (logBox && logBox.h) + ' px, dock ' + (dockBox && dockBox.h) + ' px');
        check(name + 'the log keeps room to be read (at least 60 px high, 30 px in a window under 500 px high)', !!logBox && logBox.h >= (h < 500 ? 30 : 60), JSON.stringify(logBox));
        console.log('   ' + size + ': one row, field ' + input.w + ' px');
        check(name + 'the field keeps room to type (90 px at least)', input.w >= 90, input.w);
        const fieldLook = await page.evaluate(() => { const f = document.querySelector('.gc-input'), c = getComputedStyle(f); return { font: parseFloat(c.fontSize), h: f.getBoundingClientRect().height }; });
        check(name + 'the field\'s text is 16 px or more (an iPhone does not zoom into it) and the field is 44 px high or more', fieldLook.font >= 16 && fieldLook.h >= 44, JSON.stringify(fieldLook));
        check(name + 'the page does not scroll sideways', await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth && document.body.scrollWidth <= window.innerWidth));
        const labels = await page.evaluate(() => [...document.querySelectorAll('#gchat .gt-btn')].map((b) => [b.getAttribute('aria-label'), b.title]));
        check(name + 'the labels and tooltips', JSON.stringify(labels) === JSON.stringify([['Emoji', 'Emoji'], ['Voice to text: your browser\'s speech service hears you', 'Voice to text: your browser\'s speech service hears you']]), JSON.stringify(labels));
        await shot(page, 'idle', size);

        // one row until the words need more: a short line, a long line, one line at a time, Send, a deleted line
        const emptyDock = await rect(page, '#gchat'), emptyLog = await rect(page, '.gc-log');
        const rows = async () => {
            const e = await rect(page, '.gt-emoji'), m = await rect(page, '.gt-mic'), i = await rect(page, '.gc-input'), sd = await rect(page, '.gc-form .gc-send:not(.gc-login)');
            const one = oneRow(e, m, i, sd), two = e.y >= i.bottom - 2 && sd.y >= i.bottom - 2 && e.right <= m.x + 1 && m.right <= sd.x + 1 && e.x >= i.x + 0.25 * i.w && sd.right <= i.right + 12;
            return { one, two, field: i.h };
        };
        await page.fill('.gc-input', 'hi there');
        await shot(page, 'short', size);
        let st = await rows();
        check(name + 'a short line: still one row, the field 44 px high', st.one && st.field === 44, JSON.stringify(st));
        await page.fill('.gc-input', 'This is a long message that wraps onto more than one line, so the box gets a second row for the buttons and Send. '.repeat(2).slice(0, 199));
        await sleep(100);
        await shot(page, 'long', size);
        st = await rows();
        const longDock = await rect(page, '#gchat'), longLog = await rect(page, '.gc-log');
        if (h <= 500) check(name + 'a low window: a long line is still one row (the field scrolls inside its 44 px)', st.one && st.field === 44, JSON.stringify(st));
        else {
            check(name + 'a long line: two rows (the field on top over the whole width, the buttons and Send under it at the right), the field taller than 44 px', st.two && !st.one && st.field > 44, JSON.stringify(st));
            if (w > 700) check(name + 'two rows: the dock is taller than with one row, and the log keeps room (60 px at least)', longDock.h > emptyDock.h && longLog.h >= 60, JSON.stringify([emptyDock, longDock, emptyLog, longLog]));
        }
        await page.fill('.gc-input', 'w'.repeat(1000));
        await sleep(100);
        const draft = await page.evaluate(() => { const f = document.querySelector('.gc-input'); return { h: f.getBoundingClientRect().height, scrolls: f.scrollHeight > f.clientHeight, sideways: document.documentElement.scrollWidth > window.innerWidth }; });
        check(name + 'a draft of 1,000 characters (one word): the field stays at four lines at the most (110 px) and scrolls inside itself, and nothing scrolls sideways', draft.h <= 111 && draft.scrolls && !draft.sideways, JSON.stringify(draft));
        const dockWidth = (await rect(page, '#gchat')).w;
        hub.say('long.word', 'W'.repeat(1000));
        await sleep(200);
        const wrapped = await page.evaluate(() => { const l = document.querySelector('#gchat .gc-log'); return { width: document.getElementById('gchat').getBoundingClientRect().width, log: l.scrollWidth <= l.clientWidth, page: document.documentElement.scrollWidth <= window.innerWidth, last: l.lastElementChild.innerText.length }; });
        check(name + 'a line that is one word of 1,000 characters wraps inside the log: the dock keeps its width and nothing scrolls sideways', Math.abs(wrapped.width - dockWidth) < 1 && wrapped.log && wrapped.page && wrapped.last > 1000, JSON.stringify([dockWidth, wrapped]));
        const sweep = await page.evaluate(() => {      // (one letter more, then one less, each measured three times: the class never flips by itself, and it comes and goes at one length)
            const f = document.querySelector('.gc-input'), form = f.form, tall = () => form.classList.contains('gt-tall'), up = [], down = [], steady = [];
            const at = (n) => { f.value = 'x'.repeat(n); const a = []; for (let k = 0; k < 3; k++) { f.dispatchEvent(new Event('input', { bubbles: true })); a.push(tall()); } steady.push(a[0] === a[1] && a[1] === a[2]); return a[2]; };
            for (let n = 1; n <= 150; n++) up.push(at(n));
            for (let n = 150; n >= 1; n--) down.push(at(n));
            const first = up.indexOf(true), back = down.indexOf(false);
            return { steady: steady.every(Boolean), once: first > 0 && up.slice(first).every(Boolean) && up.slice(0, first).every((x) => !x), downOnce: back > 0 && down.slice(back).every((x) => !x) && down.slice(0, back).every(Boolean), at: first + 1, goes: 150 - back + 1 };
        });
        if (h > 500) check(name + 'no toggling on the keys: a length is the same on every measure, one change going up and one going down, at the same length', sweep.steady && sweep.once && sweep.downOnce && sweep.at === sweep.goes, JSON.stringify(sweep));
        await page.fill('.gc-input', 'y'.repeat(120));
        await page.focus('.gc-input');
        await page.keyboard.press('Enter');
        await sleep(150);
        st = await rows();
        check(name + 'after Send: the field is empty and the box is one row again', (await field(page)) === '' && st.one && st.field === 44, JSON.stringify([await field(page), st]));
        hub.received.length = 0;       // (the line that was sent above is not the voice's)
        await page.fill('.gc-input', 'z'.repeat(120));
        await page.fill('.gc-input', '');
        st = await rows();
        check(name + 'the words deleted: one row again', st.one && st.field === 44, JSON.stringify(st));
        const backDock = await rect(page, '#gchat');
        check(name + 'one row again: the dock is as low as it was empty', backDock.h === emptyDock.h, JSON.stringify([emptyDock, backDock]));

        // the picker
        const mode = h <= 500 ? 'low' : w <= 700 ? 'sheet' : 'pop';
        await page.fill('.gc-input', 'hi ');
        await page.evaluate(() => { try { localStorage.removeItem('ants.emoji.recent'); } catch (e) { /* none */ } });
        const logBefore = await rect(page, '.gc-log');
        await page.click('.gt-emoji');
        await sleep(150);
        const pick = await rect(page, '.gt-pick'), inp = await rect(page, '.gc-input'), snd = await rect(page, '.gc-form .gc-send:not(.gc-login)'), dock = await rect(page, '#gchat'), formBox2 = await rect(page, '#gchat .gc-form');
        check(name + 'the picker is open and expanded, in the place of its kind (' + mode + ')', !!pick && (await page.getAttribute('.gt-emoji', 'aria-expanded')) === 'true' && (await page.evaluate(() => document.querySelector('.gt-pick').className.split(' ').filter((c) => /^gt-(pop|low|sheet)$/.test(c)).join())) === 'gt-' + mode);
        const parts = await page.evaluate(() => {
            const tabs = [...document.querySelectorAll('.gt-tab')].map((t) => { const r = t.getBoundingClientRect(); return [Math.round(r.width), Math.round(r.height), t.getAttribute('aria-label')]; });
            const q = document.querySelector('.gt-q'), qr = q.getBoundingClientRect(), cells = [...document.querySelectorAll('.gt-e')].slice(0, 8).map((b) => { const r = b.getBoundingClientRect(); return [r.width, r.height]; });
            const cols = getComputedStyle(document.querySelector('.gt-page .gt-grid')).gridTemplateColumns.split(' ').length;
            return { tabs, q: [Math.round(qr.width), Math.round(qr.height), parseFloat(getComputedStyle(q).fontSize)], cells, cols, foot: !!document.querySelector('.gt-foot').getClientRects().length, search: q.getAttribute('placeholder') };
        });
        check(name + 'nine tabs in the window, each at least 44 high (and 30 wide), the search field 44 high with text of 16 px, the emoji at least 44 x 44', parts.tabs.length === 9 && parts.tabs.every((t) => t[1] >= 44 && t[0] >= 30) && parts.q[1] >= 44 && parts.q[2] >= 16 && parts.cells.every((c) => c[0] >= 44 && c[1] >= 44), JSON.stringify(parts));
        if (pick && inp && dock) {
            check(name + 'the picker is inside the window and above the field (nothing covers the field or Send)', pick.x >= 0 && pick.right <= w + 0.5 && pick.y >= 0 && (mode === 'sheet' ? pick.y >= inp.bottom - 1 && pick.y >= snd.bottom - 1 : pick.bottom <= inp.y + 1) && inp.bottom <= h && snd.bottom <= h, JSON.stringify([pick, inp, snd]));
            if (mode === 'pop') check(name + 'a box: 380 px wide at most, 400 high at most (120 at least), its right edge level with the form\'s', pick.w <= 380.5 && pick.h <= 400.5 && pick.h >= 120 && Math.abs(pick.right - formBox2.right) <= 3, JSON.stringify([pick, formBox2]));
            if (mode === 'low') check(name + 'a row over a low window: as wide as the form, 6 px under the top, above the field', Math.abs(pick.x - formBox2.x) <= 2 && Math.abs(pick.w - formBox2.w) <= 4 && pick.y <= 8 && pick.bottom <= formBox2.y, JSON.stringify([pick, formBox2]));
            if (mode === 'sheet') {
                const bar = await page.evaluate(() => { const t = document.querySelector('.htabs'); return { strip: !!(t && t.getClientRects().length), cls: document.documentElement.classList.contains('gt-sheet-open') }; });
                const logNow = await rect(page, '.gc-log');
                check(name + 'a sheet: the full width of the window at its bottom, 52 percent of its height (56 px more or less is the browser\'s own), the strip of views under it (hidden), the page keeps the room above it', pick.x === 0 && Math.abs(pick.right - w) <= 1 && Math.abs(pick.bottom - h) <= 1 && Math.abs(pick.h - Math.round(0.52 * h)) <= 2 && !bar.strip && bar.cls, JSON.stringify([pick, bar]));
                check(name + 'the sheet: the log gets shorter (not covered) and keeps room to be read (30 px at least), the dock ends at the sheet', !!logNow && logNow.h < logBefore.h && logNow.h >= 30 && dock.bottom <= pick.y + 2, JSON.stringify([logBefore, logNow, dock, pick]));
                check(name + 'the sheet in a narrow window has columns enough (6 at 320 px)', parts.cols >= 6, parts.cols);
            }
            check(name + 'the page does not scroll sideways with the picker open', await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth && document.body.scrollWidth <= window.innerWidth));
        }
        check(name + 'a press (mouse or finger) on the emoji button ' + (mode === 'sheet' ? 'in a window of a phone takes the keyboard away (the sheet is its place)' : 'keeps the focus in the field (the keyboard stays up)'), mode === 'sheet' ? (await active(page)) !== 'gc-input' : (await active(page)) === 'gc-input', await active(page));
        await shot(page, 'picker', size);
        await page.click('.gt-e >> nth=0');
        check(name + 'a pick goes in at the end of the field (the caret was there) and the picker stays', (await field(page)) === 'hi ' + GRIN && (await rect(page, '.gt-pick')) !== null);
        check(name + 'a press on an emoji leaves the focus where it was (it does not move to the emoji)', (await active(page)) !== 'gt-e');
        const kept = await page.evaluate(() => localStorage.getItem('ants.emoji.recent'));
        check(name + 'the pick is kept in this browser (the code point in hex)', kept === '1F600', kept);
        await page.hover('.gt-page .gt-e >> nth=2');
        const foot = await page.evaluate(() => ({ nm: document.querySelector('.gt-nm').textContent, kw: document.querySelector('.gt-kw').textContent, big: document.querySelector('.gt-big').textContent }));
        check(name + 'pointing at an emoji shows its name and its search words in the name line', foot.nm === 'Face with tears of joy' && foot.kw.indexOf('lol') !== -1 && foot.big.length > 0, JSON.stringify(foot));
        await page.click('.gt-tab >> nth=4');
        await sleep(80);
        const food = await page.evaluate(() => ({ cap: [...document.querySelectorAll('.gt-cap')].filter((c) => c.getClientRects().length).map((c) => c.textContent), sel: [...document.querySelectorAll('.gt-tab')].map((t) => t.getAttribute('aria-selected')).join(), n: document.querySelectorAll('.gt-page .gt-e').length, first: document.querySelector('.gt-page .gt-e').textContent, scroll: document.querySelector('.gt-body').scrollTop }));
        check(name + 'a tab shows its group (the food and drink: 86), the recently used row over it, the panel at its top (in the least room the lines over the groups are left out)', food.n === 86 && (food.cap.join() === '' || (food.cap.indexOf('Food and drink') !== -1 && food.cap.indexOf('Recently used') !== -1)) && food.sel.split(',')[4] === 'true' && food.scroll === 0, JSON.stringify(food));
        await shot(page, 'tab', size);
        await page.click('.gt-q');
        await page.keyboard.type('cat');
        await sleep(100);
        const found = await page.evaluate(() => ({ cap: document.querySelector('.gt-page .gt-cap').textContent, first: document.querySelector('.gt-page .gt-e').textContent, label: document.querySelector('.gt-page .gt-e').getAttribute('aria-label'), n: document.querySelectorAll('.gt-page .gt-e').length, recent: !!document.querySelector('.gt-recent').getClientRects().length, x: !!document.querySelector('.gt-x').getClientRects().length }));
        check(name + 'the search for "cat": a line with the count, the cat first, the recently used row away, the clear button there', /^\d+ results$/.test(found.cap) && found.first === String.fromCodePoint(0x1F408) && found.label === 'Cat' && found.n >= 10 && !found.recent && found.x, JSON.stringify(found));
        await shot(page, 'search', size);
        hub.received.length = 0;
        await page.keyboard.press('Enter');
        await sleep(100);
        check(name + 'Enter in the search field puts the cat in the line and sends nothing', (await field(page)) === 'hi ' + GRIN + String.fromCodePoint(0x1F408) && hub.received.filter((m) => m && m.t === 'say').length === 0, await field(page));
        await page.click('.gt-x');
        check(name + 'the clear button empties the search and the group is back', (await page.evaluate(() => document.querySelector('.gt-q').value)) === '' && (await page.evaluate(() => document.querySelectorAll('.gt-page .gt-e').length)) === 86);
        await page.focus('.gt-q');
        await page.keyboard.press('ArrowDown');
        check(name + 'the down arrow in the search field puts the focus on the first emoji (the recently used one)', (await active(page)) === 'gt-e' && (await page.evaluate(() => document.activeElement.closest('.gt-recent') !== null)));
        const walk = await page.evaluate(async () => {
            const at = () => { const a = document.activeElement; return [a.offsetLeft, a.offsetTop, a.textContent, !!a.closest('.gt-recent')]; };       // (places in the panel, which the scroll does not change)
            const key = async (k) => { document.activeElement.dispatchEvent(new KeyboardEvent('keydown', { key: k, bubbles: true, cancelable: true })); await new Promise((r) => setTimeout(r, 20)); return at(); };
            const out = { start: at() };
            out.down = await key('ArrowDown'); out.down2 = await key('ArrowDown'); out.up = await key('ArrowUp'); out.right = await key('ArrowRight'); out.left = await key('ArrowLeft');
            out.end = await key('End'); out.home = await key('Home'); out.page = await key('PageDown'); out.pageUp = await key('PageUp');
            out.cols = getComputedStyle(document.querySelector('.gt-page .gt-grid')).gridTemplateColumns.split(' ').length;
            return out;
        });
        check(name + 'the arrow keys: down goes from the recently used row to the group\'s first row (the same place), down again one row lower, up goes back; right and left one by one', walk.down[3] === false && walk.down[1] > walk.start[1] && Math.abs(walk.down[0] - walk.start[0]) <= 6 && walk.down2[1] > walk.down[1] && walk.up[1] === walk.down[1] && walk.right[0] > walk.up[0] && walk.left[0] === walk.up[0], JSON.stringify(walk));
        check(name + 'Home goes back to the first, End to the last, a page down is three rows, a page up goes back', walk.home[3] === true && walk.end[1] > walk.home[1] && walk.page[1] > walk.home[1] && walk.pageUp[1] <= walk.page[1] && walk.home[0] === walk.start[0], JSON.stringify(walk));
        await page.keyboard.press('Escape');
        await sleep(100);
        check(name + 'Escape closes the picker, the focus is in the field, and the chat is still open', (await rect(page, '.gt-pick')) === null && (await active(page)) === 'gc-input' && (await rect(page, '.gc-input')) !== null && (await page.getAttribute('#gchat', 'data-open')) !== 'false');
        if (mode === 'sheet') {
            const back = await page.evaluate(() => ({ cls: document.documentElement.classList.contains('gt-sheet-open'), pad: document.documentElement.style.getPropertyValue('--gt-sheet'), strip: !!(document.querySelector('.htabs') && document.querySelector('.htabs').getClientRects().length) }));
            const logBack = await rect(page, '.gc-log');
            check(name + 'the sheet closed: the page is as it was (the strip of views is back, the log is as tall as before)', !back.cls && back.pad === '' && back.strip && Math.abs(logBack.h - logBefore.h) <= 1, JSON.stringify([back, logBack, logBefore]));
        }
        await page.click('.gt-emoji');
        await sleep(100);
        const again = await page.evaluate(() => ({ recent: [...document.querySelectorAll('.gt-recent .gt-e')].map((b) => b.textContent).join(''), shown: !!document.querySelector('.gt-recent').getClientRects().length, tab: [...document.querySelectorAll('.gt-tab')].map((t) => t.getAttribute('aria-selected')).indexOf('true'), q: document.querySelector('.gt-q').value, row: new Set([...document.querySelectorAll('.gt-recent .gt-e')].map((b) => Math.round(b.getBoundingClientRect().top))).size }));
        check(name + 'opened again: the recently used row shows what was picked (the cat, the grin, newest first, one row), the search is empty, the last group is shown', again.shown && again.recent === String.fromCodePoint(0x1F408) + GRIN && again.row === 1 && again.q === '' && again.tab === 4, JSON.stringify(again));
        if (mode === 'sheet') {
            await page.focus('.gc-input');
            await sleep(100);
            check(name + 'the field takes the focus (the keyboard comes): the sheet gives way and the page has its room back', (await rect(page, '.gt-pick')) === null && !(await page.evaluate(() => document.documentElement.classList.contains('gt-sheet-open'))));
        } else await page.keyboard.press('Escape');
        await sleep(60);
        await page.reload({ waitUntil: 'load' });
        await page.waitForSelector('.gt-emoji', { state: 'visible', timeout: 6000 }).catch(() => {});
        if (phone || w <= 700 || h <= 500) { await page.click('.htab.t-chat').catch(() => {}); await sleep(250); }
        await page.waitForSelector('.gt-emoji', { state: 'visible', timeout: 6000 }).catch(() => {});
        await page.click('.gt-emoji');
        await sleep(150);
        check(name + 'after a reload of the page the recently used emoji are still there', (await page.evaluate(() => [...document.querySelectorAll('.gt-recent .gt-e')].map((b) => b.textContent).join(''))) === String.fromCodePoint(0x1F408) + GRIN);
        await page.keyboard.press('Escape');
        await page.fill('.gc-input', '');
        await page.evaluate(() => { try { localStorage.removeItem('ants.emoji.recent'); } catch (e) { /* none */ } });

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
        check(name + 'a refused microphone is a plain line, and the button is idle', err.shown && err.say === 'The microphone is blocked for this site. Allow it in your browser\'s site settings, then try again.' && err.pressed === 'false', JSON.stringify(err));
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
        check(name + 'Enter on the emoji button opens the picker and puts the focus in the search field', (await active(page)) === 'gt-q' && (await rect(page, '.gt-pick')) !== null, await active(page));
        await page.keyboard.press('ArrowDown');
        await page.keyboard.press('Enter');
        check(name + 'Enter on an emoji adds it (at the caret: after the "a")', (await field(page)).length === 3 && (await field(page)).startsWith('a'), JSON.stringify(await field(page)));
        await page.keyboard.press('Escape');

        if (h <= 500 && w > 700) check(name + 'a low window: the dock is the Chat view, above the strip', dockBox.bottom <= (await rect(page, '.htabs')).y + 1, JSON.stringify([dockBox, await rect(page, '.htabs')]));
        if (size === 'w900' || size === 'w1000') {
            await page.click('#gchat .gc-fold');
            await sleep(250);
            const folded = await rect(page, '#gchat');
            check(name + 'the fold works: the folded dock is as low as its header (80 px at most), not the height of the open one', !!folded && folded.h <= 80 && (await page.getAttribute('#gchat', 'data-open')) === 'false', JSON.stringify(folded));
            await shot(page, 'folded', size);
            await page.click('#gchat .gc-head');
            await sleep(250);
        }

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
        const off = await page.evaluate(() => { const f = document.querySelector('#gchat .gc-form'), i = document.querySelector('.gc-input').getBoundingClientRect(), b = f.getBoundingClientRect(); return { disabled: document.querySelector('.gc-input').disabled, tall: f.classList.contains('gt-tall'), form: Math.round(b.height), field: Math.round(i.height) }; });
        check('a chat that is read only (the field is off, Log in stands where Send does): one row', off.disabled && !off.tall && off.form <= 60, JSON.stringify(off));
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
