// OPT-IN, not part of ./run_tests.sh: the front page's HEIGHT in a REAL browser (Chromium through Playwright), on the whole site of tests/scripts/login_site.py (the real docker/nginx.conf and the real
// pages). docs/HOME_SCREEN.md, "The page's height". A headless browser has no system bars, so the fault of an installed phone window (the units say the window is taller than it shows) is IMITATED: the
// page is given a window of a height, and the three readings that the script looks at (window.innerHeight, the root's clientHeight, visualViewport.height) say that the window shows less. The pictures
// are cut at what the window shows, as a phone cuts them. The page's own CSS units cannot be told to lie, so the check of the rules in the style is: the box is never taller than any true reading.
//   1. a phone upright at 448 x 998, 412 x 915, 390 x 844 and 360 x 740, in a browser tab and in an installed window (the browser cannot be one: the style's display-mode query is made true on the way, and the script's matchMedia says standalone): the strip's bottom edge
//      is inside the window, Home is as tall as the window, and no limit is set (the page is what it was);
//   2. the fault (the window shows 60 px less than the units say): each reading alone, and all three, after the load and from the first look: the strip's bottom edge is inside what the window shows;
//   3. the keyboard: a field with the focus and a drop of 40 percent, or of 60 px: the page is not squeezed; the field lets go and the page fits what the window shows again;
//   4. a turn of the phone: the limit is taken away and the box is as tall as the window;
//   4b. a browser with no dvh and no svh (the units are made unknown words): Home is the window's height, a phone and a desktop, in a tab and installed;
//   4c. a tab whose address bar went away (the root's height stays 56 px short, the units and innerHeight do not): the page is the window's height and no limit is set, as on an older version;
//   5. "/?vp": every number is in the box, it asks for nothing but its own script and keeps nothing; a plain address has no box.
//   --shots DIR takes the pictures (DIR/NAME.png, cut at what the window shows).
//
//   node tests/scripts/web_home_height_browser.js [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox; ANTS_SERVER is not needed)
//
// Run against the page of an older version it prints FAIL for the fault (the strip is cut). Needs `playwright` (npm), a Chromium, nginx and python3; without them it says so and exits 0 (nothing was checked).
// Exit status: 0 every check passed (or none could be run), 1 a check failed.
'use strict';
const fs = require('fs');
const path = require('path');
const readline = require('readline');
const { spawn, spawnSync } = require('child_process');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the page\'s height was NOT run in a browser'); process.exit(0); }
if (spawnSync('nginx', ['-v']).error) { console.log('SKIP: no nginx program: the page\'s height was NOT run in a browser'); process.exit(0); }

const arg = (name) => (process.argv.indexOf(name) !== -1 ? process.argv[process.argv.indexOf(name) + 1] : null);
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const SIZES = [[448, 998], [412, 915], [390, 844], [360, 740]];
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

class Site {                                              // login_site.py: the whole site
    constructor() {
        this.proc = spawn('python3', [path.join(__dirname, 'login_site.py')], { stdio: ['pipe', 'pipe', 'inherit'] });
        this.lines = [];
        this.waiting = null;
        readline.createInterface({ input: this.proc.stdout }).on('line', (l) => { if (this.waiting) { const w = this.waiting; this.waiting = null; w(l); } else this.lines.push(l); });
        this.proc.on('exit', () => { if (this.waiting) this.waiting(null); });
    }
    next() { return this.lines.length ? Promise.resolve(this.lines.shift()) : new Promise((resolve) => { this.waiting = resolve; }); }
    async up() { const l = await this.next(); if (!l) throw new Error('the site did not start'); this.info = JSON.parse(l); return this.info; }
    stop() { try { this.proc.stdin.write('stop\n'); this.proc.stdin.end(); } catch (e) { /* gone */ } }
}

// The imitation: until window.__shown is a number the page sees the real window; then the three readings say that the window shows __shown px (window.__only: just one of them, 'inner', 'client' or 'vv').
const FAULT = () => {
    const only = () => window.__only || 'all';
    const lie = (kind, real) => (typeof window.__shown === 'number' && (only() === 'all' || only() === kind) ? window.__shown : real());
    const own = Object.getOwnPropertyDescriptor(window, 'innerHeight');
    const realInner = () => (own && own.get ? own.get.call(window) : own.value);
    Object.defineProperty(window, 'innerHeight', { configurable: true, get: () => lie('inner', realInner) });
    const proto = Object.getOwnPropertyDescriptor(Element.prototype, 'clientHeight').get;
    Object.defineProperty(Element.prototype, 'clientHeight', { configurable: true, get() { return this === document.documentElement ? lie('client', () => proto.call(this)) : proto.call(this); } });
    if (window.VisualViewport) {
        const vproto = Object.getOwnPropertyDescriptor(VisualViewport.prototype, 'height').get;
        Object.defineProperty(VisualViewport.prototype, 'height', { configurable: true, get() { return lie('vv', () => vproto.call(this)); } });
    }
};
const METRICS = () => {
    const box = (s) => { const e = document.querySelector(s); if (!e || !e.getClientRects().length) return null; const r = e.getBoundingClientRect(); return { top: r.top, bottom: r.bottom }; };
    return { strip: box('.htabs'), body: box('body'), limit: document.documentElement.style.getPropertyValue('--home-h'), inner: window.innerHeight, shown: typeof window.__shown === 'number' ? window.__shown : null,
        real: window.visualViewport ? Math.round(window.visualViewport.height) : null, mode: ['standalone', 'browser'].filter((m) => matchMedia('(display-mode: ' + m + ')').matches)[0], screen: document.body.getAttribute('data-screen'),
        wide: document.documentElement.scrollWidth > window.innerWidth };
};

async function main() {
    const site = new Site();
    let info;
    try { info = await site.up(); } catch (e) { console.log('SKIP: ' + e.message + ': the page\'s height was NOT run in a browser'); process.exit(0); }
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the page\'s height was NOT run in a browser'); site.stop(); process.exit(0); }
    const errors = [];
    // a phone window: touch, 2x pixels, the page's meta viewport honoured; `installed` makes (display-mode: standalone) true through the browser's own emulation
    async function open(w, h, installed, search, fault, oldBrowser) {
        const context = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: 2, isMobile: true, hasTouch: true });
        const page = await context.newPage();
        page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
        if (installed) {                                    // (CDP's setEmulatedMedia takes no display-mode feature in this browser: the style's display-mode query is made true on the way, and so is the script's)
            await page.addInitScript(() => { const mm = window.matchMedia.bind(window); window.matchMedia = (q) => (/display-mode: standalone/.test(q) ? { matches: true, media: q, addEventListener() {}, removeEventListener() {} } : mm(q)); });
        }
        if (installed || oldBrowser) {                      // an old browser: dvh and svh are words that it does not know, so @supports says no and a declaration that names one is dropped
            await page.route('**/home/home.css', async (route) => {
                const res = await route.fetch();
                let css = await res.text();
                if (installed) css = css.split('@media (display-mode: standalone), (display-mode: fullscreen), (display-mode: minimal-ui)').join('@media all');
                if (oldBrowser) css = css.replace(/(\d)(dvh|svh)/g, '$1$2x');
                await route.fulfill({ response: res, body: css });
            });
        }
        if (fault) await page.addInitScript(FAULT);
        const requests = [];
        page.on('request', (r) => requests.push(r.method() + ' ' + r.url().replace(info.site, '')));
        await page.goto(info.site + '/' + (search || ''), { waitUntil: 'load' });
        await page.waitForSelector('.htabs', { state: 'attached' });
        await sleep(900);
        return { page, context, requests };
    }
    const shot = async (page, name, shown) => {
        if (!SHOTS) return;
        fs.mkdirSync(SHOTS, { recursive: true });
        const v = page.viewportSize();
        await page.screenshot({ path: path.join(SHOTS, name + '.png'), clip: { x: 0, y: 0, width: v.width, height: Math.min(v.height, shown || v.height) } });
    };
    const inside = (m, limit) => !!m.strip && m.strip.bottom <= limit + 0.5 && m.body.bottom <= limit + 0.5;

    // ---- 1. a normal phone ----
    for (const installed of [false, true]) {
        for (const [w, h] of SIZES) {
            const { page, context } = await open(w, h, installed, '', true);
            const m = await page.evaluate(METRICS);
            const tag = w + 'x' + h + (installed ? ' installed' : ' tab') + ': ';
            check(tag + 'Home is on show and the display mode is the one asked for', m.screen === 'home' && m.mode === (installed ? 'standalone' : 'browser'), JSON.stringify(m));
            check(tag + 'the strip\'s bottom edge is inside the window', inside(m, m.inner), JSON.stringify(m));
            check(tag + 'Home is as tall as the window and no limit is set (the page is what it was)', Math.abs(m.body.bottom - m.inner) <= 1 && m.limit === '', JSON.stringify(m));
            check(tag + 'no sideways scroll', !m.wide);
            await shot(page, 'normal_' + w + (installed ? '_installed' : '_tab'));
            await context.close();
        }
    }

    // ---- 2. the fault: the units say 998, the window shows 938 ----
    for (const installed of [true, false]) {
        for (const only of ['all', 'inner', 'client', 'vv']) {
            if (!installed && only === 'client') continue;           // (a tab does not read the root's height: 4c)
            for (const late of [true, false]) {
                const w = 448, h = 998, shown = 938;
                const { page, context } = await open(w, h, installed, '', true);
                if (late) {
                    await page.evaluate((a) => { window.__only = a[0]; window.__shown = a[1]; window.dispatchEvent(new Event('resize')); }, [only, shown]);
                    await sleep(400);
                } else {                                    // from the first look: the page is loaded again with the readings already short
                    await page.evaluate((a) => { window.__only = a[0]; window.__shown = a[1]; }, [only, shown]);
                    await page.reload({ waitUntil: 'load' });
                    await page.waitForSelector('.htabs', { state: 'attached' });
                    await sleep(900);
                    await page.evaluate((a) => { window.__only = a[0]; window.__shown = a[1]; window.dispatchEvent(new Event('resize')); }, [only, shown]);
                    await sleep(400);
                }
                const m = await page.evaluate(METRICS);
                const tag = 'the fault (' + only + ' reads ' + shown + ', ' + (late ? 'after the load' : 'at the first look') + ', ' + (installed ? 'installed' : 'tab') + '): ';
                check(tag + 'the strip\'s bottom edge is inside what the window shows', inside(m, shown), JSON.stringify(m));
                check(tag + 'the limit is exactly what the window shows', m.limit === shown + 'px', JSON.stringify(m));
                if (installed && only === 'all' && late) await shot(page, 'fault_448_' + shown, shown);
                await context.close();
            }
        }
    }

    // ---- 3. the keyboard ----
    {
        const { page, context } = await open(448, 998, true, '', true);
        await page.evaluate(() => { const i = document.createElement('input'); i.type = 'text'; i.id = 'probe-field'; i.setAttribute('style', 'position:fixed;left:0;bottom:0;width:100px;z-index:1'); document.body.appendChild(i); });
        await page.focus('#probe-field');
        for (const [shown, label] of [[598, 'a drop of 40 percent (a keyboard)'], [938, 'a drop of 60 px']]) {
            await page.evaluate((n) => { window.__shown = n; window.dispatchEvent(new Event('resize')); if (window.visualViewport) window.visualViewport.dispatchEvent(new Event('resize')); }, shown);
            await sleep(400);
            const m = await page.evaluate(METRICS);
            check('a field has the focus and the window shows ' + label + ' less: the page is not squeezed and no limit is set', m.limit === '' && Math.abs(m.body.bottom - 998) <= 1, JSON.stringify(m));
            if (shown === 598) await shot(page, 'keyboard_448_field_focused', 998);
        }
        await page.evaluate(() => { window.__shown = 938; document.getElementById('probe-field').blur(); });
        await sleep(900);
        const m = await page.evaluate(METRICS);
        check('the field lets go and the window shows 938: the page fits again', m.limit === '938px' && inside(m, 938), JSON.stringify(m));
        await context.close();
    }
    {
        const { page, context } = await open(448, 998, true, '', true);
        await page.evaluate(() => { window.__shown = 598; window.dispatchEvent(new Event('resize')); });
        await sleep(400);
        const m = await page.evaluate(METRICS);
        check('a drop of 40 percent with no field in focus is not a short page: the page is not squeezed', m.limit === '' && Math.abs(m.body.bottom - 998) <= 1, JSON.stringify(m));
        await context.close();
    }

    // ---- 4. a turn of the phone ----
    {
        const { page, context } = await open(448, 998, true, '', true);
        await page.evaluate(() => { window.__shown = 938; window.dispatchEvent(new Event('resize')); });
        await sleep(400);
        const before = await page.evaluate(METRICS);
        check('turn: before it the limit is 938', before.limit === '938px', JSON.stringify(before));
        await page.evaluate(() => { window.__shown = undefined; });
        await page.setViewportSize({ width: 998, height: 448 });
        await sleep(900);
        const after = await page.evaluate(METRICS);
        check('turn: held sideways the window shows the units\' height, so the limit is gone and Home is as tall as the window', after.limit === '' && Math.abs(after.body.bottom - after.inner) <= 1 && after.inner === 448, JSON.stringify(after));
        await shot(page, 'turn_998x448');
        await context.close();
    }

    // ---- 4b. a browser that has no dvh (and no svh): Home is the window's height, as it was ----
    for (const [w, h, label] of [[448, 998, 'a phone'], [1280, 800, 'a desktop']]) {
        for (const installed of [false, true]) {
            const { page, context } = await open(w, h, installed, '', true, true);
            const m = await page.evaluate(METRICS);
            check('no dvh, ' + label + (installed ? ' (installed)' : '') + ': Home is as tall as the window (the 100vh line is what counts)', Math.abs(m.body.bottom - m.inner) <= 1 && m.body.top === 0, JSON.stringify(m));
            await shot(page, 'olddvh_' + w + (installed ? '_installed' : '_tab'));
            await context.close();
        }
    }

    // ---- 4c. a browser tab whose address bar went away: the root's height stays the small window (56 px less), the units and innerHeight are the big one; a tab behaves as it always did ----
    for (const [w, h] of SIZES) {
        const { page, context } = await open(w, h, false, '', true);
        await page.addStyleTag({ content: 'html { height: ' + (h - 56) + 'px !important; }' });
        await page.evaluate((n) => { window.__only = 'client'; window.__shown = n; window.dispatchEvent(new Event('resize')); }, h - 56);
        await sleep(400);
        const m = await page.evaluate(METRICS);
        check(w + 'x' + h + ' tab, address bar gone: Home is as tall as the window and no limit is set', Math.abs(m.body.bottom - h) <= 1 && m.limit === '', JSON.stringify(m));
        await shot(page, 'tabbar_' + w);
        await context.close();
    }

    // ---- 5. the readout ----
    {
        const plain = await open(448, 998, true, '', false);
        const plainRequests = plain.requests.map((r) => r.replace('/?vp', '/'));
        const plainBox = await plain.page.evaluate(() => !!document.getElementById('vp-readout'));
        const plainStore = await plain.page.evaluate(() => JSON.stringify([Object.keys(localStorage), Object.keys(sessionStorage)]));
        check('a plain address: no box and no request for the numbers\' script', !plainBox && !plain.requests.some((r) => /vp\.js/.test(r)), JSON.stringify(plain.requests.filter((r) => /vp/.test(r))));
        await plain.context.close();
        const { page, context, requests } = await open(448, 998, true, '?vp', false);
        const there = await page.waitForSelector('#vp-readout', { timeout: 5000 }).then(() => true, () => false);
        check('"/?vp" shows the box', there);
        await sleep(300);
        const text = await page.evaluate(() => { const b = document.getElementById('vp-readout'); return b ? b.textContent : ''; });
        for (const re of [/^display-mode standalone$/m, /^inner 448 x 998$/m, /^client height \d+$/m, /^visual viewport 448 x \d+(\.\d+)?, top 0, scale 1$/m, /^screen \d+ x \d+, avail \d+ x \d+$/m, /^pixel ratio 2$/m,
            /^vh 998, dvh 998, svh \d+, lvh \d+$/m, /^safe top \d+(\.\d+)?px, right \d+(\.\d+)?px, bottom \d+(\.\d+)?px, left \d+(\.\d+)?px$/m, /^home top 0, bottom 998, limit none$/m, /^Chrome \d+[\d.]*, Android [\d.-]+$/m]) {
            check('the readout says ' + re, re.test(text), text);
        }
        const store = await page.evaluate(() => JSON.stringify([Object.keys(localStorage), Object.keys(sessionStorage)]));
        const known = new Set(plainRequests);
        const more = requests.map((r) => r.replace('/?vp', '/')).filter((r) => !known.has(r));
        check('the readout asks for its own script and for nothing that a plain visit does not ask for', requests.filter((r) => /vp\.js/.test(r)).length === 1 && more.length === 1 && /^GET \/home\/vp\.js/.test(more[0]), more.join(' | '));
        check('the readout keeps nothing in the browser (same storage keys as a plain visit)', store === plainStore, store + ' vs ' + plainStore);
        await shot(page, 'readout_448_installed', 998);
        if (there) { await page.click('#vp-readout'); check('a tap puts the box away', await page.evaluate(() => document.getElementById('vp-readout').hidden === true)); }
        await context.close();
    }

    check('the page raised no script error', errors.length === 0, errors.join(' | '));
    await browser.close();
    site.stop();
    console.log('home height in a browser: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}
main().catch((e) => { console.log('ERROR ' + (e && e.stack || e)); try { site.stop(); } catch (x) { /* none */ } process.exit(1); });
