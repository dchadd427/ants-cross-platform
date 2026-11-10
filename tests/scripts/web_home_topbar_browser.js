// OPT-IN, not part of ./run_tests.sh: the top bar of Home in a REAL browser (Chromium through Playwright), on the whole site of tests/scripts/login_site.py (the REAL docker/nginx.conf, the real hub, a FAKE Discord:
// the chip's name is the hub's own answer, as the chat menu says it). docs/HOME_SCREEN.md, "The top bar". For every width (320, 360, 375, 390, 412, 600, 700, 701, 1180, 1280 and held sideways 568 x 320, 667 x 375)
// and for a name of one letter, a typical name, the longest name the hub allows, an accented one and a CJK one, it checks:
//   1. a phone upright (up to 700 px wide): the login chip is the round badge AND the name, the name is cut with an ellipsis only where it does not fit (a short one shows whole at 320 px), and the first row stays ONE row:
//      the logo (92 px, unchanged), then the chip, then the Menu button, no overlap, nothing past the window's edge, the page does not scroll sideways, every target 44 px or more;
//   2. wide windows and a phone held sideways keep what they were: the badge alone below 1280 px, the badge and the name at 1280;
//   3. the chip's accessible name holds the name ("Chatting as <name>"); a visitor who is not logged in sees the mark of Discord alone on a phone (the text "Log in" is not drawn) and "Log in" on a desktop;
//   --shots DIR takes the pictures (topbar_*.png).
//
//   node tests/scripts/web_home_topbar_browser.js [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox; ANTS_SERVER is not needed)
//
// Needs `playwright` (npm), a Chromium that it can start, nginx and python3 on the path; without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be made), 1 a check failed.
'use strict';
const fs = require('fs');
const path = require('path');
const readline = require('readline');
const { spawn, spawnSync } = require('child_process');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the top bar was NOT run in a browser'); process.exit(0); }
if (spawnSync('nginx', ['-v']).error) { console.log('SKIP: no nginx program: the top bar was NOT run in a browser'); process.exit(0); }

const arg = (name) => (process.argv.indexOf(name) !== -1 ? process.argv[process.argv.indexOf(name) + 1] : null);
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const SIZES = [['w320', 320, 640], ['w360', 360, 740], ['w375', 375, 667], ['w390', 390, 844], ['w412', 412, 915], ['w600', 600, 800], ['w700', 700, 800], ['w701', 701, 800], ['w1180', 1180, 800], ['w1280', 1280, 800], ['side568', 568, 320], ['side667', 667, 375]];
const NAMES = [['one', 'ab'], ['typical', 'maple'], ['medium', 'maple.leaf'], ['longest', 'LongestNameOfThirtyTwoCharacters'], ['accent', 'Zo\u00eb_M\u00fcller'], ['cjk', '\u5c71\u7530\u592a\u90ce']];
const OLD = '261842000123456789';
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

class Site {                                              // login_site.py: the whole site, and a line of commands for the fake Discord
    constructor() {
        this.proc = spawn('python3', [path.join(__dirname, 'login_site.py'), '--consent'], { stdio: ['pipe', 'pipe', 'inherit'] });
        this.lines = [];
        this.waiting = null;
        readline.createInterface({ input: this.proc.stdout }).on('line', (l) => { if (this.waiting) { const w = this.waiting; this.waiting = null; w(l); } else this.lines.push(l); });
        this.proc.on('exit', () => { if (this.waiting) this.waiting(null); });
    }
    next() { return this.lines.length ? Promise.resolve(this.lines.shift()) : new Promise((resolve) => { this.waiting = resolve; }); }
    async up() { const l = await this.next(); if (!l) throw new Error('the site did not start'); this.info = JSON.parse(l); return this.info; }
    async cmd(line) { this.proc.stdin.write(line + '\n'); return JSON.parse(await this.next()); }
    stop() { try { this.proc.stdin.write('stop\n'); this.proc.stdin.end(); } catch (e) { /* gone */ } }
}

// the boxes of the bar's parts, in CSS pixels (null: not drawn)
const BOXES = () => {
    const box = (s) => { const e = document.querySelector(s); if (!e || e.hidden || !e.getClientRects().length) return null; const r = e.getBoundingClientRect(); return { x: r.left, y: r.top, r: r.right, b: r.bottom, w: r.width, h: r.height }; };
    const cn = document.querySelector('.hbar .hlogin .gc-cn');
    return { logo: box('.hbar .hlogo'), chip: box('.hbar .hlogin'), menu: box('.hbar .hmenu'), badge: box('.hbar .hlogin .gc-av'), name: box('.hbar .hlogin .gc-cn'), bar: box('.hbar'),
        cut: !!cn && cn.scrollWidth > cn.clientWidth + 1, nameText: cn ? cn.textContent : null, label: (document.querySelector('.hbar .hlogin') || { getAttribute: () => null }).getAttribute('aria-label'),
        wide: document.documentElement.scrollWidth > window.innerWidth || document.body.scrollWidth > window.innerWidth, win: window.innerWidth };
};

async function main() {
    const site = new Site();
    let info;
    try { info = await site.up(); } catch (e) { console.log('SKIP: ' + e.message + ': the top bar was NOT run in a browser'); process.exit(0); }
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the top bar was NOT run in a browser'); site.stop(); process.exit(0); }
    const errors = [];
    const shot = async (page, name) => { if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, 'topbar_' + name + '.png') }); } };
    const live = (page, you) => page.waitForFunction((w) => { const d = document.getElementById('gchat'); return !!d && d.getAttribute('data-you') === w && d.getAttribute('data-state') === 'live'; }, you, { timeout: 10000 });
    async function openAt(context, w, h) {
        const page = await context.newPage();
        page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
        await page.setViewportSize({ width: w, height: h });
        return page;
    }
    async function logIn(context, name) {
        await site.cmd('user ' + OLD + ' ' + (name === name.toLowerCase() && /^[a-z0-9_.]+$/.test(name) ? name : 'plain_user ' + name));      // (a name that Discord would not give out as a username is the display name)
        const page = await openAt(context, 1280, 800);
        await page.goto(info.site + '/', { waitUntil: 'load' });
        await live(page, 'no');
        await page.click('.hlogin');
        const end = Date.now() + 20000;
        while (Date.now() < end) {
            if (page.url().indexOf(info.discord) === 0 && await page.$('#go').catch(() => null)) await page.click('#go').catch(() => {});
            if (page.url().indexOf(info.site) === 0 && await page.evaluate(() => { const d = document.getElementById('gchat'); return !!d && d.getAttribute('data-you') === 'yes' && d.getAttribute('data-state') === 'live'; }).catch(() => false)) break;
            await sleep(100);
        }
        await page.close();
    }
    const label = (n, s) => s[0] + ' ' + n[0] + ': ';

    // ---- not logged in: the mark alone on a phone, "Log in" on a desktop ----
    {
        const context = await browser.newContext({ deviceScaleFactor: 1 });
        for (const s of SIZES) {
            const page = await openAt(context, s[1], s[2]);
            await page.goto(info.site + '/', { waitUntil: 'load' });
            await live(page, 'no');
            const b = await page.evaluate(BOXES);
            const phoneRow = (s[1] <= 720 && s[2] > s[1]);
            check(s[0] + ' guest: the chip is there, a target of 44 px, and ' + (s[1] >= 1280 ? 'says Log in' : 'is the mark alone'), !!b.chip && b.chip.h >= 44 && b.chip.w >= 44 && (s[1] >= 1280 ? b.chip.w > 80 : b.chip.w <= 50), JSON.stringify(b.chip));
            if (phoneRow) check(s[0] + ' guest: one row (logo, chip, Menu), the menu at the right edge', !!b.logo && !!b.menu && b.logo.r <= b.chip.x + 0.5 && b.chip.r <= b.menu.x + 0.5 && b.menu.r <= s[1] && Math.abs(b.chip.y - b.logo.y) < 50 && Math.abs(b.menu.y - b.chip.y) < 4);
            check(s[0] + ' guest: no sideways scroll', !b.wide);
            if (['w390', 'w1280', 'side667'].indexOf(s[0]) !== -1) await shot(page, 'guest_' + s[0]);
            await page.close();
        }
        await context.close();
    }

    // ---- logged in, five names, every size ----
    for (const n of NAMES.filter((x) => !arg('--only') || x[0] === arg('--only'))) {
        const context = await browser.newContext({ deviceScaleFactor: 1 });
        await logIn(context, n[1]);
        for (const s of SIZES) {
            const page = await openAt(context, s[1], s[2]);
            await page.goto(info.site + '/', { waitUntil: 'load' });
            await live(page, 'yes');
            await sleep(50);
            const b = await page.evaluate(BOXES);
            const L = label(n, s);
            const phoneRow = (s[1] <= 720 && s[2] > s[1]);
            const named = b.name !== null;
            check(L + 'the chip is drawn, a target of 44 px or more, nothing past the window, no sideways scroll', !!b.chip && b.chip.h >= 44 && b.chip.w >= 44 && b.chip.r <= s[1] + 0.5 && !b.wide, JSON.stringify(b.chip));
            check(L + 'the accessible name holds the name', b.label === 'Chatting as ' + n[1], b.label);
            check(L + 'the badge is the first letter', !!b.badge && b.badge.w >= 26);
            if (phoneRow) {
                check(L + 'the name is shown beside the badge', named && b.nameText === n[1], JSON.stringify([b.nameText, b.name]));
                check(L + 'the logo keeps its size (92 px) and the row is one row: logo, chip, Menu, in that order, no overlap, the Menu at the right edge of the window',
                    !!b.logo && Math.abs(b.logo.w - 92) < 1 && b.logo.r <= b.chip.x + 0.5 && b.chip.r <= b.menu.x + 0.5 && b.menu.r <= s[1] + 0.5 && Math.abs(b.menu.r - (s[1] - 16)) < 2 && Math.abs(b.menu.y - b.chip.y) < 4 && Math.abs(b.menu.y - b.logo.y) < 50, JSON.stringify(b));
                if (n[0] === 'one' || n[0] === 'typical' || n[0] === 'cjk' || (n[0] === 'medium' && s[1] >= 360)) check(L + 'a short name shows whole (no ellipsis)', !b.cut, JSON.stringify(b.name));
                if (n[0] === 'longest' && s[1] <= 412) check(L + 'a long name is cut with an ellipsis and the chip stays between the logo and the Menu', b.cut && b.chip.x >= b.logo.r, JSON.stringify(b));
                if (n[0] === 'longest') check(L + 'the chip grows into the free space: at 390 px and up, more than the badge', b.chip.w > 100);
            } else if (s[1] >= 1280) {
                check(L + 'a desktop shows the badge and the name, as before', named && b.nameText === n[1]);
            } else {
                check(L + 'a wide window below 1280 and a phone held sideways keep the badge alone', !named && b.chip.w <= 50, JSON.stringify(b.chip));
            }
            if (['w320', 'w390', 'w412', 'side568', 'side667', 'w1280'].indexOf(s[0]) !== -1 && (n[0] === 'one' || n[0] === 'longest' || n[0] === 'typical')) await shot(page, n[0] + '_' + s[0]);
            await page.close();
        }
        await context.close();
    }

    check('nothing in the pages threw (pageerror)', errors.length === 0, errors.join(' | '));
    await browser.close();
    site.stop();
    console.log('home top bar in a browser: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}
main().catch((e) => { console.log('FAIL the run broke: ' + (e && e.stack || e)); process.exit(1); });
