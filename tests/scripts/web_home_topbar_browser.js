// OPT-IN, not part of ./run_tests.sh: the top bar of Home in a REAL browser (Chromium through Playwright), on the whole site of tests/scripts/login_site.py (the REAL docker/nginx.conf, the real hub, a FAKE Discord:
// the chip's name is the hub's own answer, as the chat menu says it). docs/HOME_SCREEN.md, "The top bar". For every width (320, 360, 375, 390, 412, 600, 700, 701, 1180, 1280 and held sideways 568 x 320, 667 x 375)
// and for a name of one letter, a typical name, the longest name the hub allows, an accented one and a CJK one, it checks:
//   1. a phone upright (up to 700 px wide): the login chip is the round badge AND the name, the name is cut with an ellipsis only where it does not fit (a short one shows whole from 390 px), and the first row stays ONE row (the bar is two rows: Games, chip and Menu, then Quick host, Quick join and My room):
//      Games from the left edge, then the chip, then the Menu button, no overlap, nothing past the window's edge, the page does not scroll sideways, every target 44 px or more;
//   2. wide windows and a phone held sideways keep what they were: the badge alone below 1280 px, the badge and the name at 1280;
//   2b. a window under 640 px wide, wider than high and taller than 500 px (620 x 560, 600 x 520, 639 x 600, 350 x 300): Games from the left edge, the chip and the Menu at the right edge on the first row, Quick host and Quick join under them, My room on a third row of its own;
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
// the bar's buttons with no room, and the same bar when My room is shown (the page holds a room: Quick host, Private room, then Home; the count is put in by hand, see SHOW_COUNT)
const ROW = () => {
    const box = (s) => { const e = document.querySelector(s); if (!e || !e.getClientRects().length) return null; const r = e.getBoundingClientRect(); return [Math.round(r.left), Math.round(r.top), Math.round(r.width), Math.round(r.height)]; };
    return { host: box('.hbar .quick.host'), join: box('.hbar .quick.join'), games: box('.hbar .games'), mine: box('.hbar .mine'), menu: box('.hbar .hmenu'), bar: box('.hbar'), chip: box('.hbar .hlogin'),
        mineDisplay: getComputedStyle(document.querySelector('.hbar .mine')).display, mineLabel: document.querySelector('.hbar .mine').getAttribute('aria-label') };
};
const SHOW_COUNT = () => { const m = document.querySelector('.hbar .mine'); const p = m.querySelector('.pill'); p.hidden = false; p.textContent = '1/4'; m.setAttribute('aria-label', 'My room, 1 of 4'); };       // (the count comes from the game server's room, which this run does not have: only the count is put in by hand)
const BOXES = () => {
    const box = (s) => { const e = document.querySelector(s); if (!e || e.hidden || !e.getClientRects().length) return null; const r = e.getBoundingClientRect(); return { x: r.left, y: r.top, r: r.right, b: r.bottom, w: r.width, h: r.height }; };
    const cn = document.querySelector('.hbar .hlogin .gc-cn');
    return { games: box('.hbar .games'), pictures: document.querySelectorAll('.hbar img, .rbar img').length, heading: (document.querySelector('.hbar h1') || { textContent: null }).textContent, headingInTree: (() => { const h = document.querySelector('.hbar h1'); return !!h && h.checkVisibility({ visibilityProperty: true }) && h.getBoundingClientRect().width <= 3; })(), chip: box('.hbar .hlogin'), menu: box('.hbar .hmenu'), badge: box('.hbar .hlogin .gc-av'), name: box('.hbar .hlogin .gc-cn'), bar: box('.hbar'),
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
    async function logIn(context, name, both) {
        if (both) await site.cmd('user ' + OLD + ' ' + name.toLowerCase().replace(/[^a-z0-9_.]/g, 'x') + ' ' + name.charAt(0).toUpperCase() + name.slice(1)); else await site.cmd('user ' + OLD + ' ' + (name === name.toLowerCase() && /^[a-z0-9_.]+$/.test(name) ? name : 'plain_user ' + name));      // (a name that Discord would not give out as a username is the display name)
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
            if (phoneRow) check(s[0] + ' guest: one row (Games, chip, Menu), the menu at the right edge', !!b.menu && b.games.x + b.games.w <= b.chip.x + 0.5 && b.chip.r <= b.menu.x + 0.5 && b.menu.r <= s[1] && Math.abs(b.menu.y - b.chip.y) < 4 && Math.abs(b.games.y - b.chip.y) < 4);
            check(s[0] + ' guest: the bar draws no logo (no picture in it), and the page\'s heading is the name, in the page for a screen reader and not drawn', b.pictures === 0 && b.heading === 'Ants!' && b.headingInTree, JSON.stringify([b.pictures, b.heading, b.headingInTree]));
            check(s[0] + ' guest: no sideways scroll', !b.wide);
            const none = await page.evaluate(ROW);
            check(s[0] + ' no room: My room is not drawn (display none: no place in the row, not in the tab order)', none.mine === null && none.mineDisplay === 'none', JSON.stringify(none));
            check(s[0] + ' no room: Quick host, Quick join and Games are drawn, each a target of 44 px or more, inside the window', [none.host, none.join, none.games].every((x) => x && x[3] >= 44 && x[2] >= 44 && x[0] >= 0 && x[0] + x[2] <= s[1]), JSON.stringify(none));
            if (phoneRow) check(s[0] + ' no room: Games is on the first row at the left edge, before the chip, and Quick host and Quick join fill the second row', none.games[1] < none.host[1] && none.games[0] <= 20 && (!none.chip || none.games[0] + none.games[2] <= none.chip[0] + 1) && Math.abs(none.host[1] - none.join[1]) <= 2 && none.host[0] + none.host[2] <= none.join[0] + 1 && none.join[0] + none.join[2] >= s[1] - 20 && none.bar[3] <= 112, JSON.stringify(none));
            check(s[0] + ' no room: Games does not run under the login chip or the Menu', !none.chip || none.games[0] + none.games[2] <= none.chip[0] + 1 || none.games[1] + none.games[3] <= none.chip[1] + 1 || none.chip[1] + none.chip[3] <= none.games[1] + 1, JSON.stringify(none));
            if (s[0] === 'side568') check(s[0] + ' no room: held sideways Games takes the place of My room (the second row starts at the left edge, no hole)', none.games[0] <= none.host[0] && none.games[1] > none.host[1], JSON.stringify(none));
            if (['w390', 'w1280', 'side667'].indexOf(s[0]) !== -1) await shot(page, 'noroom_' + s[0]);
            // Quick host asks Public or Private first (a card over Home)
            await page.waitForFunction(() => { const b = document.querySelector('.hbar .quick.host'); return b && !b.disabled; }, null, { timeout: 10000 });
            await page.focus('.hbar .quick.host');
            await page.keyboard.press('Enter');
            await sleep(150);
            const card = await page.evaluate(() => { const c = document.querySelector('.gconfirm'); if (!c) return null; const bs = [...c.querySelectorAll('button')].map((b) => { const r = b.getBoundingClientRect(); return { t: b.textContent, x: r.left, r: r.right, y: r.top, b: r.bottom, h: r.height, w: r.width }; }), r = c.getBoundingClientRect(); return { title: c.querySelector('h2').textContent, bs, box: { x: r.left, r: r.right, y: r.top, b: r.bottom }, focus: document.activeElement.textContent, role: c.getAttribute('role'), inert: document.querySelector('.hbar').hasAttribute('inert') }; });
            check(s[0] + ' Quick host: the keyboard opens the card "Host a game" with Public room, Private room and Cancel, the focus on Public room', !!card && card.title === 'Host a game' && card.bs.map((x) => x.t).join('|') === 'Public room|Private room|Cancel' && card.focus === 'Public room' && card.role === 'dialog', JSON.stringify(card));
            check(s[0] + ' Quick host: the card and its three buttons are inside the window, every button 44 px or more high and wide, the page behind is inert', !!card && card.inert && card.box.x >= 0 && card.box.r <= s[1] + 0.5 && card.box.y >= 0 && card.box.b <= s[2] + 0.5 && card.bs.every((x) => x.h >= 44 && x.w >= 44 && x.x >= 0 && x.r <= s[1] + 0.5 && x.y >= 0 && x.b <= s[2] + 0.5), JSON.stringify(card));
            if (['w390', 'w1280'].indexOf(s[0]) !== -1) await shot(page, 'hostcard_' + s[0]);
            await page.keyboard.press('Tab');
            check(s[0] + ' Quick host: Tab goes to Private room', (await page.evaluate(() => document.activeElement.textContent)) === 'Private room');
            await page.keyboard.press('Escape');
            await sleep(100);
            check(s[0] + ' Quick host: Escape cancels (no card, nothing made, the focus back on Quick host)', await page.evaluate(() => !document.querySelector('.gconfirm') && document.body.getAttribute('data-screen') === 'home' && /quick host|host/.test(document.activeElement.className)));
            // a real room: Quick host, Private room (the page makes and holds a room and shows it), then Home
            await page.focus('.hbar .quick.host'); await page.keyboard.press('Enter'); await sleep(150);
            await page.locator('.gconfirm button', { hasText: 'Private room' }).click(); await sleep(500);
            check(s[0] + ' a room: the page shows the Room, and My room is there when Home comes back', await page.evaluate(() => document.body.getAttribute('data-screen')) === 'room');
            await page.locator('.rhome').click(); await sleep(300);
            await page.evaluate(SHOW_COUNT);
            const room = await page.evaluate(ROW);
            const up = (r) => { for (const k of ['host', 'join', 'games', 'mine', 'menu', 'chip']) if (r[k]) r[k] = [r[k][0], r[k][1] - r.bar[1], r[k][2], r[k][3]]; return r; };       // (a room that cannot reach a game server shows its notice above the bar: the bar's parts are compared from the bar's own top)
            up(room);
            const same = (a, b) => a.every((r, i) => !!r && !!b[i] && r.every((v, k) => Math.abs(v - b[i][k]) <= 1));       // (to a pixel: a bar that is lower by a notice rounds each part on its own)
            check(s[0] + ' a room: My room is drawn (44 px or more)' + (phoneRow ? ', on the second row with Quick host and Quick join, and the first row does not move' : ' and Quick host, Quick join and the Menu do not move'),
                !!room.mine && room.mine[3] >= 44 && (phoneRow ? Math.abs(room.mine[1] - room.host[1]) <= 2 && room.mine[0] + room.mine[2] <= room.host[0] + 1 && room.host[0] + room.host[2] <= room.join[0] + 1 && room.join[0] + room.join[2] >= s[1] - 20 && same([room.games, room.chip, room.menu], [none.games, none.chip, none.menu])
                    : same([room.host, room.join, room.menu], [none.host, none.join, none.menu])), JSON.stringify([none, room]));
            check(s[0] + ' a room: My room and Games are inside the window and do not overlap', !!room.games && room.mine[0] >= 0 && room.games[0] + room.games[2] <= s[1] && (room.mine[0] + room.mine[2] <= room.games[0] + 1 || room.mine[1] + room.mine[3] <= room.games[1] + 1 || room.games[1] + room.games[3] <= room.mine[1] + 1 || room.games[0] + room.games[2] <= room.mine[0] + 1), JSON.stringify(room));
            if (['w390', 'w1280', 'side667'].indexOf(s[0]) !== -1) await shot(page, 'room_' + s[0]);
            if (['w390', 'w1280', 'side667'].indexOf(s[0]) !== -1) await shot(page, 'guest_' + s[0]);
            await page.close();
        }
        await context.close();
    }

    // ---- a window under 640 px wide that is wider than high and taller than 500 px (and one under 360 px wide that is wider than high): the bar's first row is Games at the left and the chip and the Menu at the right ----
    {
        const context = await browser.newContext({ deviceScaleFactor: 1 });
        for (const [name, w, h] of [['w620x560', 620, 560], ['w600x520', 600, 520], ['w639x600', 639, 600], ['w350x300', 350, 300]]) {
            const page = await openAt(context, w, h);
            await page.goto(info.site + '/', { waitUntil: 'load' });
            await live(page, 'no');
            const m = await page.evaluate(() => { const q = (e) => { const x = document.querySelector(e); if (!x || !x.getClientRects().length) return null; const r = x.getBoundingClientRect(); return [r.left, r.top, r.right, r.bottom]; }; return { games: q('.hbar .games'), chip: q('.hbar .hlogin'), menu: q('.hbar .hmenu'), host: q('.hbar .quick.host'), join: q('.hbar .quick.join'), bar: q('.hbar'), pictures: document.querySelectorAll('.hbar img').length, wide: document.documentElement.scrollWidth > innerWidth }; });
            const L = name + ': ';
            check(L + 'the first row is Games from the left edge, then the chip, then the Menu at the right edge, all on one row', !!m.games && !!m.chip && !!m.menu && m.games[0] <= 20 && m.games[2] <= m.chip[0] + 0.5 && m.chip[2] <= m.menu[0] + 0.5 && m.menu[2] >= w - 20 && m.menu[2] <= w && Math.abs(m.games[1] - m.menu[1]) < 4 && Math.abs(m.chip[1] - m.menu[1]) < 4, JSON.stringify(m));
            check(L + 'Quick host and Quick join are under it, side by side, inside the window; no picture in the bar; no sideways scroll', !!m.host && !!m.join && m.host[1] >= m.games[3] - 0.5 && Math.abs(m.host[1] - m.join[1]) <= 2 && m.host[2] <= m.join[0] + 1 && m.join[2] <= w && m.pictures === 0 && !m.wide, JSON.stringify(m));
            check(L + 'a press on the middle of Games reaches Games (the group of the chip and the Menu does not lie over it)', await page.evaluate(() => { const g = document.querySelector('.hbar .games').getBoundingClientRect(), e = document.elementFromPoint(g.left + g.width / 2, g.top + g.height / 2); return !!e && !!e.closest('.games'); }));
            check(L + 'every target is 44 px or more', [m.games, m.chip, m.menu, m.host, m.join].every((x) => x[3] - x[1] >= 43.5 && x[2] - x[0] >= 43.5), JSON.stringify(m));
            await page.evaluate(() => { const e = document.querySelector('.hbar .mine'); e.hidden = false; e.querySelector('.pill').hidden = false; e.querySelector('.pill').textContent = '1/4'; });
            const r = await page.evaluate(() => { const q = (e) => { const x = document.querySelector(e); if (!x || !x.getClientRects().length) return null; const r = x.getBoundingClientRect(); return [r.left, r.top, r.right, r.bottom]; }; return { games: q('.hbar .games'), host: q('.hbar .quick.host'), mine: q('.hbar .mine'), menu: q('.hbar .hmenu') }; });
            check(L + 'with My room (a room is held): it has the third row to itself, under Quick host and Quick join, and the first row does not move', !!r.mine && r.mine[1] >= r.host[3] - 0.5 && r.mine[0] <= 20 && r.mine[2] >= w - 20 && JSON.stringify(r.games) === JSON.stringify(m.games) && JSON.stringify(r.menu) === JSON.stringify(m.menu), JSON.stringify([m, r]));
            if (name === 'w620x560') await shot(page, 'threerow_' + name);
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
                if (s[1] >= 360) check(L + 'the name is shown beside the badge', named && b.nameText === n[1], JSON.stringify([b.nameText, b.name]));
                else check(L + 'under 360 px the row has no room for letters: the badge alone (no sliver of a name)', !named && b.chip.w <= 50, JSON.stringify(b));
                check(L + 'the first row is one row: Games from the left edge, chip, Menu, in that order, no overlap, the Menu at the right edge of the window, and no logo anywhere in the bar',
                    b.pictures === 0 && b.games.x <= 20 && b.games.x + b.games.w <= b.chip.x + 0.5 && b.chip.r <= b.menu.x + 0.5 && b.menu.r <= s[1] + 0.5 && Math.abs(b.menu.r - (s[1] - 14)) < 2 && Math.abs(b.menu.y - b.chip.y) < 4 && Math.abs(b.menu.y - b.games.y) < 4, JSON.stringify(b));
                if ((n[0] === 'one' || n[0] === 'typical') && s[1] >= 360) check(L + 'a short name shows whole (no ellipsis) from 360 px: the row has the room that the logo had', !b.cut, JSON.stringify(b.name));
                if (n[0] === 'medium' && s[1] >= 360) check(L + 'a name of ten letters shows whole from 360 px', !b.cut, JSON.stringify(b.name));
                if (n[0] === 'longest' && s[1] <= 412 && s[1] >= 360) check(L + 'a long name is cut with an ellipsis and the chip stays between Games and the Menu', b.cut && b.chip.x >= b.games.x + b.games.w - 0.5, JSON.stringify(b));
                if (n[0] === 'longest' && s[1] >= 390) check(L + 'the chip grows into the free space: at 390 px and up, more than the badge', b.chip.w > 100);
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

    // ---- the chip's dropdown (the name, the choice of the name, Log out, Forget me) on the real hub: where it stands, what is in it, the keyboard, a press elsewhere, the view of a phone, Log out and Forget me ----
    const DROP = () => {
        const q = (x) => document.querySelector(x), r = (e) => { const b = e.getBoundingClientRect(); return { x: b.left, y: b.top, r: b.right, b: b.bottom, w: b.width, h: b.height }; };
        const slot = q('#gchat-login-slot'), chip = q('.hbar .hlogin'), out = slot.querySelector('.gc-out'), forget = slot.querySelector('.gc-forget');
        return { shown: slot.getClientRects().length > 0, box: r(slot), chip: r(chip), expanded: chip.getAttribute('aria-expanded'), haspopup: chip.getAttribute('aria-haspopup'), controls: chip.getAttribute('aria-controls'), text: slot.innerText,
            overflowY: getComputedStyle(slot).overflowY, scrolls: slot.scrollHeight > slot.clientHeight + 1, focus: document.activeElement && document.activeElement.className, tab: document.getElementById('home').getAttribute('data-tab'),
            chatShown: !!document.getElementById('gchat') && document.getElementById('gchat').getClientRects().length > 0, outBox: r(out), forgetBox: r(forget), win: [innerWidth, innerHeight], inPanel: document.getElementById('gchat').contains(slot), afterChip: slot.previousElementSibling === chip,
            panelText: document.getElementById('gchat').innerText, formH: r(q('#gchat .gc-form')).h };
    };
    for (const n of NAMES.filter((x) => (x[0] === 'typical' || x[0] === 'longest') && (!arg('--only') || x[0] === arg('--only')))) {
        const context = await browser.newContext({ deviceScaleFactor: 1 });
        await logIn(context, n[1], true);
        for (const s of SIZES.filter((x) => ['w320', 'w390', 'w412', 'w700', 'w1280', 'side568', 'side667'].indexOf(x[0]) !== -1)) {
            const page = await openAt(context, s[1], s[2]);
            await page.goto(info.site + '/', { waitUntil: 'load' });
            await live(page, 'yes');
            const L = label(n, s) + 'dropdown: ', disp = n[1].charAt(0).toUpperCase() + n[1].slice(1);
            let d = await page.evaluate(DROP);
            check(L + 'shut at first, and the chip says it has one (aria-haspopup, aria-expanded false, aria-controls)', !d.shown && d.expanded === 'false' && d.haspopup === 'true' && d.controls === 'gchat-login-slot', JSON.stringify([d.shown, d.expanded, d.haspopup, d.controls]));
            check(L + 'the chat panel has no account line (no Chatting as, no Log out in it), and the dropdown stands right after the chip in the bar (Tab order, inert)', d.afterChip && !d.inPanel && d.panelText.indexOf('Chatting as') === -1 && d.panelText.indexOf('Log out') === -1);
            await page.focus('.hbar .hlogin');
            await page.keyboard.press('Enter');
            await sleep(120);
            d = await page.evaluate(DROP);
            check(L + 'the keyboard (Enter on the chip) opens it, the chip says so, and the focus is on Log out', d.shown && d.expanded === 'true' && /gc-out/.test(d.focus || ''), JSON.stringify([d.shown, d.expanded, d.focus]));
            check(L + 'it says "Chatting as <name>", the choice of the name, Log out and Forget me', d.text.indexOf('Chatting as ' + disp) === 0 || d.text.replace(/\s+/g, ' ').indexOf('Chatting as ' + disp) !== -1, d.text);
            check(L + 'the choice of the name is in it (the account has a display name too)', /Chat shows your/.test(d.text) && /Show my/.test(d.text) && /Log out/.test(d.text) && /Forget me/.test(d.text) && /Deletes your lines/.test(d.text), d.text);
            check(L + 'it is under the chip, at the right of the window, inside the window, and scrolls inside itself where it is taller than the room it has (Log out and Forget me can be reached)', d.box.y >= d.chip.b && d.box.x >= 0 && d.box.r <= s[1] + 0.5 && d.box.b <= s[2] + 0.5 && d.overflowY === 'auto' && Math.abs(d.box.r - (s[1] - 10)) < 12, JSON.stringify(d.box));
            if (d.scrolls) {
                await page.evaluate(() => document.querySelector('#gchat-login-slot .gc-forget').scrollIntoView({ block: 'nearest' }));
                const e = await page.evaluate(DROP);
                check(L + 'taller than the window: scrolled, Forget me comes into view, inside the window', e.forgetBox.b <= s[2] + 1 && e.forgetBox.y >= 0, JSON.stringify(e.forgetBox));
                await page.evaluate(() => { document.querySelector('#gchat-login-slot').scrollTop = 0; });
            }
            check(L + 'Log out and Forget me are targets of 44 px or more', d.outBox.h >= 44 && d.outBox.w >= 44 && d.forgetBox.h >= 44, JSON.stringify([d.outBox, d.forgetBox]));
            if (s[1] <= 720 && s[2] > s[1]) check(L + 'on a phone the view stays what it was (the chip does not show the Chat view)', d.tab === 'games' && !d.chatShown, d.tab);
            if (n[0] === 'typical' && ['w390', 'w1280', 'side667'].indexOf(s[0]) !== -1) await shot(page, 'dropdown_open_' + s[0]);
            await page.keyboard.press('Tab'); await sleep(40);
            check(L + 'Tab from Log out moves on inside the dropdown (Forget me), then Shift+Tab comes back', await page.evaluate(() => /gc-forget/.test(document.activeElement.className)) && (await page.keyboard.press('Shift+Tab'), await page.evaluate(() => /gc-out/.test(document.activeElement.className))));
            await page.keyboard.press('Escape');
            await sleep(80);
            d = await page.evaluate(DROP);
            check(L + 'Escape closes it, the focus goes back to the chip, aria-expanded is false', !d.shown && d.expanded === 'false' && /hlogin/.test(d.focus || ''), JSON.stringify([d.shown, d.expanded, d.focus]));
            await page.click('.hbar .hlogin'); await sleep(80);
            check(L + 'a press on the chip opens it, and a second one closes it', (await page.evaluate(DROP)).shown && (await page.click('.hbar .hlogin'), await sleep(80), !(await page.evaluate(DROP)).shown));
            await page.click('.hbar .hlogin'); await sleep(80);
            await page.mouse.click(4, s[2] - 4); await sleep(80);
            check(L + 'a press elsewhere puts it away', !(await page.evaluate(DROP)).shown);
            if (n[0] === 'typical' && ['w390', 'w1280', 'side667'].indexOf(s[0]) !== -1) {
                await shot(page, 'closed_' + s[0]);
                if (s[1] <= 700) { await page.click('.htab.t-chat'); await sleep(250); await shot(page, 'chatbottom_' + s[0]); await page.click('.htab.t-players'); await sleep(250); await shot(page, 'players_' + s[0]); }
                else { await shot(page, 'chatbottom_' + s[0]); await shot(page, 'players_' + s[0]); }
            }
            await page.close();
        }
        // Log out and Forget me of the dropdown on the real hub
        {
            const page = await openAt(context, 390, 844);
            await page.goto(info.site + '/', { waitUntil: 'load' });
            await live(page, 'yes');
            await page.click('.hbar .hlogin'); await sleep(80);
            await page.click('#gchat-login-slot .gc-pick');
            await page.waitForFunction(() => document.querySelector('#gchat-login-slot') && /Chat shows your username/.test(document.querySelector('#gchat-login-slot').innerText) && document.getElementById('gchat').getAttribute('data-state') === 'live' && document.querySelector('#gchat-login-slot').getClientRects().length > 0, null, { timeout: 12000 });
            await sleep(150);
            check(n[0] + ': a name pick on the real hub (the socket closes, the chip goes for a moment, the dropdown comes back): the focus is on the pick button again, not on the page', await page.evaluate(() => /gc-pick/.test(document.activeElement.className)), await page.evaluate(() => document.activeElement.className + '|' + document.activeElement.tagName));
            await page.click('#gchat-login-slot .gc-out');
            await live(page, 'no');
            const d = await page.evaluate(() => ({ chip: document.querySelector('.hbar .hlogin').getAttribute('aria-label'), slot: document.getElementById('gchat-login-slot').getClientRects().length, expanded: document.querySelector('.hbar .hlogin').getAttribute('aria-expanded') }));
            check(n[0] + ': Log out in the dropdown logs out on the real hub: the chip says Log in again and the dropdown is gone', d.chip === 'Log in with Discord' && d.slot === 0 && d.expanded === null, JSON.stringify(d));
            await page.close();
            await logIn(context, n[1], true);
            const p2 = await openAt(context, 390, 844);
            await p2.goto(info.site + '/', { waitUntil: 'load' });
            await live(p2, 'yes');
            await p2.click('.hbar .hlogin'); await sleep(80);
            await p2.click('#gchat-login-slot .gc-forget');
            check(n[0] + ': the first press on Forget me only asks (Are you sure? Press again), the account is still named', (await p2.evaluate(() => document.querySelector('#gchat-login-slot .gc-forget').textContent)) === 'Are you sure? Press again' && (await p2.evaluate(() => document.getElementById('gchat').getAttribute('data-you'))) === 'yes');
            await sleep(450); await p2.click('#gchat-login-slot .gc-forget');
            await live(p2, 'no');
            check(n[0] + ': Forget me in the dropdown works on the real hub (a reader afterwards)', await p2.evaluate(() => document.querySelector('.hbar .hlogin').getAttribute('aria-label') === 'Log in with Discord'));
            await p2.close();
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
